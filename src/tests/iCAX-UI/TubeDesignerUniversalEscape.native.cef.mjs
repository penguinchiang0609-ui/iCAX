// Standalone CEF/native acceptance; only picker paths are controlled.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync,readdirSync,statSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_ESCAPE_EVIDENCE||'output/tests/customer-remaining-20261023/final');mkdirSync(root,{recursive:true});
const cases=JSON.parse(readFileSync(join(root,'cases.json'),'utf8'));
let browser;
for(let attempt=0;!browser;++attempt){
 try{browser=await chromium.connectOverCDP('http://127.0.0.1:19427');}
 catch(error){if(attempt>=60)throw error;await new Promise(r=>setTimeout(r,250));}
}
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
page.setDefaultTimeout(120000);await page.setViewportSize({width:1600,height:1000});
const report={nativeBusinessMocked:false,pickerPathsControlled:true,cases:[]};
const walk=d=>readdirSync(d).flatMap(n=>{const p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});
const init=()=>page.evaluate(async()=>{const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:(window.__escapeCallId=(window.__escapeCallId??1000000000)+100000)}});await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();const project=product.getProject(window.__icaxWorkbench.getState().projectId);window.__escapeTest={scene:project.getMainScene(),project};});
try{
 for(const test of cases){
  console.log(JSON.stringify({name:test.name,stage:'start'}));
  await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
  await init();
  const generated=await page.evaluate(async ({parameters,resize,templateId})=>{
   const {scene}=window.__escapeTest;
   const preview=await scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:templateId||'single-face-security-window',parameters});
   await scene.invoke('TubeDesigner.GeneratePreview',{templateId:templateId||'single-face-security-window',...preview.parameters});
   const created=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
   if(JSON.stringify(created.product.parameters)!==JSON.stringify(preview.parameters))throw new Error('Created parameters differ from normalized preview');
   const id=created.product.entityId;
   let resized=false;
   if(resize){
    const changed={...created.product.parameters,...resize};
    await scene.invoke('TubeDesigner.UpdateProductParameters',{productEntityId:id,parameters:changed});
    const next=await scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:templateId||'single-face-security-window',parameters:changed});
    await scene.invoke('TubeDesigner.GeneratePreview',{templateId:templateId||'single-face-security-window',productEntityId:id,...next.parameters});
    const regenerated=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
    if(regenerated.instances.length!==1||regenerated.product.entityId!==id)throw new Error('Resize created another product');
    if(JSON.stringify(regenerated.product.parameters)!==JSON.stringify(next.parameters))throw new Error('Normalized parameters changed');
    resized=true;
   }
   await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[id]});
   await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[id]});
   return {model:(await scene.invoke('TubeDesigner.List',{})).tubeDesigner,resized,initialParameters:created.product.parameters};
  },test);
  const {model}=generated;console.log(JSON.stringify({name:test.name,stage:'generated-and-disassembled'}));
  if(test.templateId==='minimal-protective-grille'){
   const leaf=model.members.filter(m=>m.stableKey.startsWith('access_door.leaf.'));
   if(model.product.parameters.accessDoorEnabled){
    assert.equal(leaf.filter(m=>m.stableKey.startsWith('access_door.leaf.frame.')).length,4);
    const boundary=model.members.find(m=>m.stableKey==='main_grid.vertical.0001');
    assert.equal(boundary.properties['tubeDesigner.escapeWindow'].gap,model.product.parameters.doorGap);
   }else assert.equal(leaf.length,0);
  }
  assert(model.parts.length>0);assert.equal(model.product.modelOutdated,false);assert.equal(model.product.partsOutdated,false);
  const path=join(root,test.name+'.ictd');assert.equal((await page.evaluate(path=>window.__escapeTest.project.save(path,{timeoutMs:120000}),path)).saved,true);
  await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},path);
  await page.locator('[data-action="open-start-center"]').click();await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,model.product.entityId);
  await init();
  const target=join(root,test.name+'-export');mkdirSync(target,{recursive:true});
  await page.evaluate(target=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>target;},target);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  await page.waitForFunction(target=>document.body.innerText.includes(target),target);
  const files=walk(target);assert.equal(files.filter(p=>p.endsWith('.step')).length,model.parts.length);
  const repeated=await page.evaluate(async target=>{const {scene}=window.__escapeTest;const model=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;return scene.invoke('TubeDesigner.ExportSelected',{targetDirectory:target,partEntityIds:model.parts.map(p=>p.entityId)});},target);
  assert.equal(repeated.exportedCount,model.parts.length);assert.equal(repeated.draftOnly,false);
  assert.equal((await page.evaluate(path=>window.__escapeTest.project.save(path,{timeoutMs:120000}),path)).saved,true);
  await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();});
  await page.screenshot({path:join(root,test.name+'.png')});
  const result={name:test.name,path,target,model,files,resized:generated.resized,initialParameters:generated.initialParameters,types:model.parts.length,physicalQuantity:model.parts.reduce((n,p)=>n+p.quantity,0),passed:true};
  report.cases.push(result);writeFileSync(join(root,test.name+'-report.json'),JSON.stringify(result,null,2));
  console.log(JSON.stringify({name:test.name,types:result.types,quantity:result.physicalQuantity,passed:true}));
 }
 report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;await page.screenshot({path:join(root,'failure.png')}).catch(()=>{});}
finally{writeFileSync(join(root,'acceptance-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
