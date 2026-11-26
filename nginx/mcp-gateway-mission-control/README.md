# NGINX MCP Gateway Mission Control

A hands-on demonstration of **securing and observing Model Context Protocol (MCP) traffic using NGINX**.

Run everything locally with Docker. No Kubernetes cluster, cloud infrastructure, or special environment required.

This demo uses a real MCP SDK server that simulates Kubernetes operations. NGINX sits in front of the server, inspects JSON-RPC requests, and enforces security and traffic policies based on the MCP tool being called.

A Mission Control dashboard lets you compare direct MCP access with access through NGINX.

## What you'll see

- **MCP tool discovery and execution** using JSON-RPC
- **Native NGINX JSON body inspection** to identify MCP tools
- **Role-based access control (RBAC)** for individual tools
- **Per-tool rate limiting** with HTTP 429 responses
- **MCP-aware observability** using njs, OpenTelemetry, Prometheus, and Grafana
- **Live NGINX configuration editing** with validation and reload

## Architecture

```text
                   MCP Client / curl / Mission Control
                                  |
                    +-------------+-------------+
                    |                           |
                    v                           v
              DIRECT ACCESS               NGINX GATEWAY
              localhost:8001               localhost:8080
                    |                           |
                    |                     Authentication
                    |                     Tool-level RBAC
                    |                     Rate limiting
                    |                     Observability
                    |                           |
                    +-------------+-------------+
                                  |
                                  v
                            MCP SDK Server
                         Kubernetes Simulator
```

**Two ways to access the same MCP server:**

| Endpoint | Path | Gateway policies |
|---|---|---|
| `localhost:8001/mcp` | Direct to MCP server | Bypasses NGINX |
| `localhost:8080/mcp` | Through NGINX | RBAC, rate limits, telemetry |

The direct MCP endpoint is bound to localhost and is intended only for demonstration.

## Quick start

**Requirements:** Docker with Docker Compose v2. Docker Desktop works on Windows and macOS; Docker Engine with Compose works on Linux.

Clone the repository and change to the demo directory:

```bash
git clone https://github.com/nginx/nginx-demos.git
cd nginx-demos/nginx/mcp-gateway-mission-control
```

Create your local environment file:

```bash
cp .env.example .env
```

Edit `.env` and set a unique `NGINX_EDITOR_TOKEN` password of at least five characters. The placeholder value from `.env.example` is rejected and leaves the editor disabled.

Start everything:

```bash
docker compose up -d --build
```

Open the demo:

| Service | URL |
|---|---|
| **Mission Control** | http://localhost:3000 |
| NGINX gateway | http://localhost:8080/mcp |
| Direct MCP server | http://localhost:8001/mcp |
| Grafana | http://localhost:3002 |
| Prometheus | http://localhost:9090 |

All ports are published on `127.0.0.1` only, so the demo is not reachable from other machines on the same network (for example conference Wi-Fi). To expose it deliberately, set `DEMO_BIND=0.0.0.0` in `.env`.

If a port is already in use ("port is already allocated"), override it in `.env` with `GATEWAY_PORT`, `UI_PORT`, `MCP_DIRECT_PORT`, `GRAFANA_PORT` or `PROMETHEUS_PORT`, and use that port instead of the default in the URLs and curl commands below. `scripts/reset.sh` reads these from `.env`; for `scripts/populate.py`, pass `--url` or export `GATEWAY_PORT`.

All commands below are intended to run on the computer hosting Docker. The multiline curl examples use Bash-compatible syntax (macOS Terminal, Linux shell, Git Bash, or WSL).

---

## Try it with curl

The easiest way to understand the demo is to send requests directly to the MCP server, then send the same operations through NGINX.

### 1. Discover MCP tools — bypass NGINX

Connect directly to the MCP server on **port 8001**:

```bash
curl -i http://localhost:8001/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/list",
    "params": {}
  }'
```

The server returns the available tools, including their names, descriptions, and input schemas.

**This request bypasses NGINX completely.** No gateway authentication, RBAC, or rate limiting is applied.

### 2. Call a tool — bypass NGINX

Call the read-only `list_pods` tool directly:

```bash
curl -i http://localhost:8001/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/call",
    "params": {
      "name": "list_pods",
      "arguments": {}
    }
  }'
```

Notice that the URL is still `/mcp`. The actual tool name is specified inside the JSON-RPC body.

### 3. Delete a pod — bypass NGINX

```bash
curl -i http://localhost:8001/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "delete_pod",
      "arguments": {
        "name": "cache-9b1e"
      }
    }
  }'
```

This request goes **directly to the MCP server**, without any authentication or authorization, and the pod is really deleted from the simulated cluster: the response contains `"deleted"`. Run `list_pods` again to confirm it is gone.

The data is simulated and held in memory; `./scripts/reset.sh` restores it.

### 4. Attempt the same delete — through NGINX

Now send the request to **port 8080**, using a Viewer API key:

```bash
curl -i http://localhost:8080/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -H 'X-API-Key: viewer-demo' \
  -d '{
    "jsonrpc": "2.0",
    "id": 4,
    "method": "tools/call",
    "params": {
      "name": "delete_pod",
      "arguments": {
        "name": "demo-nonexistent-pod"
      }
    }
  }'
```

**Expected: HTTP 403 Forbidden.**

NGINX identifies the `delete_pod` tool, checks the Viewer role, and blocks the operation before it reaches the MCP server.

RBAC is an **allowlist**: each role is granted specific tools in `nginx/nginx.conf`, and any `tools/call` not granted there — including tools added to the server later — is denied with HTTP 403. Requests whose JSON-RPC method NGINX cannot read (JSON-RPC batch arrays, invalid JSON, non-JSON content types) are rejected with HTTP 400 instead of being proxied unchecked.

### 5. Call an allowed tool — through NGINX

```bash
curl -i http://localhost:8080/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -H 'X-API-Key: viewer-demo' \
  -d '{
    "jsonrpc": "2.0",
    "id": 5,
    "method": "tools/call",
    "params": {
      "name": "list_pods",
      "arguments": {}
    }
  }'
```

The Viewer is permitted to call `list_pods`, so NGINX forwards the request to the MCP server, subject to the rate limit.

### 6. Compare different roles

The demo includes three fictional API keys:

| API key | Role | Example permissions |
|---|---|---|
| `viewer-demo` | Viewer | Read-only and utility tools |
| `operator-demo` | Operator | Viewer tools plus create/update |
| `admin-demo` | Admin | All demo tools, including delete |

Try changing the `X-API-Key` header in the previous curl commands.

For example, `admin-demo` is allowed to invoke `delete_pod`, while `viewer-demo` receives HTTP 403.

**The MCP server is unchanged. NGINX controls which callers can execute which tools.**

---

## Explore Mission Control

Open **http://localhost:3000**.

### Direct to MCP

Connect directly to the MCP server, bypassing NGINX.

Discover and execute tools without gateway policies.

### Through NGINX

Route requests through NGINX and supply a demo API key.

Experiment with:

- Allowed and denied tool calls
- Different Viewer, Operator, and Admin permissions
- HTTP 400, 401 and 403 responses
- Tool-specific rate limiting

### Rate limiting

Click **Burst: 25 HTTP requests** to send 25 requests to `list_pods`.

The default NGINX limits are:

| Tool | Rate | Burst |
|---|---|---|
| `list_pods` | 10 requests/sec | 5 |
| `create_pod` | 3 requests/sec | 2 |
| `delete_pod` | 1 request/sec | 1 |

Excessive requests are rejected with HTTP **429 Too Many Requests**.

These limits are applied per client IP, with separate rate-limit zones for each configured tool. Other tools are not rate-limited in this demo.

Rejected requests carry the response header `X-MCP-Policy: rate-limited`.

---

## How NGINX handles an MCP request

The NGINX Config tab shows the actual gateway configuration.

For an incoming `tools/call` request, NGINX:

1. **Receives** the HTTP request at `/mcp`.
2. **Reads the JSON body early** using `client_body_early_read`.
3. **Extracts the MCP method and tool name** using native `json_set` functionality.
4. **Identifies the caller** from the demo API key.
5. **Checks RBAC** and rejects unauthenticated (401), unreadable (400) or unauthorized (403) requests.
6. **Applies tool-specific rate limiting**, returning HTTP 429 when necessary.
7. **Proxies allowed requests** to the MCP server.
8. **Observes the response** using njs and OpenTelemetry.

For example:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "delete_pod",
    "arguments": {
      "name": "demo-nonexistent-pod"
    }
  }
}
```

NGINX extracts:

```text
Method: tools/call
Tool:   delete_pod
```

It then combines the tool name with the caller's role to decide whether the request is permitted.

### Edit the configuration live

In the **NGINX Config** tab, change:

```nginx
limit_req zone=pod_read burst=5 nodelay;
```

to:

```nginx
limit_req zone=pod_read burst=10 nodelay;
```

Click **Validate & Apply**.

The editor validates the configuration with `nginx -t` and requests an NGINX reload. Invalid configurations are rolled back.

Edits only change the running container. `nginx/nginx.conf` in the repository is mounted read-only and is never modified; restarting the `nginx` container (or running `./scripts/reset.sh`) discards live edits.

Run the 25-request burst again to see the effect of the new allowance.

**No changes to the MCP server are required.**

---

## Observability

Open **Grafana at http://localhost:3002**.

The demo uses NGINX OpenTelemetry instrumentation, njs response inspection, an OpenTelemetry Collector, Prometheus, and Grafana.

This helps visualize gateway traffic, policy rejections, rate limiting, and MCP-specific errors.

An MCP tool can report `isError: true` even when HTTP returns `200 OK`. The njs observer helps surface those tool-level failures.

Grafana opens directly on the **NGINX MCP Observability** dashboard. To fill it with realistic traffic (allowed, blocked, unauthenticated, rate-limited, tool errors and JSON-RPC errors across all three roles):

```bash
python3 scripts/populate.py
```

Wait 10–20 seconds for the collector flush and Prometheus scrape, then refresh Grafana.

Each request is labelled with an `mcp.policy` of `allowed`, `blocked`, `unauthenticated`, `malformed` or `rate_limited`. The `mcp.outcome` label (`complete`, `tool_error`, `rpc_error`, …) is only set for `tools/call` requests that NGINX actually proxied.

---

## Resetting before a demo

```bash
./scripts/reset.sh
```

This restarts the stack without rebuilding: deleted pods and records come back, live NGINX edits are discarded, and rate-limit counters and Prometheus history are cleared.

---

## Demo notes

- This project uses a simulated Kubernetes environment, not a real Kubernetes cluster.
- The API keys are fictional and intentionally public.
- The direct MCP endpoint bypasses NGINX and should not be exposed publicly.
- The live configuration editor is intended for trusted local demonstrations.
- The **Further Architectures** tab contains illustrative examples, not additional deployed gateway functionality.
- The stack is a demonstration, not a production-hardened security solution.
- `njs/mcp-observer.mjs` is from the upstream nginx-mcp-js project (F5, Inc.) and is covered by `UPSTREAM-LICENSE` (Apache 2.0).

---

**NGINX MCP Gateway Mission Control demonstrates how NGINX can provide centralized security, traffic control, and observability for MCP tools — without modifying the MCP server.**
