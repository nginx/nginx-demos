FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV NGINX_VERSION=1.31.2

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    libpcre3-dev \
    zlib1g-dev \
    libssl-dev \
    wget \
    curl \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src

# Download and unpack NGINX source
RUN wget http://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz && \
    tar -xzf nginx-${NGINX_VERSION}.tar.gz

# Copy module source
COPY . /usr/src/ngx_stream_dns_module

# 1. Build and run pure C unit tests
WORKDIR /usr/src/ngx_stream_dns_module
RUN gcc -O2 -Wall -Wextra -Werror -I./src \
    src/ngx_dns_parser.c test/test_dns_parser.c \
    -o test_dns_parser && \
    ./test_dns_parser

# 2. Compile NGINX with Stream module enabled and dynamic DNS module
WORKDIR /usr/src/nginx-${NGINX_VERSION}
RUN ./configure \
    --prefix=/etc/nginx \
    --sbin-path=/usr/sbin/nginx \
    --modules-path=/usr/lib/nginx/modules \
    --conf-path=/etc/nginx/nginx.conf \
    --error-log-path=/var/log/nginx/error.log \
    --pid-path=/var/run/nginx.pid \
    --with-stream \
    --with-stream_ssl_module \
    --add-dynamic-module=/usr/src/ngx_stream_dns_module

RUN make && make install

# Configure NGINX Stream block for integration testing
RUN echo " \
load_module /usr/lib/nginx/modules/ngx_stream_dns_module.so; \
 \
events { \
    worker_connections 1024; \
} \
 \
stream { \
    log_format dns_log escape=none '$remote_addr [$time_local] $protocol ' \
                      'qname="$dns_request_name" qtype="$dns_request_type" ' \
                      'rcode="$dns_response_rcode_str" ips="$dns_response_ips"'; \
 \
    server { \
        listen 5353 udp; \
        listen 5353; \
 \
        dns_parse on; \
        proxy_responses 1; \
        proxy_pass 8.8.8.8:53; \
    } \
}" > /etc/nginx/nginx.conf

EXPOSE 5353/udp 5353/tcp

#CMD ["sh", "-c", "nginx && python3 /usr/src/ngx_stream_dns_module/test/test_nginx.py 127.0.0.1 5353"]
CMD ["nginx", "-g", "daemon off;"]
