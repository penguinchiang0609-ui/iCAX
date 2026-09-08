import assert from "node:assert/strict";
import { renderProfileParameterDiagram } from "../../apps/tube-designer/webpage/profileParameterDiagram.mjs";

const tests = [];
function test(name, run) { run(); tests.push(name); }

function section() {
  return {
    name: "程式槽钢", width: 80, depth: 60,
    contours: [{ kind: "polygon", points: [[-40,-30],[40,-30],[40,-24],[-34,-24],[-34,24],[40,24],[40,30],[-40,30]] }],
    parameters: { width: 80, height: 60, webThickness: 6, flangeThickness: 6 },
    parameterDefinitions: [
      { key: "width", displayName: { "zh-CN": "外宽", "en-US": "Width" }, valueType: "number", unit: "mm" },
      { key: "height", displayName: "外高", valueType: "number", unit: "mm" },
      { key: "webThickness", displayName: "腹板厚", valueType: "number", unit: "mm" },
      { key: "flangeThickness", displayName: "翼缘厚", valueType: "number", unit: "mm" },
    ],
    parameterDiagram: { schemaVersion: 1, annotations: [
      { parameter: "width", kind: "linear", axis: "x", side: "top", from: [-40,30], to: [40,30] },
      { parameter: "height", kind: "linear", axis: "y", side: "left", from: [-40,-30], to: [-40,30] },
      { parameter: "webThickness", kind: "leader", side: "left", point: [-37,0], description: "左侧竖向腹板厚度" },
      { parameter: "flangeThickness", kind: "leader", side: "right", point: [10,27], description: { "zh-CN": "上下翼缘厚度" } },
    ] },
  };
}

test("complex profiles render source-defined parameter positions and Chinese dimensions", () => {
  const html = renderProfileParameterDiagram(section());
  assert.match(html, /<svg\b/);
  for (const [key, label] of [["width","外宽"],["height","外高"],["webThickness","腹板厚"],["flangeThickness","翼缘厚"]]) {
    assert.ok(html.includes(`data-profile-annotation-key="${key}"`), key);
    assert.ok(html.includes(label), label);
  }
  assert.match(html, /80/);
  assert.match(html, /60/);
  assert.match(html, /mm/);
  assert.match(html, /左侧竖向腹板厚度/);
  assert.match(html, /上下翼缘厚度/);
  assert.doesNotMatch(html, /NaN|Infinity|undefined/);
});

test("renamed definitions render and stale geometry is identified until evaluation finishes", () => {
  const original = section();
  const html = renderProfileParameterDiagram(original, {
    title: "本次零件参数",
    definitions: [{ key: "width", displayName: "槽口外宽", unit: "mm" }],
    parameters: { width: 113.5 }, compact: true,
  });
  assert.match(html, /槽口外宽/);
  assert.match(html, /上次成功|等待更新/);
  assert.match(html, /80/);
  assert.doesNotMatch(html, /113\.5/, "An old contour must not receive newly entered but unevaluated dimensions");
  assert.match(html, /本次零件参数/);
  assert.deepEqual(original.parameters, { width: 80, height: 60, webThickness: 6, flangeThickness: 6 });
  const updated = renderProfileParameterDiagram({ ...original, parameters: { ...original.parameters, width: 113.5 } });
  assert.match(updated, /113\.5/);
});

test("profiles without metadata retain their actual contours and clearly explain missing annotations", () => {
  const value = section();
  delete value.parameterDiagram;
  const html = renderProfileParameterDiagram(value);
  assert.match(html, /<svg\b/);
  assert.match(html, /未提供|暂无|尚未|缺少/);
  assert.doesNotMatch(html, /<g[^>]*data-profile-annotation-key="width"/,
    "The renderer must not invent the semantic meaning or location of an unannotated program parameter");
});

test("untrusted parameter labels, descriptions and keys cannot become markup", () => {
  const value = section();
  const key = 'x\" onmouseover=\"alert(1)';
  value.parameters[key] = 9;
  value.parameterDefinitions.push({ key, displayName: '<img src=x onerror="alert(1)">' });
  value.parameterDiagram.annotations.push({
    parameter: key, kind: "leader", side: "bottom", point: [0,-27],
    description: '<script>alert("diagram")</script>',
  });
  // Metadata is declarative; arbitrary imported SVG must never be injected.
  value.parameterDiagram.svg = '<svg onload="alert(1)"><image href="https://untrusted.example/track.svg"/></svg>';
  const html = renderProfileParameterDiagram(value, { title: '<img src=x onerror="alert(2)">' });
  assert.doesNotMatch(html, /<script\b|<img\b|<image\b|https:\/\/untrusted\.example/);
  assert.ok(!html.includes('data-profile-annotation-key="x" onmouseover='));
  assert.match(html, /&lt;/);
});

test("invalid annotation coordinates do not poison SVG geometry or invent anchors", () => {
  const value = section();
  value.parameterDiagram.annotations = [
    { parameter: "width", kind: "linear", axis: "x", side: "top", from: [NaN,30], to: [40,30] },
    { parameter: "height", kind: "leader", side: "left", point: [Infinity,0] },
    { parameter: "webThickness", kind: "leader", side: "right", point: [0] },
    { parameter: "flangeThickness", kind: "leader", side: "bottom", point: [null,0] },
  ];
  const html = renderProfileParameterDiagram(value);
  assert.doesNotMatch(html, /NaN|Infinity|undefined/);
  assert.match(html, /未提供|暂无|尚未|缺少/);
  assert.doesNotMatch(html, /<g[^>]*data-profile-annotation-key=/);
});

test("unknown parameter names cannot silently acquire an unrelated label or default value", () => {
  const value = section();
  value.parameterDiagram.annotations.push({ parameter: "missing", kind: "leader", side: "top", point: [0,0] });
  const html = renderProfileParameterDiagram(value);
  assert.doesNotMatch(html, /NaN|Infinity|undefined/);
  assert.doesNotMatch(html, /data-profile-annotation-key="missing"/);
});

test("conditional parameter definitions and annotations follow the current evaluated values", () => {
  const value = section();
  value.parameters.includeFlanges = false;
  value.parameterDefinitions.find((item) => item.key === "flangeThickness").visibleWhen = { op: "eq", parameter: "includeFlanges", value: true };
  value.parameterDiagram.annotations.find((item) => item.parameter === "webThickness").visibleWhen = { op: "eq", parameter: "includeFlanges", value: true };
  const html = renderProfileParameterDiagram(value);
  assert.doesNotMatch(html, /data-profile-annotation-key="flangeThickness"/);
  assert.doesNotMatch(html, /<g[^>]*data-profile-annotation-key="webThickness"/);
  assert.match(html, /data-profile-annotation-key="width"/);
});

test("an unsupported diagram schema is treated as unavailable rather than guessed", () => {
  const value = section();
  value.parameterDiagram.schemaVersion = 2;
  const html = renderProfileParameterDiagram(value);
  assert.match(html, /未提供|暂无|尚未|缺少/);
  assert.doesNotMatch(html, /<g[^>]*data-profile-annotation-key=/);
  assert.equal(renderProfileParameterDiagram(null), "");
  assert.equal(renderProfileParameterDiagram({ contours: value.contours }), "");
});

console.log(`TubeDesignerProfileParameterDiagramTest: ${tests.length} tests passed`);
