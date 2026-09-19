import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  bindProductSpecificationAnnotations,
  resolveProductSpecificationAnnotationTree,
  resolveProductSpecificationAnnotations,
} from "../../apps/tube-designer/webpage/productParameterDiagram.mjs";

const packageRoot = new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/",
  import.meta.url,
);
const descriptor = JSON.parse(readFileSync(new URL("template.json", packageRoot), "utf8"));
const display = JSON.parse(readFileSync(new URL("display.json", packageRoot), "utf8"));
const template = { ...descriptor, display };
const defaults = Object.fromEntries(template.parameters.map((field) => [field.key, field.defaultValue]));
const anchors = [
  { id: "width", parameter: "width", start: [0, 0, 0], end: [1200, 0, 0], offset: [0, 0, -72] },
  { id: "height", parameter: "height", start: [0, 0, 0], end: [0, 0, 1800], offset: [-72, 0, 0] },
  { id: "side", parameter: "sideWidth", start: [0, 0, 0], end: [0, -600, 0], offset: [0, 0, -58] },
  { id: "horizontal", parameter: "horizontalMaximumCenterSpacing", start: [1200, 0, 38], end: [1200, 0, 1762], offset: [52, 0, 0] },
  { id: "vertical", parameter: "verticalMaximumCenterSpacing", start: [38, 0, 1762], end: [1162, 0, 1762], offset: [0, 0, 42] },
  { id: "door", parameter: "doorClearWidth", start: [300, 0, 400], end: [1100, 0, 400], offset: [0, 0, -38] },
  { id: "door-gap", parameter: "doorGap", start: [338, 0, 400], end: [344, 0, 400], offset: [0, 0, -68] },
  { id: "door-left", parameter: "doorLeft", start: [38, 0, 400], end: [300, 0, 400], offset: [0, 0, -84] },
];

assert.ok(!template.parameters.some((field) => ["doorHingeSide", "doorHingeCount"].includes(field.key)),
  "hinge side and count are fixed product details, not editable parameters");

function designer(parameters = {}, modelOutdated = false) {
  return {
    templates: [template],
    product: {
      entityId: "security-window-1",
      templateId: template.id,
      parameters: { ...defaults, ...parameters },
      modelOutdated,
    },
    specificationAnnotations: anchors,
  };
}

const single = resolveProductSpecificationAnnotations(designer({
  faceType: "single",
  accessDoorEnabled: true,
}), {});
assert.ok(single.some((item) => item.parameter === "width"));
assert.ok(single.some((item) => item.parameter === "height"));
assert.ok(single.some((item) => item.parameter === "horizontalMaximumCenterSpacing"));
assert.ok(single.some((item) => item.parameter === "verticalMaximumCenterSpacing"));
assert.ok(single.some((item) => item.parameter === "doorClearWidth"));
assert.ok(single.some((item) => item.parameter === "doorGap"),
  "escape-window leaf-to-frame gap must be editable as a scene specification");
assert.ok(!single.some((item) => item.parameter === "sideWidth"),
  "single-face windows must not show a hidden side-depth annotation");
assert.ok(single.every((item) => item.color === 0x27c27a),
  "specification annotations use green and stay separate from orange part selection");
assert.equal(single.find((item) => item.parameter === "width")?.groupKey, "overall");
assert.equal(single.find((item) => item.parameter === "horizontalMaximumCenterSpacing")?.groupKey, "main_grid");
assert.equal(single.find((item) => item.parameter === "doorClearWidth")?.groupKey, "door_size");
assert.equal(single.find((item) => item.parameter === "doorLeft")?.groupKey, "door_location");

const tree = resolveProductSpecificationAnnotationTree(designer({
  faceType: "single",
  accessDoorEnabled: true,
}), {});
assert.equal(tree.label, "规格标注");
assert.equal(tree.state, "all");
assert.ok(tree.children.some((item) => item.key === "overall" && item.label === "尺寸参数"));
assert.ok(tree.children.some((item) => item.key === "main_grid" && item.label === "主格栅"));
const escapeWindow = tree.children.find((item) => item.key === "section:escape_window");
assert.equal(escapeWindow?.label, "逃生窗");
assert.deepEqual(escapeWindow?.children.map((item) => item.key), ["door_size", "door_location"]);
assert.deepEqual(escapeWindow?.children.map((item) => item.label), ["开启口尺寸", "开启口位置"]);

const withoutDoor = resolveProductSpecificationAnnotations(designer({
  faceType: "single",
  accessDoorEnabled: false,
}), {});
assert.ok(!withoutDoor.some((item) => item.parameter === "doorClearWidth"));
assert.ok(!withoutDoor.some((item) => item.parameter === "doorGap"));

const pending = resolveProductSpecificationAnnotations(designer({
  faceType: "single",
  width: 1200,
}, true), {
  tubeDesignerRightDraft: { width: 1350 },
  tubeDesignerSceneSpecificationEditorParameter: "width",
});
const pendingWidth = pending.find((item) => item.parameter === "width");
assert.equal(pendingWidth.pending, true);
assert.equal(pendingWidth.color, 0xef5b55);
assert.equal(pendingWidth.oldValue, 1200);
assert.equal(pendingWidth.newValue, 1350);
assert.match(pendingWidth.label, /1,200 mm.*1,350 mm/);
assert.equal(pendingWidth.editing, true);
assert.deepEqual(pendingWidth.editor, {
  type: "number", value: 1350, min: 1, max: 20000, step: 1,
});

const freshWithStaleAnchor = resolveProductSpecificationAnnotations({
  ...designer({ faceType: "single", width: 1200 }, false),
  specificationAnnotations: anchors.map((anchor) => anchor.parameter === "width"
    ? { ...anchor, generatedValue: 900 } : anchor),
}, {});
const freshWidth = freshWithStaleAnchor.find((item) => item.parameter === "width");
assert.equal(freshWidth.pending, false, "a newly generated instance must not inherit a stale baseline");
assert.doesNotMatch(freshWidth.label, /→/);
assert.match(freshWidth.id, /^security-window-1:/,
  "annotation identity must be isolated by product instance");

const calls = [];
const view = {
  activeAreaId: "view",
  scene: { tubeDesigner: designer({ faceType: "single" }) },
  viewport: {
    setSpecificationAnnotations(items) { calls.push(["set", items]); },
    clearSpecificationAnnotations() { calls.push(["clear"]); },
  },
};
assert.ok(bindProductSpecificationAnnotations(null, view).length > 0);
assert.equal(calls.at(-1)[0], "set");
view.tubeDesignerSpecificationAnnotationGroupVisibility = { main_grid: false };
const withoutMainGrid = bindProductSpecificationAnnotations(null, view);
assert.ok(withoutMainGrid.some((item) => item.groupKey === "overall"));
assert.ok(!withoutMainGrid.some((item) => item.groupKey === "main_grid"));
assert.equal(resolveProductSpecificationAnnotationTree(view.scene.tubeDesigner, view).state, "mixed");
view.tubeDesignerSpecificationAnnotationsVisible = false;
assert.deepEqual(bindProductSpecificationAnnotations(null, view), []);
assert.equal(calls.at(-1)[0], "clear");

const generic = resolveProductSpecificationAnnotations({
  templates: [{ ...descriptor, id: "another-template" }],
  product: { entityId: "generic-product", templateId: "another-template", parameters: defaults },
  specificationAnnotations: [{
    id: "generic-width", parameter: "width", kind: "linear",
    start: [0, 0, 0], end: [1200, 0, 0], offset: [0, 0, -72],
    label: "通用产品宽度", editable: true,
  }],
}, {});
assert.equal(generic.length, 1, "模板自带锚点的产品必须可以显示规格标注");
assert.equal(generic[0].label, "通用产品宽度 1,200 mm");
assert.equal(generic[0].editable, true);

console.log("TubeDesigner 产品规格场景标注测试通过");
