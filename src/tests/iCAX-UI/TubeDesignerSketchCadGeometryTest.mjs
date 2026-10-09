import assert from "node:assert/strict";
import { boundsOfEntities, transformSketchEntities, intersectionsOfEntities, trimSketchEntity, extendSketchEntity, offsetSketchEntity, filletSketchEntities, repairSketchEntities, diagnoseSketchEntities } from "../../apps/tube-designer/webpage/sketchCadGeometry.mjs";
import { curvePoint, editableSegments } from "../../apps/tube-designer/webpage/sketchGeometry.mjs";

const near = (a, b, tolerance = 1e-7) => {
  if (Array.isArray(a)) { assert.equal(a.length, b.length); a.forEach((v, i) => near(v, b[i], tolerance)); }
  else assert.ok(Math.abs(a - b) <= tolerance, `${a} differs from ${b}`);
};
const line = (x1, y1, x2, y2, id) => ({ kind: "line", x1, y1, x2, y2, id });
const snapshot = e => JSON.stringify(e);

// Affine edits preserve exact conics, reflected directions and all spline controls.
{
  const arc = { id: "arc", kind: "circleArc", cx: 2, cy: -4, radius: 7, startAngle: .3, sweep: -4.1, rotation: .2 };
  const [mirror] = transformSketchEntities([arc], { kind: "mirror", x1: 0, y1: 0, x2: 1, y2: 0 });
  near(mirror.sweep, -arc.sweep);
  for (const t of [0, .1, .6, 1]) near(curvePoint(mirror, t), [curvePoint(arc, t)[0], -curvePoint(arc, t)[1]]);
  const ellipse = { kind: "ellipseArc", cx: 1, cy: 2, radiusX: 7, radiusY: 3, rotation: .4, startAngle: 1, sweep: 3.6 };
  const [me] = transformSketchEntities([ellipse], { kind: "mirror", x1: 3, y1: 0, x2: 3, y2: 1 });
  for (const t of [0, .25, .7, 1]) near(curvePoint(me, t), [6 - curvePoint(ellipse, t)[0], curvePoint(ellipse, t)[1]]);
  const spline = { id: "s", kind: "spline", points: [[0, 0], [1, 7], [9, 4], [10, 1]] }, before = snapshot(spline);
  const [scaled] = transformSketchEntities([spline], { kind: "scale", centerX: 1, centerY: 2, factor: 3 });
  near(scaled.points, [[-2, -4], [1, 17], [25, 8], [28, -1]]);
  assert.equal(snapshot(spline), before);
  const [rotated] = transformSketchEntities([{ kind: "rectangle", x: 0, y: 0, width: 20, height: 10, radius: 2 }], { kind: "rotate", angle: 45 });
  assert.equal(rotated.kind, "path"); assert.equal(rotated.segments.filter(s => s.kind === "circleArc").length, 4);
  const [copy] = transformSketchEntities([spline], { kind: "copy", dx: 2, dy: 3 }, { createId: id => `${id}-copy` });
  assert.equal(copy.id, "s-copy"); assert.equal(snapshot(spline), before);
  assert.throws(() => transformSketchEntities([spline], { kind: "scale", factor: 0 }), /缩放/);
  assert.throws(() => transformSketchEntities([spline], { kind: "mirror", x1: 0, x2: 0, y1: 0, y2: 0 }), /重合/);
}
// Bounds include rotated elliptical extrema and exact Bezier control hulls.
{
  const ellipse = { kind: "ellipse", cx: 3, cy: 5, radiusX: 10, radiusY: 2, rotation: Math.PI / 4 };
  const bounds = boundsOfEntities([ellipse]), half = Math.sqrt(52);
  near([bounds.minX, bounds.maxX, bounds.minY, bounds.maxY], [3 - half, 3 + half, 5 - half, 5 + half]);
  const quadratic = { kind: "path", segments: [{ kind: "bezier", points: [[0, 0], [5, 20], [10, 0]] }] };
  near(boundsOfEntities([quadratic]).maxY, 10);
  const cubic = { kind: "path", segments: [{ kind: "bezier", points: [[0, 0], [0, 16], [16, 16], [16, 0]] }] };
  near(boundsOfEntities([cubic]).maxY, 12);
  const arcs = { kind: "circleArc", cx: 0, cy: 0, radius: 10, startAngle: 0, sweep: Math.PI / 2 };
  near(Object.values(boundsOfEntities([arcs])).slice(0, 4), [0, 0, 10, 10]);
  const source = [line(0, 0, 2, 3, "a"), line(4, 7, 8, 9, "b")];
  const aligned = transformSketchEntities(source, { kind: "align", direction: "right" });
  near(aligned.map(e => e.x2), [8, 8]);
  const distributed = transformSketchEntities([line(0, 0, 2, 0), line(4, 0, 5, 0), line(10, 0, 12, 0)], { kind: "align", direction: "distributeX" });
  near(distributed.map(e => e.x1), [0, 5.5, 10]);
}
// Analytic circle/ellipse intersections and tangent roots of genuine Beziers.
{
  const circle = { id: "c", kind: "circle", cx: 0, cy: 0, radius: 10 };
  const hits = intersectionsOfEntities(circle, line(-20, 0, 20, 0));
  assert.equal(hits.length, 2); near(hits.map(h => h.point[0]).sort((a, b) => a - b), [-10, 10]);
  assert.equal(intersectionsOfEntities(circle, line(-20, 10, 20, 10)).length, 1);
  const tangent = { kind: "path", segments: [{ kind: "bezier", points: [[-1, 1], [0, -1], [1, 1]] }] };
  const bezierHit = intersectionsOfEntities(tangent, line(-2, 0, 2, 0));
  assert.equal(bezierHit.length, 1); near(bezierHit[0].point, [0, 0]); near(bezierHit[0].a.t, .5);
  const ellipse = { kind: "ellipse", cx: 0, cy: 0, radiusX: 10, radiusY: 2, rotation: Math.PI / 4 };
  const ellipseHits = intersectionsOfEntities(ellipse, line(-20, 0, 20, 0));
  assert.equal(ellipseHits.length, 2); ellipseHits.forEach(h => near(h.point[1], 0));
  const ellipseTangent = intersectionsOfEntities({ kind: "ellipse", cx: 0, cy: 0, radiusX: 10, radiusY: 5 }, { kind: "ellipse", cx: 20, cy: 0, radiusX: 10, radiusY: 5 });
  assert.equal(ellipseTangent.length, 1); near(ellipseTangent[0].point, [10, 0], 1e-6);
  const b = { kind: "path", segments: [{ kind: "bezier", points: [[-15, 0], [-5, 2], [5, -2], [15, 0]] }] };
  const generalHits = intersectionsOfEntities(b, circle);
  assert.equal(generalHits.length, 2);
  generalHits.forEach(h => { near(curvePoint(b.segments[0], h.a.t), h.point, 2e-6); near(Math.hypot(...h.point), 10, 2e-6); });
  const halfArc = { kind: "circleArc", cx: 0, cy: 0, radius: 10, startAngle: 0, sweep: Math.PI / 2 };
  const adjacentArc = { ...halfArc, startAngle: Math.PI / 2 };
  const joinedHit = intersectionsOfEntities(halfArc, adjacentArc); assert.equal(joinedHit.length, 1); near(joinedHit[0].point, [0, 10]);
  assert.throws(() => intersectionsOfEntities(halfArc, { ...halfArc, startAngle: Math.PI / 4 }), /重合/);
}
// Trim removes the selected interval, including seam-crossing closed curves.
{
  const source = line(-20, 0, 20, 0, "line"), cutters = [line(-5, -10, -5, 10), line(5, -10, 5, 10)], before = snapshot(source);
  const { parts } = trimSketchEntity(source, cutters, [0, 0]);
  assert.equal(parts.length, 2); near(parts.map(p => [p.x1, p.x2]), [[-20, -5], [5, 20]]); assert.equal(snapshot(source), before);
  const circle = { kind: "circle", cx: 0, cy: 0, radius: 10, id: "circle" }, circleBefore = snapshot(circle);
  const boundaries = [line(-20, -3, 20, -3), line(-20, 3, 20, 3)];
  for (const point of [[10, 0], [-10, 0], [0, 10], [0, -10]]) {
    const trimmed = trimSketchEntity(circle, boundaries, point).parts;
    assert.equal(trimmed.length, 1);
    const segments = editableSegments(trimmed[0]);
    assert.ok(segments.every(s => s.kind === "circleArc")); assert.ok(segments.reduce((sum, s) => sum + Math.abs(s.sweep), 0) < Math.PI * 2);
  }
  assert.equal(snapshot(circle), circleBefore);
  assert.throws(() => trimSketchEntity(circle, [line(-20, 20, 20, 20)], [10, 0]), /交点/);
  assert.throws(() => trimSketchEntity(source, [line(-20, 20, 20, 20)], [0, 0]), /交点/);
  const spline = { kind: "path", segments: [{ kind: "bezier", points: [[-10, -1], [0, 4], [10, 1]] }] };
  const spl = trimSketchEntity(spline, [line(0, -20, 0, 20)], [-8, 0]).parts[0];
  assert.equal(spl.kind, "path"); assert.equal(spl.segments[0].points.length, 3); near(spl.segments[0].points[0], [0, 2]);
}
// Natural extension chooses the first boundary beyond the nearest endpoint.
{
  const source = line(0, 0, 10, 0), before = snapshot(source);
  const extended = extendSketchEntity(source, [line(30, -5, 30, 5), line(20, -5, 20, 5)], [10, 0]).parts[0];
  near([extended.x1, extended.x2], [0, 20]); assert.equal(snapshot(source), before);
  const reverse = extendSketchEntity(source, [line(-5, -5, -5, 5)], [0, 0]).parts[0]; near(reverse.x1, -5);
  const arc = { kind: "circleArc", cx: 0, cy: 0, radius: 10, startAngle: 0, sweep: Math.PI / 4 };
  const extendedArc = extendSketchEntity(arc, [line(0, -20, 0, 20)], [7, 7]).parts[0]; near(extendedArc.sweep, Math.PI / 2);
  assert.throws(() => extendSketchEntity(source, [line(0, 5, 20, 5)], [10, 0]), /没有/);
}
// Offsets preserve analytic radii and joined line/arc contours.
{
  const circle = { kind: "circle", cx: 2, cy: 3, radius: 10 }, before = snapshot(circle);
  near(offsetSketchEntity(circle, 2).parts[0].radius, 12);
  near(offsetSketchEntity(circle, 2, [2, 3]).parts[0].radius, 8);
  assert.equal(snapshot(circle), before);
  const offset = offsetSketchEntity(line(0, 0, 10, 0), 2).parts[0]; near([offset.y1, offset.y2], [2, 2]);
  const rectangle = { kind: "rectangle", x: 0, y: 0, width: 20, height: 10, radius: 2 };
  const result = offsetSketchEntity(rectangle, 1).parts[0];
  near([boundsOfEntities([result]).minX, boundsOfEntities([result]).minY, boundsOfEntities([result]).maxX, boundsOfEntities([result]).maxY], [-1, -1, 21, 11]);
  assert.equal(result.segments.filter(s => s.kind === "circleArc").length, 4);
  const ellipseOffset = offsetSketchEntity({ kind: "ellipse", cx: 0, cy: 0, radiusX: 10, radiusY: 5 }, 2).parts[0];
  assert.ok(ellipseOffset.closed && ellipseOffset.segments.every(s => s.kind === "bezier"));
  assert.throws(() => offsetSketchEntity(circle, -10), /半径/);
  const sharpRectangle = { kind: "rectangle", x: 0, y: 0, width: 20, height: 10 };
  const inset = offsetSketchEntity(sharpRectangle, -4).parts[0]; near([boundsOfEntities([inset]).width, boundsOfEntities([inset]).height], [12, 2]);
  assert.throws(() => offsetSketchEntity(sharpRectangle, -5), /消失/);
  assert.throws(() => offsetSketchEntity(sharpRectangle, -6), /交叉/);
}
// Exact tangent fillets and chamfers reject impossible radii atomically.
{
  const input = [line(0, 0, 20, 0, "a"), line(20, 0, 20, 20, "b")], before = snapshot(input);
  const result = filletSketchEntities(input, { radius: 3 }).parts[0];
  assert.equal(result.kind, "path"); assert.equal(result.segments.length, 3);
  const [a, arc, b] = result.segments;
  near([a.x2, a.y2], [17, 0]); near([b.x1, b.y1], [20, 3]); near([arc.cx, arc.cy, arc.radius, arc.sweep], [17, 3, 3, Math.PI / 2]);
  near(curvePoint(arc, 0), [a.x2, a.y2]); near(curvePoint(arc, 1), [b.x1, b.y1]);
  const radialStart = [a.x2 - arc.cx, a.y2 - arc.cy], radialEnd = [b.x1 - arc.cx, b.y1 - arc.cy];
  near(radialStart[0] * (a.x2 - a.x1) + radialStart[1] * (a.y2 - a.y1), 0);
  near(radialEnd[0] * (b.x2 - b.x1) + radialEnd[1] * (b.y2 - b.y1), 0);
  assert.equal(snapshot(input), before);
  assert.ok(filletSketchEntities(input, { chamfer: true, distance: 4 }).parts[0].segments.every(s => s.kind === "line"));
  assert.throws(() => filletSketchEntities(input, { radius: 21 }), /超过/); assert.equal(snapshot(input), before);
  const square = { kind: "rectangle", x: 0, y: 0, width: 20, height: 20 };
  const wrapped = filletSketchEntities([square], { radius: 2, point: [0, 0] }).parts[0];
  const segments = editableSegments(wrapped);
  segments.forEach((s, i) => near(curvePoint(s, 1), curvePoint(segments[(i + 1) % segments.length], 0)));
  const crossing = [line(-10, 0, 10, 0), line(0, -10, 0, 10)], crossingBefore = snapshot(crossing);
  for (const point of [[5, 5], [-5, 5], [-5, -5], [5, -5]]) {
    const [first, rounded, last] = filletSketchEntities(crossing, { radius: 2, point }).parts[0].segments;
    near([first.x1, first.y1], [Math.sign(point[0]) * 10, 0]);
    near([last.x2, last.y2], [0, Math.sign(point[1]) * 10]);
    near([rounded.cx, rounded.cy], [Math.sign(point[0]) * 2, Math.sign(point[1]) * 2]);
    const chamfer = filletSketchEntities(crossing, { chamfer: true, distance: 3, point }).parts[0].segments[1];
    near([chamfer.x1, chamfer.y1, chamfer.x2, chamfer.y2], [Math.sign(point[0]) * 3, 0, 0, Math.sign(point[1]) * 3]);
  }
  assert.equal(snapshot(crossing), crossingBefore);
}
// Shared repair/diagnose API is linked, and no geometry operation mutates drafts.
{
  const circles = [{ id: "a", kind: "circle", cx: 0, cy: 0, radius: 3 }, { id: "b", kind: "circle", cx: 0, cy: 0, radius: 3 }], before = snapshot(circles);
  assert.equal(typeof diagnoseSketchEntities, "function"); assert.equal(typeof repairSketchEntities, "function");
  const repaired = repairSketchEntities(circles, { tolerance: .01, removeDuplicates: true });
  assert.equal(repaired.entities.length, 1); assert.equal(snapshot(circles), before);
}
console.log("TubeDesigner sketch CAD geometry tests passed");
