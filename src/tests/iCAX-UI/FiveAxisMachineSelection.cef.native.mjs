import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9482'),page=browser.contexts()[0].pages()[0],out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',path=out+'/六轴机械臂SDF演示.i5cam';
 await page.setViewportSize({width:1600,height:850});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await page.mouse.up();
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===path&&mv?.scene?.machines?.length===2&&!mv.pending;},path);
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 const machines=await page.evaluate(()=>mv.scene.machines.map(m=>({id:m.entityId,name:m.name}))),results=[];
 for(const machine of machines){
  await page.locator('[data-five-machine-instance]').selectOption(machine.id);await page.waitForFunction(()=>!mv.fiveMachine.busy);
  const ids=await page.evaluate(id=>{const m=mv.scene.machines.find(m=>m.entityId===id);return {parent:m.joints.find(j=>j.jointName==='X'||j.jointName==='J2').entityId,leaf:m.joints.find(j=>j.jointName==='A'||j.jointName==='J6').entityId};},machine.id);
  await page.locator('[data-five-machine-field="observeAutomatic"]').uncheck();
  await page.evaluate(()=>{window.selectionCanvas=mv.viewport.renderer.domElement;window.selectionObjects=new Map(mv.viewport.sceneObjects);window.selectionGeometries=new Map(mv.viewport.geometryObjects);});
  await page.locator(`[data-five-command="five.machine.part:${ids.parent}"]`).click();
  const assertSingle=async id=>{
   const result=await page.evaluate(()=>({ids:[...mv.viewport.selectedObjectIds],tree:[...document.querySelectorAll('.five-machine-tree-label[aria-pressed="true"]')].map(e=>e.closest('[data-machine-node]').dataset.machineNode),tinted:[...mv.viewport.sceneObjects].filter(([,o])=>{let tinted=false;o.traverse(n=>{if(n.isMesh&&n.userData.selectionMaterialState)tinted=true;});return tinted;}).map(([id])=>id),part:document.querySelector('[data-five-machine-field="box:part"]').value}));
   assert.deepEqual(result.ids,[id]);assert.deepEqual(result.tree,[id]);assert.deepEqual(result.tinted,[id]);assert.equal(result.part,id);return result;
  };
  const parent=await assertSingle(ids.parent);
  // Collapse the whole tree, then physically click the leaf in the 3D canvas.
  await page.locator(`[data-five-command="five.machine.tree-toggle:${machine.id}"]`).click();
  const point=await page.evaluate(async id=>{
   const THREE=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),vp=mv.viewport,object=vp.sceneObjects.get(id),center=new THREE.Box3().setFromObject(object).getCenter(new THREE.Vector3()),ray=new THREE.Raycaster();
   for(const theta of [0,Math.PI/2,Math.PI,-Math.PI/2]){
    vp.setCameraState({target:{x:center.x,y:center.y,z:center.z},radius:650,theta,phi:1.2});vp.camera.updateMatrixWorld(true);vp.scene.updateMatrixWorld(true);
    const rect=vp.renderer.domElement.getBoundingClientRect();
    for(let y=-100;y<=100;y+=5)for(let x=-100;x<=100;x+=5){
     const px=rect.width/2+x,py=rect.height/2+y;ray.setFromCamera(new THREE.Vector2(px/rect.width*2-1,1-py/rect.height*2),vp.camera);
     const hit=ray.intersectObjects([...vp.sceneObjects.values()].filter(o=>o.visible),false)[0];if(hit?.object.userData.objectId===id)return {x:rect.left+px,y:rect.top+py};
    }
   }throw Error('No visible surface for '+id);
  },ids.leaf);
  await page.evaluate(()=>{const field=document.querySelector('[data-five-machine-field="post:name"]'),right=document.querySelector('.cam-info-pane');field.focus({preventScroll:true});field.setSelectionRange(1,3);right.scrollTop=500;window.selectionRightScroll=right.scrollTop;window.selectionField=field;});
  await page.mouse.click(point.x,point.y);
  await page.waitForFunction(id=>mv.fiveMachineSelectedPartId===id,ids.leaf);
  const leaf=await assertSingle(ids.leaf);
  const retained=await page.evaluate(id=>{
   const row=document.querySelector(`[data-machine-node="${id}"] > .five-machine-tree-row`),pane=row.closest('.cam-context-pane'),rect=row.getBoundingClientRect(),bounds=pane.getBoundingClientRect();
   return {visible:rect.top>=bounds.top&&rect.bottom<=bounds.bottom,canvas:selectionCanvas===mv.viewport.renderer.domElement,objects:[...selectionObjects].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o),geometries:[...selectionGeometries].every(([id,g])=>mv.viewport.geometryObjects.get(id)===g),field:selectionField===document.querySelector('[data-five-machine-field="post:name"]'),rightScroll:document.querySelector('.cam-info-pane').scrollTop,expectedRightScroll:selectionRightScroll};
  },ids.leaf);
  for(const key of ['visible','canvas','objects','geometries','field'])assert.equal(retained[key],true,key);assert.equal(retained.rightScroll,retained.expectedRightScroll);
  // A routine local panel update must not restore a former whole-machine selection.
  await page.locator('[data-five-machine-field="box:part"]').selectOption(ids.parent);await assertSingle(ids.parent);
  await page.locator('[data-five-machine-field="box:part"]').selectOption(ids.leaf);await assertSingle(ids.leaf);
  // Selecting a faded occluder, then another part, must not leave the restored
  // opaque material orange when automatic transparency is turned off.
  await page.locator('[data-five-machine-field="observeAutomatic"]').check();
  let fadedId='';
  for(const theta of [0,Math.PI/2,Math.PI,-Math.PI/2]){
   await page.evaluate(theta=>{const camera=mv.viewport.getCameraState();mv.viewport.setCameraState({...camera,radius:2800,theta,phi:1.35});mv.fiveMachineObservation.signature='';},theta);
   await page.waitForTimeout(150);
   fadedId=await page.evaluate(()=>{const entry=[...mv.fiveMachineObservation.faded].find(([o])=>o.userData.objectId!==mv.fiveMachineSelectedPartId);if(!entry)return '';const[o,e]=entry;window.fadedSelectionProbe={object:o,original:e.original,colors:(Array.isArray(e.original)?e.original:[e.original]).map(m=>m.color?.getHex())};return o.userData.objectId;});
   if(fadedId)break;
  }
  if(fadedId){
   await page.evaluate(id=>__icaxAppShell.executeRibbonCommand('five.machine.part:'+id),fadedId);await assertSingle(fadedId);
   await page.evaluate(id=>__icaxAppShell.executeRibbonCommand('five.machine.part:'+id),ids.leaf);await assertSingle(ids.leaf);
  }
  await page.locator('[data-five-machine-field="observeAutomatic"]').uncheck();await page.waitForTimeout(120);
  if(fadedId){const restored=await page.evaluate(()=>{const p=fadedSelectionProbe;return {same:p.object.material===p.original,colors:(Array.isArray(p.object.material)?p.object.material:[p.object.material]).map(m=>m.color?.getHex()),expected:p.colors};});assert.equal(restored.same,true);assert.deepEqual(restored.colors,restored.expected);}
  await page.locator('.cam-info-pane').evaluate(e=>e.scrollTop=0);
  await page.screenshot({path:out+'/'+(machine.name.includes('IR-1600')?'robot':'gantry')+'-part-selection.png'});
  results.push({machine:machine.name,parent,leaf,retained,fadedSelectionRestored:!!fadedId});
 }
 assert.ok(results.some(r=>r.fadedSelectionRestored),'selection through temporary transparent materials');
 writeFileSync(out+'/selection-acceptance.json',JSON.stringify({pass:true,results},null,2));console.log(JSON.stringify({pass:true,machines:results.map(r=>r.machine),singlePartHighlight:true,realCanvasClickRevealsTree:true,colliderEditorLinked:true,nodesAndRightScrollPreserved:true,fadedMaterialSelectionRestored:true}));process.exit(0);
}catch(error){console.error(error);process.exit(1);}
