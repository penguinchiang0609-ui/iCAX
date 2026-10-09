import assert from "node:assert/strict";
import { buildSketchDxf, createRacetrack, createRegularPolygon, createStar, parseSketchDxf } from "../../apps/tube-designer/webpage/sketchCadExchange.mjs";
import { curvePoint, editableSegments } from "../../apps/tube-designer/webpage/sketchGeometry.mjs";
import { sketchRibbonGroups } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";

function parseDxf(text) {
  const lines = text.trim().split(/\r?\n/), result = [];
  let entity, inEntities = false;
  for (let i = 0; i < lines.length; i += 2) {
    const code = Number(lines[i]), value = lines[i + 1];
    if (code === 2 && value === "ENTITIES") { inEntities = true; continue; }
    if (!inEntities) continue;
    if (code === 0) {
      if (entity) result.push(entity);
      entity = value === "ENDSEC" || value === "EOF" ? null : { type: value, pairs: [] };
      if (value === "ENDSEC") break;
    } else if (entity) entity.pairs.push([code, value]);
  }
  return result;
}
const number = (entity, code) => Number(entity.pairs.find(pair => pair[0] === code)?.[1]);
const values = (entity, code) => entity.pairs.filter(pair => pair[0] === code).map(pair => Number(pair[1]));
const near = (actual, expected, eps = 1e-9) => {
  assert.equal(actual.length, expected.length);
  actual.forEach((value, i) => assert.ok(Math.abs(value - expected[i]) < eps, `${actual} differs from ${expected}`));
};
// Independent OCS / ellipse evaluation checks physical geometry and directed
// endpoints. A clockwise ARC must not be exported as its complementary arc.
function importedPoint(entity, t) {
  if (entity.type === "LINE") return [10, 20].map(code => number(entity, code) * (1 - t) + number(entity, code + 1) * t);
  const direction = number(entity, 230) || 1;
  if (entity.type === "ARC" || entity.type === "CIRCLE") {
    const start = entity.type === "ARC" ? number(entity, 50) * Math.PI / 180 : 0;
    let span = entity.type === "ARC" ? (number(entity, 51) - number(entity, 50)) * Math.PI / 180 : Math.PI * 2;
    if (span <= 0) span += Math.PI * 2;
    const a = start + span * t, radius = number(entity, 40);
    return [direction * (number(entity, 10) + radius * Math.cos(a)), number(entity, 20) + radius * Math.sin(a)];
  }
  if (entity.type === "ELLIPSE") {
    const a = number(entity, 41) + (number(entity, 42) - number(entity, 41)) * t;
    const [mx, my] = [number(entity, 11), number(entity, 21)], ratio = number(entity, 40);
    return [number(entity, 10) + mx * Math.cos(a) - direction * my * ratio * Math.sin(a), number(entity, 20) + my * Math.cos(a) + direction * mx * ratio * Math.sin(a)];
  }
  throw new Error(`Unsupported test entity ${entity.type}`);
}

for (const sweep of [0.6, -0.6, 4.9, -4.9]) {
  for (const radii of [[7, 7], [13, 4], [4, 13]]) {
    const source = { kind: radii[0] === radii[1] ? "circleArc" : "ellipseArc", cx: -8, cy: 7, radiusX: radii[0], radiusY: radii[1], startAngle: -5.4, sweep, rotation: 0.63 };
    const [exported] = parseDxf(buildSketchDxf([source]));
    assert.equal(exported.type, radii[0] === radii[1] ? "ARC" : "ELLIPSE");
    for (const t of [0, 0.13, 0.5, 0.86, 1]) near(importedPoint(exported, t), curvePoint(source, t));
    const [roundtrip] = parseSketchDxf(buildSketchDxf([source])).entities;
    for (const t of [0, 0.13, 0.5, 0.86, 1]) near(curvePoint(roundtrip, t), curvePoint(source, t));
  }
}

// Exact cubic / quadratic splines retain every original control point and
// clamped knot vector. Open entities do not acquire an unintended closing edge.
for (const points of [[[0, 0], [4, 17], [11, 2]], [[0, 0], [4, 17], [11, 2], [20, 8]]]) {
  const [spline] = parseDxf(buildSketchDxf([{ kind: "path", closed: false, segments: [{ kind: "bezier", points }] }]));
  assert.equal(spline.type, "SPLINE");
  assert.equal(number(spline, 71), points.length - 1);
  assert.deepEqual(values(spline, 10), points.map(p => p[0]));
  assert.deepEqual(values(spline, 20), points.map(p => p[1]));
  assert.deepEqual(values(spline, 40), [...Array(points.length).fill(0), ...Array(points.length).fill(1)]);
  const [roundtrip] = parseSketchDxf(buildSketchDxf([{ kind: "path", closed: false, segments: [{ kind: "bezier", points }] }])).entities;
  assert.equal(roundtrip.closed, false);
  assert.deepEqual(roundtrip.segments[0].points, points);
}
const path = { kind: "path", closed: false, segments: [{ kind: "line", x1: 0, y1: 0, x2: 10, y2: 0 }, { kind: "line", x1: 10, y1: 0, x2: 10, y2: 5 }] };
assert.equal(parseDxf(buildSketchDxf([path])).length, 2);
assert.equal(parseDxf(buildSketchDxf([{ ...path, closed: true }])).length, 3);
assert.equal(parseSketchDxf(buildSketchDxf([path])).entities[0].closed, false);
assert.equal(parseSketchDxf(buildSketchDxf([{ ...path, closed: true }])).entities[0].closed, true);

// Rectangles and slots stay exact LINE + ARC. A circular slot has no zero edges.
const rectangle = parseDxf(buildSketchDxf([{ kind: "rectangle", x: 2, y: 3, width: 30, height: 12, radius: 2, closed: true }]));
assert.deepEqual(rectangle.map(e => e.type), ["LINE", "ARC", "LINE", "ARC", "LINE", "ARC", "LINE", "ARC"]);
for (const [width, height, expected] of [[40, 20, 6], [20, 40, 6], [20, 20, 4]]) {
  const slot = createRacetrack({ x: -3, y: 5, width, height });
  assert.equal(slot.closed, true);
  assert.equal(slot.segments.length, expected);
  slot.segments.forEach((segment, i) => near(curvePoint(segment, 1), curvePoint(slot.segments[(i + 1) % slot.segments.length], 0)));
  assert.equal(parseDxf(buildSketchDxf([slot])).length, expected);
  const [imported] = parseSketchDxf(buildSketchDxf([slot])).entities;
  assert.equal(imported.closed, true);
  assert.equal(imported.segments.length, expected);
}
const polygon = createRegularPolygon({ centerX: 3, centerY: -4, radius: 9, sides: 7, rotation: 0.2 });
assert.equal(polygon.segments.length, 7);
polygon.segments.forEach(s => assert.ok(Math.abs(Math.hypot(s.x1 - 3, s.y1 + 4) - 9) < 1e-9));
const star = createStar({ radius: 10, innerRadius: 4, points: 5 });
assert.equal(star.segments.length, 10);
star.segments.forEach((s, i) => assert.ok(Math.abs(Math.hypot(s.x1, s.y1) - (i % 2 ? 4 : 10)) < 1e-9));
for (const generate of [() => createRegularPolygon({ sides: 2 }), () => createRegularPolygon({ radius: 0 }), () => createStar({ innerRadius: 20 }), () => createRacetrack({ width: -1 })]) assert.throws(generate);
for (const entity of [{ kind: "text", value: "O" }, { kind: "unknown" }, { kind: "circle", cx: NaN, cy: 0, radius: 1 }, { kind: "circleArc", cx: 0, cy: 0, radius: 1, sweep: 0 }]) assert.throws(() => buildSketchDxf([entity]));
assert.throws(() => buildSketchDxf([]));
assert.throws(() => buildSketchDxf([polygon], { units: "invalid" }));
const dxf = buildSketchDxf([polygon], { name: "\nEOF\r\n", units: "mm" });
assert.ok(dxf.includes("$INSUNITS\r\n70\r\n4"));
assert.equal(parseDxf(dxf).length, 7);
// Separate figures and open trajectories are valid CAD, independent of the
// closed single-outer-loop rules required by pipe profile imports.
const mixed = parseSketchDxf(buildSketchDxf([
  { kind: "circle", cx: 0, cy: 0, radius: 2 },
  { kind: "circle", cx: 20, cy: 0, radius: 3 },
  { kind: "line", x1: -10, y1: 7, x2: 8, y2: 5 },
]));
assert.equal(mixed.entities.length, 3);
assert.equal(mixed.entities.filter(entity => entity.kind === "circle").length, 2);
assert.equal(mixed.entities.filter(entity => entity.closed === false).length, 1);
const inch = parseSketchDxf(buildSketchDxf([{ kind: "circle", cx: 1, cy: -2, radius: 0.5 }], { units: "inch" })).entities[0];
near([inch.cx, inch.cy, inch.radius], [25.4, -50.8, 12.7]);
const recordDxf = pairs => "0\nSECTION\n2\nENTITIES\n" + pairs.map(([code, value]) => `${code}\n${value}\n`).join("") + "0\nENDSEC\n0\nEOF\n";
const bulge = recordDxf([[0, "LWPOLYLINE"], [90, 3], [70, 0], [230, -1], [10, -1], [20, 2], [42, 1], [10, -11], [20, 2], [10, -11], [20, 9]]);
const [bulgePath] = parseSketchDxf(bulge).entities;
assert.equal(bulgePath.closed, false);
assert.equal(bulgePath.segments[0].kind, "circleArc");
assert.equal(bulgePath.segments[0].sweep, -Math.PI);
near(curvePoint(bulgePath.segments[0], 0), [1, 2]);
near(curvePoint(bulgePath.segments[0], 1), [11, 2]);
// Knot insertion converts a multi-span cubic to exact Bézier pieces. Verify
// against an independent de Boor evaluation, rather than our exporter.
const bsPoints = [[0, 0], [3, 6], [8, -2], [13, 11], [20, 3]], bsKnots = [0, 0, 0, 0, 0.4, 1, 1, 1, 1];
const bsDxf = recordDxf([[0, "SPLINE"], [70, 8], [71, 3], [72, 9], [73, 5], ...bsKnots.map(k => [40, k]), ...bsPoints.flatMap(p => [[10, p[0]], [20, p[1]], [30, 0]])]);
const [bs] = parseSketchDxf(bsDxf).entities;
assert.equal(bs.closed, false);
assert.equal(bs.segments.length, 2);
function deBoor(t) {
  const degree = 3, k = t < 0.4 ? 3 : 4, d = bsPoints.slice(k - degree, k + 1).map(p => [...p]);
  for (let r = 1; r <= degree; r++) for (let j = degree; j >= r; j--) {
    const i = k - degree + j, alpha = (t - bsKnots[i]) / (bsKnots[i + degree - r + 1] - bsKnots[i]);
    d[j] = d[j].map((value, axis) => (1 - alpha) * d[j - 1][axis] + alpha * value);
  }
  return d[degree];
}
for (const t of [0, 0.13, 0.39, 0.4, 0.71, 1]) near(curvePoint(bs.segments[t < 0.4 ? 0 : 1], t < 0.4 ? t / 0.4 : (t - 0.4) / 0.6), deBoor(t));
assert.throws(() => parseSketchDxf(recordDxf([[0, "TEXT"], [1, "O"]])), /TEXT/);
assert.throws(() => parseSketchDxf(bsDxf.replace("70\n8", "70\n10")), /周期/);
assert.throws(() => parseSketchDxf("AutoCAD Binary DXF"), /ASCII/);
assert.throws(() => parseSketchDxf("0\nSECTION\n2"), /成对/);
const commands = sketchRibbonGroups.flatMap(group => group.commands.map(command => command.id));
assert.equal(new Set(commands).size, commands.length);
for (const name of ["move", "copy", "rotate", "mirror", "scale", "align", "measure", "trim", "extend", "offset", "fillet", "chamfer", "repair", "diagnose", "export", "ellipse", "racetrack", "polygon", "star", "array-circumferential", "array-edit", "end-cuts", "split-parts", "text-outline", "commit", "cancel"]) assert.ok(commands.includes(`sketch.${name}`));
console.log("Sketch CAD DXF exact conics, splines, closure, shapes, validation and command coverage passed.");
