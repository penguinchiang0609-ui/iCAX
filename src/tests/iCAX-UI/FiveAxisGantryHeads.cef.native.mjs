import assert from 'node:assert/strict';
import {chromium} from './node_modules/playwright/index.mjs';
import {copyFileSync,existsSync,readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';

const output=resolve('output/tests/gantry-heads-20261101/cef');mkdirSync(output,{recursive:true});
const models=JSON.parse(readFileSync('src/apps/five-axis-cam/resources/machines/gantry-heads/catalog.json','utf8'));
const report={passed:false,standaloneCef:true,packagedRuntime:true,cases:[],errors:[]};
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9637');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));assert.ok(page);
page.on('pageerror',e=>report.errors.push(e.message));page.setDefaultTimeout(45000);
const settle=()=>page.waitForFunction(()=>{const s=hv.fiveMachineParameterEdits;return !hv.pending&&!hv.fiveMachine?.busy&&!__icaxAppShell.getState().pendingCount&&!s?.request&&!s?.fileSaving&&![...(s?.entries.values()||[])].some(e=>e.timer||e.ready);},null,{timeout:60000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await settle();assert.equal(await page.evaluate(()=>hv.error||hv.fiveMachine?.error||''),'',id);};
const delta=(a,b)=>Math.max(...a.map((x,i)=>Math.abs(x-b[i])));
const matrices=()=>page.evaluate(()=>{const m=hv.scene.machines[0],tool=m.elements.find(e=>e.hasTool),support=m.elements.find(e=>e.workSupport);const matrix=id=>{const object=hv.viewport.sceneObjects.get(id);object?.updateWorldMatrix(true,false);return object?.matrixWorld.toArray();};return {tool:matrix(tool.entityId),support:matrix(support.entityId)};});
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});await page.setViewportSize({width:1600,height:1000});
 for(const model of models){
  const project=resolve(output,model.model+'-desktop-'+Date.now()+'.i5cam'),source=resolve('output/tests/gantry-heads-20261101/native',model.model+'.i5cam');assert.ok(existsSync(source));copyFileSync(source,project);
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.waitForFunction(async path=>{if(__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/')!==path.replaceAll('\\','/'))return false;const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.hv=getProjectView(__icaxAppShell.getState().activeProjectId);return hv?.scene?.machines?.length===1&&!hv.pending&&!__icaxAppShell.getState().pendingCount;},project);
  await settle();assert.equal(await page.evaluate(()=>hv.scene.machineDefinitions.length),9);
  // The native fixture intentionally contains an obstacle inside the fork.
  // Remove it from this isolated copy before exercising clear physical jogs.
  await page.evaluate(async()=>{for(const fixture of [...hv.scene.domain.fixtures]){const state=await hv.viewportSceneProxy.invoke('CamScene.ReadState',{});await hv.viewportSceneProxy.invoke('CamFixture.Remove',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,fixtureId:fixture.id});}});
  await command('five.refresh');await page.locator('[data-tab-id="simulation"]').click();await settle();await command('five.tool.machine-simulation');
  await page.waitForFunction(()=>hv.viewport.sceneObjects.size>=10&&document.querySelector('.five-machine-jog-input'));
  const facts=await page.evaluate(()=>{const m=hv.scene.machines[0],head=hv.scene.domain.setups.find(s=>s.id===hv.scene.domain.activeSetupId).headModel;return {joints:m.joints.filter(j=>j.type!=='fixed').map(j=>({id:j.entityId,name:j.jointName,position:j.position})),headKind:head.assembly?.kind,parts:head.assembly?.nodes.length,source:head.assembly?.source};});
  assert.equal(facts.joints.length,5);assert.equal(facts.headKind,model.kind);assert.equal(facts.parts,model.kind==='3+2'?1:2);assert.equal(facts.source,'MachineInstance');
  const moves=[];
  for(const name of [...model.headAxes,...model.workpieceAxes]){
   const joint=facts.joints.find(j=>j.name===name);assert.ok(joint);const before=await matrices();assert.ok(before.tool&&before.support);
   const plus=page.locator(`[data-five-command="five.machine.jog-plus:${joint.id}"]`);await plus.scrollIntoViewIfNeeded();await plus.click();await settle();
   const actual=await page.evaluate(id=>hv.scene.machines[0].joints.find(j=>j.entityId===id).position,joint.id);assert.ok(Math.abs(actual-joint.position-Math.PI/180)<1e-7,name);
   const after=await matrices(),toolDelta=delta(before.tool,after.tool),supportDelta=delta(before.support,after.support),headAxis=model.headAxes.includes(name);
   assert.ok(headAxis?toolDelta>1e-8:toolDelta<1e-8,`${model.kind} ${name} tool ${toolDelta}`);assert.ok(headAxis?supportDelta<1e-8:supportDelta>1e-8,`${model.kind} ${name} support ${supportDelta}`);
   moves.push({axis:name,position:actual,toolDelta,supportDelta});
  }
  await page.evaluate(()=>{hv.viewport.setViewDirection([1,-1.6,.75]);hv.viewport.fitViewToViewport(1.05);document.querySelector('.cam-context-pane').scrollTop=0;document.querySelector('.cam-info-pane').scrollTop=0;});await page.waitForTimeout(120);await page.screenshot({path:resolve(output,model.model+'-machine.png')});
  await page.locator('[data-tab-id="toolpath"]').click();await settle();await command('five.simulation.head-open');
  const dialog=page.getByRole('dialog',{name:'切割头',exact:true});await dialog.waitFor();assert.ok((await dialog.locator('.five-head-name').innerText()).includes(model.kind));assert.equal(await dialog.locator('[data-five-simulation-field="builtin-head"]').isDisabled(),true);
  await command('five.simulation.head-close');
  report.cases.push({model:model.model,kind:model.kind,project,...facts,moves});
 }
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
