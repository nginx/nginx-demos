# Unapproved model routing

Once only the platform can share models (Cross-namespace model sharing), team-assistant looks at the platform's own shared models. The platform offers them to every tenant through one ReferenceGrant, and keeps a list of which team is approved for which model. team-assistant is approved for the small one. It changes one line of its route and points it at the large model.

| Aspect | Details |
|---|---|
| RBAC allows | team-assistant creating and editing HTTPRoutes in its own namespace |
| RBAC can't see | which InferencePool a route points at, or whether the team is approved for it; which hostname it claims |
| Policy | P2 `restrict-model-access`: tenants may only route to InferencePools on their allowlist |
| Policy | P3 `restrict-route-hostnames`: tenants may only use the hostnames the platform gave them |
| Kind | ValidatingPolicy (both) |

```shell
make scenario S=2-unapproved-model-routing
```

## Before the policy

[`before.sh`](before.sh) lists the shared models, then applies [`assistant-70b.yaml`](assistant-70b.yaml): the baseline `assistant` route with its backend changed from `llama-8b` to `llama-70b`. Every customer now gets the large model on top-end GPUs. The allowlist says `llama-8b`, and nothing checked it.

## With the policy

[`after.sh`](after.sh) shows the allowlist, the `model-allowlist` ConfigMap in the `kyverno` namespace ([`setup/workloads/manifests/20-allowlist.yaml`](../../setup/workloads/manifests/20-allowlist.yaml)), which tenants can't edit. It installs [`policies/p2-restrict-model-access.yaml`](policies/p2-restrict-model-access.yaml) and team-assistant tries the large model again. The denial names the model the team isn't approved for and tells it to ask the platform team.

P2 matches pools by `<namespace>/<pool>`, not by a label, since a tenant can label its own pool anything. It checks `RequestMirror` targets too, because a mirror sends every request to a second backend. The route that already points at `llama-70b` stays: the cleanup scenario reports it.

The clip also installs [`policies/p3-restrict-route-hostnames.yaml`](policies/p3-restrict-route-hostnames.yaml), off screen. Every team's routes share one Gateway listener, and the Gateway API gives a longer path priority across namespaces, so without P3 team-research could attach a route to `assistant.example` with a longer path ([`steal-assistant.yaml`](steal-assistant.yaml)) and take every team-assistant customer's request, past P2: the backend is on team-research's own list. P3 checks each route's hostnames against a platform-owned list, the `hostname-allowlist` ConfigMap in the same file as the model allowlist, and denies a tenant route that names none, since that would match every hostname on the listener. The clip checks that the takeover is denied.

## Recordings

[`before.cast`](before.cast) and [`after.cast`](after.cast) are the clips from the talk, where this scenario was called Just point at the big one. `make play S=2-unapproved-model-routing` plays them.

## Tests

[`tests/`](tests/) has P2's Kyverno CLI tests and `scenario-manifests`, which checks the clip's route. `make test S=2-unapproved-model-routing` runs them without a cluster.
