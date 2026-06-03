NGINX_SRC ?= /usr/src/nginx
MODULE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

.PHONY: static dynamic test clean

## Build a static NGINX binary with the module compiled in
static:
	cd $(NGINX_SRC) && \
	  ./configure \
	    --with-compat \
	    --with-http_ssl_module \
	    --with-http_v2_module \
	    --add-module=$(MODULE_DIR) && \
	  make -j$$(nproc)

## Build a dynamic .so  (default target)
dynamic:
	cd $(NGINX_SRC) && \
	  ./configure \
	    --with-compat \
	    --with-http_ssl_module \
	    --with-http_v2_module \
	    --add-dynamic-module=$(MODULE_DIR) && \
	  make modules -j$$(nproc)

## Build dynamic module, then run smoke tests
test: dynamic
	bash $(MODULE_DIR)/t/smoke.sh $(NGINX_SRC)

## Remove build artefacts under NGINX_SRC
clean:
	cd $(NGINX_SRC) && make clean
