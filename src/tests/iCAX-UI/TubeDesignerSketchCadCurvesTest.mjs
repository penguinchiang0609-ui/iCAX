import assert from "node:assert/strict";
import { offsetSketchEntity, filletSketchEntities, boundsOfEntities } from "../../apps/tube-designer/webpage/sketchCadGeometry.mjs";
import { OFFSET_CURVE_TOLERANCE, offsetCurveSegments, curveDerivative } from "../../apps/tube-designer/webpage/sketchCadCurves.mjs";
import { curvePoint, nearestSegment, editableSegments, reverseSegment, distance } from "../../apps/tube-designer/webpage/sketchGeometry.mjs";

const near = (a, b, tolerance = 1e-7) => {
  if (Array.isArray(a)) { assert.equal(a.length, b.length); a.forEach((v, i) => near(v, b[i], tolerance)); }
  else assert.ok(Math.abs(a - b) <= tolerance, `${a} differs from ${b}`);
};
const line = (x1, y1, x2, y2) => ({ kind: "line", x1, y1, x2, y2 });
const sourceSegments = e => e.kind === "bezier" ? [structuredClone(e)] : editableSegments(e);
const cross = (a, b) => a[0] * b[1] - a[1] * b[0];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1];
const normalOffset = (s, t, amount) => {
  let d = curveDerivative(s, t), order = 1;
  while (Math.hypot(...d) < 1e-9 && order < 10) {
    order++; d = curveDerivative(s, t, order);
    if (t === 1 && (order - 1) % 2) d = d.map(v => -v);
  }
  const p = curvePoint(s, t), speed = Math.hypot(...d);
  assert.ok(speed > 0);
  return [p[0] - d[1] / speed * amount, p[1] + d[0] / speed * amount];
};
const joined = (segments, closed = false) => {
  for (let i = 0; i < segments.length - (closed ? 0 : 1); i++) near(curvePoint(segments[i], 1), curvePoint(segments[(i + 1) % segments.length], 0));
};
const tangent = (a, t, b, u) => {
  const da = curveDerivative(a, t), db = curveDerivative(b, u), scale = Math.hypot(...da) * Math.hypot(...db);
  assert.ok(scale > 0);
  near(cross(da, db) / scale, 0, 2e-8);
  assert.ok(dot(da, db) > 0, "tangents must have the same direction");
};

// Dense independent pointwise verification complements the interval certificate:
// retained cubics stay within 0.01 mm, including between their fitting samples.
const offsetSamples = [
  [{ kind: "ellipseArc", cx: 30, cy: 100, radiusX: 8, radiusY: 5, startAngle: 0, sweep: 2 * Math.PI }, -1.5],
  [{ kind: "ellipseArc", cx: -4, cy: 12, radiusX: 20, radiusY: 3, rotation: .8, startAngle: .7, sweep: -2.7 }, .8],
  [{ kind: "bezier", points: [[0, 0], [5, 10], [15, 0]] }, .7],
  [{ kind: "bezier", points: [[0, 0], [5, 10], [15, 0]] }, -.7],
  [{ kind: "bezier", points: [[0, 0], [10, 0], [0, 10], [10, 10]] }, .5],
  [{ kind: "bezier", points: [[0, 0], [0, 0], [10, 10], [10, 10]] }, 2],
  [{ kind: "bezier", points: [[0, 0], [2, 3], [4, 1], [6, 4], [8, 2], [10, 5]] }, .25],
];
let maxError = 0;
for (const [source, amount] of offsetSamples) {
  const before = JSON.stringify(source), result = offsetCurveSegments(source, amount);
  assert.ok(result.length > 0 && result.length < 256);
  assert.ok(result.every(s => s.kind === "bezier" && s.points.length === 4));
  joined(result);
  let previous = 0;
  for (let i = 0; i < result.length; i++) {
    const segment = result[i], lo = previous;
    let hi = i === result.length - 1 ? 1 : nearestSegment(source, curvePoint(segment, 1)).t;
    assert.ok(hi > lo);
    // Every accepted span is produced by exact dyadic subdivision of the
    // original parameter, making endpoint parameter recovery unambiguous.
    hi = Math.round(hi * 2 ** 18) / 2 ** 18;
    near(curvePoint(segment, 0), normalOffset(source, lo, amount), 2e-6);
    near(curvePoint(segment, 1), normalOffset(source, hi, amount), 2e-6);
    for (let j = 0; j <= 257; j++) {
      const u = j / 257, error = distance(curvePoint(segment, u), normalOffset(source, lo + (hi - lo) * u, amount));
      maxError = Math.max(error, maxError);
      assert.ok(error <= OFFSET_CURVE_TOLERANCE, `parallel error ${error} exceeds 0.01 mm`);
    }
    if (i && Math.hypot(...curveDerivative(segment, 0)) > 1e-9) tangent(result[i - 1], 1, segment, 0);
    previous = hi;
  }
  assert.equal(JSON.stringify(source), before);
}

// Actual spline entities have a zero-speed tail parameter, not a geometric
// cusp. They must offset without substituting line tessellation or mutating it.
const spline = { id: "spline", kind: "spline", points: [[20, 60], [30, 85], [45, 65], [55, 100]] }, splineBefore = JSON.stringify(spline);
for (const amount of [-1, 1]) {
  const result = offsetSketchEntity(spline, amount).parts[0];
  assert.equal(result.id, spline.id); assert.equal(result.kind, "path");
  assert.ok(result.segments.every(s => s.kind === "bezier"));
  joined(result.segments); assert.ok(!result.closed);
}
assert.equal(JSON.stringify(spline), splineBefore);
const ellipse = { id: "ellipse", kind: "ellipse", cx: 30, cy: 100, radiusX: 8, radiusY: 5 }, ellipseBefore = JSON.stringify(ellipse);
const out = offsetSketchEntity(ellipse, 1.5).parts[0], inset = offsetSketchEntity(ellipse, -1).parts[0];
joined(out.segments, true); joined(inset.segments, true);
assert.ok(out.closed && inset.closed);
near([boundsOfEntities([out]).width, boundsOfEntities([out]).height], [19, 13], .01);
near([boundsOfEntities([inset]).width, boundsOfEntities([inset]).height], [14, 8], .01);
assert.throws(() => offsetSketchEntity(ellipse, -3.2), /尖点|翻转/);
assert.equal(JSON.stringify(ellipse), ellipseBefore);
const mixed = { kind: "path", closed: true, segments: [
  { kind: "ellipseArc", cx: 0, cy: 0, radiusX: 10, radiusY: 5, startAngle: 0, sweep: Math.PI }, line(-10, 0, 10, 0),
] };
const mixedOffset = offsetSketchEntity(mixed, 1).parts[0];
joined(mixedOffset.segments, true);
assert.equal(mixedOffset.segments.filter(s => s.kind === "circleArc").length, 2);
assert.ok(mixedOffset.segments.some(s => s.kind === "bezier"));

// Curved fillets retain the original analytic arcs or Bezier controls and add
// an exact circle whose radius and both oriented tangent conditions are tested.
const pairs = [
  [{ kind: "circleArc", cx: 20, cy: 100, radius: 10, startAngle: Math.PI, sweep: -Math.PI / 2 }, line(20, 110, 20, 125)],
  [{ kind: "ellipseArc", cx: 20, cy: 100, radiusX: 10, radiusY: 5, startAngle: Math.PI, sweep: -Math.PI / 2 }, line(20, 105, 20, 125)],
  [{ kind: "bezier", points: [[10, 100], [15, 100], [17, 105], [20, 105]] }, line(20, 105, 20, 125)],
  [{ kind: "spline", points: [[20, 70], [24, 75], [28, 71], [32, 77]] }, line(32, 77, 32, 100)],
];
for (const pair of pairs) {
  const before = JSON.stringify(pair), result = filletSketchEntities(pair, { radius: 2 }).parts[0], segments = result.segments;
  const arcIndex = segments.findIndex((s, i) => i > 0 && s.kind === "circleArc" && Math.abs(s.radius - 2) < 1e-7);
  assert.ok(arcIndex > 0);
  const a = segments[arcIndex - 1], arc = segments[arcIndex], b = segments[arcIndex + 1];
  near(arc.radius, 2); joined(segments);
  tangent(a, 1, arc, 0); tangent(arc, 1, b, 0);
  assert.equal(segments[0].kind, sourceSegments(pair[0])[0].kind);
  assert.equal(JSON.stringify(pair), before);
  assert.throws(() => filletSketchEntities(pair, { radius: 100 }), /超过|范围/);
  assert.equal(JSON.stringify(pair), before);
  const path = { kind: "path", segments: [...sourceSegments(pair[0]), ...sourceSegments(pair[1])] };
  const selected = filletSketchEntities([path], { radius: 2, point: curvePoint(sourceSegments(pair[0]).at(-1), 1) }).parts[0];
  joined(selected.segments);
  assert.ok(selected.segments.some(s => s.kind === "circleArc" && Math.abs(s.radius - 2) < 1e-7));
}

const reversePair = [reverseSegment(pairs[2][0]), reverseSegment(pairs[2][1])];
const reversed = filletSketchEntities(reversePair, { radius: 2 }).parts[0];
joined(reversed.segments);
for (const pair of [
  [{ kind: "circleArc", cx: 0, cy: 0, radius: 10, startAngle: Math.PI, sweep: -Math.PI / 2 }, { kind: "circleArc", cx: 10, cy: 10, radius: 10, startAngle: Math.PI, sweep: -Math.PI / 2 }],
  [{ kind: "bezier", points: [[0, 0], [5, 0], [7, 5], [10, 5]] }, { kind: "bezier", points: [[10, 5], [10, 10], [15, 15], [20, 15]] }],
]) {
  const segments = filletSketchEntities(pair, { radius: 2 }).parts[0].segments;
  joined(segments); tangent(segments[0], 1, segments[1], 0); tangent(segments[1], 1, segments[2], 0);
  near(segments[1].radius, 2);
}
const shortTailSpline = { kind: "spline", points: [[0, 0], [4, 0], [8, 0], [10, 0]] }, shortTailLine = line(10, 0, 10, 20);
for (const source of [[shortTailSpline, shortTailLine], [{ kind: "path", segments: [...sourceSegments(shortTailSpline), shortTailLine] }]]) {
  const segments = filletSketchEntities(source, { radius: 5 }).parts[0].segments;
  joined(segments);
  const arc = segments.find(s => s.kind === "circleArc");
  near([arc.cx, arc.cy, arc.radius], [5, 5, 5]);
  near(curvePoint(segments[0], 1), [5, 0]);
  assert.equal(segments[0].kind, "bezier");
}
const seamFillet = filletSketchEntities([mixed], { radius: 1, point: [10, 0] }).parts[0];
assert.ok(seamFillet.closed); joined(seamFillet.segments, true);
assert.ok(seamFillet.segments.some(s => s.kind === "ellipseArc"));
const curvedChamfer = filletSketchEntities(pairs[1], { chamfer: true, distance: 2 }).parts[0];
joined(curvedChamfer.segments);
assert.equal(curvedChamfer.segments[0].kind, "ellipseArc"); assert.equal(curvedChamfer.segments[1].kind, "line");
assert.throws(() => offsetSketchEntity({ kind: "bezier", points: [[0, 0], [1, 1], [0, 0]] }, 1), /尖点|翻转|切向/);
console.log(`TubeDesigner sketch CAD curve tests passed; dense maximum parallel error ${maxError.toFixed(8)} mm (limit 0.01 mm)`);
