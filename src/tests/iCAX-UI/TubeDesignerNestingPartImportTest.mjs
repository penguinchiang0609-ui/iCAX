import assert from "node:assert/strict";
import {
  handleNestingPartImportAction,
  handleNestingPartImportRibbonCommand,
  renderNestingPartImportDialog,
} from "../../apps/tube-designer/webpage/nestingPartImport.mjs";
import { handlePartsAreaAction, renderNestingLeftPane, renderNestingRightPane } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { buildAutomaticDimensionReport } from "../../apps/tube-designer/webpage/partInspection.mjs";
import { ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";

const profile = {
  id: "imported-rect-40-20-1.5", kind: "rect", displayName: "矩形管",
  specification: "40 × 20 × 壁厚 1.5 mm", width: 40, depth: 20, wallThickness: 1.5, hollow: true,
};
const part = {
  entityId: "part-1", generationRunId: "batch-1", index: 1, partNumber: "横杆-1",
  name: "横杆", role: "导入零件", quantity: 2, profile, partKind: "tube", length: 1200,
  properties: {
    "tubeDesigner.profile": profile, "manufacturing.partKind": "tube",
    "manufacturing.materialCategory": "tube", "manufacturing.sourcing": "made",
    "manufacturing.process": "straight-cut", "manufacturing.requiresBending": false,
    "manufacturing.imported": true, "manufacturing.import.sourceFileName": "横杆.IGES",
    "manufacturing.geometryMeasurement": { openingCount: 0 },
    "manufacturing.material": "Q235B",
  },
};

const view = { activeAreaId: "nesting", pending: false, scene: { tubeDesigner: { nestingGroups: [] } } };
const calls = [];
const notices = [];
const context = {
  appProxy: { bridge: { async openFileDialog(options) { calls.push({ method: "openFileDialog", options }); return "D:\\cad\\横杆.IGES"; } } },
  sceneProxy: { async invoke(method, payload) {
    calls.push({ method, payload });
    return {
      partEntityId: "part-1", profile, sourceFileName: "横杆.IGES",
      recognition: { available: true, length: 1200, section: profile, features: [] },
      tubeDesigner: {
        nestingGroups: [{ productEntityId: "batch-1", generationRunId: "batch-1", name: "导入零件", parts: [part] }],
        nestingTask: { revision: "r1", parts: [{ partEntityId: "part-1", generationRunId: "batch-1" }] },
        nestingSettings: {},
      },
    };
  } },
};
const ops = { renderProject() {}, showNotice(_context, _view, text) { notices.push(text); } };

const nestingTab = ribbonDefinition.tabs.find((tab) => tab.id === "nesting");
assert.deepEqual(nestingTab.groups.map((group) => group.title), ["零件", "母材", "排样"]);
assert.ok(nestingTab.groups.flatMap((group) => group.commands)
  .some((command) => command.id === "nesting.import-part"));
assert.ok(nestingTab.groups.flatMap((group) => group.commands)
  .every((command) => !command.menuItems?.length));
assert.match(renderNestingLeftPane({}, view), /“零件”菜单.*导入 STEP \/ IGES/);

await handleNestingPartImportRibbonCommand(context, view, "nesting.import-part", ops);
assert.deepEqual(calls[0].options.filters[0].extensions, ["step", "stp", "iges", "igs"]);
assert.match(renderNestingPartImportDialog(view), /识别主方向、成品长度、截面形状与尺寸/);

await handleNestingPartImportAction(context, view, "tube-designer-nesting-import-draft", {
  dataset: { tubeDesignerNestingImportField: "material" }, value: "Q235B",
}, ops);
await handleNestingPartImportAction(context, view, "tube-designer-nesting-import-confirm", {}, ops);
assert.equal(calls[1].method, "TubeDesigner.ImportNestingPart");
assert.equal(calls[1].payload.sourcePath, "D:\\cad\\横杆.IGES");
assert.equal(calls[1].payload.material, "Q235B");
assert.deepEqual(view.tubeDesignerNestingSelectedPartIds, ["part-1"]);
assert.equal(view.tubeDesignerActiveNestingPartId, "part-1");
assert.equal(view.pending, false);
assert.match(notices[0], /40 × 20/);
assert.match(renderNestingRightPane({}, view), /data-tube-designer-part-field="material"/);

const geometryMeasurement = {
  available: true,
  source: "final-brep",
  length: 1200,
  linearReference: {
    start: [0, 0, 0], end: [1200, 0, 0], dimensionOffsetDirection: [0, 0, 1],
    sectionAxes: [[0, 1, 0], [0, 0, 1]],
  },
  section: { kind: "rect", width: 40, height: 20, wallThickness: 1.5 },
  features: [{
    kind: "through-opening", shape: "circle", diameter: 10, station: 300,
    center: [300, 0, 0], openingSpanAlong: 10, openingSpanAcross: 10,
    centerToFaceEdgeNegative: 5, centerToFaceEdgePositive: 15,
    faceEdgeClearanceNegative: 0, faceEdgeClearancePositive: 10,
  }],
};
view.scene.tubeDesigner.nestingGroups[0].parts[0].properties["manufacturing.geometryMeasurement"] = geometryMeasurement;
view.tubeDesignerPartMeasurementState = {
  key: "part-1@0", status: "ready", report: buildAutomaticDimensionReport({ geometryMeasurement }),
};
const detailHtml = renderNestingRightPane({}, view);
assert.match(detailHtml, /自动复尺/);
assert.match(detailHtml, /中心距首端/);
assert.match(detailHtml, /⌀10/);

const selectionView = {
  ...view,
  tubeDesignerNestingSelectionKind: "plan",
  tubeDesignerActiveNestingPlanId: "plan-1",
  tubeDesignerActiveNestingPartId: "part-1",
  tubeDesignerNestingResult: { plans: [{ id: "plan-1", placements: [{ partId: "part-1" }] }] },
};
selectionView.tubeDesignerNestingContextMenu = { partId: "part-1", x: 24, y: 48 };
assert.match(renderNestingLeftPane({}, selectionView), /查看所在排样/);
await handlePartsAreaAction({}, selectionView, "tube-designer-nesting-view-part-locations", {
  dataset: { tubeDesignerPartId: "part-1" },
}, { renderProject() {} });
assert.equal(selectionView.tubeDesignerActiveNestingPlanId, "plan-1");
assert.equal(selectionView.tubeDesignerActiveNestingPlacementId, "part-1#1");
assert.equal(selectionView.tubeDesignerNestingSelectionKind, "plan");

selectionView.tubeDesignerNestingSelectionKind = "plan";
await handlePartsAreaAction({}, selectionView, "tube-designer-parts-select-part", {
  dataset: { tubeDesignerPartId: "part-1" },
}, { renderProject() {} });
assert.equal(selectionView.tubeDesignerNestingSelectionKind, "part");
assert.equal(selectionView.tubeDesignerActiveNestingPlacementId, "");

console.log("TubeDesignerNestingPartImportTest: passed");
