import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');const root=resolve(process.env.ICAX_CUSTOMER_JOINT_EVIDENCE);mkdirSync(root,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:19425');const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
try{
 await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 const path=join(root,'圆管护栏-相贯与合并.ictd');
 const model=await page.evaluate(async path=>{
  const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge);await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();const project=product.getProject(window.__icaxWorkbench.getState().projectId);const scene=project.getMainScene();
  const input={templateId:'modular-guardrail'};for(const [prefix,size] of [['handrail',50],['post',40],['rail',30],['infill',20]]){input[prefix+'ProfileType']='round';input[prefix+'Width']=size;}
  const created=(await scene.invoke('TubeDesigner.GeneratePreview',input)).tubeDesigner.product;
  await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[created.entityId]});
  const result=(await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[created.entityId]})).tubeDesigner;
  await project.save(path);return result;
 },path);
 assert.equal(model.parts.length,4);assert.equal(model.parts.reduce((n,p)=>n+p.quantity,0),32);
 writeFileSync(join(root,'acceptance-report.json'),JSON.stringify({passed:true,cases:[{name:'圆管护栏-相贯与合并',path,types:4,physicalQuantity:32,model}]},null,2));
 console.log('ROUND_SAVE_CREATED');
}finally{await browser.close();}
