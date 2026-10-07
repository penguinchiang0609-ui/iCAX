import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {clonePunchRecord} from "../../apps/tube-designer/webpage/punchRecipe.mjs";
import { createPunchWizardState, installPunchCatalogue, normalizePunchFeature,
  validatePunchWizard, validatePunchFeature, removeSelectedPunchWizardFeatures } from "../../apps/tube-designer/webpage/punchWizard.mjs";

function conditionLeaves(condition) {
  if (!condition || typeof condition !== "object") return [];
  if (Array.isArray(condition.conditions)) return condition.conditions.flatMap(conditionLeaves);
  if (condition.condition) return conditionLeaves(condition.condition);
  return condition.parameter ? [condition] : [];
}

for (const id of [
  "v-notch-sharp", "edge-arc-groove", "embedded-arc-notch",
  "segmented-bend", "flexible-slit-bend",
]) {
  const descriptor = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/mold/${id}/tool.json`, import.meta.url)));
  const parameters = Object.fromEntries(descriptor.parameters.map(p => [p.key, p.defaultValue]));
  Object.assign(parameters, {bottomReference:"inner", wallThickness:2, reliefDepth:2, reliefSide:"negative",
    bendCompensation:true, kFactor:0.8});
  if (id === "v-notch-sharp") Object.assign(parameters, {bottomStrategy:"rounded"});
  else parameters.reliefDiameter = 1;
  const source = {toolRef:{id, version:descriptor.version, digest:"test"}, toolParameters:parameters,
    station:500, rotation:30, frozenTool:{immutable:true}};
  const migrated = clonePunchRecord(source);
  assert.deepEqual(migrated.toolParameters, parameters, `${id}: new process fields must survive editing`);
  assert.deepEqual(migrated.toolRef, source.toolRef);
  assert.deepEqual(migrated.frozenTool, source.frozenTool);
  assert.deepEqual(clonePunchRecord(migrated), migrated);
  for (const definition of descriptor.parameters) {
    if (definition.visibleWhen) {
      for (const condition of conditionLeaves(definition.visibleWhen)) {
        const dependency = descriptor.parameters.find(p => p.key === condition.parameter);
        assert.ok(dependency, `${id}.${definition.key}: missing ${condition.parameter}`);
        if (dependency.options && condition.op === "eq") {
          assert.ok(dependency.options.some(o => o.value === condition.value));
        }
      }
    }
  }
}
const part = { entityId: "catalogue-records", length: 500, properties: {
  "tubeDesigner.punchWizard": { features: [{ id: "old-hole", type: "circle", diameter: 24 }],
    ends: { start: { type: "miter", angle: 30 }, end: { type: "keep" } } },
} };
const state = createPunchWizardState(part);
const savedFeatures = structuredClone(state.features), savedEnds = structuredClone(state.ends);
const circle = { id: "circle", version: "1.0", digest: "circle-test", target: "side",
  parameters: [{ key: "diameter", valueType: "number", defaultValue: 12 }], defaultParameters: { diameter: 12 } };
const end = { id: "end-miter", version: "1.0", target: "end", parameters: [] };
installPunchCatalogue(state, { tools: [circle, end] });
assert.equal(state.draft.toolRef.id, "circle", "Only the newly created transient draft binds on async catalogue arrival.");
assert.equal(state.draft.toolParameters.diameter, 12);
assert.equal(state.draftToolInitializationId, undefined);
assert.deepEqual(state.features, savedFeatures, "Saved unreferenced dimensions must not migrate into a current tool.");
assert.deepEqual(state.ends, savedEnds, "Saved unreferenced end types must not migrate into current tools.");
const view = { pending: false, tubeDesignerPunchWizard: state };
assert.match(validatePunchWizard(view, part), /请选择刀具模板/);
assert.match(validatePunchFeature(part, { ...state.features[0], enabled: false }), /请选择刀具模板/);
state.features = [];
assert.match(validatePunchWizard(view, part), /请选择端部刀具模板/);
state.ends.start = { type: "keep" };
const current = normalizePunchFeature({ id: "current-hole", toolRef: { id: "circle", version: "1.0", digest: "circle-test" },
  toolParameters: { diameter: 16 }, station: 100, arrayCount: 2, arrayPitch: 50, endDatum: "short" });
state.features = [current]; state.draft = structuredClone(current); state.editingId = current.id;
state.selectedFeatureId = current.id; state.selectedFeatureIds = [current.id];
assert.equal(removeSelectedPunchWizardFeatures(view), true);
assert.equal(state.features.length, 0);
assert.equal(state.draft.toolRef.id, "circle", "Removing the edited row creates and binds a fresh draft with the ready catalogue.");
assert.equal(state.draft.toolParameters.diameter, 12);
assert.equal(state.draftToolInitializationId, undefined);
const awaiting = createPunchWizardState({ length: 500 });
const awaitingView = { pending: false, tubeDesignerPunchWizard: awaiting };
awaiting.features = [structuredClone(current)]; awaiting.editingId = current.id; awaiting.selectedFeatureIds = [current.id];
assert.equal(removeSelectedPunchWizardFeatures(awaitingView), true);
assert.equal(awaiting.draft.toolRef, undefined);
installPunchCatalogue(awaiting, { tools: [circle] });
assert.equal(awaiting.draft.toolRef.id, "circle", "A fresh draft created before catalogue arrival also binds later.");
assert.equal(current.endDatum, "short");
assert.equal(current.arrayCount, 2);
assert.equal(current.arrayPitch, 50);
console.log("Current slot parameters/conditions, explicit saved tool references and new-draft async binding passed.");
