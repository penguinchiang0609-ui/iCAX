import { arcPoint } from "./planarDimensionValues.mjs";
import { standalonePrimitiveDefinitions, standalonePrimitivePayload } from "./planarDrawingPrimitives.mjs";

export const planarToolNames = Object.freeze({ line: "直线", polyline: "多段线", rect: "矩形", circle: "圆", arc: "圆弧", ...Object.fromEntries(Object.entries(standalonePrimitiveDefinitions).map(([key, definition]) => [key, definition.name])) });
export const planarNumberPattern = "[-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:e[-+]?\\d+)?";
export const planarPointPattern = new RegExp(`^(${planarNumberPattern})\\s*[,，]\\s*(${planarNumberPattern})$`, "i");

export class PlanarEditorSession {
  constructor(state) { this.state = state; }
  begin() { const state = this.state;state.commandPoints = [];state.snapCandidates = [];state.snapSignature = "";state.drawingCreatedCount = 0;state.drawingEpoch = (state.drawingEpoch ?? 0) + 1; }
  cancel() { const state = this.state;state.snapRequest = (state.snapRequest ?? 0) + 1;state.commandPoints = [];state.polylineSegments = [];state.polylinePending = [];state.polylineHistory = [];state.commandInput = "";state.drawingEpoch = (state.drawingEpoch ?? 0) + 1; }
  static parsePoint(text, previous = [0, 0]) {
    text = String(text).trim();const relative = text.startsWith("@");if (relative) text = text.slice(1);
    const polar = text.match(new RegExp(`^(${planarNumberPattern})\\s*<\\s*(${planarNumberPattern})$`, "i"));let point;
    if (polar && relative) { const length = Number(polar[1]), angle = Number(polar[2]) * Math.PI / 180;point = [length * Math.cos(angle), length * Math.sin(angle)]; }
    else { const match = text.match(planarPointPattern);if (!match) throw new Error("请输入 X,Y；相对坐标用 @dX,dY，极坐标用 @长度<角度。");point = [Number(match[1]), Number(match[2])]; }
    if (relative) point = point.map((value, axis) => value + previous[axis]);
    if (!point.every(Number.isFinite)) throw new Error("坐标必须为有限数值。");return point;
  }
  static primitivePayload(primitive, points, closed = false, options = {}) {
    const standalone = standalonePrimitivePayload(primitive, points, options);
    if (standalone) return standalone;
    if (primitive === "line") return { primitive, points: points.slice(0, 2) };
    if (primitive === "polyline") return { primitive, points, closed };
    if (primitive === "rect") return { primitive, points: [[Math.min(points[0][0], points[1][0]), Math.min(points[0][1], points[1][1])]], width: Math.abs(points[1][0] - points[0][0]), height: Math.abs(points[1][1] - points[0][1]) };
    if (primitive === "circle" || primitive === "arc") {
      const center = points[0], radius = Math.hypot(points[1][0] - center[0], points[1][1] - center[1]);
      return { primitive, center, radius, ...(primitive === "arc" ? { startAngleDeg: Math.atan2(points[1][1] - center[1], points[1][0] - center[0]) * 180 / Math.PI, endAngleDeg: Math.atan2(points[2][1] - center[1], points[2][0] - center[0]) * 180 / Math.PI } : {}) };
    }
    throw new Error("当前工具不支持此图形。");
  }
  pointerPoint(viewport, event, { primitive, origin } = {}) {
    const state = this.state, raw = viewport?.pointOnWorkPlane(event.clientX, event.clientY, [0, 0, 1]);if (!raw) return null;
    let point = raw.slice(0, 2), snapped = false;state.lastSnapLabel = "";
    const adjacent = viewport.pointOnWorkPlane(event.clientX + 1, event.clientY, [0, 0, 1]), perPixel = adjacent ? Math.hypot(adjacent[0] - raw[0], adjacent[1] - raw[1]) : 0;
    if (primitive === "polyline" && !state.polylinePending?.length && (state.polylineMode ?? "line") === "line" && state.commandPoints.length >= 3 && Math.hypot(...state.commandPoints[0].map((value, axis) => value - raw[axis])) <= 8 * perPixel) { state.lastSnapLabel = "闭合首点";return [...state.commandPoints[0]]; }
    if (state.snapEnabled) {
      let nearest = 8 * perPixel;
      for (const candidate of state.snapCandidates ?? []) {
        if (!Array.isArray(candidate.point) || candidate.point.length < 2) continue;
        const distance = Math.hypot(candidate.point[0] - raw[0], candidate.point[1] - raw[1]);
        if (distance < nearest) { nearest = distance;point = candidate.point.slice(0, 2);snapped = true;state.lastSnapLabel = { end: "端点", mid: "中点", center: "圆心" }[candidate.type] ?? "对象点"; }
      }
    }
    origin ??= state.commandPoints?.at(-1);
    if (!snapped && state.orthoEnabled && origin) { if (Math.abs(point[0] - origin[0]) >= Math.abs(point[1] - origin[1])) point[1] = origin[1];else point[0] = origin[0]; }
    return point;
  }
  static segmentDimensions(primitive, accepted, point) {
    const start = accepted.at(-1), delta = start && point ? point.map((value, axis) => value - start[axis]) : null, distance = delta ? Math.hypot(...delta) : 0;
    const dimensions = distance > 0 ? [{ key: "length", label: "L", kind: "length", value: distance, start: [...start, 0], end: [...point, 0], offsetDirection: [-delta[1] / distance, delta[0] / distance, 0] }] : [];
    if (primitive === "polyline" && accepted.length >= 2 && distance > 0) {
      const incoming = start.map((value, axis) => value - accepted.at(-2)[axis]), length = Math.hypot(...incoming);
      if (length > 0) {
        const [ux, uy] = incoming.map(value => value / length), [vx, vy] = delta.map(value => value / distance);
        const turn = Math.atan2(ux * vy - uy * vx, ux * vx + uy * vy) * 180 / Math.PI, angle = Math.abs(turn), radius = Math.min(length, distance) * .28;
        const startAngleDeg = (turn < 0 ? Math.atan2(vy, vx) : Math.atan2(uy, ux)) * 180 / Math.PI, at = degrees => [...arcPoint(start, radius, degrees), 0];
        dimensions.push({ key: "includedAngleDeg", label: "∠", kind: "angle", unit: "deg", editable: false, value: angle, center: [...start, 0], radius, startAngleDeg, sweepAngleDeg: angle, start: at(startAngleDeg), end: at(startAngleDeg + angle) });
      }
    }
    return dimensions;
  }
}
