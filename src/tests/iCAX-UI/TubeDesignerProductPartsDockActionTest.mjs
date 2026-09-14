import assert from "node:assert/strict";
import { handleDesignerAreaAction, handleDesignerRibbonCommand } from "../../apps/tube-designer/webpage/designerActions.mjs";

const part = {
  entityId: "part-1",
  sourceMemberId: "scene-member-1",
  name: "外框立柱",
};
const view = {
  pending: false,
  scene: { tubeDesigner: {
    product: { entityId: "product-1", templateId: "single-face-security-window" },
    manufacturingGroups: [{ productEntityId: "product-1", parts: [part] }],
  } },
};
const calls = [];
view.viewport = {
  setSelectedObjectIds(ids, primary) { calls.push({ ids, primary }); },
  setContinuousRendering(value) { calls.push({ continuous: value }); },
};
const context = { mount: { querySelectorAll() { return []; } } };
let renders = 0;
const ops = { renderProject() { renders += 1; } };
const target = { dataset: { tubeDesignerPartId: "part-1" } };

const selection = await handleDesignerAreaAction(
  context, view, "tube-designer-select-product-part", target, ops,
);
assert.equal(selection.handled, true);
assert.equal(view.tubeDesignerActivePartId, "part-1");
assert.equal(view.selectedSceneObjectId, "scene-member-1");
assert.deepEqual(calls, [{ ids: ["scene-member-1"], primary: "scene-member-1" }]);
assert.equal(renders, 0, "Selecting a row must preserve the parameter editor DOM.");

const inspection = await handleDesignerAreaAction(
  context, view, "tube-designer-open-part-inspection", target, ops,
);
assert.equal(inspection.handled, true);
assert.equal(view.tubeDesignerInspectedPartId, "part-1");
assert.equal(view.tubeDesignerPartInspectionOpen, true);
assert.equal(renders, 1, "The scene dock must be able to open part inspection without a breakdown dialog.");
assert.deepEqual(calls.at(-2), { ids: ["scene-member-1"], primary: "scene-member-1" });
assert.deepEqual(calls.at(-1), { continuous: false });

view.tubeDesignerPartInspectionOpen = false;
const ribbonInspection = await handleDesignerRibbonCommand(
  context, view, "designer.inspect-active-part", ops,
);
assert.equal(ribbonInspection, true);
assert.equal(view.tubeDesignerInspectedPartId, "part-1");
assert.equal(view.tubeDesignerPartInspectionOpen, true);
assert.deepEqual(calls.at(-2), { ids: ["scene-member-1"], primary: "scene-member-1" });
assert.deepEqual(calls.at(-1), { continuous: false });

console.log("Product-scene parts select their source assembly member and open inspection from the dock or ribbon.");
