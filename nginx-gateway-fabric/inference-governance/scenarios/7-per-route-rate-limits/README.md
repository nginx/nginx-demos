# Per-route rate limits

Every object now passes every policy. A load test from team-assistant floods the shared `llama-8b` pool through a perfectly valid route. Admission control sees objects, not requests.

| Aspect | Details |
|---|---|
| RBAC allows | anything that sends requests through a valid route |
| RBAC can't see | request rates |
| Policy | P10 `tenant-rate-limit`: every tenant route to an InferencePool gets a rate limit, sized by tier |
| Kind | GeneratingPolicy (generates an NGINX Gateway Fabric RateLimitPolicy) |
| Builds on | [Network isolation for model pods](../6-model-pod-network-isolation/): the compliant `assistant-v2` route |
| In the talk | No; `make check` runs it |

```shell
make scenario S=7-per-route-rate-limits   # runs Network isolation for model pods first, off screen
```

## Before the policy

[`before.sh`](before.sh) sends 10 requests a second for 10 seconds through `assistant-v2.example` ([`flood.sh`](../tools/flood.sh)). Every one is accepted.

## With the policy

[`after.sh`](after.sh) installs [`policies/p10-tenant-rate-limit.yaml`](policies/p10-tenant-rate-limit.yaml). Kyverno generates a RateLimitPolicy for the route: 2 requests a second for the standard tier, 20 for premium. The same load test now gets HTTP 429s. team-assistant can delete the RateLimitPolicy (its role allows that, to show this), and Kyverno puts it back.

The limit is per route, so a tenant with three routes gets three times the rate; a true per-tenant budget needs a limit the gateway shares across routes. Only the generated object is specific to NGINX Gateway Fabric:

| Gateway | Generate this instead of `RateLimitPolicy` |
|---|---|
| NGINX Gateway Fabric | `gateway.nginx.org/v1alpha1 RateLimitPolicy` |
| Envoy Gateway | `gateway.envoyproxy.io/v1alpha1 BackendTrafficPolicy` (`spec.rateLimit.local`) |
| kgateway | `gateway.kgateway.dev/v1alpha1 TrafficPolicy` (`spec.rateLimit.local`) |

Gateway API has no standard rate-limit API yet: GEP-5160 (kubernetes-sigs/gateway-api#5164) is open as of October 2026. Known NGINX Gateway Fabric limitation: a RateLimitPolicy attached to a route is likely not enforced on rules that split across two or more weighted InferencePools. The demo's routes use one pool per rule.

## Tests

[`tests/`](tests/) has P10's Kyverno CLI tests, one for the RateLimitPolicy it generates at each tier. `make test S=7-per-route-rate-limits` runs them without a cluster.
