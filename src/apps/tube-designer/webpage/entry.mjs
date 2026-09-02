import * as laserCam from "../../laser-3d-cam/webpage/entry.mjs";
import { handleDesignerAreaAction, handleDesignerRibbonCommand, refreshDesignerState } from "./designerActions.mjs";
import { renderDesignerLeftPane, renderDesignerRightPane, renderDesignerViewportOverlay } from "./designerViews.mjs";
import { getRibbonDefinition as getDesignerRibbonDefinition } from "./ribbonDefinition.mjs";
import { ensureTubeDesignerStyles } from "./styles/ensureStyles.mjs";
import { getProjectView } from "../../laser-3d-cam/webpage/state/projectViewStore.mjs";

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
  const synchronization = (async () => {
    try {
      await refreshDesignerState(designerContext, view);
      const designer = view.scene?.tubeDesigner ?? {};
      const generationRunId = String(designer.generationRun?.entityId ?? "").trim();
      const memberIds = (designer.members ?? [])
        .map((member) => String(member?.entityId ?? "").trim())
        .filter(Boolean);
      if (generationRunId && memberIds.length > 0) {
        const expectation = {
          correlationId: generationRunId,
          expectedEntityIds: memberIds,
        };
        if (historyChanged) expectation.afterRevision = previousViewRevision;
        const viewContent = await laserCam.synchronizeActiveAreaView(designerContext, expectation);
        view.viewport?.fitViewForRevision?.(viewContent.revision);
        view.tubeDesignerLastOperation = {
          kind: historyChanged ? "history" : "load",
          generationRunId,
          viewRevision: viewContent.revision,
          memberIds,
        };
      }
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
    renderViewportOverlay: renderDesignerViewportOverlay,
    handleAreaAction: handleDesignerAreaAction,
    handleAreaRibbonCommand: handleDesignerRibbonCommand,
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
