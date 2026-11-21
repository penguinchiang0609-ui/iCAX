import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {chromium} from 'playwright';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';


import {serveBrowserAsset,browserAssetPath} from './browserPackageRuntime.mjs';
import {verifyHeadScopes} from './FiveAxisHeadScope.native.browserCase.mjs';
import {verifyOperations} from './FiveAxisOperations.native.browserCase.mjs';
import {verifyOperationMachineCoupling} from './OperationMachineCoupling.native.browserCase.mjs';
import {verifySupportFooter} from './FiveAxisSupportFooter.native.browserCase.mjs';
import {verifySupportRecommendationBusy} from './FiveAxisSupportRecommendationBusy.native.browserCase.mjs';
import {verifySidebarContext} from './FiveAxisSidebarContext.native.browserCase.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/head-workspace-20261102/browser');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'src/x64/Debug');
const executable=process.env.ICAX_FIVE_AXIS_NATIVE_TEST_EXECUTABLE||'FiveAxisHeadInstanceTest.exe';
const hashNative=name=>createHash('sha256').update(readFileSync(resolve(runtime,name))).digest('hex');
const nativeBinaryHashes=Object.fromEntries([executable,'FiveAxisCAM.dll','OpenCascadeResourceImport.dll','IntentToolpath.dll'].map(name=>[name,hashNative(name)]));
const child=spawn(resolve(runtime,executable),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,env:{...process.env,ICAX_USER_DATA_ROOT:resolve(output,'native-user-data')},stdio:['pipe','pipe','pipe']});
const waiting=new Map(),errors=[],nativeLog=[];let serial=0,browser,page;
const fail=error=>{for(const p of waiting.values()){clearTimeout(p.timer);p.reject(error);}waiting.clear();};child.on('error',fail);child.on('exit',code=>{if(waiting.size)fail(Error('Native bridge exited '+code));});child.stderr.on('data',d=>nativeLog.push(String(d)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){nativeLog.push(line);return;}const r=JSON.parse(line.slice(12)),p=waiting.get(r.id);if(!p)return;waiting.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{waiting.delete(id);reject(Error('Native timeout '+message.action));},60000);waiting.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:message.payloadText??serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const report={passed:false,realNativeSDO:true,productionWebGL:true,standaloneCef:false,nativeBinaryHashes,testProcessIds:{node:process.pid,native:child.pid},cases:[]};
const supportFooterAcceptance=!!process.env.ICAX_SUPPORT_FOOTER_ACCEPTANCE;
const supportRecommendationBusyAcceptance=!!process.env.ICAX_SUPPORT_RECOMMENDATION_BUSY_ACCEPTANCE;
const sidebarContextAcceptance=!!process.env.ICAX_SIDEBAR_CONTEXT_ACCEPTANCE;
const checkpoint=text=>{report.cases.push(text);console.log(text);};
try{
 let domain,targetSetupId,targetPlacementId,sibling,otherWorkpiece,info,targetMachineType;
 const edit=async(method,payload)=>invoke(method,{commandId:crypto.randomUUID(),expected:(await invoke('CamScene.ReadState')).expected,...payload});
 if(supportFooterAcceptance||supportRecommendationBusyAcceptance){
  info=await rpc({action:'info'});
  const sourcePath=resolve(process.env.ICAX_SUPPORT_FOOTER_MODEL||'TestData/FA/汽车/04_Pillar_Reinforcement_900x300_t1p4.step');
  let task=await edit('CamWorkpiece.Import',{sourcePath});
  while(task.status==='Running'){await new Promise(r=>setTimeout(r,25));task=await invoke('CamWorkpiece.QueryTask',{epoch:task.epoch,taskId:task.taskId});}
  assert.equal(task.status,'Completed',task.error);domain=await invoke('CamScene.ReadState');
  const placement=domain.placements.find(p=>p.id===domain.activePlacementId);
  const operation=await edit('CamOperation.Manage',{action:'create',workpieceId:placement.workpieceId,name:'Pillar 支撑页验收',machineType:'HeadHead'});
  domain=await invoke('CamScene.ReadState');targetSetupId=operation.setupId;targetPlacementId=operation.placementId;targetMachineType='HeadHead';
  report.source={path:sourcePath,sha256:createHash('sha256').update(readFileSync(sourcePath)).digest('hex'),workpieceId:placement.workpieceId};
 }else{
 domain=await rpc({action:'prepare-axis-test'});targetSetupId=domain.activeSetupId;targetPlacementId=domain.activePlacementId;
 const nativeHeads=(await invoke('CamHead.ListBuiltins')).items;targetMachineType=domain.setups.find(s=>s.id===targetSetupId).machineType.machineType;const siblingMachineType=process.env.ICAX_OPERATION_ACCEPTANCE?'TableTable':targetMachineType;
 const sentinelHead=nativeHeads.find(h=>h.machineTypes?.includes(siblingMachineType));assert.ok(sentinelHead,'A matching sentinel cutting head is required');
 sibling=await edit('CamSetup.Manage',{placementId:targetPlacementId,action:'create',name:'同工件的其他姿态',machineType:siblingMachineType});
 await edit('CamHead.Configure',{setupId:sibling.setupId,kind:'Builtin',builtinDefinitionId:sentinelHead.id});
 info=await rpc({action:'info'});let task=await edit('CamWorkpiece.Import',{sourcePath:info.fixturePath});
 while(task.status==='Running'){await new Promise(r=>setTimeout(r,25));task=await invoke('CamWorkpiece.QueryTask',{epoch:task.epoch,taskId:task.taskId});}
 assert.equal(task.status,'Completed',task.error);
 domain=await invoke('CamScene.ReadState');
 otherWorkpiece=await edit('CamSetup.Manage',{placementId:domain.activePlacementId,action:'create',name:'其他工件的姿态',machineType:siblingMachineType});
 await edit('CamHead.Configure',{setupId:otherWorkpiece.setupId,kind:'Builtin',builtinDefinitionId:sentinelHead.id});
 await edit('CamWorkpiece.Activate',{placementId:targetPlacementId});domain=await invoke('CamScene.ReadState');
 assert.equal(domain.activeSetupId,targetSetupId);assert.equal(domain.workpieces.length,2);assert.equal(domain.setups.length,3);
 }
 const authored=state=>Object.fromEntries(['workpieces','placements','intentToolpaths','fixtures','fixtureUnits'].map(key=>[key,state[key]]));
 const otherSetups=state=>state.setups.filter(s=>s.id!==targetSetupId);
 const activeSetup=state=>state.setups.find(s=>s.id===targetSetupId);
 const unchangedAuthored=authored(domain),unchangedOthers=otherSetups(domain);
 const assertAssignmentScope=state=>{assert.deepEqual(authored(state),unchangedAuthored,'Head selection must preserve CAD, placements, toolpaths and fixtures');assert.deepEqual(otherSetups(state),unchangedOthers,'Head assignment must preserve every other workpiece/setup');};
 browser=await chromium.launch({channel:process.env.ICAX_BROWSER_CHANNEL||'chrome',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 page=await browser.newPage({viewport:{width:1600,height:950}});page.on('pageerror',e=>errors.push(e.message));await page.exposeFunction('headWorkspaceRpc',rpc);
 await page.route('http://127.0.0.1:49189/**',route=>{
  const path=new URL(route.request().url()).pathname;
  if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div><script type="module" src="/src/tests/iCAX-UI/fixtures/fiveAxisPreview.mjs"></script></body></html>'});
  if(path==='/src/iCAX-UI/SDK/runtime.mjs')return route.fulfill({contentType:'text/javascript',body:'export async function connectApplication(){return globalThis.__fiveFixture.appProxy;}'});
  try{const file=path.startsWith('/src/tests/iCAX-UI/fixtures/')?resolve(path.slice(1)):browserAssetPath(path.slice(5));return route.fulfill({contentType:path.endsWith('.css')?'text/css':path.endsWith('.svg')?'image/svg+xml':path.endsWith('.json')?'application/json':'text/javascript',body:readFileSync(file)});}catch{return route.fulfill({status:404,body:''});}
 });
 await page.goto('http://127.0.0.1:49189/?product=icax.five-axis-cam');
 await page.waitForFunction(()=>globalThis.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(async({domain,projectPath,operationAcceptance})=>{
  const encodeVariant=operationAcceptance?(await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs')).serializeVariantText:null;
  const fixture=__fiveFixture,project=fixture.appProxy.getProduct().getProject(),proxy=project.getMainScene(),oldInvoke=proxy.invoke.bind(proxy);
  const f=window.f={view:fixture.view,held:false,holdRead:false,project,proxy,active:true,headRequests:[]};
  project.state.projectPath=projectPath;project.autosave=async()=>{f.projectSaves=(f.projectSaves||0)+1;await headWorkspaceRpc({action:'export-project',path:project.state.projectPath});return {saved:true};};
  fixture.scene.domain=domain;fixture.view.scene.domain=domain;proxy.getUndoRedoState=async()=>({revision:'1',canUndo:false,canRedo:false});project.getUndoRedoState=proxy.getUndoRedoState;
  proxy.pdo={enabled:true,async withReadDescriptor(descriptor,reader){const result=await headWorkspaceRpc({action:'pdo-read',slotId:String(descriptor.id),arenaName:descriptor.arenaName});return reader(Uint8Array.from(result.bytes).buffer,{dataVersion:result.dataVersion});}};
  proxy.invoke=async(method,payload={},options)=>{
   if(method==='CamScene.ReadState'&&f.holdRead){f.holdRead=false;f.held=true;await new Promise(r=>f.release=r);f.held=false;}
   if(method.startsWith('CamHead.'))f.headRequests.push({method,payload:structuredClone(payload)});
   if(method.startsWith('CamHead.')||method==='CamScene.ReadState'||method==='CamWorkpiece.ListFiles'||operationAcceptance&&(method.startsWith('Cam')||method.startsWith('View.'))){if(method.startsWith('View.'))(f.nativeViewRequests??=[]).push(method);return headWorkspaceRpc({action:'invoke',scope:'scene',method,payload,...(encodeVariant?{payloadText:encodeVariant(payload)}:{})});}
   return oldInvoke(method,payload,options);
  };
  proxy.resources.get=async(url,options)=>{const version=new Headers(options?.headers).get('ICAX-Resource-Version')||'',r=await headWorkspaceRpc({action:'resource',url,version,head:false,encoding:'base64'});return new Response(Uint8Array.from(atob(r.base64),x=>x.charCodeAt(0)),{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':r.version}});};
  if(operationAcceptance){
   proxy.resources.head=async(url,options)=>{const version=new Headers(options?.headers).get('ICAX-Resource-Version')||'',r=await headWorkspaceRpc({action:'resource',url,version,head:true,encoding:'base64'});return new Response(null,{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':r.version}});};
   f.resetNativeViews=async()=>{for(const area of Object.values(f.view.areas||{})){await area.viewContentRequest?.catch(()=>{});await area.viewReader?.stop?.().catch(()=>{});area.viewReader=null;area.viewContent=null;area.viewContentRequest=null;area.viewDefinitionKey=null;area.appliedViewRevision='0';area.viewApplyByRevision?.clear();}};
   await f.resetNativeViews();const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');proxy.views=new ViewClient(proxy);
  }
  f.head=()=>f.view.scene.domain.setups.find(s=>s.id===f.view.scene.domain.activeSetupId).headModel;
 },{domain,projectPath:resolve(output,process.env.ICAX_OPERATION_ACCEPTANCE?'工序工作区.i5cam':'切割头工作区.i5cam'),operationAcceptance:!!process.env.ICAX_OPERATION_ACCEPTANCE});
 const command=async id=>{let timer;try{return await Promise.race([page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Command timed out: '+id)),20000);})]);}finally{clearTimeout(timer);}};
 const idle=()=>page.waitForFunction(()=>!f.view.pending&&!f.view.fiveOperationAreaSync&&!f.view.fiveOperationNavigationPending&&!f.view.fiveWorkpieceSwitching&&!f.view.fiveSimulation?.busy&&!f.view.fiveSimulation?.headChoiceBusy&&!f.view.fiveHeadEditor?.busy&&!f.view.fiveHeadSelection?.loading&&!f.view.fiveHeadSelection?.applying&&!f.view.fiveHeadAssignments?.loading&&!f.view.fiveHeadAssignments?.busy);
 const ready=(builtinId=null)=>page.waitForFunction(id=>!f.view.pending&&f.view.fiveHeadPreview?.phase==='ready'&&(!id||f.view.fiveHeadPreview.head?.builtinDefinitionId===id),builtinId,{timeout:60000});
 const empty=()=>page.waitForFunction(()=>!f.view.pending&&f.view.fiveHeadPreview?.phase==='empty',null,{timeout:60000});
 const useSelected=async({unassigned=false}={})=>{await page.locator('[data-five-command="five.head-workspace.use"]').click();await idle();await (unassigned?empty():ready());};
 const currentHead=page.locator('[data-five-key="head-workspace-current"]');
 const field=key=>page.locator(`[data-five-head-instance-field="${key}"]`);
 if(sidebarContextAcceptance){
  await verifySidebarContext({page,rpc,invoke,command,idle,output,report,checkpoint,targetSetupId,targetPlacementId,sibling,otherWorkpiece,info});
  assert.deepEqual(errors,[]);report.passed=true;
 }else if(supportRecommendationBusyAcceptance){
  await verifySupportRecommendationBusy({page,rpc,invoke,command,idle,output,report,checkpoint,targetSetupId,targetPlacementId,info});
  assert.deepEqual(errors,[]);report.passed=true;
 }else if(supportFooterAcceptance){
  await verifySupportFooter({page,rpc,invoke,command,idle,output,report,checkpoint,targetSetupId,targetPlacementId,info});
  assert.deepEqual(errors,[]);report.passed=true;
 }else if(process.env.ICAX_OPERATION_ACCEPTANCE){
  await verifyOperations({page,rpc,invoke,command,idle,output,report,checkpoint,targetSetupId,targetPlacementId,sibling,otherWorkpiece});
  if(process.env.ICAX_OPERATION_MACHINE_COUPLING_ACCEPTANCE)await verifyOperationMachineCoupling({page,rpc,invoke,command,idle,output,report,checkpoint});
  assert.deepEqual(errors,[]);report.passed=true;
 }else{
 await page.locator('[data-tab-id="toolpath"]').click();
 await page.locator('[data-command-id="five.cam-page.head"]').click();
 await page.waitForFunction(()=>f.view.fiveSimulation?.builtinHeads?.length>0&&f.view.fiveHeadEditor?.libraryLoaded);
 assert.equal(await page.locator('[data-command-id="five.simulation.head-open"]').count(),0);
 assert.equal(await page.getByRole('dialog',{name:'切割头设置',exact:true}).count(),0);
 const allHeads=await page.evaluate(()=>f.view.fiveSimulation.builtinHeads),heads=allHeads.filter(h=>h.machineTypes?.includes(targetMachineType)),ab=heads.find(h=>h.kind==='AB')||heads[0],other=heads.find(h=>h.id!==ab.id);
 const browse=async head=>{await page.locator(`[data-head-choice-value="builtin:${head.id}"]`).click();await idle();await ready(head.id);};
 const choose=async head=>{await browse(head);await useSelected();};
 if(process.env.ICAX_HEAD_SCOPE_ACCEPTANCE){
  await verifyHeadScopes({page,rpc,invoke,command,idle,ready,empty,browse,heads,output,report,checkpoint,targetSetupId,targetPlacementId,sibling,otherWorkpiece});
  assert.deepEqual(errors,[]);report.passed=true;
 }else{
 const beforeBrowse=await invoke('CamScene.ReadState');await browse(ab);
 assert.deepEqual(await invoke('CamScene.ReadState'),beforeBrowse,'Browsing a catalog head must not bind it or change scene revision');
 assert.ok(await page.evaluate(({id,setupId})=>f.headRequests.some(r=>r.method==='CamHead.Preview'&&r.payload.kind==='Builtin'&&r.payload.builtinDefinitionId===id&&r.payload.setupId===setupId),{id:ab.id,setupId:targetSetupId}));
 assert.equal(await page.locator('[data-five-command="five.head-workspace.use"]').isEnabled(),true);
 await useSelected();
 const assigned=await invoke('CamScene.ReadState');assert.equal(activeSetup(assigned).headModel.builtinDefinitionId,ab.id);assertAssignmentScope(assigned);
 assert.equal(await page.locator('[data-five-command="five.head-workspace.use"]').isDisabled(),true,'The already used head must not be assigned again');
 assert.ok((await currentHead.innerText()).includes(ab.name));assert.equal(await page.locator(`[data-head-choice-value="builtin:${ab.id}"]`).getAttribute('aria-current'),'true');
 const layout=await page.evaluate(()=>{
  const rect=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom};};
  return {left:rect('.cam-context-pane'),right:rect('.cam-info-pane'),scene:rect('.cam-viewport'),preview:rect('[data-five-head-preview]'),field:rect('[data-five-head-instance-field="tcp.0"]'),visibleBase:getComputedStyle(document.querySelector('.cam-render-viewport-shell')).visibility,partCount:f.view.fiveHeadPreview.model.children.length};
 });
 assert.ok(layout.preview.x>=layout.scene.x&&layout.preview.right<=layout.scene.right+1&&layout.preview.width>700&&layout.preview.height>400,JSON.stringify(layout));
 assert.ok(layout.field.x>=layout.right.x&&layout.left.right<=layout.preview.x);assert.equal(layout.visibleBase,'hidden');assert.ok(layout.partCount>1);
 report.layout=layout;await page.screenshot({path:resolve(output,'切割头三栏工作区.png')});
 checkpoint('Actual application shell: CAM cutting-head page, left catalog, center native head meshes, right parameters, no settings popup');
 const instance=await page.evaluate(()=>f.head().instance.id);
 await page.evaluate(()=>{window.choiceCanvases={head:f.view.fiveHeadPreview.canvas,base:f.view.viewport.renderer.domElement};});
 await browse(other);const afterOtherPreview=await invoke('CamScene.ReadState');assert.deepEqual(afterOtherPreview,assigned);assert.equal(activeSetup(afterOtherPreview).headInstanceId,instance);
 assert.ok((await currentHead.innerText()).includes(ab.name));assert.equal(await page.locator(`[data-head-choice-value="builtin:${ab.id}"]`).getAttribute('aria-current'),'true');assert.equal(await page.locator(`[data-head-choice-value="builtin:${other.id}"]`).getAttribute('aria-pressed'),'true');assert.equal(await page.locator(`[data-head-choice-value="builtin:${other.id}"]`).getAttribute('aria-current'),null);
 await useSelected();const otherAssigned=await invoke('CamScene.ReadState');assert.equal(activeSetup(otherAssigned).headModel.builtinDefinitionId,other.id);assertAssignmentScope(otherAssigned);
 assert.ok((await currentHead.innerText()).includes(other.name));assert.equal(await page.locator(`[data-head-choice-value="builtin:${other.id}"]`).getAttribute('aria-current'),'true');
 await choose(ab);assert.equal(await page.evaluate(()=>f.head().instance.id),instance);
 const beforeUnassignedPreview=await invoke('CamScene.ReadState');await page.locator('[data-head-choice-value=""]').click();await idle();await empty();
 assert.deepEqual(await invoke('CamScene.ReadState'),beforeUnassignedPreview,'Browsing Unassigned must not detach the current head');
 await useSelected({unassigned:true});const detached=await invoke('CamScene.ReadState');assert.equal(!!activeSetup(detached).headInstanceId,false);assert.equal(!!activeSetup(detached).headModel?.instance,false);assertAssignmentScope(detached);
 assert.match(await currentHead.innerText(),/未指定/);assert.equal(await page.locator('[data-head-choice-value=""]').getAttribute('aria-current'),'true');
 await choose(ab);assert.equal(await page.evaluate(()=>f.head().instance.id),instance);
 assert.equal(await page.evaluate(()=>choiceCanvases.head===f.view.fiveHeadPreview.canvas&&choiceCanvases.base===f.view.viewport.renderer.domElement),true,'Browsing and assignment must retain both existing canvases');
 checkpoint('Catalog browsing uses a read-only native Preview query; explicit Use and Unassigned affect only the current workpiece/setup and preserve CAD/toolpaths and other setups');
 const tcp=Number(await field('tcp.0').inputValue());await field('tcp.0').fill(String(tcp+1));await page.locator('[data-five-command="five.head-instance.apply"]').click();await idle();await ready();
 assert.equal((await invoke('CamScene.ReadState')).headInstances.find(i=>i.id===instance).parameters.tcp[0],tcp+1);
 await page.locator('[data-five-head-joint]').first().fill('10');assert.ok(await page.evaluate(()=>Object.values(f.view.fiveHeadPreview.axisAngles).some(v=>Math.abs(v)>0)));
 checkpoint('Catalog switches reuse project instances; TCP edit persists through native SDO; joint trial rotation drives the central model');
 await page.locator('[data-five-head-collider-field="show"]').check();
 await page.locator('[data-five-command^="five.head-collider.edit:"]').first().click();
 assert.equal(await page.locator('.five-head-preview-stage .five-placement-tools').isVisible(),true);
 await command('five.head-collider.edit-end');
 // Stress both sidebar and nested scroll containers while a native refresh is pending.
 await page.setViewportSize({width:1366,height:620});await command('five.head-instance.library-open');
 await page.locator('[data-five-key="head-save-configuration"] > summary').click();
 await page.addStyleTag({content:'.five-workspace[data-head-workspace="true"] .cam-context-pane>.five-head-library-manager{height:220px;min-height:220px}'});
 await page.evaluate(()=>{for(let i=0;i<35;i++)f.view.fiveHeadEditor.entries.push({id:'scroll-probe-'+i,name:'滚动验证刀头 '+i});});
 await command('five.head-instance.library-close');await command('five.head-instance.library-open');
 // Keep controlled catalog rows while refreshing the actual scene through the application.
 await page.evaluate(()=>{for(let i=0;i<35;i++)f.view.fiveHeadEditor.entries.push({id:'scroll-probe-'+i,name:'滚动验证刀头 '+i});});
 await command('five.cam-page.head');
 await page.locator('[data-five-key="head-save-configuration"] > summary').evaluate(e=>{if(!e.parentElement.open)e.click();});
 await page.evaluate(()=>{
  window.headInteraction={left:document.querySelector('.cam-context-pane'),right:document.querySelector('.cam-info-pane'),library:document.querySelector('[data-five-key="head-configuration-library"]'),input:document.querySelector('[data-five-head-instance-field="tcp.0"]'),canvas:f.view.fiveHeadPreview.canvas,base:f.view.viewport.renderer.domElement,events:0};
  headInteraction.input.addEventListener('workspace-probe',()=>headInteraction.events++);
  f.holdRead=true;f.interactionRefresh=__icaxAppShell.executeRibbonCommand('five.refresh');
 });
 await page.waitForFunction(()=>f.held);
 const expected=await page.evaluate(()=>{const s=headInteraction;s.input.focus({preventScroll:true});s.input.setSelectionRange(0,1);s.left.scrollTop=110;s.right.scrollTop=100;s.library.scrollTop=90;return [s.left.scrollTop,s.right.scrollTop,s.library.scrollTop];});
 await page.evaluate(async()=>{f.release();await f.interactionRefresh;});
 const retained=await page.evaluate(()=>{const s=headInteraction;s.input.dispatchEvent(new Event('workspace-probe'));return {input:s.input===document.querySelector('[data-five-head-instance-field="tcp.0"]'),canvas:s.canvas===f.view.fiveHeadPreview.canvas,base:s.base===f.view.viewport.renderer.domElement,focus:document.activeElement===s.input,selection:[s.input.selectionStart,s.input.selectionEnd],scroll:[s.left.scrollTop,s.right.scrollTop,s.library.scrollTop],events:s.events};});
 assert.deepEqual(retained,{input:true,canvas:true,base:true,focus:true,selection:[0,1],scroll:expected,events:1});assert.ok(expected.every(x=>x>0));report.interaction=retained;
 await command('five.head-instance.refresh-profiles');
 await command('five.head-instance.library-close');
 // A conditional field that disappears must not focus a different editor.
 await field('tcpMode').selectOption('Gauge');await field('lengthMm').fill('50');
 await field('lengthMm').focus();await page.evaluate(()=>{const control=document.querySelector('[data-five-head-instance-field="tcpMode"]');control.value='Coordinates';control.dispatchEvent(new Event('change',{bubbles:true}));});
 assert.equal(await field('lengthMm').count(),0);assert.equal(await field('tcp.0').evaluate(e=>document.activeElement===e),false);
 checkpoint('Real browser retains both sidebar scrolls, nested scroll, input node/focus/selection/listener and both canvases; removed conditional field does not steal focus');
 await page.setViewportSize({width:1600,height:950});
 const picker=page.getByRole('dialog',{name:'导入切割头',exact:true});
 const beforeImport=await page.evaluate(()=>f.head().instance.id);
 await page.locator('[data-command-id="five.simulation.head-import"]').click();await picker.waitFor();await picker.locator('footer [data-nesting-file-action="cancel"]').click();await idle();assert.equal(await page.evaluate(()=>f.head().instance.id),beforeImport);
 const importPath=resolve(output,'工作区导入刀头.obj');writeFileSync(importPath,'v 0 0 0\nv 20 0 0\nv 0 30 40\nf 1 2 3\n');
 await page.locator('[data-command-id="five.simulation.head-import"]').click();await picker.waitFor();await picker.press('Control+l');await picker.locator('[data-nesting-file-directory]').fill(output);await picker.locator('[data-nesting-file-directory]').press('Enter');
 await picker.locator('[data-nesting-file-is-directory="false"]').filter({hasText:'工作区导入刀头.obj'}).click();await picker.locator('footer [data-nesting-file-action="open"]').click();
 await page.locator('[data-five-key="head-import-prompt"]').waitFor();await idle();await ready();
 assert.notEqual(await page.evaluate(()=>f.head().instance.id),beforeImport);
 await page.locator('[data-five-command="five.head-instance.finish-import"]').click();await idle();
 checkpoint('Toolbar import opens the production file picker; cancel preserves selection; a real OBJ imports into the catalog, central scene and parameter inspector');
 console.log('Checking return to path page');await command('five.cam-page.cutting');assert.equal(await page.locator('[data-five-head-preview]').count(),0);assert.equal(await page.evaluate(()=>getComputedStyle(document.querySelector('.cam-render-viewport-shell')).visibility),'visible');
 console.log('Checking return to head page');await command('five.cam-page.head');await ready();await page.screenshot({path:resolve(output,'导入刀头工作区.png')});
 console.log('Checking native save and reopen');const beforeReopen=await invoke('CamScene.ReadState');await rpc({action:'save-reopen'});const reopened=await invoke('CamScene.ReadState');assert.ok(reopened.headInstances.some(i=>i.id===instance&&i.parameters.tcp[0]===tcp+1));
 assert.equal(reopened.activeSetupId,targetSetupId);assert.equal(activeSetup(reopened).headInstanceId,activeSetup(beforeReopen).headInstanceId);assert.deepEqual(activeSetup(reopened).headModel,activeSetup(beforeReopen).headModel);assertAssignmentScope(reopened);
 await command('five.refresh');await idle();await ready();assert.equal(await page.evaluate(()=>f.head().instance.id),activeSetup(beforeReopen).headInstanceId);
 assert.ok((await currentHead.innerText()).includes(activeSetup(beforeReopen).headModel.name));
 checkpoint('The explicitly used head and current setup binding survive native save/reopen while the other workpiece/setup bindings and authored CAD/toolpaths remain unchanged');
 assert.deepEqual(errors,[]);report.passed=true;report.standaloneCef=false;report.filePicker='Production in-app picker with native file listing and import';
 }
 }
}catch(error){report.failure=error.stack;report.state=await page?.evaluate(()=>({text:document.body.innerText.slice(-2000),page:window.f?.view.fiveCamPage,area:window.f?.view.fiveAxisArea,preparationKind:window.f?.view.fivePreparationKind,operationTool:window.f?.view.fiveAxisUI?.tools?.operation,error:window.f?.view.error,activePlacementId:window.f?.view.scene?.activePlacementId,domain:window.f?.view.scene?.domain&&{activePlacementId:f.view.scene.domain.activePlacementId,activeSetupId:f.view.scene.domain.activeSetupId,placements:f.view.scene.domain.placements},areas:Object.fromEntries(Object.entries(window.f?.view.areas||{}).map(([id,area])=>[id,{definition:area.viewDefinitionKey,snapshot:area.viewReader?.snapshot,content:area.viewContent&&{entityIds:[...area.viewContent.entityIds],rows:area.viewContent.rows}}])),nativeViewRequests:window.f?.nativeViewRequests,viewportIds:[...(window.f?.view.viewport?.sceneObjects?.keys()||[])],headError:window.f?.view.fiveSimulation?.headError})).catch(()=>null);await page?.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{report.nativeBinariesUnchanged=Object.entries(nativeBinaryHashes).every(([name,sha256])=>hashNative(name)===sha256);if(!report.nativeBinariesUnchanged)report.passed=false;writeFileSync(resolve(output,'report.json'),JSON.stringify({...report,errors},null,2));writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();child.kill();}
console.log(JSON.stringify({passed:report.passed,cases:report.cases.length,output}));
