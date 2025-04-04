export default {
    dbQuery
};

function dbQuery(r) {
    r.warn('--- CLIENT REQUEST ---------------------------');
    r.warn('Client[' + r.remoteAddress + '] Method[' + r.method + '] Host[' + r.headersIn['host'] + '] URI [' + r.uri + '] Body[' + r.requestText + ']');

    // Queries the backend db
    r.warn('Subrequest [/dbQuery/backend/fetchkey' + r.uri + ']');
    r.subrequest('/dbQuery/backend/fetchkey' + r.uri, '', subReqCallback);

    function subReqCallback(reply) {
        if (reply.status != 200) {
            // Rule not found

            r.warn('Rule not found - returning 404');
            r.return(404);
        } else {
            r.warn('Rule found: URI[' + reply.uri + '] status[' + reply.status.toString() + '] body[' + reply.responseText + ']');

            var body = JSON.parse(reply.responseText);

            if (body.rule.enabled == 'false') {
                // Rule is disabled

                r.warn('Rule is disabled - returning 404');
                r.return(404);
            } else {
                r.warn('Rewriting request [' + r.headersIn['host'] + r.uri + '] -> [' + body.rule.operation.url + ']');

                // Authorization checks
                r.warn('--- Checking authorization');

		r.warn('- HTTP method received [' + r.method + '] -> needed [' + body.rule.matchRules.method + ']');
                r.warn('- JWT roles received [' + r.variables.jwt_claim_roles + '] -> needed [' + body.rule.matchRules.roles + ']');

                if (r.method == body.rule.matchRules.method && body.rule.matchRules.roles.indexOf(r.variables.jwt_claim_roles) >= 0) {
                  r.warn('--- Authorization successful');
                  var requestOk = true;

                  if (r.requestText) {
                    // Request JSON payload update
                    var requestBody=JSON.parse(r.requestText);

                    // JSON payload validation against template
                    if ('template' in body.rule) {
                      r.warn('+-- JSON template validation [' + JSON.stringify(body.rule.template) + ']');

                      if (checkJSON(r,requestBody,body.rule.template)) {
                        r.warn('+-- JSON template validation successful');
                      } else {
                        r.warn('+-- JSON template validation failed');
                        requestOk = false;
                        r.return(422);
                      }
                    }

                    if (requestOk == true) {
                      if ('json' in body.rule && 'to_server' in body.rule.json) {
                        r.warn('--- JSON payload client -> server : being updated')
                        requestBody = JSON.stringify( applyJSONChanges(r, requestBody, body.rule.json.to_server) );
                      } else {
                        r.warn('--- JSON payload client -> server : no changes')
                        requestBody = r.requestText;
                      }
                    }
                  }
                } else {
                  r.warn('--- Authorization failed');
                  requestOk = false;
                  r.return(403);
                }

                if (requestOk == true) {
                  r.warn('--- Proxying request to upstream');
                  r.subrequest('/steeringMode/' + body.rule.operation.url, {
                      method: r.method, body: requestBody
                  }, steeringModeSubReqCallback);
                }
            }
        }

        function steeringModeSubReqCallback(steeringReply) {
            // Steering mode - returns the steered API response back to the client

            r.warn('--- Upstream returned HTTP [' + steeringReply.status + '] payload [' + steeringReply.responseText + ']');

            var responseBody='';

            if (steeringReply.responseText) {
              // Response JSON payload update

              if ('json' in body.rule && 'to_client' in body.rule.json) {
                r.warn('--- JSON payload server -> client : being updated')
                responseBody = JSON.stringify( applyJSONChanges(r, responseBody=JSON.parse(steeringReply.responseText), body.rule.json.to_client) );
              } else {
                r.warn('--- JSON payload server -> client : no changes')
                responseBody = steeringReply.responseText;
              }
            }

            r.status = steeringReply.status;

            for (var header in steeringReply.headersOut) {
              if (header.toLowerCase() != "content-length") {
                r.headersOut[header] = steeringReply.headersOut[header];
              }
            }

            r.sendHeader();
            r.send(responseBody);
            r.finish();
        }
    }
}

function manipulateJSON(jsonObject, key, value) {
  // Check if the key already exists in the JSON object
  if (jsonObject.hasOwnProperty(key)) {
    // If the value is provided, update the existing key value
    if (value !== undefined) {
      jsonObject[key] = value;
    }
    // If the value is not provided, remove the existing key
    else {
      delete jsonObject[key];
    }
  }
  // If the key doesn't exist and a value is provided, add a new key value pair
  else if (value !== undefined) {
    jsonObject[key] = value;
  }

  // Return the updated JSON object as a string
  return jsonObject;
}

// Applies JSON payload transformations based on the given template
//    "set": [
//      {
//        "<key>": "<value>"
//      }
//    ],
//    "del": [
//      "<key>"
//    ]
// payload and template are JSON objects
function applyJSONChanges(r, payload, jsonTemplate) {
  r.warn('Updating JSON payload [' + JSON.stringify(payload) + '] with template [' + JSON.stringify(jsonTemplate) + ']');

  if ('set' in jsonTemplate) {
    for (var i = 0; i < jsonTemplate.set.length; i++) {
      var keyVal = jsonTemplate.set[i];

      Object.keys(keyVal).forEach(function(key) {
        var value = keyVal[key];

        r.warn('- Updating [' + key + ' = ' + value + ']');
        payload = manipulateJSON(payload, key, value);
      });
    }
  }
  if ('del' in jsonTemplate) {
    for (var i = 0; i < jsonTemplate.del.length; i++) {
      var key = jsonTemplate.del[i];

      r.warn('- Deleting [' + key + ']');
      payload = manipulateJSON(payload, key);
    }
  }

  r.warn('Done updating JSON payload [' + JSON.stringify(payload) + ']');

  return payload;
}

// Check JSON payload conformity to the template
// JSON keys and key types are verified
function checkJSON(r, payload, template) {
  const keys = Object.keys(template);

  r.warn('|-- Checking JSON payload [' + payload + ']');

  for (let i = 0; i < keys.length; i++) {
    // Property check

    if (!payload.hasOwnProperty(keys[i])) {
      // JSON key missing in payload
      r.warn('|---- Property [' + keys[i] + '] missing');
      return false;
    }

    // Property type check
    if (typeof payload[keys[i]] !== typeof template[keys[i]]) {
      // JSON key with wrong type in payload
      r.warn('|---- Property [' + keys[i] + '] type wrong');
      return false;
    }

    // Nested properties
    if (typeof template[keys[i]] === 'object') {
      if(!checkJSON(r, payload[keys[i]], template[keys[i]])) {
        return false;
      }
    }

    r.warn('|---- Property [' + keys[i] + '] ok');
  }

  return true;
}
