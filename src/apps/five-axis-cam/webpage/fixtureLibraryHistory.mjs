// Availability cache only. Product framework Database owns the resource history.
const state={canUndo:false,canRedo:false,busy:false,loaded:false,request:null};
export function fixtureLibraryHistoryState(){return {scope:'product',canUndo:state.canUndo,canRedo:state.canRedo,blocked:state.busy||!!state.request,mutationPending:state.busy};}
export async function refreshFixtureLibraryHistory(context){
 if(state.request)return state.request;
 state.request=(async()=>{try{const result=await context.productProxy.invoke('Product.GetUserDataHistory',{featureId:'fixture-library'});state.canUndo=!!result.canUndo;state.canRedo=!!result.canRedo;state.loaded=true;}finally{state.request=null;context.actions?.refreshProjectHistoryControls?.();}})();return state.request;
}
export function ensureFixtureLibraryHistory(context,view,ops){
 if(!view.fiveFixtureLibrary?.open||view.pending||state.loaded||state.busy||state.request)return;
 void refreshFixtureLibraryHistory(context).catch(e=>ops.appendProjectLog(context,'error',e.message));
}
export async function fixtureLibraryHistoryCommand(context,view,direction,ops){
 if(state.busy||state.request||view.pending)return;
 const s=view.fiveFixtureLibrary;
 if(s.editing){s.editing=null;s.names={};if(direction==='undo'){ops.renderProject(context,view);return;}}
 state.busy=true;view.pending=true;ops.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();
 try{
  await context.productProxy.invoke(direction==='redo'?'Product.RedoUserData':'Product.UndoUserData',{featureId:'fixture-library'});
  const result=await context.sceneProxy.invoke('CamFixture.LibraryList',{});
  if(view.disposed||context.isCurrentProject?.()===false)return;
  Object.assign(s,{entries:result.entries,defaultId:result.defaultId,preferencesRevision:result.preferencesRevision,names:{},editing:null});
  view.error='';ops.renderProject(context,view);
 }catch(e){view.error=e.message;ops.renderProject(context,view);}
 finally{state.busy=false;view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);await refreshFixtureLibraryHistory(context);}
}
