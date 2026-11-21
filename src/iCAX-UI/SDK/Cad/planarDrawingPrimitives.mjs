import { curveSegmentPoint, curveSegmentSamples, pointDistance } from "./planarCurveSegments.mjs";

const EPS = 1e-9;
export const standalonePrimitiveDefinitions = Object.freeze({
  ellipse: { name: "椭圆", points: 3, steps: ["指定椭圆中心", "指定第一半轴端点，或输入 A", "指定另一半轴半径点，或输入 B"] },
  ellipseArc: { name: "椭圆弧", points: 4, steps: ["指定椭圆中心", "指定第一半轴端点，或输入 A", "指定另一半轴半径点，所在侧决定方向", "指定终点方向，或输入扫过角 θ"] },
  triangle: { name: "三角形", points: 3, steps: ["指定第一个顶点", "指定第二个顶点", "指定第三个顶点"] },
  polygon: { name: "正多边形", points: 2, steps: ["指定正多边形中心", "指定外接圆半径及首顶点方向，或输入 R"] },
  star: { name: "星形", points: 3, steps: ["指定星形中心", "指定外角端点，或输入外半径 R", "指定内半径点，或输入 r"] },
  bezier: { name: "贝塞尔", points: 4, steps: ["指定贝塞尔起点", "指定第一个控制点", "指定第二个控制点", "指定贝塞尔终点"] },
  bspline: { name: "B样条", points: Infinity, steps: ["指定第一个控制点", "继续指定控制点，至少4点后右键选择结束"] },
});

const positive = (value, label) => { if (!Number.isFinite(value) || value <= EPS) throw new Error(`${label}必须大于零。`);return value; };
const integer = (value, fallback, label) => {
  value = value == null ? fallback : Number(value);
  if (!Number.isInteger(value) || value < 3 || value > 64) throw new Error(`${label}必须是3到64之间的整数。`);
  return value;
};
const anglePoint = (center, x, y, angle) => [center[0] + x * Math.cos(angle) - y * Math.sin(angle), center[1] + x * Math.sin(angle) + y * Math.cos(angle)];

export function ellipsePrimitiveParameters(points, { pointerInput = false, sweepAngleDeg } = {}) {
  const [center, axis, minor, end] = points;
  if (!center || !axis || !minor) throw new Error("请先指定椭圆中心和两个半轴。");
  const radiusX = positive(pointDistance(center, axis), "第一半轴 A"), rotation = Math.atan2(axis[1] - center[1], axis[0] - center[0]);
  const delta = minor.map((value, i) => value - center[i]);
  const signedMinor = -delta[0] * Math.sin(rotation) + delta[1] * Math.cos(rotation), radiusY = positive(Math.abs(signedMinor), "另一半轴 B");
  let sweep = 360 * (Math.sign(signedMinor) || 1);
  if (end) {
    const d = end.map((value, i) => value - center[i]);
    const local = [(d[0] * Math.cos(rotation) + d[1] * Math.sin(rotation)) / radiusX, (-d[0] * Math.sin(rotation) + d[1] * Math.cos(rotation)) / radiusY];
    if (Math.hypot(...local) <= EPS) throw new Error("椭圆弧终点方向不能位于中心。");
    if (!pointerInput && Math.abs(Math.hypot(...local) - 1) > 1e-5) throw new Error("终点必须在椭圆上；可用鼠标指定方向，或输入椭圆上的精确坐标。");
    sweep = Math.atan2(local[1], local[0]) * 180 / Math.PI;
    if (signedMinor > 0 && sweep <= 0) sweep += 360;
    if (signedMinor < 0 && sweep >= 0) sweep -= 360;
  }
  if (sweepAngleDeg != null) {
    sweep = Number(sweepAngleDeg);
    if (!Number.isFinite(sweep) || Math.abs(sweep) <= EPS || Math.abs(sweep) > 360) throw new Error("扫过角必须非零且不超过一圈，可用负数指定顺时针。");
  }
  return { center: [...center], radiusX, radiusY, rotationDeg: rotation * 180 / Math.PI, startAngleDeg: 0, sweepAngleDeg: sweep };
}

export function standalonePrimitivePayload(primitive, points, options = {}) {
  const definition = standalonePrimitiveDefinitions[primitive];
  if (!definition) return null;
  if (points.some(point => !Array.isArray(point) || point.length !== 2 || !point.every(Number.isFinite))) throw new Error("坐标必须是两个有限数值。");
  if (primitive === "bspline") {
    if (points.length < 4 || points.length > 256) throw new Error("B样条需要4到256个控制点。");
    return { primitive, degree: 3, controlPoints: points.map(point => [...point]) };
  }
  if (points.length < definition.points) throw new Error(definition.steps.at(-1));
  if (primitive === "bezier") return { primitive, controlPoints: points.slice(0, 4).map(point => [...point]) };
  if (primitive === "triangle") return { primitive, points: points.slice(0, 3).map(point => [...point]) };
  if (primitive === "ellipse" || primitive === "ellipseArc") {
    const parameters = ellipsePrimitiveParameters(points, options);
    return { primitive, ...parameters, ...(primitive === "ellipse" ? { startAngleDeg: 0, sweepAngleDeg: 360 } : {}) };
  }
  const [center, outer, inner] = points, radius = positive(pointDistance(center, outer), "外半径");
  const rotationDeg = Math.atan2(outer[1] - center[1], outer[0] - center[0]) * 180 / Math.PI;
  if (primitive === "polygon") return { primitive, center: [...center], radius, rotationDeg, sides: integer(options.sides, 6, "边数") };
  const innerRadius = positive(pointDistance(center, inner), "内半径");
  if (innerRadius >= radius) throw new Error("星形内半径必须小于外半径。");
  return { primitive, center: [...center], outerRadius: radius, innerRadius, rotationDeg, tips: integer(options.tips, 5, "角数") };
}

export function primitiveInputOptions(primitive, options = {}, dynamic = null) {
  const result = { ...options };
  const draft = dynamic?.primitive === primitive ? dynamic.drafts?.sweepAngleDeg : null;
  if (primitive === "ellipseArc" && draft?.dirty) {
    const value = Number(draft.text);
    if (Number.isFinite(value) && value !== 0 && Math.abs(value) <= 360) result.sweepAngleDeg = value;
  }
  return result;
}

export function standaloneScalarInput(primitive, points, value, pointer) {
  const center = points[0], axis = points[1];
  if (!center || !Number.isFinite(value)) throw new Error("请先指定中心，再输入有限数值。");
  if (primitive === "ellipseArc" && points.length === 3) {
    if (value === 0 || Math.abs(value) > 360) throw new Error("扫过角须非零且绝对值不超过360°。");
    const parameters = ellipsePrimitiveParameters(points, { sweepAngleDeg: value });
    return { point: curveSegmentPoint({ type: "ellipseArc", ...parameters }, 1), options: { sweepAngleDeg: value } };
  }
  if (value <= EPS) throw new Error("半径必须大于零。");
  const anglePoint = axis ?? pointer, rotation = anglePoint ? Math.atan2(anglePoint[1] - center[1], anglePoint[0] - center[0]) : 0;
  const minor = ["ellipse", "ellipseArc"].includes(primitive) && points.length === 2;
  let angle = rotation + (minor ? Math.PI / 2 : 0);
  if (minor && pointer) {
    const d = pointer.map((v, i) => v - center[i]);if (-d[0] * Math.sin(rotation) + d[1] * Math.cos(rotation) < 0) angle += Math.PI;
  }
  return { point: anglePoint ? [center[0] + value * Math.cos(angle), center[1] + value * Math.sin(angle)] : [center[0] + value, center[1]] };
}

// A neutral construction session: only preview sampling is done here. Formal
// exact curves and shape vertices are produced by the injected native service.
export class PlanarPrimitiveSession {
  constructor(primitive, points = [], options = {}) { this.primitive = primitive;this.points = points;this.options = options; }
  get definition() { return standalonePrimitiveDefinitions[this.primitive]; }
  get ready() { return this.primitive === "bspline" ? this.points.length >= 4 : this.points.length >= (this.definition?.points ?? Infinity); }
  validateConstruction() {
    if (["ellipse", "ellipseArc", "polygon", "star"].includes(this.primitive) && this.points.length >= 2) positive(pointDistance(this.points[0], this.points[1]), "半径");
    if (["ellipse", "ellipseArc"].includes(this.primitive) && this.points.length >= 3) ellipsePrimitiveParameters(this.points.slice(0, 3));
    if (this.primitive === "bspline" && this.points.length > 256) throw new Error("B样条最多支持256个控制点。");
  }
  step() { const steps = this.definition?.steps ?? [];return steps[Math.min(this.points.length, steps.length - 1)] ?? ""; }
  payload() { return standalonePrimitivePayload(this.primitive, this.points, this.options); }
  preview(pointer) {
    const points = this.points.map(point => [...point]);if (pointer) points.push([...pointer]);
    let outline = [], guides = [], controls = [], segments = [];
    const primitive = this.primitive;
    if (primitive === "triangle") outline = points.length >= 3 ? [...points.slice(0, 3), points[0]] : points;
    if (primitive === "bezier" || primitive === "bspline") {
      controls = points;for (let i = 1; i < points.length; i++) guides.push(points[i - 1], points[i]);
      if (points.length >= 2) {
        const segment = { type: primitive, controlPoints: primitive === "bezier" ? points.slice(0, 4) : points.slice(0, 256), ...(primitive === "bspline" ? { degree: Math.min(3, points.length - 1) } : {}) };
        outline = curveSegmentSamples(segment);segments = [segment];
      }
    }
    if ((primitive === "ellipse" || primitive === "ellipseArc") && points.length >= 2) {
      const construction = [...points];
      if (construction.length < 3) {
        const [center, axis] = points, dx = axis[0] - center[0], dy = axis[1] - center[1];
        construction.push([center[0] - dy * .6, center[1] + dx * .6]);
      }
      try {
        const parameters = ellipsePrimitiveParameters(construction, { ...this.options, pointerInput: true });
        if (primitive === "ellipse") parameters.sweepAngleDeg = 360;
        const segment = { type: "ellipseArc", ...parameters };outline = curveSegmentSamples(segment);segments = [segment];
        guides.push(points[0], points[1]);if (points[2]) guides.push(points[0], points[2]);
      } catch { guides.push(points[0], points[1]); }
    }
    if ((primitive === "polygon" || primitive === "star") && points.length >= 2) {
      const construction = [...points];
      if (primitive === "star" && construction.length < 3) construction.push(points[0].map((v, i) => (v + points[1][i]) / 2));
      try {
        const payload = standalonePrimitivePayload(primitive, construction, this.options), count = primitive === "star" ? payload.tips * 2 : payload.sides;
        outline = Array.from({ length: count }, (_, i) => {
          const radius = primitive === "polygon" ? payload.radius : i % 2 ? payload.innerRadius : payload.outerRadius;
          return anglePoint(payload.center, radius, 0, payload.rotationDeg * Math.PI / 180 + 2 * Math.PI * i / count);
        });outline.push([...outline[0]]);guides.push(points[0], points[1]);if (points[2]) guides.push(points[0], points[2]);
      } catch { guides.push(points[0], points[1]); }
    }
    return { outline, guides, controls, segments };
  }
}
