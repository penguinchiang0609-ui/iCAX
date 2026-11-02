import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';

// Run against an already open native dense workpiece, including its fixture
// preview. Compare the exact same buffers, camera and pointer positions.
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/cam-interaction-performance-20261026');
mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9586');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
assert.ok(page,'standalone native CAM window');
const report={passed:false,standaloneCef:true,realNativeQueue:true,errors:[]};
page.on('pageerror',e=>report.errors.push(e.message));
try{
 await page.evaluate(async()=>{
  const base=new URL('../../../',location.href).href;
  const {findProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
  const THREE=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js');
  window.densePicking={base,THREE};
  Object.defineProperty(densePicking,'view',{get:()=>findProjectView(__icaxAppShell.getState().activeProjectId)});
 });
 const nativeBefore=await page.evaluate(()=>densePicking.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
 report.comparison=await page.evaluate(()=>{
  const {view:v,THREE}=densePicking,vp=v.viewport;
  const object=vp.sceneObjects.get(v.scene.activePlacementId),geometry=object.geometry;
  if(!geometry.boundsTree)throw Error('Dense mesh must be indexed by the production viewport during loading');
  vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
  const rect=vp.renderer.domElement.getBoundingClientRect();
  geometry.computeBoundingBox();
  const center=geometry.boundingBox.getCenter(new THREE.Vector3()).applyMatrix4(object.matrixWorld).project(vp.camera);
  const event={clientX:rect.left+(center.x+1)*rect.width/2,clientY:rect.top+(1-center.y)*rect.height/2};
  const camera=vp.getCameraState(),index=geometry.index;
  const indexBefore=index.array.slice(),position=geometry.attributes.position,positionVersion=position.version;
  const accelerated=object.raycast,legacy=THREE.Mesh.prototype.raycast;
  const queries=[[-.09,-.07],[0,0],[.08,.06],[.13,-.1],[-.12,.09]].map(([x,y])=>({x:center.x+x,y:center.y+y}));
  const legacyHits=[],acceleratedHits=[],legacyMs=[],acceleratedMs=[];
  const serialize=hits=>hits.map(h=>({faceIndex:h.faceIndex,distance:h.distance,point:h.point.toArray(),normal:h.face.normal.toArray(),materialIndex:h.face.materialIndex,uv:h.uv?.toArray()})).sort((a,b)=>a.distance-b.distance||a.faceIndex-b.faceIndex);
  let legacyPivot,acceleratedPivot,legacyPivotMs,acceleratedPivotMs;
  try{
   for(const q of queries){
    vp.raycaster.setFromCamera(q,vp.camera);
    object.raycast=legacy;let t=performance.now();legacyHits.push(serialize(vp.raycaster.intersectObject(object,false)));legacyMs.push(performance.now()-t);
    object.raycast=accelerated;t=performance.now();acceleratedHits.push(serialize(vp.raycaster.intersectObject(object,false)));acceleratedMs.push(performance.now()-t);
   }
   object.raycast=legacy;let t=performance.now();legacyPivot=vp.captureOrbitPivot(event);legacyPivotMs=performance.now()-t;
   object.raycast=accelerated;t=performance.now();acceleratedPivot=vp.captureOrbitPivot(event);acceleratedPivotMs=performance.now()-t;
  }finally{object.raycast=accelerated;}
  const buffersUnchanged=geometry.index===index&&index.array.every((value,i)=>value===indexBefore[i])&&geometry.attributes.position===position&&position.version===positionVersion;
  const cameraUnchanged=JSON.stringify(camera)===JSON.stringify(vp.getCameraState());
  densePicking.event=event;
  return {triangles:index.count/3,vertices:position.count,previewMeshes:v.fixtureCandidatePreview?.group.children.filter(o=>o.isMesh).length,
   queries,legacyHits,acceleratedHits,legacyMs,acceleratedMs,legacyPivot,acceleratedPivot,legacyPivotMs,acceleratedPivotMs,
   buffersUnchanged,cameraUnchanged,event,indexed:true,geometryId:object.userData.geometryId};
 });
 const c=report.comparison;
 assert.ok(c.triangles>=50000);assert.ok(c.indexed);assert.ok(c.buffersUnchanged);assert.ok(c.cameraUnchanged);
 let actualHitCount=0;
 for(let i=0;i<c.legacyHits.length;++i){
  const a=c.acceleratedHits[i],b=c.legacyHits[i];assert.equal(a.length,b.length);actualHitCount+=a.length;
  for(let j=0;j<a.length;++j){
   assert.equal(a[j].faceIndex,b[j].faceIndex);assert.equal(a[j].materialIndex,b[j].materialIndex);
   assert.ok(Math.abs(a[j].distance-b[j].distance)<1e-6);
   for(const key of ['point','normal','uv'])if(b[j][key])for(let k=0;k<b[j][key].length;++k)assert.ok(Math.abs(a[j][key][k]-b[j][key][k])<1e-6);
  }
 }
 assert.ok(actualHitCount>0);assert.ok(c.legacyPivot);assert.ok(c.acceleratedPivot);
 assert.equal(c.acceleratedPivot.hitCount,c.legacyPivot.hitCount);
 for(let i=0;i<3;++i)assert.ok(Math.abs(c.acceleratedPivot.point[i]-c.legacyPivot.point[i])<1e-6);
 assert.ok(c.acceleratedPivotMs<50,`pivot took ${c.acceleratedPivotMs} ms`);
 assert.ok(c.legacyPivotMs/c.acceleratedPivotMs>5,'dense pivot acceleration');

 // Exercise real CEF pointer events and record the synchronous viewport handler
 // interval plus the time to the next painted frame on this physical runtime.
 await page.evaluate(()=>{
  const vp=densePicking.view.viewport,canvas=vp.renderer.domElement;
  densePicking.input={downs:[],ups:[],frames:[]};
  const starts=new WeakMap();
  const begin=e=>{if(e.target===canvas)starts.set(e,performance.now());};
  const end=e=>{if(!starts.has(e))return;const elapsed=performance.now()-starts.get(e);densePicking.input[e.type==='pointerdown'?'downs':'ups'].push(elapsed);};
  const frame=e=>{const start=performance.now();requestAnimationFrame(()=>densePicking.input.frames.push(performance.now()-start));};
  window.addEventListener('pointerdown',begin,true);window.addEventListener('pointerdown',end);
  window.addEventListener('pointerup',begin,true);window.addEventListener('pointerup',end);
  canvas.addEventListener('pointermove',frame);
  densePicking.cleanup=()=>{window.removeEventListener('pointerdown',begin,true);window.removeEventListener('pointerdown',end);window.removeEventListener('pointerup',begin,true);window.removeEventListener('pointerup',end);canvas.removeEventListener('pointermove',frame);};
  densePicking.originalCamera=vp.getCameraState();
 });
 const e=c.event;
 await page.mouse.move(e.clientX,e.clientY);
 await page.mouse.down({button:'right'});
 await page.mouse.move(e.clientX+90,e.clientY-45,{steps:16});
 await page.mouse.up({button:'right'});
 await page.waitForTimeout(250);
 report.input=await page.evaluate(()=>{
  const vp=densePicking.view.viewport,changed=JSON.stringify(vp.getCameraState())!==JSON.stringify(densePicking.originalCamera);
  const result={...densePicking.input,cameraRotated:changed,error:densePicking.view.error};
  densePicking.cleanup();vp.setCameraState(densePicking.originalCamera);return result;
 });
 assert.equal(report.input.cameraRotated,true);assert.ok(report.input.downs.length);assert.ok(Math.max(...report.input.downs)<100);assert.equal(report.input.error,'');
 const nativeAfter=await page.evaluate(()=>densePicking.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
 for(const key of ['placements','workpieces','intentToolpaths','fixtures','fixtureUnits'])assert.deepEqual(nativeAfter[key],nativeBefore[key],key);
 report.nativeStateUnchanged=true;
 assert.deepEqual(report.errors,[]);report.passed=true;
 await page.screenshot({path:resolve(output,'dense-picking-verified.png')});
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'dense-picking-failure.png')}).catch(()=>{});}
finally{
 await page.evaluate(()=>densePicking?.cleanup?.()).catch(()=>{});
 writeFileSync(resolve(output,'dense-picking-report.json'),JSON.stringify(report,null,2));
 console.log(JSON.stringify({passed:report.passed,error:report.error,triangles:report.comparison?.triangles,legacyPivotMs:report.comparison?.legacyPivotMs,acceleratedPivotMs:report.comparison?.acceleratedPivotMs,input:report.input,nativeStateUnchanged:report.nativeStateUnchanged},null,2));
 await browser.close();
}
