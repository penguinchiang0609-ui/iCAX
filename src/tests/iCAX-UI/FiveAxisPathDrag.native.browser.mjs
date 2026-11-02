import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyPathDrag({page,rpc,output,standaloneCef=false}){
  const read=()=>rpc({action:'read-state'}),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fivePathOperationPending);
  const refresh=async()=>{await command('five.refresh');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.cam-page.cutting');await idle();
  const original=await read(),cuts=original.intentToolpaths.filter(c=>c.enabled!==false&&c.machiningKind!=='Mark');assert.ok(cuts.length>=2);
  const id=cuts[0].id,other=cuts[1].id,cases=[],{drag}=gizmoDriver(page);
  const choose=async mode=>{await page.locator(`[data-five-intent="${id}"]`).click();await command('five.tool.path-'+mode);await idle();await page.waitForFunction(()=>fixture.view.viewport.transformManipulator?.options.owner==='path');};
  const capture=()=>page.evaluate(id=>{
    const v=fixture.view,vp=v.viewport,source=vp.sceneObjects.get(v.scene.intentToolpaths.find(c=>c.id===id).placementId);
    source?.updateWorldMatrix(true,true);
    const line=v.fiveCutPreview.group.children.find(o=>o.userData.cuttingLineId===id);line.updateWorldMatrix(true,false);
    const p=line.geometry.attributes.position,world=[];for(let i=0;i<p.count;i++)world.push(line.position.clone().fromBufferAttribute(p,i).applyMatrix4(line.matrixWorld).toArray());
    return {world,cad:source?.matrixWorld.toArray(),objects:v.fivePathTransform?.objects.length,mode:vp.transformManipulator.control.mode};
  },id);
  const near=(a,b,tol=1e-4)=>{assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<tol,`${a} != ${b}`));};
  const verifyNativePreview=async(held,after,selectedIds)=>{
    for(const field of ['workpieces','placements'])assert.deepEqual(after[field],original[field],`${field} are unchanged by CAM dragging`);
    for(const c of after.intentToolpaths.filter(c=>!selectedIds.includes(c.id)))assert.deepEqual(c,original.intentToolpaths.find(p=>p.id===c.id),'unselected paths are unchanged');
    const shown=await page.evaluate(({id,point})=>{
      const v=fixture.view,p=v.scene.domain.placements.find(p=>p.id===v.scene.intentToolpaths.find(c=>c.id===id).placementId),m=p.transform;
      return point.map((_,i)=>m[i*4]*point[0]+m[i*4+1]*point[1]+m[i*4+2]*point[2]+m[i*4+3]);
    },{id,point:after.intentToolpaths.find(c=>c.id===id).previewPoints[0]});
    near(shown,held.world[0]);
  };
  for(const [projection,space] of [['perspective','local'],['orthographic','world']]){
    await page.locator(`button[data-projection-mode="${projection}"]`).click();await page.locator(`button[data-transform-space="${space}"]`).click();
    for(const [mode,axis] of [['translate','X'],['rotate','Z']]){
      await choose(mode);await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,1]);fixture.view.viewport.fitView(1.1);});
      const before=await read(),base=await capture();
      await drag(axis,30,false);const held=await capture();assert.ok(held.objects>0,'the selected path overlays actually preview');assert.notDeepEqual(held.world,base.world);assert.deepEqual(held.cad,base.cad,'CAD display never moves during a path gesture');assert.deepEqual(await read(),before,'held mouse drag does not write native state');
      // A shortcut cannot switch tools while a gesture owns the pointer.
      await page.keyboard.press('w');assert.equal(await page.evaluate(()=>fixture.view.fiveAxisUI.tools.toolpath),'path-'+mode);
      await page.screenshot({path:resolve(output,`${projection}-${mode}-preview.png`)});
      await page.mouse.up();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
      const after=await read();await verifyNativePreview(held,after,[id]);assert.notDeepEqual(after.intentToolpaths.find(c=>c.id===id).curveRef,before.intentToolpaths.find(c=>c.id===id).curveRef);
      await rpc({action:'undo'});await refresh();near((await read()).intentToolpaths.find(c=>c.id===id).startPoint,before.intentToolpaths.find(c=>c.id===id).startPoint);
      await choose(mode);const cancelBefore=await read(),cancelBase=await capture();await drag(axis,24,false);await page.keyboard.press('Escape');await page.mouse.up();await idle();assert.deepEqual(await read(),cancelBefore);near((await capture()).world.flat(),cancelBase.world.flat());
      cases.push({projection,space,mode,commit:true,cancel:true,cadUnchanged:true});
    }
  }
  await choose('translate');await page.locator(`[data-five-intent="${other}"]`).click({modifiers:['Control']});await drag('Y',25,false);const held=await capture();await page.mouse.up();await idle();const grouped=await read();await verifyNativePreview(held,grouped,[id,other]);
  assert.notDeepEqual(grouped.intentToolpaths.find(c=>c.id===other).startPoint,original.intentToolpaths.find(c=>c.id===other).startPoint,'all selected paths move');
  await rpc({action:'undo'});await refresh();await rpc({action:'redo'});await refresh();near((await read()).intentToolpaths.find(c=>c.id===id).startPoint,grouped.intentToolpaths.find(c=>c.id===id).startPoint);
  await page.locator('canvas').first().focus();await page.keyboard.press('e');assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.mode),'rotate');await page.keyboard.press('r');assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.mode),'rotate');
  await rpc({action:'export-project',path:resolve(output,'path-drag-accepted.i5cam')});const saved=await read();await rpc({action:'save-reopen'});near((await read()).intentToolpaths.find(c=>c.id===id).startPoint,saved.intentToolpaths.find(c=>c.id===id).startPoint);
  writeFileSync(resolve(output,'path-drag-report.json'),JSON.stringify({passed:true,standaloneCef,cases,multiselect:true,undoRedo:true,shortcuts:true,saveReopen:true,cadUnchanged:true},null,2));
}
