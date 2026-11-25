# Cross-namespace model sharing

team-assistant wants to ship Assistant Pro for enterprise customers, and its small model can't answer their questions. team-research has a fine-tuned model that can, so team-assistant points Assistant Pro at it, and team-research shares it with a ReferenceGrant.

| Aspect | Details |
|---|---|
| RBAC allows | team-research creating ReferenceGrants in its own namespace |
| RBAC can't see | that the grant shares an InferencePool, and with it the model's internal data |
| Policy | P1 `restrict-inferencepool-referencegrants`: only the platform team may share InferencePools |
| Kind | ValidatingPolicy, also enforced as a native ValidatingAdmissionPolicy |

```shell
make scenario S=1-cross-namespace-model-sharing
```

## Before the policy

[`before.sh`](before.sh) points Assistant Pro at team-research's model in another namespace ([`assistant-research-ft.yaml`](assistant-research-ft.yaml)). NGINX Gateway Fabric refuses the cross-namespace reference until the other namespace consents. team-research's role lets it create ReferenceGrants so teams can share their own Services, so it shares the model ([`grant.yaml`](grant.yaml)), and a customer asking about renewals gets an answer built from team-research's internal data.

## With the policy

[`after.sh`](after.sh) installs [`policies/p1-restrict-inferencepool-referencegrants.yaml`](policies/p1-restrict-inferencepool-referencegrants.yaml). It uses only standard CEL and the requester's identity, so Kyverno also generates a native ValidatingAdmissionPolicy that the API server enforces without calling Kyverno. team-research tries to share its model again, and is denied: only the platform team can share InferencePools.

Grants for Services still work, so teams can go on sharing their own APIs. The grant made before the policy stays, and Assistant Pro keeps answering from it: admission only checks new changes. P1 has background scans off, so the cleanup scenario reports the route instead (through P2).

Why keep the verb and add a policy, instead of taking ReferenceGrants away from tenants: see the comment at the top of [`setup/workloads/manifests/01-rbac.yaml`](../../setup/workloads/manifests/01-rbac.yaml).

## Recordings

[`before.cast`](before.cast) and [`after.cast`](after.cast) are the clips from the talk, where this scenario was called Borrow a model. `make play S=1-cross-namespace-model-sharing` plays them.

## Tests

[`tests/`](tests/) has P1's Kyverno CLI tests, one that checks the platform team can still share pools, and `scenario-manifests`, which checks the clip's route against P2 (that's how the cleanup scenario finds it). `make test S=1-cross-namespace-model-sharing` runs them without a cluster.
