import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyScreenRotation({page,command,read,rpc,refresh,base,output}) {
  const cases=[],{pick,drag,locate,numeric}=gizmoDriver(page),idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const frame=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<2e-5,`${a} != ${b}`));
  const matrix=()=>page.evaluate(()=>fixture.view.viewport.sceneObjects.get(fixture.view.fivePlacement.placementId).matrix.clone().transpose().toArray());
  const undo=async()=>{await command('five.tool.select');await rpc({action:'undo'});await refresh();};
  const oracle=async(angle)=>page.evaluate(async({angle,base})=>{
    const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view.viewport,d=fixture.view.fivePlacement;
    const b=v.sceneObjects.get(d.placementId).geometry.boundingBox,pivot=b.getCenter(new T.Vector3()).applyMatrix4(new T.Matrix4().set(...base));
    const axis=new T.Vector3(...d.rotationAxis),q=new T.Quaternion().setFromAxisAngle(axis,angle*Math.PI/180),m=[...base];
    for(let col=0;col<3;col++){const p=new T.Vector3(base[col],base[col+4],base[col+8]).applyQuaternion(q);[m[col],m[col+4],m[col+8]]=p.toArray();}
    const p=new T.Vector3(base[3],base[7],base[11]).sub(pivot).applyQuaternion(q).add(pivot);[m[3],m[7],m[11]]=p.toArray();return m;
  },{angle,base});
  const shaderErrors=[];page.on('console',m=>{if(m.type()==='error'&&/Shader Error|WebGLProgram|VALIDATE_STATUS/i.test(m.text()))shaderErrors.push(m.text());});
  for(const projection of ['perspective','orthographic'])for(const space of ['world','local']){
    await command('five.tool.select');await page.locator(`button[data-projection-mode="${projection}"]`).click();await page.locator(`button[data-transform-space="${space}"]`).click();
    await page.evaluate(()=>{const v=fixture.view.viewport,c=v.getCameraState();v.setCameraState({...c,theta:.65,phi:1.05});});
    await command('five.tool.rotate');await page.mouse.move(5,5);await frame();
    if(space==='world')await page.screenshot({path:resolve(output,`rotation-idle-${projection}.png`)});
    await pick('Z');await page.mouse.move(5,5);await frame();
    const visual=await page.evaluate(()=>{
      const c=fixture.view.viewport.transformManipulator.control,g=c._gizmo.gizmo.rotate;
      return {selected:g.children.find(h=>h.name==='Z').material.color.getHex(),expected:c._gizmo.materialLib.active.color.getHex(),outer:g.children.find(h=>h.name==='E').visible,
        shaded:g.children.filter(h=>h.userData.ringEye).map(h=>h.userData.ringEye.value.length())};
    });
    assert.equal(visual.selected,visual.expected);assert.equal(visual.outer,true);assert.equal(visual.shaded.length,3);assert.ok(visual.shaded.every(n=>n>.99));
    if(space==='world')await page.screenshot({path:resolve(output,`rotation-selected-${projection}.png`)});
    await pick('E');assert.equal(await page.locator('.five-placement-field>span').innerText(),'视图');
    const axis=await page.evaluate(()=>fixture.view.fivePlacement.rotationAxis),before=await read();assert.ok(Math.abs(Math.hypot(...axis)-1)<1e-8);
    await numeric().fill('25');near(await matrix(),await oracle(25));assert.deepEqual(await read(),before);
    // Actual camera gestures must retain the originally selected screen axis and placement draft.
    const draft=await matrix();
    for(const button of ['right','middle']){
      const at=await locate('E'),old=await page.evaluate(()=>fixture.view.viewport.getCameraState());
      await page.mouse.move(at.x,at.y);await page.mouse.down({button});
      const pivot=button==='right'?await page.evaluate(()=>fixture.view.viewport.navigation.orbitPivot):null;
      await page.mouse.move(at.x+24,at.y+16,{steps:6});
      if(button==='right')assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.navigation.orbitPivot),pivot,'orbit retains its captured pivot throughout the drag');
      await page.mouse.up({button});await frame();
      const next=await page.evaluate(()=>fixture.view.viewport.getCameraState());
      if(button==='right'){
        assert.ok(next.theta!==old.theta||next.phi!==old.phi,'right drag orbits over the placement ring');
      }else{
        assert.notDeepEqual(next.target,old.target,'middle drag pans over the placement ring without modifiers');
        assert.equal(next.theta,old.theta);assert.equal(next.phi,old.phi);
      }
      assert.equal(next.radius,old.radius);near(await matrix(),draft);assert.deepEqual(await read(),before);
      near(await page.evaluate(()=>fixture.view.fivePlacement.rotationAxis),axis);
    }
    await numeric().fill('40');near(await matrix(),await oracle(40));const target=await matrix();
    await numeric().press('Enter');await idle();near((await read()).placements[0].transform,target);
    const once=await read();await numeric().press('Enter');await idle();assert.deepEqual(await read(),once);
    await undo();await rpc({action:'redo'});await refresh();near((await read()).placements[0].transform,target);await undo();
    await command('five.tool.rotate');const beforeDrag=await read(),camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
    await drag('E',25,false);assert.deepEqual(await read(),beforeDrag);const preview=await matrix(),angle=Number(await numeric().inputValue());assert.ok(Math.abs(angle)>.1);near(preview,await oracle(angle));
    assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);
    await page.mouse.up();await idle();near((await read()).placements[0].transform,preview);await undo();
    await command('five.tool.rotate');await drag('E',20,false);await page.keyboard.press('Escape');await page.mouse.up();near(await matrix(),base);near((await read()).placements[0].transform,base);
    cases.push(`${projection}/${space}: front/back shading, persistent yellow axis, usable screen ring, exact-angle quaternion oracle, frozen screen axis and draft after right-drag orbit and middle-drag pan, drag, Escape, undo and redo`);
  }
  await command('five.tool.rotate');await pick('E');await numeric().fill('15');await numeric().press('Enter');await idle();
  const saved=(await read()).placements[0].transform;
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});near(reopened.placements[0].transform,saved);assert.deepEqual(shaderErrors,[]);
  cases.push('screen rotation persists through project save/reopen; no WebGL shader compilation errors');
  writeFileSync(resolve(output,'screen-rotation-report.json'),JSON.stringify({passed:true,cases,standaloneCef:false},null,2));return cases;
}
