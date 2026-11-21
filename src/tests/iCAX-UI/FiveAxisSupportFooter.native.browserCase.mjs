import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';

export async function verifySupportFooter({page,rpc,invoke,command,idle,output,report,checkpoint,targetPlacementId,info}){
 const read=()=>invoke('CamScene.ReadState');
 const edit=async(method,payload)=>invoke(method,{commandId:crypto.randomUUID(),expected:(await read()).expected,...payload});
 const settle=(timeout=30000)=>page.waitForFunction(()=>!f.view.pending&&!f.view.fixtureRequest&&!f.view.fiveOperationBusy&&!f.view.fiveOperationAreaSync&&!f.view.fiveOperationNavigationPending&&!f.view.fiveWorkpieceSwitching,undefined,{timeout});
 const cad=state=>state.workpieces.map(w=>({id:w.id,sourceRef:w.sourceRef,geometryRef:w.geometryRef,previewRef:w.previewRef,coordinateFrame:w.coordinateFrame}));
 const digest=value=>createHash('sha256').update(JSON.stringify(value)).digest('hex');
 const pillarId=(await read()).placements.find(p=>p.id===targetPlacementId).workpieceId;
 const reference=page.locator('[data-five-key="operation-reference-value"]');
 const chooser=page.locator('[data-five-cam-context-field="five-workpiece"]');
 const chosenWorkpiece=()=>page.locator('[data-five-cam-context-choice="five-workpiece"][aria-pressed="true"]').getAttribute('data-value');
 const chooseWorkpiece=async id=>{await chooser.click();await page.locator(`[data-five-cam-context-choice="five-workpiece"][data-value="${id}"]`).click();await settle();};
 const create=async(workpieceId,machineType)=>{
  const before=await read(),ids=new Set(before.setups.map(s=>s.id)),source=before.workpieces.find(w=>w.id===workpieceId);
  await page.locator('[data-five-command="five.operation.create"]').first().click();await settle();
  const dialog=page.getByRole('dialog',{name:'新增工序',exact:true}),machine=dialog.locator('[name="machineType"]'),apply=dialog.locator('[data-operation-create-apply]');
  await dialog.waitFor();assert.equal(await dialog.locator('[name="workpieceId"]').count(),0);assert.equal(await machine.inputValue(),'');assert.equal(await apply.isDisabled(),true);assert.ok((await dialog.locator('[data-operation-create-workpiece]').innerText()).includes(source.name));
  await page.screenshot({path:resolve(output,'新增工序-仅选择机型.png')});
  await machine.selectOption(machineType);await settle();assert.deepEqual(await read(),before,'Selecting a machine in the modal must remain a local draft');
  await page.evaluate(()=>{window.workpieceCreateKept={dialog:document.querySelector('.five-operation-create-dialog'),machine:document.querySelector('.five-operation-create-dialog [name="machineType"]'),canvas:f.view.viewport.renderer.domElement};workpieceCreateKept.machine.focus({preventScroll:true});f.holdRead=true;f.workpieceCreateRefresh=__icaxAppShell.executeRibbonCommand('five.refresh');});
  await page.waitForFunction(()=>f.held);await page.evaluate(async()=>{f.release();await f.workpieceCreateRefresh;});await settle();
  assert.deepEqual(await page.evaluate(()=>({dialog:workpieceCreateKept.dialog===document.querySelector('.five-operation-create-dialog'),machine:workpieceCreateKept.machine===document.querySelector('.five-operation-create-dialog [name="machineType"]'),focus:document.activeElement===workpieceCreateKept.machine,canvas:workpieceCreateKept.canvas===f.view.viewport.renderer.domElement})),{dialog:true,machine:true,focus:true,canvas:true});assert.equal(await machine.inputValue(),machineType);assert.deepEqual(await read(),before);
  await apply.click();await settle();assert.equal(await dialog.count(),0);
  const after=await read(),added=after.setups.filter(s=>!ids.has(s.id));assert.equal(added.length,1);
  const placement=after.placements.find(p=>p.setupId===added[0].id);assert.equal(placement.workpieceId,workpieceId);assert.equal(after.activePlacementId,placement.id);assert.equal(added[0].machineType.machineType,machineType);assert.equal(await chosenWorkpiece(),workpieceId);
  return placement;
 };
 const assertList=async workpieceId=>{
  const state=await read(),expected=state.placements.filter(p=>p.setupId&&p.workpieceId===workpieceId).map(p=>p.id).sort();
  const actual=(await page.locator('.five-operation-list [data-five-command^="five.setup.activate:"]').evaluateAll(nodes=>nodes.map(node=>node.dataset.fiveCommand.split(':')[1]))).sort();
  assert.deepEqual(actual,expected);assert.equal(await chosenWorkpiece(),workpieceId);return actual;
 };
 await command('five.refresh');await settle();
 let task=await edit('CamWorkpiece.Import',{sourcePath:info.fixturePath});
 while(task.status==='Running'){await new Promise(r=>setTimeout(r,25));task=await invoke('CamWorkpiece.QueryTask',{epoch:task.epoch,taskId:task.taskId});}
 assert.equal(task.status,'Completed',task.error);
 let state=await read();const secondSource=state.placements.find(p=>p.id===state.activePlacementId),secondId=secondSource.workpieceId;
 const beforeContextCad=cad(state);await command('five.refresh');await settle();await page.locator('[data-tab-id="operation"]').click();await settle();
 assert.equal(await chooser.getAttribute('aria-disabled'),'false');assert.equal(await page.locator('[data-ribbon-workspace-field="five-pose"]').count(),0);
 assert.deepEqual(await assertList(secondId),[]);assert.equal((await read()).activePlacementId,secondSource.id,'A workpiece with no operation must keep its own source placement instead of falling back to another workpiece');
 assert.equal(await reference.count(),0);assert.match(await page.locator('.cam-info-pane').innerText(),/新增工序/);
 await page.screenshot({path:resolve(output,'工序-当前工件暂无工序.png')});
 const secondOperation=await create(secondId,'HeadTable');await assertList(secondId);
 await chooseWorkpiece(pillarId);assert.equal((await read()).activePlacementId,targetPlacementId);assert.deepEqual(await assertList(pillarId),[targetPlacementId]);
 const pillarOperation=await create(pillarId,'HeadHead');assert.equal((await assertList(pillarId)).length,2);
 await chooseWorkpiece(secondId);assert.equal((await read()).activePlacementId,secondOperation.id);assert.deepEqual(await assertList(secondId),[secondOperation.id]);
 await chooseWorkpiece(pillarId);assert.equal((await read()).activePlacementId,pillarOperation.id);await assertList(pillarId);
 await page.locator('[data-tab-id="workpiece"]').click();await settle();state=await read();assert.equal(state.placements.find(p=>p.id===state.activePlacementId).workpieceId,pillarId);
 await page.locator('[data-tab-id="operation"]').click();await settle();assert.equal((await read()).activePlacementId,pillarOperation.id);assert.equal(await chosenWorkpiece(),pillarId);
 await page.waitForFunction(id=>{let count=0;f.view.viewport?.sceneObjects.get(id)?.traverse(node=>count+=node.geometry?.attributes?.position?.count||0);return count>0;},pillarOperation.id);assert.ok(await page.evaluate(()=>f.nativeViewRequests?.includes('View.GetOrCreate')));report.realNativeView=true;
 assert.deepEqual(cad(await read()),beforeContextCad);report.workpieceContext={currentWorkpieceId:pillarId,emptyWorkpieceId:secondId,sourcePlacementId:secondSource.id,pillarOperationId:pillarOperation.id,secondOperationId:secondOperation.id,modalWorkpieceSelector:false,onlyCurrentOperations:true,crossWorkpieceFallback:false};
 await page.screenshot({path:resolve(output,'工序-当前Pillar工件.png')});
 checkpoint('The top workpiece chooser scopes the left operation list; an empty workpiece keeps its own source CAD, New Operation chooses only a machine and keeps its selected workpiece, and switching pages restores each workpiece’s last operation');

 const supportEntry=page.locator('[data-command-id="five.fixture.generate"]');report.workpieceContext.preparationKind=await page.evaluate(()=>f.view.fivePreparationKind);assert.equal(report.workpieceContext.preparationKind,'setup','Returning from the source page must restore the operation selection kind as well as its native active placement');assert.equal(await supportEntry.isEnabled(),true,'The restored operation must be ready for Add Support');
 await supportEntry.click();await settle();await page.locator('[data-five-key="fixture-design"]').waitFor();
 const body=page.locator('[data-five-key="design-body"]'),advanced=page.locator('[data-five-key="design-options"]'),generate=page.locator('.five-design-footer [data-five-command="five.fixture.design.generate"]'),cancel=page.locator('.five-design-footer [data-five-command="five.fixture.design.cancel"]');
 const baseline=await read(),baselineCad=cad(baseline),baselineCadHash=digest(baselineCad);
 await page.evaluate(()=>{const original=f.proxy.invoke.bind(f.proxy);f.supportGenerateRequests=[];f.proxy.invoke=(method,payload={},options)=>{if(method==='CamFixture.Generate')f.supportGenerateRequests.push({commandId:payload.commandId,setupId:payload.setupId,recipe:structuredClone(payload.recipe)});return original(method,payload,options);};});
 const layout=()=>page.evaluate(()=>{
  const rect=node=>{const r=node.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,bottom:r.bottom,right:r.right};};
  const pane=document.querySelector('.cam-info-pane'),reference=document.querySelector('[data-five-key="operation-reference-value"]'),generator=document.querySelector('[data-five-key="fixture-design"]'),body=document.querySelector('[data-five-key="design-body"]'),footer=document.querySelector('[data-five-key="design-footer"]');
  const actions=[...footer.querySelectorAll('button')].map(button=>{const r=button.getBoundingClientRect(),hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);return {command:button.dataset.fiveCommand,rect:rect(button),hit:!!hit&&button.contains(hit),disabled:button.disabled};});
  return {viewport:{width:innerWidth,height:innerHeight},pane:rect(pane),reference:rect(reference.closest('.five-section')),generator:rect(generator),body:{...rect(body),scrollTop:body.scrollTop,scrollHeight:body.scrollHeight,clientHeight:body.clientHeight},footer:rect(footer),actions};
 });
 const assertLayout=value=>{assert.ok(value.reference.bottom<=value.generator.y+1);assert.ok(value.generator.bottom<=value.pane.bottom+1);assert.ok(value.footer.bottom<=value.pane.bottom+1);assert.ok(value.footer.y>=value.pane.y);assert.ok(value.actions.every(action=>action.hit&&action.rect.y>=value.pane.y&&action.rect.bottom<=value.pane.bottom+1));assert.ok(value.body.clientHeight>0);};
 await page.setViewportSize({width:1366,height:720});
 const legacy=await page.addStyleTag({content:'.five-workspace[data-fixture-design="true"] .cam-info-pane{display:block!important}.five-workspace[data-fixture-design="true"] .cam-info-pane>.five-fixture-generator{height:100%!important;flex:none!important}'});
 const beforeFix=await layout();assert.ok(beforeFix.footer.bottom>beforeFix.pane.bottom+8,'The prior block/height:100% rules must reproduce the clipped footer');assert.ok(beforeFix.actions.some(action=>!action.hit));report.legacyLayout=beforeFix;await page.screenshot({path:resolve(output,'支撑编辑器-旧规则复现.png')});
 await legacy.evaluate(node=>node.remove());assertLayout(await layout());
 checkpoint('The prior block pane plus 100% generator reproduces a clipped footer; removing that test override restores the fixed remaining-height layout beneath the actual workpiece reference');
 report.layouts=[];
 for(const height of [950,720,600,520]){
  await page.setViewportSize({width:1366,height});
  for(const open of [false,true]){
   if(await advanced.evaluate(node=>node.open)!==open)await advanced.locator('summary').click();
   await body.evaluate(node=>node.scrollTop=0);const top=await layout();assertLayout(top);
   if(open){const bounds=await body.boundingBox();await page.mouse.move(bounds.x+bounds.width/2,bounds.y+bounds.height/2);await page.mouse.wheel(0,5000);await page.waitForFunction(()=>document.querySelector('[data-five-key="design-body"]').scrollTop>0);}
   const bottom=await layout();assertLayout(bottom);if(open)assert.ok(bottom.body.scrollTop>0);
   report.layouts.push({height,advanced:open,top,bottom});
   if(await generate.isEnabled())await generate.click({trial:true});await cancel.click({trial:true});
   if(height===520)await page.screenshot({path:resolve(output,`支撑编辑器-520-${open?'展开':'折叠'}.png`)});
   if(height===720&&!open)await page.screenshot({path:resolve(output,'支撑编辑器-720-折叠.png')});
  }
 }
 checkpoint('At heights 950, 720, 600 and 520, collapsed and expanded advanced settings retain a visible, mouse-reachable footer while actual wheel scrolling moves the editor body beneath it');
 await page.setViewportSize({width:1366,height:600});
 await body.evaluate(node=>node.scrollTop=0);await page.locator('[data-five-command="five.fixture.design.pick"]').click();await settle();
 await page.evaluate(()=>{f.view.viewport.setViewDirection([1,-1,-1]);f.view.viewport.fitView();});await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
 const facePixel=await page.evaluate(async()=>{
  const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=f.view,vp=v.viewport,mesh=vp.sceneObjects.get(v.fixtureDesignPlacement),g=mesh.geometry,positions=g.attributes.position,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();mesh.updateWorldMatrix(true,true);vp.camera.updateMatrixWorld(true);
  for(const face of [...v.fixtureDesignFaces].sort((a,b)=>+b.count-+a.count))for(let index=+face.first;index<+face.first+ +face.count;++index){
   const point=new T.Vector3();for(let corner=0;corner<3;++corner)point.add(new T.Vector3().fromBufferAttribute(positions,g.index?g.index.getX(index*3+corner):index*3+corner));point.multiplyScalar(1/3).applyMatrix4(mesh.matrixWorld).project(vp.camera);
   const x=r.x+(point.x+1)*r.width/2,y=r.y+(1-point.y)*r.height/2;if(Math.abs(point.x)>.85||Math.abs(point.y)>.85||Math.abs(point.z)>1||document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
   ray.setFromCamera({x:point.x,y:point.y},vp.camera);const hit=ray.intersectObject(mesh,false)[0];if(hit&&hit.faceIndex>=+face.first&&hit.faceIndex<+face.first+ +face.count)return {x,y,faceId:String(face.faceId)};
  }throw Error('No visible native Pillar face for the manual support-selection click');
 });
 await page.mouse.click(facePixel.x,facePixel.y);await page.waitForFunction(id=>f.view.fixtureDesignDraft.supportFaces.includes(id),facePixel.faceId);report.manualSupportFace=facePixel.faceId;
 const supportCount=page.locator('[data-five-fixture-field="design:supportCount"]');await supportCount.fill('2');await page.locator('[data-five-fixture-field="design:connectionCount"]').fill('2');
 await page.evaluate(()=>{const input=document.querySelector('[data-five-fixture-field="design:supportCount"]');window.supportFooterKept={input,body:document.querySelector('[data-five-key="design-body"]'),footer:document.querySelector('[data-five-key="design-footer"]'),reference:document.querySelector('[data-five-key="operation-reference-value"]'),canvas:f.view.viewport.renderer.domElement,events:0};input.addEventListener('support-footer-probe',()=>supportFooterKept.events++);input.focus({preventScroll:true});input.setSelectionRange(0,1);f.holdRead=true;f.supportFooterRefresh=__icaxAppShell.executeRibbonCommand('five.refresh');});
 await page.waitForFunction(()=>f.held);const latestScroll=await body.evaluate(node=>{node.scrollTop=125;return node.scrollTop;});assert.ok(latestScroll>0);
 await page.evaluate(async()=>{f.release();await f.supportFooterRefresh;});await settle();
 const retained=await page.evaluate(()=>{const s=supportFooterKept;s.input.dispatchEvent(new Event('support-footer-probe'));return {input:s.input===document.querySelector('[data-five-fixture-field="design:supportCount"]'),body:s.body===document.querySelector('[data-five-key="design-body"]'),footer:s.footer===document.querySelector('[data-five-key="design-footer"]'),reference:s.reference===document.querySelector('[data-five-key="operation-reference-value"]'),canvas:s.canvas===f.view.viewport.renderer.domElement,focus:document.activeElement===s.input,selection:[s.input.selectionStart,s.input.selectionEnd],events:s.events,scroll:s.body.scrollTop};});
 assert.deepEqual(retained,{input:true,body:true,footer:true,reference:true,canvas:true,focus:true,selection:[0,1],events:1,scroll:latestScroll});report.interaction=retained;assertLayout(await layout());
 checkpoint('A delayed real scene refresh preserves the support input, focus, selection, event listener, original canvas, reference/footer/body nodes and the latest editor scroll');
 await page.locator('[data-five-fixture-field="design:windowMode"]').selectOption('None');await page.locator('[data-five-fixture-field="design:jointMode"]').selectOption('Weld');
 await body.evaluate(node=>node.scrollTop=node.scrollHeight);assertLayout(await layout());assert.equal(await generate.isEnabled(),true);
 const started=performance.now();await generate.click();await settle(180000);
 report.generation=await page.evaluate(()=>({requests:f.supportGenerateRequests,error:f.view.error||'',partCount:f.view.fiveFixtureGenerated?.design.candidate?.parts?.length||0,supportStatus:f.view.fiveFixtureGenerated?.design.candidate?.supportCheck?.status||null,previewObjects:f.view.fixtureCandidatePreview?.group.children.length||0}));report.generation.elapsedMs=performance.now()-started;report.generation.requestedCounts={support:2,connection:2};assert.equal(report.generation.requests.length,1,'The mouse click must reach exactly one real native generation request');
 assert.deepEqual(await read(),baseline,'Generation must remain temporary and preserve all native CAD, operation and tooling state');assertLayout(await layout());await page.screenshot({path:resolve(output,'支撑编辑器-真实生成后.png')});
 await cancel.click();await settle();assert.equal(await page.locator('[data-five-key="fixture-design"]').count(),0);assert.deepEqual(await read(),baseline);assert.equal(await page.evaluate(()=>f.view.viewport.renderer.domElement===supportFooterKept.canvas),true);
 report.cad={before:baselineCadHash,after:digest(cad(await read())),unchanged:true};assert.equal(report.cad.after,report.cad.before);
 checkpoint('The visible footer starts one real Pillar support generation with two transverse and two longitudinal plates; Cancel discards the temporary result and preserves the complete native project and CAD hash');
}
