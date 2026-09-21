import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { parameterAutoFillPatch } from "../../apps/tube-designer/webpage/parameterAutoFill.mjs";

const read = id => JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/mold/${id}/tool.json`, import.meta.url), "utf8"));

for (const id of ["v-notch-sharp", "embedded-arc-notch", "edge-arc-groove"]) {
  const descriptor = read(id);
  const definition = descriptor.parameters.find(item => item.key === "maleFemaleSize");
  assert.ok(definition?.autoFill, `${id} must declare its automatic公母尺寸 source`);
  assert.equal(definition.displayName, "公母尺寸（mm）");

  const defaults = Object.fromEntries(descriptor.parameters.map(item => [item.key, item.defaultValue]));
  let patch = parameterAutoFillPatch(descriptor.parameters, defaults, "maleFemale", true, {
    wallThickness: 1.9999999999999998,
  });
  assert.deepEqual(patch, { maleFemale: true, maleFemaleSize: 2 });

  patch = parameterAutoFillPatch(descriptor.parameters, { ...defaults, maleFemaleSize: 3 }, "maleFemale", true, {
    wallThickness: 2,
  });
  assert.deepEqual(patch, { maleFemale: true }, "a positive manual公母尺寸 must survive re-enabling");

  patch = parameterAutoFillPatch(descriptor.parameters, defaults, "maleFemale", false, { wallThickness: 2 });
  assert.deepEqual(patch, { maleFemale: false });

  patch = parameterAutoFillPatch(descriptor.parameters, defaults, "maleFemale", true, { wallThickness: 0 });
  assert.deepEqual(patch, { maleFemale: true }, "unknown wall thickness must not fabricate a value");
}

console.log("Descriptor-driven公母尺寸 auto-fill passed for V槽, 嵌入圆弧槽 and 边弧槽.");
