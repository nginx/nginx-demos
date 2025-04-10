#!/usr/bin/python3

from flask import Flask, request, jsonify
from datetime import datetime
import socket

app = Flask(__name__)

# curl -ks -X GET https://127.0.0.1:5000/echo_data | jq
@app.route("/get_data", methods=["GET"])
def get_data():
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    hostname = socket.gethostname()

    data = {
        "timestamp": timestamp,
        "hostname": hostname
    }

    return jsonify(data)

# curl -ks -X POST https://127.0.0.1:5000/echo_data -d '{"var":123}' -H "Content-Type: application/json"
@app.route("/echo_data", methods=["POST"])
def echo_data():
    payload = request.get_json() if request.get_json() != None else ''
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    hostname = socket.gethostname()

    return jsonify({"payload": payload, "hostname": hostname, "timestamp": timestamp})

if __name__ == "__main__":
    app.run(ssl_context="adhoc",host="0.0.0.0", port=5000)
