#!/usr/bin/env python3
"""Populate real NGINX metrics through MCP JSON-RPC. Python stdlib only.

Each round sends traffic as every demo role (plus one unauthenticated request),
so Grafana shows allowed, blocked (403), unauthenticated (401), rate-limited (429),
tool errors and JSON-RPC errors.
"""
import argparse, json, os, time, urllib.request, urllib.error, concurrent.futures
from collections import Counter

p = argparse.ArgumentParser()
p.add_argument('--url', default=f"http://localhost:{os.environ.get('GATEWAY_PORT', '8080')}/mcp")
p.add_argument('--rounds', type=int, default=12)
p.add_argument('--burst', type=int, default=30)
p.add_argument('--keys', default='viewer-demo,operator-demo,admin-demo',
               help='comma-separated demo API keys to rotate through')
p.add_argument('--burst-key', default='viewer-demo', help='API key used for the rate-limit burst')
a = p.parse_args()
seq = 0


def call(method, params=None, key=None):
    global seq
    seq += 1
    body = json.dumps({'jsonrpc': '2.0', 'id': seq, 'method': method, 'params': params or {}}).encode()
    headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}
    if key:
        headers['X-API-Key'] = key
    req = urllib.request.Request(a.url, data=body, headers=headers, method='POST')
    try:
        with urllib.request.urlopen(req, timeout=12) as r:
            status = r.status; payload = r.read(100000).decode(errors='replace')
    except urllib.error.HTTPError as e:
        status = e.code; payload = e.read(100000).decode(errors='replace')
    except Exception as e:
        return f'network_error: {e}'
    try:
        data = json.loads(payload)
        outcome = ('rpc_error ' + str(data['error']['code']) if 'error' in data
                   else 'tool_error' if data.get('result', {}).get('isError') else 'success')
    except Exception:
        # NGINX's own 401/403/400 bodies are plain text; 429 is an HTML page.
        outcome = 'rate_limited' if status == 429 else (payload.strip().splitlines() or ['empty'])[0][:60]
    return f'{status} {outcome}'


def tool(name, arguments=None, key=None):
    return call('tools/call', {'name': name, 'arguments': arguments or {}}, key)


# delete_* target nonexistent IDs so populating metrics never removes demo data.
PLAN = [('list_pods', {}), ('get_pod', {'name': 'api-7d9c'}), ('list_records', {}),
        ('echo', {'message': 'demo'}), ('fail_operation', {}), ('get_pod', {'name': 'does-not-exist'}),
        ('create_record', {'id': 'customer-101', 'name': 'duplicate'}),
        ('delete_pod', {'name': 'populate-nonexistent'}), ('delete_record', {'id': 'populate-nonexistent'})]

keys = [k for k in a.keys.split(',') if k]
print('Gateway:', a.url)
for i in range(a.rounds):
    key = keys[i % len(keys)]
    print(f'--- round {i + 1} as {key}')
    for name, args in PLAN:
        print(f'{name:20} {tool(name, args, key)}')
        time.sleep(0.12)  # stay under per-tool rate limits outside the burst
    print('rpc unknown method: ', call('demo/method_does_not_exist', key=key))
    print('rpc unknown tool:   ', tool('not_a_real_tool', key=key))
    print('no API key:         ', tool('list_pods'))
    if i % 3 == 0:
        print('slow_operation:    ', tool('slow_operation', key=key))
    time.sleep(0.25)

print(f'Rate-limit burst ({a.burst} x list_pods as {a.burst_key}):')
with concurrent.futures.ThreadPoolExecutor(max_workers=20) as pool:
    results = list(pool.map(lambda _: tool('list_pods', key=a.burst_key), range(a.burst)))
print(Counter(results))
print('Wait 10-20 seconds for collector flush and Prometheus scrape, then refresh Grafana.')
