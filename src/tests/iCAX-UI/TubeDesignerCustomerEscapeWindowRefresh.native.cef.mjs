// Re-save the accepted models against the final descriptor after UI declaration edits.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_ESCAPE_EVIDENCE||'output/tests/customer-escape-window-20261023/verified');const report=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));assert(report.passed);
let browser;for(let i=0;!browser;++i){try{browser=await chromium.connectOverCDP('http://127.0.0.1:19427');}catch(e){if(i>=60)throw e;await new Promise(r=>setTimeout(r,250));}}
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));page.setDefaultTimeout(120000);await page.setViewportSize({width:1600,height:1000});
try{
 await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 for(const entry of report.cases){
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path},entry.path);
  await page.locator('[data-action="open-start-center"]').click();await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,entry.model.product.entityId);
  const result=await page.evaluate(async entry=>{
   const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{initialCallId:(window.__escapeCallId=(window.__escapeCallId??1000000000)+100000)}});await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();const project=product.getProject(window.__icaxWorkbench.getState().projectId);const scene=project.getMainScene();
   const parameters=entry.model.product.parameters;
   const preview=await scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:'single-face-security-window',parameters});
   await scene.invoke('TubeDesigner.GeneratePreview',{templateId:'single-face-security-window',productEntityId:entry.model.product.entityId,...preview.parameters});
   await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[entry.model.product.entityId]});
   const model=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
   const exported=await scene.invoke('TubeDesigner.ExportSelected',{partEntityIds:model.parts.map(p=>p.entityId),targetDirectory:entry.target});
   return {model,exported,saved:(await project.save(entry.path)).saved};
  },entry);
  assert(result.saved);assert.deepEqual(result.model.product.parameters,entry.model.product.parameters);assert.equal(result.model.parts.length,entry.types);assert.equal(result.exported.exportedCount,entry.types);assert.equal(result.model.product.modelOutdated,false);
  entry.model=result.model;entry.finalDescriptorRebuilt=true;
  writeFileSync(join(root,entry.name+'-report.json'),JSON.stringify(entry,null,2));console.log(JSON.stringify({name:entry.name,finalDescriptorRebuilt:true}));
 }
 report.finalDescriptorRebuilt=true;writeFileSync(join(root,'acceptance-report.json'),JSON.stringify(report,null,2));
}finally{await browser.close();}
