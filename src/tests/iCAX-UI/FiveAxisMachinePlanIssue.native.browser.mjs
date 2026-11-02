import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {copyFileSync,existsSync,mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {browserAssetPath} from './browserPackageRuntime.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/machine-plan-issue-20261101/native'),runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'output/tests/order-pillar-recovery-20261101/runtime');mkdirSync(output,{recursive:true});
const source=resolve(process.env.ICAX_PLAN_ISSUE_PROJECT||'output/acceptance/cam-pillar-extraction-20261101/workflow.i5cam'),project=resolve(output,'pillar-copy.i5cam');assert.notEqual(source,project);copyFileSync(source,project);if(existsSync(source+'.log'))copyFileSync(source+'.log',project+'.log');
const child=spawn(resolve(runtime,'FiveAxisFixturePillarTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
let serial=0,browser,page;const waiting=new Map(),logs=[],calls=[],report={passed:false,native:true,standaloneCef:false,source,project};
const fail=e=>{for(const p of waiting.values()){clearTimeout(p.timer);p.reject(e);}waiting.clear();};child.on('error',fail);child.on('exit',code=>fail(Error('Bridge exited '+code)));child.stderr.on('data',b=>logs.push(String(b)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__'))return logs.push(line);const r=JSON.parse(line.slice(12)),p=waiting.get(r.id);if(!p)return;waiting.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=m=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{waiting.delete(id);reject(Error('Timeout '+(m.method||m.action)));},180000);waiting.set(id,{resolve,reject,timer});calls.push({action:m.action,method:m.method});child.stdin.write(JSON.stringify({id,...m,...(m.action==='invoke'?{payload:undefined,payloadText:m.payloadText??serializeVariantText(m.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const replay=process.env.ICAX_PLAN_ISSUE_ATTEMPT?JSON.parse(readFileSync(process.env.ICAX_PLAN_ISSUE_ATTEMPT,'utf8')).report:null;
try{
 await rpc({action:'open-project',path:project,replayJournal:!replay});const before=await invoke('CamScene.ReadState'),setup=before.setups.find(s=>s.id===before.activeSetupId);report.cutCount=before.intentToolpaths.length;report.configuration={machineSetup:setup.machineSetup,headModel:setup.headModel};
 writeFileSync(resolve(output,'before.json'),JSON.stringify(before,null,2));console.log(JSON.stringify({stage:'opened',cuts:report.cutCount,binding:setup.machineSetup?.machineId,independent:!!setup.headModel?.motion,status:setup.generatedGeometry?.status}));
 if(process.argv.includes('--inspect-only')){report.passed=true;report.inspectOnly=true;}
 else{
  if(replay){const snapshot=await invoke('CamSimulation.ReadMachine',{setupId:setup.id});assert.equal(snapshot.modelKey,replay.modelKey,'Captured native witness belongs to a different scene model');report.verifiedNativeReplay=true;}
  browser=await chromium.launch({channel:'msedge',headless:true});page=await browser.newPage({viewport:{width:1440,height:1000}});const pageErrors=[];page.on('pageerror',e=>pageErrors.push(e.message));await page.exposeFunction('nativeRpc',rpc);
  await page.route('https://native-plan-issue.test/**',route=>{const p=decodeURIComponent(new URL(route.request().url()).pathname);if(p==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"><body style="margin:0"><div id="app" style="height:100vh"></div>'});if(!p.startsWith('/src/'))return route.abort();try{return route.fulfill({contentType:p.endsWith('.css')?'text/css':'text/javascript',body:readFileSync(browserAssetPath(p.slice(5)))});}catch{return route.abort();}});
  await page.goto('https://native-plan-issue.test/');const info=await rpc({action:'info'});
  await page.evaluate(async({info,project})=>{
   const {serializeVariantText}=await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs'),entry=await import('/src/apps/five-axis-cam/webpage/entry.mjs'),{ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs'),{getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
   const view=getProjectView(info.projectId),f=window.fixture={entry,view,logs:[],saves:0};
   const {PDOStore}=await import('/src/iCAX-UI/SDK/PDO/pdoStore.mjs'),events=new EventTarget();
   const sceneProxy={sceneId:info.sceneId,state:{sceneId:info.sceneId},pdo:{enabled:true,async withReadDescriptor(descriptor,reader){const r=await nativeRpc({action:'pdo-read',slotId:String(descriptor.id),arenaName:descriptor.arenaName});return reader(Uint8Array.from(r.bytes).buffer,{dataVersion:r.dataVersion});}},invoke:(method,payload={})=>nativeRpc({action:'invoke',scope:'scene',method,payloadText:serializeVariantText(payload)}),async getState(){return this.state;}};
   // The bridge publishes View resources directly, without collider streams;
   // task descriptors still read the real native PDO arena above.
   sceneProxy.pdoStore=new PDOStore();sceneProxy.subscribe=(name,handler)=>{const receive=e=>handler(e.detail);events.addEventListener(String(name),receive);return()=>events.removeEventListener(String(name),receive);};
   const resource=async(url,options,head)=>{const version=new Headers(options?.headers).get('ICAX-Resource-Version')??String(options?.version??''),r=await nativeRpc({action:'resource',url,version,head,encoding:'base64'});return new Response(head?null:Uint8Array.from(atob(r.base64||''),c=>c.charCodeAt(0)),{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':r.version}});};
   sceneProxy.resources={get:(u,o)=>resource(u,o,false),head:(u,o)=>resource(u,o,true)};sceneProxy.views=new ViewClient(sceneProxy);
   const context=f.context={mount:document.querySelector('#app'),sceneProxy,project:{projectId:info.projectId,projectName:'立柱加强板 · 运动问题定位',projectPath:project},projectProxy:{state:{projectPath:project},async autosave(){f.saves++;return nativeRpc({action:'export-project',path:project});}},activeRibbonTabId:'toolpath',appProxy:{bridge:{}},productProxy:{invoke:(method,payload={})=>nativeRpc({action:'invoke',scope:'product',method,payloadText:serializeVariantText(payload)})},isCurrentProject:()=>true,actions:{refreshProductRibbon(){},refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){},appendProjectLog(...args){f.logs.push(args);},async selectRibbonTab(id){context.activeRibbonTabId=id;entry.mountProject(context);}}};entry.mountProject(context);
  },{info,project});
  await page.waitForFunction(()=>fixture.view.scene?.intentToolpaths?.length===21&&!fixture.view.pending,null,{timeout:180000});
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.simulation'));
  if(replay)await page.evaluate(async body=>{const I=await import('/src/apps/five-axis-cam/webpage/machinePlanIssue.mjs');fixture.view.fiveMachine.report=body;fixture.view.fiveMachine.snapshot=body;I.presentMachinePlanIssue(fixture.view,body);window.planning=fixture.entry.handleRibbonCommand(fixture.context,'five.plan-issue.open');},replay);
  else await page.evaluate(()=>{window.planning=fixture.entry.handleRibbonCommand(fixture.context,'five.machine.build');});
  console.log(JSON.stringify({stage:'planning'}));
  const deadline=Date.now()+720000;while(Date.now()<deadline){const s=await page.evaluate(()=>({busy:fixture.view.fiveMachine?.busy,planning:fixture.view.fiveMachine?.planning,error:fixture.view.fiveMachine?.error,issue:!!fixture.view.fiveMachinePlanIssue,progress:fixture.view.fiveMachine?.planTask?.machineProgress}));if(!s.busy&&!s.planning)break;console.log(JSON.stringify({stage:'progress',...s}));await new Promise(r=>setTimeout(r,20000));}
  await page.evaluate(()=>planning);const state=await page.evaluate(()=>({error:fixture.view.fiveMachine?.error,report:fixture.view.fiveMachine?.report,issue:!!fixture.view.fiveMachinePlanIssue,logs:fixture.logs}));writeFileSync(resolve(output,'native-attempt.json'),JSON.stringify(state,null,2));
  assert.equal(state.report?.playbackAllowed,false,JSON.stringify({error:state.error,issue:state.issue}));assert.equal(state.issue,true);await page.locator('.five-plan-issue').waitFor();report.text=await page.locator('.five-plan-issue').innerText();assert.match(report.text,/空移|切割|初始姿态/);
  await page.screenshot({path:resolve(output,'native-failure-dialog.png')});
  const inspect=page.locator('[data-five-command="five.plan-issue.inspect"]');if(await inspect.count()){
   await inspect.click();await page.waitForFunction(()=>fixture.view.fiveMachine.inspect&&fixture.view.fiveMachine.headGroup,null,{timeout:60000});
   report.inspection=await page.evaluate(async()=>{const s=fixture.view.fiveMachine,r=s.report,w=r.rejectedMotion||r,{machineForward}=await import('/src/apps/five-axis-cam/webpage/machineKinematics.mjs'),world=machineForward(r,w.collisionJoints),group=s.headGroup,indices=r.headNodes;return {allowed:r.playbackAllowed,playing:!!s.playing,parts:group.userData.assembly?group.children.map((p,i)=>({actual:p.matrix.elements,expected:world[indices[i]].elements})):[{actual:group.matrix.elements,expected:world[indices[0]].elements}]};});
   assert.equal(report.inspection.allowed,false);assert.equal(report.inspection.playing,false);for(const p of report.inspection.parts)assert.deepEqual(p.actual,p.expected,'Inspection must display exact native rejected joints, including asynchronous head loading');await page.screenshot({path:resolve(output,'native-collision-pose.png')});
   await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.plan-issue.open'));
  }
  const locate=page.locator('[data-five-command="five.plan-issue.locate"]');if(await locate.count()){
   const expected=JSON.stringify((await invoke('CamScene.ReadState')).expected);await locate.click();await page.waitForFunction(target=>!!fixture.view.fiveMachinePlanIssue?.marker&&fixture.view.fiveCamPage===(target?.role==='Rapid'?'rapid':'cutting')&&(target?.role!=='Rapid'||fixture.view.fiveRapid?.id?.split('/')[1]===target.intentId),state.report.failedTarget);
   report.location=await page.evaluate(()=>({page:fixture.view.fiveCamPage,rapid:fixture.view.fiveRapid?.id,marker:fixture.view.fiveMachinePlanIssue?.marker?.position.toArray(),camera:fixture.view.viewport.getCameraState()}));
   if(state.report.failedTarget?.role==='Rapid')assert.equal(report.location.rapid?.split('/')[1],state.report.failedTarget.intentId,'Locate after inspecting must select the reported connection, not the first connection');
   assert.equal(JSON.stringify((await invoke('CamScene.ReadState')).expected),expected,'Diagnostic navigation mutated the native project');await page.screenshot({path:resolve(output,'native-located.png')});
   await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.plan-issue.open'));
   const edit=page.locator('[data-five-command="five.plan-issue.edit"]');if(await edit.count()){await edit.click();await page.waitForFunction(()=>fixture.view.fiveRapid?.pathEdit||fixture.view.fiveAxisPointEdit);report.edit=await page.evaluate(()=>({rapid:!!fixture.view.fiveRapid?.pathEdit,cut:!!fixture.view.fiveAxisPointEdit,error:fixture.view.error}));assert.equal(report.edit.error||'','');await page.screenshot({path:resolve(output,'native-editor.png')});}
  }
  assert.deepEqual(pageErrors,[]);report.passed=true;report.nativeReportStatus=state.report.status;report.collisionStatus=state.report.collisionStatus;report.failedTarget=state.report.failedTarget;console.log(JSON.stringify({passed:true,output,location:report.location,edit:report.edit}));
 }
}catch(e){report.error=e.stack;await page?.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw e;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'calls.json'),JSON.stringify(calls,null,2));writeFileSync(resolve(output,'native.log'),logs.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}child.stdin.end();}
