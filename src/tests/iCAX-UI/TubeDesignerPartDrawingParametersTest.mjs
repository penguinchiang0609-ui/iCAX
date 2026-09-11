import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { fieldControl, isDrawingParameterVisible, parameterFields } from "../../apps/tube-designer/webpage/partDrawingParameters.mjs";

const action = name => `tube-designer-drawing-${name}`;
const keys = markup => [...markup.matchAll(/data-tube-designer-punch-parameter="([^"]+)"/g)].map(match => match[1]);

test("each V groove is an independent descriptor with only its own fields", () => {
  const ids = ["v-notch-sharp", "edge-arc-groove"];
  for (const id of ids) {
    const descriptor = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/mold/${id}/tool.json`, import.meta.url), "utf8"));
    const before = { toolParameters: {} };
    const markup = parameterFields(action, descriptor, before);
    const defaults = Object.fromEntries(descriptor.parameters.map((item) => [item.key, item.defaultValue]));
    const visibleKeys = descriptor.parameters.filter((item) => isDrawingParameterVisible(item.visibleWhen, defaults)).map((item) => item.key);
    assert.deepEqual(keys(markup).sort(), visibleKeys.sort(), id);
    assert.deepEqual(before, { toolParameters: {} }, "Rendering does not install defaults into the model");
    assert.equal(descriptor.parameters.some((item) => item.key === "style"), false, `${id} has no style switch`);
  }
  const sharp = JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/mold/v-notch-sharp/tool.json", import.meta.url), "utf8"));
  assert.ok(keys(parameterFields(action, sharp, { toolParameters: { bottomStrategy: "flat" } })).includes("flatWidth"));
});

test("nested eq/ne/all/any conditions honor stored false and zero values and schema defaults", () => {
  const schema = { parameters: [
    { key: "kind", defaultValue: "v", valueType: "string" },
    { key: "enabled", defaultValue: false, valueType: "boolean" },
    { key: "count", defaultValue: 0, valueType: "integer" },
    { key: "detail", defaultValue: 12, visibleWhen: { op: "all", conditions: [
      { op: "eq", parameter: "kind", value: "v" },
      { any: [{ op: "eq", key: "enabled", value: true }, { all: [{ op: "eq", key: "count", value: 0 }, { op: "ne", key: "enabled", value: true }] }] },
    ] } },
  ] };
  assert.ok(keys(parameterFields(action, schema, {})).includes("detail"));
  assert.ok(!keys(parameterFields(action, schema, { toolParameters: { count: 2 } })).includes("detail"));
  assert.ok(keys(parameterFields(action, schema, { parameters: { count: 2 }, toolParameters: { count: 0 } })).includes("detail"));
  assert.equal(isDrawingParameterVisible({ op: "any", conditions: [] }, {}), false);
  assert.equal(isDrawingParameterVisible({ op: "all", conditions: [] }, {}), true);
});

test("number inputs preserve limits, integer steps, units and dedicated action/end contracts", () => {
  const markup = fieldControl(action, "parameter", "长度", 0, { valueType: "integer", min: 0, max: 12, step: 0.25, unit: "mm" }, "start", "length");
  assert.match(markup, /type="number" step="1" min="0" max="12" value="0"/);
  assert.match(markup, /data-cam-change-action="tube-designer-drawing-field-change"/);
  assert.match(markup, /data-tube-designer-punch-end="start"/);
  assert.match(markup, /data-tube-designer-punch-parameter="length"/);
  assert.match(markup, /<small>mm<\/small>/);
  assert.match(fieldControl(action, "size", "尺寸", NaN, { constraints: { minimum: 0.1, maximum: 20, step: 0.01 } }), /step="0.01" min="0.1" max="20" value=""/);
});

test("choices, checkboxes and localized text render safely without HTML or attribute injection", () => {
  const markup = fieldControl(action, 'x" onfocus="bad', '<支管>', 'b"', { options: [
    { value: "a", displayName: { "zh-CN": "圆管" } }, { value: 'b"', label: '<矩形&管>' },
  ] });
  assert.match(markup, /<span>&lt;支管&gt;<\/span>/);
  assert.match(markup, /value="b&quot;" selected>&lt;矩形&amp;管&gt;/);
  assert.match(markup, /data-tube-designer-punch-field="x&quot; onfocus=&quot;bad"/);
  assert.ok(!markup.includes('<矩形')); assert.match(markup, />圆管<\/option>/);
  assert.match(fieldControl(action, "through", "贯穿", true, { valueType: "boolean" }), / checked/);
  assert.ok(!fieldControl(action, "through", "贯穿", false, { valueType: "boolean" }).includes("checked"));
  assert.ok(!fieldControl(action, "through", "贯穿", "false", { valueType: "boolean" }).includes("checked"));
  assert.match(fieldControl(action, "name", "名称", '"x"', { valueType: "string" }), /type="text" value="&quot;x&quot;"/);
});

test("fixed and unavailable tools produce read-only notes without editable geometry controls", () => {
  assert.equal(parameterFields(action, null, {}), "");
  assert.match(parameterFields(action, null, { toolRef: { id: "gone" } }), /节点只读/);
  const fixed = parameterFields(action, { kind: "fixed", parameters: [{ key: "diameter", defaultValue: 5 }] }, {});
  assert.match(fixed, /定式刀具/); assert.ok(!fixed.includes("<input"));
});
