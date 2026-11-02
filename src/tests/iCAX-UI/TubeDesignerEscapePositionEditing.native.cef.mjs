// Existing product -> scene position selector -> regenerate -> real parts -> reopen.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_ESCAPE_POSITION_EVIDENCE||'output/tests/escape-position-editing');
mkdirSync(root,{recursive:true});
const cold=process.env.ICAX_ESCAPE_POSITION_REOPEN_ONLY==='1';
let browser;
for(let attempt=0;!browser;attempt++){
 try{browser=await chromium.connectOverCDP(process.env.ICAX_ESCAPE_POSITION_CDP||'http://127.0.0.1:19459');}
 catch(error){if(attempt>=40)throw error;await new Promise(r=>setTimeout(r,250));}
}
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
assert(page);page.setDefaultTimeout(120000);
const report={nativeBusinessMocked:false,editingThroughSceneAnnotation:true,coldProcess:cold,checks:[]};
const savedPath=join(root,'existing-window-position-edited.ictd');
const write=()=>writeFileSync(join(root,cold?'cold-reopen-report.json':'acceptance-report.json'),JSON.stringify(report,null,2));
const init=()=>page.evaluate(async()=>{
 const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
 const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1726000000}});
 await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();
 const project=product.getProject(window.__icaxWorkbench.getState().projectId);
 window.__positionEdit={project,scene:project.getMainScene()};
});
const list=()=>page.evaluate(async()=>(await window.__positionEdit.scene.invoke('TubeDesigner.List',{})).tubeDesigner);
const open=async path=>{
 await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},path);
 await page.locator('[data-action="open-start-center"]').click();
 await page.locator('[data-action="choose-project"]').click();
 await page.waitForFunction(path=>{
  const shell=window.__icaxAppShell.getState();
  return shell.activeProjectPath.replaceAll('\\','/').toLowerCase()===path.replaceAll('\\','/').toLowerCase()
   && shell.pendingCount===0&&window.__icaxWorkbench?.getState().projectId===shell.activeProjectId;
 },path);
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.templateId==='single-face-security-window');
 await init();
};
const ready=()=>page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated===false);
const validate=(model,position,id)=>{
 assert.equal(model.product.entityId,id);assert.equal(model.product.parameters.doorPosition,position);
 assert.equal(model.product.modelOutdated,false);assert.equal(model.product.partsOutdated,false);
 const opening=model.members.find(m=>m.properties?.['tubeDesigner.escapeWindowLayout']).properties['tubeDesigner.escapeWindowLayout'];
 const [x,z]=position.split('_');
 assert.deepEqual(opening.sharedOuterSides,Object.fromEntries(['bottom','left','right','top'].map(k=>[k,k===x||k===z])));
 assert.equal(opening.fixedFrameConstruction.horizontal,'one_member_per_continuous_row');
 assert.equal(opening.fixedFrameConstruction.railKeys.length,z==='middle'?2:1);
 for(const side of ['bottom','left','right','top'])assert.equal(model.members.some(m=>m.stableKey.startsWith('access_door.fixed_frame.'+side+'.')),!opening.sharedOuterSides[side]);
 for(const key of opening.fixedFrameConstruction.railKeys){
  const member=model.members.find(m=>m.stableKey===key).properties['assemblyFrame.member'];
  assert.equal(member.start[0],model.product.parameters.frameWidth/2);
  assert.equal(member.end[0],model.product.parameters.width-model.product.parameters.frameWidth/2);
 }
 assert(model.parts.length>0);return opening;
};
const disassemble=async()=>{
 await page.locator('[data-command-id="designer.disassemble"]').click();
 await page.locator('[data-cam-action="tube-designer-batch-disassembly-confirm"]').click();
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().parts.length>0&&!window.__icaxWorkbench.getTubeDesignerState().product.partsOutdated);
};
try{
 await page.setViewportSize({width:1600,height:1000});
 if(cold){
  const previous=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));
  await open(savedPath);const model=await list();validate(model,previous.finalPosition,previous.productId);
  assert.deepEqual(model.members.map(m=>m.entityId),previous.finalMemberIds);
  assert.deepEqual(model.parts.map(p=>[p.entityId,p.quantity]),previous.finalParts);
  report.productId=model.product.entityId;report.position=model.product.parameters.doorPosition;
  report.checks.push({name:'cold reopen preserves position, member IDs and real parts',passed:true});
 }else{
  const input=process.env.ICAX_ESCAPE_POSITION_INPUT;assert(input,'An existing project is required');
  await open(input);let model=await list();const id=model.product.entityId;
  report.input=input;report.productId=id;
  const template=model.templates.find(t=>t.id==='single-face-security-window');
  assert(!template.extensions.parameterDependencies.creationOnly.includes('doorPosition'));
  assert(!template.extensions.parameterDependencies.manufacturingOnly.includes('doorPosition'));
  if(model.product.modelOutdated){await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();await ready();}
  await disassemble();model=await list();validate(model,'center_middle',id);
  await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();window.__positionEditCanvas=document.querySelector('canvas');});
  const entry=page.locator('[data-cam-action="tube-designer-edit-scene-specification"][data-tube-designer-parameter-key="doorPosition"]');
  assert.equal(await page.locator('[data-tube-designer-parameter-form] [data-tube-designer-parameter="doorPosition"]').count(),0);
  for(const position of ['left_top','right_bottom','center_middle']){
   await entry.waitFor({state:'visible'});const box=await entry.boundingBox();assert(box);
   await page.mouse.dblclick(box.x+box.width/2,box.y+box.height/2);
   const editor=page.locator('[data-tube-designer-scene-specification-input="doorPosition"]');
   await editor.waitFor({state:'visible'});assert.equal(await editor.evaluate(n=>n.tagName),'SELECT');
   assert.equal(await editor.locator('option').count(),9);
   if(position==='left_top')await page.screenshot({path:join(root,'existing-window-position-selector.png')});
   await editor.selectOption(position);
   await page.waitForFunction(position=>{const p=window.__icaxWorkbench.getTubeDesignerState().product;return p?.parameters.doorPosition===position&&p.modelOutdated&&p.partsOutdated;},position);
   model=await list();assert.equal(model.product.entityId,id);
   assert.equal(model.product.modelOutdated,true);assert.equal(model.product.partsOutdated,true);
   assert(await page.locator('[data-cam-action="tube-designer-confirm-update"]').isVisible());
   await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();await ready();
   await disassemble();model=await list();const opening=validate(model,position,id);
   assert(await page.evaluate(()=>window.__positionEditCanvas===document.querySelector('canvas')),'Position editing replaced the canvas');
   await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.fitView();});
   await page.screenshot({path:join(root,position+'.png')});
   report.checks.push({name:'existing instance position edited, regenerated and disassembled',position,productId:id,
    members:model.members.length,parts:model.parts.length,sharedOuterSides:opening.sharedOuterSides,passed:true});write();
   console.log(JSON.stringify({position,members:model.members.length,passed:true}));
  }
  let rejected='';try{await page.evaluate(async parameters=>window.__positionEdit.scene.invoke('TubeDesigner.UpdateProductParameters',{
   productEntityId:window.__icaxWorkbench.getTubeDesignerState().product.entityId,parameters}),{...model.product.parameters,infillPattern:'horizontal'});}catch(e){rejected=e.message;}
  assert.match(rejected,/结构.*固定/);report.checks.push({name:'actual structure remains locked',passed:true});
  report.finalPosition=model.product.parameters.doorPosition;
  report.finalMemberIds=model.members.map(m=>m.entityId);report.finalParts=model.parts.map(p=>[p.entityId,p.quantity]);
  assert((await page.evaluate(path=>window.__positionEdit.project.save(path),savedPath)).saved);
  await open(savedPath);const reopened=await list();validate(reopened,report.finalPosition,id);
  assert.deepEqual(reopened.members.map(m=>m.entityId),report.finalMemberIds);
  assert.deepEqual(reopened.parts.map(p=>[p.entityId,p.quantity]),report.finalParts);
  report.checks.push({name:'save and reopen preserve the edited existing instance',passed:true});
 }
 report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:join(root,cold?'cold-failure.png':'failure.png')}).catch(()=>{});}
finally{write();console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
