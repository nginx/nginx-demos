[![Project Status: Active – The project has reached a stable, usable state and is being actively developed.](https://www.repostatus.org/badges/latest/active.svg)](https://www.repostatus.org/#active)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/nginx/nginx-demos/badge)](https://securityscorecards.dev/viewer/?uri=github.com/nginx/nginx-demos)
[![Community Support](https://badgen.net/badge/support/community/cyan?icon=awesome)](/SUPPORT.md)
[![Community Forum](https://img.shields.io/badge/community-forum-009639?logo=discourse&link=https%3A%2F%2Fcommunity.nginx.org)](https://community.nginx.org)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-2.1-4baaaa.svg)](/CODE_OF_CONDUCT.md)

# NGINX Demos

This repository contains a collection of curated and updated NGINX demos covering NGINX offerings.

## Repository Structure

The demos are divided by NGINX product offering into unique distinct folders. Each folder then contains one or more demos covering various use cases within the respective product offering.

Each demo might have unique deployment requirements. Please refer to each individual README for more details.

## Available Demos

### NGINX

|Title|Description|Owner|
|-----|-----------|-----|
|[NGINX advanced healthchecks](nginx/advanced-healthchecks/)|Advanced active healthchecks for NGINX Plus|@fabriziofiorucci|
|[NGINX API gateway](nginx/api-gateway/)|Configure NGINX as an API gateway|@alessfg|
|[NGINX API steering](nginx/api-steering/)|NGINX as an API gateway using an external data source for authentication, authorization and steering|@fabriziofiorucci|
|[NGINX Docker image builder](nginx/docker-image-builder/)|Tool to build several Docker images for NGINX Plus, NGINX App Protect, NGINX Agent|@fabriziofiorucci|
|[NGINX multicloud gateway](nginx/multicloud-gateway/)|NGINX setup for URI-based kubernetes traffic routing|@fabriziofiorucci|
|[NGINX SOAP REST](nginx/soap-to-rest/)|Example NGINX configuration to translate between SOAP and REST|@fabriziofiorucci|

### NGINX Gateway Fabric (NGF)

|Title|Description|Owner|
|-----|-----------|-----|
|[NGINX Gateway Fabric traffic splitting](nginx-gateway-fabric/traffic-splitting/)|Simple overview of configuring NGINX Gateway Fabric to route traffic within Kubernetes|@sjberman|

### NGINX Ingress Controller (NIC)

|Title|Description|Owner|
|-----|-----------|-----|
|[NGINX Ingress Controller deployment](nginx-ingress-controller/ingress-deployment/)|Simple overview of deploying and configuring NGINX Ingress Controller|@DylenTurnbull|

### NGINX Instance Manager (NIM)

|Title|Description|Owner|
|-----|-----------|-----|
|[NGINX Instance Manager Docker deployment](nginx-instance-manager/docker-deployment/)|Tool to build docker images for NGINX Instance Manager|@fabriziofiorucci|

### NGINX Workshops

|Title|Description|Owner|
|-----|-----------|-----|
|[NGINX basics](nginx-workshops/README.md)|A 101 level introduction to NGINX|@apcurrier, @chrisakker, @sdutta9 |
|[NGINX Ingress Controller](nginx-workshops/README.md)|Learn everything you need to get started with NGINX Ingress Controller and its capabilities|@apcurrier, @chrisakker, @sdutta9|
|[NGINXaaS for Azure](nginx-workshops/README.md)|Learn everything you need to get started with NGINX as a Service for Azure (NGINXaaS) and its capabilities|@apcurrier, @chrisakker, @sdutta9|
|[NGINX One Console](nginx-workshops/README.md)|Learn everything you need to get started with NGINX One Console and its capabilities|@apcurrier, @chrisakker, @sdutta9|

## Contributing

Please see the [contributing guide](/CONTRIBUTING.md) for guidelines on how to best contribute to this project.

## License

[Apache License, Version 2.0](/LICENSE)

&copy; [F5, Inc.](https://www.f5.com/) 2025
