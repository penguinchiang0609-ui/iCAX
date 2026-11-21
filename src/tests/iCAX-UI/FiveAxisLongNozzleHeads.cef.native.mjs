import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';

const output=resolve('output/acceptance/long-nozzle-heads-20261102');mkdirSync(output,{recursive:true});
const port=Number(process.env.ICAX_LONG_HEAD_PORT||9695);assert(port>=9690);
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
const report={passed:false,realNativeSDO:true,heads:[],errors:[]};
const hash=x=>createHash('sha256').update(JSON.stringify(x)).digest('hex');
page.setDefaultTimeout(60000);page.on('pageerror',e=>report.errors.push(e.message));
const invoke=(method,payload={})=>page.evaluate(({method,payload})=>lh.viewportSceneProxy.invoke(method,payload),{method,payload});
const read=()=>invoke('CamScene.ReadState');
const selected=state=>state.setups.find(s=>s.id===state.activeSetupId);
const command=id=>page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);
const finishTask=async(task,method='CamSimulation.QueryTask')=>{
 const until=Date.now()+120000;
 while(task.status==='Running'){
  assert(Date.now()<until,'Native task timed out');await new Promise(r=>setTimeout(r,100));
  task=await invoke(method,{epoch:task.epoch,taskId:task.taskId});
 }
 assert.equal(task.status,'Completed',task.error);return task;
};
const idle=()=>page.waitForFunction(async()=>{
 if(__icaxAppShell.getState().pendingCount||!window.lh||lh.pending||lh.fiveHeadEditor?.busy)return false;
 const context=lh.fiveRapid?.saveBinding?.context;
 if(!context)return true;
 const {getWindowCloseGuard}=await import('../../../apps/five-axis-cam/webpage/entry.mjs');
 return !getWindowCloseGuard(context)?.blocked;
});
const closeSaved=async()=>{
 await idle();await page.evaluate(()=>{window.longHeadClosing=__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId);});
 const save=page.locator('[data-project-close-choice="save"]');
 await save.waitFor({timeout:2000}).catch(()=>{});if(await save.isVisible())await save.click();
 await page.evaluate(()=>longHeadClosing);
};
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await restoreNativeCefViewport(page);
 const active=await page.evaluate(()=>__icaxAppShell.getState());
 if(active.activeProjectPath){
  assert.match(active.activeProjectPath,/(collision-workspace|long-nozzle-heads)-20261102/,'Only use the owned acceptance project');
  await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.lh=getProjectView(__icaxAppShell.getState().activeProjectId);});
  await closeSaved();
 }
 report.project=resolve(output,`long-heads-${Date.now()}.i5cam`);
 copyFileSync(resolve('output/acceptance/selected-collision-20261102/full-head/yoke-contact.i5cam'),report.project);
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),report.project);
 await page.waitForFunction(async()=>{const a=__icaxAppShell.getState();if(a.pendingCount||!a.activeProjectId)return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.lh=getProjectView(a.activeProjectId);return !!lh.scene?.domain&&!lh.pending;});
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));
 let baseline=await read();
 // This older acceptance fixture saved a 1 mm nozzle stand-off. Its local
 // copy uses a virtual TCP, so the additional path offset must be zero.
 await invoke('CamRapid.Configure',{commandId:crypto.randomUUID(),expected:baseline.expected,setupId:baseline.activeSetupId,settings:{...selected(baseline).rapidSettings,followHeightMm:0}});
 baseline=await read();
 await finishTask(await invoke('CamRapid.Generate',{commandId:crypto.randomUUID(),expected:baseline.expected,setupId:baseline.activeSetupId,geometryRevision:selected(baseline).generatedGeometry.revision,safe:false}),'CamRapid.QueryStatus');
 baseline=await read();
 assert.equal(selected(baseline).rapidSettings.followHeightMm,0);assert(selected(baseline).generatedGeometry.transitions.every(t=>t.geometryStatus==='Generated'));
 report.zeroExtraOffsetRapidGeneration=true;
 const heads=(await invoke('CamHead.ListBuiltins')).items;
 assert.deepEqual(heads.map(h=>h.kind),['AC','AB','BC','A','Fixed']);
 const preserved=state=>Object.fromEntries(['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'].map(k=>[k,state[k]]));
 report.beforeHash=hash(preserved(baseline));report.beforePathHash=hash(selected(baseline).generatedGeometry);
 let state=baseline;
 for(const item of heads){
  console.log('Loading '+item.kind);
  await invoke('CamHead.Configure',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,kind:'Builtin',builtinDefinitionId:item.id});
  state=await read();const head=selected(state).headModel;
  const expectedTcp=item.kind==='Fixed'?[0,3,-227.7]:item.kind==='BC'?[-1.99768074009807e-16,-3.26246023178101,-300.2]:[-3.26246023178101,0,-300.2];
  head.tcpLocalPosition.forEach((n,i)=>assert(Math.abs(n-expectedTcp[i])<1e-7));
  assert.equal(head.rotaryAxisCount,item.headAxes.length);assert.deepEqual(head.assembly.headAxes,item.headAxes);
  assert.equal(hash(preserved(state)),report.beforeHash);assert.equal(hash(selected(state).generatedGeometry),report.beforePathHash);
  const machine=await invoke('CamSimulation.ReadMachine',{setupId:state.activeSetupId});
  writeFileSync(resolve(output,item.kind+'-native.json'),JSON.stringify({head,machine},null,2));
  assert.equal(machine.independentHead,true);assert.equal(machine.configured,true);
  assert.deepEqual(machine.configuration.tcp,head.tcpLocalPosition);
  assert.deepEqual(machine.collisionDefinitions.map(d=>d.geometry),head.assembly.nodes.map(n=>n.collisionBoxes));
  if(['AC','BC'].includes(item.kind)){
   const check=await finishTask(await invoke('CamSimulation.CheckCollision',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,pathKind:'Cutting'}));
   writeFileSync(resolve(output,item.kind+'-collision.json'),JSON.stringify(check,null,2));
   state=await read();
   let playback=await invoke('CamSimulation.StartPlayback',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,firstSegment:'0'});
   const until=Date.now()+60000;
   do {assert(Date.now()<until,'Playback timed out');await new Promise(r=>setTimeout(r,100));playback=await invoke('CamSimulation.QueryPlayback',{epoch:playback.epoch,taskId:playback.taskId,throughSegment:'0'});assert(!['Failed','Superseded','Cancelled'].includes(playback.status),playback.error);}while(!playback.segments?.length);
   writeFileSync(resolve(output,item.kind+'-playback.json'),JSON.stringify(playback,null,2));
   assert.deepEqual(playback.headCoverage.tcp,head.tcpLocalPosition);
   await invoke('CamSimulation.CancelTask',{epoch:playback.epoch,taskId:playback.taskId});
  }
  await page.evaluate(async state=>{const {mergeDomainState}=await import('../../../apps/five-axis-cam/webpage/domainActions.mjs');mergeDomainState(lh.scene,state);},state);
  await command('five.simulation.head-open');
  await page.waitForFunction(id=>lh.fiveHeadPreview?.phase==='ready'&&lh.fiveHeadPreview.head?.builtinDefinitionId===id,item.id);
  const preview=await page.evaluate(()=>({canvas:[lh.fiveHeadPreview.canvas.width,lh.fiveHeadPreview.canvas.height],tcp:lh.fiveHeadPreview.head.tcpLocalPosition,parts:lh.fiveHeadPreview.model.children.length}));
  assert(preview.canvas.every(n=>n>0));assert.deepEqual(preview.tcp,head.tcpLocalPosition);
  await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,item.kind+'.png')});
  await page.locator('[data-five-head-collider-field="show"]').check();
  assert.equal(await page.evaluate(()=>lh.fiveHeadPreview.colliderRoot.visible),true);
  await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,item.kind+'-boxes.png')});
  await page.locator('[data-five-head-collider-field="show"]').uncheck();
  report.heads.push({kind:item.kind,id:item.id,tcp:head.tcpLocalPosition,axes:item.headAxes,preview,nativeConfigured:true});
  await command('five.simulation.head-close');await idle();
 }
 await idle();const saved=await read();await closeSaved();
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),report.project);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.lh=getProjectView(__icaxAppShell.getState().activeProjectId);return !!lh.scene?.domain&&!lh.pending;});
 const reopened=await read();assert.equal(hash(preserved(reopened)),report.beforeHash);assert.deepEqual(selected(reopened).headModel,selected(saved).headModel);
 assert.deepEqual(reopened.headInstances,saved.headInstances);report.savedAndReopened=true;
 report.cadAndPathsUnchanged=true;assert.deepEqual(report.errors,[]);
 await closeSaved();report.acceptanceProjectClosed=true;report.passed=true;
}catch(error){report.failure=error.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'native-cef.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
