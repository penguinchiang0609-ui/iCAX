import {supportsToolAxis} from './toolAxisEligibility.mjs';
import { activeTool, selectTool } from './workspaceModel.mjs';
import { flushToolAxisPointEdits, inspectToolAxisSample,exitToolAxisPointModes } from './toolAxisPoints.mjs';
import { flushToolAxisEdits } from './toolAxisParameters.mjs';
import { disposeToolAxisPointRotation } from './toolAxisPointRotation.mjs';
import { pickDisplayedToolAxis, syncToolAxisPickHighlight, setToolAxisDisplayVisible } from './toolAxisPicking.mjs';

const alive=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const state=view=>view.fivePoseSelection??={active:false,hover:null,selected:null};
const inCutting=view=>view.fiveAxisArea==='toolpath'&&view.fiveCamPage!=='rapid';
const selectable=view=>(view.scene?.intentToolpaths||[]).some(c=>c.enabled!==false&&supportsToolAxis(c));
const sourceKey=(view,cut)=>JSON.stringify([cut.id,cut.poseRef,cut.processRef,cut.featureRef,cut.curveRef,cut.sourceState,cut.start,cut.reversed,
  view.scene?.domain?.placements?.find(p=>p.id===cut.placementId)?.transform]);
const busy=view=>!!(view.pending||view.fiveTreeRequest||view.fiveWorkpieceSwitching||view.fiveToolAxisRequest
  ||view.fiveAxisPointLookup||view.fiveAxisPointEdit?.rotation?.gesture||view.fivePlacement?.request
  ||view.fiveOrder?.request||view.fiveRapid?.request||view.fiveRapidBatch?.request||view.fiveRapidBatch?.running
  ||view.fiveGeneration?.request||view.fiveGeneration?.running||view.fiveStart?.request||view.fiveHole?.request
  ||view.fiveLeads?.request||view.fiveLeads?.saving||view.fiveReverseRequest||view.fiveClear?.request
  ||view.fiveSimulation?.request||view.fiveSimulation?.collisionRequest||view.fiveSimulation?.checking
  ||view.fiveSimulation?.thresholdRequest||view.fiveCommandSavingId||view.fiveNameSavingId||view.fiveAxisImportActive);

function release(view,d){
  d.hover=null;d.selected=null;d.selectedKey=null;d.inspectToken=null;syncToolAxisPickHighlight(view);
  if(d.hoverViewport){
    // Registration may have taken hover ownership earlier in this same render.
    if(!view.fiveAxisRegistrationPick)d.hoverViewport.setHoverEnabled(false);
    d.hoverViewport=null;
  }
}
function closeOtherInteractions(view){
  exitToolAxisPointModes(view);
  if(view.fiveAxisPointEdit){view.fiveAxisPointEdit.picking=false;view.fiveAxisPointEdit.rotation=null;}
  view.fiveAxisRegistrationPick=null;view.fiveAxisExtractionPick=null;
  for(const field of ['fiveAxisRegistrationOpen','fiveAxisExtractionOpen','fiveAxisSplitOpen','fiveAxisStartOpen',
    'fiveAxisHoleOpen','fiveAxisLeadsOpen','fiveAxisReverseOpen','fiveAxisClearOpen','fiveGenerationOpen'])view[field]=false;
  if(view.fiveOrder){view.fiveOrder.open=false;view.fiveOrder.menu=null;}
  if(view.fiveRapid){view.fiveRapid.cancel?.();view.fiveRapid.open=false;view.fiveRapid.running=false;view.fiveRapid.token=(view.fiveRapid.token||0)+1;}
  clearTimeout(view.fiveLeads?.timer);
}

export function disposePoseSelection(view){
  const d=view.fivePoseSelection;if(!d)return;d.active=false;release(view,d);
}

// Run before producing panel/overlay HTML. History can change an immutable pose
// without changing the scene epoch; never keep a highlighted old anchor while
// the ordinary point editor silently reloads a different position.
export function preparePoseSelection(view){
  const d=view.fivePoseSelection;if(!d?.active)return;
  if(view.disposed||!inCutting(view)||activeTool(view)!=='keypoint'||d.epoch!==view.scene?.domain?.expected?.epoch){disposePoseSelection(view);return;}
  if(!d.selected)return;
  const id=d.selected.intentId,cut=view.scene?.intentToolpaths?.find(c=>c.id===id);
  if(cut&&cut.enabled!==false&&supportsToolAxis(cut)&&sourceKey(view,cut)===d.selectedKey)return;
  d.selected=null;d.selectedKey=null;d.hover=null;d.inspectToken=null;
  const point=view.fiveAxisPointEdit;if(point?.id===id&&!point.dirty&&!point.request)view.fiveAxisPointEdit=null;
  const ui=view.fiveAxisUI;
  if(ui){
    if(ui.selectedIntentId===id)ui.selectedIntentId='';
    if(ui.cutSelectionAnchor===id)ui.cutSelectionAnchor='';
    if(ui.selectedIntentIds?.includes(id))ui.selectedIntentIds=ui.selectedIntentIds.filter(value=>value!==id);
  }
  syncToolAxisPickHighlight(view);
}

export function renderPoseSelectionControl(view){
  const d=state(view),disabled=!d.active&&(busy(view)||!selectable(view));
  return `<button type="button" class="five-pose-select${d.active?' active':''}" data-five-key="pose-selection-mode" data-five-command="five.pose-select.toggle" aria-label="姿态选择模式" aria-pressed="${d.active}" title="${d.active?'退出姿态选择；选择模式中可旋转、平移和缩放视角':'进入姿态选择；点击场景中的刀轴查看姿态'}" ${disabled?'disabled':''}>姿态选择：${d.active?'开启':'关闭'}</button>`;
}

export async function poseSelectionCommand(context,view,action,ops){
  if(!alive(context,view)||!['toggle','exit'].includes(action))return;
  const d=state(view);
  if(action==='exit'||d.active){d.active=false;release(view,d);ops.renderProject(context,view);return;}
  if(!inCutting(view)||busy(view))return;
  if(!selectable(view)){view.error='请先建立并启用切割刀路。';ops.renderProject(context,view);return;}
  try{
    await flushToolAxisPointEdits(context,view,ops);
    await flushToolAxisEdits(context,view,ops);
  }catch(error){if(alive(context,view)){view.error=error.message;ops.renderProject(context,view);}return;}
  if(!alive(context,view)||busy(view)||!inCutting(view))return;
  closeOtherInteractions(view);selectTool(view,'keypoint');view.fiveCamPage='cutting';
  Object.assign(d,{active:true,hover:null,selected:null,selectedKey:null,epoch:view.scene?.domain?.expected?.epoch});view.error='';
  setToolAxisDisplayVisible(view,true);syncPoseSelection(context,view);ops.renderProject(context,view);
}

export function handlePoseSelectionPick(context,view,_data,_hit,event,_hits,ops){
  const d=view.fivePoseSelection;if(!d?.active)return false;
  // Blank/model hits are consumed too: this mode never falls through to mesh,
  // ordinary path, order or topology selection.
  if(!alive(context,view)||view.pending||!event)return true;
  const hit=pickDisplayedToolAxis(view,event);d.hover=null;
  if(!hit){syncToolAxisPickHighlight(view);return true;}
  const cut=view.scene?.intentToolpaths?.find(c=>c.id===hit.intentId);if(!cut)return true;
  d.selected=hit;d.selectedKey=sourceKey(view,cut);const token={hit};d.inspectToken=token;syncToolAxisPickHighlight(view);
  void inspectToolAxisSample(context,view,hit,ops).catch(error=>{
    if(alive(context,view)&&view.fivePoseSelection===d&&d.active&&d.inspectToken===token){view.error=error.message;ops.renderProject(context,view);}
  });
  return true;
}

export function handlePoseSelectionHover(context,view,_data,_hit,event,_hits,pointer){
  const d=view.fivePoseSelection;if(!d?.active)return false;
  const hit=alive(context,view)&&!view.pending&&pointer?.pointerActive!==false&&event?.type==='pointermove'&&!event.buttons
    ?pickDisplayedToolAxis(view,event):null;
  d.hover=hit;syncToolAxisPickHighlight(view);return true;
}

export function syncPoseSelection(context,view){
  preparePoseSelection(view);
  const d=view.fivePoseSelection;if(!d)return;
  const enabled=d.active&&alive(context,view)&&inCutting(view)&&activeTool(view)==='keypoint'
    &&d.epoch===view.scene?.domain?.expected?.epoch;
  if(!enabled){if(d.active)d.active=false;if(d.hover||d.selected||d.hoverViewport)release(view,d);return;}
  const vp=view.viewport;if(!vp)return;
  disposeToolAxisPointRotation(view);if(vp.transformManipulator)vp.clearTransformManipulator();
  vp.setCameraNavigationEnabled(true);vp.setPickingEnabled(true);vp.setHoverEnabled(true);d.hoverViewport=vp;
  syncToolAxisPickHighlight(view);
}
