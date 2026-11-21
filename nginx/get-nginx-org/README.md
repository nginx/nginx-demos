# get.nginx.org NGINX Open Source installer

## Overview

A one-line installer for NGINX Open Source, meant to be served as plain text at `https://get.nginx.org/nginx`:

```sh
curl --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx | sh
```

Keep `--proto '=https'`: it makes curl refuse plain HTTP, also after a redirect. Pipe into `sh`, not `sudo sh`; the script uses `sudo` itself for the commands that need root.

Installing NGINX Open Source from the nginx.org packages means following the [Linux packages](https://nginx.org/en/linux_packages.html) page: install prerequisites, import and check the signing key by hand, add the repository, and pin it. [`nginx.sh`](nginx.sh) runs those steps for the host's OS in about a minute, plus download time.

* [`nginx.sh`](nginx.sh) is the installer, served at `https://get.nginx.org/nginx`.
* [`index.txt`](index.txt) is the plain text page for `https://get.nginx.org/`, listing the available installers.

## What the installer does

1. Detects the OS from `/etc/os-release` and the architecture from the package manager (`dpkg`, `rpm`, or `apk`), and stops on anything nginx.org doesn't package for. It doesn't rely on `uname -m`, which reports `x86_64` inside a 32-bit userspace on a 64-bit kernel.
2. Prints the repository, signing keys, packages, and files it will change, then waits 10 seconds with a warning about running scripts piped from `curl`.
3. Installs the prerequisites it's missing: `curl`, `gpg` (or `openssl` on Alpine), and CA certificates.
4. Downloads the nginx signing key and stops unless every key in it matches a fingerprint pinned in the script. Revoked and expired keys are left out, and the script stops if no valid key is left. It never installs the download itself: it imports it into a private gpg home and exports clean copies of the accepted keys, so nothing else in the file, such as a subkey with no binding signature, reaches the system's keyring. On Alpine, it checks the RSA key's SHA-256, which covers both the modulus and the exponent.
5. Adds the nginx.org repository for the chosen branch, limited to nginx packages: on Debian and Ubuntu, a pin gives `nginx` and `nginx-*` from nginx.org priority 900 and everything else from it -1, and dnf gets `includepkgs=nginx*` and `priority=9`. A repository file the script replaces is kept, the first time, as `FILE.bak`. If the file is a symlink, the script writes its target and keeps the link and the target's permissions. On Alpine, it removes every earlier `@nginx` entry for nginx.org, over `http` or `https`, before adding the chosen branch.
6. Stops, before installing anything, if another enabled package source serves nginx.org packages: another apt source, `.repo` file, zypper repository, or entry in `/etc/apk/repositories.d`. The error names it. With one for the other branch still enabled, the package manager could keep or pick that branch.
7. Installs `nginx` and any requested dynamic modules. On Debian and Ubuntu, it stops if any package index fails to download, since `apt-get update` otherwise succeeds and the install would quietly use the distro's nginx.
8. Checks that the installed `nginx` package came from nginx.org: on RPM systems by the key that signed it, and on apt and apk systems (which have already checked the signature) by its packager. It then asks the package manager whether the nginx.org repository offers that exact version, so a local rebuild that copies nginx.org's fields, or a newer version the package manager kept, doesn't pass. It checks the package's own `/usr/sbin/nginx`, not whichever `nginx` comes first in `PATH`, and doesn't run that one.
9. Runs `nginx -t`, and warns at the end if the configuration in `/etc/nginx` doesn't load in the new build. The packages are installed either way, so the script still exits 0.

The nginx.org repository and its keys stay on the system after the install. `--help` lists what to remove to undo that.

The script is POSIX `sh`, so it runs on Alpine and other systems without `bash`. All of its code runs from a `main` function called inside a `{ ... }` block on the last line. If the download is cut short anywhere, including partway through that line, the shell fails to parse the script and runs nothing.

## Usage

Pass options after `sh -s --`:

```sh
curl --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx | sh -s -- --stable --module njs
```

|Option|Description|
|------|-----------|
|`--mainline`|Install the mainline branch (default)|
|`--stable`|Install the stable branch|
|`--module NAME`|Also install `nginx-module-NAME`. Repeat for more modules: `acme`, `geoip`, `image-filter`, `njs`, `otel`, `perl`, `xslt`|
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
curl --proto '=https' --tlsv1.2 -fsSL https://get.nginx.org/nginx -o install-nginx.sh
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
|SLES and openSUSE Leap|15 SP7 and later (x86_64 only), 16|
|Alpine|3.21, 3.22, 3.23, 3.24|
|Amazon Linux|2023|

nginx.org lists SLES 15 SP6, but its current SLES 15 packages need OpenSSL 3.2 and SP6 ships 3.1.4, so neither branch installs there. The script requires SP7 or later.

## Maintenance

* **New distribution versions:** add them to the `SUPPORTED_*` lists at the top of [`nginx.sh`](nginx.sh), set `LISTS_CHECKED` to the date you compared them with the [Linux packages](https://nginx.org/en/linux_packages.html#distributions) page, and update the tables in this README and [`index.txt`](index.txt). The script's "unsupported system" error names that date.
* **New signing keys:** if nginx.org adds a key to `nginx_signing.key`, the script stops with the new fingerprint until it's added to `PGP_FINGERPRINTS`. Check the new key against the [Linux packages](https://nginx.org/en/linux_packages.html) page and the signatures of the existing keys before adding it. If the Alpine key changes, check its modulus against that page, then set `RSA_KEY_SHA256` to the output of `openssl pkey -pubin -in nginx_signing.rsa.pub -outform DER | openssl dgst -sha256`.
* **New dynamic modules:** add them to `MODULES`.

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
