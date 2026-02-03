# NGINX Docker image builder

## Description

This repository can be used to build a docker image that includes:

- [NGINX Plus](https://docs.nginx.com/nginx) in privileged or unprivileged/non-root mode
- [NGINX Open Source](https://nginx.org/)
- [F5 WAF for NGINX](https://docs.nginx.com/waf)
- [NGINX Agent](https://docs.nginx.com/nginx-agent)

## Tested releases

This repository has been tested with:

- [NGINX Plus](https://docs.nginx.com/nginx) R33+
- [NGINX Open Source](https://nginx.org) 1.24.0+
- [NGINX Agent](https://docs.nginx.com/nginx-agent) 2.19+
- [NGINX Instance Manager](https://docs.nginx.com/nginx-instance-manager) 2.20+
- [F5 WAF for NGINX](https://docs.nginx.com/waf) 5+
- [NGINX One Console](https://docs.nginx.com/nginx-one-console)

## Prerequisites

- Linux host running Docker to build the image
- NGINX Plus license
- Access to either control plane:
  - [NGINX Instance Manager](https://docs.nginx.com/nginx-instance-manager)
  - [NGINX One Cloud Console](https://docs.nginx.com/nginx-one-console)
- Docker/Docker-compose or Openshift/Kubernetes cluster

## Building the docker image

The `./scripts/build.sh` install script can be used to build the Docker image:

```shell
NGINX Docker Image builder

 This tool builds a Docker image to run NGINX Plus/Open Source, F5 WAF for NGINX and NGINX Agent

 === Usage:

 ./scripts/build.sh [options]

 === Options:

 -h                     - This help
 -t [target image]      - The Docker image to be created
 -C [file.crt]          - Certificate to pull packages from the official NGINX repository
 -K [file.key]          - Key to pull packages from the official NGINX repository
 -w                     - Add F5 WAF for NGINX (requires NGINX Plus)
 -O                     - Use NGINX Open Source instead of NGINX Plus
 -u                     - Build unprivileged image (only for NGINX Plus)
 -i [uid:gid]           - Set NGINX UID and GID (only for unprivileged images)
 -a [2|3]               - Add NGINX Agent v2 or v3

 === Examples:

 NGINX Plus and NGINX Agent image:
 ./scripts/build.sh -C nginx-repo.crt -K nginx-repo.key -t registry.ff.lan:31005/nginx-docker:plus-agent-root -a 2

 NGINX Plus, F5 WAF for NGINX and NGINX Agent image:
 ./scripts/build.sh -C nginx-repo.crt -K nginx-repo.key -t registry.ff.lan:31005/nginx-docker:plus-nap-agent-root -w -a 2

 NGINX Plus, F5 WAF for NGINX and NGINX Agent unprivileged image:
 ./scripts/build.sh -C nginx-repo.crt -K nginx-repo.key -t registry.ff.lan:31005/nginx-docker:plus-nap-agent-nonroot -w -u -a 2

 NGINX Plus, F5 WAF for NGINX and NGINX Agent unprivileged image, custom UID and GID:
 ./scripts/build.sh -C nginx-repo.crt -K nginx-repo.key -t registry.ff.lan:31005/nginx-docker:plus-nap-agent-nonroot -w -u -i 1234:1234 -a 2

 NGINX Opensource and NGINX Agent image:
 ./scripts/build.sh -O -t registry.ff.lan:31005/nginx-docker:oss-root -a 2
```

### Agent version compatibility

When using NGINX Agent, you need to be mindful of compatibility with other NGINX Products. Review the [Agent compatibility list](https://docs.nginx.com/nginx-agent/technical-specifications/#nginx-agent-30-compatibility) for details.
Based on the information in the compatibility matrix, set the `-a` option accordingly.

## Steps

1. Clone this repository
2. For NGINX Plus only: get your license certificate and key
3. Build the Docker image using `./scripts/build.sh`

### Running the docker image on Kubernetes

1. Edit `manifests/nginx-manifest.yaml` and specify the correct image by modifying the `image:` line, and set the following environment variables

- `NGINX_LICENSE` - NGINX R33+ JWT license token
- `NGINX_AGENT_ENABLED` - NGINX Agent enabled or not
- `NGINX_AGENT_SERVER_HOST` - NGINX Instance Manager / NGINX One Console hostname/IP address
- `NGINX_AGENT_SERVER_GRPCPORT` - NGINX Instance Manager / NGINX One Console gRPC port
- `NGINX_AGENT_SERVER_TOKEN` - NGINX One Console authentication token (not needed for NGINX Instance Manager)
- `NGINX_AGENT_INSTANCE_GROUP` - instance group (NGINX Instance Manager) / config sync group (NGINX One Console) for the NGINX instance
- `NGINX_AGENT_TAGS` - comma separated list of tags for the NGINX instance
- `NAP_WAF` - set to `"true"` to enable F5 WAF for NGINX (docker image built using `-w`) - NGINX Plus only
- `NAP_WAF_PRECOMPILED_POLICIES` - set to `"true"` to enable F5 WAF for NGINX precompiled policies (docker image built using `-w`) - NGINX Plus only
- `NGINX_AGENT_LOG_LEVEL` - NGINX Agent loglevel, optional. If not specified defaults to `info`

1. Deploy on Kubernetes using the example manifest `manifest/nginx-manifest.yaml`

1. After startup the NGINX instance will register to NGINX Instance Manager / NGINX One console and will be displayed on the "instances" dashboard if the NGINX Agent has been build into the docker image

### Running the docker image on Docker

1. Start using

```shell
docker run --rm --name nginx -p [PORT_TO_EXPOSE] \
  -e "NGINX_LICENSE=<NGINX_JWT_LICENSE_TOKEN>" \
  -e "NGINX_AGENT_ENABLED=[true|false]" \
  -e "NGINX_AGENT_SERVER_HOST=<NGINX_INSTANCE_MANAGER_OR_NGINX_ONE_CONSOLE_FQDN_OR_IP>" \
  -e "NGINX_AGENT_SERVER_GRPCPORT=<NGINX_INSTANCE_MANAGER_OR_NGINX_ONE_CONSOLE_GRPC_PORT>" \
  -e "NGINX_AGENT_TLS_ENABLE=[true|false]" \
  -e "NGINX_AGENT_TLS_SKIP_VERIFY=[true|false]" \
  -e "NGINX_AGENT_SERVER_TOKEN=<NGINX_ONE_CONSOLE_AUTHENTICATION_TOKEN>" \
  -e "NGINX_AGENT_INSTANCE_GROUP=<NGINX_INSTANCE_MANAGER_OR_NGINX_ONE_CONSOLE_OPTIONAL_INSTANCE_GROUP_OR_CONFIG_SYNC_GROUP_NAME>" \
  -e "NGINX_AGENT_TAGS=<OPTIONAL_COMMA_DELIMITED_TAG_LIST>" \
  -e "NAP_WAF=[true|false]" \
  -e "NAP_WAF_PRECOMPILED_POLICIES=[true|false]" \
  -e "NGINX_AGENT_LOG_LEVEL=[panic|fatal|error|info|debug|trace]" \
  -e "NGINX_AGENT_ALLOWED_DIRECTORIES=/etc/nginx:/usr/local/etc/nginx:/usr/share/nginx/modules:/etc/nms" \
  <NGINX_DOCKER_IMAGE_NAME:TAG>
```

1. After startup the NGINX instance will register to NGINX Instance Manager / NGINX One Console and will be displayed on the "instances" dashboard if the NGINX Agent has been build into the docker image
