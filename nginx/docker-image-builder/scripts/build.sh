#!/bin/bash

# https://docs.nginx.com/nginx/admin-guide/installing-nginx/installing-nginx-docker/#docker_plus

BANNER="NGINX Docker Image builder\n\n
This tool builds a Docker image to run NGINX Plus/Open Source, NGINX App Protect WAF and NGINX Agent\n\n
=== Usage:\n\n
$0 [options]\n\n
=== Options:\n\n
-h\t\t\t- This help\n
-t [target image]\t- The Docker image to be created\n
-C [file.crt]\t\t- Certificate to pull packages from the official NGINX repository\n
-K [file.key]\t\t- Key to pull packages from the official NGINX repository\n
-w\t\t\t- Add NGINX App Protect WAF (requires NGINX Plus)\n
-O\t\t\t- Use NGINX Open Source instead of NGINX Plus\n
-u\t\t\t- Build unprivileged image (only for NGINX Plus)\n
-i [uid:gid]\t\t- Set NGINX UID and GID (only for unprivileged images)\n
-a [2|3]\t\t- Add NGINX Agent v2 or v3\n\n
=== Examples:\n\n
NGINX Plus and NGINX Agent image:\n
  $0 -C nginx-repo.crt -K nginx-repo.key -t registry.ff.lan:31005/nginx-docker:plus-agent-root -a 2\n\n

NGINX Plus, NGINX App Protect WAF and NGINX Agent image:\n
  $0 -C nginx-repo.crt -K nginx-repo.key -t registry.ff.lan:31005/nginx-docker:plus-nap-agent-root -w -a 2\n\n

NGINX Plus, NGINX App Protect WAF and NGINX Agent unprivileged image:\n
  $0 -C nginx-repo.crt -K nginx-repo.key -t registry.ff.lan:31005/nginx-docker:plus-nap-agent-nonroot -w -u -a 2\n\n

NGINX Plus, NGINX App Protect WAF and NGINX Agent unprivileged image, custom UID and GID:\n
  $0 -C nginx-repo.crt -K nginx-repo.key -t registry.ff.lan:31005/nginx-docker:plus-nap-agent-nonroot -w -u -i 1234:1234 -a 2\n\n

NGINX Opensource and NGINX Agent image:\n
  $0 -O -t registry.ff.lan:31005/nginx-docker:oss-root -a 2\n"

NGINX_UID=101
NGINX_GID=101

while getopts 'ht:C:K:a:wOui:' OPTION
do
  case "$OPTION" in
    h)
      echo -e $BANNER
      exit
    ;;
    t)
      IMAGENAME=$OPTARG
    ;;
    C)
      NGINX_CERT=$OPTARG
    ;;
    K)
      NGINX_KEY=$OPTARG
    ;;
    a)
      NGINX_AGENT=true
      NGINX_AGENT_VERSION=$OPTARG
    ;;
    w)
      NAP_WAF=true
    ;;
    O)
      NGINX_OSS=true
    ;;
    u)
      UNPRIVILEGED=true
    ;;
    i)
      NGINX_UID=`echo $OPTARG | awk -F: '{print $1}'`
      NGINX_GID=`echo $OPTARG | awk -F: '{print $2}'`
    ;;
  esac
done

if [ -z "$1" ]
then
  echo -e $BANNER
  exit
fi

if [ -z "${IMAGENAME}" ]
then
  echo "Docker image name is required"
  exit
fi

if [ -z "${NGINX_AGENT_VERSION}" ]
then
  echo "NGINX Agent version is required"
  exit
fi

if ([ -z "${NGINX_OSS}" ] && ([ -z "${NGINX_CERT}" ] || [ -z "${NGINX_KEY}" ]) )
then
  echo "NGINX certificate and key are required for NGINX Plus"
  exit
fi

if ([ -z "${NGINX_UID}" ] || -z "${NGINX_GID}" ])
then
  echo "Invalid UID and/or GID"
  exit
fi

if [ "${NGINX_AGENT}" ]
then
  if [ "${NGINX_AGENT_VERSION}" -eq "2" ] || [ "${NGINX_AGENT_VERSION}" -eq "3" ]
  then
    echo "=> Building with NGINX Agent v${NGINX_AGENT_VERSION}"
  else
    echo "NGINX Agent version must be either '2' or '3'"
    exit
  fi
fi

echo "=> Target docker image is $IMAGENAME"

if ([ ! -z "${NAP_WAF}" ] && [ -z "${NGINX_OSS}" ])
then
  echo "=> Building with NGINX App Protect WAF"
  OPT_PLATFORM="--platform linux/amd64" # for NGINX App Protect WAF, which is only available for x86_64
fi

if [ -z "${NGINX_OSS}" ]
then
  if [ -z "${UNPRIVILEGED}" ]
  then
    DOCKERFILE_NAME=Dockerfile.plus
    echo "=> Building with NGINX Plus"
  else
    DOCKERFILE_NAME=Dockerfile.plus.unprivileged
    echo "=> Building with NGINX Plus unprivileged"
  fi

  echo "=> Using UID:GID $NGINX_UID:$NGINX_GID"

  DOCKER_BUILDKIT=1 docker build --no-cache -f $DOCKERFILE_NAME \
    --secret id=nginx-key,src=$NGINX_KEY --secret id=nginx-crt,src=$NGINX_CERT \
    --build-arg NAP_WAF=$NAP_WAF --build-arg NGINX_AGENT=$NGINX_AGENT \
    --build-arg NGINX_AGENT_VERSION=$NGINX_AGENT_VERSION \
    --build-arg UID=$NGINX_UID \
    --build-arg GID=$NGINX_GID \
    $OPT_PLATFORM \
    -t $IMAGENAME .
else
  echo "=> Building with NGINX Open Source"
  DOCKER_BUILDKIT=1 docker build --no-cache -f Dockerfile.oss \
    --build-arg NGINX_AGENT=$NGINX_AGENT \
    --build-arg NGINX_AGENT_VERSION=$NGINX_AGENT_VERSION \
    -t $IMAGENAME .
fi

echo "=> Build complete for $IMAGENAME"
