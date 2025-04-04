## Sample webserver

This is a simple Python webserver to test the NGINX advanced healthcheck lab.

To run it:

```
$ pip install -r requirements.txt
[...]
$ python webserver.py 
 * Serving Flask app 'webserver'
 * Debug mode: off
WARNING: This is a development server. Do not use it in a production deployment. Use a production WSGI server instead.
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:8080
 * Running on http://192.168.1.19:8080
Press CTRL+C to quit
```

It responds to HTTP `GET /` requests on port 8080:

```
$ curl -i http://127.0.0.1:8080
HTTP/1.1 200 OK
Server: Werkzeug/2.2.2 Python/3.10.9
Date: Mon, 19 Dec 2022 16:29:48 GMT
Content-Type: text/html; charset=utf-8
Content-Length: 42
Connection: close

This is the webserver running on fflaptop
```
