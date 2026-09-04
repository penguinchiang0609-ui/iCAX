import * as laserCam from "../../laser-3d-cam/webpage/entry.mjs";
import {
  fitDesignerDefaultView,
  getDesignerRenderSignature,
  handleDesignerAreaAction,
  handleDesignerRibbonCommand,
  refreshDesignerState,
} from "./designerActions.mjs";
import {
  renderDesignerLeftPane,
  renderDesignerOperationOverlay,
  renderDesignerRightPane,
  renderDesignerViewportOverlay,
} from "./designerViews.mjs";
import { getRibbonDefinition as getDesignerRibbonDefinition } from "./ribbonDefinition.mjs";
import { ensureTubeDesignerStyles } from "./styles/ensureStyles.mjs";
import { getProjectView } from "../../laser-3d-cam/webpage/state/projectViewStore.mjs";
import { typedVariant } from "../../../iCAX-UI/SDK/SDO/variantSerializer.mjs";

export function getRibbonDefinition() {
  return getDesignerRibbonDefinition();
}

export function mountProduct(context) {
  ensureTubeDesignerStyles();
  laserCam.mountProduct(context);
}

export async function mountProject(context) {
  ensureTubeDesignerStyles();
  const view = getProjectView(context.project?.projectId ?? "");
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
  if (shouldRefresh) {
    view.tubeDesignerLoading = true;
  }
  const initialMount = laserCam.mountProject(designerContext);
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
      await refreshDesignerState(designerContext, view);
      const designer = view.scene?.tubeDesigner ?? {};
      const generationRunId = String(designer.generationRun?.entityId ?? "").trim();
      const memberIds = (designer.members ?? [])
        .map((member) => String(member?.entityId ?? "").trim())
        .filter(Boolean);
      const renderChanged = previousRenderSignature !== getDesignerRenderSignature(designer);
      const activeProductChanged = String(previousDesigner.activeProductId ?? "")
        !== String(designer.activeProductId ?? "");
      const requiresViewSynchronization = !historyChanged
        ? memberIds.length > 0
        : renderChanged;
      if (requiresViewSynchronization) {
        const expectation = {
          correlationId: generationRunId,
          expectedEntityIds: memberIds,
        };
        if (historyChanged && !activeProductChanged) {
          expectation.afterRevision = previousViewRevision;
          expectation.excludedEntityIds = previousMemberIds.filter((id) => !memberIds.includes(id));
        }
        const viewContent = await laserCam.synchronizeActiveAreaView(designerContext, expectation);
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
      view.tubeDesignerLoading = false;
      view.tubeDesignerLoaded = true;
      laserCam.mountProject(designerContext);
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
  return laserCam.handleRibbonCommand(withDesignerContext(context), commandId);
}

export function getWindowCloseGuard(context) {
  const view = getProjectView(context.project?.projectId ?? "");
  const operation = view.tubeDesignerExportOperation ?? view.tubeDesignerOperation;
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
    areaTitleOverrides: { view: "产品设计" },
    areaRenderers: {
      view: {
        left: renderDesignerLeftPane,
        right: renderDesignerRightPane,
      },
    },
    resolveWorkbenchPresentation: () => ({ className: "tube-designer-workspace" }),
    resolveViewportBackgroundColor: () => 0x13252d,
    resolveAreaViewDefinition: resolveDesignerAreaViewDefinition,
    renderViewportOverlay: renderDesignerViewportOverlay,
    renderWorkbenchSuffix: renderDesignerOperationOverlay,
    handleAreaAction: handleDesignerAreaAction,
    handleAreaRibbonCommand: handleDesignerRibbonCommand,
  };
}

function resolveDesignerAreaViewDefinition(_context, view, areaId, fallback) {
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
      projection: laserCam.RENDER_ENTITY_VIEW_PROJECTION,
    }],
  };
}

function getHistoryToken(scene) {
  const undoRedo = scene?.undoRedo ?? {};
  const stepKey = (step) => `${String(step?.id ?? "")}:${String(step?.name ?? "")}`;
  return JSON.stringify({
    undo: (undoRedo.undoSteps ?? []).map(stepKey),
    redo: (undoRedo.redoSteps ?? []).map(stepKey),
  });
}
