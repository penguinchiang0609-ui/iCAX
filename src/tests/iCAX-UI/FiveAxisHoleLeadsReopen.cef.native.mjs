import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {chromium} from 'playwright';

const prior=JSON.parse(readFileSync(resolve(process.env.ICAX_HOLE_LEAD_REPORT),'utf8'));assert.equal(prior.passed,true);
const runtime=resolve(process.env.ICAX_CEF_RUNTIME),output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),base=pathToFileURL(runtime+'/').href;
mkdirSync(output,{recursive:true});const browser=await chromium.connectOverCDP('http://127.0.0.1:'+process.env.ICAX_CEF_PORT);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page);page.setDefaultTimeout(120000);
const report={passed:false,standaloneCef:true,project:prior.project,errors:[],holes:[]};page.on('pageerror',error=>report.errors.push(error.message));
try{
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),prior.project);
  await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
  await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.holeView=getProjectView(__icaxAppShell.getState().activeProjectId);},base);
  await page.waitForFunction(()=>!holeView.pending&&!!holeView.scene?.domain);
  await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));
  const state=await page.evaluate(()=>holeView.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
  const ids=prior.holes.map(h=>h.id);assert.deepEqual(state.intentToolpaths.filter(c=>c.boundaryRole==='Opening').map(c=>c.id).sort(),ids.slice().sort());
  const preview=await page.evaluate(({expected,ids})=>holeView.viewportSceneProxy.invoke('CamProcess.PreviewLeads',{expected,intentIds:ids},{timeoutMs:120000}),{expected:state.expected,ids});
  for(const item of preview.items){const original=prior.holes.find(h=>h.id===item.intentId);assert.ok(original);assert.equal(item.leads.length,2);
    for(const lead of item.leads){assert.equal(lead.kind,'Arc');assert.equal(lead.lengthMm,1);assert.ok(lead.points.length>2);for(const p of lead.points){assert.ok(p.every(Number.isFinite));assert.ok(Math.hypot(p[0]-original.center[0],p[1]-original.center[1])<=original.radius+1e-6);}}
    report.holes.push({id:item.intentId,number:original.number,inside:true,leads:2});
  }
  assert.equal(report.holes.length,3);assert.equal(await page.evaluate(()=>holeView.error),'');
  await page.waitForFunction(()=>holeView.viewport?.getDebugState()?.objectCount>0);
  await page.screenshot({path:resolve(output,'三孔孔内圆弧引线-独立程序重开.png')});assert.deepEqual(report.errors,[]);
  report.passed=true;report.cases=['Fresh standalone process opens the native saved original workpiece','All six saved arc leads reproduce inside their original holes','Actual native viewport displays the reopened workpiece and leads'];console.log(JSON.stringify({passed:true,standaloneCef:true,holes:3}));
}catch(error){report.error=error.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
