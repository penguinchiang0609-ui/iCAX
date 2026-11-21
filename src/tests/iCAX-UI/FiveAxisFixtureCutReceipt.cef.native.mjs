import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

const port=Number(process.env.ICAX_RECEIPT_CEF_PORT||9695);assert(port>=9690,'Use an isolated acceptance window');
const output=resolve('output/acceptance/fixture-cut-receipt-20261102/cef');mkdirSync(output,{recursive:true});
const project=resolve(output,`fixture-before-cuts-${Date.now()}.i5cam`);
copyFileSync(resolve('output/acceptance/fixture-cut-receipt-20261102/native/before-cuts.i5cam'),project);
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`),report={passed:false,standaloneCef:true,project,errors:[]};
try{
 const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));page.setDefaultTimeout(180000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{const {findProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.receiptAudit=findProjectView(__icaxAppShell.getState().activeProjectId);return receiptAudit?.scene?.domain&&!receiptAudit.pending;});
 const idle=()=>page.waitForFunction(()=>!receiptAudit.pending&&!__icaxAppShell.getState().pendingCount);
 const read=()=>page.evaluate(()=>receiptAudit.viewportSceneProxy.invoke('CamScene.ReadState',{}));
 const before=await read();assert.equal(before.intentToolpaths.length,0);assert.equal(before.fixtures.length,8);
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));await idle();
 await page.locator('[data-command-id="five.extraction.begin.Faces"]').click();await idle();
 // Feed a real native face reference; face pointer hit testing is not this regression.
 await page.evaluate(async()=>{
  const v=receiptAudit,d=v.scene.domain,placement=d.placements.find(p=>p.id===d.activePlacementId),proxy=v.viewportSceneProxy;
  const sources=await proxy.invoke('CamCut.QuerySources',{expected:d.expected,setupId:d.activeSetupId,placementId:placement.id});
  const {extractionDraft}=await import('../../../apps/five-axis-cam/webpage/extractionActions.mjs');
  extractionDraft(v).extraction.faces=sources.faceUses.filter(f=>String(f.localId)==='1');
  const original=proxy.invoke;window.receiptAuditCalls=[];
  proxy.invoke=async function(method,payload,...rest){const call={method,payload:structuredClone(payload)};receiptAuditCalls.push(call);try{const result=await original.call(this,method,payload,...rest);call.completed=true;return result;}catch(error){call.error=error.message;throw error;}};
  window.receiptAuditCanvas=v.viewport.renderer.domElement;
 });
 // Rerender through the existing scope controls so the button reflects the selection.
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.extraction.scope.inner'));await idle();
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.extraction.scope.all'));await idle();
 await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
 await page.waitForFunction(()=>receiptAudit.scene.domain.intentToolpaths.length===21||receiptAudit.error);
 assert.equal(await page.evaluate(()=>receiptAudit.error),'');
 const created=await read();assert.equal(created.intentToolpaths.length,21);
 assert.deepEqual(await page.evaluate(()=>receiptAudit.scene.domain.expected),created.expected);
 assert.equal(await page.locator('[data-five-intent]').count(),21);
 await page.locator('[data-command-id="five.simulation.head-open"]').click();await idle();
 await page.waitForFunction(()=>receiptAudit.fiveSimulation?.builtinHeads?.length>0);
 const head=await page.evaluate(()=>receiptAudit.fiveSimulation.builtinHeads[0]);
 await page.locator('#five-current-head').selectOption('builtin:'+head.id);
 await idle();await page.waitForFunction(id=>receiptAudit.scene.domain.setups.find(s=>s.id===receiptAudit.scene.domain.activeSetupId).headModel.builtinDefinitionId===id,head.id);
 const after=await read();assert.equal(after.setups[0].headModel.builtinDefinitionId,head.id);
 assert.deepEqual(after.intentToolpaths,created.intentToolpaths);
 for(const key of ['workpieces','placements','fixtures'])assert.deepEqual(after[key],before[key],key);
 assert.equal(await page.evaluate(()=>receiptAudit.fiveSimulation.headError),'');
 assert(await page.evaluate(()=>receiptAuditCanvas===receiptAudit.viewport.renderer.domElement));
 report.calls=await page.evaluate(()=>receiptAuditCalls.filter(c=>['CamCut.Create','CamHead.Configure'].includes(c.method)));
 assert.equal(report.calls.filter(c=>c.method==='CamCut.Create').length,1);assert(report.calls.every(c=>c.completed&&!c.error));
 report.verified={cuts:21,fixtureParts:8,head:head.name,currentState:true,cadAndCutsUnchanged:true,canvasPreserved:true};
 await page.screenshot({path:resolve(output,'created-and-head-selected.png')});report.passed=true;
}catch(error){report.failure=error.stack;throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();console.log(JSON.stringify(report));}
