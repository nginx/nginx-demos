# Shared by the scenarios' tests

The policy tests are Kyverno CLI tests: each folder with a `kyverno-test.yaml` runs some policies against some objects and says what result to expect. There are 23. Each scenario's are in `scenarios/<scenario>/tests/`: one per policy, named after it, and in most scenarios a `scenario-manifests` test that checks the clips' manifests get the verdict the clip claims. One more is in setup, [`setup/tests/policies/baseline-check/`](../../setup/tests/policies/baseline-check/): it checks that what `make up` installs passes every policy, so the platform doesn't fight itself and the cleanup scenario has no false positives. The cleanup scenario checks the same thing on the live cluster.

This folder has what every test runs against. These aren't tests:

- [`cluster.yaml`](cluster.yaml) is a fake cluster. The tests run without Kubernetes, but the policies look things up as they run (a namespace's tier, a team's allowed models), and `cluster.yaml` provides those objects: the namespaces and their labels, the model allowlist, the shared InferencePools.
- [`crds/`](crds/) has the upstream definitions of the Gateway API, Inference Extension and NGINX Gateway Fabric kinds, so the CLI understands them offline.

`make test` runs every test in the lab with the Kyverno CLI pinned in [`lab/Dockerfile`](../../lab/Dockerfile) (v1.19.2), without a cluster. `make test S=<scenario>` runs one scenario's.

## Writing more tests

A new test goes in `scenarios/<scenario>/tests/<name>/kyverno-test.yaml`:

```yaml
policies: [../../policies/<policy>.yaml]
resources: [resources.yaml]
clusterResources: [../../../_shared/cluster.yaml]
```

Four things to know about the CLI:

- It resolves the CRD paths in `cluster.yaml` from each test's folder, not from `cluster.yaml`. That's why every test sits four folders below the demo root, `setup/tests/policies/baseline-check/` included; a test anywhere else needs its own copy of the CRD list.
- With `clusterResources`, `resource.Get()` reads the fake cluster; `context:` files are ignored.
- `namespaceObject` is empty in CLI tests, so these policies read namespace labels with `resource.Get('v1', 'namespaces', '', ns)`. That works the same way in a real cluster.
- A test resource must not have the same name, namespace and kind as one in `cluster.yaml` (the CLI panics with "already exists").

## Design notes

- Denials say what to do next. P2, P5 and P6 end with "ask the platform team": the teams in this demo are well-meaning, and a denial that names the proper route is more useful than a bare no.
- Missing labels fail safe. Only namespaces labeled `tenant=platform` are exempt from P2, P7 and P10; an unlabeled namespace is treated as a tenant. A missing `tier` means standard (P5, P8, P10) and a missing `regulated` means not regulated (P6).
- P2 and P7 check `RequestMirror` targets (on a rule or on a backendRef) as well as `backendRefs`, because a mirror sends every request to a second backend.
- P4 and P5 match Pods, Deployments, StatefulSets, DaemonSets, ReplicaSets, Jobs and CronJobs. What a checked controller creates (a Deployment's ReplicaSet, and Pods from a StatefulSet, DaemonSet, Job or a Deployment's ReplicaSet) isn't reported again; a hand-made ReplicaSet and its Pods are. ownerReferences are whatever the creator writes, so at admission that only counts when one of the Kubernetes controllers made the request (each runs as its own `kube-system` service account); a tenant's bare Pod that claims an owner is checked like any bare Pod (`owner-check-tenant` and `owner-check-controller` test both). Kyverno's default `resourceFilters` skip ReplicaSets at admission, but the background scan of CEL policies still reports them, so the policies skip them themselves. P5 looks at the pod label and the nodeSelector; a real cluster would also check node affinity and DRA DeviceClasses.
- P11 uses P4's test (a model-server workload, or its pod template, with no `cost-center`) so what admission denies today is what cleanup removes. It matches Deployments, StatefulSets and Jobs and deletes in the foreground, so their pods go too. In a DeletingPolicy test, `pass` means the object would be deleted.
- P10 limits each route, not each tenant, so a tenant with more routes gets more requests through.
