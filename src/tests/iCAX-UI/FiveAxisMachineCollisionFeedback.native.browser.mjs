import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {existsSync,mkdirSync,readFileSync,statSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {browserAssetPath} from './browserPackageRuntime.mjs';

const output=resolve(process.env.MACHINE_COLLISION_FEEDBACK_OUTPUT||'output/work/machine-kinematics/machine-collision-feedback-20261025/native-browser');
const source=resolve(process.env.MACHINE_COLLISION_SOURCE||'output/work/machine-kinematics/closest-ik-cef-20261025/closest-two-machines.i5cam');
const capture=JSON.parse(readFileSync(process.env.MACHINE_COLLISION_CAPTURE||'output/work/machine-kinematics/closest-ik-cef-20261025/collision-live-state.json','utf8'));
const nativeDirectory=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'output/work/machine-kinematics/machine-offset-replan-product');
const runtime=resolve(process.env.ICAX_FIVE_AXIS_RUNTIME_DIRECTORY||'D:/penguinchinage0609/iCAX/output/tests/split-chain-machines-20261024/runtime');
const executable=resolve(nativeDirectory,process.env.ICAX_FIVE_AXIS_NATIVE_EXECUTABLE||'FiveAxisMachineOffsetReplanTest.exe');
mkdirSync(output,{recursive:true});
const project=resolve(output,'collision-editor-copy.i5cam');assert.notEqual(source,project);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
// The user scene remains open elsewhere. Copy one immutable journal prefix,
// dropping a possibly unfinished final line, and never open/save its source.
const sourceStat=statSync(source),archive=readFileSync(source),journal=existsSync(source+'.log')?readFileSync(source+'.log'):Buffer.alloc(0);
assert.equal(statSync(source).mtimeMs,sourceStat.mtimeMs,'The archive changed while capturing the test copy');
writeFileSync(project,archive);if(journal.length)writeFileSync(project+'.log',journal.subarray(0,journal.lastIndexOf(10)+1));
writeFileSync(resolve(output,'source-copy.json'),JSON.stringify({source,project,archiveSha256:hash(archive),sourceArchiveSha256After:hash(readFileSync(source)),journalPrefixBytes:journal.length,readOnlySource:true},null,2));

const environment={...process.env,Path:runtime+';'+(process.env.Path||process.env.PATH||'')};delete environment.PATH;
const native=spawn(executable,['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:nativeDirectory,windowsHide:true,env:environment,stdio:['pipe','pipe','pipe']});
let sequence=0,browser,page;const waiting=new Map(),nativeLog=[],calls=[],pageErrors=[],report={pass:false,headless:true,standaloneCef:false,nativeTransport:'production DLL stdin SDO/View/resource bridge; no visible native application',binaryHashes:[executable,resolve(nativeDirectory,'Machine.dll'),resolve(nativeDirectory,'FiveAxisCAM.dll')].map(path=>({path,sha256:hash(readFileSync(path))}))};
const failAll=error=>{for(const p of waiting.values()){clearTimeout(p.timer);p.reject(error);}waiting.clear();};
native.on('error',failAll);native.on('exit',code=>failAll(Error('Native bridge exited '+code)));native.stderr.on('data',bytes=>nativeLog.push(String(bytes)));
createInterface({input:native.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){nativeLog.push(line);return;}const reply=JSON.parse(line.slice(12)),pending=waiting.get(reply.id);if(!pending)return;waiting.delete(reply.id);clearTimeout(pending.timer);reply.ok?pending.resolve(reply.result):pending.reject(Error(reply.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++sequence,timer=setTimeout(()=>{waiting.delete(id);reject(Error('Native timeout '+message.action+' '+(message.method||'')));},60000);waiting.set(id,{resolve,reject,timer});const payload=message.action==='invoke'?{...message,payload:undefined,payloadText:message.payloadText??serializeVariantText(message.payload??{})}:message;calls.push({id,...payload});native.stdin.write(JSON.stringify({id,...payload})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const write=(name,value)=>writeFileSync(resolve(output,name),JSON.stringify(value,null,2));
const positions=machine=>Object.fromEntries(machine.joints.map(j=>[j.entityId,j.position]));
try{
 await rpc({action:'open-project',path:project});
 // Restore only the private native scene to the exact reported failure state.
 // These calls are setup, separate from the tested UI actions below.
 const captured=capture.machine;
 for(const joint of captured.joints.filter(j=>j.type!=='fixed'))await invoke('Machine.SetJointPosition',{entityId:joint.entityId,position:joint.position});
 for(const owner of new Set(captured.collisions.map(c=>c.entityId))){
  const attachments=captured.collisions.filter(c=>c.entityId===owner&&c.geometry?.type==='box');if(!attachments.length)continue;
  await invoke('Machine.SetBoxColliders',{entityId:owner,boxes:attachments.map(c=>({id:c.id||'',name:c.name||c.collisionName,enabled:c.enabled!==false,position:c.pose.slice(0,3).map(v=>v*1000),rotationRadians:c.pose.slice(3),size:c.geometry.size.map(v=>v*1000)}))});
 }
 const before=(await invoke('Machine.List')).machines,tc=before.find(m=>m.entityId===captured.entityId),a=tc.joints.find(j=>j.jointName==='A');
 assert.ok(tc&&a);const beforePositions=before.map(m=>({id:m.entityId,positions:positions(m),collisions:m.collisions}));
 const state=await invoke('CamScene.ReadState');
 const blocked=await invoke('CamSimulation.MoveMachineIK',{expected:state.expected,machineId:tc.entityId,tipId:a.entityId,motionMode:'FK',jointPosition:a.position+.1});
 write('blocked-native.json',blocked);assert.equal(blocked.status,'Blocked');assert.equal(blocked.collisionStatus,'Blocked');
 assert.deepEqual((await invoke('Machine.List')).machines.map(m=>({id:m.entityId,positions:positions(m),collisions:m.collisions})),beforePositions,'Rejected A motion must not mutate any joint or box');
 if(process.argv.includes('--prepare-only')){console.log(JSON.stringify({prepared:true,nativeBlocked:true,collisionPairs:blocked.collisionPairs??null}));report.prepareOnly=true;}
 else{
  assert.ok(Array.isArray(blocked.collisionPairs)&&blocked.collisionPairs.length,'Native collision must carry exact box pairs');
  const endpoints=blocked.collisionPairs.flatMap(p=>[p.first,p.second]);
  assert.ok(endpoints.some(e=>e.boxName==='x_carriage_box_2'));assert.ok(endpoints.some(e=>e.boxName==='z_slide_box_1'));
  const expectedRefs=[...new Map(endpoints.filter(e=>e.editable).map(e=>[e.ownerElementId+':'+e.boxIndex,e])).values()];assert.equal(expectedRefs.length,2,'Only the actual X/Z collider pair is implicated');
  const info=await rpc({action:'info'}),{chromium}=createRequire(import.meta.url)(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
  browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});page=await browser.newPage({viewport:{width:1440,height:950}});page.on('pageerror',error=>pageErrors.push(error.message));await page.exposeFunction('fiveNativeRpc',rpc);
  await page.route('http://127.0.0.1:49197/**',route=>{const path=decodeURIComponent(new URL(route.request().url()).pathname);if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"><body style="margin:0"><div id="app" style="height:100vh"></div>'});if(!path.startsWith('/src/'))return route.abort();try{return route.fulfill({contentType:path.endsWith('.css')?'text/css':'text/javascript',body:readFileSync(browserAssetPath(path.slice(5)))});}catch{return route.abort();}});
  await page.goto('http://127.0.0.1:49197/');
  await page.evaluate(async({info,project})=>{
   const {serializeVariantText}=await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs'),entry=await import('/src/apps/five-axis-cam/webpage/entry.mjs'),{ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs'),{getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
   const view=getProjectView(info.projectId),f=window.fixture={entry,view,logs:[],mutations:[],saves:0};
   const sceneProxy={sceneId:info.sceneId,state:{sceneId:info.sceneId},pdo:{enabled:false},async invoke(method,payload={}){if(!/\.(Read|List|Get|Query|Resolve|Bind|Unbind)/.test(method))f.mutations.push({method,payload:structuredClone(payload)});return fiveNativeRpc({action:'invoke',scope:'scene',method,payloadText:serializeVariantText(payload)});},async getState(){return this.state;}};
   const resource=async(url,options,head)=>{const version=new Headers(options?.headers).get('ICAX-Resource-Version')??String(options?.version??''),r=await fiveNativeRpc({action:'resource',url,version,head,encoding:'base64'});const binary=atob(r.base64||''),bytes=Uint8Array.from(binary,c=>c.charCodeAt(0));return new Response(head?null:bytes,{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':r.version}});};
   sceneProxy.resources={get:(u,o)=>resource(u,o,false),head:(u,o)=>resource(u,o,true)};sceneProxy.views=new ViewClient(sceneProxy);
   const context=f.context={mount:document.querySelector('#app'),sceneProxy,project:{projectId:info.projectId,projectName:'碰撞盒定位 · 原生验收',projectPath:project},projectProxy:{state:{projectPath:project},async autosave(){f.saves++;return fiveNativeRpc({action:'export-project',path:project});}},activeRibbonTabId:'simulation',appProxy:{bridge:{}},productProxy:{async invoke(method,payload={}){return fiveNativeRpc({action:'invoke',scope:'product',method,payloadText:serializeVariantText(payload)});}},isCurrentProject:()=>true,actions:{refreshProductRibbon(){},refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){},appendProjectLog(...args){f.logs.push(args);},async selectRibbonTab(id){context.activeRibbonTabId=id;entry.mountProject(context);}}};
   entry.mountProject(context);
  },{info,project});
  await page.waitForFunction(()=>{if(fixture.view.error)throw Error(String(fixture.view.error));return fixture.view.scene?.machines?.length===2&&!fixture.view.pending;},null,{timeout:60000});
  const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveMachine?.busy,null,{timeout:30000});};
  await command('five.tool.machine-instances');
  await command('five.machine.select:'+tc.entityId);await command('five.machine.part:'+a.entityId);
  await page.evaluate(async()=>{fixture.feedback=await import('/src/apps/five-axis-cam/webpage/machineCollisionFeedback.mjs');});
  // Exercise the actual input -> production SDO -> rejected-input queue ->
  // feedback path as well as the direct FK native result checked above.
  const physicalInput=page.locator(`.cam-info-pane [data-five-machine-field="joint:${a.entityId}"]`);await physicalInput.fill(String((a.position+.1)*180/Math.PI));await physicalInput.blur();
  await page.waitForFunction(()=>fixture.view.fiveMachineCollisionFeedback?.overlays?.size===2,null,{timeout:30000});
  const rejectedInput=await page.evaluate(()=>{fixture.navigationMutationStart=fixture.mutations.length;return {errors:[...(fixture.view.fiveMachineParameterEdits?.entries.values()||[])].map(e=>e.error),commands:fixture.mutations.map(m=>m.method),feedback:fixture.view.fiveMachineCollisionFeedback.pairs};});assert.ok(rejectedInput.errors.some(Boolean),'The real numeric move remains a rejected uncommitted draft before quick-edit');assert.ok(rejectedInput.commands.includes('CamSimulation.MoveMachine'));write('rejected-physical-input.json',rejectedInput);
  const visuals=await page.evaluate(()=>{const s=fixture.view.fiveMachineCollisionFeedback;return {validity:s.validity,refs:s.refs,overlays:[...s.overlays.values()].map(value=>{const object=value.object||value.wire||value;return {name:object.name,userData:object.userData,parent:object.parent?.userData?.objectId,color:object.material.color.getHex(),renderOrder:object.renderOrder,fill:object.children[0]?.children[0]?.material.color.getHex(),fillOpacity:object.children[0]?.children[0]?.material.opacity,position:object.position.toArray(),scale:object.scale.toArray()};}),buttons:[...document.querySelectorAll('[data-five-command^="five.machine.collision-edit:"]')].map(n=>({label:n.getAttribute('aria-label'),command:n.dataset.fiveCommand}))};});write('initial-visuals.json',visuals);assert.equal(visuals.validity,'Current');assert.equal(visuals.overlays.length,2);assert.ok(visuals.overlays.every(o=>o.color===0xff4444&&o.fill===0xff4444&&o.fillOpacity===0&&o.renderOrder>=1200),'Reported boxes need red wireframes without filled faces covering the workpiece');
  const target=page.getByRole('button',{name:/编辑碰撞盒.*Z 滑枕.*z_slide_box_1/}).first();await target.click();
  await page.waitForFunction(id=>fixture.view.fiveMachineSelectedPartId===id&&fixture.view.fiveMachineColliders?.mode==='bounds',tc.joints.find(j=>j.jointName==='Z').entityId,{timeout:30000});
  const editor=await page.evaluate(()=>{const v=fixture.view,c=v.fiveMachineColliders,d=c.drafts.get(c.partId);return {part:v.fiveMachineSelectedPartId,mode:c.mode,box:d.boxes.find(b=>b.id===c.active),openRows:[...document.querySelectorAll('.five-collider-card[data-editing="true"]')].map(n=>n.dataset.boxId),redRows:[...document.querySelectorAll('.five-collider-card[data-collision="true"]')].map(n=>n.dataset.boxId),feedback:v.fiveMachineCollisionFeedback.validity,overlays:v.fiveMachineCollisionFeedback.overlays.size,mutations:fixture.mutations.slice(fixture.navigationMutationStart),saves:fixture.saves,rejectedInputs:fixture.view.fiveMachineParameterEdits?.entries.size||0};});assert.equal(editor.box.name,'z_slide_box_1');assert.equal(editor.openRows.length,1);assert.deepEqual(editor.redRows,[editor.box.id],'Only the implicated Z collider row is marked red');assert.equal(editor.feedback,'Current');assert.equal(editor.overlays,2);assert.equal(editor.saves,0);assert.equal(editor.rejectedInputs,0,'Quick-edit discards only the rejected move draft');assert.ok(!editor.mutations.some(r=>/SetBox|SetJoint|MoveMachine|SetElement/.test(r.method)));write('quick-editor.json',editor);
  const focus=page.locator('.five-placement-tools[data-transform-owner="collider"] [data-transform-mode="focus"]');await focus.click();await page.locator('canvas.icax-three-viewport-canvas').focus();await page.keyboard.press('f');
  assert.deepEqual((await invoke('Machine.List')).machines.map(m=>({id:m.entityId,positions:positions(m),collisions:m.collisions})),beforePositions,'Selecting, highlighting, quick-editing and focusing must not change any machine data');
  await page.screenshot({path:resolve(output,'collision-boxes-red.png')});
  const width=page.locator(`[data-five-machine-field="box:${editor.box.id}:size:0"]`);await width.fill('207.002');await width.blur();
  await page.waitForFunction(()=>{const c=fixture.view.fiveMachineColliders;return !c.saving&&![...c.drafts.values()].some(d=>d.dirty||d.timer||d.requested)&&fixture.saves>0;},null,{timeout:30000});
  assert.equal(await page.evaluate(()=>fixture.view.fiveMachineCollisionFeedback.validity),'Stale');assert.equal(await page.evaluate(()=>fixture.view.fiveMachineCollisionFeedback.overlays.size),0);
  await page.locator('[data-five-command="five.machine.collision-recheck"]').click();await page.waitForFunction(()=>!fixture.view.fiveMachineCollisionFeedback?.rechecking&&(!fixture.view.fiveMachineCollisionFeedback||fixture.view.fiveMachineCollisionFeedback.validity!=='Stale'),null,{timeout:30000});
  const cleared=await page.evaluate(()=>({feedback:fixture.view.fiveMachineCollisionFeedback&&{validity:fixture.view.fiveMachineCollisionFeedback.validity,overlays:fixture.view.fiveMachineCollisionFeedback.overlays.size},saves:fixture.saves,mutations:fixture.mutations.slice(fixture.navigationMutationStart)}));assert.ok(!cleared.feedback||cleared.feedback.overlays===0);const repaired=(await invoke('Machine.List')).machines;assert.deepEqual(repaired.map(m=>({id:m.entityId,positions:positions(m)})),beforePositions.map(({id,positions})=>({id,positions})),'Collider correction/recheck cannot move the machine');write('repaired-rechecked.json',{...cleared,machines:repaired});
  const undone=await rpc({action:'undo'});assert.equal(undone.changed,true);await page.evaluate(()=>fixture.entry.onProjectHistoryChanged(fixture.context));await command('five.machine.collision-recheck');await page.waitForFunction(()=>fixture.view.fiveMachineCollisionFeedback?.validity==='Current'&&fixture.view.fiveMachineCollisionFeedback.overlays.size===2,null,{timeout:30000});
  assert.deepEqual((await invoke('Machine.List')).machines.map(m=>({id:m.entityId,positions:positions(m),collisions:m.collisions})),beforePositions,'Undo restores the one edited collider; read-only checks introduce no undo command');
  assert.deepEqual(pageErrors,[]);await page.screenshot({path:resolve(output,'collision-boxes-after-undo.png')});report.pass=true;report.cases=['real reported TC-800 X/Z overlap from native FK rejection','exact box identities through production native result','two cross-part red collider overlays with actual meshes','one click opens implicated Z box from a rejected numeric move draft','F focus and editor navigation do not modify machine geometry or poses','editing only the Z box saves the private project and marks diagnosis stale','read-only recheck clears corrected collision without moving machine','native undo restores exact collider data and recheck restores both red boxes'];report.native=blocked;report.visuals=visuals;report.editor=editor;report.repaired=cleared;console.log(JSON.stringify({pass:true,cases:report.cases.length,output}));
 }
}catch(error){report.error=error.stack;if(page){try{write('failure-state.json',await page.evaluate(()=>({error:fixture.view.error,pending:fixture.view.pending,sceneKeys:Object.keys(fixture.view.scene||{}),machine:fixture.view.fiveMachine,machines:fixture.view.scene?.machines?.length,logs:fixture.logs,pageErrors:[],feedback:fixture.view.fiveMachineCollisionFeedback&&{validity:fixture.view.fiveMachineCollisionFeedback.validity,refs:fixture.view.fiveMachineCollisionFeedback.refs}})));}catch{}}await page?.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{write('acceptance.json',report);write('native-calls.json',calls);writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}native.stdin.end();}
