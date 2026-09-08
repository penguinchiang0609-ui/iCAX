import assert from "node:assert/strict";
import { ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";
import { renderProductTemplateManagerDialog } from "../../apps/tube-designer/webpage/templateLibrary.mjs";
import { handleDesignerRibbonCommand } from "../../apps/tube-designer/webpage/designerActions.mjs";

const productGroup = ribbonDefinition.tabs.find((tab) => tab.id === "view")?.groups
  .find((group) => group.title === "产品模板");
const manager = productGroup?.commands?.find((command) => command.id === "designer.templates.manage");
assert.ok(manager, "产品页签应提供模板管理入口");
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
assert.match(html, /\.iPT（ZIP）/);
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
console.log("TubeDesigner product template library tests passed");
