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
    renderProject() {}, showNotice() {},
  });

  assert.deepEqual(calls, ["TubeDesigner.DisassembleSelected", "TubeDesigner.GetTemplateDescriptor"]);
  assert.deepEqual(view.scene.tubeDesigner.templates[0].parameters, descriptor.parameters);
  assert.doesNotMatch(renderDesignerRightPane({}, view), /正在载入产品参数/);
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
await exportsWithoutMutatingOrRerenderingInstanceDock();
await deletesOnlyTheActiveProductAndSelectsTheRemainingInstance();
console.log("Product part generation keeps parameters hydrated; export preserves the instance dock.");
