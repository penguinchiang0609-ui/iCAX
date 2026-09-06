import { attachMachineAppearanceAutoApply, attachMachineInstanceNameAutoApply, attachMachineJointLimitAutoApply, attachMachineToolTCPAutoApply, attachMachineTransformAutoApply, handleMachineAction, handleMachineRibbonCommand, importMachinePath as importMachinePathAction, selectMachineDefinition as selectMachineDefinitionAction, selectMachineInstance as selectMachineInstanceAction, setMachineInstanceEnabled as setMachineInstanceEnabledAction } from "./machine/machineActions.mjs";
import { renderMachineLeftPane, renderMachineRightPane } from "./machine/machineArea.mjs";
import { handleMachiningAction, handleMachiningRibbonCommand, setJobMachine as setJobMachineAction } from "./machining/machiningActions.mjs";
import { renderMachiningLeftPane, renderMachiningRightPane } from "./machining/machiningArea.mjs";
import { findCommandTitle, getTabTitle, normalizeCamTab } from "./ribbon/ribbonDefinition.mjs";
import { renderToolpathOverlay } from "./toolpath/toolpathViews.mjs";
import { renderViewLeftPane, renderViewRightPane } from "./view/viewArea.mjs";
import { handleWorkpieceAction, handleWorkpieceRibbonCommand, importModelPath as importModelPathAction } from "./workpiece/workpieceActions.mjs";
import { renderWorkpieceLeftPane, renderWorkpieceRightPane } from "./workpiece/workpieceArea.mjs";
import { renderEdge, renderFace, renderLoop } from "./workpiece/workpieceViews.mjs";
import { createWorkbench } from "./createWorkbench.mjs";
import { RENDER_ENTITY_VIEW_PROJECTION } from "./projection.mjs";
export { RENDER_ENTITY_VIEW_PROJECTION } from "./projection.mjs";
export { getRibbonDefinition } from "./ribbon/ribbonDefinition.mjs";

const AREA_VIEW_DEFINITIONS = Object.freeze({
  machine: Object.freeze({
    sources: Object.freeze([Object.freeze({
      sourceId: "machine-elements",
      role: "machine",
      language: "sql",
      where: "WHERE HAS CMachineElementComponent AND HAS CRenderInstanceComponent",
      projection: RENDER_ENTITY_VIEW_PROJECTION,
    })]),
  }),
  workpiece: Object.freeze({
    sources: Object.freeze([Object.freeze({
      sourceId: "workpieces",
      role: "workpiece",
      language: "sql",
      where: "WHERE HAS CWorkpieceComponent AND HAS CRenderInstanceComponent",
      projection: RENDER_ENTITY_VIEW_PROJECTION,
    })]),
  }),
  machining: Object.freeze({
    sources: makeSceneViewSources(),
  }),
  view: Object.freeze({
    sources: makeSceneViewSources(),
  }),
});

function makeSceneViewSources() {
  return Object.freeze([
    Object.freeze({
      sourceId: "scene-renderables",
      role: "scene",
      language: "sql",
      where: "WHERE HAS CRenderInstanceComponent",
      projection: RENDER_ENTITY_VIEW_PROJECTION,
    }),
    Object.freeze({
      sourceId: "machine-elements",
      role: "machine",
      language: "sql",
      where: "WHERE HAS CMachineElementComponent AND HAS CRenderInstanceComponent",
      projection: RENDER_ENTITY_VIEW_PROJECTION,
    }),
    Object.freeze({
      sourceId: "workpieces",
      role: "workpiece",
      language: "sql",
      where: "WHERE HAS CWorkpieceComponent AND HAS CRenderInstanceComponent",
      projection: RENDER_ENTITY_VIEW_PROJECTION,
    }),
  ]);
}


const workbench = createWorkbench({
  attachMachineAppearanceAutoApply,
  attachMachineInstanceNameAutoApply,
  attachMachineJointLimitAutoApply,
  attachMachineToolTCPAutoApply,
  attachMachineTransformAutoApply,
  handleMachineAction,
  handleMachineRibbonCommand,
  importMachinePathAction,
  selectMachineDefinitionAction,
  selectMachineInstanceAction,
  setMachineInstanceEnabledAction,
  renderMachineLeftPane,
  renderMachineRightPane,
  handleMachiningAction,
  handleMachiningRibbonCommand,
  setJobMachineAction,
  renderMachiningLeftPane,
  renderMachiningRightPane,
  findCommandTitle,
  getTabTitle,
  normalizeCamTab,
  renderToolpathOverlay,
  renderViewLeftPane,
  renderViewRightPane,
  handleWorkpieceAction,
  handleWorkpieceRibbonCommand,
  importModelPathAction,
  renderWorkpieceLeftPane,
  renderWorkpieceRightPane,
  renderEdge,
  renderFace,
  renderLoop,
  areaViewDefinitions: AREA_VIEW_DEFINITIONS,
  camSelection: true,
  sceneMethods: ["Job.Get", "MachineDefinition.List", "Machine.List", "Workpiece.List", "IntentToolpath.List", "Toolpath.List", "Selection.Get"],
});
export const { mountProduct, mountProject, synchronizeActiveAreaView, waitForActiveAreaAction, handleRibbonCommand } = workbench;
