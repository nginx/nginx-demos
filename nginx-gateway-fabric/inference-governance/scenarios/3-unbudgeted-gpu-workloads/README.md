# Unbudgeted GPU workloads

With the large model and team-research's model both closed to it, team-assistant runs its own: its role lets it create model servers, so it deploys a copy of the large model on h100s, the top-end GPU class, in its own namespace. Nothing says who pays for it.

| Aspect | Details |
|---|---|
| RBAC allows | team-assistant creating Deployments and InferencePools in its own namespace |
| RBAC can't see | which GPU class a Deployment asks for, or who pays for it |
| Policy | P4 `require-cost-center`: every InferencePool and model server names who pays |
| Policy | P5 `restrict-gpu-class-by-tier`: standard-tier tenants stay off h100 |
| Kind | ValidatingPolicy (both) |

```shell
make scenario S=3-unbudgeted-gpu-workloads
```

## Before the policies

[`before.sh`](before.sh) applies [`shadow-70b.yaml`](shadow-70b.yaml): a model server on h100, its endpoint picker and an InferencePool, none with a `cost-center` label. The cost meter ([`cost-meter.sh`](../tools/cost-meter.sh)) shows it reserving an h100 that nobody is billed for.

## With the policies

[`after.sh`](after.sh) installs [`policies/p4-require-cost-center.yaml`](policies/p4-require-cost-center.yaml) and [`policies/p5-restrict-gpu-class-by-tier.yaml`](policies/p5-restrict-gpu-class-by-tier.yaml). team-assistant redeploys the same model and P4 denies it: it needs a cost-center label. It adds the label and tries again, and P5 denies it: team-assistant is on the standard tier, and h100 is premium only.

A model server is anything that runs pods labeled `inference.example.com/role: model-server`: a bare Pod or any workload controller. P4 and P5 check the controller and its pod template, and don't report the Pods a checked controller creates a second time. Tier comes from the namespace label, and a namespace without one is treated as standard. The platform's `models` namespace is exempt from P5, since the shared large model runs on h100 there.

The model server from before the policies keeps running and costing money. The cleanup scenario reports it, and the cleanup policy there can delete it.

## Recordings

[`before.cast`](before.cast) and [`after.cast`](after.cast) are the clips from the talk, where this scenario was called Shadow GPU. `make play S=3-unbudgeted-gpu-workloads` plays them.

## Tests

[`tests/`](tests/) has the Kyverno CLI tests for P4 and P5. `owner-check-tenant` and `owner-check-controller` cover a Pod that claims an owner, made by a tenant or by a Kubernetes controller, and `scenario-manifests-p3` and `-p4` check the clip's manifest. `make test S=3-unbudgeted-gpu-workloads` runs them without a cluster.
