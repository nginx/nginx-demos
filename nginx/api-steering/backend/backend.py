#!/usr/bin/python3

import json
from flask import Flask, jsonify, abort, make_response, request

app = Flask(__name__)

with open('db.json') as db:
  rules = json.load(db)

@app.route('/backend/fetchkey/<path:uri>', methods=['GET'])
def get_key(uri):
    rule = [rule for rule in rules if rule['uri'] == uri]
    if len(rule) == 0:
        abort(404)
    return jsonify({'rule': rule[0]})

@app.route('/backend/fetchallkeys', methods=['GET'])
def get_all_keys():
    return jsonify({'rules': rules})

@app.route('/jwks.json', methods=['GET'])
def get_jwks():
    return jsonify({"keys": [{ "k":"ZmFudGFzdGljand0", "kty":"oct", "kid":"0001" }]})

@app.errorhandler(404)
def not_found(error):
    return make_response(jsonify({'error': 'Not found'}), 404)

if __name__ == '__main__':
    app.run(host='0.0.0.0')
