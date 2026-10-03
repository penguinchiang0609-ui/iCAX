import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { parameterVisible, matchesParameterCondition, availableParameterChoices, effectiveParameterChoice } from "../../apps/tube-designer/webpage/parameterConditions.mjs";

const descriptor = JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/product/single_face_security_window/template.json", import.meta.url), "utf8"));
const fields = new Map(descriptor.parameters.map(field => [field.key, field]));
const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
const children = [...fields.values()].filter(field => field.key.startsWith("foldedPost"));
let checks = 0;
let cases = 0;
function verify(values) {
  // Multi-face products always use their built-in route; hidden planning and layout drafts are preserved.
  const multi = values.faceType !== "single";
  const active = multi
    ? ["segment_weld", "plane_v_notch"].includes(values.frameManufacturingMode)
      || ["two", "three"].includes(values.faceType) && values.frameManufacturingMode === "spatial_v_notch"
    : values.frameLayout === "four_sides" && values.assemblyPlanningMode === "builtin_rules"
      && values.frameManufacturingMode === "segment_weld";
  const before = JSON.stringify(values);
  for (const field of children) {
    const expected = active && (field.key === "foldedPostJoint"
      || field.group === "folded_post_profile" && values.foldedPostJoint !== "weld"
      || ["foldedPostInsertDepth", "foldedPostFitGap"].includes(field.key) && values.foldedPostJoint === "insert"
      || ["foldedPostTabWidth", "foldedPostTabLength", "foldedPostSideClearance"].includes(field.key) && values.foldedPostJoint === "tabs");
    assert.equal(parameterVisible(field, values), expected, `${field.key}: ${JSON.stringify(values)}`);
    assert.equal(matchesParameterCondition(field.enabledWhen, values), expected, `${field.key} enabled`);
    checks += 2;
  }
  const weldSingle = !multi && values.frameLayout === "four_sides"
    && values.assemblyPlanningMode === "builtin_rules" && values.frameManufacturingMode === "segment_weld"
    && values.foldedPostJoint === "weld";
  for (const [key, expected] of [
    ["frameJoinType", weldSingle],
    ["frameButtWrapMode", weldSingle && values.frameJoinType === "butt_90"],
  ]) {
    assert.equal(parameterVisible(fields.get(key), values), expected, `${key} visible: ${JSON.stringify(values)}`);
    assert.equal(matchesParameterCondition(fields.get(key).enabledWhen, values), expected, `${key} enabled`);
    checks += 2;
  }
  const cornerVisible = multi && values.frameManufacturingMode === "segment_weld";
  assert.equal(parameterVisible(fields.get("frameCornerJoin"), values), cornerVisible, "multi-face weld layout visibility");
  assert.equal(matchesParameterCondition(fields.get("frameCornerJoin").enabledWhen, values), cornerVisible && values.foldedPostJoint === "weld", "insert/tabs retain but disable the weld layout draft");
  checks += 2;
  assert.equal(JSON.stringify(values), before, "condition evaluation preserves all process and profile drafts");
  checks++;
  cases++;
}
const dimensions = ["faceType", "frameLayout", "frameManufacturingMode", "assemblyPlanningMode", "foldedPostJoint", "frameCornerJoin", "frameJoinType", "frameButtWrapMode"];
function combinations(index, values) {
  if (index === dimensions.length) return verify(values);
  const key = dimensions[index];
  for (const choice of fields.get(key).choices) combinations(index + 1, { ...values, [key]: choice.value });
}
combinations(0, defaults);
function validateChoiceReferences(condition) {
  if (!condition) return;
  for (const child of condition.conditions ?? []) validateChoiceReferences(child);
  const choices = fields.get(condition.parameter)?.choices;
  if (choices && ["eq", "ne"].includes(condition.op)) {
    assert.ok(choices.some(choice => Object.is(choice.value, condition.value)), `condition uses undeclared choice ${condition.parameter}/${condition.value}`);
  }
}
for (const field of [...children, ...["frameJoinType", "frameButtWrapMode", "frameCornerJoin"].map(key => fields.get(key))]) {
  validateChoiceReferences(field.visibleWhen);
  validateChoiceReferences(field.enabledWhen);
}
assert.equal(descriptor.extensions.resourceRoles.profiles.foldedPost.parameter, "foldedPostProfileType");
assert.deepEqual(fields.get("foldedPostProfileType").presentation.profileConstraints, { sectionKinds: ["rect"], hollow: true });
assert.deepEqual(descriptor.extensions.productDiagram.profileRoles.foldedPost.memberStableKeyPrefixes, ["outer_frame.vertical.", "outer_frame.left.", "outer_frame.right."]);
for (const field of children.filter(field => field.group === "folded_post_process")) {
  assert.deepEqual(descriptor.extensions.productDiagram.sceneBindings[field.key].memberStableKeyPrefixes, ["outer_frame.vertical.", "outer_frame.left.", "outer_frame.right."]);
}
assert.equal(children.length, 11);
const modeField = fields.get("frameManufacturingMode");
for (const faceType of ["single", "two", "three", "five"]) {
  const values = { ...defaults, faceType, frameManufacturingMode: "spatial_v_notch", foldedPostJoint: "tabs" };
  const spatialAvailable = ["two", "three"].includes(faceType);
  assert.equal(availableParameterChoices(modeField, values).some(choice => choice.value === "spatial_v_notch"), spatialAvailable);
  assert.equal(effectiveParameterChoice(modeField, values.frameManufacturingMode, values), spatialAvailable ? "spatial_v_notch" : "plane_v_notch");
  checks += 2;
}
console.log(`Post connection parameter conditions: ${checks} checks across ${cases} combinations passed using parameterConditions.mjs.`);
