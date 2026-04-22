# NGINX Instance Manager unprivileged docker image

This repository builds an unprivileged / non-root NGINX Instance Manager docker image

## Docker image creation

Docker image creation is supported for:

- [NGINX Instance Manager](https://docs.nginx.com/nginx-instance-manager/) 2.21+

## Tested releases

This repository has been tested on `amd64` and `arm64` architectures with:

- NGINX Instance Manager 2.21.1


## Prerequisites

This repository has been tested with:

- Valid NGINX license certificate and key to fetch NGINX Instance Manager packages
- Docker Engine - Community 28.1.1 to build the image
- Linux host running Docker to build the image

## How to build

The install script can be used to build the Docker image:
```code
$ ./build.sh
NGINX Instance Manager Docker image builder

 This tool builds a Docker image to run NGINX Instance Manager

 === Usage:

 ./build.sh [options]

 === Options:

 -h                     - This help
 -t [target image]      - Docker image name to be created

 -C [file.crt]          - Certificate file to pull packages from the official NGINX repository
 -K [file.key]          - Key file to pull packages from the official NGINX repository

 === Examples:

        ./build.sh -C nginx-repo.crt -K nginx-repo.key -t my.registry.tld/nginx-instance-manager:latest

```

### Building the docker image

1. Clone this repository
2. Get your license certificate and key to fetch NGINX Instance Manager packages from the NGINX repository
3. Build NGINX Instance Manager Docker image using:
```code
./build.sh -C <NGINX_CERTIFICATE> -K <NGINX_CERTIFICATE_KEY> -t <TARGET_DOCKER_IMAGE_NAME>
```

### Running with Docker compose

See [docker-compose](docker-compose)
