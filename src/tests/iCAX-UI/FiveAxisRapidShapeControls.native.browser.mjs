import assert from 'node:assert/strict';
import {writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

// Run only in an isolated, native-backed project. The caller binds the real
// workbench view as window.fixture.view and supplies its normal command/read
// helpers; this case never opens, closes or replaces the user's project.
export async function runRapidShapeControlsCase({page,command,read,idle,output}){
 mkdirSync(output,{recursive:true});const cases=[];
 const g=s=>s.setups.find(x=>x.id===s.activeSetupId).generatedGeometry;
 const field=name=>page.locator(`[data-five-rapid-field="${name}"]`).first();
 const button=name=>page.locator(`[data-five-command="five.rapid.${name}"]`).first();
 const editing=()=>page.evaluate(()=>{const e=fixture.view.fiveRapid.pathEdit;return {points:e.points,directions:e.directions,selected:e.selected,selectedPoints:e.selectedPoints,construction:e.construction,samplePoints:e.samplePoints};});
 const before=await read(),connections=g(before).transitions;
 const automatic=connections.find(t=>!t.manualPath&&Math.abs(t.directions?.[0]?.[2]??0)>.9&&Math.abs(t.directions?.at(-1)?.[2]??0)>.9);
 assert.ok(automatic,'An automatic upright connection is needed for the Arch editing case');
 await command('five.tool.rapid');await field('connection').selectOption(`${automatic.from}/${automatic.to}`);await idle();
 await field('strategy').selectOption('Frog');
 await page.waitForFunction(()=>!fixture.view.fiveRapid.policyChanged&&!fixture.view.fiveRapid.savingPath&&!fixture.view.fiveRapid.saveTimer);
 await button('regenerate').click();await idle();
 assert.equal(await page.evaluate(()=>fixture.view.error),'');
 await button('path-edit').click();await idle();
 const initial=await editing();assert.equal(initial.construction.kind,'Arch');assert.equal(initial.points.length,3);assert.ok(initial.samplePoints.length>3);
 assert.equal(await page.evaluate(()=>fixture.view.fiveRapidPathOverlay.group.children.length),3);
 const ends=[initial.points[0],initial.points.at(-1)],camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
 await gizmoDriver(page).drag('X',24);
 await page.waitForFunction(()=>!fixture.view.fiveRapid.pathEdit.changed&&!fixture.view.fiveRapid.savingPath);
 const edited=await editing();assert.notDeepEqual(edited.construction,initial.construction);assert.deepEqual([edited.points[0],edited.points.at(-1)],ends);assert.equal(edited.points.length,3);
 assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);
 const saved=await read(),editedConnection=g(saved).transitions.find(t=>`${t.from}/${t.to}`===`${automatic.from}/${automatic.to}`);
 assert.deepEqual(Object.keys(editedConnection.manualPath),['construction']);assert.equal(editedConnection.geometryStatus,'Draft');
 cases.push('A real drag edits one Arch shape handle; only three handles exist, endpoints/camera stay fixed, and native persistence stores the construction rather than sampled points');

 // Hold the response, not a fabricated result, while the user changes focus,
 // text selection and three independent scroll containers.
 const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-rapid-body::after{content:"";display:block;height:1500px;flex:none;grid-column:1/-1}.cam-context-pane{overflow:auto!important}.cam-info-pane{height:300px!important;min-height:0!important;max-height:300px!important;overflow:auto!important}.five-rapid-body{height:210px!important;min-height:0!important;overflow:auto!important}'});
 await page.evaluate(()=>{
  const v=fixture.view,proxy=v.fiveRapid.saveBinding.context.sceneProxy,original=proxy.invoke.bind(proxy);
  window.rapidShapeResponse={proxy,original,hold:true};proxy.invoke=async(method,...args)=>{const result=await original(method,...args);if(method==='CamRapid.Preview'&&rapidShapeResponse.hold)await new Promise(resolve=>{rapidShapeResponse.held=true;rapidShapeResponse.release=()=>{rapidShapeResponse.hold=false;resolve();};});return result;};
 });
 try{
  await button('preview').click();await page.waitForFunction(()=>rapidShapeResponse.held);
  const positions=await page.evaluate(()=>{
   const input=document.querySelector('[data-five-rapid-field="pathLiftHeight"]');window.rapidShapeInput=input;window.rapidShapeCanvas=fixture.view.viewport.renderer.domElement;input.focus({preventScroll:true});input.setSelectionRange(0,1);
   return ['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((selector,i)=>{const n=document.querySelector(selector);n.scrollTop=90+i*20;return n.scrollTop;});
  });assert.ok(positions.every(x=>x>0));
  await page.evaluate(()=>rapidShapeResponse.release());await idle();
  assert.equal(await page.evaluate(()=>rapidShapeInput===document.querySelector('[data-five-rapid-field="pathLiftHeight"]')&&rapidShapeCanvas===fixture.view.viewport.renderer.domElement&&document.activeElement===rapidShapeInput),true);
  assert.deepEqual(await field('pathLiftHeight').evaluate(n=>[n.selectionStart,n.selectionEnd]),[0,1]);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(s=>document.querySelector(s).scrollTop)),positions);
  cases.push('A real delayed native response retains the latest input node, focus, selection, canvas and both sidebars plus nested scrolling');
 }finally{await page.evaluate(()=>{if(window.rapidShapeResponse){rapidShapeResponse.release?.();rapidShapeResponse.proxy.invoke=rapidShapeResponse.original;}});await stress.evaluate(n=>n.remove());}

 const authored=g(before).transitions.find(t=>t.manualPath?.points?.length>=2);
 if(authored){
  await button('path-cancel').click();await field('connection').selectOption(`${authored.from}/${authored.to}`);await button('path-edit').click();await idle();
  const original=await editing();assert.equal(original.points.length,authored.manualPath.points.length+2);assert.deepEqual(original.points.slice(1,-1),authored.manualPath.points);
  await button('path-select-all').click();const group=await editing();assert.equal(group.selectedPoints.length,group.points.length-2);
  await gizmoDriver(page).drag('X',18);
  await page.waitForFunction(()=>!fixture.view.fiveRapid.pathEdit.changed&&!fixture.view.fiveRapid.savingPath);
  const moved=await editing(),delta=moved.points[1].map((n,i)=>n-original.points[1][i]);assert.ok(Math.hypot(...delta)>0);
  for(let i=1;i<moved.points.length-1;i++)for(let k=0;k<3;k++)assert.ok(Math.abs(moved.points[i][k]-original.points[i][k]-delta[k])<1e-8);
  assert.deepEqual(moved.points[0],original.points[0]);assert.deepEqual(moved.points.at(-1),original.points.at(-1));
  cases.push('The existing authored Polyline is retained point for point on entry; one real group drag moves all internal controls and neither endpoint');
 }
 cases.push(await runRapidTurnControlsCase({page,read,idle}));
 const after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(after[key],before[key],key+' changed');assert.deepEqual(g(after).operations,g(before).operations);
 await page.screenshot({path:resolve(output,'rapid-shape-controls.png')});
 const report={passed:true,cases,realNative:true,realMouse:true,cadAndCuttingAndFixturesUnchanged:true};writeFileSync(resolve(output,'rapid-shape-controls-report.json'),JSON.stringify(report,null,2));return report;
}
export async function runRapidTurnControlsCase({page,read,idle}){
 const g=s=>s.setups.find(x=>x.id===s.activeSetupId).generatedGeometry;
 const field=name=>page.locator(`[data-five-rapid-field="${name}"]`).first(),button=name=>page.locator(`[data-five-command="five.rapid.${name}"]`).first();
 const editing=()=>page.evaluate(()=>{const e=fixture.view.fiveRapid.pathEdit;return {points:e.points,construction:e.construction,samplePoints:e.samplePoints};});
 const before=await read(),corner=g(before).transitions.find(t=>t.planningMethod==='CornerBridge');assert.ok(corner,'The real project must exercise the short turn connection');
 await button('path-cancel').click();await field('connection').selectOption(`${corner.from}/${corner.to}`);await button('path-edit').click();await idle();
 const turnBefore=await editing();assert.equal(turnBefore.construction,undefined);assert.deepEqual(turnBefore.points,corner.points);assert.equal(turnBefore.points.length,5);assert.equal(turnBefore.samplePoints,undefined);
 assert.equal(await field('pathLiftHeight').count(),0);assert.equal(await page.locator('.five-rapid-point-editor strong').textContent(),'折线控制点');
 assert.equal(await button('path-insert').count(),1);assert.equal(await button('path-delete').count(),1);
 const turnCamera=await page.evaluate(()=>fixture.view.viewport.getCameraState());await gizmoDriver(page).drag('X',16);
 await page.waitForFunction(()=>!fixture.view.fiveRapid.pathEdit.changed&&!fixture.view.fiveRapid.savingPath);
 const turnAfter=await editing();assert.notDeepEqual(turnAfter.points[2],turnBefore.points[2]);assert.deepEqual(turnAfter.points[1],turnBefore.points[1]);assert.deepEqual(turnAfter.points[3],turnBefore.points[3]);
 assert.deepEqual([turnAfter.points[0],turnAfter.points.at(-1)],[turnBefore.points[0],turnBefore.points.at(-1)]);assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),turnCamera);
 const turnSaved=g(await read()).transitions.find(t=>`${t.from}/${t.to}`===`${corner.from}/${corner.to}`);assert.deepEqual(turnSaved.manualPath.points,turnAfter.points.slice(1,-1));assert.equal(turnSaved.manualPath.construction,undefined);
 const message='The short turn connection exposes all five original polyline vertices; a real turn-vertex drag persists the same path type, with the other vertices, locked endpoints and camera unchanged';
 const after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(after[key],before[key],key+' changed');assert.deepEqual(g(after).operations,g(before).operations);
 return message;
}
