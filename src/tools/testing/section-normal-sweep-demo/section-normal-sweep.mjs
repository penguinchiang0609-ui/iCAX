const turn = 2 * Math.PI;
const angleTolerance = 1e-5;
const parameterTolerance = 1e-8;
const sections = new WeakMap();

const add = (p, q) => [p[0] + q[0], p[1] + q[1]];
const sub = (p, q) => [p[0] - q[0], p[1] - q[1]];
const scale = (p, s) => [p[0] * s, p[1] * s];
const dot = (p, q) => p[0] * q[0] + p[1] * q[1];
const cross = (p, q) => p[0] * q[1] - p[1] * q[0];
const left = p => [-p[1], p[0]];
const length = p => Math.hypot(...p);
const unit = p => scale(p, 1 / length(p));
const distance = (p, q) => length(sub(p, q));
const clamp = t => Math.max(0, Math.min(1, t));
const svg = p => `${p[0]},${-p[1]}`;

export function pointAt(curve, t) {
  if (curve.kind === 'line') return add(curve.a, scale(sub(curve.b, curve.a), t));
  const a = curve.a0 + t * curve.sweep;
  return add(curve.center, scale([Math.cos(a), Math.sin(a)], curve.radius));
}

function tangentAt(curve, t) {
  if (curve.kind === 'line') return unit(sub(curve.b, curve.a));
  const a = curve.a0 + t * curve.sweep;
  return scale([-Math.sin(a), Math.cos(a)], Math.sign(curve.sweep));
}

function inward(curve, t) {
  return scale(left(tangentAt(curve, t)), curve.materialSide);
}

function tail(curve, a, b) {
  if (curve.kind === 'line') return 'L' + svg(pointAt(curve, b));
  const change = curve.sweep * (b - a);
  if (Math.abs(change) >= turn - parameterTolerance)
    return tail(curve, a, (a + b) / 2) + tail(curve, (a + b) / 2, b);
  return `A${curve.radius},${curve.radius} 0 ${Math.abs(change) > Math.PI ? 1 : 0} ${change < 0 ? 1 : 0} ${svg(pointAt(curve, b))}`;
}

export function curvePath(curve, a = 0, b = 1) {
  return 'M' + svg(pointAt(curve, a)) + tail(curve, a, b);
}

function curveArea(curve, a = 0, b = 1) {
  const p = pointAt(curve, a), q = pointAt(curve, b);
  if (curve.kind === 'line') return cross(p, q) / 2;
  return (cross(curve.center, sub(q, p)) + curve.radius ** 2 * curve.sweep * (b - a)) / 2;
}

function closedPath(curves) {
  return 'M' + svg(pointAt(curves[0], 0)) + curves.map(c => tail(c, 0, 1)).join('') + 'Z';
}

function polygonArea(points) {
  return Math.abs(points.reduce((sum, point, i) => sum + cross(point, points[(i + 1) % points.length]), 0)) / 2;
}

function polygonPath(points) {
  return 'M' + points.map(svg).join('L') + 'Z';
}

function prepare(input) {
  let known = sections.get(input);
  if (known) return known;
  const ids = new Set();
  const rings = input.rings.map(ring => {
    let signedArea = 0;
    for (const curve of ring.curves) {
      if (ids.has(String(curve.id))) throw new Error('轮廓曲线编号重复');
      ids.add(String(curve.id));
      if (!['line', 'arc'].includes(curve.kind)) throw new Error('当前 Demo 支持直线和圆弧');
      signedArea += curveArea(curve);
    }
    if (!Number.isFinite(signedArea) || !signedArea) throw new Error('无效的闭合轮廓');
    const materialSide = Math.sign(signedArea) * (ring.role === 'outer' ? 1 : -1);
    return { ...ring, signedArea, curves: ring.curves.map(curve => ({ ...curve, role: ring.role, materialSide })) };
  });
  const guides = rings.filter(ring => ring.role === 'outer').flatMap(ring => ring.curves);
  const others = rings.filter(ring => ring.role === 'inner');
  const curves = rings.flatMap(ring => ring.curves);
  const size = Math.max(1, ...curves.map(c => c.kind === 'arc' ? c.radius : distance(c.b, c.a)));
  const tolerance = Math.max(2e-7, size * 2e-8, input.nativeToleranceMm || 0);
  for (const ring of rings) {
    if (ring.curves.some((curve, i) => distance(pointAt(curve, 1), pointAt(ring.curves[(i + 1) % ring.curves.length], 0)) > tolerance))
      throw new Error('原轮廓在导入公差内未闭合');
  }
  const area = rings.reduce((sum, r) => sum + Math.abs(r.signedArea) * (r.role === 'outer' ? 1 : -1), 0);
  known = { rings, guides, others, curves, size, tolerance, area };
  sections.set(input, known);
  return known;
}

function arcParameter(curve, angle, tolerance = parameterTolerance) {
  const oriented = (angle - curve.a0) * Math.sign(curve.sweep);
  const delta = ((oriented % turn) + turn) % turn;
  const t = (delta > turn - tolerance ? 0 : delta) / Math.abs(curve.sweep);
  return t <= 1 + tolerance ? clamp(t) : null;
}

function intersections(model, curve, p, n) {
  const hits = [];
  const addHit = (d, u) => hits.push({ d, u, curve, point: add(p, scale(n, d)), outward: -dot(n, inward(curve, u)) });
  if (curve.kind === 'line') {
    const edge = sub(curve.b, curve.a);
    const det = cross(n, edge);
    if (Math.abs(det) <= length(edge) * 1e-12) return hits;
    const v = sub(curve.a, p);
    const u = cross(v, n) / det;
    const slack = model.tolerance / length(edge);
    if (u >= -slack && u <= 1 + slack) addHit(cross(v, edge) / det, clamp(u));
    return hits;
  }
  const fromCenter = sub(p, curve.center);
  const along = dot(fromCenter, n);
  const perpendicular = cross(fromCenter, n);
  const squared = curve.radius ** 2 - perpendicular ** 2;
  if (squared < -curve.radius * model.tolerance) return hits;
  const root = Math.sqrt(Math.max(0, squared));
  for (const d of [-along - root, -along + root]) {
    const v = add(fromCenter, scale(n, d));
    const u = arcParameter(curve, Math.atan2(v[1], v[0]), model.tolerance / (curve.radius * Math.abs(curve.sweep)));
    if (u !== null) addHit(d, u);
  }
  return hits;
}

function firstExit(model, p, n) {
  const hits = model.curves.flatMap(curve => intersections(model, curve, p, n))
    .filter(hit => hit.d > model.tolerance).sort((a, b) => a.d - b.d);
  for (let i = 0; i < hits.length;) {
    let j = i + 1;
    while (j < hits.length && hits[j].d - hits[i].d <= model.tolerance) ++j;
    const group = hits.slice(i, j);
    const exits = group.filter(hit => hit.outward > 1e-9);
    // A ray touching a corner can have entry and exit at the same distance.
    if (exits.length && !group.some(hit => hit.outward < -1e-9)) return exits[0];
    i = j;
  }
  return null;
}

export function castAt(input, guideId, t) {
  const model = prepare(input);
  const guide = model.guides.find(curve => String(curve.id) === String(guideId));
  if (!guide) throw new Error('找不到导向皮边');
  const point = pointAt(guide, clamp(t)), normal = inward(guide, clamp(t));
  const hit = firstExit(model, point, normal);
  return { point, normal, exit: hit?.point ?? null, exitCurveId: hit?.curve.id ?? null, distance: hit?.d ?? null };
}

function eventParameters(model, guide) {
  // Between vertex alignments and target tangencies, the first-exit branch
  // is invariant. A witness ray identifies that branch once per interval.
  const events = [0, 1];
  const push = t => {
    if (t !== null && t > parameterTolerance && t < 1 - parameterTolerance) events.push(t);
  };
  if (guide.kind === 'line') {
    const v = sub(guide.b, guide.a), v2 = dot(v, v), tangent = unit(v);
    const project = p => push(dot(sub(p, guide.a), v) / v2);
    for (const curve of model.curves) {
      project(pointAt(curve, 0));
      project(pointAt(curve, 1));
      if (curve.kind !== 'arc') continue;
      for (const sign of [-1, 1]) {
        const radial = scale(tangent, sign);
        if (arcParameter(curve, Math.atan2(radial[1], radial[0])) !== null)
          project(add(curve.center, scale(radial, curve.radius)));
      }
    }
  } else {
    const addDirection = angle => {
      push(arcParameter(guide, angle));
      push(arcParameter(guide, angle + Math.PI));
    };
    for (const curve of model.curves) {
      for (const t of [0, 1]) {
        const v = sub(pointAt(curve, t), guide.center);
        if (length(v) > model.tolerance) addDirection(Math.atan2(v[1], v[0]));
      }
      if (curve.kind === 'line') {
        const v = sub(curve.b, curve.a);
        addDirection(Math.atan2(v[1], v[0]));
      } else {
        const v = sub(curve.center, guide.center), d = length(v);
        if (d <= curve.radius) continue;
        const a = Math.atan2(v[1], v[0]), delta = Math.asin(curve.radius / d);
        addDirection(a - delta);
        addDirection(a + delta);
      }
    }
  }
  events.sort((a, b) => a - b);
  return events.filter((t, i) => i === 0 || t - events[i - 1] > parameterTolerance);
}

function limitHit(model, guide, target, t, witness) {
  if (!target) return null;
  const p = pointAt(guide, t), n = inward(guide, t);
  const hits = intersections(model, target, p, n).filter(hit => hit.d > model.tolerance && hit.outward >= -1e-8);
  return hits.sort((a, b) => Math.abs(a.u - witness.u) - Math.abs(b.u - witness.u))[0] ?? null;
}

function guideSpans(model, guide) {
  const events = eventParameters(model, guide);
  const spans = [];
  for (let i = 1; i < events.length; ++i) {
    const t0 = events[i - 1], t1 = events[i], mid = (t0 + t1) / 2;
    const hit = firstExit(model, pointAt(guide, mid), inward(guide, mid));
    const target = hit?.curve ?? null;
    const start = limitHit(model, guide, target, t0, hit), end = limitHit(model, guide, target, t1, hit);
    const prior = spans.at(-1);
    if (prior && prior.target === target && (!target || (prior.end && start && distance(prior.end.point, start.point) <= model.tolerance * 2))) {
      prior.t1 = t1;
      prior.end = end;
    } else spans.push({ guide, t0, t1, target, start, end, hit });
  }
  for (const span of spans) {
    span.kind = !span.target || !span.start || !span.end || (model.others.length && span.target.role !== 'inner') ? 'stop' : 'continuous';
    span.reason = span.kind === 'continuous' ? '法向进入材料，对侧出点可连续追踪'
      : span.target?.role === 'outer' ? '内壁对侧结束，法向射线穿到另一段外壁'
      : '区间的首个出点无法连续确定';
  }
  return spans;
}

function joins(model, a, b) {
  if (a.kind !== 'continuous' || b.kind !== 'continuous') return false;
  const eps = model.tolerance * 3;
  if (distance(pointAt(a.guide, a.t1), pointAt(b.guide, b.t0)) > eps || distance(a.end.point, b.start.point) > eps)
    return false;
  const sideA = Math.sign(a.end.u - a.start.u);
  const sideB = Math.sign(b.end.u - b.start.u);
  return sideA !== 0 && sideB !== 0 &&
    dot(tangentAt(a.guide, a.t1), tangentAt(b.guide, b.t0)) > 1 - angleTolerance &&
    dot(scale(tangentAt(a.target, a.end.u), sideA), scale(tangentAt(b.target, b.start.u), sideB)) > 1 - angleTolerance;
}

function spanArea(span) {
  const p = pointAt(span.guide, span.t0), q = pointAt(span.guide, span.t1);
  return Math.abs(curveArea(span.guide, span.t0, span.t1) + cross(q, span.end.point) / 2
    - curveArea(span.target, span.start.u, span.end.u) + cross(span.start.point, p) / 2);
}

function band(group) {
  const first = group[0], last = group.at(-1);
  return 'M' + svg(pointAt(first.guide, first.t0)) +
    group.map(span => tail(span.guide, span.t0, span.t1)).join('') +
    'L' + svg(last.end.point) +
    [...group].reverse().map(span => tail(span.target, span.end.u, span.start.u)).join('') + 'Z';
}

function coversInner(model, spans) {
  for (const ring of model.others) for (const curve of ring.curves) {
    const intervals = spans.filter(s => s.kind === 'continuous' && s.target === curve)
      .map(s => [Math.min(s.start.u, s.end.u), Math.max(s.start.u, s.end.u)]).sort((a, b) => a[0] - b[0]);
    let cursor = 0;
    const eps = model.tolerance * 3 / (curve.kind === 'line' ? distance(curve.a, curve.b) : curve.radius * Math.abs(curve.sweep));
    for (const [a, b] of intervals) {
      if (Math.abs(a - cursor) > eps || b <= a) return false;
      cursor = b;
    }
    if (1 - cursor > eps) return false;
  }
  return true;
}

function wallRegions(model, guides) {
  const all = guides.flatMap(g => g.spans);
  const valid = all.filter(span => span.kind === 'continuous');
  if (!valid.length || !coversInner(model, all)) return [];
  const cyclic = valid.length === all.length && all.every((s, i) => joins(model, s, all[(i + 1) % all.length]));
  if (cyclic) return [{
    kind: 'continuous', label: '闭合连续区', path: model.rings.map(ring => closedPath(ring.curves)).join(''),
    owners: model.guides.map(c => c.id), area: valid.reduce((sum, span) => sum + spanArea(span), 0),
  }];

  const startIndex = all.findIndex((span, i) => !joins(model, all[(i + all.length - 1) % all.length], span));
  const ordered = [...all.slice(startIndex), ...all.slice(0, startIndex)];
  const groups = [];
  let group = [];
  for (const span of ordered) {
    if (group.length && !joins(model, group.at(-1), span)) { groups.push(group); group = []; }
    if (span.kind === 'continuous') group.push(span);
  }
  if (group.length) groups.push(group);
  const regions = groups.map(parts => ({
    kind: 'wall', label: '直壁区', path: band(parts), owners: [...new Set(parts.map(p => p.guide.id))],
    area: parts.reduce((sum, part) => sum + spanArea(part), 0),
  }));
  for (let i = 0; i < groups.length; ++i) {
    const a = groups[i].at(-1), b = groups[(i + 1) % groups.length][0];
    const nextGuide = model.guides[(model.guides.indexOf(a.guide) + 1) % model.guides.length];
    const vertex = pointAt(a.guide, 1);
    if (b.guide !== nextGuide || a.guide.kind !== 'line' || b.guide.kind !== 'line' ||
        a.target.kind !== 'line' || b.target.kind !== 'line' ||
        Math.abs(dot(inward(a.guide, 1), inward(b.guide, 0))) > angleTolerance ||
        distance(vertex, pointAt(b.guide, 0)) > model.tolerance ||
        distance(a.end.point, b.start.point) > model.tolerance * 3) continue;
    const points = [pointAt(a.guide, a.t1), vertex, pointAt(b.guide, b.t0), a.end.point];
    const area = polygonArea(points);
    if (area <= model.tolerance ** 2) continue;
    regions.push({ kind: 'shared-corner', label: '角部重合区', path: polygonPath(points),
      owners: [a.guide.id, b.guide.id], area });
  }
  return regions;
}

function collinear(model, a, b) {
  if (a.kind !== 'line' || b.kind !== 'line') return false;
  const direction = tangentAt(a, 0);
  return Math.abs(cross(direction, tangentAt(b, 0))) < angleTolerance &&
    Math.abs(cross(direction, sub(b.a, a.a))) <= model.tolerance * 3;
}

function branchPair(model, guides) {
  // A single outer-ring polygon can have two coplanar pieces of the flange
  // exit skin separated by the web. Continue that support only when unique.
  if (model.others.length || model.rings.length !== 1) return null;
  const candidates = [];
  for (const { curve, spans } of guides) {
    if (curve.kind !== 'line') continue;
    for (let a = 0; a + 2 < spans.length; ++a) {
      const before = spans[a];
      for (let b = a + 2; b < spans.length; ++b) {
        const after = spans[b], gap = spans.slice(a + 1, b);
        if (!before.end || !after.start || before.target === after.target ||
            !collinear(model, before.target, after.target)) continue;
        if (!gap.every(s => s.start && s.end && s.hit.d > Math.max(before.end.d, after.start.d) + model.tolerance))
          continue;
        const sequence = [before, ...gap, after];
        const jumps = sequence.slice(1).flatMap((span, i) =>
          distance(sequence[i].end.point, span.start.point) > model.tolerance * 3 ? [i] : []);
        if (jumps.length !== 2) continue;
        const deeperSpans = sequence.slice(jumps[0] + 1, jumps[1] + 1);
        candidates.push({ a: before.end.point, b: after.start.point, guide: before.guide, deeperSpans,
          reason: '翼板下皮在腹板处中断。两侧下皮共线，延续得到分叉界面。' });
      }
    }
  }
  const vertices = model.curves.map(c => pointAt(c, 0));
  const proven = [];
  for (const candidate of candidates) {
    const ai = vertices.findIndex(p => distance(p, candidate.a) <= model.tolerance * 3);
    const bi = vertices.findIndex(p => distance(p, candidate.b) <= model.tolerance * 3);
    if (ai < 0 || bi < 0 || ai === bi) continue;
    const lo = Math.min(ai, bi), hi = Math.max(ai, bi);
    if (hi - lo <= 1 || hi - lo >= vertices.length - 1) continue;
    const segment = sub(vertices[hi], vertices[lo]), n = unit(segment);
    const touchesBoundaryInside = model.curves.some(curve => {
      if (curve.kind === 'line' && Math.abs(cross(n, tangentAt(curve, 0))) < 1e-12) {
        if (Math.abs(cross(n, sub(curve.a, vertices[lo]))) > model.tolerance) return false;
        const a = dot(sub(curve.a, vertices[lo]), n), b = dot(sub(curve.b, vertices[lo]), n);
        return Math.min(Math.max(a, b), length(segment)) - Math.max(Math.min(a, b), 0) > model.tolerance;
      }
      return intersections(model, curve, vertices[lo], n).some(hit => hit.d > model.tolerance &&
        hit.d < length(segment) - model.tolerance);
    });
    if (touchesBoundaryInside) continue;
    const parts = [model.curves.slice(lo, hi), [...model.curves.slice(hi), ...model.curves.slice(0, lo)]];
    const mid = pointAt(candidate.guide, 0.5);
    const flangeIndex = parts.findIndex(curves => curves.includes(candidate.guide));
    if (flangeIndex < 0 || Math.abs(cross(segment, sub(mid, vertices[lo]))) <= model.tolerance) continue;
    const regions = parts.map((curves, i) => {
      const closingArea = cross(pointAt(curves.at(-1), 1), pointAt(curves[0], 0)) / 2;
      return { kind: i === flangeIndex ? 'wall' : 'branch',
        label: i === flangeIndex ? '翼板区' : '腹板区', path: closedPath(curves),
        owners: i === flangeIndex ? [candidate.guide.id] : curves.map(curve => curve.id),
        area: Math.abs(curves.reduce((sum, curve) => sum + curveArea(curve), closingArea)) };
    });
    if (regions.every(r => r.area > model.tolerance ** 2) &&
        Math.abs(regions[0].area + regions[1].area - model.area) <= model.tolerance * model.size * 10)
      proven.push({ ...candidate, regions, webBoundary: parts[1 - flangeIndex] });
  }
  if (proven.length !== 1) return null;
  const result = proven[0];
  for (const span of result.deeperSpans) {
    span.kind = 'branch';
    span.reason = '实际法向出点跳到腹板底部。虚拟界面用于划分翼板与腹板。';
  }
  return result;
}

function inspectSection(input) {
  const model = prepare(input);
  const guides = model.guides.map(curve => ({ id: curve.id, curve, spans: guideSpans(model, curve) }));
  const branch = branchPair(model, guides);
  return { model, guides, branch,
    partition: branch?.regions ?? (model.others.length === 1 && model.rings.length === 2 ? wallRegions(model, guides) : []) };
}

export function analyzeSection(input) {
  const { model, guides, branch, partition } = inspectSection(input);
  const regions = partition
    .map((region, i) => ({ id: i + 1, ...region }));
  const areaResidual = Math.abs(model.area - regions.reduce((sum, region) => sum + region.area, 0));
  const complete = regions.length > 0 && areaResidual <= model.tolerance * model.size * 10;
  const publicGuides = guides.map(({ id, curve, spans }, index) => {
    const events = spans.slice(1).map((next, i) => {
      const prior = spans[i];
      const kind = prior.kind === 'branch' || next.kind === 'branch' ? 'branch' : joins(model, prior, next) ? 'transition' : 'stop';
      const reason = kind === 'branch' ? '实际出点跳变，进入或离开分叉'
        : kind === 'transition' ? '对侧虽换边，出点沿相切皮边继续'
        : '对侧出点跳变，结束当前扫掠';
      return { t: next.t0, kind, reason, beforeDistance: prior.end?.d, afterDistance: next.start?.d };
    });
    const nextGuide = guides[(index + 1) % guides.length];
    if (joins(model, spans.at(-1), nextGuide.spans[0]))
      events.push({ t: 1, kind: 'transition', reason: '入口法向和对侧出点都能连续跨过接缝',
        nextGuideId: nextGuide.id });
    return { id, curve, events,
      spans: spans.map(({ t0, t1, kind, reason, target }) => ({ t0, t1, kind, reason, targetId: target?.id ?? null })) };
  });
  return {
    guides: publicGuides,
    regions: complete ? regions : [],
    branches: branch ? [{ a: branch.a, b: branch.b, reason: branch.reason }] : [],
    summary: { status: complete ? 'complete' : 'partial', regionCount: complete ? regions.length : 0,
      cornerCount: complete ? regions.filter(r => r.kind === 'shared-corner').length : 0,
      branchCount: branch ? 1 : 0, areaResidual,
      reason: complete ? '分区面积与截面材料面积一致' : '当前法向扫掠未形成完整可验证分区' },
  };
}

function intersectSupport(point, direction, line) {
  const along = sub(line.b, line.a);
  const det = cross(direction, along);
  if (Math.abs(det) <= length(along) * 1e-12) return null;
  const offset = sub(line.a, point);
  return { point: add(point, scale(direction, cross(offset, along) / det)),
    parameter: cross(offset, direction) / det };
}

function betweenEnds(hit, model, line) {
  const slack = model.tolerance * 3 / length(sub(line.b, line.a));
  return hit && hit.parameter >= -slack && hit.parameter <= 1 + slack;
}

function closedWallCoverage(model, guides, partition) {
  if (partition.length === 1 && partition[0].kind === 'continuous') {
    return { walls: [{ id: 1, label: '连续壁区', guideIds: model.guides.map(guide => guide.id), polygon: [] }],
      junctions: [], patchRules: [] };
  }
  const cores = partition.filter(region => region.kind === 'wall');
  const corners = partition.filter(region => region.kind === 'shared-corner');
  if (cores.length < 3 || cores.length !== corners.length || cores.length + corners.length !== partition.length ||
      cores.some(region => region.owners.length !== 1)) return null;
  const walls = [];
  for (const core of cores) {
    const guideId = core.owners[0];
    const g = guides.find(item => item.id === guideId);
    const spans = g.spans.filter(span => span.kind === 'continuous');
    const target = spans[0]?.target;
    const guide = g.curve;
    if (guide.kind !== 'line' || target?.kind !== 'line' ||
        spans.some(span => !collinear(model, target, span.target)) ||
        Math.abs(dot(tangentAt(guide, 0), tangentAt(target, 0))) < 1 - angleTolerance)
      return null;

    // A straight wall is capped by the neighboring exterior skins at both
    // ends. A deep physical ray outside the proven opposite skin is not a cap.
    const adjacent = corners.filter(corner => corner.owners.includes(guideId));
    if (adjacent.length !== 2) return null;
    const capLines = adjacent.map(corner => model.guides.find(curve =>
      curve.id === corner.owners.find(owner => owner !== guideId)));
    const cutAt = end => {
      const point = pointAt(guide, end);
      const other = intersectSupport(point, inward(guide, end), target);
      const caps = capLines.filter(cap =>
        cap.kind === 'line' && (distance(cap.a, point) <= model.tolerance || distance(cap.b, point) <= model.tolerance) &&
        other && Math.abs(cross(tangentAt(cap, 0), sub(other.point, cap.a))) <= model.tolerance * 3 &&
        dot(sub(other.point, cap.a), sub(other.point, cap.b)) <= model.tolerance * length(sub(cap.b, cap.a)));
      return caps.length === 1 && dot(sub(other.point, point), inward(guide, end)) > model.tolerance ? other.point : null;
    };
    const start = cutAt(0), end = cutAt(1);
    if (!start || !end) return null;
    const id = walls.length + 1;
    walls.push({ id, label: '直壁 ' + id, guideIds: [guide.id],
      supportIds: [guide.id, ...new Set(spans.map(span => span.target.id))],
      polygon: [guide.a, guide.b, end, start] });
  }
  const junctions = corners.map((corner, i) => ({
    id: i + 1, label: '角部 ' + (i + 1),
    wallIds: corner.owners.map(guideId => walls.find(wall => wall.guideIds.includes(guideId)).id).sort((a, b) => a - b),
    reason: '相邻直壁的支撑带分别延续到相邻外皮',
  }));
  return { walls, junctions, patchRules: [] };
}

function branchCoverage(model, guides, branch) {
  // The flange is proven by the same opposite support on both sides of
  // the event. Its span across the event is an extension of that support.
  const flange = branch.guide;
  const flangeGuide = guides.find(guide => guide.curve === flange);
  const firstDeep = flangeGuide.spans.indexOf(branch.deeperSpans[0]);
  const lastDeep = flangeGuide.spans.indexOf(branch.deeperSpans.at(-1));
  const outerNormal = inward(flange, 0);
  const underside = model.curves.filter(curve => curve.kind === 'line' &&
    (distance(curve.a, branch.a) <= model.tolerance * 3 || distance(curve.b, branch.a) <= model.tolerance * 3) &&
    Math.abs(dot(tangentAt(curve, 0), tangentAt(flange, 0))) >= 1 - angleTolerance);
  const farUnderside = model.curves.filter(curve => curve.kind === 'line' &&
    (distance(curve.a, branch.b) <= model.tolerance * 3 || distance(curve.b, branch.b) <= model.tolerance * 3) &&
    Math.abs(dot(tangentAt(curve, 0), tangentAt(flange, 0))) >= 1 - angleTolerance);
  if (firstDeep <= 0 || lastDeep >= flangeGuide.spans.length - 1 || underside.length !== 1 || farUnderside.length !== 1 ||
      underside[0] === farUnderside[0] || !collinear(model, underside[0], farUnderside[0])) return null;
  const undersideEnds = [flange.a, flange.b].map(p => intersectSupport(p, outerNormal, underside[0])?.point);
  if (undersideEnds.some((p, i) => !p || dot(sub(p, i ? flange.b : flange.a), outerNormal) <= model.tolerance)) return null;

  // Accept a web only if its two native side skins actually sweep into
  // each other. The remote first exit seen from the flange is not a wall.
  const sideSkins = branch.webBoundary.filter(curve => curve.kind === 'line' &&
    Math.abs(dot(tangentAt(curve, 0), outerNormal)) >= 1 - angleTolerance);
  if (sideSkins.length !== 2) return null;
  const [leftSide, rightSide] = sideSkins;
  const facing = (source, opposite) =>
    guides.find(item => item.curve === source)?.spans.every(span => span.target === opposite && span.start && span.end);
  if (!facing(leftSide, rightSide) || !facing(rightSide, leftSide)) return null;
  const nativeCaps = branch.webBoundary.filter(curve => curve.kind === 'line' &&
    Math.abs(dot(tangentAt(curve, 0), outerNormal)) <= angleTolerance &&
    sideSkins.every(side => [side.a, side.b].some(p =>
      distance(curve.a, p) <= model.tolerance * 3 || distance(curve.b, p) <= model.tolerance * 3)));
  if (nativeCaps.length !== 1) return null;
  const cap = nativeCaps[0];
  const webEnds = sideSkins.map(side => {
    const foot = intersectSupport(side.a, outerNormal, cap);
    const top = intersectSupport(side.a, outerNormal, flange);
    const root = intersectSupport(side.a, outerNormal, underside[0]);
    if (!betweenEnds(foot, model, cap) || !betweenEnds(top, model, flange) || !root ||
        dot(sub(root.point, top.point), outerNormal) <= model.tolerance ||
        dot(sub(foot.point, root.point), outerNormal) <= model.tolerance) return null;
    return { foot: foot.point, top: top.point };
  });
  if (webEnds.some(end => !end)) return null;
  const patchRules = branch.webBoundary.filter(curve => {
    if (curve.kind !== 'arc') return false;
    const ends = [pointAt(curve, 0), pointAt(curve, 1)];
    const matches = (a, b) => [branch.a, branch.b].some(p => distance(a, p) <= model.tolerance * 3) &&
      sideSkins.some(side => [side.a, side.b].some(p => distance(b, p) <= model.tolerance * 3));
    return matches(ends[0], ends[1]) || matches(ends[1], ends[0]);
  }).map(curve => ({ curveId: curve.id, ownerWallIds: [1, 2] }));

  return {
    walls: [
      { id: 1, label: '翼板区', guideIds: [flange.id], supportIds: [flange.id, underside[0].id, farUnderside[0].id],
        polygon: [flange.a, flange.b, undersideEnds[1], undersideEnds[0]] },
      { id: 2, label: '腹板区', guideIds: [leftSide.id, rightSide.id],
        supportIds: [leftSide.id, rightSide.id, cap.id, flange.id],
        polygon: [webEnds[0].foot, webEnds[1].foot, webEnds[1].top, webEnds[0].top] },
    ],
    junctions: [{ id: 1, label: '翼板与腹板交界', wallIds: [1, 2], guideId: flange.id,
      eventParameters: [branch.deeperSpans[0].t0, branch.deeperSpans.at(-1).t1],
      reason: '翼板下皮两侧共线，腹板两侧可互扫并限定在翼板上皮以内' }],
    patchRules,
  };
}

export function planSharedCoverage(input) {
  const { model, guides, branch, partition } = inspectSection(input);
  const planned = branch ? branchCoverage(model, guides, branch) :
    closedWallCoverage(model, guides, partition);
  return planned ? { status: 'planned', ...planned } :
    { status: 'unresolved', reason: '未能唯一确定连续壁和交界处的原生支撑边' };
}
