import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_CUSTOMER_JOINT_EVIDENCE||'output/tests/customer-joints-merge-20261023/verified-final');
const baseline=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));assert.equal(baseline.passed,true);
const browser=await chromium.connectOverCDP('http://127.0.0.1:19425');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));page.setDefaultTimeout(120000); await page.setViewportSize({width:1600,height:1000});
await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
const holeOnly=process.argv.includes('--hole-only');
const report=holeOnly?JSON.parse(readFileSync(join(root,'cold-reopen-report.json'),'utf8'))
 :{coldProcess:true,businessMocked:false,pickerPathsControlled:true,cases:[]};
if(holeOnly){report.automationCorrection=report.error;delete report.error;}
const walk=dir=>readdirSync(dir).flatMap(n=>{let p=join(dir,n);return statSync(p).isDirectory()?walk(p):[p]});
try{
 const roundOnly=process.argv.includes('--round-only');
 const negative=roundOnly?null:{name:'同长不同孔',path:baseline.differentHole.path||join(root,'同长不同孔.ictd'),types:5,physicalQuantity:32,model:baseline.differentHole.model};
 const cases=roundOnly?baseline.cases.filter(c=>c.name==='圆管护栏-相贯与合并'):(holeOnly?[negative]:[...baseline.cases,negative]);
 for(const entry of cases){
  await page.locator('[data-action="open-start-center"]').click();
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path},entry.path);
  await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,entry.model.product.entityId);
  let restored=await page.evaluate(()=>window.__icaxWorkbench.getTubeDesignerState());
  assert.equal(restored.instances.length,1);
  assert.equal(restored.product.templateId,entry.model.product.templateId);
  assert.deepEqual(restored.product.parameters,entry.model.product.parameters);
  let parts=restored.manufacturingGroups.flatMap(g=>g.parts);
  assert.equal(parts.length,entry.types);
  assert.equal(parts.reduce((n,p)=>n+p.quantity,0),entry.physicalQuantity);
  await page.locator('[data-command-id="designer.disassemble"]').click();
  await page.locator('[data-cam-action="tube-designer-batch-disassembly-confirm"]').click();
  await page.waitForFunction(types=>window.__icaxWorkbench.getTubeDesignerState().parts.length===types,entry.types);
  // Wait for the real native operation to finish, not only the pre-existing row count.
  await page.waitForFunction(()=>!document.querySelector('[data-command-id="designer.export-active-product-parts"]')?.disabled);
  restored=await page.evaluate(()=>window.__icaxWorkbench.getTubeDesignerState());
  parts=restored.manufacturingGroups.flatMap(g=>g.parts);
  assert.equal(parts.reduce((n,p)=>n+p.quantity,0),entry.physicalQuantity);
  for(const part of parts){const v=part.properties['tubeDesigner.manufacturingValidation'];assert.equal(v.status,'verified');assert.equal(v.resourceId,part.manufacturingGeometryResourceId);assert.equal(v.resourceVersion,part.manufacturingGeometryResourceVersion);}
  const target=join(root,'cold',entry.name+'-export');mkdirSync(target,{recursive:true});
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>path},target);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  await page.waitForFunction(target=>document.body.innerText.includes(target),target);
  const files=walk(target);assert.equal(files.filter(p=>p.endsWith('.step')).length,entry.types);
  const repeated=await page.evaluate(async({ids,target})=>{
    const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge);
    await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();
    return product.getProject(window.__icaxWorkbench.getState().projectId).getMainScene().invoke('TubeDesigner.ExportSelected',{partEntityIds:ids,targetDirectory:target});
  },{ids:parts.map(p=>p.entityId),target});
  assert.equal(repeated.exportedCount,entry.types);assert.equal(repeated.draftOnly,false);
  assert.equal(walk(target).filter(p=>p.endsWith('.step')).length,entry.types);
  const saved=await page.evaluate(async path=>{
    const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
    const app=await AppProxy.create(window.icax??window.icaxNativeBridge);await app.getState();
    const product=app.getProduct('icax.tube-designer');await product.getState();
    return (await product.getProject(window.__icaxWorkbench.getState().projectId).save(path)).saved;
  },entry.path);
  assert.equal(saved,true);
  const result={name:entry.name,path:entry.path,target,types:entry.types,physicalQuantity:entry.physicalQuantity,
    parametersPreserved:true,repeatedExport:true,savedAfterRepeatedExport:true,model:restored,files,passed:true};report.cases.push(result);
  console.log(JSON.stringify({name:entry.name,types:entry.types,quantity:entry.physicalQuantity,reopened:true}));
 }
 report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:join(root,'cold-failure.png')}).catch(()=>{});}
finally{writeFileSync(join(root,'cold-reopen-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
