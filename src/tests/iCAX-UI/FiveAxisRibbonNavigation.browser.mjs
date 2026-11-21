import assert from 'node:assert/strict';
import {resolve} from 'node:path';

// The shell, product entry, View decoder and WebGL viewport are production
// code. This local transport owns only two placements and async acknowledgments.
export async function verifyRibbonNavigation(page,output){
 const cases=[];
 await page.evaluate(()=>{
  const fixture=__fiveFixture,project=fixture.appProxy.getProduct().getProject(),proxy=project.getMainScene();
  const original={invoke:proxy.invoke,start:proxy.views.start,history:proxy.getUndoRedoState,autosave:project.autosave,path:project.state.projectPath,channel:proxy.sceneChannelId};
  const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],placed=[...identity];placed[3]=360;
  const nav=fixture.ribbonNavigation={sourceId:'sample-panel',operationId:'preview-operation-placement',setupId:'preview-setup',revision:1,activeId:'sample-panel',calls:[],saves:[],readers:[],held:null,armed:null,release:null,original};
  nav.arm=phase=>{if(nav.armed||nav.held)throw Error('Navigation fixture already holds an acknowledgment');nav.armed=phase;};
  nav.resume=()=>nav.release?.();
  nav.pause=async phase=>{
   if(nav.armed!==phase)return;nav.armed=null;nav.held={phase};
   await new Promise(resolve=>{nav.release=()=>{nav.held=null;nav.release=null;resolve();};});
  };
  nav.setActive=id=>{if(![nav.sourceId,nav.operationId].includes(id))throw Error('Unknown navigation placement');nav.activeId=id;nav.revision++;};
  proxy.invoke=async function(method,payload={},options){
   nav.calls.push({method,payload:structuredClone(payload)});
   if(method==='CamWorkpiece.Activate'){
    fixture.requests.push({scope:'scene',method,payload:structuredClone(payload)});await nav.pause('activate');nav.setActive(payload.placementId);return {};
   }
   if(method==='CamOperation.MachineAssignments'){
    fixture.requests.push({scope:'scene',method,payload:structuredClone(payload)});
    return {scopes:{Setup:{mode:'Assigned',machineType:'HeadHead'},Workpiece:{mode:'Inherit'},Project:{mode:'Inherit'},Product:{mode:'Inherit'}},effective:{machineType:'HeadHead',source:'Setup'},productSnapshot:{mode:'Inherit'},productPreferencesRevision:'1'};
   }
   if(/(?:\.Generate|\.Create|\.SetTransform|\.ApplyRegistration|\.Scale|\.Configure|\.Edit)/.test(method))throw Error(`Tab navigation attempted a geometry write: ${method}`);
   if(method==='CamScene.ReadState')await nav.pause('read');
   const state=await original.invoke.call(this,method,payload,options);
   if(method==='CamScene.ReadState'&&state.workpieces.length){
    state.expected.mutationSequence=String(nav.revision);state.activePlacementId=nav.activeId;
    state.activeSetupId=nav.activeId===nav.operationId?nav.setupId:'';
    state.placements=[{id:nav.sourceId,workpieceId:nav.sourceId,setupId:'',transform:identity},{id:nav.operationId,workpieceId:nav.sourceId,setupId:nav.setupId,transform:placed}];
    state.workpieces[0].sourcePlacementId=nav.sourceId;
    const setup=state.setups.find(item=>item.id===nav.setupId);setup.name='工序 1';setup.enabled=true;setup.machineType={machineType:'HeadHead',source:'Setup'};
    state.intentToolpaths=state.intentToolpaths.map(cut=>({...cut,placementId:nav.operationId}));
   }
   return state;
  };
  proxy.getUndoRedoState=async()=>({revision:String(nav.revision),canUndo:false,canRedo:false});
  project.state.projectPath='C:/fixtures/ribbon-navigation.i5cam';
  project.autosave=async(path)=>{nav.saves.push({path,revision:nav.revision});await nav.pause('save');return {saved:true};};
  proxy.views.start=async function(definition,options){
   const reader=await original.start.call(this,definition,options),kind=definition.sources?.some(source=>Object.hasOwn(source.parameters||{},'setupId'))?'setup':'source';
   nav.readers.push({kind,definition:structuredClone(definition)});
   const map=snapshot=>({...snapshot,viewId:`ribbon-navigation-${kind}`,revision:String(100+nav.revision),rows:(snapshot.rows||[]).map(row=>({...row,entityId:kind==='setup'?nav.operationId:nav.sourceId,data:{...row.data,setupPlacementTransform:kind==='setup'?placed:identity}}))});
   return {get snapshot(){return map(reader.snapshot);},async poll(){return map(await reader.poll());},async waitForSnapshot(predicate){const snapshot=map(await reader.poll());if(!predicate(snapshot))throw Error('Navigation View expectation mismatch');return snapshot;},async stop(){return reader.stop();}};
  };
  nav.input=()=>JSON.stringify({workpieces:fixture.view.scene.domain.workpieces.map(({id,sourceRef,geometryRef,previewRef,coordinateFrame})=>({id,sourceRef,geometryRef,previewRef,coordinateFrame})),intentToolpaths:fixture.view.scene.domain.intentToolpaths});
  nav.restore=()=>{nav.resume();proxy.invoke=original.invoke;proxy.views.start=original.start;if(original.history)proxy.getUndoRedoState=original.history;else delete proxy.getUndoRedoState;if(original.autosave)project.autosave=original.autosave;else delete project.autosave;if(original.path===undefined)delete project.state.projectPath;else project.state.projectPath=original.path;if(original.channel===undefined)delete proxy.sceneChannelId;else proxy.sceneChannelId=original.channel;};
 });
 const tab=id=>page.locator(`.product-ribbon .ribbon-tab[data-tab-id="${id}"]`);
 const click=async id=>{const box=await tab(id).boundingBox();assert.ok(box,`visible ${id} tab`);await page.mouse.click(box.x+box.width/2,box.y+box.height/2);};
 const settled=async id=>page.waitForFunction(id=>{
  const view=__fiveFixture.view,frame=id==='workpiece'?'workpiece':'setup';
  return __icaxAppShell.getState().activeRibbonTabId===id&&view.fiveAxisArea===id&&view.activeAreaId===id&&!view.pending&&!view.sceneRefreshPending&&!view.fiveOperationNavigationPending&&!view.fiveOperationAreaSync&&!view.fiveWorkpieceSwitching&&!view.fiveFrameCameraPending&&view.fiveViewportFrame===frame&&view.viewport?.sceneObjects.has(view.scene.activePlacementId);
 },id,{timeout:20000});
 const go=async id=>{await click(id);await settled(id);};
 const held=phase=>page.waitForFunction(phase=>__fiveFixture.ribbonNavigation.held?.phase===phase,phase,{timeout:10000});
 const assertInline=async()=>assert.equal(await page.locator('dialog.five-blocking-progress[open],.five-blocking-operation').count(),0,'navigation must leave actual tabs clickable');
 const snapshot=()=>page.evaluate(()=>{
  const view=__fiveFixture.view;
  return {tab:__icaxAppShell.getState().activeRibbonTabId,area:view.activeAreaId,placement:view.scene.activePlacementId,setup:view.scene.domain.activeSetupId,frame:view.fiveViewportFrame,canvas:view.viewport.renderer.domElement===ribbonNavigationCanvas,viewport:view.viewport===ribbonNavigationViewport,camera:view.viewport.getCameraState()};
 });
 const activations=from=>page.evaluate(from=>__fiveFixture.ribbonNavigation.calls.slice(from).filter(call=>call.method==='CamWorkpiece.Activate').map(call=>call.payload.placementId),from);
 try{
  await page.evaluate(()=>__fiveFixture.populate(18));await settled('workpiece');
  await page.evaluate(()=>{
   const view=__fiveFixture.view,v=view.viewport,camera=v.getCameraState();
   v.setCameraState({...camera,theta:camera.theta+.19,phi:camera.phi*.92,radius:camera.radius*1.08});
   window.ribbonNavigationCanvas=v.renderer.domElement;window.ribbonNavigationViewport=v;
   window.ribbonSourceCamera=v.getCameraState();window.ribbonInputBaseline=__fiveFixture.ribbonNavigation.input();
  });
  const button=tab('operation'),box=await button.boundingBox();
  await button.evaluate(node=>{window.ribbonPressedTab=node;window.ribbonPressedClicks=0;node.addEventListener('click',()=>ribbonPressedClicks++);});
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();
  const pressed=await page.evaluate(()=>{
   const context=__fiveFixture.view.fiveThumbnails.context,root=document.querySelector('.product-ribbon');
   if(typeof context.actions.refreshProductRibbon!=='function')throw Error('The actual shell ribbon refresh action is unavailable');
   const observer=new MutationObserver(()=>{});observer.observe(root,{childList:true,subtree:true,attributes:true});
   context.actions.refreshProductRibbon();const mutations=observer.takeRecords().length;observer.disconnect();
   return {sameTab:ribbonPressedTab===document.querySelector('.ribbon-tab[data-tab-id="operation"]'),sameRibbon:root===document.querySelector('.product-ribbon'),mutations};
  });
  assert.ok(pressed.sameTab&&pressed.sameRibbon&&pressed.mutations>0,JSON.stringify(pressed));
  await page.mouse.up();await settled('operation');
  assert.equal(await page.evaluate(()=>ribbonPressedClicks),1);
  assert.deepEqual((await snapshot()).placement,'preview-operation-placement');
  assert.equal((await snapshot()).frame,'setup');assert.equal((await snapshot()).canvas,true);assert.equal((await snapshot()).viewport,true);
  cases.push('Pointer-down, actual shell ribbon refresh and pointer-up activate Operation with one real click and retain the button listener');

  await page.evaluate(()=>{const v=__fiveFixture.view.viewport,c=v.getCameraState();v.setCameraState({...c,theta:c.theta-.27,phi:c.phi*.89,radius:c.radius*1.13});window.ribbonSetupCamera=v.getCameraState();});
  await go('toolpath');assert.deepEqual((await snapshot()).camera,await page.evaluate(()=>ribbonSetupCamera));
  await go('workpiece');assert.deepEqual((await snapshot()).camera,await page.evaluate(()=>ribbonSourceCamera));
  await go('operation');assert.deepEqual((await snapshot()).camera,await page.evaluate(()=>ribbonSetupCamera));
  assert.ok((await snapshot()).canvas&&(await snapshot()).viewport);
  cases.push('One click switches source and operation contexts; Workpiece restores its own camera while Operation and Toolpath share the setup camera and viewport');

  for(const phase of ['activate','save','read']){
   await go('workpiece');
   const from=await page.evaluate(phase=>{const nav=__fiveFixture.ribbonNavigation;nav.arm(phase);return nav.calls.length;},phase);
   await click('operation');await held(phase);await assertInline();
   await click('workpiece');await click('toolpath');
   await page.evaluate(()=>__fiveFixture.ribbonNavigation.resume());await settled('toolpath');
   const state=await snapshot();assert.equal(state.placement,'preview-operation-placement');assert.equal(state.frame,'setup');assert.ok(state.canvas&&state.viewport);
   assert.deepEqual(await activations(from),['preview-operation-placement'],`queued intermediate Workpiece must not create an extra native switch during ${phase}`);
   assert.deepEqual(state.camera,await page.evaluate(()=>ribbonSetupCamera));
   assert.equal(await page.evaluate(()=>__fiveFixture.view.error||''),'');
  }
  cases.push('Activate, save and read holds keep tabs clickable; rapid real clicks settle on the last Toolpath target without a needless intermediate source write');

  await go('workpiece');
  const currentStart=await page.evaluate(()=>{const nav=__fiveFixture.ribbonNavigation;nav.arm('activate');return nav.calls.length;});
  await click('operation');await held('activate');await assertInline();await click('workpiece');
  await page.evaluate(()=>__fiveFixture.ribbonNavigation.resume());await settled('workpiece');
  assert.deepEqual(await activations(currentStart),['preview-operation-placement','sample-panel']);
  assert.deepEqual((await snapshot()).camera,await page.evaluate(()=>ribbonSourceCamera));
  cases.push('A last click on the still-active Workpiece tab cancels the pending Operation destination and cannot be overwritten by its old continuation');

  await page.evaluate(()=>__fiveFixture.ribbonNavigation.arm('activate'));
  await click('operation');await held('activate');
  const nextTab=tab('toolpath'),nextBox=await nextTab.boundingBox();
  await nextTab.evaluate(node=>{window.ribbonNextPressedTab=node;window.ribbonNextPressedClicks=0;node.addEventListener('click',()=>ribbonNextPressedClicks++);});
  await page.mouse.move(nextBox.x+nextBox.width/2,nextBox.y+nextBox.height/2);await page.mouse.down();
  await page.evaluate(()=>__fiveFixture.ribbonNavigation.resume());await settled('operation');
  assert.equal(await page.evaluate(()=>ribbonNextPressedTab.isConnected&&ribbonNextPressedTab===document.querySelector('.ribbon-tab[data-tab-id="toolpath"]')),true,'A completed earlier page transition must leave the next pressed tab connected');
  await page.mouse.up();await settled('toolpath');
  assert.equal(await page.evaluate(()=>ribbonNextPressedClicks),1);
  assert.deepEqual((await snapshot()).camera,await page.evaluate(()=>ribbonSetupCamera));
  cases.push('A previous page completes its full shell render between pointer-down and pointer-up on the next tab without swallowing that next click');
  await go('workpiece');

  // Model an external context read while Workpiece is visible. The normal
  // attach path must reconcile it before committing the next requested tab.
  const syncStart=await page.evaluate(()=>{const nav=__fiveFixture.ribbonNavigation;nav.setActive(nav.operationId);nav.arm('save');window.ribbonAreaSync=__icaxAppShell.executeRibbonCommand('five.refresh');return nav.calls.length;});
  await held('save');assert.equal(await page.evaluate(()=>!!__fiveFixture.view.fiveOperationAreaSync),true);await assertInline();
  await click('toolpath');await page.evaluate(()=>__fiveFixture.ribbonNavigation.resume());await settled('toolpath');
  assert.deepEqual(await activations(syncStart),['sample-panel','preview-operation-placement']);
  assert.deepEqual((await snapshot()).camera,await page.evaluate(()=>ribbonSetupCamera));
  cases.push('An actual delayed automatic area reconciliation waits safely and then honors a single new Toolpath click');

  await go('workpiece');
  await page.evaluate(()=>__fiveFixture.ribbonNavigation.arm('activate'));
  await click('operation');await held('activate');
  await page.evaluate(()=>{const proxy=__fiveFixture.appProxy.getProduct().getProject().getMainScene();proxy.sceneChannelId='navigation-invalidated-channel';__fiveFixture.ribbonNavigation.resume();});
  await page.waitForFunction(()=>{const view=__fiveFixture.view;return !view.pending&&!view.fiveOperationNavigationPending&&!view.fiveWorkpieceSwitching;});
  assert.equal((await snapshot()).tab,'workpiece');assert.equal((await snapshot()).area,'workpiece');
  assert.deepEqual((await snapshot()).camera,await page.evaluate(()=>ribbonSourceCamera));assert.ok((await snapshot()).canvas);
  await page.evaluate(()=>{const nav=__fiveFixture.ribbonNavigation,proxy=__fiveFixture.appProxy.getProduct().getProject().getMainScene();if(nav.original.channel===undefined)delete proxy.sceneChannelId;else proxy.sceneChannelId=nav.original.channel;});
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await settled('workpiece');
  cases.push('A late preparation from an invalidated scene channel does not reclaim another current page, viewport or camera');

  const unchanged=await page.evaluate(()=>{
   const nav=__fiveFixture.ribbonNavigation;
   return {inputs:nav.input()===ribbonInputBaseline,forbidden:nav.calls.filter(call=>call.method!=='CamWorkpiece.Activate'&&/(?:\.Generate|\.Create|\.SetTransform|\.ApplyRegistration|\.Scale|\.Configure|\.Edit)/.test(call.method)),canvas:__fiveFixture.view.viewport.renderer.domElement===ribbonNavigationCanvas,viewport:__fiveFixture.view.viewport===ribbonNavigationViewport,error:__fiveFixture.view.error||''};
  });
  assert.deepEqual(unchanged,{inputs:true,forbidden:[],canvas:true,viewport:true,error:''});
  await page.screenshot({path:resolve(output,'ribbon-single-click-navigation.png')});
  cases.push('CAD and toolpath inputs remain unchanged; navigation invokes only context activation, reads and controlled archive saves');
  return cases;
 }finally{
  await page.evaluate(()=>__fiveFixture.ribbonNavigation?.restore());
 }
}
