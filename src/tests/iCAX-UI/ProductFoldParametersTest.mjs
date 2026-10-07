// Product-owned fold fields use the same public conditions as all editors.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";
import { parameterEnabled, parameterVisible } from "../../apps/tube-designer/webpage/parameterConditions.mjs";
import { productDisplayParameters, manufacturingOnlyParameterKeys } from "../../apps/tube-designer/webpage/productParameterDependencies.mjs";
import { productSceneMemberIds } from "../../apps/tube-designer/webpage/productParameterDiagram.mjs";
import { renderDesignerRightParameterContent } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { applyProductControlChoice, productControlFields, productControlChoices, productControlValue } from "../../apps/tube-designer/webpage/productControls.mjs";

const directory = new URL("../../apps/tube-designer/templates/product/single_face_security_window/", import.meta.url);
const descriptor = JSON.parse(readFileSync(new URL("template.json", directory), "utf8"));
const template = { ...descriptor, available: true, descriptorLoaded: true,
  display: JSON.parse(readFileSync(new URL("display.json", directory), "utf8")),
  groups: descriptor.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
  parameters: descriptor.parameters.map(field => ({ ...field, displayName: catalogText(field.displayName),
    type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
    groupKey: field.group, options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
const definitions = new Map(descriptor.parameters.map(field => [field.key, field]));
const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
const rolePrefixes = ["outerFrame", "doorFrame", "doorLeafFrame"];
const foldFields = rolePrefixes.flatMap(prefix => ["BendKFactor", "FoldBridge", "VGrooveBottomStrategy", "VGrooveRoundRadius", "VGrooveMaleFemale"]
  .map(suffix => definitions.get(prefix + suffix)));
for (const prefix of rolePrefixes) {
  const k = definitions.get(prefix + "BendKFactor"), bridge = definitions.get(prefix + "FoldBridge"),
    male = definitions.get(prefix + "VGrooveMaleFemale");
  assert.equal(k.defaultValue, 0); assert.equal(k.min, 0); assert.equal(k.max, 1);
  assert.equal(bridge.defaultValue, 1); assert.equal(bridge.min, 0.1); assert.equal(bridge.max, 10); assert.equal(bridge.step, 0.1);
  assert.equal(male.defaultValue, true); assert.equal(male.valueType, "boolean");
  const strategy = definitions.get(prefix + "VGrooveBottomStrategy"), radius = definitions.get(prefix + "VGrooveRoundRadius");
  assert.equal(strategy.presentation.visible, false);
  assert.deepEqual(strategy.choices.map(choice => choice.value), ["sharp", "rounded"]);
  assert.equal(radius.defaultValue, 2); assert.equal(radius.min, 0); assert.equal(radius.max, 10000);
}
for (const removed of ["bendKFactor", "vGrooveMaleFemale", "bendKByMaterial", "bendKMaterialOverrides"])
  assert.equal(definitions.has(removed), false, "Only independent current role fields are persisted");
const manufacturing = manufacturingOnlyParameterKeys(descriptor);
for (const field of foldFields) {
  assert.ok(rolePrefixes.some(prefix => field.group === prefix + "_fold_process"));
  assert.ok(manufacturing.has(field.key));
  assert.equal(field.presentation?.visible === false, field.key.endsWith("VGrooveBottomStrategy"));
  assert.equal(field.presentation?.editor, undefined, "Ordinary product fields must not expose a tool editor");
  // Native descriptor validation supports only this current condition grammar.
  function requireNativeGrammar(condition) {
    assert.ok(["eq", "ne", "all", "any", "not"].includes(condition.op), "Unsupported native condition: " + condition.op);
    for (const child of condition.conditions ?? []) requireNativeGrammar(child);
    if (condition.condition) requireNativeGrammar(condition.condition);
  }
  requireNativeGrammar(field.visibleWhen);
  requireNativeGrammar(field.enabledWhen);
}
for (const prefix of rolePrefixes)
  assert.ok(descriptor.extensions.parameterLayout.sections.find(section => section.key === "process").groups.includes(prefix + "_fold_process"));

// This audits declarations only; condition execution remains the production module.
let enumConditions = 0;
function checkDeclaredConditions(value) {
  if (!value || typeof value !== "object") return;
  if (["eq", "ne", "in", "notIn"].includes(value.op) && value.parameter) {
    const field = definitions.get(value.parameter);
    assert.ok(field, `Undeclared condition parameter: ${value.parameter}`);
    if (field.valueType === "enum") {
      const choices = field.choices.map(choice => choice.value);
      for (const choice of ["in", "notIn"].includes(value.op) ? value.values : [value.value])
        assert.ok(choices.includes(choice), `Unavailable enum condition: ${value.parameter}/${choice}`);
      enumConditions++;
    }
  }
  for (const child of Object.values(value)) checkDeclaredConditions(child);
}
checkDeclaredConditions(descriptor);

let states = 0;
for (const face of ["single", "two", "three", "five"])
for (const mode of ["segment_weld", "plane_v_notch", "spatial_v_notch"])
for (const frame of ["four_sides", "left_right", "top_bottom"])
for (const planning of ["builtin_rules", "external_templates"])
for (const opening of [false, true])
for (const fixed of [false, true])
for (const leaf of [false, true])
for (const outerTool of ["v-notch-sharp", "edge-arc-groove"])
for (const fixedTool of ["v-notch-sharp", "edge-arc-groove"])
for (const leafTool of ["v-notch-sharp", "edge-arc-groove"]) {
for (const bottom of ["sharp", "rounded"]) {
  const values = { ...defaults, faceType: face, frameManufacturingMode: mode, frameLayout: frame,
    assemblyPlanningMode: planning, accessDoorEnabled: opening,
    doorFrameJoinType: fixed ? "v_groove_90:tool_library" : "miter_45",
    doorLeafFrameJoinType: leaf ? "v_groove_90:tool_library" : "miter_45",
    outerFrameGrooveTool: `system:${outerTool}`, doorFrameGrooveTool: `system:${fixedTool}`,
    doorLeafFrameGrooveTool: `system:${leafTool}`,
    outerFrameBendKFactor: 0.51, doorFrameBendKFactor: 0.62, doorLeafFrameBendKFactor: 0.73,
    outerFrameFoldBridge: 0.9, doorFrameFoldBridge: 1.1, doorLeafFrameFoldBridge: 1.3,
    outerFrameVGrooveMaleFemale: false, doorFrameVGrooveMaleFemale: true, doorLeafFrameVGrooveMaleFemale: false,
    outerFrameVGrooveBottomStrategy: bottom, doorFrameVGrooveBottomStrategy: bottom, doorLeafFrameVGrooveBottomStrategy: bottom,
    outerFrameVGrooveRoundRadius: 2.1, doorFrameVGrooveRoundRadius: 2.2, doorLeafFrameVGrooveRoundRadius: 2.3 };
  const before = structuredClone(values);
  const builtin = planning === "builtin_rules" || face !== "single";
  const outerActive = (face !== "single" || frame === "four_sides")
    && (mode === "plane_v_notch" || mode === "spatial_v_notch" && ["two", "three"].includes(face));
  const roles = [[outerActive, outerTool], [opening && fixed, fixedTool], [opening && leaf, leafTool]];
  for (const [index, prefix] of rolePrefixes.entries()) {
    const [active, selected] = roles[index];
    for (const [suffix, tool] of [["BendKFactor", "edge-arc-groove"], ["FoldBridge", null], ["VGrooveMaleFemale", "v-notch-sharp"],
      ["VGrooveBottomStrategy", "v-notch-sharp"], ["VGrooveRoundRadius", "v-notch-sharp"]]) {
      const field = definitions.get(prefix + suffix);
      const expected = builtin && active && (!tool || selected === tool) && (suffix !== "VGrooveRoundRadius" || bottom === "rounded");
      assert.equal(parameterVisible(field, values), expected && suffix !== "VGrooveBottomStrategy", `${field.key}/${JSON.stringify(roles)}`);
      assert.equal(parameterEnabled(field, values), expected, field.key);
    }
  }
  assert.deepEqual(productDisplayParameters(descriptor, { ...values, ...Object.fromEntries(foldFields.map(field => [field.key, field.defaultValue])) }),
    productDisplayParameters(descriptor, values), "Fold settings cannot expire the display model");
  assert.deepEqual(values, before, "Conditions must preserve inactive values verbatim");
  states++;
}
}

const members = ["outer_frame.left.0001", "access_door.fixed_frame.left.0001", "access_door.leaf.frame.left.0001", "main_grid.horizontal.0001"]
  .map((stableKey, index) => ({ stableKey, entityId: `member-${index}` }));
for (const [index, prefix] of rolePrefixes.entries()) {
  for (const field of foldFields.filter(field => field.group === prefix + "_fold_process"))
    assert.deepEqual(productSceneMemberIds(descriptor, members, field.key), [`member-${index}`],
      "Each role field links only its own frame, not another frame or infill");
  const values = { ...defaults, faceType: "three", frameManufacturingMode: "plane_v_notch",
    accessDoorEnabled: true, doorFrameJoinType: "v_groove_90:tool_library", doorLeafFrameJoinType: "v_groove_90:tool_library",
    [prefix + "GrooveTool"]: "user:unmapped-tool" };
  for (const field of foldFields.filter(field => field.group === prefix + "_fold_process"))
    assert.equal(parameterVisible(field, values), false, "No guessed mapping for a user tool");
}

for (const [outer, fixed, expected] of [
  ["system:v-notch-sharp", "miter_45", ["outerFrameFoldBridge", "outerFrameVGrooveMaleFemale"]],
  ["system:edge-arc-groove", "miter_45", ["outerFrameBendKFactor", "outerFrameFoldBridge"]],
  ["system:edge-arc-groove", "v_groove_90:tool_library", ["outerFrameBendKFactor", "outerFrameFoldBridge", "doorFrameFoldBridge", "doorFrameVGrooveMaleFemale"]],
]) {
  const parameters = { ...defaults, faceType: "three", accessDoorEnabled: true,
    frameManufacturingMode: "plane_v_notch", outerFrameGrooveTool: outer,
    doorFrameJoinType: fixed, doorLeafFrameJoinType: "miter_45" };
  const product = { entityId: "product", templateId: template.id, parameters, quantity: 1 };
  const html = renderDesignerRightParameterContent({ templates: [template], product, parts: [] }, {
    tubeDesignerParameterPanelProductId: product.entityId, tubeDesignerParameterDisclosureState: { initialized: true },
    tubeDesignerExpandedParameterGroups: ["section:materials", "section:process"],
  }).scrollContent;
  for (const field of foldFields) assert.equal(html.includes(`data-tube-designer-parameter="${field.key}"`),
    expected.includes(field.key), "The actual right pane must honor public conditions");
  for (const prefix of rolePrefixes) assert.equal(html.includes(`data-tube-designer-parameter-group="group:${prefix}_fold_process"`), false,
    "Each part has one group containing its connection and fold rows");
  for (const [key, title] of [["assembly_outer", "外框"], ["assembly_fixed", "逃生窗框"], ["assembly_grid", "格栅"]]) {
    assert.ok(html.includes(`data-tube-designer-parameter-group="scene:process:section-group:${key}"`));
    assert.ok(html.includes(`<summary><span>${title}</span>`));
  }
  assert.ok(html.includes("装配"));
}
console.log(`ProductFoldParametersTest passed: ${states} role/layout/route states, ${enumConditions} enum declarations, real right-pane rows and scene links.`);

const tools = ["v-notch-sharp", "edge-arc-groove"].map(id => {
  const toolDir = new URL("../../apps/tube-designer/templates/mold/" + id + "/", import.meta.url);
  const manifest = readFileSync(new URL("tool.json", toolDir)), descriptor = JSON.parse(manifest);
  const digest = createHash("sha256").update(manifest).update(Buffer.from([0]))
    .update(readFileSync(new URL(descriptor.kind === "programmatic" ? "tool.py" : "geometry.json", toolDir)))
    .update(readFileSync(new URL("../../apps/tube-designer/templates/_shared/section_geometry.py", import.meta.url))).digest("hex");
  return { ...descriptor, libraryScope: "system", digest,
    defaultParameters: Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue])) };
});
for (const faceType of ["single", "two", "three", "five"]) {
  for (const prefix of rolePrefixes) {
    const control = productControlFields(template).find(field => field.key === (prefix === "outerFrame" ? "outerFrameBendType" : prefix + "Connection"));
    let values = { ...defaults, faceType, accessDoorEnabled: true, frameManufacturingMode: "plane_v_notch" };
    if(prefix!=="outerFrame")values=applyProductControlChoice({},template,prefix+"Structure",values,"folded");
    const before = structuredClone(values);
    assert.ok(productControlChoices(template, control, values).some(choice => choice.value === "rounded-v-groove"));
    values = applyProductControlChoice({ tubeDesignerSystemPunchTools: tools }, template, control, values, "rounded-v-groove");
    assert.equal(productControlValue(template, control, values), "rounded-v-groove");
    assert.equal(values[prefix + "VGrooveBottomStrategy"], "rounded");
    assert.equal(values[prefix + "GrooveTool"], "system:v-notch-sharp");
    assert.equal(parameterVisible(definitions.get(prefix + "VGrooveRoundRadius"), values), true);
    for (const other of rolePrefixes.filter(role => role !== prefix))
      for (const suffix of ["BendKFactor", "FoldBridge", "VGrooveBottomStrategy", "VGrooveRoundRadius", "VGrooveMaleFemale"])
        assert.equal(values[other + suffix], before[other + suffix], "A role choice must preserve all other roles");
    assert.deepEqual(productDisplayParameters(template, values), productDisplayParameters(template, before));
    values[prefix + "VGrooveRoundRadius"] = 3.7;
    values = applyProductControlChoice({ tubeDesignerSystemPunchTools: tools }, template, control, values, "v-groove");
    assert.equal(productControlValue(template, control, values), "v-groove");
    assert.equal(values[prefix + "VGrooveBottomStrategy"], "sharp");
    assert.equal(parameterVisible(definitions.get(prefix + "VGrooveRoundRadius"), values), false);
    assert.equal(values[prefix + "VGrooveRoundRadius"], 3.7, "Hidden round-root draft is retained");
    values = applyProductControlChoice({ tubeDesignerSystemPunchTools: tools }, template, control, values, "rounded-v-groove");
    assert.equal(values[prefix + "VGrooveRoundRadius"], 3.7);
  }
}
for(const key of ["outerFrameStructure","outerFrameBendType"])
  assert.deepEqual(productSceneMemberIds(template,members,key),["member-0"],"New two-step controls link the outer frame");
assert.equal(productControlFields(template).some(field=>field.key==="outerFrameFoldLayout"),false);
console.log("ProductFoldParametersTest passed: 12 independent rounded / sharp product-choice transitions, with real tool descriptors.");
