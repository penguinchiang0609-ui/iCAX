import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import {
  AppSDO,
  allocateResourceURL,
  connectApplication,
  ensureUsableChannelId,
  isUsableChannelId,
  ProductProxy,
  ProjectProxy,
  ResourceClient,
  SceneProxy,
  MockHostBridge,
  ProjectSDO,
  ViewClient,
  loadRenderResource,
  parseViewResource,
  parseRenderGeometryResource,
  parseRenderMaterialResource,
  loadProductModule,
  mountProductModule,
  makeResourceCollectionURL,
  parseResourceURL,
  resolveFrontendEntry,
  validateBridge,
} from "../../iCAX-UI/SDK/index.mjs";
import { SDOClient, SDOFrameKind } from "../../iCAX-UI/SDK/SDO/sdoClient.mjs";
import { makeSDOMethodCode, makeSDOMethodCodeFromName, makePDOID } from "../../iCAX-UI/SDK/SDO/sdoMethod.mjs";
import { deserializeVariantText, serializeVariantText } from "../../iCAX-UI/SDK/SDO/variantSerializer.mjs";
import { PDOClient } from "../../iCAX-UI/SDK/PDO/pdoClient.mjs";
import { resolveStandardViewDirection } from "../../iCAX-UI/SDK/Viewport/threeViewport.mjs";
import { renderMachineRightPane } from "../../apps/_shared/workbench/machine/machineArea.mjs";
import { activateProjectArea, getProjectArea, setProjectAreaViewContent } from "../../apps/_shared/workbench/state/projectViewStore.mjs";
import { loadPreviewMeshResource } from "../../apps/tube-one/webpage/previewMeshResource.mjs";
import { projectThumbnailVertices } from "../../apps/tube-designer/webpage/partThumbnail.mjs";
import {
  buildAutomaticDimensionReport,
} from "../../apps/tube-designer/webpage/partInspection.mjs";
import {
  ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS,
  fitDesignerDefaultView,
  getDesignerDefaultViewDirection,
  handleDesignerAreaAction,
} from "../../apps/tube-designer/webpage/designerActions.mjs";
import {
  buildPartCategories,
  applyReusablePresetValues,
  getDefaultTemplate,
  getReusablePresetValues,
  renderDesignerDialogs,
  renderDesignerRightPane,
  renderDesignerLeftPane,
} from "../../apps/tube-designer/webpage/designerViews.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
import { captureScrollAnchor, restoreScrollAnchor } from "../../apps/tube-designer/webpage/scrollAnchor.mjs";
import {
  PROFILE_PREVIEW_CACHE_PROGRESS_DELAY_MS,
  PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS,
  renderProfileLibraryLeftPane,
  renderProfileLibraryRightPane,
  renderProfileLibraryViewportOverlay,
} from "../../apps/tube-designer/webpage/profileLibrary.mjs";
import { ribbonDefinition as tubeDesignerRibbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";
import {
  buildMaterialProfileGroups,
  buildProfileGroups,
  filterManufacturingParts,
  renderNestingLeftPane,
  renderNestingResultDock,
  renderNestingRightPane,
  renderNestingViewportOverlay,
  renderPartsLeftPane,
} from "../../apps/tube-designer/webpage/partsArea.mjs";
import {
  hasProductionWorkflowAccess,
  PRODUCTION_WORKFLOW_ENTITLEMENT,
} from "../../apps/tube-designer/webpage/featureAccess.mjs";
import {
  buildProfileFromSectionDraft,
  createInitialSketchState,
  ensureSketchState,
  isSectionPreviewReady,
  renderSketchLeftPane,
  renderSketchRightPane,
  renderSketchViewportOverlay,
} from "../../apps/tube-designer/webpage/sketchArea.mjs";

function testSDOMethodCodes() {
  assert.equal(makeSDOMethodCode("App", "GetState"), makeSDOMethodCodeFromName(AppSDO.getState));
  assert.equal(makeSDOMethodCode("Product", "OpenProjectCatalog"), "5952739237587920785");
  assert.equal(makeSDOMethodCode("Machine", "Import"), makeSDOMethodCodeFromName("Machine.Import"));
  assert.throws(() => makeSDOMethodCodeFromName("Cam.Machine.Import"), /SDOName\.MethodName/);
  assert.equal(makePDOID("PreviewMesh", "MainViewport"), makeSDOMethodCode("PreviewMesh", "MainViewport"));
}

function testVariantSerializer() {
  const source = {
    productId: "icax.mock-product",
    enabled: true,
    count: 3,
    items: ["a", "b"],
    nested: { path: "D:/demo.ilcam" },
  };

  const text = serializeVariantText(source);
  assert.match(text, /"__variant_type":"Object"/);
  assert.deepEqual(deserializeVariantText(text), source);
}

function testTubeDesignerBuildsTemplateDefinedPartCategories() {
  const basePart = {
    profile: { displayName: "矩形管", specification: "25 × 25 × R2 × 1.5" },
    quantity: 1,
  };
  const parts = [
    {
      ...basePart,
      entityId: "part-1",
      name: "产品-主竖杆-01",
      partNumber: "产品-主竖杆-01",
      fileName: "产品-主竖杆-01.step",
      length: 800,
      properties: {
        "manufacturing.categoryKey": "main.vertical",
        "manufacturing.categoryName": "主竖杆",
      },
    },
    {
      ...basePart,
      entityId: "part-2",
      name: "产品-主竖杆-02",
      partNumber: "产品-主竖杆-02",
      fileName: "产品-主竖杆-02.step",
      length: 420,
      quantity: 2,
      properties: {
        "manufacturing.categoryKey": "main.vertical",
        "manufacturing.categoryName": "主竖杆",
      },
    },
    {
      ...basePart,
      entityId: "part-3",
      name: "产品-窗内竖杆-01",
      partNumber: "产品-窗内竖杆-01",
      fileName: "产品-窗内竖杆-01.step",
      length: 420,
      properties: {
        "manufacturing.categoryKey": "door.vertical",
        "manufacturing.categoryName": "窗内竖杆",
      },
    },
  ];
  const categories = buildPartCategories(parts, "product-1");

  assert.equal(categories.length, 2);
  assert.equal(categories[0].name, "主竖杆");
  assert.deepEqual(categories[0].parts.map((part) => part.entityId), ["part-1", "part-2"]);
  assert.equal(categories[0].quantity, 3);
  assert.deepEqual(categories[0].lengths, [800, 420]);
  assert.equal(categories[0].specifications.length, 1);
  assert.equal(categories[1].name, "窗内竖杆");

  const view = {
    pending: false,
    tubeDesignerBreakdownOpen: true,
    tubeDesignerSelectedPartIds: parts.map((part) => part.entityId),
    scene: {
      tubeDesigner: {
        product: {
          entityId: "product-1",
          name: "单面防盗窗-20260904",
          productCode: "TD-001",
          templateId: "single-face-security-window",
          parameters: { width: 1200, height: 1800 },
        },
        manufacturingGroups: [{
          productEntityId: "product-1",
          name: "单面防盗窗-20260904",
          productCode: "TD-001",
          templateId: "single-face-security-window",
          parameters: { width: 1200, height: 1800 },
          parts,
        }],
      },
    },
  };
  const collapsedHtml = renderDesignerRightPane({}, view);
  assert.doesNotMatch(collapsedHtml, /\browspan=/);
  assert.equal((collapsedHtml.match(/data-tube-designer-product-row=/g) ?? []).length, 1);
  assert.equal((collapsedHtml.match(/data-tube-designer-category-row=/g) ?? []).length, 2);
  assert.equal((collapsedHtml.match(/data-tube-designer-part-row=/g) ?? []).length, 0);
  assert.match(collapsedHtml, /tube-designer-tree-level-0[\s\S]*?单面防盗窗-20260904/);
  assert.match(collapsedHtml, /tube-designer-tree-level-1[\s\S]*?主竖杆/);
  assert.doesNotMatch(collapsedHtml, /[›⌄]|tube-designer-tree-leaf/);
  assert.match(collapsedHtml, /data-cam-action="tube-designer-expand-breakdown-all"/);
  assert.match(collapsedHtml, /data-cam-action="tube-designer-collapse-breakdown-all"/);
  assert.match(collapsedHtml, /id="tube-designer-breakdown-title">零件清单</);
  assert.match(collapsedHtml, /data-cam-action="tube-designer-enter-cutting"/);
  assert.doesNotMatch(collapsedHtml, /产品拆单结果/);
  assert.match(collapsedHtml, /data-cam-action="tube-designer-set-breakdown-page"/);

  view.tubeDesignerExpandedBreakdownCategoryIds = [categories[0].id];
  const expandedHtml = renderDesignerRightPane({}, view);
  assert.equal((expandedHtml.match(/data-tube-designer-part-row=/g) ?? []).length, 2);
  assert.equal((expandedHtml.match(/<th\b/g) ?? []).length, 5);
  assert.match(expandedHtml, /<thead><tr><th class="tube-designer-check-cell">/);
  assert.match(expandedHtml, /<\/th><th class="tube-designer-tree-column">名称<\/th><th>规格<\/th><th>示意图<\/th><th class="tube-designer-action-column">复尺<\/th>/);
  assert.equal((expandedHtml.match(/>名称<\/th>/g) ?? []).length, 1);
  assert.doesNotMatch(expandedHtml, />层级<\/th>/);
  assert.doesNotMatch(expandedHtml, />零件编号<\/th>/);
  assert.doesNotMatch(expandedHtml, />序号<\/th>|>长度<\/th>|>数量<\/th>|>文件名<\/th>/);
  assert.doesNotMatch(expandedHtml, /\.step/);
  assert.match(expandedHtml, /矩形管 25 × 25 × R2 × 1\.5\*len\*800/);
  assert.match(expandedHtml, /产品-主竖杆-01/);
  assert.match(expandedHtml, /tube-designer-tree-level-2[\s\S]*?产品-主竖杆-01/);
  assert.match(expandedHtml, /data-cam-action="tube-designer-open-part-inspection"[^>]*>复尺<\/button>/);
  assert.doesNotMatch(expandedHtml, /tube-designer-part-inspection-link/);

  const secondProductPart = {
    ...parts[0],
    entityId: "part-4",
    name: "第二产品-主竖杆-01",
    partNumber: "第二产品-主竖杆-01",
    fileName: "第二产品-主竖杆-01.step",
  };
  view.scene.tubeDesigner.manufacturingGroups.push({
    productEntityId: "product-2",
    name: "第二个产品实例",
    productCode: "TD-002",
    templateId: "single-face-security-window",
    parameters: { width: 900, height: 1500 },
    parts: [secondProductPart],
  });
  view.tubeDesignerBreakdownProductIds = ["product-1", "product-2"];
  view.tubeDesignerSelectedPartIds.push(secondProductPart.entityId);
  const firstPageHtml = renderDesignerRightPane({}, view);
  assert.equal((firstPageHtml.match(/data-tube-designer-product-row=/g) ?? []).length, 1);
  assert.match(firstPageHtml, /data-tube-designer-product-row="product-1"/);
  assert.doesNotMatch(firstPageHtml, /data-tube-designer-product-row="product-2"/);
  assert.match(firstPageHtml, /第 1 \/ 2 页/);

  view.tubeDesignerBreakdownPageProductId = "product-2";
  const secondPageHtml = renderDesignerRightPane({}, view);
  assert.equal((secondPageHtml.match(/data-tube-designer-product-row=/g) ?? []).length, 1);
  assert.match(secondPageHtml, /data-tube-designer-product-row="product-2"/);
  assert.doesNotMatch(secondPageHtml, /data-tube-designer-product-row="product-1"/);
  assert.match(secondPageHtml, /第 2 \/ 2 页/);

  view.tubeDesignerPartInspectionOpen = true;
  view.tubeDesignerInspectedPartId = "part-1";
  const inspectionHtml = renderDesignerRightPane({}, view);
  assert.match(inspectionHtml, /零件复尺 · 产品-主竖杆-01/);
  assert.doesNotMatch(inspectionHtml, /\.step/);
  assert.match(inspectionHtml, /data-tube-designer-part-inspection-viewport/);
  assert.match(inspectionHtml, /data-cam-action="tube-designer-toggle-automatic-dimensions"/);
  assert.doesNotMatch(inspectionHtml, /手工两点测量|tube-designer-toggle-part-measurement|tube-designer-clear-part-measurement/);
  assert.doesNotMatch(inspectionHtml, /tube-designer-breakdown-dialog/);
  assert.doesNotMatch(inspectionHtml, /data-tube-designer-part-thumbnail/);
  view.tubeDesignerPartInspectionOpen = false;
  view.tubeDesignerInspectedPartId = "";

  view.tubeDesignerBreakdownOpen = false;
  view.tubeDesignerDisassemblySelectorOpen = true;
  view.scene.tubeDesigner.instances = [{
    entityId: "product-1",
    name: "单面防盗窗-20260904",
    productCode: "TD-001",
    templateId: "single-face-security-window",
    parameters: { width: 1200, height: 1800 },
    hasDisassembly: true,
    partCount: 3,
  }];
  view.tubeDesignerSelectedInstanceIds = ["product-1"];
  const selectionHtml = renderDesignerRightPane({}, view);
  assert.match(selectionHtml, /选择要拆单的产品实例/);
  assert.doesNotMatch(selectionHtml, /当前状态|等待拆单|将重新拆单/);
}

function testTubeDesignerSeparatesBasicAndAdvancedProductionWorkflows() {
  assert.deepEqual(
    tubeDesignerRibbonDefinition.tabs.map((tab) => [tab.id, tab.title]),
    [["view", "产品"], ["nesting", "下料"], ["profiles", "管型"], ["sketch", "草图"], ["about", "关于"]],
  );
  assert.equal(tubeDesignerRibbonDefinition.tabs.some((tab) => tab.id === "parts"), false);
  assert.equal(hasProductionWorkflowAccess({}), false);
  assert.equal(hasProductionWorkflowAccess({
    entitlements: [PRODUCTION_WORKFLOW_ENTITLEMENT],
  }), true);

  const parts = [{
    entityId: "part-a",
    name: "立柱 A",
    partNumber: "P-A",
    length: 1200,
    quantity: 2,
    profile: { displayName: "矩形管", specification: "40 × 20 × 1.5" },
    properties: {
      "manufacturing.material": "Q235B",
      "tubeDesigner.endProcess": { startCut: "square", endCut: "square" },
    },
  }, {
    entityId: "part-b",
    name: "横杆 B",
    partNumber: "P-B",
    length: 800,
    quantity: 1,
    profile: { displayName: "矩形管", specification: "40 × 20 × 1.5" },
    properties: {
      "manufacturing.material": "Q235B",
      "tubeDesigner.endProcess": { startCut: "square", endCut: "square" },
    },
  }, {
    entityId: "part-c",
    name: "圆杆 C",
    partNumber: "P-C",
    length: 600,
    quantity: 1,
    profile: { displayName: "圆管", specification: "⌀19 × 1" },
    properties: {
      "manufacturing.material": "304",
      "tubeDesigner.endProcess": { startCut: "miter-45", endCut: "miter-45" },
    },
  }];
  const grouped = buildMaterialProfileGroups(parts);
  assert.equal(grouped.length, 2);
  const q235 = grouped.find((group) => group.material === "Q235B");
  assert.equal(q235.quantity, 3);
  assert.equal(q235.totalLength, 3200);
  const sameSectionDifferentMaterials = [
    parts[0],
    { ...parts[1], properties: { ...parts[1].properties, "manufacturing.material": "304" } },
  ];
  const sectionGroups = buildProfileGroups(sameSectionDifferentMaterials);
  assert.equal(sectionGroups.length, 1);
  assert.equal(sectionGroups[0].quantity, 3);
  assert.equal(sectionGroups[0].totalLength, 3200);
  assert.equal(buildMaterialProfileGroups(sameSectionDifferentMaterials).length, 2);
  const profileSnapshot = {
    id: "rect", kind: "rect", displayName: "矩形管", specification: "同名规格",
    width: 40, depth: 20, wallThickness: 1.5, cornerRadius: 2,
  };
  assert.equal(buildProfileGroups([
    { ...parts[0], profile: profileSnapshot },
    { ...parts[1], profile: { ...profileSnapshot, width: 50 } },
    { ...parts[2], profile: { ...profileSnapshot, wallThickness: 2 } },
  ]).length, 3, "Equal display labels must not merge different section dimensions or wall thicknesses");
  assert.equal(buildProfileGroups([
    { ...parts[0], profile: profileSnapshot },
    { ...parts[1], profile: { ...profileSnapshot, displayName: "自定义名称" } },
  ]).length, 1, "Renaming a section does not change its geometry");
  assert.deepEqual(
    filterManufacturingParts(parts, { filter: "straight" }).map((part) => part.entityId),
    ["part-a", "part-b"],
  );
  assert.deepEqual(
    filterManufacturingParts(parts, { filter: "special" }).map((part) => part.entityId),
    ["part-c"],
  );
  assert.deepEqual(
    filterManufacturingParts(parts, { query: "p-b" }).map((part) => part.entityId),
    ["part-b"],
  );

  const view = {
    pending: false,
    scene: { tubeDesigner: { manufacturingGroups: [{
      productEntityId: "product-1",
      name: "产品一",
      productCode: "TD-001",
      parts,
    }] } },
  };
  const unlocked = renderPartsLeftPane({}, { ...view, tubeDesignerProductionAccess: true });
  assert.match(unlocked, /2 个材料截面组/);
  assert.match(unlocked, /data-cam-action="tube-designer-parts-select-part"/);
  assert.match(unlocked, /data-cam-change-action="tube-designer-parts-search"/);
  assert.match(unlocked, /斜切 \/ 异形/);

  const cuttingView = { ...view, activeAreaId: "nesting", tubeDesignerProductionAccess: true };
  const cuttingLeft = renderNestingLeftPane({
    entitlements: [PRODUCTION_WORKFLOW_ENTITLEMENT],
  }, cuttingView);
  assert.match(cuttingLeft, /<strong>零件清单<\/strong>/);
  assert.match(cuttingLeft, /tube-designer-cutting-parts/);
  assert.match(cuttingLeft, /2 种截面/);
  assert.doesNotMatch(cuttingLeft, /材料截面组|材料未指定|Q235B|304/);
  assert.doesNotMatch(cuttingLeft, /进入排样|进入下料/);
  const cuttingCenter = renderNestingViewportOverlay({}, cuttingView);
  assert.match(cuttingCenter, /待排零件/);
  assert.doesNotMatch(cuttingCenter, /tube-designer-parts-toggle-dimensions/);
  const cuttingRight = renderNestingRightPane({
    entitlements: [PRODUCTION_WORKFLOW_ENTITLEMENT],
  }, cuttingView);
  assert.match(cuttingRight, /<strong>当前零件<\/strong>/);
  assert.match(cuttingRight, /下料信息/);
  const emptyResultDock = renderNestingResultDock({}, cuttingView);
  assert.match(emptyResultDock, /aria-label="排样结果列表"/);
  assert.match(emptyResultDock, /尚未生成排样结果/);
  const populatedResultDock = renderNestingResultDock({}, {
    ...cuttingView,
    scene: { tubeDesigner: {
      ...cuttingView.scene.tubeDesigner,
      nestingResult: { plans: [{
        id: "stock-1",
        name: "Q235B-6000-01",
        material: "Q235B",
        profile: "矩形管 40 × 20 × 1.5",
        stockLength: 6000,
        usedLength: 5280,
        remainingLength: 720,
        placements: [{}, {}, {}],
        utilization: 0.88,
      }] },
    } },
  });
  assert.match(populatedResultDock, /Q235B-6000-01/);
  assert.match(populatedResultDock, /88%/);

  const choiceHtml = renderDesignerDialogs({}, {
    tubeDesignerPostDisassemblyChoice: { groupCount: 2, partCount: 7 },
  });
  assert.match(choiceHtml, /拆单完成/);
  assert.match(choiceHtml, /data-cam-action="tube-designer-export-after-disassembly"/);
  assert.match(choiceHtml, /data-cam-action="tube-designer-enter-cutting"/);

  const productHtml = renderDesignerLeftPane({}, {
    pending: false,
    scene: { tubeDesigner: {
      product: { entityId: "product-1" },
      activeProductId: "product-1",
      templates: [{ id: "single-face-security-window", name: "防盗窗/单面防盗窗" }],
      instances: [{
        entityId: "product-1",
        name: "产品一",
        productCode: "TD-001",
        templateId: "single-face-security-window",
        parameters: { width: 1200, height: 1800 },
        hasDisassembly: true,
        partCount: 3,
      }],
    } },
  });
  assert.match(productHtml, /data-cam-action="tube-designer-open-instance-breakdown"/);
  assert.match(productHtml, />零件清单<\/button>/);
}

async function testTubeDesignerEntersCuttingFromThePartList() {
  let selectedAreaId = "view";
  let rendered = false;
  const view = {
    tubeDesignerBreakdownOpen: true,
    tubeDesignerPostDisassemblyChoice: { groupCount: 1, partCount: 3 },
  };
  const result = await handleDesignerAreaAction({
    actions: {
      selectRibbonTab: async (areaId) => {
        selectedAreaId = areaId;
      },
    },
  }, view, "tube-designer-enter-cutting", {}, {
    renderProject() {
      rendered = true;
    },
  });

  assert.equal(result.handled, true);
  assert.equal(selectedAreaId, "nesting");
  assert.equal(view.tubeDesignerBreakdownOpen, false);
  assert.equal(view.tubeDesignerPostDisassemblyChoice, null);
  assert.equal(rendered, false);
}

function testTubeDesignerSketchJoinsTheMainWorkflow() {
  const state = createInitialSketchState();
  const view = { scene: { tubeDesigner: { members: [] } }, tubeDesignerSketch: state };
  assert.equal(state.mode, "section");
  assert.equal(isSectionPreviewReady(state.section), false);
  assert.match(renderSketchLeftPane({}, view), /还没有截面/);
  assert.match(renderSketchViewportOverlay({}, view), /管型截面图/);

  const outer = {
    id: "outer",
    kind: "rectangle",
    x: -25,
    y: -15,
    width: 50,
    height: 30,
    radius: 0,
    closed: true,
  };
  state.section.entities = [outer];
  state.section.selectedId = "outer";
  assert.equal(isSectionPreviewReady(state.section), true);
  const profile = buildProfileFromSectionDraft(state.section, "草图矩形管");
  assert.equal(profile.schema, "icax.imported-tube-profile");
  assert.equal(profile.sourceFormat, "icax.tube-sketch");
  assert.equal(profile.width, 50);
  assert.equal(profile.depth, 30);
  assert.equal(profile.contourCount, 1);
  assert.equal(profile.contours[0].segments.length, 4);
  assert.match(renderSketchLeftPane({}, view), /tube-sketch-preview-svg/);
  assert.match(renderSketchRightPane({}, view), /几何参数/);
  state.mode = "side";
  assert.equal(state.section.entities[0].id, "outer", "switching modes keeps the section draft");
  state.mode = "section";

  const sideEntity = {
    id: "side-path",
    kind: "freehand",
    points: [[10, 8], [30, 16], [55, 9]],
    closed: false,
  };
  const sideView = {
    scene: { tubeDesigner: {
      product: { entityId: "product-1", sketches: { side: { "member-1": {
        schema: "icax.tube-sketch",
        schemaVersion: 1,
        kind: "side",
        entities: [sideEntity],
      } } } },
      members: [{ entityId: "member-1", name: "主管", length: 1200 }],
    } },
    tubeDesignerSketch: createInitialSketchState(),
  };
  const hydrated = ensureSketchState(sideView);
  hydrated.mode = "side";
  assert.equal(hydrated.targetMemberId, "member-1");
  assert.equal(hydrated.sideByMember["member-1"].entities[0].id, "side-path");
  assert.match(renderSketchLeftPane({}, sideView), /三维切割预览/);
  assert.match(renderSketchViewportOverlay({}, sideView), /管型侧面切割图/);

  assert.ok(tubeDesignerRibbonDefinition.tabs.some((tab) => tab.id === "sketch" && tab.title === "草图"));
  assert.deepEqual(
    tubeDesignerRibbonDefinition.tabs.map((tab) => tab.id),
    ["view", "nesting", "profiles", "sketch", "about"],
  );
  const commands = tubeDesignerRibbonDefinition.tabs
    .find((tab) => tab.id === "sketch")
    .groups.flatMap((group) => group.commands)
    .map((command) => command.id);
  assert.ok(commands.includes("sketch.mode-section"));
  assert.ok(commands.includes("sketch.mode-side"));
  assert.ok(commands.includes("sketch.freehand"));
  assert.ok(commands.includes("sketch.commit"));
}

function testTubeDesignerChoosesTheFirstAvailableCatalogTemplate() {
  const templates = [
    { id: "straight-stair-railing", name: "楼梯/护栏/直跑楼梯护栏", available: true, extensions: { catalog: { groupOrder: 20, templateOrder: 10 } } },
    { id: "five-face-security-window", name: "防盗窗/五面防盗窗", available: true, extensions: { catalog: { groupOrder: 10, templateOrder: 40 } } },
    { id: "single-face-security-window", name: "防盗窗/单面防盗窗", available: true, extensions: { catalog: { groupOrder: 10, templateOrder: 10 } } },
  ];
  assert.equal(getDefaultTemplate(templates)?.id, "single-face-security-window");
}

function testTubeDesignerBuildsCollapsibleTemplateHierarchyFromDisplayNames() {
  const templates = [
    {
      id: "single-face-security-window",
      name: "防盗窗/单面防盗窗",
      version: "2.6.0",
      available: true,
      parameters: [],
      extensions: { catalog: { groupOrder: 10, templateOrder: 10 } },
    },
    {
      id: "two-face-security-window",
      name: "防盗窗/两面防盗窗",
      version: "1.6.0",
      available: true,
      parameters: [],
      extensions: { catalog: { groupOrder: 10, templateOrder: 20 } },
    },
    {
      id: "straight-stair-railing",
      name: "楼梯/护栏/直跑楼梯护栏",
      version: "1.0.0",
      available: true,
      parameters: [],
      extensions: { catalog: { groupOrder: 20, templateOrder: 10 } },
    },
    {
      id: "straight-steel-staircase",
      name: "楼梯/钢楼梯/直跑钢楼梯",
      version: "1.0.0",
      available: true,
      parameters: [],
      extensions: { catalog: { groupOrder: 20, templateOrder: 20 } },
    },
  ];
  const view = {
    pending: false,
    tubeDesignerAddDialogOpen: true,
    tubeDesignerAddTemplateId: "single-face-security-window",
    tubeDesignerAddInstanceName: "单面防盗窗 测试",
    tubeDesignerAddDraft: {},
    scene: { tubeDesigner: { templates } },
  };

  const collapsed = renderDesignerRightPane({}, view);
  assert.equal((collapsed.match(/data-tube-designer-template-group-depth="0"/g) ?? []).length, 2);
  assert.equal((collapsed.match(/tube-designer-template-card/g) ?? []).length, 4);
  assert.match(collapsed, /data-tube-designer-template-group-id="template-path:防盗窗"[^>]*aria-expanded="false"/);
  assert.match(collapsed, /data-tube-designer-template-group-id="template-path:楼梯"[^>]*aria-expanded="false"/);
  assert.equal((collapsed.match(/class="tube-designer-template-group-items" hidden/g) ?? []).length, 4);

  view.tubeDesignerExpandedTemplateGroupIds = ["template-path:防盗窗"];
  const securityWindowExpanded = renderDesignerRightPane({}, view);
  assert.equal((securityWindowExpanded.match(/tube-designer-template-card/g) ?? []).length, 4);
  assert.match(securityWindowExpanded, />单面防盗窗<\/strong>/);
  assert.match(securityWindowExpanded, />两面防盗窗<\/strong>/);
  assert.doesNotMatch(securityWindowExpanded, />防盗窗\/单面防盗窗<\/strong>/);

  view.tubeDesignerExpandedTemplateGroupIds = [
    "template-path:楼梯",
    "template-path:楼梯/钢楼梯",
  ];
  const nestedExpanded = renderDesignerRightPane({}, view);
  assert.match(nestedExpanded, /data-tube-designer-template-group-id="template-path:楼梯\/护栏"/);
  assert.match(nestedExpanded, /data-tube-designer-template-group-id="template-path:楼梯\/护栏"[^>]*aria-expanded="false"/);
  assert.match(nestedExpanded, /data-tube-designer-template-group-id="template-path:楼梯\/钢楼梯"[^>]*aria-expanded="true"/);
  assert.match(nestedExpanded, />直跑钢楼梯<\/strong>/);
  assert.match(nestedExpanded, />直跑楼梯护栏<\/strong>/);
}

function testTubeDesignerOperationOverlayCoversTheWholeWindow() {
  const rule = tubeDesignerCss.match(/\.tube-designer-operation-wait\s*\{([^}]*)\}/)?.[1] ?? "";
  assert.match(rule, /position:\s*fixed\s*;/);
  assert.match(rule, /inset:\s*0\s*;/);
}

function testTubeDesignerProfileMiniatureConstrainsSvgIntrinsicSize() {
  const rule = tubeDesignerCss.match(
    /\.tube-profile-library-miniature\s+\.tube-profile-library-svg\s*\{([^}]*)\}/,
  )?.[1] ?? "";
  assert.match(rule, /position:\s*absolute\s*;/);
  assert.match(rule, /top:\s*7%\s*;/);
  assert.match(rule, /left:\s*7%\s*;/);
  assert.match(rule, /display:\s*block\s*;/);
  assert.match(rule, /width:\s*86%\s*;/);
  assert.match(rule, /height:\s*86%\s*;/);
  assert.match(rule, /min-width:\s*0\s*;/);
  assert.match(rule, /min-height:\s*0\s*;/);
  assert.match(rule, /max-width:\s*none\s*;/);
  assert.match(rule, /max-height:\s*none\s*;/);
}

function testTubeDesignerProfilePreviewProgressCoversTheWholeWindow() {
  assert.equal(PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS, 500);
  assert.ok(PROFILE_PREVIEW_CACHE_PROGRESS_DELAY_MS > 0);
  assert.ok(PROFILE_PREVIEW_CACHE_PROGRESS_DELAY_MS < PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS);
  const rule = tubeDesignerCss.match(
    /\.tube-profile-library-preview-wait\s*\{([^}]*)\}/,
  )?.[1] ?? "";
  assert.match(rule, /position:\s*fixed\s*;/);
  assert.match(rule, /inset:\s*0\s*;/);
  const zIndex = Number(rule.match(/z-index:\s*(\d+)\s*;/)?.[1] ?? 0);
  assert.ok(zIndex > 20, "profile preview progress must cover the product ribbon");
  const hiddenRule = tubeDesignerCss.match(
    /\.tube-profile-library-preview-wait\[hidden\]\s*\{([^}]*)\}/,
  )?.[1] ?? "";
  assert.match(hiddenRule, /display:\s*none\s*;/);
}

function testGenericProgressOverlayCoversTheWholeProductPage() {
  const css = readFileSync(
    new URL("../../apps/_shared/workbench/styles/laser3dcam.css", import.meta.url),
    "utf8",
  );
  const rule = css.match(/\.cam-progress-backdrop\s*\{([^}]*)\}/)?.[1] ?? "";
  assert.match(rule, /position:\s*fixed\s*;/);
  assert.match(rule, /inset:\s*0\s*;/);
  const zIndex = Number(rule.match(/z-index:\s*(\d+)\s*;/)?.[1] ?? 0);
  assert.ok(zIndex > 20, "progress overlay must cover the product ribbon");
}

function testTubeDesignerTemplateSwitchProgressDoesNotFlash() {
  assert.equal(ADD_TEMPLATE_PROGRESS_MINIMUM_VISIBLE_MS, 500);
}

function testTubeDesignerExpandedGroupsKeepTheirFullContentHeight() {
  for (const selector of [
    ".tube-designer-config-subsection",
    ".tube-designer-parameter-section",
    ".tube-designer-parameter-subsection",
    ".tube-designer-template-group",
  ]) {
    const escapedSelector = selector.replaceAll(".", "\\.");
    const rule = tubeDesignerCss.match(new RegExp(`${escapedSelector}\\s*\\{([^}]*)\\}`))?.[1] ?? "";
    assert.match(rule, /min-height:\s*max-content\s*;/, `${selector} must not shrink and clip expanded content`);
  }
  for (const selector of [
    ".tube-designer-subsection-list",
    ".tube-designer-parameter-sections",
    ".tube-designer-parameter-subsection-list",
    ".tube-designer-template-list",
    ".tube-designer-template-group-items",
  ]) {
    const escapedSelector = selector.replaceAll(".", "\\.");
    const rule = tubeDesignerCss.match(new RegExp(`${escapedSelector}\\s*\\{([^}]*)\\}`))?.[1] ?? "";
    assert.match(rule, /grid-auto-rows:\s*max-content\s*;/, `${selector} must size rows from expanded content`);
  }
}

function testTubeDesignerPreservesFocusedItemPositionWhenScrollLengthChanges() {
  const scroller = {
    scrollTop: 200,
    ownerDocument: null,
    contains: (element) => element === before,
    getBoundingClientRect: () => ({ top: 100 }),
    querySelectorAll: () => [after],
  };
  const before = {
    closest: () => before,
    getAttribute: () => "accessDoorEnabled",
    getBoundingClientRect: () => ({ top: 360 - scroller.scrollTop }),
  };
  let focusOptions = null;
  const after = {
    getAttribute: () => "accessDoorEnabled",
    getBoundingClientRect: () => ({ top: 460 - scroller.scrollTop }),
    focus: (options) => { focusOptions = options; },
  };
  scroller.ownerDocument = { activeElement: before };

  const anchor = captureScrollAnchor(scroller, before);
  assert.equal(anchor.viewportOffset, 60);
  assert.equal(anchor.restoreFocus, true);

  restoreScrollAnchor(scroller, anchor);
  assert.equal(scroller.scrollTop, 300);
  assert.deepEqual(focusOptions, { preventScroll: true });
  assert.equal(after.getBoundingClientRect().top - scroller.getBoundingClientRect().top, 60);
}

function testTubeDesignerManifestDoesNotPublishCompatibilityTemplates() {
  const manifest = JSON.parse(readFileSync(
    new URL("../../apps/tube-designer/product.manifest.json", import.meta.url),
    "utf8",
  ));
  const templateIds = manifest.capabilities.tubeDesigner.templates.map((template) => template.templateId);
  assert.deepEqual(
    templateIds.filter((templateId) => /^security-window-[123]$/.test(templateId)),
    [],
  );
}

function testSecurityWindowHorizontalProfilesAreSmallerThanFrames() {
  const templateFolders = [
    "single_face_security_window",
    "two_face_security_window",
    "three_face_security_window",
    "five_face_security_window",
  ];
  for (const folder of templateFolders) {
    const descriptor = JSON.parse(readFileSync(
      new URL(`../../apps/tube-designer/templates/${folder}/template.json`, import.meta.url),
      "utf8",
    ));
    const templateSource = readFileSync(
      new URL(`../../apps/tube-designer/templates/${folder}/template.py`, import.meta.url),
      "utf8",
    );
    const pythonVersion = templateSource.match(/^TEMPLATE_VERSION\s*=\s*"([^"]+)"/m)?.[1];
    assert.equal(pythonVersion, descriptor.version, `${descriptor.id} JSON/Python version identity`);
    const parameters = Object.fromEntries(
      descriptor.parameters.map((parameter) => [parameter.key, parameter.defaultValue]),
    );
    assert.equal(parameters.horizontalWidth, 22, `${descriptor.id} horizontal width`);
    assert.equal(parameters.horizontalDepth, 22, `${descriptor.id} horizontal depth`);
    assert.ok(parameters.horizontalWidth < parameters.frameWidth, `${descriptor.id} width hierarchy`);
    assert.ok(parameters.horizontalDepth < parameters.frameDepth, `${descriptor.id} depth hierarchy`);
    const balanced = descriptor.extensions.parameterPresets.presets
      .find((preset) => preset.value === "stainless_balanced")?.values;
    assert.equal(balanced?.horizontalWidth, 22, `${descriptor.id} balanced width`);
    assert.equal(balanced?.horizontalDepth, 22, `${descriptor.id} balanced depth`);
  }
}

function testTubeDesignerBuildsParameterHierarchyFromDisplayNamePaths() {
  const template = {
    id: "single-face-security-window",
    name: "单面防盗窗",
    available: true,
    groups: [
      { key: "door_frame_profile", displayName: "固定窗框矩形管", order: 30 },
      { key: "door_position", displayName: "逃生窗位置", order: 20 },
      { key: "overall", displayName: "基本尺寸", order: 10 },
    ],
    parameters: [
      { key: "doorWidth", displayName: "逃生窗/位置与尺寸/窗框宽度", groupKey: "door_position", group: "逃生窗位置", type: "number", defaultValue: 300, order: 20 },
      { key: "doorFrameWidth", displayName: "逃生窗/固定窗框截面/截面宽度", groupKey: "door_frame_profile", group: "固定窗框矩形管", type: "number", defaultValue: 25, order: 10 },
      { key: "width", displayName: "产品宽度", groupKey: "overall", group: "基本尺寸", type: "number", defaultValue: 1200, order: 10 },
      { key: "doorLeft", displayName: "逃生窗/位置与尺寸/窗框左边距", groupKey: "door_position", group: "逃生窗位置", type: "number", defaultValue: 450, order: 10 },
      { key: "accessDoorEnabled", displayName: "逃生窗/是否启用", groupKey: "door_position", group: "逃生窗位置", type: "boolean", defaultValue: false, order: 1 },
    ],
  };
  const html = renderDesignerRightPane({}, {
    pending: false,
    scene: {
      tubeDesigner: {
        product: {
          entityId: "product-parameter-tree",
          name: "单面防盗窗-测试",
          templateId: template.id,
          parameters: {},
        },
        templates: [template],
        members: [],
        joints: [],
        parts: [],
      },
    },
  });

  assert.equal((html.match(/class="tube-designer-parameter-section"/g) ?? []).length, 2);
  assert.equal((html.match(/class="tube-designer-parameter-subsection"/g) ?? []).length, 2);
  assert.match(html, /<summary><span>逃生窗<\/span><small>4 项<\/small><\/summary>/);
  assert.match(html, /<summary><span>位置与尺寸<\/span><small>2 项<\/small><\/summary>/);
  assert.match(html, /<summary><span>固定窗框截面<\/span><small>1 项<\/small><\/summary>/);
  assert.doesNotMatch(html, /逃生窗\/是否启用/);
  assert.ok(html.indexOf("基本尺寸") < html.indexOf("<summary><span>逃生窗"));
  assert.ok(html.indexOf("位置与尺寸") < html.indexOf("固定窗框截面"));
  assert.ok(html.indexOf("窗框左边距") < html.indexOf("窗框宽度"));
}

function testTubeDesignerBuildsDimensionsOnlyFromFinalGeometryMeasurement() {
  const report = buildAutomaticDimensionReport({
    length: 9999,
    sectionWidth: 999,
    geometryMeasurement: {
      available: true,
      source: "final-brep",
      length: 1000,
      section: { shape: "rectangle", width: 40, height: 30, wallThickness: 1.5 },
      linearReference: {
        start: [0, 0, 0],
        end: [1000, 0, 0],
        sectionAxes: [[0, 1, 0], [0, 0, 1]],
        dimensionOffsetDirection: [0, 0, 1],
      },
      features: [
        {
          kind: "through-opening",
          center: [100, 0, 0],
          station: 100,
          shape: "circle",
          diameter: 20,
          openingSpanAlong: 20,
          openingSpanAcross: 20,
          faceTangent: [0, 1, 0],
          centerToFaceEdgeNegative: 15,
          centerToFaceEdgePositive: 15,
          faceEdgeClearanceNegative: 5,
          faceEdgeClearancePositive: 5,
        },
        {
          kind: "through-opening",
          center: [300, 0, 0],
          station: 300,
          shape: "circle",
          diameter: 20,
          openingSpanAlong: 20,
          openingSpanAcross: 20,
          faceTangent: [0, 1, 0],
          centerToFaceEdgeNegative: 15,
          centerToFaceEdgePositive: 15,
          faceEdgeClearanceNegative: 5,
          faceEdgeClearancePositive: 5,
        },
      ],
    },
  });
  assert.equal(report.source, "final-brep");
  assert.equal(report.length, 1000);
  assert.match(report.profile, /40 × 30/);
  assert.equal(report.holes.length, 2);
  assert.equal(report.holes[0].startEdgeDistance, 90);
  assert.equal(report.holes[1].endEdgeDistance, 690);
  assert.deepEqual(report.pitches, [{ from: 1, to: 2, centerDistance: 200, edgeClearance: 180 }]);
  assert.ok(report.annotations.length >= 7);

  const ignoredTemplateData = buildAutomaticDimensionReport({
    properties: {
      "manufacturing.geometryMeasurement": {
        available: true,
        source: "template",
        length: 7777,
      },
    },
  });
  assert.equal(ignoredTemplateData.hasLinearReference, false);
  assert.equal(ignoredTemplateData.holes.length, 0);
}

function testTubeDesignerUserPresetKeepsOnlyReusableTemplateParameters() {
  const template = {
    id: "security-window",
    version: "2.0.0",
    parameters: [
      { key: "productCode" }, { key: "width" }, { key: "height" },
      { key: "tubeSpecificationPreset" }, { key: "frameWidth" }, { key: "wallThickness" },
    ],
    extensions: {
      productIdentity: { codeParameter: "productCode" },
      primaryDimensions: { widthParameter: "width", heightParameter: "height" },
      parameterPresets: { selectorParameter: "tubeSpecificationPreset" },
    },
  };
  const values = {
    productCode: "A-001", width: 1200, height: 1800,
    tubeSpecificationPreset: "custom", frameWidth: 38, wallThickness: 1.2,
    retiredParameter: 99,
  };
  assert.deepEqual(getReusablePresetValues(template, values), {
    frameWidth: 38,
    wallThickness: 1.2,
  });
  assert.deepEqual(applyReusablePresetValues(template, values, {
    frameWidth: 42,
    retiredParameter: 100,
  }), {
    ...values,
    frameWidth: 42,
  });
}

function testTubeDesignerRendersBuiltInAndPersonalPresetChoices() {
  const template = {
    id: "security-window",
    version: "2.0.0",
    name: "防盗窗/单面防盗窗",
    available: true,
    groups: [{ key: "profile", displayName: "管材", order: 1 }],
    parameters: [
      {
        key: "tubeSpecificationPreset", type: "select", defaultValue: "balanced",
        options: [{ value: "balanced", label: "均衡配置" }, { value: "custom", label: "自定义" }],
        groupKey: "profile", group: "管材", order: 1,
      },
      { key: "frameWidth", type: "number", defaultValue: 38, displayName: "框宽", groupKey: "profile", group: "管材", order: 2 },
    ],
    extensions: {
      parameterPresets: {
        selectorParameter: "tubeSpecificationPreset",
        customValue: "custom",
        presets: [{ value: "balanced", values: { frameWidth: 38 } }],
      },
    },
  };
  const html = renderDesignerRightPane({}, {
    scene: { tubeDesigner: {
      product: { entityId: "product-1", name: "测试产品", templateId: template.id, parameters: { tubeSpecificationPreset: "balanced", frameWidth: 38 } },
      templates: [template], members: [], joints: [], parts: [],
    } },
    tubeDesignerUserData: {
      customers: [{ id: "customer-1", name: "王师傅" }],
      parameterPresets: [{
        id: "preset-1", name: "常用厚料", templateId: template.id,
        templateVersion: template.version, customerId: "customer-1", values: { frameWidth: 42 },
      }],
    },
  });
  assert.match(html, /模板内置/);
  assert.match(html, /我的常用方案/);
  assert.match(html, /王师傅 \/ 常用厚料/);
  assert.match(html, /data-cam-change-action="tube-designer-apply-parameter-preset"/);
  assert.equal((html.match(/data-tube-designer-parameter="tubeSpecificationPreset"/g) ?? []).length, 0);
}

function testTubeDesignerRendersFrozenDxfProfilesWithoutEditableDimensions() {
  const imported = {
    schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "imported-dxf",
    name: "供应商梅花管", sourceFileName: "plum-50.dxf", sourceUnit: "毫米",
    specification: "DXF 50 × 48 mm", width: 50, depth: 48,
    wallThickness: 1.6, cornerRadius: 0, contourCount: 2,
    contours: [{ kind: "polygon", points: [[-25, -24], [25, -24], [25, 24], [-25, 24]] }],
  };
  const template = {
    id: "profile-test", version: "1.0.0", name: "测试产品", available: true,
    groups: [{ key: "profile", displayName: "管型", order: 1 }],
    parameters: [
      { key: "frameProfileType", type: "select", displayName: "截面类型", defaultValue: "rect", options: [{ value: "rect", label: "矩形管" }], groupKey: "profile", group: "管型", order: 1 },
      { key: "frameWidth", type: "number", displayName: "截面宽度", defaultValue: 38, groupKey: "profile", group: "管型", order: 2 },
      { key: "frameDepth", type: "number", displayName: "截面深度", defaultValue: 25, groupKey: "profile", group: "管型", order: 3 },
      { key: "frameWallThickness", type: "number", displayName: "壁厚", defaultValue: 1.2, groupKey: "profile", group: "管型", order: 4 },
      { key: "frameCornerRadius", type: "number", displayName: "圆角", defaultValue: 2, groupKey: "profile", group: "管型", order: 5 },
    ],
  };
  const html = renderDesignerRightPane({}, {
    pending: false,
    scene: { tubeDesigner: {
      product: { entityId: "product-dxf", name: "DXF 产品", templateId: template.id, parameters: {
        frameProfileType: "rect", frameWidth: 38, frameDepth: 25,
        frameWallThickness: 1.2, frameCornerRadius: 2,
        tubeDesignerProfileOverrides: { frame: { ...imported, savedProfileId: "profile-1" } },
      } },
      templates: [template], members: [], joints: [], parts: [],
    } },
    tubeDesignerUserData: { customers: [], parameterPresets: [], profiles: [{ ...imported, id: "profile-1" }] },
  });
  assert.match(html, /我的管型/);
  assert.match(html, /供应商梅花管/);
  assert.match(html, /冻结截面，不支持尺寸参数修改/);
  assert.match(html, /data-cam-action="tube-designer-import-profile-dxf"/);
  assert.match(html, /data-cam-action="tube-designer-open-profile-library"/);
  assert.match(html, /我的管型管理 \(1\)/);
  assert.doesNotMatch(html, /data-tube-designer-parameter="frameWidth"/);
  assert.doesNotMatch(html, /data-tube-designer-parameter="frameDepth"/);
  assert.doesNotMatch(html, /data-tube-designer-parameter="frameWallThickness"/);
  assert.doesNotMatch(html, /data-tube-designer-parameter="frameCornerRadius"/);

  const libraryHtml = renderDesignerRightPane({}, {
    pending: false,
    scene: { tubeDesigner: {
      product: { entityId: "product-dxf", name: "DXF 产品", templateId: template.id, parameters: {} },
      templates: [template], members: [], joints: [], parts: [],
    } },
    tubeDesignerProfileLibraryDialog: { mode: "right" },
    tubeDesignerUserData: { customers: [], parameterPresets: [], profiles: [{ ...imported, id: "profile-1", revision: 2 }] },
  });
  assert.match(libraryHtml, /我的管型管理/);
  assert.match(libraryHtml, /data-cam-action="tube-designer-rename-library-profile"/);
  assert.match(libraryHtml, /data-cam-action="tube-designer-delete-library-profile"/);
  assert.match(libraryHtml, /删除只影响“我的管型”列表/);
}

function testTubeDesignerHasIndependentEditableProfileLibrary() {
  const preview = {
    schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "parametric-package",
    name: "参数梅花管", specification: "50 × 48 × 1.5", width: 50, depth: 48,
    wallThickness: 1.5, cornerRadius: 0, contourCount: 2,
    contours: [
      { kind: "roundedRectangle", width: 50, height: 48, radius: 4 },
      { kind: "roundedRectangle", width: 47, height: 45, radius: 2.5 },
    ],
    parameters: { width: 50, depth: 48, wallThickness: 1.5 },
    parameterDefinitions: [], editableParameters: true, frozenGeometry: false,
  };
  const editable = {
    id: "profile-editable", revision: 3, profileType: "parametric-package",
    name: "参数梅花管", sourceFileName: "plum.icaxprofile",
    descriptor: {
      id: "plum", version: "1.2.0",
      parameters: [
        { key: "width", displayName: { "zh-CN": "外宽" }, valueType: "number", defaultValue: 50, min: 10 },
        { key: "depth", displayName: { "zh-CN": "外高" }, valueType: "number", defaultValue: 48, min: 10 },
      ],
    },
    defaultParameters: { width: 50, depth: 48 }, previewProfile: preview,
  };
  const frozen = {
    id: "profile-dxf", revision: 1, profileType: "imported-dxf",
    name: "供应商 DXF", sourceFileName: "vendor.dxf", sourceUnit: "毫米",
    specification: "DXF 30 × 20 mm", width: 30, depth: 20, contourCount: 1,
    contours: [{ kind: "roundedRectangle", width: 30, height: 20, radius: 0 }],
  };
  const system = {
    id: "round", profileType: "parametric-package", name: "圆管",
    descriptor: {
      id: "round", version: "2.0.0",
      displayName: { "zh-CN": "圆管" },
      parameters: [
        { key: "width", displayName: { "zh-CN": "外径" }, valueType: "number", defaultValue: 40, min: 1 },
        { key: "wallThickness", displayName: { "zh-CN": "壁厚" }, valueType: "number", defaultValue: 2, min: 0.1 },
      ],
    },
    defaultParameters: { width: 40, wallThickness: 2 },
    previewProfile: {
      schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "parametric-package",
      name: "圆管", specification: "Φ40 × 2", width: 40, depth: 40,
      wallThickness: 2, contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }],
    },
  };
  const view = {
    pending: false,
    tubeDesignerSelectedProfileId: editable.id,
    tubeDesignerSystemProfiles: [system],
    tubeDesignerUserData: { profiles: [editable, frozen] },
  };
  const left = renderProfileLibraryLeftPane({}, view);
  const right = renderProfileLibraryRightPane({}, view);
  const previewHtml = renderProfileLibraryViewportOverlay({}, view);
  assert.match(left, /可编辑管型包/);
  assert.match(left, /DXF 冻结截面/);
  assert.match(left, /data-tube-profile-library-group="system"[^>]*open/);
  assert.match(left, /data-tube-profile-library-group="user"[^>]*open/);
  assert.match(left, /系统内置/);
  assert.match(left, /data-tube-designer-profile-key="system:round"/);
  assert.match(left, /data-tube-designer-profile-key="user:profile-editable"/);
  assert.equal(view.tubeDesignerSelectedProfileId, "user:profile-editable");
  assert.match(right, /默认参数/);
  assert.match(right, /data-tube-profile-editor-parameter="width"/);
  assert.match(right, /保存名称和默认参数/);
  assert.match(right, /tube-profile-library-miniature/);
  assert.match(right, /data-cam-action="tube-designer-profile-export-dxf"/);
  assert.match(right, /data-cam-action="tube-designer-profile-export-step"/);
  assert.match(previewHtml, /tube-profile-library-preview-hud/);
  assert.match(previewHtml, /data-tube-profile-preview-progress/);
  assert.match(previewHtml, /data-tube-profile-preview-wait/);
  assert.match(previewHtml, /data-tube-profile-preview-wait-progress[^>]*role="progressbar"/);
  assert.match(previewHtml, /data-tube-profile-preview-wait[^>]*aria-hidden="true"[^>]*hidden/);
  assert.doesNotMatch(previewHtml, /tube-profile-library-svg/);
  assert.ok(tubeDesignerRibbonDefinition.tabs.some((tab) => tab.id === "profiles" && tab.title === "管型"));
  assert.ok(tubeDesignerRibbonDefinition.tabs
    .find((tab) => tab.id === "profiles")
    .groups.flatMap((group) => group.commands)
    .some((command) => command.id === "profiles.import-package"));
  assert.ok(tubeDesignerRibbonDefinition.tabs
    .find((tab) => tab.id === "profiles")
    .groups.flatMap((group) => group.commands)
    .some((command) => command.id === "profiles.export-step"));

  const systemView = {
    pending: false,
    tubeDesignerSelectedProfileId: "system:round",
    tubeDesignerSystemProfiles: [system],
    tubeDesignerUserData: { profiles: [editable, frozen] },
  };
  const systemRight = renderProfileLibraryRightPane({}, systemView);
  assert.match(systemRight, /系统内置管型/);
  assert.match(systemRight, /预览参数/);
  assert.match(systemRight, /data-tube-profile-editor-parameter="width"/);
  assert.match(systemRight, /仅影响当前预览与本次导出/);
  assert.doesNotMatch(systemRight, /data-tube-profile-editor-name/);
  assert.doesNotMatch(systemRight, /tube-designer-profile-library-save/);
  assert.doesNotMatch(systemRight, /tube-designer-profile-library-delete/);
  assert.match(systemRight, /data-tube-designer-profile-key="system:round"/);
}

function testTubeDesignerProfileSvgPreservesCurvesAndVisibleBounds() {
  const renderMiniature = (profile) => renderProfileLibraryRightPane({}, {
    pending: false,
    tubeDesignerSelectedProfileId: profile.id,
    tubeDesignerUserData: { profiles: [profile] },
  });
  const outerPathTag = (html) => (html.match(/<path\b[^>]*>/g) ?? [])
    .find((tag) => /\bclass="outer"/.test(tag)) ?? "";
  const pathData = (tag) => tag.match(/\sd="([^"]+)"/)?.[1] ?? "";
  const assertViewBoxMargin = (html, bounds, label) => {
    const svgTag = html.match(/<svg\b[^>]*class="[^"]*tube-profile-library-svg[^"]*"[^>]*>/)?.[0] ?? "";
    const match = svgTag.match(/\bviewBox="([^"]+)"/);
    assert.ok(match, `${label} should render an SVG viewBox`);
    const [x, y, width, height] = match[1].trim().split(/\s+/).map(Number);
    assert.ok([x, y, width, height].every(Number.isFinite), `${label} viewBox should be numeric`);
    assert.ok(x < bounds.minX, `${label} should have left padding`);
    assert.ok(y < bounds.minY, `${label} should have bottom padding`);
    assert.ok(x + width > bounds.maxX, `${label} should have right padding`);
    assert.ok(y + height > bounds.maxY, `${label} should have top padding`);
  };
  const baseProfile = {
    revision: 1,
    profileType: "imported-dxf",
    sourceFileName: "curve.dxf",
    sourceUnit: "毫米",
    contourCount: 2,
  };

  const circleHtml = renderMiniature({
    ...baseProfile,
    id: "svg-circle",
    name: "圆管",
    width: 40,
    depth: 40,
    contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 16 }],
  });
  assert.match(circleHtml, /<svg\b[^>]*preserveAspectRatio="xMidYMid meet"/);
  assert.match(circleHtml, /<circle\b[^>]*class="outer"/);
  assert.doesNotMatch(circleHtml, /<polygon\b[^>]*class="outer"/);
  assertViewBoxMargin(circleHtml, { minX: -20, minY: -20, maxX: 20, maxY: 20 }, "circle");

  const offsetCircleHtml = renderMiniature({
    ...baseProfile,
    id: "svg-offset-circle",
    name: "偏心圆",
    width: 10,
    depth: 10,
    contourCount: 1,
    contours: [{ kind: "circle", center: [10, 20], radius: 5 }],
  });
  assertViewBoxMargin(
    offsetCircleHtml,
    { minX: 5, minY: -25, maxX: 15, maxY: -15 },
    "offset circle after SVG Y-axis inversion",
  );

  const ellipseHtml = renderMiniature({
    ...baseProfile,
    id: "svg-ellipse",
    name: "椭圆管",
    width: 60,
    depth: 30,
    contours: [
      { kind: "ellipse", width: 60, height: 30 },
      { kind: "ellipse", width: 56, height: 26 },
    ],
  });
  assert.match(ellipseHtml, /<ellipse\b[^>]*class="outer"/);
  assert.doesNotMatch(ellipseHtml, /<polygon\b[^>]*class="outer"/);
  assertViewBoxMargin(ellipseHtml, { minX: -30, minY: -15, maxX: 30, maxY: 15 }, "ellipse");

  const quarterWeight = Math.SQRT1_2;
  const nurbsEllipseHtml = renderMiniature({
    ...baseProfile,
    id: "svg-nurbs-ellipse",
    name: "四段 NURBS 椭圆管",
    width: 60,
    depth: 30,
    contourCount: 1,
    contours: [{
      kind: "path",
      segments: [
        [[30, 0], [30, 15], [0, 15]],
        [[0, 15], [-30, 15], [-30, 0]],
        [[-30, 0], [-30, -15], [0, -15]],
        [[0, -15], [30, -15], [30, 0]],
      ].map((controlPoints) => ({
        kind: "nurbs",
        degree: 2,
        controlPoints,
        knots: [0, 0, 0, 1, 1, 1],
        weights: [1, quarterWeight, 1],
      })),
    }],
  });
  const nurbsEllipsePath = pathData(outerPathTag(nurbsEllipseHtml));
  assert.ok((nurbsEllipsePath.match(/[ACQ]/gi) ?? []).length >= 4,
    "four NURBS ellipse quarters should remain four smooth curve segments");
  assert.match(nurbsEllipsePath, /Z\s*$/i, "four NURBS ellipse quarters should close the contour");
  assert.doesNotMatch(nurbsEllipseHtml, /<polygon\b[^>]*class="outer"/,
    "four NURBS ellipse quarters must not collapse to a four-point polygon");
  assertViewBoxMargin(
    nurbsEllipseHtml,
    { minX: -30, minY: -15, maxX: 30, maxY: 15 },
    "NURBS ellipse",
  );

  const arcHtml = renderMiniature({
    ...baseProfile,
    id: "svg-arcs",
    name: "圆弧管",
    width: 40,
    depth: 40,
    contourCount: 1,
    contours: [{
      kind: "path",
      segments: [
        { kind: "arc", start: [-20, 0], middle: [0, 20], end: [20, 0] },
        { kind: "arc", start: [20, 0], middle: [0, -20], end: [-20, 0] },
      ],
    }],
  });
  const arcPath = outerPathTag(arcHtml);
  assert.match(arcPath, /\sd="[^"]*\bA\b[^"]*"/, "circular arcs should use SVG arc commands");
  assert.doesNotMatch(arcHtml, /<polygon\b[^>]*class="outer"/);
  assertViewBoxMargin(arcHtml, { minX: -20, minY: -20, maxX: 20, maxY: 20 }, "arc path");

  const plumControlPoints = [
    [0, -31.6578], [14.6946, -16.8832], [33.287, -7.4734],
    [23.7764, 11.0676], [20.5725, 31.6578], [0, 28.3422],
    [-20.5725, 31.6578], [-23.7764, 11.0676],
    [-33.287, -7.4734], [-14.6946, -16.8832],
  ];
  const plumHtml = renderMiniature({
    ...baseProfile,
    id: "svg-five-petal-plum",
    name: "五瓣梅花管",
    sourceFileName: "06_DXF五瓣梅花管_外径约60.dxf",
    width: 66.574,
    depth: 63.3156,
    contours: [
      {
        kind: "path",
        segments: [{
          kind: "bspline",
          degree: 3,
          periodic: true,
          knots: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
          controlPoints: plumControlPoints,
        }],
      },
      { kind: "circle", radius: 12 },
    ],
  });
  const plumPath = outerPathTag(plumHtml);
  assert.match(plumPath, /\sd="[^"]+"/,
    "five-petal plum spline should produce visible SVG geometry");
  assertViewBoxMargin(
    plumHtml,
    { minX: -33.287, minY: -31.6578, maxX: 33.287, maxY: 31.6578 },
    "five-petal plum",
  );
}

async function testTubeDesignerHydratesProfilePreviewIntoTheThreeDimensionalViewport() {
  const applied = [];
  const fitted = [];
  const logs = [];
  const profile = {
    id: "8c31dc03-39a9-44cb-9f0e-293a1158f48d",
    revision: 1,
    profileType: "imported-dxf",
    name: "自动化圆管",
    specification: "DXF 40 × 40 mm",
    width: 40,
    depth: 40,
    contours: [{ kind: "circle", radius: 20 }],
  };
  const view = {
    activeAreaId: "profiles",
    tubeDesignerSelectedProfileId: profile.id,
    tubeDesignerUserData: { profiles: [profile] },
    viewport: {
      getAppliedViewState: () => ({ revision: "0", entityIds: [] }),
      applyViewSnapshot: async (snapshot, resources) => {
        applied.push({ snapshot, resources });
        return { applied: true, revision: snapshot.revision, entityIds: [`user:${profile.id}`] };
      },
      setVisibleEntityIds: () => {},
      setSelectedObjectIds: () => {},
      fitViewForRevision: (revision) => fitted.push(revision),
      setStandardView: () => {},
    },
  };
  const resources = { get: async () => null };
  const context = {
    actions: { log: (level, message) => logs.push({ level, message }) },
    sceneProxy: {
      resources,
      invoke: async (method, payload) => {
        assert.equal(method, "TubeDesigner.GenerateProfilePreview");
        assert.deepEqual(payload.profileRef, { scope: "user", id: profile.id });
        assert.equal(payload.length, 1000);
        return {
          geometryResourceId: "resource://profile-preview.mesh",
          geometryResourceVersion: 7,
          materialResourceId: "resource://profile-preview.material",
          materialResourceVersion: 2,
          profile,
        };
      },
    },
  };
  const startedAt = performance.now();
  renderProfileLibraryViewportOverlay(context, view);
  await delay(0);
  await delay(PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS + 20);
  assert.equal(applied.length, 1);
  assert.equal(applied[0].resources, resources);
  assert.equal(applied[0].snapshot.rows[0].entityId, `user:${profile.id}`);
  assert.equal(applied[0].snapshot.rows[0].data.geometry.version, 7);
  assert.equal(fitted.length, 1);
  assert.ok(performance.now() - startedAt >= PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS);
  assert.ok(logs.some((entry) => entry.message.includes("三维管型已生成")));

  const cachedStartedAt = performance.now();
  renderProfileLibraryViewportOverlay(context, view);
  await delay(30);
  assert.equal(applied.length, 2, "a cached preview may be reapplied when another resource is visible");
  assert.equal(view.tubeDesignerProfilePreviewRequest, null);
  assert.ok(
    performance.now() - cachedStartedAt < PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS,
    "a fast cached preview must finish without forcing a half-second progress flash",
  );
}

async function testTubeDesignerSystemProfilePreviewUsesScopedReference() {
  const profile = {
    id: "round", profileType: "parametric-package", name: "圆管",
    descriptor: {
      id: "round", version: "2.0.0",
      parameters: [
        { key: "width", displayName: { "zh-CN": "外径" }, valueType: "number", defaultValue: 40 },
        { key: "wallThickness", displayName: { "zh-CN": "壁厚" }, valueType: "number", defaultValue: 2 },
      ],
    },
    defaultParameters: { width: 40, wallThickness: 2 },
    previewProfile: {
      name: "圆管", specification: "Φ40 × 2", width: 40, depth: 40,
      contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }],
    },
  };
  let invocation = null;
  let finish = null;
  const view = {
    activeAreaId: "profiles",
    tubeDesignerSelectedProfileId: "system:round",
    tubeDesignerSystemProfiles: [profile],
    tubeDesignerUserData: { profiles: [] },
  };
  const context = {
    actions: { log: () => {} },
    sceneProxy: {
      invoke(method, payload) {
        invocation = { method, payload };
        return new Promise((resolve) => { finish = resolve; });
      },
    },
  };
  renderProfileLibraryViewportOverlay(context, view);
  await delay(0);
  assert.equal(invocation?.method, "TubeDesigner.GenerateProfilePreview");
  assert.deepEqual(invocation?.payload?.profileRef, { scope: "system", id: "round" });
  assert.deepEqual(invocation?.payload?.parameters, { width: 40, wallThickness: 2 });
  assert.equal(view.tubeDesignerSelectedProfileId, "system:round");
  view.activeAreaId = "view";
  view.tubeDesignerProfilePreviewRequest = null;
  finish?.({});
  await delay(0);
}

function testViewportOwnsTransientInteraction() {
  const viewportSource = readFileSync(
    new URL("../../iCAX-UI/SDK/Viewport/threeViewport.mjs", import.meta.url),
    "utf8",
  );
  assert.doesNotMatch(viewportSource, /InputPDO|writeInputStatePDO|flushInputPDO/);
  assert.match(viewportSource, /#applyLocalNavigation/);
  assert.match(viewportSource, /getCameraState\(\)/);
  assert.match(viewportSource, /setCameraState\(value = \{\}\)/);
  assert.match(viewportSource, /setPickingEnabled\(enabled\)/);
  assert.match(viewportSource, /setContinuousRendering\(enabled\)/);
  assert.match(viewportSource, /getProjectionMode\(\)/);
  assert.match(viewportSource, /setProjectionMode\(value, controlOptions = \{\}\)/);
  assert.match(viewportSource, /new THREE\.OrthographicCamera/);
  assert.match(viewportSource, /icax-three-projection-toggle/);
  assert.match(viewportSource, /setOrbitConstrained\(constrained\)/);
  assert.match(viewportSource, /setDimensionAnnotations\(annotations = \[\]\)/);
  assert.match(viewportSource, /fitViewToViewport\(padding = 1\.18\)/);
  assert.match(viewportSource, /setPresentationAxis\(sourceAxis, targetAxis = \[1, 0, 0\]\)/);

  const inspectionSource = readFileSync(
    new URL("../../apps/tube-designer/webpage/partInspection.mjs", import.meta.url),
    "utf8",
  );
  assert.match(inspectionSource, /continuousRender:\s*false/);
  assert.match(inspectionSource, /constrainOrbit:\s*false/);
  assert.match(inspectionSource, /showProjectionToggle:\s*true/);
  assert.match(inspectionSource, /pickingEnabled:\s*false/);
  assert.match(inspectionSource, /antialias:\s*true/);
  assert.match(inspectionSource, /pixelRatioCap:\s*2/);
  assert.match(inspectionSource, /fitViewToViewport\(1\.16\)/);
  assert.match(inspectionSource, /tubeInspectionDimensionCount/);

  const designerEntrySource = readFileSync(
    new URL("../../apps/tube-designer/webpage/entry.mjs", import.meta.url),
    "utf8",
  );
  assert.match(designerEntrySource, /setProjectionToggleVisible\?\.\(!\["sketch", "about"\]\.includes\(normalizedAreaId\)\)/);
  assert.match(designerEntrySource, /setOrbitConstrained\?\.\(normalizedAreaId === "view"\)/);
  assert.match(designerEntrySource, /tubeDesignerProjectionModes\[currentAreaId\] = mode/);
}

function testStandardViewDirections() {
  assert.deepEqual(resolveStandardViewDirection("front"), [0, -1, 0]);
  assert.deepEqual(resolveStandardViewDirection("top-front-right"), [1, -1, 1]);
  assert.deepEqual(resolveStandardViewDirection("bottom-front-left"), [-1, -1, -1]);
  assert.deepEqual(resolveStandardViewDirection("left-bottom"), [-1, 0, -1]);
  assert.deepEqual(resolveStandardViewDirection("iso"), [1, -1, 0.78]);
  assert.equal(resolveStandardViewDirection("front-back"), null);
  assert.equal(resolveStandardViewDirection("unknown"), null);
}

function testTubeDesignerAppliesDefaultViewAfterNewRevisionIsFitted() {
  const events = [];
  const view = {
    viewport: {
      fitViewForRevision(revision) {
        events.push(["fit", revision]);
        return { fitted: true, revision, renderSequence: 41 };
      },
      setViewDirection(direction) {
        events.push(["view-direction", direction]);
        return true;
      },
      getAppliedViewState() {
        events.push(["receipt"]);
        return { revision: "revision-42", renderSequence: 42 };
      },
    },
  };

  const product = {
    templateId: "two-face-security-window",
    parameters: { sidePosition: "right" },
  };
  assert.deepEqual(fitDesignerDefaultView(view, "revision-42", product), {
    revision: "revision-42",
    defaultViewDirection: [0.18, -1, 0.08],
    fitRenderSequence: 41,
    defaultViewRenderSequence: 42,
  });
  assert.deepEqual(events, [
    ["fit", "revision-42"],
    ["view-direction", [0.18, -1, 0.08]],
    ["receipt"],
  ]);
  assert.deepEqual(getDesignerDefaultViewDirection({
    templateId: "two-face-security-window",
    parameters: { sidePosition: "left" },
  }), [-0.18, -1, 0.08]);
  assert.deepEqual(getDesignerDefaultViewDirection(product), [0.18, -1, 0.08]);
}

function testPartThumbnailNormalizesTheLongestAxisHorizontally() {
  const makeTubeVertices = (start, end, radius = 2) => {
    const values = [];
    for (const point of [start, end]) {
      for (const offset of [[-radius, -radius], [radius, -radius], [radius, radius], [-radius, radius]]) {
        values.push(point[0] + offset[0], point[1] + offset[1], point[2]);
      }
    }
    return values;
  };
  const projectedSpan = (vertices) => {
    const points = projectThumbnailVertices(vertices);
    const xs = points.map((point) => point.x);
    const ys = points.map((point) => point.y);
    return {
      width: Math.max(...xs) - Math.min(...xs),
      height: Math.max(...ys) - Math.min(...ys),
    };
  };
  for (const vertices of [
    makeTubeVertices([-50, 0, 0], [50, 0, 0]),
    makeTubeVertices([0, -50, 0], [0, 50, 0]),
    makeTubeVertices([0, 0, -50], [0, 0, 50]),
  ]) {
    const span = projectedSpan(vertices);
    assert.ok(span.width > span.height * 5, `thumbnail was not horizontal: ${JSON.stringify(span)}`);
  }
}

function testViewResourceParsing() {
  const snapshot = parseViewResource(makeViewResourceFixture());
  assert.equal(snapshot.revision, "3");
  assert.equal(snapshot.count, 1);
  assert.deepEqual(snapshot.sources, [
    { sourceId: "scene", role: "scene" },
    { sourceId: "workpieces", role: "workpiece" },
  ]);
  assert.deepEqual(snapshot.entityIds, [
    "00112233-4455-6677-8899-aabbccddeeff",
  ]);
  assert.deepEqual(snapshot.rows[0].sourceIds, ["scene", "workpieces"]);
  assert.equal(snapshot.rows[0].data.visible, true);
  assert.equal(
    snapshot.rows[0].components.CRenderInstanceComponent.present,
    true,
  );
  assert.equal(
    snapshot.rows[0].components.CTransformComponent.present,
    false,
  );
}

function testRenderResourceParsing() {
  const geometry = parseRenderGeometryResource(makeRenderGeometryResourceFixture());
  assert.equal(geometry.kind, "mesh");
  assert.equal(geometry.dataVersion, "7");
  assert.equal(geometry.flags, 3);
  assert.deepEqual([...geometry.positions], [0, 0, 0, 10, 0, 0, 0, 10, 0]);
  assert.deepEqual([...geometry.indices], [0, 1, 2]);
  assert.deepEqual([...geometry.vertexColors], [0xff3366ff, 0xff3366ff, 0xff3366ff]);

  const material = parseRenderMaterialResource(makeRenderMaterialResourceFixture());
  assert.equal(material.dataVersion, "9");
  assert.equal(material.colorRGBA, 0x3366ccff);
  assert.equal(material.lineWidth, 2.5);
  assert.equal(material.baseColorTextureUrl,
    "icax-resource://app/product/project/scene/texture");
}

async function testRenderResourceLoaderUsesViewReferenceVersion() {
  let requestedUrl = "";
  let requestedVersion = "";
  const loaded = await loadRenderResource({
    get: async (url, { headers }) => {
      requestedUrl = url;
      requestedVersion = headers.get("ICAX-Resource-Version");
      return new Response(makeRenderGeometryResourceFixture().slice(0), { status: 200 });
    },
  }, {
    url: "icax-resource://app/product/project/scene/geometry",
    version: "7",
  });
  assert.equal(requestedUrl, "icax-resource://app/product/project/scene/geometry");
  assert.equal(requestedVersion, "7");
  assert.equal(loaded.type, "geometry");
  assert.equal(loaded.data.dataVersion, "7");
}

async function testTubePreviewLoadsStandardRenderGeometry() {
  let requestedVersion = "";
  const preview = await loadPreviewMeshResource({
    get: async (_url, { headers }) => {
      requestedVersion = headers.get("ICAX-Resource-Version");
      return new Response(makeRenderGeometryResourceFixture().slice(0), {
        status: 200,
        headers: {
          "ICAX-FlatBuffer-Identifier": "ICRG",
          "ICAX-Resource-Version": "7",
        },
      });
    },
  }, {
    url: "icax-resource://app/product/project/scene/tube-preview",
    version: 7,
  });
  assert.equal(requestedVersion, "7");
  assert.equal(preview.format, "render-geometry");
  assert.equal(preview.resourceVersion, 7);
  assert.equal(preview.vertexCount, 3);
  assert.equal(preview.triangleCount, 1);
  assert.deepEqual([...preview.indices], [0, 1, 2]);
}

function testProjectAreaMembershipIsolation() {
  const projectView = {
    activeAreaId: "",
    areas: {},
    layout: { leftWidth: 320, rightWidth: 340 },
    selectedSceneObjectId: "",
    selectedMachineInstanceId: "",
  };
  activateProjectArea(projectView, "machine");
  setProjectAreaViewContent(projectView, "machine", {
    revision: "1",
    entityIds: ["machine-axis"],
  });
  projectView.layout.leftWidth = 410;
  projectView.selectedSceneObjectId = "machine-axis";
  activateProjectArea(projectView, "machine");
  assert.equal(projectView.layout.leftWidth, 410);
  assert.equal(projectView.selectedSceneObjectId, "machine-axis");
  activateProjectArea(projectView, "workpiece");
  setProjectAreaViewContent(projectView, "workpiece", {
    revision: "4",
    entityIds: ["workpiece-1"],
  });
  assert.equal(projectView.layout.leftWidth, 320);
  assert.equal(projectView.selectedSceneObjectId, "");
  assert.deepEqual([...getProjectArea(projectView, "workpiece").viewContent.entityIds], ["workpiece-1"]);
  projectView.layout.leftWidth = 270;
  projectView.selectedSceneObjectId = "workpiece-1";
  activateProjectArea(projectView, "machine");
  assert.equal(projectView.layout.leftWidth, 410);
  assert.equal(projectView.selectedSceneObjectId, "machine-axis");
  assert.deepEqual([...getProjectArea(projectView, "machine").viewContent.entityIds], ["machine-axis"]);
}

async function testViewReadersUseSceneChannelAndResourceSnapshots() {
  const payload = makeViewResourceFixture();
  const sceneInvocations = [];
  const viewId = "11111111-2222-4333-8444-555555555555";
  const sceneProxy = {
    invoke: async (method, request) => {
      sceneInvocations.push({ method, request });
      if (method === "View.Release") {
        return { viewId: request.viewId, released: true };
      }
      return {
        viewId,
        resource: {
          url: "icax-resource://app/product/project/scene/view",
          version: "3",
          format: "ICVW",
        },
      };
    },
    resources: {
      head: async () => new Response(null, {
        status: 200,
        headers: { "ICAX-Resource-Version": "3" },
      }),
      get: async () => new Response(payload.slice(0), {
        status: 200,
        headers: { "ICAX-Resource-Version": "3" },
      }),
    },
  };
  const client = new ViewClient(sceneProxy);
  const definition = {
    sources: [
      {
        sourceId: "scene",
        role: "scene",
        where: "WHERE HAS CRenderInstanceComponent",
      },
      {
        sourceId: "workpieces",
        role: "workpiece",
        where: "WHERE HAS CWorkpieceComponent",
      },
    ],
  };
  const first = await client.start(definition);
  const second = await client.start(definition);

  assert.notEqual(first, second);
  assert.equal(first.viewId, second.viewId);
  assert.equal(sceneInvocations.length, 2);
  assert.ok(sceneInvocations.every(({ method }) =>
    method === "View.GetOrCreate"));
  assert.equal((await first.poll()).revision, "3");
  assert.equal((await second.poll()).revision, "3");
  assert.equal(await first.stop(), true);
  assert.equal(await first.stop(), false);
  assert.equal(await second.stop(), true);
  assert.deepEqual(
    sceneInvocations.map(({ method }) => method),
    [
      "View.GetOrCreate",
      "View.GetOrCreate",
      "View.Release",
      "View.Release",
    ],
  );
  assert.equal(sceneInvocations[0].request.sources.length, 2);
  assert.deepEqual(sceneInvocations[2].request, { viewId });
  assert.deepEqual(sceneInvocations[3].request, { viewId });
  await client.dispose();
}

function makeViewResourceFixture() {
  const bytes = Buffer.from(
    "GAAAAElDVlcAAA4AGAAAAAQAEAAIAAwADgAAAPABAACIAQAADAAAAAMAAAAAAAAAAQAAABAAAAAAAAoAEAAEAAgADAAKAAAANAEAAAgBAAAEAAAAAgAAAHAAAAAQAAAADAAMAAQAAAAAAAgADAAAADQAAAAEAAAAJgAAAHsiX192YXJpYW50X3R5cGUiOiJPYmplY3QiLCJ2YWx1ZSI6e319AAATAAAAQ1RyYW5zZm9ybUNvbXBvbmVudAAMABAACAAGAAcADAAMAAAAAAABAWQAAAAEAAAAVgAAAHsiX192YXJpYW50X3R5cGUiOiJPYmplY3QiLCJ2YWx1ZSI6eyJ2aXNpYmxlIjp7Il9fdmFyaWFudF90eXBlIjoiYm9vbCIsInZhbHVlIjp0cnVlfX19AAAYAAAAQ1JlbmRlckluc3RhbmNlQ29tcG9uZW50AAAAAAIAAAAYAAAABAAAAAoAAAB3b3JrcGllY2VzAAAFAAAAc2NlbmUAAAAkAAAAMDAxMTIyMzMtNDQ1NS02Njc3LTg4OTktYWFiYmNjZGRlZWZmAAAAAAIAAAA8AAAABAAAANT///8YAAAABAAAAAkAAAB3b3JrcGllY2UAAAAKAAAAd29ya3BpZWNlcwAACAAMAAQACAAIAAAAFAAAAAQAAAAFAAAAc2NlbmUAAAAFAAAAc2NlbmUAAAAkAAAAMTExMTExMTEtMjIyMi00MzMzLTg0NDQtNTU1NTU1NTU1NTU1AAAAAA==",
    "base64",
  );
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

function makeRenderGeometryResourceFixture() {
  const bytes = Buffer.from(
    "JAAAAElDUkcAAAAAGAAwAAAABAAkAAgADAAQABQAGAAcACAAGAAAAAEAAAABAAAAAwAAAIQAAABYAAAAOAAAACQAAAAQAAAABwAAAAAAAAAAAAAAAwAAAAAAAAABAAAAAgAAAAMAAAD/ZjP//2Yz//9mM/8GAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/CQAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwkAAAAAAAAAAAAAAAAAAAAAACBBAAAAAAAAAAAAAAAAAAAgQQAAAAA=",
    "base64",
  );
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

function makeRenderMaterialResourceFixture() {
  const bytes = Buffer.from(
    "IAAAAElDUk0YACgAAAAgAAQACAAMAAAAEAAUABgAHAAYAAAA/8xmM/8RERH/////AAAgQAMAAAAEAAAADAAAAAkAAAAAAAAAMQAAAGljYXgtcmVzb3VyY2U6Ly9hcHAvcHJvZHVjdC9wcm9qZWN0L3NjZW5lL3RleHR1cmUAAAA=",
    "base64",
  );
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

async function testBridgeValidation() {
  assert.throws(() => validateBridge({}), /getApplicationChannelId/);
  assert.ok(validateBridge(new MockHostBridge()) instanceof MockHostBridge);
  const bridge = new MockHostBridge({ directoryDialogPath: "D:/exports" });
  assert.equal(await bridge.openDirectoryDialog({ title: "Choose export folder" }), "D:/exports");
}

function testChannelIdValidation() {
  assert.equal(isUsableChannelId("00000000-0000-0000-0000-000000000000"), false);
  assert.equal(isUsableChannelId("00000000-0000-4000-8000-000000000001"), true);
  assert.throws(() => ensureUsableChannelId("00000000-0000-0000-0000-000000000000"), /non-nil channel id/);
}

async function testSDOPromiseFlow() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { sdo: { timeoutMs: 1000 } } });

  const state = await app.getState();
  assert.equal(state.state, "Running");
  assert.equal(state.products.length, 1);
  assert.equal(app.getProduct("icax.mock-product"), null);

  const product = await app.startProduct("icax.mock-product");
  assert.ok(product instanceof ProductProxy);
  assert.equal(product.productId, "icax.mock-product");
  assert.ok(product.productChannelId);
  assert.equal(app.getProduct(product.productId), product);

  const stoppedState = await app.stopProduct(product.productId);
  assert.equal(stoppedState.runningProductCount, 0);
  assert.equal(app.getProduct(product.productId), null);

  const restartedProduct = await app.startProduct("icax.mock-product");
  assert.ok(restartedProduct instanceof ProductProxy);

  const created = await restartedProduct.openProjectCatalog("D:/projects/created.icax", {
    catalogName: "Created Catalog",
    projectName: "Created Project",
  });
  assert.ok(created.projectProxy instanceof ProjectProxy);
  assert.ok(created.sceneProxy instanceof SceneProxy);
  assert.equal(restartedProduct.getProject(created.projectProxy.projectId), created.projectProxy);
  assert.equal(restartedProduct.state.catalogs.length, 1);
  assert.equal(restartedProduct.state.catalogs[0].catalogPath, "D:/projects/created.icax");

  const resolved = await app.resolveProjectFile("D:/projects/mock.icax");
  assert.equal(resolved.resolve.status, "Resolved");
  assert.equal(resolved.resolve.productId, "icax.mock-product");

  const opened = await app.openProjectFile("D:/projects/mock.icax");
  assert.equal(opened.resolve.status, "Resolved");
  assert.equal(opened.catalog.mainProject.state, "Running");
  assert.ok(opened.projectProxy instanceof ProjectProxy);
  assert.ok(opened.sceneProxy instanceof SceneProxy);
  assert.equal(opened.productProxy.getProject(opened.projectProxy.projectId), opened.projectProxy);
  assert.equal(opened.projectProxy.getMainScene(), opened.sceneProxy);

  const sceneState = await opened.sceneProxy.getState();
  assert.equal(sceneState.sceneChannelId, opened.sceneProxy.sceneChannelId);
  assert.equal(sceneState.sceneName, "Main Scene");

  const undoRedoState = await opened.sceneProxy.getUndoRedoState();
  assert.deepEqual(undoRedoState.undoSteps, []);
  assert.deepEqual(await opened.sceneProxy.invoke(ProjectSDO.undo), undoRedoState);
}

async function testSceneChannelRegistrationFromProjectState() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { sdo: { timeoutMs: 1000 } } });
  const product = await app.startProduct("icax.mock-product");
  bridge.projectOpened = true;

  const project = await product.adoptProject({
    projectId: "00000000-0000-4000-8000-000000000401",
    projectName: "Registered Later",
    projectPath: "D:/projects/mock.icax",
    mainSceneId: "00000000-0000-4000-8000-000000000501",
    mainScene: {
      sceneId: "00000000-0000-4000-8000-000000000501",
      sceneName: "Main Scene",
    },
    scenes: [{
      sceneId: "00000000-0000-4000-8000-000000000501",
      sceneName: "Main Scene",
    }],
  });

  assert.ok(project instanceof ProjectProxy);
  assert.ok(project.getMainScene() instanceof SceneProxy);
  assert.equal(project.getMainScene().sceneChannelId, "00000000-0000-4000-8000-000000000201");
}

async function testProductProxyCanInvokeProductSpecificMethods() {
  const calls = [];
  const proxy = new ProductProxy({
    bridge: null,
    async invoke(...args) {
      calls.push(args);
      return { customers: [] };
    },
  }, {
    productId: "icax.tube-designer",
    productChannelId: "00000000-0000-4000-8000-000000000811",
  });
  assert.deepEqual(
    await proxy.invoke("TubeDesigner.ListUserData", {}, { timeoutMs: 30000 }),
    { customers: [] },
  );
  assert.deepEqual(calls, [[
    "00000000-0000-4000-8000-000000000811",
    "TubeDesigner.ListUserData",
    {},
    { timeoutMs: 30000 },
  ]]);
}

async function testDirectResourceAccess() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { sdo: { timeoutMs: 1000 } } });
  const opened = await app.openProjectFile("D:/projects/resources.icax");
  const resources = opened.sceneProxy.resources;
  assert.ok(app.resources instanceof ResourceClient);
  assert.ok(opened.productProxy.resources instanceof ResourceClient);
  assert.ok(opened.projectProxy.resources instanceof ResourceClient);
  assert.ok(resources instanceof ResourceClient);

  const scope = {
    applicationId: "icax",
    productId: "icax.mock-product",
    projectId: opened.projectProxy.projectId,
    sceneId: opened.sceneProxy.sceneId,
  };
  const allocated = allocateResourceURL(scope);
  const url = allocated.url;
  assert.equal(parseResourceURL(url).resourceId, allocated.resourceId);
  assert.throws(
    () => parseResourceURL(`${makeResourceCollectionURL(scope)}/`),
    /empty path segment/,
  );
  assert.throws(
    () => parseResourceURL(url.replace("/resources/", "//resources/")),
    /empty path segment/,
  );
  assert.throws(
    () => makeResourceCollectionURL({
      applicationId: "icax..unsafe",
    }),
    /URL-safe stable ID/,
  );
  const source = new Uint8Array([8, 0, 0, 0, 73, 67, 82, 83, 1, 2, 3, 4]);
  const created = await resources.create(url, source, {
    headers: {
      "Content-Type": "application/vnd.icax.flatbuffer",
      "ICAX-Resource-Type": "test.transport",
      "ICAX-Schema-Version": "1",
    },
  });
  assert.equal(created.status, 201);
  assert.equal(created.headers.get("ETag"), "\"icax-v1\"");

  const head = await resources.head(url);
  assert.equal(head.status, 200);
  assert.equal(head.headers.get("Content-Length"), String(source.byteLength));
  assert.equal(head.headers.get("ICAX-Schema-Version"), "1");
  assert.equal((await head.arrayBuffer()).byteLength, 0);

  const fetched = await opened.sceneProxy.fetchResource(url);
  assert.equal(fetched.status, 200);
  assert.deepEqual(
    [...new Uint8Array(await fetched.arrayBuffer())],
    [...source],
  );

  const cached = await resources.get(url, {
    headers: { "If-None-Match": "\"icax-v1\"" },
  });
  assert.equal(cached.status, 304);

  const removed = await resources.delete(url, {
    headers: { "If-Match": "\"icax-v1\"" },
  });
  assert.equal(removed.status, 204);
  assert.equal((await resources.get(url)).status, 404);

  const posted = await resources.post(
    makeResourceCollectionURL(scope),
    source,
    {
      headers: {
        "Content-Type": "application/vnd.icax.flatbuffer",
      },
    },
  );
  assert.equal(posted.status, 201);
  const postedURL = posted.headers.get("Location");
  assert.equal(parseResourceURL(postedURL).scope, "scene");
  assert.equal((await resources.get(postedURL)).status, 200);
}

async function testSDOEventFlow() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const sdo = new SDOClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();

  const events = [];
  const unsubscribeStateChanged = sdo.subscribe(channelId, "App.StateChanged", (event) => events.push(event));
  const unsubscribeAll = sdo.subscribeAll(channelId, (event) => events.push({ wildcard: true, event }));

  bridge.emitSDOFrame(channelId, "App.StateChanged", { state: "Running", phase: "Running" });
  await delay(10);

  assert.equal(events.length, 2);
  assert.equal(events[0].callId, 0);
  assert.equal(events[0].kind, SDOFrameKind.Event);
  assert.equal(events[0].ok, true);
  assert.equal(events[0].payload.state, "Running");
  assert.equal(events[1].wildcard, true);
  assert.equal(events[1].event.payload.phase, "Running");

  unsubscribeStateChanged();
  unsubscribeAll();
  bridge.emitSDOFrame(channelId, "App.StateChanged", { state: "Stopped" });
  await delay(10);
  assert.equal(events.length, 2);
}

async function testSDOReportFlow() {
  const bridge = new MockHostBridge({ delayMs: 50 });
  const sdo = new SDOClient(bridge, { timeoutMs: 40 });
  const channelId = await bridge.getApplicationChannelId();
  const reports = [];
  let completed = false;

  const task = sdo.invoke(channelId, AppSDO.getState, {}, {
    onReport: (report) => reports.push(report),
  });
  task.then(() => {
    completed = true;
  });

  assert.equal(task.callId > 0, true);
  assert.equal(typeof task.onReport, "function");
  assert.equal(sdo.pending.has(String(task.callId)), true);

  bridge.emitReport(channelId, task.callId, AppSDO.getState, {
    progress: 0.25,
    state: "Recognizing",
    message: "Recognizing features",
  }, { delayMs: 20 });

  await delay(30);
  assert.equal(completed, false);
  assert.equal(sdo.pending.has(String(task.callId)), true);
  assert.equal(reports.length, 1);
  assert.equal(reports[0].payload.progress, 0.25);
  assert.equal(reports[0].payload.state, "Recognizing");
  assert.equal(reports[0].kind, SDOFrameKind.Report);
  assert.equal(task.getLatestReport(), reports[0]);

  const state = await task;
  assert.equal(state.state, "Running");
  assert.equal(sdo.pending.has(String(task.callId)), false);
}

async function testFrontendSDOExposure() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const sdo = new SDOClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();
  const callId = 7001;

  const unexpose = sdo.expose(channelId, "Frontend.GetSelection", async (payload, call) => {
    await call.report({ phase: "Reading" });
    return { selection: [payload.requestedId] };
  });

  bridge.emitRequest(channelId, callId, "Frontend.GetSelection", { requestedId: "part-42" });
  await delay(15);

  const frames = bridge.postedFrames.filter((frame) => Number(frame.callId) === callId);
  assert.deepEqual(frames.map((frame) => frame.kind), [SDOFrameKind.Report, SDOFrameKind.Response]);
  assert.equal(frames.every((frame) => Number(frame.callId) === callId), true);
  assert.equal(deserializeVariantText(frames[0].payloadText).phase, "Reading");
  assert.deepEqual(deserializeVariantText(frames[1].payloadText).selection, ["part-42"]);

  unexpose();
  sdo.dispose();
}

async function testSDODispose() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const sdo = new SDOClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();

  const events = [];
  sdo.subscribe(channelId, "App.StateChanged", (event) => events.push(event));
  assert.equal(sdo.startedChannelIds.has(channelId), true);

  sdo.dispose();
  assert.equal(sdo.startedChannelIds.has(channelId), false);

  bridge.emitSDOFrame(channelId, "App.StateChanged", { state: "Running" });
  await delay(10);
  assert.equal(events.length, 0);
}

async function testPDOBridgeInjection() {
  const calls = [];
  const bridge = {
    pdo: {
      withRead(options, reader) {
        calls.push(options);
        return reader(new DataView(new ArrayBuffer(options.payloadSize)));
      },
    },
  };
  const declaration = {
    id: makePDOID("PreviewMesh", "MainViewport"),
    version: 7,
    payloadSize: 16,
  };
  const pdo = new PDOClient({
    enabled: true,
    sharedArenaName: "icax-pdo-test",
    declarations: [declaration],
  }, bridge);

  const byteLength = await pdo.withRead("PreviewMesh", "MainViewport", (view) => view.byteLength);
  assert.equal(byteLength, 16);
  assert.deepEqual(calls[0], {
    arenaName: "icax-pdo-test",
    id: declaration.id,
    version: declaration.version,
    payloadSize: declaration.payloadSize,
  });
}

async function testProductModuleLoader() {
  const shellBaseUrl = new URL("../../iCAX-UI/SDK/AppShell/app/bootstrap.mjs", import.meta.url).href;
  const entry = "apps/laser-3d-cam/webpage/entry.mjs";
  const moduleUrl = resolveFrontendEntry(entry, shellBaseUrl);
  assert.ok(moduleUrl.endsWith("/src/apps/laser-3d-cam/webpage/entry.mjs"));
  assert.throws(() => resolveFrontendEntry("https://example.com/product.mjs", shellBaseUrl), /External product frontend entries/);
  assert.equal(
    resolveFrontendEntry("https://example.com/product.mjs", shellBaseUrl, { allowExternalEntries: true }),
    "https://example.com/product.mjs",
  );

  const cache = new Map();
  const module = await loadProductModule({
    productId: "icax.test-product",
    frontendEntry: "./fixtures/mock-product/entry.mjs",
  }, cache, { baseUrl: import.meta.url });
  assert.equal(typeof module.mountProduct, "function");
  assert.equal(cache.get("icax.test-product"), module);

  const calls = [];
  mountProductModule({
    mountProduct: (context) => calls.push(`product:${context.mode}`),
    mountProject: (context) => calls.push(`project:${context.mode}`),
  }, { mode: "product" });
  mountProductModule({
    mountProduct: (context) => calls.push(`product:${context.mode}`),
    mountProject: (context) => calls.push(`project:${context.mode}`),
  }, { mode: "project" });
  assert.deepEqual(calls, ["product:product", "project:project"]);
}

function testMachineJointTransformPanel() {
  const backendEditable = { editable: true, hasRange: false, step: 1, precision: 3 };
  const makeView = (joint) => ({
    selectedMachineInstanceId: "machine-1",
    selectedSceneObjectId: "joint-1",
    pending: false,
    scene: {
      machines: [{ entityId: "machine-1", isLoaded: true, linearJogStep: 10, angularJogStep: 1 }],
      machineElement: {
        entityId: "joint-1",
        kind: "link",
        name: "axis",
        transform: { position: [10, 20, 30], rotationRadians: [0.1, 0.2, 0.3], scale: [1, 1, 1] },
        transformEditPolicy: {
          reason: "joint",
          position: [backendEditable, backendEditable, backendEditable],
          rotationRadians: [backendEditable, backendEditable, backendEditable],
          scale: [backendEditable, backendEditable, backendEditable],
        },
        joint,
      },
    },
  });

  const linear = renderMachineRightPane({}, makeView({
    type: "prismatic",
    axis: [-1, 0, 0],
    position: 25,
    lower: -100,
    upper: 200,
  }));
  const input = (html, attribute) => html.match(new RegExp(`<input[^>]*${attribute}[^>]*>`))?.[0] ?? "";
  assert.match(input(linear, "data-cam-transform-position-x"), /data-cam-joint-position/);
  assert.doesNotMatch(input(linear, "data-cam-transform-position-x"), /\bdisabled\b/);
  for (const attribute of [
    "data-cam-transform-position-y",
    "data-cam-transform-position-z",
    "data-cam-transform-rotation-yaw",
    "data-cam-transform-rotation-pitch",
    "data-cam-transform-rotation-roll",
    "data-cam-transform-scale-x",
    "data-cam-transform-scale-y",
    "data-cam-transform-scale-z",
  ]) {
    assert.match(input(linear, attribute), /\bdisabled\b/);
  }
  assert.doesNotMatch(linear, /data-cam-joint-position-editor/);

  const rotary = renderMachineRightPane({}, makeView({
    type: "revolute",
    axis: [1, 0, 0],
    position: Math.PI / 4,
    lower: -Math.PI,
    upper: Math.PI,
  }));
  assert.match(input(rotary, "data-cam-transform-rotation-roll"), /data-cam-joint-position/);
  assert.doesNotMatch(input(rotary, "data-cam-transform-rotation-roll"), /\bdisabled\b/);
  for (const attribute of [
    "data-cam-transform-position-x",
    "data-cam-transform-position-y",
    "data-cam-transform-position-z",
    "data-cam-transform-rotation-yaw",
    "data-cam-transform-rotation-pitch",
  ]) {
    assert.match(input(rotary, attribute), /\bdisabled\b/);
  }
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

testSDOMethodCodes();
testTubeDesignerBuildsTemplateDefinedPartCategories();
testTubeDesignerSeparatesBasicAndAdvancedProductionWorkflows();
await testTubeDesignerEntersCuttingFromThePartList();
testTubeDesignerSketchJoinsTheMainWorkflow();
testTubeDesignerChoosesTheFirstAvailableCatalogTemplate();
testTubeDesignerBuildsCollapsibleTemplateHierarchyFromDisplayNames();
testTubeDesignerOperationOverlayCoversTheWholeWindow();
testTubeDesignerProfileMiniatureConstrainsSvgIntrinsicSize();
testTubeDesignerProfilePreviewProgressCoversTheWholeWindow();
testGenericProgressOverlayCoversTheWholeProductPage();
testTubeDesignerTemplateSwitchProgressDoesNotFlash();
testTubeDesignerExpandedGroupsKeepTheirFullContentHeight();
testTubeDesignerPreservesFocusedItemPositionWhenScrollLengthChanges();
testTubeDesignerManifestDoesNotPublishCompatibilityTemplates();
testSecurityWindowHorizontalProfilesAreSmallerThanFrames();
testTubeDesignerBuildsParameterHierarchyFromDisplayNamePaths();
testTubeDesignerBuildsDimensionsOnlyFromFinalGeometryMeasurement();
testTubeDesignerUserPresetKeepsOnlyReusableTemplateParameters();
testTubeDesignerRendersBuiltInAndPersonalPresetChoices();
testTubeDesignerRendersFrozenDxfProfilesWithoutEditableDimensions();
testTubeDesignerHasIndependentEditableProfileLibrary();
testTubeDesignerProfileSvgPreservesCurvesAndVisibleBounds();
await testTubeDesignerHydratesProfilePreviewIntoTheThreeDimensionalViewport();
await testTubeDesignerSystemProfilePreviewUsesScopedReference();
testViewportOwnsTransientInteraction();
testStandardViewDirections();
testTubeDesignerAppliesDefaultViewAfterNewRevisionIsFitted();
testPartThumbnailNormalizesTheLongestAxisHorizontally();
testVariantSerializer();
testViewResourceParsing();
testRenderResourceParsing();
testProjectAreaMembershipIsolation();
await testViewReadersUseSceneChannelAndResourceSnapshots();
await testRenderResourceLoaderUsesViewReferenceVersion();
await testTubePreviewLoadsStandardRenderGeometry();
await testBridgeValidation();
testChannelIdValidation();
await testSDOPromiseFlow();
await testProductProxyCanInvokeProductSpecificMethods();
await testSceneChannelRegistrationFromProjectState();
await testDirectResourceAccess();
await testSDOEventFlow();
await testSDOReportFlow();
await testFrontendSDOExposure();
await testSDODispose();
await testPDOBridgeInjection();
await testProductModuleLoader();
testMachineJointTransformPanel();

console.log("SDK tests passed");


