import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyStandardHoleSplitting({page,rpc,output}) {
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const before=await read();
  assert.ok(before.intentToolpaths.some(c=>c.cutType==='StandardHole'&&c.geometryKind==='Slot'));
  await command('five.tool.split');await page.evaluate(()=>fixture.view.viewport.fitView());
  const point=await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const view=fixture.view,v=view.viewport,box=v.renderer.domElement.getBoundingClientRect();
    const dialog=document.querySelector('[data-five-key="split-dialog"]').getBoundingClientRect();
    const screen=p=>{const q=p.project(v.camera);return {x:box.left+(q.x+1)*box.width/2,y:box.top+(1-q.y)*box.height/2};};
    for(const cut of view.scene.intentToolpaths.filter(c=>c.cutType==='StandardHole'&&c.geometryKind==='Slot')){
      const placement=view.scene.domain.placements.find(p=>p.id===cut.placementId),matrix=new THREE.Matrix4().set(...placement.transform);
      const project=p=>screen(new THREE.Vector3(...p).applyMatrix4(matrix));
      const start=project(cut.startPoint);
      for(const p of cut.previewPoints.slice(2,-2)){
        const q=project(p);
        if(q.x>dialog.right+24&&q.x<box.right-80&&q.y>box.top+100&&q.y<box.bottom-80&&Math.hypot(q.x-start.x,q.y-start.y)>30)return {id:cut.id,...q};
      }
    }
    throw Error('No unobstructed standard slot boundary');
  });
  const original=before.intentToolpaths.find(c=>c.id===point.id);
  await page.evaluate(()=>{fixture.standardSplitCanvas=fixture.view.viewport.renderer.domElement;fixture.standardSplitTree=document.querySelector('.five-cut-tree');});
  await page.mouse.click(point.x,point.y);await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.deepEqual(await page.evaluate(()=>fixture.view.fiveSplit.points.map(p=>p.intentId)),[point.id]);
  assert.deepEqual(await read(),before,'point resolution does not convert the standard hole');
  await page.getByRole('button',{name:'取消',exact:true}).click();
  assert.deepEqual(await read(),before,'Cancel leaves the standard hole and project unchanged');
  await command('five.tool.split');
  await page.mouse.click(point.x,point.y);await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await page.screenshot({path:resolve(output,'standard-slot-split-picked.png')});
  await page.getByRole('button',{name:'确定',exact:true}).click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const after=await read();assert.equal(after.intentToolpaths.length,before.intentToolpaths.length+1);
  const pieces=after.intentToolpaths.filter(c=>c.split&&c.sourceKey===original.sourceKey);assert.equal(pieces.length,2);
  assert.ok(pieces.some(c=>c.id===original.id&&c.number===original.number));
  assert.ok(pieces.every(c=>c.cutType==='Contour'&&!c.canBeStandardHole&&c.processRef.url===original.processRef.url));
  const definitions=await invoke('CamCut.Read',{expected:after.expected,intentIds:pieces.map(c=>c.id)});
  assert.ok(definitions.items.every(c=>!c.closed&&c.geometryKind==='GeneralContour'));
  assert.deepEqual(after.intentToolpaths.filter(c=>!pieces.some(p=>p.id===c.id)),before.intentToolpaths.filter(c=>c.id!==point.id));
  assert.equal(await page.evaluate(()=>fixture.standardSplitCanvas===fixture.view.viewport.renderer.domElement&&fixture.standardSplitTree===document.querySelector('.five-cut-tree')),true);
  await page.screenshot({path:resolve(output,'standard-slot-split-confirmed.png')});
  await rpc({action:'undo'});await command('five.refresh');await idle();
  assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths,'one undo restores the standard hole');
  await rpc({action:'redo'});await command('five.refresh');await idle();
  assert.deepEqual((await read()).intentToolpaths,after.intentToolpaths);
  writeFileSync(resolve(output,'standard-hole-split-report.json'),JSON.stringify({passed:true,
    model:'Pololu-2257-stamped-bracket.step',before:before.intentToolpaths.length,after:after.intentToolpaths.length,
    cases:['real mouse picks a standard slot directly','picking and Cancel do not change the project','confirm converts and splits in one transaction','exact open contour fragments keep identity number and process','unrelated paths unchanged','one undo restores the standard hole; redo restores fragments','canvas and tree nodes preserved'],native:true,browser:'Edge',standaloneCef:false},null,2));
}
