import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createPunchWizardState, getPunchWizardPayload, normalizePunchFeature,
  openPunchParameters, parameterFields, renderPunchWizardDialog, selectPunchTool } from "../../apps/tube-designer/webpage/punchWizard.mjs";

const descriptor = JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/mold/paired-side-slots/tool.json", import.meta.url), "utf8"));
const hidden = descriptor.parameters.find(field => field.key === "allowSideOpening");
assert.equal(hidden.presentation.visible, false);
const tool = { ...descriptor, digest: "hidden-parameter-fixture",
  defaultParameters: Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue])) };
const part = { entityId: "hidden-parameters", name: "侧壁母槽", length: 1000 };
const state = createPunchWizardState(part);
Object.assign(state, { tools: [tool], catalogueStatus: "ready" });
const feature = normalizePunchFeature({ station: 300, recordKind: "tool", layoutDatum: "base" });
selectPunchTool(state, feature, tool.id);
state.features = [feature];
const view = { pending: false, tubeDesignerPunchWizard: state };
assert.equal(openPunchParameters(view, "0"), true);
const renders = () => [parameterFields(name => "test-" + name, tool, feature),
  renderPunchWizardDialog(part, view, { tableMode: true })];
for (const value of [false, true]) {
  feature.toolParameters.allowSideOpening = value;
  for (const html of renders()) {
    assert.doesNotMatch(html, /data-tube-designer-punch-parameter="allowSideOpening"/);
    assert.doesNotMatch(html, /承接侧缘加工许可/);
    assert.match(html, /data-tube-designer-punch-parameter="tabWidth"/);
  }
  assert.equal(getPunchWizardPayload(view).features[0].toolParameters.allowSideOpening, value,
    "Hiding an internal parameter does not discard the manufacturing recipe value");
}

// Both entry points share the complete current condition contract, including
// compound conditions; this cannot drift into another local interpreter.
const condition = { op: "all", conditions: [{ parameter: "pairCount", op: "eq", value: 4 },
  { not: { parameter: "allowEndOpening", op: "eq", value: true } }] };
tool.parameters.push({ key: "conditionalFixture", displayName: "条件参数", valueType: "number",
  defaultValue: 1, visibleWhen: condition });
for (const [pairCount, allowEndOpening, expected] of [[2, false, false], [4, false, true], [4, true, false]]) {
  Object.assign(feature.toolParameters, { pairCount, allowEndOpening });
  for (const html of renders()) {
    assert.equal(html.includes('data-tube-designer-punch-parameter="conditionalFixture"'), expected);
  }
}
assert.equal(feature.toolParameters.allowSideOpening, true);
console.log("Punch hidden manufacturing parameters and shared compound visibility passed for both editing entry points.");
