import assert from "node:assert/strict";
import { profileSvgGeometry } from "../../apps/tube-designer/webpage/profileSvg.mjs";

const line = (start, end) => ({ kind: "line", start, end });
const path = segments => profileSvgGeometry({ contours: [{ kind: "path", segments }] });
const commands = geometry => geometry.markup.match(/\bd="([^"]+)"/)[1];
const nearPoint = (actual, expected) => actual.forEach((value, index) =>
  assert.ok(Math.abs(value - expected[index]) < 1e-6, `${actual} should equal ${expected}`));

// Reversing a stored rectangle edge must keep a single closed boundary. Before
// this fix it introduced two extra M commands and filled only half the rectangle.
const rectangle = [line([-10, -10], [10, -10]), line([10, -10], [10, 10]),
  line([10, 10], [-10, 10]), line([-10, 10], [-10, -10])];
assert.deepEqual(path([rectangle[0], { ...line([10, 10], [10, -10]), reversed: true }, ...rectangle.slice(2)]), path(rectangle));

// Non-unit knot domains and proper subintervals ensure a reversed spline does
// not reverse its poles/knots or accidentally render the entire underlying curve.
const weight = Math.SQRT1_2;
const rationalQuarterCircle = u => {
  const a = (1 - u) ** 2, b = 2 * u * (1 - u) * weight, c = u ** 2;
  return [10 * (a + b) / (a + b + c), 10 * (b + c) / (a + b + c)];
};
const spline = { degree: 2, controlPoints: [[10, 0], [10, 10], [0, 10]],
  knots: [-7, 2], multiplicities: [3, 3], periodic: false, startParameter: -4, endParameter: 1 };
const cases = [
  { segment: { kind: "arc", start: [10, 0], middle: [10 * weight, 10 * weight], end: [0, 10] }, start: [10, 0], end: [0, 10] },
  { segment: { kind: "ellipseArc", center: [0, 0], majorRadius: 10, minorRadius: 4, rotation: 0, startAngle: 0, endAngle: Math.PI / 2 }, start: [10, 0], end: [0, 4] },
  { segment: { kind: "bezier", controlPoints: [[10, 0], [6, 15], [0, 10]] }, start: [10, 0], end: [0, 10] },
  { segment: { kind: "bezier", controlPoints: [[10, 0], [10, 10], [0, 10]], weights: [1, weight, 1] }, start: [10, 0], end: [0, 10] },
  { segment: { ...spline, kind: "bspline" }, start: [80 / 9, 50 / 9], end: [170 / 81, 800 / 81] },
  { segment: { ...spline, kind: "nurbs", weights: [1, weight, 1] }, start: rationalQuarterCircle(1 / 3), end: rationalQuarterCircle(8 / 9) },
  { segment: { kind: "bspline", degree: 1, periodic: true,
    controlPoints: [[0, 0], [10, 0], [10, 10], [0, 10]], knots: [0, 1, 2, 3, 4],
    startParameter: 0.2, endParameter: 0.8 }, start: [2, 0], end: [8, 0] },
];
for (const { segment, start, end } of cases) {
  const reversed = { ...segment, reversed: true }, original = structuredClone(reversed);
  const forward = path([segment, line(end, start)]), backward = path([reversed, line(start, end)]);
  assert.equal((commands(backward).match(/\bM /g) ?? []).length, 1, `${segment.kind} must join its closing edge`);
  const initial = commands(backward).match(/^M ([^ ]+)/)[1].split(",").map(Number);
  nearPoint(initial, end);
  const curveCommand = commands(backward).replace(/ L [^ ]+ Z$/, "");
  nearPoint(curveCommand.trim().split(/\s+/).at(-1).split(",").map(Number), start);
  for (const key of ["minX", "minY", "maxX", "maxY"])
    assert.ok(Math.abs(forward.bounds[key] - backward.bounds[key]) < 1e-6, `${segment.kind} reversal must preserve bounds`);
  assert.deepEqual(reversed, original, `${segment.kind} preview must not mutate saved exact curves`);
}
console.log("Reversed profile curves: closed-path continuity, arc/ellipse/Bezier direction, trimmed B-spline/NURBS/periodic domains, bounds and input preservation passed.");
