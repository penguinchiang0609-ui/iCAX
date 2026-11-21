import { invokeGeometry } from "./editingTarget.mjs";
import { synchronizeNodeEditor, applyNodeEdit } from "./nodeEditing.mjs";
import { openSketch, completeSketch, cancelSketch, waitGeometryTask } from "./sketchOperations.mjs";
import { createDomPatcher } from "../../../iCAX-UI/SDK/Forms/domPatch.mjs";
import { createPlanarContextMenu } from "../../../iCAX-UI/SDK/Cad/planarContextMenu.mjs";
import { polylineSegmentKinds } from "../../../iCAX-UI/SDK/Cad/planarCurveSegments.mjs";
import { createWorkbench, releaseProject as releaseWorkbenchProject } from "../../_shared/workbench/createWorkbench.mjs";
import { getProjectView, findProjectView } from "../../_shared/workbench/state/projectViewStore.mjs";
import { capturePaneInteraction } from "../../_shared/workbench/utils/paneInteractionState.mjs";
import { RENDER_ENTITY_VIEW_PROJECTION } from "../../_shared/workbench/projection.mjs";
import { getRibbonDefinition as defineRibbon } from "./ribbonDefinition.mjs";
import { ensureWorkspaceStyles } from "./workspace.css.mjs";
import { renderWorkspaceLeft, renderWorkspaceRight, renderWorkspaceBottom, renderCommandPanel, renderSceneStatusBar } from "./workspaceViews.mjs";
import { AREA_IDS, AREA_TITLES, clearSelectedDraft, draftKey, getAreaEntityIds, getAreaRows, getCanvasObject, getSelectedObject, getWorkspaceState, hasPropertyDrafts, normalizeAreaId, selectObject, updateDraft, isGeometryEditing, geometryObjectKind, geometryMethod, activeSketch } from "./workspaceState.mjs";
import { downloadReport, importDxf, importWithUnits, invokePlane, readReport, refreshPlaneState, saveObject, startNesting, supports, synchronizeAvailability, synchronizePlaneView, watchTask } from "./nativeOperations.mjs";
import { acceptDrawingArcParameters, acceptDrawingArcStart, acceptDrawingDrag, acceptDrawingPoint, acceptDrawingPointerPoint, beginDrawing, cancelDrawing, canCloseSelectedPolyline, clearDrawingGesture, closePolyline, drawingDragKey, drawingPointerPoint, drawingStep, finishDrawing, finishPolylineSegment, polylineModeShortcuts, previewDrawingPoint, refreshDrawingSnapCandidates, runDrawingInput, setPolylineMode, undoPolylineInput, undoStandaloneDrawingInput } from "./drawingCommands.mjs";
import { disposeDrawingPreview } from "./drawingPreview.mjs";
import { drawingDimensionMousePoint, ensureDrawingDynamicInput, focusDrawingDimensionField, handleDrawingDimensionKey, renderDrawingDynamicInput, synchronizeDrawingDynamicInput, updateDrawingDimensionDraft } from "./drawingDynamicInput.mjs";
import { beginSelectedDimensionEdit, cancelSelectedDimensionEdit, clearSelectedDimensionAnnotations, commitSelectedDimensions, handleSelectedDimensionKey, refreshSelectedDimensionPresentation, renderSelectedDimensionAnnotations, synchronizeSelectedDimensionAnnotations, updateSelectedDimensionDraft } from "./selectedDimensionAnnotations.mjs";
import { userErrorMessage } from "./errorMessages.mjs";
import { renderPlaneLayers, ensurePlaneLayerStyles, openLayerManager, inputPlaneLayer, changePlaneLayer, handlePlaneLayerAction, flushPlaneLayerEdits, planeLayerPending } from "./layers.mjs";
import { renderDrawingToolPalette, synchronizeDrawingToolPalette, closeDrawingToolPaletteMenu, disposeDrawingToolPalette } from "./drawingToolPalette.mjs";
import { handleArtworkAction, openArtworkTool, placeArtworkText } from "./artworkOperations.mjs";

const mounted = new WeakMap();
const areaViews = Object.fromEntries(AREA_IDS.map(area => [area, { sources: [{
  sourceId: `iplane-${area}`, role: area, language: "sql",
  where: "WHERE HAS CRenderInstanceComponent", projection: RENDER_ENTITY_VIEW_PROJECTION,
}] }]));

function synchronizeViewport(context, view) {
  const viewport = view.viewport;
  if (!viewport) return;
  const ui = getWorkspaceState(view), area = normalizeAreaId(view.activeAreaId);
  if (isGeometryEditing(view)) refreshDrawingSnapCandidates(context, view);
  viewport.setGridVisible?.(ui.gridVisible);
  viewport.setVisibleEntityIds(getAreaEntityIds(context, view, area));
  const selected = getAreaEntityIds(context, view, area, true);
  // Business selection belongs to the plane product. The neutral emphasis API
  // shows it without introducing a 3D transform/axis gesture on the worktable.
  viewport.clearTransformManipulator?.();
  viewport.setSelectedObjectIds([], "");
  viewport.setEmphasizedObjectIds(selected, { visual: { color: 0xffad1f, emissive: 0xd47700, emissiveIntensity: 0.68, linewidth: 2 } });
}

function fitPendingCanvas(context, view) {
  const ui = getWorkspaceState(view);
  if (ui.fitCanvasKey !== viewportKey(context, view) || !getAreaEntityIds(context, view).length) return;
  if (ui.activeTool.startsWith("plane.draw-")) { ui.fitCanvasKey = "";return; }
  if (view.viewport?.fitView()) ui.fitCanvasKey = "";
}

function viewportKey(context, view) {
  const area = normalizeAreaId(view.activeAreaId), canvas = getCanvasObject(context, view);
  if (getWorkspaceState(view).activeSketchId) return `sketch:${getWorkspaceState(view).activeSketchId}`;
  if (area === "drawing") return "drawing:worktable";
  return `${area}:${canvas?.kind ?? ""}:${canvas?.row.id ?? ""}`;
}

function synchronizePresentation(context, view, mount) {
  const workbench = mount.querySelector(".iplane-workspace"), area = normalizeAreaId(view.activeAreaId);
  if (!workbench) return;
  workbench.dataset.iplaneArea = area;
  const objectsOpen = !isGeometryEditing(view) || getWorkspaceState(view).objectsPanelOpen === true;
  workbench.dataset.iplaneObjectsOpen = String(objectsOpen);
  const objectsPane = workbench.querySelector(".cam-context-pane");
  if (objectsPane) { objectsPane.inert = !objectsOpen;objectsPane.setAttribute("aria-hidden", String(!objectsOpen)); }
  for (const [field, variable] of [["leftWidth", "--cam-left-width"], ["rightWidth", "--cam-right-width"], ["bottomHeight", "--cam-bottom-height"]]) {
    const value = Number(view.layout?.[field]);
    if (Number.isFinite(value)) workbench.style.setProperty(variable, `${value}px`);
  }
  const canvas = getCanvasObject(context, view);
  const title = workbench.querySelector(".cam-viewer-head strong");
  if (title) title.textContent = activeSketch(context, view) ? `草图 · ${activeSketch(context, view).ownerName}` : area === "drawing" ? "底图 · 刀路" : canvas?.row.name ?? context.project?.projectName ?? "";
  const subtitle = workbench.querySelector(".cam-viewer-head>div>span:first-child");
  if (subtitle) subtitle.textContent = AREA_TITLES[area];
}

const workbench = createWorkbench({
  sceneMethods: [], camSelection: false, areaViewDefinitions: areaViews,
  resolveSelectedSceneObjectIds() { return []; },
  mapAreaViewSnapshot(context, view, area, snapshot) {
    // Native View publication and the domain query have independent timing.
    // Keep its authoritative render rows even when a new business object has
    // not reached ReadState yet. Visibility is applied separately below.
    return snapshot;
  },
  onAreaViewSnapshotApplied(context, view) { synchronizeViewport(context, view);fitPendingCanvas(context, view); },
});

const { patchNode, patchChildren } = createDomPatcher({ keyAttribute: "data-iplane-key" });
const commandPanelStates = new WeakMap();

function patchContents(container, html) {
  const template = container.ownerDocument.createElement("template");
  template.innerHTML = `<div>${html}</div>`;
  patchChildren(container, template.content.firstElementChild);
}

function patchPanels(context, view, mount) {
  const left = mount.querySelector(".cam-context-pane"), right = mount.querySelector(".cam-info-pane");
  const bottom = mount.querySelector("[data-iplane-bottom-root]");
  if (!left?.querySelector("[data-iplane-panel]") || !right || !bottom) return false;
  const restore = capturePaneInteraction(mount, [".cam-context-pane", ".cam-info-pane", "[data-iplane-bottom-root]", "[data-iplane-layer-host]"]);
  patchContents(left, renderWorkspaceLeft(context, view));
  patchContents(right, renderWorkspaceRight(context, view));
  const statusBar = mount.querySelector("[data-iplane-statusbar]");
  if (statusBar) {
    const statusTemplate = mount.ownerDocument.createElement("template");statusTemplate.innerHTML = renderSceneStatusBar(context, view);
    patchNode(statusBar, statusTemplate.content.firstElementChild);
  }
  const template = mount.ownerDocument.createElement("template");template.innerHTML = renderWorkspaceBottom(context, view);
  patchNode(bottom, template.content.firstElementChild);
  const layerHost = mount.querySelector("[data-iplane-layer-host]");
  if (layerHost) patchContents(layerHost, renderPlaneLayers(context, view));
  synchronizePresentation(context, view, mount);
  restore();
  return true;
}

function repaint(context, view, ops, ribbon = false) {
  if (view.disposed || context.isCurrentProject?.() === false) return;
  const live = view.iplaneLive;
  if (live?.context?.sceneProxy === context.sceneProxy) { context = live.context;ops = live.ops; }
  synchronizeAvailability(context, view);
  ops.renderProject(context, view);
  if (ribbon) context.actions?.refreshProductRibbon?.();
}

function updateCommandInput(context, view, source) {
  getWorkspaceState(view).commandInput = source.value;
  for (const host of [view.iplaneLive?.context?.mount ?? context.projectMount ?? context.mount, view.iplaneCommandMount]) {
    for (const input of host?.querySelectorAll?.("[data-iplane-command-input],[data-iplane-draw-value]") ?? []) {
      if (input !== source && input.value !== source.value) input.value = source.value;
    }
  }
}

async function runPlaneOperation(state, operation) {
  getWorkspaceState(state.view).operationError = "";
  try { await operation(); }
  catch (error) { if (state.context.isCurrentProject?.() !== false && !state.view.disposed) { const ui = getWorkspaceState(state.view);if (!ui.operationError) state.ops.appendProjectLog?.(state.context, "error", error?.message ?? String(error));ui.operationError = userErrorMessage(error); } }
  repaint(state.context, state.view, state.ops, true);
}

async function activateDrawingTool(context, view, ops, commandId) {
  const ui = getWorkspaceState(view);
  if (ui.busy || ui.geometryTaskId || ui.drawingCommitting) return;
  closeDrawingToolPaletteMenu(view);
  if (commandId === "plane.objects-toggle") { ui.objectsPanelOpen = !ui.objectsPanelOpen;return; }
  if (commandId !== "plane.select" && !isGeometryEditing(view)) throw new Error("请先进入底图或草图编辑。");
  if (commandId === "plane.text" || commandId === "plane.image") {
    if (ui.activeSketchId) throw new Error("文字和参考图片请放到底图；CAD草图只编辑轮廓。");
    const method = commandId === "plane.text" ? "PlaneText.Create" : "PlaneImage.Import";
    if (!supports(context, view, method)) throw new Error("原生模块未提供此对象类型。");
    closeDrawingContextMenu(view);clearSelectedDimensionAnnotations(view);cancelDrawing(view);
    await openArtworkTool(context, view, ops, commandId);return;
  }
  if (commandId.startsWith("plane.draw-") && ui.commandAvailability?.[commandId]?.disabled) throw new Error(ui.commandAvailability[commandId].unavailableReason);
  if (commandId === "plane.erase" && (!supports(context, view, geometryMethod(view, "Edit")) || !ui.geometryCapabilities?.edits?.includes("delete"))) throw new Error("原生模块未提供图形擦除能力。");
  closeDrawingContextMenu(view);clearSelectedDimensionAnnotations(view);cancelDrawing(view);
  ui.activeTool = commandId;
  if (commandId.startsWith("plane.draw-")) await beginDrawing(context, view, commandId.slice(11));
  else if (commandId === "plane.erase") { ui.panelMode = "object";ui.commandStatus = "点击要擦除的图形；Esc 返回选择。"; }
  else if (commandId.startsWith("plane.edit-")) { ui.panelMode = "drawing-edit";ui.commandStatus = "选择图形，在右侧输入编辑参数。"; }
  else ui.panelMode = "object";
}

function handlePlaneKey(state, event) {
  if (!state || state.context.isCurrentProject?.() === false) return;
  const { context, view, ops } = state, ui = getWorkspaceState(view);
  const selectedDimension = handleSelectedDimensionKey(view, event);
  if (selectedDimension) { if (selectedDimension.commit) void runPlaneOperation(state, () => commitSelectedDimensions(context, view, ops));return; }
  const dimension = handleDrawingDimensionKey(view, event);
  if (dimension) {
    if (dimension.arcStart) void runPlaneOperation(state, () => acceptDrawingArcStart(context, view, ops, dimension.arcStart));
    else if (dimension.arc) void runPlaneOperation(state, () => acceptDrawingArcParameters(context, view, ops, dimension.arc));
    else if (dimension.point) void runPlaneOperation(state, () => {
      if (dimension.preparePoints) ui.commandPoints = dimension.preparePoints;
      return acceptDrawingPoint(context, view, ops, dimension.point, false, false, dimension.sweepAngleDeg != null ? { sweepAngleDeg: dimension.sweepAngleDeg } : {});
    });
    else { const point = drawingDimensionMousePoint(view);if (point) previewDrawingPoint(view, point); }
    return;
  }
  const textInput = event.target.closest?.("input,textarea,select,[contenteditable]:not([contenteditable='false'])");
  if (!textInput && !event.isComposing && (event.key === "Backspace" || event.key.toLowerCase() === "z" && (event.ctrlKey || event.metaKey) && !event.shiftKey && !event.altKey)
      && undoStandaloneDrawingInput(view)) {
    event.preventDefault();event.stopPropagation();closeDrawingContextMenu(view);repaint(context, view, ops, true);return;
  }
  if (ui.activeTool === "plane.draw-polyline" && !textInput && !event.isComposing && !ui.busy && !ui.drawingCommitting) {
    const mode = !event.ctrlKey && !event.metaKey && !event.altKey && polylineModeShortcuts[event.key.toUpperCase()];
    if (mode) { event.preventDefault();event.stopPropagation();closeDrawingContextMenu(view);void runPlaneOperation(state, () => setPolylineMode(context, view, mode));return; }
    if (event.key === " " && ui.polylineMode === "bspline" && !event.ctrlKey && !event.metaKey && !event.altKey) {
      event.preventDefault();event.stopPropagation();closeDrawingContextMenu(view);void runPlaneOperation(state, () => finishPolylineSegment(view));return;
    }
    if (event.key === "Backspace" || event.key.toLowerCase() === "z" && (event.ctrlKey || event.metaKey) && !event.shiftKey && !event.altKey) {
      if (undoPolylineInput(view)) { event.preventDefault();event.stopPropagation();closeDrawingContextMenu(view);repaint(context, view, ops, true);return; }
    }
  }
  if (event.key === "Enter" && event.target.matches?.("[data-iplane-command-input],[data-iplane-draw-value]")) {
    event.preventDefault();const input = ui.commandInput, epoch = ui.drawingEpoch;
    void runPlaneOperation(state, async () => { await runDrawingInput(context, view, ops, input);if (ui.drawingEpoch === epoch && ui.commandInput === input) ui.commandInput = ""; });
  } else if (event.key === "Escape" && !event.target.matches?.("[data-iplane-field]")) {
    event.preventDefault();cancelDrawing(view);
    repaint(context, view, ops, true);
  } else if (event.key.toLowerCase() === "c" && !event.ctrlKey && !event.metaKey && !event.altKey && !event.isComposing && !event.repeat
      && !event.target.closest?.("input,textarea,select,[contenteditable]:not([contenteditable='false'])") && !ui.busy
      && (ui.activeTool === "plane.draw-polyline" && drawingStep(view).canClose
        || ui.activeTool === "plane.select" && canCloseSelectedPolyline(context, view))) {
    event.preventDefault();closeDrawingContextMenu(view);void runPlaneOperation(state, () => closePolyline(context, view, ops));
  } else if (event.key === "Enter" && ["plane.draw-polyline", "plane.draw-bspline"].includes(ui.activeTool) && !event.target.matches?.("input,textarea,select")
      && !event.target.closest?.("[data-iplane-context-menu]")) {
    event.preventDefault();closeDrawingContextMenu(view);void runPlaneOperation(state, () => finishDrawing(context, view, ops));
  } else if (event.key === "Delete" && !event.target.matches?.("input,textarea,select")) {
    const selected = getSelectedObject(context, view);
    if (selected?.kind === geometryObjectKind(view) && supports(context, view, geometryMethod(view, "Edit"))) { event.preventDefault();void runPlaneOperation(state, () => invokeGeometry(context, view, ops, "Edit", { id: selected.row.id, operation: "delete" })); }
  }
}

function toggle(context, view, setting, ops) {
  const ui = getWorkspaceState(view);ui[setting] = !ui[setting];
  repaint(context, view, ops, true);
}

function closeDrawingContextMenu(view, focusCanvas = false) {
  const menu = view.iplaneContextMenu;
  if (!menu) return false;
  const wasVisible = menu.element.isConnected;
  menu.close();delete view.iplaneContextMenu;
  synchronizeDrawingDynamicInput(view);
  refreshSelectedDimensionPresentation(view);
  if (focusCanvas) view.viewport?.root?.querySelector("canvas")?.focus({ preventScroll: true });
  return wasVisible;
}

function openDrawingContextMenu(state, event) {
  const { context, view } = state, ui = getWorkspaceState(view);
  if (!isGeometryEditing(view)) return;
  const drawing = ui.activeTool.startsWith("plane.draw-");
  if (!drawing && !(ui.activeTool === "plane.select" && canCloseSelectedPolyline(context, view))) return;
  event.preventDefault();event.stopPropagation();closeDrawingToolPaletteMenu(view);closeDrawingContextMenu(view);
  clearDrawingGesture(view);
  const polyline = drawing && ui.activeTool === "plane.draw-polyline", pending = ui.polylinePending ?? [];
  const idle = !ui.busy && !ui.drawingCommitting;
  const items = drawing ? [
    { id: "close-drawing", label: "闭合", enabled: idle && polyline && drawingStep(view).canClose },
    { id: "cancel-drawing", label: "取消" },
    { id: "finish-drawing", label: "结束", enabled: idle && (polyline || ui.activeTool === "plane.draw-bspline" ? drawingStep(view).canFinish : ui.commandPoints.length > 0 || ui.drawingCreatedCount > 0),
      ...(ui.activeTool === "plane.draw-bspline" && ui.commandPoints.length < 4 ? { hint: "至少需要4个控制点。" } : {}) },
  ] : [{ id: "close-polyline", label: "闭合轮廓", enabled: idle }];
  if (polyline) {
    const segmentTypes = polylineSegmentKinds.filter(([kind]) => ui.geometryCapabilities?.segmentTypes?.includes(kind));
    const blocked = pending.length > 0 && (ui.polylineMode !== "bspline" || pending.length < 3);
    const label = polylineSegmentKinds.find(([kind]) => kind === ui.polylineMode)?.[1] ?? "当前曲段";
    const hint = blocked ? ui.polylineMode === "bspline" ? "B样条尚未完成，至少需要4个控制点（含起点）。" : `${label}尚未完成，请${drawingStep(view).label}。` : "";
    items.unshift({ id: "polyline-segments", label: "切换曲段", submenuHint: hint, children: segmentTypes.map(([kind, title]) => ({
      id: `polyline-kind-${kind}`, label: title, kind, checked: ui.polylineMode === kind, enabled: idle && (!blocked || ui.polylineMode === kind),
    })) });
    if (ui.polylineMode === "bspline" && pending.length) items.splice(1, 0, {
      id: "finish-polyline-segment", label: "完成本段", enabled: idle && pending.length >= 3,
      hint: pending.length < 3 ? "至少需要4个控制点（含起点）。" : "",
    });
  }
  const menu = createPlanarContextMenu({
    mount: context.mount, boundsElement: view.viewport?.root?.querySelector("canvas"),
    clientX: event.clientX, clientY: event.clientY, label: drawing ? "绘图操作" : "轮廓操作", items,
    rootAttributes: { "data-iplane-context-menu": "", class: "iplane-context-menu" },
    itemAttributes: item => item.children ? {} : item.kind ? { "data-iplane-action": "polyline-kind", "data-iplane-polyline-kind": item.kind }
      : { "data-iplane-action": item.id },
  });
  view.iplaneContextMenu = { ...menu, tool: ui.activeTool, epoch: ui.drawingEpoch };
  synchronizeDrawingDynamicInput(view);
  refreshSelectedDimensionPresentation(view);
}

function attachDrawingWindowEvents(mount, view) {
  if (view.iplaneDrawingEventCleanup) return;
  const window = mount.ownerDocument.defaultView;
  const current = () => {
    const state = mounted.get(mount);
    return state?.view === view && !view.disposed && state.context.isCurrentProject?.() !== false ? state : null;
  };
  const escape = event => {
    const state = current();
    if (event.key !== "Escape" || !state || !isGeometryEditing(view)) return;
    if (closeDrawingToolPaletteMenu(view, true)) { event.preventDefault();event.stopPropagation();return; }
    const document = mount.ownerDocument;
    const ownedDraftAtBody = Boolean(view.iplaneSelectedDimensions?.draft)
      && [document.body, document.documentElement].includes(document.activeElement)
      && [document.body, document.documentElement].includes(event.target)
      && ![...document.querySelectorAll("dialog[open],[aria-modal='true'],.modal-backdrop")].some(node => node.getClientRects().length > 0);
    if (!mount.contains(event.target) && !mount.contains(document.activeElement) && !ownedDraftAtBody) return;
    if (view.iplaneContextMenu?.closeSubmenu()) { event.preventDefault();event.stopPropagation();return; }
    if (closeDrawingContextMenu(view, true)) { event.preventDefault();event.stopPropagation();return; }
    if (cancelSelectedDimensionEdit(view)) { event.preventDefault();event.stopPropagation();return; }
    if (!getWorkspaceState(view).activeTool.startsWith("plane.draw-")) return;
    event.preventDefault();event.stopPropagation();cancelDrawing(view);repaint(state.context, view, state.ops, true);
  };
  const blur = () => { if (current()) { closeDrawingContextMenu(view);if (getWorkspaceState(view).activeTool.startsWith("plane.draw-")) clearDrawingGesture(view); } };
  const outside = event => {
    if (!current() || !view.iplaneContextMenu || view.iplaneContextMenu.element.contains(event.target)) return;
    const wasVisible = view.iplaneContextMenu.element.isConnected;
    closeDrawingContextMenu(view);
    // Dismissing a menu on the worktable must not also add a drawing vertex.
    if (wasVisible && event.button === 0 && event.target.closest?.(".icax-three-viewport canvas")) { event.preventDefault();event.stopPropagation(); }
  };
  const resize = () => { if (current()) closeDrawingContextMenu(view); };
  window.addEventListener("keydown", escape, { capture: true });window.addEventListener("blur", blur);
  window.addEventListener("pointerdown", outside, { capture: true });window.addEventListener("resize", resize);
  view.iplaneDrawingEventCleanup = () => {
    window.removeEventListener("keydown", escape, { capture: true });window.removeEventListener("blur", blur);
    window.removeEventListener("pointerdown", outside, { capture: true });window.removeEventListener("resize", resize);
    delete view.iplaneDrawingEventCleanup;
  };
}

function attachPanels(context, view, mount, ops) {
  const existing = mounted.get(mount);
  if (existing && (existing.view !== view || existing.context.sceneProxy !== context.sceneProxy)) { closeDrawingToolPaletteMenu(existing.view);closeDrawingContextMenu(existing.view);clearSelectedDimensionAnnotations(existing.view);cancelDrawing(existing.view); }
  mounted.set(mount, { context, view, ops });
  view.iplaneLive = { context, ops };
  synchronizePresentation(context, view, mount);
  synchronizeViewport(context, view);
  fitPendingCanvas(context, view);
  synchronizeDrawingDynamicInput(view);
  synchronizeSelectedDimensionAnnotations(context, view, ops);
  synchronizeNodeEditor(context, view, ops);
  synchronizeDrawingToolPalette(context, view, { onCommand(commandId) {
    const live = mounted.get(mount);
    if (!live || live.view !== view || live.context.isCurrentProject?.() === false) return;
    void runPlaneOperation(live, async () => {
      await activateDrawingTool(live.context, view, live.ops, commandId);
      view.viewport?.root?.querySelector("canvas")?.focus({ preventScroll: true });
    });
  } });
  context.actions?.refreshBottomDock?.();
  attachDrawingWindowEvents(mount, view);
  if (existing) return;
  const run = runPlaneOperation;
  mount.addEventListener("click", event => {
    const layerTarget = event.target.closest?.("[data-layer-ui-action]");
    const layerState = mounted.get(mount);
    if (layerTarget && layerState && !layerTarget.disabled && layerState.context.isCurrentProject?.() !== false) {
      event.preventDefault();event.stopPropagation();
      void run(layerState, () => handlePlaneLayerAction(layerState.context, layerState.view, layerState.ops, layerTarget));return;
    }
    const target = event.target.closest?.("[data-iplane-action]");
    const state = mounted.get(mount);
    if (!target || target.disabled || !state || state.context.isCurrentProject?.() === false) return;
    const { context, view, ops } = state, action = target.dataset.iplaneAction;
    if (target.closest("[data-iplane-context-menu]")) closeDrawingContextMenu(view, true);
    if (action.startsWith("artwork-")) {
      void run(state, () => handleArtworkAction(context, view, ops, action, target));
    } else if (action === "select-object") {
      const ui = getWorkspaceState(view);
      if (ui.viewportKey && view.viewport) ui.cameraByArea[ui.viewportKey] = view.viewport.getCameraState();
      selectObject(view, target.dataset.iplaneObjectKind, target.dataset.iplaneObjectId, event.ctrlKey || event.metaKey || event.shiftKey);
      if (isGeometryEditing(view) && ui.activeTool.startsWith("plane.edit-")) ui.panelMode = "drawing-edit";
      repaint(context, view, ops);
    } else if (action === "sketch-open") {
      void run(state, () => openSketch(context, view, ops, target.dataset.iplaneObjectId));
    } else if (action === "sketch-complete") {
      void run(state, () => completeSketch(context, view, ops));
    } else if (action === "sketch-cancel") {
      void run(state, () => cancelSketch(context, view, ops));
    } else if (action === "cancel-geometry-task") {
      void run(state, () => context.sceneProxy.invoke("PlaneTask.Cancel", { taskId: getWorkspaceState(view).geometryTaskId }));
    } else if (action.startsWith("toggle-")) {
      const setting = { snap: "snapEnabled", grid: "gridVisible", ortho: "orthoEnabled" }[action.slice(7)];
      if (setting) toggle(context, view, setting, ops);
    } else if (action === "reset-draft") {
      clearSelectedDraft(context, view);getWorkspaceState(view).operationError = "";repaint(context, view, ops);
    } else if (action === "apply-properties") {
      void run(state, () => saveObject(context, view, ops));
    } else if (action === "import-with-units") {
      void run(state, () => importWithUnits(context, view, ops));
    } else if (action === "resume-import") {
      const ui = getWorkspaceState(view);ui.panelMode = "import-units";ui.panelEpoch = (ui.panelEpoch ?? 0) + 1;repaint(context, view, ops);
    } else if (action === "cancel-import") {
      const ui = getWorkspaceState(view);ui.pendingImport = null;delete ui.formDrafts["import-units"];ui.panelMode = "object";ui.panelEpoch = (ui.panelEpoch ?? 0) + 1;repaint(context, view, ops);
    } else if (action === "close-panel") {
      const ui = getWorkspaceState(view);ui.panelMode = "object";ui.panelEpoch = (ui.panelEpoch ?? 0) + 1;repaint(context, view, ops);
    } else if (action === "reset-form") {
      const ui = getWorkspaceState(view);delete ui.formDrafts[target.dataset.iplaneForm];ui.operationError = "";repaint(context, view, ops);
    } else if (action === "create-rectangle") {
      const ui = getWorkspaceState(view), draft = ui.formDrafts["stock-rect"] ?? {};
      const submitted = JSON.stringify(draft);
      const panelEpoch = ui.panelEpoch ?? 0;
      void run(state, async () => {
        const response = await invokePlane(context, view, ops, "PlaneStock.CreateRectangle", { name: draft.name ?? "矩形板材", width: Number(draft.width ?? 3000), height: Number(draft.height ?? 1500), quantity: Number(draft.quantity ?? 1) }, { afterCommand() { if (JSON.stringify(ui.formDrafts["stock-rect"] ?? {}) === submitted) delete ui.formDrafts["stock-rect"]; } });
        if (response?.id && (ui.panelEpoch ?? 0) === panelEpoch) { selectObject(view, "stocks", String(response.id));repaint(context, view, ops);view.viewport?.fitView(); }
      });
    } else if (action === "apply-nesting-settings") {
      const ui = getWorkspaceState(view), source = view.scene?.iplaneCam?.nestingParameters ?? {}, draft = ui.formDrafts["nesting-settings"] ?? {};
      const submitted = JSON.stringify(draft);
      const parameters = Object.fromEntries(["partGap", "stockMargin", "rotationStepDeg", "timeLimitSeconds"].map(key => [key, Number(draft[key] ?? source[key] ?? ({ partGap: 5, stockMargin: 5, rotationStepDeg: 90, timeLimitSeconds: 10 }[key]))]));
      parameters.allowMirror = Boolean(draft.allowMirror ?? source.allowMirror ?? false);
      void run(state, () => invokePlane(context, view, ops, "PlaneNesting.SetParameters", parameters, { refreshView: false, afterCommand() { if (JSON.stringify(ui.formDrafts["nesting-settings"] ?? {}) === submitted) delete ui.formDrafts["nesting-settings"]; } }));
    } else if (action === "apply-node-edit") {
      void run(state, () => applyNodeEdit(context, view, ops));
    } else if (action === "apply-drawing-edit") {
      const ui = getWorkspaceState(view), selected = getSelectedObject(context, view), operation = ui.activeTool.replace("plane.edit-", "");
      void run(state, async () => {
        if (selected?.kind !== geometryObjectKind(view)) throw new Error("请先选择刀路或草图曲线。");
        const draft = ui.formDrafts["drawing-edit"] ?? {};
        const submitted = JSON.stringify(draft);
        const parameters = operation === "move" || operation === "copy" ? { dx: Number(draft.dx ?? 0), dy: Number(draft.dy ?? 0) }
          : operation === "rotate" ? { angleDeg: Number(draft.angleDeg ?? 0) }
          : operation === "mirror" ? { axis: draft.axis ?? "y", cx: Number(draft.cx ?? 0), cy: Number(draft.cy ?? 0) }
          : operation === "array-rectangular" ? { rows: Number(draft.rows ?? 2), columns: Number(draft.columns ?? 2), spacingX: Number(draft.spacingX ?? 100), spacingY: Number(draft.spacingY ?? 100), async: true }
          : operation === "array-polar" ? { count: Number(draft.count ?? 4), cx: Number(draft.cx ?? 0), cy: Number(draft.cy ?? 0), angleDeg: Number(draft.angleDeg ?? 360), async: true }
          : { factor: Number(draft.factor ?? 1) };
        const response = await invokeGeometry(context, view, ops, "Edit", { id: selected.row.id, operation, ...parameters }, { refreshView: !operation.startsWith("array-"), retainOwnedResponse: Boolean(parameters.async) });
        const completed = response?.taskId ? await waitGeometryTask(context, view, ops, response) : response;
        if (completed && JSON.stringify(ui.formDrafts["drawing-edit"] ?? {}) === submitted) delete ui.formDrafts["drawing-edit"];
        if (response?.taskId && completed && context.isCurrentProject?.() !== false && !view.disposed) { await refreshPlaneState(context, view, ops, { refreshView: true });context.actions?.refreshNativeProjectHistory?.({ sceneProxy: context.sceneProxy, projectId: context.project?.projectId }); }
      });
    } else if (action === "polyline-kind") {
      void runPlaneOperation(state, () => setPolylineMode(context, view, target.dataset.iplanePolylineKind));
    } else if (action === "finish-polyline-segment") {
      void runPlaneOperation(state, () => finishPolylineSegment(view));
    } else if (action === "submit-drawing-input") {
      const ui = getWorkspaceState(view), input = ui.commandInput, epoch = ui.drawingEpoch;
      void run(state, async () => { await runDrawingInput(context, view, ops, input);if (ui.drawingEpoch === epoch && ui.commandInput === input) ui.commandInput = ""; });
    } else if (action === "finish-drawing") {
      void run(state, () => finishDrawing(context, view, ops));
    } else if (action === "close-drawing" || action === "close-polyline") {
      void run(state, () => closePolyline(context, view, ops));
    } else if (action === "cancel-drawing") {
      cancelDrawing(view);repaint(context, view, ops, true);
    } else if (action === "cancel-nesting") {
      const ui = getWorkspaceState(view);
      void run(state, () => invokePlane(context, view, ops, "PlaneNesting.Cancel", { taskId: ui.runningTaskId }, { refreshView: false }));
    } else if (action === "download-report") {
      void run(state, async () => { if (await readReport(context, view, ops)) downloadReport(view); });
    }
  });
  mount.addEventListener("focusin", event => {
    const state = mounted.get(mount), field = event.target?.dataset?.iplaneDimension;
    if (field && state && state.context.isCurrentProject?.() !== false) focusDrawingDimensionField(state.view, field);
  });
  mount.addEventListener("dblclick", event => {
    const state = mounted.get(mount), target = event.target.closest?.("[data-iplane-dimension-edit]");
    if (!target || !state || state.context.isCurrentProject?.() === false) return;
    event.preventDefault();event.stopPropagation();beginSelectedDimensionEdit(state.view, target.dataset.iplaneDimensionEdit);
  });
  mount.addEventListener("input", event => {
    const state = mounted.get(mount);if (!state || state.context.isCurrentProject?.() === false) return;
    const { context, view, ops } = state;
    if (event.target.matches?.("[data-iplane-drawing-option]")) {
      const ui = getWorkspaceState(view), primitive = event.target.dataset.iplaneDrawingOptionPrimitive, key = event.target.dataset.iplaneDrawingOption;
      if (!ui.geometryCapabilities?.primitiveOptions?.[primitive]?.[key]) return;
      ui.drawingShapeOptions ??= {};ui.drawingShapeOptions[primitive] ??= {};ui.drawingShapeOptions[primitive][key] = event.target.value;
      ensureDrawingDynamicInput(view);
      if (ui.pointerPoint) previewDrawingPoint(view, ui.pointerPoint);
      return;
    }
    if (event.target.matches?.("[data-layer-ui-field]")) { inputPlaneLayer(view, event.target);repaint(context, view, ops);return; }
    const selectedDimension = event.target?.dataset?.iplaneSelectedDimension;
    if (selectedDimension && updateSelectedDimensionDraft(view, selectedDimension, event.target.value)) return;
    const dimension = event.target?.dataset?.iplaneDimension;
    if (dimension && updateDrawingDimensionDraft(view, dimension, event.target.value)) {
      const point = drawingDimensionMousePoint(view);if (point) previewDrawingPoint(view, point);
      return;
    }
    if (event.target.matches?.("[data-iplane-command-input],[data-iplane-draw-value]")) {
      updateCommandInput(context, view, event.target);
      return;
    }
    if (event.target.matches?.("[data-iplane-form-field]")) {
      const ui = getWorkspaceState(view), form = event.target.dataset.iplaneForm;
      ui.formDrafts[form] ??= {};ui.formDrafts[form][event.target.dataset.iplaneFormField] = event.target.type === "checkbox" ? event.target.checked : event.target.value;return;
    }
    if (!event.target.matches?.("[data-iplane-field]")) return;
    const selected = getSelectedObject(context, view);
    if (!selected || event.target.dataset.iplaneOwner !== draftKey(selected.kind, selected.row)) return;
    updateDraft(view, selected.kind, selected.row, event.target.dataset.iplaneField, event.target.value);
    repaint(context, view, ops);
  });
  mount.addEventListener("change", event => {
    const state = mounted.get(mount);
    if (!state || state.context.isCurrentProject?.() === false || !event.target.matches?.("[data-layer-ui-field]")) return;
    void run(state, () => changePlaneLayer(state.context, state.view, state.ops, event.target));
  });
  mount.addEventListener("keydown", event => handlePlaneKey(mounted.get(mount), event));
  mount.addEventListener("contextmenu", event => {
    const state = mounted.get(mount);
    if (!state || state.context.isCurrentProject?.() === false || !event.target.closest?.(".icax-three-viewport canvas")) return;
    openDrawingContextMenu(state, event);
  });
  mount.addEventListener("pointerdown", event => {
    const state = mounted.get(mount);
    if (!state || state.context.isCurrentProject?.() === false || event.button !== 0 || !event.target.closest?.(".icax-three-viewport canvas") || !drawingDragKey(state.view)) return;
    const ui = getWorkspaceState(state.view), point = drawingPointerPoint(state.view, event);
    if (point) ui.drawGesture = { pointerId: event.pointerId, x: event.clientX, y: event.clientY, point, tool: ui.activeTool, epoch: ui.drawingEpoch, moved: false };
  }, { capture: true });
  mount.addEventListener("pointermove", event => {
    const state = mounted.get(mount);
    if (!state || state.context.isCurrentProject?.() === false || !isGeometryEditing(state.view) || !event.target.closest?.(".icax-three-viewport canvas")) return;
    const ui = getWorkspaceState(state.view);if (!ui.activeTool.startsWith("plane.draw-") || ui.busy) return;
    const gesture = ui.drawGesture;
    if (gesture?.pointerId === event.pointerId && Math.hypot(event.clientX - gesture.x, event.clientY - gesture.y) > 3) gesture.moved = true;
    const origin = (ui.activeTool === "plane.draw-polyline" ? ui.polylinePending?.at(-1) : null) ?? ui.commandPoints.at(-1) ?? (gesture?.moved ? gesture.point : null);
    const point = drawingPointerPoint(state.view, event, origin);if (point) previewDrawingPoint(state.view, point);
  });
  const clearGesture = event => {
    const state = mounted.get(mount);if (!state || state.context.isCurrentProject?.() === false) return;
    const gesture = getWorkspaceState(state.view).drawGesture;
    // The SDK releases capture before delivering its completed drag callback.
    // A normal pointerup owns cleanup until that callback has consumed the anchor.
    if (event.type === "lostpointercapture" && gesture?.releasing) return;
    if (gesture?.pointerId === event.pointerId) clearDrawingGesture(state.view);
  };
  mount.addEventListener("pointerup", event => {
    const state = mounted.get(mount), gesture = state && getWorkspaceState(state.view).drawGesture;
    if (gesture?.pointerId === event.pointerId) gesture.releasing = true;
  }, { capture: true });
  for (const name of ["pointerup", "pointercancel", "lostpointercapture"]) mount.addEventListener(name, clearGesture);
}

function withPlaneContext(context) {
  const view = getProjectView(context.project?.projectId ?? ""), ui = getWorkspaceState(view);
  return {
    ...context, iplaneCamUI: ui, forceThreeViewport: true, showViewportGrid: ui.gridVisible,
    noticePresentation: "log", errorPresentation: "log", onlyHandleAreaRibbonCommand: true,
    areaTitleOverrides: AREA_TITLES, normalizeAreaId,
    areaRenderers: Object.fromEntries(AREA_IDS.map(area => [area, { left: renderWorkspaceLeft, right: renderWorkspaceRight }])),
    resolveWorkbenchPresentation: (_context, current) => ({ className: "iplane-workspace", attributes: `data-iplane-area="${normalizeAreaId(current.activeAreaId)}"` }),
    renderWorkbenchSuffix: renderWorkspaceBottom,
    renderSceneStatusBar,
    renderViewportOverlay: (ctx, current) => renderDrawingDynamicInput() + renderSelectedDimensionAnnotations() + renderDrawingToolPalette() + `<div data-iplane-layer-host data-iplane-key="layer-host">${renderPlaneLayers(ctx, current)}</div>`,
    onPlaneSketchContextChanged(current) { cancelDrawing(current);clearSelectedDimensionAnnotations(current); },
    viewportOptions: { ...context.viewportOptions, projectionMode: "orthographic", showProjectionToggle: false, immediateBlankPick: true, blankDoubleClickFitEnabled: false,
      showOrientationGizmo: false, zoomToPointer: true, getCameraNavigationMode: event => event.button === 1 ? "pan" : null,
      onCameraChange(camera, change) {
        if (ui.viewportKey) ui.cameraByArea[ui.viewportKey] = camera;
        if (["pan", "orbit", "zoom", "wheel"].includes(change?.reason)) ui.fitCanvasKey = "";
        synchronizeDrawingDynamicInput(view);refreshSelectedDimensionPresentation(view);
        context.viewportOptions?.onCameraChange?.(camera, change);
      } },
    configureViewport(_context, current, area) {
      current.viewport?.setProjectionMode("orthographic");
      current.viewport?.setProjectionToggleVisible?.(false);
      current.viewport?.setBlankDoubleClickFitEnabled?.(false);
      const cube = _context.mount?.querySelector("[data-cam-viewcube]");if (cube) cube.hidden = true;
      const key = viewportKey(_context, current);
      if (ui.viewportKey !== key) {
        ui.viewportKey = key;
        const saved = ui.cameraByArea[key];
        if (saved) current.viewport?.setCameraState(saved);
        else if (current.viewport) {
          current.viewport.setStandardView("top");
          // At the Z pole the SDK keeps the meridian selected by theta.
          // The plane worktable uses the conventional X-right / Y-up view.
          current.viewport.setCameraState({ ...current.viewport.getCameraState(), theta: -Math.PI / 2 });
          ui.fitCanvasKey = key;
        }
      }
    },
    beforeProjectRender(_context, current) {
      if (current.activeAreaId !== normalizeAreaId(_context.activeRibbonTabId)) { closeDrawingToolPaletteMenu(current);clearSelectedDimensionAnnotations(current); }
      const menu = current.iplaneContextMenu;
      if (menu && (!menu.element.isConnected || current.activeAreaId !== normalizeAreaId(_context.activeRibbonTabId)
          || menu.tool !== ui.activeTool || menu.epoch !== ui.drawingEpoch)) closeDrawingContextMenu(current);
      if (ui.viewportKey && current.viewport) ui.cameraByArea[ui.viewportKey] = current.viewport.getCameraState();
      if (current.activeAreaId !== normalizeAreaId(_context.activeRibbonTabId)) { ui.panelMode = "object";ui.panelEpoch = (ui.panelEpoch ?? 0) + 1; }
      if (current.activeAreaId !== "nesting" && normalizeAreaId(_context.activeRibbonTabId) === "nesting") {
        const changed = ui.activeTool !== "plane.select";
        cancelDrawing(current);
        ui.commandStatus = "选择零件查看参数，或在下方选择板材与排样结果。";
        if (changed) _context.actions?.refreshProductRibbon?.();
      }
    },
    tryRenderProjectPatch(_context, current, mount) { return patchPanels(_context, current, mount); },
    afterProjectPatch(_context, current, mount, ops) {
      _context.configureViewport(_context, current, current.activeAreaId);attachPanels(_context, current, mount, ops);
    },
    afterProjectRender: attachPanels,
    getAreaViewportRectangleSelectionKey(_context, current) {
      const live = mounted.get(_context.mount);
      return live?.view === current && live.context.isCurrentProject?.() !== false ? drawingDragKey(current) : null;
    },
    handleAreaViewportRectangleSelect(_context, current, _rectangle, event, ops) {
      const live = mounted.get(_context.mount);
      if (!live || live.view !== current || live.context.isCurrentProject?.() === false) return true;
      _context = live.context;ops = live.ops;
      const ui = getWorkspaceState(current);
      if (!drawingDragKey(current)) return true;
      void acceptDrawingDrag(_context, current, ops, event).catch(error => {
        if (!ui.operationError) ops.appendProjectLog?.(_context, "error", error?.message ?? String(error));ui.operationError = userErrorMessage(error);
      }).finally(() => repaint(_context, current, ops, true));
      return true;
    },
    handleAreaViewportPick(_context, current, userData, _hit, event, _hits, ops) {
      // The shared viewport survives navigation. Its original onPick closure
      // must dispatch through the latest mounted page, not its creation page.
      const live = mounted.get(_context.mount);
      if (!live || live.view !== current || live.context.isCurrentProject?.() === false) return true;
      _context = live.context;ops = live.ops;
      const ui = getWorkspaceState(current);
      if (ui.busy) return true;
      if (ui.activeTool === "plane.text" && ui.panelMode === "artwork:text-create") {
        const point = drawingPointerPoint(current, event);
        if (point && placeArtworkText(current, point)) repaint(_context, current, ops);
        return true;
      }
      if (ui.activeTool.startsWith("plane.draw-")) {
        if (!isGeometryEditing(current)) return true;
        const point = drawingPointerPoint(current, event);
        if (point) void acceptDrawingPointerPoint(_context, current, ops, point).catch(error => { if (!ui.operationError) ops.appendProjectLog?.(_context, "error", error?.message ?? String(error));ui.operationError = userErrorMessage(error); }).finally(() => repaint(_context, current, ops, true));
        return true;
      }
      const entityId = String(userData?.objectId ?? userData?.entityId ?? "");
      const row = getAreaRows(_context, current).find(item => item.entityIds.includes(entityId));
      if (row?.referenceOnly === true) return true;
      if (ui.activeTool === "plane.erase") {
        if (!isGeometryEditing(current) || !row || row.objectKind !== geometryObjectKind(current) || row.visible === false || ui.geometryTaskId || ui.drawingCommitting) return true;
        if (row.locked === true) { ui.commandStatus = "当前图层已锁定，不能擦除。";repaint(_context, current, ops);return true; }
        void runPlaneOperation(live, () => invokeGeometry(_context, current, ops, "Edit", { id: row.id, operation: "delete" }));
        return true;
      }
      selectObject(current, row?.objectKind ?? (isGeometryEditing(current) ? geometryObjectKind(current) : "parts"), row?.id ?? "", event?.ctrlKey || event?.shiftKey || event?.metaKey);
      if (ui.activeTool.startsWith("plane.edit-")) ui.panelMode = "drawing-edit";
      repaint(_context, current, ops);return true;
    },
    async handleAreaRibbonCommand(_context, current, commandId, ops) {
      closeDrawingContextMenu(current);
      if (commandId === "plane.select" || /^plane\.(draw|edit)-/.test(commandId)) clearSelectedDimensionAnnotations(current);
      const state = getWorkspaceState(current);
      const settings = { "plane.snap-toggle": "snapEnabled", "plane.grid-toggle": "gridVisible", "plane.ortho-toggle": "orthoEnabled" };
      if (settings[commandId]) { toggle(_context, current, settings[commandId], ops);return true; }
      if (commandId === "plane.sketch-open") { await openSketch(_context, current, ops);return true; }
      if (commandId === "plane.sketch-complete") { await completeSketch(_context, current, ops);return true; }
      if (commandId === "plane.sketch-cancel") { await cancelSketch(_context, current, ops);return true; }
      if (commandId.startsWith("plane.polyline-kind-")) { setPolylineMode(_context, current, commandId.slice("plane.polyline-kind-".length));repaint(_context, current, ops, true);return true; }
      if (commandId === "plane.edit-close") { await closePolyline(_context, current, ops);return true; }
      if (commandId === "plane.edit-delete") { const selected = getSelectedObject(_context, current);if (selected?.kind === geometryObjectKind(current)) await invokeGeometry(_context, current, ops, "Edit", { id: selected.row.id, operation: "delete" });return true; }
      if (commandId === "view.fit") { current.viewport?.fitView();return true; }
      if (commandId === "view.reset-layout") {
        current.layout = { leftWidth: 320, rightWidth: 340, bottomHeight: 180 };repaint(_context, current, ops);return true;
      }
      if (commandId === "plane.import-dxf" || commandId === "plane.import-parts" || commandId === "plane.import-stocks") {
        await importDxf(_context, current, ops, { "plane.import-dxf": "drawing", "plane.import-parts": "parts", "plane.import-stocks": "stocks" }[commandId]);return true;
      }
      if (["plane.stock-rect", "plane.nesting-settings"].includes(commandId)) { state.panelMode = commandId === "plane.stock-rect" ? "stock-rect" : "nesting-settings";state.panelEpoch = (state.panelEpoch ?? 0) + 1;repaint(_context, current, ops);return true; }
      if (commandId === "plane.nesting-run") { await startNesting(_context, current, ops);return true; }
      if (["plane.layout-report", "plane.layout-properties"].includes(commandId)) { await readReport(_context, current, ops);return true; }
      if (commandId === "plane.delete-part" || commandId === "plane.copy-part") {
        const selected = getSelectedObject(_context, current);
        if (selected?.kind !== "parts") throw new Error("请先选择零件。");
        await invokePlane(_context, current, ops, commandId === "plane.delete-part" ? "PlaneObject.Remove" : "PlaneObject.Duplicate", { id: selected.row.id, kind: "parts" });return true;
      }
      if (["plane.select", "plane.erase", "plane.text", "plane.image"].includes(commandId) || /^plane\.(draw|edit)-/.test(commandId)) {
        await activateDrawingTool(_context, current, ops, commandId);repaint(_context, current, ops, true);return true;
      }
      if (commandId === "plane.layers") { openLayerManager(current);repaint(_context, current, ops);return true; }
      return false;
    },
  };
}

export function getRibbonDefinition(context = {}) {
  const view = context.project?.projectId ? getProjectView(context.project.projectId) : { iplaneCamUI: undefined };
  synchronizeAvailability(context, view);
  return defineRibbon({ ...context, iplaneCamUI: getWorkspaceState(view) });
}

export function mountProduct(context) { ensureWorkspaceStyles();ensurePlaneLayerStyles(context.mount?.ownerDocument);return workbench.mountProduct(context); }

export async function mountProject(context) {
  if (!context.project?.projectId || context.isCurrentProject?.() === false) return;
  ensureWorkspaceStyles();
  ensurePlaneLayerStyles(context.mount?.ownerDocument);
  const view = getProjectView(context.project.projectId);
  const incoming = context.scene ?? context.sceneProxy?.state ?? {};
  view.scene = { ...incoming, iplaneCam: incoming.iplaneCam ?? view.scene?.iplaneCam ?? {} };
  const wrapped = withPlaneContext(context);
  workbench.mountProject(wrapped);
  await refreshPlaneState(wrapped, view, view.iplaneLive?.ops, { render: true });
  await synchronizePlaneView(wrapped, view, view.iplaneLive?.ops);
  if (view.disposed || context.isCurrentProject?.() === false) return;
  synchronizeViewport(wrapped, view);
  const running = view.scene?.tasks?.find(task => ["Running", "Queued", "CancelRequested"].includes(String(task.status)));
  if (running?.taskId) watchTask(wrapped, view, view.iplaneLive?.ops, String(running.taskId));
  else { const ui = getWorkspaceState(view);ui.runningTaskId = "";ui.taskWatchEpoch = (ui.taskWatchEpoch ?? 0) + 1;clearTimeout(view.iplaneTaskTimer); }
}

export function handleRibbonCommand(context, commandId) { return workbench.handleRibbonCommand(withPlaneContext(context), commandId); }
export function synchronizeActiveAreaView(context, expectation) { return workbench.synchronizeActiveAreaView(withPlaneContext(context), expectation); }
export function getBottomDockDefinition() {
  return { defaultTabId: "command", panels: [{ id: "command", title: "命令" }] };
}
export function mountBottomDockPanel(context, panelId) {
  if (panelId !== "command") return;
  const view = getProjectView(context.project?.projectId ?? ""), mount = context.mount;
  if (!mount || view.disposed) return;
  const existing = commandPanelStates.get(mount);
  commandPanelStates.set(mount, { context, view });
  view.iplaneCommandMount = mount;
  const restore = capturePaneInteraction(mount, ["[data-iplane-command-root]"]);
  patchContents(mount, renderCommandPanel(context, view));restore();
  if (existing) return;
  const current = () => {
    const owner = commandPanelStates.get(mount);
    if (!owner || owner.view.disposed || owner.context.isCurrentProject?.() === false) return null;
    const live = owner.view.iplaneLive;
    return live?.context?.sceneProxy === owner.context.sceneProxy ? { ...live, view: owner.view } : null;
  };
  mount.addEventListener("input", event => {
    if (!event.target.matches?.("[data-iplane-command-input]")) return;
    const state = current();if (state) updateCommandInput(state.context, state.view, event.target);
  });
  mount.addEventListener("keydown", event => handlePlaneKey(current(), event));
}
export async function prepareProjectSave(context) {
  await workbench.waitForActiveAreaAction(context);
  const view = findProjectView(context.project?.projectId);
  if (view?.iplaneOperationDone) await view.iplaneOperationDone;
  if (view) await flushPlaneLayerEdits(view.iplaneLive?.context ?? withPlaneContext(context), view, view.iplaneLive?.ops);
  if (view && hasPropertyDrafts(view)) throw new Error("属性有未应用的改动，请先应用或还原后再保存工程。");
  if (view && Object.values(getWorkspaceState(view).formDrafts).some(form => Object.keys(form).length > 0)) throw new Error("参数面板有未应用的改动，请先应用或还原后再保存工程。");
}
export function getWindowCloseGuard(context) {
  const view = findProjectView(context.project?.projectId), ui = view && getWorkspaceState(view);
  return { blocked: Boolean(ui?.busy || ui?.runningTaskId || ui?.geometryTaskId || planeLayerPending(view)), message: "当前操作尚未结束，请等待完成或取消操作后关闭工程。" };
}
export async function onProjectHistoryChanged(context) {
  const view = findProjectView(context.project?.projectId);if (!view) return;
  const wrapped = withPlaneContext(context);await refreshPlaneState(wrapped, view, view.iplaneLive?.ops, { refreshView: true });
}
export function releaseProject(context) {
  const view = findProjectView(context.project?.projectId);
  if (view) { clearTimeout(view.iplaneTaskTimer);disposeDrawingToolPalette(view);closeDrawingContextMenu(view);clearSelectedDimensionAnnotations(view);cancelDrawing(view);disposeDrawingPreview(view);view.iplaneDrawingEventCleanup?.();view.iplaneCommandMount = null; }
  return releaseWorkbenchProject(context);
}
