# Network isolation for model pods

team-assistant calls a `llama-70b` pod by its IP address from a pod in its own namespace, which skips the gateway, the allowlist and the cost meter. It creates no configuration, so admission control has nothing to check: the other scenarios' policies never see the call.

| Aspect | Details |
|---|---|
| RBAC allows | team-assistant running pods in its own namespace |
| RBAC can't see | network traffic |
| Policy | P9 `tenant-network-isolation`: every namespace with a tenant label gets a NetworkPolicy so model servers only accept traffic from the gateway and their own endpoint pickers |
| Kind | GeneratingPolicy |
| In the talk | No; `make check` runs it |

```shell
make scenario S=6-model-pod-network-isolation
```

## Before the policy

[`before.sh`](before.sh) runs a `curl` pod in team-assistant's namespace and calls a `llama-70b` pod directly. The model answers.

## With the policy

[`after.sh`](after.sh) installs [`policies/p9-tenant-network-isolation.yaml`](policies/p9-tenant-network-isolation.yaml). Kyverno generates a `model-server-ingress` NetworkPolicy in every namespace with a `tenant` label, platform namespaces included, so the shared models in `models` are covered too. The direct call times out. Through the gateway, a compliant route still works ([`assistant-v2.yaml`](assistant-v2.yaml)): a new route for the customer assistant on the small model, which [Per-route rate limits](../7-per-route-rate-limits/) uses next.

## Tests

[`tests/`](tests/) has P9's Kyverno CLI test, which checks the NetworkPolicy it generates, and `scenario-manifests`, which checks that the clip's compliant route passes the policies installed before it. `make test S=6-model-pod-network-isolation` runs them without a cluster.
