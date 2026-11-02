import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyRapidOriginalVertices({page,rpc,output,standaloneCef=false}){
 const read=()=>rpc({action:'read-state'}),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 const idle=()=>page.waitForFunction(()=>{const v=fixture.view,s=v.fiveRapid;return !v.pending&&!s?.running&&!s?.savingPath&&!s?.topologySaving&&!s?.saveTimer&&!s?.pathEdit?.changed;});
 const field=name=>page.locator('.cam-info-pane [data-five-rapid-field="'+name+'"]');
 const button=name=>page.locator('.cam-info-pane [data-five-command="five.rapid.path-'+name+'"]');
 const edit=()=>page.evaluate(()=>structuredClone(fixture.view.fiveRapid.pathEdit));
 const geometry=s=>s.setups.find(t=>t.id===s.activeSetupId).generatedGeometry;
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
 await command('five.tool.generate');await idle();
 const initial=await read(),connection=geometry(initial).transitions.find(t=>t.planningMethod==='CornerBridge')||geometry(initial).transitions.find(t=>t.policy.strategy==='Quick'&&!t.manualPath);
 assert.ok(connection,'actual generated Quick retraction');const pair=connection.from+'/'+connection.to;
 const savedRoute=async()=>geometry(await read()).transitions.find(t=>t.from+'/'+t.to===pair);
 await field('connection').selectOption(pair);await button('edit').click();await idle();
 const seed=await edit();assert.equal(seed.pathKind,'Polyline');assert.equal(seed.construction,undefined);assert.equal(seed.samplePoints,undefined);
 assert.deepEqual(seed.points,connection.points,'the controls are the original exact vertices');assert.deepEqual(seed.directions,connection.directions);
 assert.ok(seed.points.length===4||seed.points.length===5);assert.equal(await button('insert').innerText(),'插入点');assert.equal(await button('delete').innerText(),'删除点');
 assert.equal((await savedRoute()).manualPath,connection.manualPath,'opening the controls does not write a manual route');
 const pointOnCanvas=async(point,{index=null,segment=false}={})=>page.evaluate(async({point,index,segment})=>{
  const base=location.protocol==='file:'?new URL('../../../',location.href).href:'/src/';
  const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{rapidPathPointAtPointer}=await import(base+'apps/five-axis-cam/webpage/rapidPathEditing.mjs');
  const v=fixture.view,vp=v.viewport,canvas=vp.renderer.domElement,target=new T.Vector3(...point);
  for(const radius of [40,15,80])for(const vector of [[1,-1,1],[-1,-1,1],[0,.1,1]]){
   const d=new T.Vector3(...vector).normalize();vp.setCameraState({...vp.getCameraState(),target:{x:point[0]+radius*.3,y:point[1],z:point[2]},radius,theta:Math.atan2(d.y,d.x),phi:Math.acos(d.z)});vp.camera.updateMatrixWorld(true);
   await new Promise(resolve=>requestAnimationFrame(resolve));
   const p=target.clone().project(vp.camera),r=canvas.getBoundingClientRect(),event={clientX:r.left+(p.x+1)*r.width/2,clientY:r.top+(1-p.y)*r.height/2};
   if(document.elementFromPoint(event.clientX,event.clientY)!==canvas)continue;
   const hit=rapidPathPointAtPointer(v,event);
   if(segment&&(!hit||Math.hypot(...hit.point.map((n,i)=>n-point[i]))>.01||v.fiveRapid.pathEdit.points.some(q=>new T.Vector3(...q).project(vp.camera).sub(p).multiply(new T.Vector3(r.width/2,r.height/2,0)).length()<14)))continue;
   if(index!==null&&index>0&&index<v.fiveRapid.pathEdit.points.length-1&&hit?.index!==index)continue;
   return {x:event.clientX,y:event.clientY};
  }throw Error('No exposed original rapid vertex/segment');
 },{point,index,segment});
 await button('insert').click();
 const a=seed.points[1],b=seed.points[2];let hit;
 for(const fraction of [.25,.5,.75]){try{hit=await pointOnCanvas(a.map((n,i)=>n+(b[i]-n)*fraction),{segment:true});break;}catch(error){if(fraction===.75)throw error;}}
 await page.mouse.click(hit.x,hit.y);await idle();const inserted=await edit(),insertedRoute=await savedRoute();assert.equal(inserted.points.length,seed.points.length+1);assert.equal(inserted.inserting,true);
 assert.deepEqual(insertedRoute.manualPath.points,inserted.points.slice(1,-1));assert.deepEqual(insertedRoute.points,inserted.points);assert.equal(insertedRoute.manualPath.construction,undefined);
 assert.deepEqual([inserted.points[0],inserted.points.at(-1)],[seed.points[0],seed.points.at(-1)]);
 const insertedPoint=inserted.points.find(p=>!seed.points.some(q=>Math.hypot(...p.map((n,i)=>n-q[i]))<1e-7));assert.ok(insertedPoint);
 await button('delete').click();const index=inserted.points.indexOf(insertedPoint),drop=await pointOnCanvas(insertedPoint,{index});await page.mouse.click(drop.x,drop.y);await idle();
 assert.deepEqual((await edit()).points,seed.points);assert.equal((await edit()).deleting,true);
 for(const index of [0,seed.points.length-1]){const before=await read(),position=await pointOnCanvas(seed.points[index],{index});await page.mouse.click(position.x,position.y);await idle();assert.deepEqual(await read(),before,'endpoints cannot be deleted');}
 await page.keyboard.press('Escape');await rpc({action:'undo'});await command('five.refresh');await idle();assert.deepEqual((await savedRoute()).manualPath,insertedRoute.manualPath);
 await rpc({action:'redo'});await command('five.refresh');await idle();assert.deepEqual((await savedRoute()).manualPath.points,seed.points.slice(1,-1));
 await command('five.tool.rapid');await field('connection').selectOption(pair);await button('edit').click();await idle();
 await field('pathPoint').selectOption('1');const beforeDrag=await savedRoute();await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,1]);fixture.view.viewport.fitView(1.1);});
 await gizmoDriver(page).drag('X',20);await idle();const dragged=await savedRoute();assert.notDeepEqual(dragged.manualPath.points,beforeDrag.manualPath.points);assert.deepEqual([dragged.points[0],dragged.points.at(-1)],[seed.points[0],seed.points.at(-1)]);
 const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-rapid-body::after{content:"";display:block;height:1500px;flex:none;grid-column:1/-1}.cam-context-pane,.cam-info-pane{overflow:auto!important}.five-rapid-body{max-height:420px;overflow:auto!important}'});
 await page.evaluate(()=>{
  const proxy=fixture.view.fiveRapid.saveBinding.context.sceneProxy,invoke=proxy.invoke.bind(proxy);window.vertexReceipt={proxy,invoke,hold:true};
  proxy.invoke=async(method,...args)=>{const result=await invoke(method,...args);if(method==='CamRapid.EditPath'&&vertexReceipt.hold)await new Promise(resolve=>{vertexReceipt.held=true;vertexReceipt.release=()=>{vertexReceipt.hold=false;resolve();};});return result;};
 });
 try{
  await field('pathX').fill(String((await edit()).points[1][0]+.1));await page.waitForFunction(()=>vertexReceipt.held);
  const latest=await page.evaluate(()=>{
   const input=document.querySelector('.cam-info-pane [data-five-rapid-field="pathX"]');window.vertexInput=input;window.vertexCanvas=fixture.view.viewport.renderer.domElement;
   input.focus({preventScroll:true});input.setSelectionRange(1,3);window.vertexKeyEvents=0;input.addEventListener('keydown',()=>vertexKeyEvents++);
   return ['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((selector,i)=>{const n=document.querySelector(selector);n.scrollTop=55+i*20;return n.scrollTop;});
  });
  await page.evaluate(()=>vertexReceipt.release());await idle();
  assert.equal(await page.evaluate(()=>vertexInput===document.querySelector('.cam-info-pane [data-five-rapid-field="pathX"]')&&vertexCanvas===fixture.view.viewport.renderer.domElement&&document.activeElement===vertexInput),true);
  assert.deepEqual(await field('pathX').evaluate(n=>[n.selectionStart,n.selectionEnd]),[1,3]);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(selector=>document.querySelector(selector).scrollTop)),latest);
  await field('pathX').press('Shift');assert.ok(await page.evaluate(()=>vertexKeyEvents>0));
 }finally{await page.evaluate(()=>{vertexReceipt.release?.();vertexReceipt.proxy.invoke=vertexReceipt.invoke;});await stress.evaluate(n=>n.remove());}
 const finalRoute=await savedRoute();
 await page.screenshot({path:resolve(output,'original-polyline-controls.png')});
 const after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(after[key],initial[key],key+' is unchanged');
 await rpc({action:'export-project',path:resolve(output,'original-vertices-accepted.i5cam')});await rpc({action:'save-reopen'});assert.deepEqual((await savedRoute()).manualPath,finalRoute.manualPath);
 writeFileSync(resolve(output,'original-vertices-report.json'),JSON.stringify({passed:true,standaloneCef,originalVertexCount:seed.points.length,allOriginalVertices:true,noSamplesAsControls:true,insertion:true,deletion:true,endpointProtection:true,realMouseDrag:true,undoRedo:true,saveReopen:true,asyncFocusSelectionScrollAndNodes:true,cadAndCuttingAndFixturesUnchanged:true},null,2));
}
