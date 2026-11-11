#!/usr/bin/env bash
# "Skip the gateway", with the policy: G1 generates a NetworkPolicy in every tenant namespace.
source "$(dirname "$0")/../lib.sh"
clip "Skip the gateway · A NetworkPolicy for every tenant"

say "Model servers accept traffic only from the gateway and their endpoint picker"
say "A GeneratingPolicy writes that NetworkPolicy into every tenant and platform namespace."
install_policy g1-tenant-network-isolation
wait_for 60 "kubectl -n models get networkpolicy model-server-ingress"
show "kubectl get networkpolicies -A -l app.kubernetes.io/managed-by=kyverno"
pause
POD_IP=$(kubectl -n models get pods -l model-pool=llama-70b -o jsonpath='{.items[0].status.podIP}')
export POD_IP
say "The direct call again"
# Same call shape as before.sh, so the script expands $POD_IP (not the pod). curl exit 28 is a timeout (the
# NetworkPolicy dropping packets); any other failure, like an empty POD_IP, fails make check.
expect_denied "(28)" "tenant -n team-assistant run direct-call --rm -i --restart=Never --quiet --image=\$CURL_IMAGE --image-pull-policy=Never -- \\
  curl -sS -m 5 -o /dev/null http://\$POD_IP:8000/v1/models"
pause
say "Through the gateway, a compliant route still works"
expect_ok "tenant apply -f scenarios/skip-the-gateway/assistant-v2.yaml"
wait_for 30 "tools/chat.sh assistant-v2.example | grep -q 'model: llama-8b'"
show "tools/chat.sh assistant-v2.example"
pause
