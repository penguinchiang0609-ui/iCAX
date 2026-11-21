import { draftKey, getAreaRows, getCanvasObject, getObjectRows, getSelectedObject, getWorkspaceState, readSnapshot, selectObject, geometryMethod, geometryObjectKind, restoreSketchContext } from "./workspaceState.mjs";
import { userErrorMessage } from "./errorMessages.mjs";

export const COMMAND_METHODS = Object.freeze({
  "plane.import-dxf": "PlaneDrawing.Import", "plane.import-parts": "PlanePart.Import",
  "plane.import-stocks": "PlaneStock.Import", "plane.stock-rect": "PlaneStock.CreateRectangle",
  "plane.copy-part": "PlaneObject.Duplicate", "plane.delete-part": "PlaneObject.Remove",
  "plane.nesting-run": "PlaneNesting.Run", "plane.nesting-settings": "PlaneNesting.SetParameters",
  "plane.layout-report": "PlaneNesting.ReadReport", "plane.layout-properties": "PlaneNesting.ReadReport",
  "plane.layout-export": "PlaneLayout.Export", "plane.layers": "PlaneLayer.Update",
  "plane.sketch-open": "PlaneSketch.Open", "plane.sketch-complete": "PlaneSketch.Complete", "plane.sketch-cancel": "PlaneSketch.Cancel",
});

export function supports(context, view, method) {
  return (readSnapshot(context, view).capabilities?.enabledMethods ?? []).includes(method);
}

export function synchronizeAvailability(context, view) {
  const ui = getWorkspaceState(view), capability = readSnapshot(context, view).capabilities ?? {};
  const availability = Object.fromEntries(Object.entries(COMMAND_METHODS).map(([id, method]) => [id, {
    disabled: ui.busy || Boolean(ui.geometryTaskId) || !supports(context, view, method),
    unavailableReason: ui.busy || ui.geometryTaskId ? "当前操作正在完成" : "当前原生模块尚未提供此功能",
  }]));
  const selected = getSelectedObject(context, view);
  for (const id of ["plane.sketch-complete", "plane.sketch-cancel"]) if (availability[id]) availability[id].disabled ||= !ui.activeSketchId || Boolean(ui.geometryTaskId);
  if (availability["plane.sketch-open"]) availability["plane.sketch-open"].disabled ||= Boolean(ui.activeSketchId) || !["parts", "stocks"].includes(selected?.kind);
  ui.canManageLayers = supports(context, view, "PlaneLayer.Update");
  ui.geometryCapabilities = { primitives: capability.drawingPrimitives ?? [], edits: capability.drawingEdits ?? [], segmentTypes: Array.isArray(capability.polylineSegmentTypes) ? capability.polylineSegmentTypes : [], primitiveOptions: capability.drawingPrimitiveOptions ?? {} };
  for (const command of ["plane.copy-part", "plane.delete-part"]) if (availability[command] && selected?.kind !== "parts") {
    availability[command].disabled = true;availability[command].unavailableReason = "请先选择零件";
  }
  if (ui.runningTaskId) { availability["plane.nesting-run"].disabled = true;availability["plane.nesting-run"].unavailableReason = "本次排样尚未结束"; }
  for (const command of ["plane.layout-report", "plane.layout-properties"]) if (!resultId(context, view)) { availability[command].disabled = true;availability[command].unavailableReason = "请先选择排样结果"; }
  for (const primitive of ["line", "polyline", "rect", "circle", "arc", "ellipse", "ellipseArc", "bezier", "bspline", "triangle", "polygon", "star"]) {
    availability[`plane.draw-${primitive}`] = { disabled: ui.busy || Boolean(ui.geometryTaskId) || !supports(context, view, geometryMethod(view, "Create")) || !(capability.drawingPrimitives ?? []).includes(primitive), unavailableReason: "当前原生模块尚未提供此绘制类型" };
  }
  if (!ui.geometryCapabilities.segmentTypes.length) {
    availability["plane.draw-polyline"].disabled = true;
    availability["plane.draw-polyline"].unavailableReason = "原生模块未声明多义线曲段能力。";
  }
  for (const operation of ["move", "copy", "rotate", "mirror", "scale", "trim", "extend", "offset", "fillet", "chamfer", "join", "break", "close", "delete", "nodes", "array-rectangular", "array-polar"]) {
    availability[`plane.edit-${operation}`] = { disabled: ui.busy || Boolean(ui.geometryTaskId) || !supports(context, view, geometryMethod(view, "Edit")) || !(capability.drawingEdits ?? []).includes(operation), unavailableReason: "当前原生模块尚未提供此编辑类型" };
    if (selected?.kind === geometryObjectKind(view) && Array.isArray(selected.row.drawingEdits) && !selected.row.drawingEdits.includes(operation)) {
      availability[`plane.edit-${operation}`].disabled = true;
      availability[`plane.edit-${operation}`].unavailableReason = "当前对象不支持此编辑。";
    }
  }
  if (availability["plane.edit-close"]) availability["plane.edit-close"].disabled ||= selected?.kind !== geometryObjectKind(view) || selected.row.canClosePolyline !== true;
  if (availability["plane.edit-delete"]) availability["plane.edit-delete"].disabled ||= selected?.kind !== geometryObjectKind(view);
  ui.commandAvailability = availability;
}

function live(context, view) { return view.iplaneLive?.context?.sceneProxy === context.sceneProxy ? view.iplaneLive : { context, ops: view.iplaneLive?.ops }; }
function isOwned(context, view) { return !view.disposed && view.sceneProxy === context.sceneProxy; }
function isCurrent(context, view) { return isOwned(context, view) && context.isCurrentProject?.() !== false; }
function paint(context, view, ops, ribbon = false) {
  if (!isCurrent(context, view)) return;
  const current = live(context, view);context = current.context;ops = current.ops ?? ops;
  synchronizeAvailability(context, view);ops?.renderProject(context, view);
  if (ribbon) context.actions?.refreshProductRibbon?.();
}

export async function synchronizePlaneView(context, view, ops, { excludedEntityIds = [] } = {}) {
  if (!context.sceneProxy?.views?.start || !ops?.refreshActiveAreaView) return null;
  const rows = getAreaRows(context, view), nativeProjection = Boolean(readSnapshot(context, view).capabilities?.enabledMethods?.length || rows.some(row => row.previewRef?.url));
  const expectation = nativeProjection ? { expectedEntityIds: [...new Set(rows.flatMap(row => row.entityIds))], excludedEntityIds } : {};
  await ops.refreshActiveAreaView(context, view, expectation);
  const references = rows.filter(row => row.previewRef?.url).flatMap(row => row.entityIds.map(id => [id, row.previewRef, row.materialRef]));
  const reader = view.areas?.[view.activeAreaId]?.viewReader;
  if (!references.length || !reader?.waitForSnapshot) return;
  const snapshot = await reader.waitForSnapshot(candidate => {
    const candidates = new Map((candidate.rows ?? []).map(row => [String(row.entityId), row.data]));
    const sameReference = (actual, expected) => String(actual?.url ?? "") === String(expected.url) && String(actual?.version ?? "") === String(expected.version);
    return references.every(([id, expected, expectedMaterial]) => {
      const actual = candidates.get(id);
      return sameReference(actual?.geometry, expected) && (!expectedMaterial?.url || sameReference(actual?.material, expectedMaterial));
    });
  });
  if (!isCurrent(context, view)) return null;
  return ops.refreshActiveAreaView(live(context, view).context, view, { ...expectation, expectedRevision: String(snapshot.revision) });
}

export async function refreshPlaneState(context, view, ops, { render = true, refreshView = false, viewArea, previousEntityIds = [] } = {}) {
  const proxy = context.sceneProxy, request = view.iplaneStateRequest = (view.iplaneStateRequest ?? 0) + 1;
  const payload = await proxy.invoke("PlaneScene.ReadState", {}, { timeoutMs: 30000 });
  if (request !== view.iplaneStateRequest || !isCurrent(context, view)) return null;
  if (!payload?.iplaneCam || typeof payload.iplaneCam !== "object") throw new Error("原生模块未返回有效的平面工程数据。");
  view.scene = { ...view.scene, ...payload };
  view.iplaneRevision = String(payload.revision ?? "");
  const sketchTransition = restoreSketchContext(context, view);
  if (sketchTransition) context.onPlaneSketchContextChanged?.(view, sketchTransition);
  synchronizeAvailability(context, view);
  if (refreshView && proxy.views?.start) {
    const expectedEntityIds = getAreaRows(live(context, view).context, view).flatMap(row => row.entityIds), retained = new Set(expectedEntityIds);
    await synchronizePlaneView(live(context, view).context, view, ops, { excludedEntityIds: viewArea === view.activeAreaId ? previousEntityIds.filter(id => !retained.has(id)) : [] });
  }
  if (render) paint(context, view, ops, true);
  return payload;
}

export async function invokePlane(context, view, ops, method, payload = {}, options = {}) {
  const ui = getWorkspaceState(view);
  if (ui.busy) throw new Error("请等待当前操作完成。");
  if (ui.geometryTaskId) throw new Error("请等待几何操作完成，或先取消当前操作。");
  if (!supports(context, view, method)) throw new Error("当前原生模块尚未提供此功能。");
  const proxy = context.sceneProxy;
  const renderMutation = new Set(["PlaneDrawing.Create", "PlaneDrawing.Import", "PlaneDrawing.Edit", "PlanePart.Import", "PlaneStock.Import", "PlaneStock.CreateRectangle", "PlaneObject.Remove", "PlaneObject.Duplicate", "PlaneSketch.Open", "PlaneSketch.Create", "PlaneSketch.Edit", "PlaneSketch.Remove", "PlaneSketch.Complete", "PlaneSketch.Cancel", "PlaneLayer.Assign", "PlaneLayer.Update", "PlaneLayer.Remove"]).has(method);
  const viewArea = view.activeAreaId;
  // Entering a sketch changes the visible business context while its owner
  // remains in the native View. Sketch curve deletion is a reversible tombstone.
  // Only physical disposal may require old render entities to leave that View.
  const disposesEntities = method === "PlaneObject.Remove" || method === "PlaneDrawing.Edit" && payload.operation === "delete" || method === "PlaneSketch.Cancel";
  const previousEntityIds = disposesEntities ? getAreaRows(context, view).flatMap(row => row.entityIds) : [];
  let resolveFinished;
  const finished = new Promise(resolve => { resolveFinished = resolve; });
  view.iplaneOperationDone = finished;
  ui.busy = true;ui.operationError = "";paint(context, view, ops, true);
  try {
    const response = await proxy.invoke(method, { ...payload, ...(view.iplaneRevision ? { expectedRevision: view.iplaneRevision } : {}) }, { timeoutMs: options.timeoutMs ?? 120000 });
    if (!isCurrent(context, view)) return options.retainOwnedResponse && isOwned(context, view) ? response : null;
    // Commands return identifiers. Only the query below supplies project data.
    if (options.afterCommand) options.afterCommand(response);
    if (options.refresh !== false) await refreshPlaneState(context, view, ops, { render: false, refreshView: options.refreshView ?? renderMutation, viewArea, previousEntityIds });
    if (!isCurrent(context, view)) return options.retainOwnedResponse && isOwned(context, view) ? response : null;
    if (options.mutation !== false) await live(context, view).context.actions?.refreshNativeProjectHistory?.({ sceneProxy: proxy, projectId: context.project?.projectId });
    return response;
  } catch (error) {
    if (isCurrent(context, view)) {
      if (options.isErrorCurrent?.() !== false) ui.operationError = userErrorMessage(error);
      ops?.appendProjectLog?.(live(context, view).context, "error", error?.message ?? String(error));
    }
    throw error;
  } finally {
    resolveFinished();if (view.iplaneOperationDone === finished) view.iplaneOperationDone = null;
    ui.busy = false;paint(context, view, ops, true);
  }
}

function importedName(path) { return String(path).split(/[\\/]/).at(-1)?.replace(/\.dxf$/i, "") || "导入图形"; }
export async function importDxf(context, view, ops, kind) {
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
  if (typeof bridge?.openFileDialog !== "function") throw new Error("当前宿主未提供文件选择能力。");
  const selected = await bridge.openFileDialog({ title: { drawing: "导入底图 DXF", parts: "导入零件 DXF", stocks: "导入板材 DXF" }[kind], filters: [{ name: "DXF", extensions: ["dxf"] }], multiple: true });
  if (!selected || !isCurrent(context, view)) return;
  const paths = Array.isArray(selected) ? selected : [selected];
  await importDxfPaths(context, view, ops, kind, paths);
}

async function importDxfPaths(context, view, ops, kind, paths, units) {
  const ui = getWorkspaceState(view);let panelEpoch = ui.panelEpoch ?? 0;
  for (const [index, sourcePath] of paths.entries()) {
    if (!isCurrent(context, view)) return;
    if (!/\.dxf$/i.test(String(sourcePath))) throw new Error("请选择 DXF 文件。");
    const method = { drawing: "PlaneDrawing.Import", parts: "PlanePart.Import", stocks: "PlaneStock.Import" }[kind];
    let response;
    try {
      response = await invokePlane(context, view, ops, method, { sourcePath, name: importedName(sourcePath), ...(index === 0 && units ? { units } : {}), ...(kind === "drawing" ? {} : { quantity: 1 }) });
    } catch (error) {
      if (String(error?.message ?? error).includes("PLANE_DXF_UNITS_REQUIRED") && isCurrent(context, view)) {
        ui.operationError = "";ui.pendingImport = { kind, sourcePath, paths: paths.slice(index) };
        delete ui.formDrafts["import-units"];
        if ((ui.panelEpoch ?? 0) === panelEpoch) { ui.panelMode = "import-units";ui.panelEpoch = (ui.panelEpoch ?? 0) + 1; }
        paint(context, view, ops);return;
      }
      throw error;
    }
    if (response?.id && (ui.panelEpoch ?? 0) === panelEpoch) { selectObject(view, kind, String(response.id));panelEpoch = ui.panelEpoch; }
  }
  ui.pendingImport = null;delete ui.formDrafts["import-units"];
  paint(context, view, ops);
  if (isCurrent(context, view) && (ui.panelEpoch ?? 0) === panelEpoch) view.viewport?.fitView();
}

export async function importWithUnits(context, view, ops) {
  const ui = getWorkspaceState(view), pending = ui.pendingImport;
  if (!pending) throw new Error("没有等待确认单位的导入文件。");
  const units = ui.formDrafts["import-units"]?.units;
  if (!["mm", "cm", "m", "in"].includes(units)) throw new Error("请选择原 DXF 文件使用的单位。");
  await importDxfPaths(context, view, ops, pending.kind, pending.paths, units);
}

export async function saveObject(context, view, ops) {
  const selected = getSelectedObject(context, view);if (!selected) return;
  const key = draftKey(selected.kind, selected.row), draft = getWorkspaceState(view).drafts[key];
  if (!draft || !Object.keys(draft).length) return;
  const values = { ...draft };
  if (Object.hasOwn(values, "quantity")) {
    values.quantity = Number(values.quantity);
    if (!Number.isSafeInteger(values.quantity) || values.quantity < 1) throw new Error("数量必须为正整数。");
  }
  const submitted = JSON.stringify(draft);
  await invokePlane(context, view, ops, "PlaneObject.Update", { id: selected.row.id, kind: selected.kind, ...values }, {
    afterCommand() { if (JSON.stringify(getWorkspaceState(view).drafts[key]) === submitted) delete getWorkspaceState(view).drafts[key]; },
  });
}

export function resultId(context, view) {
  const selected = getSelectedObject(context, view), canvas = getCanvasObject(context, view);
  const row = selected?.kind === "results" ? selected.row : canvas?.kind === "results" ? canvas.row : null;
  return row ? row.ancestorIds?.[0] ?? row.id : "";
}

export async function readReport(context, view, ops) {
  const ui = getWorkspaceState(view), panelEpoch = ui.panelEpoch ?? 0;
  const id = resultId(context, view);if (!id) throw new Error("请先选择排样结果。");
  const response = await invokePlane(context, view, ops, "PlaneNesting.ReadReport", { resultId: id }, { refresh: false, mutation: false });
  if (!response) return;
  if ((ui.panelEpoch ?? 0) !== panelEpoch || !isCurrent(context, view)) return;
  ui.report = response;ui.panelMode = "report";ui.panelEpoch = (ui.panelEpoch ?? 0) + 1;paint(context, view, ops);return response;
}

export async function startNesting(context, view, ops) {
  const response = await invokePlane(context, view, ops, "PlaneNesting.Run", {}, { refreshView: false });
  if (!response?.taskId) throw new Error("排样未返回任务编号。");
  getWorkspaceState(view).commandStatus = "正在排样…";
  watchTask(context, view, ops, String(response.taskId));
}

export function watchTask(context, view, ops, taskId) {
  const ui = getWorkspaceState(view);clearTimeout(view.iplaneTaskTimer);ui.runningTaskId = taskId;
  const generation = ui.taskWatchEpoch = (ui.taskWatchEpoch ?? 0) + 1, panelEpoch = ui.panelEpoch ?? 0;
  const poll = async () => {
    if (!isOwned(context, view) || ui.runningTaskId !== taskId || ui.taskWatchEpoch !== generation) return;
    try {
      const response = await context.sceneProxy.invoke("PlaneNesting.ReadTask", { taskId }, { timeoutMs: 30000 });
      if (!isOwned(context, view) || ui.runningTaskId !== taskId || ui.taskWatchEpoch !== generation) return;
      if (ui.taskReadError && ui.operationError === ui.taskReadError) ui.operationError = "";
      ui.taskReadError = "";
      ui.task = response.task ?? response;
      const status = String(ui.task.status ?? "");
      if (["Running", "Pending", "Queued", "CancelRequested"].includes(status)) {
        paint(context, view, ops);view.iplaneTaskTimer = setTimeout(poll, 400);return;
      }
      if (!isCurrent(context, view)) { ui.runningTaskId = "";return; }
      await refreshPlaneState(context, view, ops, { refreshView: true });
      if (!isCurrent(context, view) || ui.taskWatchEpoch !== generation) return;
      ui.runningTaskId = "";
      const openResult = ui.task.resultId && (ui.panelEpoch ?? 0) === panelEpoch;
      if (openResult) selectObject(view, "results", String(ui.task.resultId));
      if (["Failed", "Error", "Interrupted"].includes(status)) {
        const diagnostic = String(ui.task.message ?? ui.task.error ?? "排样未完成。");ui.operationError = userErrorMessage(diagnostic);ops?.appendProjectLog?.(live(context, view).context, "error", diagnostic);
      }
      else if (view.activeAreaId === "nesting" && status === "Stale") ui.commandStatus = String(ui.task.error || "排样输入已修改，本次结果未提交。");
      else if (view.activeAreaId === "nesting" && status === "Cancelled") ui.commandStatus = "本次排样已取消。";
      else if (view.activeAreaId === "nesting" && status === "Completed") ui.commandStatus = "排样已完成。";
      else if (view.activeAreaId === "nesting" && status === "Partial") ui.commandStatus = "排样已结束，部分零件尚未排入。";
      paint(context, view, ops, true);if (openResult) view.viewport?.fitView();
    } catch (error) {
      if (isOwned(context, view) && ui.taskWatchEpoch === generation) {
        ui.taskReadError = userErrorMessage(error);ui.operationError = ui.taskReadError;ops?.appendProjectLog?.(live(context, view).context, "error", error?.message ?? String(error));paint(context, view, ops);
        // A failed status query says nothing about the worker's lifetime.
        // Keep its identity and close guard until an authoritative terminal reply.
        view.iplaneTaskTimer = setTimeout(poll, 1200);
      }
    }
  };
  view.iplaneTaskTimer = setTimeout(poll, 0);
}

export function reportCsv(report) {
  const escape = value => `"${String(value ?? "").replaceAll('"', '""')}"`;
  report = report?.report ?? report ?? {};
  const data = report.metrics ?? report, polygonScope = (data.validationScope ?? report.validationScope) === "PolygonMaterial";
  const statuses = { Completed: "已完成", Partial: "部分排入", Failed: "失败", Cancelled: "已取消", Stale: "输入已变更" }, status = statuses[report.status] ?? report.status;
  const rows = [["排样报告", report.name ?? ""], ["结果状态", status], ["输入状态", report.stale ? "输入已修改，保留先前结果" : "报告对应的输入快照"], ["需求数量", data.required], ["已排数量", data.placed], ["未排数量", data.unplacedCount ?? report.unplaced?.length], ["用板数", data.boardCount],
    [polygonScope ? "零件计算轮廓净面积（mm²）" : "零件净面积（mm²）", data.placedMaterialArea], [polygonScope ? "板材计算轮廓净面积（mm²）" : "板材净面积（mm²）", data.stockNetArea], [polygonScope ? "计算轮廓净面积利用率" : "净面积利用率", Number.isFinite(data.utilization) ? `${(data.utilization * 100).toFixed(2)}%` : data.utilization],
    ["几何检查", data.valid === true ? "通过" : data.valid === false ? "未通过" : ""], ["检查范围", polygonScope ? "计算轮廓（含内孔）；原始曲线保留" : data.validationScope], ["停止原因", statuses[data.stopReason] ?? data.stopReason], ["计算耗时（ms）", data.elapsedMs]];
  if (polygonScope && report.approximationBoundMm != null) rows.push(["曲线近似误差上限（mm）", report.approximationBoundMm], ["计算零件间距（含近似安全余量，mm）", report.calculationPartGap], ["计算板边距（含近似安全余量，mm）", report.calculationStockMargin]);
  rows.push([], ["结果板材"], ["板材", "物理库存序号", "已排数量", "计算轮廓净面积利用率"]);
  for (const board of report.boards ?? []) rows.push([board.name, board.physicalIndex, board.placed, Number.isFinite(board.utilization) ? `${(board.utilization * 100).toFixed(2)}%` : board.utilization]);
  rows.push([], ["排入零件"], ["板材", "来源零件", "需求实例序号", "X（mm）", "Y（mm）", "角度（°）", "镜像"]);
  for (const board of report.boards ?? []) for (const placement of board.placements ?? []) rows.push([board.name, placement.partName ?? placement.partId, placement.instanceIndex, placement.x, placement.y, placement.angleDeg, placement.flip ? "是" : "否"]);
  rows.push([], ["未排零件"], ["来源零件", "需求实例序号", "原因"]);
  for (const entry of report.unplaced ?? []) rows.push([entry.partName ?? entry.partId, entry.instanceIndex, entry.reason]);
  return "\ufeff" + rows.map(row => row.map(escape).join(",")).join("\r\n");
}

export function downloadReport(view) {
  const report = getWorkspaceState(view).report;if (!report) return;
  const url = URL.createObjectURL(new Blob([reportCsv(report)], { type: "text/csv;charset=utf-8" }));
  const link = document.createElement("a");link.href = url;link.download = "排样报告.csv";document.body.append(link);link.click();link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 0);
}
