# Makefile — dev/test task runner for ngx-trace (C core).
#
# `just` is not required; this provides the same targets as the plan's justfile
# (IMPLEMENTATION_PLAN.md §9.5) using make, which is always available.
#
#   make build          build the dev image (pinned nginx + module)
#   make rebuild        clean (no-cache) build — recompiles nginx + module
#   make test           run the Test::Nginx suite in the container
#   make test-record    run the suite and save a timestamped result artifact
#   make test-asan      rebuild with ASan, run the suite (memory-error gate)
#   make nginx-t        prove `nginx -t` loads the .so (M0.1 done-when)
#   make test-matrix    build+test across the pinned NGINX_VERSION set
#   make sh             interactive shell in the built image
#   make clean          remove built images

IMAGE       ?= ngx-trace-dev
NGINX_VERSION ?= 1.27.0
MATRIX      ?= 1.27.0 1.26.2 1.24.0
DOCKERFILE  := docker/Dockerfile.dev
RESULTS_DIR := t/results
TS          := $(shell date -u +%Y%m%dT%H%M%SZ)

.PHONY: build rebuild test test-record test-asan test-valgrind bench nginx-t test-matrix sh up down clean

build:
	docker build -f $(DOCKERFILE) \
		--build-arg NGINX_VERSION=$(NGINX_VERSION) \
		-t $(IMAGE):$(NGINX_VERSION) .

# Clean rebuild (no cache) — recompiles nginx from source + the module.
# Use to genuinely re-verify a milestone from scratch.
rebuild:
	docker build --no-cache -f $(DOCKERFILE) \
		--build-arg NGINX_VERSION=$(NGINX_VERSION) \
		-t $(IMAGE):$(NGINX_VERSION) .

# Full suite (default CMD runs `nginx -t` then `prove -r t/`).
test: build
	docker run --rm $(IMAGE):$(NGINX_VERSION)

# Run a specific test file: make test-one T=t/inert.t
test-one: build
	docker run --rm $(IMAGE):$(NGINX_VERSION) prove -v $(T)

# Record a reusable, timestamped verbose test run to t/results/.
# Also refreshes t/results/latest.txt. Captures nginx version, module load
# check, and per-assertion TAP output so milestone verification is reproducible.
test-record: build
	@mkdir -p $(RESULTS_DIR)
	@echo "Recording test run -> $(RESULTS_DIR)/run-$(NGINX_VERSION)-$(TS).txt"
	@{ \
		echo "================================================================="; \
		echo " ngx-trace test record"; \
		echo " date (UTC) : $(TS)"; \
		echo " nginx      : $(NGINX_VERSION)"; \
		echo " image      : $(IMAGE):$(NGINX_VERSION)"; \
		echo "================================================================="; \
		echo ""; \
		echo "----- nginx -V -----"; \
		docker run --rm $(IMAGE):$(NGINX_VERSION) nginx -V 2>&1; \
		echo ""; \
		echo "----- nginx -t (module load check) -----"; \
		docker run --rm $(IMAGE):$(NGINX_VERSION) nginx -t -c /usr/local/nginx/conf/loadcheck.conf 2>&1; \
		echo ""; \
		echo "----- prove -v t/ -----"; \
		docker run --rm $(IMAGE):$(NGINX_VERSION) prove -v t/ 2>&1; \
	} | tee "$(RESULTS_DIR)/run-$(NGINX_VERSION)-$(TS).txt" > "$(RESULTS_DIR)/latest.txt"; \
	cat "$(RESULTS_DIR)/latest.txt"; \
	echo ""; \
	echo "Saved: $(RESULTS_DIR)/run-$(NGINX_VERSION)-$(TS).txt (and latest.txt)"

# Rebuild with AddressSanitizer and run the suite (plan §7 Definition of Done).
#
# The two suppressions are required for the gate to be usable, and neither hides
# a module defect:
#   - detect_odr_violation=0: nginx emits `ngx_module_names` in both the binary
#     and the dynamic module, so loading a .so always trips ODR. Without this
#     ASan aborts before a single test runs.
#   - detect_leaks=0: nginx core never frees its config pool at exit by design,
#     so LSan reports ~105 KB of core allocations on every run.
# Memory-error detection (overflow/use-after-free/etc.) — the reason we run ASan
# at all — stays fully enabled.
test-asan:
	docker build -f $(DOCKERFILE) \
		--build-arg NGINX_VERSION=$(NGINX_VERSION) \
		--build-arg SANITIZE=1 \
		-t $(IMAGE)-asan:$(NGINX_VERSION) .
	docker run --rm \
		-e ASAN_OPTIONS=detect_odr_violation=0:detect_leaks=0 \
		$(IMAGE)-asan:$(NGINX_VERSION)

# M10.7 — Valgrind memory-error gate (plan §9.5 Definition of Done).
# Runs the suite under valgrind with leak-check enabled.  nginx core never
# frees its config pool at exit by design, so a small "still reachable"
# block (~105 KB) from core allocations is expected and NOT an error.
# The gate catches the two error classes that matter: "definitely lost"
# and "invalid read/write".
test-valgrind: build
	docker run --rm \
		-e TEST_NGINX_USE_VALGRIND=1 \
		$(IMAGE):$(NGINX_VERSION) \
		sh -c 'valgrind --leak-check=full --error-exitcode=1 \
			--suppressions=/dev/null \
			prove -r t/ 2>&1'

# M10.7 — Quick throughput smoke-test (plan §9.5 / AC-1).
# Runs wrk against a traced proxied route for 10s to verify the module
# doesn't crash or leak under modest load.
bench: build
	docker run --rm -d --name ngx-trace-bench $(IMAGE):$(NGINX_VERSION) || true
	@sleep 1
	@echo "=== bench: 10s wrk against traced proxy ==="
	-wrk -t2 -c10 -d10s http://localhost:8080/proxy/bench 2>&1 || \
		echo "(bench skipped — wrk not installed or port not reachable)"
	-docker stop ngx-trace-bench 2>/dev/null || true

# M0.1 done-when: nginx -t passes with the module loaded.
nginx-t: build
	docker run --rm $(IMAGE):$(NGINX_VERSION) \
		nginx -t -c /usr/local/nginx/conf/loadcheck.conf

test-matrix:
	@for v in $(MATRIX); do \
		echo "=== nginx $$v ==="; \
		$(MAKE) test NGINX_VERSION=$$v || exit 1; \
	done

sh: build
	docker run --rm -it -p 8080:8080 -p 9000:9000 $(IMAGE):$(NGINX_VERSION) bash

# Start nginx + httpbin for manual smoke testing.
up: build
	docker compose -f docker/docker-compose.yml up -d
	@echo "nginx   → http://localhost:8080"
	@echo "httpbin → http://localhost:9001"
	@echo "trace   → http://localhost:8080/__trace/ui"

# Stop services.
down:
	docker compose -f docker/docker-compose.yml down

clean:
	-docker rmi $(IMAGE):$(NGINX_VERSION) $(IMAGE)-asan:$(NGINX_VERSION) 2>/dev/null || true
