import { invokeGeometry } from "./editingTarget.mjs";
import { PlanarEditorSession, planarNumberPattern, planarPointPattern, planarToolNames } from "../../../iCAX-UI/SDK/Cad/planarEditorSession.mjs";
import { PlanarPolylineSession, polylineSegmentKinds } from "../../../iCAX-UI/SDK/Cad/planarCurveSegments.mjs";
import { PlanarPrimitiveSession, standalonePrimitiveDefinitions, primitiveInputOptions, standaloneScalarInput } from "../../../iCAX-UI/SDK/Cad/planarDrawingPrimitives.mjs";
import { getObjectRows, getSelectedObject, getWorkspaceState, selectObject, isGeometryEditing, geometryMethod, geometryObjectKind } from "./workspaceState.mjs";
import { invokePlane, supports } from "./nativeOperations.mjs";
import { clearDrawingPreview, updateDrawingPreview } from "./drawingPreview.mjs";
import { clearDrawingDynamicInput, constrainDrawingDimensionPoint, drawingArcParameters, ensureDrawingDynamicInput, recordDrawingDimensionPointer, rejectDrawingArcSweep, setDrawingAuxiliaryDimensions, synchronizeDrawingDynamicInput } from "./drawingDynamicInput.mjs";
import { arcPoint, arcSweep } from "./arcDimensionValues.mjs";

const captions = { line: "依次指定两个端点", polyline: "指定起点，选择直线、圆弧、椭圆弧、贝塞尔或B样条曲段", rect: "指定矩形两个对角点", circle: "指定圆心，再指定半径点", arc: "指定圆心、起点和终点", ...Object.fromEntries(Object.entries(standalonePrimitiveDefinitions).map(([key, definition]) => [key, definition.steps[0]])) };
const numberPattern = planarNumberPattern, pointPattern = planarPointPattern, shapeNames = planarToolNames;

function requirePolylineCapability(view, mode = "line") {
  const types = getWorkspaceState(view).geometryCapabilities?.segmentTypes;
  if (!Array.isArray(types) || !types.length) throw new Error("原生模块未声明多义线曲段能力。");
  if (!types.includes(mode)) throw new Error("原生模块未声明此多义线曲段类型。");
}

export function drawingStep(view) {
  const ui = getWorkspaceState(view), primitive = ui.activeTool.replace("plane.draw-", ""), count = ui.commandPoints.length;
  const steps = { line: ["指定起点", "指定终点"], rect: ["指定第一个对角点", "指定另一个对角点"], circle: ["指定圆心", "指定半径点，或输入半径"], arc: ["指定圆心", "指定圆弧起点，或输入半径", "指定圆弧终点，或输入终止角度"], polyline: ["指定第一个顶点", "继续指定顶点，C 闭合，Enter 开放完成"] };
  const polyline = primitive === "polyline" ? new PlanarPolylineSession(ui) : null;
  const standalone = standalonePrimitiveDefinitions[primitive] ? new PlanarPrimitiveSession(primitive, ui.commandPoints) : null;
  return { primitive, name: shapeNames[primitive], count, label: polyline ? polyline.step() : standalone ? standalone.step() : steps[primitive]?.[Math.min(count, steps[primitive].length - 1)] ?? "",
    scalar: ["circle", "arc", "ellipse", "ellipseArc", "polygon", "star"].includes(primitive) && count > 0,
    canFinish: primitive === "bspline" ? count >= 4 : primitive === "polyline" && (!polyline.pending.length && polyline.segments.length > 0 || polyline.mode === "bspline" && polyline.pending.length >= 3),
    canClose: primitive === "polyline" && (!polyline.pending.length && (polyline.segments.some(segment => segment.type !== "line") || count >= 3) || polyline.mode === "bspline" && polyline.pending.length >= 3),
    inputLabel: primitive === "ellipseArc" && count >= 3 ? "扫过角 θ（可为负 °）或椭圆终点 X,Y"
      : ["ellipse", "ellipseArc"].includes(primitive) && count > 0 ? `${count === 1 ? "第一半轴 A" : "另一半轴 B"}（mm）或 X,Y`
      : primitive === "star" && count > 0 ? `${count === 1 ? "外半径 R" : "内半径 r"}（mm）或 X,Y`
      : primitive === "polygon" && count > 0 ? "外接圆半径 R（mm）或 X,Y"
      : primitive === "arc" && count >= 2 ? "终止角（绝对 °）或 X,Y" : ["circle", "arc"].includes(primitive) && count === 1 ? "半径（mm）或 X,Y" : "坐标 X,Y（mm）" };
}

function nextShapeName(context, view, primitive) {
  const prefix = shapeNames[primitive] ?? "图形", names = new Set(getObjectRows(context, view, geometryObjectKind(view)).map(row => row.name));
  let index = 1;while (names.has(`${prefix} ${index}`)) index++;
  return `${prefix} ${index}`;
}

function resetPreview(view) {
  const ui = getWorkspaceState(view);ui.drawGesture = null;ui.pointerPoint = null;
  clearDrawingDynamicInput(view);
  clearDrawingPreview(view);view.viewport?.clearMeasurementPoints();
}

function endDrawingCommand(view, status) {
  const ui = getWorkspaceState(view);
  ui.commandPoints = [];new PlanarPolylineSession(ui).begin();ui.activeTool = "plane.select";ui.snapRequest = (ui.snapRequest ?? 0) + 1;
  resetPreview(view);ui.commandStatus = status;
  if (view.viewport?.root) delete view.viewport.root.dataset.iplaneDrawingTool;
}

export const parsePoint = PlanarEditorSession.parsePoint;
const pointsToPayload = PlanarEditorSession.primitivePayload;

export function refreshDrawingSnapCandidates(context, view) {
  const ui = getWorkspaceState(view);
  if (!(ui.activeTool.startsWith("plane.draw-") || ui.activeTool === "plane.text") || !supports(context, view, geometryMethod(view, "ReadGeometry"))) return;
  const rows = getObjectRows(context, view, geometryObjectKind(view)).filter(row => row.visible !== false && row.locked !== true);
  const signature = JSON.stringify(rows.map(row => [row.id, row.geometryVersion, row.geometryRef?.url, row.geometryRef?.version]).sort((a, b) => a[0].localeCompare(b[0])));
  if (signature === ui.snapSignature) return;
  ui.snapSignature = signature;ui.snapCandidates = [];
  const version = ui.snapRequest = (ui.snapRequest ?? 0) + 1;
  // The tool becomes usable immediately; snap data may arrive independently.
  void Promise.allSettled(rows.map(row => context.sceneProxy.invoke(geometryMethod(view, "ReadGeometry"), { id: row.id }, { timeoutMs: 30000 }))).then(results => {
    if (version !== ui.snapRequest || signature !== ui.snapSignature || view.disposed || context.isCurrentProject?.() === false) return;
    ui.snapCandidates = results.flatMap(result => result.status === "fulfilled" ? result.value.snapPoints ?? [] : []);
  });
}

export async function beginDrawing(context, view, primitive) {
  if (primitive === "polyline") requirePolylineCapability(view);
  const ui = getWorkspaceState(view);new PlanarEditorSession(ui).begin();
  new PlanarPolylineSession(ui).begin();
  resetPreview(view);
  ui.activeTool = `plane.draw-${primitive}`;ui.panelMode = "object";ui.commandInput = "";
  ui.operationError = "";ui.commandStatus = `${captions[primitive] ?? primitive}，Esc 取消。`;
  if (view.viewport?.root) view.viewport.root.dataset.iplaneDrawingTool = primitive;
  refreshDrawingSnapCandidates(context, view);
}

export function drawingPointerPoint(view, event, suppliedOrigin) {
  recordDrawingDimensionPointer(view,event);const ui = getWorkspaceState(view);
  return new PlanarEditorSession(ui).pointerPoint(view.viewport,event,{ primitive: ui.activeTool.replace("plane.draw-", ""), origin: suppliedOrigin ?? (ui.activeTool === "plane.draw-polyline" ? ui.polylinePending?.at(-1) : undefined) });
}

function updatePolylinePreview(view, pointer, closed = false) {
  const ui = getWorkspaceState(view), polyline = new PlanarPolylineSession(ui), preview = polyline.preview(pointer);
  updateDrawingPreview(view, "polyline", ui.commandPoints, pointer, { segments: preview.segments, controls: preview.controls, closed });
  setDrawingAuxiliaryDimensions(view, polyline.dimensions(pointer));
  synchronizeDrawingDynamicInput(view);
}

export function setPolylineMode(context, view, mode) {
  const ui = getWorkspaceState(view);
  if (ui.activeTool !== "plane.draw-polyline" || ui.busy || ui.drawingCommitting) return false;
  requirePolylineCapability(view, mode);
  new PlanarPolylineSession(ui).setMode(mode);
  ui.commandStatus = drawingStep(view).label;ui.operationError = "";
  updatePolylinePreview(view, ui.pointerPoint);return true;
}

export function finishPolylineSegment(view) {
  const ui = getWorkspaceState(view);
  if (ui.activeTool !== "plane.draw-polyline" || ui.busy || ui.drawingCommitting) return false;
  const changed = new PlanarPolylineSession(ui).commitPending();
  ui.commandStatus = drawingStep(view).label;ui.operationError = "";
  updatePolylinePreview(view, null);return changed;
}

export function undoPolylineInput(view) {
  const ui = getWorkspaceState(view);
  if (ui.activeTool !== "plane.draw-polyline" || ui.busy || ui.drawingCommitting) return false;
  const changed = new PlanarPolylineSession(ui).undo();
  if (changed) { ui.commandStatus = drawingStep(view).label;ui.operationError = "";updatePolylinePreview(view, null); }
  return changed;
}

export const polylineModeShortcuts = Object.freeze(Object.fromEntries(polylineSegmentKinds.map(([kind, , key]) => [key, kind])));

export function previewDrawingPoint(view, point) {
  point = constrainDrawingDimensionPoint(view, point);
  const ui = getWorkspaceState(view);ui.pointerPoint = point;
  const accepted = ui.commandPoints.length ? ui.commandPoints : ui.drawGesture?.moved ? [ui.drawGesture.point] : [];
  const primitive = ui.activeTool.replace("plane.draw-", "");
  if (primitive === "polyline") {
    updatePolylinePreview(view, point);return;
  }
  const arc = primitive === "arc" ? drawingArcParameters(view) : null;
  if (arc && (ui.dynamicInput.phase === "sweep" || ui.dynamicInput.editing)) updateDrawingPreview(view, "arc", [arc.center, arcPoint(arc.center, arc.radius, arc.startAngleDeg), arcPoint(arc.center, arc.radius, arc.startAngleDeg + arc.sweepAngleDeg)], null, { arc });
  else updateDrawingPreview(view, primitive, accepted, point);
  if (["line", "polyline", "triangle"].includes(primitive)) {
    setDrawingAuxiliaryDimensions(view, PlanarEditorSession.segmentDimensions(primitive === "triangle" ? "polyline" : primitive,accepted,point));
  } else if (!ui.dynamicInput && accepted.length && ["circle", "rect"].includes(primitive)) {
    const [x, y] = accepted[0], dx = point[0] - x, dy = point[1] - y;
    setDrawingAuxiliaryDimensions(view, primitive === "circle" ? [{ key: "radius", label: "R", kind: "radius", value: Math.hypot(dx, dy), start: [x, y, 0], end: [point[0], point[1], 0] }]
      : [{ key: "width", label: "W", kind: "linear", value: Math.abs(dx), start: [x, y, 0], end: [point[0], y, 0], offsetDirection: [0, -(Math.sign(dy) || 1), 0] },
        { key: "height", label: "H", kind: "linear", value: Math.abs(dy), start: [x, y, 0], end: [x, point[1], 0], offsetDirection: [-(Math.sign(dx) || 1), 0, 0] }]);
  }
  synchronizeDrawingDynamicInput(view);
}

export async function acceptDrawingPointerPoint(context, view, ops, point) {
  if (getWorkspaceState(view).activeTool === "plane.draw-polyline") return acceptDrawingPoint(context, view, ops, point, false, false, { pointerInput: true });
  const constrained = constrainDrawingDimensionPoint(view, point, { commit: true });
  if (!constrained) return false;
  const ui = getWorkspaceState(view), arc = drawingArcParameters(view);
  if (arc && ui.commandPoints.length >= 2) return acceptDrawingArcParameters(context, view, ops, arc);
  return acceptDrawingPoint(context, view, ops, constrained, false, false, { pointerInput: true, ...(arc ? { arcStart: arc } : {}) });
}

export function acceptDrawingArcStart(context, view, ops, parameters) {
  const ui = getWorkspaceState(view), center = ui.commandPoints[0];
  if (ui.activeTool !== "plane.draw-arc" || !center || ui.busy) return false;
  ui.commandPoints = [[...center], arcPoint(center, parameters.radius, parameters.startAngleDeg)];
  ensureDrawingDynamicInput(view, parameters);
  ui.commandStatus = "指定终点或输入扫过角 θ（°）。";
  previewDrawingPoint(view, arcPoint(center, parameters.radius, parameters.startAngleDeg + parameters.sweepAngleDeg));return false;
}

export async function acceptDrawingArcParameters(context, view, ops, parameters) {
  const ui = getWorkspaceState(view), center = ui.commandPoints[0];
  if (ui.activeTool !== "plane.draw-arc" || !center || ui.busy) return false;
  const { radius, startAngleDeg, sweepAngleDeg } = parameters;
  if (sweepAngleDeg < 360 && arcSweep(startAngleDeg, startAngleDeg + sweepAngleDeg) === 360)
    return rejectDrawingArcSweep(view, "扫过角过小，无法表示，请增大角度。");
  ui.commandPoints = [[...center], arcPoint(center, radius, startAngleDeg)];ensureDrawingDynamicInput(view, parameters);
  return acceptDrawingPoint(context, view, ops, arcPoint(center, radius, startAngleDeg + sweepAngleDeg), false, false,
    { payload: { primitive: "arc", center: [...center], radius, startAngleDeg, endAngleDeg: sweepAngleDeg === 360 ? startAngleDeg : startAngleDeg + sweepAngleDeg } });
}

export async function acceptDrawingPoint(context, view, ops, point, finish = false, closed = false, options = {}) {
  const ui = getWorkspaceState(view), primitive = ui.activeTool.replace("plane.draw-", "");
  if (!isGeometryEditing(view)) throw new Error("请在底图中绘制图形。");
  if (!captions[primitive]) throw new Error("请选择绘图工具。");
  if (ui.busy || ui.drawingCommitting) return false;
  if (primitive === "polyline") requirePolylineCapability(view, ui.polylineMode);
  ui.commandPoints ??= [];
  const polyline = primitive === "polyline" ? new PlanarPolylineSession(ui) : null;
  if (primitive === "polyline" && point && !polyline.pending.length && polyline.mode === "line" && ui.commandPoints.length >= 3
      && Math.hypot(point[0] - ui.commandPoints[0][0], point[1] - ui.commandPoints[0][1]) < 1e-9) {
    point = null;finish = true;closed = true;
  }
  if (polyline) {
    if (point) polyline.accept(point, { pointer: options.pointerInput === true });
    if (!finish) { ui.commandStatus = polyline.step();updatePolylinePreview(view, null);return false; }
    options = { ...options, payload: polyline.payload(closed) };
  } else if (point) {
    ui.commandPoints.push(point);
    try { if (standalonePrimitiveDefinitions[primitive]) new PlanarPrimitiveSession(primitive, ui.commandPoints).validateConstruction(); }
    catch (error) { ui.commandPoints.pop();throw error; }
  }
  if (closed && primitive !== "polyline") throw new Error("请选择多义线后闭合。");
  const required = standalonePrimitiveDefinitions[primitive]?.points ?? (primitive === "arc" || closed ? 3 : 2);
  if (primitive === "bspline" && finish && ui.commandPoints.length < 4) throw new Error("B样条至少需要4个控制点，再右键选择结束。");
  if (!polyline && !(primitive === "bspline" && finish) && ui.commandPoints.length < required) {
    ensureDrawingDynamicInput(view, options.arcStart);
    ui.commandStatus = `${drawingStep(view).label}；已输入 ${ui.commandPoints.length} 个点。`;
    updateDrawingPreview(view, primitive, ui.commandPoints, null);
    return false;
  }
  const tool = ui.activeTool, epoch = ui.drawingEpoch;
  const panelEpoch = ui.panelEpoch ?? 0;
  ui.drawingCommitting = true;
  if (closed) updatePolylinePreview(view, null, true);
  try {
    const primitiveOptions = { ...primitiveInputOptions(primitive, ui.drawingShapeOptions?.[primitive], ui.dynamicInput), ...options };
    const response = await invokeGeometry(context, view, ops, "Create", { ...(options.payload ?? pointsToPayload(primitive, ui.commandPoints, closed, primitiveOptions)), name: nextShapeName(context, view, primitive) }, { isErrorCurrent: () => ui.drawingEpoch === epoch });
    if (isGeometryEditing(view) && ui.activeTool === tool && ui.drawingEpoch === epoch) {
      // A list selection during Create must survive, but the submitted prefix
      // has been consumed regardless of which properties panel is now shown.
      if (response?.id && (ui.panelEpoch ?? 0) === panelEpoch) selectObject(view, geometryObjectKind(view), String(response.id));
      ui.drawingCreatedCount = (ui.drawingCreatedCount ?? 0) + 1;
      if ((primitive === "polyline" || primitive === "bspline") && finish) endDrawingCommand(view, closed ? "已闭合多段线。" : `已结束${shapeNames[primitive]}。`);
      else { ui.commandPoints = [];resetPreview(view);ui.commandStatus = `${captions[primitive]}，Esc 取消。`; }
    }
    return true;
  } catch (error) {
    if (ui.drawingEpoch !== epoch) return false;
    // Keep the accepted prefix, so an invalid end point can be corrected.
    if (ui.drawingEpoch === epoch && primitive !== "polyline" && point) ui.commandPoints.pop();
    if (ui.drawingEpoch === epoch) {
      if (polyline) updatePolylinePreview(view, null);else updateDrawingPreview(view, primitive, ui.commandPoints, null);
    }
    throw error;
  } finally {
    ui.drawingCommitting = false;
  }
}

export async function finishDrawing(context, view, ops) {
  const ui = getWorkspaceState(view);
  if (["plane.draw-polyline", "plane.draw-bspline"].includes(ui.activeTool)) return acceptDrawingPoint(context, view, ops, null, true);
  if (!ui.activeTool.startsWith("plane.draw-") || ui.busy) return false;
  endDrawingCommand(view, "已结束绘图。");return true;
}

export function undoStandaloneDrawingInput(view) {
  const ui = getWorkspaceState(view), primitive = ui.activeTool.replace("plane.draw-", "");
  if (!standalonePrimitiveDefinitions[primitive] || ui.busy || ui.drawingCommitting || !ui.commandPoints.length) return false;
  ui.commandPoints.pop();clearDrawingDynamicInput(view);ensureDrawingDynamicInput(view);
  ui.commandStatus = drawingStep(view).label;ui.operationError = "";
  if (ui.pointerPoint) previewDrawingPoint(view, ui.pointerPoint);else updateDrawingPreview(view, primitive, ui.commandPoints, null);
  return true;
}

export function canCloseSelectedPolyline(context, view) {
  const selected = getSelectedObject(context, view);
  return isGeometryEditing(view) && selected?.kind === geometryObjectKind(view) && selected.row.canClosePolyline === true
    && supports(context, view, geometryMethod(view, "Edit"));
}

export async function closePolyline(context, view, ops) {
  const ui = getWorkspaceState(view);
  if (ui.activeTool === "plane.draw-polyline") return acceptDrawingPoint(context, view, ops, null, true, true);
  if (ui.activeTool !== "plane.select" || !canCloseSelectedPolyline(context, view)) throw new Error("请选择可闭合的开放多段线。");
  const selected = getSelectedObject(context, view), epoch = ui.drawingEpoch, panelEpoch = ui.panelEpoch ?? 0;
  try {
    await invokeGeometry(context, view, ops, "Edit", { id: selected.row.id, operation: "close" }, { isErrorCurrent: () => ui.drawingEpoch === epoch && (ui.panelEpoch ?? 0) === panelEpoch });
    if (ui.drawingEpoch === epoch && (ui.panelEpoch ?? 0) === panelEpoch) ui.commandStatus = "已闭合所选轮廓。";
    return true;
  } catch (error) {
    if (ui.drawingEpoch !== epoch || (ui.panelEpoch ?? 0) !== panelEpoch) return false;
    throw error;
  }
}

export function drawingDragKey(view) {
  const ui = getWorkspaceState(view);
  return isGeometryEditing(view) && !ui.busy && !ui.drawingCommitting && ui.commandPoints.length <= 1
    && ["plane.draw-line", "plane.draw-rect", "plane.draw-circle"].includes(ui.activeTool)
    ? `${ui.activeTool}:${ui.drawingEpoch}` : null;
}

export async function acceptDrawingDrag(context, view, ops, event) {
  const ui = getWorkspaceState(view), gesture = ui.drawGesture;ui.drawGesture = null;
  if (!gesture || gesture.epoch !== ui.drawingEpoch || gesture.tool !== ui.activeTool) return false;
  const origin = ui.commandPoints[0] ?? gesture.point;
  const point = drawingPointerPoint(view, event, origin);if (!point) return false;
  if (!ui.commandPoints.length) ui.commandPoints.push(origin);
  return acceptDrawingPointerPoint(context, view, ops, point);
}

export function clearDrawingGesture(view) {
  const ui = getWorkspaceState(view);ui.drawGesture = null;ui.pointerPoint = null;
  if (ui.activeTool === "plane.draw-polyline" && ui.commandPoints.length) updatePolylinePreview(view, null);
  else if (ui.commandPoints.length) updateDrawingPreview(view, ui.activeTool.replace("plane.draw-", ""), ui.commandPoints, null);
  else clearDrawingPreview(view);
}

function numericArguments(text) {
  const residual = text.replace(new RegExp(numberPattern, "ig"), "").replace(/[\s,，]/g, "");
  const values = (text.match(new RegExp(numberPattern, "ig")) ?? []).map(Number);
  if (residual || !values.every(Number.isFinite)) throw new Error("命令参数只能包含数值、逗号和空格。");
  return values;
}

export async function runDrawingInput(context, view, ops, text) {
  if (!isGeometryEditing(view)) throw new Error("请在底图页输入绘图和编辑命令。");
  const ui = getWorkspaceState(view);text = String(text).trim();
  if (!text) {
    if (["plane.draw-polyline", "plane.draw-bspline"].includes(ui.activeTool)) return acceptDrawingPoint(context, view, ops, null, true);
    return false;
  }
  if (ui.activeTool === "plane.draw-polyline") {
    const mode = { L: "line", A: "arc", E: "ellipseArc", B: "bezier", S: "bspline", "直线段": "line", "圆弧段": "arc", "椭圆弧段": "ellipseArc", "贝塞尔段": "bezier", "B样条段": "bspline" }[text.toUpperCase()] ?? { "直线段": "line", "圆弧段": "arc", "椭圆弧段": "ellipseArc", "贝塞尔段": "bezier", "B样条段": "bspline" }[text];
    if (mode) return setPolylineMode(context, view, mode);
    if (text === "完成段") return finishPolylineSegment(view);
  }
  if (/^(c|闭合)$/i.test(text)) return closePolyline(context, view, ops);
  if ((text.startsWith("@") || pointPattern.test(text)) && ui.activeTool.startsWith("plane.draw-")) return acceptDrawingPoint(context, view, ops, parsePoint(text, (ui.activeTool === "plane.draw-polyline" ? ui.polylinePending?.at(-1) : null) ?? ui.commandPoints?.at(-1) ?? [0, 0]));
  if (new RegExp(`^${numberPattern}$`, "i").test(text) && drawingStep(view).scalar) {
    const value = Number(text), center = ui.commandPoints[0];
    if (!Number.isFinite(value)) throw new Error("输入值必须为有限数值。");
    if (standalonePrimitiveDefinitions[ui.activeTool.slice(11)]) {
      const input = standaloneScalarInput(ui.activeTool.slice(11), ui.commandPoints, value, ui.pointerPoint);
      return acceptDrawingPoint(context, view, ops, input.point, false, false, input.options);
    }
    if (ui.commandPoints.length === 1) {
      if (!(value > 0)) throw new Error("半径必须大于零。");
      return acceptDrawingPoint(context, view, ops, [center[0] + value, center[1]]);
    }
    const radius = Math.hypot(ui.commandPoints[1][0] - center[0], ui.commandPoints[1][1] - center[1]), angle = value * Math.PI / 180;
    return acceptDrawingPoint(context, view, ops, [center[0] + radius * Math.cos(angle), center[1] + radius * Math.sin(angle)]);
  }
  const [name, ...rest] = text.split(/\s+/), operation = { "直线": "line", "矩形": "rect", "圆": "circle", "圆弧": "arc", "折线": "polyline", "多义线": "polyline", "多段线": "polyline", "椭圆": "ellipse", "椭圆弧": "ellipseArc", "贝塞尔": "bezier", "B样条": "bspline", "三角形": "triangle", "正多边形": "polygon", "星形": "star", "移动": "move", "旋转": "rotate", "缩放": "scale", "删除": "delete" }[name] ?? Object.keys(shapeNames).find(key => key.toLowerCase() === name.toLowerCase()) ?? name.toLowerCase();
  const values = numericArguments(rest.join(" "));
  let payload;
  if (operation === "line" && values.length === 4) payload = { primitive: "line", points: [values.slice(0, 2), values.slice(2)] };
  if (operation === "rect" && values.length === 4) payload = { primitive: "rect", points: [values.slice(0, 2)], width: values[2], height: values[3] };
  if (operation === "circle" && values.length === 3) payload = { primitive: "circle", center: values.slice(0, 2), radius: values[2] };
  if (operation === "arc" && values.length === 5) payload = { primitive: "arc", center: values.slice(0, 2), radius: values[2], startAngleDeg: values[3], endAngleDeg: values[4] };
  if (operation === "polyline" && values.length >= 4 && values.length % 2 === 0) payload = { primitive: "polyline", points: Array.from({ length: values.length / 2 }, (_, i) => values.slice(i * 2, i * 2 + 2)), closed: false };
  if (operation === "ellipse" && [4, 5].includes(values.length)) payload = { primitive: operation, center: values.slice(0, 2), radiusX: values[2], radiusY: values[3], rotationDeg: values[4] ?? 0 };
  if (operation === "ellipseArc" && [6, 7].includes(values.length)) payload = { primitive: operation, center: values.slice(0, 2), radiusX: values[2], radiusY: values[3], rotationDeg: values.length === 7 ? values[4] : 0, startAngleDeg: values.at(-2), sweepAngleDeg: values.at(-1) };
  if (operation === "triangle" && values.length === 6) payload = { primitive: operation, points: [values.slice(0, 2), values.slice(2, 4), values.slice(4)] };
  if (operation === "polygon" && [4, 5].includes(values.length)) payload = { primitive: operation, center: values.slice(0, 2), radius: values[2], sides: values[3], rotationDeg: values[4] ?? 0 };
  if (operation === "star" && [5, 6].includes(values.length)) payload = { primitive: operation, center: values.slice(0, 2), outerRadius: values[2], innerRadius: values[3], tips: values[4], rotationDeg: values[5] ?? 0 };
  if ((operation === "bezier" && values.length === 8 || operation === "bspline" && values.length >= 8 && values.length % 2 === 0)) payload = { primitive: operation, controlPoints: Array.from({ length: values.length / 2 }, (_, i) => values.slice(i * 2, i * 2 + 2)), ...(operation === "bspline" ? { degree: 3 } : {}) };
  if (payload) {
    if (payload.primitive === "polyline") requirePolylineCapability(view);
    const epoch = ui.drawingEpoch, panelEpoch = ui.panelEpoch ?? 0;
    let response;
    try {
      response = await invokeGeometry(context, view, ops, "Create", { ...payload, name: nextShapeName(context, view, payload.primitive) }, { isErrorCurrent: () => ui.drawingEpoch === epoch });
    } catch (error) {
      if (ui.drawingEpoch !== epoch) return false;
      throw error;
    }
    if (ui.drawingEpoch === epoch) {
      if (response?.id && (ui.panelEpoch ?? 0) === panelEpoch) selectObject(view, geometryObjectKind(view), String(response.id));
      if (ui.activeTool.startsWith("plane.draw-")) ui.drawingCreatedCount = (ui.drawingCreatedCount ?? 0) + 1;
      ui.commandPoints = [];new PlanarPolylineSession(ui).begin();resetPreview(view);
    }
    return true;
  }
  const selected = getSelectedObject(context, view);
  if (["move", "rotate", "scale", "delete"].includes(operation)) {
    if (selected?.kind !== geometryObjectKind(view)) throw new Error("请先选择要编辑的底图图形。");
    const args = operation === "move" && values.length === 2 ? { dx: values[0], dy: values[1] } : operation === "rotate" && values.length === 1 ? { angleDeg: values[0] } : operation === "scale" && values.length === 1 ? { factor: values[0] } : operation === "delete" && !values.length ? {} : null;
    if (!args) throw new Error("移动输入 dx,dy；旋转输入角度；缩放输入倍数；删除不需要参数。");
    await invokeGeometry(context, view, ops, "Edit", { id: selected.row.id, operation, ...args });return true;
  }
  throw new Error("命令示例：直线 0,0 100,0；矩形 0,0 100,50；圆 50,50 20；移动 10,0。");
}

export function cancelDrawing(view) {
  const ui = getWorkspaceState(view);new PlanarEditorSession(ui).cancel();ui.activeTool = "plane.select";resetPreview(view);
  if (view.viewport?.root) delete view.viewport.root.dataset.iplaneDrawingTool;
  ui.commandStatus = "已取消当前输入，返回选择。";
}
