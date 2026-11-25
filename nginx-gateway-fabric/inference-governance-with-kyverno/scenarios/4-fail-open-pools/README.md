# Fail-open pools

Assistant Pro is customer-facing, so team-assistant never wants it to show an error. If the endpoint picker is down, an InferencePool that fails closed (the default) refuses requests; one that fails open sends them straight to the model's pods and skips the picker's scheduling. The platform's rule is that regulated teams fail closed, and team-assistant answers customers, so it's regulated. It owns the pool it deployed in the Unbudgeted GPU workloads scenario, so it switches it to `FailOpen`.

| Aspect | Details |
|---|---|
| RBAC allows | team-assistant editing its own InferencePool |
| RBAC can't see | `failureMode`, or that the namespace is regulated |
| Policy | P6 `deny-failopen-for-regulated`: InferencePools in regulated namespaces may not fail open |
| Kind | ValidatingPolicy |
| Builds on | [Unbudgeted GPU workloads](../3-unbudgeted-gpu-workloads/): the `shadow-70b` pool, with P4 installed |

```shell
make scenario S=4-fail-open-pools   # runs Unbudgeted GPU workloads first, off screen
```

## Before the policy

[`before.sh`](before.sh) patches `shadow-70b` to `failureMode: FailOpen`. P4, installed in Unbudgeted GPU workloads, checks every update, so the clip first adds the pool's `cost-center` label off screen.

## With the policy

[`after.sh`](after.sh) installs [`policies/p6-deny-failopen-for-regulated.yaml`](policies/p6-deny-failopen-for-regulated.yaml) and team-assistant applies the pool with `FailOpen` again ([`failopen-pool.yaml`](failopen-pool.yaml)). The denial says the namespace is regulated, that requests must go through the endpoint picker, and to ask the platform team. The pool switched before the policy stays `FailOpen` until someone changes it back: the cleanup scenario reports it.

## Also here: an external fallback (P7)

Another way to keep serving is to fall back to an outside model vendor when the pool is busy. [`fallback.yaml`](fallback.yaml) does it with an ExternalName Service and a route to it, which skips the endpoint picker, the allowlist and cost attribution. [`policies/p7-inference-backends-only.yaml`](policies/p7-inference-backends-only.yaml) stops it: tenant routes may only send traffic to InferencePools, and tenants may not create ExternalName Services. No clip applies the fallback; to try it from `make shell`, as team-assistant:

```shell
kubectl --as=team-assistant-dev apply -f scenarios/4-fail-open-pools/fallback.yaml   # works, before P7
kubectl apply -f scenarios/4-fail-open-pools/policies/p7-inference-backends-only.yaml
kubectl --as=team-assistant-dev create --dry-run=server -f scenarios/4-fail-open-pools/fallback.yaml   # denied
```

If the fallback is in place when the cleanup scenario runs, it reports that too.

## Recordings

[`before.cast`](before.cast) and [`after.cast`](after.cast) are the clips from the talk, where this scenario was called Keep serving. `make play S=4-fail-open-pools` plays them.

## Tests

[`tests/`](tests/) has the Kyverno CLI tests for P7 and P6. `scenario-manifests-p6` checks the external fallback and `scenario-manifests-p7` the clip's FailOpen pool. `make test S=4-fail-open-pools` runs them without a cluster.
