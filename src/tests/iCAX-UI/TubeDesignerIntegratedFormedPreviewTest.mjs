import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { buildIntegratedFormedPreview, makeIntegratedFormedMesh } from "../../apps/tube-designer/webpage/integratedFormedPreview.mjs";
import { parseRenderGeometryResource } from "../../iCAX-UI/SDK/Viewport/renderResource.mjs";

const root = new URL("../../apps/tube-designer/templates/assembly/", import.meta.url);
const vector = (points, a, b) => [points[3 * b] - points[3 * a], points[3 * b + 1] - points[3 * a + 1], points[3 * b + 2] - points[3 * a + 2]];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a.reduce((sum, value, index) => sum + value * b[index], 0);

function planFromTemplate(template, angle, parameterOverrides = {}) {
  const parameters = {
    ...Object.fromEntries((template.parameters ?? []).map((item) => [item.key, item.defaultValue])),
    angle, ...parameterOverrides,
  };
  const source = template.previewScene.formedPreviews?.find((item) =>
    !item.when || (item.when.op === "eq" && parameters[item.when.parameter] === item.when.value));
  const formedPreviewRecipe = source ? Object.fromEntries(Object.entries(source)
    .filter(([key]) => key !== "when")
    .map(([key, value]) => [key, typeof value === "string" && /^\$[A-Za-z][A-Za-z0-9_]*$/.test(value)
      ? parameters[value.slice(1)] : value])) : null;
  return {
    templateId: template.id,
    parameters,
    formedPreviewRecipe,
    resolvedWorkflow: { realization: template.manufacturingPlan.realization,
      partOperations: template.id === "bend" ? [] : [{ processId: "node-slot", toolParameters: {
        [template.id === "node-v-notch-integrated" ? "leaveBottom" : "bridge"]: 1,
        bottomReference: "outer", bottomCut: false,
      } }] },
    designParts: template.previewScene.designParts.map((part) => ({
      request: { profileRef: part.profileRef, parameters: part.parameters, length: part.length },
    })),
    manufacturingParts: template.previewScene.manufacturingParts.map((part) => ({
      blankId: part.id, participantRoles: template.manufacturingPlan.blankParts[0].participants,
      request: { length: template.id === "bend"
        ? 520 + (Number(parameterOverrides.bendRadius) + Number(parameterOverrides.bendFactor) * 2)
          * angle * Math.PI / 180 : 520 },
    })),
  };
}

function assertClosedManifold(mesh) {
  const edgeCounts = new Map();
  const positions = mesh.positions;
  assert.equal(positions.length % 3, 0);
  assert.equal(mesh.indices.length % 3, 0);
  assert.ok(positions.every(Number.isFinite), "mesh vertices must be finite");
  for (let offset = 0; offset < mesh.indices.length; offset += 3) {
    const [a, b, c] = mesh.indices.slice(offset, offset + 3);
    const normal = cross(vector(positions, a, b), vector(positions, a, c));
    assert.ok(dot(normal, normal) > 1e-10, "triangles may not collapse");
    for (const [from, to] of [[a, b], [b, c], [c, a]]) {
      const key = `${Math.min(from, to)}:${Math.max(from, to)}`;
      const current = edgeCounts.get(key) ?? { count: 0, direction: 0 };
      current.count++;
      current.direction += from < to ? 1 : -1;
      edgeCounts.set(key, current);
    }
  }
  assert.ok(edgeCounts.size > 0);
  for (const edge of edgeCounts.values()) {
    assert.equal(edge.count, 2, "one continuous hollow piece must have no exposed shell edge");
    assert.equal(edge.direction, 0, "neighboring triangles must have opposite edge winding");
  }
}

function assertClosedOrientedSurface(mesh) {
  assertClosedManifold(mesh);
  const positions = mesh.positions;
  const count = mesh.metadata.contourVertexCount;
  const normalAt = (offset) => {
    const [a, b, c] = mesh.indices.slice(offset, offset + 3);
    return cross(vector(positions, a, b), vector(positions, a, c));
  };
  assert.ok(normalAt(0)[1] > 0, "outer long face must point outward");
  assert.ok(normalAt(6 * count)[1] < 0, "inner long face must point into the lumen");
  assert.ok(normalAt(24 * count)[0] < 0, "first annular mouth must face backward");
  assert.ok(dot(normalAt(24 * count + 6),
    [Math.cos(mesh.metadata.angle * Math.PI / 180), 0,
      Math.sin(mesh.metadata.angle * Math.PI / 180)]) > 0,
  "second annular mouth must face along the bent leg");
}

function ringCenter(mesh, ringIndex) {
  const count = mesh.metadata.contourVertexCount;
  const center = [0, 0, 0];
  for (let vertex = ringIndex * count; vertex < (ringIndex + 1) * count; vertex++) {
    for (let axis = 0; axis < 3; axis++) center[axis] += mesh.positions[vertex * 3 + axis] / count;
  }
  return center;
}

function assertNear(actual, expected, label) {
  assert.ok(Math.abs(actual - expected) < 1e-6, `${label}: expected ${expected}, got ${actual}`);
}

function assertMiteredNotIntersecting(mesh) {
  const count = mesh.metadata.contourVertexCount;
  const positions = mesh.positions;
  const [centerX] = mesh.metadata.centerlineCorner;
  const halfTangent = mesh.metadata.miterPlane[2];
  const plane = (index) => positions[3 * index] + halfTangent * positions[3 * index + 2] - centerX;
  // Both tube halves terminate on the same miter plane. Their other rings sit
  // strictly on opposite sides, unlike the old overlapping complete tubes.
  for (const offset of [0, 3 * count]) for (let index = 0; index < count; index++) {
    assert.ok(plane(offset + index) < -1e-6);
    assert.ok(Math.abs(plane(offset + count + index)) < 1e-6);
    assert.ok(plane(offset + 2 * count + index) > 1e-6);
  }
  assert.deepEqual(mesh.metadata.hinge.map((value) => Number(value.toFixed(6))), [0, 0, -19]);
  assert.equal(Number(mesh.metadata.outerCorner[0].toFixed(6)),
    Number(Math.tan(mesh.metadata.angle * Math.PI / 360).toFixed(6)));
  // Each remote end has distinct outer and inner loops: the pipe remains
  // hollow and has an annular wall, rather than solid capped rods.
  for (const station of [0, 2]) {
    const outer = station * count, inner = (station + 3) * count;
    const distance = Math.hypot(...vector(positions, inner, outer));
    assert.ok(distance > 1, "the mouth must show a wall between outer and inner loops");
  }
}

for (const id of ["node-v-notch-integrated", "node-edge-arc-integrated"]) {
  const template = JSON.parse(await readFile(new URL(`${id}/assembly.json`, root), "utf8"));
  for (const angle of [45, 90, 135, 170]) {
    const plan = planFromTemplate(template, angle);
    const mesh = makeIntegratedFormedMesh(plan);
    assert.equal(mesh.metadata.angle, angle);
    assertClosedOrientedSurface(mesh);
    assertMiteredNotIntersecting(mesh);
    const preview = buildIntegratedFormedPreview({ plan });
    assert.equal(preview.rows.length, 1);
    assert.deepEqual(preview.rows[0].roles, ["segmentA", "segmentB"]);
    const bytes = preview.resources.get(preview.rows[0].data.geometry.url);
    assert.ok(bytes instanceof ArrayBuffer);
    const decoded = parseRenderGeometryResource(bytes);
    assert.equal(decoded.kind, "mesh");
    assert.equal(decoded.indices.length, mesh.indices.length);
  }
}

const cutThrough = planFromTemplate(JSON.parse(await readFile(
  new URL("node-edge-arc-integrated/assembly.json", root), "utf8")), 90);
cutThrough.resolvedWorkflow.partOperations[0].toolParameters.bottomCut = true;
assert.throws(() => makeIntegratedFormedMesh(cutThrough), /槽底已切断/);

const bendTemplate = JSON.parse(await readFile(new URL("bend/assembly.json", root), "utf8"));
for (const angle of [45, 90, 135, 170]) for (const bendRadius of [40, 80]) {
  const plan = planFromTemplate(bendTemplate, angle,
    { bendRadius, bendFactor: 0.5 });
  const mesh = makeIntegratedFormedMesh(plan);
  assertClosedManifold(mesh);
  const count = mesh.metadata.contourVertexCount;
  const normalAt = (offset) => {
    const [a, b, c] = mesh.indices.slice(offset, offset + 3);
    return cross(vector(mesh.positions, a, b), vector(mesh.positions, a, c));
  };
  assert.ok(normalAt(0)[1] > 0, "cold bend outer surface must face outward");
  assert.ok(normalAt(6 * count)[1] < 0, "cold bend inner surface must face the lumen");
  const mouthOffset = 12 * count * (mesh.metadata.stationCount - 1);
  assert.ok(normalAt(mouthOffset)[0] < 0, "first cold bend mouth must face backward");
  assert.ok(dot(normalAt(mouthOffset + 6),
    [Math.cos(angle * Math.PI / 180), 0, Math.sin(angle * Math.PI / 180)]) > 0,
  "second cold bend mouth must face along its straight leg");
  const radius = bendRadius + plan.parameters.bendFactor * mesh.metadata.section.wall;
  assertNear(mesh.metadata.centerlineRadius, radius, "developed centerline radius");
  assertNear(ringCenter(mesh, 0)[0], -260, "first straight end");
  const arcStart = ringCenter(mesh, 1);
  assertNear(arcStart[0], 0, "arc start x");
  assertNear(arcStart[1], 0, "arc start y");
  assertNear(arcStart[2], 0, "arc start z");
  const halfArc = ringCenter(mesh, 1 + mesh.metadata.arcSegments / 2);
  const halfAngle = angle * Math.PI / 360;
  // Odd arc segment counts interpolate at the nearest station instead.
  if (mesh.metadata.arcSegments % 2 === 0) {
    assertNear(halfArc[0], radius * Math.sin(halfAngle), "arc midpoint x");
    assertNear(halfArc[2], radius * (1 - Math.cos(halfAngle)), "arc midpoint z");
  }
  const arcEnd = ringCenter(mesh, mesh.metadata.stationCount - 2);
  const end = ringCenter(mesh, mesh.metadata.stationCount - 1);
  const radians = angle * Math.PI / 180;
  assertNear(arcEnd[0], radius * Math.sin(radians), "arc end x");
  assertNear(arcEnd[1], 0, "arc end y");
  assertNear(arcEnd[2], radius * (1 - Math.cos(radians)), "arc end z");
  assertNear(end[0] - arcEnd[0], 260 * Math.cos(radians), "second straight x");
  assertNear(end[1], 0, "second straight end y");
  assertNear(end[2] - arcEnd[2], 260 * Math.sin(radians), "second straight z");
  const preview = buildIntegratedFormedPreview({ plan });
  assert.deepEqual(preview.rows[0].roles, ["segmentA", "segmentB"]);
  assert.equal(parseRenderGeometryResource(preview.resources.get(preview.rows[0].data.geometry.url)).kind, "mesh");
  const { values, anchors } = preview.annotations;
  assert.equal(preview.annotations.kind, "cold-bend-development");
  assertNear(values.angle, angle, "annotated angle");
  assertNear(values.R, bendRadius, "annotated radius");
  assertNear(values.t, 2, "annotated wall thickness");
  assertNear(values.K, 0.5, "annotated bend factor");
  assertNear(values.calculationRadius, radius, "annotated calculation radius");
  assertNear(values.arcLength, radius * radians, "annotated arc length");
  assertNear(values.straightA, 260, "annotated first straight");
  assertNear(values.straightB, 260, "annotated second straight");
  assertNear(values.blankLength, plan.manufacturingParts[0].request.length, "actual blank length");
  for (const [key, center] of [["straightStart", ringCenter(mesh, 0)],
    ["arcStart", arcStart], ["arcEnd", arcEnd], ["straightEnd", end]]) {
    anchors[key].forEach((value, axis) => assertNear(value, center[axis], `${key} axis ${axis}`));
  }
  assertNear(Math.hypot(...anchors.wallOuter.map((value, axis) => value - anchors.wallInner[axis])),
    values.t, "wall thickness anchor span");
  assertNear(Math.hypot(...anchors.arcMid.map((value, axis) => value - anchors.bendCenter[axis])),
    values.calculationRadius, "calculation-radius anchor span");
}

const largerK = planFromTemplate(bendTemplate, 90,
  { bendRadius: 40, bendFactor: 0.8 });
const lowKValues = buildIntegratedFormedPreview({ plan: planFromTemplate(bendTemplate, 90,
  { bendRadius: 40, bendFactor: 0.2 }) }).annotations.values;
const highKValues = buildIntegratedFormedPreview({ plan: largerK }).annotations.values;
assertNear(highKValues.blankLength - lowKValues.blankLength,
  (0.8 - 0.2) * 2 * Math.PI / 2, "K changes blank length");
assertNear(highKValues.arcLength - lowKValues.arcLength,
  (0.8 - 0.2) * 2 * Math.PI / 2, "K changes annotated arc length");
largerK.manufacturingParts[0].request.length += 3;
assertNear(buildIntegratedFormedPreview({ plan: largerK }).annotations.values.blankLength,
  highKValues.blankLength + 3, "blank annotation follows native plan length");
const planar = planFromTemplate(bendTemplate, 90, { bendRadius: 40, bendFactor: 0.5 });
assert.throws(() => makeIntegratedFormedMesh({ ...planar,
  parameters: { ...planar.parameters, bendMethod: "notched" },
  formedPreviewRecipe: null }), /没有可绘制/);
assert.throws(() => makeIntegratedFormedMesh({ ...planar,
  parameters: { ...planar.parameters, bendRadius: 1 },
  formedPreviewRecipe: { ...planar.formedPreviewRecipe, radius: 1 } }), /折弯半径小于管截面内侧/);

console.log(`Integrated formed appearance preview: ${fileURLToPath(import.meta.url)} passed`);
