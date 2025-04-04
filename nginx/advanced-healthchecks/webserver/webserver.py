#
# Minimal webserver
#
# To test:
# curl -s http://127.0.0.1:8080/
#

import socket
from flask import Flask

app = Flask(__name__)

@app.route('/')
def index():
    return 'This is the webserver running on ' + socket.gethostname() + '\n'

if __name__ == '__main__':
    app.run(debug=False, port=8080, host='0.0.0.0')
