#!/bin/bash

PROJECT_NAME=nginx-api-steering
DOCKERCOMPOSE=docker-compose.yaml
NAMESPACE=nginx-api-steering

BANNER="NGINX API Steering lab - https://github.com/fabriziofiorucci/NGINX-API-Steering\n\n
=== Usage:\n\n
$0 [options]\n\n
=== Options:\n\n
-h\t\t\t\t\t\t- This help\n
-o [start|stop|start-k8s|stop-k8s|build]\t- Action\n
-C [file.crt]\t\t\t\t\t- Certificate file to pull packages from the official NGINX repository\n
-K [file.key]\t\t\t\t\t- Key file to pull packages from the official NGINX repository\n\n
=== Examples:\n\n
Build docker images:\n
\t$0 -o build -C /etc/ssl/nginx/nginx-repo.crt -K /etc/ssl/nginx/nginx-repo.key\n\n
\tNote: Images are built as\n
\t- nginx-api-steering:latest\n
\t- api-server:latest\n
\t- backend:latest\n\n
docker-compose lab start (build images if needed):\n
\t$0 -o start -C /etc/ssl/nginx/nginx-repo.crt -K /etc/ssl/nginx/nginx-repo.key\n\n
docker-compose lab stop:\n
\t$0 -o stop\n\n
Kubernetes lab start:\n
\t$0 -o start-k8s\n\n
\tPrerequisites:\n\n
\t1. Docker images must be built using the \"build\" action\n
\t2. Docker images must be pushed to a local registry\n
\t3. Docker images in the local registry names must be referenced in the kubernetes.yaml file\n
\tNote: The lab is deployed in the \"$NAMESPACE\" namespace\n\n
Kubernetes lab stop:\n\n
\tNote: The \"$NAMESPACE\" namespace is deleted\n\n
\t$0 -o stop-k8s\n
"


while getopts 'ho:C:K:' OPTION
do
  case "$OPTION" in
    h)
      echo -e $BANNER
      exit
    ;;
    o)
      MODE=$OPTARG
    ;;
    C)
      export NGINX_CERT=$OPTARG
    ;;
    K)
     export NGINX_KEY=$OPTARG
    ;;
  esac
done

if [ -z "$1" ] || [ -z "${MODE}" ]
then
  echo -e $BANNER
  exit
fi

case $MODE in
  'start'|'build')
    if [ -z "${NGINX_CERT}" ] || [ -z "${NGINX_KEY}" ]
    then
      echo "Missing NGINX Plus certificate/key"
      exit
    fi

    if [ "$MODE" = "start" ]
    then
      DOCKER_BUILDKIT=1 docker-compose -p $PROJECT_NAME -f $DOCKERCOMPOSE up -d --remove-orphans
    else
      DOCKER_BUILDKIT=1 docker-compose -p $PROJECT_NAME -f $DOCKERCOMPOSE build
    fi
  ;;
  'stop')
    export NGINX_CERT="x"
    export NGINX_KEY="x"
    docker-compose -p $PROJECT_NAME -f $DOCKERCOMPOSE down
  ;;
  'start-k8s')
    echo "Deploying on Kubernetes namespace $NAMESPACE"
    kubectl create ns $NAMESPACE

    kubectl create configmap backend-db -n $NAMESPACE \
      --from-file=db.json=backend/db-k8s.json

    kubectl create configmap nginx-conf -n $NAMESPACE \
      --from-file=nginx.conf=nginx/nginx.conf \
      --from-file=api.conf=nginx/api.conf \
      --from-file=steering.js=nginx/steering.js \
      --from-file=steering.conf=nginx/steering.conf-k8s \
      --from-file=default.conf=/dev/null

    kubectl apply -n $NAMESPACE -f kubernetes.yaml

  ;;
  'stop-k8s')
    echo "Removing Kubernetes namespace $NAMESPACE"
    kubectl delete ns $NAMESPACE
  ;;
  *)
    echo -e $BANNER
    exit
  ;;
esac
