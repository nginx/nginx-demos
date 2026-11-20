#!/bin/sh
# shellcheck disable=SC2086 # package lists are split into words on purpose
#
# get.nginx.org/nginx: install NGINX Open Source from the official nginx.org
# package repositories.
#
#   curl -fsSL https://get.nginx.org/nginx | sh
#   curl -fsSL https://get.nginx.org/nginx | sh -s -- --stable --module njs
#
# Piping a download into a shell runs whatever the server sends, unread. To
# review the script first:
#
#   curl -fsSL https://get.nginx.org/nginx -o install-nginx.sh
#   less install-nginx.sh
#   sh install-nginx.sh --dry-run
#   sh install-nginx.sh
#
# What it does, following https://nginx.org/en/linux_packages.html:
#   1. Detects the OS and architecture.
#   2. Prints the repository, signing keys, packages, and files it will
#      install, then pauses so you can cancel.
#   3. Installs the prerequisites (curl, gpg or openssl, CA certificates).
#   4. Downloads the nginx signing key and stops unless it matches the
#      fingerprints pinned below.
#   5. Adds the nginx.org repository for the chosen branch.
#   6. Installs nginx and any requested dynamic modules.
#
# All the work happens in main(), called inside a { } block on the last line,
# so a download cut short anywhere runs nothing.

set -eu

SCRIPT_URL="https://get.nginx.org/nginx"
NGINX_ORG="https://nginx.org"
PACKAGES_DOC="$NGINX_ORG/en/linux_packages.html"
PAUSE=10

# Distributions and versions nginx.org builds packages for, from
# https://nginx.org/en/linux_packages.html#distributions. All of them are
# x86_64 and aarch64, except SLES 15, which is x86_64 only. nginx.org lists
# SLES 15 SP6, but its packages need OpenSSL 3.2 and SP6 ships 3.1, so this
# script needs SP7 or later.
SUPPORTED_RHEL="8 9 10"
SUPPORTED_DEBIAN="bookworm trixie"
SUPPORTED_UBUNTU="jammy noble resolute"
SUPPORTED_SLES="15 16"
SUPPORTED_ALPINE="3.21 3.22 3.23 3.24"
SUPPORTED_AMZN="2023"

# Dynamic modules shipped as nginx-module-NAME packages, from
# https://nginx.org/en/linux_packages.html#dynmodules
MODULES="acme geoip image-filter njs otel perl xslt"

# nginx_signing.key holds the 2011 signing key and the two 2024 keys it
# certifies. Every key in the downloaded file must be one of these, or the
# script stops before changing the repository configuration.
PGP_KEY_URL="$NGINX_ORG/keys/nginx_signing.key"
PGP_FINGERPRINTS="
573BFD6B3D8FBC641079A6ABABF5BD827BD9BF62
8540A6F18833A80E9C1653A42FD21310B49F6B46
9E9BE90EACBCDE69FE9B204CBCDCD8A38D88A2B3
"

# Alpine uses a separate RSA key, the one with the modulus nginx.org asks you
# to check by hand at https://nginx.org/en/linux_packages.html#Alpine. This is
# the SHA-256 of its DER encoding, which covers the modulus and the exponent.
RSA_KEY_URL="$NGINX_ORG/keys/nginx_signing.rsa.pub"
RSA_KEY_SHA256="03833138bf6288dcb545f7a154af24f5c63b94931fef6b078cd8061204a44327"

# --- Output ------------------------------------------------------------------

setup_colors() {
    bold="" red="" yellow="" green="" reset=""
    if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != dumb ]; then
        esc=$(printf '\033')
        bold="${esc}[1m" red="${esc}[31m" yellow="${esc}[33m" green="${esc}[32m" reset="${esc}[0m"
    fi
}

say()   { printf '%s\n' "$*"; }
step()  { printf '\n%s==> %s%s\n' "$bold" "$*" "$reset"; }
field() { printf '  %-15s %s\n' "$1" "$2"; }
warn()  { printf '%sWarning:%s %s\n' "$yellow" "$reset" "$*" >&2; }
die()   { printf '%sError:%s %s\n' "$red" "$reset" "$*" >&2; exit 1; }

# 573BFD6B3D8F... -> 573B FD6B 3D8F ...
spaced() { printf '%s\n' "$1" | sed 's/..../& /g; s/ $//'; }

supported_systems() {
    printf '  %-21s %s\n' \
        "RHEL and derivatives" "$SUPPORTED_RHEL" \
        "Debian" "$SUPPORTED_DEBIAN" \
        "Ubuntu" "$SUPPORTED_UBUNTU" \
        "SLES, openSUSE Leap" "$SUPPORTED_SLES (15 needs SP7 or later, x86_64 only)" \
        "Alpine" "$SUPPORTED_ALPINE" \
        "Amazon Linux" "$SUPPORTED_AMZN"
}

usage() {
    cat <<EOF
Install NGINX Open Source from the nginx.org package repositories.

Usage:
  curl -fsSL $SCRIPT_URL | sh -s -- [options]
  sh install-nginx.sh [options]

Options:
  --mainline      Install the mainline branch (default)
  --stable        Install the stable branch
  --module NAME   Also install the nginx-module-NAME package. Repeat for more
                  modules. Available: $MODULES
  -n, --dry-run   Show the plan and the commands without changing anything
  -y, --yes       Don't pause for $PAUSE seconds before making changes
  -h, --help      Show this help

Supported systems (x86_64 and aarch64):
$(supported_systems)

Manual installation steps: $PACKAGES_DOC
EOF
}

# --- Helpers -----------------------------------------------------------------

has() { command -v "$1" >/dev/null 2>&1; }

# contains WORD LIST: is WORD one of the whitespace-separated words in LIST?
contains() {
    for _item in $2; do
        if [ "$_item" = "$1" ]; then return 0; fi
    done
    return 1
}

# Run a command as root, or only print it in a dry run. Package managers get
# /dev/null as stdin so they can't swallow the rest of a piped script.
run() {
    if [ "$dry_run" = 1 ]; then
        say "  + ${sudo:+$sudo }$*"
    else
        $sudo "$@" </dev/null
    fi
}

# write_file DEST: install stdin as DEST, mode 644, owned by root.
write_file() {
    _src="$tmp_dir/$(basename "$1")"
    cat >"$_src"
    if [ "$dry_run" = 1 ]; then
        say "  + write $1:"
        sed 's/^/      /' "$_src"
    else
        run install -m 644 "$_src" "$1"
    fi
}

fetch() {
    curl -fsSL --proto '=https' --tlsv1.2 --retry 3 -o "$2" "$1" ||
        die "couldn't download $1"
}

# Where every nginx.org package installs the binary.
NGINX_BIN=/usr/sbin/nginx

# Any nginx already on the system, packaged or not, for the pre-install warning.
find_nginx() {
    if [ -x "$NGINX_BIN" ]; then
        echo "$NGINX_BIN"
    elif has nginx; then
        command -v nginx
    fi
}

# The architecture packages are installed for. On a 64-bit kernel, uname -m
# says x86_64 even inside a 32-bit userspace that nginx.org doesn't build for.
native_arch() {
    case $os in
        debian|ubuntu) has dpkg && dpkg --print-architecture ;;
        alpine) has apk && apk --print-arch ;;
        *) has rpm && rpm --eval '%{_arch}' ;;
    esac || uname -m
}

# --- Steps -------------------------------------------------------------------

parse_args() {
    while [ $# -gt 0 ]; do
        case $1 in
            --mainline) channel=mainline ;;
            --stable) channel=stable ;;
            --module)
                [ $# -ge 2 ] || die "--module needs a module name. Available: $MODULES"
                add_module "$2"
                shift
                ;;
            --module=*) add_module "${1#*=}" ;;
            -n|--dry-run) dry_run=1 ;;
            -y|--yes) assume_yes=1 ;;
            -h|--help) usage; exit 0 ;;
            *) die "unknown option: $1 (see --help)" ;;
        esac
        shift
    done
}

add_module() {
    contains "$1" "$MODULES" || die "unknown module: $1. Available: $MODULES"
    if ! contains "$1" "$modules"; then
        modules="${modules:+$modules }$1"
    fi
}

unsupported() {
    die "$os_name on $arch isn't supported by the nginx.org packages.
Supported systems:
$(supported_systems)
See $PACKAGES_DOC"
}

detect_os() {
    [ -r /etc/os-release ] || die "can't detect the OS: /etc/os-release is missing."
    # shellcheck disable=SC1091
    . /etc/os-release
    os_name=${PRETTY_NAME:-${ID:-unknown}}
    version_id=${VERSION_ID:-}
    arch=$(uname -m)

    # Amazon Linux lists rhel in ID_LIKE, so it has to match first.
    case " ${ID:-} ${ID_LIKE:-} " in
        *" ubuntu "*)
            os=ubuntu version=${UBUNTU_CODENAME:-${VERSION_CODENAME:-}} supported=$SUPPORTED_UBUNTU ;;
        *" debian "*)
            os=debian version=${VERSION_CODENAME:-} supported=$SUPPORTED_DEBIAN ;;
        *" amzn "*)
            os=amzn version=$version_id supported=$SUPPORTED_AMZN ;;
        *" rhel "*|*" centos "*|*" ol "*)
            os=rhel version=${version_id%%.*} supported=$SUPPORTED_RHEL ;;
        " sles "*|" sles_sap "*|" opensuse-leap "*)
            os=sles version=${version_id%%.*} supported=$SUPPORTED_SLES ;;
        *" alpine "*)
            os=alpine version=$(echo "$version_id" | cut -d. -f1,2) supported=$SUPPORTED_ALPINE ;;
        *)
            unsupported ;;
    esac

    arch=$(native_arch)
    case $arch in
        x86_64|amd64) arch=x86_64 ;;
        aarch64|arm64) arch=aarch64 ;;
        *) unsupported ;;
    esac

    contains "$version" "$supported" || unsupported
    if [ "$os" = sles ] && [ "$version" = 15 ]; then
        [ "$arch" = x86_64 ] || unsupported
        case $version_id in
            15.[7-9]|15.[1-9][0-9]) ;;
            *) unsupported ;;
        esac
    fi
}

check_privileges() {
    if [ "$(id -u)" = 0 ]; then
        sudo="" run_as="root"
    elif has sudo; then
        sudo=sudo run_as="root, using sudo"
    elif has doas; then
        sudo=doas run_as="root, using doas"
    elif [ "$dry_run" = 1 ]; then
        sudo=sudo run_as="root"
    else
        die "this script needs root. Run it as root, or install sudo."
    fi
}

# Work out the repository, packages, prerequisites, and files for this OS.
plan() {
    pkgs_url=$NGINX_ORG/packages
    if [ "$channel" = mainline ]; then
        pkgs_url=$pkgs_url/mainline
    fi

    packages=nginx
    for _m in $modules; do
        packages="$packages nginx-module-$_m"
    done

    key_url=$PGP_KEY_URL
    key_file=$tmp_dir/nginx_signing.key

    case $os in
        debian|ubuntu)
            pkg=apt gpg_pkg=gpg ca_bundle=/etc/ssl/certs/ca-certificates.crt
            keyring=/usr/share/keyrings/nginx-archive-keyring.gpg
            repo_url="$pkgs_url/$os $version nginx"
            changes="$keyring (signing keys)
/etc/apt/sources.list.d/nginx.list (repository)
/etc/apt/preferences.d/99nginx (prefers nginx.org packages)"
            ;;
        rhel|amzn)
            # Minimal images, such as UBI minimal, only have microdnf.
            pkg=dnf gpg_pkg=gnupg2 ca_bundle=/etc/pki/tls/certs/ca-bundle.crt
            if ! has dnf && has microdnf; then
                pkg=microdnf
            fi
            rpm_key=/etc/pki/rpm-gpg/RPM-GPG-KEY-nginx
            if [ "$os" = amzn ]; then
                rpm_path=amzn/2023
            else
                rpm_path=centos/$version
            fi
            repo_url="$pkgs_url/$rpm_path/$arch/"
            changes="$rpm_key (signing keys, also imported into rpm)
/etc/yum.repos.d/nginx.repo (repository)"
            ;;
        sles)
            pkg=zypper gpg_pkg=gpg2 ca_bundle=/etc/ssl/ca-bundle.pem
            repo_url="$pkgs_url/sles/$version"
            changes="rpm database (signing keys)
/etc/zypp/repos.d/nginx-$channel.repo (repository, added with zypper)"
            ;;
        alpine)
            pkg=apk ca_bundle=/etc/ssl/certs/ca-certificates.crt
            key_url=$RSA_KEY_URL
            key_file=$tmp_dir/nginx_signing.rsa.pub
            repo_url="$pkgs_url/alpine/v$version/main"
            packages=$(for _p in $packages; do printf '%s@nginx ' "$_p"; done)
            packages=${packages% }
            changes="/etc/apk/keys/nginx_signing.rsa.pub (signing key)
/etc/apk/repositories (adds the @nginx repository)"
            ;;
    esac

    prereqs=""
    if ! has curl; then
        prereqs="curl"
    fi
    if [ "$os" = alpine ]; then
        if ! has openssl; then prereqs="${prereqs:+$prereqs }openssl"; fi
    elif ! has gpg; then
        prereqs="${prereqs:+$prereqs }$gpg_pkg"
    fi
    if [ ! -e "$ca_bundle" ]; then
        prereqs="${prereqs:+$prereqs }ca-certificates"
    fi

    nginx_bin=$(find_nginx)
}

show_plan() {
    printf '%sNGINX Open Source installer%s (%s)\n\n' "$bold" "$reset" "$SCRIPT_URL"
    field "System" "$os_name ($arch)"
    field "Branch" "$channel"
    field "Packages" "$packages"
    field "Repository" "$repo_url"
    field "Signing key" "$key_url"
    if [ "$os" = alpine ]; then
        field "" "used only if its SHA-256 matches the pinned one"
    else
        field "" "used only if every key in it is one of:"
        for _fpr in $PGP_FINGERPRINTS; do
            field "" "  $(spaced "$_fpr")"
        done
    fi
    field "Prerequisites" "${prereqs:-none needed}"
    printf '%s\n' "$changes" | {
        _label="Changes"
        while IFS= read -r _line; do
            field "$_label" "$_line"
            _label=""
        done
    }
    field "Runs as" "$run_as"

    if [ -z "$modules" ]; then
        printf '\nDynamic modules ship as separate packages. Add any of\n%s\nwith --module NAME.\n' \
            "$MODULES"
    fi
    if [ "$nginx_bin" = "$NGINX_BIN" ]; then
        echo
        warn "$("$nginx_bin" -v 2>&1 | sed 's/^nginx version: //') is already installed at $nginx_bin.
The nginx.org packages will replace or upgrade it. Back up /etc/nginx first."
    elif [ -n "$nginx_bin" ]; then
        echo
        warn "$("$nginx_bin" -v 2>&1 | sed 's/^nginx version: //') is already installed at $nginx_bin.
The nginx.org packages install $NGINX_BIN and leave it in place, but it
comes first in your PATH. Run $NGINX_BIN to use the packaged nginx."
    fi
}

pause() {
    if [ "$dry_run" = 1 ]; then
        printf '\nDry run: these are the commands the script would run. Nothing changes.\n'
        return
    fi
    if [ "$assume_yes" = 1 ]; then
        return
    fi

    cat <<EOF

${yellow}Warning:${reset} this script changes your system as root. It adds the
nginx.org signing keys and repository, then installs packages. If you piped
it from curl into a shell without reading it, press Ctrl+C now and review
it first:

    curl -fsSL $SCRIPT_URL -o install-nginx.sh
    less install-nginx.sh
    sh install-nginx.sh

EOF
    if [ -t 1 ]; then
        _left=$PAUSE
        while [ "$_left" -gt 0 ]; do
            printf '\rStarting in %2ss. Press Ctrl+C to cancel. ' "$_left"
            sleep 1
            _left=$((_left - 1))
        done
        printf '\r%-45s\n' "Starting."
    else
        say "Starting in ${PAUSE}s. Press Ctrl+C to cancel."
        sleep "$PAUSE"
    fi
}

apt_get() {
    run env DEBIAN_FRONTEND=noninteractive apt-get -q -y "$@"
}

install_prerequisites() {
    step "Installing prerequisites"
    if [ -z "$prereqs" ]; then
        say "Already installed."
        return
    fi
    case $pkg in
        apt)
            apt_get update
            apt_get install --no-install-recommends $prereqs
            ;;
        dnf|microdnf) run "$pkg" install -y $prereqs ;;
        zypper) run zypper --non-interactive install $prereqs ;;
        apk) run apk add --no-cache $prereqs ;;
    esac
}

verify_pgp_key() {
    fetch "$PGP_KEY_URL" "$key_file"
    mkdir -m 700 "$tmp_dir/gnupg"
    # gpg can print some keys and still fail on the rest of the file, so check
    # its exit status before trusting what it printed.
    _keys=$(gpg --homedir "$tmp_dir/gnupg" --batch --with-colons --show-keys "$key_file" 2>/dev/null) ||
        die "gpg couldn't read $PGP_KEY_URL.
Nothing was installed from nginx.org."
    # The fingerprint is field 10 of the fpr line after each pub line. sed, not
    # awk, because SUSE's minimal images don't have awk.
    _found=$(printf '%s\n' "$_keys" | sed -n '/^pub:/{n;s/^fpr:\{9\}\([0-9A-F]*\):.*/\1/p;}')
    [ -n "$_found" ] || die "$PGP_KEY_URL doesn't contain any PGP keys."

    for _fpr in $_found; do
        contains "$_fpr" "$PGP_FINGERPRINTS" || die "$PGP_KEY_URL contains a key this script doesn't know:
    $(spaced "$_fpr")
Nothing was installed from nginx.org. Check $PACKAGES_DOC, and get a newer
copy of this script if nginx has added a signing key."
        say "  ${green}ok${reset}  $(spaced "$_fpr")"
    done
}

verify_rsa_key() {
    fetch "$RSA_KEY_URL" "$tmp_dir/download.pub"
    # Re-encode, so only the key itself ends up in /etc/apk/keys.
    openssl pkey -pubin -in "$tmp_dir/download.pub" -out "$key_file" 2>/dev/null ||
        die "$RSA_KEY_URL isn't a public key."
    _sha256=$(openssl pkey -pubin -in "$key_file" -outform DER 2>/dev/null |
        openssl dgst -sha256 2>/dev/null | sed 's/^.*= *//') || true
    [ "$_sha256" = "$RSA_KEY_SHA256" ] || die "$RSA_KEY_URL doesn't match the pinned nginx signing key.
Nothing was installed from nginx.org. Check $PACKAGES_DOC#Alpine"
    say "  ${green}ok${reset}  SHA-256 $RSA_KEY_SHA256"
}

verify_signing_key() {
    step "Verifying the nginx signing key"
    if [ "$dry_run" = 1 ] && [ -n "$prereqs" ]; then
        say "Skipped: the dry run can't install the prerequisites it needs."
        return
    fi
    if [ "$os" = alpine ]; then
        verify_rsa_key
    else
        verify_pgp_key
    fi
}

# yum_repo BRANCH: one section of /etc/yum.repos.d/nginx.repo
yum_repo() {
    _url=$NGINX_ORG/packages
    _enabled=0
    if [ "$1" = mainline ]; then
        _url=$_url/mainline
    fi
    if [ "$1" = "$channel" ]; then
        _enabled=1
    fi
    # shellcheck disable=SC2016 # $basearch is a yum variable
    printf '[nginx-%s]\nname=nginx %s repo\nbaseurl=%s/%s/$basearch/\n' "$1" "$1" "$_url" "$rpm_path"
    printf 'gpgcheck=1\nenabled=%s\ngpgkey=file://%s\nmodule_hotfixes=true\n' "$_enabled" "$rpm_key"
    if [ "$os" = amzn ]; then
        echo "priority=9"
    fi
}

add_repository() {
    step "Adding the nginx.org $channel repository"
    case $pkg in
        apt)
            if [ -s "$key_file" ]; then
                gpg --homedir "$tmp_dir/gnupg" --batch --yes --dearmor \
                    -o "$tmp_dir/nginx-archive-keyring.gpg" "$key_file"
            fi
            run install -m 644 "$tmp_dir/nginx-archive-keyring.gpg" "$keyring"
            echo "deb [signed-by=$keyring] $repo_url" |
                write_file /etc/apt/sources.list.d/nginx.list
            printf 'Package: *\nPin: origin nginx.org\nPin: release o=nginx\nPin-Priority: 900\n' |
                write_file /etc/apt/preferences.d/99nginx
            ;;
        dnf|microdnf)
            run install -m 644 "$key_file" "$rpm_key"
            run rpm --import "$rpm_key"
            { yum_repo stable; echo; yum_repo mainline; } |
                write_file /etc/yum.repos.d/nginx.repo
            ;;
        zypper)
            run rpm --import "$key_file"
            for _repo in nginx-stable nginx-mainline; do
                if zypper --non-interactive repos "$_repo" >/dev/null 2>&1; then
                    run zypper --non-interactive removerepo "$_repo"
                fi
            done
            run zypper --non-interactive addrepo --gpgcheck --type yum --refresh --check \
                "$pkgs_url/sles/\$releasever_major" "nginx-$channel"
            ;;
        apk)
            run install -m 644 "$key_file" /etc/apk/keys/nginx_signing.rsa.pub
            { grep -v '^@nginx https://nginx.org/packages/' /etc/apk/repositories || true
              echo "@nginx $repo_url"; } |
                write_file /etc/apk/repositories
            ;;
    esac
}

install_packages() {
    step "Installing $packages"
    case $pkg in
        apt)
            # apt-get update exits 0 when an index fails to download. If the
            # nginx.org one did, the install would quietly use the distro's
            # nginx, so stop instead.
            apt_get -o APT::Update::Error-Mode=any update ||
                die "apt couldn't download every package index. Check the errors above.
nginx.org is set up as a repository, but nothing was installed from it."
            # Keep any existing configuration files rather than prompting.
            apt_get -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold \
                install $packages
            ;;
        dnf|microdnf) run "$pkg" install -y $packages ;;
        zypper) run zypper --non-interactive install --from "nginx-$channel" $packages ;;
        # Without --upgrade, apk add keeps an installed version that still
        # satisfies the request, unlike the other package managers' install.
        apk) run apk add --no-cache --upgrade $packages ;;
    esac
}

# The nginx packages installed now, for downgrade_command.
installed_packages() {
    case $pkg in
        # The second status letter is i for installed. The first is the
        # selection, which is h rather than i for a held package.
        apt) dpkg-query -W -f '${db:Status-Abbrev} ${Package}\n' 'nginx*' 2>/dev/null |
                 sed -n 's/^.i. *//p' ;;
        dnf|microdnf|zypper) rpm -qa --qf '%{NAME}\n' 'nginx*' ;;
        apk) apk info 2>/dev/null ;;
    esac | grep -E '^nginx(-module-.+)?$' | tr '\n' ' ' | sed 's/ $//'
}

# The commands that move the installed nginx packages to the stable branch.
# Package managers don't downgrade on their own when the repository changes.
downgrade_command() {
    _pkgs=$(installed_packages)
    _sudo=${sudo:+$sudo }
    case $pkg in
        apt)
            # A held package (first status letter h) has to be unheld first.
            _held=$(dpkg-query -W -f '${db:Status-Abbrev} ${Package}\n' 'nginx*' 2>/dev/null |
                sed -n 's/^hi. *//p' | grep -E '^nginx(-module-.+)?$' | tr '\n' ' ' | sed 's/ $//')
            if [ -n "$_held" ]; then
                printf '%sapt-mark unhold %s\n    ' "$_sudo" "$_held"
            fi
            printf '%sapt-get install --allow-downgrades' "$_sudo"
            for _p in $_pkgs; do printf ' %s/%s' "$_p" "$version"; done
            ;;
        dnf|microdnf) printf '%s%s distro-sync %s' "$_sudo" "$pkg" "$_pkgs" ;;
        zypper) printf '%szypper install --oldpackage --from nginx-stable %s' "$_sudo" "$_pkgs" ;;
        apk) printf '%sapk upgrade --available %s' "$_sudo" "$_pkgs" ;;
    esac
}

# Did the installed nginx package come from nginx.org? On RPM systems, check
# the key the package was signed with. apt and apk have already checked the
# signature against the pinned key, so check the packager, which also tells
# nginx.org packages apart from distro ones.
from_nginx_org() {
    case $pkg in
        apt)
            [ "$(dpkg-query -W -f '${Maintainer}' nginx 2>/dev/null)" = \
                "NGINX Packaging <nginx-packaging@f5.com>" ]
            ;;
        dnf|microdnf|zypper)
            # A key ID is the last 16 hex digits of the key's fingerprint.
            _keyid=$(LC_ALL=C rpm -q --qf '%{SIGPGP:pgpsig}\n' nginx 2>/dev/null |
                sed -n 's/.*Key ID \([0-9a-fA-F]\{16\}\).*/\1/p' | tr 'a-f' 'A-F')
            for _fpr in $PGP_FINGERPRINTS; do
                if [ -n "$_keyid" ] && [ "${_fpr#????????????????????????}" = "$_keyid" ]; then
                    return 0
                fi
            done
            return 1
            ;;
        apk)
            sed -n '/^P:nginx$/,/^$/p' /lib/apk/db/installed 2>/dev/null |
                grep -q '^m:NGINX Packaging <nginx-packaging@f5\.com>$'
            ;;
    esac
}

finish() {
    if [ "$dry_run" = 1 ]; then
        step "Dry run finished. Nothing was changed."
        return
    fi

    # Check the binary the package installed, not whatever comes first in PATH.
    [ -x "$NGINX_BIN" ] || die "the packages installed, but $NGINX_BIN wasn't found."
    _installed=$("$NGINX_BIN" -v 2>&1) || die "the packages installed, but $NGINX_BIN doesn't run:
$_installed"
    _installed=${_installed#nginx version: }
    from_nginx_org || die "$_installed is installed, but not from nginx.org.
The package manager chose another repository's nginx package. Check the
output above for download errors, and that nginx.org has packages for
$os_name on $arch."

    # Stable releases have an even minor version, mainline an odd one. An odd
    # one after a stable install means a newer mainline build was kept.
    _minor=$(echo "$_installed" | sed -n 's|^nginx/[0-9]*\.\([0-9]*\)\..*|\1|p')
    if [ "$channel" = stable ] && [ -n "$_minor" ] && [ $((_minor % 2)) = 1 ]; then
        die "$_installed from the mainline branch is still installed.
The stable repository is set up, but the package manager won't downgrade
nginx on its own. Check that your configuration doesn't use features newer
than stable, then run:

    $(downgrade_command)

If a hold or lock keeps nginx on its version, remove it first. Or stay on
mainline by running this script again without --stable."
    fi

    step "Installed $_installed"

    if [ -d /run/systemd/system ]; then
        _start="${sudo:+$sudo }systemctl enable --now nginx"
    elif has rc-service; then
        _start="${sudo:+$sudo }rc-update add nginx && ${sudo:+$sudo }rc-service nginx start"
    else
        _start="${sudo:+$sudo }$NGINX_BIN"
    fi
    field "Start nginx" "$_start"
    field "Configuration" "/etc/nginx/nginx.conf and /etc/nginx/conf.d/"
    field "Documentation" "https://nginx.org/en/docs/"
}

main() {
    channel=mainline modules="" dry_run=0 assume_yes=0
    setup_colors
    parse_args "$@"
    detect_os
    check_privileges

    tmp_dir=$(mktemp -d)
    trap 'rm -rf "$tmp_dir"' EXIT
    trap 'exit 130' INT TERM

    plan
    show_plan
    pause
    install_prerequisites
    verify_signing_key
    add_repository
    install_packages
    finish
}

{ main "$@"; exit; }
