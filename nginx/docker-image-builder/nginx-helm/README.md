# nginx-plus Helm Chart

A production-grade Helm chart for **NGINX Plus** on Kubernetes.  
All environment variables are taken directly from the upstream manifest:  
[nginx-demos/nginx-manifest.yaml](https://github.com/nginx/nginx-demos/blob/main/nginx/docker-image-builder/manifests/nginx-manifest.yaml)

## Deployment modes

| Mode | values file |
|------|-------------|
| NGINX Plus only | `values-nginx-only.yaml` |
| NGINX Plus + NGINX Agent | `values-nginx-agent.yaml` |
| NGINX Plus + Agent + App Protect WAF | `values-nginx-agent-waf.yaml` |

## Prerequisites

- Kubernetes ≥ 1.24
- Helm ≥ 3.10
- NGINX Plus image built from [nginx-demos docker-image-builder](https://github.com/nginx/nginx-demos/tree/main/nginx/docker-image-builder)
- A `license-token` Secret in the target namespace (NGINX Plus R33+):

```bash
kubectl create secret generic license-token \
  --from-file=license.jwt=./license.jwt \
  --type=nginx.com/license \
  -n <namespace>
```

## Quick Start

```bash
# NGINX Plus only
helm install nginx-plus . \
  -f values-nginx-only.yaml \
  --set image.repository=registry.example.com/nginx-plus \
  --set image.tag=r33

# With NGINX Agent (NIM / NGINX One Console)
helm install nginx-plus . \
  -f values-nginx-agent.yaml \
  --set image.repository=registry.example.com/nginx-plus \
  --set image.tag=r33 \
  --set agent.serverHost=nim.example.com \
  --set agent.serverToken=YOUR_TOKEN \
  --set agent.tlsSkipVerify=true

# With Agent + App Protect WAF
helm install nginx-plus . \
  -f values-nginx-agent-waf.yaml \
  --set image.repository=registry.example.com/nginx-plus \
  --set image.tag=r33-nap \
  --set agent.serverHost=nim.example.com \
  --set agent.serverToken=YOUR_TOKEN \
  --set agent.tlsSkipVerify=true
```

## Environment Variables Reference

### License

| Helm value | Env var | Description |
|---|---|---|
| `license.secretName` | `NGINX_LICENSE` (secretKeyRef) | Name of the k8s Secret holding the JWT |
| `license.secretKey` | — | Key within that Secret (default: `license.jwt`) |

### NGINX Agent

| Helm value | Env var | Default | Description |
|---|---|---|---|
| `agent.enabled` | `NGINX_AGENT_ENABLED` | `false` | Enable the NGINX Agent |
| `agent.serverHost` | `NGINX_AGENT_SERVER_HOST` | `""` | NIM / NGINX One Console hostname (**required** when enabled) |
| `agent.serverGrpcPort` | `NGINX_AGENT_SERVER_GRPCPORT` | `"443"` | gRPC port |
| `agent.serverToken` | `NGINX_AGENT_SERVER_TOKEN` | `""` | Authentication token |
| `agent.tlsEnable` | `NGINX_AGENT_TLS_ENABLE` | `"true"` | Enable TLS for agent connection |
| `agent.tlsSkipVerify` | `NGINX_AGENT_TLS_SKIP_VERIFY` | `"false"` | Skip TLS verification |
| `agent.instanceGroup` | `NGINX_AGENT_INSTANCE_GROUP` | `""` | Instance Group / Config Sync Group name |
| `agent.tags` | `NGINX_AGENT_TAGS` | `""` | Comma-separated tags (v2: `"prod,team"` / v3: `"tenant=prod"`) |
| `agent.logLevel` | `NGINX_AGENT_LOG_LEVEL` | `"info"` | Agent log level |
| `agent.allowedDirectories` | `NGINX_AGENT_ALLOWED_DIRECTORIES` | `"/etc/nginx:..."` | Directories the agent may manage |
| `agent.features` | `NGINX_AGENT_FEATURES` | `""` | Comma-separated feature list |

### NGINX App Protect WAF

| Helm value | Env var | Default | Description |
|---|---|---|---|
| `waf.enabled` | `NAP_WAF` | `false` | Enable App Protect WAF |
| `waf.precompiledPolicies` | `NAP_WAF_PRECOMPILED_POLICIES` | `false` | Use precompiled WAF policies |

## Upgrade

```bash
helm upgrade nginx-plus ./nginx-plus -f values-nginx-agent-waf.yaml \
  --set image.tag=r33-nap
```

## Uninstall

```bash
helm uninstall nginx-plus
```
