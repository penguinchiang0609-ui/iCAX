// Equal-section outer-frame posts have only welding and tabs in the current product contract.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  applyProductControlChoice, productControlChoices, productControlFields, productControlValue,
} from "../../apps/tube-designer/webpage/productControls.mjs";
import {
  effectiveParameterChoice, matchesParameterCondition, parameterEnabled, parameterVisible,
} from "../../apps/tube-designer/webpage/parameterConditions.mjs";
import {
  manufacturingOnlyParameterKeys, productDisplayParameters,
} from "../../apps/tube-designer/webpage/productParameterDependencies.mjs";

const template = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/template.json", import.meta.url), "utf8"));
const parameters = new Map(template.parameters.map(field => [field.key, field]));
const controls = productControlFields(template);
const post = parameters.get("foldedPostJoint");
const tabFields = ["foldedPostTabWidth", "foldedPostTabLength", "foldedPostSideClearance"]
  .map(key => parameters.get(key));
const defaults = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
const manufacturingKeys = manufacturingOnlyParameterKeys(template);
assert.ok(post && tabFields.every(Boolean));
assert.deepEqual(post.choices.map(choice => choice.value), ["weld", "tabs"]);
assert.equal(post.defaultValue, "weld");
assert.equal(post.presentation?.unavailableChoiceFallback, undefined,
  "The current post selector must not map a removed whole-tube insertion choice to another method");
assert.equal(post.presentation?.choiceConditions, undefined);
for (const key of ["foldedPostInsertDepth", "foldedPostFitGap"]) {
  assert.equal(parameters.has(key), false, `Removed host field ${key} must be absent`);
  assert.equal(JSON.stringify(template).includes(key), false,
    `Removed host field ${key} must have no dependency, diagram, highlight or other metadata references`);
}
for (const field of [post, ...tabFields]) assert.ok(manufacturingKeys.has(field.key), field.key);

// Inspect condition declarations; evaluate all runtime conditions with the shared production module.
let checkedEnumConditions = 0;
function checkConditions(value, path = "template") {
  if (Array.isArray(value)) {
    value.forEach((item, index) => checkConditions(item, `${path}[${index}]`));
    return;
  }
  if (!value || typeof value !== "object") return;
  if (["eq", "ne", "in", "notIn"].includes(value.op) && typeof value.parameter === "string") {
    const field = parameters.get(value.parameter);
    assert.ok(field, `${path} references an undeclared parameter ${value.parameter}`);
    if (field.valueType === "enum") {
      const choices = (field.choices ?? field.options ?? [])
        .map(choice => typeof choice === "object" ? choice.value : choice);
      const compared = ["in", "notIn"].includes(value.op) ? value.values : [value.value];
      assert.ok(Array.isArray(compared), `${path} must declare an array of comparison values`);
      for (const option of compared) assert.ok(choices.includes(option),
        `${path}: ${value.parameter}/${value.op} references unavailable enum value ${String(option)}`);
      checkedEnumConditions++;
    }
  }
  for (const [key, item] of Object.entries(value)) checkConditions(item, `${path}.${key}`);
}
checkConditions(template);
assert.ok(checkedEnumConditions > 100, "The recursive audit must cover the complete current descriptor");

const routes = [
  { faceType: "single", modes: ["segment_weld"], postVisible: false },
  { faceType: "two", modes: ["segment_weld", "plane_v_notch", "spatial_v_notch"], postVisible: true },
  { faceType: "three", modes: ["segment_weld", "plane_v_notch", "spatial_v_notch"], postVisible: true },
  { faceType: "five", modes: ["segment_weld", "plane_v_notch"], postVisible: true },
];
let testedPostStates = 0, testedBranchSelections = 0;
for (const { faceType, modes, postVisible } of routes) {
  for (const mode of ["segment_weld", "plane_v_notch", "spatial_v_notch"]) {
    const applicable = modes.includes(mode);
    for (const joint of ["weld", "tabs"]) {
      const values = { ...defaults, faceType, frameManufacturingMode: mode, foldedPostJoint: joint,
        assemblyPlanningMode: "builtin_rules", frameLayout: "four_sides", infillPattern: "grid",
        foldedPostTabWidth: 11.5, foldedPostTabLength: 15.5, foldedPostSideClearance: 0.35 };
      const before = structuredClone(values);
      assert.equal(parameterVisible(post, values), applicable && postVisible,
        `${faceType}/${mode}/${joint}: post selector visibility`);
      assert.equal(parameterEnabled(post, values), applicable,
        `${faceType}/${mode}/${joint}: post selector availability`);
      assert.equal(effectiveParameterChoice(post, joint, values), joint);
      for (const field of tabFields) {
        assert.equal(field.presentation.visible, false, `${field.key}: internal tab dimensions remain hidden`);
        assert.equal(parameterVisible(field, values), false, `${field.key}: no exposed resource-level editor`);
        assert.equal(matchesParameterCondition(field.visibleWhen, values), applicable && joint === "tabs",
          `${faceType}/${mode}/${joint}: ${field.key} is applicable only to tab joints`);
        assert.equal(parameterEnabled(field, values), applicable && joint === "tabs",
          `${faceType}/${mode}/${joint}: ${field.key} is enabled only to tab joints`);
      }
      const changed = { ...values, foldedPostJoint: joint === "weld" ? "tabs" : "weld" };
      assert.deepEqual(productDisplayParameters(template, changed), productDisplayParameters(template, values),
        `${faceType}/${mode}: changing post method cannot expire display geometry`);
      for (const field of tabFields) assert.equal(changed[field.key], values[field.key],
        `${field.key}: switching post method must retain the inactive draft`);
      assert.deepEqual(values, before, "Visibility and display projection must not mutate the parameter draft");
      assert.equal(effectiveParameterChoice(post, "insert", values), "insert",
        "Condition evaluation must not silently map an invalid removed choice into the current contract");
      testedPostStates++;

      for (const direction of ["horizontal", "vertical"]) {
        const key = `${direction}EndConnection`, control = controls.find(field => field.key === `${key}Choice`);
        assert.ok(control);
        assert.deepEqual(parameters.get(key).choices.map(choice => choice.value), ["insert", "weld", "tabs"]);
        assert.equal(parameters.get(key).defaultValue, "insert");
        assert.equal(parameterVisible(control, values), true);
        assert.equal(productControlChoices(template, control, values).find(choice => choice.value === "insert").disabled, false);
        const branchDraft = { ...values, [key]: "weld" }, frozen = structuredClone(branchDraft);
        const inserted = applyProductControlChoice({}, template, control, branchDraft, "insert");
        assert.equal(productControlValue(template, control, inserted), "insert");
        assert.deepEqual(inserted, { ...branchDraft, [key]: "insert" },
          `${direction}: branch insertion must remain independent of the equal-section post choice`);
        assert.deepEqual(branchDraft, frozen, "Selecting branch insertion must retain the previous draft");
        assert.deepEqual(productDisplayParameters(template, inserted), productDisplayParameters(template, branchDraft));
        testedBranchSelections++;
      }
    }
  }
}

for (const patch of [
  { frameLayout: "left_right" }, { frameLayout: "top_bottom" },
  { assemblyPlanningMode: "external_templates", accessDoorEnabled: false, infillPattern: "vertical" },
]) {
  const values = { ...defaults, ...patch, faceType: "single", foldedPostJoint: "tabs" };
  assert.equal(parameterVisible(post, values), false);
  assert.equal(parameterEnabled(post, values), false);
  for (const field of tabFields) assert.equal(parameterEnabled(field, values), false);
}

// The single-face product applies the same hidden post parameters through its external frame choice.
const outer = controls.find(field => field.key === "outerFrameConnection");
let values = { ...defaults, foldedPostTabWidth: 11.5, foldedPostTabLength: 15.5, foldedPostSideClearance: 0.35 };
const display = productDisplayParameters(template, values);
for (const choice of ["tabs", "miter", "tabs"]) {
  const before = structuredClone(values);
  values = applyProductControlChoice({}, template, outer, values, choice);
  assert.equal(productControlValue(template, outer, values), choice);
  assert.equal(values.foldedPostJoint, choice === "tabs" ? "tabs" : "weld");
  for (const field of tabFields) assert.equal(values[field.key], before[field.key]);
  assert.deepEqual(productDisplayParameters(template, values), display);
  assert.equal(Object.hasOwn(values, outer.key), false);
}

console.log(`EqualSectionPostChoicesTest passed: ${testedPostStates} post states, ${testedBranchSelections} branch insertions, ${checkedEnumConditions} enum conditions.`);
