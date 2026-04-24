/*
 * svcDiscovery.js — NGINX njs module for Consul-compatible service discovery.
 *
 * Handlers
 *   collect()         — receives mirror subrequests; writes one JSON file
 *                       per unique (proto, name, addr, port) to REGISTRY_DIR.
 *   cleanup()         — js_periodic handler; removes files whose Updated
 *                       timestamp is older than TTL_MS. Runs every
 *                       CLEANUP_INTERVAL_S seconds on a single worker.
 *   catalogServices() — GET /v1/catalog/services
 *   healthService()   — GET /v1/health/service/:service
 *   allServices()     — GET /v1/health/services  (all, custom endpoint)
 *
 * Requires: NGINX njs ≥ 0.8.1  (js_periodic + fs.promises)
 */

import fs from 'fs';

/* ── tunables ─────────────────────────────────────────────────────────────
 *
 * TTL_MS
 *   How old (in ms) a service file must be before cleanup() deletes it.
 *   A service is refreshed on every mirrored request, so this is the
 *   maximum silence period before a service is considered gone.
 *   Must be larger than CLEANUP_INTERVAL_S × 1000.
 *   Default: 5 minutes.
 *
 * CLEANUP_INTERVAL_S
 *   How often js_periodic fires cleanup(). Must match the interval=
 *   value in nginx.conf. Kept here as documentation; NGINX owns the
 *   actual scheduling.
 *   Default: 60 seconds.
 * ────────────────────────────────────────────────────────────────────── */
const TTL_MS             = 5 * 60 * 1000;   // 5 minutes
const CLEANUP_INTERVAL_S = 60;              // must match interval= in svcDiscovery.conf

const REGISTRY_DIR = '/var/lib/nginx/svcDiscovery';

/* ── helpers ──────────────────────────────────────────────────────────── */

function sanitize(s) {
    return String(s).replace(/[^A-Za-z0-9.\-]/g, '_');
}

function registryPath(proto, name, addr, port) {
    return `${REGISTRY_DIR}/${sanitize(proto)}_${sanitize(name)}_${sanitize(addr)}_${port}.json`;
}

async function readAllServices() {
    let files;
    try {
        files = await fs.promises.readdir(REGISTRY_DIR);
    } catch (_) {
        return [];
    }
    const reads = files
        .filter(f => f.endsWith('.json'))
        .map(async f => {
            try {
                const raw = await fs.promises.readFile(`${REGISTRY_DIR}/${f}`, 'utf8');
                return JSON.parse(raw);
            } catch (_) {
                return null;
            }
        });
    return (await Promise.all(reads)).filter(Boolean);
}

function toHealthEntry(svc) {
    return {
        Node: {
            Node:    'nginx',
            Address: svc.Address
        },
        Service: {
            ID:      svc.ID,
            Service: svc.Service,
            Tags:    svc.Tags,
            Address: svc.Address,
            Port:    svc.Port
        },
        Checks: [{
            Node:        'nginx',
            CheckID:     `service:${svc.ID}`,
            Name:        `Service '${svc.Service}' check`,
            Status:      'passing',
            Notes:       'Auto-registered via NGINX mirror directive',
            ServiceID:   svc.ID,
            ServiceName: svc.Service
        }]
    };
}

/* ── handlers ─────────────────────────────────────────────────────────── */

/**
 * collect()
 *
 * Receives mirror subrequests from every discoverable server and writes
 * a JSON file for that (proto, name, addr, port) tuple.  The Updated
 * field is refreshed on every call, which resets the TTL clock.
 */
async function collect(r) {
    const name  = r.headersIn['X-Consul-Server-Name']  || '';
    const addr  = r.headersIn['X-Consul-Server-Addr']  || '';
    const port  = r.headersIn['X-Consul-Server-Port']  || '80';
    const proto = r.headersIn['X-Consul-Server-Proto'] || 'http';

    if (!name || !addr) {
        r.return(400, 'Missing X-Consul-Server-Name or X-Consul-Server-Addr\n');
        return;
    }

    const service = {
        ID:      `${proto}-${name}-${addr}-${port}`,
        Service: name,
        Address: addr,
        Port:    parseInt(port, 10),
        Tags:    [proto],
        Updated: new Date().toISOString()
    };

    try {
        await fs.promises.writeFile(
            registryPath(proto, name, addr, port),
            JSON.stringify(service)
        );
        r.return(200, '');
    } catch (e) {
        r.return(500, `Write failed: ${e.message}\n`);
    }
}

/**
 * cleanup()
 *
 * Called by js_periodic every CLEANUP_INTERVAL_S seconds on one worker.
 *
 * For each .json file in REGISTRY_DIR it checks whether (now - Updated)
 * exceeds TTL_MS.  If so the file is deleted and a notice is written to
 * the error log at INFO level.
 *
 * Design notes
 * ────────────
 * • js_periodic runs on a single designated worker, so there is no
 *   concurrent cleanup race.
 * • collect() is the only other writer and it writes atomically
 *   (writeFile overwrites in place), so a collect() and cleanup() for
 *   the same file cannot interleave at the filesystem level.
 * • If a server block is removed and NGINX is reloaded, the mirror
 *   subrequests for that block stop immediately.  After TTL_MS of
 *   silence its file will be removed by the next cleanup() run.
 * • Quiet-but-valid servers (zero traffic for > TTL_MS) will also be
 *   evicted.  Mitigate with a periodic probe / health-check script
 *   that sends a lightweight request to each vhost.
 */
async function cleanup(e) {
    const now = Date.now();
    let files;

ngx.log(ngx.INFO, `=> cleanup job at ${now}`);

    try {
        files = await fs.promises.readdir(REGISTRY_DIR);
    } catch (err) {
        ngx.log(ngx.ERR,
            `cleanup: cannot read registry dir: ${err.message}`);
        return;
    }

    const candidates = files.filter(f => f.endsWith('.json'));
    ngx.log(ngx.INFO, `candidates: ${candidates}`);

    for (var i = 0; i < candidates.length; i++) {
        var fname = candidates[i];

        const fpath = `${REGISTRY_DIR}/${fname}`;
        ngx.log(ngx.INFO, `checking ${fpath}`);

        let svc;
        try {
            const raw = await fs.promises.readFile(fpath, 'utf8');
            svc = JSON.parse(raw);
        } catch (_) {
            // Unreadable / malformed — remove it
            try { await fs.promises.unlink(fpath); } catch (_) {}
            continue;
        }

        const updatedAt = new Date(svc.Updated).getTime();
        if (isNaN(updatedAt)) {
            // Missing or invalid Updated field — treat as stale
            try { await fs.promises.unlink(fpath); } catch (_) {}
            ngx.log(ngx.WARN,
                `cleanup: removed entry with invalid timestamp: ${fname}`);
            continue;
        }

        const ageMs = now - updatedAt;

        if (ageMs > TTL_MS) {
            try {
                await fs.promises.unlink(fpath);
                ngx.log(ngx.INFO,
                    `cleanup: evicted stale service "${svc.ID}" ` +
                    `(age ${Math.round(ageMs / 1000)}s, ttl ${TTL_MS / 1000}s)`);
            } catch (err) {
                ngx.log(ngx.ERR,
                    `cleanup: failed to delete ${fname}: ${err.message}`);
            }
        } else {
            ngx.log(ngx.INFO, `cleanup: keeping active service "${svc.ID}"`);
        }
    }
}

/**
 * GET /v1/catalog/services
 * Standard Consul response: { "service-name": ["tag", …], … }
 */
async function catalogServices(r) {
    const services = await readAllServices();
    const out = {};
    for (var j = 0; j < services.length; j++) {
        var svc = services[j];
        if (!out[svc.Service]) out[svc.Service] = [];

        for (var i = 0; i < svc.Tags.length; i++) {
            var tag = svc.Tags[i];
            if (!out[svc.Service].includes(tag)) out[svc.Service].push(tag);
        }
    }
    r.headersOut['Content-Type'] = 'application/json';
    r.return(200, JSON.stringify(out, null, 2));
}

/**
 * GET /v1/health/service/:consul_service_name
 * Standard Consul health endpoint for a single named service.
 */
async function healthService(r) {
    const target   = r.variables.consul_service_name;
    const services = await readAllServices();
    const matched  = services.filter(s => s.Service === target);
    r.headersOut['Content-Type'] = 'application/json';
    r.return(200, JSON.stringify(matched.map(toHealthEntry), null, 2));
}

/**
 * GET /v1/health/services   (custom — not standard Consul)
 * All registered services in health-check format.
 */
async function allServices(r) {
    const services = await readAllServices();
    r.headersOut['Content-Type'] = 'application/json';
    r.return(200, JSON.stringify(services.map(toHealthEntry), null, 2));
}

export default { collect, cleanup, catalogServices, healthService, allServices };
