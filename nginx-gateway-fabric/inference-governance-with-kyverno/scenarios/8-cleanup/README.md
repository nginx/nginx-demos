# Cleaning up objects created before the policies

Each scenario's policy stopped the next attempt, but admission only checks new changes, so everything created before a policy existed is still running. Kyverno's background scans check existing objects against the installed policies and record what fails in PolicyReports.

| Aspect | Details |
|---|---|
| Shows | the background scan's PolicyReports: each object that got in before a policy, with the reason |
| Policy | P11 `cleanup-unowned-gpu-workloads`: deletes model servers in tenant namespaces nobody pays for (no clip installs it) |
| Kind | DeletingPolicy |
| Builds on | the main demo: [Cross-namespace model sharing](../1-cross-namespace-model-sharing/), [Unapproved model routing](../2-unapproved-model-routing/), [Unbudgeted GPU workloads](../3-unbudgeted-gpu-workloads/), [Fail-open pools](../4-fail-open-pools/) |

```shell
make scenario S=8-cleanup   # runs the main demo's scenarios first, off screen
```

## The reports

[`run.sh`](run.sh) waits for the background scan, then lists what failed:

| Object | Policy | Left by |
|---|---|---|
| `team-assistant/HTTPRoute/assistant-pro` | `restrict-model-access` | Cross-namespace model sharing |
| `team-assistant/HTTPRoute/assistant` | `restrict-model-access` | Unapproved model routing |
| `team-assistant/Deployment/shadow-70b` | `require-cost-center`, `restrict-gpu-class-by-tier` | Unbudgeted GPU workloads |
| `team-assistant/InferencePool/shadow-70b` | `deny-failopen-for-regulated` | Fail-open pools |

It also expects Priority caps by tier's objective when that scenario ran, and the external fallback when someone applied it by hand. It fails on anything missing or extra, so a policy the background scan silently skips can't pass `make check`.

On a real cluster, this list is what to fix before you switch a policy from Audit to Deny. Policy Reporter shows the same reports for the whole cluster: `make reporter`, then http://localhost:8082.

## Cleaning up

[`policies/p11-cleanup-unowned-gpu-workloads.yaml`](policies/p11-cleanup-unowned-gpu-workloads.yaml) removes what the report found for P4: every hour it deletes Deployments, StatefulSets and Jobs in tenant namespaces that run model servers with no `cost-center` label, and their pods with them. It uses the same test as P4, so what admission denies today is what cleanup removes.

Deleting is irreversible, so no clip installs it. Roll it out after the report: run P4 in Audit, read the PolicyReports, tell the owners and give them time to add the label, then install it.

## Recordings

[`run.cast`](run.cast) is the clip from the talk, where this was the Finale. `make play S=8-cleanup` plays it.

## Tests

[`tests/`](tests/) has P11's Kyverno CLI tests. In a DeletingPolicy test, `pass` means the object would be deleted. `make test S=8-cleanup` runs them without a cluster.
