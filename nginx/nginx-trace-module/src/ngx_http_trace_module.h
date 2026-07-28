/*
 * ngx_http_trace_module - shared declarations.
 *
 * Constants, configuration/context types and the prototypes shared across
 * the module's translation units.  See ngx_http_trace_module.c for the
 * directive table and module definition.
 */

#ifndef _NGX_HTTP_TRACE_MODULE_H_INCLUDED_
#define _NGX_HTTP_TRACE_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/* ----- M0.4 / M5 shared-memory session store & ring buffer ---------------- *
 *
 * A slab-backed shm zone holds captured transactions so the control-plane /
 * API endpoint can read them back — potentially from a different worker than
 * the one that captured the request (FR-SHM-1, FR-API-6, AC-12). M5 grows the
 * M0.4 single slot into two bounded, slab-resident structures:
 *
 *   - a bounded transaction RING BUFFER (M5.2): a fixed array of slots written
 *     head-forward; when full, the oldest slot is overwritten (NFR-PERF-4).
 *   - a bounded SESSION store (M5.1): active/stopped/expired capture sessions,
 *     each with a TTL (expires_at), caps, and lifecycle state (FR-SHM-1/2,
 *     FR-SEL-5).
 *
 * Both live in slab memory and every access is guarded by the zone's slab
 * mutex, held only long enough to copy in/out (FR-SHM-2, M5.5).
 */

/*
 * M8 raised this from 4096: a transaction may now carry two body previews
 * (2 x BODY_HARD_MAX) on top of the timeline and the upstream header blocks
 * (2 x 1024). 8192 keeps the worst case inside one slot; see the M8 sizing
 * decision in IMPLEMENTATION_PLAN.md.
 */
#define NGX_HTTP_TRACE_SLOT_MAX   8192  /* max JSON bytes stored per slot     */
#define NGX_HTTP_TRACE_RING_SLOTS   64  /* bounded ring capacity (NFR-PERF-4) */
#define NGX_HTTP_TRACE_MAX_SESSIONS 32  /* hard ceiling on session store      */
#define NGX_HTTP_TRACE_SUMM_MAX    128  /* max method/path bytes kept for list */
/* M10.3 (REVIEW.md #14): session JSON response buffer.  Worst case is a
 * path_prefix of 128 characters all JSON-escaped to \u00xx form (6×), plus
 * ~200 bytes of fixed fields: 128*6 + 200 ≈ 968 → round to 1024. */
#define NGX_HTTP_TRACE_API_SESSION_BUF  1024

/* Session lifecycle state (schema §8.3: capturing|stopped|expired). */
#define NGX_HTTP_TRACE_SESS_CAPTURING  0
#define NGX_HTTP_TRACE_SESS_STOPPED    1
#define NGX_HTTP_TRACE_SESS_EXPIRED    2

/* stopped_reason (schema §8.3: null|expired|max_reached|manual). */
#define NGX_HTTP_TRACE_STOP_NONE        0
#define NGX_HTTP_TRACE_STOP_EXPIRED     1
#define NGX_HTTP_TRACE_STOP_MAX_REACHED 2
#define NGX_HTTP_TRACE_STOP_MANUAL      3

typedef struct {
    ngx_uint_t  committed;                    /* 1 once a txn has been stored */
    size_t      len;                          /* bytes used in json[]         */
    ngx_uint_t  seq;                          /* monotonic ring sequence      */
    ngx_uint_t  session_id;                   /* owning session (0 == none)   */
    ngx_uint_t  txn_seq;                      /* per-session txn index (1..)  */
    time_t      started_at;                   /* request start wall-clock     */
    time_t      ended_at;                     /* commit wall-clock time       */
    ngx_uint_t  duration_us;                  /* request duration (us)        */
    ngx_uint_t  status;                       /* finalizing HTTP status       */
    ngx_uint_t  has_fault;                    /* summary.fault present (bool) */
    ngx_uint_t  fault_code;                   /* fault status, 0 if none      */
    /* M9 correlation keys (FR-EBPF-2): emitted in every transaction so
     * the eBPF agent can join on {worker_pid, connection_id, timestamp}. */
    ngx_pid_t   worker_pid;
    ngx_uint_t  connection_id;
    /* Compact method + path for the TransactionSummary list tier (FR-JSON-2:
     * no heavy detail). Byte copies so the list tier needs no re-parse. */
    size_t      method_len;
    u_char      method[16];
    size_t      path_len;
    u_char      path[NGX_HTTP_TRACE_SUMM_MAX];
    u_char      json[NGX_HTTP_TRACE_SLOT_MAX];/* the transaction as JSON      */
} ngx_http_trace_slot_t;

/* One capture session (M5.1/M6.2). Lives in the slab; visible to all workers. */
typedef struct {
    ngx_uint_t  id;                   /* session id (0 == free entry)         */
    ngx_uint_t  state;                /* SESS_* lifecycle                     */
    ngx_uint_t  stopped_reason;       /* STOP_*                               */
    time_t      created_at;
    time_t      active_since;         /* visible-to-all-workers time (FR-SEL-5)*/
    time_t      expires_at;           /* TTL horizon (0 == no expiry)         */
    time_t      evict_at;             /* removal horizon once expired (M5.4)  */
    ngx_uint_t  max_transactions;     /* per-session cap (bounded by global)  */
    ngx_uint_t  captured;             /* transactions committed to this sess  */

    /* M6.2 filter (schema §8.3 TraceSession.filter). A request is associated
     * with this session when it is traced and matches path_prefix (+fault). */
    unsigned    fault_only:1;         /* only commit faults to this session   */
    ngx_uint_t  fault_code;           /* optional specific fault code (0=any) */
    size_t      path_len;             /* path_prefix length (0 == match all)  */
    u_char      path_prefix[NGX_HTTP_TRACE_SUMM_MAX];
} ngx_http_trace_session_t;

typedef struct {
    ngx_slab_pool_t          *shpool;    /* the zone's slab allocator         */

    /* M5.2 bounded transaction ring buffer. */
    ngx_http_trace_slot_t    *ring;      /* RING_SLOTS contiguous slots       */
    ngx_uint_t                head;      /* next write index (mod RING_SLOTS) */
    ngx_uint_t                count;     /* live slots (<= RING_SLOTS)        */
    ngx_uint_t                seq;       /* monotonic txn sequence counter    */

    /* M9 — count of transactions dropped because the serialised form would
     * overflow the slot (D24). Surfaces what was previously only in the
     * diagnostics log so operators can see drops from the API. */
    ngx_uint_t                dropped;   /* overflow-dropped txn count        */

    /* M5.1 session store. */
    ngx_http_trace_session_t *sessions;  /* MAX_SESSIONS contiguous entries   */
    ngx_uint_t                nsessions; /* live (non-free) session entries    */
    ngx_uint_t                session_seq;/* monotonic session id counter      */
} ngx_http_trace_shctx_t;


/* Self-diagnostics verbosity ladder (FR-CFG-16, §13.1). Ordered so a numeric
 * comparison gates output: a message at level L is emitted iff L <= configured. */
#define NGX_HTTP_TRACE_LOG_OFF    0
#define NGX_HTTP_TRACE_LOG_ERROR  1
#define NGX_HTTP_TRACE_LOG_WARN   2
#define NGX_HTTP_TRACE_LOG_INFO   3
#define NGX_HTTP_TRACE_LOG_DEBUG  4
#define NGX_HTTP_TRACE_LOG_TRACE  5


/* M7.4 — Layer-2 interception is only attempted on nginx releases whose phase
 * engine / content-handler contract we have actually validated. Outside this
 * window `trace_intercept on` degrades to Layer 1 with a WARN (FR-L2-4). */
#define NGX_HTTP_TRACE_L2_MIN_VERSION  1018000
#define NGX_HTTP_TRACE_L2_MAX_VERSION  1029999


typedef struct {
    ngx_shm_zone_t  *shm_zone;        /* set by `trace_zone`; NULL => inert    */

    /* global caps (FR-CFG-9/10/14) */
    ngx_uint_t       max_sessions;    /* trace_max_sessions                    */
    ngx_uint_t       max_transactions;/* trace_max_transactions                */
    ngx_msec_t       retention;       /* trace_retention (ms)                  */

    /* Layer toggles parsed now, behavior wired in later milestones. */
    ngx_flag_t       intercept;       /* trace_intercept (FR-CFG-5)            */
    ngx_flag_t       intercept_active;/* M7.4: intercept survived version gate */
    ngx_uint_t       ebpf;            /* trace_ebpf: off/on (FR-CFG-8)         */
    ngx_flag_t       ebpf_tls;        /* trace_ebpf on tls                     */

    /*
     * M8.6 hardened mode (NFR-SEC-7). When on, body capture is force-disabled
     * everywhere regardless of per-location config, and the raw-byte error_log
     * emit inherited from the M0 spike is suppressed.
     */
    ngx_flag_t       hardened;        /* trace_hardened on|off                 */

    /* self-diagnostics (FR-CFG-15/16, §13.1) */
    ngx_str_t        log_path;        /* trace_log <path>|off                  */
    ngx_uint_t       log_level;       /* trace_log_level (ladder above)        */
    ngx_open_file_t *log_file;        /* dedicated log sink, or NULL           */
} ngx_http_trace_main_conf_t;


/* Merged per-location (also used for server scope) capture configuration. */
typedef struct {
    ngx_flag_t   enable;             /* trace on|off (FR-CFG-2)                */
    ngx_array_t *watch;              /* trace_watch: ngx_http_complex_value-ish;
                                        stored as ngx_str_t names (FR-CFG-3)   */
    ngx_uint_t   upstream_capture;   /* trace_upstream_capture (FR-CFG-6)      */
    ngx_uint_t   body_capture;       /* trace_body_capture (FR-CFG-11)         */
    size_t       body_max;           /* trace_body_max (FR-CFG-12)             */
    ngx_array_t *redact;             /* trace_redact: ngx_str_t names (FR-CFG-13)*/
    ngx_str_t    grpc_proto;         /* trace_grpc_proto (FR-CFG-7)            */
    ngx_flag_t   fault_only;         /* trace_fault_only on|off (FR-SEL-4)     */
    ngx_uint_t   fault_code;         /* optional fault_code filter; 0 == any   */
} ngx_http_trace_loc_conf_t;


/* upstream_capture enum */
#define NGX_HTTP_TRACE_UP_OFF      0
#define NGX_HTTP_TRACE_UP_HEADERS  1
#define NGX_HTTP_TRACE_UP_FULL     2

/* body_capture enum (bit flags: request=1, response=2, both=3) */
#define NGX_HTTP_TRACE_BODY_OFF       0
#define NGX_HTTP_TRACE_BODY_REQUEST   1
#define NGX_HTTP_TRACE_BODY_RESPONSE  2
#define NGX_HTTP_TRACE_BODY_BOTH      3

/*
 * M8 body-capture hard ceiling (NFR-MEM-1 / G3). Whatever trace_body_max says,
 * a single request can never pin more than this per direction — the budget is
 * min(trace_body_max, HARD_MAX), so a misconfiguration cannot exhaust the pool.
 */
#define NGX_HTTP_TRACE_BODY_HARD_MAX  2048

/* ebpf enum */
#define NGX_HTTP_TRACE_EBPF_OFF  0
#define NGX_HTTP_TRACE_EBPF_ON   1


/* ----- M2 per-request trace context & Layer-1 timeline -------------------- *
 *
 * When a request is selected for tracing (M2.2 selector) the module builds a
 * per-request trace context in r->pool holding an append-only list of steps
 * (FR-CTX-1/2, CON-ARCH-2). Each observer phase appends one step with its
 * {phase, t_offset_us}, a watch-list variable snapshot, and a derived status.
 * At LOG the whole context is serialized to JSON and committed once to the shm
 * ring (M2.7 / G5). Non-traced requests carry a ctx with `no_trace` set so
 * every later hook early-returns with zero work (M2.2 / G2 / NFR-PERF-1).
 */

/* Step status (FR-STATUS-1, §2). */
#define NGX_HTTP_TRACE_ST_SUCCESS   0
#define NGX_HTTP_TRACE_ST_ERROR     1
#define NGX_HTTP_TRACE_ST_SKIPPED   2
#define NGX_HTTP_TRACE_ST_DISABLED  3

/* Variable op classification (FR-VAR-2). */
#define NGX_HTTP_TRACE_OP_READ        0
#define NGX_HTTP_TRACE_OP_SET         1
#define NGX_HTTP_TRACE_OP_SET_FAILED  2

/* Step type (FR-STATUS-2): a plain observer step, a condition step, or (M8.4)
 * a subrequest step nested under the parent's timeline. */
#define NGX_HTTP_TRACE_STEP_PHASE       0
#define NGX_HTTP_TRACE_STEP_CONDITION   1
#define NGX_HTTP_TRACE_STEP_SUBREQUEST  2

/* Max steps captured per request (bounded per-request state, NFR-MEM-1). */
#define NGX_HTTP_TRACE_MAX_STEPS  64

/* M3 upstream capture caps (bounded per-request/per-try state, G3/NFR-MEM-1). */
#define NGX_HTTP_TRACE_MAX_TRIES      8    /* upstream.tries[] entries kept    */
#define NGX_HTTP_TRACE_UP_REQ_MAX     1024 /* captured sent-request bytes/try  */
#define NGX_HTTP_TRACE_UP_RESP_MAX    1024 /* captured resp-header bytes/try   */

/* Upstream protocol classification (FR-JSON-3). */
#define NGX_HTTP_TRACE_PROTO_HTTP  0
#define NGX_HTTP_TRACE_PROTO_GRPC  1

/*
 * Fault error_state classification (FR-FAULT-1, M4.1). A coarse machine-readable
 * label for *where/why* a request failed, distinct from the finalizing HTTP
 * status. Derived at LOG from the status, the phase that errored, and whether
 * the failure originated upstream.
 */
#define NGX_HTTP_TRACE_ES_NONE            0
#define NGX_HTTP_TRACE_ES_ACCESS_DENIED   1   /* 401/403, access/auth phase    */
#define NGX_HTTP_TRACE_ES_NOT_FOUND       2   /* 404                            */
#define NGX_HTTP_TRACE_ES_CLIENT_ERROR    3   /* other 4xx                      */
#define NGX_HTTP_TRACE_ES_UPSTREAM_ERROR  4   /* 5xx with a failed upstream try */
#define NGX_HTTP_TRACE_ES_SERVER_ERROR    5   /* other 5xx                      */


/* One watched variable's snapshot at a step: name, evaluated value, op. */
typedef struct {
    ngx_str_t   name;      /* watch name without a leading '$'               */
    ngx_str_t   value;     /* evaluated value (empty when not_found)         */
    ngx_uint_t  op;        /* read / set / set_failed                        */
} ngx_http_trace_var_t;


/*
 * One upstream connection attempt (FR-UP-1/4, FR-RETRY-1, schema §8.3
 * upstream.tries[]). Byte-exact sent request and received response headers are
 * captured per try (capped copies in r->pool), alongside the parsed status and
 * u->state timing/bytes. For gRPC (FR-JSON-3) the protocol is marked and the
 * trailer-sourced grpc-status/message are surfaced as the authoritative result.
 */
typedef struct {
    ngx_uint_t   seq;              /* try index (0-based)                      */
    ngx_uint_t   protocol;         /* PROTO_HTTP / PROTO_GRPC                  */
    ngx_str_t    peer;             /* resolved peer addr (u->state[].peer)     */

    ngx_str_t    request;          /* byte-exact sent request (capped)         */
    unsigned     request_truncated:1;

    ngx_str_t    response_headers; /* byte-exact resp header block (capped)    */
    unsigned     response_truncated:1;
    unsigned     have_response:1;  /* process_header snapshotted this try      */
    unsigned     response_logged:1;/* logged resp bytes at most once per try  */

    ngx_uint_t   status;           /* parsed HTTP status (u->headers_in.status)*/
    off_t        response_length;  /* bytes received (u->state.bytes_received) */
    ngx_msec_int_t connect_time;   /* u->state.connect_time (ms, -1 if unset)  */
    ngx_msec_int_t response_time;  /* u->state.response_time (ms)              */

    /* gRPC trailer-as-truth (FR-GRPC-2): authoritative result distinct from
       the HTTP :status. grpc_status < 0 means "no trailer captured". */
    ngx_int_t    grpc_status;
    ngx_str_t    grpc_message;
} ngx_http_trace_try_t;


/* One recorded timeline entry (FR-CTX-2, schema §8.3 Step). */
typedef struct {
    ngx_uint_t             seq;          /* order within the transaction     */
    ngx_str_t              phase;        /* phase label, e.g. "REWRITE"       */
    ngx_str_t              handler;      /* inferred handler / "core"         */
    ngx_msec_int_t         t_offset_us;  /* microseconds since request start  */
    ngx_uint_t             duration_us;  /* L2: time spent inside the handler */
    ngx_flag_t             timed;        /* L2: duration_us was measured      */
    ngx_uint_t             status;       /* ST_* derived status               */
    ngx_uint_t             type;         /* STEP_PHASE / STEP_CONDITION       */
    ngx_flag_t             evaluated;    /* condition steps: taken/not-taken  */
    ngx_str_t              note;         /* short free-form note (no payload) */
    ngx_array_t           *vars;         /* ngx_http_trace_var_t snapshot     */
} ngx_http_trace_step_t;


/*
 * Fault attribution (FR-FAULT-1, schema §8.3 summary.fault). Populated at LOG
 * when a request finalizes as denied/errored. Links back to the exact failing
 * step by seq so the timeline and the fault agree.
 */
typedef struct {
    unsigned    have:1;        /* a fault was detected for this request       */
    ngx_str_t   phase;         /* phase where the failure surfaced            */
    ngx_str_t   handler;       /* inferred handler at that phase              */
    ngx_uint_t  code;          /* nginx error code (== status here)          */
    ngx_uint_t  status;        /* finalizing HTTP status                     */
    ngx_uint_t  error_state;   /* ES_* coarse classification                 */
    ngx_str_t   message;       /* short human label (no payload bytes)       */
    ngx_int_t   step_seq;      /* seq of the linked step, -1 if none         */
} ngx_http_trace_fault_t;


/*
 * Per-request trace context (module ctx slot, FR-CTX-3). Also carries the M0.3
 * upstream-capture state so the module keeps a single ctx slot per request
 * (skill:handler-module-ctx). Allocated lazily on the first traced hit.
 */
typedef struct {
    unsigned              decided:1;     /* trace decision has been made       */
    unsigned              no_trace:1;    /* decision: not traced (G2 fast path)*/
    unsigned              committed:1;   /* commit-once guard (G5, M2.7)       */
    unsigned              started:1;     /* start_msec baseline captured        */
    ngx_msec_t            start_msec;    /* request start (t_offset baseline) */
    time_t                start_time;    /* request start wall-clock (M6 summary)*/
    ngx_array_t          *steps;         /* ngx_http_trace_step_t, append-only*/
    ngx_uint_t            seq;           /* next step seq                     */

    /* effect-inference deltas (M2.4): last observed URI / chosen location.   */
    ngx_str_t             last_uri;
    ngx_str_t             last_loc;

    /* M0.3 upstream-capture spike state (was a separate ctx). */
    ngx_http_handler_pt   orig_content_handler;
    ngx_int_t           (*orig_create_request)(ngx_http_request_t *r);
    ngx_int_t           (*orig_process_header)(ngx_http_request_t *r);
    unsigned              request_logged:1;
    unsigned              response_logged:1;
    unsigned              wrapped:1;

    /* M3 upstream capture: per-try model (FR-UP-*, FR-RETRY-1). */
    ngx_array_t          *tries;         /* ngx_http_trace_try_t, per attempt */
    ngx_http_trace_try_t *cur_try;       /* try currently being assembled     */
    ngx_uint_t            protocol;      /* PROTO_* for this upstream (grpc?)  */

    /* M4 fault attribution (FR-FAULT-1/2). */
    ngx_http_trace_fault_t fault;        /* populated at LOG on denied/errored */

    /* M5 session association (0 == ring-only, no owning session). */
    ngx_uint_t            session_id;    /* resolved by the selector (M5.1)   */

    /* M8 body capture (FR-BODY-*): bounded, opt-in, redaction-aware. */
    u_char               *req_body;      /* client request body prefix        */
    size_t                req_body_len;
    off_t                 req_body_total;/* full observed length (pre-cap)    */
    u_char               *resp_body;     /* response body prefix              */
    size_t                resp_body_len;
    off_t                 resp_body_total;
    unsigned              req_body_truncated:1;
    unsigned              resp_body_truncated:1;
    unsigned              req_body_binary:1;   /* non-UTF8/control detected   */
    unsigned              resp_body_binary:1;
    unsigned              resp_body_done:1;    /* last_buf seen (FR-BODY-4)   */

    /* M8.2 response metadata recorded at the header filter (FR-BODY-4/5). */
    ngx_str_t             resp_content_type;
    ngx_str_t             resp_content_encoding;

    /* M8.4 subrequest correlation: how many subrequest steps we have added. */
    ngx_uint_t            nsubrequests;
} ngx_http_trace_ctx_t;


void *ngx_http_trace_create_main_conf(ngx_conf_t *cf);
char *ngx_http_trace_init_main_conf(ngx_conf_t *cf, void *conf);
void *ngx_http_trace_create_srv_conf(ngx_conf_t *cf);
char *ngx_http_trace_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
void *ngx_http_trace_create_loc_conf(ngx_conf_t *cf);
char *ngx_http_trace_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
ngx_int_t ngx_http_trace_postconfiguration(ngx_conf_t *cf);

/* M1 directive value parsers (enum-valued directives). */
char *ngx_http_trace_set_upstream_capture(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_http_trace_set_body_capture(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_http_trace_set_ebpf(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
char *ngx_http_trace_set_log_level(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
char *ngx_http_trace_set_str_array(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
char *ngx_http_trace_set_fault_only(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

/*
 * Self-diagnostics emit (FR-LOG-1..6, §13.1). Leveled and short-circuiting:
 * a call at a level above the configured threshold returns immediately without
 * formatting. Routes to the dedicated trace_log sink when configured, otherwise
 * to nginx's error_log. Never logs payload bytes or secrets.
 */
void ngx_http_trace_diag(ngx_http_trace_main_conf_t *mcf,
    ngx_uint_t level, ngx_log_t *log, const char *fmt, ...);

/* M0.4 shm zone directive + init, LOG-phase commit, control endpoint. */
char *ngx_http_trace_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
ngx_int_t ngx_http_trace_init_zone(ngx_shm_zone_t *shm_zone,
    void *data);
char *ngx_http_trace_control(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
ngx_int_t ngx_http_trace_control_handler(ngx_http_request_t *r);
void ngx_http_trace_commit(ngx_http_request_t *r);

/* M5 session store & ring maintenance (all callers hold the slab mutex). */
void ngx_http_trace_expire_locked(ngx_http_trace_shctx_t *shctx,
    time_t now);
ngx_http_trace_session_t *ngx_http_trace_session_find_locked(
    ngx_http_trace_shctx_t *shctx, ngx_uint_t id);

/* M6 control-plane API: session matching, allocation, and routed handlers. */
ngx_uint_t ngx_http_trace_session_match_locked(
    ngx_http_trace_shctx_t *shctx, u_char *uri, size_t uri_len);
ngx_uint_t ngx_http_trace_session_alloc_locked(
    ngx_http_trace_shctx_t *shctx, ngx_http_trace_main_conf_t *mcf,
    ngx_uint_t max_txn, ngx_msec_t ttl_ms, ngx_str_t *path_prefix,
    ngx_uint_t fault_only, ngx_uint_t fault_code);
ngx_int_t ngx_http_trace_api(ngx_http_request_t *r,
    ngx_http_trace_main_conf_t *mcf, ngx_http_trace_shctx_t *shctx,
    ngx_str_t *sub);
ngx_int_t ngx_http_trace_send_json(ngx_http_request_t *r,
    ngx_uint_t status, u_char *body, size_t len);
ngx_int_t ngx_http_trace_send_text(ngx_http_request_t *r,
    ngx_uint_t status, ngx_str_t *ctype, u_char *body, size_t len);
ngx_int_t ngx_http_trace_dump_ring(ngx_http_request_t *r,
    ngx_http_trace_shctx_t *shctx, ngx_uint_t filtered,
    ngx_uint_t want_session, u_char **out_body);

/* M6 API serialization helpers + session-state labels (schema §8.3). */
const char *ngx_http_trace_sess_state_str(ngx_uint_t state);
const char *ngx_http_trace_stop_reason_str(ngx_uint_t reason);
u_char *ngx_http_trace_json_session(u_char *p, u_char *last,
    ngx_http_trace_session_t *s);
u_char *ngx_http_trace_json_summary(u_char *p, u_char *last,
    ngx_http_trace_slot_t *slot);

/*
 * Pass-through observer handlers. M0.2 registers one in every *registrable*
 * phase. They MUST NOT alter routing:
 *   - regular phases return NGX_DECLINED so the phase engine advances to the
 *     next handler exactly as if we were not present;
 *   - the LOG-phase handler returns NGX_OK (LOG handlers are always run and are
 *     expected to return NGX_OK).
 * NGX_HTTP_FIND_CONFIG_PHASE, POST_REWRITE and POST_ACCESS are internal and
 * cannot take handlers (skill:handler-phase-registration).
 */
ngx_int_t ngx_http_trace_log_handler(ngx_http_request_t *r);

/* M2 selector, context, timeline, watch snapshot, inference. */
ngx_int_t ngx_http_trace_post_read_handler(ngx_http_request_t *r);
ngx_http_trace_ctx_t *ngx_http_trace_get_ctx(ngx_http_request_t *r);
ngx_int_t ngx_http_trace_decide(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
ngx_http_trace_step_t *ngx_http_trace_add_step(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx, const char *phase, const char *handler);
void ngx_http_trace_snapshot_watch(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx, ngx_http_trace_step_t *step);
void ngx_http_trace_infer(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx, const char *phase);
ngx_int_t ngx_http_trace_phase_observer(ngx_http_request_t *r,
    const char *phase, ngx_int_t can_decide);
ngx_int_t ngx_http_trace_obs_server_rewrite(ngx_http_request_t *r);
ngx_int_t ngx_http_trace_obs_rewrite(ngx_http_request_t *r);
ngx_int_t ngx_http_trace_obs_preaccess(ngx_http_request_t *r);
ngx_int_t ngx_http_trace_obs_access(ngx_http_request_t *r);

/* M0.3 upstream-capture spike (forward declarations). */
void ngx_http_trace_log_bytes(ngx_http_request_t *r, const char *what,
    u_char *start, u_char *end);
void ngx_http_trace_log_request_bufs(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
ngx_int_t ngx_http_trace_create_request_wrap(ngx_http_request_t *r);
ngx_int_t ngx_http_trace_process_header_wrap(ngx_http_request_t *r);
void ngx_http_trace_wrap_upstream_callbacks(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
ngx_int_t ngx_http_trace_content_handler_wrap(ngx_http_request_t *r);
ngx_int_t ngx_http_trace_precontent_handler(ngx_http_request_t *r);

/* M7.3 — resolve a content handler to a stable, human-meaningful name. */
const char *ngx_http_trace_resolve_handler_name(ngx_http_request_t *r);

/* M3 upstream capture: per-try assembly, state harvest, degrade, serialize. */
ngx_http_trace_try_t *ngx_http_trace_try_begin(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
void ngx_http_trace_copy_capped(ngx_http_request_t *r, ngx_str_t *dst,
    u_char *src, size_t len, size_t cap, unsigned *truncated);
void ngx_http_trace_capture_request(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
void ngx_http_trace_capture_response(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
void ngx_http_trace_grpc_trailers(ngx_http_request_t *r,
    ngx_http_trace_try_t *try);
void ngx_http_trace_harvest_state(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
ngx_int_t ngx_http_trace_upstream_enabled(ngx_http_request_t *r);
u_char *ngx_http_trace_json_upstream(u_char *p, u_char *last,
    ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx);

/* M4 fault detection + fault-only commit gating. */
void ngx_http_trace_detect_fault(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx, ngx_uint_t status,
    ngx_http_trace_step_t *fail_step);
const char *ngx_http_trace_error_state_str(ngx_uint_t es);
u_char *ngx_http_trace_json_fault(u_char *p, u_char *last,
    ngx_http_trace_ctx_t *ctx);

/* ----- M8 redaction, body capture & subrequest correlation ---------------- *
 *
 * The output filter chain saved at postconfiguration. Declared here (and
 * defined in the redact/body TU) so both the header and body filters can hand
 * off to the next module (skill:filter-registration-order).
 */
extern ngx_http_output_header_filter_pt  ngx_http_trace_next_header_filter;
extern ngx_http_output_body_filter_pt    ngx_http_trace_next_body_filter;

/*
 * M8.0 redaction (NFR-SEC-2/3/8, G6). ngx_http_trace_redact_ctx is the single
 * cross-cutting pass invoked from commit immediately before serialization, so
 * no capture path can reach shm unredacted.
 */
ngx_int_t ngx_http_trace_redact_match(ngx_http_trace_loc_conf_t *tlcf,
    u_char *name, size_t n);
void ngx_http_trace_redact_value(ngx_http_request_t *r, ngx_str_t *str);
void ngx_http_trace_redact_header_block(ngx_http_request_t *r,
    ngx_http_trace_loc_conf_t *tlcf, ngx_str_t *block);
void ngx_http_trace_redact_ctx(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);

/* M8.1/M8.2 body capture: budget, gating, bounded append, and the filters. */
size_t ngx_http_trace_body_budget(ngx_http_trace_loc_conf_t *tlcf);
ngx_int_t ngx_http_trace_body_enabled(ngx_http_request_t *r,
    ngx_uint_t direction);
void ngx_http_trace_body_append(ngx_http_request_t *r, u_char **buf,
    size_t *len, off_t *total, unsigned *truncated, unsigned *binary,
    u_char *src, size_t n, size_t budget);
void ngx_http_trace_capture_request_body(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
void ngx_http_trace_capture_response_meta(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx);
ngx_int_t ngx_http_trace_header_filter(ngx_http_request_t *r);
ngx_int_t ngx_http_trace_body_filter(ngx_http_request_t *r, ngx_chain_t *in);
u_char *ngx_http_trace_json_body(u_char *p, u_char *last, const char *name,
    u_char *buf, size_t len, off_t total, unsigned truncated, unsigned binary,
    ngx_str_t *content_type, ngx_str_t *content_encoding);

/* M8.4 subrequest correlation: record a subrequest as a step on the parent. */
void ngx_http_trace_note_subrequest(ngx_http_request_t *r);

/* M8.6 hardened mode (NFR-SEC-7). */
char *ngx_http_trace_set_hardened(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

/*
 * M9.1 — Layer-3 emit API (FR-L3-1/2). ngx_http_trace_step appends a
 * self-described step to the current request's trace timeline when the request
 * is traced, and returns immediately (O(1), no allocation) otherwise. Intended
 * for cooperating C modules and — with a thin binding — njs/Lua scripts.
 */
ngx_int_t ngx_http_trace_step(ngx_http_request_t *r,
    const char *name, const char *result, const char *detail);

/*
 * M9.3 — serialise a session + its transactions into a request-pool buffer
 * for an external collector (schema §8.3). The caller supplies a buffer and
 * gets back the byte count; -1 on allocation failure. Stateless: the caller
 * holds the slab mutex if consistency guarantees are needed.
 */
ngx_int_t ngx_http_trace_export_session(ngx_http_request_t *r,
    ngx_http_trace_shctx_t *shctx, ngx_http_trace_session_t *sess,
    u_char **out_body);

/*
 * JSON serialization primitives (M2.7). ngx_http_trace_json_str escapes an
 * arbitrary byte string into a bounded JSON string literal; the *_str helpers
 * map internal enums onto their normative schema labels.
 */
const char *ngx_http_trace_status_str(ngx_uint_t status);
const char *ngx_http_trace_op_str(ngx_uint_t op);
u_char *ngx_http_trace_json_str(u_char *p, u_char *last, ngx_str_t *s);
u_char *ngx_http_trace_json_vars(u_char *p, u_char *last,
    ngx_http_trace_step_t *step);


extern ngx_module_t  ngx_http_trace_module;

#endif /* _NGX_HTTP_TRACE_MODULE_H_INCLUDED_ */
