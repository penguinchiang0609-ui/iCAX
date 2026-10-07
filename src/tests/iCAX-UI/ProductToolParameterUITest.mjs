import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { makeProductToolBinding, productToolCandidates, productToolParameterUI, productToolRole } from "../../apps/tube-designer/webpage/productResourceBindings.mjs";
import { parameterVisible, parameterEnabled } from "../../apps/tube-designer/webpage/parameterConditions.mjs";
const read = (path) => JSON.parse(readFileSync(new URL(path, import.meta.url), "utf8"));
const original = read("../../apps/tube-designer/templates/product/single_face_security_window/template.json");
const tool = { ...read("../../apps/tube-designer/templates/mold/v-notch-sharp/tool.json"), libraryScope: "system" };
tool.parameters.find((item) => item.key === "leaveBottom").presentation = { advanced: true };
const field = original.parameters.find((item) => item.key === "outerFrameGrooveTool");
const template = structuredClone(original);
const role = template.extensions.resourceRoles.tools[productToolRole(field)];
role.productParameterUIByResource = { "system:v-notch-sharp": { displayName: { "zh-CN": "产品槽根设置" },
  fields: [{ key: "leaveBottom", displayName: { "zh-CN": "产品余厚" }, order: 1 },
    { key: "reliefLength", order: 2, visibleWhen: { op: "eq", parameter: "bottomStrategy", value: "relief" },
      enabledWhen: { op: "eq", parameter: "reliefShape", value: "circle" } }],
  fixedParameters: { angle: 90, segmentedBend: false } } };
const before = JSON.stringify(tool);
const binding = makeProductToolBinding(field, tool, null, template);
const ui = productToolParameterUI(template, field, binding, tool);
assert.equal(ui.available, true);
assert.equal(ui.displayName, "产品槽根设置");
assert.deepEqual(ui.definitions.map((item) => item.key), ["leaveBottom", "reliefLength"]);
assert.equal(ui.definitions[0].displayName, "产品余厚");
assert.equal(ui.definitions[0].level, "basic");
assert.equal(ui.definitions[0].presentation.advanced, false, "the product controls its own field level");
assert.equal(ui.values.angle, 90);
assert.equal(binding.parameters.segmentedBend, false);
assert.equal(binding.snapshot.parameterDefinitions.length, tool.parameters.length, "binding retains the full real resource schema");
assert.equal(JSON.stringify(tool), before, "the library schema is unchanged");
const relief = ui.definitions[1];
assert.equal(parameterVisible(relief, { ...ui.values, bottomStrategy: "sharp" }), false);
assert.equal(parameterEnabled(relief, { ...ui.values, reliefShape: "rectangle" }), false);
const explicit = makeProductToolBinding(field, tool, { ...binding.parameters, angle: 45 }, template);
assert.equal(explicit.parameters.angle, 45, "an explicit draft is not silently rewritten");
assert.equal(productToolParameterUI(template, field, explicit, tool).fixedConflict, true);
assert.match(productToolParameterUI(template, field, explicit, tool).error, /固定/);
const view = { tubeDesignerSystemPunchTools: [tool, { ...tool, id: "undeclared-tool" }] };
assert.deepEqual(productToolCandidates(view, template, field).map((item) => item.id), [tool.id]);
const policy = role.productParameterUIByResource["system:v-notch-sharp"];
for (const fields of [[{ key: "unknown" }], [{ key: "leaveBottom" }, { key: "leaveBottom" }], [{ key: "angle" }]]) {
  policy.fields = fields;
  assert.equal(productToolParameterUI(template, field, binding, tool).available, false);
  assert.equal(productToolCandidates(view, template, field).length, 0);
}
policy.fields = [];
assert.equal(productToolParameterUI(template, field, binding, tool).available, true, "a declared preset can open no editable parameters");
policy.fixedParameters.angle = "90";
assert.equal(productToolParameterUI(template, field, binding, tool).available, false, "invalid fixed types fail closed");
policy.fixedParameters.angle = 999999;
assert.equal(productToolParameterUI(template, field, binding, tool).available, false, "fixed values use the real resource bounds");
delete role.productParameterUIByResource;
assert.equal(productToolParameterUI(template, field, binding, tool).available, false, "missing policy never opens the whole resource schema");
console.log("Product tool UI: exact resource policy, curated labels/order, shared conditions, fixed new defaults, unchanged saved input/library schema, invalid declarations fail closed.");
