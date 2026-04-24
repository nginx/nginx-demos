# Makefile — ngx_stream_radius_module
#
# Usage:
#   make NGINX_SRC=/path/to/nginx          — static build
#   make dynamic NGINX_SRC=/path/to/nginx  — dynamic (.so) build
#   make test                              — run unit tests

NGINX_SRC   ?= /usr/local/src/nginx
MODULE_DIR  := $(shell pwd)

.PHONY: all static dynamic test clean

all: dynamic

static:
	@echo "==> Configuring static module into NGINX at $(NGINX_SRC)"
	cd $(NGINX_SRC) && ./configure \
		--with-stream \
		--with-stream_ssl_module \
		--add-module=$(MODULE_DIR)
	$(MAKE) -C $(NGINX_SRC)

dynamic:
	@echo "==> Building dynamic module (NGINX at $(NGINX_SRC))"
	cd $(NGINX_SRC) && ./configure \
		--with-stream \
		--with-compat \
		--add-dynamic-module=$(MODULE_DIR)
	$(MAKE) -C $(NGINX_SRC) modules
	@echo ""
	@echo "==> Module built: $(NGINX_SRC)/objs/ngx_stream_radius_module.so"

test:
	$(MAKE) -C tests

clean:
	$(MAKE) -C $(NGINX_SRC) clean 2>/dev/null || true
	$(MAKE) -C tests clean 2>/dev/null || true
