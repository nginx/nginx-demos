#!flask/bin/python
from flask import Flask, jsonify, abort, make_response, request

app = Flask(__name__)

rules = [
    {
        'fqdn': u'api.ff.lan',
        'uri': u'/getmyip',
        'title': u'Gets my IP in plaintext',
        'rewrite': u'http://api.ipify.org',
        'enabled': u'true'
    },
    {
        'fqdn': u'api.ff.lan',
        'uri': u'/getmyip/json',
        'title': u'Gets my IP in json',
        'rewrite': u'http://api.ipify.org?format=json',
        'enabled': u'true'
    },
    {
        'fqdn': u'api.ff.lan',
        'uri': u'/getmyip/json/callback',
        'title': u'Gets my IP in json with callback',
        'rewrite': u'http://api.ipify.org?format=jsonp',
        'enabled': u'true'
    }
]

@app.route('/mcgw', methods=['GET'])
def get_key_query_string():
    fqdn = request.args.get('fqdn')
    uri = request.args.get('uri')
    rule = [rule for rule in rules if rule['fqdn'] == fqdn and rule['uri'] == uri]
    if len(rule) == 0:
        abort(404)
    return jsonify([rule[0]])

@app.route('/mcgw/keys/<string:fqdn>/<path:uri>', methods=['GET'])
def get_key(fqdn,uri):
    rule = [rule for rule in rules if rule['fqdn'] == fqdn and rule['uri'] == uri]
    if len(rule) == 0:
        abort(404)
    return jsonify({'rule': rule[0]})

@app.route('/mcgw/keys', methods=['GET'])
def get_all_keys():
    return jsonify({'rules': rules})

@app.errorhandler(404)
def not_found(error):
    return make_response(jsonify({'error': 'Not found'}), 404)

if __name__ == '__main__':
    app.run(host='0.0.0.0')
