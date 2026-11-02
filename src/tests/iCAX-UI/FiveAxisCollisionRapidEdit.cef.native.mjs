import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';

const port=Number(process.env.ICAX_COLLISION_TEST_PORT);
if(!Number.isInteger(port)||port<9690)throw Error('Use an independent acceptance runtime, never the user window.');
const output=resolve('output/acceptance/keypose-collision-20261102');mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
page.setDefaultTimeout(20000);const report={passed:false,cases:[]};
try{
 await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.rapidAudit=getProjectView(__icaxAppShell.getState().activeProjectId);});
 assert.match(await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath),/keypose-collision-20261102/);
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));
 // The native yoke fixture intentionally contains only cuts. Prepare neutral
 // rapid geometry explicitly before measuring edit/recheck side effects.
 await page.evaluate(async()=>{
  const v=rapidAudit,sceneProxy=v.viewportSceneProxy,setup=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId);
  if(setup.generatedGeometry.transitions.every(t=>['Generated','Draft'].includes(t.geometryStatus)&&t.points?.length>1))return;
  const {waitForTaskResult}=await import('../../../apps/five-axis-cam/webpage/taskProgressStream.mjs');
  const {readMachiningState,mergeDomainState}=await import('../../../apps/five-axis-cam/webpage/domainActions.mjs');
  const task=await sceneProxy.invoke('CamRapid.Generate',{commandId:crypto.randomUUID(),expected:structuredClone(v.scene.domain.expected),setupId:setup.id,geometryRevision:setup.generatedGeometry.revision,safe:false});
  const finished=await waitForTaskResult({sceneProxy},task,{queryMethod:'CamRapid.QueryStatus',cancelMethod:'CamRapid.CancelTask'});if(finished.status!=='Completed')throw Error(finished.error||finished.status);
  mergeDomainState(v.scene,await readMachiningState({sceneProxy}));
 });
 const idle=()=>page.waitForFunction(()=>!rapidAudit.pending&&!rapidAudit.fiveHistoryBusy&&!rapidAudit.fiveRapid?.request&&!rapidAudit.fiveRapid?.savingPath&&!rapidAudit.fiveRapid?.fileSaving&&!rapidAudit.fiveRapid?.needsFileSave&&!rapidAudit.fiveRapid?.pathEdit?.changed&&!rapidAudit.fiveSimulation?.checking,null,{timeout:120000});
 const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
 await command('five.cam-page.rapid');await command('five.cam-inspector.collision');
 const pair=await page.evaluate(()=>{const t=rapidAudit.scene.domain.setups[0].generatedGeometry.transitions[0];return `${t.from}/${t.to}`;});
 await page.locator(`[data-five-rapid-id="${pair}"]`).click();await idle();
 await page.locator('[data-five-command="five.simulation.collision-check-path"]').click();await idle();
 const adjust=page.locator('[data-five-command^="five.simulation.collision-edit-rapid:"]');
 if(await adjust.count())await adjust.first().click();else await page.locator('[data-five-command="five.rapid.path-edit"]').click();
 await page.waitForFunction(()=>!!rapidAudit.fiveRapid?.pathEdit);await idle();
 await page.locator('[data-five-rapid-field="pathPoint"]').selectOption('0');await idle();
 const baseline=await page.evaluate(()=>({position:rapidAudit.fiveRapid.pathEdit.points[0],direction:rapidAudit.fiveRapid.pathEdit.directions[0],cad:structuredClone(rapidAudit.scene.domain.workpieces),geometry:structuredClone(rapidAudit.scene.domain.setups[0].generatedGeometry)}));
 await page.evaluate(()=>{const proxy=rapidAudit.viewportSceneProxy;window.rapidAuditOriginal=proxy.invoke;window.rapidAuditCalls=[];proxy.invoke=async function(method,payload,...rest){rapidAuditCalls.push({method,payload:structuredClone(payload)});return rapidAuditOriginal.call(this,method,payload,...rest);};});
 const z=page.locator('.cam-info-pane [data-five-rapid-field="pathZ"]');await z.fill(String(baseline.position[2]+2.5));await z.blur();
 await page.waitForFunction(()=>rapidAuditCalls.some(c=>c.method==='CamRapid.EditPath'));await idle();
 const after=await page.evaluate(()=>({calls:rapidAuditCalls,error:rapidAudit.error,saveError:rapidAudit.fiveWorkpieceSaveError,cad:rapidAudit.scene.domain.workpieces,geometry:rapidAudit.scene.domain.setups[0].generatedGeometry}));
 assert.equal(after.error||'','');assert.equal(after.saveError||'','');assert.equal(after.calls.filter(c=>c.method==='CamRapid.EditPath').length,1);assert(!after.calls.some(c=>/\.Generate$|CheckCollision|AutoRepair/.test(c.method)));
 assert.deepEqual(after.cad,baseline.cad);assert.deepEqual(after.geometry.operations,baseline.geometry.operations);
 const changed=after.geometry.transitions.find(t=>`${t.from}/${t.to}`===pair);assert.equal(changed.points[0][2],baseline.position[2]+2.5);assert.equal(changed.manualPath.start.point[2],baseline.position[2]+2.5);
 for(const original of baseline.geometry.transitions.filter(t=>`${t.from}/${t.to}`!==pair)){const current=after.geometry.transitions.find(t=>t.from===original.from&&t.to===original.to);assert.deepEqual(current.points,original.points);assert.deepEqual(current.directions,original.directions);}
 report.cases.push('Collision inspector endpoint XYZ autosaves one local rapid edit without generation, recheck or changes to cuts, CAD and other rapid geometry');
 await command('edit.undo');assert.equal(await z.inputValue(),String(baseline.position[2]));await command('edit.redo');assert.equal(await z.inputValue(),String(baseline.position[2]+2.5));
 report.cases.push('Endpoint edit survives actual Undo and Redo while its editor stays usable');
 const before=await page.evaluate(()=>rapidAuditCalls.length);await page.locator('[data-five-command="five.simulation.collision-check-path"]').click();await idle();
 const checked=await page.evaluate(begin=>({calls:rapidAuditCalls.slice(begin),report:rapidAudit.fiveSimulation.collisionReport,error:rapidAudit.error,message:rapidAudit.fiveSimulation.message}),before);
 assert.equal(checked.error||'','');assert.equal(checked.message||'','');assert(!checked.calls.some(c=>/\.Generate$|AutoRepair/.test(c.method)));
 const calls=checked.calls.filter(c=>c.method==='CamSimulation.CheckCollision');assert.equal(calls.length,1);assert.equal(`${calls[0].payload.from}/${calls[0].payload.to}`,pair);assert.equal(await page.locator('dialog[open]').count(),0);
 if(checked.report.keyposeChecks?.some(row=>row.status==='Collision'&&`${row.location?.from}/${row.location?.to}`===pair))assert.match(await page.locator(`[data-five-rapid-id="${pair}"]`).innerText(),/检测到碰撞/);
 report.cases.push('Explicit rapid recheck checks this pair only, without a modal dialog or generation');report.passed=true;
 await page.screenshot({path:resolve(output,'rapid-edit.png')});
}catch(error){report.error=error.stack;report.ui=await page.evaluate(()=>({error:rapidAudit.error,saveError:rapidAudit.fiveWorkpieceSaveError,message:rapidAudit.fiveSimulation?.message,rapid:rapidAudit.fiveRapid?.id})).catch(()=>null);await page.screenshot({path:resolve(output,'rapid-edit-failure.png')}).catch(()=>{});process.exitCode=1;}
finally{await page.evaluate(()=>{if(window.rapidAuditOriginal)rapidAudit.viewportSceneProxy.invoke=rapidAuditOriginal;}).catch(()=>{});writeFileSync(resolve(output,'rapid-edit.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
