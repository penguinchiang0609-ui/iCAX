import assert from 'node:assert/strict';
import {basename,dirname,resolve} from 'node:path';

const types=['HeadHead','HeadTable','TableTable'];
const multiply=(a,b)=>Array.from({length:16},(_,index)=>{const row=Math.floor(index/4),column=index%4;return [0,1,2,3].reduce((sum,k)=>sum+a[row*4+k]*b[k*4+column],0);});
const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const near=(actual,expected,label)=>{assert.equal(actual.length,expected.length,label);actual.forEach((value,index)=>assert.ok(Math.abs(value-expected[index])<1e-6,`${label}[${index}]: ${value} != ${expected[index]}`));};

export async function verifyOperationMachineCoupling({page,rpc,invoke,command,idle,output,report,checkpoint}){
 const read=()=>invoke('CamScene.ReadState');
 const edit=async(method,payload)=>invoke(method,{commandId:crypto.randomUUID(),expected:(await read()).expected,...payload});
 const cad=state=>state.workpieces.map(workpiece=>({id:workpiece.id,geometryRef:workpiece.geometryRef,sourceRef:workpiece.sourceRef,previewRef:workpiece.previewRef,coordinateFrame:workpiece.coordinateFrame}));
 const setup=(state,id)=>state.setups.find(value=>value.id===id);
 const placement=(state,id)=>state.placements.find(value=>value.id===id);
 const initial=await read(),sourceCad=cad(initial),workpieceId=initial.workpieces.find(workpiece=>workpiece.enabled!==false).id;
 const evidence={operations:{},heads:{},placement:[],realFixturePicker:true};
 const activate=async operation=>{await page.locator('[data-tab-id="operation"]').click();await idle();await command('five.setup.activate:'+operation.placementId);await idle();assert.equal((await read()).activeSetupId,operation.setupId);};
 const create=async machineType=>{
  await page.locator('[data-tab-id="operation"]').click();await idle();
  const chooser=page.locator('[data-five-cam-context-field="five-workpiece"]'),chosen=page.locator('[data-five-cam-context-choice="five-workpiece"][aria-pressed="true"]');
  if(await chosen.getAttribute('data-value')!==workpieceId){await chooser.click();await page.locator(`[data-five-cam-context-choice="five-workpiece"][data-value="${workpieceId}"]`).click();await idle();}
  const beforeState=await read(),before=new Set(beforeState.setups.map(value=>value.id));
  await page.locator('[data-five-command="five.operation.create"]').first().click();
  const modal=page.getByRole('dialog',{name:'新增工序',exact:true});await modal.waitFor();
  assert.equal(await modal.locator('[name="workpieceId"]').count(),0);assert.equal(await modal.locator('[data-operation-create-workpiece]').innerText(),beforeState.workpieces.find(w=>w.id===workpieceId).name);assert.equal(await modal.locator('[name="machineType"]').inputValue(),'');assert.equal(await modal.locator('[data-operation-create-apply]').isDisabled(),true);
  await modal.locator('[name="machineType"]').selectOption(machineType);await modal.locator('[data-operation-create-apply]').click();await idle();
  const state=await read(),created=state.setups.filter(value=>!before.has(value.id));assert.equal(created.length,1);const value=created[0],pose=state.placements.find(p=>p.setupId===value.id);
  assert.equal(value.machineType.machineType,machineType);assert.equal(pose.workpieceId,workpieceId);return {setupId:value.id,placementId:pose.id,workpieceId};
 };
 const headReady=()=>page.waitForFunction(()=>!f.view.pending&&!f.view.fiveHeadSelection?.loading&&!f.view.fiveHeadAssignments?.loading&&!f.view.fiveHeadAssignments?.busy&&f.view.fiveHeadPreview?.phase==='ready');
 const heads=(await invoke('CamHead.ListBuiltins')).items;
 for(const type of types){
  const operation=await create(type);evidence.operations[type]=operation;
  const available=heads.filter(head=>head.machineTypes?.includes(type));assert.ok(available.length,`Missing declared ${type} head fixture`);const chosen=available[0];
  await page.locator('[data-tab-id="toolpath"]').click();await idle();await command('five.cam-page.head');await idle();
  await page.locator(`[data-head-choice-value="builtin:${chosen.id}"]`).waitFor();
  for(const other of heads.filter(head=>!head.machineTypes?.includes(type))){const row=page.locator(`[data-head-choice-value="builtin:${other.id}"]`);if(await row.count())assert.equal(await row.isDisabled(),true,'The current operation cannot choose an incompatible catalog head');}
  await page.locator(`[data-head-choice-value="builtin:${chosen.id}"]`).click();await idle();await headReady();
  assert.deepEqual(await page.evaluate(()=>f.view.fiveHeadSelection.head.machineTypes),[type]);
  await page.locator('[data-five-command="five.head-workspace.use"]').click();await idle();await headReady();
  const applied=setup(await read(),operation.setupId).headModel;assert.deepEqual(applied.machineTypes,[type]);assert.equal(applied.builtinDefinitionId,chosen.id);assert.ok(applied.instance?.id);
  evidence.heads[type]={definitionId:chosen.id,instanceId:applied.instance.id,machineTypes:applied.machineTypes};
  const wrong=heads.find(head=>!head.machineTypes?.includes(type));assert.ok(wrong);
  const before=await read();await assert.rejects(edit('CamHead.Configure',{setupId:operation.setupId,kind:'Builtin',builtinDefinitionId:wrong.id}),/CAM_HEAD_MACHINE_TYPE_MISMATCH/);assert.deepEqual(await read(),before);
 }
 checkpoint('The real cutting-head catalog filters by each operation type; declared two-, one- and zero-rotary heads can be applied only to their matching operation, and the native command independently rejects a forged wrong choice');

 const assignments=setupId=>invoke('CamHead.Assignments',{setupId});
 for(const operation of Object.values(evidence.operations))await edit('CamHead.Assign',{setupId:operation.setupId,scope:'Setup',action:'inherit'});
 await edit('CamHead.Assign',{setupId:evidence.operations.HeadHead.setupId,scope:'Project',action:'set',kind:'Instance',instanceId:evidence.heads.HeadHead.instanceId});
 await edit('CamHead.Assign',{setupId:evidence.operations.HeadTable.setupId,scope:'Workpiece',action:'set',kind:'Instance',instanceId:evidence.heads.HeadTable.instanceId});
 assert.equal((await assignments(evidence.operations.HeadHead.setupId)).effective.instanceId,evidence.heads.HeadHead.instanceId);
 assert.equal((await assignments(evidence.operations.HeadTable.setupId)).effective.instanceId,evidence.heads.HeadTable.instanceId);
 assert.equal((await assignments(evidence.operations.TableTable.setupId)).effective.instanceId,'');
 await edit('CamHead.Assign',{setupId:evidence.operations.TableTable.setupId,scope:'Setup',action:'set',kind:'Instance',instanceId:evidence.heads.TableTable.instanceId});
 await command('five.refresh');await idle();
 checkpoint('Head inheritance skips incompatible defaults and continues to the next scope, while a compatible operation override remains independently assigned');

 const info=await rpc({action:'info'}),headInstanceState=(await read()).headInstances;
 for(const type of types){
  const operation=evidence.operations[type];await activate(operation);
  await page.evaluate(()=>{f.couplingFixtureImport=__icaxAppShell.executeRibbonCommand('five.fixture.import');});
  const picker=page.getByRole('dialog',{name:'导入模型',exact:true});await picker.waitFor();await picker.press('Control+l');await picker.locator('[data-nesting-file-directory]').fill(dirname(info.fixturePath));await picker.locator('[data-nesting-file-directory]').press('Enter');
  await picker.locator('[data-nesting-file-is-directory="false"]').filter({hasText:basename(info.fixturePath)}).click();await picker.locator('footer [data-nesting-file-action="open"]').click();await page.evaluate(()=>f.couplingFixtureImport);await idle();
  const before=await read(),parts=before.fixtures.filter(part=>part.setupId===operation.setupId);assert.ok(parts.length,'The production picker must actually import BRep tooling');
  await command('five.setup.activate:'+operation.placementId);await idle();
  await command('five.tool.translate');await idle();
  const scope=type==='HeadHead'?'Workpiece':'Assembly';await page.locator(`.five-placement-tools[data-transform-owner="placement"][aria-label="${scope==='Assembly'?'装夹总成变换':'工件变换'}"]`).waitFor();
  assert.equal(await page.evaluate(()=>f.view.fivePlacement.motionScope),scope);
  const x=page.locator('[data-five-transform-field="position-X"]');await x.waitFor();await x.fill(String(Number(await x.inputValue())+17));await x.press('Enter');await idle();
  await page.waitForFunction(()=>!f.view.fivePlacement?.saving&&!f.view.fivePlacement?.request);
  assert.equal(await page.evaluate(()=>f.view.error||''),'');
  const translated=await read(),localBefore=placement(before,operation.placementId).transform,localAfter=placement(translated,operation.placementId).transform;
  const assemblyBefore=setup(before,operation.setupId).assemblyTransform||identity,assemblyAfter=setup(translated,operation.setupId).assemblyTransform||identity;
  const effectiveBefore=multiply(assemblyBefore,localBefore),expected=[...effectiveBefore];expected[3]+=17;near(multiply(assemblyAfter,localAfter),expected,type+' effective translation');
  const nativeGeneration=setup(translated,operation.setupId).generatedGeometry;
  const uiGeneration=await page.evaluate(id=>f.view.scene.domain.setups.find(value=>value.id===id)?.generatedGeometry,operation.setupId);
  assert.equal(uiGeneration?.status,nativeGeneration?.status,'The committed UI geometry status must agree with the native operation');
  assert.equal(uiGeneration?.validity,nativeGeneration?.validity);
  assert.deepEqual(translated.fixtures.filter(part=>part.setupId===operation.setupId),parts,'Moving the operation retains every tooling local transform and BRep reference');
  if(scope==='Assembly')assert.deepEqual(localAfter,localBefore,'A rotary workpiece keeps its geometry placement inside the assembly');else assert.deepEqual(assemblyAfter,assemblyBefore,'A fixed workpiece changes only its own placement');
  await command('five.tool.rotate');await idle();const rotation=page.locator('[data-five-transform-field="rotation-Z"]');await rotation.fill(String(Number(await rotation.inputValue())+20));await rotation.press('Enter');await idle();await page.waitForFunction(()=>!f.view.fivePlacement?.saving&&!f.view.fivePlacement?.request);
  const rotated=await read();assert.deepEqual(rotated.fixtures.filter(part=>part.setupId===operation.setupId),parts);assert.deepEqual(cad(rotated),sourceCad);assert.deepEqual(rotated.headInstances,headInstanceState,'Assembly placement must preserve every cutting-head resource and calibration');
  assert.equal(await page.evaluate(()=>f.view.error||''),'');
  const rotatedEffective=multiply(setup(rotated,operation.setupId).assemblyTransform||identity,placement(rotated,operation.placementId).transform);
  assert.ok([0,1,2,4,5,6,8,9,10].some(index=>Math.abs(rotatedEffective[index]-expected[index])>1e-6),'The UI rotation must actually change the operation orientation');
  near([rotatedEffective[3],rotatedEffective[7],rotatedEffective[11]],[expected[3],expected[7],expected[11]],type+' position retained through rotation');
  if(scope==='Assembly')assert.deepEqual(placement(rotated,operation.placementId).transform,localBefore);else assert.deepEqual(setup(rotated,operation.setupId).assemblyTransform||identity,assemblyBefore);
  const assembly=setup(rotated,operation.setupId).assemblyTransform||identity,visible=[placement(rotated,operation.placementId),...parts].map(part=>({id:part.id,matrix:multiply(assembly,part.transform)}));
  await page.waitForFunction(expected=>expected.every(part=>{const matrix=f.view.viewport?.sceneObjects.get(part.id)?.matrix?.elements;return matrix&&part.matrix.every((value,index)=>Math.abs(value-matrix[(index%4)*4+Math.floor(index/4)])<1e-6);}),visible);
  evidence.placement.push({machineType:type,motionScope:scope,localTransform:placement(rotated,operation.placementId).transform,assemblyTransform:setup(rotated,operation.setupId).assemblyTransform,viewportEffectiveMatrices:visible,fixtureGeometryRefs:parts.map(part=>({id:part.id,geometryRef:part.geometryRef,transform:part.transform}))});
  await page.screenshot({path:resolve(output,`工序-${type}-实际工装与放置.png`)});
 }
 checkpoint('The production model picker imports real tooling: HeadHead moves the workpiece alone, while HeadTable and TableTable translate and rotate the installation assembly with unchanged local workpiece/tooling BRep and transforms');

 const fixed=await read();const current=evidence.operations.TableTable;
 await assert.rejects(edit('CamOperation.AssignMachineType',{setupId:current.setupId,scope:'Setup',action:'set',machineType:'HeadHead'}),/CAM_OPERATION_MACHINE_TYPE_FIXED/);assert.deepEqual(await read(),fixed);
 const snapshot=state=>({workpieces:state.workpieces,placements:state.placements,fixtures:state.fixtures,fixtureUnits:state.fixtureUnits,setups:state.setups.map(value=>({id:value.id,machineType:value.machineType,assemblyTransform:value.assemblyTransform,headModel:value.headModel}))});
 const archive=resolve(output,'三种机型-切割头与真实工装总成.i5cam');await rpc({action:'export-project',path:archive});await page.evaluate(()=>f.resetNativeViews());await rpc({action:'save-reopen'});await command('five.refresh');await idle();assert.deepEqual(snapshot(await read()),snapshot(fixed));assert.deepEqual(cad(await read()),sourceCad);
 evidence.project=archive;report.machineCoupling=evidence;
 checkpoint('Native save and reopen preserve all three fixed operation types, compatible head bindings, real imported tooling and assembly transforms without altering shared source CAD');
}
