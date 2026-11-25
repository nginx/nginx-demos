# Priority caps by tier

Priority is part of what a tier buys: when a pool is saturated, higher-priority requests are served first. An InferenceObjective sets it, and tenants can write their own at any value. team-assistant is on the standard tier, which gets priority 0, and gives itself 100.

| Aspect | Details |
|---|---|
| RBAC allows | team-assistant creating InferenceObjectives in its own namespace |
| RBAC can't see | the priority, or the tenant's tier |
| Policy | P8 `cap-objective-priority`: priority capped by tier, 0 for standard and 10 for premium |
| Kind | ValidatingPolicy |
| Builds on | [Unbudgeted GPU workloads](../3-unbudgeted-gpu-workloads/): the `shadow-70b` pool the objective points at |
| In the talk | No; `make check` runs it |

```shell
make scenario S=5-priority-caps-by-tier   # runs Unbudgeted GPU workloads first, off screen
```

## Before the policy

[`before.sh`](before.sh) applies [`objective.yaml`](objective.yaml), priority 100 on team-assistant's own pool. An objective lives in its pool's namespace, so this shows the tier contract being ignored, not one tenant overtaking another on a shared pool.

## With the policy

[`after.sh`](after.sh) installs [`policies/p8-cap-objective-priority.yaml`](policies/p8-cap-objective-priority.yaml) and applies the same objective again. The denial gives the namespace, its tier and the highest priority it may use. The objective from before stays; the cleanup scenario reports it when this scenario has run.

## Tests

[`tests/`](tests/) has P8's Kyverno CLI tests and `scenario-manifests`, which checks the clip's objective. `make test S=5-priority-caps-by-tier` runs them without a cluster.
