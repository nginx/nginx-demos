#!/bin/bash

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
yq '.clickhouse.address=strenv(NIM_CLICKHOUSE_ADDRESSPORT)|.clickhouse.username=strenv(NIM_CLICKHOUSE_USERNAME)|.clickhouse.password=strenv(NIM_CLICKHOUSE_PASSWORD)' /etc/nms/nms.conf > /etc/nms/nms.conf-updated
mv /etc/nms/nms.conf-updated /etc/nms/nms.conf
#chown nms:nms /etc/nms/nms.conf
#chmod 644 /etc/nms/nms.conf

yq '.clickhouse.address="tcp://"+strenv(NIM_CLICKHOUSE_ADDRESSPORT)|.clickhouse.username=strenv(NIM_CLICKHOUSE_USERNAME)|.clickhouse.password=strenv(NIM_CLICKHOUSE_PASSWORD)' /etc/nms/nms-sm-conf.yaml > /etc/nms/nms-sm-conf.yaml-updated
mv /etc/nms/nms-sm-conf.yaml-updated /etc/nms/nms-sm-conf.yaml
#chown nms:nms /etc/nms/nms-sm-conf.yaml
#chmod 644 /etc/nms/nms-sm-conf.yaml

# Start nms-core - from /lib/systemd/system/nms-core.service
/bin/bash -c '`which mkdir` -p /var/lib/nms/dqlite/'
/bin/bash -c '`which mkdir` -p /var/lib/nms/secrets/'
/bin/bash -c '`which mkdir` -p /var/run/nms/'
/bin/bash -c '`which mkdir` -p /var/log/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/log/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/run/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/lib/nms/'
/bin/bash -c '`which chmod` 0775 /var/log/nms/'
/bin/bash -c '`which chown` -R nms:nms /etc/nms/certs/services/core'
/bin/bash -c '`which chown` nms:nms /etc/nms/certs/services/ca.crt'
/bin/bash -c '`which chmod` 0700 /etc/nms/certs/services/core'
/bin/bash -c '`which chmod` 0600 /etc/nms/certs/services/core/*'
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-core &

# Start nms-dpm - from /lib/systemd/system/nms-dpm.service
/bin/bash -c '`which mkdir` -p /var/lib/nms/streaming/'
/bin/bash -c '`which mkdir` -p /var/lib/nms/dqlite/'
/bin/bash -c '`which mkdir` -p /var/run/nms/'
/bin/bash -c '`which mkdir` -p /var/log/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/lib/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/run/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/log/nms/'
/bin/bash -c '`which chmod` 0775 /var/log/nms/'
/bin/bash -c '`which chown` -R nms:nms /etc/nms/certs/services/dataplane-manager'
/bin/bash -c '`which chown` nms:nms /etc/nms/certs/services/ca.crt'
/bin/bash -c '`which chmod` 0700 /etc/nms/certs/services/dataplane-manager'
/bin/bash -c '`which chmod` 0600 /etc/nms/certs/services/dataplane-manager/*'
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-dpm &

# Start nms-ingestion - from /lib/systemd/system/nms-ingestion.service
/bin/bash -c '`which mkdir` -p /var/run/nms/'
/bin/bash -c '`which mkdir` -p /var/log/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/log/nms/'
/bin/bash -c '`which chmod` 0775 /var/log/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/run/nms/'
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-ingestion &

# Start nms-integrations - from /lib/systemd/system/nms-integrations.service
/bin/bash -c '`which mkdir` -p /var/lib/nms/dqlite/'
/bin/bash -c '`which mkdir` -p /var/run/nms/'
/bin/bash -c '`which mkdir` -p /var/log/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/lib/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/run/nms/'
/bin/bash -c '`which chown` -R nms:nms /var/log/nms/'
/bin/bash -c '`which chmod` 0775 /var/log/nms/'
/bin/bash -c '`which chown` nms:nms /etc/nms/certs/services/ca.crt'
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-integrations &

sleep 5

# Start nms-sm - from /lib/systemd/system/nms-sm.service
function repeat { while [ 1 ] ; do "$@" ; sleep 1 ; done; };repeat /usr/bin/nms-sm start &

chmod 666 /var/run/nms/*.sock

/etc/init.d/nginx start

# License activation
if ((${#NIM_LICENSE[@]}))
then
	curl -s -X PUT -k https://127.0.0.1/api/platform/v1/license -u "$NIM_USERNAME:$NIM_PASSWORD" -d '{ "desiredState": { "content": "'$NIM_LICENSE'" }, "metadata": { "name": "license" } }' -H "Content-Type: application/json"
fi

while [ 1 ]
do
	sleep 60
done
