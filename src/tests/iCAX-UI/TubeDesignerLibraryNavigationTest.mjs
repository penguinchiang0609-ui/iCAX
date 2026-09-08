import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";

const profiles = ribbonDefinition.tabs.find((tab) => tab.id === "profiles");
assert.equal(profiles.title, "管型库");
assert.deepEqual(profiles.groups.flatMap((group) => group.commands).map((command) => command.title), [
  "草图新建管型", "导入可编辑管型包", "导入 DXF 管型", "导出截面 DXF", "导出管子 STEP",
], "renaming navigation must not rename the profile entities and their operations");

const components = ribbonDefinition.tabs.find((tab) => tab.id === "components");
assert.equal(components.title, "配件库");
assert.deepEqual(components.groups.flatMap((group) => group.commands).map((command) => command.id), [
  "components.draw", "components.import", "components.export-step",
]);
const exportGroup = components.groups.find((group) => group.commands.some((command) => command.id === "components.export-step"));
assert.equal(exportGroup.title, "导出配件");
assert.equal(exportGroup.commands[0].title, "导出配件 STEP");
assert.equal(exportGroup.commands[0].iconName, "save");
assert.ok(!components.groups.flatMap((group) => group.commands).some((command) => command.id === "components.refresh"));

const sketch = ribbonDefinition.tabs.find((tab) => tab.id === "sketch");
const editCommands = sketch.groups.find((group) => group.title === "编辑").commands;
assert.ok(editCommands.some((command) => command.id === "sketch.break"));
assert.ok(editCommands.some((command) => command.id === "sketch.insert-point"));
assert.ok(editCommands.some((command) => command.id === "sketch.join"));
assert.deepEqual(sketch.groups.find((group) => group.title === "完成").commands.map((command) => command.title), ["确认", "取消"]);

const entry = readFileSync(new URL("../../apps/tube-designer/webpage/entry.mjs", import.meta.url), "utf8");
const areaNames = entry.match(/areaTitleOverrides:\s*\{([^}]+)\}/)?.[1] ?? "";
assert.match(areaNames, /profiles:\s*"管型库"/);
assert.match(areaNames, /components:\s*"配件库"/);
console.log("TubeDesignerLibraryNavigationTest: passed");
