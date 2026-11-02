import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {existsSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out=resolve(process.env.SURFACE_OUTPUT||'output/tests/machine-surface-20261024');mkdirSync(out,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9496'),page=browser.contexts()[0].pages()[0];
const reopen=process.argv.includes('--reopen'),results=[];
const bind=async path=>page.waitForFunction(async path=>{const s=__icaxAppShell.getState();if(s.activeProjectPath.replaceAll('\\','/')!==path.replaceAll('\\','/'))return false;const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(s.activeProjectId);return mv?.scene?.machineDefinitions&&!mv.pending&&!s.pendingCount;},path);
const settle=async()=>{await page.waitForFunction(()=>!mv.pending&&!mv.fiveMachine?.busy);assert.equal(await page.evaluate(()=>mv.fiveMachine?.error||''),'');};
const snapshot=()=>page.evaluate(()=>{const machine=mv.scene.machines[0],ids=new Set(machine.elements.map(e=>e.entityId));let triangles=0,duplicates=0,meshes=0;const visible=[];
 for(const [id,object] of mv.viewport.sceneObjects){if(!ids.has(id))continue;object.traverse(o=>{if(!o.isMesh||!o.geometry)return;meshes++;const p=o.geometry.attributes.position,ix=o.geometry.index,seen=new Set(),count=ix?ix.count:p.count;for(let i=0;i<count;i+=3){const v=[0,1,2].map(c=>{const k=ix?ix.getX(i+c):i+c;return [p.getX(k),p.getY(k),p.getZ(k)].join(',')}).sort().join('|');if(seen.has(v))duplicates++;seen.add(v);triangles++;}visible.push({id,visible:o.visible,triangles:count/3,opacity:o.material.opacity,polygonOffset:o.material.polygonOffset});});}
 return {machineId:machine.entityId,parts:machine.elements.filter(e=>e.kind!=='machine').length,triangles,duplicates,meshes,visible};});
try{
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 for(const model of [{key:'lc3015',source:'industrial-ac',triangles:39966,parts:10},{key:'ir1600',source:'industrial-arm',triangles:14384,parts:9}]){
  const path=resolve(out,model.key+'-surface.i5cam');
  if(reopen||existsSync(path))await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
  else await page.evaluate(path=>__icaxAppShell.createProject({productId:'icax.five-axis-cam',projectName:'机床表面验收',projectPath:path}),path);
  await page.waitForTimeout(500);await bind(path);await page.locator('[data-tab-id="simulation"]').click();await page.waitForTimeout(500);await bind(path);
  if(!reopen&&await page.evaluate(()=>mv.scene.machines.length===0)){
   const id=await page.evaluate(source=>mv.scene.machineDefinitions.find(d=>d.sourcePath.includes(source)).id,model.source);
   await page.locator(`[data-five-command="five.machine.definition-select:${id}"]`).click();
   await page.locator('[data-command-id="five.machine-panel.instantiate"]').click();
  }else await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
  await page.waitForFunction(()=>mv.scene.machines.length===1);await settle();
  await page.waitForFunction(n=>mv.viewport.sceneObjects.size>=n,model.parts);
  const geometry=await snapshot();assert.equal(geometry.meshes,model.parts);assert.equal(geometry.triangles,model.triangles);assert.equal(geometry.duplicates,0);assert.ok(geometry.visible.every(o=>!o.polygonOffset));
  await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1)});
  const base=await page.evaluate(()=>mv.viewport.getCameraState()),views=[];
  for(const projectionMode of ['perspective','orthographic'])for(const theta of [-1.0,.6,2.2,3.8]){
   await page.evaluate(({base,projectionMode,theta})=>{mv.viewport.setCameraState({...base,projectionMode,theta});mv.fiveMachineObservation.signature='';},{base,projectionMode,theta});
   await page.waitForTimeout(180);
   views.push(await page.evaluate(()=>({camera:mv.viewport.getCameraState(),near:mv.viewport.camera.near,far:mv.viewport.camera.far,logarithmic:mv.viewport.renderer.capabilities.logarithmicDepthBuffer,faded:mv.fiveMachineObservation.faded.size,meshCount:mv.viewport.sceneObjects.size})));
   if(theta===2.2)await page.screenshot({path:resolve(out,`${model.key}-${projectionMode}${reopen?'-reopened':''}.png`)});
  }
  if(model.key==='lc3015')assert.ok(views.some(v=>v.faded>0),'exercise actual automatic occlusion');
  assert.ok(views.every(v=>v.logarithmic),'use depth encoding that survives close inspection');
  const close=[];
  for(const factor of [.2,.3,.45])for(const theta of [-1.0,-.8,-.6]){
   await page.evaluate(({base,factor,theta})=>mv.viewport.setCameraState({...base,projectionMode:'perspective',radius:base.radius*factor,theta,phi:1.05}),{base,factor,theta});await page.waitForTimeout(180);
   close.push(await page.evaluate(()=>({camera:mv.viewport.getCameraState(),near:mv.viewport.camera.near,far:mv.viewport.camera.far,faded:mv.fiveMachineObservation.faded.size})));
   if(factor===.2&&theta===-.8){
    await page.screenshot({path:resolve(out,`${model.key}-close${reopen?'-reopened':''}.png`)});
    if(model.key==='lc3015'&&!reopen){
     const comparison=await page.evaluate(async()=>{const THREE=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=mv.viewport,size=v.renderer.getSize(new THREE.Vector2()),images={};for(const logarithmicDepthBuffer of [false,true]){const renderer=new THREE.WebGLRenderer({antialias:true,logarithmicDepthBuffer,preserveDrawingBuffer:true});renderer.setSize(size.x,size.y);renderer.setClearColor(v.renderer.getClearColor(new THREE.Color()));renderer.outputColorSpace=v.renderer.outputColorSpace;renderer.toneMapping=v.renderer.toneMapping;renderer.render(v.scene,v.camera);images[logarithmicDepthBuffer?'after':'before']=renderer.domElement.toDataURL('image/png');renderer.dispose();renderer.forceContextLoss();}return {images,near:v.camera.near,far:v.camera.far};});
     for(const [key,data] of Object.entries(comparison.images))writeFileSync(resolve(out,`native-depth-${key}.png`),Buffer.from(data.split(',')[1],'base64'));
     writeFileSync(resolve(out,'native-depth-comparison.json'),JSON.stringify({sameNativeScene:true,near:comparison.near,far:comparison.far},null,2));
    }
   }
  }
  if(model.key==='lc3015')assert.ok(close.some(v=>v.near===.001),'inspect from inside the enclosure bounds');
  await page.evaluate(base=>mv.viewport.setCameraState(base),base);
  const box=await page.evaluate(()=>{const r=mv.viewport.renderer.domElement.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};});
  const x=box.x+box.width*.52,y=box.y+box.height*.55;
  await page.mouse.up({button:'middle'});await page.mouse.up({button:'right'});
  await page.mouse.move(x,y);await page.mouse.down({button:'right'});await page.mouse.move(x+100,y+40,{steps:16});await page.mouse.up({button:'right'});await page.mouse.wheel(0,-180);
  await page.waitForFunction(base=>{const c=mv.viewport.getCameraState();return c.theta!==base.theta&&c.radius!==base.radius;},base);
  const navigation=await page.evaluate(()=>mv.viewport.getCameraState());assert.notEqual(navigation.theta,base.theta);assert.notEqual(navigation.radius,base.radius);
  const control=page.locator('[data-five-machine-field="observeAutomatic"]');await control.uncheck();await page.waitForTimeout(180);assert.equal(await page.evaluate(()=>mv.fiveMachineObservation.faded.size),0);await control.check();
  await page.evaluate(base=>{mv.viewport.setCameraState({...base,theta:-1.0,phi:1.05,radius:base.radius*.85});},base);await page.waitForTimeout(180);
  await page.screenshot({path:resolve(out,`${model.key}-final${reopen?'-reopened':''}.png`)});
  assert.equal((await snapshot()).triangles,model.triangles);
  if(!reopen)await page.evaluate(path=>mv.viewportSceneProxy.invoke('Project.Save',{projectPath:path}),path);
  results.push({model:model.key,geometry,views,close,navigation,saved:true,reopened:reopen});
 }
 writeFileSync(resolve(out,reopen?'cef-reopened.json':'cef-surfaces.json'),JSON.stringify({pass:true,results},null,2));
 console.log(JSON.stringify({pass:true,reopen,models:results.map(r=>({model:r.model,triangles:r.geometry.triangles,duplicates:r.geometry.duplicates,views:r.views.length}))}));
 await browser.close();
}catch(error){await browser.close();console.error(error);process.exitCode=1;}
