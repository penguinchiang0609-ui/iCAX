// Refresh saved engineering evidence with the frozen final template package.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync,readdirSync,statSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_ESCAPE_EVIDENCE||'output/tests/customer-remaining-20261023/accepted');
const before=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));assert(before.passed);
const browser=await chromium.connectOverCDP('http://127.0.0.1:19427');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));page.setDefaultTimeout(120000);
const report={nativeBusinessMocked:false,pickerPathsControlled:true,finalPackage:true,cases:[]};
const walk=d=>readdirSync(d).flatMap(n=>{const p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});
try{
 for(const entry of before.cases){
  await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},entry.path);
  await page.locator('[data-action="open-start-center"]').click();await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,entry.model.product.entityId);
  const target=join(root,'final-exports',entry.name+'-export');mkdirSync(target,{recursive:true});
  const result=await page.evaluate(async({entry,target})=>{
   const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
   const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{initialCallId:1500000000}});await app.getState();
   const product=app.getProduct('icax.tube-designer');await product.getState();
   const project=product.getProject(window.__icaxWorkbench.getState().projectId),scene=project.getMainScene();
   const templateId=entry.model.product.templateId,productEntityId=entry.model.product.entityId;
   const preview=await scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId,parameters:entry.model.product.parameters});
   await scene.invoke('TubeDesigner.GeneratePreview',{templateId,productEntityId,...preview.parameters});
   await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[productEntityId]});
   const model=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
   const exported=await scene.invoke('TubeDesigner.ExportSelected',{targetDirectory:target,partEntityIds:model.parts.map(p=>p.entityId)});
   const saved=await project.save(entry.path);return {model,exported,saved,parameters:preview.parameters};
  },{entry,target});
  assert.deepEqual(result.model.product.parameters,result.parameters);assert.equal(result.saved.saved,true);
  assert.equal(result.model.product.entityId,entry.model.product.entityId);
  assert.equal(result.model.product.modelOutdated,false);assert.equal(result.model.product.partsOutdated,false);
  assert.equal(result.exported.exportedCount,entry.types);assert.equal(result.exported.draftOnly,false);
  const model=result.model,physicalQuantity=model.parts.reduce((n,p)=>n+p.quantity,0);assert.equal(physicalQuantity,entry.physicalQuantity);
  const item={...entry,model,target,files:walk(target),types:model.parts.length,physicalQuantity,passed:true};
  report.cases.push(item);writeFileSync(join(root,entry.name+'-report.json'),JSON.stringify(item,null,2));
  console.log(JSON.stringify({name:entry.name,finalPackageSaved:true,passed:true}));
 }
 report.passed=true;writeFileSync(join(root,'acceptance-report.json'),JSON.stringify(report,null,2));
}catch(e){report.error=e.stack;process.exitCode=1;}
finally{writeFileSync(join(root,'finalization-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
