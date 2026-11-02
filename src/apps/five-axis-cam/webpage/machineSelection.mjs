import {selectMachinePoseTarget} from './machineIK.mjs';
import {machineAssemblySelectedIds} from './machineAssemblyPlacement.mjs';
import {activeTool} from './workspaceModel.mjs';
import {selectMachineColliderPart,stopMachineColliderEdit} from './machineColliders.mjs';
import {revealMachineTreePart} from './machineTree.mjs';

const instancePage=view=>view.fiveAxisArea==='simulation'&&activeTool(view)==='machine-instances';
const selectedMachine=view=>view.scene?.machines?.find(m=>m.entityId===view.fiveMachine?.machineId);
const isPart=(machine,id)=>machine?.elements?.some(e=>e.entityId===id&&e.kind!=='machine');

export function machineSelectedObjectIds(view){
  const assembly=machineAssemblySelectedIds(view);if(assembly)return assembly;
  if(!instancePage(view))return undefined;
  if(view.fiveMachineColliders?.mode)return [];
  const id=view.fiveMachineSelectedPartId;
  return isPart(selectedMachine(view),id)?[id]:[];
}
export function syncMachinePartSelection(view){
  const ids=machineSelectedObjectIds(view);if(!ids)return;
  if(!isPart(selectedMachine(view),view.fiveMachineSelectedPartId))view.fiveMachineSelectedPartId='';
  view.selectedSceneObjectId=view.fiveMachineSelectedPartId||'';
  view.viewport?.setSelectedObjectIds?.(ids,ids[0]||'');
}
export function selectMachinePart(context,view,id,ops,{reveal=false}={}){
  const machine=selectedMachine(view);if(id&&!isPart(machine,id))return false;
  view.fiveMachineSelectedPartId=id||'';view.selectedSceneObjectId=id||'';
  stopMachineColliderEdit(view);
  selectMachineColliderPart(view,id||'');
  if(!view.fiveMachineColliders?.mode)selectMachinePoseTarget(view);
  // Local selection does not change the machine, job, collision report or resources.
  ops.renderProject(context,view);
  syncMachinePartSelection(view);
  if(reveal&&id)revealMachineTreePart(context,view,machine,id);
  return true;
}
export function handleMachinePartPick(context,view,userData,hit,ops){
  if(!instancePage(view))return false;
  if(view.pending||view.fiveMachine?.busy||view.fiveMachineIK?.inflight||view.fiveMachineIK?.gesture||view.fiveMachineIK?.dragging||view.fiveMachineColliders?.mode)return true;
  let id=userData?.objectId;
  // Mesh leaves may carry their owner on an ancestor (multi-material assets).
  for(let object=hit?.object;!isPart(selectedMachine(view),id)&&object;object=object.parent)id=object.userData?.objectId;
  selectMachinePart(context,view,isPart(selectedMachine(view),id)?id:'',ops,{reveal:true});
  return true;
}
