#!/usr/bin/env bash
# "Skip the gateway", before the policy
# Every admission rule so far guards config. This creates no config: it calls the llama-70b
# pods directly from a pod in team-assistant's namespace. Nothing for admission to see.
source "$(dirname "$0")/../lib.sh"
clip "Skip the gateway"

say "No route this time: team-assistant calls a llama-70b pod by its IP"
show "tenant -n models get pods -l model-pool=llama-70b -o wide"
POD_IP=$(kubectl -n models get pods -l model-pool=llama-70b -o jsonpath='{.items[0].status.podIP}')
export POD_IP
pause
expect_ok "tenant -n team-assistant run direct-call --rm -i --restart=Never --quiet --image=\$CURL_IMAGE --image-pull-policy=Never -- \\
  curl -s -m 5 http://\$POD_IP:8000/v1/chat/completions -H 'content-type: application/json' \\
  -d '{\"model\":\"auto\",\"max_tokens\":30,\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}' | jq -er '.model + \": \" + .choices[0].message.content'"
say "The call skipped the gateway and the allowlist, and created nothing for admission to check."
pause
