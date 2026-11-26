import express from 'express';
import {McpServer} from '@modelcontextprotocol/sdk/server/mcp.js';
import {StreamableHTTPServerTransport} from '@modelcontextprotocol/sdk/server/streamableHttp.js';
import {z} from 'zod';

const app=express();app.use(express.json({limit:'1mb'}));
const pods=new Map([
 ['api-7d9c', {name:'api-7d9c',namespace:'production',image:'nginx:1.27',replicas:3,status:'Running'}],
 ['worker-4f2a',{name:'worker-4f2a',namespace:'production',image:'node:22',replicas:2,status:'Running'}],
 ['cache-9b1e',{name:'cache-9b1e',namespace:'staging',image:'redis:7',replicas:1,status:'Running'}]
]);
const records=new Map([['customer-101',{id:'customer-101',name:'Acme Corp',tier:'enterprise'}],['customer-102',{id:'customer-102',name:'Northwind',tier:'standard'}]]);
const text=x=>({content:[{type:'text',text:typeof x==='string'?x:JSON.stringify(x,null,2)}]});
const failure=message=>({isError:true,content:[{type:'text',text:message}]});
function makeServer(){
 const s=new McpServer({name:'mission-control-operations',version:'1.0.0'});
 s.tool('list_pods','List Kubernetes-style pods in the demo cluster (simulated; no real cluster is modified).', {namespace:z.string().optional()},async({namespace})=>text([...pods.values()].filter(p=>!namespace||p.namespace===namespace)));
 s.tool('get_pod','Read one pod by name.',{name:z.string()},async({name})=>pods.has(name)?text(pods.get(name)):failure('Pod not found: '+name));
 s.tool('create_pod','Create a simulated pod.',{name:z.string(),namespace:z.string().default('staging'),image:z.string().default('nginx:latest')},async({name,namespace,image})=>{if(pods.has(name))return failure('Pod already exists');const p={name,namespace,image,replicas:1,status:'Running'};pods.set(name,p);return text({created:p});});
 s.tool('update_pod','Update the image of a simulated pod.',{name:z.string(),image:z.string()},async({name,image})=>{const p=pods.get(name);if(!p)return failure('Pod not found');p.image=image;return text({updated:p});});
 s.tool('delete_pod','DESTRUCTIVE: delete a simulated pod.',{name:z.string()},async({name})=>{const p=pods.get(name);if(!p)return failure('Pod not found');pods.delete(name);return text({deleted:p});});
 s.tool('list_records','List sample customer records.',{},async()=>text([...records.values()]));
 s.tool('read_record','Read a customer record.',{id:z.string()},async({id})=>records.has(id)?text(records.get(id)):failure('Record not found'));
 s.tool('create_record','Create a sample customer record.',{id:z.string(),name:z.string()},async({id,name})=>{if(records.has(id))return failure('Record already exists');const r={id,name,tier:'standard'};records.set(id,r);return text({created:r});});
 s.tool('update_record','Update a sample customer record.',{id:z.string(),name:z.string()},async({id,name})=>{const r=records.get(id);if(!r)return failure('Record not found');r.name=name;return text({updated:r});});
 s.tool('delete_record','DESTRUCTIVE: delete a sample customer record.',{id:z.string()},async({id})=>{const r=records.get(id);if(!r)return failure('Record not found');records.delete(id);return text({deleted:r});});
 s.tool('echo','Echo text back.',{message:z.string()},async({message})=>text(message));
 s.tool('slow_operation','Wait 1.5 seconds to demonstrate latency.',{},async()=>{await new Promise(r=>setTimeout(r,1500));return text('Completed after 1.5 seconds');});
 s.tool('fail_operation','Return an MCP application error.',{},async()=>failure('Simulated backend operation failure'));
 return s;
}
app.post('/mcp',async(req,res)=>{const s=makeServer();const transport=new StreamableHTTPServerTransport({sessionIdGenerator:undefined,enableJsonResponse:true});try{await s.connect(transport);await transport.handleRequest(req,res,req.body);}catch(e){console.error(e);if(!res.headersSent)res.status(500).json({error:'MCP server failure'});}finally{res.on('close',()=>{transport.close().catch(()=>{});s.close().catch(()=>{});});}});
app.get('/health',(_req,res)=>res.json({ok:true,pods:pods.size,records:records.size}));
app.get('/mcp',(_req,res)=>res.status(405).end());
app.delete('/mcp',(_req,res)=>res.status(405).end());
app.listen(3001,'0.0.0.0',()=>console.log('Real MCP SDK server listening on :3001'));
