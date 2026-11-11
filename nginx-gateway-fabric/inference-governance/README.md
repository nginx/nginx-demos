# Who's governing the GPU lane? Policy as code for AI inference traffic

Platform teams give tenants self-service on a shared cluster with RBAC: each team gets a role in
its own namespace and creates routes, model servers and grants without a ticket. RBAC decides who
may create a kind of object; it doesn't look at what's in it. So a well-meaning team can point its route at a
model it isn't approved for, run its own model on top-end GPUs nobody pays for, or switch off the
platform's failure mode, all with permissions it was rightly given.

This demo shows those gaps on [NGINX Gateway Fabric](https://github.com/nginx/nginx-gateway-fabric)
with the [Gateway API Inference Extension](https://gateway-api-inference-extension.sigs.k8s.io/),
and closes them with [Kyverno](https://kyverno.io/) policies written in CEL. Kyverno enforces the
rules at admission, finds what got in before a policy existed (background scans and
[Policy Reporter](https://github.com/kyverno/policy-reporter)), and can clean up what's left (a
DeletingPolicy).

It was presented at KyvernoCon, KubeCon + CloudNativeCon North America 2026, by Ciara Stacke and
Alessandro Fael Garcia. The slides are in [`slides/`](slides/) (PowerPoint and PDF, with speaker
notes in the PowerPoint), and the demo as shown in the talk is in [`recordings/`](recordings/).

Everything runs on a laptop: a kind cluster with simulated models
([llm-d-inference-sim](https://github.com/llm-d/llm-d-inference-sim)), so no GPUs are needed. All
names, teams, models and data are fictional.

## The story

Three namespaces share the cluster:

- `models`, the platform's: a small model (`llama-8b`, on entry-level L4 GPUs) and a large one
  (`llama-70b`, on top-end H100s).
- `team-research`, a premium-tier data science team. It may use every model, and runs its own
  fine-tuned one, `research-ft`, on internal data.
- `team-assistant`, customer support. It's on the standard tier, regulated because it answers
  customers, and approved for the small model only.

team-assistant wants better answers for enterprise customers, and the small model can't give
them. Asking the platform team for a bigger model means a ticket and a wait, so the team uses the
self-service it has, and each time it's stopped it tries the next thing:

| Scenario | team-assistant... | RBAC allows... | ...but can't see | Kyverno policy |
|---|---|---|---|---|
| Borrow a model | asks team-research to share its fine-tuned model with a ReferenceGrant | creating ReferenceGrants | that the grant shares an InferencePool | `restrict-inferencepool-referencegrants` (also a native ValidatingAdmissionPolicy) |
| Just point at the big one | points its route at the platform's large model | creating HTTPRoutes | which pool the route uses, and whether the team is approved for it | `restrict-model-access` |
| Shadow GPU | runs its own large model on H100s, with no cost-center | creating Deployments and InferencePools | the GPU class, and who pays | `require-cost-center`, `restrict-gpu-class-by-tier` |
| Keep serving | sets its pool to `FailOpen` so it keeps serving if the endpoint picker is down | editing its own InferencePool | `failureMode`, and that the namespace is regulated | `deny-failopen-for-regulated` |

Each scenario has a "before" clip, where the change works, and an "after" clip, which installs
the policy and tries the same change again: it's denied, with a message that says why and who to
ask. What got in before a policy existed is still running, since admission only checks new
changes. The Finale lists it from Kyverno's background scans.

Three more scenarios aren't in the main demo, but work and are checked by `make check`:

- Cut the line (`cap-objective-priority`) caps InferenceObjective priority by tier.
- Skip the gateway (`tenant-network-isolation`, a GeneratingPolicy) generates NetworkPolicies so
  model pods only take traffic from the gateway.
- Valid config, bad request (`tenant-rate-limit`, a GeneratingPolicy) generates a rate limit per
  tenant route, sized by tier, as an NGINX Gateway Fabric RateLimitPolicy.

The ten policies, with their Kyverno CLI tests, are described in
[`policies/README.md`](policies/README.md).

## Requirements

- Docker (Docker Desktop, Colima, OrbStack or Linux Docker), with about 6 CPUs and 10 GB of memory
  for its VM.
- `make` and `git`.

Every other tool runs in a container, the lab, built from
[`env/toolbox/Dockerfile`](env/toolbox/Dockerfile) with the versions in
[`versions.env`](versions.env). The lab runs its own Docker daemon, which holds the demo's images
and the kind cluster, so Docker only ever shows one container, `inference-governance-lab`. It
needs to run privileged.

## Quick start

```shell
make test     # Kyverno CLI tests for every policy, no cluster needed
make up       # build the lab, pull images, create the cluster, install everything (8-10 minutes the first time)
make demo     # run the main demo, pausing at every beat: press Enter to continue
make reset    # back to the starting state
make stop     # stop the lab and its cluster; any make target starts it again
```

`make up` ends with a health check. Once it says "Ready.", the gateway answers on
http://localhost:8080:

```shell
make shell                                     # a shell in the lab
tools/chat.sh assistant.example                # team-assistant's customer assistant
tools/chat.sh research.example                 # team-research's fine-tuned model
```

Run a single clip from `make shell`, for example `scenarios/borrow-a-model/before.sh`. The clips
must run in order on one environment, since later ones use what earlier ones leave behind; start
from `make up` or `make reset`.

| Command | What it does |
|---|---|
| `make demo` | The main demo: the four scenarios and the Finale, pausing at every beat |
| `make check` | Every clip, including the extra scenarios, without pauses, asserting each outcome |
| `make play` | Play the recordings (see below) |
| `make shell` | A shell in the lab |
| `make reporter` | Policy Reporter's UI on http://localhost:8082 |
| `make verify` / `make diagnose` | Health checks / dump the gateway's state when traffic doesn't flow |
| `make reset` | Remove every demo policy, report and generated object, and restore the starting state (about a minute) |
| `make stop` | Stop the lab and the cluster; any target starts them again, then `make reset` |
| `make down` / `make clean` | Delete the cluster / delete the lab, cluster and images included |
| `make test` / `make lint` | Kyverno CLI tests (94) / shellcheck and yamllint |

### Behind a TLS-intercepting proxy

If your network intercepts TLS, point `CA_BUNDLE` at the proxy's CA certificate (PEM) the first
time you run a target:

```shell
CA_BUNDLE=~/proxy-ca.pem make up
```

The lab trusts it for downloads and image pulls. It's passed to the image build as a secret and
mounted at runtime, so no image layer contains it. The kind nodes don't get it, so the cluster
never pulls images itself: `make up` pulls every image in [`env/images.txt`](env/images.txt) in
the lab and loads it into the nodes, and every manifest uses `imagePullPolicy: Never`.

## The recordings

[`recordings/`](recordings/) has the main demo as [asciinema](https://asciinema.org/) recordings,
one per clip, as shown in the talk. Play them with `make play`. That needs the lab, but not the
cluster. Playback pauses at every beat like the live run: Space continues, `]` skips to the next
beat, Ctrl-C ends the clip. `make play-1` to `make play-9` play one clip each (`make help` lists
them).

The recordings are 100x28: the terminal needs to be at least that big, and `make play` refuses a
smaller one. `make record` records a new set from the current cluster.

## Versions

Pinned in [`versions.env`](versions.env): NGINX Gateway Fabric 2.8.0, Gateway API v1.6.3,
Inference Extension v1.6.2, llm-d-router v0.11.0 (endpoint picker), llm-d-inference-sim v1.0.0,
Kyverno v1.19.2, Policy Reporter chart 3.11.2, and kind v0.33.0 (Kubernetes v1.37.0). The lab's
tools (kubectl, helm, yq, the Kyverno CLI) are pinned there too.

## Using the policies elsewhere

The policies use the Gateway API, the Inference Extension and Kubernetes objects, so they work
with any Gateway API implementation. The exception is `tenant-rate-limit`, which generates an
NGINX Gateway Fabric RateLimitPolicy; [`policies/README.md`](policies/README.md) has the Envoy
Gateway and kgateway equivalents. To install all of them on a cluster:

```shell
kubectl apply -f policies/rbac/ && kubectl apply -f policies/*/policy.yaml
```

Start them in Audit mode, check the reports, then switch to Deny. `cleanup-unowned-gpu-workloads`
deletes workloads: install it only once the owners have seen what it would remove.

## Troubleshooting

Run the `kubectl` commands from `make shell`.

| Symptom | Likely cause | Fix |
|---|---|---|
| `make up` fails with x509 or TLS errors | a TLS-intercepting proxy | set `CA_BUNDLE` (above), then `make clean && make up` |
| The lab's Docker daemon doesn't start | the lab needs a privileged container | `docker logs inference-governance-lab`; Docker must allow privileged containers |
| A pod shows `ErrImageNeverPull` | the image isn't preloaded | add it to `env/images.txt`, run `env/images.sh` from `make shell`, delete the pod |
| Pods sit in `CreateContainerError` after the laptop slept | the kind nodes kept stale container names | `make down && make up` (`make stop` before sleeping avoids it) |
| `make verify`: HTTP 502 or 503 | the endpoint picker or a model isn't ready yet | wait 30 seconds and run `make verify` again |
| `make verify`: HTTP 000 or "connection reset by peer" | nothing serves the gateway's NodePort | `make diagnose`: the data plane pod in `gateway` should be Ready |
| A clip hangs while installing a policy | Kyverno's webhook isn't up | `kubectl -n kyverno get pods`, then `make reset` |
| The Finale lists fewer findings than expected | the background scan hasn't run yet | it runs every 30 seconds: run the clip again |
