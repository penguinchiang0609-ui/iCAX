// Run in a fresh process after the nine-case cold regression. Do not regenerate
// before export: the saved final BRep versions themselves must be available.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync,readdirSync,statSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_CUSTOMER_JOINT_EVIDENCE||'output/tests/customer-joints-merge-20261023/delivery-verified');
const phase=process.argv.includes('--second')?'second':'first';
const baseline=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8')).cases.find(c=>c.name==='圆管护栏-相贯与合并');
const browser=await chromium.connectOverCDP('http://127.0.0.1:19425');const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));page.setDefaultTimeout(120000); await page.setViewportSize({width:1600,height:1000});
const report={phase,freshProcess:true,regeneratedBeforeExport:false};
try{
 await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path},baseline.path);
 await page.locator('[data-action="open-start-center"]').click();await page.locator('[data-action="choose-project"]').click();
 await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,baseline.model.product.entityId);
 const state=await page.evaluate(()=>window.__icaxWorkbench.getTubeDesignerState());const parts=state.manufacturingGroups.flatMap(g=>g.parts);
 assert.equal(parts.length,4);assert.equal(parts.reduce((n,p)=>n+p.quantity,0),32);assert.deepEqual(state.product.parameters,baseline.model.product.parameters);
 const target=join(root,'round-'+phase+'-fresh-export');mkdirSync(target,{recursive:true});
 await page.evaluate(target=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>target},target);
 await page.locator('[data-command-id="designer.export-active-product-parts"]').click();await page.waitForFunction(target=>document.body.innerText.includes(target),target);
 const walk=d=>readdirSync(d).flatMap(n=>{const p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});assert.equal(walk(target).filter(f=>f.endsWith('.step')).length,4);
 assert.equal(await page.evaluate(async path=>{const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge);await app.getState();const p=app.getProduct('icax.tube-designer');await p.getState();return (await p.getProject(window.__icaxWorkbench.getState().projectId).save(path)).saved;},baseline.path),true);
 report.passed=true;report.target=target;report.model=state;
}catch(e){report.error=e.stack;process.exitCode=1;await page.screenshot({path:join(root,'round-'+phase+'-fresh-failure.png')}).catch(()=>{});}
finally{writeFileSync(join(root,'round-'+phase+'-fresh-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
