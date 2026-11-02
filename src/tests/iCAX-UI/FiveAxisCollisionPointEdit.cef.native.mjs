import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';

const port=Number(process.env.ICAX_COLLISION_TEST_PORT);
if(!Number.isInteger(port)||port<9690)throw Error('Use the independent collision acceptance window, never the user window.');
const output=resolve('output/acceptance/keypose-collision-20261102');mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
page.setDefaultTimeout(120000);const report={passed:false,cases:[]};
try{
 await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.pointAudit=getProjectView(__icaxAppShell.getState().activeProjectId);});
 assert.match(await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath),/keypose-collision-20261102/);
 const idle=()=>page.waitForFunction(()=>!pointAudit.pending&&!pointAudit.fiveHistoryBusy&&!pointAudit.fiveToolAxisRequest&&!pointAudit.fiveAxisPointEdit?.loading&&!pointAudit.fiveAxisPointEdit?.request&&!pointAudit.fiveAxisPointSaving&&!pointAudit.fiveSimulation?.checking);
 const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
 const ids=await page.evaluate(()=>pointAudit.scene.domain.setups[0].generatedGeometry.operations.map(o=>o.intentId));
 if(!await page.evaluate(id=>pointAudit.fiveAxisUI?.selectedIntentIds?.includes(id),ids[0]))await page.locator(`[data-five-intent="${ids[0]}"]`).click();await idle();
 await command('five.cam-inspector.collision');
 await page.locator('[data-five-command^="five.simulation.collision-edit-point:"]').first().click();
 await page.waitForFunction(id=>pointAudit.fiveAxisPointEdit?.id===id&&pointAudit.fiveAxisPointEdit?.axis&&pointAudit.fiveAxisPointEdit?.positionValues&&!pointAudit.fiveAxisPointEdit.loading,ids[0]);await idle();
 const baseline=await page.evaluate(()=>({id:pointAudit.fiveAxisPointEdit.id,pointId:pointAudit.fiveAxisPointEdit.pointId,position:pointAudit.fiveAxisPointEdit.positionValues.map(Number),direction:pointAudit.fiveAxisPointEdit.values.map(Number),cad:structuredClone(pointAudit.scene.domain.workpieces),operations:structuredClone(pointAudit.scene.domain.setups[0].generatedGeometry.operations)}));
 await page.evaluate(()=>{const proxy=pointAudit.viewportSceneProxy;window.pointAuditOriginal=proxy.invoke;window.pointAuditCalls=[];proxy.invoke=async function(method,payload,...rest){pointAuditCalls.push({method,payload:structuredClone(payload)});return pointAuditOriginal.call(this,method,payload,...rest);};});
 const field=page.locator('[data-five-point-field="position-2"]');await field.fill(String(baseline.position[2]+2.5));await page.locator('[data-five-command="five.points.apply"]').click();await idle();
 const changed=await page.evaluate(()=>({position:pointAudit.fiveAxisPointEdit.positionValues.map(Number),calls:pointAuditCalls,cad:pointAudit.scene.domain.workpieces,operations:pointAudit.scene.domain.setups[0].generatedGeometry.operations,error:pointAudit.error,saveError:pointAudit.fiveWorkpieceSaveError}));
 assert.equal(changed.error||'','');assert.equal(changed.saveError||'','');assert.equal(changed.position[2],baseline.position[2]+2.5);
 assert.equal(changed.calls.filter(c=>c.method==='CamCut.EditToolAxisPoint').length,1);assert(!changed.calls.some(c=>/\.Generate$|CheckCollision|AutoRepair/.test(c.method)));
 assert.deepEqual(changed.cad,baseline.cad);for(const op of baseline.operations.filter(o=>o.intentId!==baseline.id))assert.deepEqual(changed.operations.find(o=>o.intentId===op.intentId),op);
 report.cases.push('Adjusting an endpoint XYZ in the collision inspector saves one native edit without calculation or changing CAD and other cuts');
 await field.blur();await command('edit.undo');assert.deepEqual(await page.evaluate(()=>pointAudit.fiveAxisPointEdit.positionValues.map(Number)),baseline.position);
 await command('edit.redo');assert.equal(await field.inputValue(),String(baseline.position[2]+2.5));
 report.cases.push('The same endpoint edit can be undone and redone through actual history buttons');
 const before=await page.evaluate(()=>pointAuditCalls.length);await page.locator('[data-five-command="five.simulation.collision-check-path"]').click();await idle();
 const checked=await page.evaluate(begin=>({calls:pointAuditCalls.slice(begin),report:pointAudit.fiveSimulation.collisionReport,error:pointAudit.error,message:pointAudit.fiveSimulation.message}),before);
 assert.equal(checked.error||'','');assert.equal(checked.message||'','');assert.equal(checked.report.checkMode,'SelectedPath');assert.equal(checked.report.checkedIntentId,baseline.id);
 const generates=checked.calls.filter(c=>c.method==='CamPlan.Generate');assert.equal(generates.length,1);assert.equal(generates[0].payload.intentId,baseline.id);
 const checks=checked.calls.filter(c=>c.method==='CamSimulation.CheckCollision');assert.equal(checks.length,1);assert.equal(checks[0].payload.intentId,baseline.id);
 report.cases.push('Explicit recheck projects and checks only the edited cut');report.passed=true;
 await page.screenshot({path:resolve(output,'point-edit.png')});
}catch(error){report.error=error.stack;report.ui=await page.evaluate(()=>({error:pointAudit.error,pointError:pointAudit.fiveAxisPointEdit?.error,message:pointAudit.fiveSimulation?.message})).catch(()=>null);await page.screenshot({path:resolve(output,'point-edit-failure.png')}).catch(()=>{});process.exitCode=1;}
finally{await page.evaluate(()=>{if(window.pointAuditOriginal)pointAudit.viewportSceneProxy.invoke=pointAuditOriginal;}).catch(()=>{});writeFileSync(resolve(output,'point-edit.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
