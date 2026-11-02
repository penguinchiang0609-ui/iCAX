import { curvePoint, editableSegments, TAU } from "../../_shared/workbench/sketch/sketchGeometry.mjs";
export { parseSketchDxf, mapSketchCadDocument } from "./sketchCadDxfImport.mjs";

const finite = (value, label) => {
  const number = Number(value);
  if (value === "" || value == null || !Number.isFinite(number)) throw new Error(`${label}必须是有限数值。`);
  return number;
};
const positive = (value, label) => {
  const number = finite(value, label);
  if (number <= 0) throw new Error(`${label}必须大于零。`);
  return number;
};
const count = (value, label, minimum = 3) => {
  const number = finite(value, label);
  if (!Number.isInteger(number) || number < minimum || number > 500) throw new Error(`${label}必须为 ${minimum}～500 的整数。`);
  return number;
};
const line = (a, b) => ({ kind: "line", x1: a[0], y1: a[1], x2: b[0], y2: b[1] });
const polygonPath = points => ({ kind: "path", closed: true, segments: points.map((point, index) => line(point, points[(index + 1) % points.length])) });

export function createRegularPolygon({ centerX = 0, centerY = 0, radius = 10, sides = 6, rotation = 0 } = {}) {
  const cx = finite(centerX, "中心 X"), cy = finite(centerY, "中心 Y"), r = positive(radius, "外接圆半径");
  const n = count(sides, "边数"), angle = finite(rotation, "旋转角");
  return polygonPath(Array.from({ length: n }, (_, i) => [cx + r * Math.cos(angle + i * TAU / n), cy + r * Math.sin(angle + i * TAU / n)]));
}

export function createStar({ centerX = 0, centerY = 0, radius = 10, innerRadius = 5, points = 5, rotation = 0 } = {}) {
  const cx = finite(centerX, "中心 X"), cy = finite(centerY, "中心 Y"), outer = positive(radius, "外半径"), inner = positive(innerRadius, "内半径");
  const n = count(points, "角数"), angle = finite(rotation, "旋转角");
  if (inner >= outer) throw new Error("内半径必须小于外半径。");
  return polygonPath(Array.from({ length: n * 2 }, (_, i) => {
    const r = i % 2 ? inner : outer, a = angle + i * Math.PI / n;
    return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
  }));
}

export function createRacetrack({ x = 0, y = 0, width = 40, height = 20 } = {}) {
  const left = finite(x, "X"), bottom = finite(y, "Y"), w = positive(width, "宽度"), h = positive(height, "高度");
  return { kind: "path", closed: true, segments: editableSegments({ kind: "rectangle", x: left, y: bottom, width: w, height: h, radius: Math.min(w, h) / 2 }) };
}

/** AC1027 DXF: editable conics remain conics, Bézier pieces become exact clamped
 * B-splines. Ordered path pieces retain their endpoint direction. No sampling. */
export function buildSketchDxf(entities, { name = "Sketch", units = "mm" } = {}) {
  if (!Array.isArray(entities) || !entities.length) throw new Error("草图中没有可导出的图形。");
  const unitCode = ({ mm: 4, cm: 5, m: 6, inch: 1, in: 1, unitless: 0 })[units];
  if (unitCode == null) throw new Error("不支持的 DXF 长度单位。");
  const values = [], pair = (code, value) => values.push(String(code), typeof value === "number" ? decimal(value) : String(value));
  function decimal(value) {
    if (!Number.isFinite(value)) throw new Error("草图包含无效坐标，无法导出。");
    return Object.is(value, -0) || Math.abs(value) < 1e-14 ? "0" : String(value);
  }
  const point = (code, p) => { pair(code, p[0]); pair(code + 10, p[1]); pair(code + 20, 0); };
  let handle = 256;
  const begin = (type, subclass) => { pair(0, type); pair(5, (handle++).toString(16).toUpperCase()); pair(100, "AcDbEntity"); pair(8, "0"); pair(100, subclass); };
  const normal = direction => { pair(210, 0); pair(220, 0); pair(230, direction); };
  const mod = value => ((value % TAU) + TAU) % TAU;
  function emit(s) {
    if (s.kind === "line") {
      begin("LINE", "AcDbLine"); point(10, [finite(s.x1, "X"), finite(s.y1, "Y")]); point(11, [finite(s.x2, "X"), finite(s.y2, "Y")]); return;
    }
    if (s.kind === "bezier") {
      if (!Array.isArray(s.points) || s.points.length < 2 || s.points.length > 26) throw new Error("Bezier 控制点数量无效。");
      const controls = s.points.map(p => [finite(p?.[0], "控制点 X"), finite(p?.[1], "控制点 Y")]);
      const degree = controls.length - 1;
      begin("SPLINE", "AcDbSpline"); normal(1); pair(70, 8); pair(71, degree); pair(72, controls.length * 2); pair(73, controls.length); pair(74, 0);
      for (let i = 0; i < controls.length; i++) pair(40, 0);
      for (let i = 0; i < controls.length; i++) pair(40, 1);
      controls.forEach(p => point(10, p)); return;
    }
    if (!["circle", "circleArc", "ellipse", "ellipseArc"].includes(s.kind)) throw new Error(`不支持导出图形：${s.kind ?? "未知"}。`);
    const cx = finite(s.cx, "中心 X"), cy = finite(s.cy, "中心 Y"), rotation = finite(s.rotation ?? 0, "旋转角");
    const rx = positive(s.radius ?? s.radiusX, "半径"), ry = positive(s.radius ?? s.radiusY, "半径");
    const start = finite(s.startAngle ?? 0, "起始角"), sweep = finite(s.sweep ?? TAU, "弧角");
    if (Math.abs(sweep) < 1e-12 || Math.abs(sweep) > TAU + 1e-9) throw new Error("圆弧角度须大于零且不超过一周。");
    const full = Math.abs(Math.abs(sweep) - TAU) < 1e-9, direction = Math.sign(sweep);
    if (Math.abs(rx - ry) < 1e-12) {
      // ARC angles are counterclockwise in OCS. For -Z the arbitrary X axis is
      // -world-X; reflect both center and angles to preserve clockwise order.
      begin(full ? "CIRCLE" : "ARC", "AcDbCircle"); point(10, [direction * cx, cy]); pair(40, rx); normal(direction);
      if (!full) {
        pair(100, "AcDbArc");
        const a = direction > 0 ? start + rotation : Math.PI - start - rotation;
        pair(50, mod(a) * 180 / Math.PI); pair(51, mod(a + Math.abs(sweep)) * 180 / Math.PI);
      }
      return;
    }
    const swap = ry > rx, majorAngle = rotation + (swap ? Math.PI / 2 : 0), major = Math.max(rx, ry);
    const parameter = start - (swap ? Math.PI / 2 : 0);
    begin("ELLIPSE", "AcDbEllipse"); point(10, [cx, cy]); point(11, [major * Math.cos(majorAngle), major * Math.sin(majorAngle)]); normal(direction); pair(40, Math.min(rx, ry) / major);
    const first = full ? 0 : mod(direction * parameter);
    pair(41, first); pair(42, full ? TAU : first + Math.abs(sweep));
  }
  pair(0, "SECTION"); pair(2, "HEADER"); pair(9, "$ACADVER"); pair(1, "AC1027"); pair(9, "$INSUNITS"); pair(70, unitCode); pair(9, "$MEASUREMENT"); pair(70, [0, 1].includes(unitCode) ? 0 : 1); pair(0, "ENDSEC");
  pair(0, "SECTION"); pair(2, "TABLES"); pair(0, "TABLE"); pair(2, "LAYER"); pair(70, 1); pair(0, "LAYER"); pair(100, "AcDbSymbolTableRecord"); pair(100, "AcDbLayerTableRecord"); pair(2, "0"); pair(70, 0); pair(62, 7); pair(6, "CONTINUOUS"); pair(0, "ENDTAB"); pair(0, "ENDSEC");
  pair(0, "SECTION"); pair(2, "ENTITIES"); pair(999, String(name).replace(/[\r\n\0]/g, " ").slice(0, 200));
  for (const entity of entities) {
    if (entity?.kind === "text") throw new Error("文字必须先转换为轮廓后再导出 DXF。");
    let segments;
    if (["circle", "ellipse"].includes(entity?.kind)) segments = [entity];
    else if (["line", "circleArc", "ellipseArc", "bezier"].includes(entity?.kind)) segments = [entity];
    else segments = editableSegments(entity ?? {});
    if (!segments.length) throw new Error(`图形 ${entity?.id ?? entity?.kind ?? "未知"} 没有可导出的轮廓。`);
    segments.forEach(emit);
    if (entity.closed && !["circle", "ellipse"].includes(entity.kind)) {
      const first = curvePoint(segments[0], 0), last = curvePoint(segments.at(-1), 1);
      if (Math.hypot(first[0] - last[0], first[1] - last[1]) > 1e-9) emit(line(last, first));
    }
  }
  pair(0, "ENDSEC"); pair(0, "EOF");
  return values.join("\r\n") + "\r\n";
}
