import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyHeadScopes({page,rpc,invoke,command,idle,ready,empty,browse,heads,output,report,checkpoint,targetSetupId,targetPlacementId,sibling,otherWorkpiece}){
 const query=setupId=>invoke('CamHead.Assignments',{setupId});
 const read=()=>invoke('CamScene.ReadState');
 const edit=async(method,payload)=>invoke(method,{commandId:crypto.randomUUID(),expected:(await read()).expected,...payload});
 const setting=page.locator('[data-five-head-scope]'),use=page.locator('[data-five-command="five.head-workspace.use"]'),inherit=page.locator('[data-five-command="five.head-workspace.inherit"]');
 const current=page.locator('[data-five-key="head-current-name"]'),source=page.locator('[data-five-key="head-current-source"]');
 const refresh=async()=>{await command('five.refresh');await idle();await page.waitForFunction(()=>!!f.view.fiveHeadAssignments?.data&&!f.view.fiveHeadAssignments.loading);};
 const reopenWorkspace=async()=>{await refresh();await command('five.cam-page.head');await idle();await page.waitForFunction(()=>f.view.fiveSimulation?.builtinHeads?.length>0&&!f.view.fiveSimulation.builtinHeadsLoading);};
 const selectScope=async scope=>{await setting.selectOption(scope);await idle();assert.equal(await setting.inputValue(),scope);};
 const set=async(scope,head)=>{await selectScope(scope);await browse(head);assert.equal(await use.isEnabled(),true);await use.click();await idle();};
 const restore=async scope=>{await selectScope(scope);assert.equal(await inherit.isEnabled(),true);await inherit.click();await idle();};
 const unassign=async scope=>{await selectScope(scope);await page.locator('[data-head-choice-value=""]').click();await idle();await empty();await use.click();await idle();};
 const effective=async(setupId,origin,head)=>{
  const assignments=await query(setupId);assert.equal(assignments.effective.source,origin);
  if(head){assert.ok(assignments.effective.instanceId);const state=await read();assert.equal(state.setups.find(s=>s.id===setupId).headModel.builtinDefinitionId,head.id);}else assert.equal(assignments.effective.instanceId,'');
  return assignments;
 };
 const geometry=state=>({cad:state.workpieces.map(w=>({id:w.id,sourceRef:w.sourceRef,geometryRef:w.geometryRef,previewRef:w.previewRef})),placements:state.placements.map(p=>({id:p.id,setupId:p.setupId,workpieceId:p.workpieceId,transform:p.transform})),cuts:state.intentToolpaths,paths:state.setups.map(s=>({id:s.id,operations:s.generatedGeometry?.operations,transitions:s.generatedGeometry?.transitions,sampleGeometryRef:s.generatedGeometry?.sampleGeometryRef}))});
 const [projectHead,workpieceHead,poseHead]=heads,nextHead=workpieceHead;assert.ok(poseHead,'Scope acceptance needs three distinct compatible catalog heads');
 const initialIDs=[targetSetupId,sibling.setupId,otherWorkpiece.setupId];
 for(const setupId of initialIDs)await edit('CamHead.Assign',{setupId,scope:'Setup',action:'inherit'});
 await refresh();const baseline=await read(),baselineGeometry=geometry(baseline),initialProductSnapshot=(await query(targetSetupId)).productSnapshot;
 const unassignedArchive=resolve(output,'旧工程-继承未指定.i5cam');await rpc({action:'export-project',path:unassignedArchive});
 assert.deepEqual(await setting.locator('option').evaluateAll(options=>options.map(o=>[o.value,o.textContent])),[['Setup','本工序'],['Workpiece','本工件'],['Project','本工程'],['Product','本产品']]);
 await set('Project',projectHead);for(const id of initialIDs)await effective(id,'Project',projectHead);
 await set('Workpiece',workpieceHead);await effective(targetSetupId,'Workpiece',workpieceHead);await effective(sibling.setupId,'Workpiece',workpieceHead);await effective(otherWorkpiece.setupId,'Project',projectHead);
 await set('Setup',poseHead);await effective(targetSetupId,'Setup',poseHead);
 await set('Project',nextHead);await effective(targetSetupId,'Setup',poseHead);await effective(sibling.setupId,'Workpiece',workpieceHead);await effective(otherWorkpiece.setupId,'Project',nextHead);
 assert.ok((await current.innerText()).includes(poseHead.name));assert.match(await source.innerText(),/本工序单独指定/);
 assert.deepEqual(geometry(await read()),baselineGeometry,'Default assignments must preserve authored CAD, cutting paths and rapid geometry');
 checkpoint('Four visible assignment scopes use real native defaults; project/workpiece changes affect only inheritors and retain explicit pose assignments and all CAD/cutting/rapid geometry');

 const machineType=(await read()).setups.find(s=>s.id===targetSetupId).machineType.machineType;
 const created=await edit('CamSetup.Manage',{placementId:targetPlacementId,action:'create',name:'继承工件默认的新姿态',machineType});
 assert.equal((await query(created.setupId)).scopes.Setup.mode,'Inherit');await effective(created.setupId,'Workpiece',workpieceHead);
 await edit('CamWorkpiece.Activate',{placementId:targetPlacementId});await refresh();
 await unassign('Setup');await effective(targetSetupId,'Setup',null);assert.match(await current.innerText(),/未指定/);await restore('Setup');await effective(targetSetupId,'Workpiece',workpieceHead);await ready(workpieceHead.id);
 await unassign('Workpiece');for(const id of [targetSetupId,sibling.setupId,created.setupId])await effective(id,'Workpiece',null);await effective(otherWorkpiece.setupId,'Project',nextHead);
 await set('Project',projectHead);for(const id of [targetSetupId,sibling.setupId,created.setupId])await effective(id,'Workpiece',null);
 await restore('Workpiece');for(const id of [targetSetupId,sibling.setupId,created.setupId])await effective(id,'Project',projectHead);
 checkpoint('Explicit None masks higher defaults until Restore inheritance; a newly created pose inherits its workpiece default instead of copying the source pose override');

 await set('Setup',poseHead);await set('Workpiece',workpieceHead);const changed=await query(sibling.setupId);
 assert.equal((await rpc({action:'undo'})).changed,true);await refresh();await effective(sibling.setupId,'Project',projectHead);await effective(targetSetupId,'Setup',poseHead);
 assert.equal((await rpc({action:'redo'})).changed,true);await refresh();assert.deepEqual(await query(sibling.setupId),changed);
 const savedAssignments=await Promise.all(initialIDs.map(query)),archive=resolve(output,'四级刀头默认与覆盖.i5cam');await rpc({action:'export-project',path:archive});
 await rpc({action:'save-reopen'});await refresh();for(let i=0;i<initialIDs.length;i++)assert.deepEqual(await query(initialIDs[i]),savedAssignments[i]);
 report.project=archive;await page.screenshot({path:resolve(output,'四级刀头指定.png')});
 checkpoint('Real database undo/redo and native save/reopen preserve default scopes, effective sources and explicit overrides');

 const beforeProduct=await read(),saves=await page.evaluate(()=>f.projectSaves||0);
 await set('Product',nextHead);assert.deepEqual(await read(),beforeProduct,'The product default must not mutate the current project');assert.equal(await page.evaluate(()=>f.projectSaves||0),saves,'Product preferences must not autosave the current project');
 assert.deepEqual((await query(targetSetupId)).productSnapshot,initialProductSnapshot);assert.match(await page.locator('[data-five-key="head-product-scope-note"]').innerText(),/以后新建工程/);
 await rpc({action:'new-project'});let fresh=await rpc({action:'prepare-axis-test'});let freshSetup=fresh.activeSetupId;
 await page.evaluate(path=>{f.project.state.projectPath=path;},resolve(output,'以后新工程.i5cam'));await reopenWorkspace();await ready(nextHead.id);await effective(freshSetup,'Product',nextHead);assert.match(await source.innerText(),/本工程创建时/);
 const frozenProduct=(await query(freshSetup)).productSnapshot,frozenArchive=resolve(output,'新工程-锁定产品默认.i5cam');await rpc({action:'export-project',path:frozenArchive});
 await set('Product',projectHead);await effective(freshSetup,'Product',nextHead);assert.deepEqual((await query(freshSetup)).productSnapshot,frozenProduct);
 await rpc({action:'new-project'});fresh=await rpc({action:'prepare-axis-test'});freshSetup=fresh.activeSetupId;await page.evaluate(path=>{f.project.state.projectPath=path;},resolve(output,'再次新工程.i5cam'));await reopenWorkspace();await ready(projectHead.id);await effective(freshSetup,'Product',projectHead);
 await rpc({action:'open-project',path:frozenArchive});await page.evaluate(path=>{f.project.state.projectPath=path;},frozenArchive);await reopenWorkspace();const frozen=await read();await effective(frozen.activeSetupId,'Product',nextHead);assert.deepEqual((await query(frozen.activeSetupId)).productSnapshot,frozenProduct);
 await rpc({action:'open-project',path:unassignedArchive});await page.evaluate(path=>{f.project.state.projectPath=path;},unassignedArchive);await reopenWorkspace();const reopened=await query(targetSetupId);assert.equal(reopened.effective.instanceId,'');assert.deepEqual(reopened.productSnapshot,initialProductSnapshot);
 await rpc({action:'open-project',path:archive});await page.evaluate(path=>{f.project.state.projectPath=path;},archive);await reopenWorkspace();await ready(poseHead.id);await effective(targetSetupId,'Setup',poseHead);await page.screenshot({path:resolve(output,'产品默认不影响旧工程.png')});
 report.productDefaults={existingProjectsUnchanged:true,futureProjectsCaptureAtCreation:true,oldProjectsKeepFrozenSnapshot:true,currentProjectAutosaveSuppressed:true,nativeNewProjectFixture:true,applicationNewProjectContainer:false};
 checkpoint('Product defaults affect future native projects only; current projects are not saved or changed, newly created projects capture the then-current default, and reopening old projects never adopts a newer global default');
}
