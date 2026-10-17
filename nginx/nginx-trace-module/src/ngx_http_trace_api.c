/*
 * ngx_http_trace_module - control-plane API & minimal UI.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_trace_module.h"

/*
 * M6.6/M8.5 — the single-page UI (FR-UI-1..7), compiled in as a static asset so
 * the control plane has zero filesystem dependencies. It talks only to the
 * sibling API paths, deriving the API base by stripping the trailing "/ui" from
 * its own location, so it works under any `trace_control` prefix.
 *
 * Layout is the three-pane Apigee-style trace view: a left rail of captured
 * transactions (FR-UI-2, polled while the session is `capturing`), a center
 * timeline grouped by phase with per-step status icon and elapsed time
 * (FR-UI-3), and a right detail panel with the variable snapshot, upstream
 * request/response, gRPC trailers, body previews and a collapsed Properties
 * block (FR-UI-4). Search highlights matches and auto-expands the groups that
 * contain them (FR-UI-5); view options persist in localStorage (FR-UI-6);
 * Share copies the API-issued deep link and Import renders an exported session
 * entirely client-side, so it also works from `file://` with no nginx at all
 * (FR-UI-7).
 *
 * Security note: every captured byte is rendered through esc() before it
 * reaches innerHTML. Captured payloads are attacker-influenced, so the viewer
 * must not become the injection sink for the data it is inspecting.
 */
static ngx_str_t  ngx_http_trace_ui_html = ngx_string(
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>nginx trace</title><style>"
    "*{box-sizing:border-box}"
    "body{font:13px/1.45 system-ui,sans-serif;margin:0;color:#222;"
    "display:flex;flex-direction:column;height:100vh;overflow:hidden}"
    "#bar{display:flex;flex-wrap:wrap;gap:6px;align-items:center;"
    "padding:5px 8px;border-bottom:1px solid #ccc;background:#f7f7f7}"
    "#main{flex:1;display:flex;min-height:0}"
    "#rail{width:268px;overflow:auto;border-right:1px solid #ccc}"
    "#mid{flex:1;overflow:auto;padding:8px 10px}"
    "#side{width:334px;overflow:auto;border-left:1px solid #ccc;padding:8px 10px}"
    "h3{font-size:11px;letter-spacing:.04em;text-transform:uppercase;"
    "color:#666;margin:12px 0 4px}"
    ".tx{padding:5px 8px;border-bottom:1px solid #eee;cursor:pointer}"
    ".tx:hover{background:#f0f6ff}.tx.sel{background:#dbeafe}"
    ".tx .m{font-weight:600}.tx .p{color:#333;word-break:break-all}"
    ".tx .meta{color:#777;font-size:11px}"
    ".badge{background:#c00;color:#fff;border-radius:3px;padding:0 4px;"
    "font-size:10px;margin-left:4px}"
    "details.grp{margin:3px 0}"
    "details.grp>summary{cursor:pointer;font-weight:600;padding:2px 0}"
    ".st{display:flex;gap:6px;padding:2px 0 2px 16px;cursor:pointer;"
    "border-radius:3px}"
    ".st:hover{background:#f0f6ff}.st.sel{background:#dbeafe}"
    ".st .h{flex:1;word-break:break-all}"
    ".st .t{color:#777;white-space:nowrap;font-variant-numeric:tabular-nums}"
    ".ok{color:#0a0}.er{color:#c00}.sk{color:#b80}.di{color:#999}"
    "table{border-collapse:collapse;width:100%;font-size:12px;margin:2px 0}"
    "td,th{border:1px solid #ddd;padding:2px 5px;text-align:left;"
    "vertical-align:top;word-break:break-all}"
    "th{background:#fafafa;white-space:nowrap}"
    "pre{background:#f6f6f6;padding:6px;overflow:auto;white-space:pre-wrap;"
    "word-break:break-all;margin:3px 0;max-height:260px}"
    "mark{background:#ff0}label{user-select:none}"
    "#stat{color:#555;margin-left:auto}"
    "</style></head><body>"

    "<div id=bar>"
    "<b>nginx trace</b>"
    "<select id=sess onchange=pickSess()></select>"
    "<button onclick=mkSess()>New</button>"
    "<button onclick=stopSess()>Stop</button>"
    "<button onclick=refresh()>Refresh</button>"
    "<button onclick=doExport()>Export</button>"
    "<button onclick=doShare()>Share</button>"
    "<label>Import <input id=imp type=file accept=.json,application/json "
    "onchange=doImport(this)></label>"
    "<input id=q placeholder=Search oninput=draw()>"
    "<label><input type=checkbox id=vSkipped onchange=saveOpts()>skipped</label>"
    "<label><input type=checkbox id=vDisabled onchange=saveOpts()>disabled</label>"
    "<label><input type=checkbox id=vCond onchange=saveOpts()>conditions</label>"
    "<label><input type=checkbox id=vFlow onchange=saveOpts()>flow info</label>"
    "<span id=stat></span>"
    "</div>"
    "<div id=main><div id=rail></div><div id=mid></div><div id=side></div></div>"

    "<script>"
    /* ---- state & helpers ------------------------------------------- */
    "var B=location.pathname.replace(/\\/ui$/,'');"
    "var S=0,TX=[],T=null,STEP=-1,OFF=false,CAP=false,poll=0;"
    "function E(i){return document.getElementById(i)}"
    "function esc(s){return String(s==null?'':s)"
    ".replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}"
    "function api(u,m,b){return fetch(B+u,{method:m||'GET',body:b})"
    ".then(function(r){if(!r.ok)throw new Error(r.status);return r.json()})}"
    "function say(s){E('stat').textContent=s}"

    /* FR-UI-6: view options persisted per user. */
    "function opts(){return{sk:E('vSkipped').checked,di:E('vDisabled').checked,"
    "co:E('vCond').checked,fl:E('vFlow').checked}}"
    "function saveOpts(){try{localStorage.setItem('ngxtrace.opts',"
    "JSON.stringify(opts()))}catch(x){}draw()}"
    "function loadOpts(){var o={};"
    "try{o=JSON.parse(localStorage.getItem('ngxtrace.opts')||'{}')}catch(x){}"
    "E('vSkipped').checked=o.sk!==false;E('vDisabled').checked=!!o.di;"
    "E('vCond').checked=o.co!==false;E('vFlow').checked=!!o.fl}"

    /* FR-UI-3: per-step status icon + elapsed with an epsilon marker. */
    "function ico(s){return s=='error'?'<span class=er>&#10007;</span>'"
    ":s=='skipped'?'<span class=sk>&#8856;</span>'"
    ":s=='disabled'?'<span class=di>&#8861;</span>'"
    ":'<span class=ok>&#10003;</span>'}"
    "function el(u){u=u||0;return u<1000?'&#949;'+u+'&#181;s'"
    ":(u/1000).toFixed(2)+'ms'}"
    "function pth(x){return x.path||x.uri||''}"

    /* ---- sessions -------------------------------------------------- */
    "function loadSess(){return api('/sessions').then(function(d){"
    "var a=d.sessions||[],h='',i;"
    "for(i=0;i<a.length;i++){h+='<option value='+a[i].id+'>#'+a[i].id+' '"
    "+a[i].state+' ('+a[i].captured+')</option>'}"
    "E('sess').innerHTML=h||'<option value=0>no sessions</option>';"
    "if(!S&&a.length)S=a[0].id;"
    "if(S)E('sess').value=S;"
    "for(i=0;i<a.length;i++){if(a[i].id==S)CAP=a[i].state=='capturing'}"
    "return a})}"
    "function pickSess(){S=+E('sess').value;T=null;STEP=-1;loadTxns()}"
    "function mkSess(){api('/sessions','POST').then(function(d){"
    "S=d.id;OFF=false;return loadSess()}).then(loadTxns)"
    ".catch(function(x){say('create failed: '+x.message)})}"
    "function stopSess(){if(!S)return;api('/sessions/'+S,'DELETE')"
    ".then(loadSess).catch(function(x){say('stop failed: '+x.message)})}"
    "function refresh(){OFF=false;loadSess().then(loadTxns)}"

    /* ---- transactions (FR-UI-2) ------------------------------------ */
    "function loadTxns(quiet){if(!S){TX=[];rail();return Promise.resolve()}"
    "return api('/sessions/'+S+'/transactions').then(function(d){"
    "TX=d.transactions||[];rail();if(!quiet)say(TX.length+' transactions')})"
    ".catch(function(x){if(!quiet)say('list failed: '+x.message)})}"
    "function rail(){var h='',i,x;"
    "for(i=0;i<TX.length;i++){x=TX[i];"
    "h+='<div class=\"tx'+((T&&T.seq==x.seq)?' sel':'')+'\" onclick=open_('+i+')>'"
    "+'<div><span class=m>'+esc(x.method)+'</span> '"
    "+(x.fault?'<span class=badge>fault</span>':'')+'</div>'"
    "+'<div class=p>'+esc(pth(x))+'</div>'"
    "+'<div class=meta>'+x.status+' &#183; '+el(x.duration_us)+'</div></div>'}"
    "E('rail').innerHTML=h||'<p style=padding:8px>No transactions.</p>'}"

    "function open_(i){var x=TX[i];STEP=-1;"
    "if(OFF||x.steps){T=x;draw();return}"
    "api('/sessions/'+S+'/transactions/'+x.seq).then(function(d){"
    "d.seq=x.seq;T=d;draw()}).catch(function(e){say('open failed: '+e.message)})}"

    /* ---- center timeline (FR-UI-3) --------------------------------- */
    "function timeline(){var o=opts(),i,s,g=[],h='';"
    "if(!T){return '<p>Select a transaction.</p>'}"
    "h+='<div><b>'+esc(T.method)+' '+esc(pth(T))+'</b> &#8594; '+T.status+'</div>';"
    "var st=T.steps||[];"
    "for(i=0;i<st.length;i++){s=st[i];"
    "if(s.type=='condition'&&!o.co)continue;"
    "if(s.status=='skipped'&&!o.sk)continue;"
    "if(s.status=='disabled'&&!o.di)continue;"
    "if(!g.length||g[g.length-1].ph!=s.phase)g.push({ph:s.phase,it:[]});"
    "g[g.length-1].it.push([i,s])}"
    "if(!g.length){return h+'<p>No steps match the current view options.</p>'}"
    "for(i=0;i<g.length;i++){"
    "h+='<details class=grp open><summary>'+esc(g[i].ph)+' <span class=meta>('"
    "+g[i].it.length+')</span></summary>';"
    "for(var j=0;j<g[i].it.length;j++){var k=g[i].it[j][0];s=g[i].it[j][1];"
    "h+='<div class=\"st'+(k==STEP?' sel':'')+'\" onclick=pick('+k+')>'"
    "+ico(s.status)+'<span class=h>'+esc(s.handler)"
    "+(s.type=='condition'?' <span class=meta>[condition'"
    "+(s.evaluated?'':' not taken')+']</span>':'')"
    "+(o.fl&&s.note?' <span class=meta>'+esc(s.note)+'</span>':'')"
    "+'</span><span class=t>'+el(s.duration_us!=null?s.duration_us:s.t_offset_us)"
    "+'</span></div>'}"
    "h+='</details>'}"
    "return h}"
    "function pick(i){STEP=i;draw()}"

    /* ---- right detail panel (FR-UI-4) ------------------------------ */
    "function kv(k,v){return '<tr><th>'+k+'<td>'+v}"
    "function detail(){if(!T)return '';var h='',i,s=(T.steps||[])[STEP];"
    "if(s){h+='<h3>Step</h3><table>'"
    "+kv('phase',esc(s.phase))+kv('handler',esc(s.handler))"
    "+kv('status',ico(s.status)+' '+esc(s.status))"
    "+kv('offset',el(s.t_offset_us));"
    "if(s.duration_us!=null)h+=kv('duration',el(s.duration_us));"
    "if(s.type)h+=kv('type',esc(s.type));"
    "if(s.note)h+=kv('note',esc(s.note));"
    "h+='</table>';"
    "var v=s.vars||{},k=Object.keys(v);"
    "h+='<h3>Variables</h3>';"
    "if(!k.length){h+='<p>No watched variables.</p>'}"
    "else{h+='<table><tr><th>variable<th>op<th>value';"
    "for(i=0;i<k.length;i++){var op=v[k[i]].op;"
    "h+='<tr><td>'+esc(k[i])+'<td>'"
    "+(op=='set'?'=':op=='set_failed'?'&#8800;':'read')"
    "+'<td>'+esc(v[k[i]].value)}"
    "h+='</table>'}"
    "h+='<details><summary>Properties</summary><pre>'"
    "+esc(JSON.stringify(s,null,1))+'</pre></details>'}"

    "if(T.fault){h+='<h3>Fault</h3><table>'"
    "+kv('phase',esc(T.fault.phase))+kv('handler',esc(T.fault.handler))"
    "+kv('code',T.fault.code)+kv('status',T.fault.status)"
    "+kv('error_state',esc(T.fault.error_state))"
    "+kv('message',esc(T.fault.message))+kv('step_seq',T.fault.step_seq)"
    "+'</table>'}"

    "if(T.upstream){var u=T.upstream,tr=u.tries||[];"
    "h+='<h3>Upstream ('+esc(u.protocol)+')</h3>';"
    "for(i=0;i<tr.length;i++){var t=tr[i];"
    "h+='<details'+(i==tr.length-1?' open':'')+'><summary>try '+t.seq+' &#183; '"
    "+esc(t.peer)+' &#183; '+t.status+'</summary><table>'"
    "+kv('bytes',t.bytes);"
    "if(t.connect_ms!=null)h+=kv('connect',t.connect_ms+'ms');"
    "if(t.response_ms!=null)h+=kv('response',t.response_ms+'ms');"
    "if(t.grpc_status!==undefined)h+=kv('grpc-status',t.grpc_status===null"
    "?'none':t.grpc_status);"
    "if(t.grpc_message)h+=kv('grpc-message',esc(t.grpc_message));"
    "h+='</table>'"
    "+'<h3>Request sent'+(t.request_truncated?' (truncated)':'')+'</h3>'"
    "+'<pre>'+esc(t.request)+'</pre>'"
    "+'<h3>Response received'+(t.response_truncated?' (truncated)':'')+'</h3>'"
    "+'<pre>'+esc(t.response_headers)+'</pre></details>'}}"

    "h+=body_('Request body',T.request_body);"
    "h+=body_('Response body',T.response_body);"
    "return h}"

    "function body_(t,b){if(!b)return '';"
    "var h='<h3>'+t+'</h3><table>'"
    "+kv('captured',b.captured_bytes+' / '+b.total_bytes+' bytes')"
    "+kv('truncated',b.truncated?'yes':'no');"
    "if(b.content_type)h+=kv('content-type',esc(b.content_type));"
    "if(b.content_encoding)h+=kv('content-encoding',esc(b.content_encoding));"
    "h+='</table>';"
    "if(b.preview!=null)h+='<pre>'+esc(b.preview)+'</pre>';"
    "else if(b.preview_hex!=null)h+='<pre>'+esc(b.preview_hex)+'</pre>';"
    "else if(b.total_bytes)h+='<p>Not captured (served from file or capture off).</p>';"
    "return h}"

    /* ---- draw + search (FR-UI-5) ----------------------------------- */
    "function draw(){E('mid').innerHTML=timeline();E('side').innerHTML=detail();"
    "rail();var q=E('q').value;if(q){hilite(E('mid'),q);hilite(E('side'),q)}"
    "if(T)location.hash='s='+S+'&t='+(T.seq||0)}"
    "function hilite(root,q){"
    "var w=document.createTreeWalker(root,NodeFilter.SHOW_TEXT,null,false);"
    "var n,l=[];while((n=w.nextNode()))l.push(n);"
    "var ql=q.toLowerCase();"
    "for(var i=0;i<l.length;i++){var t=l[i],v=t.nodeValue,lo=v.toLowerCase();"
    "if(lo.indexOf(ql)<0)continue;"
    "var d=t.parentNode.closest?t.parentNode.closest('details'):null;"
    "while(d){d.open=true;d=d.parentNode.closest('details')}"
    "var out='',idx=0,p;"
    "while((p=lo.indexOf(ql,idx))>=0){out+=esc(v.slice(idx,p))+'<mark>'"
    "+esc(v.slice(p,p+q.length))+'</mark>';idx=p+q.length}"
    "out+=esc(v.slice(idx));"
    "var sp=document.createElement('span');sp.innerHTML=out;"
    "t.parentNode.replaceChild(sp,t)}}"

    /* ---- export / share / import (FR-UI-7, FR-API-8/9) ------------- */
    "function doExport(){if(!S){say('no session');return}"
    "api('/sessions/'+S+'/export').then(function(d){"
    "var a=document.createElement('a');"
    "a.href=URL.createObjectURL(new Blob([JSON.stringify(d)],"
    "{type:'application/json'}));"
    "a.download='trace-session-'+S+'.json';a.click();say('exported')})"
    ".catch(function(x){say('export failed: '+x.message)})}"

    "function copy_(s){try{navigator.clipboard.writeText(s)}catch(x){}"
    "say('link copied: '+s)}"
    "function doShare(){if(OFF||!S){copy_(location.href);return}"
    "api('/sessions/'+S+'/share').then(function(d){"
    "copy_(d.url+(T?'&t='+(T.seq||0):''))})"
    ".catch(function(){copy_(location.href)})}"

    "function doImport(inp){var f=inp.files&&inp.files[0];if(!f)return;"
    "var rd=new FileReader();rd.onload=function(){var d;"
    "try{d=JSON.parse(rd.result)}catch(x){say('import: bad JSON');return}"
    "OFF=true;CAP=false;TX=d.transactions||[];T=TX[0]||null;STEP=-1;"
    "E('sess').innerHTML='<option>offline'"
    "+(d.session?' #'+d.session.id:'')+'</option>';"
    "say('offline: '+TX.length+' transactions');draw();"
    "if(location.protocol.indexOf('file')!==0){"
    "api('/import','POST',rd.result).catch(function(){})}};"
    "rd.readAsText(f)}"

    /* ---- boot: deep-link restore + live polling -------------------- */
    "function fromHash(){var m=/s=(\\d+)/.exec(location.hash);"
    "if(m)S=+m[1];m=/t=(\\d+)/.exec(location.hash);return m?+m[1]:0}"
    "loadOpts();"
    "var wantT=fromHash();"
    "loadSess().then(loadTxns).then(function(){"
    "if(wantT){for(var i=0;i<TX.length;i++){if(TX[i].seq==wantT){open_(i);return}}}"
    "draw()}).catch(function(){draw()});"
    "poll=setInterval(function(){if(!OFF&&S&&CAP){"
    "loadSess();loadTxns(true)}},2000);"
    "</script></body></html>");

static void ngx_http_trace_import_body_handler(ngx_http_request_t *r);


/*
 * `trace_control;` — install the control content handler on a location.
 */
char *
ngx_http_trace_control(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_trace_control_handler;

    return NGX_CONF_OK;
}

/* Session lifecycle state label (schema §8.3). */
const char *
ngx_http_trace_sess_state_str(ngx_uint_t state)
{
    switch (state) {
    case NGX_HTTP_TRACE_SESS_STOPPED: return "stopped";
    case NGX_HTTP_TRACE_SESS_EXPIRED: return "expired";
    default:                          return "capturing";
    }
}

/* stopped_reason label; emits the bare token `null` when none (schema §8.3). */
const char *
ngx_http_trace_stop_reason_str(ngx_uint_t reason)
{
    switch (reason) {
    case NGX_HTTP_TRACE_STOP_EXPIRED:     return "\"expired\"";
    case NGX_HTTP_TRACE_STOP_MAX_REACHED: return "\"max_reached\"";
    case NGX_HTTP_TRACE_STOP_MANUAL:      return "\"manual\"";
    default:                              return "null";
    }
}

/*
 * Serialize one session into buf as a TraceSession object (schema §8.3). Field
 * names are normative: id, created_at, active_since, expires_at, state,
 * stopped_reason, max_transactions, captured, filter{...}. Caller holds the
 * mutex (reads shared session fields). Returns the advanced write pointer.
 */
u_char *
ngx_http_trace_json_session(u_char *p, u_char *last,
    ngx_http_trace_session_t *s)
{
    ngx_str_t  pp;

    pp.data = s->path_prefix;
    pp.len  = s->path_len;

    p = ngx_snprintf(p, last - p,
        "{\"id\":%ui,\"created_at\":%T,\"active_since\":%T,\"expires_at\":%T,"
        "\"state\":\"%s\",\"stopped_reason\":%s,\"max_transactions\":%ui,"
        "\"captured\":%ui,\"filter\":{\"path_prefix\":",
        s->id, s->created_at, s->active_since, s->expires_at,
        ngx_http_trace_sess_state_str(s->state),
        ngx_http_trace_stop_reason_str(s->stopped_reason),
        s->max_transactions, s->captured);

    p = ngx_http_trace_json_str(p, last, &pp);

    p = ngx_snprintf(p, last - p,
        ",\"fault_only\":%s,\"fault_code\":%ui}}",
        s->fault_only ? "true" : "false", s->fault_code);

    return p;
}

/*
 * Allocate a new session in the store (caller holds the mutex). Enforces
 * trace_max_sessions (FR-API-1 -> 429). Returns the new id, or 0 if the store
 * is full. ttl_ms == 0 falls back to the configured retention.
 */
ngx_uint_t
ngx_http_trace_session_alloc_locked(ngx_http_trace_shctx_t *shctx,
    ngx_http_trace_main_conf_t *mcf, ngx_uint_t max_txn, ngx_msec_t ttl_ms,
    ngx_str_t *path_prefix, ngx_uint_t fault_only, ngx_uint_t fault_code)
{
    ngx_http_trace_session_t  *s;
    ngx_uint_t                 i;
    time_t                     now, ttl_s;

    if (shctx->nsessions >= mcf->max_sessions) {
        return 0;                            /* cap reached (429) */
    }

    s = NULL;
    for (i = 0; i < NGX_HTTP_TRACE_MAX_SESSIONS; i++) {
        if (shctx->sessions[i].id == 0) {
            s = &shctx->sessions[i];
            break;
        }
    }
    if (s == NULL) {
        return 0;                            /* hard ceiling reached */
    }

    now = ngx_time();
    ttl_s = (time_t) ((ttl_ms ? ttl_ms : mcf->retention) / 1000);
    if (ttl_s == 0) {
        ttl_s = 60;                          /* sane floor so sessions expire */
    }

    ngx_memzero(s, sizeof(ngx_http_trace_session_t));
    s->id = ++shctx->session_seq;
    s->state = NGX_HTTP_TRACE_SESS_CAPTURING;
    s->stopped_reason = NGX_HTTP_TRACE_STOP_NONE;
    s->created_at = now;
    s->active_since = now;
    s->expires_at = now + ttl_s;
    s->evict_at = s->expires_at + ttl_s;     /* grace = one more TTL (M5.4) */
    s->max_transactions =
        (max_txn && max_txn < mcf->max_transactions) ? max_txn
                                                      : mcf->max_transactions;
    s->captured = 0;
    s->fault_only = fault_only ? 1 : 0;
    s->fault_code = fault_code;

    if (path_prefix != NULL && path_prefix->len) {
        s->path_len = ngx_min(path_prefix->len, NGX_HTTP_TRACE_SUMM_MAX);
        ngx_memcpy(s->path_prefix, path_prefix->data, s->path_len);
    }

    shctx->nsessions++;

    return s->id;
}

/*
 * Emit a TransactionSummary (schema §8.3, FR-JSON-2: no steps/bodies). Built
 * from the compact summary fields cached on the slot, so no re-parse of the
 * full JSON is needed. Caller holds the mutex.
 */
u_char *
ngx_http_trace_json_summary(u_char *p, u_char *last, ngx_http_trace_slot_t *slot)
{
    ngx_str_t  m, u;

    m.data = slot->method; m.len = slot->method_len;
    u.data = slot->path;   u.len = slot->path_len;

    p = ngx_snprintf(p, last - p, "{\"seq\":%ui,\"method\":", slot->txn_seq);
    p = ngx_http_trace_json_str(p, last, &m);
    p = ngx_snprintf(p, last - p, ",\"uri\":");
    p = ngx_http_trace_json_str(p, last, &u);
    p = ngx_snprintf(p, last - p,
        ",\"status\":%ui,\"started_at\":%T,\"ended_at\":%T,"
        "\"duration_us\":%ui,\"fault\":%s}",
        slot->status, slot->started_at, slot->ended_at, slot->duration_us,
        slot->has_fault ? "true" : "false");

    return p;
}

/*
 * Routed control-plane API (M6.2-M6.6). `sub` is the path after the control
 * location prefix, already stripped of the leading '/'. Recognized routes:
 *   GET  ui
 *   POST sessions                              (create; 429 at cap)
 *   GET  sessions                              (list)
 *   GET  sessions/{id}                         (detail)
 *   DEL  sessions/{id}                         (stop; stopped_reason=manual)
 *   GET  sessions/{id}/transactions            (summary list)
 *   GET  sessions/{id}/transactions/{txn}      (full transaction)
 *   GET  sessions/{id}/export                  (whole session artifact)
 */
ngx_int_t
ngx_http_trace_api(ngx_http_request_t *r, ngx_http_trace_main_conf_t *mcf,
    ngx_http_trace_shctx_t *shctx, ngx_str_t *sub)
{
    ngx_http_trace_session_t  *sess;
    ngx_http_trace_slot_t     *slot;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_str_t                  seg, arg, ppfx;
    u_char                    *body, *p, *last, *rest;
    size_t                     cap;
    time_t                     expires;
    ngx_int_t                  rc;
    ngx_uint_t                 id, i, idx, n, want_txn, max_txn, fcode, fonly;
    ngx_msec_t                 ttl_ms;

    /* ---- ui ---------------------------------------------------------- */
    if (sub->len == 2 && ngx_strncmp(sub->data, "ui", 2) == 0) {
        ngx_str_t  html = ngx_string("text/html");
        if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
            return NGX_HTTP_NOT_ALLOWED;
        }
        (void) ngx_http_discard_request_body(r);
        return ngx_http_trace_send_text(r, NGX_HTTP_OK, &html,
            ngx_http_trace_ui_html.data, ngx_http_trace_ui_html.len);
    }

    /* ---- import (FR-API-9) ------------------------------------------- */
    /*
     * The offline viewer is entirely client-side (the browser parses the export
     * itself), so this endpoint exists to *validate* an artifact and to give the
     * UI a definite accept/reject answer. We deliberately do not re-inject the
     * transactions into shm: doing so would let an unauthenticated POST forge
     * arbitrary trace content into an operator's live ring, which is a far worse
     * trade than requiring the viewer to render its own file.
     */
    if (sub->len == 6 && ngx_strncmp(sub->data, "import", 6) == 0) {
        if (!(r->method & NGX_HTTP_POST)) {
            return NGX_HTTP_NOT_ALLOWED;
        }
        rc = ngx_http_read_client_request_body(r,
                                              ngx_http_trace_import_body_handler);
        if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            return rc;
        }
        return NGX_DONE;               /* body handler finalizes (G8) */
    }

    /* ---- GET /session (M9 convenience: create session via GET) -------- */
    if (sub->len >= 7 && ngx_strncmp(sub->data, "session", 7) == 0
        && (sub->len == 7 || sub->data[7] == '?' || sub->data[7] == '&'))
    {
        if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
            return NGX_HTTP_NOT_ALLOWED;
        }
        (void) ngx_http_discard_request_body(r);

        max_txn = 0; fonly = 0; fcode = 0; ttl_ms = 0;
        ppfx.len = 0; ppfx.data = NULL;
        if (r->args.len) {
            if (ngx_http_arg(r, (u_char *) "max", 3, &arg) == NGX_OK) {
                rc = ngx_atoi(arg.data, arg.len);
                if (rc != NGX_ERROR) { max_txn = (ngx_uint_t) rc; }
            }
            if (ngx_http_arg(r, (u_char *) "path", 4, &arg) == NGX_OK) {
                ppfx = arg;
            }
            if (ngx_http_arg(r, (u_char *) "ttl", 3, &arg) == NGX_OK) {
                rc = ngx_atoi(arg.data, arg.len);
                if (rc != NGX_ERROR && rc > 0) {
                    ttl_ms = (ngx_msec_t) rc * 1000;
                }
            }
            if (ngx_http_arg(r, (u_char *) "fault_only", 10, &arg)
                == NGX_OK)
            {
                fonly = (arg.len == 1 && arg.data[0] == '1')
                        || (arg.len == 4
                            && ngx_strncmp(arg.data, "true", 4) == 0);
            }
            if (ngx_http_arg(r, (u_char *) "fault_code", 10, &arg)
                == NGX_OK)
            {
                rc = ngx_atoi(arg.data, arg.len);
                if (rc != NGX_ERROR && rc >= 100 && rc <= 599) {
                    fcode = (ngx_uint_t) rc;
                }
            }
        }

        ngx_shmtx_lock(&shctx->shpool->mutex);
        ngx_http_trace_expire_locked(shctx, ngx_time());
        id = ngx_http_trace_session_alloc_locked(shctx, mcf, max_txn, ttl_ms,
                                                 &ppfx, fonly, fcode);
        if (id == 0) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            body = (u_char *) "{\"error\":\"max_sessions_reached\"}";
            return ngx_http_trace_send_json(r,
                NGX_HTTP_TOO_MANY_REQUESTS, body, ngx_strlen(body));
        }
        sess = ngx_http_trace_session_find_locked(shctx, id);
        body = ngx_pnalloc(r->pool, NGX_HTTP_TRACE_API_SESSION_BUF);
        if (body == NULL) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        p = ngx_http_trace_json_session(body, body + NGX_HTTP_TRACE_API_SESSION_BUF, sess);
        ngx_shmtx_unlock(&shctx->shpool->mutex);

        return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                        (size_t) (p - body));
    }

    /* ---- GET /last (M9 convenience: last committed transaction) ---------- */
    if (sub->len == 4 && ngx_strncmp(sub->data, "last", 4) == 0) {
        ngx_http_trace_slot_t  *last_slot;
        ngx_uint_t              idx;

        if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
            return NGX_HTTP_NOT_ALLOWED;
        }
        (void) ngx_http_discard_request_body(r);

        body = ngx_pnalloc(r->pool, NGX_HTTP_TRACE_SLOT_MAX + 1);
        if (body == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ngx_shmtx_lock(&shctx->shpool->mutex);
        ngx_http_trace_expire_locked(shctx, ngx_time());

        last_slot = NULL;
        for (i = 0; i < shctx->count; i++) {
            idx = (shctx->head + NGX_HTTP_TRACE_RING_SLOTS
                   - shctx->count + i) % NGX_HTTP_TRACE_RING_SLOTS;
            if (shctx->ring[idx].committed && shctx->ring[idx].len > 0) {
                last_slot = &shctx->ring[idx];
            }
        }

        if (last_slot == NULL) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            return NGX_HTTP_NOT_FOUND;
        }

        p = ngx_cpymem(body, last_slot->json, last_slot->len);
        ngx_shmtx_unlock(&shctx->shpool->mutex);

        return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                        (size_t) (p - body));
    }

    /* Everything else is under "sessions". */
    if (sub->len < 8 || ngx_strncmp(sub->data, "sessions", 8) != 0) {
        return NGX_HTTP_NOT_FOUND;
    }

    /* rest = sub after "sessions", stripped of one leading '/'. */
    rest = sub->data + 8;
    n = sub->len - 8;
    if (n && *rest == '/') { rest++; n--; }

    /* ---- collection: /sessions -------------------------------------- */
    if (n == 0) {
        if (r->method & NGX_HTTP_POST) {
            (void) ngx_http_discard_request_body(r);

            /* Creation parameters via query args (FR-API-1). */
            max_txn = 0; fonly = 0; fcode = 0; ttl_ms = 0;
            ppfx.len = 0; ppfx.data = NULL;
            if (r->args.len) {
                if (ngx_http_arg(r, (u_char *) "max", 3, &arg) == NGX_OK) {
                    rc = ngx_atoi(arg.data, arg.len);
                    if (rc != NGX_ERROR) { max_txn = (ngx_uint_t) rc; }
                }
                if (ngx_http_arg(r, (u_char *) "path", 4, &arg) == NGX_OK) {
                    ppfx = arg;
                }
                /* M9 — TTL (FR-API-12, D22): restores the time-boxing
                 * guarantee NFR-SEC-6 rests on. Seconds, converted to ms.
                 * 0 means "use trace_retention" (the existing default). */
                if (ngx_http_arg(r, (u_char *) "ttl", 3, &arg) == NGX_OK) {
                    rc = ngx_atoi(arg.data, arg.len);
                    if (rc != NGX_ERROR && rc > 0) {
                        ttl_ms = (ngx_msec_t) rc * 1000;
                    }
                }
                if (ngx_http_arg(r, (u_char *) "fault_only", 10, &arg)
                    == NGX_OK)
                {
                    fonly = (arg.len == 1 && arg.data[0] == '1')
                            || (arg.len == 4
                                && ngx_strncmp(arg.data, "true", 4) == 0);
                }
                if (ngx_http_arg(r, (u_char *) "fault_code", 10, &arg)
                    == NGX_OK)
                {
                    rc = ngx_atoi(arg.data, arg.len);
                    if (rc != NGX_ERROR && rc >= 100 && rc <= 599) {
                        fcode = (ngx_uint_t) rc;
                    }
                }
            }

            ngx_shmtx_lock(&shctx->shpool->mutex);
            ngx_http_trace_expire_locked(shctx, ngx_time());
            id = ngx_http_trace_session_alloc_locked(shctx, mcf, max_txn, ttl_ms,
                                                     &ppfx, fonly, fcode);
            if (id == 0) {
                ngx_shmtx_unlock(&shctx->shpool->mutex);
                body = (u_char *) "{\"error\":\"max_sessions_reached\"}";
                return ngx_http_trace_send_json(r,
                    NGX_HTTP_TOO_MANY_REQUESTS, body, ngx_strlen(body));
            }
            sess = ngx_http_trace_session_find_locked(shctx, id);
            body = ngx_pnalloc(r->pool, NGX_HTTP_TRACE_API_SESSION_BUF);
            if (body == NULL) {
                ngx_shmtx_unlock(&shctx->shpool->mutex);
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            p = ngx_http_trace_json_session(body, body + NGX_HTTP_TRACE_API_SESSION_BUF, sess);
            ngx_shmtx_unlock(&shctx->shpool->mutex);

            return ngx_http_trace_send_json(r, NGX_HTTP_CREATED, body,
                                            (size_t) (p - body));
        }

        if (r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD)) {
            (void) ngx_http_discard_request_body(r);

            cap = 32 + (size_t) NGX_HTTP_TRACE_MAX_SESSIONS * 640;
            body = ngx_pnalloc(r->pool, cap);
            if (body == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            p = body; last = body + cap;
            p = ngx_cpymem(p, "{\"sessions\":[", sizeof("{\"sessions\":[") - 1);

            ngx_shmtx_lock(&shctx->shpool->mutex);
            ngx_http_trace_expire_locked(shctx, ngx_time());
            for (i = 0, n = 0; i < NGX_HTTP_TRACE_MAX_SESSIONS; i++) {
                if (shctx->sessions[i].id == 0) { continue; }
                if (n++) { *p++ = ','; }
                p = ngx_http_trace_json_session(p, last, &shctx->sessions[i]);
            }
            ngx_shmtx_unlock(&shctx->shpool->mutex);

            p = ngx_snprintf(p, last - p,
                "],\"nsessions\":%ui,\"dropped\":%ui}",
                n, shctx->dropped);
            return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                            (size_t) (p - body));
        }

        return NGX_HTTP_NOT_ALLOWED;
    }

    /* ---- item: /sessions/{id}[/...] --------------------------------- */
    seg.data = rest;
    seg.len = 0;
    while (seg.len < n && rest[seg.len] != '/') { seg.len++; }

    id = ngx_atoi(seg.data, seg.len);
    if (id == (ngx_uint_t) NGX_ERROR || id == 0) {
        return NGX_HTTP_NOT_FOUND;
    }

    /* tail = remainder after the id segment, stripped of one '/'. */
    rest += seg.len;
    n -= seg.len;
    if (n && *rest == '/') { rest++; n--; }

    /* ---- DELETE /sessions/{id} -> stop (stopped_reason=manual) ------ */
    if (n == 0 && (r->method & NGX_HTTP_DELETE)) {
        (void) ngx_http_discard_request_body(r);
        ngx_shmtx_lock(&shctx->shpool->mutex);
        sess = ngx_http_trace_session_find_locked(shctx, id);
        if (sess == NULL) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            return NGX_HTTP_NOT_FOUND;
        }
        sess->state = NGX_HTTP_TRACE_SESS_STOPPED;
        sess->stopped_reason = NGX_HTTP_TRACE_STOP_MANUAL;
        body = ngx_pnalloc(r->pool, NGX_HTTP_TRACE_API_SESSION_BUF);
        if (body == NULL) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        p = ngx_http_trace_json_session(body, body + NGX_HTTP_TRACE_API_SESSION_BUF, sess);
        ngx_shmtx_unlock(&shctx->shpool->mutex);
        return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                        (size_t) (p - body));
    }

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    (void) ngx_http_discard_request_body(r);

    /* ---- GET /sessions/{id} -> detail ------------------------------- */
    if (n == 0) {
        ngx_shmtx_lock(&shctx->shpool->mutex);
        ngx_http_trace_expire_locked(shctx, ngx_time());
        sess = ngx_http_trace_session_find_locked(shctx, id);
        if (sess == NULL) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            return NGX_HTTP_NOT_FOUND;
        }
        body = ngx_pnalloc(r->pool, NGX_HTTP_TRACE_API_SESSION_BUF);
        if (body == NULL) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        p = ngx_http_trace_json_session(body, body + NGX_HTTP_TRACE_API_SESSION_BUF, sess);
        ngx_shmtx_unlock(&shctx->shpool->mutex);
        return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                        (size_t) (p - body));
    }

    /* ---- GET /sessions/{id}/share (FR-API-8) ------------------------ */
    /*
     * A deep link into the UI for this session, time-boxed to the session's own
     * retention horizon: after `expires_at` the session is evicted and the link
     * resolves to a 404 (AC-15), so the URL cannot outlive the data it points
     * at. The link is built from this request's Host + the control prefix so it
     * is correct behind any prefix, and it carries no credential — access stays
     * governed by whatever protects the `trace_control` location (FR-API-10).
     */
    if (n == 5 && ngx_strncmp(rest, "share", 5) == 0) {
        ngx_str_t  host, prefix;

        ngx_shmtx_lock(&shctx->shpool->mutex);
        ngx_http_trace_expire_locked(shctx, ngx_time());
        sess = ngx_http_trace_session_find_locked(shctx, id);
        if (sess == NULL) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            return NGX_HTTP_NOT_FOUND;
        }
        expires = sess->expires_at;
        ngx_shmtx_unlock(&shctx->shpool->mutex);

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        host = r->headers_in.server;
        if (host.len == 0 && r->headers_in.host != NULL) {
            host = r->headers_in.host->value;
        }

        /*
         * The control prefix is conventionally written with a trailing slash
         * ("/__trace/"), so append "ui" rather than "/ui" in that case —
         * otherwise the link carries a double slash, which is a different path
         * to nginx and would not match the location on the way back in.
         */
        prefix = (clcf != NULL) ? clcf->name : r->uri;
        if (prefix.len && prefix.data[prefix.len - 1] == '/') {
            prefix.len--;
        }

        cap = 128 + host.len + prefix.len;
        body = ngx_pnalloc(r->pool, cap);
        if (body == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        p = body;
        last = body + cap;

        p = ngx_cpymem(p, "{\"url\":\"", sizeof("{\"url\":\"") - 1);
        p = ngx_snprintf(p, last - p, "%s://%V%V/ui#s=%ui",
                         r->connection->ssl ? "https" : "http",
                         &host, &prefix, id);
        p = ngx_snprintf(p, last - p, "\",\"session_id\":%ui,\"expires_at\":%T}",
                         id, expires);

        return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                        (size_t) (p - body));
    }

    /* ---- GET /sessions/{id}/export ---------------------------------- */
    if (n == 6 && ngx_strncmp(rest, "export", 6) == 0) {
        cap = 128 + (size_t) NGX_HTTP_TRACE_RING_SLOTS
                    * (NGX_HTTP_TRACE_SLOT_MAX + 1);
        body = ngx_pnalloc(r->pool, cap);
        if (body == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        p = body; last = body + cap;

        ngx_shmtx_lock(&shctx->shpool->mutex);
        ngx_http_trace_expire_locked(shctx, ngx_time());
        sess = ngx_http_trace_session_find_locked(shctx, id);
        if (sess == NULL) {
            ngx_shmtx_unlock(&shctx->shpool->mutex);
            return NGX_HTTP_NOT_FOUND;
        }
        p = ngx_cpymem(p, "{\"session\":", sizeof("{\"session\":") - 1);
        p = ngx_http_trace_json_session(p, last, sess);
        p = ngx_cpymem(p, ",\"transactions\":[",
                       sizeof(",\"transactions\":[") - 1);
        for (i = 0, n = 0; i < shctx->count; i++) {
            idx = (shctx->head + NGX_HTTP_TRACE_RING_SLOTS - shctx->count + i)
                  % NGX_HTTP_TRACE_RING_SLOTS;
            slot = &shctx->ring[idx];
            if (!slot->committed || slot->len == 0
                || slot->session_id != id)
            {
                continue;
            }
            if (n++) { *p++ = ','; }
            p = ngx_cpymem(p, slot->json, slot->len);
        }
        ngx_shmtx_unlock(&shctx->shpool->mutex);

        p = ngx_cpymem(p, "]}", 2);
        return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                        (size_t) (p - body));
    }

    /* ---- transactions[...] ------------------------------------------ */
    if (n < 12 || ngx_strncmp(rest, "transactions", 12) != 0) {
        return NGX_HTTP_NOT_FOUND;
    }
    rest += 12;
    n -= 12;
    if (n && *rest == '/') { rest++; n--; }

    /* GET /sessions/{id}/transactions/{txn} -> full transaction detail. */
    want_txn = 0;
    if (n != 0) {
        want_txn = ngx_atoi(rest, n);
        if (want_txn == (ngx_uint_t) NGX_ERROR || want_txn == 0) {
            return NGX_HTTP_NOT_FOUND;
        }
    }

    cap = 64 + (size_t) NGX_HTTP_TRACE_RING_SLOTS
                * (NGX_HTTP_TRACE_SLOT_MAX + 1);
    body = ngx_pnalloc(r->pool, cap);
    if (body == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    p = body; last = body + cap;

    ngx_shmtx_lock(&shctx->shpool->mutex);
    ngx_http_trace_expire_locked(shctx, ngx_time());
    sess = ngx_http_trace_session_find_locked(shctx, id);
    if (sess == NULL) {
        ngx_shmtx_unlock(&shctx->shpool->mutex);
        return NGX_HTTP_NOT_FOUND;
    }

    if (want_txn != 0) {
        /* Detail tier: locate the one transaction by per-session seq. */
        for (i = 0; i < shctx->count; i++) {
            idx = (shctx->head + NGX_HTTP_TRACE_RING_SLOTS - shctx->count + i)
                  % NGX_HTTP_TRACE_RING_SLOTS;
            slot = &shctx->ring[idx];
            if (slot->committed && slot->len && slot->session_id == id
                && slot->txn_seq == want_txn)
            {
                p = ngx_cpymem(p, slot->json, slot->len);
                ngx_shmtx_unlock(&shctx->shpool->mutex);
                return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                                (size_t) (p - body));
            }
        }
        ngx_shmtx_unlock(&shctx->shpool->mutex);
        return NGX_HTTP_NOT_FOUND;           /* unknown txn */
    }

    /* List tier: TransactionSummary[] (FR-API-5). */
    p = ngx_cpymem(p, "{\"transactions\":[", sizeof("{\"transactions\":[") - 1);
    for (i = 0, n = 0; i < shctx->count; i++) {
        idx = (shctx->head + NGX_HTTP_TRACE_RING_SLOTS - shctx->count + i)
              % NGX_HTTP_TRACE_RING_SLOTS;
        slot = &shctx->ring[idx];
        if (!slot->committed || slot->len == 0 || slot->session_id != id) {
            continue;
        }
        if (n++) { *p++ = ','; }
        p = ngx_http_trace_json_summary(p, last, slot);
    }
    ngx_shmtx_unlock(&shctx->shpool->mutex);

    p = ngx_cpymem(p, "]}", 2);
    return ngx_http_trace_send_json(r, NGX_HTTP_OK, body,
                                    (size_t) (p - body));
}

/*
 * Send a response body with an explicit status and content-type (M6 helper).
 * Copies nothing — `body` must live for the duration of the request (typically
 * a request-pool buffer). Handles the empty-body and HEAD cases.
 */
ngx_int_t
ngx_http_trace_send_text(ngx_http_request_t *r, ngx_uint_t status,
    ngx_str_t *ctype, u_char *body, size_t len)
{
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t   out;

    r->headers_out.status = status;
    r->headers_out.content_length_n = len;
    r->headers_out.content_type = *ctype;
    r->headers_out.content_type_len = ctype->len;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only || len == 0) {
        return rc;
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = body;
    b->last = body + len;
    b->memory = 1;
    b->last_buf = 1;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}

/* Send a JSON body with the given status (M6 helper). */
ngx_int_t
ngx_http_trace_send_json(ngx_http_request_t *r, ngx_uint_t status,
    u_char *body, size_t len)
{
    ngx_str_t  ctype = ngx_string("application/json");

    return ngx_http_trace_send_text(r, status, &ctype, body, len);
}

/*
 * Serialize the live ring into a request-pool buffer as
 * {"transactions":[...]} (optionally filtered to one session). Fills the buffer
 * under the mutex (copy-out then release, M5.5) and returns the byte length, or
 * NGX_ERROR on allocation failure. This is the legacy/default read tier.
 */
ngx_int_t
ngx_http_trace_dump_ring(ngx_http_request_t *r, ngx_http_trace_shctx_t *shctx,
    ngx_uint_t filtered, ngx_uint_t want_session, u_char **out_body)
{
    ngx_http_trace_slot_t  *slot;
    u_char                 *body, *p;
    size_t                  cap, len;
    ngx_uint_t              i, idx;

    cap = sizeof("{\"transactions\":[]}")
          + (size_t) NGX_HTTP_TRACE_RING_SLOTS * (NGX_HTTP_TRACE_SLOT_MAX + 1);

    body = ngx_pnalloc(r->pool, cap);
    if (body == NULL) {
        return NGX_ERROR;
    }

    p = ngx_cpymem(body, "{\"transactions\":[",
                   sizeof("{\"transactions\":[") - 1);

    for (i = 0, len = 0; i < shctx->count; i++) {
        idx = (shctx->head + NGX_HTTP_TRACE_RING_SLOTS - shctx->count + i)
              % NGX_HTTP_TRACE_RING_SLOTS;
        slot = &shctx->ring[idx];

        if (!slot->committed || slot->len == 0) {
            continue;
        }
        if (filtered && slot->session_id != want_session) {
            continue;
        }

        if (len++) {
            *p++ = ',';
        }
        p = ngx_cpymem(p, slot->json, slot->len);
    }

    p = ngx_cpymem(p, "]}", 2);
    *out_body = body;

    return (ngx_int_t) (p - body);
}

/*
 * FR-API-9 — `POST /__trace/import` body handler. Runs once the whole body has
 * arrived (nginx calls us; we never block waiting for it, per G8). Validates
 * that the payload looks like a session export and reports how many
 * transactions the offline viewer should expect, then finalizes.
 *
 * The check is deliberately shallow — a shape probe, not a parse. There is no
 * JSON parser in this module and adding one to consume untrusted input on the
 * control plane would be a poor trade: the viewer already parses the file with
 * the browser's own parser, so all we owe it is a clear accept/reject.
 */
static void
ngx_http_trace_import_body_handler(ngx_http_request_t *r)
{
    ngx_chain_t  *cl;
    ngx_buf_t    *b;
    u_char       *body, *p, *found;
    size_t        len, n;
    ngx_int_t     rc;
    ngx_uint_t    ntxn;

    len = 0;

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        body = (u_char *) "{\"error\":\"empty_body\"}";
        rc = ngx_http_trace_send_json(r, NGX_HTTP_BAD_REQUEST, body,
                                      ngx_strlen(body));
        ngx_http_finalize_request(r, rc);
        return;
    }

    /*
     * A body large enough to have spilled to disk is larger than any export we
     * produce, so reject it rather than read the file: the control plane must
     * not do blocking I/O on attacker-controlled size (G8, NFR-REL-1).
     */
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        if (cl->buf->in_file) {
            body = (u_char *) "{\"error\":\"body_too_large\"}";
            rc = ngx_http_trace_send_json(r,
                     NGX_HTTP_REQUEST_ENTITY_TOO_LARGE, body, ngx_strlen(body));
            ngx_http_finalize_request(r, rc);
            return;
        }
        len += (size_t) (cl->buf->last - cl->buf->pos);
    }

    /* Flatten the chain so the shape probe can scan one contiguous run. */
    body = ngx_pnalloc(r->pool, len + 1);
    if (body == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    p = body;
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        b = cl->buf;
        p = ngx_cpymem(p, b->pos, (size_t) (b->last - b->pos));
    }
    *p = '\0';

    /* Shape probe: must carry a "transactions" array. */
    found = ngx_strlcasestrn(body, body + len, (u_char *) "\"transactions\"",
                             sizeof("\"transactions\"") - 2);
    if (found == NULL) {
        p = (u_char *) "{\"error\":\"not_a_session_export\"}";
        rc = ngx_http_trace_send_json(r, NGX_HTTP_BAD_REQUEST, p,
                                      ngx_strlen(p));
        ngx_http_finalize_request(r, rc);
        return;
    }

    /* Count transactions by their "txn":"trace" markers (one per record). */
    ntxn = 0;
    p = body;
    n = len;
    for ( ;; ) {
        found = ngx_strlcasestrn(p, p + n, (u_char *) "\"txn\"",
                                 sizeof("\"txn\"") - 2);
        if (found == NULL) {
            break;
        }
        ntxn++;
        n -= (size_t) (found - p) + 1;
        p = found + 1;
    }

    p = ngx_pnalloc(r->pool, 96);
    if (p == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    n = (size_t) (ngx_snprintf(p, 96,
                      "{\"imported\":true,\"transactions\":%ui,\"bytes\":%uz}",
                      ntxn, len) - p);

    rc = ngx_http_trace_send_json(r, NGX_HTTP_OK, p, n);
    ngx_http_finalize_request(r, rc);
}


/*
 * Control endpoint content handler (M6.5). Routes on the sub-path relative to
 * the matched location prefix:
 *   - empty sub-path (exact-match location, legacy): dump the ring.
 *   - "sessions", "sessions/{id}[/transactions[/{txn}]|/export]": the M6 API.
 *   - "ui": the minimal SPA (M6.6).
 * 503 in inert mode (no trace_zone). trace_control gating is implicit: only
 * locations that declared the directive install this handler (FR-API-11).
 */
ngx_int_t
ngx_http_trace_control_handler(ngx_http_request_t *r)
{
    ngx_http_trace_main_conf_t  *mcf;
    ngx_http_trace_shctx_t      *shctx;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_int_t                    rc, blen;
    ngx_str_t                    arg, sub;
    u_char                      *body;
    ngx_uint_t                   want_session, filtered;

    mcf = ngx_http_get_module_main_conf(r, ngx_http_trace_module);
    if (mcf == NULL || mcf->shm_zone == NULL) {
        /* Inert mode: routable but the tracing plane is unavailable. */
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    shctx = mcf->shm_zone->data;
    if (shctx == NULL || shctx->ring == NULL) {
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    /* Sub-path = r->uri with the matched location prefix stripped. */
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    sub.len = 0;
    sub.data = (u_char *) "";
    if (clcf != NULL && r->uri.len >= clcf->name.len) {
        sub.data = r->uri.data + clcf->name.len;
        sub.len = r->uri.len - clcf->name.len;
        /* tolerate a leading '/' between prefix and sub-path */
        while (sub.len && sub.data[0] == '/') {
            sub.data++;
            sub.len--;
        }
    }

    /* Routed API / UI when a sub-path is present. */
    if (sub.len != 0) {
        return ngx_http_trace_api(r, mcf, shctx, &sub);
    }

    /* Legacy/default tier: dump the ring (GET/HEAD only). */
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    filtered = 0;
    want_session = 0;
    if (r->args.len
        && ngx_http_arg(r, (u_char *) "session", 7, &arg) == NGX_OK)
    {
        want_session = ngx_atoi(arg.data, arg.len);
        if (want_session == (ngx_uint_t) NGX_ERROR || want_session == 0) {
            return NGX_HTTP_NOT_FOUND;
        }
        filtered = 1;
    }

    ngx_shmtx_lock(&shctx->shpool->mutex);
    ngx_http_trace_expire_locked(shctx, ngx_time());
    if (filtered
        && ngx_http_trace_session_find_locked(shctx, want_session) == NULL)
    {
        ngx_shmtx_unlock(&shctx->shpool->mutex);
        return NGX_HTTP_NOT_FOUND;           /* unknown/expired id (AC-15) */
    }
    blen = ngx_http_trace_dump_ring(r, shctx, filtered, want_session, &body);
    ngx_shmtx_unlock(&shctx->shpool->mutex);

    if (blen == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return ngx_http_trace_send_json(r, NGX_HTTP_OK, body, (size_t) blen);
}


/* M9.3 (FR-API-10): export_session helper — reusable by collector modules.
 * Writes the JSON for session <sess> into a request-pool buffer set at
 * <*out_body>, returning the byte count.  Returns a negative value on
 * allocation failure.  The output is the same as GET /sessions/:sid. */
ngx_int_t
ngx_http_trace_export_session(ngx_http_request_t *r,
    ngx_http_trace_shctx_t *shctx, ngx_http_trace_session_t *sess,
    u_char **out_body)
{
    u_char  *body, *p;

    body = ngx_pnalloc(r->pool, NGX_HTTP_TRACE_API_SESSION_BUF);
    if (body == NULL) {
        return NGX_ERROR;
    }

    p = ngx_http_trace_json_session(body, body + NGX_HTTP_TRACE_API_SESSION_BUF, sess);
    if (p == NULL) {
        return NGX_ERROR;
    }

    *out_body = body;
    return (ngx_int_t) (p - body);
}
