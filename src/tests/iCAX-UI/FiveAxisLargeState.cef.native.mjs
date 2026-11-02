// Runs against the owned real FiveAxisCAM AppShell/CEF process. No native or
// resource transport is replaced. The input is a private copy of a real project.
import assert from 'node:assert/strict';
import {writeFileSync,readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/five-axis-sdo-large-response');
const project=resolve(output,'egg-standalone.i5cam'),port=Number(process.env.ICAX_CEF_PORT||9437);
const owned=JSON.parse(readFileSync(resolve(output,'cef-process.json'),'utf8').replace(/^\uFEFF/,''));
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`);
const page=browser.contexts()[0].pages()[0];page.setDefaultTimeout(180000);
const report={passed:false,standaloneCef:true,realNativeQueue:true,realResourceBridge:true,pid:owned.pid,pageErrors:[]};
const save=()=>writeFileSync(resolve(output,'standalone-cef.json'),JSON.stringify(report,null,2));
page.on('pageerror',e=>report.pageErrors.push(e.message));
try{
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready'||window.__icaxAppShell?.getState().activeProductId);
  report.url=page.url();report.initial=await page.evaluate(()=>__icaxAppShell.getState());save();
  const began=Date.now();report.opened=await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);report.openMs=Date.now()-began;
  assert.equal(report.opened.error,'');assert.ok(report.opened.activeProjectId);
  const moduleUrl=pathToFileURL(resolve('src/apps/_shared/workbench/state/projectViewStore.mjs')).href;
  await page.evaluate(async url=>{const {getProjectView}=await import(url);window.getOwnedFiveView=()=>getProjectView(__icaxAppShell.getState().activeProjectId);},moduleUrl);
  await page.waitForFunction(()=>getOwnedFiveView().error||getOwnedFiveView().scene?.domain?.setups?.some(s=>s.generatedGeometry?.operations?.length===480)&&!getOwnedFiveView().pending);
  assert.equal(await page.evaluate(()=>getOwnedFiveView().error),'');
  report.before=await page.evaluate(()=>{const v=getOwnedFiveView(),g=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).generatedGeometry;return {operations:g.operations.length,resource:g.sampleGeometryRef,points:g.operations.reduce((n,o)=>n+o.parts.reduce((m,p)=>m+p.points.length,0),0),error:v.error};});
  assert.equal(report.before.operations,480);assert.ok(report.before.points>0);assert.equal(report.before.error,'');save();
  await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));
  const start=Date.now();await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.tool.generate'));
  await page.waitForFunction(()=>!getOwnedFiveView().pending&&!getOwnedFiveView().fiveRapidBatch?.running);
  report.generationMs=Date.now()-start;
  report.after=await page.evaluate(()=>{const v=getOwnedFiveView(),g=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).generatedGeometry;
    return {operations:g.operations.length,transitions:g.transitions.length,generated:g.rapidGeneratedCount,error:v.error,allGenerated:g.transitions.every(t=>t.geometryStatus==='Generated'&&t.validation==='Unchecked'&&t.points.length===4&&t.directions.length===4),resource:g.sampleGeometryRef,task:v.fiveRapidBatch?.task?.status};});
  assert.equal(report.after.error,'');assert.equal(report.after.operations,480);assert.equal(report.after.transitions,479);assert.equal(report.after.generated,'479');assert.equal(report.after.allGenerated,true);assert.equal(report.after.task,'Completed');save();
  await page.screenshot({path:resolve(output,'standalone-480-rapids.png')});
  // Refresh the completed real window through its own Scene/resource bridges.
  // Embedded save/reopen is covered separately by the native/browser test;
  // this raw project file does not create a product project-catalog directory.
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));
  await page.waitForFunction(()=>getOwnedFiveView().scene?.domain?.setups?.some(s=>s.generatedGeometry?.rapidGeneratedCount==='479')&&!getOwnedFiveView().pending);
  report.refreshed=await page.evaluate(()=>{const v=getOwnedFiveView(),g=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId).generatedGeometry;return {error:v.error,operations:g.operations.length,rapids:g.rapidGeneratedCount,hydrated:g.transitions.every(t=>t.points.length===4)};});
  assert.equal(report.refreshed.error,'');assert.equal(report.refreshed.operations,480);assert.equal(report.refreshed.rapids,'479');assert.equal(report.refreshed.hydrated,true);assert.deepEqual(report.pageErrors,[]);
  report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;try{await page.screenshot({path:resolve(output,'standalone-failure.png')});}catch{}}
finally{save();console.log(JSON.stringify(report,null,2));await browser.close();}
