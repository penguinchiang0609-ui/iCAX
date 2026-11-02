import {disposeFixturePlacement} from './fixturePlacement.mjs';
import {revealSceneWorkpiece} from './sceneListReveal.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import { activeTool, selectTool, workspaceState } from './workspaceModel.mjs';
import { workpieceThumbnail } from './workpieceThumbnails.mjs';
import { disposeToolAxisPointRotation } from './toolAxisPointRotation.mjs';
import { disposeToolAxisPointPreview,exitToolAxisPointModes } from './toolAxisPoints.mjs';
import { disposePlacementPreview, openPlacement, placementTools } from './workpiecePlacement.mjs';
import {disposeAlignment,flushAlignmentEdits} from './workpieceAlignment.mjs';
import { disposeSimulation } from './trajectorySimulation.mjs';
import { disposeCutPreview } from './cutPreview.mjs';
import { disposeGeneratedPreview } from './toolpathGeneration.mjs';
import { disposeLeadOverlay } from './cutLeads.mjs';
import { disposeFixtureDesign } from './fixtureDesign.mjs';
import {disposeSelectionFeedback} from './selectionFeedback.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import {disposeLocating} from './setupLocating.mjs';
import {visibleWorkpiecePlacements} from './workpieceState.mjs';

export const workpiecePlacements = view => {
  const all=visibleWorkpiecePlacements(view), ids=[...new Set(all.map(p=>p.workpieceId))];
  return ids.map(id=>all.find(p=>p.workpieceId===id&&p.id===view.scene.activePlacementId)||all.find(p=>p.workpieceId===id));
};
export const workpieceSelectionBusy = view => !!(view.pending || view.fiveJobRequest || view.fiveWorkpieceEnable?.request || view.fiveWorkpieceLock?.request || view.fiveWorkpieceRemoval?.request || view.fiveAlignment?.request || view.fixtureRequest || view.fiveWorkpieceSwitching || view.fivePlacement?.request || view.fiveTreeRequest
  || view.fiveStart?.request || view.fiveHole?.request || view.fiveLeads?.request || view.fiveLeads?.saving
  || view.fiveReverseRequest || view.fiveClear?.request || view.fiveOrder?.request || view.fiveGeneration?.request
  || view.fiveSetupRequest || view.fiveLocating?.request || view.fiveRapid?.request || view.fiveRapidBatch?.request || view.fiveSimulation?.request || view.fiveCommandSavingId || view.fiveToolAxisRequest || view.fiveAxisPointEdit?.dirty || view.fiveAxisPointEdit?.rotation?.gesture || view.fiveAxisPointLookup);
export function workpieceLabel(view, placement) {
  const items=view.scene.domain.workpieces, item=view.scene.domain.workpieces.find(w=>w.id===placement.workpieceId);
  return `${items.indexOf(item)+1} · ${item?.name || '工件'}${item?.enabled===false?'（已禁用）':''}`;
}
export function renderWorkpieces(view, compact=false) {
  const items=workpiecePlacements(view), disabled=workpieceSelectionBusy(view)?'disabled':'';
  if(compact){
    const current=items.find(p=>p.id===view.scene.activePlacementId);if(!current)return '';
    return `<div class="five-workpiece-select" data-five-key="workpiece-selector"><details data-five-key="workpiece-chooser" data-five-workpiece-chooser><summary aria-label="选择当前工件" aria-disabled="${!!disabled}" tabindex="${disabled?-1:0}">${workpieceThumbnail(view,current)}<strong>${esc(workpieceLabel(view,current))}</strong><span aria-hidden="true">⌄</span></summary><div class="five-workpiece-options" aria-label="工件列表">${items.map(p=>`<button type="button" class="five-tree-row ${p.id===current.id?'selected':''}" data-five-key="workpiece-${attr(p.id)}" data-five-workpiece="${attr(p.id)}" aria-pressed="${p.id===current.id}" ${disabled}>${workpieceThumbnail(view,p)}<span><strong>${esc(workpieceLabel(view,p))}</strong><small>${p.id===current.id?'当前工件':'切换工件'}</small></span></button>`).join('')}</div></details></div>`;
  }
  return `<section data-five-key="workpieces"><div class="five-fixture-heading"><strong>工件</strong><span>${items.length}</span></div><div class="five-workpiece-list" data-five-key="workpiece-list">${items.map(p=>`<button type="button" class="five-tree-row ${p.id===view.scene.activePlacementId?'selected':''}" data-five-key="workpiece-${attr(p.id)}" data-five-workpiece="${attr(p.id)}" aria-pressed="${p.id===view.scene.activePlacementId}" ${disabled}>${workpieceThumbnail(view,p)}<span><strong>${esc(workpieceLabel(view,p))}</strong><small>${(view.scene.domain.intentToolpaths||[]).filter(c=>c.placementId===p.id).length} 条切割定义</small></span></button>`).join('')||'<div class="five-empty-row">尚未导入工件</div>'}</div><div class="five-tree-footer"><button type="button" data-five-command="workpiece.import" ${disabled}>导入</button></div></section>`;
}
export function resetWorkpieceTools(view) {
  disposeFixturePlacement(view);
  view.fiveSetupCheckOpen=false;
  view.fiveSetupNameEdit=null;view.fiveFixtureDesignOpen=false;view.fiveFixtureNewUnit=false;
  if(view.fiveFixtureLibrary){view.fiveFixtureLibrary.open=false;view.fiveFixtureLibrary.choosing=false;}
  if(view.fiveJobPlayback&&!view.fiveJobPlayback.switching)view.fiveJobPlayback=null;
  disposeLocating(view);disposeAlignment(view);
  disposeSelectionFeedback(view);disposeFixtureDesign(view);view.fixtureDesignPicking=false;view.fixtureDesignFaces=null;view.fixtureDesignPlacement=null;
  if(view.fivePoseSelection){view.fivePoseSelection.active=false;view.fivePoseSelection.hover=null;view.fivePoseSelection.selected=null;}
  disposeToolAxisPointRotation(view);disposeToolAxisPointPreview(view);exitToolAxisPointModes(view);
  view.fiveAxisPointEdit=null;
  disposePlacementPreview(view);view.fivePlacement=null;
  disposeSimulation(view);view.fiveSimulation=null;
  disposeCutPreview(view);disposeGeneratedPreview(view);disposeLeadOverlay(view);
  view.fiveRapid?.cancel?.();view.fiveRapid=null;view.fiveRapidBatch=null;
  view.fiveGeneration=null;view.fiveOrder=null;
  view.fiveGenerationOpen=false;view.fiveAxisReverseOpen=false;view.fiveAxisClearOpen=false;
  view.fiveAxisRegistrationPick=null;view.fiveAxisExtractionPick=null;
  view.fiveAxisRegistrationOpen=false;view.fiveAxisExtractionOpen=false;
  view.fiveAxisStartOpen=false;view.fiveAxisSplitOpen=false;view.fiveAxisHoleOpen=false;view.fiveAxisLeadsOpen=false;
  view.fiveStart=null;view.fiveSplit=null;view.fiveHole=null;view.fiveLeads=null;
  const ui=workspaceState(view);ui.selectedIntentId='';ui.selectedIntentIds=[];ui.selectedGroupId='';ui.selectedGroupIds=[];
  ui.tools.workpiece='select';view.fivePreparationKind='workpiece';
  view.fiveLocating=null;view.fiveSetupRequest=null;
  view.viewport?.setSpecificationAnnotations?.([]);
}
export async function selectWorkpiece(context,view,id,ops,{kind='setup'}={}) {
  if(view.fiveAlignment&&!await flushAlignmentEdits(context,view,ops))return;
  if(!view.scene?.domain?.placements?.find(p=>p.id===id)?.setupId)kind='workpiece';
  if(workpieceSelectionBusy(view)||view.viewport?.transformManipulator?.pointerId!=null||!visibleWorkpiecePlacements(view).some(p=>p.id===id))return;
  const beginPlacement=()=>{
    if(view.fiveAxisArea!=='workpiece'||view.disposed||context.isCurrentProject?.()===false)return;
    resetWorkpieceTools(view);
    selectTool(view,kind==='workpiece'?'select':'translate');if(kind!=='workpiece'&&view.viewport?.sceneObjects.get(id)?.geometry)openPlacement(view,'translate');
  };
  if(id===view.scene.activePlacementId){beginPlacement();ops.renderProject(context,view);return;}
  resetWorkpieceTools(view);view.fiveWorkpieceSwitching=true;view.pending=true;view.error='';ops.renderProject(context,view);
  try {
    await context.sceneProxy.invoke('CamWorkpiece.Activate',{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,placementId:id});
    await saveCommittedProjectEdit(context,view,ops);
    if(view.disposed||context.isCurrentProject?.()===false)return;
    view.fiveActivePlacementId=id;
    await ops.refreshSceneState(context,view);
    await ops.fitViewAfterRenderPublish(context,view,{entityIds:[id]});
  } catch(error) { if(!view.disposed)view.error=error.message; }
  finally {view.fiveWorkpieceSwitching=false;view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false){if(!view.error)beginPlacement();ops.renderProject(context,view);}}
}
export function handleWorkpiecePick(context,view,data,hit,_event,_hits,ops) {
  if(view.fiveAxisArea==='workpiece'){
    // Blank/model clicks never toggle the active workpiece or tint the whole solid.
    // Alignment/fixture/reference picking has already had first refusal.
    const id=String(data?.objectId||'');
    if(hit&&workpiecePlacements(view).some(p=>p.id===id)){
      if(id===view.scene.activePlacementId)revealSceneWorkpiece(context,view,ops);
      else if(placementTools.includes(activeTool(view)))void selectWorkpiece(context,view,id,ops).then(()=>{
        if(!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveAxisArea==='workpiece'&&view.scene.activePlacementId===id)revealSceneWorkpiece(context,view,ops);
      });
    }
    return true;
  }
  return false;
}
