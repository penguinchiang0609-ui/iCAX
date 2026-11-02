import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyPlacementExit({page,command,read,rpc,refresh,output}) {
  const cases=[],{pick,numeric}=gizmoDriver(page),idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const point=async model=>page.evaluate(async model=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view.viewport,canvas=v.renderer.domElement,r=canvas.getBoundingClientRect();
    v.scene.updateMatrixWorld(true);v.camera.updateMatrixWorld(true);
    const ray=new THREE.Raycaster();
    for(const fy of [.2,.35,.5,.65,.8])for(const fx of [.12,.3,.5,.7,.88]){
      const x=r.x+r.width*fx,y=r.y+r.height*fy;if(document.elementFromPoint(x,y)!==canvas)continue;
      ray.setFromCamera(new THREE.Vector2(fx*2-1,1-fy*2),v.camera);
      if((ray.intersectObjects([...v.sceneObjects.values()],false).length>0)!==model)continue;
      const m=v.transformManipulator;
      if(m){m.update();if(ray.intersectObject(m.control._gizmo.picker[m.control.mode],true).some(h=>h.object.visible))continue;}
      return {x,y};
    }
    throw Error(`No ${model?'model':'blank'} canvas point`);
  },model);
  const mode=()=>page.evaluate(()=>fixture.view.fiveAxisUI.tools.workpiece);
  const clickBlank=async()=>{const p=await point(false);await page.mouse.click(p.x,p.y,{button:'right'});};
  const camera=()=>page.evaluate(()=>fixture.view.viewport.getCameraState());
  const assertExited=async before=>{
    assert.equal(await mode(),await page.evaluate(()=>fixture.exitNodes.mode));assert.equal(await numeric().count(),0);
    const visual=await page.evaluate(()=>{const v=fixture.view,p=fixture.exitNodes;return {open:v.fivePlacement.open,previews:v.viewport.transformPreviews.size,
      manipulator:!!v.viewport.transformManipulator,navigation:v.viewport.cameraNavigationEnabled,canvas:v.viewport.renderer.domElement===p.canvas,
      model:[...v.viewport.sceneObjects.values()][0]===p.model,geometry:[...v.viewport.sceneObjects.values()][0].geometry===p.geometry,
      active:document.querySelectorAll('.five-placement-tools button[aria-pressed="true"]').length===1,
      highlighted:v.viewport.selectedObjectIds.size>0};});
    assert.deepEqual(visual,{open:true,previews:1,manipulator:true,navigation:true,canvas:true,model:true,geometry:true,active:true,highlighted:false});
    assert.deepEqual(await read(),before,'exiting does not commit, undo or change Scene selection');
  };
  const capture=()=>page.evaluate(()=>{const v=fixture.view,m=[...v.viewport.sceneObjects.values()][0];fixture.exitNodes={mode:v.fiveAxisUI.tools.workpiece,canvas:v.viewport.renderer.domElement,model:m,geometry:m.geometry};});
  for(const projection of ['perspective','orthographic']){
    await command('five.tool.select');await page.locator(`[data-projection-mode="${projection}"]`).click();
    for(const tool of ['translate','rotate','scale']){
      await command(`five.tool.${tool}`);const before=await read();await capture();
      if(projection==='orthographic'&&tool==='scale'){
        const b=await point(false);await page.mouse.move(b.x,b.y);await page.mouse.down({button:'right'});
        await page.mouse.move(b.x+1,b.y+1);await page.mouse.up({button:'right'});
      }else await clickBlank();
      await assertExited(before);
      await command(`five.tool.${tool}`);await pick('X');await numeric().fill(tool==='scale'?'1.3':'12');await capture();
      const previewCamera=await camera(),p=await point(true);await page.mouse.click(p.x,p.y,{button:'right'});
      assert.equal(await mode(),tool,'right click on the model does not exit');
      await numeric().click({button:'right'});assert.equal(await mode(),tool,'right click in numeric editor does not exit');
      const b=await point(false);await page.mouse.move(b.x,b.y);await page.mouse.down({button:'right'});
      await page.mouse.move(b.x+38,b.y+8,{steps:8});await page.mouse.move(b.x,b.y,{steps:8});await page.mouse.up({button:'right'});
      assert.equal(await mode(),tool,'a drag returning to its origin is still not a click');assert.deepEqual(await camera(),previewCamera);
      await clickBlank();await assertExited(before);assert.deepEqual(await camera(),previewCamera);
      cases.push(`${projection}/${tool}: blank right click cancels uncommitted input while keeping the active mode, handles, toolbar and canvas/model/geometry/Scene`);
    }
  }
  for(const tool of ['translate','rotate','scale']){
    await command(`five.tool.${tool}`);await pick('X');await numeric().fill(tool==='scale'?'1.1':'5');await numeric().press('Enter');await idle();
    const committed=await read();await capture();await clickBlank();await assertExited(committed);
    await rpc({action:'undo'});await refresh();
  }
  cases.push('right-click exit preserves committed translation, rotation and uniform scale without adding an undo step');
  await command('five.tool.translate');await pick('X');await numeric().fill('3');
  await page.evaluate(()=>{fixture.holdMethod='CamPlacement.SetTransform';fixture.hold=true;});await numeric().press('Enter');await page.waitForFunction(()=>fixture.held);
  await clickBlank();assert.equal(await mode(),'translate');
  assert.ok(await page.evaluate(()=>!!fixture.view.fivePlacement.request));
  await page.evaluate(()=>fixture.release());await idle();const committed=await read();await capture();await clickBlank();await assertExited(committed);
  await rpc({action:'undo'});await refresh();
  cases.push('an in-flight native confirmation cannot be discarded by right click; normal exit works after completion');
  await command('five.tool.translate');await pick('X');await numeric().fill('2');
  await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await numeric().press('Enter');await idle();
  const request=await page.evaluate(()=>fixture.view.fivePlacement.request);assert.ok(request);
  await clickBlank();assert.equal(await mode(),'translate');assert.deepEqual(await page.evaluate(()=>fixture.view.fivePlacement.request),request);
  await numeric().press('Enter');await idle();const confirmed=await read();await capture();await clickBlank();await assertExited(confirmed);
  await rpc({action:'undo'});await refresh();
  cases.push('an unknown receipt retains the same retry request until confirmed; exit afterward preserves the acknowledged result');
  await command('five.tool.rotate');await pick('Z');await numeric().fill('20');const beforeEscape=await read();await capture();
  await numeric().press('Escape');await assertExited(beforeEscape);
  await command('five.tool.scale');const p=await point(false);await page.mouse.move(p.x,p.y);await page.mouse.down({button:'right'});
  await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.mouse.up({button:'right'});assert.equal(await mode(),'scale');
  await capture();const beforeClick=await read();await clickBlank();await assertExited(beforeClick);
  cases.push('Escape cancels uncommitted input and keeps the active mode; a cancelled right gesture is not applied on release');
  await page.screenshot({path:resolve(output,'right-click-retains-active-workpiece.png')});
  return cases;
}
