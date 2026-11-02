// Real creation controls and face-local scene dimensions in standalone CEF.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync,readdirSync,statSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_ESCAPE_EVIDENCE||'output/tests/customer-universal-escape-20261023/editor');mkdirSync(root,{recursive:true});
let browser;for(let i=0;!browser;i++){try{browser=await chromium.connectOverCDP('http://127.0.0.1:19427');}catch(e){if(i>60)throw e;await new Promise(r=>setTimeout(r,250));}}
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));page.setDefaultTimeout(120000);
const report={nativeBusinessMocked:false,pickerPathsControlled:true,cases:[],checks:[]};
const cases=[{name:'editor-single-vertical',face:'single',pattern:'vertical',position:'right_top',edit:'doorRightMargin',value:80,view:'front'},
 {name:'editor-three-left-grid',face:'three',surface:'left',pattern:'grid',position:'center_middle',edit:'doorHorizontalOffset',value:25,view:'left'},
 {name:'editor-five-bottom-horizontal',face:'five',surface:'bottom',pattern:'horizontal',position:'left_bottom',edit:'doorBottomMargin',value:70,view:'bottom'}];
const walk=d=>readdirSync(d).flatMap(n=>{const p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});
const ready=()=>page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
try{
 await page.setViewportSize({width:1600,height:1000});
 for(const c of cases){
  console.log(JSON.stringify({name:c.name,stage:'creation-controls'}));
  await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
  await page.locator('[data-command-id="designer.add"]').click();
  const dialog=page.locator('[data-tube-designer-add-dialog]');
  await dialog.locator('[data-tube-designer-template-id="single-face-security-window"]').click();await ready();
  const field=k=>dialog.locator(`[data-tube-designer-parameter="${k}"]`);
  const select=async(k,v)=>{await field(k).selectOption(v);await ready();};
  const fill=async(k,v)=>{await field(k).fill(String(v));await field(k).press('Tab');await ready();};
  await select('infillPattern','horizontal');await fill('doorClearWidth',c.face==='single'?600:260);await fill('doorClearHeight',260);
  await select('faceType',c.face);
  if(c.surface)await select(c.face==='three'?'accessDoorFace3':'accessDoorFace5',c.surface);
  await select('infillPattern',c.pattern);
  for(const x of ['left','center','right'])for(const z of ['bottom','middle','top']){
   await select('doorPosition',x+'_'+z);
   const expected=[{left:'doorLeftMargin',right:'doorRightMargin',center:'doorHorizontalOffset'}[x],
    {bottom:'doorBottomMargin',top:'doorTopMargin',middle:'doorVerticalOffset'}[z]];
   for(const k of ['doorLeftMargin','doorRightMargin','doorBottomMargin','doorTopMargin','doorHorizontalOffset','doorVerticalOffset'])assert.equal(await field(k).count(),expected.includes(k)?1:0);
  }
  await select('doorPosition',c.position);
  for(const k of ['doorLeft','doorBottom','doorUOffset','doorVOffset'])assert.equal(await field(k).count(),0);
  report.checks.push({name:c.name+' creation condition switching',positions:9,passed:true});
  await dialog.screenshot({path:join(root,c.name+'-add.png')});
  await dialog.locator('[data-cam-action="tube-designer-confirm-add"]').click();await dialog.waitFor({state:'hidden'});
  await page.waitForFunction(face=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.faceType===face,c.face);
  await page.evaluate(async view=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView(view);await window.__icaxWorkbench.fitView();},c.view);
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const label=page.locator(`[data-tube-designer-parameter-key="${c.edit}"][data-cam-action="tube-designer-edit-scene-specification"]`).first();
  const box=await label.boundingBox();assert(box);await page.mouse.dblclick(box.x+box.width/2,box.y+box.height/2);
  const input=page.locator(`[data-tube-designer-scene-specification-input="${c.edit}"]`);
  await input.fill(String(c.value));await input.press('Enter');
  await page.waitForFunction(c=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters[c.edit]===c.value,c);
  await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();
  await page.waitForFunction(()=>!window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated);
  if(c.face==='single'){
   const leafCount=()=>page.evaluate(()=>window.__icaxWorkbench.getTubeDesignerState().members.filter(m=>m.stableKey.startsWith('access_door.leaf.vertical.')).length);
   const before=await leafCount();
   await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));});
   const caption=page.locator('[data-tube-designer-parameter-key="doorVerticalMaximumCenterSpacing"][data-cam-action="tube-designer-edit-scene-specification"]:visible').first();
   await caption.waitFor({state:'visible'});
   const b=await caption.boundingBox();assert(b);await page.mouse.dblclick(b.x+b.width/2,b.y+b.height/2);
   const input=page.locator('[data-tube-designer-scene-specification-input="doorVerticalMaximumCenterSpacing"]');await input.fill('80');await input.press('Enter');
   await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.doorVerticalMaximumCenterSpacing===80);
   await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();await page.waitForFunction(()=>!window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated);
   assert(await leafCount()>before);report.checks.push({name:'vertical leaf spacing still changes real rods',before,after:await leafCount(),passed:true});
  }
  assert.equal(await page.locator('[data-tube-designer-parameter-form] [data-tube-designer-parameter="doorPosition"]').count(),0);
  report.checks.push({name:c.name+' scene dimension edited',parameter:c.edit,value:c.value,passed:true});
  await page.locator('[data-command-id="designer.disassemble"]').click();await page.locator('[data-cam-action="tube-designer-batch-disassembly-confirm"]').click();
  await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().parts.length>0&&!document.querySelector('[data-command-id="designer.export-active-product-parts"]')?.disabled);
  const path=join(root,c.name+'.ictd'),target=join(root,c.name+'-export');mkdirSync(target,{recursive:true});
  const model=await page.evaluate(async path=>{
   const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const a=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1700000000}});await a.getState();const product=a.getProduct('icax.tube-designer');await product.getState();const project=product.getProject(window.__icaxWorkbench.getState().projectId);window.__universalEditor={project,scene:project.getMainScene()};
   let model=(await window.__universalEditor.scene.invoke('TubeDesigner.List',{})).tubeDesigner;
   await window.__universalEditor.scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[model.product.entityId]});
   model=(await window.__universalEditor.scene.invoke('TubeDesigner.List',{})).tubeDesigner;await project.save(path,{timeoutMs:120000});return model;
  },path);
  await page.evaluate(target=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>target;},target);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();await page.waitForFunction(target=>document.body.innerText.includes(target),target);
  await page.evaluate(async view=>{await window.__icaxWorkbench.setStandardView(view);await window.__icaxWorkbench.fitView();},c.view);
  await page.screenshot({path:join(root,c.name+'.png')});
  const entry={name:c.name,path,target,model,files:walk(target),types:model.parts.length,physicalQuantity:model.parts.reduce((n,p)=>n+p.quantity,0),passed:true};
  assert.equal(entry.files.filter(f=>f.endsWith('.step')).length,entry.types);
  report.cases.push(entry);writeFileSync(join(root,c.name+'-report.json'),JSON.stringify(entry,null,2));console.log(JSON.stringify({name:c.name,passed:true}));
 }
 report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;await page.screenshot({path:join(root,'failure.png')}).catch(()=>{});}
finally{writeFileSync(join(root,'acceptance-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
