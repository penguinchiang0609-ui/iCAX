import assert from "node:assert/strict";
import {
  handleNestingPartImportAction,
  handleNestingPartImportRibbonCommand,
  renderNestingPartImportDialog,
} from "../../apps/tube-designer/webpage/nestingPartImport.mjs";
import {
  handleDesignerAreaAction,
  handleDesignerRibbonCommand,
} from "../../apps/tube-designer/webpage/designerActions.mjs";
import { renderDesignerDialogs } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { handlePartsAreaAction, renderNestingLeftPane, renderNestingRightPane } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { buildAutomaticDimensionReport } from "../../apps/tube-designer/webpage/partInspection.mjs";
import { ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";

const profile = {
  id: "imported-rect-40-20-1.5", kind: "rect", displayName: "矩形管",
  specification: "40 × 20 × 壁厚 1.5 mm", width: 40, depth: 20, wallThickness: 1.5, hollow: true,
};
const part = {
  entityId: "part-1", generationRunId: "batch-1", index: 1, partNumber: "横杆-1",
  name: "横杆", role: "导入零件", quantity: 2,
  profile, partKind: "tube", length: 1200,
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
assert.equal(nestingTab.groups[0].commands[0].id, "nesting.add-from-products");
assert.equal(nestingTab.groups[0].commands[0].title, "从产品添加");
assert.ok(nestingTab.groups.flatMap((group) => group.commands)
  .some((command) => command.id === "nesting.import-part"));
assert.ok(nestingTab.groups.flatMap((group) => group.commands)
  .every((command) => !command.menuItems?.length));
assert.match(renderNestingLeftPane({}, view), /产品页完成拆单.*“零件”菜单批量从产品添加.*导入 STEP \/ IGES/);

const productInstances = Array.from({ length: 90 }, (_, index) => ({
  entityId: `product-${index + 1}`,
  name: `产品 ${index + 1}`,
  productCode: `P-${index + 1}`,
  quantity: 500,
  templateId: "bulk-product",
  parameters: { width: 1200 + index, height: 800 },
  hasDisassembly: index !== 1,
  partCount: index === 1 ? 0 : 1,
}));
const manufacturingGroups = productInstances
  .filter((instance) => instance.hasDisassembly)
  .map((instance) => ({
    productEntityId: instance.entityId,
    name: instance.name,
    parts: [{ entityId: `source-${instance.entityId}`, partKind: "tube" }],
  }));
const productSelectionView = {
  activeAreaId: "nesting",
  pending: false,
  scene: {
    tubeDesigner: {
      instances: productInstances,
      templates: [{ id: "bulk-product", name: "批量产品", displayName: "批量产品", available: true }],
      manufacturingGroups,
      nestingGroups: [{ generationRunId: "existing", parts: [{ entityId: "existing-part" }] }],
    },
  },
};
const productImportCalls = [];
const productImportContext = {
  activeRibbonTabId: "nesting",
  sceneProxy: { async invoke(method, payload) {
    productImportCalls.push({ method, payload });
    if (method === "TubeDesigner.StageNestingParts") {
      const stagedPartIds = payload.partEntityIds.map((partEntityId) => `staged-${partEntityId}`);
      return {
        stagedParts: payload.partEntityIds.map((sourcePartEntityId, index) => ({
          sourcePartEntityId,
          partEntityId: stagedPartIds[index],
          generationRunId: "bulk",
          quantity: 500,
          manufacturingGeometryResourceId: `resource-${index}`,
          manufacturingGeometryResourceVersion: 1,
        })),
        nestingTask: { parts: stagedPartIds.map((partEntityId) => ({ partEntityId, generationRunId: "bulk" })) },
        partEntityIds: stagedPartIds,
      };
    }
    throw new Error(`Unexpected call: ${method}`);
  } },
  actions: { async selectRibbonTab(areaId) { productSelectionView.activeAreaId = areaId; } },
};
const productImportNotices = [];
const productImportOps = { renderProject() {}, showNotice(_context, _view, text) { productImportNotices.push(text); } };

assert.equal(await handleDesignerRibbonCommand(
  productImportContext,
  productSelectionView,
  "nesting.add-from-products",
  productImportOps,
), true);
assert.equal(productSelectionView.tubeDesignerDisassemblySelectorOpen, true);
assert.equal(productSelectionView.tubeDesignerSelectedInstanceIds.length, 89);
const productSelectorHtml = renderDesignerDialogs(productSelectionView.scene.tubeDesigner, productSelectionView);
assert.match(productSelectorHtml, /选择已拆单产品/);
assert.match(productSelectorHtml, /这里不会重新拆单/);
assert.match(productSelectorHtml, /已选择 89 \/ 89 个已拆单实例/);
assert.match(productSelectorHtml, /1 个未拆单不可选/);
assert.match(productSelectorHtml, /未拆单，请先到产品页生成零件清单/);
assert.match(productSelectorHtml, /产品 80/);

await handleDesignerAreaAction(
  productImportContext,
  productSelectionView,
  "tube-designer-toggle-instance",
  { dataset: { tubeDesignerInstanceId: "product-2" }, checked: true },
  productImportOps,
);
assert.equal(productSelectionView.tubeDesignerSelectedInstanceIds.length, 89);
assert.equal(productSelectionView.tubeDesignerSelectedInstanceIds.includes("product-2"), false);

await handleDesignerAreaAction(
  productImportContext,
  productSelectionView,
  "tube-designer-toggle-instance",
  { dataset: { tubeDesignerInstanceId: "product-3" }, checked: false },
  productImportOps,
);
assert.equal(productSelectionView.tubeDesignerSelectedInstanceIds.length, 88);
assert.equal(productSelectionView.tubeDesignerSelectedInstanceIds.includes("product-3"), false);

await handleDesignerAreaAction(
  productImportContext,
  productSelectionView,
  "tube-designer-confirm-disassemble",
  {},
  productImportOps,
);
assert.deepEqual(productImportCalls.map(({ method }) => method), ["TubeDesigner.StageNestingParts"]);
assert.equal(productImportCalls[0].payload.partEntityIds.length, 88);
assert.equal(productImportCalls[0].payload.partEntityIds.length * 500, 44_000);
assert.equal(productImportCalls[0].payload.partEntityIds.includes("source-product-2"), false);
assert.equal(productImportCalls[0].payload.partEntityIds.includes("source-product-3"), false);
assert.equal(productSelectionView.tubeDesignerNestingSelectedPartIds.length, 88);
assert.equal(productSelectionView.scene.tubeDesigner.nestingGroups[0].parts[0].entityId, "existing-part");
assert.equal(productSelectionView.scene.tubeDesigner.nestingGroups[1].parts.length, 88);
assert.equal(productSelectionView.scene.tubeDesigner.manufacturingGroups, manufacturingGroups);
assert.equal(productSelectionView.activeAreaId, "nesting");
assert.match(productImportNotices[0], /已拆单产品导入/);

const noDisassemblyView = {
  activeAreaId: "nesting",
  pending: false,
  scene: { tubeDesigner: { instances: [productInstances[1]], manufacturingGroups: [] } },
};
assert.equal(await handleDesignerRibbonCommand(
  productImportContext,
  noDisassemblyView,
  "nesting.add-from-products",
  productImportOps,
), true);
assert.equal(noDisassemblyView.tubeDesignerDisassemblySelectorOpen, undefined);
assert.match(noDisassemblyView.error, /没有可导入的已拆单产品.*产品页生成零件清单/);

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
