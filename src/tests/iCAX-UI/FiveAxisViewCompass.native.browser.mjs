import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyViewCompass({page,read,output}) {
  const cases=[],before=await read(),original=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  const frames=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const camera=()=>page.evaluate(()=>fixture.view.viewport.getCameraState());
  const set=async value=>{await page.evaluate(value=>fixture.view.viewport.setCameraState(value),value);await frames();};
  const ring=()=>page.locator('[data-cam-compass]');
  const geometry=()=>ring().evaluate(n=>{
    const m=n.ownerSVGElement.getScreenCTM(),p=new DOMPoint(+n.dataset.cx,+n.dataset.cy).matrixTransform(m);
    return {x:p.x,y:p.y,rx:+n.dataset.rx*m.a,ry:+n.dataset.ry*m.d,sign:+n.dataset.sign};
  });
  const near=(a,b,message)=>assert.ok(Math.abs(a-b)<.035,`${message}: ${a} != ${b}`);
  const move=async(g,angle)=>{await page.mouse.move(g.x+g.rx*Math.cos(angle),g.y+g.ry*Math.sin(angle));await frames();};
  const begin=async()=>{
    const g=await geometry(),c=await camera(),start=Math.sin(c.phi)<0?-Math.PI/2:Math.PI/2;
    await move(g,start);
    assert.ok(await page.evaluate(()=>document.querySelector('[data-cam-compass]:hover')),'the exposed ring can be picked');
    await page.mouse.down();assert.equal(await page.locator('.cam-viewcube').getAttribute('data-compass-dragging'),'true');
    return {...g,start};
  };
  const turn=async(g,delta)=>{for(let i=1;i<=18;i++)await move(g,g.start+delta*i/18);};
  for(const projectionMode of ['perspective','orthographic']){
    for(const phi of [1.05,0,Math.PI/2,Math.PI,2.3,-1.05]){
      await set({...original,projectionMode,phi,theta:.4});
      const c=await camera(),g=await begin();await turn(g,Math.PI/2);await page.mouse.up();await frames();
      const after=await camera();near(after.theta,c.theta+g.sign*Math.PI/2,`${projectionMode}/${phi} quarter turn`);
      assert.deepEqual({...after,target:c.target,theta:c.theta},c,'compass preserves pitch, zoom and projection while rotating around its captured world point');
      assert.equal(await page.locator('.cam-viewcube').getAttribute('data-compass-dragging'),null);
    }
    cases.push(`${projectionMode}: compass quarter-turns with real dragging at oblique, top, bottom, horizon and inverted views; pitch, zoom, pivot and projection preserved`);
  }
  await set({...original,phi:1.05,theta:0});
  let g=await begin();await turn(g,2*Math.PI+.65);await page.mouse.up();await frames();near((await camera()).theta,2*Math.PI+.65,'continuous angle across the wrap');
  const cancelStart=await camera();g=await begin();await turn(g,.7);await page.keyboard.press('Escape');await page.mouse.up();await frames();assert.deepEqual(await camera(),cancelStart);
  g=await begin();await turn(g,.5);
  await page.evaluate(()=>{const h=document.querySelector('.cam-viewcube');h.dispatchEvent(new PointerEvent('pointercancel',{pointerId:1,bubbles:true}));});
  await page.mouse.up();await frames();assert.deepEqual(await camera(),cancelStart);
  g=await begin();await turn(g,-.4);const blurState=await camera();await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.mouse.up();await frames();assert.deepEqual(await camera(),blurState);assert.equal(await page.locator('.cam-viewcube').getAttribute('data-compass-dragging'),null);
  await ring().focus();const keyboardStart=await camera();await page.keyboard.press('ArrowRight');await frames();near((await camera()).theta,keyboardStart.theta+5*Math.PI/180,'keyboard nudge');assert.ok(await ring().evaluate(n=>n===document.activeElement));
  await page.keyboard.press('Shift+ArrowLeft');await frames();near((await camera()).theta,keyboardStart.theta-10*Math.PI/180,'keyboard coarse nudge');
  assert.ok(Number.isFinite(Number(await ring().getAttribute('aria-valuenow'))));
  cases.push('Compass retains capture across SVG updates and full turns; Escape/pointer cancellation restore the camera; blur ends dragging; keyboard focus survives redraw');

  await set({...original,phi:1.05,theta:-.75});
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.translate'));
  const {pick,numeric}=gizmoDriver(page);await pick('X');await numeric().fill('7.25');
  await numeric().evaluate(n=>{window.compassInput=n;n.setSelectionRange(1,3);});
  const preview=()=>page.evaluate(()=>fixture.view.viewport.sceneObjects.get(fixture.view.fivePlacement.placementId).matrix.toArray());
  const draft=await preview();g=await begin();await turn(g,.4);await page.mouse.up();await frames();
  assert.deepEqual(await preview(),draft);assert.equal(await numeric().inputValue(),'7.25');
  assert.deepEqual(await numeric().evaluate(n=>[n===window.compassInput,n===document.activeElement,n.selectionStart,n.selectionEnd]),[true,true,1,3]);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.mode),'translate');
  assert.deepEqual(await read(),before,'compass never persists workpiece geometry, including with an uncommitted transform draft');
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.select'));
  await set({...original,phi:1.05,theta:-.75});await page.mouse.move(5,5);await frames();
  await page.screenshot({path:resolve(output,'viewcube-compass-scene.png')});
  const box=await page.locator('.cam-viewcube').boundingBox();await page.screenshot({path:resolve(output,'viewcube-compass-detail.png'),clip:{x:box.x-10,y:box.y-5,width:box.width+20,height:156}});
  cases.push('Dragging the compass preserves a live workpiece transform preview, input node, focus, caret selection and native state');
  await set(original);
  return cases;
}
