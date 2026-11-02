import {mergeDomainState,readMachiningState} from './domainActions.mjs';

// Geometry edits update workpiece resources without blocking the workspace
// or rereading the machine lists. Keep the current sidebar and canvas.
export async function refreshWorkpieceGeometry(context,view,ops){
  const scene=view.scene,proxy=context.sceneProxy;
  const current=()=>!view.disposed&&context.isCurrentProject?.()!==false&&view.scene===scene&&context.sceneProxy===proxy;
  const state=await readMachiningState(context,undefined,{waitForGeneration:false});
  if(!current())return false;
  mergeDomainState(scene,state);
  await ops.refreshActiveAreaView(context,view);
  return current();
}
