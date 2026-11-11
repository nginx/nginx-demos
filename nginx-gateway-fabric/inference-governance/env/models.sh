#!/usr/bin/env bash
# Renders one simulated model: sim Deployment + EPP + InferencePool (from the llm-d-router chart).
# Usage: models.sh <name> <namespace> <gpu-class> <dataset> <cost-center|-> <failure-mode>
#   cost-center "-" renders the model with no cost-center label at all ("Shadow GPU").
# Prints YAML to stdout. Needs helm and a llm-d-router checkout in $LLMD_ROUTER_DIR.
set -euo pipefail
name=$1 ns=$2 gpu=$3 dataset=$4 cost=$5 failmode=$6
if [ "$cost" = - ]; then cc_yaml="" cc_flow=""; else cc_yaml="cost-center: $cost" cc_flow=", cost-center: $cost"; fi
chart="$LLMD_ROUTER_DIR/config/charts/llm-d-router-gateway"
[ -d "$chart/charts" ] || helm dependency build "$chart" >/dev/null

helm template "$name" "$chart" -n "$ns" \
  --set "router.modelServers.matchLabels.model-pool=$name" \
  --set "router.inferencePool.failureMode=$failmode" \
  --set "router.epp.image.tag=$LLMD_ROUTER_VERSION" \
  --set router.epp.image.pullPolicy=Never \
  --set router.epp.resources.requests.cpu=50m \
  --set router.epp.resources.requests.memory=128Mi \
  --set router.epp.resources.limits.memory=512Mi \
| yq "(select(.kind == \"InferencePool\") | .metadata.labels) += {\"model\": \"$name\"}" \
| if [ "$cost" = - ]; then cat; else yq "(select(.kind == \"InferencePool\") | .metadata.labels.cost-center) = \"$cost\""; fi

cat <<YAML
---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: $name
  namespace: $ns
  labels: {model-pool: $name$cc_flow}
spec:
  replicas: 1
  selector:
    matchLabels: {model-pool: $name}
  template:
    metadata:
      labels:
        model-pool: $name
        $cc_yaml
        gpu.example.com/class: $gpu
        inference.example.com/role: model-server
    spec:
      containers:
      - name: sim
        image: ghcr.io/llm-d/llm-d-inference-sim:$INFERENCE_SIM_VERSION
        imagePullPolicy: Never   # preloaded by env/images.sh
        # Served as "<name>" and "auto": clients always ask for "auto", the reply names the real model.
        args: [--model, $name, --served-model-name, $name, auto, --port, "8000", --dataset-path, /data/dataset.sqlite3,
               --time-to-first-token, 200ms, --inter-token-latency, 20ms, --max-num-seqs, "8"]
        ports: [{name: http, containerPort: 8000}]
        readinessProbe: {httpGet: {path: /health, port: http}}
        resources: {requests: {cpu: 10m, memory: 64Mi}}
        volumeMounts: [{name: dataset, mountPath: /data}]
      volumes:
      - name: dataset
        configMap: {name: dataset-$dataset, items: [{key: dataset.sqlite3, path: dataset.sqlite3}]}
YAML
