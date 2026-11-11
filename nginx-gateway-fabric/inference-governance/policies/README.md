# Policies

| ID | Policy | Kind | Scenario | What it stops |
|---|---|---|---|---|
| P1 | `restrict-inferencepool-referencegrants` | ValidatingPolicy (+ native VAP) | Borrow a model | Tenants sharing InferencePools across namespaces |
| P2 | `restrict-model-access` | ValidatingPolicy | Just point at the big one | Routing to InferencePools not on the tenant's allowlist |
| P3 | `require-cost-center` | ValidatingPolicy | Shadow GPU | Pools and model servers nobody pays for |
| P4 | `restrict-gpu-class-by-tier` | ValidatingPolicy | Shadow GPU | Standard-tier tenants on h100 (top-end) GPUs |
| P5 | `cap-objective-priority` | ValidatingPolicy | Cut the line | Tenants raising their own InferenceObjective priority |
| P6 | `inference-backends-only` | ValidatingPolicy | (repo only: `scenarios/keep-serving/fallback.yaml`) | Fallbacks to plain Services or external vendors |
| P7 | `deny-failopen-for-regulated` | ValidatingPolicy | Keep serving | Regulated pools that keep serving when the endpoint picker is down |
| G1 | `tenant-network-isolation` | GeneratingPolicy | Skip the gateway | Calling model pods directly, around the gateway |
| G2 | `tenant-rate-limit` | GeneratingPolicy | Valid config, bad request | Valid routes flooding shared pools |
| C1 | `cleanup-unowned-gpu-workloads` | DeletingPolicy | (none: no clip installs it) | Model servers in tenant namespaces nobody pays for, deleted hourly |

The letter in an ID is the policy type: P a ValidatingPolicy, G a GeneratingPolicy, C a
DeletingPolicy. The number is the order in this folder; the clips show the IDs in policy paths,
and denials show the policy's name.

Install all of them: `kubectl apply -f policies/rbac/ && kubectl apply -f policies/*/policy.yaml`
(the demo installs them one scenario at a time instead). C1 deletes workloads: install it only
after P3 has run in Audit and the owners have seen the report.

The main demo (`make demo`) uses P1-P4 and P7. P5, G1 and G2 have their own scenarios, which
`make check` runs; P6 has CLI tests and `scenarios/keep-serving/fallback.yaml` (apply it by hand);
C1 has CLI tests only.

## Design notes

- Denials say what to do next. P2, P4 and P7 end with "ask the platform team": the teams in this demo
  are well-meaning, and a denial that names the proper route is more useful than a bare no.

- C1 uses P3's test (a model-server workload, or its pod template, with no `cost-center`) so what
  admission denies today is what cleanup removes. It matches Deployments, StatefulSets and Jobs
  and deletes in the foreground, so their pods go too. In a DeletingPolicy test, `pass` means
  the object would be deleted.

- Missing labels fail safe. Only namespaces labeled `tenant=platform` are exempt from P2, P6 and
  G2; an unlabeled namespace is treated as a tenant. A missing `tier` means standard (P4, P5, G2)
  and a missing `regulated` means not regulated (P7).
- P2 and P6 check `RequestMirror` targets (on a rule or on a backendRef) as well as
  `backendRefs`, because a mirror sends every request to a second backend.
- P3 and P4 match Pods, Deployments, StatefulSets, DaemonSets, ReplicaSets, Jobs and CronJobs.
  What a checked controller creates (a Deployment's ReplicaSet, and Pods from a StatefulSet,
  DaemonSet, Job or a Deployment's ReplicaSet) isn't reported again; a hand-made ReplicaSet and
  its Pods are. Kyverno's default `resourceFilters` skip ReplicaSets at admission, but the
  background scan of CEL policies still reports them, so the policies skip them themselves. P4
  looks at the pod label and the nodeSelector; a real cluster would also check node affinity and
  DRA DeviceClasses.
- G2 limits each route, not each tenant, so a tenant with more routes gets more requests through.

## Tests

`make test` runs `kyverno test policies/` in the lab, with the Kyverno CLI from `KYVERNO_CLI_IMAGE`
in `versions.env` (v1.19.2). No cluster needed.
Each test uses the shared fake cluster in `_shared/cluster.yaml` (namespaces, allowlist, pools)
and the CRDs in `_crds/`.

Three things to know about the CLI if you write more tests:
- With `clusterResources`, `resource.Get()` reads the fake cluster; `context:` files are ignored.
- `namespaceObject` is empty in CLI tests, so these policies read namespace labels with
  `resource.Get('v1', 'namespaces', '', ns)`. That works the same way in a real cluster.
- A test resource must not have the same name/namespace/kind as one in `_shared/cluster.yaml`
  (the CLI panics with "already exists").

## Gateway portability

Only G2's generated object is gateway-specific. Equivalents:

| Gateway | Generate this instead of `RateLimitPolicy` |
|---|---|
| NGINX Gateway Fabric | `gateway.nginx.org/v1alpha1 RateLimitPolicy` |
| Envoy Gateway | `gateway.envoyproxy.io/v1alpha1 BackendTrafficPolicy` (`spec.rateLimit.local`) |
| kgateway | `gateway.kgateway.dev/v1alpha1 TrafficPolicy` (`spec.rateLimit.local`) |

Gateway API has no standard rate-limit API yet; GEP-5160 (kubernetes-sigs/gateway-api#5164) is
open as of October 2026.

Known NGF limitation: a route-attached RateLimitPolicy is likely not enforced on rules that split
across two or more weighted InferencePools. The demo routes use a single pool per rule.
