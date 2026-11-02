import {readMachiningState,mergeDomainState} from './domainActions.mjs';

// Planning changes machining metadata, not the machine catalog or viewport
// resources. Keep its readback under the planning owner and retain the Scene.
export async function refreshMachinePlanState(context,view,ops){
 const scene=view.scene,proxy=context.sceneProxy,setupId=scene?.domain?.activeSetupId;
 const expected=JSON.stringify(scene?.domain?.expected),refreshRequest=view.sceneRefreshRequest;
 const active=()=>!view.disposed&&context.isCurrentProject?.()!==false&&context.sceneProxy===proxy&&view.scene===scene&&scene.domain?.activeSetupId===setupId&&JSON.stringify(scene.domain?.expected)===expected&&view.sceneRefreshRequest===refreshRequest;
 if(!active())return false;
 const state=await readMachiningState(context,undefined,{waitForGeneration:false});
 if(!active())return false;
 if(!state?.expected||!Array.isArray(state.workpieces)||!Array.isArray(state.placements)||state.activeSetupId!==setupId||state.expected.epoch!==scene.domain.expected.epoch)
  throw Error('工程或装夹已变化，未应用旧规划状态，请重新规划。');
 mergeDomainState(scene,state);ops.renderProject(context,view);return true;
}
