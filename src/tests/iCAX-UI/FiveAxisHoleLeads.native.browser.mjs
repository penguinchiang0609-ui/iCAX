import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';

const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY),output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),source=resolve(process.env.ICAX_HOLE_LEAD_PROJECT);
const assets=resolve(process.env.ICAX_BROWSER_RUNTIME_ROOT||'src');mkdirSync(output,{recursive:true});
const native=spawn(resolve(runtime,process.env.ICAX_FIVE_AXIS_NATIVE_EXECUTABLE||'FiveAxisProcessAddTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
let sequence=0,browser,page;const waiting=new Map(),log=[];
createInterface({input:native.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){log.push(line);return;}const response=JSON.parse(line.slice(12)),wait=waiting.get(response.id);if(!wait)return;waiting.delete(response.id);clearTimeout(wait.timer);response.ok?wait.resolve(response.result):wait.reject(Error(response.error));});
native.stderr.on('data',data=>log.push(String(data)));
native.on('exit',code=>{for(const wait of waiting.values()){clearTimeout(wait.timer);wait.reject(Error('Native exited '+code));}waiting.clear();});
const rpc=message=>new Promise((resolve,reject)=>{const id=++sequence,timer=setTimeout(()=>{waiting.delete(id);reject(Error('Native timeout '+message.action));},120000);waiting.set(id,{resolve,reject,timer});const wire=message.action==='invoke'?{...message,payload:undefined,payloadText:serializeVariantText(message.payload||{})}:message;native.stdin.write(JSON.stringify({id,...wire})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const report={passed:false,native:true,browser:true,standaloneCef:false,source,holes:[],cases:[],errors:[]};
try{
  await rpc({action:'open-project',path:source});const info=await rpc({action:'info'});
  browser=await chromium.launch({channel:'msedge',headless:true});page=await browser.newPage({viewport:{width:1600,height:1000}});page.setDefaultTimeout(45000);
  page.on('pageerror',error=>report.errors.push(error.message));await page.exposeFunction('fiveNativeRpc',rpc);
  await page.route('**/*',route=>{const name=decodeURIComponent(new URL(route.request().url()).pathname);if(name==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"><body style="margin:0"><div id="app" style="height:100vh"></div></body></html>'});if(!name.startsWith('/src/'))return route.abort();try{return route.fulfill({contentType:name.endsWith('.css')?'text/css':name.endsWith('.mjs')||name.endsWith('.js')?'text/javascript':'application/octet-stream',body:readFileSync(resolve(assets,name.slice(5)))});}catch{return route.abort();}});
  await page.goto('http://localhost/');
  await page.evaluate(async info=>{
    const {serializeVariantText}=await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs'),entry=await import('/src/apps/five-axis-cam/webpage/entry.mjs');
    const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs'),{getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const {PDOStore}=await import('/src/iCAX-UI/SDK/PDO/pdoStore.mjs');
    const view=getProjectView(info.projectId),f=window.fixture={entry,view,logs:[]};
    const events=new EventTarget();
    const sceneProxy={sceneId:info.sceneId,state:{sceneId:info.sceneId},pdoStore:new PDOStore(),
      subscribe(name,handler){const receive=event=>handler(event.detail);events.addEventListener(String(name),receive);return()=>events.removeEventListener(String(name),receive);},
      pdo:{enabled:true,async withReadDescriptor(descriptor,reader){const result=await fiveNativeRpc({action:'pdo-read',slotId:String(descriptor.id),arenaName:descriptor.arenaName});return reader(Uint8Array.from(result.bytes).buffer,{dataVersion:result.dataVersion});}},
      invoke:(method,payload={})=>fiveNativeRpc({action:'invoke',scope:'scene',method,payload}),async getState(){return this.state;}};
    const resource=async(url,options,head)=>{const version=new Headers(options?.headers).get('ICAX-Resource-Version')??String(options?.version??'');const result=await fiveNativeRpc({action:'resource',url,version,head,encoding:'base64'});return new Response(head?null:Uint8Array.from(atob(result.base64),c=>c.charCodeAt(0)),{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':result.version}});};
    sceneProxy.resources={get:(url,options)=>resource(url,options,false),head:(url,options)=>resource(url,options,true)};sceneProxy.views=new ViewClient(sceneProxy);
    const context=f.context={mount:document.querySelector('#app'),sceneProxy,project:{projectId:info.projectId,projectName:'原始三孔引线验收'},activeRibbonTabId:'toolpath',isCurrentProject:()=>true,
      productProxy:{invoke:(method,payload={})=>fiveNativeRpc({action:'invoke',scope:'product',method,payload})},actions:{refreshProductRibbon(){},appendProjectLog(...args){f.logs.push(args);},async selectRibbonTab(id){context.activeRibbonTabId=id;entry.mountProject(context);}}};
    entry.mountProject(context);
  },info);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!!fixture.view.scene?.domain),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const field=name=>page.locator(`[data-five-lead-field="${name}"]`),read=()=>rpc({action:'read-state'});
  const ready=()=>page.waitForFunction(()=>fixture.view.fiveLeads?.loaded&&!fixture.view.fiveLeads.previewBusy);
  const save=async()=>{await page.locator('[data-five-command="five.leads.confirm"]').click();await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisLeadsOpen);assert.equal(await page.evaluate(()=>fixture.view.fiveLeads?.error||''),'');};
  await idle();const initial=await read(),holes=initial.intentToolpaths.filter(c=>c.boundaryRole==='Opening');assert.equal(holes.length,3);
  const settings=async kind=>{
    if(await page.evaluate(()=>fixture.view.fiveAxisUI?.tools.toolpath!=='leads'))await command('five.tool.leads');
    await command('five.leads.settings');await ready();
    for(const role of ['entry','exit']){await field(role+'.kind').selectOption(kind);await field(role+'.length').fill('1');if(kind==='Arc')await field(role+'.radius').fill('1');else await field(role+'.angle').fill('0');}
    await save();
  };
  await page.locator(`[data-five-intent="${holes[0].id}"]`).click();await idle();await settings('Line');
  for(const hole of holes){console.log('Hole '+hole.number);
    const none={kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0},nominal=await invoke('CamProcess.PreviewLeads',{expected:(await read()).expected,intentIds:[hole.id],patch:{leadIn:none,leadOut:none}}),points=nominal.items[0].cutPoints;
    const center=[0,1,2].map(i=>(Math.min(...points.map(p=>p[i]))+Math.max(...points.map(p=>p[i])))/2),radius=Math.hypot(points[0][0]-center[0],points[0][1]-center[1]);for(const p of points)assert.ok(Math.abs(Math.hypot(p[0]-center[0],p[1]-center[1])-radius)<1e-5);
    await page.locator(`[data-five-intent="${hole.id}"]`).click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
    const draft=(await invoke('CamProcess.PreviewLeads',{expected:(await read()).expected,intentIds:[hole.id]})).items[0];assert.equal(draft.leads.length,2);for(const lead of draft.leads){assert.equal(lead.kind,'Line');assert.equal(lead.lengthMm,1);for(const p of lead.points)assert.ok(Math.hypot(p[0]-center[0],p[1]-center[1])<=radius+1e-6);}
    await page.screenshot({path:resolve(output,'hole-'+hole.number+'.png')});report.holes.push({id:hole.id,number:hole.number,center,radius,inside:true});
  }
  await settings('Arc');for(const hole of holes){await page.locator(`[data-five-intent="${hole.id}"]`).click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');}
  const arcs=(await invoke('CamProcess.PreviewLeads',{expected:(await read()).expected,intentIds:holes.map(h=>h.id)})).items;for(const item of arcs){const h=report.holes.find(h=>h.id===item.intentId);for(const lead of item.leads){assert.equal(lead.kind,'Arc');assert.equal(lead.lengthMm,1);for(const p of lead.points)assert.ok(Math.hypot(p[0]-h.center[0],p[1]-h.center[1])<=h.radius+1e-6);}}
  await page.screenshot({path:resolve(output,'three-hole-arcs.png')});let state=await read();const policies=await invoke('CamProcess.Read',{expected:state.expected,intentIds:holes.map(h=>h.id)});
  assert.deepEqual(state.workpieces,initial.workpieces);assert.deepEqual(state.placements,initial.placements);assert.deepEqual(state.fixtureUnits,initial.fixtureUnits);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));state=await rpc({action:'save-reopen'});
  const after=await invoke('CamProcess.Read',{expected:state.expected,intentIds:holes.map(h=>h.id)});assert.deepEqual(after.items.map(i=>i.assignment),policies.items.map(i=>i.assignment));
  const reopened=await invoke('CamProcess.PreviewLeads',{expected:state.expected,intentIds:holes.map(h=>h.id)});assert.equal(reopened.items.length,3);for(const item of reopened.items)assert.equal(item.leads.filter(l=>l.kind==='Arc'&&l.lengthMm===1).length,2);
  const project=resolve(output,'三孔孔内引线验收.i5cam');await rpc({action:'export-project',path:project});report.project=project;
  report.cases=['Current lead tool: configure and click each original hole to apply line leads','Continuous arc application stays inside all three holes','Workpiece, placements and fixtures unchanged','Native save and reopen preserve all six arc leads'];assert.deepEqual(report.errors,[]);report.passed=true;console.log(JSON.stringify({passed:true,holes:3,cases:report.cases}));
}catch(error){report.error=error.stack;if(page){report.ui=await page.evaluate(()=>({error:fixture.view.error,leadError:fixture.view.fiveLeads?.error})).catch(()=>null);await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'native.log'),log.join('\n'));await browser?.close();await rpc({action:'exit'}).catch(()=>{});native.stdin.end();}
