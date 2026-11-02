import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
const require=createRequire(import.meta.url),{chromium}=require('playwright');
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/local-update-20261102');mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_URL||'http://127.0.0.1:9686'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
page.setDefaultTimeout(30000);const report={passed:false,standaloneCef:true,realNative:true,cases:[]},errors=[];
page.on('pageerror',e=>errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!selectedCollisionAudit.pending&&!selectedCollisionAudit.fiveToolAxisRequest&&!selectedCollisionAudit.fiveAxisPointEdit?.loading,undefined,{timeout:120000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>selectedCollisionAudit.error),'',id);};
try{
 await page.waitForFunction(async()=>{const a=window.__icaxAppShell?.getState();if(!a||a.pendingCount)return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const v=getProjectView(a.activeProjectId),d=v?.scene?.domain;if(d?.setups?.find(s=>s.id===d.activeSetupId)?.generatedGeometry?.operations?.length!==21)return false;window.selectedCollisionAudit=v;return true;},null,{timeout:120000});
 await page.waitForLoadState('networkidle');
 const initial=await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.selectedCollisionAudit=getProjectView(__icaxAppShell.getState().activeProjectId);const v=selectedCollisionAudit,d=v.scene.domain,s=d.setups.find(s=>s.id===d.activeSetupId);window.selectedCollisionCalls=[];const proxy=v.viewportSceneProxy,original=proxy.invoke;window.selectedCollisionOriginal=original;proxy.invoke=async function(method,payload,...rest){const call={method,payload:structuredClone(payload)};selectedCollisionCalls.push(call);const result=await original.call(this,method,payload,...rest);call.result=structuredClone(result);return result;};return {state:structuredClone(d),setupId:s.id,id:s.generatedGeometry.operations.find(op=>op.order==='18').intentId,history:await proxy.getUndoRedoState()};});
 assert.equal(initial.state.setups.find(s=>s.id===initial.setupId).simulationCheck.validity,'Current','Start with an actual collision report');
 await page.locator('[data-tab-id="toolpath"]').click();await command('five.cam-page.cutting');await page.locator(`[data-five-intent="${initial.id}"]`).click();await idle();await command('five.cam-inspector.points');
 const field=page.getByRole('textbox',{name:'关键点 欧拉角 X',exact:true});await field.waitFor();
 const begin=await page.evaluate(()=>selectedCollisionCalls.length);
 // Both endpoint key poses can be saved. Saving/inspecting must do no planning.
 for(const endpoint of ['start','end']){
  await page.locator('[data-five-point-field="point"]').selectOption(endpoint);await idle();await field.fill(String(Number(await field.inputValue())+.2));await field.press('Enter');await idle();
  assert.equal(await page.evaluate(()=>selectedCollisionAudit.fiveAxisPointEdit.error),'');
 }
 await page.waitForTimeout(1100);
 const beforeCheck=await page.evaluate(begin=>selectedCollisionCalls.slice(begin).filter(c=>/\.Generate$|CamSimulation\.(CheckCollision|AutoRepairCollision)$/.test(c.method)),begin);assert.deepEqual(beforeCheck,[]);
 report.cases.push('Manual start and end key-pose edits start no generation, collision check or automatic repair');
 await command('five.cam-inspector.collision');assert.equal(await page.getByText('刀路已过期，请重新生成。',{exact:true}).count(),0);
 assert.deepEqual(await page.evaluate(begin=>selectedCollisionCalls.slice(begin).filter(c=>/\.Generate$|CamSimulation\.(CheckCollision|AutoRepairCollision)$/.test(c.method)),begin),[]);
 // Hold a real selected-path acceptance reply while scrolling/focusing.
 await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{height:420px!important;overflow:auto!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;min-height:900px}'});
 await page.evaluate(()=>{const v=selectedCollisionAudit;window.selectedCollisionNodes={canvas:v.viewport.renderer.domElement,search:document.querySelector('[data-five-search]')};selectedCollisionNodes.search.addEventListener('selected-check-probe',()=>selectedCollisionNodes.events=(selectedCollisionNodes.events||0)+1);});
 await page.locator('[data-five-command="five.simulation.collision-check-path"]').click();
 await page.waitForFunction(()=>!selectedCollisionAudit.fiveSimulation.checking,undefined,{timeout:240000});await idle();
 const after=await page.evaluate(()=>{const v=selectedCollisionAudit,d=v.scene.domain,s=d.setups.find(s=>s.id===d.activeSetupId),n=selectedCollisionNodes;n.search.dispatchEvent(new Event('selected-check-probe'));return {state:structuredClone(d),message:v.fiveSimulation.message,preparation:v.fiveSimulation.selectedPreparation,report:s.simulationCheck,calls:selectedCollisionCalls,interaction:{canvasSame:n.canvas===v.viewport.renderer.domElement,searchSame:n.search===document.querySelector('[data-five-search]'),events:n.events}};});
 assert.equal(after.message,'');assert.equal(after.report.validity,'Current');assert.equal(after.report.checkMode,'SelectedPath');assert.equal(after.report.checkedIntentId,initial.id);
 const generated=after.calls.slice(begin).filter(c=>c.method==='CamPlan.Generate');assert.equal(generated.length,1);assert.equal(generated[0].payload.intentId,initial.id);assert.equal(Number(after.preparation.calculatedCount),1);assert.equal(Number(after.preparation.reusedCount),20);
 const checked=after.calls.slice(begin).filter(c=>c.method==='CamSimulation.CheckCollision');assert.equal(checked.length,1);assert.equal(checked[0].payload.intentId,initial.id);assert.ok(!after.calls.slice(begin).some(c=>c.method==='CamRapid.Generate'||c.method==='CamSimulation.AutoRepairCollision'));
 const beforeGeometry=initial.state.setups.find(s=>s.id===initial.setupId).generatedGeometry,afterGeometry=after.state.setups.find(s=>s.id===initial.setupId).generatedGeometry;
 for(const op of beforeGeometry.operations.filter(op=>op.intentId!==initial.id)){const current=afterGeometry.operations.find(x=>x.intentId===op.intentId);assert.deepEqual(current.parts,op.parts);assert.deepEqual(current.curveRevision,op.curveRevision);}
 for(const key of ['workpieces','placements','fixtures','fixtureUnits'])assert.equal(JSON.stringify(after.state[key]),JSON.stringify(initial.state[key]),key);
 assert.deepEqual(after.interaction,{canvasSame:true,searchSame:true,events:1});
 const indexSet=new Set(after.report.segments.filter(s=>s.status!=='Unchecked'&&after.report.frames[Number(s.index)].intentId===initial.id).map(s=>Number(s.index)));
 assert.ok(indexSet.size>0);assert.ok(Number(after.report.checkedSegmentCount)<after.report.segments.length);
 report.cases.push('Explicit recheck prepares one selected cut, reuses 20 cut geometries, checks only that cut, and runs no rapid generation or automatic repair');
 report.preparation=after.preparation;report.check={count:after.report.checkedSegmentCount,reused:after.report.reusedSegmentCount,total:after.report.segments.length};report.interaction=after.interaction;
 assert.deepEqual(errors,[]);report.passed=true;await page.screenshot({path:resolve(output,'手动调端点后-只检测本条.png')});
}catch(error){report.error=error.stack;throw error;}finally{await page.evaluate(()=>{if(window.selectedCollisionAudit&&window.selectedCollisionOriginal)selectedCollisionAudit.viewportSceneProxy.invoke=selectedCollisionOriginal;}).catch(()=>{});writeFileSync(resolve(output,'selected-check-report.json'),JSON.stringify(report,null,2));await browser.close();}
