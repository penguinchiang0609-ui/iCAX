import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {migratePunchRecord} from "../../apps/tube-designer/webpage/punchToolMigration.mjs";

for (const id of ["v-notch-sharp", "edge-arc-groove"]) {
  const descriptor = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/mold/${id}/tool.json`, import.meta.url)));
  const parameters = Object.fromEntries(descriptor.parameters.map(p => [p.key, p.defaultValue]));
  Object.assign(parameters, {bottomReference:"inner", wallThickness:2, reliefDepth:2, reliefSide:"negative",
    bendCompensation:true, useDefaultKFactor:false, kFactor:0.8});
  if (id === "v-notch-sharp") Object.assign(parameters, {flatReference:"actual", bottomStrategy:"rounded"});
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
      const condition = definition.visibleWhen;
      const dependency = descriptor.parameters.find(p => p.key === condition.parameter);
      assert.ok(dependency);
      if (dependency.options) assert.ok(dependency.options.some(o => o.value === condition.value));
    }
  }
}
const old = migratePunchRecord({toolRef:{id:"v-notch-sharp"}, toolParameters:{rootRadius:2, bridge:1}});
assert.equal(old.toolParameters.bottomStrategy, "rounded");
assert.equal(old.toolParameters.roundRadius, 2);
assert.equal(old.toolParameters.leaveBottom, 1);
console.log("Slot parameter migration and descriptor checks passed.");
