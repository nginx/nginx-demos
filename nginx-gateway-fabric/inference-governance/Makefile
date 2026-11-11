# Every target runs inside the lab container (tools/toolbox), so the host needs only Docker, make
# and git. The lab is built and started on first use.
include versions.env
export
TOOLBOX := tools/toolbox

.PHONY: help
help: ## List targets
	@grep -hE '^[a-zA-Z_-]+:.*## ' $(MAKEFILE_LIST) | awk 'BEGIN{FS=":.*## "}{printf "  %-16s %s\n",$$1,$$2}'
	@printf '\nOne recording each, in order:\n'
	@n=0; for c in $(RECORDINGS); do n=$$((n + 1)); printf '  play-%-14s or play-%s\n' "$$n" "$$c"; done

.PHONY: shell
shell: ## Open a shell in the lab (run clips from here: scenarios/borrow-a-model/before.sh)
	$(TOOLBOX)

.PHONY: up
up: ## Create the cluster, pull and load images, install everything (about 5-8 minutes)
	$(TOOLBOX) bash -c 'env/images.sh pull && env/install-platform.sh && env/install-workloads.sh && tools/verify.sh'

.PHONY: images
images: ## Pull every image into the lab (do this on a network that works); `make up` loads them
	$(TOOLBOX) env/images.sh pull

.PHONY: verify
verify: ## Check the environment is healthy
	$(TOOLBOX) tools/verify.sh

.PHONY: diagnose
diagnose: ## Dump gateway/data-plane state when traffic doesn't flow
	$(TOOLBOX) tools/diagnose.sh

.PHONY: reset
reset: ## Back to the starting state without recreating the cluster
	$(TOOLBOX) tools/reset.sh

.PHONY: stop
stop: ## Stop the lab and its cluster to free CPU and memory; any target restarts it (then make reset)
	$(TOOLBOX) --stop

.PHONY: down
down: ## Delete the cluster (the lab and its pulled images stay)
	$(TOOLBOX) kind delete cluster --name $(CLUSTER_NAME)

.PHONY: clean
clean: ## Delete the lab container: cluster, pulled images and all
	$(TOOLBOX) --remove

.PHONY: test
test: ## Run every policy's Kyverno CLI tests (no cluster needed)
	$(TOOLBOX) kyverno test policies/ --require-tests --remove-color

.PHONY: lint
lint: ## shellcheck + yamllint
	$(TOOLBOX) bash -c 'shellcheck -x -P SCRIPTDIR env/*.sh tools/*.sh tools/toolbox scenarios/*.sh scenarios/*/*.sh && yamllint -c .yamllint.yaml .'

.PHONY: check
check: ## Run every clip non-interactively and assert outcomes (from a fresh `make up` or `make reset`)
	DEMO_AUTO=1 $(TOOLBOX) scenarios/run-all.sh

.PHONY: demo
demo: ## Run the main demo live, pausing at every beat (Enter continues)
	$(TOOLBOX) scenarios/run-all.sh --demo

.PHONY: record
record: ## Reset, then record the main demo with asciinema into recordings/ (one file per clip)
	$(TOOLBOX) bash -c 'tools/reset.sh && scenarios/run-all.sh --record'

.PHONY: play
play: ## Play the recordings as if live: Space at each beat (CLIP=keep-serving plays matching clips)
	$(TOOLBOX) tools/play.sh $(CLIP)

# One target per recording: its number, make play-1 to make play-9, or its name,
# the clip's script path with dashes (scenarios/keep-serving/before.sh is make play-keep-serving-before).
# Both play recordings/07-keep-serving-before.cast. make help lists them.
RECORDINGS := $(shell ls recordings 2>/dev/null | sed -n 's/^[0-9][0-9]-\(.*\)\.cast$$/\1/p')
play-%:
	@$(TOOLBOX) tools/play.sh --clip $*

.PHONY: reporter
reporter: ## Open Policy Reporter UI on http://localhost:8082
	$(TOOLBOX) kubectl -n policy-reporter port-forward --address 0.0.0.0 svc/policy-reporter-ui 8082:8080
