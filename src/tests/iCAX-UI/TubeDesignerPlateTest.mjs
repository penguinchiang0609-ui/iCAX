import assert from "node:assert/strict";
import { buildProfileGroups, filterManufacturingParts, listManufacturingParts, renderNestingLeftPane,
  renderNestingRightPane, renderPartsViewportOverlay, handlePartsAreaAction } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { buildNestingRequest } from "../../apps/tube-designer/webpage/nestingWorkflow.mjs";
import { buildAutomaticDimensionReport } from "../../apps/tube-designer/webpage/partInspection.mjs";
import { isTubeNestingPart } from "../../apps/tube-designer/webpage/manufacturingParts.mjs";
import { buildPartCategories, renderDesignerBreakdownRows } from "../../apps/tube-designer/webpage/designerViews.mjs";

assert.ok(isTubeNestingPart({}));
for (const kind of ["plate", "accessory", "glass", "unknown", "", " ", null, 1]) {
  assert.equal(isTubeNestingPart({ properties: { "manufacturing.partKind": kind } }), false);
}
assert.equal(isTubeNestingPart({ properties: { "manufacturing.partKind": "tube", "manufacturing.materialCategory": "plate" } }), false);
assert.equal(isTubeNestingPart({ properties: { "manufacturing.plate": {} } }), false);

const tube = { entityId: "t", name: "横管", quantity: 1, length: 1200,
  profile: { kind: "rect", width: 40, depth: 20, wallThickness: 2, displayName: "矩形管" } };
const plate = { entityId: "p", name: "封板", quantity: 1, length: 600, status: "ready", properties: {
  "manufacturing.partKind": "plate", "manufacturing.materialCategory": "plate",
  "manufacturing.plate": { width: 300, height: 600, thickness: 2, areaMm2: 180000 } } };
const plateCategories = buildPartCategories([plate], "window");
assert.deepEqual(plateCategories[0].specifications, ["板件 300 × 600 × 2 mm"]);
assert.deepEqual(plateCategories[0].lengths, []);
const plateBreakdown = renderDesignerBreakdownRows({ manufacturingGroups: [
  { productEntityId: "window", name: "板窗", parts: [plate] },
] }, { tubeDesignerExpandedBreakdownCategoryIds: plateCategories.map((category) => category.id) });
assert.match(plateBreakdown, /板件 300 × 600 × 2 mm/);
assert.match(plateBreakdown, /data-tube-designer-part-row="p"/);
assert.doesNotMatch(plateBreakdown, /\*len\*/);
const groups = buildProfileGroups([tube, plate]);
assert.equal(groups.length, 1);
assert.deepEqual(groups[0].parts.map((part) => part.entityId), ["t"]);
assert.equal(buildProfileGroups([tube, plate], { includeNonTube: true }).length, 2);
assert.deepEqual(filterManufacturingParts([tube, plate], { filter: "plate" }), [plate]);
assert.ok(!filterManufacturingParts([tube, plate], { filter: "special" }).includes(plate));

const view = { scene: { tubeDesigner: { manufacturingGroups: [{ name: "窗", parts: [tube, plate] }],
  nestingSettings: { version: 2, parameters: { partGap: 2 },
    stocks: [{ profileKey: groups[0].key, rows: [{ id: "stock", length: 6000, quantity: -1 }] }] } } },
  tubeDesignerActivePartId: "p" };
assert.equal(listManufacturingParts(view.scene.tubeDesigner).length, 2);
assert.deepEqual(buildNestingRequest(view).parts.map((part) => part.partEntityId), ["t"]);
view.tubeDesignerSelectedPartIds = ["p"];
assert.throws(() => buildNestingRequest(view), /请先.*零件/);
const left = renderNestingLeftPane({}, view);
assert.match(left, /不参与管材排样/);
assert.match(left, /300 × 600 × 2 mm/);
assert.match(renderNestingRightPane({}, view), /板件尺寸/);
assert.match(renderPartsViewportOverlay({}, view), /可导出/);
assert.doesNotMatch(renderPartsViewportOverlay({}, view), /可排样/);

const ops = { renderProject() {} };
view.activeAreaId = "nesting";
await handlePartsAreaAction({}, view, "tube-designer-parts-select-filtered", {}, ops);
assert.deepEqual(view.tubeDesignerSelectedPartIds, ["t"]);
assert.match(renderNestingLeftPane({}, view), /管材已选 1 \/ 1 件/);
await handlePartsAreaAction({}, view, "tube-designer-parts-toggle-part",
  { checked: true, dataset: { tubeDesignerPartId: "p" } }, ops);
assert.deepEqual(view.tubeDesignerSelectedPartIds, ["t"]);
await handlePartsAreaAction({}, view, "tube-designer-parts-toggle-group",
  { checked: true, dataset: { tubeDesignerPartIds: "t p" } }, ops);
assert.deepEqual(view.tubeDesignerSelectedPartIds, ["t"]);
await handlePartsAreaAction({}, view, "tube-designer-parts-clear-filtered", {}, ops);
assert.deepEqual(view.tubeDesignerSelectedPartIds, []);
view.tubeDesignerPartFilter = "plate";
const plateOnly = renderNestingLeftPane({}, view);
assert.match(plateOnly, /data-cam-action="tube-designer-parts-select-filtered" disabled/);
assert.match(plateOnly, /data-cam-action="tube-designer-parts-clear-filtered" disabled/);
await handlePartsAreaAction({}, view, "tube-designer-parts-select-filtered", {}, ops);
assert.deepEqual(view.tubeDesignerSelectedPartIds, []);
view.activeAreaId = "parts";
await handlePartsAreaAction({}, view, "tube-designer-parts-select-filtered", {}, ops);
assert.deepEqual(view.tubeDesignerSelectedPartIds, ["p"], "The parts area must still allow plate selection for export");

const report = buildAutomaticDimensionReport({ ...plate, geometryMeasurement: {
  source: "final-brep", available: true, partKind: "plate", features: [],
  plate: { longSide: 600, shortSide: 300, thickness: 2, center: [0, 0, 0],
    axes: [[1, 0, 0], [0, 0, 1], [0, 1, 0]] } } });
assert.equal(report.annotations.length, 3);
assert.equal(report.plate.thickness, 2);
assert.match(report.profile, /板件 600 × 300 × 2 mm/);
console.log("Plate grouping, nesting exclusion, dimensions and display checks passed.");
