// Real scene SDO regression for continuous spline kerfs, including their
// editable Bezier parallels. No browser or simulated native proxy is involved.
import assert from "node:assert/strict";
import { mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { repositoryRoot, startSideSketchNativeBridge } from "./fixtures/sideSketchNativeBridge.mjs";
import { offsetSketchEntity } from "../../apps/tube-designer/webpage/sketchCadGeometry.mjs";
import { curvePoint, editableSegments, distance } from "../../apps/tube-designer/webpage/sketchGeometry.mjs";

const artifacts = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repositoryRoot, "output/tests/sketch-cad-kerf-native"));
mkdirSync(artifacts, { recursive: true });
const bridge = await startSideSketchNativeBridge(artifacts), results = [];
const rectangle = (width, height) => {
  const points = [[-width / 2, -height / 2], [width / 2, -height / 2], [width / 2, height / 2], [-width / 2, height / 2]];
  return { kind: "path", closed: true, segments: points.map((start, i) => ({ kind: "line", start, end: points[(i + 1) % 4] })) };
};
const profile = { schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "fixed-section", profileForm: "fixed",
  name: "连续样条切缝原生回归", width: 80, depth: 60, contours: [rectangle(80, 60), rectangle(76, 56)] };
const blankVolume = (80 * 60 - 76 * 56) * 200;
const samples = [
  [[20, 60], [30, 85], [45, 65], [55, 100]],
  [[20, 70], [24, 75], [28, 71], [32, 77]],
  [[20, 70], [30, 72], [40, 73], [50, 75]],
];
const lengthOf = entity => editableSegments(entity).reduce((sum, s) => {
  let length = 0, previous = curvePoint(s, 0);
  // A separate fine chord integration checks actual removed kerf area against
  // the saved editable curve, including zero-speed endpoint parameters.
  for (let i = 1; i <= 8192; i++) { const p = curvePoint(s, i / 8192); length += distance(previous, p); previous = p; }
  return sum + length;
}, 0);

try {
  for (const [index, points] of samples.entries()) {
    const source = { id: `spline-${index}`, kind: "spline", points, closed: false };
    const parallel = { ...offsetSketchEntity(source, 1).parts[0], id: `parallel-${index}` };
    for (const [name, entities] of [["source", [source]], ["parallel", [parallel]], ["both", [source, parallel]]]) {
      const id = `continuous-spline-${index}-${name}`, snapshot = structuredClone(entities);
      const sketch = { schema: "icax.tube-sketch", schemaVersion: 1, kind: "side", unit: "mm", length: 200,
        faceHeight: 280, perimeter: 280, coordinateSpace: "arc-length-axial", trajectoryWidth: .8, entities };
      const preview = await bridge.invoke("PreviewNestingSideSketchPart", { profile, length: 200, quantity: 1, previewResourceKey: id, sketch });
      assert.equal(preview.available, true, `${id} must generate its actual full-width preview`);
      const added = await bridge.invoke("AddNestingSideSketchPart", { profile, length: 200, quantity: 1, name: id, sketch });
      assert.ok(added.partEntityId);
      const solid = await bridge.rpc({ action: "geometry", payload: { partEntityId: added.partEntityId } });
      assert.equal(solid.valid, true, `${id} must produce a valid BRep`);
      const removed = blankVolume - solid.volume, expected = 2 * .8 * entities.reduce((sum, e) => sum + lengthOf(e), 0);
      assert.ok(Math.abs(removed - expected) < .03, `${id}: removed ${removed} vs expected ${expected}`);
      const part = added.tubeDesigner.nestingGroups.flatMap(group => group.parts).find(p => p.entityId === added.partEntityId);
      assert.deepEqual(part.properties["tubeDesigner.sideSketch"].entities, snapshot, `${id} keeps every original spline/Bezier control on persistence`);
      assert.deepEqual(entities, snapshot);
      results.push({ id, partEntityId: added.partEntityId, valid: solid.valid, actualRemovedVolume: removed, expectedRemovedVolume: expected });
      console.log(`native continuous spline kerf: ${id} passed`);
    }
  }
  writeFileSync(resolve(artifacts, "report.json"), JSON.stringify({ dllDirectory: bridge.dllDirectory, dllHash: bridge.dllHash,
    cases: results, coverage: "Real scene SDO preview, AddNestingSideSketchPart, native BRep validity and kerf area/volume, and editable spline/Bezier parameter persistence. Browser, OS file chooser and installed CEF are outside this direct-native test." }, null, 2));
} finally { await bridge.close(); }
