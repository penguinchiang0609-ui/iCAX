import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
const require=createRequire(import.meta.url),{chromium}=require('playwright');
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/collision-inspector-20261102');mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_URL||'http://127.0.0.1:9682'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
page.setDefaultTimeout(30000);const errors=[],report={passed:false,realNative:true,standaloneCef:true,errors,cases:[]};page.on('pageerror',e=>errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!collisionInspectorAudit.pending&&!collisionInspectorAudit.fiveToolAxisRequest&&!collisionInspectorAudit.fiveProcessRead,undefined,{timeout:120000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>collisionInspectorAudit.error),'',id);};
const immutable=d=>JSON.stringify({workpieces:d.workpieces,placements:d.placements,fixtures:d.fixtures,fixtureUnits:d.fixtureUnits,cuts:d.intentToolpaths,geometry:d.setups.find(s=>s.id===d.activeSetupId).generatedGeometry});
try{
 const project=resolve(process.env.ICAX_COLLISION_PROJECT||resolve(output,'Pillar-碰撞界面验证.i5cam'));
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready'&&!__icaxAppShell.getState().pendingCount);
 if((await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath)).replaceAll('\\','/')!==project.replaceAll('\\','/')){
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.waitForFunction(path=>!__icaxAppShell.getState().pendingCount&&__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/')===path,project.replaceAll('\\','/'),{timeout:120000});
 }
 await page.waitForFunction(async()=>{if(!window.__icaxAppShell||__icaxAppShell.getState().startupPhase!=='ready'||__icaxAppShell.getState().pendingCount)return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.collisionInspectorAudit=getProjectView(__icaxAppShell.getState().activeProjectId);return !!collisionInspectorAudit.scene?.domain&&!collisionInspectorAudit.pending;},null,{timeout:120000});
 const read=()=>page.evaluate(()=>collisionInspectorAudit.viewportSceneProxy.invoke('CamScene.ReadState',{})),before=await read(),history=await page.evaluate(()=>collisionInspectorAudit.viewportSceneProxy.getUndoRedoState());
 await page.locator('[data-tab-id="toolpath"]').click();await command('five.tool.collision');
 await page.waitForSelector('[data-five-collision-inspector]');assert.equal(await page.locator('[data-five-collision-dialog],[data-five-key="collision-dialog"]').count(),0);
 await page.waitForFunction(()=>!collisionInspectorAudit.fiveSimulation?.checking,undefined,{timeout:360000});
 const oracle=await page.evaluate(async()=>{
  const {collisionPathResults}=await import('../../../apps/five-axis-cam/webpage/collisionPathResults.mjs'),r=collisionPathResults(collisionInspectorAudit);
  return {validity:r.report?.validity,total:r.report?.segments.length,collisionCount:r.report?.collisionCount,paths:[...r.paths].map(([path,segments])=>({path,indices:segments.map(s=>Number(s.index)),collisions:segments.filter(s=>s.status==='Collision').length}))};
 });
 assert.equal(oracle.validity,'Current');assert.ok(oracle.paths.some(p=>p.path.startsWith('cut:')&&p.collisions>0));assert.ok(oracle.paths.some(p=>p.path.startsWith('rapid:')&&p.collisions>0));report.oracle=oracle;
 for(const kind of ['cut','rapid']){
  await command('five.cam-page.'+(kind==='cut'?'cutting':'rapid'));await command('five.cam-inspector.collision');
  const expected=oracle.paths.filter(p=>p.path.startsWith(kind+':')).map(p=>p.path.slice(kind.length+1)).sort();
  const marked=await page.locator(kind==='cut'?'[data-five-intent]:has([data-five-collision-warning])':'[data-five-rapid-id]:has([data-five-collision-warning])').evaluateAll((nodes,kind)=>nodes.map(n=>n.getAttribute(kind==='cut'?'data-five-intent':'data-five-rapid-id')).sort(),kind);assert.deepEqual(marked,expected);
  const examples=oracle.paths.filter(p=>p.path.startsWith(kind+':')&&p.collisions>0).slice(0,2);
  for(const example of examples){
   const id=example.path.slice(kind.length+1);await page.locator(kind==='cut'?`[data-five-intent="${id}"]`:`[data-five-rapid-id="${id}"]`).click();await idle();
   const records=await page.locator('[data-five-command^="five.simulation.collision-result:"]').evaluateAll(nodes=>nodes.map(n=>Number(n.dataset.fiveCommand.split(':')[1])));assert.ok(records.length>0);assert.ok(records.every(index=>example.indices.includes(index)));
   await page.locator(`[data-five-command="five.simulation.collision-result:${records.at(-1)}"]`).click();await idle();
   const pose=await page.evaluate(async path=>{
    const v=collisionInspectorAudit,s=v.fiveSimulation,{collisionDiagnosis}=await import('../../../apps/five-axis-cam/webpage/collisionDiagnostics.mjs'),{collisionPathKey}=await import('../../../apps/five-axis-cam/webpage/collisionPathResults.mjs'),d=collisionDiagnosis(v,s);
    return {path:collisionPathKey(d?.frame),index:d?.index,cursor:s.cursor,visible:s.group?.visible,head:s.group?.position.toArray(),expected:d?.pose.position.toArray(),rotation:s.group?.quaternion.toArray(),expectedRotation:d?.pose.rotation.toArray(),overlay:s.collisionDiagnosticOverlay?.userData,diagnosis:document.querySelector('[data-collision-segment]')?.dataset.collisionSegment};
   },example.path);
   assert.equal(pose.path,example.path);assert.equal(pose.index,records.at(-1));assert.equal(Number(pose.diagnosis),pose.index);assert.equal(pose.visible,true);assert.deepEqual(pose.head,pose.expected);assert.deepEqual(pose.rotation,pose.expectedRotation);assert.equal(pose.overlay.segmentIndex,pose.index);assert.equal(pose.overlay.poseSource,'NativeCollisionWitness');report.cases.push(pose);
  }
  await page.screenshot({path:resolve(output,kind==='cut'?'切割刀路-碰撞详情.png':'空移刀路-碰撞详情.png')});
 }
 // Hold a real native refresh; continue scrolling after the request was sent.
 await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{height:340px!important;display:block!important;overflow:auto!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;min-height:1100px}.five-path-list{height:180px!important;flex:none!important;overflow:auto!important}.five-path-list::after{content:"";display:block;min-height:1000px}'});
 await page.evaluate(()=>{
  const v=collisionInspectorAudit,proxy=v.viewportSceneProxy,original=proxy.invoke;window.collisionRefreshProbe={proxy,original,events:0};const probe=collisionRefreshProbe;
  proxy.invoke=async function(method,...args){const result=await original.call(this,method,...args);if(method==='CamScene.ReadState'&&!probe.heldOnce){probe.heldOnce=true;probe.held=true;await new Promise(resolve=>probe.release=resolve);probe.held=false;}return result;};
  probe.input=document.querySelector('[data-five-simulation-field="threshold"]');probe.canvas=v.viewport.renderer.domElement;probe.search=document.querySelector('[data-five-search]');probe.input.addEventListener('collision-probe',()=>probe.events++);void __icaxAppShell.executeRibbonCommand('five.refresh');
 });
 await page.waitForFunction(()=>collisionRefreshProbe.held);
 const scrolls=await page.evaluate(()=>{const p=collisionRefreshProbe;p.input.focus({preventScroll:true});p.input.setSelectionRange(0,2);return ['.cam-context-pane','.cam-info-pane','.five-path-list','.five-collision-issue-list'].map((selector,i)=>{const n=document.querySelector(selector);if(!n)return null;n.scrollTop=55+i*20;return n.scrollTop;});});
 await page.evaluate(()=>collisionRefreshProbe.release());await idle();
 report.interaction=await page.evaluate(()=>{const p=collisionRefreshProbe;p.proxy.invoke=p.original;p.input.dispatchEvent(new Event('collision-probe'));return {inputSame:p.input===document.querySelector('[data-five-simulation-field="threshold"]'),canvasSame:p.canvas===collisionInspectorAudit.viewport.renderer.domElement,searchSame:p.search===document.querySelector('[data-five-search]'),focused:document.activeElement===p.input,selection:[p.input.selectionStart,p.input.selectionEnd],events:p.events,scrolls:['.cam-context-pane','.cam-info-pane','.five-path-list','.five-collision-issue-list'].map(s=>document.querySelector(s)?.scrollTop??null)};});
 assert.deepEqual(report.interaction,{inputSame:true,canvasSame:true,searchSame:true,focused:true,selection:[0,2],events:1,scrolls});
 await command('five.cam-inspector.properties');assert.equal(await page.locator('[data-five-collision-inspector]').count(),0);assert.equal(await page.evaluate(()=>document.activeElement===collisionRefreshProbe.input),false);assert.equal(await page.evaluate(()=>collisionInspectorAudit.fiveSimulation.collisionDiagnosticOverlay),null);
 assert.equal(immutable(await read()),immutable(before));assert.deepEqual(await page.evaluate(()=>collisionInspectorAudit.viewportSceneProxy.getUndoRedoState()),history);assert.deepEqual(errors,[]);report.passed=true;
}catch(error){report.error=error.stack;throw error;}finally{writeFileSync(resolve(output,'inspector-report.json'),JSON.stringify(report,null,2));await browser.close();}
