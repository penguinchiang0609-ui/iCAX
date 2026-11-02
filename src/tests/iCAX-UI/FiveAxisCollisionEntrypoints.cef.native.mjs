import assert from 'node:assert/strict';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';

const port=Number(process.env.ICAX_COLLISION_TEST_PORT);if(!Number.isInteger(port)||port<9690)throw Error('Specify a dedicated acceptance runtime port (9690 or above); never use the user window.');
const output=resolve(process.env.ICAX_COLLISION_TEST_OUTPUT||'output/acceptance/keypose-collision-20261102');mkdirSync(output,{recursive:true});
// A fresh archive must not replay a previous run's sidecar journal, whose
// entity identities belong to a different native fixture.
const project=resolve(output,`collision-entrypoints-${Date.now()}.i5cam`);
const alreadyOpen=process.env.ICAX_COLLISION_ENTRIES_ALREADY_OPEN==='1';
if(!alreadyOpen)copyFileSync(resolve('output/acceptance/selected-collision-20261102/full-head/yoke-contact.i5cam'),project);
const browser=await chromium.connectOverCDP('http://127.0.0.1:'+port);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
page.setDefaultTimeout(120000);const report={passed:false,standaloneCef:true,realNativeChecks:true,cases:[]},errors=[];
page.on('pageerror',e=>errors.push(e.message));
try{
 await page.setViewportSize({width:1440,height:1000});
 await page.waitForFunction(()=>__icaxAppShell.getState().startupPhase==='ready');
 if(!alreadyOpen)await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{
  const a=__icaxAppShell.getState();if(a.pendingCount||!a.activeProjectId)return false;
  const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
  window.entryAudit=getProjectView(a.activeProjectId);return !!entryAudit.scene?.domain;
 });
 const idle=()=>page.waitForFunction(()=>!entryAudit.pending&&!entryAudit.fiveSimulation?.checking&&!entryAudit.fiveToolAxisRequest);
 const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
 await page.evaluate(()=>{
  for(const style of document.querySelectorAll('style'))if(style.textContent.includes('.cam-context-pane::after,.cam-info-pane::after,.five-collision-issue-list::after'))style.remove();
  for(const selector of ['.cam-context-pane','.cam-info-pane','.five-collision-issue-list']){const pane=document.querySelector(selector);if(pane)pane.scrollTop=0;}
 });
 await page.evaluate(()=>{
  const proxy=entryAudit.viewportSceneProxy,original=proxy.invoke;
  window.entryAuditCalls=[];window.entryAuditOriginal=original;
  proxy.invoke=async function(method,payload,...rest){
   const call={method,payload:structuredClone(payload)};entryAuditCalls.push(call);
   const result=await original.call(this,method,payload,...rest);
   if(method==='CamSimulation.CheckCollision'&&window.entryAuditHold){
    window.entryAuditHold=false;window.entryAuditHeld=true;
    await new Promise(resolve=>window.entryAuditRelease=resolve);window.entryAuditHeld=false;
   }return result;
  };
 });
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));await command('five.cam-page.cutting');
 const intents=await page.evaluate(()=>entryAudit.scene.domain.setups[0].generatedGeometry.operations.map(o=>o.intentId));assert(intents.length>1);
 if(!await page.evaluate(id=>entryAudit.fiveAxisUI?.selectedIntentIds?.includes(id),intents[0]))await page.locator(`[data-five-intent="${intents[0]}"]`).click();await idle();
 await command('five.cam-inspector.properties');
 const begin=await page.evaluate(()=>entryAuditCalls.length);
 await page.evaluate(()=>window.entryAuditHold=true);
 await page.locator('[data-command-id="five.simulation.collision-check"]').click();
 await page.waitForFunction(()=>window.entryAuditHeld);
 assert.equal(await page.locator('[data-five-collision-inspector]').count(),1);
 assert.equal(await page.locator('[data-five-floating="collision-check"]').count(),0);
 assert.equal(await page.locator('dialog[open]').count(),0);
 assert.match(await page.locator('.five-collision-inspector-heading').innerText(),/当前装夹全部刀路/);
 assert.equal(await page.locator('[data-five-command="five.simulation.collision-cancel"]').isEnabled(),true);
 const globalRequest=await page.evaluate(begin=>entryAuditCalls.slice(begin).find(c=>c.method==='CamSimulation.CheckCollision')?.payload,begin);
 assert(globalRequest);for(const field of ['intentId','from','to'])assert(!Object.hasOwn(globalRequest,field));
 // Hold only the real native reply, then continue scrolling while it is pending.
 const scrollStyle=await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{height:400px!important;overflow:auto!important}.cam-context-pane::after,.cam-info-pane::after,.five-collision-issue-list::after{content:"";display:block;min-height:1400px;flex:0 0 1400px}.five-collision-issue-list{max-height:150px!important;overflow:auto!important}'});
 await page.locator('[data-five-search]').fill('圆');
 const interactionBefore=await page.evaluate(()=>{
  const n=document.querySelector('[data-five-search]');
  window.entryAuditEvents=0;
  window.entryAuditNodes={input:n,threshold:document.querySelector('[aria-label="碰撞阈值"]'),canvas:entryAudit.viewport.renderer.domElement};n.addEventListener('audit-probe',()=>window.entryAuditEvents=(window.entryAuditEvents||0)+1);
  n.focus({preventScroll:true});n.setSelectionRange(0,2);
  const selectors=['.cam-context-pane','.cam-info-pane','.five-collision-issue-list'];
  selectors.forEach(s=>document.querySelector(s).scrollTop=40);
  const scroll=selectors.map((s,i)=>{const p=document.querySelector(s);p.scrollTop=100+i*40;return p.scrollTop;});
  return {selection:[n.selectionStart,n.selectionEnd],scroll};
 });assert(interactionBefore.scroll.every(n=>n>0));
 await page.evaluate(()=>entryAuditRelease());await idle();
 const interaction=await page.evaluate(()=>{
  const n=document.querySelector('[data-five-search]');n.dispatchEvent(new Event('audit-probe'));
  return {inputSame:n===entryAuditNodes.input,thresholdSame:document.querySelector('[aria-label="碰撞阈值"]')===entryAuditNodes.threshold,canvasSame:entryAudit.viewport.renderer.domElement===entryAuditNodes.canvas,focused:document.activeElement===n,selection:[n.selectionStart,n.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.five-collision-issue-list'].map(s=>document.querySelector(s).scrollTop),events:window.entryAuditEvents};
 });
 report.interaction=interaction;assert(interaction.inputSame&&interaction.thresholdSame&&interaction.canvasSame&&interaction.focused);assert.deepEqual(interaction.selection,interactionBefore.selection);assert.deepEqual(interaction.scroll,interactionBefore.scroll);assert.equal(interaction.events,1);
 const global=await page.evaluate(()=>structuredClone(entryAudit.fiveSimulation.collisionReport));
 assert.equal(global.validity,'Current');assert.notEqual(global.checkMode,'SelectedPath');assert(!global.checkedIntentId);
 assert(new Set(global.frames.filter(f=>f.role!=='Rapid').map(f=>f.intentId)).size>1);
 report.cases.push('The actual top ribbon button starts an unscoped native check and shows inline global progress; asynchronous publication preserves nodes, focus, selection and latest left/right/nested scroll');

 await scrollStyle.evaluate(node=>node.remove());
 await page.evaluate(()=>{for(const selector of ['.cam-context-pane','.cam-info-pane','.five-collision-issue-list'])document.querySelector(selector).scrollTop=0;});
 await page.locator('[data-five-search]').fill('');
 await page.locator(`[data-five-intent="${intents[1]}"]`).click();await idle();
 const selectedBegin=await page.evaluate(()=>entryAuditCalls.length);
 await page.locator('[data-five-command="five.simulation.collision-check-path"]').click();await idle();
 const selected=await page.evaluate(begin=>({calls:entryAuditCalls.slice(begin),report:structuredClone(entryAudit.fiveSimulation.collisionReport),message:entryAudit.fiveSimulation.message}),selectedBegin);
 assert.equal(selected.message,'');const checks=selected.calls.filter(c=>c.method==='CamSimulation.CheckCollision');assert.equal(checks.length,1);assert.equal(checks[0].payload.intentId,intents[1]);
 assert.equal(selected.report.checkMode,'SelectedPath');assert.equal(selected.report.checkedIntentId,intents[1]);assert(Number(selected.report.checkedSegmentCount)<selected.report.segments.length);
 for(const row of global.segments){if(global.frames[Number(row.index)].intentId===intents[1])continue;assert.deepEqual(selected.report.segments[Number(row.index)],row);}
 report.cases.push('The right inspector checks only its selected cut and preserves other results');
 const beforeNavigation=await page.evaluate(()=>entryAuditCalls.length);
 await command('five.cam-inspector.properties');await command('five.cam-inspector.collision');await command('five.alerts.open:collision');
 assert.deepEqual(await page.evaluate(begin=>entryAuditCalls.slice(begin).filter(c=>/\.Generate$|CheckCollision|AutoRepair/.test(c.method)),beforeNavigation),[]);
 assert.deepEqual(await page.evaluate(begin=>entryAuditCalls.slice(begin).filter(c=>/\.Generate$|AutoRepair/.test(c.method)),begin),[]);
 report.cases.push('Opening the collision tab or alert starts no calculation; neither entry starts generation or repair');
 report.global={checkMode:global.checkMode,checked:global.checkedSegmentCount,total:global.segmentCount,keyposes:global.keypointCheckCount};
 report.selected={checkMode:selected.report.checkMode,checked:selected.report.checkedSegmentCount,total:selected.report.segmentCount,intentId:selected.report.checkedIntentId};
 assert.deepEqual(errors,[]);await page.screenshot({path:resolve(output,'collision-entrypoints.png')});report.passed=true;
}catch(e){report.error=e.stack;report.ui=await page.evaluate(()=>({error:window.entryAudit?.error,message:window.entryAudit?.fiveSimulation?.message,text:document.body.innerText.slice(-1800)})).catch(()=>null);await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});process.exitCode=1;}
finally{
 await page.evaluate(()=>{window.entryAuditRelease?.();if(window.entryAuditOriginal)entryAudit.viewportSceneProxy.invoke=entryAuditOriginal;}).catch(()=>{});
 writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();
}
