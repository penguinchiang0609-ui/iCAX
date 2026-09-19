// Real descriptor validation, preview and production decomposition in an isolated scene.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { copyFileSync, mkdirSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { renderDesignerRightPane } from '../../apps/tube-designer/webpage/designerViews.mjs';
import { renderProductTemplateLibraryRightPane } from '../../apps/tube-designer/webpage/templateLibrary.mjs';
const root=fileURLToPath(new URL('../../../',import.meta.url));
const native=resolve(root,'tmp/security-window-frames-native');
const runtime=resolve(native,'runtime');
mkdirSync(runtime,{recursive:true});
for(const name of readdirSync(resolve(root,'src/x64/Debug')).filter(n=>/\.dll$/i.test(n)))
  copyFileSync(resolve(root,'src/x64/Debug',name),resolve(runtime,name));
copyFileSync(resolve(native,'SecurityWindowFramesBridge.exe'),resolve(runtime,'SecurityWindowFramesBridge.exe'));
const bridge=spawn(resolve(runtime,'SecurityWindowFramesBridge.exe'),[],{cwd:root,windowsHide:true,
  env:{...process.env,PATH:runtime+';'+resolve(root,'src/x64/Debug')+';'+process.env.PATH},stdio:['pipe','pipe','pipe']});
let sequence=0,stderr='';
const pending=new Map();
bridge.stderr.on('data',b=>stderr+=b);
const failAll=e=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(e);}pending.clear();};
bridge.on('error',failAll);
bridge.on('exit',code=>{if(pending.size)failAll(new Error(`Bridge exited ${code}: ${stderr}`));});
createInterface({input:bridge.stdout}).on('line',line=>{
  let response;try{response=JSON.parse(line);}catch{return failAll(new Error(line));}
  const p=pending.get(response.id);if(!p)return;
  pending.delete(response.id);clearTimeout(p.timer);
  response.ok?p.resolve(response.result):p.reject(new Error(response.error));
});
const invoke=(method,payload={})=>new Promise((resolve,reject)=>{
  const id=++sequence;
  pending.set(id,{resolve,reject,timer:setTimeout(()=>{pending.delete(id);reject(new Error(`Timeout: ${method}`));},180000)});
  bridge.stdin.write(JSON.stringify({id,method,payload})+'\n');
});
try{
  const templateId='single-face-security-window';
  const template=(await invoke('GetTemplateDescriptor',{templateId})).template;
  assert.ok(template.parameters.find(p=>p.key==='frameManufacturingMode').presentation.choiceConditions);
  const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs');
  const browser=await chromium.launch({headless:true,channel:'msedge'});
  try{
    const page=await browser.newPage();
    for(const faceType of ['two','three','five']){
      const parameters={...Object.fromEntries(template.parameters.map(p=>[p.key,p.defaultValue])),
        faceType,frameManufacturingMode:'spatial_v_notch'};
      const product={entityId:'test-window',templateId,name:'test',parameters,quantity:1};
      const view={scene:{tubeDesigner:{templates:[template],product,activeProductId:product.entityId}},
        tubeDesignerParameterPanelProductId:product.entityId,
        tubeDesignerExpandedParameterGroups:template.groups.map(g=>g.key),
        tubeDesignerProductTemplateLibrary:{scope:'system',selectedId:templateId,parameterDrafts:{[templateId]:parameters}}};
      await page.setContent(`<section id="product">${renderDesignerRightPane({},view)}</section><section id="library">${renderProductTemplateLibraryRightPane({},view)}</section>`);
      for(const [id,attr] of [['product','data-tube-designer-parameter'],['library','data-tube-template-library-parameter']]){
        const select=page.locator(`#${id} select[${attr}="frameManufacturingMode"]`);
        assert.equal(await select.inputValue(),faceType==='five'?'plane_v_notch':'spatial_v_notch');
        assert.equal(await select.locator('option[value="spatial_v_notch"]').count(),faceType==='five'?0:1);
        assert.equal(await page.locator(`#${id} [${attr}="spatialFrameMaximumStockLength"]`).count(),0);
      }
      assert.equal(parameters.frameManufacturingMode,'spatial_v_notch');
    }
    console.log('native descriptor -> browser: product/library mode choices and retained drafts passed');
  }finally{await browser.close();}
  if(!process.env.ICAX_SECURITY_FRAME_PRODUCTION_ONLY) for(const [face,mode,count] of [
    ['single','plane_v_notch',1],['two','plane_v_notch',5],['three','plane_v_notch',6],
    ['five','plane_v_notch',6],['five','spatial_v_notch',6],
    ['two','spatial_v_notch',2],['three','spatial_v_notch',3],
  ]){
    const parameters={faceType:face,frameManufacturingMode:mode,accessDoorEnabled:true};
    const preview=await invoke('GenerateProductTemplatePreview',{templateId,parameters});
    assert.equal(preview.templateId,templateId);
    assert.equal(preview.parameters.faceType,face);
    assert.equal(preview.parameters.frameManufacturingMode,mode);
    const keys=preview.items.map(i=>i.stableKey||i.key);
    assert.equal(keys.filter(k=>k?.startsWith('outer_frame.')).length,count);
    const plan=await invoke('GetProductManufacturingPlan',{templateId,parameters});
    assert.ok(plan.partCount>count);
    console.log(`native preview+plan ${face}/${mode}: outer ${count}, parts ${plan.partCount}`);
  }
  const longPreview=await invoke('GenerateProductTemplatePreview',{templateId,
    parameters:{faceType:'three',frameManufacturingMode:'spatial_v_notch',height:6000,accessDoorEnabled:false}});
  assert.equal(longPreview.parameters.height,6000);
  assert.equal(Object.hasOwn(longPreview.parameters,'spatialFrameMaximumStockLength'),false);
  assert.equal(longPreview.items.filter(i=>(i.stableKey||i.key)?.startsWith('outer_frame.')).length,3);
  console.log('native long spatial frame: no stock-length parameter or generation restriction');
  if(process.env.ICAX_SECURITY_FRAME_PRODUCTION!=='0'){
    for(const face of ['five','two','three']){
      const parameters={faceType:face,frameManufacturingMode:face==='five'?'plane_v_notch':'spatial_v_notch',
        accessDoorEnabled:true,verticalMaximumCenterSpacing:600,
        sideVerticalMaximumCenterSpacing:600,topBottomRodMaximumCenterSpacing:600};
      const generated=await invoke('GeneratePreview',{templateId,...parameters});
      assert.equal(generated.tubeDesigner.product.parameters.faceType,face);
      assert.equal(generated.tubeDesigner.product.parameters.frameManufacturingMode,parameters.frameManufacturingMode);
      const result=await invoke('Disassemble');
      assert.ok(result.tubeDesigner);
      const expected=face==='five'?6:face==='two'?2:3;
      assert.equal(result.geometryChecks.filter(c=>c.key.startsWith('outer_frame.')).length,expected);
      for(const check of result.geometryChecks){
        assert.equal(check.valid,true,check.key);
        assert.equal(check.solids,1,check.key);
        assert.ok(check.volume>0,check.key);
      }
      console.log(`native production boolean ${face}: ${expected} outer parts, valid connected BReps`);
    }
  }
  console.log('Security-window native frame acceptance passed');
}finally{bridge.stdin.end();bridge.kill();}
