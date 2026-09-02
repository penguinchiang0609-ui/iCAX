import { getEnabledMachines, getJobMachineId, getMachineDefinitions, getMachineId, getMachines } from "../state/sceneSelectors.mjs";

export function exposeLaserCamAutomation(context, view, commands) {
  window.__icaxLaser3DCAM ??= {};
  window.__icaxLaser3DCAM.getState = () => ({
    projectId: context.project?.projectId ?? "",
    sceneId: context.sceneProxy?.sceneId ?? "",
    selectedMachineDefinitionId: view.selectedMachineDefinitionId ?? "",
    selectedMachineInstanceId: view.selectedMachineInstanceId ?? "",
    machineDefinitionCount: getMachineDefinitions(view.scene).length,
    machineInstanceCount: getMachines(view.scene).length,
    enabledMachineInstanceCount: getEnabledMachines(view.scene).length,
    machineInstances: getMachines(view.scene).map((machine) => ({
      id: getMachineId(machine),
      name: machine.name ?? machine.modelName ?? "",
      enabled: machine.enabled !== false,
    })),
    jobMachineId: getJobMachineId(view.scene),
    modelLoaded: Boolean(view.scene?.model?.isLoaded),
    modelSourcePath: view.scene?.model?.sourcePath ?? "",
    tubeGeometry: view.scene?.tubeGeometry ?? null,
    selectedCADIntentNodeId: view.selectedCADIntentNodeId ?? "",
    livePreviewNodeId: view.cadIntentLivePreviewNodeId ?? "",
    livePreviewResourceUrl: view.cadIntentLivePreviewResourceUrl ?? "",
    livePreviewResourceVersion: view.cadIntentLivePreviewResourceVersion ?? 0,
    error: view.error ?? "",
  });
  window.__icaxLaser3DCAM.getViewportDebugState = (options = {}) =>
    view.viewport?.getDebugState?.({
      samplePixels: options.samplePixels ?? true,
      includeObjects: options.includeObjects ?? false,
    }) ?? null;
  window.__icaxLaser3DCAM.waitForRenderableViewport = async (options = {}) =>
    waitForRenderableViewport(view, options);
  window.__icaxLaser3DCAM.importMachineDefinition = async (sourcePath) => {
    await commands.importMachineDefinition(context, view, sourcePath);
    return {
      state: window.__icaxLaser3DCAM.getState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true }),
    };
  };
  window.__icaxLaser3DCAM.importWorkpiece = async (sourcePath) => {
    await commands.importWorkpiece(context, view, sourcePath);
    return {
      state: window.__icaxLaser3DCAM.getState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true }),
    };
  };
  window.__icaxLaser3DCAM.recognizeCADIntent = async () => {
    await commands.recognizeCADIntent(context, view);
    return {
      state: window.__icaxLaser3DCAM.getState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true }),
      tubeDom: window.__icaxLaser3DCAM.getTubeCADIntentDomState(),
    };
  };
  window.__icaxLaser3DCAM.getTubeCADIntentDomState = () => ({
    workspaceMode: view.tubeWorkspaceMode ?? "main",
    mainWorkspaceCount: document.querySelectorAll(".tubest-main-left").length,
    editorWorkspaceCount: document.querySelectorAll(".tubest-part-editor-left").length,
    sectionGroupCount: document.querySelectorAll(".tubest-section-group").length,
    manufacturingTreeCount: document.querySelectorAll(".tubest-resource-tree-section").length,
    layerStripCount: document.querySelectorAll(".tubest-layer-strip").length,
    bottomDockCount: document.querySelectorAll(".tubest-bottom-dock").length,
    mainPropertyPaneCount: document.querySelectorAll(".tube-main-properties").length,
    workpiecePaginationCount: document.querySelectorAll(".tube-workpiece-pagination").length,
    nestingResultListCount: document.querySelectorAll(".tube-main-bottom-dock").length,
    bottomTabButtonCount: document.querySelectorAll("[data-cam-action='tube-bottom-tab']").length,
    activeBottomTab: document.querySelector(".tube-bottom-tabs button.active")?.dataset?.tubeBottomTab ?? "",
    nestingBottomPanelCount: document.querySelectorAll("[data-tube-bottom-panel='nesting']").length,
    stockBottomPanelCount: document.querySelectorAll("[data-tube-bottom-panel='stock']").length,
    bottomSplitterCount: document.querySelectorAll("[data-cam-resize-pane='bottom']").length,
    bottomHeight: getComputedStyle(document.querySelector(".cam-workbench") ?? document.documentElement)
      .getPropertyValue("--cam-bottom-height").trim(),
    toolbarTooltipCandidateCount: document.querySelectorAll(
      ".ribbon-command, .quick-button, .tubest-part-toolbar button, .tube-cad-tool-ribbon button",
    ).length,
    visibleToolbarTooltipCount: [...document.querySelectorAll(".icax-toolbar-tooltip")]
      .filter((element) => !element.hidden && getComputedStyle(element).display !== "none").length,
    toolbarTooltipText: document.querySelector(".icax-toolbar-tooltip:not([hidden])")?.textContent?.trim() ?? "",
    visibleMainViewCubeCount: [...document.querySelectorAll(".cam-viewcube")]
      .filter((element) => getComputedStyle(element).display !== "none").length,
    visibleMainAxisCount: [...document.querySelectorAll(".icax-three-axis-gizmo")]
      .filter((element) => getComputedStyle(element).display !== "none").length,
    visibleRibbonGroupCount: [...document.querySelectorAll(".ribbon-group")]
      .filter((element) => getComputedStyle(element).display !== "none").length,
    visibleVectorRibbonIconCount: [...document.querySelectorAll(".ribbon-command .command-icon.vector")]
      .filter((element) => getComputedStyle(element.closest(".ribbon-group")).display !== "none").length,
    workpieceCardCount: document.querySelectorAll(".tube-workpiece-card").length,
    activeWorkpieceCardCount: document.querySelectorAll(".tube-workpiece-card.active").length,
    selectedWorkpieceCardCount: document.querySelectorAll(".tube-workpiece-card.selected").length,
    workpieceThumbnailCount: document.querySelectorAll("[data-tube-workpiece-thumbnail]").length,
    hydratedWorkpieceThumbnailCount: document.querySelectorAll("[data-tube-thumbnail-ready='true']").length,
    resourceWorkpieceThumbnailCount: document.querySelectorAll("[data-tube-thumbnail-source='resource']").length,
    workpieceQuantityLabels: [...document.querySelectorAll(".tube-workpiece-quantity strong")]
      .map((element) => element.textContent?.trim() ?? ""),
    workpieceSearchValue: document.querySelector("[data-tube-workpiece-search]")?.value ?? "",
    workpieceSearchInvalid: document.querySelector("[data-tube-workpiece-search]")?.getAttribute("aria-invalid") === "true",
    visibleWorkpieceCardCount: [...document.querySelectorAll(".tube-workpiece-card")]
      .filter((element) => !element.hidden).length,
    workpiecePathInputCount: document.querySelectorAll("[data-cam-model-path]").length,
    addWorkpieceButtonCount: document.querySelectorAll("[data-cam-action='tube-add-workpiece']").length,
    deleteWorkpieceButtonCount: document.querySelectorAll("[data-cam-action='tube-delete-workpiece']").length,
    legacyWorkpieceRibbonCommandCount: document.querySelectorAll(
      "[data-command-id='workpiece.import'], [data-command-id='workpiece.update'], [data-command-id='workpiece.delete']",
    ).length,
    workpieceImportDialogCount: document.querySelectorAll(".tube-import-dialog[role='dialog']").length,
    workpieceImportPreviewCount: document.querySelectorAll(".tube-import-preview canvas[data-tube-thumbnail-source='resource']").length,
    historyNodeCount: document.querySelectorAll(".tube-node-row").length,
    selectedHistoryNodeCount: document.querySelectorAll(".tube-node-row.selected").length,
    parameterCount: document.querySelectorAll("[data-tube-parameter]").length,
    editableParameterCount: document.querySelectorAll("[data-tube-parameter]:not([disabled])").length,
    derivedParameterCount: document.querySelectorAll("[data-tube-parameter][disabled]").length,
    previewActive: Boolean(document.querySelector(".tube-preview-state.active")),
    hasCSGOverlay: Boolean(document.querySelector(".tube-csg-overlay")),
    hasFeatureOverlay: Boolean(document.querySelector(".tube-csg-overlay")),
    editorDialogCount: document.querySelectorAll(".tube-cad-editor-dialog[role='dialog']").length,
    editorSceneId: view.tubeEditorSceneId ?? "",
    editorSceneProxyId: view.tubeEditorSceneProxy?.sceneId ?? "",
    mainSceneId: view.tubeMainSceneId ?? "",
    sectionPrimitiveCount: view.scene?.tubeGeometry?.sectionPrimitives?.length ?? 0,
    viewportLayout: getViewportLayoutDiagnostics(view),
    selectedNodeId: view.selectedCADIntentNodeId ?? "",
    previewNodeId: view.cadIntentPreviewNodeId ?? "",
  });
  window.__icaxLaser3DCAM.selectCADIntentNode = async (nodeId) => {
    const row = [...document.querySelectorAll(".tube-node-row")]
      .find((element) => element.dataset.camNodeId === String(nodeId));
    if (!row) {
      throw new Error(`CAD intent node is not visible in the history tree: ${nodeId}`);
    }
    row.click();
    const expectedNode = (view.scene?.tubeGeometry?.solidNodes ?? [])
      .concat(view.scene?.tubeGeometry?.sectionPrimitives ?? [])
      .find((node) => node.id === String(nodeId));
    const deadline = performance.now() + 10000;
    while (performance.now() < deadline) {
      if (view.selectedCADIntentNodeId === String(nodeId)
        && (!expectedNode?.previewAvailable || view.cadIntentPreviewNodeId === String(nodeId))) {
        break;
      }
      await delay(50);
    }
    return {
      state: window.__icaxLaser3DCAM.getState(),
      tubeDom: window.__icaxLaser3DCAM.getTubeCADIntentDomState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true }),
    };
  };
  window.__icaxLaser3DCAM.setCADIntentParameter = async (nodeId, parameterName, value) => {
    await window.__icaxLaser3DCAM.selectCADIntentNode(nodeId);
    const readyDeadline = performance.now() + 15000;
    while (view.pending && performance.now() < readyDeadline) {
      await delay(50);
    }
    const form = [...document.querySelectorAll("[data-tube-parameter-form]")]
      .find((element) => element.dataset.tubeNodeId === String(nodeId));
    const input = form?.querySelector(`[data-tube-parameter="${cssEscape(parameterName)}"]:not([disabled])`);
    const applyButton = form?.querySelector("[data-cam-action='tube-save-parameters']");
    if (!input || !applyButton) {
      throw new Error(`Editable CAD intent parameter is not visible: ${nodeId}.${parameterName}`);
    }
    const captureGeometryState = () => {
      const scene = view.scene ?? {};
      const workpiece = scene.model
        ?? scene.workpiece
        ?? (scene.workpieces ?? []).find((item) => item.entityId === scene.model?.entityId)
        ?? (scene.workpieces ?? [])[0]
        ?? {};
      const node = (scene.tubeGeometry?.solidNodes ?? [])
        .concat(scene.tubeGeometry?.sectionPrimitives ?? [])
        .find((item) => item.id === String(nodeId));
      return {
        version: Number(scene.tubeGeometry?.version ?? 0),
        geometryRevision: Number(workpiece.geometryRevision ?? 0),
        brepResourceId: String(workpiece.brepResourceId ?? ""),
        renderGeometryResourceUrl: String(workpiece.previewResourceUrl ?? ""),
        previewResourceUrl: String(node?.metadata?.previewResourceUrl ?? ""),
        previewResourceVersion: Number(node?.metadata?.previewResourceVersion ?? 0),
      };
    };
    const before = captureGeometryState();
    input.value = String(value);
    input.dispatchEvent(new Event("input", { bubbles: true }));
    applyButton.click();
    const deadline = performance.now() + 60000;
    while (performance.now() < deadline) {
      const previewState = view.viewport?.getDebugState?.({ samplePixels: false }) ?? null;
      const current = captureGeometryState();
      const previewChanged = current.previewResourceUrl !== before.previewResourceUrl
        || current.previewResourceVersion !== before.previewResourceVersion;
      if ((current.version > before.version
          && current.geometryRevision > before.geometryRevision
          && current.brepResourceId !== before.brepResourceId
          && current.renderGeometryResourceUrl !== before.renderGeometryResourceUrl
          && previewChanged
          && !view.pending
          && view.cadIntentPreviewNodeId === String(nodeId)
          && previewState?.ghostPreviewVisible)
        || view.error) {
        break;
      }
      await delay(50);
    }
    const node = (view.scene?.tubeGeometry?.solidNodes ?? [])
      .concat(view.scene?.tubeGeometry?.sectionPrimitives ?? [])
      .find((item) => item.id === String(nodeId));
    const parameter = (node?.parameters ?? [])
      .find((item) => item.name === String(parameterName));
    const after = captureGeometryState();
    return {
      beforeVersion: before.version,
      afterVersion: after.version,
      beforeGeometryRevision: before.geometryRevision,
      afterGeometryRevision: after.geometryRevision,
      beforeBRepResourceId: before.brepResourceId,
      afterBRepResourceId: after.brepResourceId,
      beforeRenderGeometryResourceUrl: before.renderGeometryResourceUrl,
      afterRenderGeometryResourceUrl: after.renderGeometryResourceUrl,
      beforePreviewResourceUrl: before.previewResourceUrl,
      afterPreviewResourceUrl: after.previewResourceUrl,
      beforePreviewResourceVersion: before.previewResourceVersion,
      afterPreviewResourceVersion: after.previewResourceVersion,
      nodeId: String(nodeId),
      parameterName: String(parameterName),
      value: parameter?.value ?? null,
      error: view.error ?? "",
    };
  };
  window.__icaxLaser3DCAM.previewCADIntentParameter = async (nodeId, parameterName, value) => {
    await window.__icaxLaser3DCAM.selectCADIntentNode(nodeId);
    const form = [...document.querySelectorAll("[data-tube-parameter-form]")]
      .find((element) => element.dataset.tubeNodeId === String(nodeId));
    const input = form?.querySelector(`[data-tube-parameter="${cssEscape(parameterName)}"]:not([disabled])`);
    if (!input) {
      throw new Error(`Editable CAD intent parameter is not visible: ${nodeId}.${parameterName}`);
    }
    const beforeWorkpiece = view.scene?.model ?? view.scene?.workpiece ?? {};
    const before = {
      version: Number(view.scene?.tubeGeometry?.version ?? 0),
      geometryRevision: Number(beforeWorkpiece.geometryRevision ?? 0),
      brepResourceId: String(beforeWorkpiece.brepResourceId ?? ""),
    };
    input.value = String(value);
    input.dispatchEvent(new Event("input", { bubbles: true }));
    const deadline = performance.now() + 60000;
    while (performance.now() < deadline) {
      if ((view.cadIntentLivePreviewNodeId === String(nodeId)
          && view.cadIntentLivePreviewResourceUrl)
        || form.closest(".cam-panel")?.querySelector(".tube-preview-state.invalid")) {
        break;
      }
      await delay(50);
    }
    const afterWorkpiece = view.scene?.model ?? view.scene?.workpiece ?? {};
    const viewport = view.viewport?.getDebugState?.({ samplePixels: false }) ?? null;
    return {
      nodeId: String(nodeId),
      parameterName: String(parameterName),
      value: Number(input.value),
      beforeVersion: before.version,
      afterVersion: Number(view.scene?.tubeGeometry?.version ?? 0),
      beforeGeometryRevision: before.geometryRevision,
      afterGeometryRevision: Number(afterWorkpiece.geometryRevision ?? 0),
      beforeBRepResourceId: before.brepResourceId,
      afterBRepResourceId: String(afterWorkpiece.brepResourceId ?? ""),
      livePreviewNodeId: view.cadIntentLivePreviewNodeId ?? "",
      livePreviewResourceUrl: view.cadIntentLivePreviewResourceUrl ?? "",
      livePreviewResourceVersion: view.cadIntentLivePreviewResourceVersion ?? 0,
      ghostPreviewVisible: viewport?.ghostPreviewVisible ?? false,
      ghostPreviewVertexCount: viewport?.ghostPreviewVertexCount ?? 0,
      status: form.closest(".cam-panel")?.querySelector(".tube-preview-state span")?.textContent ?? "",
    };
  };
  window.__icaxLaser3DCAM.fitView = async () => {
    await commands.fitView(context, view);
    return window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true });
  };
  window.__icaxLaser3DCAM.setMachineInstanceEnabled = async (machineEntityId, enabled) => {
    await commands.setMachineInstanceEnabled(context, view, machineEntityId, enabled);
    return window.__icaxLaser3DCAM.getState();
  };
  window.__icaxLaser3DCAM.setMachineInstanceName = async (machineEntityId, name) => {
    await commands.setMachineInstanceName(context, view, machineEntityId, name);
    return window.__icaxLaser3DCAM.getState();
  };
  window.__icaxLaser3DCAM.setJobMachine = async (machineEntityId) => {
    await commands.setJobMachine(context, view, machineEntityId);
    return window.__icaxLaser3DCAM.getState();
  };
  window.__icaxLaser3DCAM.pickMachineObject = async (payload) => {
    const result = await commands.pickMachineObject(context, view, payload);
    return {
      result,
      state: window.__icaxLaser3DCAM.getState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true, includeObjects: true }),
    };
  };
  window.__icaxLaser3DCAM.setMachineElementAppearance = async (payload) => {
    const result = await commands.setMachineElementAppearance(context, view, payload);
    return {
      result,
      state: window.__icaxLaser3DCAM.getState(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true, includeObjects: true }),
    };
  };
  window.__icaxLaser3DCAM.setStandardView = async (viewName) => {
    const result = await commands.setStandardCameraView(context, view, viewName);
    return {
      result,
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true, includeObjects: true }),
    };
  };
  window.__icaxLaser3DCAM.getMachineDomState = () => {
    const viewCube = document.querySelector("[data-cam-viewcube]");
    const axisGizmo = document.querySelector(".icax-three-axis-gizmo");
    const axisStyle = axisGizmo ? window.getComputedStyle(axisGizmo) : null;
    return {
      hasMachineImportPathInput: Boolean(document.querySelector("[data-cam-machine-path]")),
      accordionCount: document.querySelectorAll(".cam-accordion-item").length,
      hasViewCube: Boolean(viewCube),
      viewCubeButtonCount: document.querySelectorAll(".cam-viewcube [data-cam-action='view-standard']").length,
      viewCubePieceCount: Number(viewCube?.dataset.camViewCubePieceCount ?? 0),
      viewCubeCanvasCount: document.querySelectorAll(".cam-viewcube canvas").length,
      viewCubeCellCount: document.querySelectorAll(".cam-viewcube-cell").length,
      viewCubePitch: viewCube?.style.getPropertyValue("--viewcube-pitch") ?? "",
      viewCubeYaw: viewCube?.style.getPropertyValue("--viewcube-yaw") ?? "",
      hasViewCubeEmbeddedAxis: Boolean(document.querySelector(".cam-viewcube-axis")),
      hasAxisGizmo: Boolean(axisGizmo),
      viewportCanvasCount: document.querySelectorAll(".icax-three-viewport-canvas").length,
      axisCanvasCount: document.querySelectorAll(".icax-three-axis-canvas").length,
      axisGizmoLeft: axisStyle?.left ?? "",
      axisGizmoBottom: axisStyle?.bottom ?? "",
      hasAppearanceEditor: Boolean(document.querySelector("[data-cam-appearance-editor]")),
      hasCollisionToggle: Boolean(document.querySelector("[data-cam-machine-show-collider]")),
      selectedTreeRows: document.querySelectorAll(".cam-tree-row.selected").length,
    };
  };
  window.__icaxLaser3DCAM.currentProjectId = context.project?.projectId ?? "";
}

async function waitForRenderableViewport(view, options = {}) {
  const timeoutMs = Number(options.timeoutMs ?? 15000);
  const includeObjects = options.includeObjects ?? true;
  const deadline = performance.now() + Math.max(1000, timeoutMs);
  let lastState = null;
  while (performance.now() < deadline) {
    if (view.viewport?.refreshAll) {
      await Promise.race([view.viewport.refreshAll(), delay(500)]);
    }
    await delay(80);
    lastState = view.viewport?.getDebugState?.({ samplePixels: true, includeObjects }) ?? null;
    if (
      Number(lastState?.geometryCount ?? 0) > 0
      && Number(lastState?.objectCount ?? 0) > 0
      && Number(lastState?.visibleObjectCount ?? 0) > 0
      && hasFiniteVector3(lastState?.cameraPosition)
      && hasFiniteVector3(lastState?.cameraDirection)
    ) {
      return lastState;
    }
  }
  return lastState;
}

function getViewportLayoutDiagnostics(view) {
  const describe = (selector) => {
    const element = document.querySelector(selector);
    if (!element) return null;
    const rect = element.getBoundingClientRect();
    const style = window.getComputedStyle(element);
    return {
      connected: element.isConnected,
      width: rect.width,
      height: rect.height,
      display: style.display,
      visibility: style.visibility,
    };
  };
  return {
    viewportMounted: Boolean(view.viewport?.host && view.viewport?.root?.parentElement),
    hostMatchesLiveElement: view.viewport?.host === document.querySelector("[data-cam-render-viewport]"),
    workbench: describe(".tube-cad-editor-dialog"),
    viewport: describe(".tube-cad-editor-dialog .cam-viewport"),
    host: describe(".tube-cad-editor-dialog [data-cam-render-viewport]"),
    root: describe(".tube-cad-editor-dialog .icax-three-viewport"),
    canvas: describe(".tube-cad-editor-dialog .icax-three-viewport-canvas"),
  };
}

function hasFiniteVector3(value) {
  if (Array.isArray(value)) {
    return value.length >= 3
      && value.slice(0, 3).every((component) => Number.isFinite(Number(component)));
  }
  return Boolean(value)
    && [value.x, value.y, value.z].every((component) => Number.isFinite(Number(component)));
}

function cssEscape(value) {
  return globalThis.CSS?.escape
    ? globalThis.CSS.escape(String(value))
    : String(value).replace(/["\\]/g, "\\$&");
}

function delay(durationMs) {
  return new Promise((resolve) => window.setTimeout(resolve, durationMs));
}
