import {activeTool} from './workspaceModel.mjs';

export const activeMachineSetup=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
// A binding owns CAM playback even when its machine or current plan is missing.
// Such a job must stay blocked rather than animate a stationary workpiece.
export const camMachinePlayback=view=>view.fiveAxisArea==='toolpath'&&activeTool(view)==='simulation'&&!!(activeMachineSetup(view)?.machineSetup?.machineId||activeMachineSetup(view)?.headModel?.motion);
export const machinePlaybackActive=view=>camMachinePlayback(view)||view.fiveAxisArea==='simulation'&&activeTool(view)==='machine-simulation';
export const machineViewportActive=view=>view.fiveAxisArea==='simulation'||camMachinePlayback(view);
export const machineViewportFrame=view=>camMachinePlayback(view)?'cam-head':view.fiveAxisArea==='simulation'?'machine':'setup';

// Display the terminal tool mechanism. Its upstream linear slides and the
// separate work-support branch remain solver inputs, not CAM display objects.
export function camHeadElements(view){
 const setup=activeMachineSetup(view),binding=setup?.machineSetup;
 const machine=view.scene?.machines?.find(m=>(m.entityId||m.id)===binding?.machineId);if(!machine||!binding?.toolId)return [];
 const elements=new Map((machine.elements||[]).map(e=>[e.entityId,e])),joints=new Map((machine.joints||[]).map(j=>[j.entityId,j]));
 const assembly=setup.headModel?.assembly,declaredDepth=assembly?.source==='MachineInstance'?assembly.tool+1:Infinity;
 const result=[],visited=new Set();let element=elements.get(binding.toolId);
 while(element&&element.kind!=='machine'&&!visited.has(element.entityId)&&result.length<declaredDepth){
  visited.add(element.entityId);
  const linear=joints.get(element.entityId)?.type==='prismatic';if(linear&&result.length)break;
  result.push(element);if(linear)break;element=elements.get(element.parentEntityId);
 }
 return result.reverse();
}

export function machineMounts(view,machineId){
 const d=view.scene?.domain;if(!d)return [];
 const setups=new Map((d.setups||[]).filter(s=>s.machineSetup?.machineId&&s.machineSetup.machineId===machineId).map(s=>[s.id,s]));
 return [...(d.placements||[]).map(p=>({...p,kind:'工件',name:d.workpieces?.find(w=>w.id===p.workpieceId)?.name||'工件'})),
   ...(d.fixtures||[]).filter(f=>f.enabled!==false).map(f=>({...f,kind:'夹具',name:f.name||'夹具'}))]
  .filter(p=>setups.has(p.setupId)).map(p=>({...p,supportId:setups.get(p.setupId).machineSetup.supportId,setupName:setups.get(p.setupId).name}));
}
