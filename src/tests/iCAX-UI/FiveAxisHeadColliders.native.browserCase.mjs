import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyHeadColliders({page,invoke,rpc,report,output,heads}){
 const combo=page.locator('[data-five-simulation-field="head-choice"]'),ac=heads.find(h=>h.kind==='AC');await combo.selectOption('builtin:'+ac.id);
 const settled=()=>page.waitForFunction(()=>{const s=f.view.fiveHeadColliders;return !f.view.pending&&!f.view.fiveSimulation.headChoiceBusy&&!s?.saving&&!s?.timer&&!s?.parts.some(p=>p.dirty)&&!s?.request;},null,{timeout:60000});
 await settled();await page.waitForFunction(()=>f.view.fiveHeadPreview?.phase==='ready');
 const baseline=await invoke('CamScene.ReadState');
 const button=action=>page.locator(`[data-five-command="five.head-collider.${action}"]`);
 await page.locator('[data-five-head-collider-field="show"]').check();
 assert.equal(await page.evaluate(()=>f.view.fiveHeadPreview.colliderObjects.size),9);
 assert.equal(await page.evaluate(()=>f.head().collisionModel.boxCount),'6');
 const initial=await page.evaluate(()=>{const s=f.view.fiveHeadColliders,p=s.parts.find(p=>p.index===s.partIndex),b=p.boxes[0];return {part:p.index,id:b.id,position:b.position,size:b.size};});
 await button('edit:'+initial.id).click();
 const toolbar=page.locator('.five-head-preview-stage [data-transform-owner="head-collider"]');assert.equal(await toolbar.isVisible(),true);assert.equal(await page.locator('.five-head-settings .five-placement-tools,.five-head-settings .five-collider-tools').count(),0);
 const selectionState=await invoke('CamScene.ReadState');for(const [key,mode] of [['w','translate'],['e','rotate'],['r','scale'],['m','bounds']]){await page.locator('[data-five-head-preview]').focus();await page.keyboard.press(key);await page.waitForFunction(mode=>f.view.fiveHeadColliders.mode===mode,mode);}assert.deepEqual(await invoke('CamScene.ReadState'),selectionState);
 const field=(group,i)=>page.locator(`[data-five-head-collider-field="box:${initial.id}:${group}:${i}"]`);
 await page.evaluate(()=>{f.I.headInstanceField(f.context,f.view,'tcp.0','11',f.ops);f.ops.renderProject();f.holdRead=true;});
 await field('size',0).fill('205.002');await page.waitForFunction(()=>f.held);
 await page.locator('[data-five-head-instance-field="tcp.0"]').fill('12');
 await field('size',0).fill('206.002');await field('position',0).fill('7');await field('rotation',2).fill('17');
 const interaction=await page.evaluate(()=>{const input=document.activeElement;input.focus({preventScroll:true});input.setSelectionRange(0,1);window.hcKept={input,canvas:f.view.fiveHeadPreview.canvas,model:f.view.fiveHeadPreview.model,left:document.querySelector('.left'),right:document.querySelector('.right'),events:0};input.addEventListener('head-box-probe',()=>hcKept.events++);hcKept.left.scrollTop=170;hcKept.right.scrollTop=230;document.querySelectorAll('.nested').forEach(n=>n.scrollTop=35);return [hcKept.left.scrollTop,hcKept.right.scrollTop];});
 await page.evaluate(()=>f.release());await settled();
 assert.deepEqual(await page.evaluate(()=>{hcKept.input.dispatchEvent(new Event('head-box-probe'));return {same:hcKept.input===document.activeElement,selection:[hcKept.input.selectionStart,hcKept.input.selectionEnd],scroll:[hcKept.left.scrollTop,hcKept.right.scrollTop],nested:[...document.querySelectorAll('.nested')].map(n=>n.scrollTop),canvas:hcKept.canvas===f.view.fiveHeadPreview.canvas,model:hcKept.model===f.view.fiveHeadPreview.model,events:hcKept.events};}),{same:true,selection:[0,1],scroll:interaction,nested:[35,35],canvas:true,model:true,events:1});
 let state=await invoke('CamScene.ReadState'),head=state.setups.find(s=>s.id===state.activeSetupId).headModel;
 let box=head.instance.parameters.collisionOverrides.find(p=>p.partIndex===initial.part).boxes[0];assert.ok(Math.abs(box.geometry.size[0]-.206002)<1e-12);assert.equal(box.pose[0],.007);assert.ok(Math.abs(box.pose[5]-17*Math.PI/180)<1e-10);
 assert.equal(head.tcpLocalPosition[0],baseline.setups.find(s=>s.id===baseline.activeSetupId).headModel.tcpLocalPosition[0]);assert.equal(await page.locator('[data-five-head-instance-field="tcp.0"]').inputValue(),'12');
 await page.locator('[data-five-command="five.head-instance.apply"]').click();await settled();assert.ok(Math.abs((await invoke('CamScene.ReadState')).setups.find(s=>s.id===state.activeSetupId).headModel.instance.parameters.collisionOverrides[0].boxes[0].geometry.size[0]-.206002)<1e-12);
 const beforeInvalid=await invoke('CamScene.ReadState');await field('size',0).fill('0');await page.waitForFunction(()=>f.view.fiveHeadColliders.error.includes('尺寸'));assert.deepEqual(await invoke('CamScene.ReadState'),beforeInvalid);await field('size',0).fill('206.002');await settled();
 await page.locator(`[data-five-head-collider-field="box:${initial.id}:enabled"]`).uncheck();await settled();assert.equal((await invoke('CamScene.ReadState')).setups.find(s=>s.id===state.activeSetupId).headModel.collisionModel.boxCount,'5');
 await page.locator(`[data-five-head-collider-field="box:${initial.id}:enabled"]`).check();await settled();
 // Face dragging after both joints and the local box have rotated must keep
 // the opposite face fixed in preview world coordinates.
 const joints=page.locator('[data-five-head-joint]');await joints.nth(0).fill('37');await joints.nth(0).dispatchEvent('input');await joints.nth(1).fill('22');await joints.nth(1).dispatchEvent('input');
 await button('mode:bounds').click();
 const handle=await page.evaluate(({part,id})=>{
  const s=f.view.fiveHeadPreview,T=f.THREE,e=s.colliderObjects.get(part+':'+id),r=s.canvas.getBoundingClientRect();e.wire.updateWorldMatrix(true,false);const points=[];
  for(let i=0;i<6;i++){const axis=Math.floor(i/2),sign=i%2?1:-1,p=new T.Vector3().setComponent(axis,sign*.5).applyMatrix4(e.wire.matrixWorld),q=p.clone().project(s.camera),step=new T.Vector3().setComponent(axis,sign).transformDirection(e.wire.matrixWorld),v=p.clone().addScaledVector(step,10).project(s.camera),x=r.left+(q.x+1)*r.width/2,y=r.top+(1-q.y)*r.height/2,dx=(v.x-q.x)*r.width/2,dy=-(v.y-q.y)*r.height/2,n=Math.hypot(dx,dy);if(Math.abs(q.z)<1&&x>r.left+15&&x<r.right-15&&y>r.top+15&&y<r.bottom-15)points.push({i,axis,sign,x,y,dx:dx/n,dy:dy/n,n});}
  points.sort((a,b)=>b.n-a.n);const h=points[0];if(!h)throw Error('No visible box face handle');h.opposite=new T.Vector3().setComponent(h.axis,-h.sign*.5).applyMatrix4(e.wire.matrixWorld).toArray();h.size=f.view.fiveHeadColliders.parts.find(p=>p.index===part).boxes.find(b=>b.id===id).size[h.axis];return h;
 },initial);
 await page.mouse.move(handle.x,handle.y);await page.mouse.down();await page.mouse.move(handle.x+handle.dx*32,handle.y+handle.dy*32,{steps:8});await page.mouse.up();await settled();
 const dragged=await page.evaluate(({part,id,handle})=>{const s=f.view.fiveHeadPreview,e=s.colliderObjects.get(part+':'+id);e.wire.updateWorldMatrix(true,false);return {opposite:new f.THREE.Vector3().setComponent(handle.axis,-handle.sign*.5).applyMatrix4(e.wire.matrixWorld).toArray(),size:f.view.fiveHeadColliders.parts.find(p=>p.index===part).boxes.find(b=>b.id===id).size[handle.axis]};},{...initial,handle});
 assert.ok(dragged.size>handle.size);dragged.opposite.forEach((v,i)=>assert.ok(Math.abs(v-handle.opposite[i])<1e-5));
 // Use the shared SDK's real picking driver on this preview's gizmo.
 await page.evaluate(()=>{window.fixture={view:{get viewport(){const s=f.view.fiveHeadPreview;return {camera:s.camera,renderer:s.renderer,get transformManipulator(){return s.colliderManipulator;}};}}};});
 const driver=gizmoDriver(page),transforms=[];
 for(const [mode,axis] of [['translate','X'],['rotate','Z'],['scale','X']]){
  await button('mode:'+mode).click();await button('focus').click();const before=await invoke('CamScene.ReadState');await driver.drag(axis,22);await settled();const after=await invoke('CamScene.ReadState');assert.notDeepEqual(after.headInstances,before.headInstances,mode+' must persist a real edit');transforms.push(mode);
 }
 await button('mode:translate').click();const cancelState=await invoke('CamScene.ReadState');await driver.drag('X',18,false);await page.keyboard.press('Escape');await page.mouse.up();await settled();assert.deepEqual(await invoke('CamScene.ReadState'),cancelState);
 // Add/remove on the fork must update that part's independent collision graph.
 await page.locator('[data-five-head-collider-field="part"]').selectOption('0');await button('add').click();await settled();const forkAdded=await page.evaluate(()=>f.view.fiveHeadColliders.active);head=(await invoke('CamScene.ReadState')).setups.find(s=>s.id===state.activeSetupId).headModel;assert.equal(head.assembly.nodes[0].collisionBoxes.length,4);assert.equal(head.collisionModel.boxCount,'6');await button('remove:'+forkAdded).click();await settled();
 await page.locator('[data-five-head-collider-field="part"]').selectOption(String(initial.part));await button('edit:'+initial.id).click();await button('focus').click();
 await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,'切割头碰撞盒编辑.png')});
 state=await invoke('CamScene.ReadState');for(const key of ['workpieces','placements','intentToolpaths','fixtures','fixtureUnits'])assert.deepEqual(state[key],baseline[key],key);
 const invalid={action:'colliders',commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,instanceId:state.setups.find(s=>s.id===state.activeSetupId).headInstanceId,collisionOverrides:[{partIndex:999,boxes:[]}]};await assert.rejects(invoke('CamHead.Instance',invalid),/CAM_HEAD_COLLISION_PART_INVALID/);assert.deepEqual(await invoke('CamScene.ReadState'),state);
 // Persist the edited head independently in My, then delete the library copy.
 await page.evaluate(async()=>{await f.I.headInstanceCommand(f.context,f.view,'library-open',f.ops);});await page.locator('[data-five-key="head-save-configuration"] > summary').click();await page.locator('[data-five-head-instance-field="profileName"]').fill('碰撞盒编辑验收');await page.locator('[data-five-command="five.head-instance.save-profile"]').click();await settled();
 const entry=(await invoke('CamHead.Configurations',{action:'list'})).items.find(e=>e.name==='碰撞盒编辑验收');assert.ok(entry);
 const expected=state.setups.find(s=>s.id===state.activeSetupId).headModel.instance.parameters.collisionOverrides;await page.locator('[data-five-head-instance-field="entryId"]').selectOption(entry.id);await page.locator('[data-five-command="five.head-instance.use-profile"]').click();await settled();assert.deepEqual((await invoke('CamScene.ReadState')).setups.find(s=>s.id===state.activeSetupId).headModel.instance.parameters.collisionOverrides,expected);
 await page.locator('[data-five-head-instance-field="entryId"]').selectOption(entry.id);await page.locator('[data-five-command="five.head-instance.delete-profile"]').click();await settled();await page.locator('[data-five-command="five.head-instance.library-close"]').click();
 const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.setups.find(s=>s.id===reopened.activeSetupId).headModel.instance.parameters.collisionOverrides,expected);await page.evaluate(()=>f.refresh());
 report.headColliders={passed:true,parts:2,initialBoxes:9,nativeToolBoxes:6,combinedPreviewAngles:[37,22],faceDragOppositeFaceFixed:true,transforms,escapeCancel:true,liveDraftFocusAndScrollRetained:true,personalLibraryAndProjectReopen:true,invalidDimensionsAndPartRejected:true};
}
