#!/usr/bin/env bash
# "Borrow a model", before the policy
# team-assistant wants to ship Assistant Pro: the small model answers FAQ questions but not
# renewals and pricing. It points the "pro" assistant at team-research's fine-tuned model. The gateway controller
# refuses the cross-namespace reference (controller validation: no ReferenceGrant). team-research's
# role (tenant-editor, env/manifests/01-rbac.yaml) lets it create ReferenceGrants, which RBAC can't
# restrict by what they point at; it shares the model, the route resolves, and a customer gets that
# model's internal data.
source "$(dirname "$0")/../lib.sh"

clip "Borrow a model"

say "team-assistant's customer assistant today, on the shared small model (llama-8b): fine for FAQ questions"
show "tools/chat.sh assistant.example"
say "Its goal this quarter: Assistant Pro, for enterprise customers. The small model can't handle their questions:"
show "tools/chat.sh assistant.example 'Can I get a discount on my renewal?'"
pause

say "team-research's fine-tuned model can. team-assistant points Assistant Pro at it, in another namespace"
show "yq '.spec.rules' scenarios/borrow-a-model/assistant-research-ft.yaml"
expect_ok "tenant apply -f scenarios/borrow-a-model/assistant-research-ft.yaml"
wait_for 30 "kubectl -n team-assistant get httproute assistant-pro -o json | jq -e '.status.parents[0].conditions[] | select(.type == \"ResolvedRefs\" and .reason == \"RefNotPermitted\")'"
pause

say "The gateway controller refuses it: a pool in another namespace needs that namespace's consent"
show "kubectl -n team-assistant get httproute assistant-pro -o json | jq -r '.status.parents[0].conditions[] | select(.type == \"ResolvedRefs\") | .reason + \": \" + .message'"
pause

say "team-research's role lets it create ReferenceGrants, so teams can share their own Services"
# `can-i` prints yes/no and exits non-zero on no: make check fails if the tenant role changes.
expect_ok "kubectl auth can-i create referencegrants -n team-research --as team-research-dev"
say "RBAC can't see what a grant points at. team-research shares its model:"
expect_ok "research apply -f scenarios/borrow-a-model/grant.yaml"
wait_for 30 "tools/chat.sh assistant-pro.example | grep -q INTERNAL"
show "kubectl -n team-assistant get httproute assistant-pro -o json | jq -r '.status.parents[0].conditions[] | select(.type == \"ResolvedRefs\") | .reason + \": \" + .message'"
pause

say "The same question, on Assistant Pro"
show "tools/chat.sh assistant-pro.example 'Can I get a discount on my renewal?'"
pause
