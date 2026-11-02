import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync,readdirSync,statSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_ESCAPE_EVIDENCE||'output/tests/customer-escape-window-20261023/verified');
const baseline=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));assert(baseline.passed);
let browser;for(let i=0;!browser;++i){try{browser=await chromium.connectOverCDP('http://127.0.0.1:19427');}catch(e){if(i>=60)throw e;await new Promise(r=>setTimeout(r,250));}}
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));page.setDefaultTimeout(120000);await page.setViewportSize({width:1600,height:1000});
const report={coldProcess:true,businessMocked:false,pickerPathsControlled:true,cases:[]};
const runId=String(Date.now());
const walk=d=>readdirSync(d).flatMap(n=>{const p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});
try{
 await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 for(const entry of baseline.cases){
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path},entry.path);
  await page.locator('[data-action="open-start-center"]').click();await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,entry.model.product.entityId);
  await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));});
  const restored=await page.evaluate(()=>window.__icaxWorkbench.getTubeDesignerState());
  assert.deepEqual(restored.product.parameters,entry.model.product.parameters);
  let parts=restored.manufacturingGroups.flatMap(g=>g.parts);assert.equal(parts.length,entry.types);assert.equal(parts.reduce((n,p)=>n+p.quantity,0),entry.physicalQuantity);
  const target=join(root,'cold',runId,entry.name+'-export');mkdirSync(target,{recursive:true});
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>path},target);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();await page.waitForFunction(path=>document.body.innerText.includes(path),target);
  assert.equal(walk(target).filter(p=>p.endsWith('.step')).length,entry.types);
  await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const v=getProjectView(window.__icaxWorkbench.getState().projectId);return !v.pending&&!v.tubeDesignerExportOperation;});
  // Inspect and save the loaded engineering without regeneration or a new
  // disassembly, so the audited files remain the actual persisted-resource export.
  const final=await page.evaluate(async path=>{
   const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1800000000}});await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();const project=product.getProject(window.__icaxWorkbench.getState().projectId);const scene=project.getMainScene();
   const model=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
   const saved=await project.save(path,{timeoutMs:120000});return {model,saved};
  },entry.path);
  assert.equal(final.saved.saved,true);
  assert.equal(final.model.product.activeGenerationRunId,entry.model.product.activeGenerationRunId);
  assert.equal(final.model.product.modelOutdated,false);assert.equal(final.model.product.partsOutdated,false);
  assert.equal(final.model.parts.length,entry.types);
  assert.equal(final.model.parts.reduce((n,p)=>n+p.quantity,0),entry.physicalQuantity);
  for(const part of final.model.parts){const proof=part.properties['tubeDesigner.manufacturingValidation'];assert.equal(proof.resourceId,part.manufacturingGeometryResourceId);assert.equal(proof.resourceVersion,part.manufacturingGeometryResourceVersion);assert.equal(proof.status,'verified');}
  const p=final.model.product.parameters;
  let face=p.faceType==='two'?p.accessDoorFace2:p.faceType==='three'?p.accessDoorFace3:p.faceType==='five'?p.accessDoorFace5:'front';
  if(face==='side')face=p.sidePosition;
  await page.evaluate(async face=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView(face);await window.__icaxWorkbench.fitView();},face);
  await page.screenshot({path:join(root,entry.name+'-cold.png')});
  report.cases.push({name:entry.name,path:entry.path,target,types:entry.types,physicalQuantity:entry.physicalQuantity,model:final.model,parametersPreserved:true,exportedBeforeRegeneration:true,exportedPersistedResources:true,regenerated:false,saved:true,passed:true});
  console.log(JSON.stringify({name:entry.name,reopened:true,passed:true}));
 }
 report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;await page.screenshot({path:join(root,'cold-failure.png')}).catch(()=>{});}
finally{writeFileSync(join(root,'cold-reopen-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
