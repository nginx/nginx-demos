# Building ngx_stream_radius_module

## Prerequisites

| Requirement | Minimum Version | Notes |
|---|---|---|
| GCC or Clang | GCC 4.8 / Clang 3.5 | C99 required |
| NGINX source | 1.11.5 | Same version as running binary for dynamic builds |
| PCRE | 8.x or 2.x | For NGINX stream regex maps |
| OpenSSL | 1.0.2 | For `--with-stream_ssl_module` |
| zlib | 1.x | Standard NGINX dependency |

## Operating Systems

Tested on:
- Ubuntu 20.04, 22.04, 24.04
- Debian 11, 12
- RHEL / CentOS / AlmaLinux 8, 9
- FreeBSD 13, 14

## Dynamic Module Build (Recommended)

Dynamic modules are `.so` files loaded at runtime with `load_module`. They do not require recompiling NGINX itself.

### Step 1: Match NGINX version

```bash
# Find the exact version of the installed NGINX
nginx -v
# Example output: nginx version: nginx/1.24.0
```

### Step 2: Download matching NGINX source

```bash
NGINX_VER=1.24.0  # match your installed version
wget https://nginx.org/download/nginx-${NGINX_VER}.tar.gz
tar xzf nginx-${NGINX_VER}.tar.gz
```

### Step 3: Reproduce the original configure flags

This is critical — the module must be compiled with the same flags as the running NGINX.

```bash
# Find original configure arguments
nginx -V 2>&1 | grep "configure arguments"

# Run the same configure, adding --with-compat and the module
cd nginx-${NGINX_VER}
./configure \
    <original-flags-here> \
    --with-compat \
    --add-dynamic-module=/path/to/ngx_stream_radius_module
```

Alternatively, for a minimal build:

```bash
./configure \
    --with-compat \
    --with-stream \
    --with-stream_ssl_module \
    --add-dynamic-module=/path/to/ngx_stream_radius_module
```

### Step 4: Build the module only

```bash
make modules
# Produces: objs/ngx_stream_radius_module.so
```

### Step 5: Install

```bash
# Debian/Ubuntu
sudo cp objs/ngx_stream_radius_module.so /usr/lib/nginx/modules/

# RHEL/CentOS
sudo cp objs/ngx_stream_radius_module.so /etc/nginx/modules/

# Custom NGINX install
sudo cp objs/ngx_stream_radius_module.so /usr/local/nginx/modules/
```

### Step 6: Load in nginx.conf

```nginx
# At the very top of nginx.conf, before any blocks
load_module modules/ngx_stream_radius_module.so;
```

## Static Module Build

Static modules are compiled directly into the NGINX binary.

```bash
cd nginx-${NGINX_VER}
./configure \
    --with-stream \
    --with-stream_ssl_module \
    --add-module=/path/to/ngx_stream_radius_module
make
sudo make install
```

No `load_module` directive is needed for static builds.

## NGINX Plus Dynamic Module

NGINX Plus binary modules require compilation against the open-source compatibility layer with the exact same version.

```bash
# Find NGINX Plus version (maps to OSS version)
nginx -v
# nginx version: nginx/1.25.3 (nginx-plus-r31)

# Use the OSS version number for the source download
NGINX_VER=1.25.3
wget https://nginx.org/download/nginx-${NGINX_VER}.tar.gz
tar xzf nginx-${NGINX_VER}.tar.gz

cd nginx-${NGINX_VER}
./configure \
    --with-compat \
    --with-stream \
    --add-dynamic-module=/path/to/ngx_stream_radius_module

make modules
sudo cp objs/ngx_stream_radius_module.so /etc/nginx/modules/
```

## Distribution Packages

### Ubuntu / Debian (nginx from official repo)

```bash
# Install build dependencies
sudo apt-get install -y \
    nginx \
    nginx-dev \
    build-essential \
    libpcre3-dev \
    libssl-dev \
    zlib1g-dev

# Get source matching installed version
apt-get source nginx

# Build module
cd nginx-*/
./configure \
    --with-compat \
    --with-stream \
    --add-dynamic-module=/path/to/ngx_stream_radius_module
make modules
```

### RHEL / CentOS / AlmaLinux

```bash
sudo yum install -y nginx nginx-devel gcc make pcre-devel openssl-devel zlib-devel

# Find source RPM
yumdownloader --source nginx
rpm2cpio nginx-*.src.rpm | cpio -idmv
tar xzf nginx-*.tar.gz

cd nginx-*/
./configure \
    --with-compat \
    --with-stream \
    --add-dynamic-module=/path/to/ngx_stream_radius_module
make modules
```

## Verifying the Build

```bash
# Test configuration syntax
nginx -t

# Check module is loaded
nginx -T 2>/dev/null | grep radius

# Run unit tests (no NGINX required)
cd tests && make
```

## Troubleshooting

### `ngx_stream_module.h: No such file`

The stream module headers are missing. Ensure `--with-stream` is in the configure flags.

### `API version mismatch`

The module `.so` was compiled against a different NGINX version than what is installed. Rebuild the module against the exact installed version (`nginx -v`).

### `unknown directive "radius_parse"`

The module is not loaded. Check:
1. The `load_module` line is present and correct
2. `nginx -T` shows the module is loaded
3. The `.so` file exists and is readable by nginx

### `cannot open shared object`

Check the module path and permissions:
```bash
ls -la $(nginx -V 2>&1 | grep -oP -- '--modules-path=\K\S+')
```
