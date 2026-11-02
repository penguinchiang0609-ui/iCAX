import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyStandardHoles({page,rpc,output,automotive=false}) {
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const openAdvancedHole=async()=>{
    await command('five.tool.settings');
    assert.equal(await page.locator('[data-five-command="five.tool.path-hole"]').isVisible(),false);
    await page.locator('[data-five-key="advanced-settings"] summary').click();
    await page.getByRole('button',{name:'孔加工方式',exact:true}).click();
  };
  const before=await read(),cut=before.intentToolpaths.find(c=>automotive?c.previewPoints.length<200:c.cutType==='StandardHole');assert.ok(cut);
  await openAdvancedHole();
  await page.locator('#five-hole-cut').selectOption(cut.id);
  const type=page.locator('#five-hole-type'),confirm=page.locator('[data-five-command="five.hole.confirm"]');
  assert.equal(await page.getByRole('dialog',{name:'孔加工方式',exact:true}).count(),1);
  assert.equal(await page.locator('[data-five-key="draft-note"]').count(),0);
  assert.equal(await page.locator('.five-dock').isVisible(),false);
  assert.equal(await type.inputValue(),automotive?'Contour':'StandardHole');
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const point=await page.evaluate(id=>{
    const cut=fixture.view.scene.intentToolpaths.find(c=>c.id===id),v=fixture.view.viewport,box=v.renderer.domElement.getBoundingClientRect(),object=v.sceneObjects.get(cut.placementId);
    for(const p of cut.previewPoints){
      const q=object.localToWorld(v.camera.position.clone().set(...p)).project(v.camera),x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;
      if(x>650&&x<box.right-30&&y>box.y+50&&y<box.bottom-30)return {x,y};
    }
    throw Error('No visible standard-hole selection point');
  },cut.id);
  await page.mouse.click(point.x,point.y);
  assert.equal(await page.locator('#five-hole-cut').inputValue(),cut.id,'actual viewport line selection updates dialog');
  if(automotive) {
    assert.equal(await type.locator('option[value="StandardHole"]').isDisabled(),true);
    await assert.rejects(invoke('CamCut.SetType',{commandId:crypto.randomUUID(),expected:before.expected,intentIds:[cut.id],cutType:'StandardHole'}),/NOT_ORIGINAL_STANDARD_HOLE/);
    assert.deepEqual(await read(),before,'scanned splines are not fitted to fake standard holes');
    await page.screenshot({path:resolve(output,'automotive-standard-hole.png')});
    await confirm.click();await idle();assert.deepEqual(await read(),before,'confirming contour is a no-op');
  } else {
    await type.selectOption('Contour');
    await page.locator('[data-five-command="five.hole.close"]').click();assert.deepEqual(await read(),before,'cancel discards selection');
    await openAdvancedHole();await type.selectOption('Contour');
    await page.evaluate(()=>{fixture.loseMethod='CamCut.SetType';window.holeCanvas=document.querySelector('canvas');window.holeTree=document.querySelector('.five-cut-tree');});
    await confirm.click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
    const contour=await read();assert.equal(contour.intentToolpaths[0].cutType,'Contour');
    for(const k of ['curveRef','sourceKey','processRef','start','number'])assert.deepEqual(contour.intentToolpaths[0][k],cut[k]);
    assert.equal(await page.evaluate(()=>holeCanvas===document.querySelector('canvas')&&holeTree===document.querySelector('.five-cut-tree')),true);
    await rpc({action:'undo'});await command('five.refresh');assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);
    await rpc({action:'redo'});await command('five.refresh');assert.deepEqual((await read()).intentToolpaths,contour.intentToolpaths);
    await openAdvancedHole();await type.selectOption('StandardHole');await confirm.click();await idle();
    await command('five.tool.path-start');
    await page.mouse.click(point.x,point.y);await idle();
    assert.equal(await page.getByText('标准孔不能设置起点。',{exact:true}).count(),1);
    await openAdvancedHole();
    await page.screenshot({path:resolve(output,'standard-hole-setting.png')});
    if(process.env.ICAX_STANDARD_HOLE_PROJECT)await rpc({action:'export-project',path:resolve(process.env.ICAX_STANDARD_HOLE_PROJECT)});
    await type.selectOption('Contour');await confirm.click();await idle();
    assert.equal((await read()).intentToolpaths[0].cutType,'Contour');
  }
  writeFileSync(resolve(output,'standard-holes-report.json'),JSON.stringify({passed:true,automotive,coverage:{native:true,realMouse:true,standaloneCef:false},cases:automotive?
    ['real Mercedes spline selection','standard-hole option unavailable','native rejects forced conversion without mutation','contour no-op']:
    ['FACAM two-selector dialog','actual viewport selection','cancel read-only','lost response recovery','exact geometry and process unchanged','canvas/tree nodes retained','undo redo','standard restored','standard start blocked']},null,2));
}
