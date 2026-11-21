import { awaitOperationTask } from "../../../iCAX-UI/SDK/Cad/operationTask.mjs";
import { getWorkspaceState, getSelectedObject, leaveSketchContext } from "./workspaceState.mjs";
import { invokePlane, refreshPlaneState } from "./nativeOperations.mjs";
import { cancelDrawing } from "./drawingCommands.mjs";
import { clearSelectedDimensionAnnotations } from "./selectedDimensionAnnotations.mjs";
import { userErrorMessage } from "./errorMessages.mjs";

const owned = (context, view) => !view.disposed && view.sceneProxy === context.sceneProxy;
const current = (context, view) => owned(context, view) && context.isCurrentProject?.() !== false;

export async function waitGeometryTask(context, view, ops, response) {
  if (!response?.taskId) return response;
  const ui = getWorkspaceState(view), taskId = String(response.taskId);ui.geometryTaskId = taskId;ui.geometryTask = response;
  try {
    const task = await awaitOperationTask({
      read: () => context.sceneProxy.invoke("PlaneTask.Read", { taskId }, { timeoutMs: 30000 }),
      isCurrent: () => owned(context, view) && ui.geometryTaskId === taskId,
      onState(task) {
        ui.geometryTask = task;
        if (ui.geometryTaskReadError && ui.operationError === ui.geometryTaskReadError) ui.operationError = "";
        ui.geometryTaskReadError = "";
        if (current(context, view)) { ops?.renderProject(context, view);context.actions?.refreshProductRibbon?.(); }
      },
      onReadError(error) {
        ui.geometryTaskReadError = `暂时无法查询操作状态：${error?.message ?? String(error)}`;
        ui.operationError = ui.geometryTaskReadError;
        if (current(context, view)) ops?.renderProject(context, view);
      },
    });
    if (!task) return null;
    if (task.status !== "Completed") {
      const error = new Error(task.error || ({ Stale: "编辑输入已改变，请重试。", Cancelled: "操作已取消。" })[task.status] || "几何操作未完成。");
      ui.operationError = userErrorMessage(error);
      if (current(context, view)) ops?.appendProjectLog?.(context, "error", error.message);
      throw error;
    }
    return task;
  } finally {
    if (ui.geometryTaskId === taskId) { ui.geometryTaskId = "";ui.geometryTask = null; }
    if (current(context, view)) { ops?.renderProject(context, view);context.actions?.refreshProductRibbon?.(); }
  }
}

export async function openSketch(context, view, ops, ownerId) {
  const ui = getWorkspaceState(view), selected = getSelectedObject(context, view);
  ownerId ??= selected?.row.id;
  if (!ownerId) throw new Error("请先选择零件或板材。");
  if (ui.activeSketchId) throw new Error("请先完成或取消当前草图编辑。");
  const restore = { selectionKind: ui.selectionKind, selectedIdsByKind: structuredClone(ui.selectedIdsByKind), canvasObject: ui.canvasObject ? { ...ui.canvasObject } : null, panelMode: ui.panelMode, formDrafts: ui.formDrafts };
  cancelDrawing(view);clearSelectedDimensionAnnotations(view);
  await invokePlane(context, view, ops, "PlaneSketch.Open", { ownerId }, { afterCommand(response) {
    ui.sketchReturn = restore;ui.formDrafts = {};ui.activeSketchId = String(response.sketchId ?? response.id);ui.selectionKind = "sketch-curves";ui.selectedIdsByKind["sketch-curves"] = [];ui.panelMode = "object";
  } });
  if (current(context, view)) { view.viewport?.fitView();ui.commandStatus = "编辑CAD草图，完成后更新所属零件或板材。"; }
}

function leaveSketch(view) {
  cancelDrawing(view);clearSelectedDimensionAnnotations(view);leaveSketchContext(view);
}

export async function completeSketch(context, view, ops) {
  const ui = getWorkspaceState(view), id = ui.activeSketchId;if (!id) return;
  cancelDrawing(view);
  const response = await invokePlane(context, view, ops, "PlaneSketch.Complete", { id, async: true }, { refreshView: false, retainOwnedResponse: true });
  if (!await waitGeometryTask(context, view, ops, response) || !owned(context, view)) return;
  leaveSketch(view);
  if (current(context, view)) { await refreshPlaneState(context, view, ops, { refreshView: true });context.actions?.refreshNativeProjectHistory?.({ sceneProxy: context.sceneProxy, projectId: context.project?.projectId }); }
}

export async function cancelSketch(context, view, ops) {
  const ui = getWorkspaceState(view), id = ui.activeSketchId;if (!id) return;
  if (ui.geometryTaskId) { await context.sceneProxy.invoke("PlaneTask.Cancel", { taskId: ui.geometryTaskId });return; }
  cancelDrawing(view);await invokePlane(context, view, ops, "PlaneSketch.Cancel", { id }, { refreshView: false });
  if (!current(context, view)) return;leaveSketch(view);await refreshPlaneState(context, view, ops, { refreshView: true });
}
