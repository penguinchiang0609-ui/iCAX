import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";
import { getObjectRows, getWorkspaceState, selectObject } from "./workspaceState.mjs";
import { invokePlane, refreshPlaneState, supports } from "./nativeOperations.mjs";
import { waitGeometryTask } from "./sketchOperations.mjs";

const current = (context, view) => !view.disposed && view.sceneProxy === context.sceneProxy && context.isCurrentProject?.() !== false;
const modePrefix = "artwork:";
const textDefaults = Object.freeze({ text: "文字", fontFamily: "Microsoft YaHei", fontSizeMm: 10, x: 0, y: 0, angleDeg: 0 });
const traceDefaults = Object.freeze({ threshold: 128, invert: false, minAreaMm2: 0, simplifyMm: .1 });

function objectRow(context, view, id) { return getObjectRows(context, view, "drawing").find(row => row.id === id); }
function selectableResult(context, view, ids) {
  const ui = getWorkspaceState(view);
  if (!current(context, view) || view.activeAreaId !== "drawing" || ui.activeSketchId || ui.activeTool !== "plane.select") return "";
  return ids.find(id => objectRow(context, view, id)?.referenceOnly === false) ?? "";
}
function modeKey(kind, id = "") { return `${modePrefix}${kind}${id ? `:${id}` : ""}`; }
function openPanel(view, kind, id = "") {
  const ui = getWorkspaceState(view);ui.panelMode = modeKey(kind, id);ui.artworkTargetId = id;
  ui.panelEpoch = (ui.panelEpoch ?? 0) + 1;ui.operationError = "";
}
function numeric(value, label, minimum = -Infinity, maximum = Infinity) {
  const result = typeof value === "number" ? value : String(value).trim() ? Number(value) : NaN;
  if (!Number.isFinite(result) || result < minimum || result > maximum) throw new Error(`${label}必须为${minimum > 0 ? "正" : "有效"}数值。`);
  return result;
}
function textParameters(source) {
  const text = String(source.text ?? ""), fontFamily = String(source.fontFamily ?? "").trim();
  if (!text.trim()) throw new Error("请输入文字内容。");
  if (!fontFamily) throw new Error("请输入字体名称。");
  return { text, fontFamily, fontSizeMm: numeric(source.fontSizeMm, "字高", Number.MIN_VALUE),
    x: numeric(source.x, "X坐标"), y: numeric(source.y, "Y坐标"), angleDeg: numeric(source.angleDeg, "角度") };
}
function imageParameters(source) {
  return { widthMm: numeric(source.widthMm ?? source.width, "图片宽度", Number.MIN_VALUE),
    heightMm: numeric(source.heightMm ?? source.height, "图片高度", Number.MIN_VALUE),
    x: numeric(source.x, "X坐标"), y: numeric(source.y, "Y坐标"), angleDeg: numeric(source.angleDeg, "角度") };
}
function completedIds(response) {
  return [...new Set([response?.resultId, ...(Array.isArray(response?.resultIds) ? response.resultIds : [])].filter(Boolean).map(String))];
}

async function runArtworkTask(context, view, ops, method, payload) {
  const response = await invokePlane(context, view, ops, method, payload, { refreshView: false, retainOwnedResponse: true });
  const completed = await waitGeometryTask(context, view, ops, response);
  if (!completed || !current(context, view)) return null;
  await refreshPlaneState(context, view, ops, { refreshView: true });
  await context.actions?.refreshNativeProjectHistory?.({ sceneProxy: context.sceneProxy, projectId: context.project?.projectId });
  return completed;
}

export async function openArtworkTool(context, view, ops, commandId) {
  const ui = getWorkspaceState(view);
  if (ui.activeSketchId) throw new Error("文字和参考图片请放到底图；CAD草图只编辑轮廓。");
  if (commandId === "plane.text") {
    if (!supports(context, view, "PlaneText.Create")) throw new Error("原生模块未提供文字对象。");
    ui.activeTool = "plane.text";openPanel(view, "text-create");ui.commandStatus = "点击画布定位文字，再确认内容并添加。";return true;
  }
  if (commandId !== "plane.image") return false;
  if (!supports(context, view, "PlaneImage.Import")) throw new Error("原生模块未提供参考图片。");
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge;
  if (typeof bridge?.openFileDialog !== "function") throw new Error("当前宿主未提供文件选择能力。");
  const sourcePath = await bridge.openFileDialog({ title: "导入参考图片", filters: [{ name: "图片", extensions: ["png", "jpg", "jpeg", "bmp", "tif", "tiff"] }], multiple: false });
  if (!sourcePath || !current(context, view)) return true;
  if (Array.isArray(sourcePath)) throw new Error("请一次选择一张参考图片。");
  const panelEpoch = ui.panelEpoch ?? 0, drawingEpoch = ui.drawingEpoch;
  const completed = await runArtworkTask(context, view, ops, "PlaneImage.Import", { sourcePath: String(sourcePath) });
  if (!completed || !current(context, view) || (ui.panelEpoch ?? 0) !== panelEpoch || ui.drawingEpoch !== drawingEpoch) return true;
  const id = completedIds(completed)[0];if (!id) throw new Error("图片导入完成但未返回对象标识。");
  ui.activeTool = "plane.select";
  if (selectableResult(context, view, [id])) {
    ui.fitCanvasKey = "drawing:worktable";selectObject(view, "drawing", id);openPanel(view, "image-edit", id);
  }
  ops?.appendProjectLog?.(context, "info", "已导入参考图片。");return true;
}

export function placeArtworkText(view, point) {
  const ui = getWorkspaceState(view);
  if (ui.activeTool !== "plane.text" || ui.panelMode !== modeKey("text-create") || ui.busy || ui.geometryTaskId || !point?.every(Number.isFinite)) return false;
  const draft = ui.formDrafts[ui.panelMode] ??= {};
  draft.x = point[0];draft.y = point[1];ui.commandStatus = "已定位文字，确认内容后点击添加文字。";return true;
}

function input(view, mode, name, label, fallback, { type = "text", min, max, step = "any", multiline = false } = {}) {
  const value = getWorkspaceState(view).formDrafts[mode]?.[name] ?? fallback;
  const attributes = `data-iplane-key="artwork-input:${escapeAttr(mode)}:${name}" data-iplane-form="${escapeAttr(mode)}" data-iplane-form-field="${name}"`;
  return `<label class="iplane-field" data-iplane-key="artwork-field:${escapeAttr(mode)}:${name}"><span>${label}</span>${multiline
    ? `<textarea rows="4" ${attributes}>${escapeText(String(value ?? ""))}</textarea>`
    : `<input type="${type}" ${attributes} value="${escapeAttr(String(value ?? ""))}" ${min != null ? `min="${min}"` : ""} ${max != null ? `max="${max}"` : ""} ${type === "number" ? `step="${step}"` : ""} autocomplete="off">`}</label>`;
}
function poseInputs(view, mode, row) {
  return input(view, mode, "x", "X（mm）", row.x ?? 0, { type: "number" }) + input(view, mode, "y", "Y（mm）", row.y ?? 0, { type: "number" })
    + input(view, mode, "angleDeg", "角度（°）", row.angleDeg ?? 0, { type: "number" });
}

export function renderArtworkOperationPanel(context, view) {
  const ui = getWorkspaceState(view), mode = ui.panelMode;
  if (!mode.startsWith(modePrefix)) return "";
  const kind = mode.slice(modePrefix.length).split(":")[0], id = ui.artworkTargetId, row = id ? objectRow(context, view, id) : null;
  const busy = ui.busy || ui.geometryTaskId, unavailable = busy || row?.locked === true;
  let title, fields = "", action, button, note = "";
  if (kind === "text-create" || kind === "text-edit") {
    if (kind === "text-edit" && row?.contentType !== "text") return "";
    const values = { ...textDefaults, ...row };
    title = kind === "text-create" ? "添加文字" : "编辑文字";
    if (kind === "text-create") note = "可在画布点击定位，修改文字后点击添加。";
    fields = input(view, mode, "text", "文字内容", values.text, { multiline: true }) + input(view, mode, "fontFamily", "字体", values.fontFamily)
      + input(view, mode, "fontSizeMm", "字高（mm）", values.fontSizeMm, { type: "number", min: .001 }) + poseInputs(view, mode, values);
    action = kind === "text-create" ? "artwork-create-text" : "artwork-update-text";button = kind === "text-create" ? "添加文字" : "应用文字";
  } else if (kind === "image-edit") {
    if (row?.contentType !== "image") return "";
    title = "参考图片";note = "图片保留原始内容，可调整位置、尺寸和角度。";
    fields = input(view, mode, "widthMm", "宽度（mm）", row.width, { type: "number", min: .001 })
      + input(view, mode, "heightMm", "高度（mm）", row.height, { type: "number", min: .001 }) + poseInputs(view, mode, row);
    action = "artwork-update-image";button = "应用图片参数";
  } else if (kind === "image-trace") {
    if (row?.contentType !== "image") return "";
    title = "图片转为加工曲线";note = "确认参数后生成加工曲线，参考原图保留。";
    fields = input(view, mode, "threshold", "黑白阈值（0–255）", traceDefaults.threshold, { type: "number", min: 0, max: 255, step: 1 })
      + `<label class="iplane-checkbox" data-iplane-key="artwork-trace-invert"><input type="checkbox" data-iplane-key="artwork-trace-invert-input" data-iplane-form="${escapeAttr(mode)}" data-iplane-form-field="invert" ${ui.formDrafts[mode]?.invert ?? traceDefaults.invert ? "checked" : ""}>反色</label>`
      + input(view, mode, "minAreaMm2", "过滤小轮廓（mm²）", traceDefaults.minAreaMm2, { type: "number", min: 0 })
      + input(view, mode, "simplifyMm", "轮廓精简精度（mm）", traceDefaults.simplifyMm, { type: "number", min: 0 });
    action = "artwork-vectorize";button = "生成加工曲线";
  } else if (kind === "load-part") {
    const source = getObjectRows(context, view, "parts").find(part => part.id === id);if (!source) return "";
    title = "加载零件到底图";note = `生成“${source.name}”的独立加工副本，后续编辑不修改来源零件。`;
    fields = input(view, mode, "x", "X（mm）", 0, { type: "number" }) + input(view, mode, "y", "Y（mm）", 0, { type: "number" });
    action = "artwork-load-part";button = "加载到底图";
  } else if (kind === "load-result") {
    const source = getObjectRows(context, view, "results").find(board => board.id === id && board.depth === 1);if (!source) return "";
    title = "加载结果板到底图";note = `将“${source.name}”上的排样作为独立加工副本加载，后续编辑不修改来源排样结果。`;
    action = "artwork-load-result";button = "加载这张板";
  } else return "";
  return `<section class="iplane-panel" data-iplane-key="artwork-panel:${escapeAttr(mode)}" data-iplane-artwork-panel="${kind}"><h3>${title}</h3>${note ? `<p class="iplane-context-note">${escapeText(note)}</p>` : ""}<div class="iplane-fields" data-pane-interaction-scope="${escapeAttr(mode)}">${fields}<button type="button" data-iplane-key="artwork-submit:${escapeAttr(mode)}" data-iplane-action="${action}" ${unavailable ? "disabled" : ""}>${button}</button><button type="button" data-iplane-action="reset-form" data-iplane-form="${escapeAttr(mode)}">还原参数</button><button type="button" data-iplane-action="artwork-cancel">返回对象参数</button></div></section>`;
}

export function renderArtworkObjectActions(context, view, kind, row) {
  const ui = getWorkspaceState(view), disabled = ui.busy || ui.geometryTaskId || row.locked === true;
  const button = (action, label) => `<button type="button" data-iplane-key="${action}:${escapeAttr(row.id)}" data-iplane-action="${action}" data-iplane-object-id="${escapeAttr(row.id)}" ${disabled ? "disabled" : ""}>${label}</button>`;
  if (kind === "drawing" && row.contentType === "text" && supports(context, view, "PlaneText.Update")) return button("artwork-open-text", "编辑文字");
  if (kind === "drawing" && row.contentType === "image") return (supports(context, view, "PlaneImage.Update") ? button("artwork-open-image", "图片参数") : "")
    + (supports(context, view, "PlaneImage.Vectorize") ? button("artwork-open-vectorize", "转为加工曲线") : "");
  if (ui.activeSketchId) return "";
  if (kind === "parts" && supports(context, view, "PlaneMachining.LoadPart")) return button("artwork-open-load-part", "加载到底图");
  if (kind === "results" && row.depth === 1 && supports(context, view, "PlaneMachining.LoadResult")) return button("artwork-open-load-result", "加载这张板到底图");
  return "";
}

export async function handleArtworkAction(context, view, ops, action, target) {
  const ui = getWorkspaceState(view), id = String(target?.dataset?.iplaneObjectId ?? ui.artworkTargetId ?? "");
  const openKinds = { "artwork-open-text": "text-edit", "artwork-open-image": "image-edit", "artwork-open-vectorize": "image-trace", "artwork-open-load-part": "load-part", "artwork-open-load-result": "load-result" };
  if (openKinds[action]) { openPanel(view, openKinds[action], id);return; }
  const mode = ui.panelMode;
  if (!mode.startsWith(modePrefix)) return;
  if (action === "artwork-cancel") { delete ui.formDrafts[mode];ui.panelMode = "object";ui.artworkTargetId = "";ui.activeTool = "plane.select";ui.panelEpoch = (ui.panelEpoch ?? 0) + 1;return; }
  const draft = ui.formDrafts[mode] ?? {}, submitted = JSON.stringify(draft), panelEpoch = ui.panelEpoch ?? 0, drawingEpoch = ui.drawingEpoch;
  const row = id ? objectRow(context, view, id) : null;
  let method, payload;
  if (action === "artwork-create-text") { method = "PlaneText.Create";payload = textParameters({ ...textDefaults, ...draft }); }
  if (action === "artwork-update-text") { if (row?.contentType !== "text") throw new Error("请选择要编辑的文字。");method = "PlaneText.Update";payload = { id, ...textParameters({ ...textDefaults, ...row, ...draft }) }; }
  if (action === "artwork-update-image") { if (row?.contentType !== "image") throw new Error("请选择参考图片。");method = "PlaneImage.Update";payload = { id, ...imageParameters({ ...row, ...draft }) }; }
  if (action === "artwork-vectorize") {
    if (row?.contentType !== "image") throw new Error("请选择要转为加工曲线的图片。");
    const values = { ...traceDefaults, ...draft }, threshold = numeric(values.threshold, "阈值", 0, 255);
    if (!Number.isInteger(threshold)) throw new Error("阈值必须为0到255的整数。");
    method = "PlaneImage.Vectorize";payload = { id, threshold, invert: Boolean(values.invert), minAreaMm2: numeric(values.minAreaMm2, "过滤面积", 0), simplifyMm: numeric(values.simplifyMm, "轮廓精简精度", 0) };
  }
  if (action === "artwork-load-part") { method = "PlaneMachining.LoadPart";payload = { partId: id, x: numeric(draft.x ?? 0, "X坐标"), y: numeric(draft.y ?? 0, "Y坐标") }; }
  if (action === "artwork-load-result") { method = "PlaneMachining.LoadResult";payload = { boardId: id }; }
  if (!method) return;
  const completed = method === "PlaneImage.Update" ? await invokePlane(context, view, ops, method, payload, { refreshView: true }) : await runArtworkTask(context, view, ops, method, payload);
  if (!completed || !current(context, view)) return;
  const unchanged = JSON.stringify(ui.formDrafts[mode] ?? {}) === submitted;
  if (unchanged) delete ui.formDrafts[mode];
  if ((ui.panelEpoch ?? 0) === panelEpoch && ui.drawingEpoch === drawingEpoch && unchanged) {
    const ids = completedIds(completed);
    if (method.startsWith("PlaneMachining.")) {
      ui.activeTool = "plane.select";ui.panelMode = "object";ui.fitCanvasKey = "drawing:worktable";
      const selectionBeforeNavigation = JSON.stringify(ui.selectedIdsByKind.drawing);
      await context.actions?.selectRibbonTab?.("drawing");
      const selectableId = selectableResult(context, view, ids);
      if (selectableId && ui.drawingEpoch === drawingEpoch && JSON.stringify(ui.selectedIdsByKind.drawing) === selectionBeforeNavigation) selectObject(view, "drawing", selectableId);
    } else if (method === "PlaneText.Create" || method === "PlaneImage.Vectorize") {
      ui.activeTool = "plane.select";ui.panelMode = "object";
      const selectableId = selectableResult(context, view, ids);
      if (selectableId) selectObject(view, "drawing", selectableId);
    }
  }
  ops?.appendProjectLog?.(context, "info", ({ "PlaneText.Create": "已添加文字。", "PlaneText.Update": "已更新文字。", "PlaneImage.Update": "已更新参考图片。", "PlaneImage.Vectorize": "已生成加工曲线，参考原图保留。", "PlaneMachining.LoadPart": "已加载独立零件加工副本。", "PlaneMachining.LoadResult": "已加载该板的独立加工副本。" })[method]);
}
