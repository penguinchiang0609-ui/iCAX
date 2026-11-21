import {saveEditedProject,flushEditedProjectSave,isEditedProjectSaving,clearCommittedSaveError} from './projectEditPersistence.mjs';
import {disposeHeadChoicePicker} from './headChoicePicker.mjs';
import {blockingProgress,installBlockingProgressStyle,syncBlockingProgress,disposeBlockingProgress} from './blockingProgress.mjs';
import {hasUnappliedWorkpieceDrafts,clearWorkpieceEditorDrafts} from './workpieceEditDrafts.mjs';
import {fixtureLibraryHistoryState,ensureFixtureLibraryHistory,fixtureLibraryHistoryCommand} from './fixtureLibraryHistory.mjs';
import {machineIKCommand,selectMachinePoseTarget,finishMachinePoseField,flushMachinePoseEdits,cancelMachinePoseDraft,cancelRejectedMachinePoseDrafts,beginMachinePoseHudEdit,finishMachinePoseHudEdit,syncMachineIK,disposeMachineIK,finishMachineIK,machinePoseSpaceChanged} from './machineIK.mjs';
import {finishMachineParameterField,flushMachineParameterEdits,cancelInvalidMachineParameters,cancelRejectedMachineMoves} from './machineParameterEdits.mjs';
import {syncMachineCollisionFeedback,disposeMachineCollisionFeedback,machineCollisionFeedbackTarget,setMachineCollisionFeedback,machineCollisionFeedbackGeometrySignature,machineCollisionFeedbackSignature} from './machineCollisionFeedback.mjs';
import {syncFixturePlacement,disposeFixturePlacement} from './fixturePlacement.mjs';
import {attachFixturePlateEditor,disposeFixturePlateEditor,fixturePlateHistoryState,fixturePlateHistoryCommand,fixturePlateCommand} from './fixturePlateEditor.mjs';
import { disposeFixtureDesign,attachFixtureDirectionMenu,syncSupportDirectionArrow,designCommand,highlightDesign } from './fixtureDesign.mjs';
import {fixtureDesignBusy} from './fixtureDesignState.mjs';
import {syncSelectionFeedback,handleSelectionFeedbackHover,disposeSelectionFeedback} from './selectionFeedback.mjs';
import {attachFaceListFocus,faceFocusShortcut} from './faceSelectionFocus.mjs';
import {cutFocusShortcut,focusSelectedCuts,focusCutNode} from './cutFocus.mjs';
import {rapidFocusShortcut,focusRapid} from './rapidFocus.mjs';
import {fixturePartFocusShortcut,focusFixturePart} from './fixturePartFocus.mjs';
import {listKeyboardAction,revealListRow} from './listNavigation.mjs';
import {selectOrderStrategy} from './orderStrategy.mjs';
import {orderFieldChanged} from './orderParameters.mjs';
import {commandAvailability} from './commandAvailability.mjs';
import {fixtureMeshColor,fixturePickable,syncFixturePresentation} from './fixturePresentation.mjs';
import { selectWorkpiece, handleWorkpiecePick, workpieceSelectionBusy, resetWorkpieceTools } from './workpieceSelection.mjs';
import {removeWorkpiece,toggleWorkpieceLock,toggleWorkpieceEnabled,savePlacementDraft} from './workpieceActions.mjs';
import {workpieceLocked,workpieceEnabled,enabledWorkpiecesOnly,visibleWorkpiecePlacements} from './workpieceState.mjs';
import {editWorkpieceName,cancelWorkpieceName,saveWorkpieceName,flushWaitingWorkpieceNames} from './workpieceNames.mjs';
import {positionRibbonWorkspacePicker} from '../../../iCAX-UI/SDK/AppShell/app/ribbonWorkspace.mjs';
import { extractionRectangleSelectionKey, handleExtractionRectangleSelect } from './extractionSelection.mjs';
import { hydrateWorkpieceThumbnails, disposeWorkpieceThumbnails, workpieceChooserKey } from './workpieceThumbnails.mjs';
import { typedVariant } from '../../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { cutTreeCommand, selectCutGroup, cutTreeBusy } from './cutTree.mjs';
import { attachCutTreeDrag } from './cutTreeDrag.mjs';
import {attachPreparationDrag} from './preparationDrag.mjs';
import { editCutCommandText, saveCutCommandText, flushWaitingCommands, editCutName, saveCutName } from './cutParameters.mjs';
import { hasUnappliedCutDrafts, clearCutEditorDrafts } from './cutEditDrafts.mjs';
import { clearCutSaveError } from './cutEditPersistence.mjs';
import { toolAxisDisplayCommand, syncToolAxisDisplay, disposeToolAxisDisplay } from './toolAxisDisplay.mjs';
import { poseSelectionCommand, handlePoseSelectionPick, handlePoseSelectionHover, syncPoseSelection, preparePoseSelection, disposePoseSelection } from './toolAxisPoseSelection.mjs';
import { toolAxisPointAngleChanged, toolAxisPointRotationCommand, toolAxisPointSpaceChanged, syncToolAxisPointRotation, disposeToolAxisPointRotation } from './toolAxisPointRotation.mjs';
import { toolAxisPointCommand, toolAxisPointField, syncToolAxisPoints, disposeToolAxisPointPreview, handleToolAxisPointPick, flushToolAxisPointEdits, exitToolAxisPointModes, prepareToolAxisPointModes,inspectToolAxisAtPoint } from './toolAxisPoints.mjs';
import { reverseToolAxes, toolAxisCommand, toolAxisFieldChanged, syncToolAxisParameters, disposeToolAxisPreview, flushToolAxisEdits } from './toolAxisParameters.mjs';
import { disposeWorkpieceFilePicker, disposeInactiveWorkpieceFilePicker } from './workpieceFilePicker.mjs';
import {disposeCuttingHeadFilePicker,disposeInactiveCuttingHeadFilePicker} from './cuttingHeadImport.mjs';
import { openOrdering, orderCommand, orderingPick, handleOrderPick, orderContextMenu } from './toolpathOrdering.mjs';
import { openRapid, rapidCommand, rapidPick, rapidFieldChanged, handleRapidPick, syncRapidPathEditing, disposeRapidPathEditing, flushRapidPathSave, cancelInvalidRapidInput } from './rapidPlanning.mjs';
import {selectRapidDefaultStrategy,selectRapidSafety} from './rapidDefaultStrategy.mjs';
import {cancelRapidPathMode,beginRapidHudEdit,finishRapidHudEdit} from './rapidPathEditing.mjs';
import { simulationCommand, simulationField, syncSimulation, disposeSimulation, pauseSimulation } from './trajectorySimulation.mjs';
import {syncCuttingHeadPreview,disposeCuttingHeadPreview} from './cuttingHeadPreview.mjs';
import {headColliderField,headColliderCommand,flushHeadColliderSaves,scheduleHeadColliderSave,cancelInvalidHeadColliderEdits} from './cuttingHeadColliders.mjs';
import {headInstanceField,headInstanceCommand} from './cuttingHeadInstances.mjs';
import {headScopeCommand,headScopeField,refreshHeadAssignments} from './headAssignmentScopes.mjs';
import {syncAlertPanel,disposeAlertPanel,renderWorkspaceAlerts,workspaceAlertItems,installAlertStyle} from './alerts.mjs';
import {installCollisionProgressStyle} from './collisionProgress.mjs';
import {installCollisionDiagnosticsStyle} from './collisionDiagnostics.mjs';
import {machineCommand,machineField,syncMachine,stopMachine,selectedMachineDefinition} from './machineSimulation.mjs';
import {camMachinePlayback,machineViewportActive,camHeadElements} from './machinePlaybackContext.mjs';
import {clearIndependentHead} from './independentHeadPlayback.mjs';
import {syncMachineAssemblyPlacement,handleMachineAssemblyPick,disposeMachineAssemblyPlacement} from './machineAssemblyPlacement.mjs';
import {syncMachinePlanRecovery,disposeMachinePlanRecovery} from './machinePlanRecovery.mjs';
import {machinePlanIssueCommand,syncMachinePlanIssue,disposeMachinePlanIssue} from './machinePlanIssue.mjs';
import {definitionHistoryState,ensureDefinitionHistory,definitionHistoryCommand} from './machineDefinitionHistory.mjs';
import {attachMachineJog,syncMachineJog,stopMachineJog,consumeMachineJogClick} from './machineJog.mjs';
import {machineColliderValueChanged,cancelMachineColliderValue,machineColliderSpaceChanged,disposeMachineColliders,syncMachineColliderEdit,stopMachineColliderEdit,focusMachineCollider,attachMachineColliderBounds,finishMachineColliderField,flushMachineColliderSaves} from './machineColliders.mjs';
import {disposeMachineObservation} from './machineObservation.mjs';
import {machineSelectedObjectIds,handleMachinePartPick,selectMachinePart} from './machineSelection.mjs';
import {ownedProjectKeydown,activateProjectDocumentBinding,releaseProjectDocumentBindings} from './projectKeyboard.mjs';
import {transformShortcutButton,transformShortcutMode} from './transformHud.mjs';
export { getStartupPresentation } from "./startup.mjs";
import { createWorkbench } from '../../_shared/workbench/createWorkbench.mjs';
import { cutCreationTimeoutMs, machiningTaskTimeoutMs } from './operationTiming.mjs';
import { RENDER_ENTITY_VIEW_PROJECTION } from '../../_shared/workbench/projection.mjs';
import { projectWorkpiece, mergeDomainState, importModelPath, chooseWorkpiece, cancelImport, readMachiningState,syncIntentGeneration } from './domainActions.mjs';
import { releaseProject as releaseWorkbench } from '../../_shared/workbench/createWorkbench.mjs';
import { getProjectView, findProjectView } from '../../_shared/workbench/state/projectViewStore.mjs';
import { AREAS, TOOLS, activeTool, selectTool, normalizeArea, workspaceState, currentDraft, geometryEditing, preparationKind, switchCamPage,camPathKind,switchCamPathKind } from './workspaceModel.mjs';
import { renderLeft, renderRight, renderSummary, renderOverlay, renderDock } from './workspaceViews.mjs';
import { patchWorkspacePanels } from './workspaceDom.mjs';
import { attachFloatingDialogs, syncFloatingDialogs, disposeFloatingDialogs } from './floatingDialogs.mjs';
import { updateRegistrationAnnotations } from './registrationAnnotations.mjs';
import { handleRegistrationHover, syncRegistrationInteraction, disposeRegistrationInteraction } from './registrationInteraction.mjs';
import { selectedPlacement, runRegistration, loadRegistrationDraft, registrationFieldChanged, beginRegistrationPick, flipRegistrationDirection, handleRegistrationPick, openRegistration, confirmRegistration } from './registrationActions.mjs';
import { extractionCommand, extractionDraft, extractionChanged, extractionDirectionChanged, handleExtractionPick } from './extractionActions.mjs';
import {syncExtractionDirection,disposeExtractionDirection} from './extractionDirection.mjs';
import { editProcessCommand, loadProcessDraft, processDraftChanged } from './processEditing.mjs';
import { editCutCommand, loadCutDraft, cutDraftChanged } from './cutEditing.mjs';
import { reverseCommand } from './cutReversing.mjs';
import { clearCommand, openCutClearing } from './cutClearing.mjs';
import { generationCommand, updateGeneratedPreview, disposeGeneratedPreview, generatedGeometry } from './toolpathGeneration.mjs';
import { fixtureCommand, fixtureDraft, fixtureFieldChanged, fixturePicked, highlightFixture, beginFixtureInput, finishFixtureInput, flushFixtureEdits } from './fixtureActions.mjs';
import {fixtureLibraryField,closeFixtureChooser} from './fixtureLibrary.mjs';
import {preparationCommand} from './preparationTree.mjs';
import {operationCommand,operationField,syncMachineTypeAssignments,prepareOperationArea,syncOperationPreparationKind} from './operationWorkspace.mjs';
import {prepareOperationNavigation} from './operationNavigation.mjs';
import {syncOperationCreateDialog,disposeOperationCreateDialog} from './operationCreateDialog.mjs';
import { placementTools, preparePlacement, openPlacement, placementCommand, placementFieldChanged, placementSpaceChanged, syncPlacementInteraction, disposePlacementPreview, transformParameterCommand, transformParameterChanged } from './workpiecePlacement.mjs';
import { updateCutPreview, disposeCutPreview } from './cutPreview.mjs';
import { selectCut, handleCutLinePick, cutRectangleSelectionKey, handleCutRectangleSelect } from './cutSelection.mjs';
import { openCutStarting, handleStartPick, startCommand } from './cutStarting.mjs';
import { openCutSplitting, handleSplitPick, splitCommand } from './cutSplitting.mjs';
import { openPathEditing,pathEditField,pathEditCommand } from './pathEditing.mjs';
import {syncPathTransform,disposePathTransform} from './pathTransform.mjs';
import {toolpathLayerCommand,toolpathLayerInput,toolpathLayerChange,flushToolpathLayerEdits} from './toolpathLayers.mjs';
import {holeDimensionField,saveHoleDimensions} from './standardHoleDimensions.mjs';
import { openStandardHoles, standardHoleChanged, standardHoleCommand } from './standardHoles.mjs';
import { openLeads, openLeadSettings, applyLeadToCut, syncLeads, leadFieldChanged, leadCommand, disposeLeadOverlay } from './cutLeads.mjs';
import { syncCompensation, disposeCompensationOverlay } from './cutCompensation.mjs';
import { getRibbonDefinition,syncRibbonCommandAvailability } from './ribbon.mjs';
import {camPage,camPageForTool,CAM_PAGES,camContextCommand,camContextKey,positionCamContextPicker,attachCamContextPickers} from './camWorkspace.mjs';
import {cancelBlockingOperation} from './operationFeedback.mjs';
import {openAlignment,disposeAlignment,alignmentCommand,alignmentFieldChanged,finishAlignmentField,scheduleAlignmentSave,flushAlignmentEdits,handleAlignmentPick,handleAlignmentHover,syncAlignment} from './workpieceAlignment.mjs';
import {disposeSceneSnapping} from './sceneSnapping.mjs';
import {ensureFixtureUnitDetails} from './fixtureQueries.mjs';
export { getRibbonDefinition };
export function handleRibbonInput(context,id,value){
  const view=findProjectView(context.project?.projectId);if(!view||view.pending)return;
  if(id==='five.path.merge-tolerance')view.fiveMergeTolerance=value;
  if(id==='five.order.height-penalty')orderFieldChanged(view,'heightPenaltyWeight',value);
}

const bindings = new WeakMap();
const cam = createWorkbench({ isSceneOperationPending:view=>!!view.fivePathOperationPending||!!view.fiveSplit?.picking||!!view.fiveSplit?.committing,sceneMethods: ['CamScene.ReadState','MachineDefinition.List','Machine.List'], mergeScenePayload: mergeDomainState,hydrateScenePayload:async(payload,proxy)=>{
  // Reading the workspace must not run a machine move/collision check.
  // Explicit jog, collision and planning commands prepare their own inputs.
  return readMachiningState({sceneProxy:proxy},payload,{waitForGeneration:false});
},
  importModelPathAction: importModelPath, normalizeCamTab: normalizeArea, viewRequestTimeoutMs: cutCreationTimeoutMs,
  sceneRequestTimeoutMs: machiningTaskTimeoutMs,
  shouldRenderViewSnapshot:(_context,view)=>!view.fiveMachineJog?.running,
  mapAreaViewSnapshot:workpieceViewSnapshot,
  onAreaViewSnapshotApplied:workpieceViewSnapshotApplied,
  resolveSelectedSceneObjectIds:view=>view.fiveAxisArea==='toolpath'?[]:machineSelectedObjectIds(view) });
export function activeWorkpieceView(view) {
  const machineFrame=machineViewportActive(view);
  const setupId=view.scene?.domain?.activeSetupId;
  const configuration=view.scene?.domain?.setups?.find(s=>s.id===setupId)?.machineSetup;
  const machineId=camMachinePlayback(view)?configuration?.machineId:view.fiveMachine?.machineId||configuration?.machineId;
  const showSetup=view.fiveAxisArea!=='simulation'||activeTool(view)==='machine-simulation'&&view.scene?.machines?.some(m=>m.entityId===machineId)&&configuration?.machineId===machineId;
  const sourceOnly=view.fiveAxisArea==='workpiece';
  const id=sourceOnly||!showSetup||enabledWorkpiecesOnly(view)&&!workpieceEnabled(view)?null:setupId;
  const standalone=showSetup&&(sourceOnly||!setupId)&&view.scene?.activePlacementId;
  const workpieceId=standalone?view.scene.domain.placements.find(p=>p.id===standalone)?.workpieceId:null;
  const sources=['CFiveAxisPlacementComponent','CFiveAxisFixtureComponent'].map((component,index)=>({
    sourceId:`five-axis-active-${index}`,role:'scene',language:'sql',
    where:id?`WHERE HAS CRenderInstanceComponent AND HAS ${component} AND ${component}.SetupID = :setupId`
      :index===0&&workpieceId?`WHERE HAS CRenderInstanceComponent AND HAS ${component} AND ${component}.WorkpieceID = :workpieceId AND ${component}.SetupID = :sourceSetupId`:'WHERE FALSE',
    parameters:id?{setupId:typedVariant('uuid',id)}:index===0&&workpieceId?{workpieceId:typedVariant('uuid',workpieceId),sourceSetupId:typedVariant('uuid','00000000-0000-0000-0000-000000000000')}:{},
    projection:machineFrame?RENDER_ENTITY_VIEW_PROJECTION:[...RENDER_ENTITY_VIEW_PROJECTION,
      {alias:index===0?'setupPlacementTransform':'setupFixtureTransform',component,property:'Transform'}],
  }));
  if(machineFrame){
    const headOnly=camMachinePlayback(view),head=headOnly?camHeadElements(view):[],parameters=machineId?{machineId:typedVariant('uuid',machineId)}:{};
    head.forEach((e,i)=>parameters['head'+i]=typedVariant('unsigned_long_long',e.sourceIndex));
    const restriction=headOnly?head.length?` AND (${head.map((_,i)=>`CMachineElementComponent.SourceIndex = :head${i}`).join(' OR ')}) AND CMachineElementComponent.ElementKind = 'part'`:null:'';
    sources.push({sourceId:headOnly?'five-cam-head-elements':'five-machine-elements',role:'scene',language:'sql',where:machineId&&restriction!==null?'WHERE HAS CMachineElementComponent AND HAS CRenderInstanceComponent AND CMachineElementComponent.MachineID = :machineId'+restriction:'WHERE FALSE',parameters:restriction===null?{}:parameters,projection:RENDER_ENTITY_VIEW_PROJECTION});
  }
  return {sources};
}
function syncWorkpieceViewDefinition(context,view,ops){
  const area=view.areas?.[view.activeAreaId],key=JSON.stringify(activeWorkpieceView(view));
  if(!area?.viewDefinitionKey||area.viewDefinitionKey===key||view.fiveAreaProjectionKey===key)return;
  view.fiveAreaProjectionKey=key;
  // Local panel patches do not restart the native View. Changing the playback
  // owner changes both its sources and coordinate frame, even within CAM.
  stopMachine(view,true);
  void ops.refreshActiveAreaView(context,view,{}).then(()=>{
    if(!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveAreaProjectionKey===key)ops.renderProject(context,view);
  }).catch(error=>{
    if(!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveAreaProjectionKey===key){view.error=error.message;ops.renderProject(context,view);}
  });
}
import {setupCommand} from './setupManagement.mjs';
import {workpieceViewSnapshot,workpieceViewSnapshotApplied} from './workpieceViewSnapshot.mjs';
import {jobCommand} from './jobExecution.mjs';
import {locatingCommand,locatingField,syncLocating,handleLocatingHover,handleLocatingPick,disposeLocating} from './setupLocating.mjs';
import {canEnterArea,workflowArea} from './workflow.mjs';
function styles() {
  installBlockingProgressStyle(document);
  installCollisionProgressStyle(document);installCollisionDiagnosticsStyle(document);installAlertStyle(document);
  if (document.getElementById('five-axis-workspace-style')) return;
  const link = document.createElement('link');
  Object.assign(link, { id: 'five-axis-workspace-style', rel: 'stylesheet', href: new URL('./workspace.css', import.meta.url).href });
  document.head.append(link);
}
function sections(context, view) {
  if(view.fiveCutSaveError)view.error=view.fiveCutSaveError;
  if(view.fiveWorkpieceSaveError)view.error=view.fiveWorkpieceSaveError;
  return [['.cam-context-pane', renderLeft(context, view)], ['.cam-info-pane', renderRight(context, view)],
    ['[data-five-summary]', renderSummary(context, view)], ['[data-five-overlay]', renderOverlay(context, view)], ['[data-five-dock]', renderDock(context, view)], ['[data-five-alert-host]',renderWorkspaceAlerts(view)]];
}
function finishFloatingInput(context,view,target,cancel,ops){
 if(['name','x','y','z','rx','ry','rz'].includes(target.dataset?.fiveFixtureField)){void finishFixtureInput(context,view,target,cancel,ops).catch(()=>{});return true;}
 const owner=target.closest?.('.five-placement-hud')?.dataset.transformOwner;if(!owner)return false;
 if(view.fiveFloatingInputCommit)return true;
 const typedValue=target.value;
 const task=(async()=>{
  if(owner==='rapid'){
   if(!finishRapidHudEdit(view,cancel))return;
   if(cancel){if(view.fiveRapid.pathEdit?.changed)view.fiveRapid.savePath?.();ops.renderProject(context,view);}else await flushRapidPathSave(context,view,ops);
  }else if(owner==='machine-ik'){
   if(finishMachinePoseHudEdit(context,view,cancel,ops)&&!cancel)await flushMachinePoseEdits(context,view,ops);
  }else if(owner==='fixture'){
   if(cancel||fixtureDraft(view)?.dirty)await fixtureCommand(context,view,cancel?'reset':'apply',ops);
  }else if(owner==='point'){
   if(cancel||view.fiveAxisPointEdit?.dirty)await toolAxisPointRotationCommand(context,view,cancel?'cancel':'apply',ops);
  }else if(owner==='placement')await placementCommand(context,view,cancel?'close':'apply',ops);
  else if(owner==='collider'){if(cancel)cancelMachineColliderValue(context,view,ops);else{await command(context,view,'five.machine.box-value-apply',ops);await flushMachineColliderSaves(context,view);}}
  // A normal asynchronous patch preserves focused typing. An explicit accept
  // or cancel instead adopts this editor's just-rendered, authoritative draft.
  if(target.isConnected&&(cancel||target.value===typedValue))for(const input of target.closest('.five-placement-hud').querySelectorAll('input'))if(cancel||input===target)input.value=input.defaultValue;
 })();
 view.fiveFloatingInputCommit=task;
 void task.catch(error=>{if(context.isCurrentProject?.()!==false&&!view.disposed){view.error=error.message;ops.renderProject(context,view);}}).finally(()=>{
  if(view.fiveFloatingInputCommit!==task)return;
  view.fiveFloatingInputCommit=null;
  if(!view.disposed&&context.isCurrentProject?.()!==false){
   // The accepted native edit and autosave finish before this gate opens.
   // Refresh after clearing it so the HUD and history controls become usable.
   ops.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();
  }
 });return true;
}
function floatingTransformDraft(view){
 const r=view.fiveRapid,h=r?.hudEditing,p=view.fiveMachineIK?.pose,c=view.fiveMachineColliders;
 return !!(h&&h.edit.revision!==h.before.revision||p?.hudEditing&&p.revision!==p.hudEditing.revision||c?.hud?.candidate&&JSON.stringify(c.hud.candidate)!==JSON.stringify(c.drafts.get(c.partId)?.boxes.find(b=>b.id===c.hud.id)));
}
async function inspectFace(context,view,id,ops){
  if(view.disposed||context.isCurrentProject?.()===false||view.pending||view.fixtureRequest)return;
  const support=id.startsWith('five.fixture.design.'),area=view.fiveAxisArea;
  const render=()=>{
    if(view.disposed||context.isCurrentProject?.()===false||view.fiveAxisArea!==area)return;
    const selected=support?view.fixtureDesignSelectedFace:currentDraft(view,'extract').extraction?.selectedFace;
    const prefix=support?'five.fixture.design.select-face:':'five.extraction.select-face.';
    for(const button of context.mount.querySelectorAll('[data-five-face-focus]')){
      if(!button.dataset.fiveCommand?.startsWith(prefix))continue;
      const active=button.dataset.fiveCommand===prefix+selected;
      button.setAttribute('aria-pressed',String(active));button.parentElement.classList.toggle('is-selected',active);
    }
    if(support)highlightDesign(view);else updateCutPreview(view);
    syncSelectionFeedback(context,view);
  };
  // Inspecting a face only updates its row and 3D overlay. Keep the live panel,
  // keyboard focus and other parameter drafts outside the command/save queues.
  if(support)await designCommand(context,view,id.slice('five.fixture.design.'.length),{...ops,renderProject:render});
  else {const [,,action,argument]=id.split('.');await extractionCommand(context,view,action,argument,ops);render();}
}
async function command(context, view, id, ops) {
  if(id.startsWith('five.fixture.light-'))return true;
  if(id.startsWith('five.head-collider.')){await headColliderCommand(context,view,id.slice('five.head-collider.'.length),ops);return true;}
  if(id==='five.head-workspace.use'){await headScopeCommand(context,view,'set',ops);return true;}
  if(id==='five.head-workspace.inherit'){await headScopeCommand(context,view,'inherit',ops);return true;}
  if(id==='five.head-workspace.refresh'){await refreshHeadAssignments(context,view,ops);return true;}
  if(id.startsWith('five.plan-issue.')){await machinePlanIssueCommand(context,view,id.slice('five.plan-issue.'.length),ops,next=>command(context,view,next,ops));return true;}
  if(id==='five.head-instance.machine'){view.fiveSimulation.headOpen=false;await context.actions?.selectRibbonTab?.('simulation');await command(context,view,'five.tool.machine-instances',ops);return true;}
  if(id.startsWith('five.head-instance.')){await headInstanceCommand(context,view,id.slice('five.head-instance.'.length),ops);return true;}
  if(id.startsWith('five.order.strategy.')){if(selectOrderStrategy(view,id.slice('five.order.strategy.'.length)))context.actions?.refreshProductRibbon?.();return true;}
  if(/^five\.(?:fixture\.design\.(?:select-face|focus-face):|extraction\.(?:select-face|focus-face)\.)/.test(id)){await inspectFace(context,view,id,ops);return true;}
  if(id.startsWith('five.rapid-safety.')){selectRapidSafety(view,id==='five.rapid-safety.true');ops.renderProject(context,view);return true;}
  if(id.startsWith('five.rapid-default.')){
    selectRapidDefaultStrategy(view,id.slice('five.rapid-default.'.length));return true;
  }
  // Presentation/cancellation controls must not wait behind edit-save queues
  // or the calculation whose dialog the user is trying to dismiss.
  if(id==='five.operation.cancel'){await cancelBlockingOperation(view);ops.renderProject(context,view);return true;}
  if(id==='five.layer.toggle'){await toolpathLayerCommand(context,view,'toggle',ops);return true;}
  if(id==='five.order.close'){await orderCommand(context,view,'close',ops);return true;}
  if(id==='five.generate.close'||id==='five.generate.cancel'||id.startsWith('five.generate.failure:')){
    await generationCommand(context,view,id.slice('five.generate.'.length),ops);return true;
  }
  if(id==='five.rapid.check'||id==='five.rapid.cancel-check'){
    await rapidCommand(context,view,id.slice('five.rapid.'.length),ops);return true;
  }
  if(id==='five.rapid.path-cancel'){await rapidCommand(context,view,'path-cancel',ops);return true;}
  if(view.fiveFixtureEditRequest&&!/(?:cancel|stop)(?:$|[.:])/.test(id)){await view.fiveFixtureEditRequest;if(view.disposed||context.isCurrentProject?.()===false)return true;}
  const progress=blockingProgress(view);
  if(progress && id!==progress.cancel)return true;
  if(!id.startsWith('five.align.')&&view.fiveAlignment&&(view.fiveAlignment.dirty||view.fiveAlignment.request||view.fiveAlignment.saving)){
    if(!await flushAlignmentEdits(context,view,ops))return true;
  }
  if(id==='five.alerts.toggle'||id==='five.alerts.close'){view.fiveAlertsOpen=id==='five.alerts.close'?false:!view.fiveAlertsOpen;ops.renderProject(context,view);return true;}
  if(id.startsWith('five.alerts.open:')){
    const item=workspaceAlertItems(view).find(item=>item.commandId===id);
    if(!item||item.disabled)return true;
    const target=item.target.commandId,error=view.error;
    view.fiveAlertsOpen=false;
    if(target==='five.tool.collision'){
      if(!view.pending)await command(context,view,target,ops);
      else ops.renderProject(context,view);
    }else await command(context,view,target,ops);
    // Opening one alert is navigation, not acknowledgement of another one.
    if(error&&!view.error){view.error=error;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
    return true;
  }
  if(id==='five.simulation.collision-close'){await simulationCommand(context,view,'collision-close',ops);return true;}
  if(id==='five.points.add'&&view.fiveAxisPointInsert){await toolAxisPointCommand(context,view,'exit-add',ops);return true;}
  if(id==='five.points.delete'&&view.fiveAxisPointDelete){await toolAxisPointCommand(context,view,'exit-delete',ops);return true;}
  if(id==='five.rapid.path-insert'&&view.fiveRapid?.pathEdit?.inserting||id==='five.rapid.path-delete'&&view.fiveRapid?.pathEdit?.deleting){cancelRapidPathMode(view);ops.renderProject(context,view);return true;}
  await finishMachineIK(view);
  if(id==='five.machine.collision-recheck'){
    if(view.fiveMachineCollisionChecking||view.pending||view.fiveMachine?.busy)return true;
    const machineState=view.fiveMachine??={};view.fiveMachineCollisionChecking=true;machineState.busy=true;ops.renderProject(context,view);
    try{
      await view.fiveMachineIK?.editTask;cancelRejectedMachinePoseDrafts(view);cancelRejectedMachineMoves(view);
      await flushMachineParameterEdits(context,view,ops);await flushMachinePoseEdits(context,view,ops);await flushMachineColliderSaves(context,view);
      const machineId=view.fiveMachineCollisionFeedback?.machineId||view.fiveMachine?.machineId,machine=view.scene?.machines?.find(m=>m.entityId===machineId);if(!machine||machine.enabled===false)return true;
      const setup=view.scene.domain?.setups?.find(s=>s.machineSetup?.machineId===machineId),positions=Object.fromEntries(machine.joints.filter(j=>j.type!=='fixed').map(j=>[j.entityId,j.position]));
      const geometrySignature=machineCollisionFeedbackGeometrySignature(view,machineId),sourceSignature=machineCollisionFeedbackSignature(view,machineId);
      const result=await context.sceneProxy.invoke('CamSimulation.CheckMachineMove',{expected:view.scene.domain.expected,...(setup?{setupId:setup.id}:{machineId}),positions},{timeoutMs:60000});
      if(view.disposed||context.isCurrentProject?.()===false)return true;
      if(sourceSignature!==machineCollisionFeedbackSignature(view,machineId))return true;
      setMachineCollisionFeedback(view,machineId,result,{geometrySignature});syncMachineCollisionFeedback(view);
      if(result.collisionStatus==='Unchecked')throw Error(result.reason||'碰撞检查未完成，请重试。');
      if(result.collisionStatus==='Clear')ops.appendProjectLog?.(context,'info','当前机床碰撞检查通过。');
    }finally{view.fiveMachineCollisionChecking=false;machineState.busy=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
    return true;
  }
  if(id.startsWith('five.machine.collision-edit:')){
    const index=Number(id.slice('five.machine.collision-edit:'.length)),target=machineCollisionFeedbackTarget(view,index);
    if(!target)return true;
    await view.fiveMachineIK?.editTask;cancelRejectedMachinePoseDrafts(view);cancelRejectedMachineMoves(view);
    await flushMachineParameterEdits(context,view,ops);await flushMachinePoseEdits(context,view,ops);await flushMachineColliderSaves(context,view);
    let current=machineCollisionFeedbackTarget(view,index);if(!current||current.machineId!==target.machineId||current.ownerElementId!==target.ownerElementId||current.boxIndex!==target.boxIndex||current.boxId!==target.boxId)return true;
    if(activeTool(view)!=='machine-instances'){
      await command(context,view,'five.tool.machine-instances',ops);
      if(!view.scene?.machines&&await ops.refreshSceneState(context,view)===false)return true;
      if(ops.refreshActiveAreaView)await ops.refreshActiveAreaView(context,view,{});
      current=machineCollisionFeedbackTarget(view,index);if(!current||current.machineId!==target.machineId||current.ownerElementId!==target.ownerElementId||current.boxIndex!==target.boxIndex||current.boxId!==target.boxId)return true;
    }
    if(view.fiveMachine?.machineId!==current.machineId)await machineCommand(context,view,'select:'+current.machineId,ops);
    selectMachinePart(context,view,current.ownerElementId,ops,{reveal:true});
    const box=view.fiveMachineColliders?.drafts.get(current.ownerElementId)?.boxes[current.draftIndex];if(!box)return true;
    await command(context,view,'five.machine.box-edit:'+box.id+':bounds',ops);
    focusMachineCollider(view,box.id);
    const row=context.mount.querySelector(`[data-box-id="${box.id}"]`);row?.scrollIntoView?.({block:'nearest'});
    return true;
  }
  if(id!=='five.machine.mount')await flushMachineParameterEdits(context,view,ops);
  await flushRapidPathSave(context,view,ops);
  if(id.startsWith('five.machine-ik.')){stopMachineColliderEdit(view);if(commandAvailability(view,id)?.disabled)return true;if(activeTool(view)!=='machine-instances')await command(context,view,'five.tool.machine-instances',ops);await machineIKCommand(context,view,id.slice(16),ops);return true;}
  if(view.fiveMachineJog?.running)await stopMachineJog(view);
  if(id==='five.tool.machine-definitions'){await machineCommand(context,view,'definitions-open',ops);return true;}
  if(id.startsWith('five.machine-panel.')){
    if(commandAvailability(view,id)?.disabled)return true;
    const action=id.slice(19),definition=action==='import'||action==='delete',instance=['joints','colliders','post'].includes(action);
    if(action==='instantiate'){await machineCommand(context,view,'definitions-open',ops);return true;}
    if(action==='delete'){const d=selectedMachineDefinition(view);if(d)await machineCommand(context,view,'definition-delete:'+d.id,ops);return true;}
    const tool=definition?'machine-definitions':instance?'machine-instances':'machine-simulation';
    if(activeTool(view)!==tool)await command(context,view,'five.tool.'+tool,ops);
    if(instance){
      if(action==='post')await machineCommand(context,view,'part:',ops);
      const pane=context.mount.querySelector('.cam-info-pane'),section=pane?.querySelector(`[data-five-key="machine-${action}"]`);
      if(section&&pane)pane.scrollTop+=section.getBoundingClientRect().top-pane.getBoundingClientRect().top-12;
    }else await machineCommand(context,view,action,ops);
    return true;
  }
  if(id.startsWith('five.machine.')){if(id.startsWith('five.machine.box-edit:')){await flushMachinePoseEdits(context,view,ops);disposeMachineIK(view);}await machineCommand(context,view,id.slice(13),ops);if(id==='five.machine.box-edit-end'){selectMachinePoseTarget(view);ops.renderProject(context,view);}return true;}
  if(commandAvailability(view,id)?.disabled)return true;
  if(view.fiveFixtureLibrary?.choosing&&!id.startsWith('five.fixture.'))closeFixtureChooser(context,view);
  if(id.startsWith('five.workpiece-name.save:')){await saveWorkpieceName(context,view,id.slice('five.workpiece-name.save:'.length),ops);return true;}
  if(id.startsWith('five.preparation.')){await preparationCommand(context,view,id.slice('five.preparation.'.length),ops);return true;}
  if(id.startsWith('five.operation.')){await operationCommand(context,view,id.slice('five.operation.'.length),ops);return true;}
  if(id.startsWith('five.job.')){await jobCommand(context,view,id.slice(9),ops);return true;}
  if(id==='five.tool.export'){await jobCommand(context,view,'open',ops);return true;}
  if(view.fiveJobRequest&&id!=='five.refresh')return true;
  const requested=id.startsWith('five.tool.')?TOOLS[id.slice(10)]?.area:id.startsWith('five.area.')?id.slice(10):null;
  if(requested&&!canEnterArea(view,requested)){await context.actions?.selectRibbonTab?.(workflowArea(view,requested));return true;}
  if(id.startsWith('five.setup.')){await setupCommand(context,view,id.slice(11),ops);return true;}
  if(id.startsWith('five.locating.')){await locatingCommand(context,view,id.slice(14),ops);return true;}
  if(view.fiveSetupRequest||view.fiveLocating?.request){if(id!=='five.refresh')return true;}
  if(id==='workpiece.enable'){await toggleWorkpieceEnabled(context,view,ops);return true;}
  if(view.fiveWorkpieceEnable?.request&&id!=='five.refresh')return true;
  if(id==='workpiece.lock'){await toggleWorkpieceLock(context,view,ops);return true;}
  if(view.fiveWorkpieceLock?.request&&id!=='five.refresh')return true;
  if(id==='workpiece.remove'){await removeWorkpiece(context,view,ops);return true;}
  if(view.fiveWorkpieceRemoval?.request&&id!=='five.refresh')return true;
  if(id.startsWith('five.align.')){await alignmentCommand(context,view,id.slice(11),ops);return true;}
  if((view.fiveAlignment?.request||view.fiveAlignment?.saving)&&id!=='five.refresh')return true;
  if(id.startsWith('five.pose-select.')){await poseSelectionCommand(context,view,id.slice('five.pose-select.'.length),ops);return true;}
  if(view.fivePoseSelection?.active&&!['five.display.numbers','five.refresh'].includes(id)&&!id.startsWith('five.points.select'))await poseSelectionCommand(context,view,'exit',ops);
  if(id.startsWith('five.normals.')){toolAxisDisplayCommand(context,view,id.slice(13),ops);return true;}
  if(view.fiveAxisPointEdit?.rotation?.gesture)return true;
  if(id.startsWith('five.points.')){view.fiveCamInspector='collision';await toolAxisPointCommand(context,view,id.slice(12),ops);return true;}
  if(id==='five.axes.reverse-all'){await flushToolAxisPointEdits(context,view,ops);await reverseToolAxes(context,view,ops);return true;}
  if(id.startsWith('five.axis.')){await toolAxisCommand(context,view,id.slice(10),ops);return true;}
  if(view.fiveToolAxisRequest&&id!=='five.refresh')return true;
  if(view.fiveAxisPointEdit?.dirty&&id!=='five.refresh')await flushToolAxisPointEdits(context,view,ops);
  if(id.startsWith('five.transform.')){await transformParameterCommand(context,view,id.slice(15),ops);return true;}
  if(id.startsWith('five.name.retry:')){await saveCutName(context,view,id.slice(16),ops);return true;}
  if(id==='five.display.numbers'){const ui=workspaceState(view);ui.showPathNumbers=ui.showPathNumbers===false;ops.renderProject(context,view);return true;}
  if(id.startsWith('five.commands.retry:')){await saveCutCommandText(context,view,id.slice(20),ops);return true;}
  if (id.startsWith('five.placement.')) { await placementCommand(context,view,id.slice(15),ops); return true; }
  if (view.fivePlacement?.request && id !== 'five.refresh') return true;
  if (id === 'five.spatial-leads') {
    const doc = context.mount.ownerDocument;
    if (!doc.querySelector('.five-notice-dialog')) {
      const dialog = doc.createElement('dialog');
      dialog.className = 'five-notice-dialog';
      dialog.setAttribute('aria-label', '空间引线');
      dialog.innerHTML = '<strong>空间引线</strong><p>暂未实现</p><form method="dialog"><button class="primary-button" autofocus>确定</button></form>';
      dialog.addEventListener('close', () => dialog.remove(), { once: true });
      doc.body.append(dialog);
      dialog.showModal();
    }
    return true;
  }
  const ui = workspaceState(view);
  if(id.startsWith('five.cam-context.')){
    if(workpieceSelectionBusy(view))return true;
    let placementId=id.slice(id.indexOf(':')+1);const placement=view.scene?.domain?.placements.find(p=>p.id===placementId);
    if(!placement)return true;
    const memory=view.fiveCamPlacements??=new Map(),previous=selectedPlacement(view);if(previous?.setupId)memory.set(previous.workpieceId,previous.id);
    if(id.startsWith('five.cam-context.workpiece:')){
      const eligible=view.scene.domain.placements.filter(p=>p.setupId&&p.workpieceId===placement.workpieceId&&(view.fiveAxisArea==='operation'||view.scene.domain.setups.find(s=>s.id===p.setupId)?.enabled!==false));
      placementId=(eligible.find(p=>p.id===memory.get(placement.workpieceId))||eligible[0]||placement).id;
    }
    await selectWorkpiece(context,view,placementId,ops);const selected=view.scene.domain.placements.find(p=>p.id===placementId);if(!view.error&&selected?.setupId)memory.set(selected.workpieceId,selected.id);return true;
  }
  if(id.startsWith('five.cam-inspector.')){
    const tab=id.slice('five.cam-inspector.'.length);if(!['properties','collision'].includes(tab)||view.pending)return true;
    view.fiveCamInspector=tab;
    if(tab==='collision'){
      pauseSimulation(view);exitToolAxisPointModes(view);
      view.fiveAxisExtractionOpen=false;view.fiveAxisSplitOpen=false;view.fiveAxisHoleOpen=false;
      ui.tools.toolpath=camPathKind(view)==='rapid'?'rapid':'select';
      ops.renderProject(context,view);return true;
    }
    if(camPathKind(view)==='rapid'){ui.tools.toolpath='rapid';ops.renderProject(context,view);return true;}
    exitToolAxisPointModes(view);ui.tools.toolpath='select';
    ops.renderProject(context,view);return true;
  }
  if(id.startsWith('five.cam-process.')){
    const tab=id.slice('five.cam-process.'.length);if(!['parameters','geometry','commands'].includes(tab)||view.pending)return true;
    if(view.fiveAxisExtractionOpen)await extractionCommand(context,view,'close',null,ops);
    switchCamPage(view,'process');view.fiveCamProcessTab=tab;ui.tools.toolpath=tab==='parameters'?'process':'select';
    view.fiveAxisLeadsOpen=false;view.fiveAxisClearOpen=false;view.fiveAxisStartOpen=false;
    ops.renderProject(context,view);if(tab==='parameters')await loadProcessDraft(context,view,ops);return true;
  }
  if(id.startsWith('five.collision-details.')){
    const kind=id.slice('five.collision-details.'.length);if(!['cutting','rapid','all'].includes(kind)||view.pending)return true;
    if(camPage(view)!=='collision')await command(context,view,'five.cam-page.collision',ops);
    exitToolAxisPointModes(view);cancelRapidPathMode(view);
    view.fiveCollisionDetails=kind;
    if(kind!=='all')switchCamPathKind(view,kind);
    ui.tools.toolpath=camPathKind(view)==='rapid'?'rapid':'select';view.fiveCamInspector='collision';
    if(camPathKind(view)==='rapid'&&(!view.fiveRapid?.open||!view.fiveRapid?.id))openRapid(view);
    if(view.fiveSimulation){view.fiveSimulation.repairScope=null;view.fiveSimulation.collisionPathSelection=null;}
    ops.renderProject(context,view);return true;
  }
  if(id.startsWith('five.cam-page.')){
    if(view.pending||view.fiveRapid?.request||view.fiveOrder?.request)return true;
    const page=id.slice('five.cam-page.'.length);if(!CAM_PAGES.some(([id])=>id===page))return true;
    if(view.fiveAxisExtractionOpen)await extractionCommand(context,view,'close',null,ops);
    if(camPage(view)==='head'&&page!=='head')await flushHeadColliderSaves(context,view,ops);
    pauseSimulation(view);switchCamPage(view,page);const rapid=camPathKind(view)==='rapid';
    ui.tools.toolpath=({cutting:'select',rapid:'rapid',process:'process',order:'order',collision:rapid?'rapid':'select',simulation:'simulation',head:'select'})[page];
    if(page==='collision')view.fiveCamInspector='collision';
    if(page==='simulation')view.fiveCamInspector='properties';
    view.fiveCamProcessTab='parameters';
    if(view.fiveOrder){view.fiveOrder.open=false;view.fiveOrder.menu=null;}
    view.fiveAxisLeadsOpen=false;view.fiveAxisClearOpen=false;view.fiveAxisStartOpen=false;
    view.fiveAxisSplitOpen=false;view.fiveAxisHoleOpen=false;view.fiveGenerationOpen=false;
    if(view.fiveAxisPointInsert||view.fiveAxisPointDelete)exitToolAxisPointModes(view);
    view.fiveRapid?.cancel?.();if(view.fiveRapid){view.fiveRapid.running=false;view.fiveRapid.open=false;view.fiveRapid.token++;}
    view.error='';if(rapid)openRapid(view);
    if(view.fiveSimulation)view.fiveSimulation.headOpen=page==='head';
    ops.renderProject(context,view);if(page==='head')await simulationCommand(context,view,'head-open',ops);if(page==='process')await loadProcessDraft(context,view,ops);return true;
  }
  if(id.startsWith('five.tree.')){await cutTreeCommand(context,view,id.slice(10),ops);return true;}
  if(view.fiveTreeRequest&&id!=='five.refresh')return true;
  if(/^five\.simulation\.collision-(check|manual-scope|auto-scope)(:(Cutting|Rapid))?$/.test(id)){
    await command(context,view,'five.tool.collision',ops);
    await simulationCommand(context,view,id.slice(16),ops);return true;
  }
  if(id==='five.simulation.head-open')return command(context,view,'five.cam-page.head',ops);
  if(id.startsWith('five.simulation.')){await simulationCommand(context,view,id.slice(16),ops);return true;}
  if(view.fiveSimulation?.request&&!['five.refresh','five.tool.simulation'].includes(id))return true;
  if(id.startsWith('five.rapid.')){await rapidCommand(context,view,id.slice(11),ops);return true;}
  if(view.fiveRapid?.request&&!['five.refresh','five.tool.rapid','five.tool.settings'].includes(id))return true;
  if(id.startsWith('five.order.')){await orderCommand(context,view,id.slice(11),ops);return true;}
  if(view.fiveOrder?.request&&!['five.refresh','five.tool.manual-order','five.tool.auto-order'].includes(id))return true;
  if(id.startsWith('five.generate.')){await generationCommand(context,view,id.slice(14),ops);return true;}
  if(view.fiveGeneration?.request&&!['five.refresh','five.tool.generate'].includes(id))return true;
  if(view.fiveClear?.request&&!id.startsWith('five.clear.')&&!['five.refresh','five.tool.clear'].includes(id))return true;
  if(id==='five.tool.reverse'){await reverseCommand(context,view,'apply',ops);return true;}
  if(id.startsWith('five.reverse.')){await reverseCommand(context,view,id.slice(13),ops);return true;}
  if(view.fiveReverseRequest&&!['five.refresh','five.tool.reverse'].includes(id))return true;
  if(id.startsWith('five.clear.')){await clearCommand(context,view,id.slice(11),ops);return true;}
  if (id.startsWith('five.fixture.')) { await fixtureCommand(context,view,id.slice(13),ops); return true; }
  if(view.fixtureRequest)return true;
  if (id === 'workpiece.import') { if(!workpieceSelectionBusy(view)){await command(context,view,'five.tool.select',ops);await chooseWorkpiece(context, view, ops);} return true; }
  if (id === 'five.import.cancel') { await cancelImport(context, view, ops); return true; }
  if(id==='five.leads.settings'){if(activeTool(view)!=='leads')await command(context,view,'five.tool.leads',ops);if(!view.pending&&!view.fiveLeads?.request){openLeadSettings(view);ops.renderProject(context,view);}return true;}
  if (id.startsWith('five.leads.')) { await leadCommand(context,view,id.slice(11),ops); return true; }
  if (id.startsWith('five.process.')) { await editProcessCommand(context,view,id.slice(13),ops); return true; }
  if (id.startsWith('five.start.')) { await startCommand(context,view,id.slice(11),ops); return true; }
  if (id.startsWith('five.cut.')) { await editCutCommand(context,view,id.slice(9),ops); return true; }
  if (id.startsWith('five.split.')) { await splitCommand(context,view,id.slice(11),ops); return true; }
  if (id.startsWith('five.layer.')) {await toolpathLayerCommand(context,view,id.slice(11),ops);return true;}
  if (['five.path.merge','five.path.apply'].includes(id)) {await pathEditCommand(context,view,id.slice(10),ops);return true;}
  if(id.startsWith('five.hole-dimensions.apply:')){await saveHoleDimensions(context,view,id.slice('five.hole-dimensions.apply:'.length),ops);return true;}
  if (id.startsWith('five.hole.')) { await standardHoleCommand(context,view,id.slice(10),ops); return true; }
  if (id.startsWith('five.extraction.')) {
    const [, , action, argument] = id.split('.');
    if(action==='begin'){
      if(commandAvailability(view,id)?.disabled)return true;
      view.fiveCamPage='cutting';
      const mode=argument||'Faces';
      if(!view.fiveAxisExtractionOpen||!['extract','manual-extract'].includes(activeTool(view)))await command(context,view,'five.tool.extract',ops);
      if(!view.disposed&&context.isCurrentProject?.()!==false&&!view.pending&&extractionDraft(view).extraction.mode!==mode)await extractionCommand(context,view,'mode',mode,ops);
      if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);return true;
    }
    await extractionCommand(context, view, action, argument, ops);
    if (!view.disposed && context.isCurrentProject?.() !== false) ops.renderProject(context, view);
    return true;
  }
  if (id.startsWith('five.reference.')) {
    const [, , action, prefix] = id.split('.');
    if (action === 'cancel') view.fiveAxisRegistrationPick = null;
    else if (['rough','fine'].includes(activeTool(view))) {
      if (action === 'pick') beginRegistrationPick(view, activeTool(view), prefix);
      else if (action === 'capture') {
        const draft = currentDraft(view), previous = view.fiveAxisRegistrationPick?.prefix;
        draft.capture = prefix; registrationFieldChanged(view, draft, 'capture');
        if (previous) beginRegistrationPick(view, activeTool(view), previous);
      }
      else if (action === 'clear') {
        const draft = currentDraft(view);
        for (const axis of 'XYZ') draft[`${prefix}${axis}`] = '';
        delete draft.referenceQueries[prefix]; registrationFieldChanged(view, draft, `${prefix}X`);
        beginRegistrationPick(view, activeTool(view), prefix);
      }
      else if (action === 'flip') flipRegistrationDirection(view, activeTool(view), prefix);
      else return false;
    }
    ops.renderProject(context, view); return true;
  }
  if (id.startsWith('five.registration.')) {
    const [, , action, tool] = id.split('.');
    if (action === 'cancel') {
      view.fiveAxisRegistrationOpen = false; view.fiveAxisRegistrationPick = null; view.error = '';
      ops.renderProject(context, view); return true;
    }
    if (action === 'confirm' && tool === activeTool(view)) { await confirmRegistration(context, view, tool, ops); return true; }
    if (action === 'load' && tool === activeTool(view)) { await loadRegistrationDraft(context, view, ops, true); return true; }
    if (['rough', 'fine'].includes(tool) && ['solve', 'apply'].includes(action)) {
      await runRegistration(context, view, tool, action === 'apply', ops); return true;
    }
    return false;
  }
  if (id.startsWith('five.area.')) { await context.actions?.selectRibbonTab?.(id.slice(10)); return true; }
  if (id === 'five.refresh') { if (!view.pending) await ops.refreshSceneState(context, view); return true; }
  if (id.startsWith('five.tool.') || id.startsWith('five.rough.')) {
    if(view.fivePathTransform?.gesture||view.fivePathOperationPending)return true;
    const requestedTool = id.startsWith('five.rough.') ? 'rough' : id.slice(10);
    const manualStartMode=requestedTool==='manual-order-current'?'after':requestedTool==='manual-order-start'?'start':null;
    const toolId=manualStartMode?'manual-order':requestedTool;
    if(toolId!=='simulation'&&view.fiveJobPlayback){pauseSimulation(view);view.fiveJobPlayback=null;}
    if(!TOOLS[toolId])return false;
    if(toolId==='manual-order'&&(!manualStartMode||manualStartMode===view.fiveOrder?.mode)&&activeTool(view)==='manual-order'&&view.fiveOrder?.open){await orderCommand(context,view,view.fiveOrder.prefix.length?'apply':'close',ops);return true;}
    if(toolId==='simulation')return command(context,view,'five.cam-page.simulation',ops);
    if(toolId==='collision'){
      if(!['cutting','rapid','collision'].includes(camPage(view)))switchCamPage(view,'collision');
      if(camPathKind(view)==='rapid'&&!view.fiveRapid?.open)openRapid(view);
      return command(context,view,'five.cam-inspector.collision',ops);
    }
    if(toolId==='scale'&&view.scene?.domain?.workpieces?.find(w=>w.id===selectedPlacement(view)?.workpieceId)?.locked)return true;
    if(workpieceLocked({...view,fiveAxisArea:TOOLS[toolId].area,fivePreparationKind:['scale','align','rough'].includes(toolId)?'workpiece':'setup'})&&[...placementTools,'align','rough','fine'].includes(toolId))return true;
    if(placementTools.includes(toolId)){
      if(view.viewport?.transformManipulator?.pointerId!=null)return true;
      if(activeTool(view)===toolId&&view.fivePlacement?.open&&!view.fivePlacement.panel)return true;
    }
    if(view.fiveLeads?.request||view.fiveLeads?.saving)return true;
    if(toolId==='leads'&&activeTool(view)==='leads'){ui.tools.toolpath='select';view.fiveAxisLeadsOpen=false;view.fiveLeads=null;ops.renderProject(context,view);return true;}
    view.fiveAxisLeadsOpen=false;view.fiveLeads=null;
    view.fiveAxisClearOpen=false;if(!view.fiveClear?.request)view.fiveClear=null;
    view.error='';
    if(view.fiveStart?.request&&toolId!=='path-start')return true;
    view.fiveAxisRegistrationPick = null;
    view.fiveAxisExtractionPick = null;
    if(!view.fiveStart?.request)view.fiveStart=null;
    view.fiveAxisStartOpen=false;
    if(view.fiveOrder){view.fiveOrder.open=false;view.fiveOrder.menu=null;}
    if(view.fiveRapid&&!['rapid','settings'].includes(toolId)){view.fiveRapid.cancel?.();view.fiveRapid.open=false;view.fiveRapid.running=false;view.fiveRapid.token++;}
    if (!selectTool(view, toolId)) return false;
    if(toolId==='registration')view.fiveLocatingOpen=true;
    if(toolId!=='align')disposeAlignment(view);
    if(TOOLS[toolId].area==='toolpath'){
      switchCamPage(view,camPageForTool(view,toolId));
      if(camPage(view)==='process'&&toolId!=='process')view.fiveCamProcessTab='geometry';
    }
    if (placementTools.includes(toolId)){openPlacement(view,toolId);if(toolId==='scale'&&view.fivePlacement)Object.assign(view.fivePlacement,{oneShot:true,axis:'xyz'});}
    else if (view.fivePlacement) { view.fivePlacement.open=false;disposePlacementPreview(view); }
    if(toolId==='align')openAlignment(view);
    view.fiveAxisRegistrationOpen = false;
    view.fiveAxisExtractionOpen = ['extract','manual-extract'].includes(toolId);
    view.fiveAxisSplitOpen=false;view.fiveSplit=null;
    view.fiveAxisHoleOpen=false;view.fiveHole=null;
    if(['leads','overcut'].includes(toolId))openLeads(view);
    if(toolId==='clear')openCutClearing(view);
    view.fiveGenerationOpen=toolId==='generate';
    if(['rapid','settings'].includes(toolId))openRapid(view,toolId==='settings');
    if(toolId==='split')openCutSplitting(view);
    if(['path-translate','path-rotate'].includes(toolId)){disposePathTransform(view);openPathEditing(view,toolId.slice(5));}
    if(toolId==='path-hole')openStandardHoles(view);
    if (view.activeAreaId !== TOOLS[toolId].area) await context.actions?.selectRibbonTab?.(TOOLS[toolId].area);
    if(['manual-order','auto-order'].includes(toolId))openOrdering(view,manualStartMode||'start');
    if(toolId==='auto-order'){await orderCommand(context,view,'apply',ops);return true;}
    if(toolId==='generate'){await generationCommand(context,view,'apply',ops);if(!view.error){selectTool(view,'rapid');openRapid(view);}ops.renderProject(context,view);return true;}
    if(toolId==='path-start')openCutStarting(view);
    if(['extract','manual-extract'].includes(toolId))await extractionCommand(context,view,'open',null,ops);
    if (['rough', 'fine'].includes(toolId)) {
      // Restore persisted measurements before starting another selection session.
      if (!currentDraft(view, toolId).registrationDirty) await loadRegistrationDraft(context, view, ops, true);
      if (activeTool(view) !== toolId || context.isCurrentProject?.() === false) return true;
      openRegistration(view, toolId, id.startsWith('five.rough.') ? id.slice(11) : undefined);
    }
  } else if (id.startsWith('five.dock.')) ui.dock = id.slice(10);
  else return false;
  ops.renderProject(context, view);
  if(id==='five.tool.scale'){const input=context.mount.querySelector('.five-scale-operation [data-five-placement-field]');input?.focus({preventScroll:true});input?.select();}
  await loadProcessDraft(context,view,ops);
  await loadCutDraft(context,view,ops);
  return true;
}
function attach(context, view, mount, ops) {
  syncBlockingProgress(context,view,mount,ops);
  if(view.fiveMachineDefinitionsOpen&&view.fiveAxisArea==='simulation')ensureDefinitionHistory(context,view,ops);
  const allowed=workflowArea(view,view.fiveAxisArea);
  if(!view.pending&&!view.fiveWorkflowRedirect&&(allowed!==view.fiveAxisArea||context.requestedRibbonTabId&&context.requestedRibbonTabId!==allowed)){
    view.fiveWorkflowRedirect=true;view.fiveAxisArea=allowed;
    void Promise.resolve().then(()=>context.actions?.selectRibbonTab?.(allowed)).finally(()=>{view.fiveWorkflowRedirect=false;});return;
  }
  if(enabledWorkpiecesOnly(view)&&!view.scene?.activePlacementId&&!view.error&&!view.fiveProcessingSelection&&!workpieceSelectionBusy(view)){
    const next=visibleWorkpiecePlacements(view)[0];
    if(next){view.fiveProcessingSelection=true;void selectWorkpiece(context,view,next.id,ops).finally(()=>{view.fiveProcessingSelection=false;});return;}
  }
  // A viewport resource may become ready after the pre-render pass.
  if(preparePlacement(view)){ops.renderProject(context,view);return;}
  let binding = bindings.get(mount);
  if (!binding) {
    binding = { context, view, ops }; bindings.set(mount, binding);
    binding.disposeCamContextPickers=attachCamContextPickers(mount);
    binding.disposeSupportMenu=attachFixtureDirectionMenu(mount,()=>syncSupportDirectionArrow(binding.view));
    binding.disposeExtractionMenu=attachFixtureDirectionMenu(mount,()=>syncExtractionDirection(binding.view),'.five-extraction-direction-menu');
    binding.disposeFaceListFocus=attachFaceListFocus(mount,id=>{
      const {context:ctx,view:current,ops:activeOps}=binding;if(current.disposed||ctx.isCurrentProject?.()===false||current.pending||current.fixtureRequest)return;
      void command(ctx,current,id,activeOps).catch(error=>{if(ctx.isCurrentProject?.()!==false){current.error=error.message;activeOps.renderProject(ctx,current);}});
    });
    attachMachineColliderBounds(mount,binding);
    mount.addEventListener('focusin',event=>{const {context:ctx,view:current}=binding,owner=event.target.closest?.('.five-placement-hud')?.dataset.transformOwner;beginFixtureInput(ctx,current,event.target);if(owner==='rapid')beginRapidHudEdit(current);if(owner==='machine-ik')beginMachinePoseHudEdit(current);});
    mount.addEventListener('focusout',event=>{const {context:ctx,view:current,ops:activeOps}=binding,hud=event.target.closest?.('.five-placement-hud');if(ctx.isCurrentProject?.()===false)return;if(['name','x','y','z','rx','ry','rz'].includes(event.target.dataset?.fiveFixtureField)){finishFloatingInput(ctx,current,event.target,false,activeOps);return;}if(!hud||hud===event.relatedTarget?.closest?.('.five-placement-hud'))return;finishFloatingInput(ctx,current,event.target,false,activeOps);});
    mount.addEventListener('focusout',event=>{if(event.target.dataset?.fiveHeadColliderField?.startsWith('box:'))scheduleHeadColliderSave(binding.context,binding.view,binding.ops,0);if(event.target.dataset?.fiveMachineField)finishMachineParameterField(binding.view);if(event.target.dataset?.fiveMachineField?.startsWith('ik-pose:'))finishMachinePoseField(binding.context,binding.view,event.target.dataset.fiveMachineField,binding.ops);if(event.target.dataset?.fiveMachineField?.startsWith('box:'))finishMachineColliderField(binding.view);});
    mount.addEventListener('focusout',event=>{if(binding.context.isCurrentProject?.()!==false&&binding.view.fiveRapid?.settings&&event.target.hasAttribute?.('data-five-rapid-field'))binding.view.fiveRapid.savePath?.(0);});
    mount.addEventListener('dblclick',async event=>{
      const part=event.target.closest?.('[data-five-part-select]');
      if(part){const {context:ctx,view:current,ops:activeOps}=binding;if(!event.target.closest('.five-plate-row-actions')&&!current.disposed&&!current.pending&&!current.fixtureRequest&&current.fiveAxisArea==='operation'&&ctx.isCurrentProject?.()!==false){event.preventDefault();await command(ctx,current,'five.fixture.part-select:'+part.dataset.fivePartSelect,activeOps);if(ctx.isCurrentProject?.()!==false&&!current.disposed)focusFixturePart(current,part.dataset.fivePartSelect);}return;}
      const rapid=event.target.closest?.('.five-tree-row[data-five-rapid-id]');
      if(rapid){const {context:ctx,view:current}=binding;if(!rapid.disabled&&!current.disposed&&!current.pending&&camPathKind(current)==='rapid'&&ctx.isCurrentProject?.()!==false){event.preventDefault();focusRapid(current,rapid.dataset.fiveRapidId);}return;}
      const cut=event.target.closest?.('.five-tree-row[data-five-node]');
      if(cut){const {context:ctx,view:current}=binding;if(!cut.disabled&&!current.disposed&&!current.pending&&current.fiveAxisArea==='toolpath'&&ctx.isCurrentProject?.()!==false){event.preventDefault();focusCutNode(current,cut.dataset.fiveNode);}return;}
      const library=event.target.closest?.('[data-five-library-select]');
      if(library){const {context:ctx,view:current,ops:activeOps}=binding;if(ctx.isCurrentProject?.()!==false){event.preventDefault();await fixtureCommand(ctx,current,'library.edit-name:'+library.dataset.fiveLibrarySelect,activeOps);}return;}
      const target=event.target.closest?.('[data-five-command^="five.setup.activate:"], [data-five-setup-label]');if(!target)return;
      const {context:ctx,view:current,ops:activeOps}=binding;if(ctx.isCurrentProject?.()===false||workpieceSelectionBusy(current))return;
      event.preventDefault();const id=target.dataset.fiveSetupLabel||target.dataset.fiveCommand.slice('five.setup.activate:'.length);
      if(selectedPlacement(current)?.id!==id)await setupCommand(ctx,current,'activate:'+id,activeOps);
      if(selectedPlacement(current)?.id===id)await setupCommand(ctx,current,'edit-name',activeOps);
    });
    mount.addEventListener('five-snap-state',()=>{const {context:ctx,view:current,ops:activeOps}=binding;if(!current.disposed&&ctx.isCurrentProject?.()!==false)activeOps.renderProject(ctx,current);});
    attachCutTreeDrag(mount,binding);
    attachPreparationDrag(mount,binding);
    attachFloatingDialogs(mount,binding);
    mount.addEventListener('focusout',event=>{if(event.target.dataset?.fiveMachineField?.startsWith('ik-pose:'))finishMachinePoseField(binding.context,binding.view,event.target.dataset.fiveMachineField,binding.ops);const t=event.target;if(t.hasAttribute?.('data-five-workpiece-name'))void saveWorkpieceName(binding.context,binding.view,t.dataset.fiveWorkpieceName,binding.ops);if(t.hasAttribute?.('data-five-cut-command'))void saveCutCommandText(binding.context,binding.view,t.dataset.fiveCutId,binding.ops);if(t.hasAttribute?.('data-five-cut-name'))void saveCutName(binding.context,binding.view,t.dataset.fiveCutId,binding.ops);
      if(t.hasAttribute?.('data-five-frame-field')){
        const row=t.dataset.fiveFrameField.split('-')[0];
        if(row!==event.relatedTarget?.dataset?.fiveFrameField?.split('-')[0]){
          finishAlignmentField(binding.view,t.dataset.fiveFrameField);
          const values=binding.view.fiveAlignment?.raw[row];if(values)for(const node of mount.querySelectorAll(`[data-five-frame-field^="${row}-"]`))node.value=values['XYZ'.indexOf(node.dataset.fiveFrameField.at(-1))];
          scheduleAlignmentSave(binding.context,binding.view,binding.ops,0);
        }
      }
      if(t.hasAttribute?.('data-five-transform-field')&&event.relatedTarget?.closest?.('[data-five-command="workpiece.lock"],[data-five-command="workpiece.remove"],[data-five-command="workpiece.enable"],[data-command-id="workpiece.lock"],[data-command-id="workpiece.remove"],[data-command-id="workpiece.enable"]')==null)void transformParameterCommand(binding.context,binding.view,'apply',binding.ops);});
    mount.addEventListener('click', async event => {
      const { context: ctx, view: current, ops: activeOps } = binding;
      if(consumeMachineJogClick(current,event))return;
      if (ctx.isCurrentProject?.() === false) return;
      const camContext=event.target.closest?.('[data-five-cam-context-area]');
      if(camContext){
        if(camContext.dataset.fiveCamContextArea!==current.fiveAxisArea){event.preventDefault();return;}
        const picker=event.target.closest('[data-five-cam-context-picker]'),summary=event.target.closest('[data-five-cam-context-picker]>summary');
        if(summary){event.preventDefault();if(workpieceSelectionBusy(current)||summary.getAttribute('aria-disabled')==='true')return;picker.open=!picker.open;positionCamContextPicker(picker);return;}
        const choice=event.target.closest('[data-five-cam-context-choice]');
        if(choice){
          event.preventDefault();const id=camContextCommand(current,camContext.dataset.fiveCamContextArea,choice.dataset.fiveCamContextChoice,choice.dataset.value);if(!id||choice.disabled)return;
          if(picker){picker.open=false;picker.querySelector('summary').focus({preventScroll:true});}
          try{await command(ctx,current,id,activeOps);}catch(error){if(ctx.isCurrentProject?.()!==false){current.error=error.message;activeOps.renderProject(ctx,current);}}return;
        }
      }
      if(event.target.closest?.('.five-fixture-generator summary')&&fixtureDesignBusy(current)){event.preventDefault();return;}
      if(event.target.closest?.('.five-extraction-direction-menu>summary')&&(current.pending||currentDraft(current,'extract').extraction?.confirmationRequest)){event.preventDefault();return;}
      const chooser=event.target.closest?.('[data-five-workpiece-chooser], [data-five-setup-chooser]');
      if(chooser&&workpieceSelectionBusy(current)){event.preventDefault();return;}
      for(const open of mount.querySelectorAll('[data-five-workpiece-chooser][open], [data-five-setup-chooser][open]'))if(open!==chooser)open.open=false;
      if(event.target.closest?.('[data-five-key="fixture-add"]>summary')&&(current.pending||current.fixtureRequest)){event.preventDefault();return;}
      const target = event.target.closest?.('[data-five-workpiece], [data-five-command], [data-five-part-select], [data-five-check], [data-five-group], [data-five-intent], [data-five-order-id], [data-five-rapid-id]');
      if (!target || target.disabled) return;
        if(chooser&&target.dataset.fiveCommand?.startsWith('five.setup.activate:')){chooser.open=false;chooser.querySelector('summary').focus({preventScroll:true});}
      if(target.matches?.('.five-placement-tools button[aria-pressed="true"],.five-motion-switch button[aria-pressed="true"]'))return;
      try {
        if(target.dataset.fiveCommand==='five.rapid.path-cancel'){await command(ctx,current,'five.rapid.path-cancel',activeOps);return;}
        await current.fiveFloatingInputCommit;
        if((current.pending&&!current.fiveCommandSavingId&&!current.fiveNameSavingId)||current.fiveTreeRequest||current.fiveToolAxisRequest){if(!target.hasAttribute('data-five-command'))return;}
        if(!target.hasAttribute('data-five-command')&&current.fivePoseSelection?.active)await poseSelectionCommand(ctx,current,'exit',activeOps);
        if(!target.hasAttribute('data-five-command')&&current.fiveAxisPointEdit?.dirty)await flushToolAxisPointEdits(ctx,current,activeOps);
        if(current.fiveCamPage==='collision'&&(target.hasAttribute('data-five-intent')||target.hasAttribute('data-five-rapid-id'))){
          await flushRapidPathSave(ctx,current,activeOps);
          exitToolAxisPointModes(current);cancelRapidPathMode(current);
          const rapid=target.hasAttribute('data-five-rapid-id');switchCamPathKind(current,rapid?'rapid':'cutting');
          workspaceState(current).tools.toolpath=rapid?'rapid':'select';current.fiveCamInspector='collision';
          if(rapid){if(!current.fiveRapid?.open||!current.fiveRapid?.id)openRapid(current);rapidPick(current,target.dataset.fiveRapidId);}
          else{if(current.fiveRapid)current.fiveRapid.open=false;selectCut(current,target.dataset.fiveIntent);}
          if(current.fiveSimulation){current.fiveSimulation.repairScope=null;current.fiveSimulation.collisionPathSelection=null;}
          activeOps.renderProject(ctx,current);return;
        }
        if(target.hasAttribute('data-five-workpiece')){if(chooser){chooser.open=false;chooser.querySelector('summary').focus({preventScroll:true});}await selectWorkpiece(ctx,current,target.dataset.fiveWorkpiece,activeOps);return;}
        if(target.hasAttribute('data-five-part-select')){await cam.handleRibbonCommand(ctx,'five.fixture.part-select:'+target.dataset.fivePartSelect);return;}
        if(target.hasAttribute('data-five-check')){const key=target.dataset.fiveCheck;if(key.startsWith('g:'))selectCutGroup(current,key.slice(2),{ctrlKey:true});else selectCut(current,key.slice(2),{ctrlKey:true});activeOps.renderProject(ctx,current);return;}
        if(target.hasAttribute('data-five-group')){selectCutGroup(current,target.dataset.fiveGroup,event);activeOps.renderProject(ctx,current);return;}
        if(target.hasAttribute('data-five-rapid-id')){if(activeTool(current)!=='rapid'||!current.fiveRapid?.open)await command(ctx,current,'five.tool.rapid',activeOps);rapidPick(current,target.dataset.fiveRapidId);activeOps.renderProject(ctx,current);return;}
        if(target.hasAttribute('data-five-order-id')){orderingPick(current,target.dataset.fiveOrderId);activeOps.renderProject(ctx,current);return;}
        if (target.hasAttribute('data-five-intent')) {
          if(orderingPick(current,target.dataset.fiveIntent)){activeOps.renderProject(ctx,current);return;}
          if(current.fiveOrder?.request)return;
          if(current.fiveReverseRequest||current.fiveClear?.request)return;
          if(['leads','overcut'].includes(activeTool(current))&&(current.fiveLeads?.request||current.fiveLeads?.saving||current.pending))return;
          if(['path-hole','path-start'].includes(activeTool(current))&&(current.pending||current.fiveHole?.request||current.fiveStart?.request))return;
          selectCut(current,target.dataset.fiveIntent,event);
          activeOps.renderProject(ctx, current);
          if(await applyLeadToCut(ctx,current,target.dataset.fiveIntent,activeOps))return;
          await loadProcessDraft(ctx,current,activeOps);
          await loadCutDraft(ctx,current,activeOps);
        } else await cam.handleRibbonCommand(ctx, target.dataset.fiveCommand);
      } catch (error) {
        if (ctx.isCurrentProject?.() === false) return;
        current.error = error.message; activeOps.renderProject(ctx, current);
      }
    });
    mount.addEventListener('pointerdown',event=>{if(event.target.closest?.('[data-five-order-id]'))event.stopPropagation();},true);
    mount.addEventListener('contextmenu',event=>{const label=event.target.closest?.('[data-five-order-id]');if(label&&orderContextMenu(binding.view,label.dataset.fiveOrderId,event))binding.ops.renderProject(binding.context,binding.view);});
    binding.keydown=ownedProjectKeydown(mount,binding,()=>bindings.get(mount)===binding,event=>{
      const {context:ctx,view:current,ops:activeOps}=binding;
      if(['Enter',' '].includes(event.key)&&event.target.closest?.('.five-fixture-generator summary')&&fixtureDesignBusy(current)){event.preventDefault();return;}
      if(event.isComposing&&['Enter','Escape'].includes(event.key))return;
      if(ctx.isCurrentProject?.()!==false&&mount.contains(event.target)&&camContextKey(event,workpieceSelectionBusy(current)))return;
      const run=promise=>void Promise.resolve(promise).catch(error=>{if(ctx.isCurrentProject?.()!==false&&!current.disposed){current.error=error.message;activeOps.renderProject(ctx,current);}});
      if(event.key==='Enter'&&event.target.dataset?.fiveHeadInstanceField==='name'&&ctx.isCurrentProject?.()!==false){event.preventDefault();run(command(ctx,current,'five.head-instance.rename',activeOps));return;}
      if(event.key==='Escape'&&event.target.dataset?.fiveHeadInstanceField==='name'&&ctx.isCurrentProject?.()!==false){event.preventDefault();run(command(ctx,current,'five.head-instance.rename-cancel',activeOps));return;}
      if(activeTool(current)==='manual-order'&&current.fiveOrder?.open&&['Enter','Escape'].includes(event.key)&&mount.contains(event.target)
        &&!event.repeat&&!event.isComposing&&!event.ctrlKey&&!event.metaKey&&!event.altKey&&!event.shiftKey
        &&!event.target.closest?.('input,textarea,select,[contenteditable]:not([contenteditable="false"]),[role="textbox"]')){
        event.preventDefault();run(command(ctx,current,event.key==='Enter'?'five.order.apply':'five.order.close',activeOps));return;
      }
      const navigation=listKeyboardAction(event,mount);
      if(navigation){
        event.preventDefault();
        if(current.pending||current.fixtureRequest||current.fiveFixtureDesignRequest||current.fiveTreeRequest||current.fiveToolAxisRequest||event.key==='Enter'&&!navigation.focus)return;
        const row=navigation.row;
        if(row.dataset.fiveNode&&cutTreeBusy(current))return;
        revealListRow(row,mount);
        if(row.dataset.fiveFixturePart){run((async()=>{await command(ctx,current,row.dataset.fiveCommand,activeOps);if(navigation.focus&&ctx.isCurrentProject?.()!==false&&!current.disposed)focusFixturePart(current,row.dataset.fiveFixturePart);})());return;}
        if(row.dataset.fiveFaceFocus){run(command(ctx,current,navigation.focus?row.dataset.fiveFaceFocus:row.dataset.fiveCommand,activeOps));return;}
        if(row.dataset.fiveNode){
          if(navigation.focus){focusCutNode(current,row.dataset.fiveNode);return;}
          if(row.dataset.fiveGroup)selectCutGroup(current,row.dataset.fiveGroup);else selectCut(current,row.dataset.fiveIntent);
          activeOps.renderProject(ctx,current);
          run((async()=>{await loadProcessDraft(ctx,current,activeOps);await loadCutDraft(ctx,current,activeOps);})());return;
        }
        if(navigation.focus)focusRapid(current,row.dataset.fiveRapidId);
        else {rapidPick(current,row.dataset.fiveRapidId);activeOps.renderProject(ctx,current);}return;
      }
      const faceFocus=faceFocusShortcut(event,current,mount);if(faceFocus){event.preventDefault();run(command(ctx,current,faceFocus,activeOps));return;}
      if(fixturePartFocusShortcut(event,current,mount)){event.preventDefault();focusFixturePart(current);return;}
      if(rapidFocusShortcut(event,current,mount)){event.preventDefault();focusRapid(current);return;}
      if(cutFocusShortcut(event,current,mount)){event.preventDefault();focusSelectedCuts(current);return;}
      if(event.key==='Escape'&&current.fiveMachineDefinitionsOpen&&current.fiveAxisArea==='simulation'&&ctx.isCurrentProject?.()!==false){event.preventDefault();void machineCommand(ctx,current,'definitions-close',activeOps);return;}
      if(event.key==='Escape'&&current.fivePathTransform?.gesture&&ctx.isCurrentProject?.()!==false){event.preventDefault();current.viewport?.transformManipulator?.finish(true);return;}
      if(ctx.isCurrentProject?.()!==false&&['name','x','y','z','rx','ry','rz'].includes(event.target.dataset?.fiveFixtureField)&&['Enter','Escape'].includes(event.key)){event.preventDefault();finishFloatingInput(ctx,current,event.target,event.key==='Escape',activeOps);return;}
      if(event.key==='Escape'&&ctx.isCurrentProject?.()!==false&&current.fiveFixtureLibrary?.choosing){event.preventDefault();void fixtureCommand(ctx,current,'library.close',activeOps);return;}
      if(event.key==='Escape'&&ctx.isCurrentProject?.()!==false&&activeTool(current)==='fixture'&&current.fiveFixtureDesignOpen&&!current.pending&&!current.fixtureRequest){event.preventDefault();void fixtureCommand(ctx,current,current.fixtureDesignPicking?'design.pick':'design.close',activeOps);return;}
      if(event.key==='Escape'&&ctx.isCurrentProject?.()!==false&&activeTool(current)==='fixture'&&current.fiveFixtureSelectedPart&&!current.fiveFixturePlate?.open&&!current.fiveFixtureBreakdown&&!current.pending&&!current.fixtureRequest){event.preventDefault();run(fixtureCommand(ctx,current,'parts-done',activeOps));return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute?.('data-five-library-field')&&['Enter','Escape'].includes(event.key)){if(event.isComposing)return;event.preventDefault();void fixtureCommand(ctx,current,event.key==='Enter'?'library.rename:'+event.target.dataset.fiveLibraryField:'library.cancel-name',activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute?.('data-five-workpiece-name')&&['Enter','Escape'].includes(event.key)){if(event.isComposing)return;event.preventDefault();const id=event.target.dataset.fiveWorkpieceName;if(event.key==='Escape'){cancelWorkpieceName(current,id);event.target.value=current.scene.domain.workpieces.find(w=>w.id===id)?.name||'';activeOps.renderProject(ctx,current);}else void saveWorkpieceName(ctx,current,id,activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute?.('data-five-setup-name')&&['Enter','Escape'].includes(event.key)){if(event.isComposing)return;event.preventDefault();void setupCommand(ctx,current,event.key==='Enter'?'rename':'cancel-name',activeOps);return;}
      if(current.fiveAlignment?.open&&ctx.isCurrentProject?.()!==false&&mount.contains(event.target)&&!event.defaultPrevented){
        if(event.key==='Escape'){event.preventDefault();void alignmentCommand(ctx,current,'close',activeOps);return;}
        if(event.key==='Enter'&&event.target.hasAttribute?.('data-five-frame-field')){event.preventDefault();finishAlignmentField(current,event.target.dataset.fiveFrameField);activeOps.renderProject(ctx,current);scheduleAlignmentSave(ctx,current,activeOps,0);return;}
      }
      if(ctx.isCurrentProject?.()!==false&&mount.contains(event.target)&&event.target.closest?.('.five-placement-hud')&&['Enter','Escape'].includes(event.key)){
        event.preventDefault();if(event.key==='Escape'&&current.viewport?.transformManipulator?.finish(true))return;finishFloatingInput(ctx,current,event.target,event.key==='Escape',activeOps);return;
      }
      if(ctx.isCurrentProject?.()!==false&&!current.pending&&!current.fixtureRequest&&!current.fiveMachineColliders?.dragging&&!current.fiveMachineIK?.dragging&&current.viewport?.transformManipulator?.pointerId==null){
        const button=transformShortcutButton(event,mount);
        if(button){event.preventDefault();if(button.getAttribute('aria-pressed')!=='true')run(command(ctx,current,button.dataset.fiveCommand,activeOps));return;}
      }
      if(ctx.isCurrentProject?.()!==false&&current.fiveFixtureGizmo&&!event.target.closest?.('input,select,textarea,[contenteditable]:not([contenteditable="false"])')){
        if(event.key==='Escape'&&current.viewport?.transformManipulator?.finish(true)){event.preventDefault();return;}
      }
      if(event.key==='Escape'&&current.fiveMachineIK?.mode&&ctx.isCurrentProject?.()!==false&&!event.target.closest?.('input,select,textarea,[contenteditable]:not([contenteditable="false"])')){event.preventDefault();if(!current.viewport?.transformManipulator?.finish(true)&&!cancelMachinePoseDraft(ctx,current,activeOps))run(machineIKCommand(ctx,current,'close',activeOps));return;}
      if(event.key==='Enter'&&event.target.matches?.('[data-five-collider-value]')&&ctx.isCurrentProject?.()!==false){event.preventDefault();void command(ctx,current,'five.machine.box-value-apply',activeOps);return;}
      if(current.fiveMachineColliders?.mode&&ctx.isCurrentProject?.()!==false&&!event.defaultPrevented&&mount.contains(event.target)&&(!event.target.closest?.('input,textarea,select,[contenteditable]')||event.key==='Escape'&&event.target.matches?.('[data-five-collider-value]'))){
        if(event.key==='Escape'){event.preventDefault();if(current.fiveMachineColliders.boundsFinish){current.fiveMachineColliders.boundsFinish(true);return;}if(!current.viewport?.transformManipulator?.finish(true)){cancelMachineColliderValue(ctx,current,activeOps);}return;}
        if(current.fiveMachineColliders.dragging)return;
        if(transformShortcutMode(event)==='focus'&&!current.pending&&!current.fiveMachine?.busy&&!current.fiveMachineJog?.running){event.preventDefault();focusMachineCollider(current,current.fiveMachineColliders.active);return;}
      }
      const placementMode=transformShortcutMode(event);
      if(['translate','rotate','scale'].includes(placementMode)&&(placementMode==='scale'?current.fiveAxisArea==='workpiece':current.fiveAxisArea==='operation')&&ctx.isCurrentProject?.()!==false&&mount.contains(event.target)
        && !event.defaultPrevented&&!event.repeat&&!event.isComposing&&!event.ctrlKey&&!event.metaKey&&!event.altKey&&!event.shiftKey
        && !event.target.closest?.('input,textarea,select,[contenteditable]:not([contenteditable="false"]),[role="textbox"],[role="dialog"]')
        && ['select',...placementTools].includes(activeTool(current))&&current.scene?.model?.isLoaded
        && !workpieceSelectionBusy(current)&&current.viewport?.transformManipulator?.pointerId==null){
        event.preventDefault();void command(ctx,current,`five.tool.${placementMode}`,activeOps);return;
      }
      if(activeTool(current)==='keypoint'&&ctx.isCurrentProject?.()!==false){
        const d=current.fiveAxisPointEdit,typing=event.target.matches?.('input,textarea,select,[contenteditable="true"]');
        if(event.key==='Escape'&&d?.influence?.picking){event.preventDefault();d.influence.picking=false;d.influence.rangeMode=false;d.influence.rangeStart=null;activeOps.renderProject(ctx,current);return;}
        if(event.key==='Delete'&&!typing&&!event.target.closest?.('.five-influence-graph')){event.preventDefault();void toolAxisPointCommand(ctx,current,'delete-point',activeOps);return;}
      }
      if(activeTool(current)==='rapid'&&current.fiveRapid?.pathEdit&&ctx.isCurrentProject?.()!==false){
        const typing=event.target.matches?.('input,textarea,select,[contenteditable="true"]');
        if(event.key==='Escape'){event.preventDefault();if(current.viewport?.transformManipulator?.finish(true))return;if(cancelRapidPathMode(current))activeOps.renderProject(ctx,current);else void rapidCommand(ctx,current,'path-cancel',activeOps);return;}
        if(event.key==='Delete'&&!typing){event.preventDefault();void rapidCommand(ctx,current,'path-delete-point',activeOps);return;}
        if(event.key==='Enter'&&!event.isComposing&&event.target.matches?.('input[data-five-rapid-field]')){event.preventDefault();run(flushRapidPathSave(ctx,current,activeOps));return;}
      }
      if(event.key==='Escape'&&current.fivePoseSelection?.active&&ctx.isCurrentProject?.()!==false){event.preventDefault();void poseSelectionCommand(ctx,current,'exit',activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&mount.contains(event.target)&&event.target.hasAttribute?.('data-five-point-angle')&&['Enter','Escape'].includes(event.key)){event.preventDefault();void toolAxisPointRotationCommand(ctx,current,event.key==='Enter'?'apply':'cancel',activeOps);return;}
      if(event.key==='Escape'&&activeTool(current)==='keypoint'&&ctx.isCurrentProject?.()!==false){if(current.fiveAxisPointEdit?.rotation?.gesture&&current.viewport?.transformManipulator?.finish(true)){event.preventDefault();return;}if(current.fiveAxisPointInsert||current.fiveAxisPointDelete||current.fiveAxisPointLookup){event.preventDefault();void toolAxisPointCommand(ctx,current,current.fiveAxisPointDelete?'exit-delete':'exit-add',activeOps);return;}}
      if(ctx.isCurrentProject?.()!==false&&mount.contains(event.target)&&event.target.hasAttribute?.('data-five-axis-field')&&['Enter','Escape'].includes(event.key)){event.preventDefault();void toolAxisCommand(ctx,current,event.key==='Enter'?'apply':'cancel',activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&mount.contains(event.target)&&event.target.hasAttribute?.('data-five-transform-field')&&['Enter','Escape'].includes(event.key)){event.preventDefault();void transformParameterCommand(ctx,current,event.key==='Enter'?'apply':'cancel',activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&mount.contains(event.target)&&event.target.hasAttribute?.('data-five-cut-name')&&event.key==='Enter'){event.preventDefault();void saveCutName(ctx,current,event.target.dataset.fiveCutId,activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&mount.contains(event.target)&&workpieceChooserKey(event))return;
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute?.('data-five-point-field')&&['Enter','Escape'].includes(event.key)){event.preventDefault();void toolAxisPointCommand(ctx,current,event.key==='Enter'?'apply':'cancel',activeOps);return;}
      if(event.key==='Enter'&&event.target.matches?.('[data-five-placement-field]')&&current.fivePlacement?.open&&ctx.isCurrentProject?.()!==false){event.preventDefault();void placementCommand(ctx,current,'apply',activeOps);}
      if(event.key==='Escape'&&!event.defaultPrevented&&mount.isConnected&&activeTool(current)==='registration'&&current.fiveLocatingOpen!==false&&ctx.isCurrentProject?.()!==false){event.preventDefault();void locatingCommand(ctx,current,current.fiveLocating?.picking!=null?'cancel-pick':'close',activeOps);return;}
      if(event.key==='Escape'&&current.fivePlacement?.open&&ctx.isCurrentProject?.()!==false){event.preventDefault();void placementCommand(ctx,current,'close',activeOps);}
      if(event.key==='Escape'&&current.fiveAxisRegistrationOpen&&!current.pending&&ctx.isCurrentProject?.()!==false){event.preventDefault();current.fiveAxisRegistrationOpen=false;current.fiveAxisRegistrationPick=null;current.error='';activeOps.renderProject(ctx,current);}
      if(event.key==='Escape'&&current.fiveCamPage!=='head'&&current.fiveSimulation?.headOpen&&ctx.isCurrentProject?.()!==false){event.preventDefault();void simulationCommand(ctx,current,'head-close',activeOps);}
      else if(event.key==='Escape'&&activeTool(current)==='simulation'&&ctx.isCurrentProject?.()!==false){event.preventDefault();void simulationCommand(ctx,current,'pause',activeOps);}
      if(event.key==='Escape'&&['rapid','settings'].includes(activeTool(current))&&ctx.isCurrentProject?.()!==false){event.preventDefault();void rapidCommand(ctx,current,'close',activeOps);}
      if(ctx.isCurrentProject?.()!==false&&ctx.mount.contains(event.target)&&['Enter',' '].includes(event.key)&&event.target.hasAttribute?.('data-five-order-id')){event.preventDefault();orderingPick(current,event.target.dataset.fiveOrderId);activeOps.renderProject(ctx,current);}
      if(event.key==='Escape'&&['manual-order','auto-order'].includes(activeTool(current))&&ctx.isCurrentProject?.()!==false){event.preventDefault();void orderCommand(ctx,current,'close',activeOps);}
      if(event.key==='Escape'&&activeTool(current)==='clear'&&ctx.isCurrentProject?.()!==false){event.preventDefault();void clearCommand(ctx,current,'close',activeOps);}
      if(event.key==='Escape'&&activeTool(current)==='reverse'&&ctx.isCurrentProject?.()!==false){event.preventDefault();void reverseCommand(ctx,current,'close',activeOps);}
      if(event.key==='Escape'&&['leads','overcut'].includes(activeTool(current))&&ctx.isCurrentProject?.()!==false){event.preventDefault();if(activeTool(current)==='leads'&&!current.fiveAxisLeadsOpen){void command(ctx,current,'five.tool.leads',activeOps);}else void leadCommand(ctx,current,'close',activeOps);}
      if(event.key==='Escape'&&activeTool(current)==='path-start'&&ctx.isCurrentProject?.()!==false){
        event.preventDefault();void startCommand(ctx,current,'close',activeOps);
      }
    });
    binding.visibility=()=>{pauseSimulation(binding.view);if(!mount.ownerDocument.hidden)syncSimulation(binding.context,binding.view,binding.ops);};
    mount.addEventListener('input', event => {
      const { context: ctx, view: current, ops: activeOps } = binding;
      if (ctx.isCurrentProject?.() === false) return;
      if(event.target.hasAttribute('data-five-head-collider-field')){if(event.target.tagName!=='SELECT')headColliderField(ctx,current,event.target.dataset.fiveHeadColliderField,event.target.type==='checkbox'?event.target.checked:event.target.value,activeOps);return;}
      if(event.target.hasAttribute('data-five-head-instance-field')){if(event.target.tagName!=='SELECT'){headInstanceField(ctx,current,event.target.dataset.fiveHeadInstanceField,event.target.value,activeOps);activeOps.renderProject(ctx,current);}return;}
      if(event.target.hasAttribute('data-five-extraction-direction')){extractionDirectionChanged(current,Number(event.target.dataset.fiveExtractionDirection),event.target.value);syncExtractionDirection(current);activeOps.renderProject(ctx,current);return;}
      if(event.target.hasAttribute('data-five-hole-dimension')){holeDimensionField(current,event.target.dataset.fiveCutId,event.target.dataset.fiveHoleDimension,event.target.value);activeOps.renderProject(ctx,current);return;}
      if(event.target.hasAttribute('data-five-path-field')){pathEditField(current,event.target.dataset.fivePathField,event.target.value);syncPathTransform(ctx,current,activeOps);return;}
      if(event.target.hasAttribute('data-five-layer-field')){toolpathLayerInput(current,event.target);activeOps.renderProject(ctx,current);return;}
      if(event.target.hasAttribute('data-five-library-field')){fixtureLibraryField(current,event.target.dataset.fiveLibraryField,event.target.value);return;}
      if(event.target.hasAttribute('data-five-workpiece-name')){editWorkpieceName(current,event.target.dataset.fiveWorkpieceName,event.target.value);ctx.actions?.refreshProjectHistoryControls?.();return;}
      if(event.target.hasAttribute('data-five-setup-name')){currentDraft(current,'setup').setupName=event.target.value;ctx.actions?.refreshProjectHistoryControls?.();return;}
      if(event.target.hasAttribute('data-five-locating-field')){locatingField(current,event.target.dataset.fiveLocatingField,event.target.value);activeOps.renderProject(ctx,current);return;}
      if(event.target.hasAttribute('data-five-point-angle')){toolAxisPointAngleChanged(ctx,current,event.target.value,activeOps);return;}
      if(event.target.hasAttribute('data-five-point-field')&&event.target.tagName!=='SELECT'){toolAxisPointField(current,event.target.dataset.fivePointField,event.target.value);activeOps.renderProject(ctx,current);return;}
      if(event.target.hasAttribute('data-five-axis-field')&&event.target.tagName!=='SELECT'){toolAxisFieldChanged(current,event.target.dataset.fiveAxisField,event.target.value);activeOps.renderProject(ctx,current);return;}
      if(event.target.dataset.fiveMachineField){if(event.target.closest?.('.five-placement-hud[data-transform-owner="machine-ik"]'))beginMachinePoseHudEdit(current);machineField(ctx,current,event.target.dataset.fiveMachineField,event.target.type==='checkbox'?event.target.checked:event.target.value,activeOps);return;}
      if(event.target.hasAttribute('data-five-transform-field')){
        const d=current.fiveAlignment;if(d){if(d.dirty||d.saving||d.request||d.selecting||d.picking||Object.keys(d.fieldErrors).length)return;disposeAlignment(current);selectTool(current,'select');}
        transformParameterChanged(current,event.target.dataset.fiveTransformField,event.target.value);activeOps.renderProject(ctx,current);return;
      }
      if(event.target.hasAttribute('data-five-frame-field')){alignmentFieldChanged(current,event.target.dataset.fiveFrameField,event.target.value);activeOps.renderProject(ctx,current);scheduleAlignmentSave(ctx,current,activeOps);return;}
      if(event.target.hasAttribute('data-five-cut-command')){editCutCommandText(current,event.target.dataset.fiveCutId,event.target.dataset.fiveCutCommand,event.target.value);return;}
      if(event.target.hasAttribute('data-five-cut-name')){editCutName(current,event.target.dataset.fiveCutId,event.target.value);return;}
      if(event.target.dataset.fiveColliderValue){machineColliderValueChanged(ctx,current,event.target.dataset.fiveColliderValue,event.target.value,activeOps);return;}
      if(event.target.dataset.fivePlacementField){placementFieldChanged(current,event.target.dataset.fivePlacementField,event.target.value);activeOps.renderProject(ctx,current);return;}
      if(event.target.dataset.fiveSimulationField){
        // A native select emits input and change. Commit a model choice once,
        // after selection, rather than queuing the same save a second time.
        if(!['builtin-head','head-choice'].includes(event.target.dataset.fiveSimulationField))simulationField(ctx,current,event.target.dataset.fiveSimulationField,event.target.value,activeOps);
        return;
      }
      if(event.target.dataset.fiveRapidField&&event.target.tagName!=='SELECT'){if(event.target.closest?.('.five-placement-hud[data-transform-owner="rapid"]'))beginRapidHudEdit(current);rapidFieldChanged(current,event.target.dataset.fiveRapidField,event.target.value);activeOps.renderProject(ctx,current);return;}
      if(event.target.dataset.fiveLeadField){leadFieldChanged(ctx,current,event.target.dataset.fiveLeadField,event.target.value,activeOps);return;}
      if (event.target.hasAttribute('data-five-search')) {
        workspaceState(current).search = event.target.value;
        activeOps.renderProject(ctx, current);
      }
      const name = event.target.dataset.fiveField;
      const fixtureField = event.target.dataset.fiveFixtureField;
      if (fixtureField) { fixtureFieldChanged(current,fixtureField,event.target.value,event.target,ctx); if(fixtureField==='clearance'||fixtureField.startsWith('design:'))activeOps.renderProject(ctx,current);else if(fixtureField!=='name')syncFixturePlacement(ctx,current,activeOps);ctx.actions?.refreshProjectHistoryControls?.(); }
      if (name && Object.hasOwn(currentDraft(current), name)) {
        const draft = currentDraft(current); draft[name] = event.target.value; draft.edited = true;
        processDraftChanged(current,name);
        cutDraftChanged(current,name);
        if (['rough', 'fine'].includes(activeTool(current)) && name !== 'note') {
          registrationFieldChanged(current, draft, name); activeOps.renderProject(ctx, current);
        }
        if (['extract', 'manual-extract'].includes(activeTool(current)) && name.startsWith('extraction')) {
          extractionChanged(current); activeOps.renderProject(ctx, current);
        }
      }
    });
    mount.addEventListener('change', event => {
      const { context: ctx, view: current, ops: activeOps } = binding;
      if(event.target.tagName==='SELECT'&&event.target.hasAttribute('data-five-cam-context-field')){
        if(ctx.isCurrentProject?.()===false)return;
        const area=event.target.closest('[data-five-cam-context-area]')?.dataset.fiveCamContextArea,id=camContextCommand(current,area,event.target.dataset.fiveCamContextField,event.target.value);
        if(id)void command(ctx,current,id,activeOps).catch(error=>{if(ctx.isCurrentProject?.()!==false){current.error=error.message;activeOps.renderProject(ctx,current);}});else activeOps.renderProject(ctx,current);
        return;
      }
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute('data-five-operation-field')){void operationField(ctx,current,event.target.dataset.fiveOperationField,event.target.value,activeOps);return;}
      if(event.target.hasAttribute('data-five-head-scope')){headScopeField(ctx,current,event.target.value,activeOps);return;}
      if(event.target.hasAttribute('data-five-head-collider-field')){if(ctx.isCurrentProject?.()!==false&&event.target.tagName==='SELECT')headColliderField(ctx,current,event.target.dataset.fiveHeadColliderField,event.target.value,activeOps);return;}
      if(event.target.hasAttribute('data-five-head-instance-field')){if(ctx.isCurrentProject?.()!==false&&event.target.tagName==='SELECT'){headInstanceField(ctx,current,event.target.dataset.fiveHeadInstanceField,event.target.value,activeOps);activeOps.renderProject(ctx,current);}return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute('data-five-extraction-scope')){const group=event.target.closest('[data-five-key="extraction-scope"]'),inner=group.querySelector('[value="inner"]').checked,outer=group.querySelector('[value="outer"]').checked;void command(ctx,current,'five.extraction.scope.'+(inner&&outer?'all':inner?'inner':outer?'outer':'none'),activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&(event.target.hasAttribute('data-five-layer-field')||event.target.hasAttribute('data-five-layer-target'))){void toolpathLayerChange(ctx,current,event.target,activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute('data-five-path-field')){pathEditField(current,event.target.dataset.fivePathField,event.target.value);syncPathTransform(ctx,current,activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.matches?.('[data-five-machine-instance]')){void command(ctx,current,'five.machine.select:'+event.target.value,activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.dataset.fiveMachineField){const key=event.target.dataset.fiveMachineField;if(key.startsWith('joint:'))finishMachineParameterField(current);else if(event.target.tagName==='SELECT'){machineField(ctx,current,key,event.target.value,activeOps);activeOps.renderProject(ctx,current);}return;}
      // Text edits are handled by input. A change emitted while patching away a
      // focused field must not re-enter the panel patch during its removal.
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute('data-five-point-field')){if(event.target.tagName!=='SELECT')return;const field=event.target.dataset.fivePointField;if(field==='point')void toolAxisPointCommand(ctx,current,'select',activeOps,event.target.value);else{toolAxisPointField(current,field,event.target.value);activeOps.renderProject(ctx,current);}return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.hasAttribute('data-five-axis-field')){if(event.target.tagName!=='SELECT')return;toolAxisFieldChanged(current,event.target.dataset.fiveAxisField,event.target.value);activeOps.renderProject(ctx,current);return;}
      if(event.target.hasAttribute('data-five-default-strategy')&&ctx.isCurrentProject?.()!==false){void command(ctx,current,`five.rapid-default.${event.target.value}`,activeOps).catch(error=>{current.error=error.message;activeOps.renderProject(ctx,current);});return;}
      if(event.target.hasAttribute('data-five-rapid-safe')&&ctx.isCurrentProject?.()!==false){void command(ctx,current,`five.rapid-safety.${event.target.checked}`,activeOps).catch(error=>{current.error=error.message;activeOps.renderProject(ctx,current);});return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.dataset.fiveSimulationField){simulationField(ctx,current,event.target.dataset.fiveSimulationField,event.target.value,activeOps);return;}
      if(ctx.isCurrentProject?.()!==false&&event.target.dataset.fiveRapidField){if(event.target.tagName!=='SELECT')return;rapidFieldChanged(current,event.target.dataset.fiveRapidField,event.target.value);activeOps.renderProject(ctx,current);return;}
      if(event.target.hasAttribute('data-five-clear-select')&&ctx.isCurrentProject?.()!==false&&!current.pending&&!current.fiveClear?.request){
        const ui=workspaceState(current),id=event.target.value;if(current.scene.intentToolpaths?.find(c=>c.id===id)?.locked)return;ui.selectedGroupId='';ui.selectedGroupIds=[];ui.selectedIntentIds=id?[id]:[];ui.selectedIntentId=id;activeOps.renderProject(ctx,current);return;
      }
      if(event.target.hasAttribute('data-five-reverse-select')&&ctx.isCurrentProject?.()!==false&&!current.pending&&!current.fiveReverseRequest){
        const ui=workspaceState(current),id=event.target.value;if(current.scene.intentToolpaths?.find(c=>c.id===id)?.locked)return;ui.selectedGroupId='';ui.selectedGroupIds=[];ui.selectedIntentIds=id?[id]:[];ui.selectedIntentId=id;activeOps.renderProject(ctx,current);return;
      }
      if(ctx.isCurrentProject?.()!==false&&event.target.dataset.fiveHoleField) {
        standardHoleChanged(current,event.target.dataset.fiveHoleField,event.target.value);activeOps.renderProject(ctx,current);return;
      }
      const name = event.target.dataset.fiveField;
      if (ctx.isCurrentProject?.() === false || !name || !Object.hasOwn(currentDraft(current), name)) return;
      const draft = currentDraft(current); draft[name] = event.target.value; draft.edited = true;
      if (['rough', 'fine'].includes(activeTool(current)) && name !== 'note') registrationFieldChanged(current, draft, name);
      // Text is already in the local draft. Re-render only condition selectors;
      // a blur/change emitted during node removal must not re-enter a DOM patch.
      if (event.target.tagName === 'SELECT') activeOps.renderProject(ctx, current);
    });
  }
  Object.assign(binding, { context, view, ops, area: view.activeAreaId, sceneProxy: context.sceneProxy });
  const placement=selectedPlacement(view),placements=view.scene?.domain?.placements||[];
  const needsSource=view.fiveAxisArea==='workpiece'&&placement?.setupId&&placements.some(p=>!p.setupId&&p.workpieceId===placement.workpieceId);
  const owner=placement?.workpieceId||placements.find(p=>p.id===view.scene?.domain?.activePlacementId)?.workpieceId;
  const needsOperation=['operation','toolpath','simulation'].includes(view.fiveAxisArea)&&!placement?.setupId&&placements.some(p=>p.setupId&&p.workpieceId===owner&&(view.fiveAxisArea==='operation'||view.scene.domain.workpieces.some(w=>w.id===p.workpieceId&&w.enabled!==false)&&view.scene.domain.setups.some(s=>s.id===p.setupId&&s.enabled!==false)));
  const areaSyncKey=JSON.stringify([view.fiveAxisArea,view.scene?.domain?.expected,placement?.id]);
  if(!view.error&&!view.fiveOperationCreateDialog&&!view.fiveOperationNavigationPending&&!view.fiveOperationAreaSync&&view.fiveOperationAreaSyncKey!==areaSyncKey&&!workpieceSelectionBusy(view)&&(needsSource||needsOperation)){
    const area=view.fiveAxisArea;view.fiveOperationAreaSync=true;view.fiveOperationAreaSyncKey=areaSyncKey;
    void Promise.resolve().then(()=>{
      if(!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveAxisArea===area)return prepareOperationArea(context,view,ops,area);
    }).catch(error=>{if(!view.disposed&&context.isCurrentProject?.()!==false)view.error=error.message;}).finally(()=>{
      view.fiveOperationAreaSync=false;
      if(!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveAxisArea===area)ops.renderProject(context,view);
    });return;
  }
  activateProjectDocumentBinding(mount,binding,()=>attachMachineJog(mount,binding));
  syncFloatingDialogs(mount,binding);
  syncOperationCreateDialog(context,view,mount,ops);
  hydrateWorkpieceThumbnails(context,view);
  ensureFixtureUnitDetails(context,view,ops);
  for(const picker of mount.querySelectorAll('[data-ribbon-workspace-picker][open]'))positionRibbonWorkspacePicker(picker);
  for(const picker of mount.querySelectorAll('[data-five-cam-context-picker][open]'))positionCamContextPicker(picker);
  flushWaitingWorkpieceNames(context,view,ops);
  flushWaitingCommands(context,view,ops);
  context.actions?.refreshProjectHistoryControls?.();
  ensureFixtureLibraryHistory(context,view,ops);
  mount.querySelector('.five-workspace')?.setAttribute('data-head-workspace',String(view.fiveAxisArea==='toolpath'&&camPage(view)==='head'));
  mount.querySelector('.five-workspace')?.setAttribute('data-cam-page',String(view.fiveAxisArea==='toolpath'));
  mount.querySelector('.five-workspace')?.setAttribute('data-positioning', String(['select', 'rough', 'fine', ...placementTools].includes(activeTool(view))));
  mount.querySelector('.five-workspace')?.setAttribute('data-generating',String(activeTool(view)==='generate'));
  mount.querySelector('.five-workspace')?.setAttribute('data-fixture-design',String(view.fiveAxisArea==='operation'&&activeTool(view)==='fixture'&&!!view.fiveFixtureDesignOpen));
  mount.querySelector('.five-workspace')?.setAttribute('data-preparation-kind',preparationKind(view));
  mount.querySelector('.five-workspace')?.setAttribute('data-trajectory-simulation',String(activeTool(view)==='simulation'));
  mount.querySelector('.five-workspace')?.setAttribute('data-machine-program-open',String(machineViewportActive(view)&&!!view.fiveMachine?.programOpen&&view.fiveMachine?.programReport===view.fiveMachine?.report));
  mount.querySelector('.five-workspace')?.setAttribute('data-machine-editing',String(view.fiveAxisArea==='simulation'&&['machine-definitions','machine-instances'].includes(activeTool(view))));
  mount.querySelector('.five-workspace')?.setAttribute('data-generated-state',generatedGeometry(view)?.status||'Missing');
  mount.querySelector('.five-workspace')?.setAttribute('data-path-numbers',String(workspaceState(view).showPathNumbers!==false));
  mount.querySelector('.five-workspace')?.setAttribute('data-position-open', String(!!view.fiveAxisRegistrationOpen));
  mount.querySelector('.five-workspace')?.setAttribute('data-starting',String(activeTool(view)==='path-start'&&!!view.fiveAxisStartOpen));
  mount.querySelector('.five-workspace')?.setAttribute('data-extracting',String(['extract','manual-extract','split','path-hole','path-start','leads','overcut','reverse','clear','generate','manual-order','auto-order','rapid','settings'].includes(activeTool(view))));
  mount.querySelector('.five-workspace')?.setAttribute('data-extraction-open',String(['rapid','settings'].includes(activeTool(view))?!!view.fiveRapid?.open:['manual-order','auto-order'].includes(activeTool(view))?!!view.fiveOrder?.open:activeTool(view)==='generate'?!!view.fiveGenerationOpen:activeTool(view)==='clear'?!!view.fiveAxisClearOpen:activeTool(view)==='reverse'?!!view.fiveAxisReverseOpen:['leads','overcut'].includes(activeTool(view))?!!view.fiveAxisLeadsOpen:activeTool(view)==='path-start'?false:activeTool(view)==='path-hole'?!!view.fiveAxisHoleOpen:activeTool(view)==='split'?!!view.fiveAxisSplitOpen:!!view.fiveAxisExtractionOpen));
  if(view.fiveCutAnnotations&&!(view.fiveAxisArea==='toolpath'&&view.scene.intentToolpaths?.length)){
    view.viewport?.setSpecificationAnnotations([]);view.fiveCutAnnotations=false;
  }
  updateRegistrationAnnotations(view);
  syncRegistrationInteraction(context, view);
  syncWorkpieceViewDefinition(context,view,ops);
  syncCompensation(context,view,ops);
  updateCutPreview(view);
  updateGeneratedPreview(view);
  syncSimulation(context,view,ops);
  syncCuttingHeadPreview(context,view,ops);
  attachFixturePlateEditor(context,view,ops);
  syncMachine(context,view,ops);
  syncMachineJog(context,view);
  syncIntentGeneration(context,view,ops);
  syncMachineTypeAssignments(context,view,ops);
  syncLeads(context,view,ops);
  highlightFixture(view);
  syncFixturePresentation(view);
  if(activeTool(view)!=='keypoint')disposeToolAxisPointRotation(view);
  syncPlacementInteraction(context,view,ops);
  if(syncAlignment(context,view)){ops.renderProject(context,view);return;}
  syncLocating(context,view);
  syncToolAxisParameters(context,view,ops);
  syncToolAxisPoints(context,view,ops);
  syncToolAxisPointRotation(context,view,ops);
  syncToolAxisDisplay(context,view,ops);
  syncPoseSelection(context,view);
  syncRapidPathEditing(context,view,ops);
  syncExtractionDirection(view);
  syncFixturePlacement(context,view,ops);
  syncSelectionFeedback(context,view);
  syncMachineIK(context,view,ops);
  syncMachineColliderEdit(context,view,ops);
  syncMachineCollisionFeedback(view);
  syncMachineAssemblyPlacement(context,view,ops);
  syncMachinePlanRecovery(view);
  syncMachinePlanIssue(view);
  syncPathTransform(context,view,ops);
  if(geometryEditing(view)||view.fiveAxisArea==='toolpath'){
    view.selectedSceneObjectId='';
    view.viewport?.setSelectedObjectIds?.([]);
  }
  const definition=getRibbonDefinition(context);
  const signature = JSON.stringify([view.pending, view.scene?.model?.isLoaded, view.scene?.selection?.id, activeTool(view), currentDraft(view, 'rough').roughMethod, definition]);
  if (signature !== binding.ribbonSignature) { binding.ribbonSignature = signature; context.actions?.refreshProductRibbon?.(); }
  syncRibbonCommandAvailability(mount.closest('.workbench')?.querySelector('.product-ribbon'),definition);
  const fixtureDesignHistoryBusy=!!view.fiveFixtureDesignRequest;
  if(binding.fixtureDesignHistoryBusy!==fixtureDesignHistoryBusy){binding.fixtureDesignHistoryBusy=fixtureDesignHistoryBusy;context.actions?.refreshProjectHistoryControls?.();}
  syncAlertPanel(context,view,binding);
  const historySignature=JSON.stringify(view.scene?.domain?.expected);
  if(!view.pending&&historySignature&&historySignature!==binding.historySignature){
    binding.historySignature=historySignature;
    Promise.resolve(context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId})).catch(error=>{
      if(!view.disposed&&context.isCurrentProject?.()!==false)ops.appendProjectLog(context,'error',`更新撤销状态失败：${error.message}`);
    });
  }
  void loadRegistrationDraft(context, view, ops);
}
function decorate(context) {
  const view = getProjectView(context.project?.projectId);
  return { ...context,
    isCurrentProject: () => !view.disposed && context.isCurrentProject?.() !== false,
    // A response updates the currently visible area, never the area at request time.
    get activeRibbonTabId() { return workflowArea(view,view.fiveAxisArea); },
    get requestedRibbonTabId(){return view.fiveRequestedRibbonTabId;},
    normalizeAreaId: id=>workflowArea(view,id),
    forceThreeViewport: true,
    showViewportGrid: true,
    viewportOptions: {
      showColliders:false,
      // Screen-resolution depth samples avoid MSAA coverage leakage without
      // forcing four times the pixel load on ordinary-DPI displays.
      antialias: false, minimumPixelRatio: 1, pixelRatioCap: 2,
      // Fixture role colors also cover existing and imported fixture units.
      // Workpieces retain their source material and vertex colors.
      meshColorRGBA: instance => fixtureMeshColor(view,instance),
      isObjectPickable: instance => fixturePickable(view,instance),
      meshEdgeDepthBiasPixels: instance => view.scene?.domain?.fixtures?.some(f=>f.id===instance?.objectId) ? 1 : 0,
      constrainOrbit: false,
      getCameraNavigationMode: event => event.button === 2 ? 'orbit' : event.button === 1 ? 'pan' : null,
      orbitAxisSnap: true,
      orbitPivotPicking: true,orbitPivotRadiusPx:6,
      getOrbitFallbackCenter:()=>{
        const object=view.viewport?.sceneObjects.get(view.scene?.activePlacementId);
        if(!object?.visible||!object.geometry)return null;
        if(!object.geometry.boundingBox)object.geometry.computeBoundingBox();object.updateWorldMatrix(true,false);
        return object.geometry.boundingBox.getCenter(object.position.clone()).applyMatrix4(object.matrixWorld).toArray();
      },
      showProjectionToggle: true, showTransformSpaceToggle: true, showOrientationGizmo:false, transformSpace: view.fivePlacementSpace ?? 'world',
      viewCubeAppearance: { beveled:true, compass:true, triad:true,
        faceLabels:{front:'前视',back:'后视',left:'左视',right:'右视',top:'顶视',bottom:'底视'} },
      onTransformSpaceChange: async space => {
        const b=bindings.get(context.mount);if(!b||b.context.isCurrentProject?.()===false)return;
        try {if(activeTool(view)==='keypoint')toolAxisPointSpaceChanged(view,space);else if(activeTool(view)==='machine-instances'&&!view.fiveMachineColliders?.mode)await machinePoseSpaceChanged(b.context,view,space,b.ops);else {machineColliderSpaceChanged(view);placementSpaceChanged(view,space);}}catch(error){view.error=error.message;view.viewport.setTransformSpace(activeTool(view)==='machine-instances'?view.fivePlacementSpace||'world':view.fivePlacement?.space||view.fivePointRotationSpace||'world');}
        b.ops.renderProject(b.context,view);
      },
      hoverDetail: 'surface', hoverEnabled: false,
      showWorldAxes: true, worldAxisLength: 240, worldAxisLabels: true,
      worldAxisPixelSize: 48, worldAxisOpacity: .45,
      gridSize: 2000, gridDivisions: 40, blankDoubleClickFitEnabled: true, immediateBlankPick: true,
      onBlankContextClick: () => {
        const binding=bindings.get(context.mount);
        if(binding?.view===view&&binding.context.isCurrentProject?.()!==false&&view.fiveMachineColliders?.mode){cancelMachineColliderValue(binding.context,view,binding.ops);return;}
        if(binding?.view===view&&binding.context.isCurrentProject?.()!==false&&placementTools.includes(activeTool(view)))
          void placementCommand(binding.context,view,'close',binding.ops);
      },
      showDisplayModeControl: true, displayMode: 'shadedEdges', showSurfaceEdges: true,
      wireframeColor: 0x8fb8c9 },
    beforeProjectRender: (_ctx,current,mount) => {syncBlockingProgress(_ctx,current,mount,null,{beforeRender:true});if(current.scene?.domain){
      projectWorkpiece(current.scene,current.fiveActivePlacementId,enabledWorkpiecesOnly(current));
      if(current.fiveBoundPlacementId!==current.scene.activePlacementId){resetWorkpieceTools(current);current.fiveBoundPlacementId=current.scene.activePlacementId;}
      current.fiveActivePlacementId=current.scene.activePlacementId;
    }current.fiveAxisArea=workflowArea(current,current.fiveAxisArea);syncOperationPreparationKind(current);prepareToolAxisPointModes(current);preparePlacement(current);preparePoseSelection(current);},
    noticePresentation: 'log',
    pendingPresentation: 'explicit',
    areaTitleOverrides: Object.fromEntries(AREAS.map(item => [item.id, item.title])),
    areaRenderers: Object.fromEntries(AREAS.map(item => [item.id, { left: renderLeft, right: renderRight }])),
    resolveAreaViewDefinition: (_ctx,current) => activeWorkpieceView(current),
    resolveWorkbenchPresentation: (_ctx, _view, _scene, area) => ({ className: 'five-workspace', attributes: `data-five-area="${area}"` }),
    resolveViewportBackgroundColor: () => 0x13252d,
    renderWorkbenchPrefix: (_ctx,current) => `<div data-five-summary hidden></div><div data-five-alert-host>${renderWorkspaceAlerts(current)}</div>`,
    renderViewportOverlay: (ctx, current) => `<div class="five-overlay" data-five-overlay>${renderOverlay(ctx, current)}</div>`,
    renderWorkbenchSuffix: (ctx, current) => `<section class="five-dock" data-five-dock aria-label="加工顺序和检查问题">${renderDock(ctx, current)}</section>`,
    handleAreaRibbonCommand: command,
    handleAreaViewportHover: (...args) => {
      const handled=handleLocatingHover(...args)||handleAlignmentHover(...args)||handlePoseSelectionHover(...args)||handleRegistrationHover(...args);
      return handleSelectionFeedbackHover(...args)||handled;
    },
    getAreaViewportRectangleSelectionKey: (ctx,current) => current.fivePoseSelection?.active||current.fiveAxisArea==='toolpath'&&activeTool(current)==='keypoint'?null:extractionRectangleSelectionKey(ctx,current) ?? (current.fiveAxisExtractionOpen?null:cutRectangleSelectionKey(ctx,current)),
    handleAreaViewportRectangleSelect: (ctx,current,...args) => current.fivePoseSelection?.active||current.fiveAxisArea==='toolpath'&&activeTool(current)==='keypoint'?undefined:current.fiveAxisExtractionPick?handleExtractionRectangleSelect(ctx,current,...args):handleCutRectangleSelect(ctx,current,...args),
    handleAreaViewportPick: (ctx,current,...args) => {
      if(handleMachinePartPick(ctx,current,args[0],args[1],args[4]))return true;
      if(handleMachineAssemblyPick(ctx,current,args[0],args[1],args[4]))return true;
      if(handlePoseSelectionPick(ctx,current,...args))return true;
      if(current.fiveAxisArea==='toolpath'&&activeTool(current)==='keypoint'){
        handleToolAxisPointPick(ctx,current,...args);return true;
      }
      const handled=handleLocatingPick(ctx,current,...args)||handleAlignmentPick(ctx,current,...args)||fixturePicked(ctx,current,...args)||handleRapidPick(ctx,current,...args)||handleOrderPick(ctx,current,...args)||handleSplitPick(ctx,current,...args)||handleStartPick(ctx,current,...args)||handleExtractionPick(ctx,current,...args)||handleRegistrationPick(ctx,current,...args)||handleWorkpiecePick(ctx,current,...args)||handleCutLinePick(ctx,current,...args);
      // CAM owns picking even when no path is hit; never select the reference solid.
      return handled||current.fiveAxisArea==='toolpath';
    },
    tryRenderProjectPatch(ctx, current, mount) {
      const previous = bindings.get(mount);
      if (previous?.view !== current || previous.area !== current.activeAreaId || previous.sceneProxy !== ctx.sceneProxy) return false;
      return patchWorkspacePanels(mount, sections(ctx, current));
    },
    afterProjectRender: attach,
    afterProjectPatch: attach,
  };
}
export function mountProduct(context) { styles(); return cam.mountProduct(context); }
export function beforeSelectRibbonTab(context,tabId){
  const view=getProjectView(context.project?.projectId),binding=bindings.get(context.mount),area=workflowArea(view,tabId);
  return prepareOperationNavigation(context,view,binding?.view===view?binding.ops:null,area);
}
export function mountProject(context) {
  if (context.isCurrentProject?.() === false) return;
  styles();
  const view = getProjectView(context.project?.projectId), area = workflowArea(view,context.activeRibbonTabId);
  const previous=bindings.get(context.mount)?.view;if(previous&&previous!==view){exitToolAxisPointModes(previous);disposeMachineIK(previous);disposeMachineAssemblyPlacement(previous);disposeMachinePlanRecovery(previous);disposeMachinePlanIssue(previous);void stopMachineJog(previous,'project-switch');}
  view.fiveRequestedRibbonTabId=context.activeRibbonTabId;
  if (view.fiveAxisArea !== area) { exitToolAxisPointModes(view);disposePoseSelection(view);view.fiveAxisRegistrationPick = null; view.fiveAxisExtractionPick = null; view.fiveAxisStartOpen=false;if(!view.fiveStart?.request)view.fiveStart=null; }
  disposeInactiveWorkpieceFilePicker(context, view);
  disposeInactiveCuttingHeadFilePicker(context,view);
  view.fiveAxisArea = area;
  syncOperationPreparationKind(view);
  return cam.mountProject(decorate(context));
}
export function handleRibbonCommand(context, id) { return cam.handleRibbonCommand(decorate(context), id); }
export function getProjectHistoryState(context){
 const view=getProjectView(context.project?.projectId),s=view.fiveMachineIK,c=view.fiveMachineColliders,h=view.fiveHeadColliders;
 if(view.fiveFixturePlate?.open)return fixturePlateHistoryState(view);
 if(view.fiveFixtureLibrary?.open){const h=fixtureLibraryHistoryState();return {...h,canUndo:h.canUndo||!!view.fiveFixtureLibrary.editing,blocked:h.blocked||!!view.pending};}
 if(view.fiveMachineDefinitionsOpen&&view.fiveAxisArea==='simulation'){const h=definitionHistoryState();return {...h,blocked:h.blocked||!!view.fiveMachine?.busy};}
 return {blocked:!!(view.pending||view.fiveFixtureDesignRequest||view.fiveLayers?.queued||view.fiveFloatingInputCommit||view.fivePlacement?.gesture||view.fiveFixtureGizmo?.gesture||view.viewport?.transformManipulator?.pointerId!=null||view.fixtureRequest||view.fiveToolAxisRequest||view.fiveAxisPointEdit?.rotation?.gesture||view.fiveAxisPointEdit?.influence?.gesture||view.fiveHistoryBusy||view.fiveMachineAssembly?.gesture||view.fiveMachineAssembly?.saving||view.fiveMachine?.busy||view.fiveMachineJog?.running||s?.gesture||s?.inflight||s?.editTask||s?.finishing||c?.dragging||h?.dragging),
  canUndo:!!(floatingTransformDraft(view)||hasUnappliedWorkpieceDrafts(view)||hasUnappliedCutDrafts(view)||view.fiveRapid?.pathEdit?.changed||view.fiveRapid?.policyChanged||view.fiveRapid?.settingsChanged||view.fiveRapid?.automaticChanged||view.fiveMachineParameterEdits?.entries.size||view.fiveMachine?.dirty||c?.hud?.error||[...(c?.drafts.values()||[])].some(d=>d.dirty)||[...(s?.drafts?.values()||[])].some(d=>d.dirty)||h?.parts.some(p=>p.dirty))};
}
export async function handleProjectHistoryCommand(context,direction){
 const view=getProjectView(context.project?.projectId),binding=bindings.get(context.mount);
 if(binding?.view!==view||context.isCurrentProject?.()===false)return false;
 if(getProjectHistoryState(context).blocked)return true;
 if(view.fiveFixturePlate?.open){fixturePlateHistoryCommand(binding.context,view,direction,binding.ops);return true;}
 exitToolAxisPointModes(view);
 const cancelledFloating=floatingTransformDraft(view),active=binding.context.mount.ownerDocument.activeElement;
 if(view.fiveRapid?.hudEditing||view.fiveMachineIK?.pose?.hudEditing||view.fiveMachineColliders?.hud?.candidate){
  if(active?.closest?.('.five-placement-hud')){finishFloatingInput(binding.context,view,active,true,binding.ops);await view.fiveFloatingInputCommit;}
  else{finishRapidHudEdit(view,true);finishMachinePoseHudEdit(binding.context,view,true,binding.ops);if(view.fiveMachineColliders?.hud?.candidate)cancelMachineColliderValue(binding.context,view,binding.ops);binding.ops.renderProject(binding.context,view);}
  if(cancelledFloating&&direction==='undo'){context.actions?.refreshProjectHistoryControls?.();return true;}
 }
 if(view.fiveFixtureLibrary?.open){await fixtureLibraryHistoryCommand(binding.context,view,direction,binding.ops);return true;}
 if(view.fiveMachineDefinitionsOpen&&view.fiveAxisArea==='simulation'){await definitionHistoryCommand(binding.context,view,direction,binding.ops);return true;}
 if(hasUnappliedWorkpieceDrafts(view)){
  clearWorkpieceEditorDrafts(view);binding.ops.renderProject(binding.context,view);
  if(direction==='undo'){context.actions?.refreshProjectHistoryControls?.();return true;}
 }
 if(hasUnappliedCutDrafts(view)){
  disposeToolAxisPointPreview(view);clearCutEditorDrafts(view);
  if(direction==='undo'){
   await loadProcessDraft(binding.context,view,binding.ops);
   await loadCutDraft(binding.context,view,binding.ops);
   binding.ops.renderProject(binding.context,view);context.actions?.refreshProjectHistoryControls?.();return true;
  }
 }
 const cancelledRapidInput=await cancelInvalidRapidInput(binding.context,view,binding.ops);
 if(cancelledRapidInput&&direction==='undo'){context.actions?.refreshProjectHistoryControls?.();return true;}
 {
  let cancelledMachineInput=cancelInvalidMachineParameters(view);
  const machine=view.fiveMachine;
  if(machine?.dirty&&![...(view.fiveMachineParameterEdits?.entries.keys()||[])].some(k=>k.startsWith('setup:'))){machine.setupKey=null;cancelledMachineInput=true;}
  // Invalid text never reached the repository. Discard that preview without
  // replaying a synthetic command or consuming a previous committed edit.
  const ik=view.fiveMachineIK,c=view.fiveMachineColliders;
  const invalidPose=[...(ik?.drafts?.values()||[])].filter(d=>d.dirty&&d.error);
  const invalidBoxes=[...(c?.drafts.values()||[])].filter(d=>d.dirty&&d.error);
  if(invalidPose.length||invalidBoxes.length){
   for(const d of invalidPose){clearTimeout(d.timer);ik.drafts.delete(d.key);}if(ik)ik.pose=null;
   for(const d of invalidBoxes){clearTimeout(d.timer);d.dirty=false;d.requested=null;d.signature=null;}
   cancelledMachineInput=true;
  }
  if(cancelledMachineInput){binding.ops.renderProject(binding.context,view);context.actions?.refreshProjectHistoryControls?.();if(direction==='undo')return true;}
 }
 if(view.fiveMachineColliders?.hud?.error){cancelMachineColliderValue(binding.context,view,binding.ops);if(direction==='undo')return true;}
 if(cancelInvalidHeadColliderEdits(view)){
  binding.ops.renderProject(binding.context,view);context.actions?.refreshProjectHistoryControls?.();if(direction==='undo')return true;
 }
 view.fiveHistoryBusy=true;
 try{
  await stopMachineJog(view);stopMachine(view,true);
  await flushMachineParameterEdits(binding.context,view,binding.ops);
  await flushRapidPathSave(binding.context,view,binding.ops);
  await flushMachinePoseEdits(binding.context,view,binding.ops);
  await flushMachineColliderSaves(binding.context,view);
  await flushHeadColliderSaves(binding.context,view,binding.ops);
  if(!await flushAlignmentEdits(binding.context,view,binding.ops))return true;
  await flushEditedProjectSave(binding.context);
  await context.actions[direction==='redo'?'redoProject':'undoProject']();
  }finally{
   view.fiveHistoryBusy=false;
   if(!view.disposed&&context.isCurrentProject?.()!==false&&bindings.get(binding.context.mount)===binding&&binding.view===view){
    syncRapidPathEditing(binding.context,view,binding.ops);
    syncMachineIK(binding.context,view,binding.ops);
   }
   context.actions?.refreshProjectHistoryControls?.();
  }
 return true;
}
export async function onProjectHistoryChanged(context) {
  const view=getProjectView(context.project?.projectId),binding=bindings.get(context.mount);
  if(binding?.view!==view||context.isCurrentProject?.()===false)return;
  const rapid=view.fiveRapid?.open?{id:view.fiveRapid.id,selected:view.fiveRapid.pathEdit?.selected,settings:view.fiveRapid.settings}:null;
  const ui=workspaceState(view),selection=[...(view.fivePreferredCutSelection??ui.selectedIntentIds??[])];
  const leadsOpen=view.fiveAxisLeadsOpen,tool=activeTool(view);
 const orderOpen=view.fiveOrder?.open,headEditorSimulation=view.fiveSimulation?.headOpen?view.fiveSimulation:null;
  const workpieceTool=activeTool(view,view.fiveAxisArea),kind=view.fivePreparationKind,fixtureId=view.fiveAxisFixtures?.selected;
  const fixtureDesignOpen=view.fiveFixtureDesignOpen,fixtureDesignPage=view.fiveFixtureDesignPage,setupCheckOpen=view.fiveSetupCheckOpen;
  // Native history changes the product domain as well as the render view.
  // Keep the next pick/command on the restored revision before re-enabling UI.
  resetWorkpieceTools(view);disposeSceneSnapping(view);
  // History refresh must keep the cutting-head editor and its canvas mounted.
  if(headEditorSimulation)view.fiveSimulation=headEditorSimulation;
  clearWorkpieceEditorDrafts(view);
  clearCutEditorDrafts(view);
  ui.selectedIntentIds=selection;ui.selectedIntentId=selection[0]||'';
  const ik=view.fiveMachineIK,c=view.fiveMachineColliders,m=view.fiveMachine;
  if(ik){ik.pose=null;ik.hud=null;ik.drafts?.clear();ik.message='';}
  if(c){c.hud=null;for(const d of c.drafts.values()){clearTimeout(d.timer);d.dirty=false;d.requested=null;d.editKey=null;}}
  if(m){m.report=null;m.snapshot=null;m.snapshotKey=null;m.postMachine=null;m.postDirty=false;m.limitDraft={};m.jointDraft={};}

  // Keep the current editor during the pending render as well as the final
  // refresh; briefly showing the workpiece editor would replace fixture inputs.
  if(['workpiece','operation'].includes(view.fiveAxisArea)){
    selectTool(view,workpieceTool);view.fivePreparationKind=kind;
    view.fiveFixtureDesignOpen=fixtureDesignOpen;view.fiveFixtureDesignPage=fixtureDesignPage;view.fiveSetupCheckOpen=setupCheckOpen;
  }
  if(await binding.ops.refreshSceneState(binding.context,view)===false)
    throw Error(view.error||'撤销或重做已完成，但场景状态刷新失败，请刷新后继续。');
  if(['workpiece','operation'].includes(view.fiveAxisArea)){
    selectTool(view,workpieceTool);view.fivePreparationKind=kind;
    view.fiveFixtureDesignOpen=fixtureDesignOpen;view.fiveFixtureDesignPage=fixtureDesignPage;view.fiveSetupCheckOpen=setupCheckOpen;
    if(view.fiveAxisFixtures?.selected&&view.scene.domain.fixtureUnits?.some(f=>f.id===fixtureId))view.fiveAxisFixtures.selected=fixtureId;
  }
  const known=new Set((view.scene.intentToolpaths||[]).filter(c=>!c.locked).map(c=>c.id));
  ui.selectedIntentIds=selection.filter(id=>known.has(id));ui.selectedIntentId=ui.selectedIntentIds[0]||'';
  if(leadsOpen&&['leads','overcut'].includes(tool))openLeads(view);
  if(orderOpen&&['manual-order','auto-order'].includes(tool))openOrdering(view);
  await loadProcessDraft(binding.context,view,binding.ops);
  await loadCutDraft(binding.context,view,binding.ops);
  binding.ops.renderProject(binding.context,view);
  if(m&&!view.scene.machines?.some(machine=>machine.entityId===m.machineId)){m.setupKey=null;binding.ops.renderProject(binding.context,view);}
  if(rapid&&['rapid','settings'].includes(activeTool(view))){
    openRapid(view,rapid.settings);
    if(!rapid.settings){rapidPick(view,rapid.id);const t=generatedGeometry(view)?.transitions?.find(t=>`${t.from}/${t.to}`===rapid.id);
      if(t&&(rapid.selected!==undefined||t.manualPath))await rapidCommand(binding.context,view,'path-edit',binding.ops);
      const edit=view.fiveRapid?.pathEdit;if(edit){edit.selected=Math.min(rapid.selected??1,edit.points.length-1);edit.selectedPoints=[edit.selected];edit.values=edit.points[edit.selected].map(v=>String(Number(v.toFixed(6))));}}
    binding.ops.renderProject(binding.context,view);
  }
  const path=context.projectProxy?.state?.projectPath||context.project?.projectPath;
  if(path&&!path.includes('://')){await saveEditedProject(context);clearCutSaveError(view);clearCommittedSaveError(view);}
}
export function shouldPreserveRibbonCommandSurface() { return true; }
export async function prepareProjectSave(context) {
  const saveView=getProjectView(context.project?.projectId);
  if(saveView.fiveFixturePlate?.open&&saveView.fiveFixturePlate.dirty){const binding=bindings.get(context.mount);await fixturePlateCommand(binding.context,saveView,'part-save',binding.ops);if(saveView.fiveFixturePlate?.dirty)throw Error(saveView.fiveFixturePlate.error||'请先完成板件编辑。');}
  await stopMachineJog(saveView,'save');
  if(saveView.fiveMachineJog?.started&&!saveView.fiveMachineJog?.ended)throw Error(saveView.fiveMachine?.error||'连续点动尚未确认停止，请稍后重试保存。');
  if(saveView.viewport?.transformManipulator?.pointerId!=null)throw Error('请先结束拖动再保存。');
  if(saveView.fiveMachineAssembly?.savingRequest)await saveView.fiveMachineAssembly.savingRequest;
  if(saveView.fiveMachineAssembly?.gesture)throw Error('请先结束工件与工装的整体拖动再保存。');
  if(saveView.fiveWorkpieceSaveError){await saveEditedProject(context);clearCommittedSaveError(saveView);}
  if(saveView.fiveAxisPointEdit?.rotation?.gesture||saveView.fiveAxisPointEdit?.influence?.gesture)throw Error('请先结束刀轴姿态拖动再保存。');
  if(saveView.fiveCutSaveError){await saveEditedProject(context);clearCutSaveError(saveView);}
  const saveBinding=bindings.get(context.mount),focused=context.mount?.ownerDocument?.activeElement;
  if(saveView.fiveFloatingInputCommit)await saveView.fiveFloatingInputCommit;
  if(saveBinding?.view===saveView){
    if(focused?.closest?.('.five-placement-hud')||['name','x','y','z','rx','ry','rz'].includes(focused?.dataset?.fiveFixtureField)){finishFloatingInput(saveBinding.context,saveView,focused,false,saveBinding.ops);await saveView.fiveFloatingInputCommit;}
    else{finishRapidHudEdit(saveView,false);finishMachinePoseHudEdit(saveBinding.context,saveView,false,saveBinding.ops);if(saveView.fiveMachineColliders?.hud?.candidate)await command(saveBinding.context,saveView,'five.machine.box-value-apply',saveBinding.ops);}
  }
  await flushMachineParameterEdits(context,getProjectView(context.project?.projectId),bindings.get(context.mount)?.ops);
  await flushMachinePoseEdits(context,getProjectView(context.project?.projectId),bindings.get(context.mount)?.ops);
  await flushMachineColliderSaves(context,getProjectView(context.project?.projectId));
  await flushHeadColliderSaves(context,getProjectView(context.project?.projectId),bindings.get(context.mount)?.ops);
  const view=getProjectView(context.project?.projectId),binding=bindings.get(context.mount);
  await flushToolpathLayerEdits(context,view,binding?.ops);
  await flushRapidPathSave(context,view,binding?.ops);
  await flushEditedProjectSave(context);
  if(view.fiveAxisPointEdit?.dirty||view.fiveAxisPointEdit?.request){
    if(binding?.view!==view||context.isCurrentProject?.()===false)throw Error('请回到当前项目完成局部刀轴编辑后保存。');
    while(view.pending&&view.fiveToolAxisRequest)await new Promise(resolve=>setTimeout(resolve,20));
    await flushToolAxisPointEdits(binding.context,view,binding.ops);
  }
  if([...(view.fiveToolAxes?.values()||[])].some(d=>d.dirty||d.request)){
    if(binding?.view!==view||context.isCurrentProject?.()===false)throw Error('请回到当前项目完成刀轴编辑后保存。');
    while(view.pending&&view.fiveToolAxisRequest)await new Promise(resolve=>setTimeout(resolve,20));
    await flushToolAxisEdits(binding.context,view,binding.ops);
  }
  if(view.fivePlacement?.panel){
    if(binding?.view!==view||context.isCurrentProject?.()===false)throw Error('请回到当前项目完成变换参数编辑后保存。');
    while(view.fivePlacement?.saving)await new Promise(resolve=>setTimeout(resolve,20));
    await transformParameterCommand(binding.context,view,'apply',binding.ops);
    if(view.fivePlacement?.panel)throw Error(view.error||'变换参数尚未保存。');
  }
  else if(view.fivePlacement?.open)await savePlacementDraft(binding.context,view,binding.ops);
  if(view.fiveFixtureLibrary?.open){
    const library=view.fiveFixtureLibrary;
    if(library.editing){await fixtureCommand(binding.context,view,'library.rename:'+library.editing,binding.ops);if(library.editing)throw Error(view.error||'工装库名称尚未保存。');}
  }
  for(const [id,d] of view.fiveWorkpieceNames||[])if(d.dirty||d.request){await saveWorkpieceName(binding.context,view,id,binding.ops);if(view.fiveWorkpieceNames.has(id))throw Error(d.error||'工件名称尚未保存。');}
  if(view.fiveSetupNameEdit){
    await setupCommand(binding.context,view,'rename',binding.ops);
    if(view.fiveSetupNameEdit)throw Error(view.error||'装夹名称尚未保存。');
  }
  if(Object.values(view.fiveAxisFixtures?.drafts||{}).some(d=>d.dirty)){
    await flushFixtureEdits(binding.context,view,binding.ops);
  }
  if(view.fiveLocating?.dirty){
    if(view.fiveLocating.points.filter(Boolean).length!==3)throw Error('请完成三点定位或还原输入后保存。');
    await locatingCommand(binding.context,view,'save',binding.ops);
    if(view.fiveLocating?.dirty||view.fiveLocating?.request)throw Error(view.error||'定位资料尚未保存。');
  }
  if(view.fiveAlignment?.open){
    if(binding?.view!==view||context.isCurrentProject?.()===false)throw Error('请回到当前项目完成坐标系编辑后保存。');
    if(!await flushAlignmentEdits(binding.context,view,binding.ops))throw Error(view.fiveAlignment?.error||Object.values(view.fiveAlignment?.fieldErrors||{})[0]||'坐标系尚未保存。');
  }
  if(!view.fiveNameDrafts?.size&&![...(view.fiveCommandDrafts?.values()||[])].some(d=>d.dirty||d.request))return;
  if(binding?.view!==view||context.isCurrentProject?.()===false)throw Error('请回到当前项目完成刀路指令编辑后保存。');
  while(view.fiveCommandSavingId||view.fiveNameSavingId)await new Promise(resolve=>setTimeout(resolve,20));
  if(view.pending)throw Error('请等待当前操作完成后保存。');
  for(const [id] of view.fiveNameDrafts||[]){await saveCutName(binding.context,view,id,binding.ops);if(view.fiveNameDrafts.has(id))throw Error(view.fiveNameDrafts.get(id).error||'刀路名称尚未保存。');}
  for(const [id,d] of view.fiveCommandDrafts||[])if(d.dirty||d.request){
    await saveCutCommandText(binding.context,view,id,binding.ops);
    if(d.dirty||d.request)throw Error(d.error||'刀路指令尚未保存，请重试。');
  }
}
export function synchronizeActiveAreaView(context, expectation) { return cam.synchronizeActiveAreaView(decorate(context), expectation); }
export function waitForActiveAreaAction(context) { const view=getProjectView(context.project.projectId);return view.fiveFixtureEditRequest ?? view.fixtureRequest ?? cam.waitForActiveAreaAction(decorate(context)); }
export function getWindowCloseGuard(context){
 const v=getProjectView(context.project?.projectId),p=v.fiveMachineParameterEdits,i=v.fiveMachineIK,c=v.fiveMachineColliders,r=v.fiveRapid;
 if(v.fiveMachineAssembly?.gesture||v.fiveMachineAssembly?.saving)return {blocked:true,message:'正在完成工件与工装的整体调整，请稍后关闭。'};
 if(v.viewport?.transformManipulator?.pointerId!=null||v.fixtureRequest||v.fiveLayers?.queued||v.fiveFloatingInputCommit)return {blocked:true,message:'正在完成编辑，请稍后关闭。'};
 if(v.pending&&v.viewportSceneProxy?.state?.state!=='Faulted'||v.fiveAxisPointEdit?.rotation?.gesture||v.fiveAxisPointEdit?.influence?.gesture||v.fiveHistoryBusy||v.fiveMachine?.busy||v.fiveMachineJog?.running||p?.request||p?.fileSaving||i?.gesture||i?.editTask||i?.inflight||i?.finishing||i?.fileSaving||c?.dragging||c?.saving||r?.running||r?.savingPath||isEditedProjectSaving(context))return {blocked:true,message:'正在完成编辑或自动保存，请稍后关闭。'};
 return null;
}
function discardPendingEditorInputs(view){
 exitToolAxisPointModes(view);
 for(const e of view.fiveMachineParameterEdits?.entries.values()||[])clearTimeout(e.timer);
 view.fiveMachineParameterEdits?.entries.clear();
 for(const d of view.fiveMachineIK?.drafts?.values()||[]){clearTimeout(d.timer);d.requested=null;d.dirty=false;}
 for(const d of view.fiveMachineColliders?.drafts.values()||[]){clearTimeout(d.timer);d.requested=null;d.dirty=false;}
 clearTimeout(view.fiveRapid?.saveTimer);
}
export async function releaseProject(context) {
  const view=findProjectView(context.project?.projectId);
  if(!view)return releaseWorkbench(context);
  const mount=context.mount,binding=mount?bindings.get(mount):null;
  if(mount){disposeBlockingProgress(view,mount.ownerDocument);disposeOperationCreateDialog(view,mount.ownerDocument);}
  releaseProjectDocumentBindings(view);
  // A background project has no mount, or the shared mount already belongs
  // to another project. Detach only this view's binding, before awaiting any
  // native cleanup which could allow that mount to be reused.
  if(binding?.view===view){
    disposeHeadChoicePicker(context,view);
    binding.disposeCamContextPickers?.();binding.disposeSupportMenu?.();binding.disposeExtractionMenu?.();binding.disposeFaceListFocus?.();binding.disposeMachineJog?.();disposeFloatingDialogs(binding);disposeAlertPanel(binding);
    mount.ownerDocument?.removeEventListener('keydown',binding.keydown);
    mount.ownerDocument?.removeEventListener('visibilitychange',binding.visibility);
    bindings.delete(mount);
  }
  discardPendingEditorInputs(view);
  await view.fiveMachineAssembly?.savingRequest;disposeMachineAssemblyPlacement(view);disposeMachinePlanRecovery(view);disposeMachinePlanIssue(view);
  await finishMachineIK(view);disposeMachineIK(view);disposeMachineObservation(view);
  await stopMachineJog(view);disposeMachineColliders(view);stopMachine(view,true);clearIndependentHead(view);
  disposeMachineCollisionFeedback(view);
  disposeSelectionFeedback(view);disposeFixturePlacement(view);disposeLocating(view);disposeSceneSnapping(view);
  disposeAlignment(view);disposeFixtureDesign(view);disposeFixturePlateEditor(view);disposeRapidPathEditing(view);disposeCompensationOverlay(view);
  disposePoseSelection(view);disposeToolAxisPointRotation(view);disposeToolAxisDisplay(view);
  disposeToolAxisPointPreview(view);disposeToolAxisPreview(view);disposeWorkpieceThumbnails(view);
  disposePathTransform(view);disposePlacementPreview(view);disposeWorkpieceFilePicker(view);disposeCuttingHeadFilePicker(view);disposeRegistrationInteraction(view);
  disposeSimulation(view);disposeCuttingHeadPreview(view);disposeGeneratedPreview(view);disposeCutPreview(view);disposeLeadOverlay(view);
  disposeExtractionDirection(view);
  return releaseWorkbench(context);
}
