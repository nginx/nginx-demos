[![License](https://img.shields.io/badge/license-apache2-green.svg)](LICENSE.md)
[![njs](https://img.shields.io/badge/njs-%3E%3D0.8.1-orange.svg)](https://nginx.org/en/docs/njs/)
[![nginx](https://img.shields.io/badge/NGINX-brightgreen.svg)](https://nginx.org/)
[![nginx](https://img.shields.io/badge/NGINX-plus-brightgreen.svg)](https://docs.nginx.com/nginx/)

# 🚀 NGINX Service Discovery

Consul-compatible service discovery implemented with NGINX + njs. Servers mirror requests to a local collector which stores one JSON file per service; a small HTTP server exposes a subset of the Consul HTTP API:

- `GET /v1/catalog/services`
- `GET /v1/health/service/:name`
- `GET /v1/health/services` (non-standard convenience endpoint)

Works with both [NGINX Open Source](https://nginx.org/) and [NGINX Plus](https://docs.nginx.com/nginx/) (requires [njs](https://nginx.org/en/docs/njs/) ≥ 0.8.1).

---

## 📁 Files

- `svcDiscovery-modules.conf` — load njs modules  
- `svcDiscovery.conf` — upstreams, maps, collector and API server blocks  
- `svcDiscovery-collect.conf` — mirror config to send subrequests to collector  
- `svcDiscovery.js` — njs handlers: `collect`, `cleanup`, `catalogServices`, `healthService`, `allServices`

---

## ⚙️ Quick install

1. Copy files into a directory on the NGINX host, e.g. `/etc/nginx/svcDiscovery`.
2. Ensure njs is installed (njs ≥ 0.8.1) and njs modules are available.
3. Create the registry directory and set ownership/permissions:
```bash
sudo mkdir -p /var/lib/nginx/svcDiscovery
sudo chown nginx:nginx /var/lib/nginx/svcDiscovery
sudo chmod 750 /var/lib/nginx/svcDiscovery
```
Adjust `nginx:nginx` to match your NGINX worker user/group.

4. Include the configs:

- In the main nginx.conf context:
```nginx
include svcDiscovery/svcDiscovery-modules.conf;
```

- In the `http` context:
```nginx
include svcDiscovery/svcDiscovery.conf;
```

- In each `server` block you want discoverable:
```nginx
include svcDiscovery/svcDiscovery-collect.conf;
```

5. Test and reload NGINX:
```bash
sudo nginx -t && sudo nginx -s reload
```

---

## 🔍 How it works (summary)

- Each discoverable server block includes `svcDiscovery-collect.conf`, which defines a `mirror` to `/svcDiscovery-mirror` (internal).  
- Mirrored subrequests go to a local upstream `svcDiscovery_collector` (127.0.0.1:10080).  
- The collector runs an njs handler (`collect`) that writes one JSON file per unique (proto, name, addr, port) to `/var/lib/nginx/svcDiscovery`.  
- A small HTTP server listens on port `8500` and exposes Consul-compatible endpoints that read those JSON files and return catalog / health responses.  
- A `js_periodic` `cleanup()` removes stale JSON files older than TTL (default 5 minutes).

---

## 🧩 Configuration snippets

svcDiscovery-collect.conf:
```nginx
mirror              /svcDiscovery-mirror;
mirror_request_body off;

location = /svcDiscovery-mirror {
    internal;
    proxy_pass          http://svcDiscovery_collector;
    proxy_http_version  1.1;
    proxy_set_header    Connection              "";
    proxy_set_header    X-Consul-Server-Name    $consul_server_name;
    proxy_set_header    X-Consul-Server-Addr    $server_addr;
    proxy_set_header    X-Consul-Server-Port    $server_port;
    proxy_set_header    X-Consul-Server-Proto   $consul_proto;
}
```

svcDiscovery-modules.conf:
```nginx
load_module modules/ngx_http_js_module.so;
load_module modules/ngx_stream_js_module.so;
```

svcDiscovery.conf (high level):
- `js_import svcDiscovery from svcDiscovery/svcDiscovery.js`  
- Upstream `svcDiscovery_collector` → `127.0.0.1:10080`  
- API server listens on port `8500` and defines:
  - `GET /v1/catalog/services` → `svcDiscovery.catalogServices`
  - `GET /v1/health/service/:name` → `svcDiscovery.healthService`
  - `GET /v1/health/services` → `svcDiscovery.allServices`
- `js_periodic svcDiscovery.cleanup interval=60s jitter=5s` (match the documented interval in `svcDiscovery.js`)

---

## ⚙️ njs tunables (in svcDiscovery.js)

- `TTL_MS` — how long a service file is kept without refresh (default: 5 minutes)  
- `CLEANUP_INTERVAL_S` — documented as 60s; `js_periodic` interval should match

Edit `TTL_MS` in `svcDiscovery.js` and reload NGINX to change TTL.

---

## 📡 Examples

Set up the [sample services](examples) and query the service discovery endpoints:

List services:
```bash
curl http://127.0.0.1:8500/v1/catalog/services
```

Sample output:
```json
{
  "app1.example.com": [
    "http"
  ],
  "secure1.example.com": [
    "https"
  ]
}
```

Get health for `app1.example.com`:
```bash
curl http://127.0.0.1:8500/v1/health/service/app1.example.com
```

Sample output:
```json
[
  {
    "Node": {
      "Node": "nginx",
      "Address": "127.0.0.1"
    },
    "Service": {
      "ID": "http-app1.example.com-127.0.0.1-80",
      "Service": "app1.example.com",
      "Tags": [
        "http"
      ],
      "Address": "127.0.0.1",
      "Port": 80
    },
    "Checks": [
      {
        "Node": "nginx",
        "CheckID": "service:http-app1.example.com-127.0.0.1-80",
        "Name": "Service 'app1.example.com' check",
        "Status": "passing",
        "Notes": "Auto-registered via NGINX mirror directive",
        "ServiceID": "http-app1.example.com-127.0.0.1-80",
        "ServiceName": "app1.example.com"
      }
    ]
  }
]
```

Get all services in health format:
```bash
curl http://127.0.0.1:8500/v1/health/services
```

Sample output:
```json
[
  {
    "Node": {
      "Node": "nginx",
      "Address": "127.0.0.1"
    },
    "Service": {
      "ID": "http-app1.example.com-127.0.0.1-80",
      "Service": "app1.example.com",
      "Tags": [
        "http"
      ],
      "Address": "127.0.0.1",
      "Port": 80
    },
    "Checks": [
      {
        "Node": "nginx",
        "CheckID": "service:http-app1.example.com-127.0.0.1-80",
        "Name": "Service 'app1.example.com' check",
        "Status": "passing",
        "Notes": "Auto-registered via NGINX mirror directive",
        "ServiceID": "http-app1.example.com-127.0.0.1-80",
        "ServiceName": "app1.example.com"
      }
    ]
  },
  {
    "Node": {
      "Node": "nginx",
      "Address": "127.0.0.1"
    },
    "Service": {
      "ID": "https-secure1.example.com-127.0.0.1-443",
      "Service": "secure1.example.com",
      "Tags": [
        "https"
      ],
      "Address": "127.0.0.1",
      "Port": 443
    },
    "Checks": [
      {
        "Node": "nginx",
        "CheckID": "service:https-secure1.example.com-127.0.0.1-443",
        "Name": "Service 'secure1.example.com' check",
        "Status": "passing",
        "Notes": "Auto-registered via NGINX mirror directive",
        "ServiceID": "https-secure1.example.com-127.0.0.1-443",
        "ServiceName": "secure1.example.com"
      }
    ]
  }
]
```

Responses follow Consul-compatible JSON structures.

---

## 🔐 Permissions & security

- The collector listens only on `127.0.0.1` and the mirror subrequest is internal, so the registry and collector are not exposed externally.  
- Ensure `/var/lib/nginx/svcDiscovery` is writable by the NGINX worker user and not world-writable.

---

## 🛠️ Troubleshooting

- If no services appear, verify:
  - discoverable server blocks include `svcDiscovery-collect.conf`
  - NGINX can write to `/var/lib/nginx/svcDiscovery`
  - njs modules are loaded and njs version ≥ 0.8.1
  - Check logs: `/var/log/nginx/svcDiscovery.log`, `/var/log/nginx/svcDiscovery-collector.log`, and the NGINX error log for njs messages

---

## 🧭 Design notes

- `collect()` writes one JSON file per tuple `(proto, name, addr, port)`; `Updated` is refreshed on each mirrored request.  
- `cleanup()` removes files older than `TTL_MS`; `js_periodic` ensures a single worker runs cleanup to avoid races.  
- Quiet services (no traffic for a time greater than `TTL_MS`) are evicted; use an external probe if persistent registration is required.

---

## 📜 License

See [LICENSE.md](LICENSE.md)
