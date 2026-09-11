import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { getRibbonDefinition, ribbonDefinition, sketchRibbonGroups } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";
import { handleDesignerRibbonCommand } from "../../apps/tube-designer/webpage/designerActions.mjs";

const resources = ribbonDefinition.tabs.find((tab) => tab.id === "resources");
assert.equal(resources.title, "资源库");
const resourceCommands = resources.groups.flatMap((group) => group.commands);
assert.deepEqual(resourceCommands.slice(0, 4).map((command) => [command.id, command.title]), [
  ["resources.products", "产品"], ["resources.profiles", "管型"], ["resources.tools", "模具"], ["resources.components", "配件"],
], "资源库的四个子项应保持扁平并置");
assert.ok(resourceCommands.some((command) => command.id === "profiles.import-package"));
assert.ok(resourceCommands.some((command) => command.id === "tools.new-sketch"));
assert.ok(resourceCommands.some((command) => command.id === "tools.import-package"));
assert.ok(resourceCommands.some((command) => command.id === "tools.import-dxf"));
assert.ok(resourceCommands.some((command) => command.id === "components.import"));
const productTemplateGroup = resources.groups.find((group) => group.title === "产品模板");
assert.deepEqual(productTemplateGroup.commands.map((command) => command.id), [
  "designer.templates.manage", "designer.templates.new", "designer.templates.import",
  "designer.templates.export", "designer.templates.delete",
]);
const activeResources = getRibbonDefinition({ resourceArea: "tools" }).tabs.find((tab) => tab.id === "resources");
const activeResourceCommands = activeResources.groups[0].commands;
assert.equal(activeResourceCommands.find((command) => command.id === "resources.tools").active, true);
assert.equal(activeResourceCommands.filter((command) => command.active).length, 1);
assert.deepEqual(
  getRibbonDefinition({ resourceArea: "components" }).tabs.find((tab) => tab.id === "resources").groups.map((group) => group.title),
  ["资源类型", "配件操作"],
  "选中配件时只展示配件操作",
);
assert.deepEqual(
  getRibbonDefinition({ resourceArea: "tools" }).tabs.find((tab) => tab.id === "resources").groups.map((group) => group.title),
  ["资源类型", "模具操作"],
  "选中模具时只展示模具操作",
);
assert.deepEqual(
  getRibbonDefinition({ resourceArea: "products" }).tabs.find((tab) => tab.id === "resources").groups.map((group) => group.title),
  ["资源类型", "产品模板"],
  "选中产品时才展示产品模板管理",
);

const componentGroup = resources.groups.find((group) => group.title === "配件操作");
assert.deepEqual(componentGroup.commands.map((command) => command.id), [
  "components.draw", "components.import", "components.export-step",
]);
assert.equal(componentGroup.commands.at(-1).title, "导出配件 STEP");
assert.equal(componentGroup.commands.at(-1).iconName, "save");
assert.ok(!componentGroup.commands.some((command) => command.id === "components.refresh"));

const editCommands = sketchRibbonGroups.find((group) => group.title === "编辑").commands;
assert.ok(editCommands.some((command) => command.id === "sketch.break"));
assert.ok(editCommands.some((command) => command.id === "sketch.insert-point"));
assert.ok(editCommands.some((command) => command.id === "sketch.join"));
assert.deepEqual(sketchRibbonGroups.find((group) => group.title === "完成").commands.map((command) => command.title), ["确认", "取消"]);

const entry = readFileSync(new URL("../../apps/tube-designer/webpage/entry.mjs", import.meta.url), "utf8");
const areaNames = entry.match(/areaTitleOverrides:\s*\{([^}]+)\}/)?.[1] ?? "";
assert.match(areaNames, /resources:\s*"资源库"/);
assert.match(areaNames, /profiles:\s*"管型库"/);
assert.match(areaNames, /tools:\s*"模具库"/);
assert.match(areaNames, /components:\s*"配件库"/);

const designerActions = readFileSync(new URL("../../apps/tube-designer/webpage/designerActions.mjs", import.meta.url), "utf8");
assert.match(
  designerActions,
  /const resourceArea\s*=\s*resourceAreas\[commandId\][\s\S]*?await context\.actions\?\.selectRibbonTab\?\.\("resources"\)/,
  "资源切换必须同步刷新应用壳层 Ribbon，不能只刷新内容区",
);

const selectedTabs = [];
let renderCount = 0;
const resourceView = { pending: false };
const resourceHandled = await handleDesignerRibbonCommand(
  { actions: { selectRibbonTab: async (tabId) => selectedTabs.push(tabId) } },
  resourceView,
  "resources.profiles",
  { renderProject: () => { renderCount += 1; } },
);
assert.equal(resourceHandled, true);
assert.equal(resourceView.tubeDesignerResourceLibraryArea, "profiles");
assert.deepEqual(selectedTabs, ["resources"]);
assert.equal(renderCount, 1);

const progressCalls = [];
const progressView = { pending: false };
await handleDesignerRibbonCommand(
  {
    project: { projectId: "project-resource-switch" },
    actions: {
      selectRibbonTab: async () => {},
      withProjectProgress: async (projectId, progress, work) => {
        progressCalls.push({ projectId, progress });
        await work();
      },
    },
  },
  progressView,
  "resources.profiles",
  { renderProject() {} },
);
assert.equal(progressCalls.length, 1);
assert.equal(progressCalls[0].projectId, "project-resource-switch");
assert.equal(progressCalls[0].progress.title, "正在切换资源库");
assert.equal(progressCalls[0].progress.minimumVisibleMs, 500);

const productView = { pending: false };
await handleDesignerRibbonCommand(
  { actions: { selectRibbonTab: async () => {} } },
  productView,
  "resources.products",
  { renderProject() {} },
);
assert.equal(productView.tubeDesignerResourceLibraryArea, "products");
assert.deepEqual(
  getRibbonDefinition({ resourceArea: productView.tubeDesignerResourceLibraryArea }).tabs.find((tab) => tab.id === "resources").groups.map((group) => group.title),
  ["资源类型", "产品模板"],
);
console.log("TubeDesignerLibraryNavigationTest: passed");
