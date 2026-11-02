// Runs the shared trace acceptance against one explicitly supplied CEF project.
'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {pathToFileURL}=require('node:url');
const {chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const args=process.argv.slice(2),option=name=>{const i=args.indexOf(name);return i<0?null:args[i+1];};
for(const name of ['--cdp','--project','--output'])assert.ok(option(name)&&!option(name).startsWith('--'),'Explicit '+name+' required');
const cdp=new URL(option('--cdp')),project=path.resolve(option('--project')).replaceAll('\\','/'),output=path.resolve(option('--output'));
assert.notEqual(cdp.port,'9488','The original user instance must never be touched');assert.ok(fs.existsSync(project));assert.match(output,/^[cC]:[\\/]/);fs.mkdirSync(output,{recursive:true});
const report={passed:false,project,cdp:cdp.href,startedAt:new Date().toISOString(),coverage:{standaloneCef:true,sharedTraceAcceptance:true,readOnlyNativeOracles:true,engineeringEdits:false}},write=()=>fs.writeFileSync(path.join(output,'standalone-report.json'),JSON.stringify(report,null,2));
let browser,page,hook=false;
(async()=>{
  browser=await chromium.connectOverCDP(cdp.href);page=browser.contexts().flatMap(c=>c.pages()).find(p=>/AppShell\/index\.html/.test(p.url()));assert.ok(page);page.setDefaultTimeout(90000);
  await page.setViewportSize({width:1600,height:1000});
  const active=await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath);assert.equal(active?.replaceAll('\\','/'),project,'Refuse to reload or switch a different project');
  await page.reload({waitUntil:'domcontentloaded'});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  await page.evaluate(project=>__icaxAppShell.openProject(project,'icax.five-axis-cam'),project);
  await page.waitForFunction(project=>__icaxAppShell.getState().activeProjectPath?.replaceAll('\\','/')===project&&!__icaxAppShell.getState().pendingCount,project);
  await page.evaluate(async()=>{
    const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
    const view=getProjectView(__icaxAppShell.getState().activeProjectId),proxy=view.viewportSceneProxy;
    window.fixture={view,context:{},entry:{handleRibbonCommand(_context,id){return __icaxAppShell.executeRibbonCommand(id);}},hold:false,held:false,release:null};
    window.traceStandaloneObserver={proxy,original:proxy.invoke};
    proxy.invoke=async function(method,...args){if(method===(fixture.holdMethod||'CamScene.ReadState')&&fixture.hold){fixture.hold=false;fixture.held=true;await new Promise(resolve=>fixture.release=resolve);fixture.held=false;}return traceStandaloneObserver.original.call(this,method,...args);};
  });hook=true;
  const idle=()=>page.waitForFunction(()=>!!window.fixture?.view?.scene?.domain&&!fixture.view.pending&&!fixture.view.fiveHistoryBusy&&!__icaxAppShell.getState().pendingCount);
  const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
  const field=name=>page.locator(`[data-five-simulation-field="${name}"]`),button=name=>page.locator(`[data-five-command="five.simulation.${name}"]`);
  const read=()=>page.evaluate(async()=>{const {hydratePathSamples}=await import('../../../apps/five-axis-cam/webpage/pathSampleResource.mjs'),proxy=fixture.view.viewportSceneProxy;return hydratePathSamples(await proxy.invoke('CamScene.ReadState',{}),proxy);});
  await idle();if(await page.locator('[data-five-alert-panel]').count())await page.getByRole('button',{name:'收起告警',exact:true}).click();
  await page.locator('[data-tab-id="toolpath"]').click();await command('five.tool.simulation');await command('view.fit');
  await page.waitForFunction(()=>fixture.view.fiveSimulation?.frames?.length>10&&!!fixture.view.fiveSimulation.trace?.rapidLine&&!!fixture.view.fiveSimulation.group);
  const {verifySimulationTrace}=await import(pathToFileURL(path.join(__dirname,'FiveAxisSimulationTrace.native.browser.mjs')).href);
  await verifySimulationTrace({page,command,field,button,idle,read,output});
  report.trace=JSON.parse(fs.readFileSync(path.join(output,'trace-report.json'),'utf8'));assert.equal(report.trace.passed,true);report.passed=true;
})().catch(async error=>{report.error=error.message;report.stack=error.stack;console.error(error.stack);if(page)await page.screenshot({path:path.join(output,'standalone-failure.png')}).catch(()=>{});}).finally(async()=>{
  if(hook&&page)await page.evaluate(()=>{const o=window.traceStandaloneObserver;if(o){o.proxy.invoke=o.original;delete window.traceStandaloneObserver;}}).catch(()=>{});
  report.finishedAt=new Date().toISOString();write();if(browser)await browser.close();console.log('Report: '+path.join(output,'standalone-report.json'));process.exitCode=report.passed?0:1;
});
