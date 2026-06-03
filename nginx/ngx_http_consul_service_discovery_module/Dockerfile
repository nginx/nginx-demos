FROM nginx:1.26-alpine AS build

RUN apk add --no-cache \
        gcc musl-dev make pcre-dev openssl-dev zlib-dev linux-headers

# Download NGINX source matching the installed binary
RUN NGINX_VER=$(nginx -v 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+') && \
    wget -q "https://nginx.org/download/nginx-${NGINX_VER}.tar.gz" -O /tmp/nginx.tar.gz && \
    tar -xzf /tmp/nginx.tar.gz -C /tmp

COPY . /tmp/ngx_http_consul_service_discovery

RUN NGINX_VER=$(nginx -v 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+') && \
    cd /tmp/nginx-${NGINX_VER} && \
    ./configure \
        --with-compat \
        --with-http_ssl_module \
        --with-http_v2_module \
        --add-dynamic-module=/tmp/ngx_http_consul_service_discovery && \
    make modules -j$(nproc) && \
    cp objs/ngx_http_consul_service_discovery_module.so /etc/nginx/modules/

# ── Final image ──────────────────────────────────────────────────────────
FROM nginx:1.26-alpine

COPY --from=build /etc/nginx/modules/ngx_http_consul_service_discovery_module.so \
                  /etc/nginx/modules/

COPY examples/nginx.conf /etc/nginx/nginx.conf

EXPOSE 8080
CMD ["nginx", "-g", "daemon off;"]
