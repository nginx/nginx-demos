#!/bin/bash

case $1 in
	'clean')
		kubectl delete secret mcgw.ff.lan -n nginx-mcgw
		rm mcgw.ff.lan.key mcgw.ff.lan.crt
	;;
	'install')
		openssl req -x509 -nodes -days 3650 -newkey rsa:2048 -keyout mcgw.ff.lan.key -out mcgw.ff.lan.crt -config mcgw.ff.lan.cnf
		kubectl create secret tls mcgw.ff.lan --key mcgw.ff.lan.key --cert mcgw.ff.lan.crt -n nginx-mcgw
	;;
	*)
		echo "$0 [clean|install]"
		exit
	;;
esac
