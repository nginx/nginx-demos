#!/usr/bin/env bash

#
# JWT Encoder Bash Script
#

secret='fantasticjwt'

# Static header fields.
header='{
    "typ": "JWT",
    "alg": "HS256",
    "kid": "0001",
    "iss": "Bash JWT Generator"
}'

# Use jq to set the dynamic `iat` and `exp`
# fields on the header using the current time.
# `iat` is set to now, and `exp` is now + 1 second.
header=$(
    echo "${header}" | jq --arg time_str "$(date +%s)" \
    '
    ($time_str | tonumber) as $time_num
    | .iat=$time_num
    | .exp=($time_num + 1)
    '
)
payload_guest='{
    "name": "Alice Guest",
    "sub": "JWT sub claim",
    "iss": "JWT iss claim",
    "roles": [
        "guest"
    ]
}'
payload_devops='{
    "name": "Bob DevOps",
    "sub": "JWT sub claim",
    "iss": "JWT iss claim",
    "roles": [
        "devops"
    ]
}'

base64_encode()
{
    declare input=${1:-$(</dev/stdin)}
    # Use `tr` to URL encode the output from base64.
    printf '%s' "${input}" | base64 | tr -d '=' | tr '/+' '_-' | tr -d '\n'
}

json() {
    declare input=${1:-$(</dev/stdin)}
    printf '%s' "${input}" | jq -c .
}

hmacsha256_sign()
{
    declare input=${1:-$(</dev/stdin)}
    printf '%s' "${input}" | openssl dgst -binary -sha256 -hmac "${secret}"
}

header_base64=$(echo "${header}" | json | base64_encode)

payload_guest_base64=$(echo "${payload_guest}" | json | base64_encode)
header_payload_guest=$(echo "${header_base64}.${payload_guest_base64}")
signature_guest=$(echo "${header_payload_guest}" | hmacsha256_sign | base64_encode)

payload_devops_base64=$(echo "${payload_devops}" | json | base64_encode)
header_payload_devops=$(echo "${header_base64}.${payload_devops_base64}")
signature_devops=$(echo "${header_payload_devops}" | hmacsha256_sign | base64_encode)

echo "${header_payload_guest}.${signature_guest}" > jwt.guest
echo "${header_payload_devops}.${signature_devops}" > jwt.devops
