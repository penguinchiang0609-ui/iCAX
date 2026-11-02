import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const root=resolve(process.env.ICAX_MOUNTING_EVIDENCE);
const original=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));
assert(original.passed);
let browser;
for(let attempt=0;;attempt++){
  try{browser=await require('playwright').chromium.connectOverCDP(process.env.ICAX_MOUNTING_CDP||'http://127.0.0.1:19478');break;}
  catch(e){if(attempt>=24)throw e;await new Promise(r=>setTimeout(r,500));}
}
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
const coldProcess=Number(process.env.ICAX_MOUNTING_PROCESS_ID);
const warmProcess=Number(process.env.ICAX_MOUNTING_WARM_PROCESS_ID);
assert(coldProcess>0&&warmProcess>0&&coldProcess!==warmProcess);
const report={passed:false,freshProcess:true,nativeBusinessMocked:false,
  processId:coldProcess,warmProcessId:warmProcess,cdpEndpoint:process.env.ICAX_MOUNTING_CDP,cases:[]};
try{
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  for(const name of ['three-groups-ui-enabled','three-groups-male-female','front-three-groups',
    ...(process.env.ICAX_THROUGH_ACCEPTANCE==='1'?['through-both-ends-ui','through-open-slot-round']:[]),
    ...(process.env.ICAX_RECEIVER_CONNECTIVITY==='1'?['half-default-cross-corners','through-large-corners','through-overlapping-mouths']:[])]){
    const summary=original.cases.find(c=>c.name===name);assert(summary);
    const frozen=JSON.parse(readFileSync(summary.reportPath,'utf8'));
    await page.evaluate(path=>window.__icaxAppShell.openProject(path,'icax.tube-designer'),summary.path);
    await page.waitForFunction(path=>{const app=window.__icaxAppShell.getState();return app.pendingCount===0
      &&app.activeProjectPath.replaceAll('\\','/')===path.replaceAll('\\','/')
      &&window.__icaxWorkbench.getState().projectId===app.activeProjectId;},summary.path);
    const model=await page.evaluate(async()=>{
      const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
      const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:2000000000}});
      await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();
      const scene=product.getProject(window.__icaxWorkbench.getState().projectId).getMainScene();
      return(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
    });
    const parts=model.manufacturingGroups.find(g=>g.productEntityId===model.product.entityId).parts;
    assert.deepEqual(model.product.parameters,frozen.model.product.parameters);
    assert.equal(model.product.entityId,frozen.model.product.entityId);
    assert.deepEqual(parts.map(p=>[p.entityId,p.quantity]),frozen.model.parts.map(p=>[p.entityId,p.quantity]));
    report.cases.push({name,passed:true,partTypes:parts.length,physicalQuantity:parts.reduce((n,p)=>n+p.quantity,0)});
  }
  report.passed=true;
}catch(e){report.error=e.stack;throw e;}
finally{writeFileSync(join(root,'cold-reopen-report.json'),JSON.stringify(report,null,2));await browser.close();}
