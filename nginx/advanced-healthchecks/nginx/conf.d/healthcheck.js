//
// NGINX Smart Healtchecks
// This configuration requires NGINX Plus
//

export default {check};

function check(r) {
  var dest_url = r.variables.healthcheck_endpoint;
  var rUri=r.uri;

  // Debug logging
  r.warn('--> Running healthcheck');
  r.warn('Client['+r.remoteAddress+'] Method['+r.method+'] URI['+rUri+'] QueryString['+r.variables.query_string+'] Checking ['+encodeURI(dest_url)+']');

  // Call the internal location to query the external healthcheck REST API
  var fullURL = '/healthCheck/' + encodeURI(dest_url);
  r.subrequest(fullURL,'',subReqCallback);

  // Callback function to evaluate external healtcheck REST API response
  function subReqCallback(reply) {
    // Gets the backend upstream server's position as mapped in the backend_server_entry map
    var backend_server_entry = r.variables.backend_server_entry;

    // Debug logging
    r.warn('JSON reply from: URI['+reply.uri+'] status['+reply.status.toString()+'] body['+reply.responseText+'] for server ['+backend_server_entry+']');

    // Checks the external healthcheck REST API response code
    if(reply.status!=200) {
      // Return code != HTTP/200, healthcheck endpoint unreachable
      r.warn('Healthcheck: server #[' + backend_server_entry + '] is down');

      // HTTP response code is not 200/OK, setting the upstream server as not available
      r.subrequest("/upstream/down/" + backend_server_entry,postUpstreamUpdate);
    } else {
      // HTTP response code is 200/OK, parse the JSON response
      var jsonReply = JSON.parse(reply.responseText);

      // Example check on 1-minute CPU load
      r.warn('Got 1-minute CPU value: '+jsonReply.cpu.load.1minute);

      // Runs the healthcheck logic
      var setDownStateTo = "false"

      // Evaluation logic goes here, this example checks the 1 minute CPU load
      if(jsonReply.cpu.load.1minute < 5) {
        // Set the upstream server up
        r.warn('Healthcheck: server #[' + backend_server_entry + '] is up');
	r.subrequest("/upstream/up/" + backend_server_entry,postUpstreamUpdate);
      } else {
        // Set the upstream server down
        r.warn('Healthcheck: server #[' + backend_server_entry + '] is down');
	r.subrequest("/upstream/down/" + backend_server_entry,postUpstreamUpdate);
      }

      return;
    }

    // Example callback function to display/handle healthcheck REST API response
    function postUpstreamUpdate(upstreamUpdateRequest) {
      r.warn('NGINX API Response: '+upstreamUpdateRequest.responseText);

      for (var header in reply.headersOut) {
        r.warn('header ['+header+'] = ['+reply.headersOut[header] + ']');
        r.headersOut[header] = reply.headersOut[header];
      }

      r.status=200;
      r.sendHeader();
      if (reply.responseText)
        r.send(reply.responseText);
      r.finish();
    }
  }
}
