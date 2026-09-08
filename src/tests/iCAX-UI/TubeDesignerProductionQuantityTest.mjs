import assert from "node:assert/strict";
import { handleDesignerAreaAction } from "../../apps/tube-designer/webpage/designerActions.mjs";
import { renderDesignerAddParameterContent, renderDesignerRightPane, renderDesignerLeftPane } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { handlePartsAreaAction, listNestingParts, renderNestingLeftPane, buildProfileGroups } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { buildNestingRequest, restoreSavedNestingTask } from "../../apps/tube-designer/webpage/nestingWorkflow.mjs";

const template = { id: "test", name: "测试产品", available: true, parameters: [] };
const profile = { id: "rect", kind: "rect", width: 40, depth: 20, wallThickness: 2 };
const source = { entityId: "source", name: "横杆", quantity: 6, unitQuantity: 2, instanceQuantity: 3, length: 1000, profile };
const frozen = { ...source, entityId: "copy", quantity: 6, instanceQuantity: 1 };
const designer = {
  templates: [template], product: { entityId: "product", name: "窗", templateId: "test", quantity: 3, parameters: {} },
  instances: [{ entityId: "product", name: "窗", templateId: "test", quantity: 3 }],
  manufacturingGroups: [{ productEntityId: "product", name: "窗", generationRunId: "source-run", parts: [source] }],
  nestingGroups: [],
};
const ops = { renderProject() {}, showNotice() {} };
const addView = { scene: { tubeDesigner: structuredClone(designer) }, tubeDesignerAddTemplateId: "test" };
assert.match(renderDesignerAddParameterContent(designer, addView), /value="1"[\s\S]*data-tube-designer-instance-quantity="add"/);
await handleDesignerAreaAction({}, addView, "tube-designer-instance-quantity-change", { value: "3", dataset: { tubeDesignerInstanceQuantity: "add" } }, ops);
assert.equal(addView.tubeDesignerAddInstanceQuantity, "3");
assert.match(renderDesignerAddParameterContent(designer, addView), /value="3"[\s\S]*data-tube-designer-instance-quantity="add"/);
for (const value of ["", "0", "-1", "1.5", "1000001", "NaN"]) {
  await assert.rejects(handleDesignerAreaAction({}, addView, "tube-designer-instance-quantity-change", { value, dataset: { tubeDesignerInstanceQuantity: "add" } }, ops), /整数/);
}
assert.match(renderDesignerLeftPane({}, addView), /数量 3/);
assert.match(renderDesignerRightPane({}, addView), /data-tube-designer-instance-quantity="right"/);

const view = { scene: { tubeDesigner: structuredClone(designer) }, tubeDesignerSelectedPartIds: ["source"], activeAreaId: "product" };
const calls = [];
let selectedRibbonTab = "";
const context = { activeRibbonTabId: "product", actions: { async selectRibbonTab(id) { selectedRibbonTab = id; } }, sceneProxy: { async invoke(method, payload) {
  calls.push({ method, payload });
  const result = structuredClone(view.scene.tubeDesigner);
  if (method === "TubeDesigner.StageNestingParts") {
    result.nestingGroups = [{ productEntityId: "batch", generationRunId: "batch", name: "窗", parts: [frozen] }];
    result.nestingTask = { revision: "stage", parts: [{ partEntityId: "copy", generationRunId: "batch" }], request: {}, result: {} };
    return { tubeDesigner: result, staged: true, partEntityIds: ["copy"] };
  }
  if (method === "TubeDesigner.SetInstanceQuantity") {
    result.product.quantity = payload.quantity;
    result.manufacturingGroups[0].parts[0].quantity = 2 * payload.quantity;
  } else if (method === "TubeDesigner.DeleteNestingParts") {
    result.nestingGroups = [];
    result.nestingTask = { revision: "deleted", parts: [], request: {}, result: {} };
  } else throw new Error(method);
  return { tubeDesigner: result };
} } };
await handlePartsAreaAction(context, view, "tube-designer-parts-open-nesting", {}, ops);
assert.equal(view.activeAreaId, "nesting");
assert.equal(context.activeRibbonTabId, "nesting");
assert.equal(selectedRibbonTab, "nesting");
assert.deepEqual(calls[0].payload.partEntityIds, ["source"]);
assert.deepEqual(view.tubeDesignerSelectedPartIds, ["source"]);
assert.deepEqual(view.tubeDesignerNestingSelectedPartIds, ["copy"]);
assert.equal(listNestingParts(view.scene.tubeDesigner)[0].quantity, 6);
const key = buildProfileGroups([frozen])[0].key;
view.scene.tubeDesigner.nestingSettings = { version: 2, stocks: [{ profileKey: key, rows: [{ id: "stock", length: 6000, quantity: -1 }] }], parameters: { partGap: 0 } };
const request = buildNestingRequest(view);
assert.equal(request.parts[0].quantity, 6);
assert.equal(request.parts[0].partEntityId, "copy");
await handleDesignerAreaAction(context, view, "tube-designer-instance-quantity-change", { value: "2", dataset: { tubeDesignerInstanceQuantity: "right" } }, ops);
assert.equal(view.scene.tubeDesigner.product.quantity, 2);
assert.equal(view.scene.tubeDesigner.manufacturingGroups[0].parts[0].quantity, 4);
assert.equal(listNestingParts(view.scene.tubeDesigner)[0].quantity, 6);
assert.deepEqual(buildNestingRequest(view), request);
const originalParts = structuredClone(view.scene.tubeDesigner.manufacturingGroups);
await handlePartsAreaAction(context, view, "tube-designer-delete-nesting-parts", {}, ops);
assert.deepEqual(calls.at(-1).payload.partEntityIds, ["copy"]);
assert.deepEqual(view.scene.tubeDesigner.manufacturingGroups, originalParts);
assert.equal(view.scene.tubeDesigner.product.quantity, 2);
assert.equal(listNestingParts(view.scene.tubeDesigner).length, 0);
assert.match(renderNestingLeftPane({}, view), /还没有零件/);

// Independent parts stay available even with no product instance or source group.
view.scene.tubeDesigner = { nestingGroups: [{ generationRunId: "batch", parts: [frozen] }],
  nestingTask: { revision: "reopened", parts: [{ partEntityId: "copy", generationRunId: "batch" }] } };
restoreSavedNestingTask(view);
assert.deepEqual(view.tubeDesignerNestingSelectedPartIds, ["copy"]);
assert.equal(listNestingParts(view.scene.tubeDesigner)[0].quantity, 6);
assert.match(renderNestingLeftPane({}, view), /删除所选/);
console.log("Production quantity and independent nesting UI regressions passed");
