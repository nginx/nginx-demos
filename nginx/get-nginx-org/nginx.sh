#!/bin/sh
# shellcheck disable=SC2086 # package lists are split into words on purpose
#
# get.nginx.org/nginx: install NGINX Open Source from the official nginx.org
# package repositories.
#
#   curl --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx | sh
#   curl --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx |
#       sh -s -- --stable --module njs
#
# Piping a download into a shell runs whatever the server sends, unread. To
# review the script first:
#
#   curl --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx -o install-nginx.sh
#   less install-nginx.sh
#   sh install-nginx.sh --dry-run
#   sh install-nginx.sh
#
# Security notes:
#   - Keep --proto '=https' and the full https:// URL. Without them curl can
#     fetch the script over plain HTTP, and anyone on the network path can
#     then run commands as root on your machine.
#   - Pipe into sh, not sudo sh. The script uses sudo itself for the commands
#     that need root. Under sudo sh everything runs as root, including the
#     parsing of the downloaded key.
#   - In a Dockerfile or CI job, don't pipe from the network on every build.
#     Save a copy you have read, record its SHA-256, check it on every build,
#     and run that copy with --yes.
#   - The nginx.org repository and its signing keys stay on the system after
#     the install, and later upgrades take nginx from nginx.org. --help lists
#     what to remove to undo that.
#   - Nothing in this script can prove that the script itself is genuine. The
#     pinned fingerprints below only protect the key download.
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
# The download command shown to users. --proto '=https' makes curl refuse
# plain HTTP, also when a redirect points to it.
SAFE_CURL="curl --proto '=https' --tlsv1.2 -fsSL"
NGINX_ORG="https://nginx.org"
PACKAGES_DOC="$NGINX_ORG/en/linux_packages.html"
PAUSE=10

# Distributions and versions nginx.org builds packages for, from
# https://nginx.org/en/linux_packages.html#distributions. All of them are
# x86_64 and aarch64, except SLES 15, which is x86_64 only.
#
# SLES 15: nginx.org lists "15 SP6+". This script needs SP7 or later, because
# the nginx.org packages need OpenSSL 3.2 and SP6 ships 3.1. That rule is this
# script's own and isn't on nginx.org, so test it again when the packages
# change. openSUSE Leap 15 ended at 15.6, so only Leap 16 passes.
#
# LISTS_CHECKED is the date these lists last matched nginx.org. Change it
# whenever you change a list.
LISTS_CHECKED="2026-11-20"
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
# field_lines LABEL TEXT: field for a text of several lines. The label is
# printed on the first line only.
field_lines() {
    printf '%s\n' "$2" | {
        _label=$1
        while IFS= read -r _line; do
            field "$_label" "$_line"
            _label=""
        done
    }
}
warn()  { printf '%sWarning:%s %s\n' "$yellow" "$reset" "$*" >&2; }
die()   { printf '%sError:%s %s\n' "$red" "$reset" "$*" >&2; exit 1; }

# 573BFD6B3D8F... -> 573B FD6B 3D8F ...
spaced() { printf '%s\n' "$1" | sed 's/..../& /g; s/ $//'; }

supported_systems() {
    printf '  %-21s %s\n' \
        "RHEL and derivatives" "$SUPPORTED_RHEL" \
        "Debian" "$SUPPORTED_DEBIAN" \
        "Ubuntu" "$SUPPORTED_UBUNTU" \
        "SLES, openSUSE Leap" "$SUPPORTED_SLES (SLES 15: SP7 or later, x86_64 only; Leap: 16)" \
        "Alpine" "$SUPPORTED_ALPINE" \
        "Amazon Linux" "$SUPPORTED_AMZN"
}

usage() {
    cat <<EOF
Install NGINX Open Source from the nginx.org package repositories.

Usage:
  $SAFE_CURL $SCRIPT_URL | sh -s -- [options]
  sh install-nginx.sh [options]

Options:
  --mainline      Install the mainline branch (default)
  --stable        Install the stable branch
  --module NAME   Also install the nginx-module-NAME package. Repeat for more
                  modules. --module=NAME works too.
                  Available: $MODULES
  -n, --dry-run   Show the plan and the commands without changing anything.
                  It still reads /etc/os-release, downloads the signing key,
                  checks it with gpg or openssl, and queries the package
                  manager
  -y, --yes       Don't pause for $PAUSE seconds before making changes. Use it
                  in a Dockerfile or CI job, where nobody can press Ctrl+C
  -h, --help      Show this help

Supported systems (x86_64 and aarch64):
$(supported_systems)

Security:
  Pipe into sh, not sudo sh: the script uses sudo itself where it needs root.
  The nginx.org repository and its signing keys stay after the install, and
  later system upgrades take nginx from nginx.org.

To undo that, as root (this leaves the nginx packages installed):
  Debian, Ubuntu  rm /etc/apt/sources.list.d/nginx.list
                     /etc/apt/preferences.d/99nginx
                     /usr/share/keyrings/nginx-archive-keyring.gpg
  RHEL, Amazon    rm /etc/yum.repos.d/nginx.repo
                     /etc/pki/rpm-gpg/RPM-GPG-KEY-nginx
  SLES            zypper removerepo nginx-stable nginx-mainline
  Alpine          delete the @nginx line in /etc/apk/repositories, then
                  rm /etc/apk/keys/nginx_signing.rsa.pub
  RHEL, Amazon and SLES also keep the keys in the rpm database. List them:
                  rpm -q gpg-pubkey --qf '%{NAME}-%{VERSION}-%{RELEASE} %{SUMMARY}\n'
                  then run rpm -e on each gpg-pubkey line that names nginx.
  A file this script replaced is kept, the first time, as FILE.bak.

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

# Shell-quote one word for display, so a printed command can be copied and
# run. Words made only of safe characters are printed as they are.
quoted() {
    case $1 in
        ''|*[!A-Za-z0-9_./:=@%+,-]*)
            printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")" ;;
        *)
            printf '%s' "$1" ;;
    esac
}

# Run a command as root, or only print it in a dry run. Commands get /dev/null
# as stdin so a package manager can't stop and wait for an answer. It isn't
# there to protect a piped script: the shell has read the whole script before
# main runs.
run() {
    if [ "$dry_run" = 1 ]; then
        _line="  +${sudo:+ $sudo}"
        for _arg in "$@"; do
            _line="$_line $(quoted "$_arg")"
        done
        say "$_line"
    else
        $sudo "$@" </dev/null
    fi
}

# write_file DEST: install stdin as DEST, owned by root. A file that already
# exists keeps its mode, since it may hold credentials; a new one gets 644.
# If DEST is a symlink, its target is written and the link is kept: the
# mode of a link itself is always 777, so replacing it with a regular file
# of that mode would let any user edit a file the package manager reads as
# root.
write_file() {
    _src="$tmp_dir/$(basename "$1")"
    cat >"$_src"
    _dest=$1
    if $sudo test -L "$1"; then
        _dest=$($sudo readlink -f "$1") ||
            die "$1 is a symlink that doesn't resolve. Nothing in it was changed."
    fi
    # Keep the first copy of a file this script replaces. It may hold settings
    # the new file drops, such as a repository file that points at a company
    # mirror. apt, dnf and apk don't read files that end in .bak. Check as
    # root, since the user may not be able to see into the directory.
    if $sudo test -e "$_dest" && ! $sudo test -e "$_dest.bak"; then
        run cp -p "$_dest" "$_dest.bak"
    fi
    if [ "$dry_run" = 1 ]; then
        say "  + write $_dest:"
        sed 's/^/      /' "$_src"
    else
        _mode=$($sudo stat -c %a "$_dest" 2>/dev/null) || _mode=644
        run install -m "$_mode" "$_src" "$_dest"
    fi
}

# read_file FILE: print FILE as root, or stop if it can't be read. A dry run
# reads it without root.
read_file() {
    if [ "$dry_run" = 1 ]; then
        cat "$1" 2>/dev/null || true
    else
        $sudo cat "$1" || die "couldn't read $1. Nothing in it was changed."
    fi
}

# fetch URL FILE. A failed download stops the script. In a dry run it warns
# and returns 1 instead, so the caller can skip its step and the dry run can
# still show the remaining commands.
#
# -q has to come first. It makes curl skip ~/.curlrc, where an "insecure" or
# "proxy" line would weaken this download without the user noticing.
fetch() {
    curl -q -fsSL --proto '=https' --tlsv1.2 --retry 3 \
        --connect-timeout 15 --max-time 120 -o "$2" "$1" && return 0
    [ "$dry_run" = 1 ] || die "couldn't download $1"
    warn "couldn't download $1"
    return 1
}

# Where every nginx.org package installs the binary.
NGINX_BIN=/usr/sbin/nginx

# The maintainer field of every nginx.org deb and apk package. from_nginx_org
# uses it to tell them apart from a distribution's own nginx packages.
NGINX_MAINTAINER="NGINX Packaging <nginx-packaging@f5.com>"

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
    die "$os_name on $arch isn't on this script's list of systems that
nginx.org builds packages for. The list was checked on $LISTS_CHECKED:
$(supported_systems)
If $PACKAGES_DOC lists your system,
this copy of the script is out of date. Get the current one from
$SCRIPT_URL"
}

detect_os() {
    [ -r /etc/os-release ] || die "can't detect the OS: /etc/os-release is missing."
    # /etc/os-release doesn't have to set every field. Clear the ones used
    # below, so a field it leaves out can't be filled in from the environment
    # and steer the script to another distribution's repository.
    unset ID ID_LIKE VERSION_ID VERSION_CODENAME UBUNTU_CODENAME PRETTY_NAME
    # shellcheck disable=SC1091
    . /etc/os-release
    os_name=${PRETTY_NAME:-${ID:-unknown}}
    version_id=${VERSION_ID:-}
    arch=$(uname -m)

    # ID_LIKE names a distribution's parents, so the order matters:
    #   - ubuntu before debian: Ubuntu and its derivatives also list debian.
    #   - amzn before rhel: Amazon Linux 2 lists "centos rhel fedora". Matched
    #     as amzn, it fails the version check instead of passing for RHEL 2.
    # The SLES patterns have no leading *, so they match ID only, not ID_LIKE.
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
/etc/apt/preferences.d/99nginx (takes nginx packages from nginx.org, and nothing else)"
            trust="apt accepts the keys for the nginx.org repository only,
and takes only nginx packages from it."
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
            trust="$pkg takes only nginx packages from this repository.
rpm accepts the keys for packages from any repository."
            ;;
        sles)
            pkg=zypper gpg_pkg=gpg2 ca_bundle=/etc/ssl/ca-bundle.pem
            # zypper expands $releasever_major itself, to 15 or 16. The plan
            # shows the expanded URL; add_repository passes zypper the variable.
            repo_url="$pkgs_url/sles/$version"
            zypper_url="$pkgs_url/sles/\$releasever_major"
            changes="rpm database (signing keys)
/etc/zypp/repos.d/nginx-$channel.repo (repository, added with zypper)"
            trust="This script doesn't limit the repository to nginx packages on zypper.
rpm accepts the keys for packages from any repository."
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
            trust="apk uses this repository only for packages asked for with @nginx,
and for what they depend on. It accepts the key for any repository."
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
        field "" "revoked and expired keys are left out"
    fi
    field "Prerequisites" "${prereqs:-none needed}"
    field_lines "Changes" "$changes"
    field "" "a file that already exists is kept, the first time, as FILE.bak"
    field_lines "Trust" "$trust"
    field "" "This stays after the install. --help lists how to undo it."
    field "Runs as" "$run_as"

    if [ -z "$modules" ]; then
        printf '\nDynamic modules ship as separate packages. Add any of\n%s\nwith --module NAME.\n' \
            "$MODULES"
    fi
    if [ "$nginx_bin" = "$NGINX_BIN" ]; then
        echo
        _old=$("$nginx_bin" -v 2>&1 | sed 's/^nginx version: //')
        if from_nginx_org; then
            say "$_old from nginx.org is already installed at $nginx_bin.
This run upgrades it if the $channel branch has a newer version."
        else
            warn "$_old is already installed at $nginx_bin.
The nginx.org packages will replace it. Back up /etc/nginx first: a
configuration written for another build of nginx may not load in this one."
        fi
    elif [ -n "$nginx_bin" ]; then
        echo
        # Don't run it to ask for its version. It is whatever comes first in
        # PATH, and this script may be running as root.
        warn "another nginx is installed at $nginx_bin.
The nginx.org packages install $NGINX_BIN and leave that one in place, but it
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

    $SAFE_CURL $SCRIPT_URL -o install-nginx.sh
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

# gpg with a private, empty home directory: no user keyring, no user options.
tmp_gpg() { gpg --homedir "$tmp_dir/gnupg" --batch "$@"; }

# key_fingerprints FILE: the fingerprint of every primary key in FILE, one per
# line. It is field 10 of the fpr line after each pub line. sed, not awk,
# because SUSE's minimal images don't have awk.
key_fingerprints() {
    tmp_gpg --with-colons --show-keys "$1" 2>/dev/null |
        sed -n '/^pub:/{n;s/^fpr:\{9\}\([0-9A-F]*\):.*/\1/p;}'
}

# Check the downloaded key file, then write clean copies of the accepted keys:
#   $key_file                              ASCII armor, for rpm --import
#   $tmp_dir/nginx-archive-keyring.gpg     binary keyring, for apt
# The download itself is never installed.
verify_pgp_key() {
    _download=$tmp_dir/download.key
    fetch "$PGP_KEY_URL" "$_download" || return 0  # dry run only, see fetch
    mkdir -m 700 "$tmp_dir/gnupg"
    # gpg can print some keys and still fail on the rest of the file, so check
    # its exit status before trusting what it printed.
    _keys=$(tmp_gpg --with-colons --show-keys "$_download" 2>/dev/null) ||
        die "gpg couldn't read $PGP_KEY_URL.
Nothing was installed from nginx.org."
    # One FINGERPRINT:VALIDITY word per key. The validity is field 2 of the
    # pub line. The fingerprint is field 10 of the fpr line after it.
    _found=$(printf '%s\n' "$_keys" |
        sed -n '/^pub:/{h;n;G;s/^fpr:\{9\}\([0-9A-F]*\):.*\npub:\([^:]*\):.*/\1:\2/p;}')
    [ -n "$_found" ] || die "$PGP_KEY_URL doesn't contain any PGP keys."

    _good=""
    for _key in $_found; do
        _fpr=${_key%%:*}
        contains "$_fpr" "$PGP_FINGERPRINTS" || die "$PGP_KEY_URL contains a key this script doesn't know:
    $(spaced "$_fpr")
Nothing was installed from nginx.org. Check $PACKAGES_DOC, and get a newer
copy of this script if nginx has added a signing key."
        # A pinned key that nginx has revoked, or that has expired, is left
        # out and not trusted. The fingerprint alone doesn't show either, and
        # older versions of rpm don't check for them. Packages signed with
        # such a key then fail the package manager's signature check.
        case ${_key#*:} in
            r) _why=revoked ;;
            e) _why=expired ;;
            i) _why=invalid ;;
            d) _why=disabled ;;
            *) _why="" ;;
        esac
        if [ -n "$_why" ]; then
            warn "left out $(spaced "$_fpr"): the key is $_why."
        else
            _good="${_good:+$_good }$_fpr"
            say "  ${green}ok${reset}  $(spaced "$_fpr")"
        fi
    done
    [ -n "$_good" ] || die "$PGP_KEY_URL has no key that is still valid.
Nothing was installed from nginx.org."

    # Write clean copies: import the file, then export only the accepted keys.
    # gpg --show-keys doesn't list everything in a key file. A subkey without
    # a valid binding signature, for one, passes the fingerprint check unseen,
    # and some signature checkers have accepted such subkeys. An export from
    # gpg's own keyring holds only what gpg has tied to the accepted keys.
    # The import may complain about a key that was left out, so the checks
    # that count come after the export.
    tmp_gpg --quiet --import "$_download" >/dev/null 2>&1 || true
    # One armor block per key. rpm imports each block of a file, but older
    # versions may read only the first key inside a block.
    : >"$key_file"
    for _fpr in $_good; do
        tmp_gpg --armor --export "$_fpr" >>"$key_file" 2>/dev/null ||
            die "gpg couldn't export $(spaced "$_fpr")."
    done
    tmp_gpg --yes --output "$tmp_dir/nginx-archive-keyring.gpg" --export $_good 2>/dev/null ||
        die "gpg couldn't export the accepted keys."
    # Both copies must hold exactly the accepted keys.
    for _file in "$key_file" "$tmp_dir/nginx-archive-keyring.gpg"; do
        _clean=$(key_fingerprints "$_file" | tr '\n' ' ')
        for _fpr in $_good; do
            contains "$_fpr" "$_clean" ||
                die "internal error: $(spaced "$_fpr") is missing from the cleaned key file."
        done
        for _fpr in $_clean; do
            contains "$_fpr" "$_good" ||
                die "internal error: the cleaned key file holds the unexpected key $(spaced "$_fpr")."
        done
    done
    key_verified=1
}

verify_rsa_key() {
    fetch "$RSA_KEY_URL" "$tmp_dir/download.pub" || return 0  # dry run only, see fetch
    # Re-encode, so only the key itself ends up in /etc/apk/keys.
    openssl pkey -pubin -in "$tmp_dir/download.pub" -out "$key_file" 2>/dev/null ||
        die "$RSA_KEY_URL isn't a public key."
    _sha256=$(openssl pkey -pubin -in "$key_file" -outform DER 2>/dev/null |
        openssl dgst -sha256 2>/dev/null | sed 's/^.*= *//') || true
    [ "$_sha256" = "$RSA_KEY_SHA256" ] || die "$RSA_KEY_URL doesn't match the pinned nginx signing key.
Nothing was installed from nginx.org. Check $PACKAGES_DOC#Alpine"
    say "  ${green}ok${reset}  SHA-256 $RSA_KEY_SHA256"
    key_verified=1
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
    if [ "$key_verified" != 1 ]; then
        say "Skipped: the dry run couldn't download the key."
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
    # Prefer nginx.org over the distribution's nginx, whatever its epoch or
    # version. dnf's default priority is 99 and the lower number wins. nginx.org
    # documents this line for Amazon Linux only. It is written for RHEL and its
    # derivatives too, because in 2024 the EL9 nginx moved to epoch 2 and dnf
    # then chose it over the nginx.org packages.
    echo "priority=9"
    # Take only nginx packages from this repository, so it can't replace
    # unrelated system packages in a later upgrade. Every package nginx.org
    # publishes here has a name that starts with nginx.
    echo "includepkgs=nginx*"
}

add_repository() {
    step "Adding the nginx.org $channel repository"
    # Outside a dry run, verify_signing_key has either verified the key or
    # stopped the script. This line guards against a later change breaking that.
    [ "$dry_run" = 1 ] || [ "$key_verified" = 1 ] ||
        die "internal error: the signing key wasn't verified."
    case $pkg in
        apt)
            # verify_pgp_key wrote this keyring. In a dry run it may be
            # missing, because the key wasn't downloaded.
            run install -m 644 "$tmp_dir/nginx-archive-keyring.gpg" "$keyring"
            echo "deb [signed-by=$keyring] $repo_url" |
                write_file /etc/apt/sources.list.d/nginx.list
            # Two rules. nginx packages from nginx.org beat the distribution's.
            # Every other package from nginx.org is never installed, so the
            # repository can't replace unrelated system packages in a later
            # upgrade. Both match the host name in the repository URL.
            # nginx.org documents one rule, for all packages, with two Pin
            # lines. apt uses only the last Pin line of a rule, so that rule
            # really matched "release o=nginx": the Origin field of a Release
            # file, which any repository can set.
            printf '%s\n' \
                'Package: nginx nginx-*' 'Pin: origin nginx.org' 'Pin-Priority: 900' '' \
                'Package: *' 'Pin: origin nginx.org' 'Pin-Priority: -1' |
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
            # Check as root: the user may not be able to read the repository
            # files, and would miss one that addrepo then finds.
            for _repo in nginx-stable nginx-mainline; do
                if $sudo zypper --non-interactive repos "$_repo" >/dev/null 2>&1; then
                    run zypper --non-interactive removerepo "$_repo"
                fi
            done
            run zypper --non-interactive addrepo --gpgcheck --type yum --refresh --check \
                "$zypper_url" "nginx-$channel"
            ;;
        apk)
            run install -m 644 "$key_file" /etc/apk/keys/nginx_signing.rsa.pub
            # Read the file as root and stop if that fails, so a failed read
            # can't replace the system's repositories with only this one.
            _repos=$(read_file /etc/apk/repositories)
            # Drop every earlier nginx.org entry, over http or https and with
            # any spacing, so only the chosen branch is left.
            { printf '%s\n' "$_repos" |
                  grep -v -E '^[[:space:]]*@nginx[[:space:]]+https?://nginx\.org/packages/' || true
              echo "@nginx $repo_url"; } |
                write_file /etc/apk/repositories
            ;;
    esac
}

# Enabled nginx.org package sources other than the one this script wrote,
# one per line with the file it is in. With one for the other branch still
# enabled, the package manager can keep or pick that branch, and a switch
# with --stable can't take effect. apt lists only fetched sources, so call
# this after apt-get update.
other_nginx_sources() {
    case $pkg in
        apt)
            # shellcheck disable=SC2016 # $(SITE) and the rest are apt's fields
            LC_ALL=C apt-get indextargets --format '$(SITE) $(RELEASE) $(SOURCESENTRY)' 2>/dev/null |
                grep -E '^https?://nginx\.org/packages/' | sort -u |
                grep -v " /etc/apt/sources.list.d/nginx.list:" || true
            ;;
        dnf|microdnf)
            # Enabled sections with an nginx.org URL, in any .repo file but
            # the one this script writes. Each section is collected in the
            # hold space as its header, plus a URL= line for an nginx.org
            # URL and an enabled=0 line if it's off. A section with no
            # enabled line is enabled.
            for _f in /etc/yum.repos.d/*.repo; do
                [ "$_f" = /etc/yum.repos.d/nginx.repo ] && continue
                $sudo cat "$_f" 2>/dev/null | sed -n '
                    /^\[/ { x; /\nURL=/ { /\nenabled=0/!p; }; s/.*//; x; h; d; }
                    /^[[:space:]]*\(baseurl\|mirrorlist\|metalink\)[[:space:]]*=.*nginx\.org\/packages\// { s/.*/URL=/; H; }
                    /^[[:space:]]*enabled[[:space:]]*=[[:space:]]*0/ { s/.*/enabled=0/; H; }
                    $ { x; /\nURL=/ { /\nenabled=0/!p; }; }' |
                    sed -n "s|^\[\([^]]*\)\].*|\1 in $_f|p"
            done
            ;;
        zypper)
            # The columns are # | Alias | Name | Enabled | GPG Check |
            # Refresh | URI. sed, not awk, because SUSE's minimal images
            # don't have awk.
            LC_ALL=C zypper -q --no-refresh repos -u -E 2>/dev/null |
                sed -n 's/^[^|]*|[[:space:]]*\([^|[:space:]]*\)[[:space:]]*|.*|[[:space:]]*\(https\{0,1\}:\/\/nginx\.org\/packages\/[^[:space:]]*\).*/\1 \2/p' |
                grep -v -E "^nginx-$channel " || true
            ;;
        apk)
            for _f in /etc/apk/repositories.d/*.list; do
                $sudo test -e "$_f" || continue
                $sudo cat "$_f" 2>/dev/null |
                    grep -E '^[^#]*https?://nginx\.org/packages/' | sed "s|\$| in $_f|" || true
            done
            ;;
    esac
}

# Stop if another enabled source serves nginx.org packages.
check_other_sources() {
    [ "$dry_run" = 1 ] && return
    _others=$(other_nginx_sources)
    [ -z "$_others" ] && return
    die "another nginx.org package source is enabled:
$(printf '%s\n' "$_others" | sed 's/^/    /')
With it, the package manager can install the other branch, or keep it after
a switch. Disable or remove it, then run this script again. Nothing was
installed from nginx.org."
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
Any failing repository stops the install here, including one that has
nothing to do with nginx. Fix or disable it, then run this script again.
nginx.org is set up as a repository, but nothing was installed from it."
            check_other_sources
            # Keep any existing configuration files rather than prompting.
            apt_get -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold \
                install $packages
            ;;
        dnf|microdnf)
            check_other_sources
            run "$pkg" install -y $packages ;;
        zypper)
            check_other_sources
            run zypper --non-interactive install --from "nginx-$channel" $packages ;;
        # Without --upgrade, apk add keeps an installed version that still
        # satisfies the request, unlike the other package managers' install.
        apk)
            check_other_sources
            run apk add --no-cache --upgrade $packages ;;
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
            [ "$(dpkg-query -W -f '${Maintainer}' nginx 2>/dev/null)" = "$NGINX_MAINTAINER" ]
            ;;
        dnf|microdnf|zypper)
            # A package can carry two RSA signatures. RSAHEADER signs the
            # header, and current rpmsign always adds it. SIGPGP signs the
            # header and payload, and rpmsign only adds it when a package needs
            # it. Ask for both: a missing one prints "(none)" and is dropped by
            # the sed below.
            # A key ID is the last 16 hex digits of the key's fingerprint.
            _keyids=$(LC_ALL=C rpm -q --qf '%{RSAHEADER:pgpsig}\n%{SIGPGP:pgpsig}\n' nginx 2>/dev/null |
                sed -n 's/.*Key ID \([0-9a-fA-F]\{16\}\).*/\1/p' | tr 'a-f' 'A-F')
            for _keyid in $_keyids; do
                for _fpr in $PGP_FINGERPRINTS; do
                    if [ "${_fpr#????????????????????????}" = "$_keyid" ]; then
                        return 0
                    fi
                done
            done
            return 1
            ;;
        apk)
            sed -n '/^P:nginx$/,/^$/p' /lib/apk/db/installed 2>/dev/null |
                grep -qxF "m:$NGINX_MAINTAINER"
            ;;
    esac
}

# Is the exact nginx version that is installed one that the nginx.org
# repository offers? from_nginx_org reads fields of the installed package,
# which a package built elsewhere can copy, and a package manager keeps an
# installed version that is newer than any it is offered. This asks the
# package manager, which checked the repository's signature against the
# pinned keys, so only a version nginx.org published passes. Call it once
# the nginx.org repository is set up. The queries run as root, since the
# repository files and caches may not be readable by the user.
offered_by_nginx_org() {
    case $pkg in
        apt)
            # The lines under the installed version (***) name the sources
            # that offer it.
            $sudo env LC_ALL=C apt-cache policy nginx 2>/dev/null |
                sed -n '/^ \*\*\* /,/^     [^ ]/p' | sed '1d;$d' |
                grep -q -E ' https?://nginx\.org/packages/'
            ;;
        # Either of this script's repositories: finish checks the branch
        # separately, so a mainline build left after --stable gets the
        # downgrade advice rather than this error.
        dnf)
            $sudo env LC_ALL=C dnf -q repoquery --qf '%{repoid}' "$(rpm -q nginx)" 2>/dev/null |
                grep -q -x -E 'nginx-(stable|mainline)'
            ;;
        microdnf)
            # microdnf repoquery prints the package only if a repository offers
            # it. includepkgs=nginx* and priority=9 keep other repositories'
            # nginx out of the way, and other_nginx_sources has stopped the
            # script if another nginx.org one is enabled.
            [ -n "$($sudo microdnf repoquery "$(rpm -q nginx)" 2>/dev/null)" ]
            ;;
        zypper)
            _v=$(rpm -q --qf '%{VERSION}-%{RELEASE}' nginx 2>/dev/null)
            $sudo env LC_ALL=C zypper -q --no-refresh search -s -x -t package nginx 2>/dev/null |
                grep -F "| $_v " | grep -q -E '\| nginx-(stable|mainline)[[:space:]]*$'
            ;;
        apk)
            # --no-cache fetches the indexes, which apk 2 doesn't keep after an
            # install with --no-cache.
            _v=$($sudo apk info -e -v nginx 2>/dev/null | sed 's/^nginx-//')
            [ -n "$_v" ] && $sudo apk --no-cache policy nginx 2>/dev/null |
                sed -n "/^  $_v:\$/,/^  [^ ]/p" | grep -q -E '^    @nginx https?://nginx\.org/packages/'
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
The package manager kept or chose another repository's nginx package. Check
the output above for download errors, and that nginx.org has packages for
$os_name on $arch.
If the other package has a higher version or epoch, the package manager won't
replace it on its own: back up /etc/nginx, remove that package, then run this
script again."

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

    # The checks above read the installed package's own fields. This one
    # confirms that nginx.org published this exact version.
    offered_by_nginx_org || die "$_installed is installed and looks like an nginx.org package, but
the nginx.org repository doesn't offer this version. It may be a local
rebuild, or a newer version that the package manager kept. Back up
/etc/nginx, remove the nginx package, then run this script again."

    # A configuration written for another build of nginx, such as a
    # distribution's with its own modules and file layout, may not load in
    # this one. nginx -t needs root to read keys and open the log files.
    step "Testing the configuration"
    _config_ok=1
    run "$NGINX_BIN" -t || _config_ok=0

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

    if [ "$_config_ok" = 0 ]; then
        echo
        warn "nginx is installed, but 'nginx -t' rejected the configuration in
/etc/nginx. See its messages above. nginx won't start or reload until the
configuration is fixed. An nginx that is already running keeps its old
configuration until then."
    fi
}

# shellcheck disable=SC2329 # called from the EXIT trap in main
cleanup() {
    # gpg may have started helper daemons for its private home directory.
    if [ -d "$tmp_dir/gnupg" ] && has gpgconf; then
        gpgconf --homedir "$tmp_dir/gnupg" --kill all >/dev/null 2>&1 || true
    fi
    rm -rf "$tmp_dir"
}

main() {
    channel=mainline modules="" dry_run=0 assume_yes=0 key_verified=0
    setup_colors
    parse_args "$@"
    detect_os
    check_privileges

    tmp_dir=$(mktemp -d)
    trap cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM

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
