import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=process.env.ICAX_CONTINUOUS_EVIDENCE;
assert(root);const warm=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));assert(warm.passed);
let browser;
for(let attempt=0;!browser;attempt++){
 try{browser=await chromium.connectOverCDP(process.env.ICAX_CONTINUOUS_CDP||'http://127.0.0.1:19459');}
 catch(error){if(attempt>=40)throw error;await new Promise(r=>setTimeout(r,250));}
}
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));assert(page);page.setDefaultTimeout(120000);
const report={nativeBusinessMocked:false,pickerPathsControlled:true,coldProcess:true,cases:[]};
const canonical=value=>JSON.parse(JSON.stringify(value));
const requested=(process.env.ICAX_CONTINUOUS_COLD_CASES||'').split(',').filter(Boolean);
try{
 for(const c of warm.cases.filter(c=>!requested.length||requested.includes(c.name))){
  await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},c.path);
  await page.locator('[data-action="open-start-center"]').click();await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,c.model.product.entityId);
  const model=await page.evaluate(async()=>{
   const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
   const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1815000000}});
   await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();
   return (await product.getProject(window.__icaxWorkbench.getState().projectId).getMainScene().invoke('TubeDesigner.List',{})).tubeDesigner;
  });
  assert.deepEqual(model.product.parameters,c.model.product.parameters);
  assert.equal(model.product.modelOutdated,false);assert.equal(model.product.partsOutdated,false);
  assert.deepEqual(canonical(model.members.map(m=>[m.stableKey,m.properties['assemblyFrame.member']])),c.model.members.map(m=>[m.stableKey,m.properties['assemblyFrame.member']]));
  assert.deepEqual(canonical(model.parts.map(p=>[p.entityId,p.quantity,p.properties['manufacturing.sourceMembers']])),c.model.parts.map(p=>[p.entityId,p.quantity,p.properties['manufacturing.sourceMembers']]));
  report.cases.push({name:c.name,members:model.members.length,types:model.parts.length,passed:true});
  if(c.name==='same-stock-center'){
   await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();});
   const toggle=page.locator('.tube-designer-scene-specification-tree input[type="checkbox"]').first();
   if(await toggle.count())await toggle.uncheck();
   await page.screenshot({path:join(root,'cold-same-stock-center.png')});
   await page.locator('.cam-viewport').screenshot({path:join(root,'continuous-members-view.png')});
  }
  console.log(JSON.stringify(report.cases.at(-1)));
 }
 report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;}
finally{writeFileSync(join(root,'cold-report.json'),JSON.stringify(report,null,2));await browser.close();}
