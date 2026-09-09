import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { ribbonDefinition, sketchRibbonGroups } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";

const resources = ribbonDefinition.tabs.find((tab) => tab.id === "resources");
assert.equal(resources.title, "资源库");
const resourceCommands = resources.groups.flatMap((group) => group.commands);
assert.deepEqual(resourceCommands.slice(0, 3).map((command) => [command.id, command.title]), [
  ["resources.profiles", "管型"], ["resources.tools", "模具"], ["resources.components", "配件"],
], "资源库的三个子项应保持扁平并置");
assert.ok(resourceCommands.some((command) => command.id === "profiles.import-package"));
assert.ok(resourceCommands.some((command) => command.id === "tools.refresh"));
assert.ok(resourceCommands.some((command) => command.id === "components.import"));

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
console.log("TubeDesignerLibraryNavigationTest: passed");
