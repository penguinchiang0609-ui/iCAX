import * as workbench from "../../_shared/workbench/entry.mjs";
import { installTubeDesignerBranding } from "./branding.mjs";
import { attachPunchEditor } from "./punchEditor.mjs";
import { patchPunchDom } from "./punchDomPatch.mjs";
import { scheduleDesignerPartThumbnailHydration } from "./partThumbnail.mjs";
import { scheduleDesignerPartInspectionHydration } from "./partInspection.mjs";
import { attachPartDrawingEditor, renderPartDrawingDialog } from "./partDrawing.mjs";
import { attachPartDrawingPreview, disposePartDrawingPreview } from "./partDrawingPreview.mjs";
import { patchPartDrawingDom, rememberPartDrawingDom } from "./partDrawingDom.mjs";
import {
  fitDesignerDefaultView,
  getDesignerRenderSignature,
  handleDesignerAreaAction,
  handleDesignerRibbonCommand,
  refreshDesignerState,
  refreshDesignerUserData,
} from "./designerActions.mjs";
import {
  renderDesignerLeftPane,
  renderDesignerDialogs,
  renderDesignerOperationOverlay,
  renderDesignerRightPane,
  renderDesignerViewportOverlay,
} from "./designerViews.mjs";
import { getRibbonDefinition as getDesignerRibbonDefinition } from "./ribbonDefinition.mjs";
import { ensureTubeDesignerStyles } from "./styles/ensureStyles.mjs";
import { renderNestingSettingsDialogs } from "./nestingSettings.mjs";
import { renderNestingPartImportDialog } from "./nestingPartImport.mjs";
import { renderNestingStandardPartDialog } from "./nestingStandardPart.mjs";
import { bindProfileParameterDiagrams } from "./profileParameterDiagram.mjs";
import { renderNestingPunchPartDialog } from "./nestingPunchPart.mjs";
import { attachTubeMachining, handleTubeMachiningViewportPick, renderTubeMachiningLeftPane, renderTubeMachiningRightPane, renderTubeMachiningViewportOverlay, renderTubeMachiningDialogs } from "./machiningArea.mjs";
import { hasUnsavedMachiningPaths } from "./machiningEditor.mjs";
import { getProjectView } from "../../_shared/workbench/state/projectViewStore.mjs";
import { renderProgress } from "../../_shared/workbench/layout/commonViews.mjs";
import { typedVariant } from "../../../iCAX-UI/SDK/SDO/variantSerializer.mjs";
import {
  renderProfileLibraryLeftPane,
  renderProfileLibraryRightPane,
  renderProfileLibraryViewportOverlay,
} from "./profileLibrary.mjs";
import {
  ensureToolLibraryCatalogue,
  renderToolLibraryLeftPane,
  renderToolLibraryRightPane,
  renderToolLibraryViewportOverlay,
} from "./toolLibrary.mjs";
import {
  attachComponentLibrary,
  renderComponentLibraryDialogs,
  renderComponentLibraryLeftPane,
  renderComponentLibraryRightPane,
  renderComponentLibraryViewportOverlay,
} from "./componentLibrary.mjs";
import {
  attachNestingPartContextMenu,
  clearPartsViewportAnnotations,
  renderNestingLeftPane,
  renderNestingResultDock,
  renderNestingRightPane,
  renderNestingViewportOverlay,
  renderPunchWizardDialog,
  restoreNestingPartListScroll,
} from "./partsArea.mjs";
import {
  attachSketchAreaInteractions,
  renderSectionSketchDialog,
  renderSketchLeftPane,
  renderSketchRightPane,
  renderSketchViewportOverlay,
} from "./sketchArea.mjs";
import {
  renderAboutLeftPane,
  renderAboutRightPane,
  renderAboutViewportOverlay,
} from "./aboutArea.mjs";
import {
  ensureProductTemplateLibraryDescriptor,
  renderProductTemplateLibraryLeftPane,
  renderProductTemplateLibraryRightPane,
  renderProductTemplateLibraryViewportOverlay,
} from "./templateLibrary.mjs";

export const TUBE_DESIGNER_LOAD_PROGRESS_MINIMUM_VISIBLE_MS = 500;

export function getRibbonDefinition(context = {}) {
  const projectId = context.project?.projectId ?? "";
  const view = getProjectView(projectId);
  return getDesignerRibbonDefinition({
    resourceArea: view.tubeDesignerResourceLibraryArea ?? "profiles",
  });
}

export function mountProduct(context) {
  ensureTubeDesignerStyles();
  installTubeDesignerBranding();
  workbench.mountProduct(context);
}

export async function mountProject(context) {
  ensureTubeDesignerStyles();
  installTubeDesignerBranding();
  const view = getProjectView(context.project?.projectId ?? "");
  // 下料已经是产品主流程的一部分，不再通过旧“零件与排样”商业权限分叉界面。
  view.tubeDesignerProductionAccess = true;
  view.tubeDesignerResourceLibraryArea ??= "profiles";
  view.scene ??= {};
  const historyToken = getHistoryToken(context.scene);
  const historyChanged = view.tubeDesignerHistoryToken !== undefined
    && view.tubeDesignerHistoryToken !== historyToken;
  view.tubeDesignerHistoryToken = historyToken;
  const ownMutation = historyChanged && view.tubeDesignerOwnMutation;
  if (ownMutation) {
    view.tubeDesignerOwnMutation = false;
  }
  const designerContext = withDesignerContext(context);
  const shouldRefresh = (!view.tubeDesignerLoaded || (historyChanged && !ownMutation))
    && !view.tubeDesignerLoading;
  const shouldRefreshUserData = !view.tubeDesignerUserDataLoaded
    && !view.tubeDesignerUserDataLoading;
  if (shouldRefresh) {
    view.tubeDesignerLoading = true;
  }
  if (shouldRefreshUserData) {
    view.tubeDesignerUserDataLoading = true;
  }
  const loadProgress = beginDesignerLoadProgress(
    view,
    historyChanged,
    shouldRefresh,
    // User data is hydrated in the background.  It must not keep the
    // product scene behind a blocking first-screen progress mask.
    false,
  );
  const initialMount = workbench.mountProject(designerContext);
  if (!shouldRefresh && !shouldRefreshUserData) {
    return initialMount;
  }

  // Scene restoration and product user-data hydration share the embedded
  // Python template host.  Keep an explicit gate so startup never runs both
  // SDO calls concurrently and leaves the library request timing out.
  let resolveSceneRefreshReady;
  const sceneRefreshReady = new Promise((resolve) => { resolveSceneRefreshReady = resolve; });
  if (!shouldRefresh) resolveSceneRefreshReady();

  if (shouldRefreshUserData) {
    const userDataSynchronization = (async () => {
      try {
        await sceneRefreshReady;
        await refreshDesignerUserData(designerContext, view);
      } finally {
        view.tubeDesignerUserDataLoading = false;
        // A first mount can happen before the product SDO is connected. Do
        // not mark that empty placeholder as successfully hydrated; opening
        // 管型库 later must be allowed to retry once the channel is ready.
        view.tubeDesignerUserDataLoaded = typeof designerContext.productProxy?.invoke === "function"
          && !view.tubeDesignerUserDataError
          && Array.isArray(view.tubeDesignerSystemProfiles)
          && view.tubeDesignerSystemProfiles.length > 0;
        workbench.mountProject(designerContext);
      }
    })();
    view.tubeDesignerUserDataSynchronizationPromise = userDataSynchronization;
    void userDataSynchronization.then(
      () => {
        if (view.tubeDesignerUserDataSynchronizationPromise === userDataSynchronization) {
          view.tubeDesignerUserDataSynchronizationPromise = null;
        }
      },
      (error) => {
        view.tubeDesignerUserDataError = error?.message ?? String(error);
        if (view.tubeDesignerUserDataSynchronizationPromise === userDataSynchronization) {
          view.tubeDesignerUserDataSynchronizationPromise = null;
        }
      },
    );
  }

  // A user-data-only refresh is deliberately fire-and-forget.  The workbench
  // is already mounted and remains interactive while the libraries hydrate.
  if (!shouldRefresh) {
    return initialMount;
  }

  const previousViewRevision = String(
    view.tubeDesignerLastOperation?.viewRevision
      ?? view.viewport?.getAppliedViewState?.().revision
      ?? "0",
  );
  const previousDesigner = view.scene?.tubeDesigner ?? {};
  const previousRenderSignature = getDesignerRenderSignature(previousDesigner);
  const previousMemberIds = (previousDesigner.members ?? [])
    .map((member) => String(member?.entityId ?? "").trim())
    .filter(Boolean);
  const synchronization = (async () => {
    try {
      if (loadProgress) {
        await waitForPaint();
        loadProgress.visibleAt = nowMilliseconds();
      }
      await refreshDesignerState(designerContext, view);
      const designer = view.scene?.tubeDesigner ?? {};
      const generationRunId = String(designer.generationRun?.entityId ?? "").trim();
      const memberIds = (designer.members ?? [])
        .map((member) => String(member?.entityId ?? "").trim())
        .filter(Boolean);
      const renderChanged = previousRenderSignature !== getDesignerRenderSignature(designer);
      const activeProductChanged = String(previousDesigner.activeProductId ?? "")
        !== String(designer.activeProductId ?? "");
      const requiresViewSynchronization = view.activeAreaId === "view" && (!historyChanged
        ? memberIds.length > 0
        : renderChanged);
      if (requiresViewSynchronization) {
        const expectation = {
          correlationId: generationRunId,
          expectedEntityIds: memberIds,
        };
        if (historyChanged && !activeProductChanged) {
          expectation.afterRevision = previousViewRevision;
          expectation.excludedEntityIds = previousMemberIds.filter((id) => !memberIds.includes(id));
        }
        if (updateDesignerLoadProgress(
          view,
          loadProgress,
          "同步三维视图",
          historyChanged ? "正在恢复历史版本对应的产品视图" : "正在装载当前产品的三维显示资源",
        )) {
          workbench.mountProject(designerContext);
          await waitForPaint();
        }
        const viewContent = await workbench.synchronizeActiveAreaView(designerContext, expectation);
        const defaultViewReceipt = memberIds.length > 0
          ? fitDesignerDefaultView(view, viewContent.revision, designer.product)
          : null;
        view.tubeDesignerLastOperation = {
          kind: historyChanged ? "history" : "load",
          generationRunId,
          viewRevision: viewContent.revision,
          memberIds,
          defaultViewDirection: defaultViewReceipt?.defaultViewDirection,
          fitRenderSequence: defaultViewReceipt?.fitRenderSequence,
          defaultViewRenderSequence: defaultViewReceipt?.defaultViewRenderSequence,
        };
      }
      view.tubeDesignerRenderSignature = getDesignerRenderSignature(designer);
    } finally {
      await finishDesignerLoadProgress(view, loadProgress);
      if (shouldRefresh) {
        view.tubeDesignerLoading = false;
        view.tubeDesignerLoaded = true;
      }
      resolveSceneRefreshReady();
      workbench.mountProject(designerContext);
    }
  })();
  view.tubeDesignerSynchronizationPromise = synchronization;
  try {
    return await synchronization;
  } finally {
    if (view.tubeDesignerSynchronizationPromise === synchronization) {
      view.tubeDesignerSynchronizationPromise = null;
    }
  }
}

export function handleRibbonCommand(context, commandId) {
  return workbench.handleRibbonCommand(withDesignerContext(context), commandId);
}

export function getWindowCloseGuard(context) {
  const view = getProjectView(context.project?.projectId ?? "");
  const operation = view.tubeDesignerExportOperation ?? view.tubeDesignerOperation;
  if (!operation && hasUnsavedMachiningPaths(view)) return { blocked: true, message: "加工区有未保存的刀路编辑，请先保存刀路或放弃编辑。" };
  if (!operation) return null;
  const completed = Math.max(0, Number(operation.completed ?? 0));
  const total = Math.max(0, Number(operation.total ?? 0));
  return {
    blocked: true,
    message: operation.kind === "export" && total > 0
      ? `加工文件正在导出（${Math.min(completed, total)} / ${total}），完成或失败后才能退出软件。`
      : `${operation.title ?? "当前任务"}尚未完成，完成或失败后才能退出软件。`,
  };
}

function withDesignerContext(context) {
  return {
    ...context,
    forceThreeViewport: true,
    showViewportGrid: false,
    areaTitleOverrides: { view: "产品", nesting: "下料", machining: "加工", resources: "资源库", templates: "产品模板", profiles: "管型库", tools: "模具库", components: "配件库", sketch: "草图", about: "关于" },
    areaRenderers: {
      view: {
        left: renderDesignerLeftPane,
        right: renderDesignerRightPane,
      },
      profiles: {
        left: renderProfileLibraryLeftPane,
        right: renderProfileLibraryRightPane,
      },
      tools: {
        left: renderToolLibraryLeftPane,
        right: renderToolLibraryRightPane,
      },
      components: {
        left: renderComponentLibraryLeftPane,
        right: renderComponentLibraryRightPane,
      },
      templates: {
        left: renderProductTemplateLibraryLeftPane,
        right: renderProductTemplateLibraryRightPane,
      },
      sketch: {
        left: renderSketchLeftPane,
        right: renderSketchRightPane,
      },
      nesting: {
        left: renderNestingLeftPane,
        right: renderNestingRightPane,
      },
      machining: { left: renderTubeMachiningLeftPane, right: renderTubeMachiningRightPane },
      about: {
        left: renderAboutLeftPane,
        right: renderAboutRightPane,
      },
    },
    normalizeAreaId: (tabId) => {
      if (tabId === "parts") return "nesting";
      if (tabId === "resources") {
        const resourceArea = getProjectView(context.project?.projectId ?? "").tubeDesignerResourceLibraryArea;
        return ["products", "profiles", "tools", "components"].includes(resourceArea)
          ? (resourceArea === "products" ? "templates" : resourceArea) : "profiles";
      }
      return ["view", "nesting", "machining", "templates", "profiles", "tools", "components", "sketch", "about"].includes(tabId)
        ? tabId : "view";
    },
    resolveWorkbenchPresentation: (_context, _view, _scene, areaId) => ({
      className: `tube-designer-workspace ${areaId === "nesting" ? "tube-designer-production-workspace" : ""} ${areaId === "sketch" ? "tube-designer-sketch-workspace" : ""} ${areaId === "about" ? "tube-designer-about-workspace" : ""}`,
      style: areaId === "nesting" ? renderNestingWorkspaceStyle() : "",
    }),
    resolveViewportBackgroundColor: () => 0x13252d,
    configureViewport: configureDesignerViewport,
    resolveAreaViewDefinition: resolveDesignerAreaViewDefinition,
    renderViewportOverlay: renderDesignerAreaViewportOverlay,
    renderWorkbenchSuffix: renderDesignerWorkbenchSuffix,
    handleAreaAction: handleDesignerAreaAction,
    handleAreaViewportPick: handleTubeMachiningViewportPick,
    handleAreaRibbonCommand: handleDesignerRibbonCommand,
    tryRenderProjectPatch(context,view,mount,ops) {
      if(view.tubeDesignerPartDrawing&&view.activeAreaId==="nesting") {
        const html=renderPartDrawingDialog(view)+renderDesignerOperationOverlay(context,view);
        if(!patchPartDrawingDom(view,mount,html))return false;
        bindProfileParameterDiagrams(mount);
        attachPartDrawingPreview(context,view,mount,ops);
        attachPartDrawingEditor(context,view,mount,ops);
        return true;
      }
      if(!view.tubeDesignerPunchWizard||view.tubeDesignerPartDrawing||view.activeAreaId!=="nesting")return false;
      const html=(renderNestingPunchPartDialog(view)||renderPunchWizardDialog(context,view))
        +renderDesignerOperationOverlay(context,view);
      if(!patchPunchDom(view,mount,html))return false;
      bindProfileParameterDiagrams(mount);
      attachPunchEditor(context,view,mount,ops);
      return true;
    },
    afterProjectRender(context, view, mount, ops) {
      bindProfileParameterDiagrams(mount);
      if ((view.activeAreaId === "profiles" || view.activeAreaId === "tools")
          && typeof context.productProxy?.invoke === "function"
          && !(view.tubeDesignerSystemProfiles?.length > 0)
          && !view.tubeDesignerProfileLibraryHydrationAttempted
          && !view.tubeDesignerUserDataLoading) {
        // The first mount intentionally does not block on product user data.
        // If the product channel became available after that mount, hydrate
        // the default 管型库 once in the background and repaint on completion.
        view.tubeDesignerProfileLibraryHydrationAttempted = true;
        void refreshDesignerUserData(context, view).then((loaded) => {
          if (loaded && (view.activeAreaId === "profiles" || view.activeAreaId === "tools")) {
            ops.renderProject(context, view);
          }
        });
      }
      if (view.activeAreaId === "tools") {
        view.tubeDesignerToolLibraryRenderProject = () => ops.renderProject(context, view);
        // Do not immediately retry from the repaint triggered by a failed
        // request; that would create an endless timeout loop.  A fresh entry
        // through the ribbon (or 重新读取) calls ensureToolLibraryCatalogue
        // explicitly and is allowed to retry the error state.
        const catalogueStatus = view.tubeDesignerToolLibrary?.catalogueStatus ?? "idle";
        if (catalogueStatus === "idle") ensureToolLibraryCatalogue(context, view, ops);
      }
      if (view.activeAreaId === "templates") {
        view.tubeDesignerProductTemplateLibraryRenderProject = () => ops.renderProject(context, view);
        void ensureProductTemplateLibraryDescriptor(context, view).then((loaded) => {
          if (loaded && view.activeAreaId === "templates") ops.renderProject(context, view);
        }).catch((error) => {
          view.tubeDesignerTemplateLoadError = error?.message ?? String(error);
        });
      }
      if (view.activeAreaId === "nesting" && view.tubeDesignerBreakdownOpen && !view.tubeDesignerPartInspectionOpen) {
        scheduleDesignerPartThumbnailHydration(context);
      }
      if (view.activeAreaId === "nesting" && view.tubeDesignerPartInspectionOpen) {
        scheduleDesignerPartInspectionHydration(context, view.scene?.tubeDesigner ?? {}, view);
      }
      restoreNestingPartListScroll(context, view, mount);
      attachNestingPartContextMenu(context, view, mount, ops);
      attachSketchAreaInteractions(context, view, mount, ops);
      attachComponentLibrary(context, view, mount, ops);
      rememberPartDrawingDom(view,mount);
      if(view.tubeDesignerPartDrawing) {
        attachPartDrawingPreview(context,view,mount,ops);
        attachPartDrawingEditor(context,view,mount,ops);
      } else {
        disposePartDrawingPreview(mount);
        attachPunchEditor(context,view,mount,ops);
      }
      attachTubeMachining(context, view, mount, ops);
    },
  };
}

function renderNestingWorkspaceStyle() {
  return [
    "width:100%",
    "height:100%",
    "grid-template-columns:var(--cam-left-width,300px) 5px minmax(0,1fr) 5px var(--cam-right-width,320px)",
    "grid-template-rows:auto minmax(0,1fr) 5px var(--cam-bottom-height,156px)",
    "grid-template-areas:'notice notice notice notice notice' 'left left-resize viewer right-resize right' 'bottom-resize bottom-resize bottom-resize bottom-resize bottom-resize' 'bottom bottom bottom bottom bottom'",
  ].join(";");
}

function configureDesignerViewport(_context, view, areaId) {
  if (areaId === "parts") {
    const key = (view.scene?.tubeDesigner?.manufacturingGroups ?? []).map(group => group.generationRunId).sort().join("|");
    if (key && view.tubeDesignerManufacturingHydrationKey !== key && _context.sceneProxy?.invoke) {
      view.tubeDesignerManufacturingHydrationKey = key;
      void _context.sceneProxy.invoke("TubeDesigner.List", {includeManufacturingGeometry: true}, {timeoutMs: 180000})
        .then(response => {
          if (view.tubeDesignerManufacturingHydrationKey !== key) return;
          view.scene.tubeDesigner = response.tubeDesigner;
          view.tubeDesignerPartViewportKey = "";
          return workbench.mountProject(_context);
        }).catch(error => { view.error = `零件几何重建失败：${error?.message ?? error}`; });
    }
  }
  const viewport = view.viewport;
  if (!viewport) return;
  const normalizedAreaId = areaId === "parts" ? "nesting"
    : (["view", "nesting", "machining", "templates", "profiles", "tools", "components", "sketch", "about"].includes(areaId) ? areaId : "view");
  view.tubeDesignerProjectionModes ??= {};
  const projectionMode = view.tubeDesignerProjectionModes[normalizedAreaId] ?? "perspective";
  viewport.setProjectionToggleVisible?.(!["sketch", "about"].includes(normalizedAreaId));
  viewport.setPickingEnabled?.(!["sketch", "about"].includes(normalizedAreaId));
  viewport.setContinuousRendering?.(normalizedAreaId !== "sketch");
  viewport.setProjectionChangeHandler?.((mode) => {
    const currentAreaId = ["view", "templates", "profiles", "tools", "components", "nesting", "machining"].includes(view.activeAreaId)
      ? view.activeAreaId : "view";
    view.tubeDesignerProjectionModes ??= {};
    view.tubeDesignerProjectionModes[currentAreaId] = mode;
  });
  viewport.setProjectionMode?.(projectionMode);
  viewport.setOrbitConstrained?.(normalizedAreaId === "view");
}

function resolveDesignerAreaViewDefinition(_context, view, areaId, fallback) {
  if (["templates", "profiles", "tools", "components", "sketch", "nesting", "machining", "about"].includes(areaId)) {
    // 管型、下料与辅助工作区自行装载当前选择，不对应产品装配 View。
    return false;
  }
  if (areaId !== "view") return fallback;
  const activeProductId = String(view.scene?.tubeDesigner?.activeProductId ?? "").trim();
  return {
    sources: [{
      sourceId: "tube-designer-active-instance",
      role: "product-instance",
      language: "sql",
      where: activeProductId
        ? "WHERE HAS CRenderInstanceComponent AND HAS CAssemblyMemberComponent AND CAssemblyMemberComponent.ProductID = :activeProductId"
        : "WHERE FALSE",
      parameters: activeProductId
        ? { activeProductId: typedVariant("uuid", activeProductId) }
        : {},
      projection: workbench.RENDER_ENTITY_VIEW_PROJECTION,
    }],
  };
}

function renderDesignerAreaViewportOverlay(context, view, scene) {
  if (view.activeAreaId !== "nesting") clearPartsViewportAnnotations(view);
  if (view.activeAreaId === "profiles") return renderProfileLibraryViewportOverlay(context, view, scene);
  if (view.activeAreaId === "templates") return renderProductTemplateLibraryViewportOverlay(context, view, scene);
  if (view.activeAreaId === "tools") return renderToolLibraryViewportOverlay(context, view, scene);
  if (view.activeAreaId === "components") return renderComponentLibraryViewportOverlay(context, view, scene);
  if (view.activeAreaId === "sketch") return renderSketchViewportOverlay(context, view, scene);
  if (view.activeAreaId === "nesting") return renderNestingViewportOverlay(context, view, scene);
  if (view.activeAreaId === "machining") return renderTubeMachiningViewportOverlay(context, view);
  if (view.activeAreaId === "about") return renderAboutViewportOverlay(context, view, scene);
  return renderDesignerViewportOverlay(context, view, scene);
}

function renderDesignerWorkbenchSuffix(context, view, scene) {
  const punchWizardDialog = view.activeAreaId === "nesting"
    ? renderPunchWizardDialog(context, view)
    : "";
  const nestingPartImportDialog = view.activeAreaId === "nesting"
    ? renderNestingPartImportDialog(view)
    : "";
  const nestingStandardPartDialog = view.activeAreaId === "nesting"
    ? renderNestingStandardPartDialog(view)
    : "";
  const nestingPunchPartDialog = view.activeAreaId === "nesting"
    ? renderNestingPunchPartDialog(view)
    : "";
  const nestingSettingsDialogs = view.activeAreaId === "nesting"
    ? renderNestingSettingsDialogs(context, view)
    : "";
  const nestingResultDock = view.activeAreaId === "nesting"
    ? renderNestingResultDock(context, view, scene)
    : "";
  const areaDialogs = view.activeAreaId === "view"
    ? ""
    : renderDesignerDialogs(view.scene?.tubeDesigner ?? {}, view);
  return `${view.activeAreaId === "nesting" ? renderPartDrawingDialog(view) : ""}${view.activeAreaId === "machining" ? renderTubeMachiningDialogs(view) : ""}${renderSectionSketchDialog(context, view)}${nestingResultDock}${areaDialogs}${nestingPartImportDialog}${nestingStandardPartDialog}${nestingPunchPartDialog}${nestingSettingsDialogs}${punchWizardDialog}${renderComponentLibraryDialogs(view)}${renderDesignerOperationOverlay(context, view, scene)}${
    view.tubeDesignerLoadProgress ? renderProgress(view.tubeDesignerLoadProgress) : ""
  }`;
}

function getHistoryToken(scene) {
  const undoRedo = scene?.undoRedo ?? {};
  const stepKey = (step) => `${String(step?.id ?? "")}:${String(step?.name ?? "")}`;
  return JSON.stringify({
    undo: (undoRedo.undoSteps ?? []).map(stepKey),
    redo: (undoRedo.redoSteps ?? []).map(stepKey),
  });
}

function beginDesignerLoadProgress(view, historyChanged, shouldRefresh, shouldRefreshUserData) {
  if (!shouldRefresh && !shouldRefreshUserData) return null;
  if (view.pending || view.progress || view.tubeDesignerLoadProgress) return null;
  // `pending` gates the base workbench's initial scene read. Loading feedback must
  // therefore use its own state while reusing the same full-screen progress view.
  const token = {};
  const readsProductAndUserData = shouldRefresh && shouldRefreshUserData;
  view.tubeDesignerLoadProgress = {
    title: historyChanged ? "正在恢复产品历史" : "正在加载产品设计",
    detail: readsProductAndUserData
      ? "正在读取产品实例、模板与用户配置"
      : shouldRefresh
        ? "正在读取产品实例与模板"
        : "正在读取用户配置",
    stage: readsProductAndUserData
      ? "读取产品与用户数据"
      : shouldRefresh ? "读取产品数据" : "读取用户数据",
    mode: "Tube Designer",
    minimumVisibleMs: TUBE_DESIGNER_LOAD_PROGRESS_MINIMUM_VISIBLE_MS,
  };
  view.tubeDesignerLoadProgressToken = token;
  return { token, startedAt: nowMilliseconds() };
}

function updateDesignerLoadProgress(view, operation, stage, detail) {
  if (!operation
      || view.tubeDesignerLoadProgressToken !== operation.token
      || !view.tubeDesignerLoadProgress) {
    return false;
  }
  view.tubeDesignerLoadProgress = {
    ...view.tubeDesignerLoadProgress,
    stage,
    detail,
  };
  return true;
}

async function finishDesignerLoadProgress(view, operation) {
  if (!operation) return;
  await waitForMinimumDuration(
    operation.visibleAt ?? operation.startedAt,
    TUBE_DESIGNER_LOAD_PROGRESS_MINIMUM_VISIBLE_MS,
  );
  if (view.tubeDesignerLoadProgressToken !== operation.token) return;
  view.tubeDesignerLoadProgress = null;
  view.tubeDesignerLoadProgressToken = null;
}

function waitForPaint() {
  if (typeof globalThis.requestAnimationFrame !== "function") return Promise.resolve();
  return new Promise((resolve) => {
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      globalThis.clearTimeout(fallback);
      resolve();
    };
    const fallback = globalThis.setTimeout(finish, 100);
    globalThis.requestAnimationFrame(() => globalThis.requestAnimationFrame(finish));
  });
}

function waitForMinimumDuration(startedAt, minimumVisibleMs) {
  const remaining = Number(minimumVisibleMs) - (nowMilliseconds() - Number(startedAt));
  if (!Number.isFinite(remaining) || remaining <= 0) return Promise.resolve();
  return new Promise((resolve) => globalThis.setTimeout(resolve, remaining));
}

function nowMilliseconds() {
  return globalThis.performance?.now?.() ?? Date.now();
}
