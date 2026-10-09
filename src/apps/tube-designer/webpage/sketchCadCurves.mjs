import { curvePoint, splitSegment, distance } from "./sketchGeometry.mjs";

// Ellipse and polynomial spline parallels are generally not conics or finite
// polynomial curves. Store editable cubic Beziers, with a certified maximum
// pointwise error of 0.01 mm against the mathematical normal offset. Sampling
// only rejects bad candidates; interval derivative bounds certify acceptance.
export const OFFSET_CURVE_TOLERANCE = 0.01;
const EPS = 1e-10;
const add = (a, b) => [a[0] + b[0], a[1] + b[1]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1]];
const mul = (a, v) => [a[0] * v, a[1] * v];
const cross = (a, b) => a[0] * b[1] - a[1] * b[0];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1];
const normal = d => { const speed = Math.hypot(...d); if (speed < EPS) throw new Error("曲线含有零切向或尖点，请先分割后再操作"); return [-d[1] / speed, d[0] / speed]; };
const range = values => [Math.min(...values), Math.max(...values)];
const intervalAdd = (a, b) => [a[0] + b[0], a[1] + b[1]];
const intervalSub = (a, b) => [a[0] - b[1], a[1] - b[0]];
const intervalMul = (a, b) => range([a[0] * b[0], a[0] * b[1], a[1] * b[0], a[1] * b[1]]);
const intervalScale = (a, v) => v >= 0 ? [a[0] * v, a[1] * v] : [a[1] * v, a[0] * v];
const absMax = a => Math.max(Math.abs(a[0]), Math.abs(a[1]));
const absMin = a => a[0] <= 0 && a[1] >= 0 ? 0 : Math.min(Math.abs(a[0]), Math.abs(a[1]));

export function curveDerivative(s, t, order = 1) {
  if (s.kind === "line") return order === 1 ? [s.x2 - s.x1, s.y2 - s.y1] : [0, 0];
  if (s.kind === "bezier") {
    let points = s.points;
    for (let k = 0; k < order; k++) {
      const n = points.length - 1;
      if (!n) return [0, 0];
      points = points.slice(1).map((p, i) => mul(sub(p, points[i]), n));
    }
    return curvePoint({ kind: "bezier", points }, t);
  }
  const sweep = s.sweep ?? Math.PI * 2, angle = (s.startAngle ?? 0) + sweep * t + order * Math.PI / 2;
  const rx = s.radius ?? s.radiusX, ry = s.radius ?? s.radiusY, r = s.rotation ?? 0;
  return [rx * Math.cos(angle) * Math.cos(r) - ry * Math.sin(angle) * Math.sin(r), rx * Math.cos(angle) * Math.sin(r) + ry * Math.sin(angle) * Math.cos(r)].map(v => v * sweep ** order);
}
function restrict(s, lo, hi) {
  let result = s;
  if (hi < 1) result = splitSegment(result, hi)[0];
  if (lo > 0) result = splitSegment(result, lo / hi)[1];
  return result;
}
function trigRange(lo, hi, cosine = false) {
  if (lo > hi) [lo, hi] = [hi, lo];
  if (hi - lo >= Math.PI * 2) return [-1, 1];
  const fn = cosine ? Math.cos : Math.sin, values = [fn(lo), fn(hi)], start = cosine ? 0 : Math.PI / 2;
  for (let k = Math.ceil((lo - start) / Math.PI); k <= Math.floor((hi - start) / Math.PI); k++) values.push(k % 2 ? -1 : 1);
  return range(values);
}
function derivativeBounds(s, lo, hi, order = 1) {
  if (s.kind === "line") return curveDerivative(s, 0, order).map(v => [v, v]);
  if (s.kind === "bezier") {
    let points = s.points;
    for (let k = 0; k < order; k++) {
      const n = points.length - 1;
      if (!n) return [[0, 0], [0, 0]];
      points = points.slice(1).map((p, i) => mul(sub(p, points[i]), n));
    }
    const local = restrict({ kind: "bezier", points }, lo, hi);
    return [0, 1].map(axis => range(local.points.map(p => p[axis])));
  }
  const sweep = s.sweep ?? Math.PI * 2, start = (s.startAngle ?? 0) + order * Math.PI / 2;
  const sine = trigRange(start + sweep * lo, start + sweep * hi), cosine = trigRange(start + sweep * lo, start + sweep * hi, true);
  const rx = s.radius ?? s.radiusX, ry = s.radius ?? s.radiusY, r = s.rotation ?? 0, k = sweep ** order;
  return [intervalScale(intervalAdd(intervalScale(cosine, rx * Math.cos(r)), intervalScale(sine, -ry * Math.sin(r))), k), intervalScale(intervalAdd(intervalScale(cosine, rx * Math.sin(r)), intervalScale(sine, ry * Math.cos(r))), k)];
}
const tangentForms = new WeakMap();
function tangentForm(s) {
  if (s.kind !== "bezier") return { curve: s, derivativeOrder: 1, atStart: 0, atEnd: 0 };
  if (tangentForms.has(s)) return tangentForms.get(s);
  let points = s.points.slice(1).map((p, i) => mul(sub(p, s.points[i]), s.points.length - 1)), atStart = 0, atEnd = 0;
  // Repeated endpoint controls can make the parameter speed vanish without a
  // geometric cusp. Factor D(t)=t^a(1-t)^b Q(t) in the Bernstein basis to use
  // the exact one-sided tangent Q, including the standard spline's tail.
  while (points.length > 1 && points[0].every(v => v === 0)) {
    const n = points.length - 1;
    points = points.slice(1).map((p, i) => mul(p, n / (i + 1))); atStart++;
  }
  while (points.length > 1 && points.at(-1).every(v => v === 0)) {
    const n = points.length - 1;
    points = points.slice(0, -1).map((p, i) => mul(p, n / (n - i))); atEnd++;
  }
  const result = { curve: { kind: "bezier", points }, derivativeOrder: 0, atStart, atEnd };
  tangentForms.set(s, result); return result;
}
function tangentState(s, t) {
  const form = tangentForm(s), q = form.derivativeOrder ? curveDerivative(form.curve, t) : curvePoint(form.curve, t);
  const dq = curveDerivative(form.curve, t, form.derivativeOrder + 1), factor = t ** form.atStart * (1 - t) ** form.atEnd;
  return { q, dq, factor };
}
export function curveTangent(s, t) { return tangentState(s, t).q; }
function parallelState(s, t, amount) {
  const { q, dq, factor } = tangentState(s, t), speed = Math.hypot(...q), n = normal(q);
  const parallelFactor = factor - amount * cross(q, dq) / speed ** 3;
  if (parallelFactor < -EPS || (parallelFactor <= EPS && t > EPS && t < 1 - EPS)) throw new Error("偏移距离使曲线产生尖点或翻转，请减小偏移距离");
  return { point: add(curvePoint(s, t), mul(n, amount)), derivative: mul(q, parallelFactor) };
}
function parallelDerivativeBounds(s, lo, hi, amount) {
  const form = tangentForm(s);
  const d = form.derivativeOrder ? derivativeBounds(form.curve, lo, hi) : [0, 1].map(axis => range(restrict(form.curve, lo, hi).points.map(p => p[axis])));
  const dd = derivativeBounds(form.curve, lo, hi, form.derivativeOrder + 1);
  const minSpeed = Math.hypot(...d.map(absMin)), maxSpeed = Math.hypot(...d.map(absMax));
  if (minSpeed < EPS) return null;
  const numerator = intervalSub(intervalMul(d[0], dd[1]), intervalMul(d[1], dd[0]));
  const curvature = intervalMul(numerator, [1 / maxSpeed ** 3, 1 / minSpeed ** 3]);
  const f = t => t ** form.atStart * (1 - t) ** form.atEnd, values = [f(lo), f(hi)];
  const extreme = form.atStart / (form.atStart + form.atEnd);
  if (extreme > lo && extreme < hi) values.push(f(extreme));
  const factor = intervalSub(range(values), intervalScale(curvature, amount));
  if (factor[0] < -EPS) return null;
  return d.map(v => intervalMul(v, factor));
}
function certify(s, amount, candidate, start, end, lo = start, hi = end, depth = 0) {
  const middle = (lo + hi) / 2, point = parallelState(s, middle, amount).point;
  const error = distance(point, curvePoint(candidate, (middle - start) / (end - start)));
  if (error >= OFFSET_CURVE_TOLERANCE * .95) return false;
  const source = parallelDerivativeBounds(s, lo, hi, amount);
  if (source) {
    const fitted = derivativeBounds(candidate, (lo - start) / (end - start), (hi - start) / (end - start)).map(v => intervalScale(v, 1 / (end - start)));
    const residual = source.map((v, i) => intervalSub(v, fitted[i]));
    // Integrating a bounded residual derivative from the midpoint bounds the
    // entire interval, including extrema between all verification samples.
    const bound = error + (hi - lo) / 2 * Math.hypot(...residual.map(absMax));
    if (bound + 1e-9 <= OFFSET_CURVE_TOLERANCE) return true;
  }
  if (depth >= 12) return false;
  return certify(s, amount, candidate, start, end, lo, middle, depth + 1) && certify(s, amount, candidate, start, end, middle, hi, depth + 1);
}
export function offsetCurveSegments(s, amount) {
  const result = [];
  const fit = (lo, hi, depth) => {
    const a = parallelState(s, lo, amount), b = parallelState(s, hi, amount), span = (hi - lo) / 3;
    const candidate = { kind: "bezier", points: [a.point, add(a.point, mul(a.derivative, span)), sub(b.point, mul(b.derivative, span)), b.point] };
    // An early midpoint rejection avoids expensive interval certification for
    // an obviously inadequate span. Acceptance still requires the certificate.
    const close = [.25, .5, .75].every(t => distance(parallelState(s, lo + (hi - lo) * t, amount).point, curvePoint(candidate, t)) < OFFSET_CURVE_TOLERANCE * .8);
    if (close && certify(s, amount, candidate, lo, hi)) { result.push(candidate); return; }
    if (depth >= 18 || result.length > 4096) throw new Error("曲线过于复杂，无法在 0.01 mm 误差内偏移，请先分割或减小距离");
    const middle = (lo + hi) / 2;
    fit(lo, middle, depth + 1); fit(middle, hi, depth + 1);
  };
  fit(0, 1, 0);
  return result;
}

function centerAt(s, t, signedRadius) {
  const { q, dq, factor } = tangentState(s, t), speed = Math.hypot(...q);
  return { point: add(curvePoint(s, t), mul(normal(q), signedRadius)), derivative: mul(q, factor - signedRadius * cross(q, dq) / speed ** 3) };
}
export function tangentFilletSegments(first, second, radius, { turnSign } = {}) {
  const da = tangentState(first, 1).q, db = tangentState(second, 0).q;
  normal(da); normal(db);
  const turn = Math.atan2(cross(da, db), dot(da, db));
  if (!turnSign && (Math.abs(turn) < 1e-7 || Math.PI - Math.abs(turn) < 1e-7)) throw new Error("相切或反向曲线没有可倒圆角的角点");
  const sign = turnSign ?? Math.sign(turn), signedRadius = radius * sign, candidates = [];
  const scale = Math.max(1, radius, distance(curvePoint(first, 0), curvePoint(first, 1)), distance(curvePoint(second, 0), curvePoint(second, 1)));
  const tolerance = Math.max(1e-9, scale * 1e-10);
  for (const seedA of [.99, .9, .7, .5, .3, .1, .01]) for (const seedB of [.01, .1, .3, .5, .7, .9, .99]) {
    let t = seedA, u = seedB;
    for (let iteration = 0; iteration < 40; iteration++) {
      const a = centerAt(first, t, signedRadius), b = centerAt(second, u, signedRadius), delta = sub(a.point, b.point), residual = Math.hypot(...delta);
      if (residual < tolerance) break;
      const determinant = cross(a.derivative, b.derivative);
      if (Math.abs(determinant) < 1e-12) break;
      const dt = -cross(delta, b.derivative) / determinant, du = -cross(delta, a.derivative) / determinant;
      let advanced = false;
      for (let factor = 1; factor > 1e-5; factor *= .5) {
        const nextT = t + dt * factor, nextU = u + du * factor;
        if (nextT <= EPS || nextT >= 1 - EPS || nextU <= EPS || nextU >= 1 - EPS) continue;
        if (distance(centerAt(first, nextT, signedRadius).point, centerAt(second, nextU, signedRadius).point) >= residual) continue;
        t = nextT; u = nextU; advanced = true; break;
      }
      if (!advanced) break;
    }
    const a = centerAt(first, t, signedRadius), b = centerAt(second, u, signedRadius);
    if (distance(a.point, b.point) > tolerance || t <= EPS || t >= 1 - EPS || u <= EPS || u >= 1 - EPS) continue;
    const p = curvePoint(first, t), q = curvePoint(second, u), center = mul(add(a.point, b.point), .5);
    const startAngle = Math.atan2(p[1] - center[1], p[0] - center[0]), endAngle = Math.atan2(q[1] - center[1], q[0] - center[0]);
    const sweep = sign * ((sign * (endAngle - startAngle) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2));
    if (Math.abs(sweep) < 1e-7 || Math.abs(sweep) > Math.PI + 1e-7 || candidates.some(v => Math.abs(v.t - t) + Math.abs(v.u - u) < 1e-7)) continue;
    candidates.push({ t, u, arc: { kind: "circleArc", cx: center[0], cy: center[1], radius, startAngle, sweep } });
  }
  // Choose the local solution which removes the least from the selected ends.
  candidates.sort((a, b) => (1 - a.t + a.u) - (1 - b.t + b.u));
  if (!candidates.length) throw new Error("圆角半径超过相邻曲线的可用范围，请减小半径或选择其他角点");
  const hit = candidates[0];
  return [restrict(first, 0, hit.t), hit.arc, restrict(second, hit.u, 1)];
}

function curveLength(s, lo, hi) {
  const speed = t => Math.hypot(...curveDerivative(s, t));
  const integrate = (a, b, fa, fm, fb, whole, depth) => {
    const m = (a + b) / 2, l = speed((a + m) / 2), r = speed((m + b) / 2);
    const left = (m - a) * (fa + 4 * l + fm) / 6, right = (b - m) * (fm + 4 * r + fb) / 6;
    if (depth >= 16 || Math.abs(left + right - whole) < 1e-8) return left + right + (left + right - whole) / 15;
    return integrate(a, m, fa, l, fm, left, depth + 1) + integrate(m, b, fm, r, fb, right, depth + 1);
  };
  const fa = speed(lo), fm = speed((lo + hi) / 2), fb = speed(hi);
  return integrate(lo, hi, fa, fm, fb, (hi - lo) * (fa + 4 * fm + fb) / 6, 0);
}
export function curveChamferSegments(first, second, amount) {
  if (curveLength(first, 0, 1) <= amount || curveLength(second, 0, 1) <= amount) throw new Error("倒角距离超过相邻曲线的可用长度");
  const locate = (s, atEnd) => {
    let lo = 0, hi = 1;
    for (let i = 0; i < 48; i++) {
      const t = (lo + hi) / 2, length = atEnd ? curveLength(s, t, 1) : curveLength(s, 0, t);
      if ((length > amount) === atEnd) lo = t; else hi = t;
    }
    return (lo + hi) / 2;
  };
  const t = locate(first, true), u = locate(second, false), p = curvePoint(first, t), q = curvePoint(second, u);
  return [restrict(first, 0, t), { kind: "line", x1: p[0], y1: p[1], x2: q[0], y2: q[1] }, restrict(second, u, 1)];
}
