import assert from "node:assert/strict";
import { ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";
import {
  productTemplateLibraryState,
  renderProductTemplateLibraryLeftPane,
  renderProductTemplateLibraryRightPane,
  renderProductTemplateLibraryViewportOverlay,
  handleProductTemplateLibraryAction,
  renderProductTemplateManagerDialog,
} from "../../apps/tube-designer/webpage/templateLibrary.mjs";
import { handleDesignerRibbonCommand } from "../../apps/tube-designer/webpage/designerActions.mjs";

const productGroup = ribbonDefinition.tabs.find((tab) => tab.id === "resources")?.groups
  .find((group) => group.title === "产品模板");
const manager = productGroup?.commands?.find((command) => command.id === "designer.templates.manage");
assert.ok(manager, "资源库应提供产品模板管理入口");
assert.deepEqual(productGroup.commands.map((item) => item.id), [
  "designer.templates.manage", "designer.templates.new", "designer.templates.import",
  "designer.templates.export", "designer.templates.delete",
]);

const view = {
  scene: { tubeDesigner: { templates: [
    { id: "builtin", name: "内置/示例", version: "1.0.0", available: true },
  ] } },
  tubeDesignerUserData: { productTemplates: [{ id: "personal-1", name: "我的模板", version: "1.0.0", description: "客户版本" }] },
  tubeDesignerTemplateManager: { mode: "list", selectedId: "builtin" },
};
let html = renderProductTemplateManagerDialog(view);
assert.match(html, /产品模板管理/);
assert.match(html, /\.itpt（ZIP）/);
assert.match(html, /内置/);
assert.match(html, /data-cam-action="tube-designer-template-delete" disabled/);
view.tubeDesignerTemplateManager.selectedId = "personal-1";
html = renderProductTemplateManagerDialog(view);
assert.match(html, /我的模板/);
assert.doesNotMatch(html, /data-cam-action="tube-designer-template-delete" disabled/);

let renders = 0;
await handleDesignerRibbonCommand(
  { project: { projectId: "test" } },
  {},
  "designer.templates.manage",
  { renderProject() { renders += 1; } },
);
assert.equal(renders, 1, "模板管理命令应打开管理窗口");

const createView = {
  scene: { tubeDesigner: { templates: [
    { id: "builtin", name: "内置/示例", version: "1.0.0", available: true },
  ] } },
  tubeDesignerUserData: { productTemplates: [{ id: "personal-1", name: "我的模板" }] },
  tubeDesignerTemplateManager: { mode: "list", selectedId: "personal-1" },
};
await handleDesignerRibbonCommand(
  { project: { projectId: "test" } },
  createView,
  "designer.templates.new",
  { renderProject() {} },
);
assert.equal(createView.tubeDesignerTemplateManager.baseTemplateId, "builtin",
  "新增模板不能把个人模板记录误当成基础模板");

const libraryView = {
  activeAreaId: "templates",
  scene: { tubeDesigner: { templates: [{ id: "builtin", name: "内置/示例", version: "1.0.0", available: true, parameters: [
    { key: "length", displayName: "长度", valueType: "number", defaultValue: 100, constraints: { minimum: 1, step: 1 } },
    { key: "railCount", valueType: "enum", defaultValue: 2, choices: [{ value: 2 }, { value: 3 }] },
    { key: "infillType", valueType: "enum", defaultValue: "glass", choices: [{ value: "bars" }, { value: "glass" }, { value: "diamond" }] },
    { key: "guardrailUse", valueType: "enum", defaultValue: "platform", choices: [{ value: "platform" }, { value: "wall" }] },
  ], extensions: { parameterRules: [{ when: { guardrailUse: "wall" }, set: { infillType: "bars" } }], catalog: { presets: [{ id: "style-a", displayName: "示例款式", parameters: { length: 140 } }] } } }] } },
  tubeDesignerUserData: { productTemplates: [{ id: "personal-1", name: "我的模板", version: "1.0.0" }] },
  viewport: { applyViewSnapshot: async ({ rows }) => ({ applied: true, entityIds: rows.map((row) => row.entityId) }), setStandardView() {}, fitViewToViewport() {} },
  sceneProxy: { resources: {}, async invoke(method) { assert.equal(method, "TubeDesigner.GenerateProductTemplatePreview"); return { items: [{ entityId: "item-1", geometry: { url: "geometry", version: 1 }, bounds: { min: [0, 0, 0], max: [10, 10, 10] } }], material: { url: "material", version: 1 } }; } },
};
assert.equal(productTemplateLibraryState(libraryView).selectedId, "builtin::style-a");
assert.match(renderProductTemplateLibraryLeftPane({}, libraryView), /产品模板/);
assert.match(renderProductTemplateLibraryLeftPane({}, libraryView), /tube-product-template-library-group/);
assert.match(renderProductTemplateLibraryLeftPane({}, libraryView), /示例款式/);
assert.match(renderProductTemplateLibraryRightPane({}, libraryView), /预览参数/);
renderProductTemplateLibraryViewportOverlay(libraryView, libraryView);
await new Promise((resolve) => setTimeout(resolve, 0));
assert.equal(libraryView.tubeDesignerProductTemplateLibrary.preview?.response?.items?.length, 1);
assert.equal(libraryView.preserveCustomViewportEntities, true,
  "产品资源页预览成功后必须保留临时三维实体，避免工作台重绘时清空中央场景");
let parameterRenders = 0;
await handleProductTemplateLibraryAction(libraryView, libraryView, "tube-designer-product-template-library-parameter-change", {
  dataset: { tubeTemplateLibraryId: "builtin::style-a", tubeTemplateLibraryParameter: "length" }, value: "220",
}, { renderProject() { parameterRenders += 1; } });
assert.equal(parameterRenders, 1);
assert.equal(libraryView.tubeDesignerProductTemplateLibrary.parameterDrafts["builtin::style-a"].length, 220);
await handleProductTemplateLibraryAction(libraryView, libraryView, "tube-designer-product-template-library-parameter-change", {
  dataset: { tubeTemplateLibraryId: "builtin::style-a", tubeTemplateLibraryParameter: "railCount" }, value: "3",
}, { renderProject() {} });
assert.equal(libraryView.tubeDesignerProductTemplateLibrary.parameterDrafts["builtin::style-a"].railCount, 3,
  "数字枚举必须以模板声明的数字类型提交，而不是以下拉框字符串提交");
await handleProductTemplateLibraryAction(libraryView, libraryView, "tube-designer-product-template-library-parameter-change", {
  dataset: { tubeTemplateLibraryId: "builtin::style-a", tubeTemplateLibraryParameter: "guardrailUse" }, value: "wall",
}, { renderProject() {} });
assert.equal(libraryView.tubeDesignerProductTemplateLibrary.parameterDrafts["builtin::style-a"].infillType, "bars",
  "模板声明的参数约束应在修改参数时自动归一化");
await handleProductTemplateLibraryAction(libraryView, libraryView, "tube-designer-product-template-library-parameter-change", {
  dataset: { tubeTemplateLibraryId: "builtin::style-a", tubeTemplateLibraryParameter: "infillType" }, value: "diamond",
}, { renderProject() {} });
assert.equal(libraryView.tubeDesignerProductTemplateLibrary.parameterDrafts["builtin::style-a"].infillType, "bars",
  "违反模板约束的参数选择不能再次把预览置于不可生成状态");

// Artwork belongs to each template package. The library only consumes the
// descriptor's asset data and must not grow an ID/name switch for new cards.
const schematicView = {
  scene: { tubeDesigner: { templates: [
    { id: "single-face-security-window", name: "单面防盗窗", available: true, extensions: { catalog: { assetData: { schematic: "data:image/svg+xml;base64,PHN2Zy8+" } } } },
    { id: "modular-guardrail-glass-straight", name: "管框玻璃栏板直式", available: true, extensions: { catalog: { assetData: { schematic: "data:image/svg+xml;base64,PHN2Zy8+" } } }, parameters: [
      { key: "layout", defaultValue: "straight" }, { key: "railCount", defaultValue: 3 },
      { key: "infillType", defaultValue: "glass" }, { key: "guardrailUse", defaultValue: "platform" },
    ] },
    { id: "u-turn-steel-staircase", name: "楼梯/钢楼梯/U形双跑钢楼梯", available: true, extensions: { catalog: { assetData: { schematic: "data:image/svg+xml;base64,PHN2Zy8+" } } } },
  ] } },
};
const schematicHtml = renderProductTemplateLibraryLeftPane({}, schematicView);
assert.equal((schematicHtml.match(/tube-product-template-library-schematic/g) ?? []).length, 3);
assert.equal((schematicHtml.match(/data:image\/svg\+xml;base64,PHN2Zy8\+/g) ?? []).length, 3);
assert.doesNotMatch(schematicHtml, />▦</);
console.log("TubeDesigner product template library tests passed");
