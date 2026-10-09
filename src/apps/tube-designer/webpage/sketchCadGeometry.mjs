import { TAU, curvePoint, editableSegments, nearestPath, splitSegment, reverseSegment, distance } from "./sketchGeometry.mjs";
import { offsetCurveSegments, tangentFilletSegments, curveChamferSegments, curveTangent } from "./sketchCadCurves.mjs";

// Editing always retains native curves. Numerical work below only finds curve
// parameters; its samples never become the resulting editable geometry.
const EPS = 1e-9;
const clone = value => typeof structuredClone === "function" ? structuredClone(value) : JSON.parse(JSON.stringify(value));
const mod = (v, period = TAU) => ((v % period) + period) % period;
const dot = (a, b) => a[0] * b[0] + a[1] * b[1];
const cross = (a, b) => a[0] * b[1] - a[1] * b[0];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1]];
const add = (a, b) => [a[0] + b[0], a[1] + b[1]];
const mul = (a, k) => [a[0] * k, a[1] * k];
const finite = (v, label = "参数") => {
  if ((typeof v !== "number" && typeof v !== "string") || String(v).trim() === "" || !Number.isFinite(Number(v))) throw new Error(`${label}必须是有限数值`);
  return Number(v);
};
const pointValue = p => {
  if (!Array.isArray(p) || p.length < 2) throw new Error("请选择有效的位置");
  return [finite(p[0], "横坐标"), finite(p[1], "纵坐标")];
};
const isClosed = e => Boolean(e.closed || ["circle", "ellipse", "rectangle"].includes(e.kind));
const line = (a, b) => ({ kind: "line", x1: a[0], y1: a[1], x2: b[0], y2: b[1] });
const segs = e => {
  if (!e || e.kind === "text") throw new Error("文字须先转成轮廓，才能进行此几何操作");
  if (e.kind === "bezier") return [clone(e)];
  const result = editableSegments(e);
  if (!result.length) throw new Error("该图形没有可编辑的曲线");
  return result;
};
function resultEntity(source, segments, closed = false) {
  const metadata = clone(source);
  for (const key of ["x", "y", "width", "height", "radius", "radiusX", "radiusY", "cx", "cy", "rotation", "startAngle", "sweep", "x1", "y1", "x2", "y2", "points", "segments", "brokenStart", "brokenEnd"]) delete metadata[key];
  if (segments.length === 1 && !closed && segments[0].kind !== "bezier") return { ...metadata, ...clone(segments[0]), closed: false };
  return { ...metadata, kind: "path", segments: clone(segments), closed };
}
function sliceSegment(s, lo, hi) {
  if (lo < EPS && hi > 1 - EPS) return clone(s);
  let result = clone(s);
  if (hi < 1 - EPS) result = splitSegment(result, hi)[0];
  if (lo > EPS) result = splitSegment(result, lo / hi)[1];
  return result;
}
function sliceChain(segments, lo, hi) {
  const result = [];
  const n = segments.length;
  for (let i = Math.floor(lo); i < Math.ceil(hi - EPS); i++) {
    const start = Math.max(0, lo - i), end = Math.min(1, hi - i);
    if (end - start > EPS) result.push(sliceSegment(segments[mod(i, n)], start, end));
  }
  return result;
}
function parameterAtAngle(s, a) {
  const start = (s.startAngle ?? 0), sweep = s.sweep ?? TAU;
  const along = mod(Math.sign(sweep) * (a - start));
  if (along > Math.abs(sweep) + 1e-8 && Math.abs(sweep) < TAU - 1e-8) return null;
  return Math.max(0, Math.min(1, along / Math.abs(sweep)));
}
function segmentBounds(s) {
  let points;
  if (s.kind === "line") points = [[s.x1, s.y1], [s.x2, s.y2]];
  else if (s.kind === "bezier") {
    if (s.points.length > 16) points = s.points;
    else {
      points = [s.points[0], s.points[s.points.length - 1]];
      const n = s.points.length - 1;
      for (let axis = 0; axis < 2; axis++) {
        const coefficients = Array(n + 1).fill(0);
        for (let i = 0; i <= n; i++) for (let j = 0; j <= n - i; j++) coefficients[i + j] += s.points[i][axis] * choose(n, i) * choose(n - i, j) * (j % 2 ? -1 : 1);
        const roots = polynomialRoots(coefficients.slice(1).map((v, i) => v * (i + 1)));
        points.push(...roots.map(t => curvePoint(s, t)));
      }
    }
  }
  else {
    points = [curvePoint(s, 0), curvePoint(s, 1)];
    const rx = s.radius ?? s.radiusX, ry = s.radius ?? s.radiusY, r = s.rotation ?? 0;
    for (const angle of [Math.atan2(-ry * Math.sin(r), rx * Math.cos(r)), Math.atan2(ry * Math.cos(r), rx * Math.sin(r))]) {
      for (const a of [angle, angle + Math.PI]) {
        const t = parameterAtAngle(s, a);
        if (t !== null) points.push(curvePoint(s, t));
      }
    }
  }
  if (!points?.length || points.some(p => p.some(v => !Number.isFinite(v)))) throw new Error("图形坐标无效");
  const minX = Math.min(...points.map(p => p[0])), minY = Math.min(...points.map(p => p[1]));
  const maxX = Math.max(...points.map(p => p[0])), maxY = Math.max(...points.map(p => p[1]));
  return { minX, minY, maxX, maxY };
}
export function boundsOfEntities(entities) {
  if (!Array.isArray(entities) || !entities.length) throw new Error("请先选择图形");
  const all = [];
  for (const e of entities) {
    if (e.kind === "text") {
      // Conservatively contain all em squares, including rotations.
      const h = Math.abs(e.height ?? e.fontSize ?? 10), w = Math.max(1, String(e.text ?? "").length) * h * 2;
      const r = e.rotation ?? 0;
      all.push(segmentBounds({ kind: "bezier", points: [[-h, -h], [w, -h], [w, 2 * h], [-h, 2 * h]].map(([x, y]) => [e.x + x * Math.cos(r) - y * Math.sin(r), e.y + x * Math.sin(r) + y * Math.cos(r)]) }));
    } else all.push(...segs(e).map(segmentBounds));
  }
  const minX = Math.min(...all.map(v => v.minX)), minY = Math.min(...all.map(v => v.minY));
  const maxX = Math.max(...all.map(v => v.maxX)), maxY = Math.max(...all.map(v => v.maxY));
  return { minX, minY, maxX, maxY, width: maxX - minX, height: maxY - minY, centerX: (minX + maxX) / 2, centerY: (minY + maxY) / 2 };
}
function affineEntity(entity, matrix) {
  const [a, b, c, d, tx, ty] = matrix, k = Math.hypot(a, b), reflection = a * d - b * c < 0;
  const point = p => [a * p[0] + c * p[1] + tx, b * p[0] + d * p[1] + ty];
  const apply = value => {
    const e = clone(value);
    if (e.kind === "rectangle") {
      if (Math.abs(b) < EPS && Math.abs(c) < EPS) {
        const p = point([e.x, e.y]), q = point([e.x + e.width, e.y + e.height]);
        return { ...e, x: Math.min(p[0], q[0]), y: Math.min(p[1], q[1]), width: Math.abs(q[0] - p[0]), height: Math.abs(q[1] - p[1]), radius: (e.radius ?? 0) * k };
      }
      return resultEntity(e, segs(e).map(apply), true);
    }
    if (e.kind === "path") { e.segments = e.segments.map(apply); return e; }
    if (e.kind === "line") {
      [e.x1, e.y1] = point([e.x1, e.y1]); [e.x2, e.y2] = point([e.x2, e.y2]);
    } else if (["circle", "circleArc", "ellipse", "ellipseArc"].includes(e.kind)) {
      [e.cx, e.cy] = point([e.cx, e.cy]);
      const oldRotation = e.rotation ?? 0;
      const axis = [a * Math.cos(oldRotation) + c * Math.sin(oldRotation), b * Math.cos(oldRotation) + d * Math.sin(oldRotation)];
      const newRotation = Math.atan2(axis[1], axis[0]);
      if (e.radius !== undefined) e.radius *= k;
      else { e.radiusX *= k; e.radiusY *= k; }
      if (e.kind === "circleArc") {
        e.startAngle = newRotation + (reflection ? -e.startAngle : e.startAngle);
        e.sweep *= reflection ? -1 : 1;
        delete e.rotation;
      } else {
        e.rotation = newRotation;
        if (e.kind === "ellipseArc" && reflection) { e.startAngle *= -1; e.sweep *= -1; }
      }
    } else if (e.kind === "text") {
      if (reflection) throw new Error("镜像文字须先转成轮廓");
      [e.x, e.y] = point([e.x, e.y]); e.rotation = (e.rotation ?? 0) + Math.atan2(b, a);
      if (e.height !== undefined) e.height *= k;
      if (e.fontSize !== undefined) e.fontSize *= k;
    } else if (Array.isArray(e.points)) e.points = e.points.map(point);
    else throw new Error("此图形暂不支持变换");
    return e;
  };
  return apply(entity);
}
export function transformSketchEntities(entities, spec, { createId } = {}) {
  if (!Array.isArray(entities) || !entities.length) throw new Error("请先选择图形");
  if (!spec) throw new Error("请选择变换方式");
  let matrices;
  if (spec.kind === "align") {
    if (entities.length < 2) throw new Error("对齐至少需要两个图形");
    const direction = spec.direction ?? spec.align, all = boundsOfEntities(entities), bounds = entities.map(e => boundsOfEntities([e]));
    matrices = entities.map(() => [1, 0, 0, 1, 0, 0]);
    if (["distributeX", "distributeY"].includes(direction)) {
      if (entities.length < 3) throw new Error("均布至少需要三个图形");
      const x = direction === "distributeX", lo = x ? "minX" : "minY", hi = x ? "maxX" : "maxY", width = x ? "width" : "height";
      const order = bounds.map((v, i) => i).sort((i, j) => bounds[i][lo] - bounds[j][lo]);
      const gap = (all[hi] - all[lo] - bounds.reduce((sum, b) => sum + b[width], 0)) / (entities.length - 1);
      let next = all[lo];
      for (const i of order) { matrices[i][x ? 4 : 5] = next - bounds[i][lo]; next += bounds[i][width] + gap; }
    } else {
      const key = { left: "minX", right: "maxX", bottom: "minY", top: "maxY", centerX: "centerX", centerY: "centerY" }[direction];
      if (!key) throw new Error("请选择有效的对齐方向");
      bounds.forEach((b, i) => { matrices[i][key.endsWith("X") ? 4 : 5] = all[key] - b[key]; });
    }
  } else {
    let matrix;
    if (["move", "copy"].includes(spec.kind)) matrix = [1, 0, 0, 1, finite(spec.dx ?? 0, "水平位移"), finite(spec.dy ?? 0, "竖直位移")];
    else if (["rotate", "scale"].includes(spec.kind)) {
      const cx = finite(spec.centerX ?? 0, "中心横坐标"), cy = finite(spec.centerY ?? 0, "中心纵坐标");
      const angle = spec.kind === "rotate" ? finite(spec.angle, "旋转角度") * Math.PI / 180 : 0;
      const factor = spec.kind === "scale" ? finite(spec.factor, "缩放倍数") : 1;
      if (factor <= EPS) throw new Error("缩放倍数必须大于零");
      const a = factor * Math.cos(angle), b = factor * Math.sin(angle);
      matrix = [a, b, -b, a, cx - a * cx + b * cy, cy - b * cx - a * cy];
    } else if (spec.kind === "mirror") {
      const p = [finite(spec.x1, "镜像轴起点"), finite(spec.y1, "镜像轴起点")], q = [finite(spec.x2, "镜像轴终点"), finite(spec.y2, "镜像轴终点")];
      const v = sub(q, p), length = Math.hypot(...v);
      if (length < EPS) throw new Error("镜像轴两点不能重合");
      const ux = v[0] / length, uy = v[1] / length, a = 2 * ux * ux - 1, b = 2 * ux * uy, d = 2 * uy * uy - 1;
      matrix = [a, b, b, d, p[0] - a * p[0] - b * p[1], p[1] - b * p[0] - d * p[1]];
    } else throw new Error("未知的变换方式");
    matrices = entities.map(() => matrix);
  }
  return entities.map((e, i) => {
    const result = affineEntity(e, matrices[i]);
    if (spec.kind === "copy") { if (createId) result.id = createId(e.id, i); else delete result.id; }
    boundsOfEntities([result]);
    return result;
  });
}

// Remaining geometric operations use the common exact-curve implementation.
export function intersectionsOfEntities(a, b) { return entityIntersections(a, b); }
export function trimSketchEntity(entity, cutters, point) { return trimEntity(entity, cutters, point); }
export function extendSketchEntity(entity, cutters, point) { return extendEntity(entity, cutters, point); }
export function offsetSketchEntity(entity, amount, point) { return offsetEntity(entity, amount, point); }
export function filletSketchEntities(entities, spec) { return filletEntities(entities, spec); }
export { repairSketchEntities, diagnoseSketchEntities } from "./sketchCadRepair.mjs";

function lineLine(a, b, infinite = false) {
  const p = [a.x1, a.y1], q = [b.x1, b.y1], u = [a.x2 - a.x1, a.y2 - a.y1], v = [b.x2 - b.x1, b.y2 - b.y1];
  const determinant = cross(u, v);
  if (Math.abs(determinant) < EPS * Math.max(1, Math.hypot(...u) * Math.hypot(...v))) return [];
  const w = sub(q, p), t = cross(w, v) / determinant, s = cross(w, u) / determinant;
  return infinite || (t >= -EPS && t <= 1 + EPS && s >= -EPS && s <= 1 + EPS) ? [{ point: add(p, mul(u, t)), t, s }] : [];
}
function conicParameter(e, p) {
  const r = e.rotation ?? 0, x = p[0] - e.cx, y = p[1] - e.cy;
  const rx = e.radius ?? e.radiusX, ry = e.radius ?? e.radiusY;
  return parameterAtAngle(e, Math.atan2((-x * Math.sin(r) + y * Math.cos(r)) / ry, (x * Math.cos(r) + y * Math.sin(r)) / rx));
}
function lineConic(a, b, infinite = false) {
  const r = b.rotation ?? 0, rx = b.radius ?? b.radiusX, ry = b.radius ?? b.radiusY;
  if (!(rx > EPS && ry > EPS)) return [];
  const local = p => { const x = p[0] - b.cx, y = p[1] - b.cy; return [(x * Math.cos(r) + y * Math.sin(r)) / rx, (-x * Math.sin(r) + y * Math.cos(r)) / ry]; };
  const p = local([a.x1, a.y1]), q = local([a.x2, a.y2]), v = sub(q, p);
  const aa = dot(v, v), bb = 2 * dot(p, v), cc = dot(p, p) - 1, discriminant = bb * bb - 4 * aa * cc;
  if (aa < EPS * EPS || discriminant < -1e-12 * Math.max(1, bb * bb, Math.abs(4 * aa * cc))) return [];
  const root = Math.sqrt(Math.max(0, discriminant));
  const roots = root < 1e-10 ? [-bb / (2 * aa)] : [(-bb - root) / (2 * aa), (-bb + root) / (2 * aa)];
  return roots.flatMap(t => {
    if (!infinite && (t < -EPS || t > 1 + EPS)) return [];
    const point = curvePoint(a, t), s = conicParameter(b, point);
    return s === null ? [] : [{ point, t, s }];
  });
}
function circleCircle(a, b) {
  const p = [a.cx, a.cy], q = [b.cx, b.cy], delta = sub(q, p), d = Math.hypot(...delta), ra = a.radius, rb = b.radius;
  if (d < EPS || d > ra + rb + EPS || d < Math.abs(ra - rb) - EPS) return [];
  const x = (ra * ra - rb * rb + d * d) / (2 * d), h = Math.sqrt(Math.max(0, ra * ra - x * x)), u = mul(delta, 1 / d), center = add(p, mul(u, x));
  return (h < EPS ? [center] : [add(center, [-u[1] * h, u[0] * h]), add(center, [u[1] * h, -u[0] * h])]).flatMap(point => {
    const t = conicParameter(a, point), s = conicParameter(b, point);
    return t === null || s === null ? [] : [{ point, t, s }];
  });
}
const choose = (n, k) => { let v = 1; for (let i = 1; i <= k; i++) v = v * (n - i + 1) / i; return v; };
const polynomialValue = (coefficients, t) => coefficients.reduceRight((v, c) => v * t + c, 0);
function polynomialRoots(coefficients) {
  const c = [...coefficients], scale = Math.max(1, ...c.map(Math.abs));
  while (c.length > 1 && Math.abs(c[c.length - 1]) < scale * 1e-13) c.pop();
  if (c.length <= 1) return [];
  if (c.length === 2) { const t = -c[0] / c[1]; return t >= -EPS && t <= 1 + EPS ? [Math.max(0, Math.min(1, t))] : []; }
  const critical = polynomialRoots(c.slice(1).map((v, i) => v * (i + 1))), limits = [0, ...critical.filter(v => v > EPS && v < 1 - EPS), 1].sort((a, b) => a - b), result = [];
  const push = t => { if (!result.some(v => Math.abs(v - t) < 1e-8)) result.push(t); };
  for (const t of limits) if (Math.abs(polynomialValue(c, t)) < scale * 1e-10) push(t);
  for (let i = 1; i < limits.length; i++) {
    let lo = limits[i - 1], hi = limits[i], flo = polynomialValue(c, lo), fhi = polynomialValue(c, hi);
    if (flo * fhi >= 0) continue;
    for (let j = 0; j < 60; j++) { const middle = (lo + hi) / 2, fm = polynomialValue(c, middle); if (flo * fm <= 0) { hi = middle; fhi = fm; } else { lo = middle; flo = fm; } }
    push((lo + hi) / 2);
  }
  return result.sort((a, b) => a - b);
}
function lineBezier(a, b) {
  const u = [a.x2 - a.x1, a.y2 - a.y1], p = [a.x1, a.y1], length2 = dot(u, u), n = b.points.length - 1;
  if (length2 < EPS * EPS) return [];
  const bernstein = b.points.map(q => cross(sub(q, p), u)), coefficients = Array(n + 1).fill(0);
  for (let i = 0; i <= n; i++) for (let j = 0; j <= n - i; j++) coefficients[i + j] += bernstein[i] * choose(n, i) * choose(n - i, j) * (j % 2 ? -1 : 1);
  return polynomialRoots(coefficients).flatMap(s => {
    const point = curvePoint(b, s), t = dot(sub(point, p), u) / length2;
    return t >= -EPS && t <= 1 + EPS ? [{ point, t, s }] : [];
  });
}
function derivative(s, t) {
  if (s.kind === "line") return [s.x2 - s.x1, s.y2 - s.y1];
  if (s.kind === "bezier") { const n = s.points.length - 1; return curvePoint({ kind: "bezier", points: s.points.slice(1).map((p, i) => mul(sub(p, s.points[i]), n)) }, t); }
  const angle = s.startAngle + s.sweep * t, rx = s.radius ?? s.radiusX, ry = s.radius ?? s.radiusY, r = s.rotation ?? 0;
  return [(-rx * Math.sin(angle) * Math.cos(r) - ry * Math.cos(angle) * Math.sin(r)) * s.sweep, (-rx * Math.sin(angle) * Math.sin(r) + ry * Math.cos(angle) * Math.cos(r)) * s.sweep];
}
function numericalIntersections(a, b) {
  const ba = segmentBounds(a), bb = segmentBounds(b), scale = Math.max(1, ba.maxX - ba.minX, ba.maxY - ba.minY, bb.maxX - bb.minX, bb.maxY - bb.minY), tolerance = scale * 1e-8;
  const stack = [{ a, b, al: 0, ah: 1, bl: 0, bh: 1, depth: 0 }], result = [];
  let visited = 0;
  while (stack.length) {
    if (++visited > 100000) throw new Error("曲线存在重合或过于复杂，请先分割重合曲线");
    const cell = stack.pop(), aa = segmentBounds(cell.a), ab = segmentBounds(cell.b);
    if (aa.maxX + tolerance < ab.minX || ab.maxX + tolerance < aa.minX || aa.maxY + tolerance < ab.minY || ab.maxY + tolerance < aa.minY) continue;
    const sizeA = Math.max(aa.maxX - aa.minX, aa.maxY - aa.minY), sizeB = Math.max(ab.maxX - ab.minX, ab.maxY - ab.minY);
    if (cell.depth > 48 || Math.max(sizeA, sizeB) < tolerance * 3) {
      let t = (cell.al + cell.ah) / 2, s = (cell.bl + cell.bh) / 2;
      for (let j = 0; j < 15; j++) {
        const delta = sub(curvePoint(a, t), curvePoint(b, s)), da = derivative(a, t), db = derivative(b, s), determinant = cross(da, db);
        if (Math.abs(determinant) < 1e-14) break;
        const dt = -cross(delta, db) / determinant, ds = -cross(delta, da) / determinant;
        if (t + dt < -1e-7 || t + dt > 1 + 1e-7 || s + ds < -1e-7 || s + ds > 1 + 1e-7) break;
        t = Math.max(0, Math.min(1, t + dt)); s = Math.max(0, Math.min(1, s + ds));
      }
      const p = curvePoint(a, t), q = curvePoint(b, s);
      if (distance(p, q) <= tolerance * 4 && !result.some(v => Math.abs(v.t - t) < 1e-6 && Math.abs(v.s - s) < 1e-6)) result.push({ point: mul(add(p, q), .5), t, s });
    } else if (sizeA >= sizeB) {
      const [left, right] = splitSegment(cell.a, .5), middle = (cell.al + cell.ah) / 2;
      stack.push({ ...cell, a: left, ah: middle, depth: cell.depth + 1 }, { ...cell, a: right, al: middle, depth: cell.depth + 1 });
    } else {
      const [left, right] = splitSegment(cell.b, .5), middle = (cell.bl + cell.bh) / 2;
      stack.push({ ...cell, b: left, bh: middle, depth: cell.depth + 1 }, { ...cell, b: right, bl: middle, depth: cell.depth + 1 });
    }
  }
  return result;
}
function segmentIntersections(a, b) {
  // Coincident spans have infinitely many intersections, rather than a finite
  // set of valid trim parameters. Avoid an unbounded numerical search.
  if (a.kind === b.kind && a.kind === "bezier" && a.points.length === b.points.length
      && (a.points.every((p, i) => distance(p, b.points[i]) < 1e-10)
        || a.points.every((p, i) => distance(p, b.points[b.points.length - i - 1]) < 1e-10))) throw new Error("曲线存在重合区间，请先删除重复曲线或分割重合部分");
  const sameCircle = a.kind === "circleArc" && b.kind === "circleArc" && distance([a.cx, a.cy], [b.cx, b.cy]) < 1e-10 && Math.abs(a.radius - b.radius) < 1e-10;
  const sameEllipse = a.kind === "ellipseArc" && b.kind === "ellipseArc" && distance([a.cx, a.cy], [b.cx, b.cy]) < 1e-10
    && Math.abs(a.radiusX - b.radiusX) < 1e-10 && Math.abs(a.radiusY - b.radiusY) < 1e-10
    && Math.abs(Math.sin((a.rotation ?? 0) - (b.rotation ?? 0))) < 1e-10;
  if (sameCircle || sameEllipse) {
    const result = [];
    for (const t of [0, .5, 1]) {
      const point = curvePoint(a, t), s = conicParameter(b, point);
      if (s !== null && s > 1e-8 && s < 1 - 1e-8) throw new Error("曲线存在重合区间，请先删除重复曲线或分割重合部分");
      if (s !== null && t !== .5 && !result.some(hit => distance(hit.point, point) < 1e-8)) result.push({ point, t, s });
    }
    for (const s of [0, .5, 1]) {
      const point = curvePoint(b, s), t = conicParameter(a, point);
      if (t !== null && t > 1e-8 && t < 1 - 1e-8) throw new Error("曲线存在重合区间，请先删除重复曲线或分割重合部分");
      if (t !== null && s !== .5 && !result.some(hit => distance(hit.point, point) < 1e-8)) result.push({ point, t, s });
    }
    return result;
  }
  if (a.kind === "line" && b.kind === "line") return lineLine(a, b);
  if (a.kind === "line" && b.kind === "bezier") return lineBezier(a, b);
  if (b.kind === "line" && a.kind === "bezier") return lineBezier(b, a).map(v => ({ point: v.point, t: v.s, s: v.t }));
  if (a.kind === "line") return lineConic(a, b);
  if (b.kind === "line") return lineConic(b, a).map(v => ({ point: v.point, t: v.s, s: v.t }));
  if (a.kind === "circleArc" && b.kind === "circleArc") return circleCircle(a, b);
  return numericalIntersections(a, b);
}
function entityIntersections(a, b) {
  const result = [], sa = segs(a), sb = segs(b);
  sa.forEach((as, i) => sb.forEach((bs, j) => {
    for (const v of segmentIntersections(as, bs)) {
      if (!result.some(hit => {
        const delta = Math.abs(hit.a.index + hit.a.t - i - v.t), along = isClosed(a) ? Math.min(delta, Math.abs(sa.length - delta)) : delta;
        return distance(hit.point, v.point) < 1e-7 && along < 1e-6;
      })) result.push({ point: v.point, a: { index: i, t: v.t }, b: { index: j, t: v.s } });
    }
  }));
  return result;
}
function trimEntity(entity, cutters, point) {
  const p = pointValue(point), segments = segs(entity), n = segments.length, closed = isClosed(entity), nearest = nearestPath(entity.kind === "bezier" ? { kind: "path", segments } : entity, p);
  if (!nearest) throw new Error("未找到要修剪的曲线");
  const cuts = [];
  for (const cutter of cutters ?? []) for (const hit of entityIntersections(entity, cutter)) {
    const at = hit.a.index + hit.a.t, value = closed ? mod(at, n) : at;
    if (!cuts.some(v => Math.abs(v - value) < 1e-7 || (closed && n - Math.abs(v - value) < 1e-7))) cuts.push(value);
  }
  cuts.sort((a, b) => a - b);
  const clicked = nearest.index + nearest.t;
  if (closed) {
    if (cuts.length < 2) throw new Error("闭合轮廓至少需要两个不同交点才能修剪");
    let lo = cuts[cuts.length - 1] - n, hi = cuts[0];
    for (let i = 0; i < cuts.length; i++) {
      const a = cuts[i], b = i + 1 < cuts.length ? cuts[i + 1] : cuts[0] + n;
      if (clicked >= a - EPS && clicked <= b + EPS) { lo = a; hi = b; break; }
    }
    return { parts: [resultEntity(entity, sliceChain(segments, hi, lo + n), false)] };
  }
  const internal = cuts.filter(t => t > EPS && t < n - EPS);
  if (!internal.length) throw new Error("曲线与所选边界没有可修剪的交点");
  const limits = [0, ...internal, n];
  let index = limits.findIndex((v, i) => i < limits.length - 1 && clicked >= v - EPS && clicked <= limits[i + 1] + EPS);
  if (index < 0) index = limits.length - 2;
  const parts = [];
  if (limits[index] > EPS) parts.push(resultEntity(entity, sliceChain(segments, 0, limits[index])));
  if (limits[index + 1] < n - EPS) parts.push(resultEntity(entity, sliceChain(segments, limits[index + 1], n)));
  return { parts };
}
function extendEntity(entity, cutters, point) {
  if (isClosed(entity)) throw new Error("闭合轮廓不能延伸，请先打断");
  const p = pointValue(point), segments = segs(entity), start = curvePoint(segments[0], 0), end = curvePoint(segments[segments.length - 1], 1), atStart = distance(p, start) <= distance(p, end), index = atStart ? 0 : segments.length - 1;
  let segment = clone(segments[index]);
  if (segment.kind === "bezier") throw new Error("样条没有唯一的自然延伸，请使用直线或圆弧端段");
  const candidates = [];
  if (segment.kind === "line") {
    const origin = atStart ? curvePoint(segment, 0) : curvePoint(segment, 1), target = atStart ? curvePoint(segment, 1) : curvePoint(segment, 0), direction = sub(origin, target), length = Math.hypot(...direction);
    if (length < EPS) throw new Error("零长度直线不能延伸");
    const unit = mul(direction, 1 / length);
    for (const cutter of cutters ?? []) for (const edge of segs(cutter)) {
      // A ray bounded by the cutter's conservative extent keeps all curve
      // intersection routines on their well-conditioned unit intervals.
      const b = segmentBounds(edge), reach = 2 * Math.max(1, ...[[b.minX, b.minY], [b.maxX, b.maxY], [b.maxX, b.minY], [b.minX, b.maxY]].map(q => distance(origin, q)));
      const ray = line(origin, add(origin, mul(unit, reach)));
      for (const hit of segmentIntersections(ray, edge)) if (hit.t > 1e-8) candidates.push({ advance: hit.t * reach, point: hit.point });
    }
    candidates.sort((a, b) => a.advance - b.advance);
    if (!candidates.length) throw new Error("延伸方向没有遇到所选边界");
    if (atStart) [segment.x1, segment.y1] = candidates[0].point; else [segment.x2, segment.y2] = candidates[0].point;
  } else if (["circleArc", "ellipseArc"].includes(segment.kind)) {
    const full = { ...segment, startAngle: 0, sweep: TAU }, originalEnd = atStart ? segment.startAngle : segment.startAngle + segment.sweep, direction = Math.sign(segment.sweep) * (atStart ? -1 : 1);
    for (const cutter of cutters ?? []) for (const hit of entityIntersections({ kind: "path", segments: [full] }, cutter)) {
      const angle = hit.a.t * TAU, advance = mod(direction * (angle - originalEnd));
      if (advance > 1e-8 && advance + Math.abs(segment.sweep) < TAU - 1e-8) candidates.push({ advance });
    }
    candidates.sort((a, b) => a.advance - b.advance);
    if (!candidates.length) throw new Error("圆弧延伸方向没有遇到所选边界");
    const change = direction * candidates[0].advance;
    if (atStart) { segment.startAngle += change; segment.sweep -= change; } else segment.sweep += change;
  } else throw new Error("此曲线暂不支持精确延伸");
  segments[index] = segment;
  return { parts: [resultEntity(entity, segments)] };
}

function signedArea(segments) {
  return segments.reduce((area, s) => {
    const a = curvePoint(s, 0), b = curvePoint(s, 1);
    if (s.kind === "line") return area + cross(a, b) / 2;
    if (["circleArc", "ellipseArc"].includes(s.kind)) return area + (s.cx * (b[1] - a[1]) - s.cy * (b[0] - a[0]) + (s.radius ?? s.radiusX) * (s.radius ?? s.radiusY) * s.sweep) / 2;
    if (s.kind === "bezier") {
      const n = s.points.length - 1;
      // Integrate the polynomial's Bernstein products analytically, without
      // tessellating the editable boundary to decide its orientation.
      for (let i = 0; i <= n; i++) for (let j = 0; j < n; j++) area += cross(s.points[i], sub(s.points[j + 1], s.points[j])) * choose(n, i) * choose(n - 1, j) / (4 * choose(2 * n - 1, i + j));
      return area;
    }
    throw new Error("此轮廓包含未知曲线");
  }, 0);
}
function offsetSegment(segment, amount) {
  const s = clone(segment);
  if (s.kind === "line") {
    const dx = s.x2 - s.x1, dy = s.y2 - s.y1, length = Math.hypot(dx, dy);
    if (length < EPS) throw new Error("零长度直线不能偏移");
    const ox = -dy / length * amount, oy = dx / length * amount;
    s.x1 += ox; s.x2 += ox; s.y1 += oy; s.y2 += oy;
  } else if (s.kind === "circleArc") {
    s.radius -= Math.sign(s.sweep) * amount;
    if (s.radius <= EPS) throw new Error("偏移距离使圆弧半径小于或等于零");
  } else throw new Error("此曲线不能直接使用解析偏移");
  return s;
}
function supportIntersections(a, b) {
  const full = s => s.kind === "circleArc" ? { ...s, startAngle: 0, sweep: TAU } : s;
  if (a.kind === "line" && b.kind === "line") return lineLine(a, b, true);
  if (a.kind === "line") return lineConic(a, full(b), true);
  if (b.kind === "line") return lineConic(b, full(a), true).map(v => ({ point: v.point, t: v.s, s: v.t }));
  return circleCircle(full(a), full(b));
}
function setSegmentEnd(s, atEnd, p) {
  if (s.kind === "line") { s[atEnd ? "x2" : "x1"] = p[0]; s[atEnd ? "y2" : "y1"] = p[1]; return; }
  let angle = Math.atan2(p[1] - s.cy, p[0] - s.cx) - (s.rotation ?? 0);
  const old = s.startAngle + (atEnd ? s.sweep : 0);
  angle += Math.round((old - angle) / TAU) * TAU;
  const sweep = atEnd ? angle - s.startAngle : s.startAngle + s.sweep - angle;
  if (sweep * s.sweep <= EPS || Math.abs(sweep) > TAU + EPS) throw new Error("偏移距离过大，圆弧在交接处已消失");
  if (!atEnd) s.startAngle = angle;
  s.sweep = sweep;
}
function offsetEntity(entity, amount, point) {
  let value = finite(amount, "偏移距离");
  if (Math.abs(value) < EPS) throw new Error("偏移距离不能为零");
  if (["circle", "circleArc"].includes(entity.kind)) {
    const result = clone(entity);
    if (point) value = (distance(pointValue(point), [entity.cx, entity.cy]) >= entity.radius ? 1 : -1) * Math.abs(value);
    result.radius += value;
    if (result.radius <= EPS) throw new Error("偏移距离使半径小于或等于零");
    return { parts: [result] };
  }
  const segments = segs(entity), closed = isClosed(entity);
  for (let i = 0; i < segments.length - (closed ? 0 : 1); i++) {
    if (distance(curvePoint(segments[i], 1), curvePoint(segments[(i + 1) % segments.length], 0)) > 1e-6) throw new Error("轮廓存在断口，请先补缝连接后再偏移");
  }
  if (point) {
    const p = pointValue(point), hit = nearestPath({ kind: "path", segments }, p);
    const side = cross(curveTangent(segments[hit.index], hit.t), sub(p, hit.point));
    if (Math.abs(side) < EPS) throw new Error("请在曲线的一侧选择偏移位置");
    value = Math.sign(side) * Math.abs(value);
  } else if (closed) value *= signedArea(segments) >= 0 ? -1 : 1;
  if (segments.some(s => ["ellipseArc", "bezier"].includes(s.kind))) return offsetGeneralCurveEntity(entity, segments, value, closed);
  const result = segments.map(s => offsetSegment(s, value));
  const joins = closed ? result.length : result.length - 1;
  for (let i = 0; i < joins; i++) {
    const j = (i + 1) % result.length, a = result[i], b = result[j], aEnd = curvePoint(a, 1), bStart = curvePoint(b, 0);
    if (distance(aEnd, bStart) < 1e-7) continue;
    const corner = curvePoint(segments[i], 1), hits = supportIntersections(a, b).sort((x, y) => distance(x.point, corner) - distance(y.point, corner));
    if (!hits.length) throw new Error("偏移后的相邻曲线无法精确相交，请减小偏移距离或先打断");
    const p = hits[0].point;
    if (distance(p, corner) > Math.max(Math.abs(value), EPS) * 1000) throw new Error("此尖角的偏移交点过远，请减小偏移距离或先倒角");
    setSegmentEnd(a, true, p); setSegmentEnd(b, false, p);
  }
  for (let i = 0; i < result.length; i++) {
    if (result[i].kind === "line" && dot(sub(curvePoint(result[i], 1), curvePoint(result[i], 0)), sub(curvePoint(segments[i], 1), curvePoint(segments[i], 0))) <= EPS) {
      throw new Error("偏移距离过大，相邻角点已交叉或使直线消失");
    }
    for (let j = i + 2; j < result.length; j++) {
      if (closed && i === 0 && j === result.length - 1) continue;
      if (segmentIntersections(result[i], result[j]).length) throw new Error("偏移结果发生自相交，请减小偏移距离或先分割轮廓");
    }
  }
  return { parts: [resultEntity(entity, result, closed)] };
}

function offsetGeneralCurveEntity(entity, segments, amount, closed) {
  const chains = segments.map(s => ["ellipseArc", "bezier"].includes(s.kind) ? offsetCurveSegments(s, amount) : [offsetSegment(s, amount)]), connectors = new Map();
  for (let i = 0; i < chains.length - (closed ? 0 : 1); i++) {
    const j = (i + 1) % chains.length, a = chains[i], b = chains[j], p = curvePoint(a.at(-1), 1), q = curvePoint(b[0], 0);
    if (distance(p, q) < 1e-7) continue;
    const da = curveTangent(segments[i], 1), db = curveTangent(segments[j], 0), turn = cross(da, db), corner = curvePoint(segments[i], 1);
    if (turn * amount < 0) {
      // At an outward corner a circular join is an exact normal-distance
      // boundary, while the original curve pieces retain their certificates.
      const startAngle = Math.atan2(p[1] - corner[1], p[0] - corner[0]), endAngle = Math.atan2(q[1] - corner[1], q[0] - corner[0]), sign = Math.sign(turn);
      const sweep = sign * mod(sign * (endAngle - startAngle));
      if (Math.abs(sweep) > Math.PI + 1e-7) throw new Error("反向尖角不能偏移，请先分割轮廓");
      connectors.set(i, { kind: "circleArc", cx: corner[0], cy: corner[1], radius: Math.abs(amount), startAngle, sweep });
    } else {
      const hits = [];
      a.forEach((as, ai) => b.forEach((bs, bi) => {
        for (const hit of segmentIntersections(as, bs)) hits.push({ ...hit, ai, bi });
      }));
      hits.sort((x, y) => distance(x.point, corner) - distance(y.point, corner));
      if (!hits.length) throw new Error("偏移后的内侧角点无法连接，请减小距离或先分割轮廓");
      const hit = hits[0];
      chains[i] = [...a.slice(0, hit.ai), ...(hit.t > EPS ? [sliceSegment(a[hit.ai], 0, hit.t)] : [])];
      chains[j] = [...(hit.s < 1 - EPS ? [sliceSegment(b[hit.bi], hit.s, 1)] : []), ...b.slice(hit.bi + 1)];
      if (!chains[i].length || !chains[j].length) throw new Error("偏移距离过大，相邻曲线已消失");
    }
  }
  const result = chains.flatMap((chain, i) => connectors.has(i) ? [...chain, connectors.get(i)] : chain);
  for (let i = 0; i < result.length; i++) for (let j = i + 2; j < result.length; j++) {
    if (closed && i === 0 && j === result.length - 1) continue;
    if (segmentIntersections(result[i], result[j]).length) throw new Error("偏移结果发生自相交，请减小偏移距离或先分割轮廓");
  }
  return { parts: [resultEntity(entity, result, closed)] };
}

function lineCorner(first, second, spec, preserveOrder = false) {
  if (first.kind !== "line" || second.kind !== "line") throw new Error("当前圆角和倒角支持直线之间的角点；曲线保持原样");
  const intersections = lineLine(first, second, true);
  if (!intersections.length) throw new Error("两条直线平行或重合，不能倒圆角");
  const vertex = intersections[0].point;
  let a = curvePoint(first, 0), b = curvePoint(second, 1);
  if (!preserveOrder) {
    const firstEnd = curvePoint(first, 1), secondStart = curvePoint(second, 0);
    if (distance(firstEnd, vertex) > distance(a, vertex)) a = firstEnd;
    if (distance(secondStart, vertex) > distance(b, vertex)) b = secondStart;
    if (spec.point) {
      const toward = sub(pointValue(spec.point), vertex);
      const chooseSide = (p, q, fallback) => {
        const dp = dot(sub(p, vertex), toward), dq = dot(sub(q, vertex), toward);
        if (Math.abs(dp - dq) < EPS) return fallback;
        const selected = dp > dq ? p : q;
        // A click on the virtual extension cannot leave a nonexistent branch.
        return Math.max(dp, dq) > EPS && distance(selected, vertex) > EPS ? selected : fallback;
      };
      a = chooseSide(curvePoint(first, 0), firstEnd, a);
      b = chooseSide(secondStart, curvePoint(second, 1), b);
    }
  }
  const av = sub(a, vertex), bv = sub(b, vertex), al = Math.hypot(...av), bl = Math.hypot(...bv);
  if (Math.min(al, bl) < EPS) throw new Error("角点处没有足够长度");
  const u = mul(av, 1 / al), v = mul(bv, 1 / bl), theta = Math.acos(Math.max(-1, Math.min(1, dot(u, v))));
  if (theta < 1e-7 || Math.PI - theta < 1e-7) throw new Error("平行或共线直线不能倒圆角");
  const chamfer = Boolean(spec.chamfer), radius = chamfer ? 0 : finite(spec.radius, "圆角半径");
  const travel = chamfer ? finite(spec.distance ?? spec.radius, "倒角距离") : radius / Math.tan(theta / 2);
  if (travel <= EPS || (!chamfer && radius <= EPS)) throw new Error("圆角半径或倒角距离必须大于零");
  if (travel >= Math.min(al, bl) - EPS) throw new Error("圆角半径或倒角距离超过相邻直线的可用长度");
  const t1 = add(vertex, mul(u, travel)), t2 = add(vertex, mul(v, travel));
  let connector;
  if (chamfer) connector = line(t1, t2);
  else {
    const bisector = add(u, v), norm = Math.hypot(...bisector), center = add(vertex, mul(bisector, radius / Math.sin(theta / 2) / norm));
    const startAngle = Math.atan2(t1[1] - center[1], t1[0] - center[0]), endAngle = Math.atan2(t2[1] - center[1], t2[0] - center[0]);
    const sign = Math.sign(cross(mul(u, -1), v)), sweep = sign * mod(sign * (endAngle - startAngle));
    connector = { kind: "circleArc", cx: center[0], cy: center[1], radius, startAngle, sweep };
  }
  return { segments: [line(a, t1), connector, line(t2, b)], vertex };
}
function filletEntities(entities, spec = {}) {
  if (!Array.isArray(entities) || !entities.length || entities.length > 2) throw new Error("请选择两条曲线，或一个含角点的轮廓");
  if (entities.length === 2) {
    const first = segs(entities[0]), second = segs(entities[1]);
    return { parts: [resultEntity(entities[0], first.length === 1 && second.length === 1 ? curveCorner(first[0], second[0], spec).segments : curveChainsCorner(first, second, spec))] };
  }
  const entity = entities[0], segments = segs(entity), closed = isClosed(entity), candidates = [];
  for (let i = 0; i < segments.length - (closed ? 0 : 1); i++) {
    const j = (i + 1) % segments.length;
    if (distance(curvePoint(segments[i], 1), curvePoint(segments[j], 0)) < 1e-7) {
      const a = curveTangent(segments[i], 1), b = curveTangent(segments[j], 0);
      if (Math.abs(cross(a, b)) > 1e-8 * Math.hypot(...a) * Math.hypot(...b)) candidates.push(i);
    }
  }
  if (!candidates.length) throw new Error("轮廓中没有可编辑的非相切角点");
  const p = spec.point ? pointValue(spec.point) : null;
  candidates.sort((i, j) => p ? distance(curvePoint(segments[i], 1), p) - distance(curvePoint(segments[j], 1), p) : i - j);
  const index = candidates[0], next = (index + 1) % segments.length;
  if (!spec.chamfer && segments.some(s => s.kind === "bezier" || s.kind === "ellipseArc" || s.kind === "circleArc")) {
    const firstCount = closed ? Math.ceil(segments.length / 2) : index + 1, start = closed ? mod(index - firstCount + 1, segments.length) : 0;
    const ordered = closed ? segments.map((_, i) => segments[mod(start + i, segments.length)]) : segments;
    return { parts: [resultEntity(entity, filletOrientedChains(ordered.slice(0, firstCount), ordered.slice(firstCount), spec, curvePoint(segments[index], 1)), closed)] };
  }
  const corner = curveCorner(segments[index], segments[next], spec, true).segments;
  if (next === 0) {
    segments[segments.length - 1] = corner[0]; segments[0] = corner[2]; segments.push(corner[1]);
  } else segments.splice(index, 2, ...corner);
  return { parts: [resultEntity(entity, segments, closed)] };
}

function curveCorner(first, second, spec, preserveOrder = false) {
  if (first.kind === "line" && second.kind === "line") return lineCorner(first, second, spec, preserveOrder);
  let vertex, a = clone(first), b = clone(second);
  if (preserveOrder) vertex = curvePoint(a, 1);
  else {
    const hits = segmentIntersections(first, second), point = spec.point ? pointValue(spec.point) : null;
    if (!hits.length) throw new Error("两条曲线没有可倒圆角的交点");
    hits.sort((x, y) => point ? distance(x.point, point) - distance(y.point, point) : Math.min(x.t, 1 - x.t) + Math.min(x.s, 1 - x.s) - Math.min(y.t, 1 - y.t) - Math.min(y.s, 1 - y.s));
    const hit = hits[0]; vertex = hit.point;
    const keepStart = (s, t) => {
      if (t < EPS) return false;
      if (t > 1 - EPS) return true;
      if (point) {
        const toward = dot(sub(point, vertex), curveTangent(s, t));
        if (Math.abs(toward) > EPS) return toward < 0;
      }
      return distance(curvePoint(s, 0), vertex) >= distance(curvePoint(s, 1), vertex);
    };
    const firstStart = keepStart(first, hit.t), secondStart = keepStart(second, hit.s);
    a = firstStart ? sliceSegment(first, 0, hit.t) : reverseSegment(sliceSegment(first, hit.t, 1));
    b = secondStart ? reverseSegment(sliceSegment(second, 0, hit.s)) : sliceSegment(second, hit.s, 1);
  }
  const amount = finite(spec.chamfer ? spec.distance ?? spec.radius : spec.radius, spec.chamfer ? "倒角距离" : "圆角半径");
  if (amount <= EPS) throw new Error("圆角半径或倒角距离必须大于零");
  return { vertex, segments: spec.chamfer ? curveChamferSegments(a, b, amount) : tangentFilletSegments(a, b, amount) };
}

function curveChainsCorner(first, second, spec) {
  const hits = [], point = spec.point ? pointValue(spec.point) : null;
  first.forEach((a, ai) => second.forEach((b, bi) => {
    for (const hit of segmentIntersections(a, b)) hits.push({ ...hit, ai, bi });
  }));
  if (!hits.length) throw new Error("两条曲线没有可倒圆角的交点");
  hits.sort((a, b) => point ? distance(a.point, point) - distance(b.point, point) : Math.min(a.ai + a.t, first.length - a.ai - a.t) + Math.min(a.bi + a.s, second.length - a.bi - a.s) - Math.min(b.ai + b.t, first.length - b.ai - b.t) - Math.min(b.bi + b.s, second.length - b.bi - b.s));
  const hit = hits[0], vertex = hit.point;
  const keepStart = (chain, index, t) => {
    const along = index + t;
    if (along < EPS) return false;
    if (along > chain.length - EPS) return true;
    if (point) {
      const projection = dot(sub(point, vertex), curveTangent(chain[index], t));
      if (Math.abs(projection) > EPS) return projection < 0;
    }
    return distance(curvePoint(chain[0], 0), vertex) >= distance(curvePoint(chain.at(-1), 1), vertex);
  };
  const reverse = chain => chain.reverse().map(reverseSegment), aStart = keepStart(first, hit.ai, hit.t), bStart = keepStart(second, hit.bi, hit.s);
  const a = aStart ? sliceChain(first, 0, hit.ai + hit.t) : reverse(sliceChain(first, hit.ai + hit.t, first.length));
  const b = bStart ? reverse(sliceChain(second, 0, hit.bi + hit.s)) : sliceChain(second, hit.bi + hit.s, second.length);
  if (!a.length || !b.length) throw new Error("角点处没有足够曲线长度");
  return filletOrientedChains(a, b, spec, vertex);
}

function filletOrientedChains(a, b, spec, vertex) {
  if (spec.chamfer) {
    const rounded = curveCorner(a.at(-1), b[0], spec, true).segments;
    return [...a.slice(0, -1), ...rounded, ...b.slice(1)];
  }
  const radius = finite(spec.radius, "圆角半径");
  if (radius <= EPS) throw new Error("圆角半径必须大于零");
  const da = curveTangent(a.at(-1), 1), db = curveTangent(b[0], 0), turn = Math.atan2(cross(da, db), dot(da, db));
  if (Math.abs(turn) < 1e-7 || Math.PI - Math.abs(turn) < 1e-7) throw new Error("相切或反向曲线没有可倒圆角的角点");
  const smooth = (first, second) => {
    const u = curveTangent(first, 1), v = curveTangent(second, 0);
    return distance(curvePoint(first, 1), curvePoint(second, 0)) < 1e-7 && dot(u, v) > 0 && Math.abs(cross(u, v)) < 1e-8 * Math.hypot(...u) * Math.hypot(...v);
  };
  let minA = a.length - 1, maxB = 0;
  while (minA > 0 && smooth(a[minA - 1], a[minA])) minA--;
  while (maxB < b.length - 1 && smooth(b[maxB], b[maxB + 1])) maxB++;
  let lastError;
  // A spline segment boundary is an editing detail; a legitimate radius may
  // trim across its short terminal segment into an earlier smooth span.
  for (let span = 0; span < a.length - minA + maxB; span++) {
    const candidates = [];
    for (let ai = Math.max(minA, a.length - 1 - span); ai < a.length; ai++) {
      const bi = span - (a.length - 1 - ai);
      if (bi < 0 || bi > maxB) continue;
      try {
        const rounded = tangentFilletSegments(a[ai], b[bi], radius, { turnSign: Math.sign(turn) });
        candidates.push({ segments: [...a.slice(0, ai), ...rounded, ...b.slice(bi + 1)], arc: rounded[1] });
      } catch (error) { lastError = error; }
    }
    if (candidates.length) {
      candidates.sort((x, y) => {
        const score = s => distance(curvePoint(s, 0), vertex) + distance(curvePoint(s, 1), vertex);
        return score(x.arc) - score(y.arc);
      });
      return candidates[0].segments;
    }
  }
  throw lastError ?? new Error("圆角半径超过相邻曲线的可用范围");
}
