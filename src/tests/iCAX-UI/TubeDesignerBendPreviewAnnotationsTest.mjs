import assert from "node:assert/strict";
import { buildBendPreviewDimensionAnnotations } from "../../apps/tube-designer/webpage/bendPreviewAnnotations.mjs";
import { makeIntegratedFormedMesh } from "../../apps/tube-designer/webpage/integratedFormedPreview.mjs";

const angle = 90;
const radians = angle * Math.PI / 180;
const matrix = [
  0, -1, 0, 100,
  1, 0, 0, 50,
  0, 0, 1, 20,
  0, 0, 0, 1,
];
const identity = [
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 0, 1,
];
const section = { width: 60, depth: 40, wallThickness: 2, cornerRadius: 3, innerRadius: 1 };

function previewFor(K, bendMethod = "cold") {
  const R = 40, straightA = 260, straightB = 260;
  const plan = {
    templateId: "bend",
    parameters: { angle, bendRadius: R, bendFactor: K, bendMethod },
    formedPreviewRecipe: bendMethod === "cold" ? {
      kind: "continuous-cold-bend", angle, radius: R, factor: K,
    } : null,
    resolvedWorkflow: { realization: "integrated" },
    designParts: [straightA, straightB].map((length) => ({ request: {
      profileRef: { id: "rect" }, parameters: section, length,
    } })),
    manufacturingParts: [{ request: { length: straightA + straightB + (R + K * section.wallThickness) * radians },
      matrix: identity, explodedMatrix: matrix }],
  };
  const formedPreview = bendMethod === "cold"
    ? { annotations: makeIntegratedFormedMesh(plan).metadata.annotations } : null;
  return { plan, formedPreview };
}

const near = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-6,
  `${actual} differs from ${expected}`);
const magnitude = (vector) => Math.hypot(...vector);
const span = (item) => item.end.map((coordinate, index) => coordinate - item.start[index]);

const cold = previewFor(0.5);
const formed = buildBendPreviewDimensionAnnotations(cold, "finished");
const blank = buildBendPreviewDimensionAnnotations(cold, "exploded");
assert.equal(formed.length, 1);
assert.equal(blank.length, 5);
assert.ok(formed.every((item) => item.start.every(Number.isFinite)
  && item.end.every(Number.isFinite) && item.offset.every(Number.isFinite)));
assert.deepEqual(cold.formedPreview.annotations.anchors.planeNormal, [0, 1, 0]);
assert.deepEqual(cold.formedPreview.annotations.anchors.radialUp, [0, 0, 1]);
assert.match(formed.find((item) => item.id === "bend-angle").label, /90°/);
assert.ok(formed.every((item) => !/K|展开|计算半径/.test(item.label)),
  "成品标注不能混入下料计算量");

// The assembly runtime emits row-major matrices, while the renderer transposes
// them before loading Three.js. These annotations must land on that same blank.
const first = blank.find((item) => item.id === "straight-a");
assert.deepEqual(first.start, [100, 50, 20]);
assert.deepEqual(first.end, [100, 310, 20]);
assert.deepEqual(first.offset, [54, 0, 0]);
const total = blank.find((item) => item.id === "total-blank");
near(magnitude(span(total)), cold.plan.manufacturingParts[0].request.length);
assert.match(total.label, /584\.4 mm/);

const compensation = blank.find((item) => item.id === "k-contribution");
assert.match(compensation.label, /Ktθ\(弧度\)/);
assert.match(compensation.label, /\+1\.57 mm/);
assert.ok(!compensation.label.includes("90°="), "degree values must not be multiplied as radians");
assert.ok(blank.every((item) => item.label.length < 50), "dimension sprite text must fit the viewport canvas");

const moreK = previewFor(1);
const moreBlank = buildBendPreviewDimensionAnnotations(moreK, "blank");
const moreCompensation = moreBlank.find((item) => item.id === "k-contribution");
const moreTotal = moreBlank.find((item) => item.id === "total-blank");
assert.match(moreCompensation.label, /\+3\.14 mm/);
assert.match(moreTotal.label, /585\.97 mm/);
assert.ok(magnitude(span(moreTotal)) > magnitude(span(total)));

assert.equal(buildBendPreviewDimensionAnnotations(previewFor(0.5, "notched"), "blank").length, 0);
assert.equal(buildBendPreviewDimensionAnnotations({ plan: { templateId: "node-v-notch-integrated" } }).length, 0);
assert.equal(buildBendPreviewDimensionAnnotations({ plan: cold.plan }, "blank").length, 5,
  "the native blank remains annotatable if the formed illustration is unavailable");

console.log("TubeDesignerBendPreviewAnnotationsTest passed");
