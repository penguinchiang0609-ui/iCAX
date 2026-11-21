import assert from "node:assert/strict";
import test from "node:test";
import { createPlanarDynamicInput } from "../../iCAX-UI/SDK/Cad/planarDynamicInput.mjs";
import { PlanarPrimitiveSession, ellipsePrimitiveParameters, standalonePrimitivePayload,
  standaloneScalarInput, primitiveInputOptions } from "../../iCAX-UI/SDK/Cad/planarDrawingPrimitives.mjs";
import { primitiveDimensionDescriptors } from "../../iCAX-UI/SDK/Cad/planarPrimitiveDimensions.mjs";
import { curveSegmentPoint } from "../../iCAX-UI/SDK/Cad/planarCurveSegments.mjs";

const near = (actual, expected, message = "coordinate") => assert.ok(Math.abs(actual - expected) < 1e-9,
  `${message}: ${actual} differs from ${expected}`);
const point = (actual, expected) => { assert.equal(actual.length, expected.length);actual.forEach((value, i) => near(value, expected[i])); };
const points = (actual, expected) => { assert.equal(actual.length, expected.length);actual.forEach((value, i) => point(value, expected[i])); };

// Only DOM presentation is absent. Construction, draft locking, keyboard
// actions, curve evaluation and preview sampling are the production SDK.
function construction(primitive, accepted = [[0, 0]], options = {}) {
  const view = { ui: { activeTool: `plane.draw-${primitive}`, drawingEpoch: 7,
    commandPoints: structuredClone(accepted), drawingShapeOptions: { [primitive]: options }, busy: false } };
  const hud = createPlanarDynamicInput({ getState: current => current.ui,
    getPrimitive: ui => ui.activeTool.slice(11), getRoot: () => null, getViewport: () => null });
  hud.ensureDrawingDynamicInput(view);
  return { view, hud, get state() { return view.ui.dynamicInput; },
    type(field, text) { assert.equal(hud.updateDrawingDimensionDraft(view, field, text), true); },
    key(field, key = "Enter", overrides = {}) {
      const event = { key, target: { dataset: { planarDimension: field } },
        preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; }, ...overrides };
      const action = hud.handleDrawingDimensionKey(view, event);
      if (action) { assert.equal(event.prevented, true);assert.equal(event.stopped, true); }
      return action;
    },
  };
}

test("ellipse A Enter then B Enter advances a rotated construction without duplicating its center", () => {
  const c = construction("ellipse", [[10, -4]]), r2 = Math.SQRT2;
  c.hud.constrainDrawingDimensionPoint(c.view, [13, -1]);
  c.type("radiusX", "10");const first = c.key("radiusX");
  points(first.preparePoints, [[10, -4]]);point(first.point, [10 + 5 * r2, -4 + 5 * r2]);
  assert.equal(first.nextField, "radiusY");assert.equal(c.state.activeField, "radiusY");
  // Feed the independently known accepted prefix back through the SDK's
  // public synchronization boundary, as a consumer does after accepting A.
  c.view.ui.commandPoints = [[10, -4], [10 + 5 * r2, -4 + 5 * r2]];
  c.hud.ensureDrawingDynamicInput(c.view);c.type("radiusY", "4");const second = c.key("radiusY");
  points(second.preparePoints, [[10, -4], [10 + 5 * r2, -4 + 5 * r2]]);
  point(second.point, [10 - 2 * r2, -4 + 2 * r2]);
  const payload = standalonePrimitivePayload("ellipse", [...second.preparePoints, second.point]);
  near(payload.radiusX, 10);near(payload.radiusY, 4);near(payload.rotationDeg, 45);assert.equal(payload.sweepAngleDeg, 360);
});

test("ellipse A Tab B Enter emits one complete three-point construction from two entered dimensions", () => {
  const c = construction("ellipse", [[2, 3]]);
  c.hud.constrainDrawingDimensionPoint(c.view, [5, 7]); // a 3-4-5 direction
  c.type("radiusX", "10");assert.deepEqual(c.key("radiusX", "Tab"), { handled: true });
  assert.deepEqual(c.view.ui.commandPoints, [[2, 3]]);assert.equal(c.state.activeField, "radiusY");
  assert.equal(c.state.drafts.radiusX.locked, true);
  c.type("radiusY", "4");const action = c.key("radiusY");
  points(action.preparePoints, [[2, 3], [8, 11]]);point(action.point, [-1.2, 5.4]);
  const prefix = [...action.preparePoints, action.point];assert.equal(prefix.length, 3);
  const payload = new PlanarPrimitiveSession("ellipse", prefix).payload();
  near(payload.radiusX, 10);near(payload.radiusY, 4);
});

test("unfinished A survives Tab and prevents B Enter from consuming any accepted prefix", () => {
  const c = construction("ellipse", [[3, 8]]), before = structuredClone(c.view.ui.commandPoints);
  c.hud.constrainDrawingDimensionPoint(c.view, [13, 8]);c.type("radiusX", "-");
  assert.deepEqual(c.key("radiusX", "Tab"), { handled: true });c.type("radiusY", "4");
  assert.deepEqual(c.key("radiusY"), { handled: true });assert.equal(c.state.errorField, "radiusX");
  assert.deepEqual(c.view.ui.commandPoints, before);assert.equal(c.state.drafts.radiusX.text, "-");
  c.type("radiusX", "10");const corrected = c.key("radiusY");
  points(corrected.preparePoints, [[3, 8], [13, 8]]);point(corrected.point, [3, 12]);
});

test("ellipse B point uses its perpendicular component rather than its distance from the center", () => {
  const payload = standalonePrimitivePayload("ellipse", [[2, 3], [8, 11], [4.8, 8.4]]);
  // Projection of (2.8,5.4) onto (-4/5,3/5) is exactly 1.
  near(payload.radiusX, 10);near(payload.radiusY, 1);near(payload.rotationDeg, Math.atan2(4, 3) * 180 / Math.PI);
  assert.throws(() => standalonePrimitivePayload("ellipse", [[2, 3], [8, 11], [5, 7]]), /大于零/);
});

test("clockwise ellipseArc scalar input preserves a negative sweep and its rotated exact endpoint", () => {
  const prefix = [[5, 6], [5, 16], [9, 6]], before = structuredClone(prefix);
  const input = standaloneScalarInput("ellipseArc", prefix, -90);
  point(input.point, [9, 6]);assert.deepEqual(input.options, { sweepAngleDeg: -90 });
  const payload = standalonePrimitivePayload("ellipseArc", [...prefix, input.point], input.options);
  near(payload.radiusX, 10);near(payload.radiusY, 4);near(payload.rotationDeg, 90);assert.equal(payload.sweepAngleDeg, -90);
  point(curveSegmentPoint({ type: "ellipseArc", ...payload }, .5), [5 + 2 * Math.SQRT2, 6 + 5 * Math.SQRT2]);
  assert.deepEqual(prefix, before);
});

test("ellipseArc HUD accepts a negative sweep independently of the B construction side", () => {
  const c = construction("ellipseArc", [[1, 2], [11, 2], [1, 6]]);
  c.type("sweepAngleDeg", "-120");const action = c.key("sweepAngleDeg");
  points(action.preparePoints, [[1, 2], [11, 2], [1, 6]]);point(action.point, [-4, 2 - 2 * Math.sqrt(3)]);
  assert.equal(action.sweepAngleDeg, -120);
  const options = primitiveInputOptions("ellipseArc", {}, c.state);
  const payload = standalonePrimitivePayload("ellipseArc", [...action.preparePoints, action.point], options);
  assert.equal(payload.sweepAngleDeg, -120);
  const descriptors = primitiveDimensionDescriptors(c.state, key => Number(c.state.drafts[key].text));
  const angle = descriptors.find(item => item.key === "sweepAngleDeg");
  assert.equal(angle.value, -120);assert.equal(angle.unit, "deg");assert.equal(angle.sweepAngleDeg, 120);
});

test("zero, out-of-range and nonfinite ellipseArc sweeps retain all three accepted construction points", () => {
  for (const text of ["0", "-361", "361", "Infinity", "1e999", "-"]) {
    const c = construction("ellipseArc", [[1, 2], [11, 2], [1, 6]]), before = structuredClone(c.view.ui.commandPoints);
    c.type("sweepAngleDeg", text);assert.deepEqual(c.key("sweepAngleDeg"), { handled: true });
    assert.equal(c.state.errorField, "sweepAngleDeg");assert.deepEqual(c.view.ui.commandPoints, before);
    assert.equal(c.state.drafts.sweepAngleDeg.text, text);
  }
});

test("ellipseArc exact coordinates reject an off-ellipse endpoint while pointer direction can project", () => {
  const input = [[0, 0], [10, 0], [0, 4], [10, 4]];
  assert.throws(() => ellipsePrimitiveParameters(input), /终点必须在椭圆上/);
  const payload = standalonePrimitivePayload("ellipseArc", input, { pointerInput: true });
  near(payload.sweepAngleDeg, 45);
  point(curveSegmentPoint({ type: "ellipseArc", ...payload }, 1), [5 * Math.SQRT2, 2 * Math.SQRT2]);
});

test("polygon R Enter retains its heading and uses the configured number of sides", () => {
  const c = construction("polygon", [[3, -2]], { sides: 7 });
  c.hud.constrainDrawingDimensionPoint(c.view, [6, 2]);c.type("radius", "10");const action = c.key("radius");
  points(action.preparePoints, [[3, -2]]);point(action.point, [9, 6]);
  const session = new PlanarPrimitiveSession("polygon", [...action.preparePoints, action.point], { sides: 7 });
  const payload = session.payload();assert.equal(payload.sides, 7);near(payload.radius, 10);
  const preview = session.preview();assert.equal(preview.outline.length, 8);point(preview.outline[0], [9, 6]);point(preview.outline.at(-1), [9, 6]);
  for (const vertex of preview.outline) near(Math.hypot(vertex[0] - 3, vertex[1] + 2), 10);
  for (let i = 0; i < 7; i++) near(Math.hypot(...preview.outline[i].map((value, axis) => value - preview.outline[i + 1][axis])), 20 * Math.sin(Math.PI / 7));
});

test("polygon fractional or out-of-range side counts reject without modifying construction coordinates", () => {
  const input = [[2, 3], [12, 3]], before = structuredClone(input);
  for (const sides of [2, 65, 4.5, "", "invalid"]) assert.throws(() => standalonePrimitivePayload("polygon", input, { sides }), /3到64/);
  assert.deepEqual(input, before);assert.equal(standalonePrimitivePayload("polygon", input).sides, 6);
  assert.equal(standalonePrimitivePayload("polygon", input, { sides: 64 }).sides, 64);
});

test("star R Enter creates only the outer prefix; invalid r/R relationships keep that prefix intact", () => {
  const c = construction("star", [[0, 0]], { tips: 5 });
  c.hud.constrainDrawingDimensionPoint(c.view, [10, 0]);c.type("outerRadius", "10");const outer = c.key("outerRadius");
  points(outer.preparePoints, [[0, 0]]);point(outer.point, [10, 0]);assert.equal(outer.nextField, "innerRadius");
  c.view.ui.commandPoints = [[0, 0], [10, 0]];c.hud.ensureDrawingDynamicInput(c.view);
  const before = structuredClone(c.view.ui.commandPoints);
  for (const text of ["10", "11", "0", "-4"]) {
    c.type("innerRadius", text);assert.deepEqual(c.key("innerRadius"), { handled: true });
    assert.equal(c.state.errorField, "innerRadius");assert.deepEqual(c.view.ui.commandPoints, before);
  }
  c.type("innerRadius", "4");const inner = c.key("innerRadius");
  points(inner.preparePoints, [[0, 0], [10, 0]]);
  point(inner.point, [1 + Math.sqrt(5), Math.sqrt(10 - 2 * Math.sqrt(5))]);
  const payload = standalonePrimitivePayload("star", [...inner.preparePoints, inner.point], { tips: 5 });
  near(payload.outerRadius, 10);near(payload.innerRadius, 4);assert.equal(payload.tips, 5);
});

test("star preview alternates both radii, honors custom tips and remains immutable on invalid inner radius", () => {
  const input = [[3, 2], [13, 2], [7, 2]], before = structuredClone(input);
  const session = new PlanarPrimitiveSession("star", input, { tips: 7 }), preview = session.preview();
  assert.equal(preview.outline.length, 15);assert.equal(session.payload().tips, 7);
  preview.outline.slice(0, -1).forEach((vertex, i) => near(Math.hypot(vertex[0] - 3, vertex[1] - 2), i % 2 ? 4 : 10));
  assert.deepEqual(input, before);
  const invalid = [[3, 2], [13, 2], [14, 2]], invalidBefore = structuredClone(invalid);
  assert.throws(() => standalonePrimitivePayload("star", invalid), /内半径必须小于/);assert.deepEqual(invalid, invalidBefore);
});

test("cubic Bezier preview follows independently known Bernstein coordinates, not its control polygon", () => {
  const input = [[0, 0], [0, 12], [12, 12], [12, 0]], before = structuredClone(input);
  const session = new PlanarPrimitiveSession("bezier", input), preview = session.preview();
  assert.ok(preview.outline.length > 4);
  point(preview.outline[0], [0, 0]);point(preview.outline.at(-1), [12, 0]);
  assert.ok(preview.outline.some(p => Math.hypot(p[0] - 1.875, p[1] - 6.75) < 1e-9));
  assert.ok(preview.outline.some(p => Math.hypot(p[0] - 6, p[1] - 9) < 1e-9));
  assert.deepEqual(session.payload(), { primitive: "bezier", controlPoints: input });assert.deepEqual(input, before);
});

test("a six-pole cubic B-spline uses every pole and matches independent rational basis evaluations", () => {
  const input = [[0, 0], [2, 8], [6, -3], [10, 9], [14, -4], [20, 2]], before = structuredClone(input);
  const session = new PlanarPrimitiveSession("bspline", input), payload = session.payload(), preview = session.preview();
  assert.equal(payload.degree, 3);assert.deepEqual(payload.controlPoints, input);assert.equal(preview.controls.length, 6);
  assert.ok(preview.outline.length > 6);point(preview.outline[0], [0, 0]);point(preview.outline.at(-1), [20, 2]);
  // Open clamped knots [0,0,0,0,1/3,2/3,1,1,1,1]. At t=1/4,
  // the six basis numerators over 512 are [8,234,234,36,0,0];
  // at 1/2: [0,16,240,240,16,0], and at 3/4: [0,0,36,234,234,8].
  for (const [parameter, expected] of [[.25, [279 / 64, 747 / 256]], [.5, [8, 47 / 16]], [.75, [749 / 64, 539 / 256]]]) {
    point(curveSegmentPoint({ type: "bspline", ...payload }, parameter), expected);
    assert.ok(preview.outline.some(p => Math.hypot(p[0] - expected[0], p[1] - expected[1]) < 1e-9));
  }
  assert.deepEqual(input, before);
});

test("B-spline keeps its unfinished control prefix and rejects fewer than four or more than 256 poles", () => {
  const prefix = [[0, 0], [3, 8], [7, -2]], before = structuredClone(prefix);
  const unfinished = new PlanarPrimitiveSession("bspline", prefix);assert.equal(unfinished.ready, false);
  assert.throws(() => unfinished.payload(), /4到256/);assert.deepEqual(prefix, before);
  const full = new PlanarPrimitiveSession("bspline", [...prefix, [10, 2], [12, 7], [17, 0]]);
  assert.equal(full.ready, true);assert.equal(full.payload().controlPoints.length, 6);
  const tooMany = Array.from({ length: 257 }, (_, i) => [i, Math.sin(i)]);
  assert.throws(() => new PlanarPrimitiveSession("bspline", tooMany).validateConstruction(), /256/);
});

test("triangle construction preserves its three exact vertices and previews a closed boundary", () => {
  const input = [[-2, 3], [8, -1], [5, 9]], session = new PlanarPrimitiveSession("triangle", input);
  assert.deepEqual(session.payload(), { primitive: "triangle", points: input });
  points(session.preview().outline, [[-2, 3], [8, -1], [5, 9], [-2, 3]]);
});
