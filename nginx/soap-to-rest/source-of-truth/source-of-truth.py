#!/usr/bin/python3
from flask import Flask, jsonify, abort, make_response, request

app = Flask(__name__)

rules = [
    {
      'ruleid': 1,
      'enabled': u'true',
      'uri': u'test.xml',
      'upstream_content': u'xml',
      'upstream': u'10.5.0.13:8000',
      'request_translation': {
	'to_xml': u'<soapenv:Envelope xmlns:soapenv="http://schemas.xmlsoap.org/soap/envelope/" xmlns:msg="http://test/sampleXMLRequest"><soapenv:Header/><soapenv:Body><msg:wsServiceRequest><msg:username>$JSON.username$</msg:username><msg:email>$JSON.email$</msg:email><msg:userdata><msg:id>$JSON.userid$</msg:id><msg:phone>$JSON.phone$</msg:phone></msg:userdata></msg:wsServiceRequest></soapenv:Body></soapenv:Envelope>'
      }
    },
    {
      'ruleid': 2,
      'enabled': u'true',
      'uri': u'auto.xml',
      'upstream_content': u'xml',
      'upstream': u'10.5.0.13:8000'
    },
    {
      'ruleid': 3,
      'enabled': u'true',
      'uri': u'auto.json',
      'upstream_content': u'json',
      'upstream': u'10.5.0.13:8000'
    }
]

@app.route('/fetchkey/<path:uri>', methods=['GET'])
def get_key(uri):
    rule = [rule for rule in rules if rule['uri'] == uri]
    if len(rule) == 0:
        abort(404)
    return jsonify({'rule': rule[0]})

@app.route('/fetchallkeys', methods=['GET'])
def get_all_keys():
    return jsonify({'rules': rules})

@app.errorhandler(404)
def not_found(error):
    return make_response(jsonify({'error': 'Not found'}), 404)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10080)
