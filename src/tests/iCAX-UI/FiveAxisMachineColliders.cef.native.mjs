import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const b=await chromium.connectOverCDP('http://127.0.0.1:9482'),p=b.contexts()[0].pages()[0];await p.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
 const path='D:/penguinchinage0609/iCAX/output/tests/machine-simulation/empty-instance.i5cam';await p.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await p.waitForFunction(async path=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.mi=(m,p)=>mv.viewportSceneProxy.invoke(m,p,{timeoutMs:120000});return __icaxAppShell.getState().activeProjectPath===path&&mv?.scene?.machines?.length&&!mv.pending;},path);
 await p.locator('[data-tab-id="simulation"]').click();await p.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 const table=await p.evaluate(()=>mv.scene.machines[0].elements.find(e=>e.name==='工作台').entityId);
 await p.locator(`[data-five-command="five.machine.part:${table}"]`).click();await p.locator('[data-five-machine-field="box:size:2"]').waitFor();
 const geometry=await p.evaluate(()=>{window.boxObjects=new Map(mv.viewport.sceneObjects);window.boxGeometry=new Map(mv.viewport.geometryObjects);return mv.viewport.sceneObjects.size;});
 const set=async(key,value)=>{await p.locator(`[data-five-machine-field="box:${key}"]`).fill(String(value));};
 const save=async()=>{await p.locator('[data-five-command="five.machine.box-save"]').click();await p.waitForFunction(()=>!mv.fiveMachine.busy);assert.equal(await p.evaluate(()=>mv.fiveMachine.error),'');};
 await set('size:2',3000);await save();
 const blocked=await p.evaluate(async()=>{const m=mv.scene.machines[0],x=m.joints.find(j=>j.jointName==='X');const r=await mi('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[x.entityId]:x.position+1}});return {status:r.collisionStatus,position:(await mi('Machine.GetElement',{entityId:x.entityId})).machineElement.joint.position,initial:x.position};});
 assert.equal(blocked.status,'Blocked');assert.equal(blocked.position,blocked.initial);
 await set('size:2',181);await set('position:0',2);await set('rotation:2',3);await save();
 const stored=await p.evaluate(id=>mi('Machine.GetElement',{entityId:id}),table);assert.equal(stored.machineElement.collisions[0].geometry.size[2],.181);assert.equal(stored.machineElement.collisions[0].pose[0],.002);assert.ok(Math.abs(stored.machineElement.collisions[0].pose[5]-Math.PI/60)<1e-9);
 const more=await p.evaluate(async id=>{const e=(await mi('Machine.GetElement',{entityId:id})).machineElement;let invalid=false;try{await mi('Machine.SetBoxColliders',{entityId:id,boxes:[{name:'invalid',position:[0,0,0],rotationRadians:[0,0,0],size:[1,-1,1]}]});}catch{invalid=true;}const after=(await mi('Machine.GetElement',{entityId:id})).machineElement;return{invalid,unchanged:JSON.stringify(e.collisions)===JSON.stringify(after.collisions),sameObjects:[...boxObjects].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o),sameGeometry:[...boxGeometry].every(([id,o])=>mv.viewport.geometryObjects.get(id)===o),preview:mv.fiveMachineColliders.overlay.children.length};},table);
 for(const k of ['invalid','unchanged','sameObjects','sameGeometry'])assert.equal(more[k],true,k);assert.ok(more.preview>1);
 await p.evaluate(path=>mi('Project.Save',{projectPath:path}),path);
 writeFileSync('output/tests/machine-simulation/box-editor-acceptance.json',JSON.stringify({table,geometry,blocked,stored:stored.machineElement.collisions[0],...more},null,2));console.log('PASS: editable box position, rotation, dimensions; immediate collision invalidation; rejected move held; invalid sizes rejected; geometry preserved; saved');process.exit(0);
}catch(e){console.error(e);process.exit(1);}
