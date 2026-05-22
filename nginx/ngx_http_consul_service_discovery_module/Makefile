# Makefile — ngx_http_consul_service_discovery
#
# Usage:
#   make static  NGINX_SRC=/path/to/nginx  — static build (linked into nginx binary)
#   make dynamic NGINX_SRC=/path/to/nginx  — dynamic (.so) build
#   make test    NGINX_SRC=/path/to/nginx  — build + run smoke tests
#   make clean   NGINX_SRC=/path/to/nginx  — remove build artefacts

NGINX_SRC  ?= /usr/local/src/nginx
MODULE_DIR := $(shell pwd)

.PHONY: all static dynamic test clean

all: dynamic

static:
	@echo "==> Configuring static module into NGINX at $(NGINX_SRC)"
	cd $(NGINX_SRC) && ./configure \
		--with-http_ssl_module \
		--with-http_v2_module \
		--add-module=$(MODULE_DIR)
	$(MAKE) -C $(NGINX_SRC)

dynamic:
	@echo "==> Building dynamic module (NGINX at $(NGINX_SRC))"
	cd $(NGINX_SRC) && ./configure \
		--with-compat \
		--with-http_ssl_module \
		--with-http_v2_module \
		--add-dynamic-module=$(MODULE_DIR)
	$(MAKE) -C $(NGINX_SRC) modules
	@echo ""
	@echo "==> Module built: $(NGINX_SRC)/objs/ngx_http_consul_service_discovery_module.so"

test: static
	bash $(MODULE_DIR)/t/smoke.sh \
		$(NGINX_SRC)/objs/nginx \
		$(MODULE_DIR)/t/nginx.conf

clean:
	$(MAKE) -C $(NGINX_SRC) clean 2>/dev/null || true
