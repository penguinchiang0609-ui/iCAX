import { curvePoint, editableSegments, reverseSegment, TAU } from "./sketchGeometry.mjs";

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

const unitScale = { 0: 1, 1: 25.4, 2: 304.8, 3: 1609344, 4: 1, 5: 10, 6: 1000, 7: 1000000, 8: 0.0000254, 9: 0.0254, 10: 914.4, 11: 1e-7, 12: 1e-6, 13: 0.001, 14: 100, 15: 10000, 16: 100000, 17: 1e12, 18: 1.495978707e14, 19: 9.4607304725808e18, 20: 3.085677581491367e19 };

/** Parse planar DXF without the closed single-section restrictions of the pipe
 * profile importer. Coordinates are converted to mm and remain at their WCS
 * position. Unsupported geometry is refused, never converted to display lines. */
export function parseSketchDxf(content) {
  if (typeof content !== "string" || !content.trim()) throw new Error("DXF 文件为空。");
  if (content.length > 32 * 1024 * 1024) throw new Error("DXF 文件超过 32 MB。");
  if (content.startsWith("AutoCAD Binary DXF")) throw new Error("请另存为 ASCII DXF 后导入。");
  const source = content.replace(/^\uFEFF/, "").replace(/\r\n?/g, "\n").split("\n");
  while (source.length && source.at(-1) === "") source.pop();
  if (source.length % 2) throw new Error("DXF 组码和值没有成对出现。");
  const pairs = [];
  for (let i = 0; i < source.length; i += 2) {
    if (!/^\s*\d+\s*$/.test(source[i])) throw new Error(`DXF 第 ${i + 1} 行不是有效组码。`);
    pairs.push([Number(source[i]), source[i + 1].trim()]);
  }
  let section = "", units = 0;
  const records = []; let record;
  for (let i = 0; i < pairs.length; i++) {
    const [code, value] = pairs[i];
    if (code === 0 && value.toUpperCase() === "SECTION" && pairs[i + 1]?.[0] === 2) { section = pairs[++i][1].toUpperCase(); continue; }
    if (code === 0 && value.toUpperCase() === "ENDSEC") { if (record) records.push(record); record = null; section = ""; continue; }
    if (section === "HEADER" && code === 9 && value === "$INSUNITS") units = Number(pairs[i + 1]?.[1]);
    if (section !== "ENTITIES") continue;
    if (code === 0) { if (record) records.push(record); record = { kind: value.toUpperCase(), values: [] }; }
    else if (record) record.values.push([code, value]);
  }
  if (record) records.push(record);
  const scale = unitScale[units];
  if (scale == null) throw new Error(`不支持 DXF 单位代码 ${units}。`);
  const entities = [], loose = [];
  const get = (r, code, fallback) => {
    const found = r.values.find(pair => pair[0] === code);
    if (!found && fallback == null) throw new Error(`DXF ${r.kind} 缺少组码 ${code}。`);
    return found ? finite(found[1], `DXF ${code}`) : fallback;
  };
  const all = (r, code) => r.values.filter(pair => pair[0] === code).map(pair => finite(pair[1], `DXF ${code}`));
  const normal = r => {
    const nx = get(r, 210, 0), ny = get(r, 220, 0), nz = get(r, 230, 1);
    if (Math.abs(nx) > 1e-9 || Math.abs(ny) > 1e-9 || Math.abs(Math.abs(nz) - 1) > 1e-9) throw new Error("二维草图不支持倾斜于 XY 平面的 DXF 图形。");
    return nz < 0 ? -1 : 1;
  };
  const xy = (r, xc = 10, yc = 20, ocs = false) => [get(r, xc) * scale * (ocs ? normal(r) : 1), get(r, yc) * scale];
  const checkZ = (r, codes = [30]) => { for (const code of codes) if (all(r, code).some(value => Math.abs(value * scale) > 1e-7)) throw new Error("二维草图只支持 Z=0 的 DXF 图形。"); };
  const addLoose = (segment, r) => { segment.dxfLayer = r.values.find(pair => pair[0] === 8)?.[1] ?? "0"; loose.push(segment); };
  const bulgeSegment = (a, b, bulge) => {
    if (Math.abs(bulge) < 1e-14) return line(a, b);
    const dx = b[0] - a[0], dy = b[1] - a[1], length = Math.hypot(dx, dy);
    if (length < 1e-12) throw new Error("DXF 多段线圆弧端点重合。");
    const k = (1 - bulge * bulge) / (4 * bulge), cx = (a[0] + b[0]) / 2 - dy * k, cy = (a[1] + b[1]) / 2 + dx * k;
    return { kind: "circleArc", cx, cy, radius: Math.hypot(a[0] - cx, a[1] - cy), startAngle: Math.atan2(a[1] - cy, a[0] - cx), sweep: 4 * Math.atan(bulge) };
  };
  function polyline(vertices, closed, r) {
    if (vertices.length < 2) throw new Error("DXF 多段线至少需要两个点。");
    const segments = vertices.slice(0, closed ? vertices.length : -1).map((v, i) => bulgeSegment(v.point, vertices[(i + 1) % vertices.length].point, v.bulge));
    entities.push({ kind: "path", segments, closed, dxfLayer: r.values.find(pair => pair[0] === 8)?.[1] ?? "0" });
  }
  for (let i = 0; i < records.length; i++) {
    const r = records[i];
    if (get(r, 67, 0) !== 0) continue;
    if (r.kind === "LINE") { checkZ(r, [30, 31]); addLoose(line(xy(r), xy(r, 11, 21)), r); }
    else if (["ARC", "CIRCLE"].includes(r.kind)) {
      checkZ(r); const direction = normal(r), [cx, cy] = xy(r, 10, 20, true), radius = positive(get(r, 40), "DXF 半径") * scale;
      if (r.kind === "CIRCLE") entities.push({ kind: "circle", cx, cy, radius, closed: true });
      else {
        const first = get(r, 50) * Math.PI / 180, last = get(r, 51) * Math.PI / 180;
        const sweep = ((last - first) % TAU + TAU) % TAU;
        if (sweep < 1e-12) throw new Error("DXF 圆弧角度为零。");
        addLoose({ kind: "circleArc", cx, cy, radius, startAngle: direction > 0 ? first : Math.PI - first, sweep: direction * sweep, closed: false }, r);
      }
    } else if (r.kind === "ELLIPSE") {
      checkZ(r, [30, 31]); const direction = normal(r), [cx, cy] = xy(r), [mx, my] = xy(r, 11, 21);
      const radiusX = positive(Math.hypot(mx, my), "DXF 椭圆长半径"), ratio = positive(get(r, 40), "DXF 椭圆轴比");
      if (ratio > 1 + 1e-9) throw new Error("DXF 椭圆轴比不能超过 1。");
      const radiusY = radiusX * ratio, rotation = Math.atan2(my, mx), first = get(r, 41, 0), last = get(r, 42, TAU);
      let sweep = ((last - first) % TAU + TAU) % TAU;
      if (sweep < 1e-10) sweep = TAU;
      const common = { cx, cy, radiusX, radiusY, rotation };
      if (Math.abs(sweep - TAU) < 1e-9) entities.push({ kind: "ellipse", ...common, closed: true });
      else addLoose({ kind: "ellipseArc", ...common, startAngle: direction * first, sweep: direction * sweep, closed: false }, r);
    } else if (r.kind === "LWPOLYLINE") {
      checkZ(r, [38]); const direction = normal(r), vertices = [];
      for (const [code, raw] of r.values) {
        if (code === 10) vertices.push({ point: [finite(raw, "DXF X") * scale * direction, NaN], bulge: 0 });
        else if (code === 20 && vertices.length) vertices.at(-1).point[1] = finite(raw, "DXF Y") * scale;
        else if (code === 42 && vertices.length) vertices.at(-1).bulge = finite(raw, "DXF bulge") * direction;
      }
      if (vertices.some(v => !v.point.every(Number.isFinite))) throw new Error("DXF 多段线点数据不完整。");
      if (get(r, 90, vertices.length) !== vertices.length) throw new Error("DXF 多段线点数量不一致。");
      polyline(vertices, Boolean(get(r, 70, 0) & 1), r);
    } else if (r.kind === "POLYLINE") {
      const flags = get(r, 70, 0), direction = normal(r);
      if (flags & (8 | 16 | 32 | 64)) throw new Error("二维草图不支持 3D 或网格多段线。");
      checkZ(r); const vertices = [];
      while (records[i + 1]?.kind === "VERTEX") {
        const vertex = records[++i]; checkZ(vertex);
        const [vx, vy] = xy(vertex); vertices.push({ point: [direction * vx, vy], bulge: direction * get(vertex, 42, 0) });
      }
      if (records[++i]?.kind !== "SEQEND") throw new Error("DXF POLYLINE 缺少 SEQEND。");
      polyline(vertices, Boolean(flags & 1), r);
    } else if (r.kind === "SPLINE") {
      checkZ(r); normal(r);
      const flags = get(r, 70, 0), degree = get(r, 71), xs = all(r, 10), ys = all(r, 20), weights = all(r, 41), knots = all(r, 40);
      if (xs.length !== ys.length || xs.length !== get(r, 73) || knots.length !== get(r, 72) || degree < 1 || degree > 25 || !Number.isInteger(degree) || knots.length !== xs.length + degree + 1) throw new Error("DXF SPLINE 的控制点、次数或节点无效。");
      if (flags & 2) throw new Error("周期 SPLINE 请在原 CAD 中转为非周期曲线后导入。");
      if (weights.length && (weights.length !== xs.length || weights.some(w => w <= 0 || Math.abs(w - weights[0]) > 1e-12))) throw new Error("该有理 SPLINE 需要先转换为圆弧或非有理曲线，不能按折线近似导入。");
      const controls = xs.map((x, p) => [x * scale, ys[p] * scale]);
      const segments = splineBezierPieces(controls, degree, knots);
      if (flags & 1) entities.push({ kind: "path", segments, closed: true });
      else segments.forEach(segment => addLoose(segment, r));
    } else throw new Error(`暂不支持 DXF ${r.kind}，请先在原 CAD 中转换为直线、圆弧、椭圆或样条轮廓。`);
    if (entities.length + loose.length > 10000) throw new Error("DXF 图形数量过多，请分批导入。");
  }
  entities.push(...joinDxfSegments(loose));
  if (!entities.length) throw new Error("DXF 中没有可用的二维图形。");
  if (entities.length > 5000) throw new Error("DXF 超过 5000 个图形，请分批导入。");
  return { entities, sourceUnitCode: units, unitScaleToMillimeter: scale };
}

function splineBezierPieces(controls, degree, sourceKnots) {
  let points = controls, knots = [...sourceKnots];
  if (knots.some((k, i) => i && k < knots[i - 1])) throw new Error("DXF SPLINE 节点必须递增。");
  const first = knots[degree], last = knots[points.length];
  if (!(last > first) || knots.slice(0, degree + 1).some(k => k !== first) || knots.slice(-degree - 1).some(k => k !== last)) throw new Error("非夹持 SPLINE 请在原 CAD 中转为夹持曲线后导入。");
  const unique = [...new Set(knots.filter(k => k > first && k < last))];
  for (const u of unique) {
    let multiplicity = knots.filter(k => k === u).length;
    if (multiplicity > degree) throw new Error("DXF SPLINE 包含断开的内部节点。");
    while (multiplicity < degree) {
      const k = knots.findIndex((value, i) => value <= u && knots[i + 1] > u), n = points.length - 1;
      const next = Array(points.length + 1);
      for (let i = 0; i <= k - degree; i++) next[i] = points[i];
      for (let i = k - multiplicity; i <= n; i++) next[i + 1] = points[i];
      for (let i = k - degree + 1; i <= k - multiplicity; i++) {
        const alpha = (u - knots[i]) / (knots[i + degree] - knots[i]);
        next[i] = points[i - 1].map((value, axis) => value * (1 - alpha) + points[i][axis] * alpha);
      }
      points = next; knots.splice(k + 1, 0, u); multiplicity++;
    }
  }
  const result = [];
  for (let i = degree; i < points.length; i++) if (knots[i + 1] > knots[i]) result.push({ kind: "bezier", points: points.slice(i - degree, i + 1).map(p => [...p]) });
  return result;
}

function joinDxfSegments(segments) {
  const epsilon = 1e-7, buckets = new Map(), ends = segments.map(s => [curvePoint(s, 0), curvePoint(s, 1)]);
  const key = (x, y) => `${x},${y}`;
  ends.forEach((pair, index) => pair.forEach((point, end) => {
    const k = key(Math.floor(point[0] / epsilon), Math.floor(point[1] / epsilon));
    if (!buckets.has(k)) buckets.set(k, []);
    buckets.get(k).push({ index, end, point });
  }));
  const matches = point => {
    const [x, y] = point.map(value => Math.floor(value / epsilon)), result = [];
    for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++)
      for (const value of buckets.get(key(x + dx, y + dy)) ?? []) if (Math.hypot(point[0] - value.point[0], point[1] - value.point[1]) < epsilon) result.push(value);
    return result;
  };
  const used = new Set(), entities = [];
  const order = segments.map((_, index) => index).sort((a, b) => Math.min(...ends[a].map(p => matches(p).length)) - Math.min(...ends[b].map(p => matches(p).length)));
  for (const index of order) {
    if (used.has(index)) continue;
    const reversed = matches(ends[index][1]).length === 1 && matches(ends[index][0]).length !== 1;
    const chain = [reversed ? reverseSegment(segments[index]) : segments[index]];
    used.add(index);
    const start = curvePoint(chain[0], 0);
    let closed = false;
    while (true) {
      const end = curvePoint(chain.at(-1), 1);
      if (Math.hypot(end[0] - start[0], end[1] - start[1]) < epsilon) { closed = true; break; }
      const allMatches = matches(end);
      if (allMatches.length !== 2) break; // A junction is not an arbitrary chain.
      const next = allMatches.filter(hit => !used.has(hit.index));
      if (next.length !== 1) break;
      used.add(next[0].index); chain.push(next[0].end ? reverseSegment(segments[next[0].index]) : segments[next[0].index]);
    }
    if (chain.length === 1 && !closed && chain[0].kind !== "bezier") entities.push({ ...chain[0], closed: false });
    else entities.push({ kind: "path", segments: chain, closed });
  }
  return entities;
}
