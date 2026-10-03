import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { buildAssemblyFinishedShapePreview } from "../../apps/tube-designer/webpage/assemblyFinishedShape.mjs";
import { parseRenderGeometryResource } from "../../iCAX-UI/SDK/Viewport/renderResource.mjs";

const request = (length, parameters = {}) => ({
  profileRef: { scope: "system", id: "rect" }, length,
  parameters: { width: 60, depth: 40, wallThickness: 2,
    cornerRadius: 3, innerRadius: 1, ...parameters },
});
const product = (sceneParameters = { angle: 90 }) => ({
  sceneParameters,
  designParts: [
    { role: "segmentA", request: request(260) },
    { role: "segmentB", request: request(300) },
  ],
});
const vector = (points, a, b) => [points[3 * b] - points[3 * a],
  points[3 * b + 1] - points[3 * a + 1], points[3 * b + 2] - points[3 * a + 2]];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a.reduce((sum, value, index) => sum + value * b[index], 0);
const near = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-6,
  `expected ${expected}, got ${actual}`);

function assertClosedHollowMesh(mesh) {
  const count = mesh.metadata.contourVertexCount;
  const points = mesh.positions;
  const edges = new Map();
  assert.equal(points.length, 6 * count * 3);
  assert.equal(mesh.indices.length % 3, 0);
  assert.ok(points.every(Number.isFinite));
  for (let index = 0; index < mesh.indices.length; index += 3) {
    const [a, b, c] = mesh.indices.slice(index, index + 3);
    const normal = cross(vector(points, a, b), vector(points, a, c));
    assert.ok(dot(normal, normal) > 1e-10, "surface triangles must not collapse");
    for (const [from, to] of [[a, b], [b, c], [c, a]]) {
      const key = `${Math.min(from, to)}:${Math.max(from, to)}`;
      const edge = edges.get(key) ?? { count: 0, winding: 0 };
      edge.count++;
      edge.winding += from < to ? 1 : -1;
      edges.set(key, edge);
    }
  }
  assert.ok(edges.size > 0);
  for (const edge of edges.values()) {
    assert.equal(edge.count, 2, "the two tube mouths must be annular, without shell gaps");
    assert.equal(edge.winding, 0, "surface winding must remain outward");
  }
  // The first mouth joins two different contour loops, leaving its center open.
  near(Math.max(...Array.from({ length: count }, (_, i) => points[3 * i + 1])), 30);
  near(Math.max(...Array.from({ length: count }, (_, i) => points[3 * (3 * count + i) + 1])), 28);
  near(Math.max(...Array.from({ length: count }, (_, i) => points[3 * i + 2])), 20);
  near(Math.max(...Array.from({ length: count }, (_, i) => points[3 * (3 * count + i) + 2])), 18);
  const normalAt = (offset) => {
    const [a, b, c] = mesh.indices.slice(offset, offset + 3);
    return cross(vector(points, a, b), vector(points, a, c));
  };
  assert.ok(normalAt(0)[1] > 0, "outer surface faces away from the lumen");
  assert.ok(normalAt(6 * count)[1] < 0, "inner surface faces into the lumen");
  assert.ok(normalAt(24 * count)[0] < 0, "first annular mouth faces backwards");
  assert.ok(dot(normalAt(24 * count + 6), mesh.metadata.endDirection) > 0,
    "second annular mouth faces along the second leg");
}

for (const angle of [45, 90, 135]) {
  const preview = buildAssemblyFinishedShapePreview(product({ angle }));
  assert.ok(preview);
  assert.deepEqual(preview.mesh.metadata.centerlineCorner, [0, 0, 0]);
  assert.equal(preview.mesh.metadata.angle, angle);
  assert.deepEqual(preview.mesh.metadata.section,
    { width: 60, depth: 40, wall: 2, outerRadius: 3, innerRadius: 1,
      lengthA: 260, lengthB: 300 });
  assertClosedHollowMesh(preview.mesh);
  assert.deepEqual(preview.rows[0].roles, ["segmentA", "segmentB"]);
  const bytes = preview.resources.get(preview.rows[0].data.geometry.url);
  assert.ok(bytes instanceof ArrayBuffer);
  const parsed = parseRenderGeometryResource(bytes);
  assert.equal(parsed.kind, "mesh");
  assert.equal(parsed.indices.length, preview.mesh.indices.length);
}

const cold = { ...product(), parameters: { bendRadius: 40, bendFactor: 0.3 },
  formedPreviewRecipe: { kind: "continuous-cold-bend", radius: 40, factor: 0.3 },
  manufacturingParts: [{ request: { length: 610 } }], processDrafts: {} };
const slotted = { ...product(), parameters: { bendRadius: 90, bendFactor: 0.8 },
  formedPreviewRecipe: { kind: "node-groove-fold", angle: 45 },
  manufacturingParts: [{ request: { length: 570, features: ["V-slot"] } }],
  processDrafts: { slotCount: 6 } };
assert.deepEqual(buildAssemblyFinishedShapePreview(cold).mesh,
  buildAssemblyFinishedShapePreview(slotted).mesh,
  "the same product scene must have the same mesh under different processes");
const unreadableProcess = product();
for (const key of ["parameters", "formedPreviewRecipe", "manufacturingParts",
  "processDrafts", "resolvedWorkflow"]) Object.defineProperty(unreadableProcess, key, {
  get() { throw new Error(`${key} must not be read`); },
});
assert.ok(buildAssemblyFinishedShapePreview(unreadableProcess));

assert.equal(buildAssemblyFinishedShapePreview(product({ angle: 90, jointAngle: 45 })).mesh.metadata.angle, 90);
assert.equal(buildAssemblyFinishedShapePreview(product({ jointAngle: 45 })).mesh.metadata.angle, 45);
assert.equal(buildAssemblyFinishedShapePreview(product({})).mesh.metadata.angle, 90);
const rotated = buildAssemblyFinishedShapePreview(product({ jointAngle: 90, planeRotation: 90 }));
near(rotated.mesh.metadata.endDirection[1], -1);
near(rotated.mesh.metadata.endDirection[2], 0);
near(rotated.mesh.metadata.miterPlane[1], -1);
near(rotated.mesh.metadata.miterPlane[2], 0);
for (const angle of [0, 180, -10, NaN])
  assert.equal(buildAssemblyFinishedShapePreview(product({ angle })), null);
assert.equal(buildAssemblyFinishedShapePreview({ ...product(), designParts: [product().designParts[0]] }), null);
const differentSection = product();
differentSection.designParts[1].request.parameters.width = 50;
assert.equal(buildAssemblyFinishedShapePreview(differentSection), null);
const differentlyRounded = product();
differentlyRounded.designParts[1].request.parameters.cornerRadius = 5;
assert.equal(buildAssemblyFinishedShapePreview(differentlyRounded), null);
const unsupportedProfile = product();
unsupportedProfile.designParts[0].request.profileRef.id = "circle";
assert.equal(buildAssemblyFinishedShapePreview(unsupportedProfile), null);
const fullyRounded = product();
for (const part of fullyRounded.designParts) Object.assign(part.request.parameters,
  { depth: 60, cornerRadius: 30, innerRadius: 28 });
const roundMesh = buildAssemblyFinishedShapePreview(fullyRounded)?.mesh;
assert.ok(roundMesh, "a fully rounded square section is still a supported hollow section");
for (let index = 0; index < roundMesh.indices.length; index += 3) {
  const [a, b, c] = roundMesh.indices.slice(index, index + 3);
  const normal = cross(vector(roundMesh.positions, a, b),
    vector(roundMesh.positions, a, c));
  assert.ok(dot(normal, normal) > 1e-10);
}
const shortLegs = product({ angle: 135 });
shortLegs.designParts[0].request.length = 30;
assert.equal(buildAssemblyFinishedShapePreview(shortLegs), null);

console.log(`Assembly finished shape: ${fileURLToPath(import.meta.url)} passed`);
