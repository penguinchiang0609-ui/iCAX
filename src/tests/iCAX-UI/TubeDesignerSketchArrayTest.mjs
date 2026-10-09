import assert from "node:assert/strict";
import { arrayTransformMatrix, buildArrayTransforms, transformArrayEntity, validateArraySpec } from "../../apps/tube-designer/webpage/sketchArray.mjs";
import { curvePoint, editableSegments } from "../../apps/tube-designer/webpage/sketchGeometry.mjs";

const near = (actual, expected, tolerance = 1e-9) => {
  assert.equal(actual.length, expected.length);
  actual.forEach((value, index) => assert.ok(Math.abs(value - expected[index]) < tolerance,
    `${actual} differs from ${expected}`));
};
const rectangular = { kind: "rectangular", columns: 3, rows: 2, spacingX: 15, spacingY: -7, groupCenter: [4, 3] };
const polar = { kind: "polar", count: 4, angleStep: 90, centerX: 1, centerY: -2, groupCenter: [11, -2], rotateItems: true };

// Counts include the source and the original is emitted only by the caller.
{
  const transforms = buildArrayTransforms(rectangular);
  assert.equal(transforms.length, 5);
  assert.deepEqual(transforms.map(({ dx, dy }) => [dx, dy]), [[15, 0], [30, 0], [0, -7], [15, -7], [30, -7]]);
  assert.deepEqual(transforms[3].groupCenter, [4, 3]);
  assert.deepEqual(validateArraySpec(rectangular, 2, 8), { ready: true, message: "将新增 10 个图形", copyCount: 10 });
  assert.equal(validateArraySpec({ ...rectangular, columns: 1, spacingX: 0 }, 1, 1).ready, true);
  assert.equal(validateArraySpec({ ...rectangular, rows: 1, spacingY: 0 }, 1, 1).ready, true);
  const transformsPolar = buildArrayTransforms(polar);
  assert.equal(transformsPolar.length, 3);
  near(transformsPolar.map((value) => value.angle), [Math.PI / 2, Math.PI, 3 * Math.PI / 2]);
  assert.equal(validateArraySpec(polar, 3, 3).copyCount, 9);
  assert.deepEqual(transformsPolar[2].center, [1, -2]);
  const origin = [11, -2];
  near(transformsPolar.map((value) => transformArrayEntity({ kind: "circle", cx: origin[0], cy: origin[1], radius: 2 }, value).cx), [1, -9, 1]);
}

// Invalid values are refused before preview allocates arrays or the editor mutates.
for (const spec of [
  null, {}, { kind: "unknown" },
  { ...rectangular, columns: 0 }, { ...rectangular, rows: -1 },
  { ...rectangular, columns: 2.2 }, { ...rectangular, rows: Infinity },
  { ...rectangular, columns: 1, rows: 1 },
  { ...rectangular, spacingX: 0 }, { ...rectangular, spacingY: 0 },
  { ...rectangular, spacingX: NaN }, { ...rectangular, spacingY: Infinity },
  { ...rectangular, spacingX: 1e308 },
  { ...rectangular, columns: Number.MAX_SAFE_INTEGER, rows: 2 },
  { ...rectangular, groupCenter: [1, Infinity] },
  { ...polar, count: 1 }, { ...polar, count: 2.5 },
  { ...polar, angleStep: 0 }, { ...polar, angleStep: Infinity },
  { ...polar, angleStep: 120 }, { ...polar, angleStep: -120 },
  { ...polar, count: 2, angleStep: 360 }, { ...polar, count: 2, angleStep: -360 },
  { ...polar, centerX: NaN }, { ...polar, centerY: Infinity },
  ...["", "   ", null, true, false].flatMap(value => [
    { ...polar, centerX: value }, { ...polar, centerY: value },
    { ...polar, groupCenter: [value, 0] },
    { ...rectangular, spacingX: value },
  ]),
  { ...polar, rotateItems: "false" },
  { ...polar, rotateItems: false, groupCenter: [1, -2] },
]) {
  const validation = validateArraySpec(spec, 1, 1);
  assert.equal(validation.ready, false, JSON.stringify(spec));
  assert.ok(validation.message.length);
  assert.deepEqual(buildArrayTransforms(spec), [], "invalid previews never allocate copies");
}
{
  const line = { kind: "rectangular", columns: 2, rows: 1, spacingX: -10, spacingY: 0 };
  assert.equal(validateArraySpec(line, 1, 4999).ready, true);
  assert.equal(validateArraySpec(line, 1, 5000).ready, false);
  assert.equal(validateArraySpec(line, 2, 4999).ready, false);
  assert.equal(validateArraySpec(line, 1, 5000, 5001).ready, true);
  for (const sourceCount of [0, -1, .5, Infinity]) assert.equal(validateArraySpec(line, sourceCount, 1).ready, false);
  for (const totalCount of [-1, .5, Infinity]) assert.equal(validateArraySpec(line, 1, totalCount).ready, false);
  assert.equal(validateArraySpec({ ...polar, angleStep: -90 }, 1, 1).ready, true);
  assert.equal(validateArraySpec({ ...polar, count: 2, angleStep: 359.9 }, 1, 1).ready, true);
}

const transform = { angle: .713, center: [7, -11], dx: 17, dy: -2 };
const expectedPoint = ([x, y]) => {
  const dx = x - 7, dy = y + 11;
  return [24 + Math.cos(.713) * dx - Math.sin(.713) * dy,
    -13 + Math.sin(.713) * dx + Math.cos(.713) * dy];
};
const matrixPoint = (matrix, [x, y]) => [matrix[0] * x + matrix[2] * y + matrix[4], matrix[1] * x + matrix[3] * y + matrix[5]];
{
  const matrix = arrayTransformMatrix(transform);
  near(matrixPoint(matrix, [3, 8]), expectedPoint([3, 8]));
  near(arrayTransformMatrix(), [1, 0, 0, 1, 0, 0]);
  assert.throws(() => arrayTransformMatrix({ angle: Infinity }), RangeError);
  assert.throws(() => arrayTransformMatrix({ center: [NaN, 0] }), RangeError);
}

// Native conics and Bezier control points stay exact, with directed sweep intact.
for (const original of [
  { id: "line", kind: "line", x1: 3, y1: 7, x2: 10, y2: -4, closed: false },
  { id: "arc", kind: "circleArc", cx: 4, cy: -3, radius: 6, startAngle: 5.9, sweep: -4.7, rotation: .23, closed: false },
  { id: "ellipse", kind: "ellipse", cx: 4, cy: -3, radiusX: 8, radiusY: 2, rotation: -.41, closed: true },
  { id: "ellipse-arc", kind: "ellipseArc", cx: 4, cy: -3, radiusX: 8, radiusY: 2, rotation: -.41, startAngle: .2, sweep: -5.6, closed: false },
  { id: "bezier", kind: "bezier", points: [[-2, 4], [5, 11], [12, -5], [17, 3]], closed: false },
]) {
  const frozen = JSON.stringify(original), result = transformArrayEntity(original, transform);
  assert.equal(result.kind, original.kind);
  assert.equal(result.id, original.id, "the caller assigns each new ID when committing");
  for (const t of [0, .17, .5, .83, 1]) near(curvePoint(result, t), expectedPoint(curvePoint(original, t)));
  assert.equal(JSON.stringify(original), frozen, "array operations do not mutate the selected source");
  if (original.sweep !== undefined) assert.equal(result.sweep, original.sweep);
  if (original.kind === "circleArc") assert.equal(result.rotation, original.rotation, "circle orientation is folded into startAngle");
  if (original.points) assert.notEqual(result.points[0], original.points[0]);
}

// Rounded rectangles are rotated as their original straight and circular edges.
{
  const original = { id: "rounded", kind: "rectangle", x: 2, y: -4, width: 20, height: 12, radius: 3, closed: true, layer: "holes", metadata: { name: "window" } };
  const segments = editableSegments(original), result = transformArrayEntity(original, transform);
  assert.equal(result.kind, "path");
  assert.equal(result.closed, true);
  assert.equal(result.segments.length, 8);
  assert.equal(result.segments.filter((value) => value.kind === "circleArc").length, 4);
  assert.equal(result.segments.filter((value) => value.kind === "line").length, 4);
  assert.equal(result.layer, "holes");
  assert.deepEqual(result.metadata, original.metadata);
  assert.notEqual(result.metadata, original.metadata);
  result.segments.forEach((segment, index) => {
    assert.equal(segment.kind, segments[index].kind);
    for (const t of [0, .27, .8, 1]) near(curvePoint(segment, t), expectedPoint(curvePoint(segments[index], t)));
    near(curvePoint(segment, 1), curvePoint(result.segments[(index + 1) % result.segments.length], 0));
  });
  const translated = transformArrayEntity(original, { dx: -20, dy: 5 });
  assert.equal(translated.kind, "rectangle");
  assert.equal(translated.radius, 3);
  near([translated.x, translated.y], [-18, 1]);
  assert.equal(transformArrayEntity(original, { angle: Math.PI * 2 }).kind, "rectangle");
}

// A compound path keeps each analytic segment and arbitrary source attributes.
{
  const original = { id: "path", kind: "path", closed: false, brokenStart: true, brokenEnd: true,
    segments: [
      { kind: "line", x1: 0, y1: 0, x2: 4, y2: 0, tag: "edge" },
      { kind: "circleArc", cx: 4, cy: 2, radius: 2, startAngle: -Math.PI / 2, sweep: Math.PI / 2 },
      { kind: "bezier", points: [[6, 2], [9, 4], [10, 0]] },
      { kind: "ellipseArc", cx: 10, cy: -2, radiusX: 4, radiusY: 2, rotation: .31, startAngle: .2, sweep: 1.7 },
    ] };
  const result = transformArrayEntity(original, transform);
  assert.equal(result.closed, false);
  assert.equal(result.brokenStart, true); assert.equal(result.brokenEnd, true);
  assert.equal(result.segments[0].tag, "edge");
  assert.deepEqual(result.segments.map((value) => value.kind), original.segments.map((value) => value.kind));
  result.segments.forEach((segment, index) => {
    for (const t of [0, .3, 1]) near(curvePoint(segment, t), expectedPoint(curvePoint(original.segments[index], t)));
  });
}

// Point-based entities retain their own representation, closure and every point.
for (const kind of ["polyline", "arc", "spline", "freehand"]) {
  const original = { id: kind, kind, points: [[1, 2], [4, 11], [8, -2], [15, 3]], closed: true, sampled: kind === "freehand" };
  const result = transformArrayEntity(original, transform);
  assert.equal(result.kind, kind); assert.equal(result.closed, true); assert.equal(result.sampled, original.sampled);
  result.points.forEach((point, index) => near(point, expectedPoint(original.points[index])));
  if (kind === "spline" || kind === "arc") {
    const before = editableSegments(original), after = editableSegments(result);
    after.forEach((segment, index) => {
      for (const t of [0, .43, 1]) near(curvePoint(segment, t), expectedPoint(curvePoint(before[index], t)));
    });
  }
}
{
  const circle = { id: "circle", kind: "circle", cx: 4, cy: 7, radius: 3, closed: true };
  const result = transformArrayEntity(circle, transform);
  assert.equal(result.kind, "circle"); assert.equal(result.radius, 3); assert.equal(result.closed, true);
  near([result.cx, result.cy], expectedPoint([circle.cx, circle.cy]));
  const text = { id: "label", kind: "text", x: -4, y: 13, value: "加工标记", rotation: .12 };
  const transformed = transformArrayEntity(text, transform);
  near([transformed.x, transformed.y], expectedPoint([text.x, text.y]));
  near([transformed.rotation], [.833]); assert.equal(transformed.value, text.value);
}

// Direction-preserving polar arrays move a multi-selection as one rigid group.
{
  const source = [
    { id: "first", kind: "line", x1: 9, y1: -3, x2: 13, y2: -3 },
    { id: "second", kind: "line", x1: 9, y1: -1, x2: 13, y2: -1 },
  ];
  const transforms = buildArrayTransforms({ ...polar, rotateItems: false });
  for (const [index, transform] of transforms.entries()) {
    const copies = source.map((entity) => transformArrayEntity(entity, transform));
    const matrix = arrayTransformMatrix(transform);
    const centers = [[1, 8], [-9, -2], [1, -12]];
    near([(copies[0].x1 + copies[0].x2) / 2, (copies[0].y1 + copies[1].y1) / 2], centers[index]);
    near([copies[0].x2 - copies[0].x1, copies[0].y2 - copies[0].y1], [4, 0]);
    near([copies[1].x1 - copies[0].x1, copies[1].y1 - copies[0].y1], [0, 2]);
    near([copies[0].x1, copies[0].y1], matrixPoint(matrix, [source[0].x1, source[0].y1]));
  }
  // The standalone helper derives its own pivot if there is no selection group.
  const result = transformArrayEntity(source[0], { center: [1, -3], angle: Math.PI / 2, rotateItems: false });
  near([result.x1, result.y1, result.x2, result.y2], [-1, 7, 3, 7]);
}

console.log("TubeDesignerSketchArrayTest passed");
