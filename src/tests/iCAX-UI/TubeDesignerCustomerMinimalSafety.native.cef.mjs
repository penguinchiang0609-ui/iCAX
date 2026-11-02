// Real descriptor and scene contracts for the customer's escape leaf.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_ESCAPE_EVIDENCE||'output/tests/customer-remaining-20261023/escape-final');mkdirSync(root,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:19427');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
const report={nativeBusinessMocked:false,checks:[]};
try{
 await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 const result=await page.evaluate(async()=>{
  const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
  const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{initialCallId:1700000000}});await app.getState();
  const product=app.getProduct('icax.tube-designer');await product.getState();
  const scene=product.getProject(window.__icaxWorkbench.getState().projectId).getMainScene();
  const templateId='single-face-security-window',checks=[];
  const preview=parameters=>scene.invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId,parameters:{infillPattern:'horizontal',width:600,height:1400,doorClearWidth:200,doorClearHeight:240,doorBottom:650,doorPosition:'right_middle',...parameters}});
  for(const parameters of [{doorGap:-1},{doorGap:100},{doorClearWidth:1000}]){
   let error='';try{await preview(parameters);}catch(e){error=e.message;}
   if(!error)throw Error('Invalid escape geometry accepted: '+JSON.stringify(parameters));
   checks.push({name:'invalid escape dimensions rejected by native preview',parameters,error,passed:true});
  }
  const off=await preview({accessDoorEnabled:false,doorClearWidth:9999,doorClearHeight:9999,doorBottom:9999,doorLeafFrameWidth:299,doorGap:100});
  await scene.invoke('TubeDesigner.GeneratePreview',{templateId,...off.parameters});
  let state=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
  if(state.members.some(m=>m.stableKey.startsWith('access_door.leaf.')))throw Error('Disabled escape leaf generated from retained drafts');
  checks.push({name:'disabled escape ignores retained opening and leaf drafts',passed:true});
  const normalized=await preview({doorPosition:'left_middle',doorGap:9});
  await scene.invoke('TubeDesigner.GeneratePreview',{templateId,...normalized.parameters});
  state=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
  if(JSON.stringify(state.product.parameters)!==JSON.stringify(normalized.parameters))throw Error('Private parameters leaked into saved instance');
  checks.push({name:'native normalized parameters persisted exactly',passed:true});
  await scene.invoke('TubeDesigner.UpdateProductParameters',{productEntityId:state.product.entityId,parameters:{...state.product.parameters,doorPosition:'right_middle'}});
  const changed=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
  if(changed.product.entityId!==state.product.entityId||changed.product.parameters.doorPosition!=='right_middle'||!changed.product.modelOutdated||!changed.product.partsOutdated)throw Error('Existing escape position was not editable');
  checks.push({name:'native position edit preserves the instance and expires model and parts',passed:true});
  return checks;
 });
 report.checks=result;assert.equal(result.length,6);report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
finally{writeFileSync(join(root,'native-safety-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
