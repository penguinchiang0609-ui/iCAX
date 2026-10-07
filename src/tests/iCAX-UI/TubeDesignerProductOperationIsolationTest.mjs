import assert from "node:assert/strict";
import {
  handleDesignerAreaAction,
  handleDesignerRibbonCommand,
} from "../../apps/tube-designer/webpage/designerActions.mjs";
import { renderDesignerRightPane } from "../../apps/tube-designer/webpage/designerViews.mjs";

const descriptor = {
  id: "security-window",
  name: "防盗窗",
  available: true,
  parameters: [{ key: "width", displayName: "正面宽度", type: "number", default: 1200 }],
};
const product = {
  entityId: "product-1", templateId: descriptor.id, name: "当前防盗窗",
  parameters: { width: 1200 }, quantity: 1,
};
const part = {
  entityId: "part-1", index: 1, name: "外框左立柱", partNumber: "TD-001",
  quantity: 1, length: 1800,
};

async function keepsParameterEditorAfterPartListGeneration() {
  const calls = [];
  const logs = [];
  const member = { entityId: "member-1" };
  const view = {
    pending: false,
    activeAreaId: "view",
    scene: { tubeDesigner: { product, activeProductId: product.entityId, templates: [descriptor] } },
  };
  const context = {
    sceneProxy: {
      async invoke(method) {
        calls.push(method);
        if (method === "TubeDesigner.DisassembleSelected") {
          return { tubeDesigner: {
            product, activeProductId: product.entityId,
            // Native disassembly replies contain only the compact catalogue.
            templates: [{ id: descriptor.id, name: descriptor.name, available: true }],
            members: [member],
            manufacturingGroups: [{ productEntityId: product.entityId, parts: [part] }],
          } };
        }
        if (method === "TubeDesigner.GetTemplateDescriptor") return { template: descriptor };
        throw new Error(`Unexpected call: ${method}`);
      },
    },
    actions: { async refreshActiveSceneState() {} },
  };
  await handleDesignerAreaAction(context, view, "tube-designer-disassemble-active-product", {}, {
    getActiveAreaViewRevision() { return "12"; },
    async refreshActiveAreaView(_context, currentView, expectation) {
      assert.equal(currentView.scene.tubeDesigner.members[0].entityId, member.entityId);
      assert.deepEqual(expectation, { expectedEntityIds: [member.entityId] });
      calls.push("View.Refresh");
      return {
        revision: "12",
        viewportReceipt: { applied: true, revision: "12", entityIds: [member.entityId] },
      };
    },
    renderProject() {}, showNotice() {},
    appendProjectLog(_context, level, message) { logs.push({ level, message }); },
  });

  assert.deepEqual(calls, ["TubeDesigner.DisassembleSelected", "TubeDesigner.GetTemplateDescriptor", "View.Refresh"]);
  assert.deepEqual(view.scene.tubeDesigner.templates[0].parameters, descriptor.parameters);
  assert.doesNotMatch(renderDesignerRightPane({}, view), /正在载入产品参数/);
  const timing = view.tubeDesignerDisassemblyTiming;
  assert.equal(timing.completed, true);
  assert.deepEqual(timing.phases.map(({ label }) => label), [
    "界面准备", "拆单请求（含后端处理、传输和解析）", "整理清单与参数",
    "确认三维视图", "同步项目状态", "界面刷新（含绘制等待）",
  ]);
  assert.ok(timing.phases.every(({ elapsedMs }) => Number.isFinite(elapsedMs) && elapsedMs >= 0));
  assert.ok(Math.abs(timing.phases.reduce((sum, phase) => sum + phase.elapsedMs, 0) - timing.totalMs) < 0.01);
  assert.match(logs.at(-1).message, /拆单总耗时（从操作开始到界面刷新）/);
}

async function rejectsAnUnappliedViewAndRecordsFailure() {
  const logs = [];
  const view = {
    pending: false, activeAreaId: "view",
    scene: { tubeDesigner: { product, templates: [descriptor] } },
  };
  await assert.rejects(handleDesignerAreaAction({
    sceneProxy: { async invoke(method) {
      if (method === "TubeDesigner.GetTemplateDescriptor") return { template: descriptor };
      return { tubeDesigner: {
        product, templates: [descriptor], members: [{ entityId: "member-1" }],
        manufacturingGroups: [{ productEntityId: product.entityId, parts: [part] }],
      } };
    } },
  }, view, "tube-designer-disassemble-active-product", {}, {
    renderProject() {}, showNotice() {},
    async refreshActiveAreaView() {
      return { revision: "12", viewportReceipt: { applied: true, revision: "12", entityIds: [] } };
    },
    appendProjectLog(_context, level, message) { logs.push({ level, message }); },
  }), /缺少 1 个预览构件/);
  assert.equal(view.pending, false);
  assert.equal(view.tubeDesignerDisassemblyTiming.completed, false);
  assert.equal(logs.at(-1).level, "error");
  assert.ok(!logs.some(({ message }) => message.startsWith("拆单总耗时")));
}

async function exportsWithoutMutatingOrRerenderingInstanceDock() {
  const designer = {
    product, activeProductId: product.entityId, templates: [descriptor],
    manufacturingGroups: [{ productEntityId: product.entityId, parts: [part] }],
  };
  const view = {
    pending: false,
    activeAreaId: "view",
    scene: { tubeDesigner: designer },
    tubeDesignerBreakdownMode: "keep-this-state",
    tubeDesignerBreakdownProductIds: ["keep-product"],
    tubeDesignerSelectedPartIds: ["keep-part"],
  };
  const calls = [];
  let renders = 0;
  await handleDesignerRibbonCommand({
    sceneProxy: {
      async invoke(method, payload) {
        calls.push({ method, payload });
        assert.equal(method, "TubeDesigner.ExportSelected");
        return { exportedCount: 1, exportedFiles: ["TD-001.step"], exportedGroups: [{ productEntityId: product.entityId }], partListFile: "D:/exports/零件清单.xlsx" };
      },
    },
    appProxy: { bridge: { openDirectoryDialog: async () => "D:/exports" } },
  }, view, "designer.export-active-product-parts", {
    renderProject() { renders += 1; },
    appendProjectLog() {},
    showNotice() { throw new Error("active-product export must not rerender for a notice"); },
  });

  assert.equal(renders, 0);
  assert.equal(view.scene.tubeDesigner, designer);
  assert.equal(view.tubeDesignerBreakdownMode, "keep-this-state");
  assert.deepEqual(view.tubeDesignerBreakdownProductIds, ["keep-product"]);
  assert.deepEqual(view.tubeDesignerSelectedPartIds, ["keep-part"]);
  assert.equal(view.tubeDesignerExportOperation, null);
  assert.deepEqual(calls, [{ method: "TubeDesigner.ExportSelected", payload: {
    targetDirectory: "D:/exports", partEntityIds: [part.entityId],
  } }]);
}

async function deletesOnlyTheActiveProductAndSelectsTheRemainingInstance() {
  const nextProduct = {
    ...product,
    entityId: "product-2",
    name: "保留的防盗窗",
    parameters: { width: 1500 },
  };
  const nextDesigner = {
    product: nextProduct,
    activeProductId: nextProduct.entityId,
    instances: [{ entityId: nextProduct.entityId, name: nextProduct.name }],
    templates: [descriptor],
    members: [],
    manufacturingGroups: [],
  };
  const calls = [];
  let refreshedHistory = 0;
  const view = {
    pending: false,
    activeAreaId: "view",
    scene: { tubeDesigner: {
      product, activeProductId: product.entityId, templates: [descriptor],
      instances: [{ entityId: product.entityId }, { entityId: nextProduct.entityId }],
    } },
    tubeDesignerRightDraftsByProductId: {
      [product.entityId]: { width: 1300 },
      [nextProduct.entityId]: { width: 1500 },
    },
    tubeDesignerRightParameterHistoryByProductId: {
      [product.entityId]: { undo: [{}], redo: [] },
    },
    tubeDesignerInstanceRuntimeVersions: {
      [product.entityId]: { modelVersion: 1 },
      [nextProduct.entityId]: { modelVersion: 1 },
    },
    tubeDesignerProductsRequiringDisassembly: [product.entityId, nextProduct.entityId],
  };
  const context = {
    sceneProxy: {
      async invoke(method, payload) {
        calls.push({ method, payload });
        return {
          deleted: true,
          deletedProductEntityId: product.entityId,
          tubeDesigner: nextDesigner,
        };
      },
    },
    actions: {
      async refreshActiveSceneState() {},
      refreshProjectHistoryControls() { refreshedHistory += 1; },
    },
  };
  await handleDesignerRibbonCommand(context, view, "designer.delete-active-product", {
    renderProject() {},
    async refreshActiveAreaView() { return { revision: "delete-revision" }; },
    showNotice() {},
  });
  assert.deepEqual(calls, [{
    method: "TubeDesigner.DeleteProduct",
    payload: { productEntityId: product.entityId },
  }]);
  assert.equal(view.scene.tubeDesigner.product.entityId, nextProduct.entityId);
  assert.equal(view.tubeDesignerRightDraftsByProductId?.[product.entityId], undefined);
  assert.equal(view.tubeDesignerRightParameterHistoryByProductId?.[product.entityId], undefined);
  assert.equal(view.tubeDesignerInstanceRuntimeVersions?.[product.entityId], undefined);
  assert.deepEqual(view.tubeDesignerProductsRequiringDisassembly, [nextProduct.entityId]);
  assert.equal(refreshedHistory, 1);
}

await keepsParameterEditorAfterPartListGeneration();
await rejectsAnUnappliedViewAndRecordsFailure();
await exportsWithoutMutatingOrRerenderingInstanceDock();
await deletesOnlyTheActiveProductAndSelectsTheRemainingInstance();
console.log("Product part generation keeps parameters hydrated; export preserves the instance dock.");
