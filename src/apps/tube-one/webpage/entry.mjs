import * as laserCam from "../../laser-3d-cam/webpage/entry.mjs";
import { handleTubeAreaAction, handleTubeRibbonCommand } from "./cadIntentActions.mjs";
import { renderTubeCADDialogPrefix, renderTubeCADDialogSuffix, renderTubeCSGViewportOverlay, renderTubeWorkpieceLeftPane, renderTubeWorkpieceRightPane } from "./cadIntentViews.mjs";
import { getRibbonDefinition as getTubeRibbonDefinition } from "./ribbon/ribbonDefinition.mjs";
import { ensureTubeOneStyles } from "./styles/ensureStyles.mjs";
import { getProjectView } from "../../laser-3d-cam/webpage/state/projectViewStore.mjs";

export function getRibbonDefinition() {
  return getTubeRibbonDefinition();
}

export function mountProduct(context) {
  ensureTubeOneStyles();
  laserCam.mountProduct(context);
}

export function mountProject(context) {
  ensureTubeOneStyles();
  laserCam.mountProject(withTubeTerminology(context));
}

export function handleRibbonCommand(context, commandId) {
  return laserCam.handleRibbonCommand(withTubeTerminology(context), commandId);
}

function withTubeTerminology(context) {
  return {
    ...context,
    areaTitleOverrides: {
      workpiece: "管件",
      machining: "编程",
      machine: "机床",
      view: "视图",
    },
    terminology: {
      workpiece: "管件",
    },
    areaRenderers: {
      ...(context.areaRenderers ?? {}),
      workpiece: {
        left: renderTubeWorkpieceLeftPane,
        right: renderTubeWorkpieceRightPane,
      },
    },
    renderViewportOverlay: renderTubeCSGViewportOverlay,
    resolveWorkbenchPresentation: (_context, view) =>
      view.tubeWorkspaceMode === "editor"
        ? {
            className: "tube-cad-editor-dialog",
            attributes: 'role="dialog" aria-modal="true" aria-labelledby="tube-cad-dialog-title"',
          }
        : { className: "tube-main-workspace" },
    renderWorkbenchPrefix: renderTubeCADDialogPrefix,
    renderWorkbenchSuffix: renderTubeCADDialogSuffix,
    resolveSceneProxy: (_context, view) =>
      view.tubeWorkspaceMode === "editor" && view.tubeEditorSceneProxy
        ? view.tubeEditorSceneProxy
        : context.sceneProxy,
    onProjectLog: (logContext, level, message) => {
      const view = getProjectView(logContext.project?.projectId ?? "");
      if (view.tubeWorkspaceMode !== "editor") return;
      view.tubeEditorLogs ??= [];
      view.tubeEditorLogs.push({
        level: String(level ?? "info"),
        message: String(message ?? ""),
        time: new Date().toLocaleTimeString("zh-CN", { hour12: false }),
      });
      if (view.tubeEditorLogs.length > 200) {
        view.tubeEditorLogs.splice(0, view.tubeEditorLogs.length - 200);
      }
    },
    resolveViewportBackgroundColor: (_context, view, areaId) =>
      areaId === "workpiece" && view.tubeWorkspaceMode === "editor"
        ? 0x9db7d0
        : 0x101518,
    handleAreaAction: handleTubeAreaAction,
    handleAreaRibbonCommand: handleTubeRibbonCommand,
  };
}
