import {activeWorkpiece} from './workpieceState.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {selectedFixture} from './fixtureActions.mjs';
import {resetWorkpieceTools,workpieceSelectionBusy} from './workpieceSelection.mjs';
import {selectTool} from './workspaceModel.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';

export async function preparationMutation(context,view,action,ops){
 if(workpieceSelectionBusy(view)||view.viewport?.transformManipulator?.pointerId!=null)return;
 const [verb,kind]=action.split(':'),p=selectedPlacement(view),w=activeWorkpiece(view),unit=selectedFixture(view);
 if(!p||!w||verb==='copy'&&kind==='fixture'&&!unit)return;
 const method=verb==='lock'?'CamWorkpiece.SetLocked':kind==='fixture'?'CamFixture.UnitCopy':kind==='setup'?'CamSetup.Copy':'CamWorkpiece.Copy';
 const request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),...(kind==='fixture'?{setupId:p.setupId,unitId:unit.id}:{placementId:p.id}),...(verb==='lock'?{locked:!w.locked}:{})};
 view.fivePreparationRequest={method,request};view.pending=true;view.error='';ops.renderProject(context,view);
 try{
  let result;try{result=await context.sceneProxy.invoke(method,request);}
  catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed')throw error;result=receipt.result;}
  view.fivePreparationRequest=null;await saveCommittedProjectEdit(context,view,ops);
  if(view.disposed||context.isCurrentProject?.()===false)return;
  if(verb==='copy')resetWorkpieceTools(view);
  if(result.placementId)view.fiveActivePlacementId=result.placementId;
  await ops.refreshSceneState(context,view);
  if(kind==='fixture'){
   view.fiveAxisFixtures??={selected:'',drafts:{},clearance:'2'};view.fiveAxisFixtures.selected=result.unitId;selectTool(view,'fixture');
  }else if(verb==='copy'){
   selectTool(view,kind==='setup'?'translate':'select');
   view.fivePreparationCollapsed?.delete('workpiece:'+result.workpieceId);view.fivePreparationCollapsed?.delete('setup:'+result.placementId);
   await ops.fitViewAfterRenderPublish(context,view,{entityIds:[result.placementId]});
  }
  ops.appendProjectLog(context,'ok',verb==='lock'?(request.locked?'工件及其装夹姿态已锁定。':'工件及其装夹姿态已解锁。'):'已复制，可通过撤销恢复。');
 }catch(error){if(!view.disposed)view.error=error.message;}
 finally{view.pending=false;view.fivePreparationRequest=null;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
