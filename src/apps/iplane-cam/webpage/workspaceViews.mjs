import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";
import { OBJECT_TITLES, draftKey, fieldValue, getCanvasObject, getObjectRows, getSelectedObject, getSelection, getWorkspaceState, hasPropertyDrafts, normalizeAreaId, readSnapshot, isGeometryEditing, geometryObjectKind, activeSketch } from "./workspaceState.mjs";
import { supports } from "./nativeOperations.mjs";
import { drawingStep } from "./drawingCommands.mjs";
import { polylineSegmentKinds } from "../../../iCAX-UI/SDK/Cad/planarCurveSegments.mjs";
import { renderArtworkObjectActions, renderArtworkOperationPanel } from "./artworkOperations.mjs";

function currentArea(context, view) { return normalizeAreaId(view.activeAreaId || context.activeRibbonTabId); }
function statusText(value) { return ({ Completed: "已完成", Partial: "部分排入", Running: "计算中", Failed: "失败", Cancelled: "已取消", CancelRequested: "取消中", Stale: "输入已变更", Undone: "结果已撤销" })[value] ?? value; }

function selectionAttributes(kind, row, selected) {
  return `data-iplane-key="object:${escapeAttr(kind)}:${escapeAttr(row.id)}" data-iplane-object-kind="${kind}" data-iplane-object-id="${escapeAttr(row.id)}" data-iplane-action="select-object" aria-selected="${selected}"`;
}

export function renderWorkspaceLeft(context, view) {
  const area = currentArea(context, view), sketch = activeSketch(context, view), kind = sketch ? "sketch-curves" : area === "drawing" ? "drawing" : "parts";
  const rows = getObjectRows(context, view, kind), selected = new Set(getSelection(view, kind));
  return `<section class="iplane-panel iplane-objects-panel" data-iplane-panel="objects" data-iplane-area="${area}" data-iplane-key="objects-panel">
    <header class="iplane-panel-heading"><strong>${sketch ? `对象 · ${escapeText(sketch.ownerName)}` : kind === "parts" ? "零件清单" : "对象"}</strong><span data-iplane-key="object-count">${rows.length} 个对象</span></header>
    <div class="iplane-object-list" data-iplane-list data-iplane-key="object-list:${kind}" role="tree" aria-label="${OBJECT_TITLES[kind]}对象">
      ${rows.length ? rows.map(row => `<button type="button" class="iplane-object-row${selected.has(row.id) ? " is-selected" : ""}" ${selectionAttributes(kind, row, selected.has(row.id))} role="treeitem" aria-level="${row.depth + 1}" ${row.referenceOnly === true ? 'disabled title="参考板材轮廓"' : ""} style="--iplane-depth:${Math.min(12, row.depth)}">${row.layerColor ? `<i class="iplane-layer-dot" style="--object-layer-color:${escapeAttr(row.layerColor)}" aria-hidden="true"></i>` : ""}<span>${escapeText(row.name)}</span><small>${escapeText(kind === "parts" && row.quantity != null ? `× ${row.quantity}` : row.contentType === "text" ? "文字" : row.contentType === "image" ? "参考图片" : row.kind ?? row.type ?? "")}</small></button>`).join("")
        : `<p class="iplane-empty" data-iplane-key="empty-list">${sketch ? "草图为空，可以绘制CAD轮廓。" : kind === "drawing" ? "底图可以直接绘制和编辑刀路，无需先定义零件或板材。" : "尚无零件。请导入零件。"}</p>`}
    </div>
    <p class="iplane-context-note" data-iplane-key="context-note">${sketch ? "CAD草图属于当前零件或板材，完成后更新其轮廓；取消保留原对象。" : kind === "drawing" ? "底图是直接加工台面，可直接绘制和编辑刀路。" : "选择零件查看参数；下方选择板材或排样方案切换画布。"}</p>
  </section>`;
}

function fact(label, value, key) {
  return `<div class="iplane-fact" data-iplane-key="fact:${key}"><dt>${escapeText(label)}</dt><dd>${escapeText(value == null || value === "" ? "—" : String(value))}</dd></div>`;
}

export function renderSceneStatusBar(_context, view) {
  const ui = getWorkspaceState(view);
  const tool = drawingStep(view).name || ({ select: "选择", erase: "橡皮擦", move: "移动", copy: "复制", rotate: "旋转", mirror: "镜像", scale: "缩放", nodes: "节点编辑", "array-rectangular": "矩形阵列", "array-polar": "圆周阵列" })[ui.activeTool.replace(/^plane\.(draw|edit)-|^plane\./, "")] || "选择";
  return `<section class="iplane-status-bar" data-iplane-statusbar data-iplane-key="scene-statusbar" aria-label="场景状态栏"><span class="iplane-status-unit">mm</span><span class="iplane-status-tool" data-iplane-key="status-tool">工具：${escapeText(tool)}</span><div class="iplane-status-controls" role="group" aria-label="绘图辅助">${[["snap", "捕捉", ui.snapEnabled], ["ortho", "正交", ui.orthoEnabled], ["grid", "网格", ui.gridVisible]].map(([key, label, active]) => `<button type="button" data-iplane-key="setting:${key}" data-iplane-action="toggle-${key}" aria-pressed="${active}" title="${label}${active ? "已开启" : "已关闭"}">${label}</button>`).join("")}</div></section>`;
}

function renderDrawingPanel(view) {
  const ui = getWorkspaceState(view), step = drawingStep(view);
  if (!step.name) return "";
  const optionKey = ({ polygon: "sides", star: "tips" })[step.primitive];
  const option = optionKey && ui.geometryCapabilities?.primitiveOptions?.[step.primitive]?.[optionKey];
  const shapeOptions = option ? `<label class="iplane-field" data-iplane-key="drawing-option-field:${step.primitive}:${optionKey}"><span>${step.primitive === "polygon" ? "边数" : "星角数"}</span><input type="number" data-iplane-key="drawing-option:${step.primitive}:${optionKey}" data-iplane-drawing-option="${optionKey}" data-iplane-drawing-option-primitive="${step.primitive}" value="${escapeAttr(String(ui.drawingShapeOptions?.[step.primitive]?.[optionKey] ?? option.default))}" min="${escapeAttr(String(option.min))}" max="${escapeAttr(String(option.max))}" step="1" ${ui.busy || ui.drawingCommitting || ui.geometryTaskId ? "disabled" : ""}></label>` : "";
  const help = step.help ?? ({
    arc: "依次指定圆心、起点、终点，逆时针生成圆弧。右键选择结束或取消。",
    ellipse: "依次指定圆心、第一半轴端点和第二半轴。右键选择结束或取消。",
    ellipseArc: "依次指定圆心、两个半轴和圆弧角度。右键选择结束或取消。",
    bezier: "依次指定起点、两个控制点和终点。右键选择结束或取消。",
    bspline: "依次指定至少四个控制点，右键选择结束。",
    triangle: "依次指定三角形的三个顶点。右键选择结束或取消。",
    polygon: "指定中心与首个顶点；边数可在右侧设置。右键选择结束或取消。",
    star: "依次指定中心、外半径和内半径；星角数可在右侧设置。右键选择结束或取消。",
    polyline: "右键切换曲段、完成本段、闭合或结束。",
  })[step.primitive] ?? "在画布依次点击，或按下鼠标拖动后松开。右键选择结束或取消。";
  const segmentKinds = polylineSegmentKinds.filter(([kind]) => ui.geometryCapabilities?.segmentTypes?.includes(kind));
  const switchBlocked = ui.polylinePending?.length > 0 && (ui.polylineMode !== "bspline" || ui.polylinePending.length < 3);
  const polylineControls = step.primitive === "polyline" && segmentKinds.length > 1 ? `<div class="iplane-polyline-kinds" data-iplane-key="polyline-kinds" role="group" aria-label="当前曲段类型">${segmentKinds.map(([kind, label]) => `<button type="button" data-iplane-key="polyline-kind:${kind}" data-iplane-action="polyline-kind" data-iplane-polyline-kind="${kind}" aria-pressed="${ui.polylineMode === kind}" ${ui.busy || ui.drawingCommitting || switchBlocked && ui.polylineMode !== kind ? "disabled" : ""} title="${label}">${label}</button>`).join("")}</div><p class="iplane-context-note" data-iplane-key="polyline-progress" data-iplane-polyline-progress>已完成 ${ui.polylineSegments?.length ?? 0} 段；当前已输入 ${ui.polylinePending?.length ?? 0} 个${ui.polylineMode === "bspline" ? "附加控制点" : "构造点"}。</p>` : "";
  return `<section class="iplane-panel" data-iplane-key="drawing-input-panel" aria-label="绘图参数"><h3>${step.name} · mm</h3>
    ${polylineControls}
    ${shapeOptions}
    <p class="iplane-drawing-step" data-iplane-key="drawing-step" data-iplane-drawing-step>${escapeText(step.label)}</p>
    <p class="iplane-context-note" data-iplane-key="drawing-help">${escapeText(help)} Esc 取消。</p>
    <label class="iplane-field" data-iplane-key="drawing-value-field"><span data-iplane-key="drawing-value-label">${escapeText(step.inputLabel)}</span><input type="text" data-iplane-key="drawing-value-input" data-iplane-draw-value data-model-value-authoritative="true" value="${escapeAttr(ui.commandInput)}" autocomplete="off" placeholder="${step.scalar ? "数值或 X,Y" : "X,Y 或 @dX,dY"}"></label>
    <button type="button" data-iplane-key="drawing-submit" data-iplane-action="submit-drawing-input" ${ui.busy ? "disabled" : ""}>确定输入</button>
    <p class="iplane-context-note" data-iplane-key="drawing-input-help">相对坐标：@dX,dY；极坐标：@长度&lt;角度。${step.primitive === "arc" ? "终止角度以 X 正方向为 0°。" : ""}${step.primitive === "polyline" && segmentKinds.length > 1 ? "B样条含起点至少4个控制点后，右键选择完成本段，可继续绘制下一段。Backspace 或 Ctrl+Z 逐点退回。" : ""}</p>
  </section>`;
}

function renderNestingOverview(context, view) {
  const snapshot = readSnapshot(context, view), parameters = snapshot.nestingParameters;
  return `<section class="iplane-panel" data-iplane-key="properties-panel"><h3>排样参数</h3>
    <p class="iplane-empty">选择左侧零件，或下方板材与排样结果，查看和编辑对应参数。</p>
    <dl class="iplane-facts" data-iplane-key="nesting-overview">${["parts", "stocks", "results"].map(kind => fact(OBJECT_TITLES[kind], getObjectRows(context, view, kind).filter(row => row.depth === 0).length, `count:${kind}`)).join("")}
      ${parameters && typeof parameters === "object" ? fact("零件间距", parameters.partGap, "part-gap") + fact("板边间距", parameters.stockMargin, "stock-margin") + fact("旋转步长", parameters.rotationStepDeg == null ? null : `${parameters.rotationStepDeg}°`, "rotation") : ""}</dl>
    ${parameters ? "" : '<p class="iplane-context-note" data-iplane-key="parameter-empty">尚无排样参数。</p>'}
  </section>`;
}

function formInput(view, mode, name, label, fallback, { type = "number", min, step = "any" } = {}) {
  const value = getWorkspaceState(view).formDrafts[mode]?.[name] ?? fallback ?? "";
  return `<label class="iplane-field" data-iplane-key="${escapeAttr(`form-field:${mode}:${name}`)}"><span>${label}</span><input type="${type}" data-iplane-key="${escapeAttr(`form-input:${mode}:${name}`)}" data-iplane-form-field="${escapeAttr(name)}" data-iplane-form="${escapeAttr(mode)}" value="${escapeAttr(String(value))}" ${min != null ? `min="${min}"` : ""} ${type === "number" ? `step="${step}"` : ""}></label>`;
}

function renderOperationPanel(context, view) {
  const ui = getWorkspaceState(view), mode = ui.panelMode;
  const formMode = mode === "drawing-edit" && ui.activeTool === "plane.edit-nodes" ? ui.nodeEditor?.form ?? mode : mode;
  const back = `${mode === "report" ? "" : `<button type="button" data-iplane-action="reset-form" data-iplane-form="${escapeAttr(formMode)}">还原参数</button>`}<button type="button" data-iplane-action="close-panel">返回对象参数</button>`;
  if (mode === "import-units") {
    const units = ui.formDrafts[mode]?.units ?? "";
    return `<section class="iplane-panel" data-iplane-key="import-units-panel"><h3>DXF 文件单位</h3><p class="iplane-context-note">该文件未声明有效单位，请选择原图使用的单位。</p><p class="iplane-context-note">${escapeText(String(ui.pendingImport?.sourcePath ?? "").split(/[\\/]/).at(-1) ?? "")}</p>
      <label class="iplane-field" data-iplane-key="units-field"><span>原图单位</span><select data-iplane-key="units-input" data-iplane-form="import-units" data-iplane-form-field="units">${[["", "请选择单位"], ["mm", "毫米（mm）"], ["cm", "厘米（cm）"], ["m", "米（m）"], ["in", "英寸（in）"]].map(([value, label]) => `<option value="${value}" ${units === value ? "selected" : ""}>${label}</option>`).join("")}</select></label>
      <button type="button" data-iplane-action="import-with-units" ${ui.busy ? "disabled" : ""}>按所选单位导入</button><button type="button" data-iplane-action="cancel-import">取消本次导入</button></section>`;
  }
  if (mode === "stock-rect") return `<section class="iplane-panel" data-iplane-key="rectangle-stock-panel"><h3>矩形板材 · mm</h3><div class="iplane-fields" data-pane-interaction-scope="stock-rect">
    ${formInput(view, mode, "name", "名称", "矩形板材", { type: "text" })}${formInput(view, mode, "width", "宽度", 3000, { min: 0.001 })}${formInput(view, mode, "height", "高度", 1500, { min: 0.001 })}${formInput(view, mode, "quantity", "库存数量", 1, { min: 1, step: 1 })}
    <button type="button" data-iplane-action="create-rectangle" ${ui.busy ? "disabled" : ""}>添加板材</button>${back}</div></section>`;
  if (mode === "nesting-settings") {
    const parameters = readSnapshot(context, view).nestingParameters ?? {}, draft = ui.formDrafts[mode] ?? {};
    return `<section class="iplane-panel" data-iplane-key="nesting-settings-panel"><h3>排样参数 · mm</h3><div class="iplane-fields" data-pane-interaction-scope="nesting-settings">
      ${formInput(view, mode, "partGap", "零件间距", parameters.partGap ?? 5, { min: 0 })}${formInput(view, mode, "stockMargin", "板边间距", parameters.stockMargin ?? 5, { min: 0 })}${formInput(view, mode, "rotationStepDeg", "旋转步长（°）", parameters.rotationStepDeg ?? 90, { min: 0.001 })}${formInput(view, mode, "timeLimitSeconds", "计算时间（秒）", parameters.timeLimitSeconds ?? 10, { min: 0.001 })}
      <label class="iplane-checkbox" data-iplane-key="mirror-setting"><input type="checkbox" data-iplane-key="mirror-input" data-iplane-form-field="allowMirror" data-iplane-form="nesting-settings" ${draft.allowMirror ?? parameters.allowMirror ? "checked" : ""}>允许镜像</label>
      <button type="button" data-iplane-action="apply-nesting-settings" ${ui.busy ? "disabled" : ""}>应用参数</button>${back}</div></section>`;
  }
  if (mode === "drawing-edit") {
    const operation = ui.activeTool.replace("plane.edit-", ""), labels = { move: "移动", copy: "复制", rotate: "旋转", scale: "缩放", mirror: "镜像", "array-rectangular": "矩形阵列", "array-polar": "圆周阵列" };
    if (operation === "nodes") {
      const state = ui.nodeEditor;
      return `<section class="iplane-panel" data-iplane-key="node-editor"><h3>节点坐标 · mm</h3>${state?.editor?.supported ? `<div class="iplane-fields" data-pane-interaction-scope="nodes:${escapeAttr(state.id)}">${state.editor.points.map((point,index) => `<div data-iplane-key="node:${index}"><strong>节点 ${index + 1}</strong>${formInput(view,state.form,`point${index}x`,"X",point[0],{type:"text"})}${formInput(view,state.form,`point${index}y`,"Y",point[1],{type:"text"})}</div>`).join("")}<button type="button" data-iplane-action="apply-node-edit" ${ui.busy || ui.geometryTaskId ? "disabled" : ""}>应用节点</button></div>` : `<p>${escapeText(state?.error || (state?.editor ? "请选择单一直线或多段线；圆和圆弧使用尺寸标注编辑。" : "请选择曲线，正在读取节点。"))}</p>`}${back}</section>`;
    }
    return `<section class="iplane-panel" data-iplane-key="drawing-edit-panel"><h3>${labels[operation] ?? "编辑"}图形</h3><p class="iplane-context-note">选择刀路或草图曲线，再输入参数。</p><div class="iplane-fields" data-pane-interaction-scope="drawing-edit:${operation}">
      ${operation === "move" || operation === "copy" ? formInput(view, mode, "dx", "X 位移（mm）", 0) + formInput(view, mode, "dy", "Y 位移（mm）", 0) : operation === "rotate" ? formInput(view, mode, "angleDeg", "角度（°）", 0) : operation === "mirror" ? formInput(view, mode, "axis", "镜像轴（x / y）", "y", {type:"text"}) + formInput(view, mode, "cx", "基点 X（mm）",0) + formInput(view, mode, "cy", "基点 Y（mm）",0) : operation === "array-rectangular" ? formInput(view,mode,"rows","行数",2,{min:1,step:1}) + formInput(view,mode,"columns","列数",2,{min:1,step:1}) + formInput(view,mode,"spacingX","X间距（mm）",100) + formInput(view,mode,"spacingY","Y间距（mm）",100) : operation === "array-polar" ? formInput(view,mode,"count","数量",4,{min:1,step:1}) + formInput(view,mode,"cx","中心X（mm）",0) + formInput(view,mode,"cy","中心Y（mm）",0) + formInput(view,mode,"angleDeg","分布角度（°）",360) : formInput(view, mode, "factor", "缩放倍数", 1, { min: 0.001 })}
      <button type="button" data-iplane-action="apply-drawing-edit" ${ui.busy || ui.geometryTaskId ? "disabled" : ""}>应用</button>${back}</div></section>`;
  }
  if (mode === "report") {
    const report = ui.report?.report ?? ui.report ?? {}, metrics = report.metrics ?? report;
    const currentResult = getObjectRows(context, view, "results").find(row => row.id === report.id);
    const polygonScope = (metrics.validationScope ?? report.validationScope) === "PolygonMaterial";
    const fields = [["结果状态", "status"], ["需求数量", "required"], ["已排数量", "placed"], ["未排数量", "unplacedCount"], ["用板数", "boardCount"], [polygonScope ? "零件计算轮廓净面积" : "零件净面积", "placedMaterialArea"], [polygonScope ? "板材计算轮廓净面积" : "板材净面积", "stockNetArea"], [polygonScope ? "计算轮廓净面积利用率" : "净面积利用率", "utilization"], ["几何检查", "valid"], ["检查范围", "validationScope"], ["计算耗时（ms）", "elapsedMs"], ["停止原因", "stopReason"]];
    const value = key => {
      const entry = metrics[key] ?? report[key];
      if (key === "status" || key === "stopReason") return statusText(entry);
      if (key === "valid" && typeof entry === "boolean") return entry ? "通过" : "未通过";
      if (key === "validationScope" && entry === "PolygonMaterial") return "计算轮廓（含内孔）";
      if (key === "utilization" && Number.isFinite(entry)) return `${(entry * 100).toFixed(2)}%`;
      if (key === "unplacedCount" && entry == null && Array.isArray(report.unplaced)) return report.unplaced.length;
      return entry;
    };
    return `<section class="iplane-panel" data-iplane-key="report-panel"><h3>排样报告</h3>${report.stale || currentResult?.stale ? '<p class="iplane-context-note">输入已修改，本报告保留的是先前结果。</p>' : ""}${polygonScope ? `<p class="iplane-context-note">面积与几何检查依据用于排样的计算轮廓（含内孔）；原始曲线保留。${Number(report.approximationBoundMm) > 0 ? `曲线近似误差上限为 ${escapeText(String(report.approximationBoundMm))} mm，计算间距已计入近似安全余量。` : ""}</p>` : ""}<dl class="iplane-facts">${fields.map(([label, key]) => fact(label, value(key), `report:${key}`)).join("")}</dl>
      ${Array.isArray(report.unplaced) && report.unplaced.length ? `<p class="iplane-context-note">未排：${escapeText(report.unplaced.map(item => typeof item === "object" ? item.partId ?? item.name ?? item.id : item).join("、"))}</p>` : ""}
      <button type="button" data-iplane-action="download-report">导出报告 CSV</button>${back}</section>`;
  }
  return "";
}

function operationNotice(view) {
  const ui = getWorkspaceState(view);
  return `${ui.geometryTaskId ? `<section class="iplane-panel" data-iplane-key="geometry-task"><h3>正在处理几何</h3><p>${escapeText(ui.geometryTask?.status ?? "Running")}</p><button type="button" data-iplane-action="cancel-geometry-task">取消操作</button></section>` : ""}${ui.operationError ? `<section class="iplane-operation-error" role="alert" data-iplane-key="operation-error">${escapeText(ui.operationError)}</section>` : ""}${ui.pendingImport && ui.panelMode !== "import-units" ? '<section class="iplane-panel" data-iplane-key="pending-import-units"><p class="iplane-context-note">导入文件尚未指定原图单位。</p><button type="button" data-iplane-action="resume-import">选择导入单位</button><button type="button" data-iplane-action="cancel-import">取消本次导入</button></section>' : ""}${ui.runningTaskId ? `<section class="iplane-panel" data-iplane-key="running-task"><h3>正在排样</h3><p class="iplane-context-note">${escapeText(ui.task?.message ?? "正在计算板材布局…")}</p><button type="button" data-iplane-action="cancel-nesting" ${ui.busy ? "disabled" : ""}>取消本次排样</button></section>` : ""}`;
}

export function renderWorkspaceRight(context, view) {
  const area = currentArea(context, view), selected = getSelectedObject(context, view);
  if (isGeometryEditing(view) && getWorkspaceState(view).activeTool.startsWith("plane.draw-")) return operationNotice(view) + renderDrawingPanel(view);
  const artwork = renderArtworkOperationPanel(context, view);
  if (artwork) return operationNotice(view) + artwork;
  const operation = renderOperationPanel(context, view);
  if (operation) return operationNotice(view) + operation;
  const settings = "";
  const sketch = activeSketch(context, view);
  if (sketch) return operationNotice(view) + `<section class="iplane-panel" data-iplane-key="sketch-properties"><h3>CAD草图 · ${escapeText(sketch.ownerName)}</h3><p>编辑外轮廓和内孔；完成时校验并更新原对象。</p>${selected ? `<dl class="iplane-facts">${fact("所选曲线",selected.row.name,"sketch-selected")}${fact("宽度",selected.row.width,"sketch-width")}${fact("高度",selected.row.height,"sketch-height")}</dl>` : ""}<button type="button" data-iplane-action="sketch-complete" ${getWorkspaceState(view).busy || getWorkspaceState(view).geometryTaskId ? "disabled" : ""}>完成草图</button><button type="button" data-iplane-action="sketch-cancel" ${getWorkspaceState(view).busy ? "disabled" : ""}>取消草图</button></section>` + settings;
  if (!selected) return operationNotice(view) + (area === "nesting" ? renderNestingOverview(context, view)
    : `<section class="iplane-panel" data-iplane-key="properties-panel"><h3>属性</h3><p class="iplane-empty">选择底图中的对象，查看图形属性。</p></section>${settings}`);
  const { kind, row } = selected, key = draftKey(kind, row);
  if (kind === "results" && row.depth > 0) {
    const source = row.partId ? getObjectRows(context, view, "parts").find(part => part.id === row.partId) : null;
    const parent = getObjectRows(context, view, "results").find(result => result.id === row.ancestorIds[0]);
    const values = row.partId ? [["来源零件", source?.name ?? row.partId], ["需求实例序号", row.instanceIndex], ["X（mm）", row.x], ["Y（mm）", row.y], ["角度（°）", row.angleDeg], ["镜像", row.flip ? "是" : "否"]]
      : [["名称", row.name], ["物理库存序号", row.physicalIndex], ["宽度（mm）", row.width], ["高度（mm）", row.height], ["已排数量", row.placed], ["计算轮廓净面积利用率", Number.isFinite(row.utilization) ? `${(row.utilization * 100).toFixed(2)}%` : row.utilization]];
    return `${operationNotice(view)}<section class="iplane-panel" data-iplane-key="result-object-info"><h3>${row.partId ? "排入零件信息" : "结果板信息"}</h3>${parent?.stale ? '<p class="iplane-context-note">输入已修改，当前显示先前的排样结果。</p>' : ""}<dl class="iplane-facts">${values.map(([label, value], index) => fact(label, value, `result-object:${index}`)).join("")}</dl>${renderArtworkObjectActions(context, view, kind, row)}</section>`;
  }
  const field = (name, label, multiline = false) => `<label class="iplane-field" data-iplane-key="field:${escapeAttr(key)}:${name}"><span>${label}</span>${multiline
    ? `<textarea rows="4" data-iplane-key="input:${escapeAttr(key)}:${name}" data-iplane-field="${name}" data-iplane-owner="${escapeAttr(key)}">${escapeText(fieldValue(view, kind, row, name))}</textarea>`
    : `<input type="text" ${name === "quantity" ? 'inputmode="numeric"' : ""} data-iplane-key="input:${escapeAttr(key)}:${name}" data-iplane-field="${name}" data-iplane-owner="${escapeAttr(key)}" value="${escapeAttr(fieldValue(view, kind, row, name))}" autocomplete="off">`}</label>`;
  return `${operationNotice(view)}<section class="iplane-panel" data-iplane-key="properties-panel"><header class="iplane-panel-heading"><strong>${kind === "results" ? "结果信息" : `${OBJECT_TITLES[kind]}参数`}</strong></header>
    <div class="iplane-fields" data-pane-interaction-scope="${escapeAttr(key)}" data-iplane-key="fields:${escapeAttr(key)}">
      ${renderArtworkObjectActions(context, view, kind, row)}
      ${["parts", "stocks"].includes(kind) && supports(context,view,"PlaneSketch.Open") ? `<button type="button" data-iplane-action="sketch-open" data-iplane-object-id="${escapeAttr(row.id)}" ${getWorkspaceState(view).busy ? "disabled" : ""}>进入草图编辑</button>` : ""}${field("name", "名称")}${["parts", "stocks"].includes(kind) ? field("quantity", kind === "parts" ? "需求数量" : "库存数量") : ""}${field("note", "备注", true)}
      <p class="iplane-draft-note" data-iplane-key="draft-note" data-iplane-draft-status>${hasPropertyDrafts(view) ? "属性有改动，应用后写入工程。" : "修改属性后点击应用。"}</p>
      ${supports(context, view, "PlaneObject.Update") && !(kind === "results" && row.depth > 0) ? `<button type="button" data-iplane-key="apply-properties" data-iplane-action="apply-properties" ${getWorkspaceState(view).busy || !getWorkspaceState(view).drafts[key] ? "disabled" : ""}>应用属性</button>` : ""}
      <button type="button" data-iplane-key="reset-draft" data-iplane-action="reset-draft">还原当前属性草稿</button>
    </div><dl class="iplane-facts" data-iplane-key="facts:${escapeAttr(key)}">${row.width != null ? fact("宽度", row.width, "width") : ""}${row.height != null ? fact("高度", row.height, "height") : ""}${row.holeCount != null ? fact("内孔", row.holeCount, "holes") : ""}${row.layerName ? fact("图层", row.layerName, "layer") : ""}${row.placed != null ? fact("已排数量", row.placed, "placed") : ""}${row.required != null ? fact("需求数量", row.required, "required") : ""}${fact("来源", row.sourceName ?? row.source, "source")}${fact("单位", row.units ?? row.unit, "units")}${fact("状态", row.status ?? row.state, "status")}</dl>
  </section>${settings}`;
}

function renderDockGroup(context, view, kind, canvas) {
  const rows = getObjectRows(context, view, kind).filter(row => kind !== "results" || row.depth <= 1), selected = new Set(getSelection(view, kind));
  const details = row => kind === "stocks"
    ? [row.width != null && row.height != null ? `${row.width} × ${row.height} ${row.units ?? row.unit ?? "mm"}` : null, row.quantity != null ? `库存 ${row.quantity}` : null]
    : [row.placed != null && row.required != null ? `已排 ${row.placed} / ${row.required}` : row.placed != null ? `已排 ${row.placed}` : null, row.stale ? "输入已变更" : statusText(row.status)];
  return `<section class="iplane-dock-group" data-iplane-dock-group="${kind}" data-iplane-key="dock-group:${kind}" aria-label="${OBJECT_TITLES[kind]}">
    <header class="iplane-panel-heading"><strong>${OBJECT_TITLES[kind]}</strong><span data-iplane-key="dock-count:${kind}">${rows.filter(row => row.depth === 0).length} ${kind === "stocks" ? "种板材" : "个方案"}</span></header>
    <div class="iplane-dock-cards" data-iplane-dock-list="${kind}" data-iplane-key="dock-list:${kind}" role="listbox" aria-label="${OBJECT_TITLES[kind]}列表" aria-multiselectable="true">
      ${rows.length ? rows.map(row => {
        const active = canvas?.kind === kind && canvas.row.id === row.id;
        return `<button type="button" class="iplane-dock-card${selected.has(row.id) ? " is-selected" : ""}${active ? " is-current-canvas" : ""}" ${selectionAttributes(kind, row, selected.has(row.id))} role="option" ${active ? 'aria-current="true"' : ""}>
          <strong>${escapeText(row.name)}</strong>${details(row).filter(value => value != null && value !== "").map((value, index) => `<span data-iplane-key="card-detail:${index}">${escapeText(String(value))}</span>`).join("")}
          ${active ? '<small data-iplane-key="current-canvas">当前画布</small>' : ""}</button>`;
      }).join("") : `<p class="iplane-empty" data-iplane-key="empty-dock:${kind}">${kind === "stocks" ? "尚无板材。" : "尚无排样结果。"}</p>`}
    </div>
  </section>`;
}

export function renderWorkspaceBottom(context, view) {
  const area = currentArea(context, view), canvas = getCanvasObject(context, view, area);
  return `<div class="iplane-bottom-root" data-iplane-bottom-root data-iplane-area="${area}" data-iplane-key="workspace-bottom">
    <div class="cam-splitter iplane-bottom-splitter" data-iplane-key="bottom-splitter" data-cam-resize-pane="bottom" data-no-window-drag title="拖拽调整板材与排样结果区域高度"${area === "drawing" || getWorkspaceState(view).activeSketchId ? " hidden" : ""}></div>
    <section class="iplane-nesting-dock" data-iplane-nesting-dock data-iplane-key="nesting-dock" aria-label="板材与排样结果"${area === "drawing" ? " hidden" : ""}>${renderDockGroup(context, view, "stocks", canvas)}${renderDockGroup(context, view, "results", canvas)}</section>
  </div>`;
}

export function renderCommandPanel(_context, view) {
  const ui = getWorkspaceState(view);
  return `<section class="iplane-command-panel" data-iplane-command-root data-iplane-key="command-panel" aria-label="命令">
    <label class="iplane-command-line"><span>命令</span><input type="text" data-iplane-key="command-input" data-iplane-command-input data-model-value-authoritative="true" value="${escapeAttr(ui.commandInput)}" autocomplete="off" placeholder="输入命令"><small data-iplane-key="command-status" data-iplane-status>${escapeText(ui.commandStatus)}</small></label>
  </section>`;
}
