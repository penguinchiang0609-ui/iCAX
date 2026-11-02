import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyPlaneTranslation({page,command,read,rpc,refresh,base,output}) {
  const cases=[],measurements=[],{pick,locate}=gizmoDriver(page);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const field=axis=>page.locator(`[data-five-placement-field="${axis.toLowerCase()}"]`);
  const matrix=()=>page.evaluate(()=>fixture.view.viewport.sceneObjects.get(fixture.view.fivePlacement.placementId).matrix.clone().transpose().toArray());
  const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<2e-5,`${a} != ${b}`));
  const expected=(plane,values,space)=>{
    const offset='XYZ'.split('').map(a=>plane.includes(a)?values[plane.indexOf(a)]:0),m=[...base];
    for(let i=0;i<3;i++)m[i*4+3]+=space==='world'?offset[i]:offset.reduce((sum,v,j)=>sum+base[i*4+j]*v,0);
    return m;
  };
  const undo=async()=>{await command('five.tool.select');await rpc({action:'undo'});await refresh();};
  const camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  for(const projection of ['perspective','orthographic']){
    await command('five.tool.select');await page.locator(`[data-projection-mode="${projection}"]`).click();
    await page.evaluate(()=>{const v=fixture.view.viewport,c=v.getCameraState();v.setCameraState({...c,theta:.65,phi:.95});});
    const fitted=await page.evaluate(()=>fixture.view.viewport.getCameraState());
    for(const space of ['world','local'])for(const plane of ['XY','XZ','YZ']){
      await page.locator(`[data-transform-space="${space}"]`).click();await command('five.tool.translate');
      const before=await read();await pick(plane);
      assert.equal(await page.locator('[data-five-placement-field]').count(),2);
      for(const axis of plane)assert.equal(await field(axis).inputValue(),'0');
      await field(plane[0]).fill('7.5');await field(plane[1]).fill('-3.25');
      const target=expected(plane,[7.5,-3.25],space);near(await matrix(),target);assert.deepEqual(await read(),before);
      await field(plane[1]).press('Enter');await idle();near((await read()).placements[0].transform,target);
      const once=await read();await field(plane[0]).press('Enter');await idle();assert.deepEqual(await read(),once);
      await field(plane[0]).fill('8.5');await field(plane[0]).press('Enter');await idle();near((await read()).placements[0].transform,expected(plane,[8.5,-3.25],space));
      await undo();await undo();near((await read()).placements[0].transform,base);
      await command('five.tool.translate');const p=await locate(plane),c=await page.evaluate(()=>fixture.view.viewport.getCameraState());
      await page.mouse.move(p.x,p.y);await page.mouse.down();await page.mouse.move(p.x+28,p.y-21,{steps:10});
      const preview=await matrix(),delta=[0,1,2].map(i=>preview[i*4+3]-base[i*4+3]);
      const local=space==='world'?delta:[0,1,2].map(j=>delta.reduce((s,v,i)=>s+base[i*4+j]*v,0));
      const normal='XYZ'.split('').find(a=>!plane.includes(a));
      assert.ok(Math.abs(local['XYZ'.indexOf(normal)])<2e-5,'drag keeps the plane normal coordinate fixed');
      for(const a of plane)assert.ok(Math.abs(local['XYZ'.indexOf(a)])>.01,'both plane coordinates move');
      near((await read()).placements[0].transform,base);assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),c);
      await page.mouse.up();await idle();near((await read()).placements[0].transform,preview);await undo();
      await command('five.tool.translate');const cancel=await locate(plane);
      await page.mouse.move(cancel.x,cancel.y);await page.mouse.down();await page.mouse.move(cancel.x+20,cancel.y+15,{steps:6});
      await page.keyboard.press('Escape');await page.mouse.up();near(await matrix(),base);near((await read()).placements[0].transform,base);
      cases.push(`${projection}/${space}/${plane}: actual plane picking and drag, two-field exact input, replacement edit, single-transaction undo, fixed normal coordinate and Escape`);
    }
    for(const mode of ['translate','rotate','scale']){
      await command(`five.tool.${mode}`);
      for(const zoom of [.4,1,2.5]){
        await page.evaluate(({fitted,zoom})=>fixture.view.viewport.setCameraState({...fitted,radius:fitted.radius*zoom}),{fitted,zoom});
        const metrics=await page.evaluate(()=>{
          const v=fixture.view.viewport,m=v.transformManipulator;m.update();const group=m.control._gizmo.gizmo[m.control.mode],b=v.renderer.domElement.getBoundingClientRect();
          const center=m.proxy.position.clone().project(v.camera),points=[];
          for(const h of group.children){if(!h.visible)continue;const a=h.geometry.attributes.position;
            for(let i=0;i<a.count;i++){const p=m.proxy.position.clone().fromBufferAttribute(a,i).applyMatrix4(h.matrixWorld).project(v.camera);points.push([(p.x-center.x)*b.width/2,(p.y-center.y)*b.height/2]);}}
          return {width:Math.max(...points.map(p=>p[0]))-Math.min(...points.map(p=>p[0])),height:Math.max(...points.map(p=>p[1]))-Math.min(...points.map(p=>p[1])),axisHandles:group.children.filter(h=>['X','Y','Z'].includes(h.name)).length};
        });
        assert.ok(Math.max(metrics.width,metrics.height)<185,`${mode} is compact`);
        measurements.push({projection,mode,zoom,...metrics});
      }
      await page.evaluate(c=>fixture.view.viewport.setCameraState(c),fitted);await page.mouse.move(5,5);
      await page.screenshot({path:resolve(output,`gizmo-${mode}-${projection}.png`)});
    }
  }
  for(const mode of ['translate','rotate','scale']){
    const rows=measurements.filter(m=>m.mode===mode),min=Math.min(...rows.map(m=>Math.max(m.width,m.height))),max=Math.max(...rows.map(m=>Math.max(m.width,m.height)));
    assert.ok(max/min<1.12,`${mode} keeps its size across zoom and projection`);
  }
  await command('five.tool.translate');await pick('XY');await field('X').fill('3.5');await field('Y').fill('-2.75');
  await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{max-height:180px!important}'});
  await page.evaluate(()=>{fixture.planeNodes={canvas:fixture.view.viewport.renderer.domElement,x:document.querySelector('[data-five-placement-field="x"]'),y:document.querySelector('[data-five-placement-field="y"]')};fixture.holdMethod='CamScene.ReadState';fixture.hold=true;});
  await field('Y').press('Enter');await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{fixture.planeNodes.y.focus({preventScroll:true});fixture.planeNodes.y.setSelectionRange(1,4);return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=45+i*20;return n.scrollTop;});});
  assert.ok(scroll.every(n=>n>0));await page.evaluate(()=>fixture.release());await idle();
  assert.deepEqual(await page.evaluate(()=>{const n=fixture.planeNodes;return {same:n.canvas===fixture.view.viewport.renderer.domElement&&n.x===document.querySelector('[data-five-placement-field="x"]')&&n.y===document.querySelector('[data-five-placement-field="y"]'),focus:document.activeElement===n.y,selection:[n.y.selectionStart,n.y.selectionEnd]};}),{same:true,focus:true,selection:[1,4]});
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),scroll);
  const saved=(await read()).placements[0].transform;
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});near(reopened.placements[0].transform,saved);
  cases.push('compact handles retain screen size in both projections and three zoom levels; delayed two-field refresh preserves nodes, focus, selection, latest sidebar scroll; save/reopen preserves the plane translation');
  writeFileSync(resolve(output,'plane-translation-report.json'),JSON.stringify({passed:true,cases,measurements,standaloneCef:false},null,2));
  return cases;
}
