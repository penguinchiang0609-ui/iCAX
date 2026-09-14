import assert from "node:assert/strict";
import { handleDesignerAreaAction } from "../../apps/tube-designer/webpage/designerActions.mjs";
import {
  productManufacturingPlanFingerprint,
  renderProductManufacturingPlan,
} from "../../apps/tube-designer/webpage/productManufacturingPlan.mjs";
import {
  productPrimaryDimensions,
  renderProductParameterDiagram,
} from "../../apps/tube-designer/webpage/productParameterDiagram.mjs";

const template = {
  id: "generic-product",
  version: "2.0.0",
  name: "通用产品",
  available: true,
  parameters: [
    { key: "width", displayName: "总宽", type: "number", unit: "mm", defaultValue: 1200 },
    { key: "height", displayName: "总高", type: "number", unit: "mm", defaultValue: 1800 },
  ],
  extensions: { primaryDimensions: { widthParameter: "width", heightParameter: "height" } },
};

const dimensions = productPrimaryDimensions(template, { width: 1350, height: 1900 });
assert.deepEqual(dimensions.map(({ kind, parameter, value }) => ({ kind, parameter, value })), [
  { kind: "width", parameter: "width", value: "1,350 mm" },
  { kind: "height", parameter: "height", value: "1,900 mm" },
]);
const diagram = renderProductParameterDiagram(template, { width: 1350, height: 1900 }, { mode: "add", activeParameter: "width" });
assert.match(diagram, /成品结构与尺寸示意/);
assert.match(diagram, /data-tube-designer-parameter-key="width"/);
assert.match(diagram, /is-width is-active/);

const firstFingerprint = productManufacturingPlanFingerprint(template, { height: 1800, width: 1200 }, 2);
assert.equal(firstFingerprint, productManufacturingPlanFingerprint(template, { width: 1200, height: 1800 }, 2));
assert.notEqual(firstFingerprint, productManufacturingPlanFingerprint(template, { width: 1200, height: 1800 }, 3));

const response = {
  instanceQuantity: 2,
  partCount: 4,
  tables: [{
    key: "parts",
    displayName: "零件清单",
    columns: [
      { key: "name", displayName: "名称", valueType: "string" },
      { key: "quantity", displayName: "单套数量", valueType: "number" },
    ],
    rows: [{ key: "rail", values: { name: "横杆", quantity: 3 } }],
  }],
};
const ready = renderProductManufacturingPlan({ status: "ready", fingerprint: firstFingerprint, response }, {
  mode: "add", fingerprint: firstFingerprint,
});
assert.match(ready, /单套 4 个制造构件 · 生产 2 套/);
assert.match(ready, /零件清单/);
assert.match(ready, /横杆/);
assert.match(ready, /表内数量为单套产品用量/);

const loadingPanel = { innerHTML: "" };
const manufacturingLabel = { textContent: "" };
const stageSections = [
  { dataset: { tubeDesignerEditorStage: "parameters" }, hidden: false },
  { dataset: { tubeDesignerEditorStage: "manufacturing" }, hidden: true },
];
const stageButtons = ["parameters", "manufacturing"].map((stage) => ({
  dataset: { tubeDesignerEditorStage: stage },
  selected: false,
  classList: { toggle(_name, selected) { this.owner.selected = selected; } },
  setAttribute() {},
}));
for (const button of stageButtons) button.classList.owner = button;
const form = {
  querySelector(selector) {
    if (selector === '.tube-designer-product-editor-stage[data-tube-designer-editor-stage="manufacturing"]') return loadingPanel;
    if (selector === '.tube-designer-product-editor-tabs button[data-tube-designer-editor-stage="manufacturing"] small') return manufacturingLabel;
    return null;
  },
  querySelectorAll(selector) {
    if (selector === "[data-tube-designer-parameter]") return [];
    if (selector === ".tube-designer-product-editor-tabs button[data-tube-designer-editor-stage]") return stageButtons;
    if (selector === ".tube-designer-product-editor-stage[data-tube-designer-editor-stage]") return stageSections;
    return [];
  },
};
const calls = [];
const context = {
  mount: { querySelector(selector) { return selector === "[data-tube-designer-add-form]" ? form : null; } },
  sceneProxy: { async invoke(method, payload) { calls.push({ method, payload }); return response; } },
};
const view = {
  scene: { tubeDesigner: { templates: [template] } },
  tubeDesignerAddTemplateId: template.id,
  tubeDesignerAddDraft: { width: 1200, height: 1800 },
  tubeDesignerAddInstanceQuantity: 2,
};
let fullRenderCount = 0;
await handleDesignerAreaAction(context, view, "tube-designer-select-product-editor-stage", {
  dataset: { tubeDesignerEditorMode: "add", tubeDesignerEditorStage: "manufacturing" },
}, { renderProject() { fullRenderCount += 1; } });
assert.equal(fullRenderCount, 0, "stage switching must patch only the current editor");
assert.equal(calls.length, 1);
assert.equal(calls[0].method, "TubeDesigner.GetProductManufacturingPlan");
assert.equal(calls[0].payload.instanceQuantity, 2);
assert.deepEqual(calls[0].payload.parameters, { width: 1200, height: 1800 });
assert.equal(stageSections[0].hidden, true);
assert.equal(stageSections[1].hidden, false);
assert.match(loadingPanel.innerHTML, /单套 4 个制造构件/);
assert.equal(manufacturingLabel.textContent, "已计算");

let finishRequest;
context.sceneProxy.invoke = () => new Promise((resolve) => { finishRequest = resolve; });
const pendingRefresh = handleDesignerAreaAction(context, view, "tube-designer-refresh-manufacturing-plan", {
  dataset: { tubeDesignerEditorMode: "add" },
}, { renderProject() { fullRenderCount += 1; } });
await new Promise((resolve) => setTimeout(resolve, 0));
assert.equal(typeof finishRequest, "function");
view.tubeDesignerAddDraft.width = 1400;
finishRequest(response);
await pendingRefresh;
assert.match(loadingPanel.innerHTML, /参数已变化，清单需要更新/,
  "a late response for old parameters must not be shown as the current plan");
assert.equal(manufacturingLabel.textContent, "待计算");

console.log("Product parameter diagram and manufacturing plan regressions passed");
