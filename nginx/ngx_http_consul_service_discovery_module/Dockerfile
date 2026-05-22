# syntax=docker/dockerfile:1
#
# Multi-stage build: compiles NGINX with the consul-service-discovery module
# and packages a minimal Debian runtime image.
#
# Build:
#   docker build --build-arg NGINX_VER=1.26.1 -t ngx-consul-discovery .
#
# Run:
#   docker run --rm -p 8080:8080 ngx-consul-discovery
#   curl http://localhost:8080/consul/services

ARG NGINX_VER=1.26.1

# ── Stage 1: builder ──────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder
ARG NGINX_VER

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential libpcre3-dev zlib1g-dev libssl-dev curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

RUN curl -fsSLo nginx.tar.gz \
        "https://nginx.org/download/nginx-${NGINX_VER}.tar.gz" \
    && tar -xzf nginx.tar.gz && rm nginx.tar.gz

COPY config    /build/module/config
COPY src/      /build/module/src/

RUN cd nginx-${NGINX_VER} && \
    ./configure \
        --prefix=/etc/nginx \
        --sbin-path=/usr/sbin/nginx \
        --conf-path=/etc/nginx/nginx.conf \
        --error-log-path=/var/log/nginx/error.log \
        --http-log-path=/var/log/nginx/access.log \
        --pid-path=/var/run/nginx.pid \
        --with-http_ssl_module \
        --with-http_v2_module \
        --add-module=/build/module \
        --with-cc-opt='-O2 -pipe' && \
    make -j"$(nproc)" && \
    make install

# ── Stage 2: runtime ──────────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        libpcre3 zlib1g libssl3 \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /var/log/nginx /var/cache/nginx

COPY --from=builder /usr/sbin/nginx   /usr/sbin/nginx
COPY --from=builder /etc/nginx        /etc/nginx
COPY examples/example.conf            /etc/nginx/nginx.conf

EXPOSE 8080
STOPSIGNAL SIGQUIT
CMD ["nginx", "-g", "daemon off;"]
