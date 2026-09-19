import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import {
  connectionParameterDefaults,
  connectionProcessCatalog,
} from "../../apps/tube-designer/webpage/connectionCatalog.mjs";
import {
  connectionLibraryState,
  connectionParameterValues,
  handleConnectionLibraryAction,
  renderConnectionLibraryLeftPane,
  renderConnectionLibraryRightPane,
  renderConnectionLibraryViewportOverlay,
} from "../../apps/tube-designer/webpage/connectionLibrary.mjs";

assert.ok(connectionProcessCatalog.length >= 4, "第一版连接库应覆盖插接、紧固和焊接典型连接");
assert.ok(connectionProcessCatalog.every((item) => item.schema === "icax.connection-process" && item.schemaVersion === 1));
assert.ok(connectionProcessCatalog.every((item) => item.participants.length >= 2), "连接模板必须描述多个参与零件");
assert.ok(connectionProcessCatalog.every((item) => item.assemblyPath.length > 0), "连接模板必须包含装配路径");
assert.ok(connectionProcessCatalog.every((item) => item.outputs.assemblyInstruction), "连接模板必须能输出装配指导");
assert.ok(connectionProcessCatalog.every((item) => item.jointFrame?.origin && item.geometryPolicy && item.manufacturingStrategy), "连接模板必须明确连接坐标系、几何保持策略和制造策略");

for (const template of connectionProcessCatalog) {
  const defaults = connectionParameterDefaults(template);
  assert.deepEqual(Object.keys(defaults), template.parameters.map((item) => item.key));
  for (const operation of template.operations) {
    assert.equal(operation.resource.kind, "punch-tool");
    const toolPath = fileURLToPath(new URL(`../../apps/tube-designer/templates/mold/${operation.resource.id}/tool.json`, import.meta.url));
    assert.equal(existsSync(toolPath), true, `${template.id} 引用的模具 ${operation.resource.id} 必须存在`);
  }
}

const view = {};
const state = connectionLibraryState(view);
assert.equal(state.selectedId, "tab-slot-lock");
assert.match(renderConnectionLibraryLeftPane({}, view), /插舌 \/ 插槽连接/);
assert.match(renderConnectionLibraryRightPane({}, view), /单件工艺复用/);
assert.match(renderConnectionLibraryRightPane({}, view), /模具：端部插舌 \/ 插槽/);
assert.match(renderConnectionLibraryViewportOverlay({}, view), /连接关系示意/);

let renders = 0;
const ops = { renderProject() { renders += 1; } };
await handleConnectionLibraryAction({}, view, "tube-designer-connection-select", { dataset: { tubeConnectionId: "slot-bolt-adjustable" } }, ops);
assert.equal(state.selectedId, "slot-bolt-adjustable");
await handleConnectionLibraryAction({}, view, "tube-designer-connection-parameter-change", {
  value: "25",
  dataset: { tubeConnectionId: "slot-bolt-adjustable", tubeConnectionParameter: "adjustment" },
}, ops);
assert.equal(connectionParameterValues(view).adjustment, 25);
assert.match(renderConnectionLibraryRightPane({}, view), /模具：长孔/);
assert.match(renderConnectionLibraryRightPane({}, view), /模具：圆孔/);
assert.equal(renders, 2);

console.log("TubeDesignerConnectionLibraryTest: passed");
