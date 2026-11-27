# =============================================================================
# Dockerfile — ngx_http_greylist_module
#
# Multi-stage build:
#   builder  — compiles the dynamic module against nginx source
#   runtime  — minimal nginx image with the compiled .so
#
# Usage:
#   docker build -t nginx-greylist .
#   docker run -p 80:80 nginx-greylist
#
# NGINX_VER is the NGINX source the module is built against. It must match the
# runtime image below (--with-compat modules only load into the same version),
# so change both together.
# =============================================================================

ARG NGINX_VER=1.27.4

# ── Stage 1: builder ──────────────────────────────────────────────────────────
FROM debian:bookworm-slim@sha256:e67eeead736ff7bbe258ef088bff683635c561b1fcfefad0806c1df4ab49ce0e AS builder

ARG NGINX_VER

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        libpcre3-dev    \
        libssl-dev      \
        zlib1g-dev      \
        wget            \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

RUN wget -q "https://nginx.org/download/nginx-${NGINX_VER}.tar.gz" \
    && tar -xzf "nginx-${NGINX_VER}.tar.gz"

COPY config ./nginx-greylist-module/config
COPY src/   ./nginx-greylist-module/src/

RUN cd nginx-${NGINX_VER} \
    && ./configure                                            \
          --with-compat                                       \
          --with-http_ssl_module                              \
          --with-http_v2_module                               \
          --add-dynamic-module=/build/nginx-greylist-module  \
    && make modules -j"$(nproc)"                              \
    && cp objs/ngx_http_greylist_module.so /build/

# ── Stage 2: runtime ──────────────────────────────────────────────────────────
FROM nginx:1.27.4@sha256:09369da6b10306312cd908661320086bf87fbae1b6b0c49a1f50ba531fef2eab

COPY --from=builder /build/ngx_http_greylist_module.so \
                    /usr/lib/nginx/modules/ngx_http_greylist_module.so

COPY nginx/nginx.conf  /etc/nginx/nginx.conf
COPY nginx/conf.d/     /etc/nginx/conf.d/

EXPOSE 80 443
CMD ["nginx", "-g", "daemon off;"]
