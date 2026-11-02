import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {verifyViewCompass} from './FiveAxisViewCompass.native.browser.mjs';
import {verifyViewCubeDrag} from './FiveAxisViewCubeDrag.native.browser.mjs';

export async function verifyCompactViewCube({page,read,output}){
  const before=await read(),original=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  const frames=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const targets=[];
  for(const z of [-1,0,1])for(const y of [-1,0,1])for(const x of [-1,0,1]){
    if(!x&&!y&&!z)continue;
    targets.push({name:[z?(z>0?'top':'bottom'):'',y?(y>0?'back':'front'):'',x?(x>0?'right':'left'):''].filter(Boolean).join('-'),eye:[x,y,z]});
  }
  for(const projection of ['perspective','orthographic']){
    await page.locator(`button[data-projection-mode="${projection}"]`).click();
    for(const target of targets){
      // Approach the target from a nearby viewing direction, then use its real
      // visible SVG hit region. Expected direction comes from the face names.
      await page.evaluate(eye=>fixture.view.viewport.setViewDirection(eye.map((v,i)=>v+[.18,.12,.15][i])),target.eye);await frames();
      const piece=page.locator(`.cam-viewcube [data-cam-view="${target.name}"]`);await piece.click();await frames();
      const state=await page.evaluate(()=>fixture.view.viewport.getViewCubeState()),n=Math.hypot(...target.eye);
      target.eye.forEach((v,i)=>assert.ok(Math.abs(state.direction['xyz'[i]]+v/n)<1e-5,`${projection}/${target.name}: ${JSON.stringify(state)}`));
      assert.equal(await page.evaluate(()=>fixture.view.viewport.getCameraState().projectionMode),projection);
      const textMatrices=await page.locator('.cam-viewcube-label').evaluateAll(nodes=>nodes.map(n=>{const m=n.transform.baseVal.consolidate().matrix;return [m.a,m.b,m.c,m.d];}));
      assert.ok(textMatrices.every(([a,b,c,d])=>a*d-b*c>.18),`${target.name}: label is readable and not mirrored`);
      if(projection==='perspective'&&['front','top','right','bottom-back-left'].includes(target.name)){
        const bounds=await page.locator('.cam-viewcube').boundingBox();await page.screenshot({path:resolve(output,`viewcube-${target.name}.png`),clip:{x:bounds.x-10,y:bounds.y-5,width:bounds.width+20,height:156}});
      }
    }
  }
  await page.evaluate(()=>fixture.view.viewport.setStandardView('top-front-right'));await frames();
  const labels=await page.locator('.cam-viewcube-label').allTextContents();assert.deepEqual(labels.sort(),['顶视','前视','右视'].sort());
  const cube=page.locator('.cam-viewcube'),projection=page.locator('.icax-three-projection-toggle');
  assert.equal(await cube.getAttribute('data-cam-viewcube-piece-count'),'26');
  const a=await cube.boundingBox(),b=await projection.boundingBox();assert.ok(a.width<=148&&a.height<=148&&b.height<32&&Math.abs(a.x-b.x)<1&&b.y-a.y<=148&&b.y>=a.y+a.height-5);
  assert.equal(await page.locator('.icax-three-axis-gizmo').count(),0);
  assert.deepEqual(await page.locator('.cam-viewcube [data-cam-axis]').evaluateAll(ns=>ns.map(n=>n.dataset.camAxis)),['x','y','z']);
  const face=page.locator('.cam-viewcube [data-cam-view="front"]');await face.hover();assert.match(await page.locator('[data-view-surface="front"] polygon').evaluate(n=>getComputedStyle(n).fill),/-hover/);
  await face.focus();await page.keyboard.press('Enter');await frames();const d=await page.evaluate(()=>fixture.view.viewport.getViewCubeState().direction);assert.ok(Math.abs(d.y-1)<1e-5);
  await page.evaluate(()=>{fixture.view.viewport.setStandardView('top-front-right');fixture.view.viewport.renderer.domElement.focus();});await page.mouse.move(5,5);await frames();
  assert.equal(await cube.getAttribute('data-appearance'),'beveled');
  const lettering=await page.locator('.cam-viewcube-label').evaluateAll(nodes=>nodes.map(n=>{const m=n.transform.baseVal.consolidate().matrix;return {name:n.dataset.faceLabel,a:m.a,b:m.b,c:m.c,d:m.d};}));
  assert.ok(lettering.every(m=>m.a*m.d-m.b*m.c>0),'face text is never mirrored');
  assert.ok(lettering.every(m=>Math.abs(m.b)+Math.abs(m.c)>.1),'text follows each face plane');
  const colors=await page.locator('.cam-viewcube defs linearGradient').evaluateAll(nodes=>nodes.filter(n=>/^cam-cube-\d+-(top|front|right)$/.test(n.id)).map(n=>n.firstElementChild.getAttribute('stop-color')));
  assert.equal(new Set(colors).size,3,'the three main faces have distinct lighting');
  await page.screenshot({path:resolve(output,'compact-view-navigation.png')});
  const bounds=await cube.boundingBox();await page.screenshot({path:resolve(output,'viewcube-detail.png'),clip:{x:bounds.x-10,y:bounds.y-5,width:bounds.width+20,height:156}});
  const compassCases=await verifyViewCompass({page,read,output});
  const dragCases=await verifyViewCubeDrag({page,read});
  assert.deepEqual(await read(),before,'view navigation must never change workpiece data');
  await page.evaluate(c=>fixture.view.viewport.setCameraState(c),original);await frames();
  return ['Beveled view cube: all 26 face/edge/corner targets clicked in both projections; distinct lighting, face-aligned non-mirrored Chinese text, generous hidden hit regions, hover and keyboard access; native geometry unchanged',...compassCases,...dragCases];
}
