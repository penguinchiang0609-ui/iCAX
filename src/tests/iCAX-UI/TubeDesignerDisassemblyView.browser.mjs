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
    const { ViewClient } = await import("/src/iCAX-UI/SDK/View/viewClient.mjs");
    const { createWorkbench } = await import("/src/apps/_shared/workbench/createWorkbench.mjs");
    const { getProjectArea } = await import("/src/apps/_shared/workbench/state/projectViewStore.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const paneHtml = (complete) => `<aside class="cam-context-pane tube-designer-instance-list" style="height:90px;overflow:auto">
      <div style="height:500px"></div></aside>
      <aside class="cam-info-pane" style="height:150px;overflow:auto">
      <section data-tube-designer-parameter-form><div data-tube-designer-parameter-scroll
        style="height:90px;overflow:auto"><div style="height:260px"></div>
        <input data-tube-designer-parameter="width" value="1200"><div style="height:300px"></div>
        ${complete ? '<input data-field="conditional-result" value="ready">' : ''}
      </div></section><div style="height:500px"></div></aside>`;
    document.body.innerHTML = `<main id="panes">${paneHtml(false)}</main><canvas id="product-scene"></canvas>`;
    const left = () => document.querySelector(".tube-designer-instance-list");
    const right = () => document.querySelector("[data-tube-designer-parameter-scroll]");
    const outerRight = () => document.querySelector(".cam-info-pane");
    const input = () => right().querySelector("input");
    const canvas = document.querySelector("canvas");
    const product = {
      entityId: "product-1", templateId: "security-window", name: "防盗窗",
      parameters: { width: 1200 }, quantity: 1,
    };
    const template = {
      id: product.templateId, name: "防盗窗", descriptorLoaded: true,
      parameters: [{ key: "width", type: "number", default: 1200 }],
    };
    // Native ICVW fixture, same as SDKTest: repository revision 3 stays unchanged.
    const payload = Uint8Array.from(atob("GAAAAElDVlcAAA4AGAAAAAQAEAAIAAwADgAAAPABAACIAQAADAAAAAMAAAAAAAAAAQAAABAAAAAAAAoAEAAEAAgADAAKAAAANAEAAAgBAAAEAAAAAgAAAHAAAAAQAAAADAAMAAQAAAAAAAgADAAAADQAAAAEAAAAJgAAAHsiX192YXJpYW50X3R5cGUiOiJPYmplY3QiLCJ2YWx1ZSI6e319AAATAAAAQ1RyYW5zZm9ybUNvbXBvbmVudAAMABAACAAGAAcADAAMAAAAAAABAWQAAAAEAAAAVgAAAHsiX192YXJpYW50X3R5cGUiOiJPYmplY3QiLCJ2YWx1ZSI6eyJ2aXNpYmxlIjp7Il9fdmFyaWFudF90eXBlIjoiYm9vbCIsInZhbHVlIjp0cnVlfX19AAAYAAAAQ1JlbmRlckluc3RhbmNlQ29tcG9uZW50AAAAAAIAAAAYAAAABAAAAAoAAAB3b3JrcGllY2VzAAAFAAAAc2NlbmUAAAAkAAAAMDAxMTIyMzMtNDQ1NS02Njc3LTg4OTktYWFiYmNjZGRlZWZmAAAAAAIAAAA8AAAABAAAANT///8YAAAABAAAAAkAAAB3b3JrcGllY2UAAAAKAAAAd29ya3BpZWNlcwAACAAMAAQACAAIAAAAFAAAAAQAAAAFAAAAc2NlbmUAAAAFAAAAc2NlbmUAAAAkAAAAMTExMTExMTEtMjIyMi00MzMzLTg0NDQtNTU1NTU1NTU1NTU1AAAAAA=="), c => c.charCodeAt(0));
    const member = { entityId: "00112233-4455-6677-8899-aabbccddeeff" };
    const view = {
      pending: false, activeAreaId: "view",
      scene: { tubeDesigner: {
        activeProductId: product.entityId, product, templates: [template], members: [member],
      } },
    };
    let completeDisassembly;
    let requested = false;
    let refreshed = false;
    const response = new Promise((resolveResponse) => { completeDisassembly = resolveResponse; });
    const context = {
      mount: document.body,
      sceneProxy: { async invoke(method) {
        if (method === "View.GetOrCreate") return { viewId: "view-1", resource: { url: "icax-resource://view", version: "3" } };
        if (method === "View.Release") return { released: true };
        if (method !== "TubeDesigner.DisassembleSelected") throw new Error(method);
        requested = true;
        return response;
      }, resources: {
        head: async () => new Response(null, { headers: { "ICAX-Resource-Version": "3" } }),
        get: async () => new Response(payload.slice()),
      } },
      actions: { async refreshActiveSceneState() {} },
    };
    const definition = { sources: [{ sourceId: "scene", where: "WHERE HAS CRenderInstanceComponent" }] };
    const client = new ViewClient(context.sceneProxy);
    const reader = await client.start(definition);
    await reader.poll();
    const area = getProjectArea(view, "view");
    area.viewReader = reader;
    area.viewDefinitionKey = JSON.stringify(definition);
    view.viewport = {
      setRenderSceneId() {}, setHighlightedObjects() {},
      async applyViewSnapshot(snapshot) {
        return { applied: true, revision: snapshot.revision, entityIds: snapshot.entityIds };
      },
    };
    let workbenchOps;
    await createWorkbench({ areaViewDefinitions: { view: definition } }).handleRibbonCommand({
      project: { projectId: "test-disassembly" },
      handleAreaRibbonCommand(_context, _view, _command, ops) { workbenchOps = ops; return true; },
    }, "test-capture-ops");
    // Apply the pre-existing product view before disassembly; no newer snapshot is published.
    await workbenchOps.refreshActiveAreaView(context, view);
    const renderProject = () => {
      const restore = capturePaneInteraction(document.body);
      document.querySelector("#panes").innerHTML = paneHtml(Boolean(view.scene.tubeDesigner.parts));
      restore();
    };
    const ops = {
      renderProject, showNotice: renderProject,
      getActiveAreaViewRevision: workbenchOps.getActiveAreaViewRevision,
      async refreshActiveAreaView(_context, currentView, expectation) {
        const content = await workbenchOps.refreshActiveAreaView(_context, currentView, expectation);
        refreshed = true;
        return content;
      },
    };
    left().scrollTop = 30;
    right().scrollTop = 40;
    const operation = handleDesignerAreaAction(context, view,
      "tube-designer-disassemble-active-product", null, ops);
    for (let i = 0; i < 100 && !requested; i += 1)
      await new Promise((done) => setTimeout(done, 5));
    left().scrollTop = 160;
    right().scrollTop = 170;
    outerRight().scrollTop = 85;
    input().focus({ preventScroll: true });
    input().value = "1250";
    input().setSelectionRange(1, 3);
    const expected = { left: left().scrollTop, right: right().scrollTop, outerRight: outerRight().scrollTop };
    completeDisassembly({ tubeDesigner: {
      activeProductId: product.entityId, product, templates: [template], members: [member],
      generationRun: { entityId: "run-1" }, parts: [{ entityId: "part-1" }],
      manufacturingGroups: [{ productEntityId: product.entityId,
        parts: [{ entityId: "part-1" }] }],
    } });
    let timeout;
    try {
      await Promise.race([operation, new Promise((_, reject) => {
        timeout = setTimeout(() => reject(new Error("Disassembly hangs after backend completion when View revision stays 3")), 2000);
      })]);
    } finally {
      clearTimeout(timeout);
      // Consume the rejected action as well when stopping a regression's hung reader.
      operation.catch(() => {});
      await client.dispose();
    }
    return {
      requested, refreshed,
      parts: view.scene.tubeDesigner.parts.length,
      canvasPreserved: document.querySelector("canvas") === canvas,
      revision: area.viewContent.revision,
      pending: view.pending,
      leftScroll: left().scrollTop, rightScroll: right().scrollTop, outerRightScroll: outerRight().scrollTop,
      expected, focusPreserved: document.activeElement === input(),
      selection: [input().selectionStart, input().selectionEnd],
      value: input().value,
      conditionalField: Boolean(document.querySelector('[data-field="conditional-result"]')),
    };
  });
  assert.equal(result.requested, true);
  assert.equal(result.refreshed, true);
  assert.equal(result.parts, 1);
  assert.equal(result.canvasPreserved, true);
  assert.equal(result.leftScroll, result.expected.left);
  assert.equal(result.rightScroll, result.expected.right);
  assert.equal(result.outerRightScroll, result.expected.outerRight);
  assert.equal(result.revision, "3");
  assert.equal(result.pending, false);
  assert.equal(result.value, "1250");
  assert.equal(result.conditionalField, true);
  assert.equal(result.focusPreserved, true);
  assert.deepEqual(result.selection, [1, 3]);
  console.log("PASS disassembly completes with unchanged View revision; real ViewClient/workbench wait; preserves canvas, latest pane/nested scroll, focus, text and selection across conditional result refresh");
} finally {
  await browser.close();
}
