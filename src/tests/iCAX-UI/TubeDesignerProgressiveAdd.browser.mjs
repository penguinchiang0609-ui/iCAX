import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
  headless: true,
  ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}),
});

try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 760 } });
  const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://tube-designer.test/**", async (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/")
      || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
      || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://tube-designer.test/");

  const result = await page.evaluate(async () => {
    const { handleDesignerAreaAction } = await import(
      "/src/apps/tube-designer/webpage/designerActions.mjs"
    );
    const template = {
      id: "single-face-security-window", version: "1", available: true,
      descriptorLoaded: true, parameters: [], groups: [],
    };
    const product = {
      entityId: "product-1", templateId: template.id, templateVersion: template.version,
      productCode: "W-1", name: "测试防盗窗", quantity: 1, createdAt: "",
      parameters: { faceType: "five" }, activeGenerationRunId: "run-1",
      modelOutdated: false, partsOutdated: false,
    };
    const receipt = {
      receiptOnly: true, activeProductId: product.entityId, product,
      generationRun: { entityId: "run-1", partCount: 88 },
      members: [{ entityId: "member-1", name: "首帧构件", index: 1 }],
    };
    const full = {
      ...receipt, receiptOnly: undefined, templates: [template],
      members: [{ entityId: "member-1", name: "完整构件", index: 1,
        profile: {}, properties: {}, length: 1200 }],
      instances: [{ ...product, active: true, memberCount: 1 }],
    };
    let resolveList;
    let listCalled = false;
    let firstFrame = false;
    let addedPayload;
    const listPending = new Promise((resolvePromise) => { resolveList = resolvePromise; });
    document.body.innerHTML = `<div id="mount"></div>`;
    const mount = document.querySelector("#mount");
    const canvas = document.createElement("canvas");
    mount.append(canvas);
    const renderSidebars = (view) => {
      let left = mount.querySelector(".tube-designer-instance-list");
      let right = mount.querySelector("[data-tube-designer-parameter-form]");
      if (!left) { left = document.createElement("aside"); left.className = "tube-designer-instance-list"; mount.append(left); }
      if (!right) { right = document.createElement("aside"); right.dataset.tubeDesignerParameterForm = ""; mount.append(right); }
      left.innerHTML = `<div style="height:400px"></div><button data-tube-designer-instance-id="product-1">产品</button><div style="height:400px"></div>`;
      right.innerHTML = `<div data-tube-designer-parameter-scroll style="height:120px;overflow:auto">
        <div style="height:220px"></div><input data-tube-designer-parameter="width" value="1200">
        <div style="height:320px"></div></div>`;
      let addForm = mount.querySelector("[data-tube-designer-add-form]");
      if (view.tubeDesignerAddDialogOpen && !addForm) {
        addForm = document.createElement("form");
        addForm.dataset.tubeDesignerAddForm = "";
        mount.append(addForm);
      } else if (!view.tubeDesignerAddDialogOpen) addForm?.remove();
    };
    const view = {
      pending: false, activeAreaId: "view", tubeDesignerAddDialogOpen: true,
      tubeDesignerAddTemplateId: template.id, tubeDesignerAddDraft: {},
      tubeDesignerAddInstanceQuantity: 1, scene: { tubeDesigner: { templates: [template], instances: [] } },
      viewport: {
        setCustomVisibleEntityIds() {},
        fitViewForRevision(revision) { return { fitted: true, revision, renderSequence: 1 }; },
        setViewDirection() { return true; },
        getAppliedViewState() { return { revision: "view-1", renderSequence: 2 }; },
      },
    };
    const context = {
      mount,
      actions: { refreshProjectHistoryControls() {}, async refreshActiveSceneState() {} },
      sceneProxy: { async invoke(method, payload) {
        if (method === "TubeDesigner.GeneratePreview") { addedPayload = payload; return { tubeDesigner: receipt }; }
        if (method === "TubeDesigner.List") { listCalled = true; return listPending; }
        throw new Error(`unexpected ${method}`);
      } },
    };
    const ops = {
      renderProject(_context, currentView) { renderSidebars(currentView); },
      async refreshActiveAreaView() {
        firstFrame = true;
        return { revision: "view-1", viewportReceipt: {
          applied: true, revision: "view-1", entityIds: ["member-1"], renderSequence: 1,
        } };
      },
      getActiveAreaViewRevision() { return "view-0"; },
      showNotice() {},
    };
    renderSidebars(view);
    await handleDesignerAreaAction(context, view, "tube-designer-confirm-add", null, ops);
    const first = {
      firstFrame, listCalled, pending: view.pending,
      receiptOnly: view.scene.tubeDesigner.receiptOnly,
      dialogOpen: view.tubeDesignerAddDialogOpen,
      payloadReceiptOnly: addedPayload.receiptOnly,
    };
    const left = mount.querySelector(".tube-designer-instance-list");
    const right = mount.querySelector("[data-tube-designer-parameter-scroll]");
    left.scrollTop = 180;
    right.scrollTop = 190;
    const input = right.querySelector("input");
    input.focus(); input.setSelectionRange(1, 3);
    const expectedLeft = left.scrollTop;
    const expectedRight = right.scrollTop;
    resolveList({ tubeDesigner: full });
    for (let attempt = 0; attempt < 100 && view.scene.tubeDesigner.receiptOnly; ++attempt)
      await new Promise((done) => setTimeout(done, 5));
    await new Promise((done) => setTimeout(done, 0));
    const refreshedInput = mount.querySelector("[data-tube-designer-parameter='width']");
    return {
      first,
      detailedMember: view.scene.tubeDesigner.members[0].name,
      canvasPreserved: mount.querySelector("canvas") === canvas,
      leftScroll: mount.querySelector(".tube-designer-instance-list").scrollTop,
      rightScroll: mount.querySelector("[data-tube-designer-parameter-scroll]").scrollTop,
      expectedLeft, expectedRight,
      focusRestored: document.activeElement === refreshedInput,
      selection: [refreshedInput.selectionStart, refreshedInput.selectionEnd],
    };
  });
  assert.deepEqual(result.first, {
    firstFrame: true, listCalled: true, pending: false,
    receiptOnly: true, dialogOpen: false, payloadReceiptOnly: true,
  });
  assert.equal(result.detailedMember, "完整构件");
  assert.equal(result.canvasPreserved, true);
  assert.equal(result.leftScroll, result.expectedLeft);
  assert.equal(result.rightScroll, result.expectedRight);
  assert.equal(result.focusRestored, true);
  assert.deepEqual(result.selection, [1, 3]);
  console.log("PASS progressive add: first frame precedes detail response, then focus and scroll survive hydration");
} finally {
  await browser.close();
}
