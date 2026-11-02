import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');const {chromium}=require('playwright');
const root=resolve('output/tests/customer-escape-window-20261023');const browser=await chromium.connectOverCDP('http://127.0.0.1:19427');const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
const report={businessMocked:false};
try{
 report.checks=await page.evaluate(async()=>{
  const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{initialCallId:1800000000}});await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();const scene=product.getProject(window.__icaxWorkbench.getState().projectId).getMainScene();
  const checks=[];
  for(const patch of [{doorColumnCount:99},{doorRowCount:99},{doorColumnCount:1,doorGap:100}]){
   let rejected='';try{await scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:'single-face-security-window',parameters:patch});}catch(e){rejected=e.message;}
   if(!/占格|容纳|可用|格栅/.test(rejected))throw new Error('Invalid opening was not rejected with its real constraint: '+JSON.stringify(patch));checks.push({patch,rejected});
  }
  for(const patch of [{accessDoorEnabled:false,doorColumnCount:99,doorRowCount:99},{infillPattern:'horizontal'},{infillPattern:'vertical'},
      {doorClearWidth:9999,doorClearHeight:9999,doorLeft:9999,doorBottom:9999,doorHorizontalTopCenterOffset:9999,doorVerticalLeftCenterOffset:9999}]){
   const preview=await scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:'single-face-security-window',parameters:patch});
   for(const [k,v] of Object.entries(patch))if(preview.parameters[k]!==v)throw new Error('Host input changed: '+k);
   checks.push({patch,previewPassed:true});
  }
  const current=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
  const parameters={...current.product.parameters,doorPosition:current.product.parameters.doorPosition==='left_bottom'?'right_top':'left_bottom'};
  await scene.invoke('TubeDesigner.UpdateProductParameters',{productEntityId:current.product.entityId,parameters});
  const changed=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
  if(changed.product.entityId!==current.product.entityId||changed.product.parameters.doorPosition!==parameters.doorPosition||!changed.product.modelOutdated||!changed.product.partsOutdated)throw new Error('Position edit did not invalidate the existing product');
  let locked='';try{await scene.invoke('TubeDesigner.UpdateProductParameters',{productEntityId:current.product.entityId,
   parameters:{...parameters,infillPattern:parameters.infillPattern==='grid'?'horizontal':'grid'}});}catch(e){locked=e.message;}
  if(!/结构.*固定/.test(locked))throw new Error('Post-creation structure change was not rejected');checks.push({positionEditable:true,structureLocked:locked});
  return checks;
 });
 assert.equal(report.checks.length,8);report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
finally{writeFileSync(join(root,'native-constraints-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error}));await browser.close();}
