export const JOIN_TOLERANCE = 0.01;
export const distance3 = (a, b) => Math.hypot(...a.map((x, i) => x - b[i]));
const newId = () => globalThis.crypto?.randomUUID?.() ?? `path-${Date.now()}-${Math.random().toString(36).slice(2)}`;
const unit = v => { const n = Math.hypot(...v); if (n < 1e-10) throw new Error("方向向量不能为零。"); return v.map(x => x / n); };
export function point3(value) {
  if (!Array.isArray(value) || value.length !== 3 || value.some(x => !Number.isFinite(Number(x)) || Math.abs(Number(x)) > 1e8)) throw new Error("请填写有效的三维坐标。");
  return value.map(Number);
}
export function pathLength(path) {
  const points = path?.points ?? [];
  return points.slice(1).reduce((n, p, i) => n + distance3(points[i], p), 0)
    + (path?.closed && points.length > 2 ? distance3(points.at(-1), points[0]) : 0);
}
export function createPath(points, closed = false, name = "新刀路") {
  const clean = points.map(point3);
  if (closed && clean.length > 2 && distance3(clean[0], clean.at(-1)) < 1e-8) clean.pop();
  if (clean.length < (closed ? 3 : 2) || clean.every(p => distance3(p, clean[0]) < 1e-8)) throw new Error("刀路需要至少两个不同点；闭合刀路需要三个点。");
  return { id: newId(), name, points: clean, closed, origin: "drawn", representation: "sampled-polyline" };
}
export function copyPaths(paths, selectedIds) {
  const ids = new Set(selectedIds);
  const copies = paths.filter(p => ids.has(p.id)).map(p => ({ ...structuredClone(p), id: newId(), name: `${p.name} 副本`, origin: "copy" }));
  if (!copies.length) throw new Error("请先选择刀路。");
  return { paths: [...paths, ...copies], selectedIds: copies.map(p => p.id) };
}
export function transformPaths(paths, selectedIds, { translation = [0, 0, 0], axis = [0, 0, 1], angle = 0, pivot = [0, 0, 0] } = {}) {
  const ids = new Set(selectedIds), t = point3(translation), o = point3(pivot), a = unit(point3(axis));
  const radians = Number(angle) * Math.PI / 180;
  if (!Number.isFinite(radians)) throw new Error("旋转角度无效。");
  const c = Math.cos(radians), s = Math.sin(radians);
  const rotate = p => {
    const dot = p.reduce((n, x, i) => n + x * a[i], 0);
    const cross = [a[1] * p[2] - a[2] * p[1], a[2] * p[0] - a[0] * p[2], a[0] * p[1] - a[1] * p[0]];
    return p.map((x, i) => x * c + cross[i] * s + a[i] * dot * (1 - c));
  };
  return paths.map(p => !ids.has(p.id) ? p : { ...p, origin: "edited", points: p.points.map(q => point3(rotate(q.map((x, i) => x - o[i])).map((x, i) => x + o[i] + t[i]))),
    ...(p.directions ? { directions: p.directions.map(rotate) } : {}), ...(p.normals ? { normals: p.normals.map(rotate) } : {}) });
}
function openPoints(path) {
  const duplicate = path.closed && distance3(path.points[0], path.points.at(-1)) < 1e-8;
  return duplicate ? path.points.slice(0, -1) : path.points;
}
export function splitPath(path, segmentIndex, fraction = 0.5) {
  const points = openPoints(path), count = points.length;
  const i = Number(segmentIndex), t = Number(fraction);
  if (!Number.isInteger(i) || i < 0 || i >= count - (path.closed ? 0 : 1) || !(t > 0 && t < 1)) throw new Error("请选择有效线段，并在其内部打断。");
  const j = (i + 1) % count;
  const p = points[i].map((x, k) => x + (points[j][k] - x) * t);
  const slice = (from, to) => points.slice(from, to).map(p => [...p]);
  const sequences = path.closed ? [[p, ...slice(j), ...slice(0, j), p]] : [[...slice(0, i + 1), p], [p, ...slice(i + 1)]];
  // An edited polyline is independent. Inferred CAD pose samples are not
  // silently interpolated across newly created discontinuities.
  return sequences.map((pts, k) => ({ ...createPath(pts, false, `${path.name} · ${k + 1}`), origin: "edited" }));
}
export function mergePaths(paths, selectedIds, tolerance = JOIN_TOLERANCE) {
  const chosen = paths.filter(p => selectedIds.includes(p.id));
  if (chosen.length !== 2 || chosen.some(p => p.closed)) throw new Error("请选择两条开放刀路进行合并。");
  const [a, b] = chosen;
  let best = null;
  for (const reverseA of [false, true]) for (const reverseB of [false, true]) {
    const pa = reverseA ? [...a.points].reverse() : a.points;
    const pb = reverseB ? [...b.points].reverse() : b.points;
    const gap = distance3(pa.at(-1), pb[0]);
    if (!best || gap < best.gap) best = { pa, pb, gap };
  }
  if (best.gap > tolerance) throw new Error(`两条刀路端点相距 ${best.gap.toFixed(3)} mm，超过 ${tolerance} mm 合并容差。`);
  const points = [...best.pa, ...best.pb.slice(1)].map(p => [...p]);
  const closed = points.length > 3 && distance3(points[0], points.at(-1)) <= tolerance;
  const merged = { ...createPath(points, closed, `${a.name} 合并`), origin: "edited" };
  return { paths: [...paths.filter(p => !selectedIds.includes(p.id)), merged], selectedIds: [merged.id] };
}
export function editPathPoint(path, index, point) {
  if (!Number.isInteger(index) || index < 0 || index >= path.points.length) throw new Error("请选择有效刀路节点。");
  const points = path.points.map(p => [...p]);
  const duplicate = path.closed && distance3(points[0], points.at(-1)) < 1e-8;
  points[index] = point3(point);
  if (duplicate && (index === 0 || index === points.length - 1)) points[index === 0 ? points.length - 1 : 0] = [...points[index]];
  const { directions, normals, ...rest } = path;
  return { ...rest, points, origin: "edited" };
}
export function nearestPathSegment(path, point) {
  const pts = openPoints(path);
  let best = null;
  for (let i = 0; i < pts.length - (path.closed ? 0 : 1); i++) {
    const a = pts[i], b = pts[(i + 1) % pts.length];
    const v = b.map((x, k) => x - a[k]), d = v.reduce((n, x) => n + x * x, 0);
    const t = d ? Math.max(0, Math.min(1, point.reduce((n, x, k) => n + (x - a[k]) * v[k], 0) / d)) : 0;
    const q = a.map((x, k) => x + v[k] * t), distance = distance3(q, point);
    if (!best || distance < best.distance) best = { index: i, fraction: Math.max(0.001, Math.min(0.999, t)), distance };
  }
  return best;
}
export function projectSidePoint(point, plane = "XZ") { return plane === "XY" ? [point[0], point[1]] : plane === "YZ" ? [point[1], point[2]] : [point[0], point[2]]; }
export function unprojectSidePoint(point, plane = "XZ", depth = 0) { return plane === "XY" ? [point[0], point[1], depth] : plane === "YZ" ? [depth, point[0], point[1]] : [point[0], depth, point[1]]; }
