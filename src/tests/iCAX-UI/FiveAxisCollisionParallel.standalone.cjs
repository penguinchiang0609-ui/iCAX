'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const args=process.argv.slice(2),option=n=>args[args.indexOf(n)+1];
for(const name of ['--cdp','--project','--output'])assert.ok(args.includes(name)&&option(name),'Explicit '+name+' required');
const project=path.resolve(option('--project')).replaceAll('\\','/'),output=path.resolve(option('--output'));
assert.match(output,/^[cC]:[\\/]/);fs.mkdirSync(output,{recursive:true});
const report={passed:false,project,coverage:{standaloneCef:true,renderedCheckButton:true,nativeReadOracles:true},samples:[]};
const expectedWorkers=Math.min(4,Math.max(1,require('node:os').cpus().length-1));
let browser,page,originalInvoke;
const write=()=>fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
(async()=>{
 browser=await chromium.connectOverCDP(option('--cdp'));
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>/AppShell\/index\.html/.test(p.url()));assert.ok(page);page.setDefaultTimeout(60000);
 await page.waitForFunction(async project=>{
  const app=window.__icaxAppShell?.getState();if(app?.startupPhase!=='ready'||app.activeProjectPath?.replaceAll('\\','/')!==project||app.pendingCount)return false;
  const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.parallelCollisionAudit=getProjectView(app.activeProjectId);
  return !!parallelCollisionAudit?.scene?.domain&&!parallelCollisionAudit.pending&&!!parallelCollisionAudit.viewportSceneProxy;
 },project);
 const read=()=>page.evaluate(async()=>{const {hydratePathSamples}=await import('../../../apps/five-axis-cam/webpage/pathSampleResource.mjs');const proxy=parallelCollisionAudit.viewportSceneProxy;return hydratePathSamples(await proxy.invoke('CamScene.ReadState',{}),proxy);});
 const setup=s=>s.setups.find(x=>x.id===s.activeSetupId);
 const immutable=s=>({workpieces:s.workpieces,placements:s.placements,fixtures:s.fixtures,fixtureUnits:s.fixtureUnits,cuts:s.intentToolpaths,geometry:(({sampleGeometryRef,...geometry})=>geometry)(setup(s).generatedGeometry)});
 const before=await read(),historyBefore=await page.evaluate(()=>parallelCollisionAudit.viewportSceneProxy.getUndoRedoState());
 await page.locator('[data-tab-id="toolpath"]').click();
 if(!await page.locator('[data-five-key="collision-dialog"]').count())await page.locator('[data-command-id="five.tool.collision"]').click();
 await page.evaluate(()=>{const proxy=parallelCollisionAudit.viewportSceneProxy;window.parallelCollisionObserver={proxy,original:proxy.invoke,replies:[]};proxy.invoke=async function(method,...args){const result=await parallelCollisionObserver.original.call(this,method,...args);if(method==='CamSimulation.CheckCollision')parallelCollisionObserver.replies.push(structuredClone(result));return result;};});originalInvoke=true;
 await page.locator('[data-five-command="five.simulation.collision-check"]').click();
 const deadline=Date.now()+180000;let sawRunning=false;
 while(Date.now()<deadline){
  const sample=await page.evaluate(()=>{const v=parallelCollisionAudit,s=v.fiveSimulation;return {checking:!!s?.checking,stage:s?.checkStage,progress:s?.checkExactProgress?structuredClone(s.checkExactProgress):null,text:document.querySelector('[data-five-key="collision-activity"]')?.textContent||'',pending:!!v.pending,error:v.error||s?.message||''};});
  if(sample.checking){sawRunning=true;report.samples.push(sample);}
  if(!sample.checking&&!sample.pending&&(sawRunning||await page.evaluate(()=>parallelCollisionObserver.replies.length))){assert.equal(sample.error,'');break;}
  await new Promise(resolve=>setTimeout(resolve,25));
 }
 const after=await read(),collision=setup(after).simulationCheck;
 assert.equal(await page.evaluate(()=>!!parallelCollisionAudit.fiveSimulation?.checking),false,'Native parallel check must finish');
 assert.equal(collision.validity,'Current');assert.equal(collision.checkOrder,'KeypointsThenIntervals');assert.equal(collision.parallelWorkers,String(expectedWorkers));
 assert.equal(collision.keypointResultsCurrent,true);assert.equal(collision.keypoints.length,Number(collision.keypointCheckCount));assert.ok(collision.keypoints.length>0);
 assert.equal(collision.segments.length,collision.frames.length-1);collision.segments.forEach((s,i)=>assert.equal(s.index,String(i)));
 assert.deepEqual(immutable(after),immutable(before),'Analysis preserves source geometry and editable machining definitions');
 assert.deepEqual(await page.evaluate(()=>parallelCollisionAudit.viewportSceneProxy.getUndoRedoState()),historyBefore,'Analysis creates no engineering undo step');
 report.startReplies=await page.evaluate(()=>parallelCollisionObserver.replies);assert.ok(report.startReplies.length);
 for(const sample of report.samples){const p=sample.progress;if(!p)continue;if(p.parallel){assert.equal(p.parallel.workers,expectedWorkers);assert.ok(p.parallel.active<=expectedWorkers);}if(p.keypoints)assert.equal(p.current,null,'Static keypoints must not invent an active motion interval');if(p.current!=null)assert.ok(p.current>=0&&p.current<p.full);}
 report.result={parallelWorkers:collision.parallelWorkers,keypointCheckCount:collision.keypointCheckCount,segmentCount:collision.segments.length,collisionCount:collision.collisionCount,uncheckedCount:collision.uncheckedCount};
 await page.screenshot({path:path.join(output,'completed.png')});report.passed=true;
})().catch(async error=>{report.error=error.message;report.stack=error.stack;if(page)await page.screenshot({path:path.join(output,'failure.png')}).catch(()=>{});console.error(error.stack);}).finally(async()=>{
 if(originalInvoke&&page)await page.evaluate(()=>{const hook=window.parallelCollisionObserver;if(hook)hook.proxy.invoke=hook.original;}).catch(()=>{});
 write();if(browser)await browser.close();console.log('Report: '+path.join(output,'report.json'));process.exitCode=report.passed?0:1;
});
