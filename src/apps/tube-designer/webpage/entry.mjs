import * as workbench from "../../_shared/workbench/entry.mjs";
import { releaseProject as releaseWorkbenchProject } from "../../_shared/workbench/createWorkbench.mjs";
import { installTubeDesignerBranding } from "./branding.mjs";
import { bindTubeDesignerWindowMemory, rememberLinkedWindowControls } from "./windowStateMemory.mjs";
import { restoreLibraryNavigation, rememberLibraryNavigation } from "./libraryNavigationMemory.mjs";
import { attachPunchEditor } from "./punchEditor.mjs";
import { patchPunchDom, closePunchDom } from "./punchDomPatch.mjs";
import { cancelDesignerPartThumbnailHydration, scheduleDesignerPartThumbnailHydration } from "./partThumbnail.mjs";
import { disposeDesignerPartInspection, scheduleDesignerPartInspectionHydration } from "./partInspection.mjs";
import { attachPartDrawingEditor, renderPartDrawingDialog } from "./partDrawing.mjs";
import { attachPartDrawingPreview, disposePartDrawingPreview } from "./partDrawingPreview.mjs";
import { disposeSideSketchPreview } from "./sideSketchPreview.mjs";
import { patchPartDrawingDom, rememberPartDrawingDom } from "./partDrawingDom.mjs";
import { patchLibraryDom, rememberLibraryDom } from "./libraryDomPatch.mjs";
import { bindFloatingEditorWindows, patchFloatingEditorDom, rememberFloatingEditorDom } from "./floatingEditorDom.mjs";
import { capturePaneInteraction } from "../../_shared/workbench/utils/paneInteractionState.mjs";
import { bindProductPartsScene, hoverProductSceneMember, selectProductSceneMember, updateProductSceneEmphasis } from "./productPartsScene.mjs";
import { attachNestingDocking, captureNestingDockInteraction,
  nestingDockZoneCounts, restoreNestingDockInteraction } from "./nestingDock.mjs";
import {
  bindDesignerProductPartsInspection,
  synchronizeBatchExcelTemplateSelection,
  fitDesignerDefaultView,
  captureDesignerScrollState,
  clearDesignerRightParameterHistories,
  getDesignerProjectHistoryState,
  getDesignerRenderSignature,
  handleDesignerAreaAction,
  handleDesignerRibbonCommand,
  preloadTubeDesignerTemplates,
  prepareDesignerProjectSave,
  rememberDesignerCreationWindowState,
  refreshDesignerState,
  refreshDesignerUserData,
  restoreLoadedProductTemplateDescriptors,
  restoreDesignerScrollState,
} from "./designerActions.mjs";
import {
  renderDesignerLeftPane,
  renderDesignerDialogs,
  renderDesignerOperationOverlay,
  renderDesignerProductPartsDock,
  renderDesignerRightPane,
  renderDesignerViewportOverlay,
} from "./designerViews.mjs";
import { getRibbonDefinition as getDesignerRibbonDefinition } from "./ribbonDefinition.mjs";
import { ensureTubeDesignerStyles } from "./styles/ensureStyles.mjs";
import { renderNestingSettingsDialogs } from "./nestingSettings.mjs";
import { connectBatchExcelAutomation, renderBatchExcelAutomationDialog, stopBatchExcelAutomation } from "./batchExcelAutomation.mjs";
import { renderNestingStandardPartDialog, captureNestingStandardPartDisclosures,
  rememberNestingStandardPartDom, patchNestingStandardPartDom } from "./nestingStandardPart.mjs";
import { disposeInactiveNestingPartFilePicker, disposeNestingPartFilePicker } from "./nestingPartFilePicker.mjs";
import { bindProfileParameterDiagrams } from "./profileParameterDiagram.mjs";
import { bindToolParameterDiagrams } from "./toolParameterDiagram.mjs";
import { bindDiagramDragging } from "./floatingParameterDiagram.mjs";
import {
  bindProductParameterDiagrams,
  bindProductSpecificationAnnotations,
  productSceneMemberIds,
} from "./productParameterDiagram.mjs";
import { renderNestingPunchPartDialog } from "./nestingPunchPart.mjs";
import { attachTubeMachining, handleTubeMachiningViewportPick, renderTubeMachiningLeftPane, renderTubeMachiningRightPane, renderTubeMachiningViewportOverlay, renderTubeMachiningDialogs } from "./machiningArea.mjs";
import { hasUnsavedMachiningPaths } from "./machiningEditor.mjs";
import { findProjectView, getProjectView } from "../../_shared/workbench/state/projectViewStore.mjs";
import { restoreProductResources } from "./productResourceSession.mjs";
import {
  actionLicenseFeature, allowLicenseFeature, commandLicenseFeature,
  ensureLicenseStatus, handleLicenseCommand, hasLicenseFeature, licenseDenialMessage,
  isLicenseWorkspaceReadOnly,
} from "./licensing.mjs";
import { renderProgress } from "../../_shared/workbench/layout/commonViews.mjs";
import { typedVariant } from "../../../iCAX-UI/SDK/SDO/variantSerializer.mjs";
import {
  renderProfileLibraryLeftPane,
  renderProfileLibraryRightPane,
  renderProfileLibraryViewportOverlay,
  bindProfileSpecificationAnnotations,
} from "./profileLibrary.mjs";
import {
  ensureToolLibraryCatalogue,
  renderToolLibraryLeftPane,
  renderToolLibraryRightPane,
  renderToolLibraryViewportOverlay,
} from "./toolLibrary.mjs";
import {
  attachAssemblyLibraryViewports,
  bindAssemblyParameterDiagrams,
  disposeAssemblyLibraryViewports,
  ensureAssemblyLibraryCatalogue,
  renderAssemblyLibraryLeftPane,
  renderAssemblyLibraryRightPane,
  renderAssemblyLibraryViewportOverlay,
} from "./assemblyLibrary.mjs";
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
  handlePartsAreaViewportPick,
  renderNestingLeftPane,
  renderNestingResultDock,
  renderNestingRightPane,
  renderNestingViewportOverlay,
  renderPunchWizardDialog,
  restoreNestingPartListScroll,
} from "./partsArea.mjs";
import {
  attachSketchAreaInteractions,
  patchSketchDialogDom,
  rememberSketchDialogDom,
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
  captureProductTemplateLibraryScrollState,
  renderProductTemplateLibraryLeftPane,
  renderProductTemplateLibraryRightPane,
  renderProductTemplateLibraryViewportOverlay,
  restoreProductTemplateLibraryScrollState,
} from "./templateLibrary.mjs";

export const TUBE_DESIGNER_LOAD_PROGRESS_MINIMUM_VISIBLE_MS = 500;
const sketchBackgroundRendering = new WeakMap();

function synchronizeSketchBackgroundRendering(view) {
  const viewport = view?.viewport;
  if (!viewport) return;
  const previous = sketchBackgroundRendering.get(view);
  if (view.tubeDesignerSketchDialogOpen) {
    if (previous?.viewport !== viewport) sketchBackgroundRendering.set(view, {
      viewport, continuous: viewport.continuousRendering !== false,
    });
    viewport.setContinuousRendering?.(false);
  } else if (previous) {
    sketchBackgroundRendering.delete(view);
    if (previous.viewport === viewport)
      viewport.setContinuousRendering?.(previous.continuous);
  }
}

export function getRibbonDefinition(context = {}) {
  const projectId = context.project?.projectId ?? "";
  const view = getProjectView(projectId);
  return getDesignerRibbonDefinition({
    resourceArea: view.tubeDesignerResourceLibraryArea ?? "profiles",
    licenseContext: context,
    licenseView: view,
  });
}

export function beforeSelectRibbonTab() {
  // Navigation and viewing are public; the commands and editors enforce grants.
  return true;
}

export function isProjectReadOnly(context) {
  return isLicenseWorkspaceReadOnly(context, getProjectView(context.project?.projectId ?? ""));
}

export function shouldPreserveRibbonCommandSurface(context, commandId) {
  const view = getProjectView(context.project?.projectId ?? "");
  return commandId.startsWith("licensing.")
    || isLicenseWorkspaceReadOnly(context, view)
    || !hasLicenseFeature(context, view, commandLicenseFeature(commandId, view.activeAreaId));
}

export function mountProduct(context) {
  disposeInactiveNestingPartFilePicker(context, null);
  ensureTubeDesignerStyles();
  installTubeDesignerBranding();
  bindTubeDesignerWindowMemory(context, null);
  workbench.mountProduct(context);
}

export async function prepareProjectSave(context) {
  const designerContext = withDesignerContext(context);
  await workbench.waitForActiveAreaAction(designerContext);
  await prepareDesignerProjectSave(designerContext, getProjectView(context.project?.projectId ?? ""));
}

export async function releaseProject(context) {
  const view = findProjectView(context?.project?.projectId);
  if (!view) return false;
  // Product catalogues remain in the product session; only this project's
  // editors, scene readers and renderer belong to the closed file.
  const released = releaseWorkbenchProject(context);
  const cleanup = (work) => { try { work(); } catch {} };
  cleanup(() => disposeNestingPartFilePicker(view));
  cleanup(() => disposeAssemblyLibraryViewports(view));
  if (context.mount && context.isCurrentProject?.() !== false) {
    cleanup(() => cancelDesignerPartThumbnailHydration(context));
    cleanup(() => disposeDesignerPartInspection(context));
    cleanup(() => disposePartDrawingPreview(context.mount));
    cleanup(() => disposeSideSketchPreview(context.mount));
    cleanup(() => stopBatchExcelAutomation());
  }
  return released;
}

export async function mountProject(context) {
  if (context.isCurrentProject?.() === false) return;
  ensureTubeDesignerStyles();
  installTubeDesignerBranding();
  const view = getProjectView(context.project?.projectId ?? "");
  await ensureLicenseStatus(context, view);
  if (view.disposed || context.isCurrentProject?.() === false) return;
  context.actions?.refreshProductRibbon?.();
  if (context.activeRibbonTabId === "about") {
    return workbench.mountProject(withDesignerContext(context));
  }
  restoreProductResources(context, view);
  bindTubeDesignerWindowMemory(context, view);
  // Keep the unified workflow presentation; page and operation access is
  // checked independently through the native capability catalogue.
  view.tubeDesignerProductionAccess = true;
  view.tubeDesignerResourceLibraryArea ??= "profiles";
  if (view.tubeDesignerResourceLibraryArea === "components") {
    view.tubeDesignerResourceLibraryArea = "profiles";
  }
  view.scene ??= {};
  const historyToken = getHistoryToken(context.scene);
  const historyChanged = view.tubeDesignerHistoryToken !== undefined
    && view.tubeDesignerHistoryToken !== historyToken;
  view.tubeDesignerHistoryToken = historyToken;
  const ownMutation = historyChanged && view.tubeDesignerOwnMutation;
  if (ownMutation) {
    view.tubeDesignerOwnMutation = false;
  }
  if (historyChanged && !ownMutation) {
    clearDesignerRightParameterHistories(view, true);
  }
  const designerContext = withDesignerContext(context);
  const shouldRefresh = (!view.tubeDesignerLoaded || (historyChanged && !ownMutation))
    && !view.tubeDesignerLoading;
  const shouldRefreshUserData = !shouldRefresh && !view.tubeDesignerUserDataLoaded
    && !view.tubeDesignerUserDataLoading;
  if (shouldRefresh) {
    view.tubeDesignerLoading = true;
    view.error = "";
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
        if (!view.disposed && designerContext.isCurrentProject?.() !== false) workbench.mountProject(designerContext);
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
    let synchronized = false;
    try {
      if (loadProgress) {
        await waitForPaint();
        loadProgress.visibleAt = nowMilliseconds();
      }
      await refreshDesignerState(designerContext, view);
      if (view.disposed || designerContext.isCurrentProject?.() === false) return;
      await preloadTubeDesignerTemplates(designerContext, view,
        async (kind, completed, total, detail) => {
          const labels = {
            product: "产品模板",
            profile: "管型模板",
            tool: "单件工艺",
          };
          const count = Number.isFinite(total)
            ? `${Math.min(Number(completed) || 0, total)} / ${total}`
            : "正在读取…";
          const resourceLabel = labels[kind] ?? "模板";
          if (updateDesignerLoadProgress(view, loadProgress,
            `加载${resourceLabel} ${count}`, detail, `正在加载${resourceLabel}`)) {
            if (view.disposed || designerContext.isCurrentProject?.() === false) return;
            workbench.mountProject(designerContext);
            await waitForPaint();
          }
        });
      const designer = view.scene?.tubeDesigner ?? {};
      if (view.disposed || designerContext.isCurrentProject?.() === false) return;
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
          "正在载入产品场景",
        )) {
          workbench.mountProject(designerContext);
          await waitForPaint();
        }
        if (view.disposed || designerContext.isCurrentProject?.() === false) return;
        const viewContent = await workbench.synchronizeActiveAreaView(designerContext, expectation);
        if (view.disposed || designerContext.isCurrentProject?.() === false) return;
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
      synchronized = true;
    } finally {
      await finishDesignerLoadProgress(view, loadProgress);
      if (view.tubeDesignerSynchronizationPromise === synchronization) {
        view.tubeDesignerLoading = false;
        view.tubeDesignerLoaded = synchronized;
      }
      resolveSceneRefreshReady();
      if (!view.disposed && designerContext.isCurrentProject?.() !== false) workbench.mountProject(designerContext);
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
  const view = getProjectView(context.project?.projectId ?? "");
  if (!allowDesignerInteraction(context, view, commandLicenseFeature(commandId, view.activeAreaId), commandId)) return true;
  return workbench.handleRibbonCommand(withDesignerContext(context), commandId);
}

export function getProjectHistoryState(context) {
  return getDesignerProjectHistoryState(getProjectView(context.project?.projectId ?? ""));
}

export function handleProjectHistoryCommand(context, direction) {
  const commandId = direction === "redo" ? "designer.history.redo" : "designer.history.undo";
  const view = getProjectView(context.project?.projectId ?? "");
  if (!allowLicenseFeature(context, view, commandLicenseFeature(commandId, view.activeAreaId))) return true;
  return workbench.handleRibbonCommand({
    ...withDesignerContext(context),
    onlyHandleAreaRibbonCommand: true,
  }, commandId);
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

function allowDesignerInteraction(context, view, featureId, commandId = "") {
  if (!commandId.startsWith("licensing.") && isLicenseWorkspaceReadOnly(context, view)) {
    return allowLicenseFeature(context, view, "page.product");
  }
  return allowLicenseFeature(context, view, featureId);
}

function withDesignerContext(context) {
  return {
    ...context,
    noticePresentation: "log",
    errorPresentation: "log",
    forceThreeViewport: true,
    showViewportGrid: false,
    areaTitleOverrides: { view: "产品", nesting: "下料", machining: "加工", resources: "资源库", templates: "产品模板", profiles: "管型库", tools: "单件工艺库", assemblies: "装配库", components: "配件库", sketch: "草图", about: "关于" },
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
      assemblies: {
        left: renderAssemblyLibraryLeftPane,
        right: renderAssemblyLibraryRightPane,
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
        return ["products", "profiles", "tools", "assemblies"].includes(resourceArea)
          ? (resourceArea === "products" ? "templates" : resourceArea) : "profiles";
      }
      return ["view", "nesting", "machining", "templates", "profiles", "tools", "assemblies", "components", "sketch", "about"].includes(tabId)
        ? tabId : "view";
    },
    resolveWorkbenchPresentation: (_context, _view, _scene, areaId) => ({
      className: `tube-designer-workspace ${areaId === "view" ? "tube-designer-product-workspace" : ""} ${areaId === "nesting" ? `tube-designer-production-workspace ${Object.entries(nestingDockZoneCounts(_view, _context)).filter(([, count]) => !count).map(([zone]) => `tube-designer-dock-${zone}-empty`).join(" ")}` : ""} ${areaId === "assemblies" ? "tube-designer-assembly-workspace" : ""} ${areaId === "sketch" ? "tube-designer-sketch-workspace" : ""} ${areaId === "about" ? "tube-designer-about-workspace" : ""}`,
      style: areaId === "nesting" ? renderNestingWorkspaceStyle(_view, _context) : areaId === "view" ? renderProductWorkspaceStyle() : "",
    }),
    resolveViewportBackgroundColor: () => 0x13252d,
    configureViewport: configureDesignerViewport,
    resolveAreaViewDefinition: resolveDesignerAreaViewDefinition,
    renderViewportOverlay: renderDesignerAreaViewportOverlay,
    renderWorkbenchSuffix: renderDesignerWorkbenchSuffix,
    refreshLicenseWorkspace: () => mountProject(context),
    refreshLicenseControls: (view) => applyLicenseControlState(context, view, context.mount),
    async handleAreaAction(context, view, action, target, ops) {
      if (await handleLicenseCommand(context, view, action, ops)) return { handled: true };
      if (!allowDesignerInteraction(context, view, actionLicenseFeature(action, view.activeAreaId))) return { handled: true };
      const result = await handleDesignerAreaAction(context, view, action, target, ops);
      rememberDesignerCreationWindowState(context, view);
      rememberLinkedWindowControls(context, view, action, target);
      rememberLibraryNavigation(view);
      return result;
    },
    handleAreaViewportPick: handleDesignerViewportPick,
    handleAreaViewportHover: handleDesignerViewportHover,
    handleAreaRibbonCommand: async (context, view, commandId, ops) => {
      if (!allowDesignerInteraction(context, view, commandLicenseFeature(commandId, view.activeAreaId), commandId)) return true;
      // Authorization remains available even while another editor is pending.
      if (await handleLicenseCommand(context, view, commandId, ops)) return true;
      return handleDesignerRibbonCommand(context, view, commandId, ops);
    },
    tryRenderProjectPatch(context,view,mount,ops) {
      if (patchSketchDialogDom(context, view, mount, ops)) return true;
      if(view.activeAreaId==='nesting' && !view.tubeDesignerPartDrawing && !view.tubeDesignerPunchWizard
        && !view.tubeDesignerNestingStandardPartDraft && patchFloatingEditorDom(view,mount,{
        suffix:renderDesignerWorkbenchSuffix(context,view,view.scene??{}),
        left:renderNestingLeftPane(context,view), right:renderNestingRightPane(context,view),
        overlay:()=>renderNestingViewportOverlay(context,view),
        sceneProxy:context.resolveSceneProxy?.(context,view) ?? context.sceneProxy ?? null,
      }))return true;
      if(view.activeAreaId==='nesting' && view.tubeDesignerNestingStandardPartDraft) {
        const html=renderNestingStandardPartDialog(view)+renderDesignerOperationOverlay(context,view);
        if(!patchNestingStandardPartDom(view,mount,html,context.sceneProxy))return false;
        bindProfileParameterDiagrams(mount);
        return true;
      }
      if(["tools","profiles","assemblies","view"].includes(view.activeAreaId)) {
        const tools=view.activeAreaId==="tools";
        const assemblies=view.activeAreaId==="assemblies";
        const product=view.activeAreaId==="view";
        const inspection = product && retainPartInspectionWindow(mount);
        const patched=patchLibraryDom(view,mount,{
          left:(product?renderDesignerLeftPane:assemblies?renderAssemblyLibraryLeftPane:tools?renderToolLibraryLeftPane:renderProfileLibraryLeftPane)(context,view),
          right:product ? renderDesignerRightPane(context,view,{includePartInspection:!inspection})
            : (assemblies?renderAssemblyLibraryRightPane:tools?renderToolLibraryRightPane:renderProfileLibraryRightPane)(context,view),
          overlay:(product?renderDesignerViewportOverlay:assemblies?renderAssemblyLibraryViewportOverlay:tools?renderToolLibraryViewportOverlay:renderProfileLibraryViewportOverlay)(context,view),
          suffix:renderDesignerWorkbenchSuffix(context,view,view.scene??{}),
          sceneProxy:context.resolveSceneProxy?.(context,view) ?? context.sceneProxy ?? null,
        });
        if(patched){bindDiagramDragging(mount,view);bindProfileParameterDiagrams(mount);bindToolParameterDiagrams(mount);bindAssemblyParameterDiagrams(mount);bindProductSceneParameterHighlights(mount,view,(level,message)=>ops.appendProjectLog(context,level,message));bindProductParameterDiagrams(mount);return true;}
        return false;
      }
      if(view.tubeDesignerPartDrawing&&view.activeAreaId==="nesting") {
        const html=renderPartDrawingDialog(view)+renderDesignerOperationOverlay(context,view);
        if(!patchPartDrawingDom(view,mount,html))return false;
        bindDiagramDragging(mount,view);
        bindProfileParameterDiagrams(mount);
        attachPartDrawingPreview(context,view,mount,ops);
        attachPartDrawingEditor(context,view,mount,ops);
        return true;
      }
      if(closePunchDom(view,mount)) {
        attachPunchEditor(context,view,mount,ops);
        return true;
      }
      if(!view.tubeDesignerPunchWizard||view.tubeDesignerPartDrawing||view.activeAreaId!=="nesting")return false;
      const html=(renderNestingPunchPartDialog(view)||renderPunchWizardDialog(context,view))
        +renderDesignerOperationOverlay(context,view);
      if(!patchPunchDom(view,mount,html))return false;
      bindProfileParameterDiagrams(mount);
      bindToolParameterDiagrams(mount);
      bindProductSceneParameterHighlights(mount, view, (level, message) => ops.appendProjectLog(context, level, message));
      bindProductParameterDiagrams(mount);
      attachPunchEditor(context,view,mount,ops);
      return true;
    },
    beforeProjectRender(context, view) {
      synchronizeSketchBackgroundRendering(view);
      captureNestingStandardPartDisclosures(view, context.mount);
      restoreLibraryNavigation(view);
      disposeInactiveNestingPartFilePicker(context, view);
      captureNestingDockInteraction(context.mount, view);
      captureDesignerScrollState(context, view);
      captureProductTemplateLibraryScrollState(context, view);
    },
    afterProjectPatch(context, view, _mount, ops) {
      applyLicenseControlState(context, view, _mount);
      bindFloatingEditorWindows(_mount,view);
      rememberFloatingEditorDom(view,_mount,{suffix:renderDesignerWorkbenchSuffix(context,view,view.scene??{}),
        sceneProxy:context.resolveSceneProxy?.(context,view) ?? context.sceneProxy ?? null});
      connectBatchExcelAutomation(context, view, { ...ops, refreshDesignerState, fitDesignerDefaultView });
      synchronizeBatchExcelTemplateSelection(view, _mount);
      bindDesignerProductPartsInspection(context, view, ops);
      restoreNestingDockInteraction(view);
      restoreDesignerScrollState(context, view, { deferred: false });
      restoreProductTemplateLibraryScrollState(context, view);
      bindDiagramDragging(context.mount,view);
      bindToolParameterDiagrams(context.mount);
      bindAssemblyParameterDiagrams(context.mount);
      attachAssemblyLibraryViewports(context, view, context.mount);
      bindTubeDesignerWindowMemory(context, view, ops);
    },
    afterProjectRender(context, view, mount, ops) {
      applyLicenseControlState(context, view, mount);
      bindFloatingEditorWindows(mount,view);
      rememberFloatingEditorDom(view,mount,{suffix:renderDesignerWorkbenchSuffix(context,view,view.scene??{}),
        sceneProxy:context.resolveSceneProxy?.(context,view) ?? context.sceneProxy ?? null});
      rememberNestingStandardPartDom(view, mount, context.sceneProxy);
      rememberSketchDialogDom(view, mount, context.sceneProxy);
      connectBatchExcelAutomation(context, view, { ...ops, refreshDesignerState, fitDesignerDefaultView });
      synchronizeBatchExcelTemplateSelection(view, mount);
      bindDesignerProductPartsInspection(context, view, ops);
      rememberLibraryDom(view,mount,renderDesignerWorkbenchSuffix(context,view,view.scene??{}),
        context.resolveSceneProxy?.(context,view) ?? context.sceneProxy ?? null);
      attachNestingDocking(context, view, mount, ops);
      restoreDesignerScrollState(context, view);
      restoreProductTemplateLibraryScrollState(context, view);
      bindProfileParameterDiagrams(mount);
      bindToolParameterDiagrams(mount);
      bindAssemblyParameterDiagrams(mount);
      bindProductSceneParameterHighlights(mount, view, (level, message) => ops.appendProjectLog(context, level, message));
      bindProductParameterDiagrams(mount);
      bindDiagramDragging(mount,view);
      if (view.activeAreaId === "assemblies") attachAssemblyLibraryViewports(context, view, mount);
      else disposeAssemblyLibraryViewports(view);
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
      if (view.activeAreaId === "assemblies") {
        view.tubeDesignerAssemblyLibraryRenderProject = () => ops.renderProject(context, view);
        const catalogueStatus = view.tubeDesignerAssemblyLibrary?.catalogueStatus ?? "idle";
        if (catalogueStatus === "idle") ensureAssemblyLibraryCatalogue(context, view, ops);
      }
      if (view.activeAreaId === "templates") {
        view.tubeDesignerProductTemplateLibraryRenderProject = () => ops.renderProject(context, view);
      }
      if (view.activeAreaId === "nesting" && view.tubeDesignerBreakdownOpen && !view.tubeDesignerPartInspectionOpen) {
        scheduleDesignerPartThumbnailHydration(context);
      }
      if (view.activeAreaId === "nesting" && view.tubeDesignerPartInspectionOpen) {
        scheduleDesignerPartInspectionHydration(context, view.scene?.tubeDesigner ?? {}, view);
      }
      restoreNestingPartListScroll(context, view, mount);
      restoreNestingDockInteraction(view);
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
      bindTubeDesignerWindowMemory(context, view, ops);
    },
  };
}

function renderNestingWorkspaceStyle(view, context) {
  const counts = nestingDockZoneCounts(view, context);
  return [
    "width:100%",
    "height:100%",
    `grid-template-columns:${counts.left ? "var(--cam-left-width,300px) 5px" : "0px 0px"} minmax(0,1fr) ${counts.right ? "5px var(--cam-right-width,320px)" : "0px 0px"}`,
    `grid-template-rows:auto minmax(0,1fr) ${counts.bottom ? "5px var(--cam-bottom-height,156px)" : "0px 0px"}`,
    "grid-template-areas:'notice notice notice notice notice' 'left left-resize viewer right-resize right' 'bottom-resize bottom-resize bottom-resize bottom-resize bottom-resize' 'bottom bottom bottom bottom bottom'",
  ].join(";");
}

function retainPartInspectionWindow(mount) {
  const inspection = mount.querySelector(".tube-designer-part-inspection-backdrop");
  if (!inspection || inspection.parentElement === mount) return inspection;
  const restorePanes = capturePaneInteraction(mount);
  const active = mount.ownerDocument.activeElement;
  const focused = inspection.contains(active);
  const scrolls = [inspection, ...inspection.querySelectorAll("*")]
    .filter((node) => node.scrollTop || node.scrollLeft)
    .map((node) => [node, node.scrollTop, node.scrollLeft]);
  mount.appendChild(inspection);
  if (focused && active.isConnected) active.focus({ preventScroll: true });
  restorePanes();
  for (const [node, top, left] of scrolls) { node.scrollTop = top; node.scrollLeft = left; }
  return inspection;
}

function renderProductWorkspaceStyle() {
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
          restoreLoadedProductTemplateDescriptors(view, response.tubeDesigner);
          view.tubeDesignerPartViewportKey = "";
          return workbench.mountProject(_context);
        }).catch(error => { view.error = `零件几何重建失败：${error?.message ?? error}`; });
    }
  }
  const viewport = view.viewport;
  if (!viewport) return;
  const normalizedAreaId = areaId === "parts" ? "nesting"
    : (["view", "nesting", "machining", "templates", "profiles", "tools", "assemblies", "components", "sketch", "about"].includes(areaId) ? areaId : "view");
  view.tubeDesignerProjectionModes ??= {};
  const projectionMode = view.tubeDesignerProjectionModes[normalizedAreaId] ?? "perspective";
  viewport.setProjectionToggleVisible?.(!["sketch", "about", "assemblies"].includes(normalizedAreaId));
  // Product members can be selected directly to locate their realised parts.
  // Specification annotations keep their own double-click editing path.
  viewport.setPickingEnabled?.(!["profiles", "assemblies", "sketch", "about"].includes(normalizedAreaId));
  synchronizeSketchBackgroundRendering(view);
  viewport.setContinuousRendering?.(!view.tubeDesignerSketchDialogOpen
    && !["sketch", "assemblies"].includes(normalizedAreaId));
  viewport.setProjectionChangeHandler?.((mode) => {
    const currentAreaId = ["view", "templates", "profiles", "tools", "assemblies", "components", "nesting", "machining"].includes(view.activeAreaId)
      ? view.activeAreaId : "view";
    view.tubeDesignerProjectionModes ??= {};
    view.tubeDesignerProjectionModes[currentAreaId] = mode;
  });
  viewport.setProjectionMode?.(projectionMode);
  viewport.setOrbitConstrained?.(normalizedAreaId === "view");
  viewport.setBlankDoubleClickFitEnabled?.(!["sketch", "about"].includes(normalizedAreaId));
}

function resolveDesignerAreaViewDefinition(_context, view, areaId, fallback) {
  if (["templates", "profiles", "tools", "assemblies", "components", "sketch", "nesting", "machining", "about"].includes(areaId)) {
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
  if (view.activeAreaId === "assemblies") return renderAssemblyLibraryViewportOverlay(context, view, scene);
  if (view.activeAreaId === "components") return renderComponentLibraryViewportOverlay(context, view, scene);
  if (view.activeAreaId === "sketch") return renderSketchViewportOverlay(context, view, scene);
  if (view.activeAreaId === "nesting") return renderNestingViewportOverlay(context, view, scene);
  if (view.activeAreaId === "machining") return renderTubeMachiningViewportOverlay(context, view);
  if (view.activeAreaId === "about") return renderAboutViewportOverlay(context, view, scene);
  return renderDesignerViewportOverlay(context, view, scene);
}

const productSceneParameterHighlightListeners = new WeakSet();
export function bindProductSceneParameterHighlights(mount, view, _appendLog = null) {
  bindProductSpecificationAnnotations(mount, view);
  bindProfileSpecificationAnnotations(mount, view);
  bindProductPartsScene(mount, view);
  if (!mount || productSceneParameterHighlightListeners.has(mount)) return;
  productSceneParameterHighlightListeners.add(mount);
  const clear = () => {
    view.tubeDesignerProductParameterHighlightIds = [];
    updateProductSceneEmphasis(view);
  };
  mount.addEventListener("tube-designer-product-parameter-focus", (event) => {
    const { key, mode, profileRole, category } = event.detail ?? {};
    if (mode !== "right" || view.activeAreaId !== "view") {
      clear();
      return;
    }
    // Only the retained material/process inspector participates. Specifications
    // are edited through green/red scene annotations and never enter this path.
    if (!key || !["materials", "process"].includes(String(category ?? ""))) {
      clear();
      return;
    }
    const designer = view.scene?.tubeDesigner ?? {};
    const product = designer.product;
    const template = (designer.templates ?? []).find((item) => item?.id === product?.templateId);
    const ids = productSceneMemberIds(template, designer.members ?? [], key, { profileRole });
    view.tubeDesignerProductParameterHighlightIds = ids;
    updateProductSceneEmphasis(view);
  });
}

export function handleDesignerViewportPick(context, view, userData, hit, event, hits, ops) {
  if (view.activeAreaId === "view") {
    if (view.pending || view.tubeDesignerExportOperation) return true;
    const selected = selectProductSceneMember(context.mount, view, hit ? userData?.objectId ?? userData?.entityId ?? "" : "", { reveal: true });
    if (!hit || selected) return true;
  }
  if (handlePartsAreaViewportPick(context, view, userData, hit, event)) return true;
  return handleTubeMachiningViewportPick(context, view, userData, hit, event, hits, ops);
}

export function handleDesignerViewportHover(context, view, userData, hit, event, hits, { pointerActive = false } = {}) {
  return hoverProductSceneMember(context.mount, view, hit ? userData?.objectId ?? userData?.entityId ?? "" : "", { active: pointerActive });
}

function renderDesignerWorkbenchSuffix(context, view, scene) {
  const punchWizardDialog = view.activeAreaId === "nesting"
    ? renderPunchWizardDialog(context, view)
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
  const productPartsDock = view.activeAreaId === "view"
    ? renderDesignerProductPartsDock(context, view)
    : "";
  const areaDialogs = view.activeAreaId === "view"
    ? ""
    : renderDesignerDialogs(view.scene?.tubeDesigner ?? {}, view);
  const loadProgress = view.tubeDesignerLoadProgress;
  // During application startup the loading status belongs to the animated
  // startup screen. Later project/history loads retain their regular feedback.
  const startupOwnsProgress = loadProgress
    && context.actions?.reportStartupProgress?.(loadProgress, context.project?.projectId) === true;
  return `${renderBatchExcelAutomationDialog(view)}${view.activeAreaId === "nesting" ? renderPartDrawingDialog(view) : ""}${view.activeAreaId === "machining" ? renderTubeMachiningDialogs(view) : ""}${renderSectionSketchDialog(context, view)}${nestingResultDock}${productPartsDock}${areaDialogs}${nestingStandardPartDialog}${nestingPunchPartDialog}${nestingSettingsDialogs}${punchWizardDialog}${renderComponentLibraryDialogs(view)}${renderDesignerOperationOverlay(context, view, scene)}${
    loadProgress && !startupOwnsProgress ? renderProgress(loadProgress) : ""
  }`;
}

const licenseControlSelector = [
  "[data-cam-action]", "[data-cam-change-action]",
  "[data-tube-designer-parameter]", "[data-product-control-key]",
  "[data-tube-designer-instance-quantity]", "[data-tube-designer-part-field]",
  "[data-tube-designer-punch-field]", "[data-tube-designer-punch-profile-parameter]",
  "[data-tube-designer-punch-array-field]", "[data-tube-designer-nesting-punch-field]",
  "[data-tube-designer-main-profile-parameter]", "[data-punch-batch-field]",
  "[data-punch-batch-definition-feature-id]", "[data-drawing-field]", "[data-drawing-parameter]",
  "[data-tube-profile-editor-name]", "[data-tube-profile-editor-parameter]",
  "[data-tube-template-create-base]", "[data-tube-template-create-name]", "[data-tube-template-create-description]",
  "[data-tube-template-library-parameter]", "[data-profile-parameter-key]", "[data-tool-parameter-key]", "[data-operation-parameter-key]",
  "[data-assembly-parameter-key]", "[data-tube-part-process-parameter]", "[data-process-anchor-key]",
  "[data-stock-operation-parameter]", "[data-stock-instance]", "[data-finished-parameter]", "[data-finished-profile-parameter]",
].join(",");
const licenseControlBindings = new WeakMap();

function controlLicenseFeature(control, areaId) {
  // Some existing editors collect their fields when Save is pressed instead
  // of giving each input a delegated action. Gate those fields as well.
  if (control.matches("input,select,textarea")) {
    if (control.matches("[data-tube-designer-part-field],[data-tube-designer-punch-field],[data-tube-designer-punch-profile-parameter],[data-tube-designer-punch-array-field],[data-tube-designer-nesting-punch-field],[data-tube-designer-main-profile-parameter],[data-punch-batch-field],[data-punch-batch-definition-feature-id],[data-drawing-field],[data-drawing-parameter]")) return "nesting.edit";
    if (areaId === "view" && control.matches("[data-tube-designer-parameter],[data-product-control-key],[data-tube-designer-instance-quantity]")) return "product.design";
    if (["templates", "profiles", "tools", "assemblies", "components"].includes(areaId)
        && !control.dataset.camAction && !control.dataset.camChangeAction) return ["product.design", "nesting.edit"];
  }
  return actionLicenseFeature(control.dataset.camAction ?? control.dataset.camChangeAction ?? "", areaId);
}

function applyLicenseControlState(context, view, mount, roots = [mount]) {
  if (!mount?.querySelectorAll) return;
  const readOnly = isLicenseWorkspaceReadOnly(context, view);
  // The public tabs and licensing commands live outside this surface.
  // Inert also stops camera, annotation, list and splitter interactions.
  mount.inert = readOnly;
  const controls = new Set(roots.flatMap(root => root?.querySelectorAll
    ? [...(root.matches?.(licenseControlSelector) ? [root] : []), ...root.querySelectorAll(licenseControlSelector)] : []));
  for (const control of controls) {
    const featureId = controlLicenseFeature(control, view.activeAreaId);
    const denied = readOnly || !hasLicenseFeature(context, view, featureId);
    if (denied) {
      if (!control.dataset.tubeDesignerLicenseDisabled) {
        control.dataset.tubeDesignerLicenseDisabled = "1";
        control.dataset.tubeDesignerLicensePreviousDisabled = control.disabled ? "1" : "0";
        control.dataset.tubeDesignerLicensePreviousTitle = control.getAttribute("title") ?? "";
      }
      control.setAttribute("aria-disabled", "true");
      control.setAttribute("title", licenseDenialMessage(context, view, featureId));
      if (control.matches("button,input,select,textarea")) {
        control.disabled = true;
      }
    } else if (control.dataset.tubeDesignerLicenseDisabled) {
      delete control.dataset.tubeDesignerLicenseDisabled;
      if (control.matches("button,input,select,textarea")) control.disabled = control.dataset.tubeDesignerLicensePreviousDisabled === "1";
      control.removeAttribute("aria-disabled");
      if (control.dataset.tubeDesignerLicensePreviousTitle) control.setAttribute("title", control.dataset.tubeDesignerLicensePreviousTitle);
      else control.removeAttribute("title");
      delete control.dataset.tubeDesignerLicensePreviousDisabled;
      delete control.dataset.tubeDesignerLicensePreviousTitle;
    }
  }
  // Scene annotation inputs and inline sheet editors can be inserted without
  // a workbench render. Apply the same policy to those actual new nodes.
  let binding = licenseControlBindings.get(mount);
  if (!binding && mount.ownerDocument?.defaultView?.MutationObserver) {
    binding = { context, view };
    binding.observer = new mount.ownerDocument.defaultView.MutationObserver(records => {
      if (binding.view.disposed || binding.context.isCurrentProject?.() === false) return;
      const added = records.flatMap(record => [...record.addedNodes]).filter(node => node.nodeType === 1);
      if (added.length) applyLicenseControlState(binding.context, binding.view, mount, added);
    });
    binding.observer.observe(mount, { childList: true, subtree: true });
    licenseControlBindings.set(mount, binding);
  }
  if (binding) { binding.context = context; binding.view = view; }
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
  const resourcesReady = view.tubeDesignerUserDataLoaded
    && view.tubeDesignerToolLibrary?.catalogueStatus === "ready";
  const minimumVisibleMs = resourcesReady ? 0 : TUBE_DESIGNER_LOAD_PROGRESS_MINIMUM_VISIBLE_MS;
  const readsProductAndUserData = shouldRefresh && shouldRefreshUserData;
  view.tubeDesignerLoadProgress = {
    title: historyChanged ? "正在恢复产品历史" : resourcesReady ? "正在打开项目" : "正在准备资源模板",
    detail: resourcesReady ? "正在读取项目和产品场景" : readsProductAndUserData
      ? "正在准备产品、管型和单件工艺"
      : shouldRefresh
        ? "正在准备产品、管型和单件工艺"
        : "正在读取用户配置",
    stage: resourcesReady ? "读取项目" : readsProductAndUserData
      ? "准备模板载入"
      : shouldRefresh ? "准备模板载入" : "读取用户数据",
    mode: "Tube Designer",
    minimumVisibleMs,
  };
  view.tubeDesignerLoadProgressToken = token;
  return { token, startedAt: nowMilliseconds(), minimumVisibleMs };
}

function updateDesignerLoadProgress(view, operation, stage, detail, title = null) {
  if (!operation
      || view.tubeDesignerLoadProgressToken !== operation.token
      || !view.tubeDesignerLoadProgress) {
    return false;
  }
  view.tubeDesignerLoadProgress = {
    ...view.tubeDesignerLoadProgress,
    ...(title ? { title } : {}),
    stage,
    detail,
  };
  return true;
}

async function finishDesignerLoadProgress(view, operation) {
  if (!operation) return;
  await waitForMinimumDuration(
    operation.visibleAt ?? operation.startedAt,
    operation.minimumVisibleMs,
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
