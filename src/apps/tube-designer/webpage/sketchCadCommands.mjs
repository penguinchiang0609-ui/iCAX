import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";

const clone = value => typeof structuredClone === "function" ? structuredClone(value) : JSON.parse(JSON.stringify(value));
export const CAD_TOOLS = new Set(["move", "copy", "rotate", "mirror", "scale", "align", "measure", "trim", "extend", "offset", "fillet", "chamfer", "repair", "diagnose", "racetrack", "polygon", "star", "array-circumferential", "end-cuts", "split-parts", "text-outline"]);
export const CAD_TRANSFORMS = new Set(["move", "copy", "rotate", "mirror", "scale"]);
export const CAD_SHAPES = new Set(["racetrack", "polygon", "star"]);
export const isCadOperation = command => String(command?.tool ?? "").startsWith("cad-");
export const cadOperationKind = command => isCadOperation(command) ? command.tool.slice(4) : "";
const labels = { move: "移动", copy: "复制", rotate: "旋转", mirror: "镜像", scale: "缩放", align: "对齐与均布", measure: "测量", trim: "修剪", extend: "延伸", offset: "偏移", fillet: "倒圆角", chamfer: "倒角", repair: "修图", diagnose: "图形检查", racetrack: "跑道形", polygon: "正多边形", star: "星形", "array-circumferential": "管周均布", "end-cuts": "端部裁切", "split-parts": "实体分件", "text-outline": "文字轮廓" };
export const cadOperationLabel = command => labels[cadOperationKind(command)] || "几何编辑";

export function beginCadOperation(state, draft, kind, options = {}) {
  const ids = new Set(draft.selectedIds ?? []);
  const sources = draft.entities.filter(entity => ids.has(entity.id));
  if ([...CAD_TRANSFORMS, "align", "offset", "fillet", "chamfer", "array-circumferential"].includes(kind) && !sources.length) throw new Error("请先选择图形，可按 Shift/Ctrl 多选或框选。");
  if (["array-circumferential", "end-cuts", "split-parts"].includes(kind) && !(options.perimeter > 0)) throw new Error("请在管材侧面展开图中使用此功能。");
  const box = options.bounds ?? { minX: 0, minY: 0, maxX: 0, maxY: 0 };
  const cx = (box.minX + box.maxX) / 2, cy = (box.minY + box.maxY) / 2;
  const base = { centerX: cx, centerY: cy, x: cx, y: cy, dx: 0, dy: 0, angle: 0, factor: 1, x1: cx, y1: cy - 10, x2: cx, y2: cy + 10, direction: "left", distance: 2, radius: 2, tolerance: 0.01, removeDuplicates: true, removeShort: false, joinGaps: true, simplify: true, trimSelfIntersections: false, count: 4, rows: 1, spacingY: 20, width: 30, height: 15, sides: 6, points: 5, innerRadius: 6, outerRadius: 12, rotation: 0, copy: false, start: 0, end: options.length || 1000, startAngle: 0, startRotation: 0, endAngle: 0, endRotation: 0, splitParts: draft.splitParts === true, text: "零件", font: "Microsoft YaHei", textHeight: 15 };
  const command = { tool: `cad-${kind}`, points: [], mode: "parameters", spec: { ...base }, sources: clone(sources), sourceIds: sources.map(entity => entity.id), sourceRefs: sources, sourceSignature: JSON.stringify(sources), error: "", issues: [], perimeter: options.perimeter, length: options.length };
  if (["measure", "trim", "extend"].includes(kind)) command.mode = "mouse";
  if (kind === "repair" || kind === "diagnose") { const chosen = sources.length ? sources : draft.entities; Object.assign(command, { sources: clone(chosen), sourceRefs: chosen, sourceIds: chosen.map(entity => entity.id), sourceSignature: JSON.stringify(chosen) }); }
  if (kind === "end-cuts" && draft.endCuts) Object.assign(command.spec, { start: draft.endCuts.start?.position ?? 0, end: draft.endCuts.end?.position ?? options.length, startAngle: draft.endCuts.start?.angleDegrees ?? 0, startRotation: draft.endCuts.start?.rotationDegrees ?? 0, endAngle: draft.endCuts.end?.angleDegrees ?? 0, endRotation: draft.endCuts.end?.rotationDegrees ?? 0 });
  state.tool = command.tool;
  state.command = command;
  return command;
}

export function cadPrompt(command) {
  if (command.error) return command.error;
  const kind = cadOperationKind(command), title = cadOperationLabel(command), n = command.points.length;
  if (kind === "measure") return n ? "测量：指定第二点 · 可连续测量 · Esc 退出" : "测量：指定第一点 · 捕捉端点/圆心 · Esc 退出";
  if (kind === "trim" || kind === "extend") return `${title}：${command.sourceIds.length ? "已选图形作为边界" : "其余图形作为边界"}，单击要${title}的位置 · Esc 退出`;
  if (kind === "fillet" || kind === "chamfer") return `${title}：在画布上点击角点或保留的一侧，填写尺寸后应用 · Esc 取消`;
  if (command.mode !== "mouse") return `${title}：填写参数，应用后可撤销 · Esc 取消`;
  if (kind === "rotate") return n ? "旋转：移动鼠标确定角度，单击应用" : "旋转：点击旋转中心";
  if (kind === "mirror") return n ? "镜像：指定镜像轴第二点" : "镜像：指定镜像轴第一点";
  if (kind === "scale") return n === 0 ? "缩放：指定缩放中心" : n === 1 ? "缩放：指定参考点" : "缩放：移动鼠标确定比例，单击应用";
  if (CAD_SHAPES.has(kind)) return n ? `${title}：移动鼠标确定尺寸，单击应用` : `${title}：指定${kind === "racetrack" ? "第一角点" : "中心"}`;
  return n ? `${title}：移动鼠标确定位置，单击应用` : `${title}：指定基点`;
}

export function updateCadPointer(command, point, commitPoint = false) {
  const kind = cadOperationKind(command), spec = command.spec, base = command.points[0];
  if (!base) {
    if (!commitPoint) return false;
    command.points.push([...point]);
    if (["rotate", "scale", "polygon", "star"].includes(kind)) Object.assign(spec, { centerX: point[0], centerY: point[1] });
    if (kind === "mirror") Object.assign(spec, { x1: point[0], y1: point[1] });
    if (kind === "racetrack") Object.assign(spec, { x: point[0], y: point[1] });
    return false;
  }
  if (["move", "copy"].includes(kind)) Object.assign(spec, { dx: point[0] - base[0], dy: point[1] - base[1] });
  else if (kind === "rotate") spec.angle = Math.atan2(point[1] - base[1], point[0] - base[0]) * 180 / Math.PI;
  else if (kind === "mirror") Object.assign(spec, { x2: point[0], y2: point[1] });
  else if (kind === "scale") {
    if (command.points.length === 1) { if (commitPoint && Math.hypot(point[0] - base[0], point[1] - base[1]) > 1e-8) command.points.push([...point]); return false; }
    spec.factor = Math.hypot(point[0] - base[0], point[1] - base[1]) / Math.hypot(command.points[1][0] - base[0], command.points[1][1] - base[1]);
  } else if (kind === "racetrack") Object.assign(spec, { x: Math.min(base[0], point[0]), y: Math.min(base[1], point[1]), width: Math.abs(point[0] - base[0]), height: Math.abs(point[1] - base[1]) });
  else if (kind === "polygon" || kind === "star") { spec.outerRadius = Math.hypot(point[0] - base[0], point[1] - base[1]); spec.rotation = Math.atan2(point[1] - base[1], point[0] - base[0]) * 180 / Math.PI; }
  command.error = "";
  return commitPoint;
}

export function cadTransformMatrix(command) {
  const kind = cadOperationKind(command), s = command.spec, num = key => cadNumber(s[key]);
  if (kind === "move" || kind === "copy") return [1, 0, 0, 1, num("dx"), num("dy")];
  const cx = num("centerX"), cy = num("centerY");
  if (kind === "rotate") { const a = num("angle") * Math.PI / 180, c = Math.cos(a), q = Math.sin(a); return [c, q, -q, c, cx - c * cx + q * cy, cy - q * cx - c * cy]; }
  if (kind === "scale") { const f = num("factor"); if (!(f > 0)) throw new Error("缩放比例须大于零"); return [f, 0, 0, f, cx * (1 - f), cy * (1 - f)]; }
  if (kind === "mirror") {
    const x = num("x1"), y = num("y1"), dx = num("x2") - x, dy = num("y2") - y, l = dx * dx + dy * dy;
    if (l < 1e-16) throw new Error("镜像轴的两个点不能重合");
    const a = (dx * dx - dy * dy) / l, b = 2 * dx * dy / l;
    return [a, b, b, -a, x - a * x - b * y, y - b * x + a * y];
  }
  return null;
}

export function cadNumber(value) {
  if (String(value ?? "").trim() === "" || !Number.isFinite(Number(value))) throw new Error("请输入有限的数值");
  return Number(value);
}

export function cadNumericSpec(command) {
  const result = { ...command.spec, kind: cadOperationKind(command) };
  for (const key of ["centerX", "centerY", "x", "y", "dx", "dy", "angle", "factor", "x1", "y1", "x2", "y2", "distance", "radius", "tolerance", "count", "rows", "spacingY", "width", "height", "sides", "points", "innerRadius", "outerRadius", "rotation", "start", "end", "startAngle", "startRotation", "endAngle", "endRotation", "textHeight"]) result[key] = cadNumber(result[key]);
  return result;
}

export function renderCadOperationPanel(command, arcLengthAxial = false) {
  if (!isCadOperation(command)) return "";
  const kind = cadOperationKind(command), s = command.spec, x = arcLengthAxial ? "U" : "X", y = arcLengthAxial ? "S" : "Y";
  const input = (key, label, type = "text") => `<label class="tube-designer-field"><span>${label}</span><input type="${type}" ${type === "text" ? 'inputmode="decimal"' : ""} value="${escapeAttr(s[key] ?? "")}" data-tube-sketch-cad-field="${key}" data-cam-change-action="tube-designer-sketch-cad-field" autocomplete="off"/></label>`;
  const check = (key, label) => `<label class="tube-sketch-cad-checkbox"><input type="checkbox" data-tube-sketch-cad-field="${key}" data-cam-change-action="tube-designer-sketch-cad-field" ${s[key] ? "checked" : ""}/><span>${label}</span></label>`;
  const center = () => input("centerX", `中心 ${x}`) + input("centerY", `中心 ${y}`);
  let fields = "";
  if (kind === "move" || kind === "copy") fields = input("dx", `${x} 位移`) + input("dy", `${y} 位移`);
  if (kind === "rotate") fields = center() + input("angle", "旋转角度（°）") + check("copy", "保留原图形");
  if (kind === "mirror") fields = input("x1", `轴起点 ${x}`) + input("y1", `轴起点 ${y}`) + input("x2", `轴终点 ${x}`) + input("y2", `轴终点 ${y}`) + check("copy", "保留原图形");
  if (kind === "scale") fields = center() + input("factor", "等比缩放比例");
  if (kind === "align") fields = `<label class="tube-designer-field"><span>对齐方式</span><select data-tube-sketch-cad-field="direction" data-cam-change-action="tube-designer-sketch-cad-field">${Object.entries({ left: "左对齐", right: "右对齐", top: "上对齐", bottom: "下对齐", centerX: "水平中心对齐", centerY: "竖直中心对齐", distributeX: "水平均布", distributeY: "竖直均布" }).map(([v, label]) => `<option value="${v}" ${s.direction === v ? "selected" : ""}>${label}</option>`).join("")}</select></label>`;
  if (kind === "offset") fields = input("distance", "偏移距离（有正负）");
  if (kind === "fillet") fields = input("radius", "圆角半径");
  if (kind === "chamfer") fields = input("distance", "两侧倒角距离");
  if (kind === "repair" || kind === "diagnose") fields = input("tolerance", "修图容差（mm）") + (kind === "repair" ? check("removeDuplicates", "去除重复图形与重叠线段") + check("joinGaps", "连接容差内断口") + check("simplify", "精简曲线与合并连续段") + check("trimSelfIntersections", "裁剪自相交（闭合环拆分，开放线去环）") + check("removeShort", "删除小于容差的短段") : "");
  if (kind === "racetrack") fields = input("x", `左下 ${x}`) + input("y", `左下 ${y}`) + input("width", "宽度") + input("height", "高度");
  if (kind === "polygon" || kind === "star") fields = center() + input("outerRadius", "外接圆半径") + input(kind === "star" ? "points" : "sides", kind === "star" ? "角数" : "边数") + (kind === "star" ? input("innerRadius", "内接圆半径") : "") + input("rotation", "起始角度（°）");
  if (kind === "array-circumferential") fields = input("count", "每圈数量（含原件）") + input("rows", "轴向行数") + input("spacingY", "轴向行距") + `<small>当前周长 ${formatNumber(command.perimeter)} mm；周向间距自动按周长 ÷ 数量计算。</small>`;
  if (kind === "end-cuts") fields = input("start", "起端位置 S") + input("end", "末端位置 S") + input("startAngle", "起端斜切角（°）") + input("startRotation", "起端方向（°）") + input("endAngle", "末端斜切角（°）") + input("endRotation", "末端方向（°）");
  if (kind === "split-parts") fields = check("splitParts", "按分离实体生成多个下料零件") + "<small>确认整个草图时按实际实体分件，预览可检查分件结果。</small>";
  if (kind === "text-outline") fields = input("text", "文字内容") + input("font", "字体") + input("textHeight", "字高") + input("x", `起点 ${x}`) + input("y", `基线 ${y}`);
  const mouse = CAD_TRANSFORMS.has(kind) || CAD_SHAPES.has(kind);
  const issues = command.issues.map((issue, i) => `<button type="button" data-cam-action="tube-designer-sketch-cad-locate" data-cad-issue="${i}">${escapeText(issue.message || issue.kind || "图形问题")}</button>`).join("");
  return `<section class="tube-sketch-cad-panel" data-tube-sketch-cad-panel="${escapeAttr(command.tool)}"><strong>${cadOperationLabel(command)}</strong>${mouse ? `<div class="tube-sketch-cad-actions" role="group" aria-label="操作方式">${["parameters", "mouse"].map(mode => `<button type="button" data-cam-action="tube-designer-sketch-cad-mode" data-cad-mode="${mode}" aria-pressed="${command.mode === mode}">${mode === "mouse" ? "鼠标交互" : "填写参数"}</button>`).join("")}</div>` : ""}<div class="tube-sketch-cad-fields">${fields}</div><small data-tube-sketch-cad-prompt>${escapeText(cadPrompt(command))}</small>${kind === "measure" ? `<output data-tube-sketch-measure-result>${escapeText(command.measurement || "指定两点，显示距离、分量和角度")}</output>` : ""}<div class="tube-sketch-cad-error" data-tube-sketch-cad-error ${command.error ? "" : "hidden"}>${escapeText(command.error)}</div>${issues ? `<div class="tube-sketch-cad-issues">${issues}</div>` : ""}<div class="tube-sketch-cad-actions">${!["measure", "trim", "extend"].includes(kind) ? `<button type="button" class="tube-designer-primary" data-cam-action="tube-designer-sketch-cad-apply">${kind === "diagnose" ? "检查" : "应用"}</button>` : ""}<button type="button" data-cam-action="tube-designer-sketch-cad-cancel">${["measure", "trim", "extend", "diagnose"].includes(kind) ? "完成" : "取消"}</button></div></section>`;
}

export function renderNumericPointPanel(state, arcLengthAxial) {
  if (!["line", "polyline", "rectangle", "circle", "ellipse", "arc", "spline"].includes(state.tool)) return "";
  const values = state.cadInput ?? {}, x = arcLengthAxial ? "U" : "X", y = arcLengthAxial ? "S" : "Y";
  const field = (key, label, value = "") => `<label class="tube-designer-field"><span>${label}</span><input type="text" inputmode="decimal" value="${escapeAttr(values[key] ?? value)}" data-tube-sketch-numeric-field="${key}" data-cam-change-action="tube-designer-sketch-numeric-field" autocomplete="off"/></label>`;
  return `<section class="tube-sketch-cad-panel tube-sketch-cad-input" data-tube-sketch-numeric-panel><strong>精确输入（mm）</strong><label class="tube-designer-field"><span>输入方式</span><select data-tube-sketch-numeric-field="mode" data-cam-change-action="tube-designer-sketch-numeric-field">${Object.entries({ absolute: "绝对坐标", relative: "相对上一点", polar: "长度与角度" }).map(([v, l]) => `<option value="${v}" ${v === (values.mode || "absolute") ? "selected" : ""}>${l}</option>`).join("")}</select></label><div class="tube-sketch-cad-fields">${field("x", x)}${field("y", y)}${field("length", state.tool === "circle" ? "半径 / 长度" : "长度")}${field("angle", "角度（°）", "0")}</div><small>绝对/相对坐标填写两坐标；长度与角度从上一点出发。Enter 指定点。</small><div class="tube-sketch-cad-error" data-tube-sketch-numeric-error ${state.cadInputError ? "" : "hidden"}>${escapeText(state.cadInputError || "")}</div><button type="button" data-cam-action="tube-designer-sketch-numeric-apply">指定点</button></section>`;
}

export function numericPoint(state) {
  const s = state.cadInput ?? {}, base = state.command?.points?.at(-1) || [0, 0];
  if (s.mode === "polar") { const l = cadNumber(s.length), angle = cadNumber(s.angle ?? 0) * Math.PI / 180; if (l < 0) throw new Error("长度不能为负"); return [base[0] + l * Math.cos(angle), base[1] + l * Math.sin(angle)]; }
  const point = [cadNumber(s.x), cadNumber(s.y)];
  return s.mode === "relative" ? [base[0] + point[0], base[1] + point[1]] : point;
}
