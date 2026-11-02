import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyLocating({page,command,read,output,base='/src/',holdRefresh=false}){
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const click=async id=>{await page.locator(`[data-five-command="${id}"]`).last().click();await idle();};
  await command('five.tool.registration');await idle();
  const dialog=page.getByRole('dialog',{name:'三点定位',exact:true});
  await dialog.waitFor();
  assert.equal(await page.locator('.cam-info-pane [data-five-key="locating-dialog"]').count(),1);
  assert.equal(await page.locator('[data-five-floating="registration"]').count(),0);
  assert.equal(await page.locator('[data-command-id="five.tool.setup"]').count(),0);
  const bounds=await dialog.boundingBox(),canvas=await page.locator('canvas').first().boundingBox();
  assert.ok(bounds.x>=canvas.x+canvas.width-2,'Locating controls stay outside the model viewport');
  await click('five.locating.pick:0');
  await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.scene.activePlacementId));
  const centres=await page.evaluate(()=>fixture.view.sceneSnapping.catalogs.get(fixture.view.scene.activePlacementId).features.filter(f=>f.featureId.startsWith('cylinder-center:')));
  assert.equal(centres.length,3);
  assert.ok(centres.every(f=>f.mode==='datum-point'&&f.label==='圆柱面轴心'));
  // Refresh while the user keeps interacting: preserve the dialog node,
  // keyboard focus and each latest independent scroll offset.
  await page.addStyleTag({content:'.locating-scroll-test .five-locating-body,.locating-scroll-test .cam-context-pane,.locating-scroll-test .cam-info-pane{max-height:130px!important}'});
  await page.evaluate(()=>document.body.classList.add('locating-scroll-test'));
  if(holdRefresh)await page.evaluate(()=>{fixture.hold=true;fixture.holdMethod='CamScene.ReadState';});
  const refreshing=command('five.refresh');
  if(holdRefresh)await page.waitForFunction(()=>fixture.held);
  else await refreshing;
  const interaction=await page.evaluate(()=>{
    const d=document.querySelector('[data-five-key="locating-dialog"]'),body=d.querySelector('.five-position-body');
    fixture.locatingNode=d;fixture.locatingButton=d;
    fixture.locatingButton.focus({preventScroll:true});
    const panes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),body];
    for(const p of panes)p.scrollTop=40;
    return panes.map(p=>p.scrollTop);
  });
  if(holdRefresh)await page.evaluate(()=>fixture.release());
  await refreshing;await idle();
  const retained=await page.evaluate(()=>({same:fixture.locatingNode===document.querySelector('[data-five-key="locating-dialog"]'),focus:document.activeElement===fixture.locatingButton,
    scroll:[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),fixture.locatingNode.querySelector('.five-position-body')].map(p=>p.scrollTop)}));
  assert.equal(retained.same,true);assert.equal(retained.focus,true);assert.deepEqual(retained.scroll,interaction);
  await page.evaluate(()=>document.body.classList.remove('locating-scroll-test'));
  for(let index=0;index<3;++index){
    const target=await page.evaluate(async({base,featureId})=>{
      const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js');
      const v=fixture.view,vp=v.viewport,id=v.scene.activePlacementId,o=vp.sceneObjects.get(id);
      const f=v.sceneSnapping.catalogs.get(id).features.find(f=>f.featureId===featureId);
      vp.setViewDirection(f.direction);vp.fitView(1.45);vp.scene.updateMatrixWorld(true);
      const p=new T.Vector3(...f.point).applyMatrix4(o.matrixWorld).project(vp.camera),r=vp.renderer.domElement.getBoundingClientRect();
      return {x:r.left+(p.x+1)*r.width/2,y:r.top+(1-p.y)*r.height/2};
    },{base,featureId:centres[index].featureId});
    await page.mouse.move(target.x,target.y);
    await page.waitForFunction(id=>fixture.view.fiveLocating.hover?.query.featureId===id,centres[index].featureId);
    await page.mouse.click(target.x,target.y);await idle();
    assert.equal(await page.evaluate(()=>fixture.view.error),'');
    assert.equal(await page.evaluate(i=>fixture.view.fiveLocating.points[i]?.query.featureId,index),centres[index].featureId);
  }
  await click('five.locating.save');
  const state=await read(),plan=state.setups.find(s=>s.id===state.activeSetupId).registrationPlan;
  assert.equal(plan.status,'Prepared');assert.equal(plan.points.length,3);assert.equal(plan.machineStatus,'Unmeasured');
  for(let i=0;i<3;i++){
    assert.equal(plan.points[i].query.featureId,centres[i].featureId);
    for(let j=0;j<3;j++)assert.ok(Math.abs(plan.points[i].modelPoint[j]-centres[i].point[j])<1e-7);
  }
  await page.screenshot({path:resolve(output,'cup-three-centres.png')});
  await click('five.locating.pick:1');await page.keyboard.press('Escape');await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveLocating.picking),null);
  assert.equal(await dialog.count(),1);
  await click('five.locating.close');assert.equal(await dialog.count(),0);
  assert.equal(await page.evaluate(()=>!!fixture.view.fiveLocating.layer||!!fixture.view.sceneSnapping?.presentation),false);
  await command('five.tool.registration');await idle();assert.equal(await dialog.count(),1);
  assert.equal(await page.evaluate(()=>fixture.view.fiveLocating.points.filter(Boolean).length),3);
  return ['Locating controls dock on the right outside the viewport; redundant setup ribbon entry removed',
    'Refresh preserves dialog DOM, keyboard focus and separate sidebar/dialog scroll positions',
    'All three real cup cylinder centres selected using mouse hover/click and saved through native BRep reference resolution',
    'Escape stops picking; closing disposes scene markers; reopening retains the three saved references'];
}
