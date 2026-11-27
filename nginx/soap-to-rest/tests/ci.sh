#!/usr/bin/env bash
# Smoke test: builds the LDAP connector, source of truth and echo server (the NGINX Plus image needs
# a license, so it isn't built here), starts them with the demo's OpenLDAP, and runs the README's
# checks against each. Run from the repo root with .github/scripts/smoke.sh nginx/soap-to-rest.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

openldap=$(sed -n 's/^ *image: \(osixia\/openldap:.*\)$/\1/p' docker-compose.yaml)

build soap-ldap-connector . -f ldap/connector/Dockerfile
build soap-source-of-truth . -f source-of-truth/Dockerfile
build soap-echo-server . -f echo-server/Dockerfile

docker network create smoke-soap >/dev/null
trap 'SMOKE_FAILED=${SMOKE_FAILED:-$?}; smoke_cleanup; docker network rm smoke-soap >/dev/null 2>&1 || true' EXIT

start soap-openldap 15389:389 --network smoke-soap --network-alias openldap -v "$PWD/ldap/ldif:/ldif:ro" "$openldap"
start soap-ldap-connector 15390:5389 --network smoke-soap -e LDAP_SERVER=openldap:389 smoke-soap-ldap-connector
start soap-source-of-truth 15080:10080 smoke-soap-source-of-truth
start soap-echo-server 15800:8000 smoke-soap-echo-server

# Load the demo's users, as the compose healthcheck does. OpenLDAP starts a temporary server to
# configure itself, stops it, then starts for real ("slapd starting"); users added to the
# temporary one are lost, so wait for the real start, then until alice can bind.
for i in $(seq 1 60); do
  docker logs smoke-soap-openldap 2>&1 | grep -q 'slapd starting' && break
  sleep 1
done
for i in $(seq 1 30); do
  docker exec smoke-soap-openldap ldapadd -c -x -D cn=admin,dc=example,dc=org -w admin -H ldap://localhost \
    -f /ldif/setup.ldif >/dev/null 2>&1 || true
  docker exec smoke-soap-openldap ldapwhoami -x -H ldap://localhost \
    -D uid=alice,ou=users,dc=example,dc=org -w testpassword >/dev/null 2>&1 && break
  [ "$i" -lt 30 ] || { echo "FAIL: couldn't load the demo's LDAP users" >&2; SMOKE_FAILED=1; exit 1; }
  sleep 2
done

wait_http http://127.0.0.1:15390/ldap/access/openapi.json
expect "LDAP connector: valid user" 200 'Authentication successful' http://127.0.0.1:15390/ldap/auth \
  -X POST -u alice:testpassword
expect "LDAP connector: wrong password" 401 'Authentication failed' http://127.0.0.1:15390/ldap/auth \
  -X POST -u alice:wrong

wait_http http://127.0.0.1:15080/fetchallkeys
expect "source of truth: all rules" 200 '"uri":"test.xml"' http://127.0.0.1:15080/fetchallkeys
expect "source of truth: one rule" 200 '"uri":"auto.json"' http://127.0.0.1:15080/fetchkey/auto.json

wait_http http://127.0.0.1:15800 -X POST -d x
expect "echo server" 200 '{"parm":123}' http://127.0.0.1:15800 -X POST -d '{"parm":123}'
