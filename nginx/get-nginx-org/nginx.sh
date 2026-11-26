#!/bin/sh
#
# get.nginx.org/nginx: install NGINX Open Source from the official nginx.org
# package repositories.
#
# Copyright (C) 2026 F5, Inc.
# SPDX-License-Identifier: BSD-2-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# Until get.nginx.org is live, this script is published in the nginx-demos
# repository on GitHub. Use this URL in the commands below:
#
#   https://raw.githubusercontent.com/nginx/nginx-demos/main/nginx/get-nginx-org/nginx.sh
#
#   curl -q --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx | sh
#   curl -q --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx |
#       sh -s -- --stable --module njs
#
# Piping a download into a shell runs whatever the server sends, unread. To
# review the script first:
#
#   curl -q --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx -o install-nginx.sh
#   less install-nginx.sh
#   sh install-nginx.sh --dry-run
#   sh install-nginx.sh
#
# Security notes:
#   - Keep -q first, --proto '=https', and the full https:// URL. -q makes curl
#     ignore ~/.curlrc (where an "insecure" line would disable TLS verification)
#     and --proto '=https' refuses plain HTTP, so nobody on the network path can
#     run commands as root on your machine.
#   - Pipe into sh, not sudo sh. The script uses sudo itself for the commands
#     that need root. Under sudo sh everything runs as root, including the
#     parsing of the downloaded key.
#   - In a Dockerfile or CI job, don't pipe from the network on every build.
#     Save a copy you have read, record its SHA-256, check it on every build,
#     and run that copy with --yes.
#   - The nginx.org repository and its signing keys stay on the system after
#     the install, and later upgrades take nginx from nginx.org. The summary
#     at the end, and --help, list what to remove to undo that.
#   - Nothing in this script can prove that the script itself is genuine. The
#     pinned fingerprints below only protect the key download.
#
# What it does, following https://nginx.org/en/linux_packages.html:
#   1. Detects the OS and architecture.
#   2. Prints the repository, signing keys, packages, and files it will
#      install, then pauses so you can cancel.
#   3. Installs the prerequisites (curl, gpg or openssl, CA certificates).
#   4. Downloads the nginx signing key and stops unless every key in it is
#      one pinned below. Only the 2024 keys are installed.
#   5. Adds the nginx.org repository for the chosen branch.
#   6. Installs nginx and any requested dynamic modules. If that fails, it
#      undoes step 5.
#
# All the work happens in main(), called inside a { } block on the last line,
# so a download cut short anywhere runs nothing.

set -eu

# Where users download this script, shown in --help, the plan, and the pause.
# Change it to https://get.nginx.org/nginx when that site is live.
SCRIPT_URL="https://raw.githubusercontent.com/nginx/nginx-demos/main/nginx/get-nginx-org/nginx.sh"
# The download command shown to users. -q (must come first) skips ~/.curlrc,
# and --proto '=https' makes curl refuse plain HTTP, also after a redirect.
SAFE_CURL="curl -q --proto '=https' --tlsv1.2 -fsSL"
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
# LISTS_CHECKED is the date these lists were last compared with nginx.org.
# Change it whenever you change a list.
LISTS_CHECKED="2026-11-24"
SUPPORTED_RHEL="8 9 10"
SUPPORTED_DEBIAN="bookworm trixie"
SUPPORTED_UBUNTU="jammy noble resolute"
SUPPORTED_SLES="15 16"
SUPPORTED_ALPINE="3.22 3.23 3.24"
SUPPORTED_AMZN="2023"

# Dynamic modules shipped as nginx-module-NAME packages, from
# https://nginx.org/en/linux_packages.html#dynmodules
MODULES="acme geoip image-filter njs otel perl xslt"

# nginx_signing.key holds the 2011 signing key and the two 2024 keys it
# certifies. The script installs only the 2024 keys. Every key in the
# downloaded file must be one of them or the 2011 key, or the script stops
# before changing the repository configuration.
PGP_KEY_URL="$NGINX_ORG/keys/nginx_signing.key"
PGP_FINGERPRINTS="
8540A6F18833A80E9C1653A42FD21310B49F6B46
9E9BE90EACBCDE69FE9B204CBCDCD8A38D88A2B3
"
# The 2011 key signs nothing nginx.org publishes now, so it isn't installed.
# The script still has to know it: nginx_signing.key holds it, and it signed
# older nginx.org rpm packages, which the script mustn't mistake for a
# distribution's nginx when it finds one installed.
PGP_RETIRED="573BFD6B3D8FBC641079A6ABABF5BD827BD9BF62"

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
                  Not every module is built for every system and branch. On
                  the stable branch, RHEL, Amazon Linux, and SLES 16 have no
                  geoip
  --unattended-upgrades
                  Debian and Ubuntu: let unattended-upgrades install nginx
                  updates from nginx.org. Off by default, which leaves nginx
                  without automatic updates
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
  Pipe into sh, not sudo sh: the script uses sudo or doas itself where it
  needs root. If the install fails, the script undoes its changes to the
  package sources. Otherwise the nginx.org repository and its signing keys
  stay, and later system upgrades take nginx from nginx.org.

To undo that, run these commands as root. They leave nginx installed. The
end of each run prints them for that system, with its packages filled in.
  Debian, Ubuntu:
$(undo_commands apt "" | sed 's/^/    /')
  RHEL and derivatives, Amazon Linux:
$(undo_commands dnf "" | sed 's/^/    /')
  SLES, openSUSE Leap:
$(undo_commands zypper "" | sed 's/^/    /')
  Alpine:
$(undo_commands apk "" "nginx nginx-module-NAME..." | sed 's/^/    /')
  A file this script replaced is kept, the first time, as FILE.bak.

Manual installation steps: $PACKAGES_DOC
EOF
}

# undo_commands PKG PREFIX [PACKAGES]: print the commands that remove the
# repository and keys this script adds with package manager PKG, each
# starting with PREFIX, such as "sudo ". nginx stays installed. usage prints
# them for every system, and finish for this one, so the two can't drift
# apart. On Alpine, PACKAGES are the installed nginx packages: apk add drops
# their @nginx tag, without which apk refuses every change once the
# repository is gone.
undo_commands() {
    case $1 in
        apt)
            for _f in /etc/apt/sources.list.d/nginx.list /etc/apt/preferences.d/99nginx \
                    "$UU_CONF" "$APT_KEYRING"; do
                say "${2}rm -f $_f"
            done
            ;;
        dnf|microdnf)
            say "${2}rm -f /etc/yum.repos.d/nginx.repo"
            say "${2}rm -f $RPM_KEY"
            ;;
        zypper)
            say "${2}zypper removerepo nginx-stable nginx-mainline"
            ;;
        apk)
            say "${2}sed -i '\\|^@nginx https://nginx\\.org/packages/|d' /etc/apk/repositories"
            say "${2}rm -f /etc/apk/keys/nginx_signing.rsa.pub"
            say "${2}apk add $3"
            ;;
    esac
    case $1 in
        dnf|microdnf|zypper)
            # One at a time: rpm -e removes nothing if one of them is missing.
            # shellcheck disable=SC2086 # $PGP_FINGERPRINTS is a word list
            for _fpr in $PGP_FINGERPRINTS; do
                say "${2}rpm -e $(rpm_key_name "$_fpr")"
            done
            ;;
    esac
}

# rpm_key_name FPR: the name rpm gives the key with fingerprint FPR in its
# database: gpg-pubkey- and the last 8 hex digits, in lower case.
rpm_key_name() {
    printf 'gpg-pubkey-%s\n' "$(printf '%s' "$1" | sed 's/.*\(........\)$/\1/' | tr 'A-F' 'a-f')"
}

# --- Helpers -----------------------------------------------------------------

has() { command -v "$1" >/dev/null 2>&1; }

# contains WORD LIST: is WORD one of the whitespace-separated words in LIST?
contains() {
    # shellcheck disable=SC2086 # $2 is a whitespace-separated word list
    for _item in $2; do
        if [ "$_item" = "$1" ]; then return 0; fi
    done
    return 1
}

# Run a command as root (via sudo or doas when not already root).
as_root() {
    if [ -n "$sudo" ]; then
        "$sudo" "$@"
    else
        "$@"
    fi
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
# as stdin so a package manager can't stop and wait for an answer.
run() {
    if [ "$dry_run" = 1 ]; then
        _line="  +${sudo:+ $sudo}"
        for _arg in "$@"; do
            _line="$_line $(quoted "$_arg")"
        done
        say "$_line"
    else
        as_root "$@" </dev/null
    fi
}

# priv CMD...: run a read-only check as root. A dry run runs it as the
# current user instead, so a dry run never calls sudo or doas and never asks
# for a password. A check that the user can't make, such as looking into a
# directory only root can open, then reports "not found" in the dry run.
priv() {
    if [ "$dry_run" = 1 ]; then
        "$@"
    else
        as_root "$@"
    fi
}

# write_file DEST: install stdin as DEST, owned by root. A file that already
# exists keeps its read/owner-write mode (since it may hold credentials),
# never write access for group or others, execute, or special bits; a new one
# gets 644.
# For a symlink, write_file writes the target and keeps the link, provided the
# target's parent directory is owned by root and not writable by group/others.
write_file() {
    _src="$tmp_dir/$(basename "$1")"
    cat >"$_src"
    _dest=$1
    if priv test -L "$1"; then
        _dest=$(priv readlink -f "$1") ||
            die "$1 is a symlink that doesn't resolve. Nothing in it was changed."
        # A root-owned file inside a directory writable by a non-root user
        # can still be unlinked and replaced by that user.
        _dir=$(dirname "$_dest")
        if _dir_owner=$(priv stat -c %u "$_dir" 2>/dev/null) &&
            _dir_mode=$(priv stat -c %a "$_dir" 2>/dev/null); then
            if [ "$_dir_owner" != 0 ] || [ $((0$_dir_mode & 022)) != 0 ]; then
                die "$1 points to $_dest in a directory writable by non-root users. Nothing in it was changed."
            fi
        elif [ "$dry_run" != 1 ]; then
            die "couldn't inspect $_dir for $1. Nothing in it was changed."
        fi
    fi
    # Keep the first copy of a file this script replaces. It may hold settings
    # the new file drops, such as a repository file that points at a company
    # mirror. apt, dnf and apk don't read files that end in .bak. Check as
    # root, since the user may not be able to see into the directory. Also
    # check -L so a dangling symlink isn't followed by cp.
    if priv test -e "$_dest" && ! priv test -e "$_dest.bak" && ! priv test -L "$_dest.bak"; then
        save_file "$_dest.bak"
        run cp -p "$_dest" "$_dest.bak"
    fi
    if [ "$dry_run" = 1 ]; then
        say "  + write $_dest:"
        sed 's/^/      /' "$_src"
    else
        save_file "$_dest"
        _mode=$(priv stat -c %a "$_dest" 2>/dev/null) || _mode=644
        # The package manager reads these files as root. Keep read bits (for
        # 600/640 credential files) and owner write, but strip group/other
        # write, execute, and special (suid/sgid/sticky) bits.
        _mode=$(printf '%o' $((0$_mode & 0644)))
        run install -m "$_mode" "$_src" "$_dest"
    fi
}

# --- Undoing the repository changes ------------------------------------------
#
# add_repository changes the package sources before anything is installed. If
# the install then fails, or is cancelled, rollback puts them back as they
# were, so a half-finished run can't leave a repository behind that later
# upgrades take nginx from without this script's checks.
#
# save_file FILE records FILE before add_repository changes it: a copy, if it
# exists, or a note to remove it. rollback is called from the EXIT trap while
# rollback_armed is 1. finish clears it once nginx is verified from nginx.org:
# from then on the repository is what keeps it up to date.

# The records live in $rollback_dir (0700 root:root under /tmp, created by
# add_repository before any write_file pipeline), rather than in variables or
# the user-owned $tmp_dir, so write_file subshells can update them and an
# unprivileged process can't tamper with what rollback deletes or restores:
#   saved/                copies of files replaced by this run
#   saved.list            paths of replaced files, one per line
#   new.list              paths of newly created files, one per line
#   rpm-keys-before.list  gpg-pubkey NEVRAs present before rpm --import
# The copies in saved/ are named after their path, with each / as %.
save_file() {
    [ "$dry_run" = 1 ] && return 0
    [ -n "$rollback_dir" ] || die "internal error: rollback directory wasn't created."
    # Only the first call for a file counts: by the second, this run may
    # have changed it.
    if as_root grep -q -x -F -- "$1" "$rollback_dir/saved.list" "$rollback_dir/new.list"; then
        return 0
    fi
    if as_root test -e "$1" || as_root test -L "$1"; then
        as_root cp -p -P "$1" "$rollback_dir/saved/$(printf '%s' "$1" | tr '/' '%')" ||
            die "couldn't save a copy of $1. Nothing in it was changed."
        printf '%s\n' "$1" | as_root tee -a "$rollback_dir/saved.list" >/dev/null ||
            die "couldn't record $1 for rollback."
    else
        printf '%s\n' "$1" | as_root tee -a "$rollback_dir/new.list" >/dev/null ||
            die "couldn't record $1 for rollback."
    fi
}

# Record the exact gpg-pubkey packages (name-version-release) already in the
# rpm database before this run imports any, so rollback removes only the exact
# keys this run imported and won't touch an unrelated key with a colliding
# 32-bit key ID.
note_rpm_keys() {
    [ "$dry_run" = 1 ] && return 0
    [ -n "$rollback_dir" ] || die "internal error: rollback directory wasn't created."
    rpm -qa --qf '%{NAME}-%{VERSION}-%{RELEASE}\n' 'gpg-pubkey' 2>/dev/null |
        as_root tee "$rollback_dir/rpm-keys-before.list" >/dev/null ||
        die "couldn't record existing rpm keys for rollback."
}

# shellcheck disable=SC2329 # called from cleanup, the EXIT trap
rollback() {
    rollback_armed=0
    printf '\n%sUndoing%s the changes to the package sources: the install failed or was cancelled.\n' \
        "$bold" "$reset" >&2
    _failed=""
    # zypper: remove the repository this run added. The files below then
    # put back the ones that were there before.
    if [ "$pkg" = zypper ]; then
        as_root zypper --non-interactive removerepo "nginx-$channel" >/dev/null 2>&1 || true
    fi
    if [ -n "$rollback_dir" ]; then
        # Read lists via fd 3 so loops run in the current shell (_failed
        # survives) while stdin stays the terminal in case sudo asks for a
        # password again.
        _new=$(as_root cat "$rollback_dir/new.list" 2>/dev/null) || _new=""
        if [ -n "$_new" ]; then
            while IFS= read -r _f <&3; do
                [ -n "$_f" ] || continue
                as_root rm -f "$_f" || _failed="$_failed $_f"
            done 3<<EOF
$_new
EOF
        fi
        _saved=$(as_root cat "$rollback_dir/saved.list" 2>/dev/null) || _saved=""
        if [ -n "$_saved" ]; then
            while IFS= read -r _f <&3; do
                [ -n "$_f" ] || continue
                # cp -P puts a symlink back as a symlink. Remove the file
                # first, so cp doesn't write through a symlink in its place.
                { as_root rm -f "$_f" &&
                    as_root cp -p -P "$rollback_dir/saved/$(printf '%s' "$_f" | tr '/' '%')" "$_f"; } ||
                    _failed="$_failed $_f"
            done 3<<EOF
$_saved
EOF
        fi
        # One at a time: rpm -e removes nothing if one of them is missing.
        # Remove only exact gpg-pubkey NEVRAs matching our pinned keys that
        # weren't present before add_repository ran.
        if as_root test -f "$rollback_dir/rpm-keys-before.list"; then
            # shellcheck disable=SC2086 # $PGP_FINGERPRINTS is a word list
            for _fpr in $PGP_FINGERPRINTS; do
                _prefix=$(rpm_key_name "$_fpr")
                _cur=$(rpm -qa --qf '%{NAME}-%{VERSION}-%{RELEASE}\n' 'gpg-pubkey' 2>/dev/null |
                    grep -E "^${_prefix}-" || true)
                if [ -n "$_cur" ]; then
                    while IFS= read -r _k <&3; do
                        [ -n "$_k" ] || continue
                        if ! as_root grep -q -x -F -- "$_k" "$rollback_dir/rpm-keys-before.list"; then
                            as_root rpm -e "$_k" || _failed="$_failed $_k"
                        fi
                    done 3<<EOF
$_cur
EOF
                fi
            done
        fi
    fi
    if [ -n "$_failed" ]; then
        warn "couldn't undo the changes to:$_failed
Remove them by hand. --help lists what the script adds."
    else
        say "The package sources are as they were before this run." >&2
    fi
}

# read_file FILE: print FILE as root, or stop if it can't be read. A dry run
# reads it without root.
read_file() {
    if [ "$dry_run" = 1 ]; then
        cat "$1" 2>/dev/null || true
    else
        as_root cat "$1" || die "couldn't read $1. Nothing in it was changed."
    fi
}

# fetch URL FILE. A failed download stops the script. In a dry run it warns
# and returns 1 instead, so the caller can skip its step and the dry run can
# still show the remaining commands.
#
# -q has to come first. It makes curl skip ~/.curlrc, where an "insecure" or
# "proxy" line would weaken this download without the user noticing.
fetch() {
    if curl -q -fsSL --proto '=https' --tlsv1.2 --retry 3 \
        --connect-timeout 15 --max-time 120 --max-filesize 1048576 \
        -o "$2" "$1"; then
        _size=$(wc -c <"$2" | tr -d '[:space:]')
        if [ "$_size" -le 1048576 ]; then
            return 0
        fi
        rm -f "$2"
        [ "$dry_run" = 1 ] || die "$1 is larger than 1 MiB."
        warn "$1 is larger than 1 MiB."
        return 1
    fi
    [ "$dry_run" = 1 ] || die "couldn't download $1"
    warn "couldn't download $1"
    return 1
}

# Where every nginx.org package installs the binary.
NGINX_BIN=/usr/sbin/nginx

# Where the signing keys are installed, for apt and for dnf.
APT_KEYRING=/usr/share/keyrings/nginx-archive-keyring.gpg
RPM_KEY=/etc/pki/rpm-gpg/RPM-GPG-KEY-nginx

# On Debian and Ubuntu, unattended-upgrades installs updates only from the
# origins it is told about, by default the distribution's own. Ubuntu Server
# runs it out of the box, so after a switch to nginx.org nginx stops getting
# the fixes it got before. With --unattended-upgrades, the script writes this
# file, which adds nginx.org. It is off by default because it installs every
# nginx.org release, new mainline features included, with no one watching.
# The apt pin already keeps that origin to nginx packages.
UU_CONF=/etc/apt/apt.conf.d/52nginx-unattended-upgrades

# The maintainer field of every nginx.org deb and apk package. from_nginx_org
# uses it to tell them apart from a distribution's own nginx packages.
NGINX_MAINTAINER="NGINX Packaging <nginx-packaging@f5.com>"

# Any nginx already on the system, packaged or not, for the pre-install warning.
find_nginx() {
    if [ -x "$NGINX_BIN" ]; then
        echo "$NGINX_BIN"
    elif [ -n "$path_nginx" ]; then
        echo "$path_nginx"
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

# set_channel BRANCH: --mainline and --stable can each be repeated, but not
# mixed, because the last one would win without a word.
set_channel() {
    if [ -n "$channel_set" ] && [ "$channel_set" != "$1" ]; then
        die "--mainline and --stable can't be used together."
    fi
    channel_set=$1 channel=$1
}

parse_args() {
    while [ $# -gt 0 ]; do
        case $1 in
            --mainline) set_channel mainline ;;
            --stable) set_channel stable ;;
            --module)
                [ $# -ge 2 ] || die "--module needs a module name. Available: $MODULES"
                add_module "$2"
                shift
                ;;
            --module=*) add_module "${1#*=}" ;;
            --unattended-upgrades) unattended=1 ;;
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
    die "$os_name on $arch isn't in this script's list of systems with
nginx.org packages. The list was checked on $LISTS_CHECKED:
$(supported_systems)
If $PACKAGES_DOC lists your system,
this copy of the script is out of date. Get the current one from
$SCRIPT_URL"
}

# os_release_field NAME: the value of field NAME in /etc/os-release, or
# nothing. Parsed as data rather than sourced, so a malformed or tampered
# file can't execute commands or alter this script's variables, and missing
# fields stay empty rather than inheriting values from the environment.
os_release_field() {
    _val=""
    while IFS= read -r _line || [ -n "$_line" ]; do
        case $_line in
            "$1"=*)
                _v=${_line#*=}
                case $_v in
                    \"*\")
                        _v=${_v#\"}
                        _v=${_v%\"}
                        _v=$(printf '%s\n' "$_v" | sed 's/\\\([\\"$`]\)/\1/g')
                        ;;
                    \'*\')
                        _v=${_v#\'}
                        _v=${_v%\'}
                        ;;
                esac
                _val=$_v
                ;;
        esac
    done </etc/os-release
    printf '%s' "$_val" | tr -d '[:cntrl:]'
}

detect_os() {
    [ -r /etc/os-release ] || die "can't detect the OS: /etc/os-release is missing."
    ID=$(os_release_field ID)
    ID_LIKE=$(os_release_field ID_LIKE)
    VERSION_ID=$(os_release_field VERSION_ID)
    VERSION_CODENAME=$(os_release_field VERSION_CODENAME)
    UBUNTU_CODENAME=$(os_release_field UBUNTU_CODENAME)
    PRETTY_NAME=$(os_release_field PRETTY_NAME)
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
            os=alpine version=$(printf '%s\n' "$version_id" | cut -d. -f1,2) supported=$SUPPORTED_ALPINE ;;
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
        die "this script needs root. Run it as root, or install sudo or doas."
    fi
}

# Work out the repository, packages, prerequisites, and files for this OS.
plan() {
    pkgs_url=$NGINX_ORG/packages
    if [ "$channel" = mainline ]; then
        pkgs_url=$pkgs_url/mainline
    fi

    packages=nginx
    # shellcheck disable=SC2086 # $modules is a whitespace-separated word list
    for _m in $modules; do
        packages="$packages nginx-module-$_m"
    done

    key_url=$PGP_KEY_URL
    key_file=$tmp_dir/nginx_signing.key
    # The directory the distribution's own nginx.conf includes sites from,
    # which the nginx.org nginx.conf doesn't. RHEL's uses conf.d, as nginx.org
    # does, so it has none.
    sites_dir="" sites_glob=""

    case $os in
        debian|ubuntu)
            pkg=apt gpg_pkg=gpg ca_bundle=/etc/ssl/certs/ca-certificates.crt
            # Debian's and Ubuntu's nginx.conf include sites-enabled/*.
            sites_dir=/etc/nginx/sites-enabled sites_glob='*'
            repo_url="$pkgs_url/$os $version nginx"
            changes="$APT_KEYRING (signing keys)
/etc/apt/sources.list.d/nginx.list (repository)
/etc/apt/preferences.d/99nginx (takes nginx packages from nginx.org, and nothing else)"
            trust="apt accepts the keys for the nginx.org repository only,
and takes only nginx packages from it."
            if [ "$unattended" = 1 ]; then
                changes="$changes
$UU_CONF (lets unattended-upgrades update them)"
                updates="unattended-upgrades, if it runs here, installs every nginx release
from nginx.org, new features included."
            else
                updates="unattended-upgrades doesn't update nginx from nginx.org. Update it with:
apt-get install --only-upgrade 'nginx*'
To have it updated automatically, run the script again with --unattended-upgrades."
            fi
            ;;
        rhel|amzn)
            # Minimal images, such as UBI minimal, only have microdnf.
            pkg=dnf gpg_pkg=gnupg2 ca_bundle=/etc/pki/tls/certs/ca-bundle.crt
            if ! has dnf && has microdnf; then
                pkg=microdnf
            fi
            if [ "$os" = amzn ]; then
                rpm_path=amzn/2023
            else
                rpm_path=centos/$version
            fi
            repo_url="$pkgs_url/$rpm_path/$arch/"
            changes="$RPM_KEY (signing keys, also imported into rpm)
/etc/yum.repos.d/nginx.repo (repository)"
            trust="$pkg takes only nginx packages from this repository.
rpm accepts the keys for packages from any repository."
            updates="nginx.org publishes no security advisories, so security-only updates
skip nginx: dnf upgrade --security, and dnf-automatic with upgrade_type
set to security. Update nginx with: $pkg upgrade 'nginx*'"
            ;;
        sles)
            pkg=zypper gpg_pkg=gpg2 ca_bundle=/etc/ssl/ca-bundle.pem
            # SUSE's nginx.conf includes vhosts.d/*.conf.
            sites_dir=/etc/nginx/vhosts.d sites_glob='*.conf'
            # zypper expands $releasever_major itself, to 15 or 16. The plan
            # shows the expanded URL; add_repository passes zypper the variable.
            repo_url="$pkgs_url/sles/$version"
            zypper_url="$pkgs_url/sles/\$releasever_major"
            changes="rpm database (signing keys)
/etc/zypp/repos.d/nginx-$channel.repo (repository, added with zypper)"
            trust="This script doesn't limit the repository to nginx packages on zypper.
rpm accepts the keys for packages from any repository."
            updates="nginx.org publishes no patches, so zypper patch skips nginx.
Update nginx with: zypper update 'nginx*'"
            ;;
        alpine)
            pkg=apk ca_bundle=/etc/ssl/certs/ca-certificates.crt
            # Alpine's nginx.conf includes http.d/*.conf.
            sites_dir=/etc/nginx/http.d sites_glob='*.conf'
            key_url=$RSA_KEY_URL
            key_file=$tmp_dir/nginx_signing.rsa.pub
            repo_url="$pkgs_url/alpine/v$version/main"
            # shellcheck disable=SC2086 # $packages is a whitespace-separated list
            packages=$(for _p in $packages; do printf '%s@nginx ' "$_p"; done)
            packages=${packages% }
            changes="/etc/apk/keys/nginx_signing.rsa.pub (signing key)
/etc/apk/repositories (adds the @nginx repository)"
            trust="apk uses this repository only for packages asked for with @nginx,
and for what they depend on. It accepts the key for any repository."
            updates="apk upgrade updates nginx from nginx.org."
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

    path_nginx=$(PATH=$user_path command -v nginx 2>/dev/null) || path_nginx=""
    nginx_bin=$(find_nginx)
}

# blocking_packages: the installed packages of the distribution's nginx
# that the switch to nginx.org can't replace, one per line. check_switch
# stops on them before anything is changed.
blocking_packages() {
    case $pkg in
        # dnf can't swap the distribution's nginx for nginx.org's in one
        # install. RHEL 9 and 10 and Amazon Linux 2023 split it into
        # packages such as nginx-core and nginx-mimetypes, which conflict
        # with the nginx.org package's files, and Amazon Linux's has a
        # higher epoch. RHEL 8's nginx is replaced, but its module packages
        # stay, built for the old nginx: a configuration that loads them
        # then fails, and nginx-module-perl conflicts with their files.
        # Every nginx package not from nginx.org has to go, except
        # nginx-filesystem, which other packages, such as php-fpm, need. The
        # nginx.org package installs alongside it.
        dnf|microdnf)
            rpm -qa --qf '%{NAME}\n' 'nginx*' 2>/dev/null | sort -u |
            while IFS= read -r _p; do
                [ "$_p" = nginx-filesystem ] && continue
                rpm_from_nginx_org "$_p" || say "$_p"
            done
            ;;
        # zypper replaces SUSE's nginx, but not while one of SUSE's own
        # nginx-module-* packages, which need its exact version, is installed.
        # They share their names with nginx.org's.
        zypper)
            rpm -qa --qf '%{NAME}\n' 'nginx-module-*' 2>/dev/null | sort -u |
            while IFS= read -r _p; do
                rpm_from_nginx_org "$_p" || say "$_p"
            done
            ;;
        # Alpine's module packages, nginx-mod-*, depend on any nginx, so apk
        # keeps them, built for Alpine's nginx, and the configuration files
        # they install then stop nginx -t. nginx.org's are nginx-module-*.
        apk)
            apk info 2>/dev/null | grep -E '^nginx-mod-' || true
            ;;
    esac
}

# Stop before changing anything if the switch from the distribution's nginx
# would fail halfway or leave a broken nginx behind. A failed install undoes
# the repository changes, but a check up front also saves the downloads. apt
# replaces Debian's and Ubuntu's nginx and removes their module packages in
# one install, so it isn't checked here: distro_losses warns about those
# modules instead.
check_switch() {
    _blockers=$(blocking_packages | tr '\n' ' ' | sed 's/ $//')
    [ -n "$_blockers" ] || return 0
    case $pkg in
        apk) _remove="apk del" _asks="apk del removes them without asking." ;;
        *) _remove="$pkg remove" _asks="$pkg lists what it will remove, including any packages that need these,
and asks before it removes anything." ;;
    esac
    die "these packages of the distribution's nginx are installed, and the
switch to nginx.org can't replace them: $_blockers
Nothing was changed. To switch to nginx.org, back up /etc/nginx, remove them,
and run this script again:

    ${sudo:+$sudo }$_remove $_blockers

$_asks
nginx.org packages some of the same modules: see --module."
}

# What the switch from the distribution's nginx drops. On Debian and
# Ubuntu, its module packages, which depend on its nginx. On every system
# but RHEL, the sites in the distribution's own sites directory, which only
# its own nginx.conf includes. Runs without sudo/doas so show_plan never
# elevates before pause().
distro_losses() {
    if [ "$pkg" = apt ]; then
        _mods=$(dpkg-query -W -f '${db:Status-Abbrev} ${Package}\n' 'libnginx-mod-*' 2>/dev/null |
            sed -n 's/^.i. *//p' | tr '\n' ' ' | sed 's/ $//')
        if [ -n "$_mods" ]; then
            warn "these module packages need the distribution's nginx and will be
removed: $_mods
nginx.org packages some of the same modules: see --module."
        fi
    fi
    if [ -n "$sites_dir" ]; then
        if [ -d "$sites_dir" ] && [ ! -r "$sites_dir" ] && [ "$(id -u)" != 0 ]; then
            warn "couldn't inspect $sites_dir without root. If it has sites in it,
the nginx.org package's nginx.conf includes only /etc/nginx/conf.d/*.conf.
The script says at the end if they no longer load."
        elif [ -n "$(site_files unpriv)" ]; then
            warn "$sites_dir has sites in it. The nginx.org package's nginx.conf
includes only /etc/nginx/conf.d/*.conf. The script says at the end if they
no longer load."
        fi
    fi
}

# site_files [unpriv]: entries in $sites_dir, if it has any, other than the
# distribution's own default site left as it was installed. Losing that one
# costs nothing: Debian's and Ubuntu's is the welcome page, and Alpine's
# answers 404. With "unpriv", runs without sudo/doas.
site_files() {
    _unpriv=${1:-}
    if [ "$_unpriv" = unpriv ]; then
        ls -A "$sites_dir" 2>/dev/null
    else
        priv ls -A "$sites_dir" 2>/dev/null
    fi | while IFS= read -r _f; do
        stock_default "$sites_dir/$_f" "$_unpriv" || printf '%s\n' "$_f"
    done
}

# stock_default FILE [unpriv]: is FILE the distribution's default site,
# unchanged?
stock_default() {
    _unpriv=${2:-}
    case $1 in
        /etc/nginx/sites-enabled/default)
            # A link to the conffile dpkg installed, with dpkg's checksum.
            if [ "$_unpriv" = unpriv ]; then
                _target=$(readlink -f "$1" 2>/dev/null) || return 1
            else
                _target=$(priv readlink -f "$1" 2>/dev/null) || return 1
            fi
            [ "$_target" = /etc/nginx/sites-available/default ] || return 1
            _want=$(dpkg-query -W -f '${Conffiles}\n' nginx-common 2>/dev/null |
                sed -n 's|^ /etc/nginx/sites-available/default \([0-9a-f]*\).*|\1|p')
            if [ "$_unpriv" = unpriv ]; then
                _got=$(md5sum "$_target" 2>/dev/null | cut -d' ' -f1)
            else
                _got=$(priv md5sum "$_target" 2>/dev/null | cut -d' ' -f1)
            fi
            [ -n "$_want" ] && [ "$_got" = "$_want" ]
            ;;
        /etc/nginx/http.d/default.conf)
            # Alpine's package writes this file from a script, so apk keeps
            # no checksum for it. This is the SHA-256 it has on Alpine 3.22
            # to 3.24.
            if [ "$_unpriv" = unpriv ]; then
                _got=$(sha256sum "$1" 2>/dev/null | cut -d' ' -f1)
            else
                _got=$(priv sha256sum "$1" 2>/dev/null | cut -d' ' -f1)
            fi
            [ "$_got" = f53fd49af9b4bc308653abb85d9989879ce1fb48e43c508f5f45c84f74513865 ]
            ;;
        *) return 1 ;;
    esac
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
        field "" "installs only these keys, the current ones:"
        # shellcheck disable=SC2086 # $PGP_FINGERPRINTS is a word list
        for _fpr in $PGP_FINGERPRINTS; do
            field "" "  $(spaced "$_fpr")"
        done
        field "" "and stops if the file holds a key it doesn't know"
    fi
    field "Prerequisites" "${prereqs:-none needed}"
    field_lines "Changes" "$changes"
    field "" "a file that already exists is kept, the first time, as FILE.bak"
    field "" "if the install fails, these changes are undone"
    field_lines "Trust" "$trust"
    field "" "This stays after the install. The summary at the end says how to undo it."
    field_lines "Updates" "$updates"
    field "Runs as" "$run_as"

    if [ -z "$modules" ]; then
        printf '\nDynamic modules ship as separate packages. Add any of\n%s\nwith --module NAME.\n' \
            "$MODULES"
    fi
    check_switch
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
            distro_losses
        fi
    fi
    if [ -n "$path_nginx" ]; then
        _canon_path=$(readlink -f "$path_nginx" 2>/dev/null || printf '%s' "$path_nginx")
        _canon_bin=$(readlink -f "$NGINX_BIN" 2>/dev/null || printf '%s' "$NGINX_BIN")
        if [ "$_canon_path" != "$_canon_bin" ]; then
            echo
            # Don't run it to ask for its version. It is whatever comes first in
            # PATH, and this script may be running as root.
            warn "another nginx is installed at $path_nginx.
The nginx.org packages install $NGINX_BIN and leave that one in place, but it
comes first in your PATH. Run $NGINX_BIN to use the packaged nginx."
        fi
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
    # shellcheck disable=SC2086 # $prereqs is a whitespace-separated package list
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

# gpg with a private, empty home directory: no user keyring, no user or system
# options.
tmp_gpg() { gpg --homedir "$tmp_dir/gnupg" --no-options --batch "$@"; }

# key_entries: read gpg --with-colons output on stdin and print one
# FINGERPRINT:VALIDITY line per primary key whose fpr line immediately follows
# its pub line. sed, not awk, because SUSE's minimal images don't have awk.
key_entries() {
    sed -n '/^pub:/{h;n;G;s/^fpr:\{9\}\([0-9A-F]*\):.*\npub:\([^:]*\):.*/\1:\2/p;}'
}

# key_invalid_reason VALIDITY: name why gpg considers a key unusable, or
# nothing if it is usable.
key_invalid_reason() {
    case $1 in
        r) printf '%s' revoked ;;
        e) printf '%s' expired ;;
        i) printf '%s' invalid ;;
        d) printf '%s' disabled ;;
    esac
}

# Check the downloaded key file, then write clean, minimal copies of the
# accepted keys:
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
    _found=$(printf '%s\n' "$_keys" | key_entries)
    [ -n "$_found" ] || die "$PGP_KEY_URL doesn't contain any PGP keys."
    # key_entries lists a key only when its fpr line comes right after its
    # pub line. A key with a designated revoker has an rvk line in between,
    # and a secret key has a sec line instead, so either would skip the
    # fingerprint check below. Such a key can carry a copy of a pinned key as
    # a subkey, and the export further down would then take it along. Stop
    # unless every key in the file was listed.
    [ "$(printf '%s\n' "$_keys" | grep -c -E '^(pub|sec):')" = \
      "$(printf '%s\n' "$_found" | grep -c .)" ] ||
        die "$PGP_KEY_URL contains a key this script can't check.
Nothing was installed from nginx.org."
    # Stop before importing if the download holds any unknown key.
    # shellcheck disable=SC2086 # $_found is newline-separated FPR:VAL entries
    for _key in $_found; do
        _fpr=${_key%%:*}
        contains "$_fpr" "$PGP_RETIRED" && continue
        contains "$_fpr" "$PGP_FINGERPRINTS" || die "$PGP_KEY_URL contains a key this script doesn't know:
    $(spaced "$_fpr")
Nothing was installed from nginx.org. Check $PACKAGES_DOC, and get a newer
copy of this script if nginx has added a signing key."
    done

    # Import into the private keyring before checking validity. --show-keys
    # evaluates each block separately, whereas --import merges duplicate blocks
    # and standalone revocation certificates so a revoked key can't slip past
    # via an earlier unrevoked block in the same file. The import may complain
    # about a key that was left out, so the checks that count run on the
    # imported keyring and again after export.
    tmp_gpg --quiet --import "$_download" >/dev/null 2>&1 || true
    _imported=$(tmp_gpg --with-colons --list-keys 2>/dev/null) ||
        die "gpg couldn't list the imported keys from $PGP_KEY_URL.
Nothing was installed from nginx.org."
    _merged=$(printf '%s\n' "$_imported" | key_entries)
    [ -n "$_merged" ] || die "$PGP_KEY_URL has no importable PGP keys.
Nothing was installed from nginx.org."
    [ "$(printf '%s\n' "$_imported" | grep -c -E '^(pub|sec):')" = \
      "$(printf '%s\n' "$_merged" | grep -c .)" ] ||
        die "$PGP_KEY_URL contains a key this script can't check.
Nothing was installed from nginx.org."

    _good=""
    # shellcheck disable=SC2086 # $_merged is newline-separated FPR:VAL entries
    for _key in $_merged; do
        _fpr=${_key%%:*}
        contains "$_fpr" "$PGP_RETIRED" && continue  # known, not installed
        contains "$_fpr" "$PGP_FINGERPRINTS" || die "$PGP_KEY_URL contains a key this script doesn't know:
    $(spaced "$_fpr")
Nothing was installed from nginx.org. Check $PACKAGES_DOC, and get a newer
copy of this script if nginx has added a signing key."
        # A pinned key that nginx has revoked, or that has expired, is left
        # out and not trusted. The fingerprint alone doesn't show either, and
        # older versions of rpm don't check for them. Packages signed with
        # such a key then fail the package manager's signature check.
        _why=$(key_invalid_reason "${_key#*:}")
        if [ -n "$_why" ]; then
            warn "left out $(spaced "$_fpr"): the key is $_why."
        elif ! contains "$_fpr" "$_good"; then
            _good="${_good:+$_good }$_fpr"
            say "  ${green}ok${reset}  $(spaced "$_fpr")"
        fi
    done
    [ -n "$_good" ] || die "$PGP_KEY_URL has no key that is still valid.
Nothing was installed from nginx.org."

    # Write clean, minimal copies: export only the accepted keys from gpg's own
    # keyring. export-minimal strips third-party signatures (preventing
    # certificate flooding), and exporting from the keyring drops unbound
    # subkeys.
    # One armor block per key. rpm imports each block of a file, but older
    # versions may read only the first key inside a block.
    : >"$key_file"
    # shellcheck disable=SC2086 # $_good is a whitespace-separated fingerprint list
    for _fpr in $_good; do
        tmp_gpg --armor --export-options export-minimal --export "$_fpr" >>"$key_file" 2>/dev/null ||
            die "gpg couldn't export $(spaced "$_fpr")."
    done
    # shellcheck disable=SC2086 # $_good is a whitespace-separated fingerprint list
    tmp_gpg --yes --export-options export-minimal --output "$tmp_dir/nginx-archive-keyring.gpg" --export $_good 2>/dev/null ||
        die "gpg couldn't export the accepted keys."
    # Both copies must hold exactly the accepted, valid keys.
    for _file in "$key_file" "$tmp_dir/nginx-archive-keyring.gpg"; do
        _out=$(tmp_gpg --with-colons --show-keys "$_file" 2>/dev/null) ||
            die "internal error: gpg couldn't read the cleaned key file."
        _clean_entries=$(printf '%s\n' "$_out" | key_entries)
        # As above: a key that key_entries can't list isn't checked.
        [ "$(printf '%s\n' "$_out" | grep -c -E '^(pub|sec):')" = \
          "$(printf '%s\n' "$_clean_entries" | grep -c .)" ] ||
            die "internal error: the cleaned key file holds a key this script can't check."
        _clean=""
        # shellcheck disable=SC2086 # $_clean_entries is newline-separated FPR:VAL entries
        for _entry in $_clean_entries; do
            _fpr=${_entry%%:*}
            _why=$(key_invalid_reason "${_entry#*:}")
            [ -z "$_why" ] ||
                die "internal error: the cleaned key file holds a $_why key $(spaced "$_fpr")."
            contains "$_fpr" "$_good" ||
                die "internal error: the cleaned key file holds the unexpected key $(spaced "$_fpr")."
            _clean="${_clean:+$_clean }$_fpr"
        done
        # shellcheck disable=SC2086 # $_good is a whitespace-separated fingerprint list
        for _fpr in $_good; do
            contains "$_fpr" "$_clean" ||
                die "internal error: $(spaced "$_fpr") is missing from the cleaned key file."
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
    # gpgcheck checks each package's signature. repo_gpgcheck also checks the
    # signature nginx.org puts on the repository metadata, so a mirror or
    # proxy can't hold back updates or point at other packages unnoticed.
    # sslverify=1 overrides any host-level sslverify=0 in dnf.conf.
    printf 'gpgcheck=1\nrepo_gpgcheck=1\nsslverify=1\nenabled=%s\ngpgkey=file://%s\nmodule_hotfixes=true\n' \
        "$_enabled" "$RPM_KEY"
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
    # Create a root-owned rollback directory directly under /tmp before
    # changing any package source. From here until the installed packages are
    # verified from nginx.org, a failure undoes these changes. See rollback.
    if [ "$dry_run" != 1 ]; then
        rollback_dir=$(as_root mktemp -d /tmp/nginx-install.XXXXXX) ||
            die "couldn't create a temporary directory for rollback."
        as_root chmod 700 "$rollback_dir"
        as_root mkdir -m 700 "$rollback_dir/saved" ||
            die "couldn't create $rollback_dir/saved."
        as_root touch "$rollback_dir/saved.list" "$rollback_dir/new.list" ||
            die "couldn't initialize rollback lists."
        rollback_armed=1
    fi
    case $pkg in
        apt)
            # verify_pgp_key wrote this keyring. In a dry run it may be
            # missing, because the key wasn't downloaded.
            save_file "$APT_KEYRING"
            run install -m 644 "$tmp_dir/nginx-archive-keyring.gpg" "$APT_KEYRING"
            echo "deb [signed-by=$APT_KEYRING] $repo_url" |
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
            # Only with --unattended-upgrades. site matches the host name in
            # the repository URL, like the pin. unattended-upgrades and apt
            # ignore this file until unattended-upgrades is installed.
            # Without the option, a file from an earlier run stays: removing
            # it would turn off updates someone chose.
            if [ "$unattended" = 1 ]; then
                printf '%s\n' '// Added by the get.nginx.org installer: install nginx updates from nginx.org.' \
                    'Unattended-Upgrade::Origins-Pattern:: "site=nginx.org";' |
                    write_file "$UU_CONF"
            fi
            ;;
        dnf|microdnf)
            note_rpm_keys
            save_file "$RPM_KEY"
            run install -m 644 "$key_file" "$RPM_KEY"
            run rpm --import "$RPM_KEY"
            { yum_repo stable; echo; yum_repo mainline; } |
                write_file /etc/yum.repos.d/nginx.repo
            ;;
        zypper)
            note_rpm_keys
            run rpm --import "$key_file"
            # zypper usually writes /etc/zypp/repos.d/ALIAS.repo, but a
            # pre-existing nginx-stable or nginx-mainline alias may live in
            # another .repo file. Save both standard paths and any .repo file
            # defining those sections before removerepo modifies them.
            save_file /etc/zypp/repos.d/nginx-stable.repo
            save_file /etc/zypp/repos.d/nginx-mainline.repo
            if [ "$dry_run" != 1 ]; then
                # shellcheck disable=SC2016 # $_f is expanded by the inner shell
                as_root sh -c 'for _f in /etc/zypp/repos.d/*.repo; do [ -e "$_f" ] && printf "%s\n" "$_f"; done' |
                while IFS= read -r _f; do
                    if as_root grep -q -E '^\[(nginx-stable|nginx-mainline)\][[:space:]]*$' "$_f" 2>/dev/null; then
                        save_file "$_f"
                    fi
                done
            fi
            # Check as root: the user may not be able to read the repository
            # files, and would miss one that addrepo then finds.
            for _repo in nginx-stable nginx-mainline; do
                if priv zypper --non-interactive repos "$_repo" >/dev/null 2>&1; then
                    run zypper --non-interactive removerepo "$_repo"
                fi
            done
            # --gpgcheck-strict sets both repo_gpgcheck=1 and pkg_gpgcheck=1
            # explicitly in the .repo file regardless of /etc/zypp/zypp.conf.
            run zypper --non-interactive addrepo --gpgcheck-strict --type yum --refresh --check \
                "$zypper_url" "nginx-$channel"
            ;;
        apk)
            save_file /etc/apk/keys/nginx_signing.rsa.pub
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

# Enabled nginx.org package sources other than the one this script wrote (and
# on Alpine, any conflicting @nginx tag), one per line with the file it is in.
# With one for the other branch still enabled, the package manager can keep or
# pick that branch, and a switch with --stable can't take effect. apt lists
# only fetched sources, so call this after apt-get update.
other_nginx_sources() {
    case $pkg in
        apt)
            # shellcheck disable=SC2016 # $(SITE) and the rest are apt's fields
            as_root env LC_ALL=C apt-get indextargets --format '$(SITE) $(RELEASE) $(SOURCESENTRY)' 2>/dev/null |
                grep -E '^https?://nginx\.org/packages/' | sort -u |
                grep -v " /etc/apt/sources.list.d/nginx.list:" || true
            ;;
        dnf|microdnf)
            # Enabled sections with an nginx.org URL, in dnf.conf, yum.conf,
            # or any .repo file but the one this script writes. Enumerate as
            # root in case /etc/yum.repos.d isn't readable by the user. Each
            # section is collected in hold space as its header, plus a URL=
            # line for an nginx.org URL (including indented continuations) and
            # an enabled=0 line if it's off. A section with no enabled line is
            # enabled. dnf reads 0, no, off, and false, in any case, as off.
            # shellcheck disable=SC2016 # $_f is expanded by the inner shell
            as_root sh -c 'for _f in /etc/yum.repos.d/*.repo /etc/dnf/dnf.conf /etc/yum.conf; do [ -e "$_f" ] && printf "%s\n" "$_f"; done' |
            while IFS= read -r _f; do
                [ "$_f" = /etc/yum.repos.d/nginx.repo ] && continue
                as_root cat "$_f" 2>/dev/null | sed -n '
                    /^\[/ { x; /\nURL=/ { /\nenabled=0/!p; }; s/.*//; x; h; d; }
                    /^[[:space:]]*\(baseurl\|mirrorlist\|metalink\)[[:space:]]*=.*nginx\.org\/packages\// { s/.*/URL=/; H; }
                    /^[[:space:]][[:space:]]*https\{0,1\}:\/\/nginx\.org\/packages\// { s/.*/URL=/; H; }
                    /^[[:space:]]*enabled[[:space:]]*=[[:space:]]*\(0\|[Nn][Oo]\|[Oo][Ff][Ff]\|[Ff][Aa][Ll][Ss][Ee]\)[[:space:]]*$/ { s/.*/enabled=0/; H; }
                    $ { x; /\nURL=/ { /\nenabled=0/!p; }; }' |
                    sed -n 's|^\[\([^]]*\)\].*|\1|p' |
                    while IFS= read -r _sec; do
                        printf '%s in %s\n' "$_sec" "$_f"
                    done
            done
            ;;
        zypper)
            # The columns are # | Alias | Name | Enabled | GPG Check |
            # Refresh | URI. Run as root in case repo files aren't readable by
            # the user. sed, not awk, because SUSE's minimal images lack awk.
            as_root env LC_ALL=C zypper -q --no-refresh repos -u -E 2>/dev/null |
                sed -n 's/^[^|]*|[[:space:]]*\([^|[:space:]]*\)[[:space:]]*|.*|[[:space:]]*\(https\{0,1\}:\/\/nginx\.org\/packages\/[^[:space:]]*\).*/\1 \2/p' |
                grep -v -E "^nginx-$channel " || true
            ;;
        apk)
            # Any nginx.org entry other than the exact @nginx line this script
            # wrote, plus any pre-existing @nginx tag pointing to another host
            # (which apk add nginx@nginx would also pull from), in
            # /etc/apk/repositories or /etc/apk/repositories.d/*.list.
            as_root cat /etc/apk/repositories 2>/dev/null |
            while IFS= read -r _line; do
                _clean_line=${_line%%#*}
                case $_clean_line in
                    "@nginx $repo_url") continue ;;
                esac
                if printf '%s\n' "$_clean_line" |
                    grep -q -E '(https?://nginx\.org/packages/|^[[:space:]]*@nginx[[:space:]])'; then
                    printf '%s in /etc/apk/repositories\n' "$_line"
                fi
            done
            # shellcheck disable=SC2016 # $_f is expanded by the inner shell
            as_root sh -c 'for _f in /etc/apk/repositories.d/*.list; do [ -e "$_f" ] && printf "%s\n" "$_f"; done' |
            while IFS= read -r _f; do
                as_root cat "$_f" 2>/dev/null |
                while IFS= read -r _line; do
                    _clean_line=${_line%%#*}
                    if printf '%s\n' "$_clean_line" |
                        grep -q -E '(https?://nginx\.org/packages/|^[[:space:]]*@nginx[[:space:]])'; then
                        printf '%s in %s\n' "$_line" "$_f"
                    fi
                done
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
The package manager could install or keep the other branch from it. Disable
or remove it, then run this script again. Nothing was installed from
nginx.org."
}

install_packages() {
    step "Installing $packages"
    # shellcheck disable=SC2086 # $packages is a whitespace-separated package list
    case $pkg in
        apt)
            # apt-get update exits 0 when an index fails to download. If the
            # nginx.org one did, the install would use the distro's nginx
            # without an error, so stop instead.
            apt_get -o APT::Update::Error-Mode=any update ||
                die "apt couldn't download every package index. Check the errors above.
Any failing repository stops the install here, including one that has
nothing to do with nginx. Fix or disable it, then run this script again.
Nothing was installed from nginx.org."
            check_other_sources
            # Keep any existing configuration files rather than prompting.
            # Without recommends, as for the prerequisites: through logrotate's
            # own recommends, a minimal Debian 13 otherwise gets 44 packages,
            # among them systemd, cron, and the exim4 mail server. logrotate
            # is the nginx package's only recommendation, and it rotates
            # nginx's logs, so it is asked for by name.
            apt_get -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold \
                install --no-install-recommends $packages logrotate
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
            run apk add --no-cache --upgrade $packages
            # apk treats a switch from Alpine's nginx as an upgrade, so it
            # runs the nginx.org package's post-upgrade script, not the
            # post-install one that creates the temporary files directory
            # the nginx.org build uses. Without it, nginx -t fails.
            if [ "$dry_run" = 1 ] || [ ! -d /var/cache/nginx ]; then
                run mkdir -p -m 755 /var/cache/nginx
            fi
            ;;
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
    esac | grep -E '^nginx(-module-[a-z0-9-]+)?$' | tr '\n' ' ' | sed 's/ $//'
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
                sed -n 's/^hi. *//p' | grep -E '^nginx(-module-[a-z0-9-]+)?$' | tr '\n' ' ' | sed 's/ $//')
            if [ -n "$_held" ]; then
                printf '%sapt-mark unhold %s\n    ' "$_sudo" "$_held"
            fi
            printf '%sapt-get install --allow-downgrades' "$_sudo"
            # shellcheck disable=SC2086 # $_pkgs is a whitespace-separated package list
            for _p in $_pkgs; do printf ' %s/%s' "$_p" "$version"; done
            ;;
        dnf|microdnf) printf '%s%s distro-sync %s' "$_sudo" "$pkg" "$_pkgs" ;;
        zypper) printf '%szypper install --oldpackage --from nginx-stable %s' "$_sudo" "$_pkgs" ;;
        apk) printf '%sapk upgrade --available %s' "$_sudo" "$_pkgs" ;;
    esac
}

# rpm_from_nginx_org PKG: was the installed rpm package PKG signed by
# nginx.org? This tells nginx.org's packages apart from the distribution's.
# It isn't a trust decision, so the retired 2011 key counts too: it signed
# the nginx.org packages installed before 2025.
rpm_from_nginx_org() {
    # Query RSA, DSA/Ed25519/ECDSA (DSAHEADER), and legacy payload signature
    # tags. Missing tags print "(none)" and are dropped by the sed below.
    # A key ID is the last 16 hex digits of the key's fingerprint.
    _keyids=$(LC_ALL=C rpm -q --qf '%{RSAHEADER:pgpsig}\n%{DSAHEADER:pgpsig}\n%{SIGPGP:pgpsig}\n%{SIGGPG:pgpsig}\n' "$1" 2>/dev/null |
        sed -n 's/.*Key ID \([0-9a-fA-F]\{16\}\).*/\1/p' | tr 'a-f' 'A-F')
    # shellcheck disable=SC2086 # $_keyids and fingerprint lists are word lists
    for _keyid in $_keyids; do
        for _fpr in $PGP_FINGERPRINTS $PGP_RETIRED; do
            if [ "${_fpr#????????????????????????}" = "$_keyid" ]; then
                return 0
            fi
        done
    done
    return 1
}

# from_nginx_org [PKG]: did the installed package PKG (default: nginx) come
# from nginx.org? On RPM systems, check the key the package was signed with.
# apt and apk have already checked the signature against the pinned key, so
# check the packager, which also tells nginx.org packages apart from distro
# ones.
from_nginx_org() {
    _pkg=${1:-nginx}
    case $pkg in
        apt)
            [ "$(dpkg-query -W -f '${Maintainer}' "$_pkg" 2>/dev/null)" = "$NGINX_MAINTAINER" ]
            ;;
        dnf|microdnf|zypper)
            rpm_from_nginx_org "$_pkg"
            ;;
        apk)
            sed -n "/^P:$_pkg\$/,/^$/p" /lib/apk/db/installed 2>/dev/null |
                grep -qxF "m:$NGINX_MAINTAINER"
            ;;
    esac
}

# offered_by_nginx_org [PKG]: is the exact version of PKG (default: nginx)
# that is installed one that the nginx.org repository offers? from_nginx_org
# reads fields of the installed package, which a package built elsewhere can
# copy, and a package manager keeps an installed version that is newer than
# any it is offered. This asks the package manager, which checked the
# repository's signature against the pinned keys, so only a version nginx.org
# published passes. Call it once the nginx.org repository is set up. The
# queries run as root, since the repository files and caches may not be
# readable by the user.
offered_by_nginx_org() {
    _pkg=${1:-nginx}
    case $pkg in
        apt)
            # The lines under the installed version (***) name the sources
            # that offer it. Strip the header line and any following version
            # header line (without dropping the last origin line if *** is the
            # last version in the table).
            as_root env LC_ALL=C apt-cache policy "$_pkg" 2>/dev/null |
                sed -n '/^ \*\*\* /,/^     [^ ]/p' | sed '1d;/^     [^ ]/d' |
                grep -q -E ' https://nginx\.org/packages/'
            ;;
        # Either of this script's repositories: finish checks the branch
        # separately, so a mainline build left after --stable gets the
        # downgrade advice rather than this error.
        dnf)
            as_root env LC_ALL=C dnf -q repoquery --qf '%{repoid}' "$(rpm -q "$_pkg")" 2>/dev/null |
                grep -q -x -E 'nginx-(stable|mainline)'
            ;;
        microdnf)
            # Restrict the query to nginx.org's enabled repository so another
            # enabled repository offering the same NEVRA can't pass.
            [ -n "$(as_root microdnf --disablerepo='*' --enablerepo="nginx-$channel" repoquery "$(rpm -q "$_pkg")" 2>/dev/null)" ]
            ;;
        zypper)
            _v=$(rpm -q --qf '%{VERSION}-%{RELEASE}' "$_pkg" 2>/dev/null)
            [ -n "$_v" ] && as_root env LC_ALL=C zypper -q --no-refresh search -s -x -t package "$_pkg" 2>/dev/null |
                grep -F "| $_v " | grep -q -E '\| nginx-(stable|mainline)[[:space:]]*$'
            ;;
        apk)
            # --no-cache fetches the indexes, which apk 2 doesn't keep after an
            # install with --no-cache.
            _v=$(as_root apk info -e -v "$_pkg" 2>/dev/null | sed "s/^$_pkg-//")
            _v_re=$(printf '%s' "$_v" | sed 's/[.[\*^$]/\\&/g')
            [ -n "$_v" ] && as_root apk --no-cache policy "$_pkg" 2>/dev/null |
                sed -n "/^  $_v_re:\$/,/^  [^ ]/p" | grep -q -E '^    @nginx https://nginx\.org/packages/'
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
    from_nginx_org nginx || die "$_installed is installed, but not from nginx.org.
The package manager kept or chose another repository's nginx package. Check
the output above for download errors, and that nginx.org has packages for
$os_name on $arch.
If the other package has a higher version or epoch, the package manager won't
replace it on its own: back up /etc/nginx, remove that package, then run this
script again."

    # Stable releases have an even minor version, mainline an odd one. An odd
    # one after a stable install means a newer mainline build was kept. The
    # stable repository intentionally stays set up for the downgrade command
    # below, so disarm rollback only for this exit.
    _minor=$(printf '%s\n' "$_installed" | sed -n 's|^nginx/[0-9]*\.\([0-9]*\)\..*|\1|p')
    if [ "$channel" = stable ]; then
        case $_minor in
            *[13579])
                rollback_armed=0
                die "$_installed from the mainline branch is still installed.
The stable repository is set up, but the package manager won't downgrade
nginx on its own. Check that your configuration doesn't use features newer
than stable, then run:

    $(downgrade_command)

If a hold or lock keeps nginx on its version, remove it first. Or stay on
mainline by running this script again without --stable."
                ;;
        esac
    fi

    # The checks above read the installed package's own fields. These confirm
    # that nginx.org published this exact version of nginx and any requested
    # dynamic modules. Rollback stays armed until every check passes.
    offered_by_nginx_org nginx || die "$_installed is installed and looks like an nginx.org package, but
the nginx.org repository doesn't offer this version. It may be a local
rebuild, or a newer version that the package manager kept. Back up
/etc/nginx, remove the nginx package, then run this script again."

    # shellcheck disable=SC2086 # $modules is a whitespace-separated word list
    for _m in $modules; do
        _mpkg="nginx-module-$_m"
        from_nginx_org "$_mpkg" || die "$_mpkg is installed, but not from nginx.org.
Remove it and run this script again."
        offered_by_nginx_org "$_mpkg" || die "$_mpkg is installed and looks like an nginx.org package, but
the nginx.org repository doesn't offer this version. Remove it and run this
script again."
    done

    # nginx and all requested modules are verified from nginx.org. The
    # repository now keeps them up to date, so it stays from here on.
    rollback_armed=0

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
    field_lines "Updates" "$updates"
    # The packages to name again on Alpine, without the @nginx tag.
    _undo_pkgs=""
    [ "$pkg" = apk ] && _undo_pkgs=$(installed_packages)
    field_lines "Undo" "To remove the nginx.org repository and keys, and keep nginx:
$(undo_commands "$pkg" "${sudo:+$sudo }" "$_undo_pkgs")"

    if [ "$_config_ok" = 0 ]; then
        echo
        warn "nginx is installed, but 'nginx -t' rejected the configuration in
/etc/nginx. See its messages above. nginx won't start or reload until the
configuration is fixed. An nginx that is already running keeps its old
configuration until then."
    elif [ -n "$sites_dir" ] && [ -n "$(site_files)" ] &&
        ! as_root "$NGINX_BIN" -T 2>/dev/null | grep -q "^# configuration file $sites_dir/"; then
        # nginx -T names every file the configuration includes. A running
        # nginx keeps serving these sites until its next reload or restart,
        # which is why nginx -t above passed without a word.
        echo
        warn "the sites in $sites_dir no longer load: the nginx.conf in
place includes only /etc/nginx/conf.d/*.conf. A running nginx drops them at
its next reload or restart. Before that, add this line to the http block of
/etc/nginx/nginx.conf:

    include $sites_dir/$sites_glob;"
    fi
}

# shellcheck disable=SC2329 # called from the EXIT trap in main
cleanup() {
    # Ignore further signals so a second Ctrl+C or SSH drop can't interrupt
    # rollback or leave temporary directories behind.
    trap '' INT TERM HUP
    if [ "$rollback_armed" = 1 ]; then
        rollback
    fi
    # gpg may have started helper daemons for its private home directory.
    if [ -d "$tmp_dir/gnupg" ] && has gpgconf; then
        gpgconf --homedir "$tmp_dir/gnupg" --kill all >/dev/null 2>&1 || true
    fi
    if [ -n "${rollback_dir:-}" ]; then
        as_root rm -rf "$rollback_dir" || true
    fi
    rm -rf "$tmp_dir"
}

main() {
    umask 022
    user_path=${PATH:-}
    PATH=/usr/sbin:/usr/bin:/sbin:/bin
    export PATH

    channel=mainline channel_set="" modules="" dry_run=0 assume_yes=0 key_verified=0
    unattended=0 rollback_armed=0 rollback_dir="" path_nginx=""
    setup_colors
    parse_args "$@"
    detect_os
    check_privileges

    tmp_dir=$(mktemp -d)
    trap cleanup EXIT
    trap 'exit 129' HUP
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
