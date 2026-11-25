# Every target runs inside the lab container (lab/toolbox), so the host needs only Docker, make
# and git. The lab is built and started on first use.
#
# setup/ builds the cluster's starting state (make up) and puts it back (make reset); nothing else
# uses it. scenarios/ is the demo, and runs on whatever state setup/ left.
include lab/versions.env
export
TOOLBOX := lab/toolbox

.PHONY: help
help: ## List targets
	@grep -hE '^[a-zA-Z_-]+:.*## ' $(MAKEFILE_LIST) | awk 'BEGIN{FS=":.*## "}{printf "  %-16s %s\n",$$1,$$2}'

.PHONY: shell
shell: ## Open a shell in the lab
	$(TOOLBOX)

.PHONY: up
up: ## Create the cluster, pull and load images, install everything (about 5-8 minutes)
	$(TOOLBOX) bash -c 'setup/cluster/images.sh pull && setup/cluster/install.sh && setup/workloads/install.sh && setup/verify.sh'

.PHONY: images
images: ## Pull every image into the lab (do this on a network that works); `make up` loads them
	$(TOOLBOX) setup/cluster/images.sh pull

.PHONY: verify
verify: ## Check the environment is healthy
	$(TOOLBOX) setup/verify.sh

.PHONY: diagnose
diagnose: ## Dump gateway/data-plane state when traffic doesn't flow
	$(TOOLBOX) setup/diagnose.sh

.PHONY: reset
reset: ## Back to the starting state without recreating the cluster
	$(TOOLBOX) setup/reset.sh

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
test: ## Run every policy's Kyverno CLI tests (no cluster needed); S=<scenario> for one scenario's
	$(TOOLBOX) kyverno test $(if $(S),scenarios/$(S)/,scenarios/ setup/tests/) --require-tests --remove-color

.PHONY: lint
lint: ## shellcheck + yamllint
	$(TOOLBOX) bash -c 'shellcheck -x -P SCRIPTDIR lab/toolbox setup/*.sh setup/*/*.sh scenarios/*.sh scenarios/*/*.sh && yamllint -c .yamllint.yaml .'

.PHONY: check
check: ## Run every scenario without pauses and assert outcomes (from `make up` or `make reset`); S=<scenario> resets, then runs one
	DEMO_AUTO=1 $(TOOLBOX) bash -c '$(if $(S),setup/reset.sh >/dev/null && )scenarios/run.sh $(S)'

.PHONY: demo
demo: ## Run the main demo live, pausing at every beat (Enter continues)
	$(TOOLBOX) scenarios/run.sh --demo

.PHONY: scenario
scenario: ## Run one scenario on its own, pausing at every beat: make scenario S=4-fail-open-pools
	@test -n "$(S)" || { echo "usage: make scenario S=<scenario> (ls scenarios/)" >&2; exit 2; }
	@test -d "scenarios/$(S)" || { echo "no scenario '$(S)' (ls scenarios/)" >&2; exit 2; }
	$(TOOLBOX) bash -c 'echo "Resetting the cluster..." && setup/reset.sh >/dev/null && scenarios/run.sh $(S)'

.PHONY: record
record: ## Reset, then record the main demo with asciinema: <clip>.cast next to each clip
	$(TOOLBOX) bash -c 'setup/reset.sh && scenarios/run.sh --record'

.PHONY: play
play: ## Play the main demo's recordings as if live: Space at each beat; S=<scenario> for one
	$(TOOLBOX) scenarios/play.sh $(S)

.PHONY: reporter
reporter: ## Open Policy Reporter UI on http://localhost:8082
	$(TOOLBOX) kubectl -n policy-reporter port-forward --address 0.0.0.0 svc/policy-reporter-ui 8082:8080
