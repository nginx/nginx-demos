#!/usr/bin/python3

import http.server
import socketserver

class EchoRequestHandler(http.server.BaseHTTPRequestHandler):
    def _set_response(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()

    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length).decode('utf-8')
        
        self._set_response()
        self.wfile.write(post_data.encode('utf-8'))

def run(server_class=http.server.HTTPServer, handler_class=EchoRequestHandler, port=8000):
    server_address = ('', port)
    httpd = server_class(server_address, handler_class)
    print(f"Starting httpd on {server_address[0]}:{server_address[1]}...")
    httpd.serve_forever()

if __name__ == '__main__':
    run()
