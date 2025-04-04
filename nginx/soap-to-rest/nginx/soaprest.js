export default {requestHandler,headerFilter};

const xml = require("xml");
const querystring = require("querystring");
const fs = require("fs");

function requestHandler(r) {
  r.warn('Request: Client['+r.remoteAddress+'] Scheme['+r.variables.scheme+'] Method['+r.method+'] Host['+r.headersIn['host']+'] URI ['+r.uri+'] Body['+r.requestText+']');

  // Queries the source of truth
  r.warn('Subrequest [/sourceOfTruth/fetchkey'+r.uri+']');
  r.subrequest('/sourceOfTruth/fetchkey'+r.uri,'',sourceOfTruthCallback);

  function sourceOfTruthCallback(reply) {
  if(reply.status!=200) {
    // Rule not found
    r.warn('Rule not found - returning 404');
    r.return(404);
  } else {
    r.warn('subReqCallback got 200');

    var body = JSON.parse(reply.responseText);

    if (body.rule.enabled=='false') {
      // Rule is disabled
      r.warn('Rule is disabled - returning 404');
      r.return(404);
    } else {
      // Rule is enabled
      r.warn('Rewrite rule ['+r.headersIn['host']+r.uri+'] -> upstream content ['+body.rule.upstream_content+']');

     // Request body translation
     let requestBody = '';
     let convertedRequestBody = '';

     if(r.requestText != null) {
       convertedRequestBody = r.requestText;
     }

     //let requestContentType = String(r.headersIn['Content-Type']);

     // Service definition JSON "upstream_content" field describes the content format expected by the upstream. It can be set to either "json" or "xml" and it enables request body translation when needed
     if(r.requestText != null) {
       if (body.rule.upstream_content == 'json') {
         r.warn('Upstream requires JSON payload');

         if(isXml(r.requestText)) {
           requestBody = xml.parse(r.requestText);

           if ('request_translation' in body.rule) {
             r.warn('Request payload translation XML -> JSON - template-based');
             convertedRequestBody = templateSoapToRest(r,requestBody,body.rule.request_translation.to_json)
           } else {
             r.warn('Request payload translation XML -> JSON - automatic mode');
             convertedRequestBody = soapToRest(r,requestBody);
           }
         }
       } else if (body.rule.upstream_content == 'xml') {
         r.warn('Upstream requires XML payload');

         if(isJson(r.requestText)) {
           requestBody = JSON.parse(r.requestText);

           if ('request_translation' in body.rule) {
             r.warn('Request payload translation JSON -> XML - template-based');
             convertedRequestBody = templateRestToSoap(r,requestBody,body.rule.request_translation.to_xml)
           } else {
             r.warn('Request payload translation JSON -> XML - automatic mode');
             convertedRequestBody = restToSoap(r,requestBody);
           }
         }
       } else {
         convertedRequestBody = r.requestText;
       }
     }

     r.warn('Request body sent to upstream: [' + convertedRequestBody + ']');

     // Proxy the request to upstream
     r.warn('Proxying to [http://'+body.rule.upstream+']');
     r.subrequest('/proxyToUpstream/'+body.rule.upstream,{method: r.method, body: convertedRequestBody},proxyCallback);

     function proxyCallback(upstreamReply) {
       // Collect upstream reply
       //r.warn('Upstream reply status ['+upstreamReply.status+'] body ['+upstreamReply.responseText+']');
       r.status=upstreamReply.status;

       let responseBody = '';
       let convertedBody = '';

       let replyContentType = upstreamReply.headersOut['Content-Type'];

       // XML to JSON and JSON to XML response payload translation
       // X-Wanted-Content HTTP request header can be set to either "json" or "xml" to enable response body translation
       if (r.headersIn['X-Wanted-Content'] == 'json' && replyContentType.includes('application/xml')) {
         responseBody = xml.parse(upstreamReply.responseText);
         convertedBody = soapToRest(r,responseBody);

         delete r.headersOut["Content-Length"];
         delete r.headersOut["Content-Type"];
         r.headersOut['Content-Type'] = 'application/json; charset=utf-8';
       } else if (r.headersIn['X-Wanted-Content'] == 'xml' && replyContentType.includes('application/json')) {
         responseBody = JSON.parse(upstreamReply.responseText);
         convertedBody = restToSoap(r,responseBody);

         delete r.headersOut["Content-Length"];
         delete r.headersOut["Content-Type"];
         r.headersOut['Content-Type'] = 'application/soap+xml; charset=utf-8';
       } else {
         convertedBody = upstreamReply.responseText;
       }

       //r.warn('Reply type: ' + upstreamReply.headersOut['Content-Type']);
       //r.warn('Converted body: ' + convertedBody);

       // Returns upstream reply headers to client
       for (var header in upstreamReply.headersOut) {
         switch (header) {
           case 'Content-Type':
           case 'Content-Length':
             break;
           default:
             r.headersOut[header] = upstreamReply.headersOut[header];
         }
       }

       r.sendHeader();
       r.send(convertedBody);
       r.finish();
      }
    }
  }
}}

function headerFilter(r) {
    // Sample response headers removal
    //delete r.headersOut["Content-Length"];
    //delete r.headersOut["Content-Type"];

    // Sample response headers injection
    //r.headersOut["Content-Type"] = "application/json";
    //r.headersOut["Content-Type"] = "application/soap+xml; charset=utf-8";
    //r.headersOut["X-Custom-Header"] = "testing123";
}

// REST to SOAP payload translation
function restToSoap(r,obj) {
    let soap = '<?xml version="1.0"?>'+
	'<soapenv:Envelope xmlns:soapenv="http://schemas.xmlsoap.org/soap/envelope/">' +
	'<soapenv:Header></soapenv:Header>' +
	'<soapenv:Body>' +
	jsonToXml(r,obj) +
	'</soapenv:Body>' +
	'<soapenv:Fault></soapenv:Fault>' +
	'</soapenv:Envelope>'

    return soap;
}

function jsonToXml(r,json) {
    let xml = '';

    Object.keys(json).forEach(k => {
        if (typeof json[k] == 'object') {
            //r.warn('==> JSON object ['+k+']');
            xml += '<'+k+'>'+jsonToXml(r,json[k])+'</'+k+'>';
        } else {
            //r.warn('JSON parameter ['+k+'] => ['+json[k]+']');
            xml += '<'+k+'>'+json[k]+'</'+k+'>';
        }
    })

    return xml;
}

// template-based JSON to XML payload translation
function templateRestToSoap(r,jsonRequestBody,translationTemplate) {
    r.warn('===> Template-based JSON to XML translation');
    r.warn('Request body: [' + jsonRequestBody + ']');
    r.warn('Template    : [' + translationTemplate + ']');

    let outputXml = '';
    let tokenFound = false;
    let tokenName = '';

    Object.keys(translationTemplate).forEach(c => {
      let char = translationTemplate[c];

      if (char != '$') {
        if (tokenFound == false) {
          outputXml += char;
        } else {
          tokenName += char;
        }
      } else {
        // JSON translation '$' token found
        if (tokenFound == true) {
          let jsonField = tokenName.substring(5);
          r.warn('=> JSON -> XML Translation token ['+tokenName+']');

          // Removes 'JSON.' at the start of the token name
          if (tokenName.substring(5) in jsonRequestBody) {
            r.warn('   '+jsonField+' found in JSON payload');
            outputXml += jsonRequestBody[tokenName.substring(5)];
          } else {
            r.warn('   '+jsonField+' missing in JSON payload');
          }
          tokenName = '';
          tokenFound = false;
        } else {
          tokenFound = true;
        }
      }
    });

    return outputXml;
}

// SOAP to REST payload translation
function soapToRest(r,obj) {
    //let json = { test: 123, code: 456 };

    let json = {};
    json = xmlToJson(r,obj);

    return JSON.stringify(json);
}

function xmlToJson(r,xml) {
    let json = {};

    Object.keys(xml).forEach(k => {
        if (typeof xml[k][Symbol.toStringTag] == 'XMLNode') {
            //r.warn('==> XML node ['+k+'] => ['+xml[k]+']');
            json[k] = xmlToJson(r,xml[k]);
        } else {
            //r.warn('XML parameter ['+k+'] => ['+xml[k]+']');
            json[k] = xml[k];
        }
    })

    return json;
}

// JSON format check
function isJson(str) {
    try {
        JSON.parse(str);
    } catch (e) {
        return false;
    }
    return true;
}

// XML format check
function isXml(str) {
    try {
        xml.parse(str);
    } catch (e) {
        return false;
    }
    return true;
}
