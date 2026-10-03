import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { assemblyProcessWithInput, createFinishedProduct, finishedProductKey,
  finishedProductShape } from "../../apps/tube-designer/webpage/finishedProductModel.mjs";
import { assemblyLibraryState, renderAssemblyLibraryLeftPane,
  renderAssemblyLibraryRightPane } from "../../apps/tube-designer/webpage/assemblyLibrary.mjs";
import { assemblyShapeOrder } from "../../apps/tube-designer/webpage/assemblyCatalog.mjs";
import { parameterVisible } from "../../apps/tube-designer/webpage/parameterConditions.mjs";
import { renderAssemblyCatalogueIllustration } from "../../apps/tube-designer/webpage/assemblyCatalogueIllustration.mjs";
import generatedShapes from "../../apps/tube-designer/webpage/finishedProductShapes.generated.mjs";

const sourceShapes = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/finished-product/shapes.json", import.meta.url), "utf8"));
assert.deepEqual(generatedShapes, sourceShapes, "the browser bundle must match the standalone product catalogue");
const shape = finishedProductShape("orthogonal-corner");
assert.equal(shape.layoutShape, "orthogonal-corner");
assert.deepEqual(shape.parameters, [], "the fixed orthogonal shape must not own assembly process settings");
assert.deepEqual(shape.spans.map((span) => [span.id, span.pose.origin, span.pose.direction, span.pose.up]), [
  ["armA", ["-$armA_length", 0, 0], [1, 0, 0], [0, 1, 0]],
  ["armB", [0, 0, 0], [0, 0, 1], [0, 1, 0]],
  ["armC", [0, 0, 0], [0, 1, 0], [0, 0, -1]],
]);
const product = createFinishedProduct(shape.id);
assert.deepEqual(Object.keys(product.spans), ["armA", "armB", "armC"]);
assert.deepEqual(product.parameters, {});
assert.ok(Object.values(product.spans).every((span) =>
  Object.keys(span).sort().join() === "length,parameters,profileRef"));
const secondProduct = createFinishedProduct(shape.id);
product.spans.armC.length = 321;
product.spans.armC.parameters.width = 22;
assert.equal(secondProduct.spans.armC.length, 220);
assert.equal(secondProduct.spans.armC.parameters.width, 20);

const stored = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/orthogonal-corner/assembly.json", import.meta.url), "utf8"));
const before = structuredClone(stored);
const adapted = assemblyProcessWithInput(stored);
assert.deepEqual(stored, before, "display adaptation cannot add finished geometry to the stored template");
assert.deepEqual(adapted.previewScene.designParts.map((part) => [part.role, part.sceneSlot, part.pose]), [
  ["memberA", "armA", { origin: ["-$memberA_length", 0, 0], direction: [1, 0, 0], up: [0, 1, 0] }],
  ["memberB", "armB", { origin: [0, 0, 0], direction: [0, 0, 1], up: [0, 1, 0] }],
  ["memberC", "armC", { origin: [0, 0, 0], direction: [0, 1, 0], up: [0, 0, -1] }],
]);
assert.deepEqual(assemblyShapeOrder([adapted]), [["orthogonal-corner", "三向直角节点"]]);
const view = { tubeDesignerAssemblyTemplates: [stored], tubeDesignerAssemblyLibrary: {
  selectedId: stored.id, workMode: "example", workModeUserSelected: true,
} };
assert.match(renderAssemblyLibraryLeftPane({}, view), /data-tube-assembly-shape="orthogonal-corner"/);
const defaultHtml = renderAssemblyLibraryRightPane({}, view);
assert.deepEqual([...defaultHtml.matchAll(/<details[^>]*data-finished-span="([^"]+)"/g)]
  .map((match) => match[1]), ["armA", "armB", "armC"]);
assert.match(renderAssemblyCatalogueIllustration(stored), /class="tube-assembly-catalogue-c"/);
for (const span of shape.spans) assert.ok(defaultHtml.includes(span.label));
const defaults = Object.fromEntries(stored.parameters.map((parameter) => [parameter.key, parameter.defaultValue]));
for (const cJoint of ["weld", "insert", "tabs"]) {
  const values = { ...defaults, cJoint };
  assemblyLibraryState(view).parameterDrafts[stored.id] = values;
  const html = renderAssemblyLibraryRightPane({}, view);
  for (const parameter of stored.parameters) {
    const selector = `data-tube-assembly-parameter="${parameter.key}"`;
    assert.equal(html.includes(selector), parameterVisible(parameter, values), `${cJoint}/${parameter.key}`);
    if (parameterVisible(parameter, values) && parameter.valueType === "number")
      assert.ok(html.includes(`min="${parameter.min}" max="${parameter.max}" step="${parameter.step ?? 0.1}"`), parameter.key);
  }
}
const renamed = structuredClone(stored);
renamed.id = "declaration-driven-scene-fixture";
const renamedView = { tubeDesignerAssemblyTemplates: [renamed], tubeDesignerAssemblyLibrary: {
  selectedId: renamed.id, workMode: "example", workModeUserSelected: true,
  parameterDrafts: { [renamed.id]: { ...defaults, cJoint: "tabs" } },
} };
const renamedHtml = renderAssemblyLibraryRightPane({}, renamedView);
assert.equal(renamedHtml.replaceAll(renamed.id, stored.id), renderAssemblyLibraryRightPane({}, view),
  "rendering the same descriptor under another ID must preserve every parameter and scene card");
assert.equal(finishedProductKey(secondProduct), finishedProductKey(createFinishedProduct(shape.id)),
  "process rendering cannot change the independent product draft");
console.log("Orthogonal corner scene passed: independent three-span shape, generic role/pose mapping, tree and declaration-driven fields.");
