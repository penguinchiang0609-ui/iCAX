// Only a native history availability cache; all steps live in framework Database.
const scope='machineDefinitions';
const state={canUndo:false,canRedo:false,loaded:false,busy:false,request:null,revision:0,signature:''};
function acceptHistory(native){
  const signature=JSON.stringify([native.undoSteps,native.redoSteps]);
  if(signature!==state.signature){state.signature=signature;state.revision++;}
  state.canUndo=!!native.canUndo;state.canRedo=!!native.canRedo;
}
export function definitionHistoryState(){return {scope:'product',canUndo:state.canUndo,canRedo:state.canRedo,mutationPending:state.busy,blocked:state.busy||!!state.request};}
export async function refreshDefinitionHistory(context){
  if(state.request)return state.request;
  state.request=(async()=>{
    try{acceptHistory(await context.productProxy.invoke('Product.GetSettingsHistory',{scope}));state.loaded=true;}
    catch(error){state.canUndo=state.canRedo=false;state.loaded=false;throw error;}
    finally{state.request=null;context.actions?.refreshProjectHistoryControls?.();}
  })();
  return state.request;
}
export function ensureDefinitionHistory(context,view,ops){
  if(!view.scene||view.disposed||state.request||state.busy||view.fiveMachine?.busy||view.fiveDefinitionRefresh)return;
  if(state.loaded&&view.fiveDefinitionRevision===state.revision)return;
  view.fiveDefinitionRefresh=true;
  void (async()=>{
    try{
      if(!state.loaded)await refreshDefinitionHistory(context);
      const revision=state.revision,result=await context.productProxy.invoke('MachineDefinition.List',{});
      if(!view.scene||view.disposed||context.isCurrentProject?.()===false||revision!==state.revision)return;
      view.scene.machineDefinitions=result.definitions;view.fiveDefinitionRevision=revision;
      ops.renderProject(context,view);
    }catch(error){ops.appendProjectLog(context,'error',error.message);}
    finally{view.fiveDefinitionRefresh=false;}
  })();
}
export async function definitionHistoryCommand(context,view,direction,ops){
  if(state.busy||state.request)return;
  state.busy=true;context.actions?.refreshProjectHistoryControls?.();
  try{
    acceptHistory(await context.productProxy.invoke(direction==='redo'?'Product.RedoSettings':'Product.UndoSettings',{scope}));
    const result=await context.productProxy.invoke('MachineDefinition.List',{});
    if(!view.scene||view.disposed||context.isCurrentProject?.()===false)return;
    view.scene.machineDefinitions=result.definitions;
    view.fiveDefinitionRevision=state.revision;
    if(view.fiveMachine)view.fiveMachine.error='';
    ops.renderProject(context,view);
  }catch(error){
    if(view.fiveMachine)view.fiveMachine.error=error.message;
    ops.appendProjectLog(context,'error',error.message);
    if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);
  }finally{state.busy=false;context.actions?.refreshProjectHistoryControls?.();}
}
