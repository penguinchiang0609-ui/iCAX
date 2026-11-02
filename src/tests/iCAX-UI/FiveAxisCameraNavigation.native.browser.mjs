import assert from 'node:assert/strict';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyCameraNavigation({page,command,read,rpc,refresh}) {
  const cases=[],idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const camera=()=>page.evaluate(()=>fixture.view.viewport.getCameraState());
  const pivotView=()=>page.evaluate(()=>{
    const vp=fixture.view.viewport,pivot=vp.navigation.orbitPivot;if(!pivot)return null;
    const point=vp.cameraState.target.clone().fromArray(pivot.point);
    return{pivot,pixel:point.clone().project(vp.camera).toArray().slice(0,2),distance:point.distanceTo(vp.camera.position)};
  });
  const {pick,numeric,locate,drag:dragHandle}=gizmoDriver(page);
  const drag=async(dx,dy,button='middle',release=true,steps=12,ctrl=false,at=null)=>{
    if(ctrl)await page.keyboard.down('Control');
    const r=await page.locator('.icax-three-viewport canvas').first().boundingBox(),x=at?.x??r.x+r.width*.35,y=at?.y??r.y+r.height*.4;
    await page.mouse.move(x,y);await page.mouse.down({button});const start=await pivotView();await page.mouse.move(x+dx,y+dy,{steps});
    if(start){const after=await pivotView();assert.deepEqual(after.pivot,start.pivot);after.pixel.forEach((v,i)=>assert.ok(Math.abs(v-start.pixel[i])<1e-7));assert.ok(Math.abs(after.distance-start.distance)<1e-6);}
    if(release){await page.mouse.up({button});if(ctrl)await page.keyboard.up('Control');}
    return {x:x+dx,y:y+dy};
  };
  const preview=()=>page.evaluate(()=>{
    const v=fixture.view,d=v.fivePlacement;
    return {mode:d.mode,axis:d.axis,value:d.value,base:d.base,original:d.original,
      transform:v.viewport.sceneObjects.get(d.placementId).matrix.toArray(),
      request:d.request??null};
  });
  const navigate=async(label,handle=false)=>{
    const before=await camera();await drag(30,20,'middle',true,8,false,handle?await locate('X'):null);
    const orbited=await camera();assert.ok(orbited.theta!==before.theta||orbited.phi!==before.phi,`${label}: orbit available`);
    assert.equal(orbited.radius,before.radius);
    await drag(20,15,'middle',true,8,true,handle?await locate('X'):null);
    const panned=await camera();assert.notDeepEqual(panned.target,orbited.target,`${label}: pan available`);
    for(const key of ['theta','phi','radius'])assert.equal(panned[key],orbited[key]);
    await page.mouse.wheel(0,120);await page.waitForFunction(r=>fixture.view.viewport.getCameraState().radius!==r,panned.radius);
  };
  const starting=await camera(),domain=await read();
  for(const projection of ['perspective','orthographic']){
    await command('five.tool.select');await page.locator(`button[data-projection-mode="${projection}"]`).click();
    const base=await camera();
    await drag(60,25,'right');assert.deepEqual(await camera(),base,'right drag no longer moves the camera');
    await drag(35,25,'middle',true,12,true);
    const panned=await camera();assert.notDeepEqual(panned.target,base.target);
    for(const key of ['theta','phi','radius'])assert.equal(panned[key],base[key],`Ctrl + middle preserves ${key}`);
    await page.evaluate(c=>fixture.view.viewport.setCameraState(c),base);
    for(const [dx,dy,axis] of [[120,7,'horizontal'],[-120,-7,'horizontal'],[7,120,'vertical'],[-7,-120,'vertical'],[70,70,'free']]){
      await page.evaluate(c=>fixture.view.viewport.setCameraState(c),base);
      await drag(dx,dy,'middle',false);
      const after=await camera();assert.equal(await page.evaluate(()=>fixture.view.viewport.root.dataset.orbitAxis),axis);
      if(axis==='horizontal'){assert.equal(after.phi,base.phi);assert.notEqual(after.theta,base.theta);}
      else if(axis==='vertical'){assert.equal(after.theta,base.theta);assert.notEqual(after.phi,base.phi);}
      else {assert.notEqual(after.theta,base.theta);assert.notEqual(after.phi,base.phi);}
      assert.equal(after.radius,base.radius);
      await page.mouse.up({button:'middle'});assert.equal(await page.evaluate(()=>fixture.view.viewport.root.dataset.orbitAxis),undefined);
    }
    await page.evaluate(c=>fixture.view.viewport.setCameraState(c),base);
    const p=await drag(80,3,'middle',false),beforeTurn=await camera();
    await page.mouse.move(p.x,p.y+60,{steps:12});const afterTurn=await camera();
    assert.notEqual(afterTurn.phi,beforeTurn.phi);assert.equal(afterTurn.theta,beforeTurn.theta);await page.mouse.up({button:'middle'});
    for(const mode of ['translate','rotate','scale']){
      await page.evaluate(c=>fixture.view.viewport.setCameraState(c),base);
      await command(`five.tool.${mode}`);assert.equal(await page.evaluate(()=>fixture.view.viewport.cameraNavigationEnabled),true);
      await pick('X');const value=mode==='scale'?'1.25':'6';await numeric().fill(value);
      const draft=await preview();
      await page.evaluate(()=>{fixture.navigationCanvas=fixture.view.viewport.renderer.domElement;fixture.navigationInput=document.querySelector('[data-five-placement-field="value"]');});
      await navigate(`${projection}/${mode}/background`);
      await navigate(`${projection}/${mode}/handle`,true);
      assert.equal(await numeric().inputValue(),value);assert.deepEqual(await preview(),draft,'camera gestures preserve the transform draft');
      assert.equal(await page.evaluate(()=>fixture.navigationCanvas===fixture.view.viewport.renderer.domElement&&fixture.navigationInput===document.querySelector('[data-five-placement-field="value"]')),true);
      assert.deepEqual(await read(),domain,'navigation never submits or cancels a transform');
      const beforeHandle=await camera();await dragHandle('X',24,false);
      assert.deepEqual(await camera(),beforeHandle,'left handle drag changes the object, not the camera');
      assert.notDeepEqual((await preview()).transform,draft.transform);
      await page.keyboard.press('Escape');await page.mouse.up();
      assert.deepEqual(await preview(),draft,'Escape cancels only the left handle gesture');
      await command('five.tool.select');assert.equal(await page.evaluate(()=>fixture.view.viewport.cameraNavigationEnabled),true);
      assert.deepEqual(await read(),domain,'uncommitted transform discarded; camera gestures never mutate Scene');
      await page.evaluate(c=>fixture.view.viewport.setCameraState(c),base);
    }
    cases.push(`${projection}: middle orbit, Ctrl + middle pan and wheel zoom remain available during Move/Rotate/Scale, including over handles; numeric drafts, native state and DOM nodes preserved; left handle editing and Escape remain independent; orbit snap and right-drag mapping retained`);
  }
  // Releasing or cancelling a camera gesture must not leave a tool dragging.
  await command('five.tool.rotate');const p=await drag(20,1,'middle',false);
  await page.evaluate(()=>window.dispatchEvent(new Event('blur')));const stopped=await camera();
  await page.mouse.move(p.x+40,p.y+30);await page.mouse.up({button:'middle'});
  assert.deepEqual(await camera(),stopped);assert.equal(await page.evaluate(()=>fixture.view.fivePlacement.mode),'rotate');
  // Async placement confirmation may refresh the panel while camera input arrives.
  await command('five.tool.translate');await pick('X');await numeric().fill('2');
  await page.evaluate(()=>{fixture.holdMethod='CamPlacement.SetTransform';fixture.hold=true;});
  await numeric().press('Enter');await page.waitForFunction(()=>fixture.held);
  const pending=await preview();await navigate('pending native confirmation');
  assert.deepEqual(await preview(),pending);await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.viewport.cameraNavigationEnabled),true);
  await page.evaluate(()=>{fixture.holdMethod='CamScene.ReadState';});
  await rpc({action:'undo'});await refresh();await command('five.tool.select');
  const restored=await read();for(const key of ['placements','workpieces','intentToolpaths'])assert.deepEqual(restored[key],domain[key]);
  assert.deepEqual(await page.evaluate(()=>fixture.view.scene.domain.expected),restored.expected);
  await page.evaluate(c=>fixture.view.viewport.setCameraState(c),starting);
  cases.push('blur cancels camera capture without leaving the tool; navigation remains available during native confirmation without altering the request or duplicating the edit');
  return cases;
}
