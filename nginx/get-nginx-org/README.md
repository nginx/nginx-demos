# get.nginx.org NGINX Open Source installer

## Overview

A one-line installer for NGINX Open Source, meant to be served as plain text at `https://get.nginx.org/nginx`:

```sh
curl -fsSL https://get.nginx.org/nginx | sh
```

Installing NGINX Open Source from the nginx.org packages means following the [Linux packages](https://nginx.org/en/linux_packages.html) page: install prerequisites, import and check the signing key by hand, add the repository, and pin it. [`nginx.sh`](nginx.sh) runs those steps for the host's OS in about a minute, plus download time.

* [`nginx.sh`](nginx.sh) is the installer, served at `https://get.nginx.org/nginx`.
* [`index.txt`](index.txt) is the plain text page for `https://get.nginx.org/`, listing the available installers.

## What the installer does

1. Detects the OS and architecture from `/etc/os-release` and `uname -m`, and stops on anything nginx.org doesn't package for.
2. Prints the repository, signing keys, packages, and files it will change, then waits 10 seconds with a warning about running scripts piped from `curl`.
3. Installs the prerequisites it's missing: `curl`, `gpg` (or `openssl` on Alpine), and CA certificates.
4. Downloads the nginx signing key and stops unless every key in it matches a fingerprint pinned in the script. On Alpine, it checks the RSA key's modulus.
5. Adds the nginx.org repository for the chosen branch, the same way the Linux packages page does.
6. Installs `nginx` and any requested dynamic modules.

The script is POSIX `sh`, so it runs on Alpine and other systems without `bash`. All of its code runs from a `main` function called on the last line, so a download that's cut short runs nothing.

## Usage

Pass options after `sh -s --`:

```sh
curl -fsSL https://get.nginx.org/nginx | sh -s -- --stable --module njs
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

To read the script before running it:

```sh
curl -fsSL https://get.nginx.org/nginx -o install-nginx.sh
less install-nginx.sh
sh install-nginx.sh --dry-run
sh install-nginx.sh
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

* **New distribution versions:** add them to the `SUPPORTED_*` lists at the top of [`nginx.sh`](nginx.sh) and to the tables in this README and [`index.txt`](index.txt).
* **New signing keys:** if nginx.org adds a key to `nginx_signing.key`, the script stops with the new fingerprint until it's added to `PGP_FINGERPRINTS`. Check the new key against the [Linux packages](https://nginx.org/en/linux_packages.html) page and the signatures of the existing keys before adding it.
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
