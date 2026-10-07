// Edge exercises the production product UI/actions. The host below models the
// native contract: EC edits retain the committed run; disassembly reads that run.
import assert from "node:assert/strict";
import { importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');

const raw = JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/template.json'));
const removedMaterialFields = ["outerFramePostMaterial", "foldedPostProfileType", "foldedPostWidth",
  "foldedPostDepth", "foldedPostCornerRadius", "foldedPostWallThickness", "foldedPostInsertDepth", "foldedPostFitGap"];
for (const key of removedMaterialFields) assert.equal(raw.parameters.some(field => field.key === key), false,
  `The uniform outer-frame material contract must not declare ${key}`);
assert.equal(raw.groups.some(group => group.key === "folded_post_profile"), false);
assert.deepEqual(raw.extensions.productDiagram.profileRoles.frame.parameters,
  ["frameProfileType", "frameWidth", "frameDepth", "frameCornerRadius", "frameWallThickness"]);
for (const key of ["foldedPostJoint", "foldedPostTabWidth", "foldedPostTabLength", "foldedPostSideClearance"]) {
  assert.ok(raw.extensions.parameterDependencies.manufacturingOnly.includes(key), `${key} must not expire the display model`);
}
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1350, height: 850 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://product-current-design.test/**", serveBrowserAsset);
  await page.goto("http://product-current-design.test/");
  await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0}.cam-workbench{display:flex;gap:10px;flex-wrap:wrap}.cam-context-pane,.cam-info-pane{height:250px;overflow:auto;width:480px}.nested-left{height:150px;overflow:auto}.nested-left>div{height:1500px}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1500px}.tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:1500px}.cam-viewport{width:250px}.tube-designer-operation-wait{pointer-events:none}.tube-designer-product-parts-dock{width:100%;height:220px}` });
  await page.evaluate(async raw => {
    const views = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const actions = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const { catalogText } = await import("/src/apps/tube-designer/webpage/productCatalog.mjs");
    const { productDisplayParameters } = await import("/src/apps/tube-designer/webpage/productParameterDependencies.mjs");
    const { patchLibraryDom, rememberLibraryDom } = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const template = { ...raw, available: true, descriptorLoaded: true, name: catalogText(raw.displayName),
      groups: raw.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: raw.parameters.map(field => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
        displayName: catalogText(field.displayName), groupKey: field.group,
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const defaults = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
    const canonical = value => JSON.stringify(Object.fromEntries(Object.entries(value).sort(([a], [b]) => a.localeCompare(b))));
    const display = value => canonical(productDisplayParameters(template, value));
    const db = new Map(["active", "outdated", "manufacturing", "current", "unselected"].map((id, index) => {
      const parameters = { ...structuredClone(defaults), width: 1200 + 100 * index, productCode: `TD-${index + 1}` };
      const generated = structuredClone(parameters);
      if (id === "outdated" || id === "unselected") parameters.foldedPostJoint = "tabs";
      if (id === "manufacturing") parameters.frameJoinType = "butt_90";
      return [id, { entityId: id, name: `窗 ${index + 1}`, templateId: template.id, templateVersion: template.version,
        createdAt: `2026-10-04T00:00:0${index}`, quantity: index + 1, parameters, generated,
        runId: `old-${id}`, parts: [] }];
    }));
    let activeId = "active", sequence = 0, revision = 1, fullRenders = 0, renderCount = 0;
    const requests = [], held = [], completed = [];
    const state = item => ({ ...item, modelOutdated: display(item.parameters) !== display(item.generated),
      partsOutdated: canonical(item.parameters) !== canonical(item.manufactured ?? item.generated),
      activeGenerationRunId: item.runId, active: item.entityId === activeId });
    const snapshot = () => {
      const item = db.get(activeId);
      return { templates: [template], activeProductId: activeId, product: structuredClone(state(item)),
        instances: [...db.values()].map(entry => structuredClone(state(entry))),
        generationRun: { entityId: item.runId }, members: [{ entityId: `member-${activeId}`,
          previewGeometryResourceId: `mesh-${activeId}`, previewGeometryResourceVersion: sequence + 1 }],
        manufacturingGroups: [...db.values()].filter(entry => entry.parts.length).map(entry => ({ productEntityId: entry.entityId,
          name: entry.name, templateId: entry.templateId, parameters: structuredClone(entry.parameters),
          generationRunId: entry.runId, parts: structuredClone(entry.parts) })), parts: [], joints: [] };
    };
    document.body.innerHTML = "<main></main>";
    const mount = document.querySelector("main");
    const view = { pending: false, activeAreaId: "view", scene: { tubeDesigner: snapshot() },
      tubeDesignerParameterPanelProductId: activeId,
      tubeDesignerExpandedParameterGroups: ["section:materials", "section:process", ...template.groups.map(group => `group:${group.key}`)],
      tubeDesignerParameterDisclosureState: { initialized: true } };
    const harness = window.h = { db, requests, held, completed, view, pause: false, errors: [],
      reportCallbacks: [], renderCount: () => renderCount, revision: () => revision };
    const context = { mount, project: { projectId: "current-design-browser" }, actions: {}, sceneProxy: {
      async invoke(method, payload, options = {}) {
        requests.push({ method, payload: structuredClone(payload) });
        if (method === "TubeDesigner.DisassembleSelected") {
          if (typeof options.onReport !== "function" || options.timeoutMs !== 120000) throw new Error("disassembly does not use reports with the unchanged inactivity timeout");
          harness.reportCallbacks.push(options.onReport);
        }
        if (harness.pause) await new Promise(resolve => held.push({ method, resolve }));
        if (method === "TubeDesigner.UpdateProductParameters") {
          db.get(payload.productEntityId).parameters = structuredClone(payload.parameters);
        } else if (method === "TubeDesigner.GeneratePreview") {
          if (harness.failGeneration) throw new Error("requested generation failed");
          const entry = db.get(payload.productEntityId);
          const values = Object.fromEntries(template.parameters.map(field => [field.key, payload[field.key]]));
          for (const key of ["tubeDesignerToolBindings", "tubeDesignerProfileOverrides"]) if (Object.hasOwn(payload, key)) values[key] = structuredClone(payload[key]);
          entry.parameters = structuredClone(values); entry.generated = structuredClone(values); entry.parts = [];
          entry.runId = `new-${entry.entityId}-${++sequence}`; activeId = entry.entityId; revision++;
        } else if (method === "TubeDesigner.ActivateProduct") {
          activeId = payload.productEntityId; revision++;
        } else if (method === "TubeDesigner.DisassembleSelected") {
          if (harness.failDisassembly) throw new Error("native disassembly failed");
          for (const id of payload.productEntityIds) {
            const entry = db.get(id);
            const manufactured = payload.productParametersByEntityId?.[id] ?? entry.generated;
            entry.manufactured = structuredClone(manufactured);
            if (payload.productParametersByEntityId?.[id]) entry.parameters = structuredClone(manufactured);
            entry.parts = [{ entityId: `part-${id}`, sourceMemberId: `member-${id}`, index: 1,
              name: "外框上下横边", partNumber: `PART-${id}`, length: entry.parameters.width, quantity: entry.quantity,
              properties: { generatedFoldedPostJoint: manufactured.foldedPostJoint, generatedFrameJoinType: manufactured.frameJoinType } }];
          }
        } else if (method === "TubeDesigner.GetTemplateDescriptor") return { template };
        else throw new Error(method);
        completed.push(method);
        return { tubeDesigner: snapshot() };
      },
    } };
    const left = () => `<div class="nested-left"><div><input id="left-note" value="ongoing left text">${views.renderDesignerLeftPane(context, view)}</div></div>`;
    const right = () => `<input id="right-note" value="ongoing right text">${views.renderDesignerRightPane(context, view)}`;
    const suffix = () => views.renderDesignerProductPartsDock(context, view) + views.renderDesignerOperationOverlay(context, view);
    const initial = () => {
      mount.innerHTML = `<div class="cam-workbench"><aside class="cam-context-pane">${left()}</aside><div class="cam-viewport"><canvas></canvas><button class="cube">视角</button></div><aside class="cam-info-pane">${right()}</aside>${suffix()}</div>`;
      mount.querySelectorAll("details").forEach(node => { node.open = true; });
      rememberLibraryDom(view, mount, suffix(), context.sceneProxy);
    };
    const render = () => {
      renderCount++;
      const restore = capturePaneInteraction(mount);
      actions.captureDesignerScrollState(context, view);
      if (!patchLibraryDom(view, mount, { left: left(), right: right(), overlay: views.renderDesignerViewportOverlay(context, view),
        suffix: suffix(), sceneProxy: context.sceneProxy })) { fullRenders++; initial(); }
      actions.restoreDesignerScrollState(context, view, { deferred: false }); restore();
    };
    harness.refresh = () => { view.scene.tubeDesigner = snapshot(); render(); };
    const ops = { renderProject: render, showNotice() {}, appendProjectLog() {},
      getActiveAreaViewRevision: () => String(revision),
      async refreshActiveAreaView(_context, current, expected) {
        requests.push({ method: "View.Refresh", expected: structuredClone(expected) });
        return { revision: String(revision), viewportReceipt: { applied: true, revision: String(revision),
          entityIds: current.scene.tubeDesigner.members.map(member => member.entityId), renderSequence: revision } };
      } };
    context.actions.refreshActiveSceneState = async () => render();
    initial();
    mount.addEventListener("change", event => {
      const target = event.target.closest("[data-cam-change-action]"); if (!target) return;
      harness.edit = actions.handleDesignerAreaAction(context, view, target.dataset.camChangeAction, target, ops);
      harness.edit.catch(error => harness.errors.push(error.message));
    });
    mount.addEventListener("click", event => {
      const target = event.target.closest("[data-cam-action]"); if (!target) return;
      harness.operation = actions.handleDesignerAreaAction(context, view, target.dataset.camAction, target, ops);
      harness.operation.catch(error => harness.errors.push(error.message));
    });
    harness.openBatch = () => actions.handleDesignerRibbonCommand(context, view, "designer.disassemble", ops);
    harness.confirmBatch = () => actions.handleDesignerAreaAction(context, view, "tube-designer-batch-disassembly-confirm", null, ops);
    harness.startBatch = async () => {
      const before = requests.filter(item => item.method === "TubeDesigner.DisassembleSelected").length;
      await harness.openBatch();
      if (!view.tubeDesignerBatchDisassemblyDialog || requests.filter(item => item.method === "TubeDesigner.DisassembleSelected").length !== before)
        throw new Error("opening batch selector must not disassemble any product");
      await actions.handleDesignerAreaAction(context, view, "tube-designer-batch-disassembly-toggle-all", { checked: true }, ops);
      return harness.confirmBatch();
    };
    harness.report = (payload, index = harness.reportCallbacks.length - 1) => harness.reportCallbacks[index]({ kind: 1, payload });
    harness.context = context;
    harness.release = method => {
      const index = held.findIndex(item => item.method === method); if (index < 0) throw new Error("no pending " + method);
      held.splice(index, 1)[0].resolve();
    };
    harness.interact = (side, values) => {
      const input = mount.querySelector(`#${side}-note`); input.focus({ preventScroll: true });
      input.value = `latest ${side} text`; input.setSelectionRange(2, 8, "backward");
      [mount.querySelector(".cam-context-pane"), mount.querySelector(".cam-info-pane"), mount.querySelector(".nested-left"),
        mount.querySelector("[data-tube-designer-parameter-scroll]")].forEach((node, index) => { node.scrollTop = values[index]; });
    };
    harness.checkInteraction = (side, values) => {
      const input = mount.querySelector(`#${side}-note`);
      if (document.activeElement !== input || input.value !== `latest ${side} text` || input.selectionStart !== 2 || input.selectionEnd !== 8 || input.selectionDirection !== "backward") throw new Error("disassembly response lost current input/selection");
      const positions = [mount.querySelector(".cam-context-pane"), mount.querySelector(".cam-info-pane"), mount.querySelector(".nested-left"),
        mount.querySelector("[data-tube-designer-parameter-scroll]")].map(node => node.scrollTop);
      if (JSON.stringify(positions) !== JSON.stringify(values)) throw new Error("disassembly response lost latest four scrolls " + positions);
    };
    const nodes = [mount.querySelector("#left-note"), mount.querySelector("#right-note"), mount.querySelector("canvas"),
      mount.querySelector('[data-tube-designer-parameter="frameWidth"]'), mount.querySelector('[data-product-control-key="outerFrameConnection"]')];
    if (nodes.some(node => !node)) throw new Error("production interaction fixture is missing an input/canvas node");
    let nodeEvents = 0; nodes.forEach(node => node?.addEventListener("audit-event", () => nodeEvents++));
    harness.checkNodes = () => {
      nodeEvents = 0;
      const next = [mount.querySelector("#left-note"), mount.querySelector("#right-note"), mount.querySelector("canvas"),
        mount.querySelector('[data-tube-designer-parameter="frameWidth"]'), mount.querySelector('[data-product-control-key="outerFrameConnection"]')];
      if (nodes.some((node, index) => node !== next[index])) throw new Error("disassembly replaced editor/canvas nodes");
      next.forEach(node => node?.dispatchEvent(new Event("audit-event")));
      if (nodeEvents !== nodes.filter(Boolean).length) throw new Error("disassembly lost node event listeners");
      if (fullRenders) throw new Error("disassembly remounted workbench " + fullRenders + " times");
    };
    harness.pause = true;
  }, raw);

  const held = async method => page.waitForFunction(method => window.h.held.some(item => item.method === method), method);
  const release = async method => page.evaluate(method => window.h.release(method), method);
  await page.locator('[data-product-control-key="outerFrameConnection"]').selectOption("tabs");
  await held("TubeDesigner.UpdateProductParameters");
  // Click while the EC update is still pending: generation must await it.
  await page.evaluate(() => document.querySelector('[data-cam-action="tube-designer-disassemble-active-product"]').click());
  await page.evaluate(() => window.h.interact("left", [110, 150, 250, 190]));
  await page.waitForTimeout(100);
  assert.equal(await page.evaluate(() => window.h.requests.some(item => item.method === "TubeDesigner.DisassembleSelected")), false,
    "disassembly started before the pending EC edit finished");
  await release("TubeDesigner.UpdateProductParameters");
  // Assembly-process edits change the manufacturing recipe without replacing
  // the committed display run or asking GeneratePreview to rebuild members.
  await page.waitForFunction(() => window.h.held.some(item => ["TubeDesigner.GeneratePreview", "TubeDesigner.DisassembleSelected"].includes(item.method)));
  const first = await page.evaluate(() => window.h.held[0].method);
  assert.equal(first, "TubeDesigner.DisassembleSelected", "assembly-process edit regenerated the display model");
  await page.evaluate(() => window.h.checkInteraction("left", [110, 150, 250, 190]));
  await page.evaluate(() => {
    const request = window.h.requests.find(item => item.method === "TubeDesigner.DisassembleSelected");
    if (request.payload.productParametersByEntityId?.active?.foldedPostJoint !== "tabs") throw new Error("tabs -> generate parts used the old committed welding recipe");
    if (window.h.view.scene.tubeDesigner.product.modelOutdated || window.h.view.tubeDesignerRightDraftDirty) throw new Error("assembly-process edit expired the display model");
  });
  await page.evaluate(() => window.h.interact("right", [150, 190, 290, 230]));
  await page.evaluate(() => {
    const h = window.h;
    const overlay = document.querySelector("[data-tube-designer-operation-wait]");
    const message = overlay?.querySelector("[data-tube-designer-operation-message]");
    const phase = overlay?.querySelector("[data-tube-designer-operation-phase]");
    if (!message || !phase) throw new Error("native progress has no visible operation fields");
    const renders = h.renderCount();
    const report = { kind: "disassembly", phase: "geometry", completed: 1, total: 41,
      message: "正在生成外框上下横边", elapsedMs: 2100 };
    h.report(report);
    if (message.textContent !== "正在生成外框上下横边（1 / 41）" || phase.textContent !== "生成零件形状") throw new Error("real native phase/count did not reach the progress DOM");
    if (h.renderCount() !== renders || overlay !== document.querySelector("[data-tube-designer-operation-wait]")) throw new Error("native progress remounted the product UI");
    h.checkInteraction("right", [150, 190, 290, 230]); h.checkNodes();
    // Reports from an operation no longer owned by this page cannot mutate it.
    const savedOperation = h.view.tubeDesignerOperation;
    h.view.tubeDesignerOperation = { ...savedOperation, id: savedOperation.id + 1 };
    h.report({ ...report, completed: 40 });
    h.view.tubeDesignerOperation = savedOperation;
    const savedProject = h.context.project;
    h.context.project = { projectId: "another-project" };
    h.report({ ...report, completed: 39 });
    h.context.project = savedProject;
    h.view.activeAreaId = "nesting"; h.report({ ...report, completed: 38 }); h.view.activeAreaId = "view";
    const savedMount = h.context.mount;
    h.context.mount = document.createElement("main"); h.report({ ...report, completed: 37 }); h.context.mount = savedMount;
    if (message.textContent !== "正在生成外框上下横边（1 / 41）" || h.renderCount() !== renders) throw new Error("a stale report changed another operation/project/page");
    h.report({ ...report, phase: "mesh", completed: 3, total: 41, message: "正在生成零件预览" });
    if (message.textContent !== "正在生成零件预览（3 / 41）" || phase.textContent !== "生成预览") throw new Error("next native phase retained the previous count");
    h.report({ ...report, phase: "validation", completed: 2, total: 36, message: "正在检查零件连接" });
    if (message.textContent !== "正在检查零件连接（2 / 36）" || phase.textContent !== "检查零件连接") throw new Error("native connection checks did not reach the progress DOM");
    h.report({ ...report, phase: "manufacturing", completed: 0, total: 0, message: "正在分析装配做法" });
    if (message.textContent !== "正在分析装配做法") throw new Error("an unknown total exposed a misleading zero count");
    h.checkInteraction("right", [150, 190, 290, 230]); h.checkNodes();
  });
  await release("TubeDesigner.DisassembleSelected");
  await page.evaluate(async () => { await window.h.edit; await window.h.operation; });
  await page.evaluate(() => {
    const h = window.h, entry = h.db.get("active");
    if (entry.runId !== "old-active" || entry.parts[0].properties.generatedFoldedPostJoint !== "tabs") throw new Error("parts lost current tabs or replaced the display run");
    const request = h.requests.find(item => item.method === "TubeDesigner.DisassembleSelected").payload.productParametersByEntityId.active;
    if (request.frameManufacturingMode !== "segment_weld" || Object.hasOwn(request, "outerFrameConnection")) throw new Error("native manufacturing input lost its declared host parameters");
    const declared = h.view.scene.tubeDesigner.templates[0].parameters.map(field => field.key).sort();
    if (JSON.stringify(Object.keys(request).sort()) !== JSON.stringify(declared)
        || request.frameWidth !== 38 || request.frameDepth !== 38 || request.frameWallThickness !== 1.2)
      throw new Error("parts generation changed the single outer-frame tube specification or public parameter contract");
    if (h.requests.some(item => ["TubeDesigner.GeneratePreview", "TubeDesigner.ActivateProduct"].includes(item.method))) throw new Error("manufacturing-only update rebuilt or changed the active display product");
    if (h.db.get("outdated").parts.length || h.db.get("unselected").parts.length) throw new Error("active-product disassembly manufactured another instance");
    h.checkInteraction("right", [150, 190, 290, 230]); h.checkNodes();
    if (h.view.pending || h.view.tubeDesignerOperation) throw new Error("completed disassembly retained its operation lock");
    const renders = h.renderCount(), finished = JSON.stringify(h.view.scene.tubeDesigner);
    h.report({ kind: "disassembly", phase: "geometry", completed: 39, total: 41, message: "late old progress" }, 0);
    if (h.view.tubeDesignerOperation || h.renderCount() !== renders || JSON.stringify(h.view.scene.tubeDesigner) !== finished
        || document.querySelector("[data-tube-designer-operation-wait]")) throw new Error("late native report changed the completed page");
    h.interact("left", [150, 190, 290, 230]);
    h.pause = false; h.operation = h.startBatch();
  });
  await page.evaluate(async () => { await window.h.operation; });
  const result = await page.evaluate(() => {
    const h = window.h;
    const disassembly = h.requests.filter(item => item.method === "TubeDesigner.DisassembleSelected").at(-1).payload;
    const other = disassembly.productParametersByEntityId?.outdated;
    if (other?.foldedPostJoint !== "tabs" || other.width !== h.db.get("outdated").parameters.width || other.productCode !== "TD-2") throw new Error("batch omitted another product's current tabs or borrowed active product parameters");
    if (disassembly.productParametersByEntityId?.manufacturing?.frameJoinType !== "butt_90") throw new Error("batch omitted another instance's manufacturing-only update");
    if (h.requests.some(item => ["TubeDesigner.GeneratePreview", "TubeDesigner.ActivateProduct"].includes(item.method))) throw new Error("batch rebuilt the display model");
    if (h.view.scene.tubeDesigner.product.entityId !== "active" || [...h.db.values()].some(entry => entry.runId !== `old-${entry.entityId}`)) throw new Error("batch changed an active product or display generation run");
    h.checkInteraction("left", [150, 190, 290, 230]);
    if (h.errors.length) throw new Error(h.errors.join("; "));
    return { ordered: h.requests.map(item => item.method) };
  });
  // A real dimension change must not silently manufacture an old display run.
  await page.evaluate(() => {
    const h = window.h;
    h.view.tubeDesignerBreakdownOpen = false;
    h.db.get("active").parameters.width += 33;
    h.view.tubeDesignerRightDraft = null;
    h.view.tubeDesignerRightDraftDirty = true;
    h.refresh();
    h.beforeRejectedRequestCount = h.requests.length;
    document.querySelector('[data-cam-action="tube-designer-disassemble-active-product"]').click();
  });
  await page.evaluate(async () => {
    const h = window.h;
    let error;
    try { await h.operation; } catch (failure) { error = failure; }
    if (!error || !/先更新产品模型/.test(error.message)) throw new Error("stale dimension change did not stop parts generation");
    if (h.requests.length !== h.beforeRejectedRequestCount) throw new Error("stale dimension change reached native disassembly");
    if (h.db.get("active").runId !== "old-active" || h.db.get("active").parameters.width !== 1233) throw new Error("stale model guard discarded the user's dimension draft");
    if (h.view.pending) throw new Error("stale model guard left the operation locked");
    h.checkInteraction("left", [150, 190, 290, 230]);
  });
  await page.evaluate(async () => {
    const h = window.h;
    h.db.get("active").parameters.width = 1200;
    h.view.tubeDesignerRightDraftDirty = false;
    h.db.get("outdated").parameters.width += 44;
    h.refresh();
    const requestCount = h.requests.length;
    let error;
    await h.openBatch();
    if (!h.view.tubeDesignerBatchDisassemblyDialog?.unavailableProductIds?.includes("outdated"))
      throw new Error("batch selector did not mark another instance's stale dimensions unavailable");
    h.view.tubeDesignerBatchDisassemblyDialog.selectedProductIds = ["outdated"];
    try { await h.confirmBatch(); } catch (failure) { error = failure; }
    if (!/模型需要更新|先更新产品模型/.test(error?.message ?? h.view.tubeDesignerBatchDisassemblyDialog?.error ?? ""))
      throw new Error("batch did not reject another instance's stale dimensions");
    if (h.requests.length !== requestCount || h.view.scene.tubeDesigner.product.entityId !== "active") throw new Error("rejected batch manufactured or activated another instance");
    if (h.db.get("outdated").parameters.width !== 1344 || h.db.get("outdated").runId !== "old-outdated") throw new Error("rejected batch discarded another instance's draft");
    h.checkInteraction("left", [150, 190, 290, 230]);
    h.view.tubeDesignerBatchDisassemblyDialog = null;
  });
  await page.evaluate(() => {
    const h = window.h;
    h.pause = true; h.failDisassembly = true; h.view.tubeDesignerBreakdownOpen = false;
    document.querySelector('[data-cam-action="tube-designer-disassemble-active-product"]').click();
  });
  await held("TubeDesigner.DisassembleSelected");
  await page.evaluate(() => {
    const h = window.h;
    h.interact("right", [160, 200, 300, 240]);
    h.report({ kind: "disassembly", phase: "geometry", completed: 0, total: 41, message: "正在生成零件几何" });
    h.checkInteraction("right", [160, 200, 300, 240]); h.checkNodes();
  });
  await release("TubeDesigner.DisassembleSelected");
  await page.evaluate(async () => {
    const h = window.h;
    let error;
    try { await h.operation; } catch (failure) { error = failure; }
    if (!error || error.message !== "native disassembly failed" || h.view.pending || h.view.tubeDesignerOperation) throw new Error("native failure retained the progress operation");
    const renders = h.renderCount(), scene = JSON.stringify(h.view.scene);
    h.report({ kind: "disassembly", phase: "completed", completed: 41, total: 41, message: "late failed progress" });
    if (h.renderCount() !== renders || h.view.tubeDesignerOperation || JSON.stringify(h.view.scene) !== scene
        || document.querySelector("[data-tube-designer-operation-wait]")) throw new Error("native failure accepted late progress");
    h.checkInteraction("right", [160, 200, 300, 240]); h.checkNodes();
  });
  assert.deepEqual(errors, []);
  console.log("Edge manufacturing updates passed: pending EC commit -> current recipe -> parts; tabs and batch instance inputs retained without display generation; real native stage/count reports patch only progress text; late reports after completion, failure, or project/page/operation changes ignored; latest focus/selection and four scroll positions; input/canvas/listeners retained.", result);
} finally { await browser.close(); }
