import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyFixtureCam({page,before,output,report,errors,readState}){
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest&&!!fixture.view.viewport?.sceneObjects.size,null,{timeout:120000});
  const area=async id=>{await page.evaluate(id=>fixture.context.actions.selectRibbonTab(id),id);await idle();};
  await area('workpiece');
  await idle();
  console.log('Native viewport ready');
  await page.waitForFunction(()=>fixture.view.scene.domain.fixtures.every(f=>fixture.view.viewport.sceneObjects.has(f.id)));
  const appearance=()=>page.evaluate(()=>{
    const v=fixture.view,vp=v.viewport;return {fixtures:v.scene.domain.fixtures.map(f=>{const o=vp.sceneObjects.get(f.id);return {id:f.id,opacity:o.material.opacity,transparent:o.material.transparent,depthWrite:o.material.depthWrite,edgeOpacity:o.userData.meshEdges?.material.opacity,pickable:vp.options.isObjectPickable(o.userData.instance)};}),workpieceOpacity:vp.sceneObjects.get(v.scene.activePlacementId).material.opacity};
  });
  report.workpiece=await appearance();assert.ok(report.workpiece.fixtures.every(f=>f.opacity===1&&f.pickable));
  // Start from an actual fixture selection and retain the same viewport.
  await page.evaluate(()=>{const v=fixture.view;fixture.canvas=v.viewport.renderer.domElement;v.selectedSceneObjectId=v.scene.domain.fixtures[0].id;v.viewport.setSelectedObjectIds([v.selectedSceneObjectId]);});
  await area('toolpath');report.cam=await appearance();
  assert.ok(report.cam.fixtures.every(f=>f.opacity===.4&&f.transparent&&!f.depthWrite&&f.edgeOpacity===.4&&!f.pickable));
  assert.equal(report.cam.workpieceOpacity,report.workpiece.workpieceOpacity);
  assert.equal(await page.evaluate(()=>fixture.canvas===fixture.view.viewport.renderer.domElement),true);
  assert.equal(await page.evaluate(()=>fixture.view.selectedSceneObjectId),'');
  assert.equal(await page.evaluate(()=>[...fixture.view.viewport.selectedObjectIds].some(id=>fixture.view.scene.domain.fixtures.some(f=>f.id===id))),false);
  report.cases.push('CAM switches existing and generated fixture meshes and edges to 40% opacity, disables depth writing/picking, clears prior fixture selection and retains the workpiece and canvas');
  await page.evaluate(()=>{
    const vp=fixture.view.viewport,original=vp.options.onPick;vp.options.onPick=(...args)=>{fixture.lastPick=args[0]?.objectId??null;fixture.pickHits=args[3].map(h=>h.object.userData.objectId);return original(...args);};
    fixture.spots=async direction=>{vp.setViewDirection(direction);vp.fitView();await new Promise(requestAnimationFrame);vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);const b=vp.renderer.domElement.getBoundingClientRect(),ids=new Set(fixture.view.scene.domain.fixtures.map(f=>f.id)),workpiece=fixture.view.scene.activePlacementId;let only=null,through=null;
      for(let y=.15;y<.9;y+=.02)for(let x=.15;x<.9;x+=.02){vp.raycaster.setFromCamera({x:x*2-1,y:1-y*2},vp.camera);const hits=vp.raycaster.intersectObjects([...vp.sceneObjects.values()].filter(o=>o.visible),false),first=hits[0]?.object.userData.objectId;if(!ids.has(first))continue;const p={x:b.left+x*b.width,y:b.top+y*b.height};if(hits.some(h=>h.object.userData.objectId===workpiece))through??=p;else only??=p;}return {only,through};};
  });
  const bottom=await page.evaluate(()=>fixture.spots([0,0,-1]));assert.ok(bottom.through,'actual fixture must occlude the workpiece for the pass-through check');
  await page.mouse.click(bottom.through.x,bottom.through.y);await idle();
  assert.equal(await page.evaluate(()=>fixture.lastPick),before.placements.find(p=>p.setupId===before.activeSetupId).id);
  assert.equal(await page.evaluate(()=>fixture.pickHits.some(id=>fixture.view.scene.domain.fixtures.some(f=>f.id===id))),false);
  const angled=await page.evaluate(()=>fixture.spots([1,-1,.7]));assert.ok(angled.only);
  await page.mouse.click(angled.only.x,angled.only.y);await idle();assert.equal(await page.evaluate(()=>fixture.lastPick),null);
  assert.equal(await page.evaluate(()=>fixture.view.selectedSceneObjectId),'');
  report.cases.push('Real pointer clicks pass through the base plate to the workpiece; fixture-only pixels act as blank space and deliver no fixture hits');
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.8]);fixture.view.viewport.fitView();});
  const cut=await page.evaluate(async()=>{
    const {nearestCutLine}=await import((fixture.moduleBase||'/src/')+'apps/five-axis-cam/webpage/cutSelection.mjs');const vp=fixture.view.viewport,b=vp.renderer.domElement.getBoundingClientRect();
    for(let y=.2;y<.85;y+=.008)for(let x=.2;x<.85;x+=.008){const e={clientX:b.left+x*b.width,clientY:b.top+y*b.height},c=nearestCutLine(fixture.view,e);if(c)return {id:c.intentId,x:e.clientX,y:e.clientY};}return null;
  });assert.ok(cut);await page.mouse.click(cut.x,cut.y);await idle();
  assert.equal(await page.evaluate(async id=>{const {workspaceState}=await import((fixture.moduleBase||'/src/')+'apps/five-axis-cam/webpage/workspaceModel.mjs');return workspaceState(fixture.view).selectedIntentIds.includes(id);},cut.id),true);
  await page.screenshot({path:resolve(output,'cam-transparent-fixtures.png')});
  report.cases.push('Actual viewport pointer still selects a cutting path on the saved workpiece');
  await area('workpiece');report.restored=await appearance();assert.deepEqual(report.restored,report.workpiece);
  const restoredSpot=await page.evaluate(()=>fixture.spots([1,-1,.7]));assert.ok(restoredSpot.only);await page.mouse.click(restoredSpot.only.x,restoredSpot.only.y);await idle();
  const selectedUnit=await page.evaluate(()=>fixture.view.fiveAxisFixtures?.selected);
  assert.ok(before.fixtureUnits.some(u=>u.id===selectedUnit));
  await area('toolpath');assert.deepEqual(await appearance(),report.cam);
  report.cases.push('Returning to the workpiece page restores opaque editable fixtures; repeated CAM entry reapplies the reference appearance');
  const after=await readState();assert.deepEqual(after.fixtures,before.fixtures);assert.deepEqual(after.fixtureUnits,before.fixtureUnits);assert.deepEqual(after.placements,before.placements);
  assert.deepEqual(errors,[]);report.passed=true;
}
