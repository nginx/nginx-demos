#!/bin/bash

BANNER="NGINX Instance Manager Docker image builder\n\n
This tool builds a Docker image to run NGINX Instance Manager\n\n
=== Usage:\n\n
$0 [options]\n\n
=== Options:\n\n
-h\t\t\t- This help\n
-t [target image]\t- Docker image name to be created\n\n
-C [file.crt]\t\t- Certificate file to pull packages from the official NGINX repository\n
-K [file.key]\t\t- Key file to pull packages from the official NGINX repository\n\n
=== Examples:\n\n
\t$0 -C nginx-repo.crt -K nginx-repo.key -t my.registry.tld/nginx-instance-manager:latest\n
"

while getopts 'hn:p:t:sC:K:A' OPTION
do
	case "$OPTION" in
		h)
			echo -e $BANNER
			exit
		;;
		n)
			DEBFILE=$OPTARG
		;;
		p)
			PUM_IMAGE=$OPTARG
		;;
		t)
			IMGNAME=$OPTARG
		;;
		C)
			NGINX_CERT=$OPTARG
		;;
		K)
			NGINX_KEY=$OPTARG
		;;
	esac
done

if [ -z "$1" ]
then
	echo -e $BANNER
	exit
fi

if [ -z "${IMGNAME}" ]
then
	echo "Docker image name is required"
	exit
fi

if ([ -z "${NGINX_CERT}" ] || [ -z "${NGINX_KEY}" ])
then
	echo "NGINX license certificate and key are required"
        exit
fi

echo "==> Building NGINX Management Suite docker image"

DOCKER_BUILDKIT=1 docker build --no-cache -f Dockerfile --secret id=nginx-key,src=$NGINX_KEY --secret id=nginx-crt,src=$NGINX_CERT -t $IMGNAME .
