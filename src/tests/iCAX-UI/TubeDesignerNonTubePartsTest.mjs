import assert from "node:assert/strict";
import { isPlatePart, isSheetPart, isComponentPart, isTubeNestingPart } from "../../apps/tube-designer/webpage/manufacturingParts.mjs";
import { buildAutomaticDimensionReport } from "../../apps/tube-designer/webpage/partInspection.mjs";
import { buildMaterialProfileGroups, buildProfileGroups, filterManufacturingParts, renderNestingLeftPane,
  renderNestingRightPane, renderPartsViewportOverlay, handlePartsAreaAction } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { buildNestingRequest } from "../../apps/tube-designer/webpage/nestingWorkflow.mjs";

const tube = { entityId: "tube", name: "横管", quantity: 2, length: 1234, status: "ready", profile: { kind: "rect", width: 40, depth: 40, wallThickness: 2 },
  properties: { "manufacturing.partKind": "tube", "manufacturing.sourcing": "made", "tubeDesigner.endProcess": { startCut: "square", endCut: "square" } } };
const glass = { entityId: "glass", name: "玻璃填充", quantity: 1, length: 9999, status: "ready", properties: {
  "manufacturing.partKind": "glass", "manufacturing.materialCategory": "glass", "manufacturing.sourcing": "purchased",
  "manufacturing.plate": { width: 600, height: 900, thickness: 8, areaMm2: 540000 },
} };
const accessory = { entityId: "accessory", name: "玻璃夹", quantity: 4, length: 9999, status: "ready", properties: {
  "manufacturing.partKind": "accessory", "manufacturing.sourcing": "purchased", "manufacturing.modelName": "玻璃固定夹",
  "manufacturing.modelBounds": { width: 30, depth: 24, height: 40 }, "manufacturing.modelReference": "system:glass-clamp",
} };
const boughtTube = { ...tube, entityId: "bought", properties: { ...tube.properties, "manufacturing.sourcing": "purchased" } };
const bentTube = { ...tube, entityId: "bent", properties: { ...tube.properties, "manufacturing.requiresBending": true } };
const curvedTube = { ...tube, entityId: "curved", properties: { ...tube.properties, "manufacturing.process": "curved-tube" } };
const parts = [tube, glass, accessory, boughtTube, bentTube, curvedTube];
assert.equal(isSheetPart(glass), true);
assert.equal(isPlatePart(glass), false, "Glass must not silently become plate");
assert.equal(isComponentPart(accessory), true);
for (const part of parts.slice(1)) assert.equal(isTubeNestingPart(part), false, part.entityId);
assert.equal(isTubeNestingPart(tube), true);
assert.equal(isTubeNestingPart({ ...tube, properties: { ...tube.properties, "tubeDesigner.cornerProcess": { grooveStyle: "v" } } }), true,
  "A continuous frame exported as an unfolded straight blank remains eligible for nesting");
assert.equal(isTubeNestingPart({ ...tube, properties: { ...tube.properties, "manufacturing.sourcing": null } }), false);
assert.equal(isTubeNestingPart({ ...tube, properties: { ...tube.properties, "manufacturing.requiresBending": "false" } }), false);
assert.deepEqual(filterManufacturingParts(parts, { filter: "straight" }), [tube]);
assert.deepEqual(filterManufacturingParts(parts, { filter: "glass" }), [glass]);
assert.deepEqual(filterManufacturingParts(parts, { filter: "accessory" }), [accessory]);
assert.deepEqual(filterManufacturingParts(parts, { filter: "non-tube" }), parts.slice(1));
assert.deepEqual(filterManufacturingParts(parts, { query: "外购" }).map((part) => part.entityId), ["glass", "accessory", "bought"]);
assert.equal(buildProfileGroups(parts).length, 1);
assert.equal(buildProfileGroups(parts, { includeNonTube: true }).length, 5, "Curved and bent tubes share an excluded section, never the eligible tube group");
assert.equal(buildMaterialProfileGroups(parts).reduce((sum, group) => sum + group.totalLength, 0), 2468);

const plateGeometry = { longSide: 900, shortSide: 600, thickness: 8, center: [0, 0, 0], axes: [[1, 0, 0], [0, 0, 1], [0, 1, 0]] };
const glassReport = buildAutomaticDimensionReport({ geometryMeasurement: { source: "final-brep", available: true, partKind: "glass", plate: plateGeometry, features: [] } });
assert.equal(glassReport.partKind, "glass");
assert.equal(glassReport.annotations.length, 3);
assert.match(glassReport.annotations[2].label, /玻璃厚度 8 mm/);
assert.match(glassReport.profile, /玻璃 900 × 600 × 8 mm/);
assert.doesNotMatch(glassReport.profile, /板件/);
const componentReport = buildAutomaticDimensionReport({ geometryMeasurement: { source: "final-brep", available: true, partKind: "accessory", bounds: { width: 30, depth: 24, height: 40 }, features: [] } });
assert.equal(componentReport.length, 0);
assert.deepEqual(componentReport.bounds, { width: 30, depth: 24, height: 40 });
assert.equal(componentReport.annotations.length, 0);
assert.equal(componentReport.holes.length, 0);
assert.equal(componentReport.hasLinearReference, false);

const group = buildProfileGroups(parts)[0];
const view = { activeAreaId: "nesting", scene: { tubeDesigner: { manufacturingGroups: [{ name: "混合护栏", parts }],
  nestingSettings: { version: 2, parameters: { partGap: 2 }, stocks: [{ profileKey: group.key, rows: [{ id: "s", length: 6000, quantity: -1 }] }] } } },
  tubeDesignerActivePartId: "accessory" };
const request = buildNestingRequest(view);
assert.deepEqual(request.parts.map((part) => part.partEntityId), ["tube"]);
const left = renderNestingLeftPane({}, view);
assert.match(left, /另行处理/);
assert.match(left, /玻璃 600 × 900 × 8 mm/);
assert.match(left, /玻璃固定夹 30 × 24 × 40 mm · 外购/);
assert.doesNotMatch(left, /9,999|9999/);
const right = renderNestingRightPane({}, view);
assert.match(right, /配件尺寸/);
assert.match(right, /供料方式/);
assert.doesNotMatch(right, /成品长度|起始端|结束端/);
view.tubeDesignerPartMeasurementState = { key: "accessory@0", status: "ready", report: componentReport };
const overlay = renderPartsViewportOverlay({}, view);
assert.match(overlay, /实体宽 X/);
assert.match(overlay, /可导出/);
assert.doesNotMatch(overlay, /可排样|可作为排样输入|成品长度|实体总长|9,999|9999/);
view.tubeDesignerActivePartId = "glass";
view.tubeDesignerPartMeasurementState = { key: "glass@0", status: "ready", report: glassReport };
const glassOverlay = renderPartsViewportOverlay({}, view);
assert.match(glassOverlay, /玻璃/);
assert.doesNotMatch(glassOverlay, /板件|可排样|可作为排样输入|9,999|9999/);
view.tubeDesignerSelectedPartIds = parts.map((part) => part.entityId);
await handlePartsAreaAction({}, view, "tube-designer-parts-select-filtered", {}, { renderProject() {} });
assert.deepEqual(view.tubeDesignerSelectedPartIds, ["tube"]);
console.log("Non-tube classification, glass/accessory geometry, manufacturing display and nesting isolation checks passed.");
