// Real standalone CEF/native business path; only file-picker destinations are controlled.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync,readdirSync,statSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_CONTINUOUS_EVIDENCE||'output/tests/customer-feedback-29bb19-20261024/native');
mkdirSync(root,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.ICAX_CONTINUOUS_CDP||'http://127.0.0.1:19459');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
assert(page);page.setDefaultTimeout(120000);
const resumed=process.env.ICAX_CONTINUOUS_RESUME==='1';
const previous=resumed?readdirSync(root).filter(n=>n.endsWith('-report.json')&&n!=='acceptance-report.json')
 .map(n=>JSON.parse(readFileSync(join(root,n),'utf8'))).filter(c=>c.passed&&c.model&&c.name):[];
const report={nativeBusinessMocked:false,pickerPathsControlled:true,checks:[],cases:previous};
const write=()=>writeFileSync(join(root,'acceptance-report.json'),JSON.stringify(report,null,2));
const check=(name,details={})=>{report.checks.push({name,passed:true,...details});write();console.log(JSON.stringify({name,passed:true}));};
const walk=dir=>readdirSync(dir).flatMap(n=>{const p=join(dir,n);return statSync(p).isDirectory()?walk(p):[p];});
const init=()=>page.evaluate(async()=>{
 const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
 const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1715000000}});
 await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();
 const project=product.getProject(window.__icaxWorkbench.getState().projectId);
 window.__continuousTest={project,scene:project.getMainScene()};
});
const list=()=>page.evaluate(async()=>(await window.__continuousTest.scene.invoke('TubeDesigner.List',{})).tubeDesigner);
const validate=(model,test)=>{
 const anchor=model.members.find(m=>m.properties?.['tubeDesigner.escapeWindowLayout']);assert(anchor);
 const layout=anchor.properties['tubeDesigner.escapeWindowLayout'];
 assert.equal(layout.position,test.position);
 assert.equal(model.product.modelOutdated,false);assert.equal(model.product.partsOutdated,false);
 const fixed=model.members.filter(m=>m.stableKey.startsWith('access_door.fixed_frame.'));
 if(test.face==='single'&&test.pattern==='grid'){
  assert.equal(layout.fixedFrameConstruction.railKeys.length,test.position.endsWith('_middle')?2:1);
  assert(layout.fixedFrameConstruction.removedGridStubKeys.length>0);
  const keys=new Set(model.members.map(m=>m.stableKey));
  assert(layout.fixedFrameConstruction.removedGridStubKeys.every(k=>!keys.has(k)));
 }else if(test.pattern==='vertical')assert.deepEqual(layout.fixedFrameConstruction.removedGridStubKeys,[]);
 assert(fixed.every(m=>m.profile.width===25));
 assert.equal(model.members.filter(m=>m.stableKey.startsWith('access_door.leaf.frame.')).length,4);
 assert.equal(model.product.parameters.doorGap,6);
 assert(model.parts.length>0);return layout;
};
const tests=[
 {name:'default-continuous-row',sameStock:false,face:'single',pattern:'grid',position:'center_middle',ui:true},
 {name:'same-stock-center',sameStock:true,face:'single',pattern:'grid',position:'center_middle'},
 {name:'same-stock-left-top',sameStock:true,face:'single',pattern:'grid',position:'left_top'},
 {name:'same-stock-right-bottom',sameStock:true,face:'single',pattern:'grid',position:'right_bottom'},
 {name:'vertical-without-added-rails',sameStock:true,face:'single',pattern:'vertical',position:'center_middle'},
 {name:'three-left-face',sameStock:true,face:'three',surface:'left',pattern:'grid',position:'right_top'},
 {name:'five-bottom-face',sameStock:true,face:'five',surface:'bottom',pattern:'grid',position:'left_bottom'},
];
report.cases=report.cases.filter(c=>tests.some(t=>t.name===c.name));
const requested=(process.env.ICAX_CONTINUOUS_CASES||'').split(',').filter(Boolean);
try{
 await page.setViewportSize({width:1600,height:1000});
 for(const test of tests.filter(t=>(!requested.length||requested.includes(t.name))&&!report.cases.some(c=>c.name===t.name))){
  console.log(JSON.stringify({case:test.name,stage:'create'}));
  await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);await init();
  if(test.ui){
   await page.locator('[data-command-id="designer.add"]').click();
   const dialog=page.locator('[data-tube-designer-add-dialog]');
   await dialog.locator('[data-tube-designer-template-id="single-face-security-window"]').click();
   const ready=()=>page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
   await ready();
   for(const x of ['left','center','right'])for(const z of ['top','middle','bottom']){
    await dialog.locator('[data-tube-designer-parameter="doorPosition"]').selectOption(x+'_'+z);await ready();
    const preview=await page.evaluate(position=>window.__continuousTest.scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{
     templateId:'single-face-security-window',parameters:{doorPosition:position}}),x+'_'+z);
    assert.equal(preview.parameters.doorPosition,x+'_'+z);
   }
   check('nine positions accepted by actual native add-dialog preview',{positions:9});
   assert.equal(await dialog.locator('[data-tube-designer-parameter="doorFrameStructure"]').count(),0);
   await dialog.locator('[data-tube-designer-parameter="doorPosition"]').selectOption(test.position);await ready();
   await dialog.screenshot({path:join(root,'nine-position-add-dialog.png')});
   await dialog.locator('[data-cam-action="tube-designer-confirm-add"]').click();await dialog.waitFor({state:'hidden'});
   await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.templateId==='single-face-security-window');
  }else{
   const parameters={faceType:test.face,infillPattern:test.pattern,doorPosition:test.position,horizontalWidth:test.sameStock?25:22,horizontalDepth:test.sameStock?25:22};
   if(test.pattern==='vertical')Object.assign(parameters,{doorClearWidth:600,doorClearHeight:600});
   if(test.face!=='single')Object.assign(parameters,{doorClearWidth:260,doorClearHeight:260,depth:700,leftWidth:700,rightWidth:700,
    [test.face==='three'?'accessDoorFace3':'accessDoorFace5']:test.surface});
   await page.evaluate(async parameters=>{
    const scene=window.__continuousTest.scene;
    const preview=await scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:'single-face-security-window',parameters});
    await scene.invoke('TubeDesigner.GeneratePreview',{templateId:'single-face-security-window',...preview.parameters});
   },parameters);
  }
  const created=await list(),id=created.product.entityId;
  console.log(JSON.stringify({case:test.name,stage:'disassemble',members:created.members.length}));
  const model=await page.evaluate(async id=>{
   const scene=window.__continuousTest.scene;
   await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[id]});
   const first=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
   await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[id]});
   const second=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
   if(JSON.stringify(first.parts.map(p=>[p.entityId,p.quantity]))!==JSON.stringify(second.parts.map(p=>[p.entityId,p.quantity])))throw Error('Repeated disassembly changed identities or quantities');
   return second;
  },id);
  const layout=validate(model,test),path=join(root,test.name+'.ictd');
  assert((await page.evaluate(path=>window.__continuousTest.project.save(path),path)).saved);
  await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},path);
  await page.locator('[data-action="open-start-center"]').click();await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,id);await init();
  const reopened=await list();validate(reopened,test);
  assert.deepEqual(reopened.members.map(m=>m.stableKey),model.members.map(m=>m.stableKey));
  const target=join(root,test.name+'-export');mkdirSync(target,{recursive:true});
  await page.evaluate(target=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>target;},target);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  await page.waitForFunction(target=>document.body.innerText.includes(target),target);
  const files=walk(target);assert.equal(files.filter(p=>p.endsWith('.step')).length,model.parts.length);assert.equal(files.filter(p=>p.endsWith('.xlsx')).length,1);
  await page.evaluate(async view=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView(view);await window.__icaxWorkbench.fitView();},test.surface==='bottom'?'bottom':test.surface==='left'?'left':'front');
  await page.screenshot({path:join(root,test.name+'.png')});
  const entry={...test,path,target,model:reopened,layout,files,types:model.parts.length,physicalQuantity:model.parts.reduce((n,p)=>n+p.quantity,0),passed:true};
  report.cases.push(entry);writeFileSync(join(root,test.name+'-report.json'),JSON.stringify(entry,null,2));write();
  check(test.name+' real create repeat-disassemble save-reopen STEP and workbook',{members:model.members.length,parts:model.parts.length});
 }
 report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:join(root,'failure.png')}).catch(()=>{});}
finally{write();console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
