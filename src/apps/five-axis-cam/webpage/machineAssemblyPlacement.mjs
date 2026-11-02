import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {button,hint} from './workspaceControls.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {invalidateMachineCollisionFeedback} from './machineCollisionFeedback.mjs';
import {acceptMachineAssemblyPlacement} from './machineSimulation.mjs';

const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
const matrix=values=>new THREE.Matrix4().set(...values);
const row=m=>m.clone().transpose().toArray();
const page=view=>view.fiveAxisArea==='simulation'&&activeTool(view)==='machine-simulation';
const bound=view=>{const c=setupOf(view)?.machineSetup;return c?.machineId&&(!view.fiveMachine?.machineId||view.fiveMachine.machineId===c.machineId);};
export function machineAssemblyParts(view){
 const domain=view.scene?.domain,setup=setupOf(view);if(!setup?.machineSetup?.machineId)return [];
 return [...(domain.placements||[]).filter(p=>p.setupId===setup.id&&domain.workpieces?.find(w=>w.id===p.workpieceId)?.enabled!==false),...(domain.fixtures||[]).filter(f=>f.setupId===setup.id&&f.enabled!==false)];
}
export function machineAssemblySelectedIds(view){return page(view)?bound(view)&&view.fiveMachineAssembly?.selected?machineAssemblyParts(view).map(p=>p.id):[]:undefined;}
export function renderMachineAssemblyPlacement(view,busy){
 if(!bound(view))return '';
 const s=view.fiveMachineAssembly;
 return `<div data-five-key="machine-assembly-tools">${button('five.machine.assembly:translate','整体平移',`${busy} aria-pressed="${!!s?.selected&&s.mode==='translate'}"`)}${button('five.machine.assembly:rotate','整体旋转',`${busy} aria-pressed="${!!s?.selected&&s.mode==='rotate'}"`)}${s?.selected?button('five.machine.assembly:done','完成调整',busy):''}${hint('工件和工装一起移动；拖动只预览，松开保存位置。需要时单独检测碰撞。')}</div>`;
}
function clearPreview(s){for(const id of s.previewIds||[])s.viewport?.clearTransformPreview(id);s.previewIds=[];}
export function disposeMachineAssemblyPlacement(view){
 const s=view.fiveMachineAssembly;if(!s)return;
 s.gesture=null;clearPreview(s);if(s.manipulator&&s.viewport?.transformManipulator===s.manipulator)s.viewport.clearTransformManipulator();s.manipulator=null;
}
export function machineAssemblyCommand(context,view,action,ops){
 if(!action.startsWith('assembly:'))return false;
 if(!bound(view))return true;
 if(view.fiveMachineAssembly?.gesture||view.fiveMachineAssembly?.saving||view.fiveMachine?.busy||view.pending)return true;
 const mode=action.slice(9);disposeMachineAssemblyPlacement(view);
 view.fiveMachineAssembly={selected:mode!=='done',mode:mode==='rotate'?'rotate':'translate'};
 ops.renderProject(context,view);return true;
}
export function handleMachineAssemblyPick(context,view,userData,hit,ops){
 if(!page(view))return false;
 if(!bound(view))return true;
 if(view.fiveMachineAssembly?.gesture||view.fiveMachineAssembly?.saving||view.fiveMachine?.busy||view.pending)return true;
 const ids=new Set(machineAssemblyParts(view).map(p=>p.id));let id=userData?.objectId;
 for(let object=hit?.object;!ids.has(id)&&object;object=object.parent)id=object.userData?.objectId;
 disposeMachineAssemblyPlacement(view);const s=view.fiveMachineAssembly??={mode:'translate'};s.selected=ids.has(id);ops.renderProject(context,view);return true;
}
export function syncMachineAssemblyPlacement(context,view,ops){
 const s=view.fiveMachineAssembly,vp=view.viewport,setup=setupOf(view),configuration=setup?.machineSetup;
 if(!s)return;
 const valid=()=>!view.disposed&&context.isCurrentProject?.()!==false&&page(view)&&setupOf(view)?.id===setup?.id&&JSON.stringify(view.scene?.domain?.expected)===s.gesture?.expectedKey;
 if(!page(view)||!bound(view)||!s.selected||view.fiveMachine?.playing||setup.locked){disposeMachineAssemblyPlacement(view);return;}
 s.viewport=vp;if(!vp?.setTransformManipulator)return;
 if(s.gesture||s.saving)return;
 const support=vp.sceneObjects.get(configuration.supportId);if(!support)return;
 support.updateWorldMatrix(true,false);const parent=support.matrixWorld.clone(),base=matrix(configuration.placement),parts=machineAssemblyParts(view),bounds=new THREE.Box3();
 for(const p of parts){const geometry=vp.sceneObjects.get(p.id)?.geometry;if(!geometry)continue;geometry.computeBoundingBox();bounds.union(geometry.boundingBox.clone().applyMatrix4(parent.clone().multiply(base).multiply(matrix(p.transform))));}
 if(bounds.isEmpty())return;const pivot=bounds.getCenter(new THREE.Vector3()),space=view.fivePlacementSpace||'world',orientation=new THREE.Quaternion().setFromRotationMatrix(parent.clone().multiply(base));
 const render=()=>{if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);};
 const preview=candidate=>{for(const p of parts)vp.setTransformPreview(p.id,parent.clone().multiply(matrix(candidate)).multiply(matrix(p.transform)).elements);s.previewIds=parts.map(p=>p.id);};
 vp.setTransformManipulator({mode:s.mode,space,position:pivot.toArray(),quaternion:orientation.toArray(),planeTranslate:true,screenRotate:true,enabled:!view.pending&&!view.fiveMachine?.busy&&!view.fiveMachine?.dirty,
  onStart:({axis,direction})=>{
   if(s.gesture||s.saving||view.pending||view.fiveMachine?.busy||view.fiveMachine?.dirty||!page(view)||!bound(view)||context.isCurrentProject?.()===false)return false;
   const basis=({X:[1,0,0],Y:[0,1,0],Z:[0,0,1]})[axis],worldAxis=basis?new THREE.Vector3(...basis):new THREE.Vector3(...direction);if(basis&&space==='local')worldAxis.applyQuaternion(orientation);
   s.gesture={expected:structuredClone(view.scene.domain.expected),expectedKey:JSON.stringify(view.scene.domain.expected),direction:worldAxis.toArray(),target:null};context.actions?.refreshProjectHistoryControls?.();
  },
  onChange:({translation,angle})=>{
   const g=s.gesture;if(!g||g.ended||!valid())return;
   let delta;if(s.mode==='translate')delta=new THREE.Matrix4().makeTranslation(...translation);
   else delta=new THREE.Matrix4().makeTranslation(...pivot.toArray()).multiply(new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(...g.direction).normalize(),angle)).multiply(new THREE.Matrix4().makeTranslation(...pivot.clone().negate().toArray()));
   g.target=row(parent.clone().invert().multiply(delta).multiply(parent).multiply(base));
   preview(g.target);
  },
  onEnd:({moved})=>{
   const g=s.gesture;if(!g)return;g.ended=true;s.saving=true;
   s.savingRequest=(async()=>{try{
    if(!moved||s.gesture!==g||!valid()||!g.target)return;
    const request={commandId:crypto.randomUUID(),expected:g.expected,setupId:setup.id,placement:g.target};
    let result;
    try{result=await context.sceneProxy.invoke('CamSimulation.SetMachinePlacement',request);}
    catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed')throw error;result=receipt.result;}
    // File persistence does not hold the next gesture or trigger a Scene read.
    void saveCommittedProjectEdit(context,view,ops,'fiveMachineAssemblySaveError');
    if(!valid())return;
    acceptMachineAssemblyPlacement(view,result.configuration,result.expected);
    invalidateMachineCollisionFeedback(view);
    await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});
   }catch(error){view.fiveMachine.error=error.message;}finally{if(s.gesture===g)s.gesture=null;s.saving=false;s.savingRequest=null;clearPreview(s);render();context.actions?.refreshProjectHistoryControls?.();}})();
  },
  onCancel:()=>{s.gesture=null;clearPreview(s);render();},
 });s.manipulator=vp.transformManipulator;
}
