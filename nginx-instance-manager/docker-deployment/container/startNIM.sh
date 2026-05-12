#!/bin/bash
set -euo pipefail

# Makes sure that Clickhouse is up and running - dedicated pod

RETCODE=-1
CH_ADDRESS=`echo $NIM_CLICKHOUSE_ADDRESSPORT | awk -F\: '{print $1}'`
CH_PORT=`echo $NIM_CLICKHOUSE_ADDRESSPORT | awk -F\: '{print $2}'`
while [ ! $RETCODE = 0 ]
do
        nc -z $CH_ADDRESS $CH_PORT
        RETCODE=$?
	echo "Waiting for ClickHouse on $CH_ADDRESS port $CH_PORT ..."
        sleep 3
done

/etc/nms/scripts/basic_passwords.sh $NIM_USERNAME $NIM_PASSWORD

# NGINX Instance Manager configuration update
# Each line sets one key in-place. Add new variables here as needed.
set_nms_conf()  { yq -i "$1=strenv($2)" /etc/nms/nms.conf; }
set_nms_sm()    { yq -i "$1=strenv($2)" /etc/nms/nms-sm-conf.yaml; }

set_nms_conf '.clickhouse.address'                      NIM_CLICKHOUSE_ADDRESSPORT
set_nms_conf '.clickhouse.username'                     NIM_CLICKHOUSE_USERNAME
set_nms_conf '.clickhouse.password'                     NIM_CLICKHOUSE_PASSWORD
set_nms_conf '.integrations.license.mode_of_operation'  NIM_LICENSE_MODE_OF_OPERATION

set_nms_sm '.clickhouse.address'    NIM_CLICKHOUSE_ADDRESSPORT
set_nms_sm '.clickhouse.username'   NIM_CLICKHOUSE_USERNAME
set_nms_sm '.clickhouse.password'   NIM_CLICKHOUSE_PASSWORD

# Create all required runtime directories once
mkdir -p \
    /var/lib/nms/dqlite/ \
    /var/lib/nms/secrets/ \
    /var/lib/nms/streaming/ \
    /var/run/nms/ \
    /var/log/nms/

# Start nms-core - from /lib/systemd/system/nms-core.service
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-core &

# Start nms-dpm - from /lib/systemd/system/nms-dpm.service
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-dpm &

# Start nms-ingestion - from /lib/systemd/system/nms-ingestion.service
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-ingestion &

# Start nms-integrations - from /lib/systemd/system/nms-integrations.service
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-integrations &

sleep 5

# Start nms-sm - from /lib/systemd/system/nms-sm.service
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-sm start &

chmod 666 /var/run/nms/*.sock

nginx

# License activation
if [ -n "${NIM_LICENSE:-}" ]; then
	curl -s -X PUT -k https://127.0.0.1/api/platform/v1/license -u "$NIM_USERNAME:$NIM_PASSWORD" -d '{ "desiredState": { "content": "'$NIM_LICENSE'" }, "metadata": { "name": "license" } }' -H "Content-Type: application/json"
fi

while [ 1 ]
do
	sleep 60
done
