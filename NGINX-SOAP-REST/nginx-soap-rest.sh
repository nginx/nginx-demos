#!/bin/bash

PROJECT_NAME=nginx-soap-rest
DOCKERCOMPOSE=docker-compose.yaml

BANNER="NGINX SOAP REST lab - https://github.com/fabriziofiorucci/NGINX-SOAP-REST\n\n
=== Usage:\n\n
$0 [options]\n\n
== Options:\n\n
-h\t\t\t- This help\n
-o [start|stop]\t- Start/stop the lab\n
-C [file.crt]\t\t- Certificate file to pull packages from the official NGINX repository\n
-K [file.key]\t\t- Key file to pull packages from the official NGINX repository\n\n
=== Examples:\n\n
Lab start:\n
\t$0 -o start -C /etc/ssl/nginx/nginx-repo.crt -K /etc/ssl/nginx/nginx-repo.key\n\n
Lab stop:\n
\t$0 -o stop\n\n
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
	'start')
		if [ -z "${NGINX_CERT}" ] || [ -z "${NGINX_KEY}" ]
		then
			echo "Missing NGINX Plus certificate/key"
			exit
		fi

		DOCKER_BUILDKIT=1 docker compose -p $PROJECT_NAME -f $DOCKERCOMPOSE up -d --remove-orphans
	;;
	'stop')
		export NGINX_CERT="x"
		export NGINX_KEY="x"
		docker compose -p $PROJECT_NAME -f $DOCKERCOMPOSE down
	;;
	*)
		echo "$0 [start|stop]"
		exit
	;;
esac
