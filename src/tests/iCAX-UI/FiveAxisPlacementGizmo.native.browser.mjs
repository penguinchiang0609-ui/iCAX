import assert from 'node:assert/strict';
import { resolve } from 'node:path';

export function gizmoDriver(page) {
  const locator=async axis=>page.evaluate(axis=>{
    const vp=fixture.view.viewport, m=vp.transformManipulator,c=m.control,canvas=vp.renderer.domElement;
    c.axis=null;
    m.update();
    const r=canvas.getBoundingClientRect();
    const p=m.proxy.position.clone().project(vp.camera),cx=r.left+(p.x+1)*r.width/2,cy=r.top+(1-p.y)*r.height/2;
    const matches=[],ray=c.getRaycaster();
    for(let y=cy-115;y<=cy+115;y+=4)for(let x=cx-115;x<=cx+115;x+=4){
      if(document.elementFromPoint(x,y)!==canvas)continue;
      ray.setFromCamera({x:(x-r.left)/r.width*2-1,y:1-(y-r.top)/r.height*2},vp.camera);
      const hit=ray.intersectObject(c._gizmo.picker[c.mode],true).find(hit=>hit.object.visible
        && (c.mode!=='rotate'||hit.object.name==='E'||hit.point.clone().sub(m.proxy.position).dot(c.eye)>=-1e-7));
      if(hit?.object.name===axis)matches.push({x,y,d:Math.hypot(x-cx,y-cy)});
    }
    const preferredRadius=70*(m.options?.sizePixels??160)/160;
    const hitAt=(x,y)=>{ray.setFromCamera({x:(x-r.left)/r.width*2-1,y:1-(y-r.top)/r.height*2},vp.camera);return ray.intersectObject(c._gizmo.picker[c.mode],true).find(hit=>hit.object.visible&&(c.mode!=='rotate'||hit.object.name==='E'||hit.point.clone().sub(m.proxy.position).dot(c.eye)>=-1e-7))?.object.name;};
    for(const match of matches){match.neighbors=[[-1,0],[1,0],[0,-1],[0,1]].map(([dx,dy])=>hitAt(match.x+dx,match.y+dy));match.interior=match.neighbors.every(name=>name===axis);}
    matches.sort((a,b)=>Number(b.interior)-Number(a.interior)||Math.abs(a.d-preferredRadius)-Math.abs(b.d-preferredRadius));
    if(!matches.length)throw Error(`No visible ${c.mode}/${c.space}/${axis} handle`);
    const axisVector=axis==='E'?c.eye.clone():vp.camera.position.clone().set(...({X:[1,0,0],Y:[0,1,0],Z:[0,0,1],XY:[1,1,0],XZ:[1,0,1],YZ:[0,1,1],XYZ:[1,1,1]})[axis]);
    if(axis!=='E'&&c.space==='local')axisVector.applyQuaternion(m.proxy.quaternion);
    if(c.mode==='rotate')axisVector.cross(c.eye).normalize();
    const q=m.proxy.position.clone().add(axisVector).project(vp.camera);
    let dx=(q.x-p.x)*r.width/2,dy=-(q.y-p.y)*r.height/2,n=Math.hypot(dx,dy);
    if(n<1e-7){dx=-(matches[0].y-cy);dy=matches[0].x-cx;n=Math.hypot(dx,dy);}
    return {...matches[0],preferredRadius,sizePixels:m.options?.sizePixels??160,dx:dx/n,dy:dy/n};
  },axis);
  const drag=async (axis,pixels=45,release=true)=>{
    const p=await locator(axis);await page.mouse.move(p.x,p.y);
    assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.axis),axis);
    await page.mouse.down();await page.mouse.move(p.x+p.dx*pixels,p.y+p.dy*pixels,{steps:8});
    if(release)await page.mouse.up();return p;
  };
  return {drag,locate:locator,pick:async axis=>{const p=await locator(axis);await page.mouse.click(p.x,p.y);},numeric:()=>page.locator('[data-five-placement-field="value"]')};
}

export async function verifyPlacementGizmo({page,command,read,refresh,rpc,waitRendered,base,output}) {
  const cases=[],{drag,pick,numeric}=gizmoDriver(page);
  const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-5,`${a} != ${b}`));
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const rendered=()=>page.evaluate(()=>fixture.view.viewport.sceneObjects.get(fixture.view.scene.domain.placements[0].id).matrix.clone().transpose().toArray());
  const undo=async()=>{await rpc({action:'undo'});await refresh();};
  const compact=async axis=>{
    assert.equal(await page.locator('.five-placement-hud input').count(),1);
    assert.equal(await page.locator('.five-placement-hud select,.five-placement-hud button,.five-placement-hud header,.five-placement-hud footer').count(),0);
    assert.equal(await page.locator('.five-placement-hud').getAttribute('data-axis'),axis.toLowerCase());
    assert.ok((await page.locator('.five-placement-hud').boundingBox()).height<65);
  };
  for(const width of [1024,1440]){
    await page.setViewportSize({width,height:900});
    const layout=await page.evaluate(()=>{
      const r=fixture.view.viewport.renderer.domElement.getBoundingClientRect();
      const box=s=>document.querySelector(s).getBoundingClientRect();
      const d=box('.icax-three-display-mode'),s=box('.icax-three-transform-space'),p=box('.icax-three-projection-toggle'),cube=box('.cam-viewcube');
      return {left:d.left-r.left,top:d.top-r.top,space:s.x+s.width/2-r.x-r.width/2,projection:p.right-cube.right,gap:p.top-cube.bottom,right:r.right-p.right};
    });
    assert.ok(Math.abs(layout.space)<2&&Math.abs(layout.projection)<2&&layout.left<20&&layout.top<20&&layout.right<20&&layout.gap>=-5&&layout.gap<8);
  }
  cases.push('display and world/local controls stay top-left/top-center; projection is compactly grouped below the view cube at two widths');

  for(const projection of ['perspective','orthographic'])for(const space of ['world','local']){
    await page.locator(`button[data-projection-mode="${projection}"]`).click();
    await page.locator(`[data-transform-space="${space}"]`).click();
    for(const axis of ['X','Y','Z']){
      await command('five.tool.translate');assert.equal(await numeric().count(),0);
      const before=await read(),camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
      await pick(axis);await compact(axis);assert.deepEqual(await read(),before,'selecting an axis must not create history');
      await numeric().fill('7.5');
      const expected=[...base],i='XYZ'.indexOf(axis);
      const direction=space==='world'?[i===0?1:0,i===1?1:0,i===2?1:0]:[base[i],base[4+i],base[8+i]];
      direction.forEach((v,j)=>expected[j*4+3]+=7.5*v);
      near(await rendered(),expected);
      // Losing focus must never commit a typed value.
      await numeric().blur();assert.deepEqual(await read(),before);
      await numeric().press('Enter');await idle();near((await read()).placements[0].transform,expected);
      await numeric().press('Enter');await idle();const once=await read();
      await numeric().press('Enter');await idle();assert.deepEqual(await read(),once,'repeated Enter is a no-op');
      await undo();await waitRendered(base);
      await command('five.tool.translate');const dragBefore=await read();
      await drag(axis,35,false);assert.deepEqual(await read(),dragBefore,'held drag is preview only');
      const dragged=await rendered();assert.ok(Math.abs(Number(await numeric().inputValue()))>.1);
      await page.mouse.up();await idle();near((await read()).placements[0].transform,dragged);
      await compact(axis);assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);
      // Exact input after dragging replaces that gesture, rather than adding it twice.
      await numeric().fill('7.5');await numeric().press('Enter');await idle();near((await read()).placements[0].transform,expected);
      if(axis==='X'&&space==='local'&&projection==='orthographic')await page.screenshot({path:resolve(output,'translate-single-value.png')});
      await undo();await waitRendered(dragged);await undo();await waitRendered(base);
    }
    cases.push(`${projection}/${space}: all XYZ handles, one input, Enter-only numeric commit, mouse-up commit and one-step undo`);
  }

  for(const space of ['world','local'])for(const axis of ['X','Y','Z']){
    await page.locator(`[data-transform-space="${space}"]`).click();await command('five.tool.rotate');
    await drag(axis,35,false);assert.ok(Math.abs(Number(await numeric().inputValue()))>1);
    const dragged=await rendered();await page.mouse.up();await idle();near((await read()).placements[0].transform,dragged);
    await compact(axis);await numeric().fill('30');
    const actual=await rendered();
    // Derive the reference pivot from original bounds and base, not the production draft solver.
    const c=await page.evaluate(base=>{const b=fixture.view.viewport.sceneObjects.get(fixture.view.fivePlacement.placementId).geometry.boundingBox;
      const l=[(b.min.x+b.max.x)/2,(b.min.y+b.max.y)/2,(b.min.z+b.max.z)/2];return [0,1,2].map(i=>base[i*4+3]+l.reduce((v,x,j)=>v+base[i*4+j]*x,0));},base);
    const i='XYZ'.indexOf(axis),u=space==='world'?[i===0?1:0,i===1?1:0,i===2?1:0]:[base[i],base[4+i],base[8+i]];
    const cosine=Math.cos(Math.PI/6),sine=Math.sin(Math.PI/6);
    const rotate=p=>{const dot=p.reduce((v,x,j)=>v+x*u[j],0),cross=[u[1]*p[2]-u[2]*p[1],u[2]*p[0]-u[0]*p[2],u[0]*p[1]-u[1]*p[0]];return p.map((v,j)=>v*cosine+cross[j]*sine+u[j]*dot*(1-cosine));};
    for(let col=0;col<3;col++)near([actual[col],actual[4+col],actual[8+col]],rotate([base[col],base[4+col],base[8+col]]));
    near([actual[3],actual[7],actual[11]],rotate([base[3]-c[0],base[7]-c[1],base[11]-c[2]]).map((v,j)=>v+c[j]));
    await numeric().press('Enter');await idle();near((await read()).placements[0].transform,actual);
    if(space==='local'&&axis==='Z')await page.screenshot({path:resolve(output,'rotate-single-value.png')});
    await undo();await waitRendered(dragged);await undo();await waitRendered(base);
  }
  cases.push('all rotation rings in both frames: auto commit, one angle input and exact rotation about part center');

  await page.locator('[data-transform-space="world"]').click();await command('five.tool.translate');
  const beforeCancel=await read();await drag('X',35,false);await page.keyboard.press('Escape');await page.mouse.up();
  await waitRendered(base);assert.deepEqual(await read(),beforeCancel);
  await drag('Y',35,false);await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.mouse.up();
  await waitRendered(base);assert.deepEqual(await read(),beforeCancel);
  await drag('Z');await idle();const moved=await read();
  await page.locator('[data-transform-space="local"]').click();near(await rendered(),moved.placements[0].transform);
  await page.locator('[data-transform-space="world"]').click();near(await rendered(),moved.placements[0].transform);
  await pick('X');await numeric().fill('2');await numeric().press('Enter');await idle();
  const accumulated=[...moved.placements[0].transform];accumulated[3]+=2;near((await read()).placements[0].transform,accumulated);
  await undo();await waitRendered(moved.placements[0].transform);await undo();await waitRendered(base);
  cases.push('Escape/blur cancel only an uncommitted gesture; frame changes preserve saved placement; sequential axes accumulate once');

  // Check coordinate frames independently of the operation's pivot and camera.
  await page.locator('[data-transform-space="world"]').click();
  await command('five.tool.rotate');await pick('Z');await numeric().fill('37');await numeric().press('Enter');await idle();
  const oriented=(await read()).placements[0].transform;
  await command('five.tool.translate');await pick('X');await numeric().fill('17');await numeric().press('Enter');await idle();
  const placed=await read(),placement=placed.placements[0].transform;
  const cameraBefore=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  const frame=()=>page.evaluate(()=>{
    const vp=fixture.view.viewport,m=vp.transformManipulator;
    m.update();vp.scene.updateMatrixWorld(true);vp.axisScene.updateMatrixWorld(true);
    const object=vp.sceneObjects.get(fixture.view.fivePlacement.placementId);
    const handles=m.control._gizmo.gizmo.translate.children;
    const axes=['X','Y','Z'].map((name,i)=>{
      const handle=handles.find(h=>h.name===name);
      return m.proxy.position.clone().set(i===0?1:0,i===1?1:0,i===2?1:0).applyQuaternion(handle.quaternion).toArray();
    });
    return {root:vp.scene.matrixWorld.toArray(),indicator:vp.axisScene.children.find(x=>x.isLineSegments).matrixWorld.toArray(),
      origin:object.matrixWorld.elements.slice(12,15),object:object.matrixWorld.clone().transpose().toArray(),axes};
  });
  const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
  for(const space of ['world','local']){
    await page.locator(`[data-transform-space="${space}"]`).click();
    for(const navigate of [false,true]){
      if(navigate)await page.evaluate(()=>{const vp=fixture.view.viewport,c=vp.getCameraState();vp.setCameraState({...c,theta:c.theta+.7,phi:c.phi+.4});vp.fitView();});
      const f=await frame();near(f.root,identity);near(f.indicator,identity);
      near(f.object,placement);near(f.origin,[placement[3],placement[7],placement[11]]);
      for(let i=0;i<3;i++)near(f.axes[i],space==='world'?[i===0?1:0,i===1?1:0,i===2?1:0]:[placement[i],placement[4+i],placement[8+i]]);
      assert.deepEqual(await read(),placed,'frame selection and camera navigation must not write placement or Scene');
    }
  }
  await page.evaluate(camera=>fixture.view.viewport.setCameraState(camera),cameraBefore);
  await undo();await waitRendered(oriented);await undo();await waitRendered(base);
  cases.push('fixed world origin and XYZ after part rotation/translation and camera navigation; object origin/axes follow only its placement; handle pivot remains separate');
  await page.locator('[data-transform-space="world"]').click();
  await page.locator('button[data-projection-mode="perspective"]').click();
  for(const mode of ['translate','rotate']){
    const before=await read();
    await command(`five.tool.${mode}`);await pick('X');await numeric().fill('13');
    await command('five.tool.select');await idle();await waitRendered(base);
    assert.equal(await page.locator('.five-placement-hud').count(),0);
    assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),false);
    assert.equal(await page.evaluate(()=>fixture.view.viewport.transformPreviews.size),0);
    assert.equal(await page.locator('.five-placement-tools').count(),0);
    assert.deepEqual(await read(),before,'returning to Select must not submit numeric drafts');
    await command(`five.tool.${mode}`);await pick('X');await numeric().fill('13');await numeric().press('Enter');await idle();
    const saved=await read();await command('five.tool.select');await idle();
    await waitRendered(saved.placements[0].transform);assert.deepEqual(await read(),saved,'returning to Select must keep committed placement');
    await undo();await waitRendered(base);
  }
  cases.push('Select exits both transform tools, removes handles/input/preview, discards unsubmitted values and retains committed placement');
  return cases;
}
