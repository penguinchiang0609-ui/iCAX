import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  buildCatalogEntries, buildTemplateGroupTree, catalogText, getCatalogEntry, getCatalogEntryId,
  getCatalogParameters, renderGuardrailSchematic,
} from "../../apps/tube-designer/webpage/productCatalog.mjs";
import { renderDesignerAddDialog, renderDesignerAddParameterContent } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { handleDesignerAreaAction } from "../../apps/tube-designer/webpage/designerActions.mjs";

const completed = [];
async function test(name, run) { await run(); completed.push(name); }
// The host sends CTemplateCodec::MakePresentationDescriptor, not raw JSON.
function presentationDescriptor(raw) {
  const groups = (raw.groups ?? []).map((group) => ({ ...group, displayName: catalogText(group.displayName) }));
  return {
    ...raw, available: true, name: catalogText(raw.displayName), groups,
    parameters: raw.parameters.map((field) => ({
      ...field, displayName: catalogText(field.displayName),
      type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
      groupKey: field.group, group: groups.find((group) => group.key === field.group)?.displayName ?? field.group,
      min: field.constraints?.minimum, max: field.constraints?.maximum, step: field.constraints?.step,
      options: field.choices?.map((choice) => ({ ...choice, label: catalogText(choice.displayName), displayName: catalogText(choice.displayName) })),
    })),
  };
}
const fields = {
  layout: "straight", railCount: 3, cornerPostMode: "shared", largePostMode: "none",
  sideLength1: 3000, sideLength2: 1500, sideLength3: 1500, guardHeight: 1200,
};
const presets = [
  { id: "r2-straight", displayName: { "zh-CN": "二横档直式" }, parameters: { railCount: 2 } },
  { id: "r3-left", displayName: { "zh-CN": "三横档左转 L 型" }, parameters: { railCount: 3, layout: "left_l" } },
  { id: "r3-right-double", displayName: "三横档右转双立柱", parameters: { railCount: 3, layout: "right_l", cornerPostMode: "double" } },
];
const guardrail = {
  id: "modular-guardrail", available: true, version: "1.0.0",
  name: "护栏/竖杆护栏/组合式竖杆护栏",
  extensions: { catalog: { groupOrder: 20, templateOrder: 10, presets } },
  parameters: Object.entries(fields).map(([key, defaultValue]) => ({ key, defaultValue })),
};
const windowTemplate = { id: "window", name: "防盗窗/平面防盗窗", available: true, extensions: { catalog: { groupOrder: 10 } }, parameters: [] };
const stair = { id: "stair", name: "楼梯/钢楼梯/直跑钢楼梯", available: true, extensions: { catalog: { groupOrder: 30 } }, parameters: [] };
const templates = [stair, guardrail, windowTemplate];

await test("catalog groups security windows, guardrails and stairs independently", () => {
  const tree = buildTemplateGroupTree(templates);
  assert.deepEqual(tree.map((group) => group.title), ["防盗窗", "护栏", "楼梯"]);
  assert.equal(tree[1].children[0].title, "竖杆护栏");
  assert.equal(tree[1].children[0].templates.length, 3);
  assert.equal(tree[0].templates[0].templateId, "window");
});

await test("presets become distinct cards without changing native template identities", () => {
  const cards = buildCatalogEntries([guardrail]);
  assert.equal(cards.length, 3);
  assert.equal(new Set(cards.map((card) => card.catalogEntryId)).size, 3);
  assert.ok(cards.every((card) => card.id === "modular-guardrail" && card.templateId === "modular-guardrail"));
  assert.equal(cards[1].displayName, "三横档左转 L 型");
  assert.equal(cards[1].catalogParameters.layout, "left_l");
  assert.equal(getCatalogEntry([guardrail], guardrail.id).presetId, "r2-straight");
  assert.equal(getCatalogEntry([guardrail], guardrail.id, "missing"), null);
  assert.notEqual(getCatalogEntryId("a:b", "c"), getCatalogEntryId("a", "b:c"));
});

await test("preset overlays cannot replace identity or mutate template defaults", () => {
  const values = getCatalogParameters(guardrail, { parameters: { railCount: 2, templateId: "wrong", madeUp: 123 } });
  assert.equal(values.railCount, 2);
  assert.equal(values.guardHeight, 1200);
  assert.equal(values.templateId, undefined);
  assert.equal(values.madeUp, undefined);
  values.railCount = 99;
  assert.equal(getCatalogParameters(guardrail).railCount, 3);
});

await test("unavailable categories and invalid or duplicate presets do not add fake cards", () => {
  const altered = structuredClone(guardrail);
  altered.extensions.catalog.presets.push(presets[0], { id: "missing-parameters" }, { id: "hidden", available: false, parameters: {} });
  const result = buildTemplateGroupTree([altered, { id: "fake", name: "幕墙/未实现", available: false }]);
  assert.equal(result.length, 1);
  assert.equal(result[0].children[0].templates.length, 3);
});

await test("active card stays unique across rerenders and input edits", () => {
  const view = {
    tubeDesignerAddTemplateId: guardrail.id, tubeDesignerAddCatalogPresetId: "r3-left",
    tubeDesignerAddInstanceName: "项目 A 左侧护栏", tubeDesignerAddDraft: { ...fields, layout: "right_l", sideLength1: 4200 },
  };
  const html = renderDesignerAddDialog({ templates }, view);
  assert.equal((html.match(/tube-designer-template-card selected/g) ?? []).length, 1);
  assert.match(html, /data-tube-designer-catalog-preset-id="r3-left"[^>]*aria-pressed="true"/);
  assert.match(html, /5 款可用/);
  assert.doesNotMatch(html, /\[object Object\]/);
  const summary = renderDesignerAddParameterContent({ templates }, view);
  assert.match(summary, /4200|4,200/);
  assert.match(summary, /右转 L 型/);
  assert.equal(view.tubeDesignerAddDraft.layout, "right_l");
});

await test("schematics distinguish layout, rail count, double posts, large posts and plates", () => {
  const left = renderGuardrailSchematic({ layout: "left_l", railCount: 3 });
  const right = renderGuardrailSchematic({ layout: "right_l", railCount: 3 });
  assert.match(right, /translate\(100 0\) scale\(-1 1\)/);
  assert.doesNotMatch(left, /scale\(-1/);
  assert.notEqual(renderGuardrailSchematic({ railCount: 2 }), renderGuardrailSchematic({ railCount: 3 }));
  assert.notEqual(left, renderGuardrailSchematic({ layout: "left_l", cornerPostMode: "double" }));
  assert.match(renderGuardrailSchematic({ largePostMode: "middle" }), /guardrail-large-post/);
  assert.match(renderGuardrailSchematic({ infillType: "plate" }), /guardrail-panel/);
  assert.doesNotMatch(renderGuardrailSchematic({ infillType: "plate" }), /guardrail-infill/);
  assert.match(renderGuardrailSchematic({ infillType: "lower_plate" }), /guardrail-infill/);
});

function harness() {
  const cards = buildCatalogEntries(templates).map((entry) => ({
    dataset: { tubeDesignerTemplateId: entry.templateId, tubeDesignerCatalogEntryId: entry.catalogEntryId },
    selected: false, attributes: {}, classList: { toggle(_name, selected) { this.owner.selected = selected; } },
    setAttribute(key, value) { this.attributes[key] = value; },
  }));
  for (const card of cards) card.classList.owner = card;
  const inputs = [];
  const form = { querySelector() { return null; }, querySelectorAll(selector) { return selector === "[data-tube-designer-parameter]" ? inputs : []; } };
  const context = { mount: {
    querySelector(selector) { return ["[data-tube-designer-add-form]", "[data-tube-designer-parameter-form]"].includes(selector) ? form : null; },
    querySelectorAll(selector) { return selector === "[data-tube-designer-template-id]" ? cards : []; },
  } };
  const view = { scene: { tubeDesigner: { templates } } };
  const ops = { renderProject() {} };
  const act = (action, dataset = {}) => handleDesignerAreaAction(context, view, `tube-designer-${action}`, { dataset }, ops);
  return { view, context, cards, act, inputs };
}

await test("selecting a preset loads its parameters and highlights only that card", async () => {
  const { view, cards, act } = harness();
  await act("open-add");
  await act("select-template", { tubeDesignerTemplateId: guardrail.id, tubeDesignerCatalogPresetId: "r3-right-double" });
  assert.equal(view.tubeDesignerAddTemplateId, guardrail.id);
  assert.equal(view.tubeDesignerAddDraft.layout, "right_l");
  assert.equal(view.tubeDesignerAddDraft.cornerPostMode, "double");
  assert.match(view.tubeDesignerAddInstanceName, /^三横档右转双立柱 /);
  assert.equal(cards.filter((card) => card.selected).length, 1);
  assert.equal(cards.find((card) => card.selected).attributes["aria-pressed"], "true");
  view.tubeDesignerAddDraft.sideLength1 = 4175;
  await act("select-template", { tubeDesignerTemplateId: guardrail.id, tubeDesignerCatalogPresetId: "r3-right-double" });
  assert.equal(view.tubeDesignerAddDraft.sideLength1, 4175, "same card must not reset user dimensions");
  await act("select-template", { tubeDesignerTemplateId: guardrail.id, tubeDesignerCatalogPresetId: "nonexistent" });
  assert.equal(view.tubeDesignerAddDraft.sideLength1, 4175);
});

await test("create sends the real template ID and edited preset parameters", async () => {
  const { view, context, act, inputs } = harness();
  await act("open-add");
  await act("select-template", { tubeDesignerTemplateId: guardrail.id, tubeDesignerCatalogPresetId: "r3-left" });
  view.tubeDesignerAddDraft.sideLength1 = 4250;
  inputs.push({ dataset: { tubeDesignerParameter: "railCount" }, type: "select-one", value: "2", selectedOptions: [{ dataset: { tubeDesignerValueType: "number" } }] });
  let payload;
  context.sceneProxy = { invoke(method, value) { assert.equal(method, "TubeDesigner.GeneratePreview"); payload = value; throw new Error("stop-after-payload"); } };
  await assert.rejects(act("confirm-add"), /stop-after-payload/);
  assert.equal(payload.templateId, "modular-guardrail");
  assert.equal(payload.layout, "left_l");
  assert.equal(payload.sideLength1, 4250);
  assert.equal(payload.railCount, 2, "numeric enum values must survive the HTML select round trip");
  assert.equal(payload.catalogEntryId, undefined);
});

await test("editing existing instances never reapplies a catalog preset", async () => {
  const { view, context, act } = harness();
  view.tubeDesignerAddCatalogPresetId = "r3-left";
  view.scene.tubeDesigner.product = {
    entityId: "saved-1", templateId: guardrail.id, name: "已保存的自定义护栏",
    parameters: { ...fields, railCount: 2, layout: "u", sideLength1: 5300 },
  };
  let payload;
  context.sceneProxy = { invoke(_method, value) { payload = value; throw new Error("stop-after-payload"); } };
  await assert.rejects(act("confirm-update"), /stop-after-payload/);
  assert.equal(payload.layout, "u");
  assert.equal(payload.railCount, 2);
  assert.equal(payload.sideLength1, 5300);
  assert.equal(payload.productEntityId, "saved-1");
});

await test("shipped railing and staircase descriptors are in distinct primary categories", () => {
  const readTemplate = (directory) => ({ ...JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/${directory}/template.json`, import.meta.url))), available: true });
  const shipped = ["single_face_security_window", "straight_stair_railing", "straight_steel_staircase", "l_turn_steel_staircase", "u_turn_steel_staircase"].map(readTemplate);
  const tree = buildTemplateGroupTree(shipped);
  assert.deepEqual(tree.map((group) => group.title), ["防盗窗", "护栏", "楼梯"]);
  assert.equal(tree[1].children[0].templates[0].id, "straight-stair-railing");
  assert.equal(tree[2].children[0].templates.length, 3);
});

await test("all 20 shipped modular guardrail styles use declared parameters and render individually", () => {
  const descriptor = presentationDescriptor(JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/modular_guardrail/template.json", import.meta.url))));
  const entries = buildCatalogEntries([descriptor]);
  assert.equal(entries.length, 20);
  assert.equal(new Set(entries.map((entry) => entry.catalogEntryId)).size, 20);
  const keys = new Set(descriptor.parameters.map((field) => field.key));
  for (const preset of descriptor.extensions.catalog.presets) {
    assert.ok(Object.keys(preset.parameters).every((key) => keys.has(key)), `${preset.id} has undeclared parameters`);
    const entry = getCatalogEntry([descriptor], descriptor.id, preset.id);
    for (const [key, value] of Object.entries(preset.parameters)) assert.deepEqual(entry.catalogParameters[key], value);
    const html = renderDesignerAddDialog({ templates: [descriptor] }, {
      tubeDesignerAddTemplateId: descriptor.id, tubeDesignerAddCatalogPresetId: preset.id,
      tubeDesignerAddDraft: entry.catalogParameters,
    });
    assert.equal((html.match(/tube-designer-template-card selected/g) ?? []).length, 1);
    assert.equal(/\[object Object\]|NaN/.test(html), false, `${preset.id}: invalid text or SVG`);
    assert.match(html, /option value="3" data-tube-designer-value-type="number"/);
  }
  assert.equal(entries.filter((entry) => entry.catalogParameters.cornerPostMode === "double").length, 6);
  assert.equal(entries.filter((entry) => entry.catalogParameters.infillType !== "bars").length, 2);
});

await test("shipped security window panel cards show original plate schematics with position differences", () => {
  const descriptor = presentationDescriptor(JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/single_face_security_window/template.json", import.meta.url))));
  const entries = buildCatalogEntries([descriptor]);
  assert.equal(entries.length, 3);
  const thumbnails = entries.map((entry) => renderDesignerAddParameterContent({ templates: [descriptor] }, {
    tubeDesignerAddTemplateId: descriptor.id, tubeDesignerAddCatalogPresetId: entry.presetId,
    tubeDesignerAddDraft: entry.catalogParameters,
  }));
  assert.doesNotMatch(thumbnails[0], /class="security-window-plate"/);
  assert.match(thumbnails[1], /class="security-window-plate"/);
  assert.match(thumbnails[2], /class="security-window-plate"/);
  const panelPoints = (html) => html.match(/class="security-window-plate" points="([^"]+)"/)[1];
  assert.notEqual(panelPoints(thumbnails[1]), panelPoints(thumbnails[2]));
  const noDoor = renderDesignerAddParameterContent({ templates: [descriptor] }, {
    tubeDesignerAddTemplateId: descriptor.id, tubeDesignerAddDraft: { ...entries[1].catalogParameters, accessDoorEnabled: false },
  });
  assert.match(noDoor, /class="security-window-plate"/);
});

console.log(`TubeDesignerProductCatalogTest: ${completed.length} passed`);
