import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  productPrimaryDimensions,
  productParameterTargetsForSceneMember,
  productSceneMemberIds,
  renderProductInstanceThumbnail,
  renderProductParameterDiagram,
} from "../../apps/tube-designer/webpage/productParameterDiagram.mjs";
import { productParameterKind } from "../../apps/tube-designer/webpage/productParameterClassification.mjs";

function loadTemplate(directory) {
  return JSON.parse(readFileSync(new URL(
    `../../apps/tube-designer/templates/product/${directory}/template.json`,
    import.meta.url,
  ), "utf8"));
}

function defaults(template, overrides = {}) {
  return {
    ...Object.fromEntries(template.parameters.map((field) => [field.key, field.defaultValue])),
    ...overrides,
  };
}

function occurrences(markup, pattern) {
  return [...markup.matchAll(pattern)].length;
}

function assertDiagramDependenciesAreProductSpecifications(template) {
  const rootGroups = new Set(template.extensions.parameterLayout.sections
    .find((section) => section.key === "product").groups);
  const productGroups = new Set(rootGroups);
  let changed = true;
  while (changed) {
    changed = false;
    for (const group of template.groups) {
      if (group.parentKey && productGroups.has(group.parentKey) && !productGroups.has(group.key)) {
        productGroups.add(group.key);
        changed = true;
      }
    }
  }
  const bindings = Object.values(template.extensions.productDiagram.bindings).flat();
  const fields = new Map(template.parameters.map((field) => [field.key, field]));
  assert.deepEqual(bindings.filter((key) => !productGroups.has(fields.get(key)?.group)), [],
    `${template.id}: diagrams may only depend on product specification parameters`);
  assert.ok(bindings.some((key) => productParameterKind(fields.get(key), template) === "structure"));
  assert.ok(bindings.some((key) => productParameterKind(fields.get(key), template) === "dimension"));
}

const securityWindow = loadTemplate("single_face_security_window");
assertDiagramDependenciesAreProductSpecifications(securityWindow);
const singleValues = defaults(securityWindow, { faceType: "single", accessDoorEnabled: true });
const single = renderProductParameterDiagram(securityWindow, singleValues, { mode: "add" });
assert.match(single, /单面防盗窗结构与尺寸示意/);
assert.equal(occurrences(single, /product-diagram-face-side/g), 0);
assert.equal(occurrences(single, /product-diagram-door/g), 1);
assert.deepEqual(productPrimaryDimensions(securityWindow, singleValues).map((item) => item.parameter), ["width", "height"]);
const singleThumbnail = renderProductInstanceThumbnail(securityWindow, singleValues);
const threeThumbnail = renderProductInstanceThumbnail(securityWindow, defaults(securityWindow, {
  faceType: "three", leftWidth: 450, rightWidth: 750, accessDoorEnabled: true,
}));
assert.match(singleThumbnail, /单面防盗窗结构与尺寸示意/);
assert.match(threeThumbnail, /三面防盗窗结构与尺寸示意/);
assert.equal(occurrences(singleThumbnail, /product-diagram-face-side/g), 0);
assert.equal(occurrences(threeThumbnail, /product-diagram-face-side/g), 2,
  "instance thumbnails must be drawn from each instance's committed structure parameters");
assert.doesNotMatch(singleThumbnail, /product-diagram-measure/,
  "compact instance thumbnails must not include dimension annotations");

const double = renderProductParameterDiagram(securityWindow, defaults(securityWindow, {
  faceType: "two", sideWidth: 600,
}), { mode: "add", activeParameter: "sideWidth" });
assert.match(double, /data-product-diagram-parameter="sideWidth"[^>]*is-active|is-active[^>]*data-product-diagram-parameter="sideWidth"/);
assert.match(double, /product-diagram-depth-line/);
assert.match(double, />600 mm<\/text>/);

const threeValues = defaults(securityWindow, {
  faceType: "three", leftWidth: 450, rightWidth: 750,
  accessDoorEnabled: false, verticalMaximumCenterSpacing: 180, sideVerticalMaximumCenterSpacing: 160,
});
const three = renderProductParameterDiagram(securityWindow, threeValues, { mode: "add", activeParameter: "leftWidth" });
assert.match(three, /三面防盗窗结构与尺寸示意/);
assert.equal(occurrences(three, /product-diagram-face-side/g), 2);
assert.equal(occurrences(three, /product-diagram-door/g), 0);
assert.match(three, /data-tube-designer-parameter-key="leftWidth"/);
assert.match(three, /第|左侧外飘深度/);
assert.deepEqual(productPrimaryDimensions(securityWindow, threeValues).map((item) => item.parameter), [
  "width", "height", "leftWidth", "rightWidth",
]);

const threeOpeningLeft = renderProductParameterDiagram(securityWindow, defaults(securityWindow, {
  faceType: "three", leftWidth: 450, rightWidth: 750,
  accessDoorEnabled: true, accessDoorFace3: "left",
}), { mode: "add", activeParameter: "accessDoorFace3" });
const threeOpeningRight = renderProductParameterDiagram(securityWindow, defaults(securityWindow, {
  faceType: "three", leftWidth: 450, rightWidth: 750,
  accessDoorEnabled: true, accessDoorFace3: "right",
}), { mode: "add" });
assert.match(threeOpeningLeft, /data-product-diagram-parameter="[^"]*accessDoorFace3[^"]*"[^>]*is-active|is-active[^>]*data-product-diagram-parameter="[^"]*accessDoorFace3/,
  "the opening-face selector must highlight its corresponding opening in the diagram");
assert.notEqual(
  threeOpeningLeft.match(/product-diagram-door" points="([^"]+)/)?.[1],
  threeOpeningRight.match(/product-diagram-door" points="([^"]+)/)?.[1],
  "changing the opening face must move the opening to the selected side of the diagram",
);
assert.deepEqual(securityWindow.parameters.find((field) => field.key === "accessDoorFace2").choices.map(({ value }) => value), ["front", "side"]);
assert.deepEqual(securityWindow.parameters.find((field) => field.key === "accessDoorFace3").choices.map(({ value }) => value), ["front", "left", "right"]);
assert.deepEqual(securityWindow.parameters.find((field) => field.key === "accessDoorFace5").choices.map(({ value }) => value), ["front", "left", "right", "bottom"]);

const fiveOpeningBottom = renderProductParameterDiagram(securityWindow, defaults(securityWindow, {
  faceType: "five", depth: 1600, accessDoorEnabled: true, accessDoorFace5: "bottom",
}), { mode: "add", activeParameter: "accessDoorFace5" });
const fiveOpeningFront = renderProductParameterDiagram(securityWindow, defaults(securityWindow, {
  faceType: "five", depth: 1600, accessDoorEnabled: true, accessDoorFace5: "front",
}), { mode: "add" });
assert.match(fiveOpeningBottom, /data-product-diagram-parameter="[^"]*accessDoorFace5[^"]*"[^>]*is-active|is-active[^>]*data-product-diagram-parameter="[^"]*accessDoorFace5/);
assert.notEqual(
  fiveOpeningBottom.match(/product-diagram-door" points="([^"]+)/)?.[1],
  fiveOpeningFront.match(/product-diagram-door" points="([^"]+)/)?.[1],
  "a five-face bottom opening must be drawn on the bottom face rather than on the front",
);

const five = renderProductParameterDiagram(securityWindow, defaults(securityWindow, {
  faceType: "five", width: 1600, height: 1200, depth: 800,
}), { mode: "right" });
assert.match(five, /五面防盗窗结构与尺寸示意/);
assert.equal(occurrences(five, /product-diagram-face-cap/g), 2);
assert.deepEqual(productPrimaryDimensions(securityWindow, defaults(securityWindow, { faceType: "five" }))
  .map((item) => item.parameter), ["width", "height", "depth"]);

const wider = renderProductParameterDiagram(securityWindow, { ...singleValues, width: 2400 }, { mode: "add" });
assert.notEqual(single.match(/product-diagram-face-front" points="([^"]+)/)?.[1], wider.match(/product-diagram-face-front" points="([^"]+)/)?.[1],
  "changing product dimensions must change the drawn proportions");

const spacingGrid = renderProductParameterDiagram(securityWindow, defaults(securityWindow, {
  faceType: "five",
  horizontalMaximumCenterSpacing: 420,
  verticalMaximumCenterSpacing: 150,
  topBottomRodMaximumCenterSpacing: 180,
  topBottomCrossbarMaximumCenterSpacing: 260,
  accessDoorEnabled: true,
  doorHorizontalTopCenterOffset: 120, doorHorizontalBottomCenterOffset: 120,
  doorHorizontalMaximumCenterSpacing: 220,
  doorVerticalLeftCenterOffset: 80, doorVerticalRightCenterOffset: 80,
  doorVerticalMaximumCenterSpacing: 160,
}), { mode: "add", activeParameter: "topBottomRodMaximumCenterSpacing" });
assert.match(spacingGrid, /data-product-diagram-parameter="[^"]*topBottomRodMaximumCenterSpacing[^"]*"[^>]*is-active|is-active[^>]*data-product-diagram-parameter="[^"]*topBottomRodMaximumCenterSpacing/);
assert.match(spacingGrid, /data-product-diagram-parameter="doorVerticalLeftCenterOffset doorVerticalRightCenterOffset doorVerticalMaximumCenterSpacing"/);
assert.match(spacingGrid, /data-product-diagram-parameter="doorHorizontalTopCenterOffset doorHorizontalBottomCenterOffset doorHorizontalMaximumCenterSpacing"/);
const automaticGrid = renderProductParameterDiagram(securityWindow, defaults(securityWindow, {
  faceType: "single", verticalMaximumCenterSpacing: 80,
}), { mode: "add", activeParameter: "verticalMaximumCenterSpacing" });
assert.match(automaticGrid, /data-product-diagram-parameter="[^"]*verticalMaximumCenterSpacing[^"]*"[^>]*is-active|is-active[^>]*data-product-diagram-parameter="[^"]*verticalMaximumCenterSpacing/);

const profileLinked = renderProductParameterDiagram(securityWindow, defaults(securityWindow), {
  mode: "add", activeParameter: "horizontalWidth",
});
assert.match(profileLinked, /data-product-diagram-parameter="[^"]*horizontalWidth[^"]*"[^>]*is-active|is-active[^>]*data-product-diagram-parameter="[^"]*horizontalWidth/,
  "editing a horizontal material field must highlight all horizontal diagram bars");
assert.match(profileLinked, /data-product-diagram-profile-role="horizontal"/);
assert.match(profileLinked, /--product-profile-stroke:/,
  "the declared profile dimensions must set the visual member thickness");
const frameProfileLinked = renderProductParameterDiagram(securityWindow, defaults(securityWindow), {
  mode: "add", activeParameter: "frameWallThickness",
});
assert.match(frameProfileLinked, /product-diagram-profile-frame[^>]*is-active|is-active[^>]*product-diagram-profile-frame/,
  "editing an outer-frame material field must highlight the frame outline");

const securitySceneMembers = [
  { entityId: "frame", role: "main", stableKey: "outer_frame" },
  { entityId: "horizontal", role: "main_grid.horizontal", stableKey: "main_grid.horizontal.1" },
  { entityId: "vertical", role: "main_grid.vertical", stableKey: "main_grid.vertical.1" },
  { entityId: "door", role: "access_door.leaf.horizontal", stableKey: "access_door.leaf.horizontal.1" },
];
assert.deepEqual(productSceneMemberIds(securityWindow, securitySceneMembers, "horizontalWidth"), ["horizontal"]);
assert.deepEqual(productSceneMemberIds(
  securityWindow,
  securitySceneMembers,
  "profile:horizontal:cornerRadius",
  { profileRole: "horizontal" },
), ["horizontal"], "an embedded profile parameter must use its declared product profile role to highlight scene members");
assert.deepEqual(productSceneMemberIds(securityWindow, securitySceneMembers, "doorClearWidth"), ["door"]);
assert.deepEqual(productSceneMemberIds(securityWindow, securitySceneMembers, "accessDoorFace3"), ["door"]);
assert.deepEqual(productSceneMemberIds(securityWindow, securitySceneMembers, "faceType"), ["frame", "horizontal", "vertical", "door"]);
const horizontalTargets = productParameterTargetsForSceneMember(securityWindow, securitySceneMembers[1]);
assert.ok(horizontalTargets.profileRoles.includes("horizontal"));
assert.ok(horizontalTargets.parameters.includes("horizontalWidth"));
assert.ok(!horizontalTargets.parameters.includes("frameWidth"));

for (const directory of [
  "modular_guardrail",
  "modular_guardrail_cross-straight",
  "modular_guardrail_diamond-straight",
  "modular_guardrail_glass-straight",
]) {
  const template = loadTemplate(directory);
  assertDiagramDependenciesAreProductSpecifications(template);
  const straightValues = defaults(template, { layout: "straight", sideBayCount1: 3 });
  const straight = renderProductParameterDiagram(template, straightValues, { mode: "add" });
  assert.match(straight, /直式护栏结构与尺寸示意/);
  assert.deepEqual(productPrimaryDimensions(template, straightValues).map((item) => item.parameter), ["sideLength1", "guardHeight"]);
  assert.equal(occurrences(straight, /class="product-diagram-post"/g), 4);

  const uValues = defaults(template, {
    layout: "u", sideLength1: 3000, sideLength2: 1500, sideLength3: 900,
    sideBayCount1: 3, sideBayCount2: 2, sideBayCount3: 2,
  });
  const u = renderProductParameterDiagram(template, uValues, { mode: "right", activeParameter: "sideLength3" });
  assert.match(u, /U 型护栏结构与尺寸示意/);
  assert.match(u, /第3边 900 mm/);
  assert.equal(occurrences(u, /class="product-diagram-post"/g), 10);
  assert.deepEqual(productPrimaryDimensions(template, uValues).map((item) => item.parameter), [
    "sideLength1", "sideLength2", "sideLength3", "guardHeight",
  ]);
  const structuralHighlight = renderProductParameterDiagram(template, uValues, { mode: "right", activeParameter: "sideBayCount2" });
  assert.match(structuralHighlight, /data-product-diagram-parameter="sideBayCount2"[^>]*is-active|is-active[^>]*data-product-diagram-parameter="sideBayCount2"/);
}

const cross = renderProductParameterDiagram(loadTemplate("modular_guardrail_cross-straight"),
  defaults(loadTemplate("modular_guardrail_cross-straight")), { mode: "add" });
assert.ok(occurrences(cross, /class="product-diagram-infill"/g) >= 6, "X infill must be reflected by the diagram");
const glass = renderProductParameterDiagram(loadTemplate("modular_guardrail_glass-straight"),
  defaults(loadTemplate("modular_guardrail_glass-straight")), { mode: "add" });
assert.ok(occurrences(glass, /class="product-diagram-glass"/g) >= 3, "panel infill must be reflected by the diagram");

const guardrail = loadTemplate("modular_guardrail");
const guardrailSceneMembers = [
  { entityId: "handrail", role: "guardrail.handrail", stableKey: "segment.1.cap.1" },
  { entityId: "post", role: "guardrail.post", stableKey: "post.1" },
  { entityId: "rail", role: "guardrail.cross_rail", stableKey: "segment.1.bay.1.bottom" },
  { entityId: "bar", role: "guardrail.vertical_bar", stableKey: "segment.1.bay.1.bar.001" },
];
assert.deepEqual(productSceneMemberIds(guardrail, guardrailSceneMembers, "railWidth"), ["rail"]);
assert.deepEqual(productSceneMemberIds(guardrail, guardrailSceneMembers, "infillType"), ["bar"]);
assert.deepEqual(productSceneMemberIds(guardrail, guardrailSceneMembers, "sideLength1"), ["handrail", "post", "rail", "bar"]);

const sceneBindingCases = [
  {
    directory: "aluminium_window",
    members: [
      { entityId: "frame", stableKey: "frame.left" },
      { entityId: "sash", stableKey: "aperture.1.1.sliding.1.left" },
      { entityId: "glass", stableKey: "aperture.1.1.sliding.1.glass" },
    ],
    parameter: "cell11",
    expected: ["sash", "glass"],
  },
  {
    directory: "decorative_door",
    members: [{ entityId: "leaf-1", stableKey: "leaf.1" }, { entityId: "leaf-2", stableKey: "leaf.2" }],
    parameter: "pattern",
    expected: ["leaf-1", "leaf-2"],
  },
  {
    directory: "louver_window",
    members: [
      { entityId: "frame", stableKey: "frame.left" },
      { entityId: "support", stableKey: "post.1" },
      { entityId: "blade", stableKey: "blade.1" },
    ],
    parameter: "bladeWidth",
    expected: ["blade"],
  },
  {
    directory: "minimal_protective_grille",
    members: [
      { entityId: "frame", stableKey: "frame.left.0001" },
      { entityId: "bar", stableKey: "inner.bar.0001" },
    ],
    parameter: "innerWidth",
    expected: ["bar"],
  },
  {
    directory: "straight_steel_staircase",
    members: [
      { entityId: "beam", stableKey: "flight.1.beam.1" },
      { entityId: "tread", stableKey: "flight.1.tread.1.deck" },
      { entityId: "post", stableKey: "flight.1.guard.left.post.1" },
    ],
    parameter: "stringerWidth",
    expected: ["beam"],
  },
];
for (const testCase of sceneBindingCases) {
  const template = loadTemplate(testCase.directory);
  assert.ok(template.extensions.sceneParameterBindings,
    `${template.id}: every shipped product template must declare scene parameter bindings`);
  assert.deepEqual(productSceneMemberIds(template, testCase.members, testCase.parameter), testCase.expected,
    `${template.id}: ${testCase.parameter} must resolve through template-owned member declarations`);
  for (const role of Object.values(template.extensions.sceneParameterBindings.profileRoles ?? {})) {
    for (const parameter of role.parameters ?? []) {
      assert.ok(template.parameters.some((field) => field.key === parameter),
        `${template.id}: profile role references unknown parameter ${parameter}`);
    }
  }
}

console.log("Product diagrams and template-declared scene parameter bindings cover every shipped product template.");
