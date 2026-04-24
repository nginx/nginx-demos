# Test services

Simple test configuration demonstrating per-server includes (`svcDiscovery-collect.conf`) and services (`HTTP` and `HTTPS`) that route to a dummy backend.

## What these examples contain
- sample NGINX server blocks for:
  - `app1.example.com` (HTTP)
  - `app2.example.com` (HTTP)
  - `secure1.example.com` (HTTPS)
  - `secure2.example.com` (HTTPS)
- a dummy backend listening on port 12345 that returns the client IP address and User Agent

## Quick test instructions

1. Copy the provided config into NGINX conf.d:
   ```bash
   sudo cp test-server.conf /etc/nginx/conf.d/test-server.conf
   ```

2. Reload NGINX:
   ```bash
   sudo nginx -s reload
   ```

3. Verify the four services using curl:

   - HTTP app1
   ```bash
   curl 127.0.0.1 -H "Host: app1.example.com"
   ```
   - HTTP app2
   ```bash
   curl 127.0.0.1 -H "Host: app2.example.com"
   ```
   - HTTPS secure1 (connects to local 127.0.0.1; ignore certs)
   ```bash
   curl -k --connect-to secure1.example.com:443:127.0.0.1 https://secure1.example.com
   ```
   - HTTPS secure2 (connects to local 127.0.0.1; ignore certs)
   ```bash
   curl -k --connect-to secure2.example.com:443:127.0.0.1 https://secure2.example.com
   ```

## Expected output
Each curl should return a short response containing the client IP address and User Agent (e.g., `Client IP: 192.168.1.100 User-Agent: curl/7.81.0`), confirming the request reached the dummy backend and the correct server block was selected.

## Notes
- The HTTPS examples use `-k` because the sample cert paths are expected to be self-signed or not trusted by your client.
- Ensure NGINX has read access to any referenced SSL certificate/key files used by secure1/secure2 server blocks.
- The configuration relies on `include svcDiscovery/svcDiscovery-collect.conf` inside each server block; ensure that file exists and is valid in your environment before reloading NGINX.

## Troubleshooting
- If NGINX fails to reload, check syntax:
  ```bash
  sudo nginx -t
  ```
- Check NGINX error logs (`/var/log/nginx/error.log` by default) for details.

After reloading the configuration you can use the curl commands above to send traffic to each test service. Once services receive traffic, they are discoverable through the service discovery endpoints.
