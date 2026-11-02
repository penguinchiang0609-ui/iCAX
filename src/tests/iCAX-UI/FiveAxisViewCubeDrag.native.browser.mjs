import assert from 'node:assert/strict';

export async function verifyViewCubeDrag({page,read}){
  const before=await read(),original=await page.evaluate(()=>fixture.view.viewport.getCameraState()),cases=[];
  const frames=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const camera=()=>page.evaluate(()=>fixture.view.viewport.getCameraState());
  const set=async c=>{await page.evaluate(c=>fixture.view.viewport.setCameraState(c),c);await frames();};
  const anchor=()=>page.locator('.cam-viewcube').evaluate(host=>{
    for(const p of host.querySelectorAll('.cam-viewcube-piece-center polygon')){
      const b=p.getBBox(),v=new DOMPoint(b.x+b.width/2,b.y+b.height/2).matrixTransform(p.getScreenCTM());
      if(document.elementFromPoint(v.x,v.y)?.closest('[data-cam-view]'))return {x:v.x,y:v.y};
    }throw Error('No visible cube face');
  });
  const begin=async()=>{const p=await anchor();await page.mouse.move(p.x,p.y);await page.mouse.down();assert.equal(await page.locator('.cam-viewcube').getAttribute('data-navigation-dragging'),'orbit');return p;};
  for(const projectionMode of ['perspective','orthographic'])for(const phi of [1.05,0,-1.05]){
    await set({...original,projectionMode,phi,theta:.4});const start=await camera(),p=await begin();
    await page.mouse.move(p.x-42,p.y+30,{steps:8});await frames();const dragged=await camera();
    assert.notEqual(dragged.theta,start.theta);assert.notEqual(dragged.phi,start.phi);
    assert.deepEqual({...dragged,target:start.target,theta:start.theta,phi:start.phi},start,'Cube drag preserves zoom and projection while rotating around its captured world point');
    // Pointer capture must survive both the live SVG redraw and leaving the widget.
    const h=await page.locator('.cam-viewcube').boundingBox();await page.mouse.move(h.x-80,h.y+h.height+40,{steps:8});await frames();
    const outside=await camera();assert.notDeepEqual(outside,dragged);await page.mouse.up();await frames();
    assert.deepEqual(await camera(),outside,'Release must not activate the face underneath the original press');
    assert.equal(await page.locator('.cam-viewcube').getAttribute('data-navigation-dragging'),null);
  }
  cases.push('Left-dragging a cube face rotates heading and pitch in both projections and top/inverted views; captured dragging continues outside and release does not snap to a standard view');

  await set({...original,phi:1.05,theta:.4});
  for(const cancel of ['Escape','pointercancel','blur']){
    const start=await camera(),p=await begin();await page.mouse.move(p.x-25,p.y+20,{steps:5});await frames();
    if(cancel==='Escape')await page.keyboard.press('Escape');
    else await page.evaluate(kind=>{
      if(kind==='blur')window.dispatchEvent(new Event('blur'));
      else{const h=document.querySelector('.cam-viewcube');h.dispatchEvent(new PointerEvent('pointercancel',{pointerId:1,bubbles:true}));}
    },cancel);
    const ended=await camera();await page.mouse.up();await frames();assert.deepEqual(await camera(),ended);
    if(cancel!=='blur')assert.deepEqual(ended,start);
    assert.equal(await page.locator('.cam-viewcube').getAttribute('data-navigation-dragging'),null);
  }
  cases.push('Escape and pointer cancellation restore the starting view; losing focus ends navigation cleanly');

  await set({...original,phi:1.05,theta:.4});const start=await camera(),p=await anchor();
  await page.mouse.move(p.x,p.y);await page.mouse.down({button:'right'});await page.mouse.move(p.x-20,p.y+15,{steps:5});await page.mouse.up({button:'right'});await frames();assert.deepEqual(await camera(),start);
  const canvas=await page.locator('.icax-three-viewport canvas').first().boundingBox();
  await page.mouse.move(canvas.x+20,canvas.y+120);await page.mouse.down();await page.mouse.move(p.x,p.y,{steps:10});await page.mouse.up();await frames();assert.deepEqual(await camera(),start,'A press outside the navigation area must not start cube navigation on entry');
  for(const mode of ['orthographic','perspective']){
    const button=page.locator(`button[data-projection-mode="${mode}"]`),b=await button.boundingBox(),c=await camera();
    await page.mouse.move(b.x+b.width/2,b.y+b.height/2);await page.mouse.down();await page.mouse.move(b.x+5,b.y+4,{steps:4});await page.mouse.up();await frames();
    const after=await camera();assert.equal(after.theta,c.theta);assert.equal(after.phi,c.phi);
    await button.click();await frames();assert.equal((await camera()).projectionMode,mode);
  }
  assert.deepEqual(await read(),before,'Navigation never changes native workpiece or machining data');
  await set(original);
  cases.push('Only a left press within the cube starts free orbit; right drags, presses that begin in the scene and projection buttons do not rotate the view; native state remains unchanged');
  return cases;
}
