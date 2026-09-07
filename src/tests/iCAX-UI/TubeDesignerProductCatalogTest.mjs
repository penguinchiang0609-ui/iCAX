import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  buildCatalogEntries, buildTemplateGroupTree, catalogText, getCatalogEntry, getCatalogEntryGroupKeys, getCatalogEntryId,
  getCatalogParameters, renderGuardrailSchematic,
} from "../../apps/tube-designer/webpage/productCatalog.mjs";
import { renderDesignerAddDialog, renderDesignerAddParameterContent, renderDesignerRightPane } from "../../apps/tube-designer/webpage/designerViews.mjs";
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

await test("the default add style expands every category enclosing its selected card", async () => {
  const ids = getCatalogEntryGroupKeys(templates, windowTemplate.id);
  assert.deepEqual(ids, ["template-path:防盗窗"]);
  const html = renderDesignerAddDialog({ templates }, {
    tubeDesignerAddTemplateId: windowTemplate.id,
    tubeDesignerAddInstanceName: "新防盗窗",
  });
  assert.match(html, /data-tube-designer-template-tab-id="template-path:防盗窗" aria-selected="true"/);
  assert.match(html, /data-tube-designer-template-id="window"[^>]*aria-pressed="true"/);
  const { view, act } = harness();
  await act("open-add");
  assert.deepEqual(view.tubeDesignerExpandedTemplateGroupIds, ids);
  assert.equal(view.tubeDesignerAddCatalogTabId, "template-path:防盗窗");
  const draftBeforeTab = view.tubeDesignerAddDraft;
  await act("select-template-tab", { tubeDesignerTemplateTabId: "template-path:护栏" });
  assert.equal(view.tubeDesignerAddCatalogTabId, "template-path:护栏");
  assert.equal(view.tubeDesignerAddTemplateId, windowTemplate.id);
  assert.equal(view.tubeDesignerAddDraft, draftBeforeTab, "switching tabs only changes the catalogue browser");
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

await test("one generator can expose separate truthful categories without duplicating template identities", () => {
  const descriptor = structuredClone(guardrail);
  descriptor.extensions.catalog.presets.push({ id: "wall", displayName: "围墙直式", categoryPath: ["护栏", "围墙栏杆"], parameters: {} });
  const tree = buildTemplateGroupTree([descriptor]);
  assert.deepEqual(tree[0].children.map((group) => group.title), ["竖杆护栏", "围墙栏杆"]);
  assert.equal(tree[0].children[1].templates[0].templateId, guardrail.id);
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
  assert.match(renderGuardrailSchematic({ infillType: "glass" }), /guardrail-glass/);
  assert.doesNotMatch(renderGuardrailSchematic({ infillType: "glass" }), /guardrail-infill/);
  assert.match(renderGuardrailSchematic({ infillType: "cross" }), /guardrail-cross/);
  assert.match(renderGuardrailSchematic({ infillType: "diamond" }), /guardrail-diamond/);
  assert.doesNotMatch(renderGuardrailSchematic({ guardrailUse: "wall", spearTipModelReference: "system:spear-tip", spearTipEnabled: false }), /guardrail-spear/);
  assert.match(renderGuardrailSchematic({ guardrailUse: "wall", spearTipEnabled: true }), /guardrail-spear/);
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

await test("all 32 shipped modular guardrail styles use declared parameters and render individually", () => {
  const descriptor = presentationDescriptor(JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/modular_guardrail/template.json", import.meta.url))));
  const entries = buildCatalogEntries([descriptor]);
  assert.equal(entries.length, 32);
  assert.equal(new Set(entries.map((entry) => entry.catalogEntryId)).size, 32);
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
  assert.equal(entries.filter((entry) => entry.catalogParameters.infillType !== "bars").length, 8);
  assert.equal(entries.filter((entry) => entry.catalogPath.includes("围墙栏杆")).length, 4);
});

await test("security catalog separates common whole products from site-dependent enclosures", () => {
  const descriptors = ["single", "two", "three", "five"].map((kind) => presentationDescriptor(
    JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/${kind}_face_security_window/template.json`, import.meta.url)))));
  const entries = buildCatalogEntries(descriptors);
  assert.equal(entries.length, 4);
  const manifest = JSON.parse(readFileSync(new URL("../../apps/tube-designer/product.manifest.json", import.meta.url)));
  const registrations = manifest.capabilities.tubeDesigner.templates;
  for (const descriptor of descriptors) {
    const registration = registrations.find((item) => item.templateId === descriptor.id);
    assert.equal(registration.version, descriptor.version);
    assert.equal(registration.displayName, descriptor.name);
    const folder = descriptor.id.replaceAll("-", "_");
    const generator = readFileSync(new URL(`../../apps/tube-designer/templates/${folder}/template.py`, import.meta.url), "utf8");
    assert.ok(generator.includes(`TEMPLATE_VERSION = "${descriptor.version}"`));
    const groups = new Set(descriptor.groups.map((group) => group.key));
    assert.ok(descriptor.parameters.every((field) => groups.has(field.groupKey)));
  }
  assert.deepEqual(entries.map((e) => e.templateId), [
    "single-face-security-window", "five-face-security-window", "two-face-security-window", "three-face-security-window"]);
  assert.ok(entries.slice(0, 2).every((e) => e.catalogPath.includes("常用款式")));
  assert.ok(entries.slice(2).every((e) => e.catalogPath.includes("局部围护（需现场封闭）")));
  for (const entry of entries) {
    assert.equal(entry.id, entry.templateId);
    assert.equal(entry.catalogParameters.accessDoorEnabled, true);
    const html = renderDesignerAddParameterContent({ templates: descriptors }, {
      tubeDesignerAddTemplateId: entry.id, tubeDesignerAddDraft: entry.catalogParameters,
    });
    assert.doesNotMatch(html, /封板|mainInfillMode|centerPlate/);
    for (const title of ["尺寸与格栅布置", "管材规格与材料", "加工与装配"]) assert.ok(html.includes(title));
    assert.match(html, /<details[^>]* open>\s*<summary><span>尺寸与格栅布置/);
    assert.match(html, /<details[^>]*depth="0"\s*>\s*<summary><span>加工与装配/);
  }
});

await test("opening processes share choices and hide all inactive fabrication fields", () => {
  const descriptors = ["single", "two", "three", "five"].map((kind) => presentationDescriptor(
    JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/${kind}_face_security_window/template.json`, import.meta.url)))));
  const reference = descriptors[0];
  for (const descriptor of descriptors) {
    for (const key of ["doorFrameJoinType", "doorLeafFrameJoinType", "doorGap", "doorHingeCount", "doorClearWidth", "doorClearHeight"]) {
      const field = descriptor.parameters.find((p) => p.key === key);
      const common = reference.parameters.find((p) => p.key === key);
      assert.deepEqual(field.options, common.options);
      assert.equal(field.min, common.min);
      assert.equal(field.max, common.max);
    }
    const defaults = getCatalogParameters(descriptor);
    const render = (draft) => renderDesignerAddParameterContent({ templates: [descriptor] }, {
      tubeDesignerAddTemplateId: descriptor.id, tubeDesignerAddDraft: { ...defaults, ...draft },
    });
    const closed = render({ accessDoorEnabled: false, frameJoinType: "miter_45" });
    for (const field of descriptor.parameters.filter((p) => p.key.startsWith("door") || p.key.startsWith("vGroove"))) {
      assert.ok(!closed.includes(`data-tube-designer-parameter="${field.key}"`), field.key);
    }
    assert.doesNotMatch(render({ doorFrameJoinType: "miter_45", doorLeafFrameJoinType: "miter_45" }), /data-tube-designer-parameter="vGroove/);
    const folded = render({ doorFrameJoinType: "v_groove_90:sharp_v" });
    assert.match(folded, /data-tube-designer-parameter="vGrooveKFactor"/);
    assert.doesNotMatch(folded, /data-tube-designer-parameter="doorFrameButtWrapMode"/);
  }
});

await test("JSON order sorts only fields within groups in add and edit, with stable ties and visibility", () => {
  const descriptor = presentationDescriptor({
    id: "ordering", displayName: "排序测试",
    groups: [{ key: "a", displayName: "A", order: 10 }, { key: "b", displayName: "B", order: 20 }],
    parameters: [
      { key: "missing1", group: "a" }, { key: "late", group: "a", order: 30 },
      { key: "tie1", group: "a", order: 10 }, { key: "missing2", group: "a" },
      { key: "tie2", group: "a", order: 10, visibleWhen: { parameter: "show", value: true } },
      { key: "zero", group: "a", order: 0 }, { key: "negative", group: "b", order: -10 },
    ].map((field) => ({ valueType: "string", defaultValue: "", displayName: field.key, ...field })),
  });
  const original = JSON.stringify(descriptor);
  const keys = (html) => [...html.matchAll(/data-tube-designer-parameter="([^"]+)"/g)].map((match) => match[1]);
  for (const show of [true, false, true]) {
    const designer = { templates: [descriptor], product: { entityId: "p", templateId: descriptor.id, parameters: { show } } };
    const expected = ["zero", "tie1", ...(show ? ["tie2"] : []), "late", "missing1", "missing2", "negative"];
    assert.deepEqual(keys(renderDesignerAddParameterContent(designer, {
      tubeDesignerAddTemplateId: descriptor.id, tubeDesignerAddDraft: { show },
    })), expected);
    assert.deepEqual(keys(renderDesignerRightPane({}, { scene: { tubeDesigner: designer } })), expected);
  }
  assert.equal(JSON.stringify(descriptor), original);
});

console.log(`TubeDesignerProductCatalogTest: ${completed.length} passed`);
