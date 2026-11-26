import http from 'node:http';
import {readFile,writeFile,access} from 'node:fs/promises';
import {timingSafeEqual} from 'node:crypto';
import {Client} from '@modelcontextprotocol/sdk/client/index.js';
import {StreamableHTTPClientTransport} from '@modelcontextprotocol/sdk/client/streamableHttp.js';
// The placeholder from .env.example must never unlock the editor.
const WEAK_TOKENS=new Set(['change-this-demo-password']);
const endpoints={direct:process.env.DIRECT_MCP_URL,gateway:process.env.MCP_URL};
const events=[];
function log(e){events.unshift({at:new Date().toISOString(),...e});events.splice(100);}
function errorText(e){return String(e?.message||e);}
async function withClient(mode,agent,apiKey,fn){
 const client=new Client({name:agent,version:'1.0.0'});
 const transport=new StreamableHTTPClientTransport(new URL(endpoints[mode]),{requestInit:{headers:{'X-Demo-Agent':agent,...(mode==='gateway'&&apiKey?{'X-API-Key':apiKey}:{})}}});
 try{await client.connect(transport);return await fn(client);}finally{await client.close().catch(()=>{});}
}
async function execute({mode,agent,apiKey,action,tool,args,count}){
 if(action==='burst'){
  // Exactly n HTTP POSTs: no SDK client initialization or extra MCP requests.
  const n=Math.min(60,Math.max(1,Number(count)||25));
  const target=endpoints[mode];
  const burstTool='list_pods';
  const results=await Promise.all(Array.from({length:n},async(_,i)=>{
   try{
    const response=await fetch(target,{
     method:'POST',
     headers:{'Content-Type':'application/json','Accept':'application/json, text/event-stream',
      'X-Demo-Agent':agent,...(mode==='gateway'&&apiKey?{'X-API-Key':apiKey}:{})},
     body:JSON.stringify({jsonrpc:'2.0',id:i+1,method:'tools/call',params:{name:burstTool,arguments:{}}}),
     signal:AbortSignal.timeout(10000)
    });
    // Consume the response so connection resources are released.
    await response.arrayBuffer();
    return {status:response.status};
   }catch(e){return {error:errorText(e)};}
  }));
  const statuses={};for(const r of results)if(r.status)statuses[r.status]=(statuses[r.status]||0)+1;
  const admitted=results.filter(r=>r.status>=200&&r.status<300).length;
  const rateLimited=statuses[429]||0;
  const otherResponses=results.filter(r=>r.status&&!(r.status>=200&&r.status<300)&&r.status!==429).length;
  const networkErrors=results.filter(r=>r.error).length;
  return {requests:n,tool:burstTool,admitted,rateLimited,otherResponses,networkErrors,
   httpStatuses:statuses,networkErrorSamples:results.filter(r=>r.error).slice(0,3).map(r=>r.error),
   note:'Exactly one read-only list_pods HTTP POST per attempt. HTTP 2xx means admitted, not necessarily MCP tool success.'};
 }
 return withClient(mode,agent,apiKey,async client=>{
  if(action==='discover')return {tools:(await client.listTools()).tools.map(t=>({name:t.name,description:t.description,inputSchema:t.inputSchema}))};
  if(action==='call'){
   const available=(await client.listTools()).tools.map(t=>t.name);
   if(!available.includes(tool))throw Error(`Tool not advertised: ${tool}`);
   const result=await client.callTool({name:tool,arguments:args||{}});
   return {tool,isError:!!result.isError,result};
  }
  throw Error('Unknown action');
 });
}
const server=http.createServer(async(req,res)=>{
 const send=(status,data)=>{res.writeHead(status,{'content-type':'application/json','cache-control':'no-store'});res.end(JSON.stringify(data));};
 if(req.url==='/api/nginx-config'&&req.method==='GET'){
  try{
   // Live config as currently loaded by NGINX (written by editor-supervisor.sh).
   const config=await readFile('/demo-control/live.conf','utf8');
   res.writeHead(200,{'content-type':'application/json','cache-control':'no-store'});
   return res.end(JSON.stringify({file:'nginx/nginx.conf',config}));
  }catch(e){return send(503,{error:'NGINX configuration is unavailable: '+errorText(e)});}
 }
 if(req.url==='/api/nginx-editor'&&req.method==='POST'){
  const secret=process.env.NGINX_EDITOR_TOKEN||'';
  if(secret.length<5||WEAK_TOKENS.has(secret))return send(503,{error:'Editor disabled. Set a unique NGINX_EDITOR_TOKEN (5+ characters, not the .env.example value) in .env and recreate ui.'});
  const received=String(req.headers['x-editor-token']||'');
  const a=Buffer.from(secret),b=Buffer.from(received);
  if(a.length!==b.length||!timingSafeEqual(a,b))return send(403,{error:'Incorrect editor password'});
  let raw='';for await(const chunk of req){raw+=chunk;if(raw.length>50000)return send(413,{error:'Configuration too large'});}
  let payload;try{payload=JSON.parse(raw);}catch{return send(400,{error:'Invalid JSON'});}
  if(!['apply','restore'].includes(payload.action))return send(400,{error:'Invalid editor action'});
  try{
   await access('/demo-control/original');
   try{await access('/demo-control/request');return send(409,{error:'Another configuration change is pending'});}catch{}
   if(payload.action==='apply'){
    if(typeof payload.config!=='string'||!payload.config.trim()||payload.config.length>40000)return send(400,{error:'Invalid configuration'});
    await writeFile('/demo-control/candidate',payload.config,'utf8');
   }
   await writeFile('/demo-control/status','pending: validating configuration');
   await writeFile('/demo-control/test-log','');
   await writeFile('/demo-control/request',payload.action);
   for(let i=0;i<40;i++){
    await new Promise(resolve=>setTimeout(resolve,250));
    const status=(await readFile('/demo-control/status','utf8')).trim();
    if(!status.startsWith('pending:')){
     const detail=(await readFile('/demo-control/test-log','utf8')).slice(0,5000);
     return send(status.startsWith('ok:')?200:422,{status,detail});
    }
   }
   return send(504,{error:'Timed out waiting for NGINX validation; check nginx logs'});
  }catch(e){return send(503,{error:'Configuration controller unavailable: '+errorText(e)});}
 }
 if(req.url==='/api/events')return send(200,events);
 if(req.url==='/api/health'){
  let gateway=false;try{gateway=(await fetch(process.env.GATEWAY_HEALTH_URL,{signal:AbortSignal.timeout(2000)})).ok;}catch{}
  return send(200,{gateway,directEndpoint:endpoints.direct,gatewayEndpoint:endpoints.gateway,
   grafanaPort:process.env.GRAFANA_PORT||'3002',prometheusPort:process.env.PROMETHEUS_PORT||'9090'});
 }
 if(req.url==='/api/run'&&req.method==='POST'){
  let raw='';for await(const part of req){raw+=part;if(raw.length>20000)return send(413,{error:'Payload too large'});}
  let p;try{p=JSON.parse(raw);}catch{return send(400,{error:'Invalid JSON'});}
  if(!['direct','gateway'].includes(p.mode)||!['discover','call','burst'].includes(p.action))return send(400,{error:'Valid mode and action required'});
  const agent=String(p.agent||'demo-agent').slice(0,60),start=Date.now();
  const apiKey=p.mode==='gateway'&&typeof p.apiKey==='string'?p.apiKey.slice(0,256):'';
  const meta={mode:p.mode,endpoint:endpoints[p.mode],route:p.mode==='direct'?'ui → mcp:3001':'ui → nginx:8080 → mcp:3001',gatewayUsed:p.mode==='gateway',action:p.action,tool:p.tool||null,agent};
  try{const result=await execute({...p,agent,apiKey});const entry={...meta,success:p.action==='burst' ? result.admitted>0 && result.otherResponses===0 && result.networkErrors===0 : !result.isError,result,ms:Date.now()-start};log(entry);return send(200,entry);}
  catch(e){const entry={...meta,success:false,error:errorText(e),ms:Date.now()-start};log(entry);return send(200,entry);}
 }
 if(req.url==='/'||req.url==='/index.html'){
  const html=await readFile(new URL('./public/index.html',import.meta.url));res.writeHead(200,{'content-type':'text/html; charset=utf-8'});return res.end(html);
 }
 res.writeHead(404);res.end('Not found');
});
server.listen(3000,'0.0.0.0');
