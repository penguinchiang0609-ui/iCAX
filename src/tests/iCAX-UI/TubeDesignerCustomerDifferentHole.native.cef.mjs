// Independent project for the negative geometry control; never a Save As clone.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync,readdirSync,statSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_CUSTOMER_JOINT_EVIDENCE||'output/tests/customer-joints-merge-20261023/accepted');
const browser=await chromium.connectOverCDP('http://127.0.0.1:19425');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));page.setDefaultTimeout(120000); await page.setViewportSize({width:1600,height:1000});
const result={independentProject:true,businessMocked:false};
try{
 await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 const state=await page.evaluate(async()=>{
  const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge);await app.getState();
  const product=app.getProduct('icax.tube-designer');await product.getState();const project=product.getProject(window.__icaxWorkbench.getState().projectId);const scene=project.getMainScene();
  window.__differentHole={project,scene};
  const created=(await scene.invoke('TubeDesigner.GeneratePreview',{templateId:'modular-guardrail',handrailWidth:50,handrailDepth:50})).tubeDesigner;
  const post=created.members.find(p=>p.stableKey==='post.0001');
  await scene.invoke('TubeDesigner.SaveSketch',{productEntityId:created.product.entityId,targetMemberId:post.entityId,
   sketch:{schema:'icax.tube-sketch',schemaVersion:1,kind:'side',unit:'mm',coordinateSpace:'arc-length-axial',length:post.length,faceHeight:160,
    entities:[{id:'different-hole',kind:'circle',cx:20,cy:100,radius:3,closed:true}]}});
  return (await scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[created.product.entityId]})).tubeDesigner;
 });
 assert.equal(state.parts.length,5);assert.equal(state.parts.reduce((n,p)=>n+p.quantity,0),32);
 const posts=state.parts.filter(p=>Math.abs(p.length-1150)<.001);assert.deepEqual(posts.map(p=>p.unitQuantity).sort((a,b)=>a-b),[1,3]);
 result.model=state;result.path=join(root,'同长不同孔-独立工程.ictd');result.target=join(root,'同长不同孔-独立-export');mkdirSync(result.target,{recursive:true});
 const exported=await page.evaluate(async({path,target})=>{
  const {project,scene}=window.__differentHole;const s=(await scene.invoke('TubeDesigner.List',{})).tubeDesigner;
  const result=await scene.invoke('TubeDesigner.ExportSelected',{partEntityIds:s.parts.map(p=>p.entityId),targetDirectory:target});await project.save(path);return result;
 },result);
 assert.equal(exported.exportedCount,5);assert.equal(exported.draftOnly,false);
 const walk=d=>readdirSync(d).flatMap(n=>{let p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});result.files=walk(result.target);
 result.name='同长不同孔';result.types=5;result.physicalQuantity=32;result.passed=true;
 const report=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));report.differentHole=result;
 writeFileSync(join(root,'acceptance-report.json'),JSON.stringify(report,null,2));
}catch(e){result.error=e.stack;process.exitCode=1;}
finally{writeFileSync(join(root,'different-hole-report.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({passed:result.passed,error:result.error}));await browser.close();}
