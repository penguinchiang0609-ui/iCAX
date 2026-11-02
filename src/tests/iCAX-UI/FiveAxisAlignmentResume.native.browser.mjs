import assert from 'node:assert/strict';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

export const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const near=(a,b,tolerance=1e-5)=>{assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<tolerance,`${v} != ${b[i]} at ${i}`));};
export async function activeHandles(page){
  await page.waitForFunction(()=>!fixture.view.pending&&!!fixture.view.viewport.transformManipulator);
  assert.deepEqual(await page.evaluate(()=>({tool:fixture.view.fiveAxisUI.tools.workpiece,mode:fixture.view.viewport.transformManipulator?.control.mode,open:fixture.view.fivePlacement?.open,toolbar:!!document.querySelector('.five-placement-tools'),alignment:!!fixture.view.fiveAlignment?.open,error:fixture.view.error})),{tool:'translate',mode:'translate',open:true,toolbar:true,alignment:false,error:''});
}
export async function verifyResume({page,read,invoke,rpc,refresh,open,action}){
  await activeHandles(page);const before=await read();
  await page.evaluate(()=>{fixture.view.viewport.setStandardView('top-front-right');fixture.view.viewport.fitView(1.25);});
  await gizmoDriver(page).drag('X',20);await page.waitForFunction(()=>!fixture.view.pending);
  const moved=await read();assert.notDeepEqual(moved.placements[0].transform,before.placements[0].transform);
  assert.deepEqual(moved.workpieces,before.workpieces,'Placement translation preserves object geometry and its local frame');
  await rpc({action:'undo'});await refresh();near((await read()).placements[0].transform,before.placements[0].transform);
  await open();await action('close');await activeHandles(page);
  // A native revision arrives while the editor is open, just as it can during
  // the standalone application's asynchronous SDO polling.
  await open();const scene=await read(),id=await page.evaluate(()=>fixture.view.scene.activePlacementId),placement=scene.placements.find(p=>p.id===id);
  await invoke('CamPlacement.SetTransform',{expected:scene.expected,commandId:crypto.randomUUID(),setupId:scene.activeSetupId,placementId:placement.id,transform:placement.transform});
  await refresh();await activeHandles(page);
  await open();await action('close');await activeHandles(page);
}
export async function worldGeometry(page){return page.evaluate(()=>{
  const v=fixture.view,mesh=v.viewport.sceneObjects.get(v.scene.activePlacementId),position=mesh.geometry.getAttribute('position'),normal=mesh.geometry.getAttribute('normal'),p=mesh.position.clone(),points=[],normals=[];
  mesh.updateMatrixWorld(true);
  for(let i=0;i<position.count;++i){points.push(...p.fromBufferAttribute(position,i).applyMatrix4(mesh.matrixWorld).toArray());if(normal)normals.push(...p.fromBufferAttribute(normal,i).transformDirection(mesh.matrixWorld).toArray());}
  return {points,normals};
});}
export async function verifyFrameReset({page,read,invoke,rpc,refresh,action}){
  const before=await read(),id=await page.evaluate(()=>fixture.view.scene.activePlacementId),original=before.placements.find(p=>p.id===id),workpiece=before.workpieces.find(w=>w.id===original.workpieceId);
  const aligned=await page.evaluate(()=>fixture.view.fiveAlignment.report.transform),world=await worldGeometry(page);
  const catalog=await invoke('CamScene.ReadSnapFeatures',{expected:before.expected,setupId:before.activeSetupId,objectId:id});
  assert.equal(await page.locator('[data-five-command="five.align.reset-frame"]').count(),0);
  await action('apply');await activeHandles(page);
  const after=await read(),placement=after.placements.find(p=>p.id===id),rebased=after.workpieces.find(w=>w.id===placement.workpieceId);
  near(placement.transform,identity);assert.notDeepEqual(rebased.geometryRef,workpiece.geometryRef);assert.deepEqual(rebased.sourceRef,workpiece.sourceRef);
  const actual=await worldGeometry(page);near(actual.points,world.points,5e-5);near(actual.normals,world.normals,1e-6);
  for(const other of before.placements.filter(p=>p.id!==id))assert.deepEqual(after.placements.find(p=>p.id===other.id),other);
  const exact=await invoke('CamScene.ReadSnapFeatures',{expected:after.expected,setupId:after.activeSetupId,objectId:id});
  assert.equal(exact.features.length,catalog.features.length);
  for(let i=0;i<catalog.features.length;++i){const f=catalog.features[i],g=exact.features[i];assert.equal(f.featureId,g.featureId);try{near(g.point,[0,1,2].map(r=>aligned[r*4+3]+f.point.reduce((s,v,c)=>s+v*aligned[r*4+c],0)));}catch(error){throw Error(`Rebased snap feature ${f.featureId} (${f.label}): ${error.message}`);}}
  const report=(await invoke('CamPlacement.ReadRegistration',{expected:after.expected,setupId:after.activeSetupId,placementId:id})).report;
  assert.equal(report.resetWorkpieceFrame,true);near(report.alignedTransform,aligned);near(report.transform,identity);
  await rpc({action:'undo'});await refresh();const undone=await read();assert.deepEqual(undone.workpieces,before.workpieces);assert.deepEqual(undone.placements,before.placements);
  await rpc({action:'redo'});await refresh();const redone=await read();assert.deepEqual(redone.workpieces,after.workpieces);assert.deepEqual(redone.placements,after.placements);
  const redoWorld=await worldGeometry(page);near(redoWorld.points,world.points,5e-5);await activeHandles(page);
}
export async function verifyReopenedFrame({page,rpc,read}){
  const saved=await read(),world=await worldGeometry(page);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});
  assert.deepEqual(reopened.placements,saved.placements);assert.deepEqual(reopened.workpieces,saved.workpieces);
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
  await activeHandles(page);near((await worldGeometry(page)).points,world.points,5e-5);
}
