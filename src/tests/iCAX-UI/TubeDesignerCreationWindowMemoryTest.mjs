import assert from "node:assert/strict";
import { createWindowStateMemory } from "../../iCAX-UI/SDK/Forms/windowStateMemory.mjs";
import { rememberDesignerCreationWindowState, restoreDesignerAddWindowState,
  buildBatchExcelTemplateDialogState } from "../../apps/tube-designer/webpage/designerActions.mjs";

const data = new Map();
const storage = { getItem: key => data.get(key) ?? null, setItem: (key, value) => data.set(key, value) };
const memory = () => createWindowStateMemory({ namespace: "creation-window-test", storage });
const template = { id: "window", available: true, parameters: [
  { key: "width", valueType: "number", defaultValue: 1200 },
  { key: "mode", valueType: "enum", defaultValue: "fixed", choices: [{ value: "fixed" }, { value: "opening" }] },
  { key: "enabled", valueType: "boolean", defaultValue: true },
  { key: "internal", valueType: "string", defaultValue: "system", presentation: { visible: false } },
  { key: "readOnly", valueType: "number", defaultValue: 4, readOnly: true },
] };
const makeView = () => ({ scene: { tubeDesigner: { templates: [template] } },
  tubeDesignerAddDialogOpen: true, tubeDesignerAddTemplateId: "window", tubeDesignerAddCatalogEntryId: "window",
  tubeDesignerAddDraft: { width: 1200, mode: "fixed", enabled: true, internal: "system", readOnly: 4 },
  tubeDesignerAddInstanceQuantity: 1 });
const controls = [
  { dataset: { tubeDesignerParameter: "width" }, type: "number", value: "1480" },
  { dataset: { tubeDesignerParameter: "mode" }, type: "select-one", value: "opening" },
  { dataset: { tubeDesignerParameter: "enabled" }, type: "checkbox", checked: false },
  { dataset: { tubeDesignerParameter: "internal" }, type: "text", value: "hidden-edit" },
];
const mount = { isConnected: true,
  querySelectorAll: selector => selector.includes("data-tube-designer-parameter") ? controls : [],
  querySelector: selector => selector.includes("instance-quantity") ? { value: "3" } : null };
rememberDesignerCreationWindowState({ mount }, makeView(), memory());
const restored = makeView();
restoreDesignerAddWindowState(restored, template, memory());
assert.deepEqual(restored.tubeDesignerAddDraft, { width: 1480, mode: "opening", enabled: false, internal: "system", readOnly: 4 });
assert.equal(restored.tubeDesignerAddInstanceQuantity, "3");
assert.equal(memory().read("product-add", "templateId"), "window");
assert.equal(memory().readFields("product-add:window:window").parameters.internal, undefined);

const other = makeView(); other.tubeDesignerAddTemplateId = "railing"; other.tubeDesignerAddCatalogEntryId = "railing";
restoreDesignerAddWindowState(other, template, memory());
assert.equal(other.tubeDesignerAddDraft.width, 1200, "templates do not share parameter defaults");
const changedChoices = { ...template, parameters: template.parameters.map(field => field.key === "mode"
  ? { ...field, choices: [{ value: "fixed" }] } : field) };
const narrowed = makeView(); restoreDesignerAddWindowState(narrowed, changedChoices, memory());
assert.equal(narrowed.tubeDesignerAddDraft.mode, "fixed", "removed choices cannot return through memory");

memory().write("excel-template:window", "columns", {
  width: { included: true, required: false, alias: "实测宽度", defaultValue: "1480" },
  mode: { included: true, required: true, alias: "款式", defaultValue: "opening" },
  internal: { included: true, required: true, alias: "泄漏", defaultValue: "hidden" },
  deleted: { included: true, alias: "旧字段", defaultValue: "0" },
});
const columns = buildBatchExcelTemplateDialogState(template, memory()).columns;
assert.equal(columns.find(field => field.key === "width").alias, "实测宽度");
assert.equal(columns.find(field => field.key === "width").required, false);
assert.equal(columns.find(field => field.key === "mode").defaultValue, "opening");
assert.equal(columns.some(field => ["internal", "deleted", "readOnly"].includes(field.key)), false);
const narrowedColumns = buildBatchExcelTemplateDialogState(changedChoices, memory()).columns;
assert.equal(narrowedColumns.find(field => field.key === "mode").defaultValue, "fixed");
assert.equal(buildBatchExcelTemplateDialogState({ ...template, id: "railing" }, memory()).columns.find(field => field.key === "width").included, false);
console.log("TubeDesigner creation window memory: passed (fresh storage readers, template isolation, typed values, narrowed choices, hidden parameters and Excel settings).");
