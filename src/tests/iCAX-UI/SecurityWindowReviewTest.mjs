import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { renderSecurityWindowReview, securityWindowOpeningDimensions } from "../../apps/tube-designer/webpage/securityWindowReview.mjs";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";
import {
  getDefaultParameters,
  renderDesignerAddParameterContent,
  renderDesignerRightPane,
} from "../../apps/tube-designer/webpage/designerViews.mjs";

// Exercise shipped descriptors in the host's presentation shape, never a copy of
// their defaults that could keep passing after the actual templates change.
const templates = ["single", "two", "three", "five"].map((prefix) => {
  const raw = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/product/${prefix}_face_security_window/template.json`, import.meta.url)));
  const groups = raw.groups.map((group) => ({ ...group, displayName: catalogText(group.displayName) }));
  return {
    ...raw, available: true, name: catalogText(raw.displayName), groups,
    parameters: raw.parameters.map((field) => ({
      ...field, displayName: catalogText(field.displayName),
      type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
      groupKey: field.group, group: groups.find((group) => group.key === field.group)?.displayName ?? field.group,
      min: field.constraints?.minimum, max: field.constraints?.maximum, step: field.constraints?.step,
      options: field.choices?.map((choice) => ({ ...choice, label: catalogText(choice.displayName), displayName: catalogText(choice.displayName) })),
    })),
  };
});
const template = templates[0];
const defaults = getDefaultParameters(templates, template.id);
for (const source of templates) {
  const shipped = getDefaultParameters(templates, source.id);
  assert.equal(source.extensions.securityWindow.reviewVersion, 2);
  assert.equal(shipped.mainHorizontalConnection, "insert");
  assert.equal(shipped.accessDoorEnabled, true);
  const html = renderSecurityWindowReview(source, shipped);
  assert.match(html, /横杆插入边框/);
  assert.doesNotMatch(html, /V 槽折弯|须先打样/);
  if (source === template) {
    assert.equal(shipped.frameLayout, "four_sides");
    for (const key of ["frameJoinType", "doorFrameJoinType", "doorLeafFrameJoinType"]) assert.equal(shipped[key], "miter_45");
    assert.match(html, /45°拼焊/);
    assert.match(html, /预览保留未切角管材/);
    assert.doesNotMatch(html, /大框未四边闭合/);
  } else {
    assert.equal(shipped.frameCornerJoin, "post_butt");
    assert.equal(shipped.sideHorizontalCount, 4);
    assert.equal(shipped.sideVerticalCount, 4);
    assert.equal(shipped.sideMaximumVerticalClearGap, 110);
    assert.match(html, /立柱贯通、横梁直拼/);
    assert.doesNotMatch(html, /预览保留未切角管材/);
    const miter = renderSecurityWindowReview(source, { ...shipped, frameCornerJoin: "rail_miter" });
    assert.match(miter, /横梁45°拼角/);
    assert.match(miter, /预览保留未切角管材/);
    assert.match(html, source.id === "five-face-security-window"
      ? /背面朝墙，需确认外凸安装条件、原窗开启和清洁检修空间/
      : /顶底未独立封闭，开边需由现场墙体、窗台等围护补齐/);
  }
  assert.match(renderSecurityWindowReview(source, { ...shipped, mainHorizontalConnection: "weld" }), /横杆平口焊接/);
}
const rendered = renderSecurityWindowReview(template, defaults);
assert.match(rendered, /data-security-window-review/);
assert.match(rendered, /800 × 1000 mm/);
assert.match(rendered, /830 × 1000 mm/);
assert.match(rendered, /成品外包尺寸/);
assert.doesNotMatch(rendered, /材质未指定|表面处理未指定/);
assert.match(renderSecurityWindowReview(template, { ...defaults, materialGrade: "unspecified" }), /材质未指定/);
assert.match(rendered, /目标尺寸为设计值，不代表现场可通行或合规结论/);
assert.match(rendered, /仅完成几何与装配校验，五金开启、墙体锚固、承载和当地要求待现场核验/);
assert.doesNotMatch(rendered, /认证通过|符合国标|合规通过/);

for (const frameLayout of ["left_right", "top_bottom"]) {
  assert.match(renderSecurityWindowReview(template, { ...defaults, frameLayout }), /大框未四边闭合/);
}
const grooveKeys = ["frameJoinType", "doorFrameJoinType", "doorLeafFrameJoinType"];
const straightJoins = { ...defaults, ...Object.fromEntries(grooveKeys.map((key) => [key, "butt_90"])) };
assert.doesNotMatch(renderSecurityWindowReview(template, straightJoins), /预览保留未切角管材/);
for (const key of grooveKeys) {
  const activeMiter = { ...straightJoins, [key]: "miter_45" };
  assert.match(renderSecurityWindowReview(template, activeMiter), /预览保留未切角管材/);
  assert.doesNotMatch(renderSecurityWindowReview(template, {
    ...activeMiter, ...(key === "frameJoinType" ? { frameLayout: "left_right" } : { accessDoorEnabled: false }),
  }), /预览保留未切角管材/);
  const field = template.parameters.find((item) => item.key === key);
  const grooveChoices = field.choices.filter((choice) => choice.value.startsWith("v_groove_90:"));
  assert.equal(grooveChoices.length, 4);
  for (const choice of grooveChoices) {
    const values = { ...defaults, [key]: choice.value };
    assert.match(renderSecurityWindowReview(template, values), /须先打样确认管材、设备及折弯补偿/);
    const inactive = key === "frameJoinType" ? { frameLayout: "left_right" } : { accessDoorEnabled: false };
    assert.doesNotMatch(renderSecurityWindowReview(template, { ...values, ...inactive }), /V 槽折弯|须先打样/);
  }
}
assert.match(renderSecurityWindowReview(template, { ...defaults, frameLayout: "left_right", doorFrameJoinType: "v_groove_90:sharp_v" }), /须先打样/);
assert.match(renderSecurityWindowReview(template, { ...defaults, accessDoorEnabled: false, frameJoinType: "v_groove_90:sharp_v" }), /须先打样/);

// Metadata supports additional security-window templates; unrelated templates stay untouched.
assert.match(renderSecurityWindowReview({ extensions: template.extensions }, defaults), /data-security-window-review/);
assert.equal(renderSecurityWindowReview({ id: "straight-stair-railing" }, defaults), "");
assert.equal(renderSecurityWindowReview(null, defaults), "");
for (const id of ["single-face-security-window", "two-face-security-window", "three-face-security-window", "five-face-security-window"]) {
  const html = renderSecurityWindowReview({ id }, defaults);
  assert.match(html, /data-security-window-review/);
  assert.match(html, /成品外包尺寸/);
  assert.match(html, /830 × 1000 mm/);
}
assert.match(renderSecurityWindowReview(template), /830 × 1000 mm/);
const maintenance = renderSecurityWindowReview(template, { ...defaults, doorUse: "maintenance", doorClearWidth: 400, doorClearHeight: 400 });
assert.match(maintenance, /检修口（非逃生窗）/);
assert.match(maintenance, /目标开启净尺寸/);
assert.match(maintenance, /400 × 400 mm/);
assert.match(maintenance, /430 × 400 mm/);
assert.match(maintenance, /当前为检修口，不作为逃生窗/);
assert.doesNotMatch(maintenance, /目标通行净尺寸/);

for (const disabled of [false, "false", "否"]) {
  const html = renderSecurityWindowReview(template, { ...defaults, accessDoorEnabled: disabled });
  assert.match(html, /未设置逃生窗/);
  assert.doesNotMatch(html, /800 × 1000 mm|830 × 1000 mm/);
}
for (const enabled of [true, "true", "是"]) {
  assert.match(renderSecurityWindowReview(template, { ...defaults, accessDoorEnabled: enabled }), /830 × 1000 mm/);
}
const malicious = '<img src=x onerror="boom"> &';
const escaped = renderSecurityWindowReview(template, { ...defaults, materialGrade: malicious });
assert.doesNotMatch(escaped, /<img/);
assert.match(escaped, /&lt;img src=x onerror=&quot;boom&quot;&gt; &amp;/);
assert.match(renderSecurityWindowReview(template, { ...defaults, doorClearWidth: malicious }), /待填写有效尺寸/);
assert.doesNotMatch(renderSecurityWindowReview(template, { ...defaults, doorClearWidth: malicious }), /<img|NaN/);
assert.match(renderSecurityWindowReview(template, { ...defaults, doorClearWidth: -1 }), /待填写有效尺寸/);
assert.match(renderSecurityWindowReview(template, { ...defaults, materialGrade: "304", surfaceTreatment: "unspecified" }), /表面处理未指定/);
assert.doesNotMatch(renderSecurityWindowReview(template, { ...defaults, materialGrade: "304", surfaceTreatment: "passivation" }), /材质未指定|表面处理未指定/);

const importedProfile = (width, depth, kind = "imported-dxf") => ({
  schema: "icax.imported-tube-profile", schemaVersion: 1, kind, width, depth,
  contours: [{ kind: "rectangle", width, height: depth }],
});
const imported = Object.freeze({
  ...defaults,
  tubeDesignerProfileOverrides: Object.freeze({
    doorLeafFrame: Object.freeze(importedProfile(33, 48)),
    doorFrame: Object.freeze(importedProfile(36, 40, "parametric-package")),
  }),
});
assert.deepEqual(securityWindowOpeningDimensions(imported), {
  width: 800, height: 1000, leafDepth: 48, fixedWidth: 36, hardware: 10,
  fixedClearWidth: 858, outsideWidth: 930, outsideHeight: 1072,
});
assert.match(renderSecurityWindowReview(template, imported), /858 × 1000 mm/);
assert.match(renderSecurityWindowReview(template, imported), /窗扇厚度 48 mm/);
assert.doesNotMatch(renderSecurityWindowReview(template, imported), /830 × 1000 mm/);
for (const invalidDepth of [undefined, 0, -1, "", malicious]) {
  const values = { ...imported, tubeDesignerProfileOverrides: { doorLeafFrame: importedProfile(33, invalidDepth) } };
  assert.equal(securityWindowOpeningDimensions(values).outsideWidth, null);
  assert.match(renderSecurityWindowReview(template, values), /待填写有效尺寸/);
  assert.doesNotMatch(renderSecurityWindowReview(template, values), /830 × 1000 mm|<img|NaN/);
}

// Exercise both production render entry points and draft changes.
const designer = { templates: [template] };
const newProduct = renderDesignerAddParameterContent(designer, {
  tubeDesignerAddTemplateId: template.id, tubeDesignerAddInstanceName: "新防盗窗",
});
assert.match(newProduct, /data-security-window-review/);
assert.match(newProduct, /830 × 1000 mm/);
const addMarkup = (draft) => renderDesignerAddParameterContent(designer, {
  tubeDesignerAddTemplateId: template.id, tubeDesignerAddDraft: draft,
});
assert.doesNotMatch(addMarkup({ accessDoorEnabled: false }), /class="notch"/);
assert.match(addMarkup({ accessDoorEnabled: false, frameJoinType: "v_groove_90:sharp_v" }), /class="notch"/);
assert.match(addMarkup(imported), /858 × 1000 mm/);
assert.doesNotMatch(addMarkup({ ...imported, tubeDesignerProfileOverrides: { doorFrame: importedProfile(0, 40) } }), /<polygon class="multi-face-door-frame"/);
const context = { mount: { querySelector() { return null; } } };
const parameters = Object.freeze({ doorClearWidth: 900, doorClearHeight: 1100 });
const view = { scene: { tubeDesigner: {
  ...designer,
  product: { entityId: "test-window", templateId: template.id, name: "防盗窗", parameters },
} } };
const existingProduct = renderDesignerRightPane(context, view);
assert.match(existingProduct, /data-security-window-review/);
assert.match(existingProduct, /930 × 1100 mm/);
assert.doesNotMatch(existingProduct, /830 × 1000 mm/);
view.tubeDesignerRightDraft = { doorUse: "maintenance", doorClearWidth: 300, doorClearHeight: 400 };
const updated = renderDesignerRightPane(context, view);
assert.match(updated, /330 × 400 mm/);
assert.match(updated, /检修口（非逃生窗）/);
assert.deepEqual(parameters, { doorClearWidth: 900, doorClearHeight: 1100 });

// Parse the real projected frame and verify geometric coordinates, not source text.
function projectedFrame(id, draft) {
  const source = templates.find((item) => item.id === id);
  const html = renderDesignerAddParameterContent({ templates: [source] }, {
    tubeDesignerAddTemplateId: id, tubeDesignerAddDraft: draft,
  });
  const match = html.match(/<polygon class="multi-face-door-frame" points="([^"]+)"/);
  assert.ok(match, `${id} must draw a parameterized opening`);
  return match[1].split(" ").map((point) => point.split(",").map(Number));
}
function assertPoint(actual, expected) {
  assert.equal(actual.length, expected.length);
  actual.forEach((value, index) => assert.ok(Math.abs(value - expected[index]) < 1e-9,
    `projected coordinate ${value} should equal ${expected[index]}`));
}
const projectedParameters = {
  ...defaults, doorClearWidth: 600, doorClearHeight: 900, doorFrameWidth: 30,
  doorLeafFrameDepth: 24, doorHardwareClearance: 12,
  doorLeft: 100, doorBottom: 200, doorUOffset: 100, doorVOffset: 200,
  frontWidth: 1200, accessDoorFace: "front", frameLayout: "four_sides",
};
const schematicWidth = 62 * (1200 / 1800);
const schematicLeft = 50 - schematicWidth / 2;
const surfaces = [
  ["single-face-security-window", [schematicLeft, 92], [schematicLeft + schematicWidth, 92], [schematicLeft, 8]],
  ["two-face-security-window", [11, 89], [69, 89], [11, 25]],
  ["three-face-security-window", [28, 89], [72, 89], [28, 25]],
  ["five-face-security-window", [25, 92], [92, 92], [25, 23]],
];
const outerWidth = 600 + 24 + 12 + 2 * 30;
const outerHeight = 900 + 2 * 30;
for (const [id, origin, uEnd, vEnd] of surfaces) {
  const points = projectedFrame(id, projectedParameters);
  const project = (u, v) => [
    origin[0] + (uEnd[0] - origin[0]) * u / 1200 + (vEnd[0] - origin[0]) * v / 1800,
    origin[1] + (uEnd[1] - origin[1]) * u / 1200 + (vEnd[1] - origin[1]) * v / 1800,
  ];
  const expected = [project(100, 200), project(100 + outerWidth, 200),
    project(100 + outerWidth, 200 + outerHeight), project(100, 200 + outerHeight)];
  points.forEach((point, index) => assertPoint(point, expected[index]));
  const changed = projectedFrame(id, { ...projectedParameters, doorClearWidth: 700 });
  assertPoint(changed[1], project(200 + outerWidth, 200));
  assertPoint(changed[0], points[0]);
  const importedPoints = projectedFrame(id, { ...projectedParameters, tubeDesignerProfileOverrides: imported.tubeDesignerProfileOverrides });
  const importedWidth = 600 + 48 + 12 + 2 * 36;
  const importedHeight = 900 + 2 * 36;
  [project(100, 200), project(100 + importedWidth, 200),
    project(100 + importedWidth, 200 + importedHeight), project(100, 200 + importedHeight)]
    .forEach((point, index) => assertPoint(importedPoints[index], point));
}
await new Promise((resolve) => setTimeout(resolve, 0));
console.log("PASS security-window review: four shipped descriptors, real add/edit rendering, connections, enclosure boundaries, active-only V groove review, imported profile dimensions and projected geometry");
