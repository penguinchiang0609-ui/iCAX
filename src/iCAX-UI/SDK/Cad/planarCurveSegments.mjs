// Exact curve parameters stay separate from display sampling. Circular
// construction follows the shared sketch editor's three-point circumcircle.
const EPS = 1e-9, TAU = Math.PI * 2, rad = value => value * Math.PI / 180;
const clone = value => structuredClone(value), mix = (a, b, t) => a.map((value, axis) => value + (b[axis] - value) * t);
export const pointDistance = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
export const polylineSegmentKinds = Object.freeze([
  ["line", "直线", "L"], ["arc", "圆弧", "A"], ["ellipseArc", "椭圆弧", "E"],
  ["bezier", "贝塞尔", "B"], ["bspline", "B样条", "S"],
]);
export function arcThroughPoints(start, middle, end) {
  const ax = middle[0] - start[0], ay = middle[1] - start[1], bx = end[0] - start[0], by = end[1] - start[1];
  const determinant = 2 * (ax * by - ay * bx), scale = Math.max(ax * ax + ay * ay, bx * bx + by * by);
  if (Math.min(pointDistance(start, middle), pointDistance(middle, end), pointDistance(start, end)) < EPS || Math.abs(determinant) <= scale * 1e-10) return null;
  const a2 = ax * ax + ay * ay, b2 = bx * bx + by * by;
  const center = [start[0] + (by * a2 - ay * b2) / determinant, start[1] + (ax * b2 - bx * a2) / determinant];
  const angle = point => Math.atan2(point[1] - center[1], point[0] - center[0]), mod = value => (value % TAU + TAU) % TAU;
  const first = angle(start), last = angle(end), through = angle(middle), ccw = mod(last - first);
  return { type: "arc", center, radius: pointDistance(start, center), startAngleDeg: first * 180 / Math.PI,
    sweepAngleDeg: (mod(through - first) < ccw ? ccw : ccw - TAU) * 180 / Math.PI };
}
export function clampedUniformKnots(count, degree) {
  return Array.from({ length: count + degree + 1 }, (_, index) => index <= degree ? 0 : index >= count ? 1 : (index - degree) / (count - degree));
}
function deBoor(points, degree, knots, parameter) {
  const n = points.length - 1;
  let span = n;
  if (parameter < knots[n + 1]) for (let index = degree; index <= n; index++) if (parameter >= knots[index] && parameter < knots[index + 1]) { span = index;break; }
  const work = Array.from({ length: degree + 1 }, (_, index) => [...points[span - degree + index]]);
  for (let level = 1; level <= degree; level++) for (let index = degree; index >= level; index--) {
    const pole = span - degree + index, denominator = knots[pole + degree - level + 1] - knots[pole];
    const alpha = denominator ? (parameter - knots[pole]) / denominator : 0;
    work[index] = mix(work[index - 1], work[index], alpha);
  }
  return work[degree];
}
function splineData(segment) {
  const points = segment.controlPoints, degree = segment.degree ?? Math.min(3, points.length - 1), knots = segment.knots ?? clampedUniformKnots(points.length, degree);
  const poles = points.map((point, index) => { const weight = segment.weights?.[index] ?? 1;return [point[0] * weight, point[1] * weight, weight]; });
  return { points, degree, knots, poles, first: knots[degree], last: knots[points.length] };
}
export function curveSegmentPoint(segment, parameter) {
  if (segment.type === "line") return mix(segment.points[0], segment.points[1], parameter);
  if (segment.type === "bezier") {
    let level = segment.controlPoints.map(point => [...point]);
    while (level.length > 1) level = level.slice(1).map((point, index) => mix(level[index], point, parameter));
    return level[0];
  }
  if (segment.type === "bspline") {
    const data = splineData(segment), value = deBoor(data.poles, data.degree, data.knots, data.first + (data.last - data.first) * parameter);
    return [value[0] / value[2], value[1] / value[2]];
  }
  const angle = rad(segment.startAngleDeg + segment.sweepAngleDeg * parameter), rotation = rad(segment.rotationDeg ?? 0);
  const x = (segment.radius ?? segment.radiusX) * Math.cos(angle), y = (segment.radius ?? segment.radiusY) * Math.sin(angle);
  return [segment.center[0] + x * Math.cos(rotation) - y * Math.sin(rotation), segment.center[1] + x * Math.sin(rotation) + y * Math.cos(rotation)];
}
export function curveSegmentTangent(segment, parameter) {
  if (segment.type === "line") return segment.points[1].map((value, axis) => value - segment.points[0][axis]);
  if (segment.type === "bezier") {
    const degree = segment.controlPoints.length - 1;
    return curveSegmentPoint({ type: "bezier", controlPoints: segment.controlPoints.slice(1).map((point, index) => point.map((value, axis) => degree * (value - segment.controlPoints[index][axis]))) }, parameter);
  }
  if (segment.type === "bspline") {
    const data = splineData(segment), u = data.first + (data.last - data.first) * parameter;
    const value = deBoor(data.poles, data.degree, data.knots, u);
    const derivatives = data.poles.slice(1).map((point, index) => {
      const denominator = data.knots[index + data.degree + 1] - data.knots[index + 1];
      return point.map((coordinate, axis) => denominator ? data.degree * (coordinate - data.poles[index][axis]) / denominator : 0);
    });
    const derivative = deBoor(derivatives, data.degree - 1, data.knots.slice(1, -1), u);
    return [0, 1].map(axis => (derivative[axis] * value[2] - value[axis] * derivative[2]) / (value[2] * value[2]) * (data.last - data.first));
  }
  const angle = rad(segment.startAngleDeg + segment.sweepAngleDeg * parameter), rotation = rad(segment.rotationDeg ?? 0), sweep = rad(segment.sweepAngleDeg);
  const x = -(segment.radius ?? segment.radiusX) * Math.sin(angle) * sweep, y = (segment.radius ?? segment.radiusY) * Math.cos(angle) * sweep;
  return [x * Math.cos(rotation) - y * Math.sin(rotation), x * Math.sin(rotation) + y * Math.cos(rotation)];
}
export function curveSegmentSamples(segment) {
  const count = segment.type === "line" ? 1 : ["arc", "ellipseArc"].includes(segment.type) ? Math.max(8, Math.ceil(Math.abs(segment.sweepAngleDeg) / 360 * 128)) : Math.max(48, (segment.controlPoints?.length ?? 4) * 12);
  return Array.from({ length: count + 1 }, (_, index) => curveSegmentPoint(segment, index / count));
}
function segmentLength(segment) {
  if (segment.type === "line") return pointDistance(...segment.points);
  if (segment.type === "arc") return Math.abs(rad(segment.sweepAngleDeg) * segment.radius);
  // Display-only numerical integration of the analytic derivative.
  const count = 48, speed = t => Math.hypot(...curveSegmentTangent(segment, t));
  let sum = speed(0) + speed(1);
  for (let index = 1; index < count; index++) sum += (index % 2 ? 4 : 2) * speed(index / count);
  return sum / (3 * count);
}
function ellipseFromInputs(start, points, pointer = false) {
  const [center, minor, end] = points;
  if (!center || !minor) return null;
  const radiusX = pointDistance(start, center), rotation = Math.atan2(start[1] - center[1], start[0] - center[0]);
  const minorDelta = minor.map((value, axis) => value - center[axis]), signedMinor = -minorDelta[0] * Math.sin(rotation) + minorDelta[1] * Math.cos(rotation), radiusY = Math.abs(signedMinor), direction = Math.sign(signedMinor) || 1;
  if (radiusX <= EPS || radiusY <= EPS) return null;
  let sweep = 360 * direction;
  if (end) {
    const delta = end.map((value, axis) => value - center[axis]);
    const local = [(delta[0] * Math.cos(rotation) + delta[1] * Math.sin(rotation)) / radiusX, (-delta[0] * Math.sin(rotation) + delta[1] * Math.cos(rotation)) / radiusY];
    if (Math.hypot(...local) <= EPS) throw new Error("请指定椭圆弧终点方向，终点不能位于椭圆中心。");
    if (!pointer && Math.abs(Math.hypot(...local) - 1) > 1e-5) throw new Error("椭圆弧终点必须在当前椭圆上；可用鼠标指定方向，或输入椭圆上的精确坐标。");
    sweep = Math.atan2(local[1], local[0]) * 180 / Math.PI;
    if (direction > 0 && sweep <= 0) sweep += 360;
    if (direction < 0 && sweep >= 0) sweep -= 360;
  }
  return { type: "ellipseArc", center: [...center], radiusX, radiusY, rotationDeg: rotation * 180 / Math.PI, startAngleDeg: 0, sweepAngleDeg: sweep };
}

export class PlanarPolylineSession {
  constructor(state) { this.state = state;state.polylineSegments ??= [];state.polylinePending ??= [];state.polylineMode ??= "line";state.polylineHistory ??= []; }
  get segments() { return this.state.polylineSegments; }
  get pending() { return this.state.polylinePending; }
  get mode() { return this.state.polylineMode; }
  get start() { return this.state.commandPoints?.at(-1); }
  begin() { this.state.polylineSegments = [];this.state.polylinePending = [];this.state.polylineMode = "line";this.state.polylineHistory = []; }
  snapshot() { return clone({ commandPoints: this.state.commandPoints, polylineSegments: this.segments, polylinePending: this.pending, polylineMode: this.mode }); }
  setMode(mode) {
    if (!polylineSegmentKinds.some(([kind]) => mode === kind)) throw new Error("请选择有效的曲段类型。");
    if (mode === this.mode) return;
    if (this.pending.length) this.commitPending();
    this.state.polylineMode = mode;
  }
  step() {
    if (!this.start) return "指定多义线起点";
    const prompts = { line: ["指定直线终点"], arc: ["指定圆弧经过点", "指定圆弧终点"], ellipseArc: ["指定椭圆中心，当前起点为第一半轴端点", "指定另一半轴半径点（所在侧决定方向）", "指定椭圆弧终点方向"], bezier: ["指定第一个控制点", "指定第二个控制点", "指定贝塞尔终点"], bspline: ["继续指定B样条控制点；含起点至少4点后，右键完成本段"] };
    return prompts[this.mode][Math.min(this.pending.length, prompts[this.mode].length - 1)];
  }
  accept(point, { pointer = false } = {}) {
    if (!Array.isArray(point) || point.length !== 2 || !point.every(Number.isFinite)) throw new Error("坐标必须为有限二维数值。");
    if (this.mode === "bspline" && this.pending.length >= 255) throw new Error("一段B样条最多支持256个控制点，请先右键完成本段。");
    const before = this.snapshot();
    if (!this.start) { this.state.commandPoints = [[...point]];this.state.polylineHistory.push(before);return; }
    this.pending.push([...point]);
    const required = { line: 1, arc: 2, ellipseArc: 3, bezier: 3 }[this.mode];
    if (!required || this.pending.length < required) { this.state.polylineHistory.push(before);return; }
    try {
      const segment = this.makeSegment(this.pending, pointer);
      if (!segment) throw new Error(this.mode === "arc" ? "圆弧三点必须互不重合且不共线，请重新指定终点。" : "曲段参数无效，请重新指定。");
      this.append(segment);
      this.state.polylineHistory.push(before);
    } catch (error) { this.pending.pop();throw error; }
  }
  makeSegment(points, pointer = false) {
    const start = this.start;
    if (!start) return null;
    if (this.mode === "line") return points[0] && pointDistance(start, points[0]) > EPS ? { type: "line", points: [[...start], [...points[0]]] } : null;
    if (this.mode === "arc") return points.length >= 2 ? arcThroughPoints(start, points[0], points[1]) : null;
    if (this.mode === "ellipseArc") return ellipseFromInputs(start, points, pointer);
    if (this.mode === "bezier") return points.length >= 3 ? { type: "bezier", controlPoints: [[...start], ...clone(points.slice(0, 3))] } : null;
    if (this.mode === "bspline") return points.length >= 3 ? { type: "bspline", degree: 3, controlPoints: [[...start], ...clone(points)] } : null;
    return null;
  }
  append(segment) {
    this.segments.push(segment);this.state.commandPoints.push(curveSegmentPoint(segment, 1));this.state.polylinePending = [];
  }
  commitPending() {
    if (!this.pending.length) return false;
    if (this.mode !== "bspline") throw new Error("当前曲段尚未完成，请按提示补齐控制点；Backspace可逐点退回。");
    const segment = this.makeSegment(this.pending);
    if (!segment) throw new Error("三次B样条至少需要4个控制点（含当前起点），请继续指定控制点。");
    const before = this.snapshot();this.append(segment);this.state.polylineHistory.push(before);return true;
  }
  undo() {
    const previous = this.state.polylineHistory.pop();
    if (previous) { Object.assign(this.state, previous);return true; }
    if (this.pending.length) { this.pending.pop();return true; }
    if (this.segments.length) { this.segments.pop();this.state.commandPoints.pop();return true; }
    if (this.state.commandPoints?.length) { this.state.commandPoints.pop();return true; }
    return false;
  }
  preview(pointer) {
    let candidate = null;
    const controls = [...this.pending, ...(pointer ? [[...pointer]] : [])];
    try {
      candidate = this.makeSegment(controls, true);
      if (!candidate && ["bezier", "bspline"].includes(this.mode) && this.start && controls.length) {
        candidate = { type: this.mode, ...(this.mode === "bspline" ? { degree: Math.min(3, controls.length) } : {}), controlPoints: [[...this.start], ...clone(controls)] };
      }
    } catch {}
    return { segments: [...this.segments, ...(candidate ? [candidate] : [])], candidate, controls: this.start ? [[...this.start], ...controls] : controls };
  }
  payload(closed = false) {
    if (this.pending.length) this.commitPending();
    if (!this.segments.length) throw new Error("请先完成至少一个曲段。");
    if (this.segments.every(segment => segment.type === "line")) {
      if (closed && this.state.commandPoints.length < 3) throw new Error("闭合折线至少需要三个顶点。");
      return { primitive: "polyline", points: clone(this.state.commandPoints), closed };
    }
    const segments = clone(this.segments), first = this.state.commandPoints[0], last = curveSegmentPoint(segments.at(-1), 1);
    if (closed && pointDistance(first, last) > EPS) segments.push({ type: "line", points: [[...last], [...first]] });
    return { primitive: "polyline", segments, closed };
  }
  dimensions(pointer) {
    const candidate = this.preview(pointer).candidate;
    if (!candidate) return [];
    const start = curveSegmentPoint(candidate, 0), end = curveSegmentPoint(candidate, 1), delta = end.map((value, axis) => value - start[axis]), chord = Math.hypot(...delta);
    const result = chord > EPS ? [{ key: "length", label: "L", kind: "length", value: segmentLength(candidate), start: [...start, 0], end: [...end, 0], offsetDirection: [-delta[1] / chord, delta[0] / chord, 0] }] : [];
    const previous = this.segments.at(-1);
    if (previous) {
      const incoming = curveSegmentTangent(previous, 1), outgoing = curveSegmentTangent(candidate, 0), a = Math.hypot(...incoming), b = Math.hypot(...outgoing);
      if (a > EPS && b > EPS) {
        const turn = Math.atan2(incoming[0] * outgoing[1] - incoming[1] * outgoing[0], incoming[0] * outgoing[0] + incoming[1] * outgoing[1]) * 180 / Math.PI;
        const value = Math.abs(turn), radius = Math.min(segmentLength(previous), segmentLength(candidate)) * .28;
        const first = Math.atan2(...(turn < 0 ? [outgoing[1], outgoing[0]] : [incoming[1], incoming[0]])) * 180 / Math.PI;
        const at = degrees => [start[0] + radius * Math.cos(rad(degrees)), start[1] + radius * Math.sin(rad(degrees)), 0];
        result.push({ key: "includedAngleDeg", label: "∠", kind: "angle", unit: "deg", editable: false, value, center: [...start, 0], radius, startAngleDeg: first, sweepAngleDeg: value, start: at(first), end: at(first + value) });
      }
    }
    if (candidate.type === "arc") result.push({ key: "radius", label: "R", kind: "radius", value: candidate.radius, start: [...candidate.center, 0], end: [...curveSegmentPoint(candidate, .5), 0] });
    return result;
  }
}
