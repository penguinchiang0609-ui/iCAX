import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {migratePunchRecord} from "../../apps/tube-designer/webpage/punchToolMigration.mjs";

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
  const migrated = migratePunchRecord(source);
  assert.deepEqual(migrated.toolParameters, parameters, `${id}: new process fields must survive editing`);
  assert.deepEqual(migrated.toolRef, source.toolRef);
  assert.deepEqual(migrated.frozenTool, source.frozenTool);
  assert.deepEqual(migratePunchRecord(migrated), migrated);
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
console.log("Current slot parameters and descriptor conditions passed.");
