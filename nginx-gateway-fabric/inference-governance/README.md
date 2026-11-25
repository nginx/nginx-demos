# Who's governing the GPU lane? Policy as code for AI inference traffic

Platform teams give tenants self-service on a shared cluster with RBAC: each team gets a role in its own namespace and creates routes, model servers and grants without a ticket. RBAC decides who may create a kind of object; it doesn't look at what's in it. So a well-meaning team can point its route at a model it isn't approved for, run its own model on top-end GPUs nobody pays for, or switch off the platform's failure mode, all with permissions it was rightly given.

This demo shows those gaps on [NGINX Gateway Fabric](https://github.com/nginx/nginx-gateway-fabric) with the [Gateway API Inference Extension](https://gateway-api-inference-extension.sigs.k8s.io/), and closes them with [Kyverno](https://kyverno.io/) policies written in CEL. Kyverno enforces the rules at admission, finds what got in before a policy existed (background scans and [Policy Reporter](https://github.com/kyverno/policy-reporter)), and can clean up what's left (a DeletingPolicy).

It was presented at KyvernoCon, KubeCon + CloudNativeCon North America 2026, by Ciara Stacke and Alessandro Fael Garcia. You can watch the session recording [here](https://www.youtube.com/watch?v=IrNXmd61Dnk). A PDF version of slides is available in the repo: [`whos-governing-the-gpu-lane.pdf`](whos-governing-the-gpu-lane.pdf). The main demo's scenario folders also have the talk's terminal recordings.

Everything runs on a laptop: a kind cluster with simulated models ([llm-d-inference-sim](https://github.com/llm-d/llm-d-inference-sim)), so no GPUs are needed. All names, teams, models and data are fictional.

## The story

Three namespaces share the cluster:

- `models`, the platform teams's: a small model (`llama-8b`, on entry-level L4 GPUs) and a large one (`llama-70b`, on top-end H100s).
- `team-research`, a premium-tier data science team. It may use every model, and runs its own fine-tuned one, `research-ft`, on internal data.
- `team-assistant`, customer support. It's on the standard tier, regulated because it answers customers, and approved for the small model only.

team-assistant wants better answers for enterprise customers, and the small model can't give them. Asking the platform team for a bigger model means a ticket and a wait, so the team uses the self-service it has, and each time it's stopped it tries the next thing.

## Scenarios

Each scenario is a folder in [`scenarios/`](scenarios/) with its own README, its clips, the manifests they apply, and its policies in `policies/`. A "before" clip makes the change and it works; an "after" clip installs the scenario's policies and tries again: it's denied, with a message that says why and who to ask.

| Number | Scenario | team-assistant... | Policies |
|---|---|---|---|
| 1 | [Cross-namespace model sharing](scenarios/1-cross-namespace-model-sharing/) | asks team-research to share its fine-tuned model with a ReferenceGrant | P1 `restrict-inferencepool-referencegrants` |
| 2 | [Unapproved model routing](scenarios/2-unapproved-model-routing/) | points its route at the platform's large model | P2 `restrict-model-access`, P3 `restrict-route-hostnames` |
| 3 | [Unbudgeted GPU workloads](scenarios/3-unbudgeted-gpu-workloads/) | runs its own large model on H100s, with no cost-center | P4 `require-cost-center`, P5 `restrict-gpu-class-by-tier` |
| 4 | [Fail-open pools](scenarios/4-fail-open-pools/) | sets its pool to `FailOpen` so it keeps serving if the endpoint picker is down | P6 `deny-failopen-for-regulated`, P7 `inference-backends-only` |
| 5 | [Priority caps by tier](scenarios/5-priority-caps-by-tier/) | gives its requests top priority | P8 `cap-objective-priority` |
| 6 | [Network isolation for model pods](scenarios/6-model-pod-network-isolation/) | calls a model pod directly, around the gateway | P9 `tenant-network-isolation` |
| 7 | [Per-route rate limits](scenarios/7-per-route-rate-limits/) | floods a shared model through a valid route | P10 `tenant-rate-limit` |
| 8 | [Cleaning up objects created before the policies](scenarios/8-cleanup/) | | background scan reports; P11 `cleanup-unowned-gpu-workloads` |

The numbers are the recommended order to roll the policies out, and the order `make check` runs them in. Cleanup comes last: its DeletingPolicy deletes workloads, so run it only once the owners have seen the reports. 1 to 4 and cleanup are the main demo, as presented in the talk (`make demo`); 5 to 7 work and `make check` runs them, but they weren't in the talk.

Every scenario also runs on its own: `make scenario S=<scenario>` resets the cluster, runs the scenarios it builds on off screen, then shows it, pausing at every beat. A scenario's `needs` file lists what it builds on. Fail-open pools and Priority caps by tier need Unbudgeted GPU workloads, Per-route rate limits needs Network isolation for model pods, and cleanup needs the main demo. The others start from a clean cluster.

Policies are numbered P1 to P11 in the order the scenarios apply them. Most are ValidatingPolicies; P9 and P10 are GeneratingPolicies and P11 is a DeletingPolicy. Each policy's file starts with a comment on what it checks and why, and its Kyverno CLI tests are next to it, in the scenario's `tests/` folder.

### Layout

Three folders, each with one job. `setup/` builds the cluster's starting state (`make up`) and puts it back (`make reset`); nothing else uses it once the cluster is up. `scenarios/` is the demo, and runs on whatever state `setup/` left. `lab/` is the container both run in.

```text
lab/                 the container every make target runs in
  Dockerfile         the image: every tool, plus its own Docker daemon
  toolbox            the launcher
  versions.env       every pinned version
setup/               builds the starting state; run by make up and make reset only
  cluster/           kind, the images, NGINX Gateway Fabric, Kyverno, Policy Reporter
  workloads/         namespaces, RBAC, models, gateway, routes
  tests/             a test that all of it passes every policy (make test runs it)
  *.sh               reset, verify, diagnose
scenarios/           the demo
  <scenario>/
    README.md        the story, what RBAC misses, the policy, what you'll see
    before.sh        the change, before the policy (cleanup has run.sh instead)
    after.sh         installs the policies and tries the change again
    *.cast           the clips as recorded in the talk (main demo only)
    policies/        the scenario's Kyverno policies
    tests/           their Kyverno CLI tests
    *.yaml           the manifests the clips apply
    needs            the scenarios it builds on, if any
  _shared/           the fake cluster every policy test runs against
  tools/             used during the demo, by name: chat.sh, cost-meter.sh, flood.sh
  run.sh  lib.sh     the runner and the clips' helpers
  record.sh play.sh  recording and playback
```

## Requirements

- Docker (Docker Desktop, Colima, OrbStack or Linux Docker), with about 4 CPUs and 6 GB of memory for its VM.
- `make` and `git`.

Every other tool runs in a container, the lab, built from [`lab/Dockerfile`](lab/Dockerfile) with the versions in [`lab/versions.env`](lab/versions.env). The lab runs its own Docker daemon, which holds the demo's images and the kind cluster, so Docker only ever shows one container, `inference-governance-lab`. It needs to run privileged.

The cluster never pulls images itself: `make up` pulls every image in [`setup/cluster/images.txt`](setup/cluster/images.txt) in the lab and loads it into the kind nodes, and every manifest uses `imagePullPolicy: Never`. Once the images are pulled, the demo runs without a network.

## Quick start

```shell
make test                      # Kyverno CLI tests for every policy, no cluster needed
make up                        # build the lab, pull images, create the cluster, install everything (8-10 minutes the first time)
make demo                      # run the main demo, pausing at every beat: press Enter to continue
make scenario S=4-fail-open-pools   # or one scenario on its own
make reset                     # back to the starting state
make stop                      # stop the lab and its cluster; any make target starts it again
```

`make up` ends with a health check. Once it says "Ready.", the gateway answers on http://localhost:8080:

```shell
make shell                                     # a shell in the lab
chat.sh assistant.example                      # team-assistant's customer assistant
chat.sh research.example                       # team-research's fine-tuned model
```

| Command | What it does |
|---|---|
| `make demo` | The main demo: the first four scenarios and cleanup, pausing at every beat |
| `make scenario S=<scenario>` | One scenario on its own, pausing at every beat (resets the cluster first) |
| `make check` | Every scenario, including the extra ones, without pauses, asserting each outcome; `S=<scenario>` for one |
| `make play` | Play the main demo's recordings (see below); `S=<scenario>` for one scenario's |
| `make shell` | A shell in the lab |
| `make reporter` | Policy Reporter's UI on http://localhost:8082 |
| `make verify` / `make diagnose` | Health checks / dump the gateway's state when traffic doesn't flow |
| `make reset` | Remove every demo policy, report and generated object, and restore the starting state (about a minute) |
| `make stop` | Stop the lab and the cluster; any target starts them again, then `make reset` |
| `make down` / `make clean` | Delete the cluster / delete the lab, cluster and images included |
| `make test` / `make lint` | Kyverno CLI tests (108; `S=<scenario>` for one scenario's) / shellcheck and yamllint |

## The recordings

The main demo's clips are recorded with [asciinema](https://asciinema.org/), as shown in the talk: each recording is next to its clip, for example `scenarios/4-fail-open-pools/before.cast` for `before.sh`. `make play` plays them in order, and `make play S=<scenario>` plays one scenario's. That needs the lab, but not the cluster. Playback pauses at every beat like the live run: Space continues, `]` skips to the next beat, Ctrl-C ends the clip.

The recordings are 100x28: the terminal needs to be at least that big, and `make play` refuses a smaller one. `make record` records the main demo again from the current cluster, replacing them.

The recordings are the talk as presented, so they show the names and layout of the time. The scenarios had other names (Borrow a model, Just point at the big one, Shadow GPU, Keep serving and the Finale), and those are the titles on screen. The policies were in a top-level `policies/` folder, for example `policies/p1-restrict-inferencepool-referencegrants/policy.yaml`; they're now in each scenario's `policies/` folder. The tools typed as `tools/chat.sh` are now in `scenarios/tools/`, and run by name: `chat.sh`.

## Versions

Pinned in [`lab/versions.env`](lab/versions.env): NGINX Gateway Fabric 2.8.0, Gateway API v1.6.3, Inference Extension v1.6.2, llm-d-router v0.11.0 (endpoint picker), llm-d-inference-sim v1.0.0, Kyverno v1.19.2, Policy Reporter chart 3.11.2, and kind v0.33.0 (Kubernetes v1.37.0). The lab's tools (kubectl, helm, yq, the Kyverno CLI) are pinned there too.

## Using the policies elsewhere

The policies use the Gateway API, the Inference Extension and Kubernetes objects, so they work with any Gateway API implementation. The exception is `tenant-rate-limit`, which generates an NGINX Gateway Fabric RateLimitPolicy; [Per-route rate limits](scenarios/7-per-route-rate-limits/) has the Envoy Gateway and kgateway equivalents.

Kyverno needs permission to read the kinds the policies look up and to generate NetworkPolicies and RateLimitPolicies: [`setup/workloads/manifests/02-kyverno-permissions.yaml`](setup/workloads/manifests/02-kyverno-permissions.yaml). To install every policy except the cleanup one, in the scenarios' recommended order:

```shell
kubectl apply -f setup/workloads/manifests/02-kyverno-permissions.yaml
for f in scenarios/[1-7]-*/policies/*.yaml; do kubectl apply -f "$f"; done
```

Start them in Audit mode, check the reports, then switch to Deny. `cleanup-unowned-gpu-workloads` (in [the cleanup scenario](scenarios/8-cleanup/)) deletes workloads: install it only once the owners have seen what it would remove. [`scenarios/_shared/README.md`](scenarios/_shared/README.md) has the design notes and how to write more tests.

## Troubleshooting

Run the `kubectl` commands from `make shell`.

| Symptom | Likely cause | Fix |
|---|---|---|
| The lab's Docker daemon doesn't start | the lab needs a privileged container | `docker logs inference-governance-lab`; Docker must allow privileged containers |
| A pod shows `ErrImageNeverPull` | the image isn't preloaded | add it to `setup/cluster/images.txt`, run `setup/cluster/images.sh` from `make shell`, delete the pod |
| Pods sit in `CreateContainerError` after the laptop slept | the kind nodes kept stale container names | `make down && make up` (`make stop` before sleeping avoids it) |
| `make verify`: HTTP 502 or 503 | the endpoint picker or a model isn't ready yet | wait 30 seconds and run `make verify` again |
| `make verify`: HTTP 000 or "connection reset by peer" | nothing serves the gateway's NodePort | `make diagnose`: the data plane pod in `gateway` should be Ready |
| A clip hangs while installing a policy | Kyverno's webhook isn't up | `kubectl -n kyverno get pods`, then `make reset` |
| The cleanup scenario lists fewer findings than expected | the background scan hasn't run yet | it runs every 30 seconds: run the clip again |
