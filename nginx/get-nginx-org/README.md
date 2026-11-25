# get.nginx.org NGINX Open Source installer

## Overview

[`nginx.sh`](nginx.sh) installs NGINX Open Source with one command. It will be served as plain text at `https://get.nginx.org/nginx`. That site isn't live yet, so for now get the script from this repository (see [Get the script from GitHub](#get-the-script-from-github)).

Keep `-q` first and `--proto '=https'` in every command: `-q` ignores `~/.curlrc` (where an `insecure` line would disable TLS verification) and `--proto '=https'` makes curl refuse plain HTTP, also after a redirect. Pipe into `sh`, not `sudo sh`; the script uses `sudo` or `doas` itself for the commands that need root.

Installing from the nginx.org packages by hand means following the [Linux packages](https://nginx.org/en/linux_packages.html) page: install prerequisites, import and check the signing key, add the repository, and pin it. The script runs those steps for the host's OS in about a minute, plus download time.

[`index.txt`](index.txt) is the plain text page for `https://get.nginx.org/`, with the commands and options. Its title line, `get.nginx.org (one-line installer for NGINX Open Source)`, is a shell syntax error, so piping the page into `sh` by mistake stops on the first line and runs none of the commands on it. Keep the parentheses in that line. A title without them, such as one with a dash, runs as a command, and the shell then goes on to run the install commands on the page.

## Get the script from GitHub

Until `get.nginx.org` is live, get the script from this repository. Every command in this README works with `https://get.nginx.org/nginx` replaced by:

```text
https://raw.githubusercontent.com/nginx/nginx-demos/main/nginx/get-nginx-org/nginx.sh
```

To install the mainline branch:

```sh
curl -q --proto '=https' --tlsv1.2 -fsSL https://raw.githubusercontent.com/nginx/nginx-demos/main/nginx/get-nginx-org/nginx.sh | sh
```

To pass options, add `-s --` after `sh`. For example, to install the stable branch with the njs module:

```sh
curl -q --proto '=https' --tlsv1.2 -fsSL https://raw.githubusercontent.com/nginx/nginx-demos/main/nginx/get-nginx-org/nginx.sh | sh -s -- --stable --module njs
```

To read the script before running it:

```sh
curl -q --proto '=https' --tlsv1.2 -fsSL https://raw.githubusercontent.com/nginx/nginx-demos/main/nginx/get-nginx-org/nginx.sh -o install-nginx.sh
less install-nginx.sh
sh install-nginx.sh --dry-run
sh install-nginx.sh
```

The `main` URL serves the latest version, though GitHub can take a few minutes to serve a change after it's merged. In a Dockerfile or CI job, use a fixed version instead: replace `main` with a commit hash from this repository's history, and check the SHA-256 of the file as shown under [Usage](#usage).

## What the installer does

1. Detects the OS from `/etc/os-release` and the architecture from the package manager (`dpkg`, `rpm`, or `apk`), and stops on anything nginx.org doesn't package for. It doesn't rely on `uname -m`, which reports `x86_64` inside a 32-bit userspace on a 64-bit kernel.
2. Prints the repository, signing keys, packages, and files it will change, then waits 10 seconds with a warning about running scripts piped from `curl`. The plan also says how nginx gets updates from then on (see [Security updates](#security-updates)).
   - It stops here, before changing anything, if packages of the distribution's nginx are installed that the switch to nginx.org can't replace. The error gives the command that removes them:
     - RHEL, its derivatives, and Amazon Linux: every nginx package not from nginx.org, except `nginx-filesystem`, which other packages such as `php-fpm` need. RHEL 9 and 10 and Amazon Linux 2023 split their nginx into packages such as `nginx-core` and `nginx-mimetypes`, which conflict with the nginx.org package's files. RHEL 8 replaces its nginx, but keeps its module packages, built for the old nginx: a configuration that loads them then fails.
     - SLES and openSUSE Leap: the distribution's own `nginx-module-*` packages, which need its exact nginx version.
     - Alpine: Alpine's `nginx-mod-*` packages, which apk would keep, built for Alpine's nginx.
   - On Debian and Ubuntu, it warns if the distribution's nginx module packages (`libnginx-mod-*`) will be removed with it. apt replaces them in the same install.
   - It warns if the distribution's own sites directory has sites in it: `/etc/nginx/sites-enabled` on Debian and Ubuntu, `/etc/nginx/vhosts.d` on SLES and openSUSE, and `/etc/nginx/http.d` on Alpine. The nginx.org `nginx.conf` includes only `/etc/nginx/conf.d`, which RHEL's also uses. The distribution's default site, left as it was installed, doesn't count.
3. Installs the prerequisites it's missing: `curl`, `gpg` (or `openssl` on Alpine), and CA certificates.
4. Downloads the nginx signing key (capped at 1 MiB) and stops unless every key in it is one the script knows: the two 2024 keys, or the 2011 key. It installs only the 2024 keys. The 2011 key signs nothing nginx.org publishes now. It also stops if gpg lists a key it couldn't check, such as one with a designated revoker, whose fingerprint gpg prints in a different place. It imports the download into a private gpg home so duplicate blocks and standalone revocation certificates are merged before checking validity, leaves out revoked and expired keys, and stops if no valid key is left. The downloaded file itself is never installed. The system gets minimal copies (`export-minimal`) of the accepted keys, so unbound subkeys and third-party signatures stay out of its keyring. On Alpine, the script checks the RSA key's SHA-256, which covers both the modulus and the exponent.
5. Adds the nginx.org repository for the chosen branch, limited to nginx packages: on Debian and Ubuntu, a pin gives `nginx` and `nginx-*` from nginx.org priority 900 and everything else from it -1, and dnf gets `includepkgs=nginx*`, `priority=9`, and `sslverify=1`. dnf also checks the signature on the repository metadata (`repo_gpgcheck=1`), as apt and zypper (`--gpgcheck-strict`) do. On Alpine, the script first removes every earlier `@nginx` entry for nginx.org, over `http` or `https`. A repository file the script replaces is kept, the first time, as `FILE.bak`. If the file is a symlink, the script writes its target (provided the target's parent directory is owned by root and not writable by group or others) and keeps the link. A file that already exists keeps its read and owner-write permissions, and loses group/other write, execute, and special bits.
6. Stops, before installing anything, if another enabled package source serves nginx.org packages (or on Alpine, if another `@nginx` repository tag is configured): another apt source, `.repo` file or `dnf.conf`/`yum.conf` section, zypper repository, extra line in `/etc/apk/repositories`, or entry in `/etc/apk/repositories.d`. The error names it. With one for the other branch still enabled, the package manager could keep or pick that branch.
7. Installs `nginx` and any requested dynamic modules. On Alpine, apk treats a switch from Alpine's own nginx as an upgrade and skips the nginx.org package's install script, so the script creates the `/var/cache/nginx` directory (`0755`) that script would have. On Debian and Ubuntu, it installs them without recommended packages, which on a minimal Debian would otherwise bring in systemd, cron, and the exim4 mail server, and asks for `logrotate`, nginx's only recommendation, by name. It stops if any package index fails to download. `apt-get update` succeeds in that case, and the install would use the distro's nginx without an error.
8. Checks that the installed `nginx` package and any requested `nginx-module-*` packages came from nginx.org: on RPM systems by the key that signed them, and on apt and apk systems (which have already checked the signature) by their packager. It then asks the package manager whether the nginx.org repository offers those exact versions. That catches a local rebuild that copies nginx.org's fields, and a newer version the package manager kept. It checks the package's own `/usr/sbin/nginx`, not whichever `nginx` comes first in `PATH`, and doesn't run that one.
9. Runs `nginx -t`, and warns at the end if the configuration in `/etc/nginx` doesn't load in the new build. The packages are installed either way, so the script still exits 0. If the configuration loads but no longer includes the sites in that directory, as happens when the nginx.org package replaces the distribution's `nginx.conf`, it warns too, and gives the `include` line to add. A running nginx keeps serving those sites until its next reload.
10. Prints how to start nginx, how it gets updates, and the commands that remove the nginx.org repository and keys on that system.

If anything fails, or the run is cancelled, between step 5 and the check in step 8 that the installed nginx came from nginx.org, the script undoes its changes to the package sources: it removes the files it added, puts back the ones it replaced, and removes the rpm keys it imported. A failed install, such as a module that nginx.org doesn't build for that system and branch, can't leave a repository behind that later upgrades take nginx from without these checks. Once nginx from nginx.org is installed, the repository stays: it's what keeps nginx up to date. The one exception is the move from mainline to stable, which stops with the command to run but keeps the stable repository, since that command needs it.

The nginx.org repository and its keys stay on the system after a successful install. The end of the run, and `--help`, list what to remove to undo that.

### Security updates

Switching to the nginx.org packages changes how nginx gets security fixes:

- **Debian and Ubuntu:** `unattended-upgrades` only installs updates from the origins it's told about, by default the distribution's own. Ubuntu Server runs it out of the box, so after the switch nginx no longer gets updates on its own. Update it with `apt-get install --only-upgrade 'nginx*'`. To have `unattended-upgrades` update it, run the script with `--unattended-upgrades`. That writes `/etc/apt/apt.conf.d/52nginx-unattended-upgrades`, which adds nginx.org to its origins. It's off by default because `unattended-upgrades` then installs every nginx.org release with no one watching, including new mainline features, not only security fixes. The apt pin still keeps that origin to nginx packages. A later run without the option leaves the file in place; to turn it off, delete it.
- **RHEL, derivatives, and Amazon Linux:** nginx.org publishes no security advisories (`updateinfo`), so `dnf upgrade --security` and `dnf-automatic` with `upgrade_type = security` skip nginx. Use a full `dnf upgrade`, or schedule `dnf upgrade 'nginx*'`.
- **SLES and openSUSE Leap:** for the same reason, `zypper patch` skips nginx. Use `zypper update 'nginx*'`.
- **Alpine:** `apk upgrade` updates nginx from nginx.org.

The script is POSIX `sh`, so it runs on Alpine and other systems without `bash`. All of its code runs from a `main` function called inside a `{ ... }` block on the last line. If the download is cut short anywhere, including partway through that line, the shell fails to parse the script and runs nothing.

## Usage

Pass options after `sh -s --`:

```sh
curl -q --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx | sh -s -- --stable --module njs
```

|Option|Description|
|------|-----------|
|`--mainline`|Install the mainline branch (default)|
|`--stable`|Install the stable branch|
|`--module NAME`|Also install `nginx-module-NAME`. Repeat for more modules: `acme`, `geoip`, `image-filter`, `njs`, `otel`, `perl`, `xslt`. Not every module is built for every system and branch: on the stable branch, RHEL, Amazon Linux, and SLES 16 have no `geoip`, and the install fails|
|`--unattended-upgrades`|Debian and Ubuntu: let `unattended-upgrades` install nginx updates from nginx.org. Off by default (see [Security updates](#security-updates))|
|`-n`, `--dry-run`|Show the plan and the commands without changing anything|
|`-y`, `--yes`|Don't pause before making changes|
|`-h`, `--help`|Show the options and supported systems|

The script runs as root, or uses `sudo` or `doas` if it isn't.

Running the script again upgrades nginx and its modules to the latest version on the chosen branch, including from stable to mainline. Running it again with `--stable` on a host with mainline installed switches the repository to stable, but package managers don't downgrade on their own. Rather than downgrade nginx unattended, which can break a configuration that uses mainline features, the script stops with an error and prints the command for that system, for example:

```sh
apt-get install --allow-downgrades nginx/noble nginx-module-njs/noble
dnf distro-sync nginx nginx-module-njs
```

On Debian and Ubuntu, if the nginx packages are held, the command starts with `apt-mark unhold`. On other systems, remove any lock on nginx first, such as a `dnf versionlock` or `zypper addlock`.

To read the script before running it:

```sh
curl -q --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx -o install-nginx.sh
less install-nginx.sh
sh install-nginx.sh --dry-run
sh install-nginx.sh
```

In a Dockerfile or CI job, don't pipe from the network on every build. Keep a copy you have read, check its SHA-256 on every build, and run it with `--yes`, replacing `HASH` with the output of `sha256sum install-nginx.sh`:

```sh
echo "HASH  install-nginx.sh" | sha256sum -c - && sh install-nginx.sh --yes
```

## Supported systems

The systems listed on the [Linux packages](https://nginx.org/en/linux_packages.html#distributions) page, on x86_64 and aarch64:

|Distribution|Versions|
|------------|--------|
|RHEL and derivatives (CentOS Stream, Rocky Linux, AlmaLinux, Oracle Linux)|8, 9, 10|
|Debian|12 (bookworm), 13 (trixie)|
|Ubuntu|22.04 (jammy), 24.04 (noble), 26.04 (resolute)|
|SLES|15 SP7 and later (x86_64 only), 16|
|openSUSE Leap|16|
|Alpine|3.22, 3.23, 3.24|
|Amazon Linux|2023|

## Maintenance

When nginx.org adds a distribution version, add it to the `SUPPORTED_*` lists at the top of [`nginx.sh`](nginx.sh), and update the tables in this README and [`index.txt`](index.txt). When a version reaches its end of life, remove it from the lists, the same way, even if nginx.org still lists it. Either way, set `LISTS_CHECKED` to the date you compared them with the [Linux packages](https://nginx.org/en/linux_packages.html#distributions) page. The script's "unsupported system" error names that date.

When nginx.org adds a key to `nginx_signing.key`, the script stops with the new fingerprint until it's added to `PGP_FINGERPRINTS`. Check the new key against the [Linux packages](https://nginx.org/en/linux_packages.html) page and the signatures of the existing keys before adding it. `PGP_RETIRED` holds the 2011 key, which the script knows but doesn't install. If the Alpine key changes, check its modulus against that page, then set `RSA_KEY_SHA256` to the output of `openssl pkey -pubin -in nginx_signing.rsa.pub -outform DER | openssl dgst -sha256`.

When nginx.org ships a new dynamic module, add it to `MODULES`. If it isn't built for every system and branch, say so in `--help`, this README, and [`index.txt`](index.txt).

## License

The installer is released under the same 2-clause BSD license as nginx, which [`nginx.sh`](nginx.sh) carries in its header. It's served on its own, so it carries its own license rather than this repository's.

## Testing

Run [ShellCheck](https://www.shellcheck.net/) against the script:

```sh
docker run --rm -v "$PWD/nginx.sh:/nginx.sh:ro" koalaman/shellcheck:stable --shell=sh /nginx.sh
```

Then install in a container for each supported system, piping the script into `sh` the way users will:

```sh
docker run --rm -v "$PWD/nginx.sh:/nginx.sh:ro" rockylinux:9 \
  sh -c 'cat /nginx.sh | sh -s -- -y --module njs && nginx -V'
```
