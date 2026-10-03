import assert from "node:assert/strict";
import { mkdirSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { spawn, spawnSync } from "node:child_process";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const source = fileURLToPath(new URL("../../", import.meta.url));
const app = resolve(source, "apps/tube-designer");
const python = process.env.ICAX_PYTHON_EXECUTABLE || "C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe";
const script = `import importlib.util,json,pathlib,sys
p=pathlib.Path(sys.argv[1])/'templates/_shared/assembly_template_runtime.py'
s=importlib.util.spec_from_file_location('browser_runtime',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
print(json.dumps(m.generate(json.load(sys.stdin),{}),ensure_ascii=False))`;
const catalogue = spawnSync(python, ["-B", "-c", script, app], { input: JSON.stringify({ action: "catalogue" }),
  encoding: "utf8", windowsHide: true, env: { ...process.env, PYTHONUTF8: "1" } });
if (catalogue.status !== 0) throw new Error(catalogue.stderr);
const templates = JSON.parse(catalogue.stdout).assemblies;
const rectProfile = JSON.parse(readFileSync(resolve(app, "templates/profile/rect/profile.json"), "utf8"));
const nativeBridgePath = process.env.ICAX_ASSEMBLY_NATIVE_BRIDGE;
let nativeBridge = null, nativeSequence = 0, nativeBuffer = "";
const nativePending = new Map();
if (nativeBridgePath) {
  nativeBridge = spawn(nativeBridgePath, [], { cwd: resolve(source, ".."), windowsHide: true,
    env: { ...process.env, PATH: `${resolve(source, "x64/Debug")};${process.env.PATH}` }, stdio: ["pipe", "pipe", "pipe"] });
  nativeBridge.stderr.on("data", (chunk) => process.stderr.write(chunk));
  nativeBridge.stdout.on("data", (chunk) => {
    nativeBuffer += chunk;
    for (;;) {
      const end = nativeBuffer.indexOf("\n"); if (end < 0) break;
      const response = JSON.parse(nativeBuffer.slice(0, end)); nativeBuffer = nativeBuffer.slice(end + 1);
      const pending = nativePending.get(response.id); nativePending.delete(response.id);
      if (pending) response.ok ? pending.resolve(response.result) : pending.reject(new Error(response.error));
    }
  });
  nativeBridge.on("exit", (code) => { for (const pending of nativePending.values()) pending.reject(new Error(`Native bridge exited ${code}`)); nativePending.clear(); });
}
const nativeInvoke = (method, payload) => new Promise((resolve, reject) => {
  const id = ++nativeSequence; nativePending.set(id, { resolve, reject });
  nativeBridge.stdin.write(JSON.stringify({ id, method: method.split(".").at(-1), payload }) + "\n");
});
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1380, height: 800 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://assembly-input.test/**", async (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/native") {
      try {
        const request = JSON.parse(route.request().postData());
        return route.fulfill({ contentType: "application/json", body: JSON.stringify(await nativeInvoke(request.method, request.payload)) });
      } catch (error) { return route.fulfill({ status: 500, body: error.message }); }
    }
    if (pathname === "/runtime") {
      const result = spawnSync(python, ["-B", "-c", script, app], { input: route.request().postData(),
        encoding: "utf8", windowsHide: true, env: { ...process.env, PYTHONUTF8: "1" } });
      return route.fulfill({ status: result.status === 0 ? 200 : 500, contentType: "application/json",
        body: result.status === 0 ? result.stdout : result.stderr || String(result.error) });
    }
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const path = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!path.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://assembly-input.test/");
  const screenshotDirectory = resolve(source, "../output/tests/assembly-process");
  mkdirSync(screenshotDirectory, { recursive: true });
  await page.exposeFunction("assemblyUiScreenshot", (name) => page.screenshot({ path: resolve(screenshotDirectory, name), fullPage: true }));
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:280px minmax(0,1fr) 430px;height:800px}.cam-context-pane,.cam-info-pane{min-height:0;overflow:hidden}.cam-viewport{position:relative;background:#13252d}.tube-connection-library-list{height:160px!important;max-height:160px!important;overflow:auto}.tube-connection-library-editor-body{height:450px;overflow:auto}#stock-profile>.tube-connection-library-parameter-grid{max-height:115px;overflow:auto}.tube-stock-operation-list{max-height:150px;overflow:auto}` });
  const result = await page.evaluate(async ({ templates, useNative, rectProfile }) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const model = await import("/src/apps/tube-designer/webpage/finishedProductModel.mjs");
    const editor = await import("/src/apps/tube-designer/webpage/assemblyProcessEditor.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const calls = [], pending = [], pendingLoads = [], savedPlans = new Map(), committedRequests = new Map();
    let holdNative = false, holdLoad = false, failNextCommit = false, nativeReplaySeen = false, currentAction, listenerCalls = 0;
    let view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "two-end-end-angle", workMode: "example", catalogueStatus: "ready" },
      tubeDesignerSystemProfiles: [{ id: "rect", name: "方矩管", profileForm: "parametric", defaultParameters: {},
        descriptor: { parameters: useNative ? rectProfile.parameters : [
          ...["width", "depth", "wallThickness", "cornerRadius", "innerRadius"].map((key) => ({ key,
            displayName: key, valueType: "number", defaultValue: 2, min: 0 })),
          { key: "showDraft", displayName: "展开草稿", valueType: "boolean", defaultValue: false },
          { key: "inactiveDraft", displayName: "保留草稿", valueType: "string", defaultValue: "draft",
            visibleWhen: { op: "eq", parameter: "showDraft", value: true } },
        ] } }], viewport: { setVisibleEntityIds() {}, setContinuousRendering() {} } };
    const actual = async (payload) => {
      const response = await fetch("/runtime", { method: "POST", body: JSON.stringify(payload) });
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    };
    const native = async (method, payload) => {
      const response = await fetch("/native", { method: "POST", body: JSON.stringify({ method, payload }) });
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    };
    const context = { project: { projectId: "local-project" }, sceneProxy: { sceneId: "scene", resources: {},
      async invoke(method, payload) {
        calls.push({ method, payload: structuredClone(payload) });
        if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") return actual({ action: "preview-plan", ...payload });
        if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") return actual({ action: "check-applicability", ...payload });
        if (method === "TubeDesigner.ResolveAssemblyProcessPlan") return actual({ action: "process-plan", ...payload });
        if (method === "TubeDesigner.PreviewAssemblyProcessPlan") {
          const plan = useNative ? await native(method, payload) : await actual({ action: "process-plan", ...payload });
          if (useNative && (plan.geometryChecks?.length !== payload.stocks.length
              || !plan.geometryChecks.every((check) => check.valid && check.solids === 1)))
            throw new Error("Native BRep checks did not confirm one valid solid per stock");
          if (!useNative) plan.nativeResults = plan.manufacturingParts.map((part) => ({ blankId: part.blankId ?? part.id,
            previewComputed: true, resultValid: true, solidCount: 1,
            geometry: { url: `memory://machined-${part.id}`, version: calls.length } }));
          return holdNative ? new Promise((resolve) => pending.push(() => resolve(plan))) : plan;
        }
        if (method === "TubeDesigner.GetAssemblyProcessPlans") {
          const response = useNative ? await native(method, payload)
            : { schema: "icax.assembly-process-plans", schemaVersion: 1, plans: [...savedPlans.values()].map((item) => structuredClone(item)) };
          return holdLoad ? new Promise((resolve) => pendingLoads.push(() => resolve(response))) : response;
        }
        if (method === "TubeDesigner.CommitAssemblyProcessPlan") {
          if (useNative) {
            const response = await native(method, payload);
            nativeReplaySeen ||= response.replayed === true;
            savedPlans.set(payload.planId, { planId: payload.planId, planDigest: response.planDigest,
              sourceInput: structuredClone({ stocks: payload.stocks, instances: payload.instances }), entityIds: response.entityIds });
            if (failNextCommit) { failNextCommit = false; throw new Error("保存响应中断，请重试"); }
            return response;
          }
          if (committedRequests.has(payload.requestId)) return { ...committedRequests.get(payload.requestId), replayed: true };
          const plan = await actual({ action: "process-plan", stocks: payload.stocks, instances: payload.instances });
          if (plan.planDigest !== payload.expectedPlanDigest) throw new Error("changed plan digest");
          if (savedPlans.has(payload.planId) && savedPlans.get(payload.planId).planDigest !== payload.expectedStoredPlanDigest)
            throw new Error("stored plan changed");
          const record = { planId: payload.planId, planDigest: plan.planDigest,
            sourceInput: structuredClone({ stocks: payload.stocks, instances: payload.instances }), entityIds: ["saved-stock"] };
          savedPlans.set(payload.planId, record);
          const response = { schema: "icax.assembly-process-commit", schemaVersion: 1, planId: record.planId,
            planDigest: record.planDigest, entityIds: record.entityIds, replayed: false };
          committedRequests.set(payload.requestId, response);
          if (failNextCommit) { failNextCommit = false; throw new Error("保存响应中断，请重试"); }
          return response;
        }
        if (method === "TubeDesigner.GetAssemblyTemplates") return { assemblies: templates, errors: [] };
        if (method === "TubeDesigner.PreviewAssemblyManufacturingPart" || method === "TubeDesigner.PreviewPunchWizard") {
          const response = { previewComputed: true, resultValid: true, solidCount: 1,
            geometry: { url: "memory://machined-stock", version: calls.length },
            baseGeometry: { url: "memory://original", version: 1 } };
          return holdNative ? new Promise((resolve) => pending.push(() => resolve(response))) : response;
        }
        if (method === "TubeDesigner.EvaluateProfilePackage") throw new Error("profile diagram not provided in browser fixture");
        throw new Error(`Unexpected method ${method}`);
      } } };
    const viewportRoot = document.createElement("div"), snapshots = [];
    const viewport = { mount(host) { host.replaceChildren(viewportRoot); return this; },
      async applyViewSnapshot(snapshot) { snapshots.push(snapshot); return { applied: true,
        entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [] }; },
      setDimensionAnnotations() {}, setVisibleEntityIds() {}, setStandardView() {}, fitViewToViewport() {},
      getVisibleBounds: () => ({ min: [0, -30, -20], max: [520, 30, 20] }), dispose() {} };
    const render = () => {
      const previousMount = document.querySelector("main");
      const restorePanes = previousMount ? capturePaneInteraction(previousMount) : () => {};
      const left = library.renderAssemblyLibraryLeftPane(context, view), right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      let mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas id="stable-canvas"></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        mount = document.querySelector("main");
        for (const [eventName, attribute] of [["click", "camAction"], ["change", "camChangeAction"]])
          mount.addEventListener(eventName, (event) => {
            const selector = eventName === "click" ? "[data-cam-action]" : "[data-cam-change-action]";
            const target = event.target.closest(selector);
            if (target) currentAction = library.handleAssemblyLibraryAction(context, view, target.dataset[attribute], target, { renderProject: render });
          });
        patch.rememberLibraryDom(view, mount, "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) throw new Error("editor remounted");
      library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory: () => viewport });
      restorePanes();
    };
    const pause = () => new Promise((resolve) => setTimeout(resolve, 15));
    const settle = async () => {
      for (let i = 0; i < (useNative ? 2400 : 180) && (view.tubeDesignerAssemblyLibrary.previewRequest || view.tubeDesignerAssemblyLibrary.selectionRequest
          || view.tubeDesignerStockProcessStorage?.loadRequest && !holdLoad); i++) {
        pending.splice(0).forEach((resolve) => resolve()); await pause();
      }
      if (view.tubeDesignerAssemblyLibrary.previewError) throw new Error(view.tubeDesignerAssemblyLibrary.previewError);
    };
    const click = async (selector) => { const node = document.querySelector(selector); if (!node) throw new Error(`Missing ${selector}`);
      currentAction = null; node.click(); await currentAction; await settle(); };
    const change = async (selector, value, wait = true) => {
      const node = document.querySelector(selector); if (!node) throw new Error(`Missing ${selector}`);
      if (node.type === "checkbox") node.checked = value; else node.value = value;
      currentAction = null; node.dispatchEvent(new Event("change", { bubbles: true })); await currentAction;
      if (wait) await settle();
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    render(); await settle();
    const original = model.finishedProductKey(model.finishedProductInput(view));
    const applicability = calls.find((call) => call.method.endsWith("CheckAssemblyTemplateApplicability"));
    const localInput = applicability?.payload.processInput;
    if (!localInput || applicability.payload.finishedProduct || localInput.shapeId || localInput.spans)
      throw new Error("Processing still receives global product styles");
    const productAngleNode = document.querySelector('[data-finished-parameter="angle"]');
    await change('[data-process-role="memberA"][data-process-anchor-key="end"]', "start");
    await click('[data-tube-assembly-view="exploded"]');
    const miter = view.tubeDesignerAssemblyLibrary.preview.plan;
    const changedEnd = miter.processInput.parts.memberA.anchor.end === "start"
      && miter.manufacturingParts.find((part) => part.sourceRole === "memberA").request.ends.start.toolRef?.id === "end-miter"
      && model.finishedProductKey(model.finishedProductInput(view)) === original
      && document.querySelector('[data-finished-parameter="angle"]') === productAngleNode;
    await click('[data-tube-assembly-id="node-v-notch-integrated"][data-cam-action="tube-designer-assembly-select"]');
    await click('[data-process-input-mode="stock-operation"]');
    await click('[data-cam-action="tube-designer-stock-operation-add"]');
    const stock = view.tubeDesignerStockProcess;
    if (stock.instances.length !== 2 || stock.instances[0].angle !== 90 || stock.instances[1].angle !== -90)
      throw new Error("Left/right folds were not saved as independent operations");
    await change('[data-stock-instance="stock-operation-2"][data-stock-operation-parameter="rotation"]', 90);
    const actualPlan = view.tubeDesignerAssemblyLibrary.preview.plan;
    if (actualPlan.manufacturingParts.length !== 1 || actualPlan.instances.length !== 2 || actualPlan.forming.length !== 2)
      throw new Error("Two folds did not resolve into one original stock");
    await window.assemblyUiScreenshot("continuous-stock-blank-ui.png");
    const canvas = document.querySelector("#stable-canvas"), label = document.querySelector('[data-cam-change-action="tube-designer-stock-label"]');
    label.addEventListener("input", () => listenerCalls++);
    holdNative = true;
    await change('[data-stock-instance="stock-operation-2"][data-stock-operation-parameter="station"]', 400, false);
    for (let i = 0; i < (useNative ? 2400 : 100) && !pending.length; i++) await pause();
    if (!pending.length) throw new Error(`No asynchronous manufacturing response pending: ${view.tubeDesignerAssemblyLibrary.previewError}`);
    label.value = "Continuous-stock-ABCDE"; label.focus({ preventScroll: true }); label.setSelectionRange(3, 12);
    const left = document.querySelector(".tube-connection-library-list"), right = document.querySelector(".tube-connection-library-editor-body");
    const nested = document.querySelector("#stock-profile>.tube-connection-library-parameter-grid"), rows = document.querySelector("#stock-operation-list");
    left.scrollTop = 37; right.scrollTop = 170; nested.scrollTop = 55; rows.scrollTop = 61;
    // These positions are changed while the backend is pending, after the request began.
    left.scrollTop = 59; right.scrollTop = 195; nested.scrollTop = 70; rows.scrollTop = 80;
    const expectedScroll = [left.scrollTop, right.scrollTop, nested.scrollTop, rows.scrollTop];
    holdNative = false; pending.splice(0).forEach((resolve) => resolve()); await settle();
    const retained = document.querySelector('[data-cam-change-action="tube-designer-stock-label"]') === label
      && document.querySelector("#stable-canvas") === canvas && document.activeElement === label
      && label.selectionStart === 3 && label.selectionEnd === 12;
    const actualScroll = [left.scrollTop, right.scrollTop, nested.scrollTop, rows.scrollTop];
    label.dispatchEvent(new Event("input", { bubbles: true }));
    const toggle = useNative ? "useInnerRadii" : "showDraft", child = useNative ? "innerRadius1" : "inactiveDraft";
    const draftValue = useNative ? "1.2" : "saved-draft";
    await change(`[data-stock-profile-parameter="${toggle}"]`, true);
    await change(`[data-stock-profile-parameter="${child}"]`, draftValue);
    const removed = document.querySelector(`[data-stock-profile-parameter="${child}"]`); removed.focus();
    await change(`[data-stock-profile-parameter="${toggle}"]`, false);
    const removedNotRefocused = !document.querySelector(`[data-stock-profile-parameter="${child}"]`) && document.activeElement !== removed;
    await change(`[data-stock-profile-parameter="${toggle}"]`, true);
    const draftPreserved = document.querySelector(`[data-stock-profile-parameter="${child}"]`).value === draftValue;
    await click('[data-cam-action="tube-designer-stock-operation-select"][data-stock-instance="stock-operation-1"]');
    await change('[data-tube-part-process-parameter="leaveBottom"]', 1.2);
    const independentSettings = !stock.instances[1].settings["node-v-notch-integrated"].processDrafts["node-slot"]
      && stock.instances[0].settings["node-v-notch-integrated"].processDrafts["node-slot"]["v-notch-sharp"].leaveBottom === 1.2;
    const finalPayload = editor.stockProcessPayload(view, library.selectedAssemblyTemplate(view), library.assemblyTemplates(view));
    const calibrationRowsBefore = snapshots.length;
    await click('[data-tube-assembly-view="finished"]');
    for (let i = 0; i < 100 && snapshots.length === calibrationRowsBefore; i++) await pause();
    const calibrationShown = view.tubeDesignerAssemblyLibrary.preview.designParts.length >= 3
      && snapshots.at(-1).rows.length === view.tubeDesignerAssemblyLibrary.preview.designParts.length
      && document.querySelector('[data-tube-assembly-view="finished"]').textContent === "成形校核";
    await window.assemblyUiScreenshot("continuous-stock-calibration-ui.png");
    stock.stock.matrix = [1, 0, 0, 27, 0, 1, 0, 19, 0, 0, 1, 8, 0, 0, 0, 1];
    await change('[data-cam-change-action="tube-designer-stock-label"]', "保存的连续管");
    failNextCommit = true;
    await click('[data-cam-action="tube-designer-stock-plan-save"]');
    await click('[data-cam-action="tube-designer-stock-plan-save"]');
    const saveCalls = calls.filter((call) => call.method === "TubeDesigner.CommitAssemblyProcessPlan");
    const retryIdPreserved = saveCalls.length === 2 && saveCalls[0].payload.requestId === saveCalls[1].payload.requestId
      && savedPlans.size === 1 && (!useNative || nativeReplaySeen);
    const savedRecord = [...savedPlans.values()][0], savedDigest = savedRecord.planDigest;
    const profiles = view.tubeDesignerSystemProfiles;
    const reopenedView = () => ({ activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerSystemProfiles: profiles, tubeDesignerAssemblyLibrary: { selectedId: "node-v-notch-integrated",
        workMode: "example", processInputMode: "stock-operation", catalogueStatus: "ready", exploded: true },
      viewport: { setVisibleEntityIds() {}, setContinuousRendering() {} }, tubeDesignerAssemblyLibraryRenderProject: render });
    view = reopenedView(); render(); await settle();
    const reopened = editor.stockProcessPayload(view, library.selectedAssemblyTemplate(view), library.assemblyTemplates(view));
    const restored = model.finishedProductKey(reopened) === model.finishedProductKey(savedRecord.sourceInput)
      && view.tubeDesignerStockProcess.planId === savedRecord.planId
      && document.querySelector("#stable-canvas") === canvas;
    await change('[data-cam-change-action="tube-designer-stock-label"]', "更新的连续管");
    await click('[data-cam-action="tube-designer-stock-plan-save"]');
    const updated = calls.filter((call) => call.method === "TubeDesigner.CommitAssemblyProcessPlan").at(-1).payload;
    const updateGuard = updated.expectedStoredPlanDigest === savedDigest && updated.planId === savedRecord.planId
      && updated.requestId !== saveCalls[0].payload.requestId && savedPlans.size === 1;
    holdLoad = true; view = reopenedView(); render();
    for (let i = 0; i < 100 && !pendingLoads.length; i++) await pause();
    await change('[data-cam-change-action="tube-designer-stock-label"]', "用户正在编辑", false);
    holdLoad = false; pendingLoads.splice(0).forEach((resolve) => resolve()); await settle();
    const pendingReadPreserved = view.tubeDesignerStockProcess.stock.label === "用户正在编辑"
      && !view.tubeDesignerStockProcess.planId;
    while (view.tubeDesignerStockProcess.instances.length)
      await click(`[data-cam-action="tube-designer-stock-operation-remove"][data-stock-instance="${view.tubeDesignerStockProcess.instances[0].instanceId}"]`);
    const raw = view.tubeDesignerAssemblyLibrary.preview.plan;
    const allRemoved = view.tubeDesignerStockProcess.instances.length === 0 && raw.instances.length === 0
      && raw.manufacturingParts.length === 1 && !raw.manufacturingParts[0].request.features.length
      && !document.querySelector('[data-tube-part-process-parameter="leaveBottom"]')
      && document.querySelector('[data-cam-action="tube-designer-stock-operation-add"]');
    await click('[data-cam-action="tube-designer-stock-plan-save"]');
    const rawRecord = [...savedPlans.values()].find((record) => record.sourceInput.instances.length === 0);
    view = reopenedView(); render(); await settle();
    await change('[data-cam-change-action="tube-designer-stock-plan-load"]', rawRecord.planId);
    const emptyRestored = view.tubeDesignerStockProcess.instances.length === 0
      && view.tubeDesignerAssemblyLibrary.preview.plan.instances.length === 0
      && view.tubeDesignerStockProcess.planId === rawRecord.planId;
    return { retained, expectedScroll, actualScroll, listenerCalls, removedNotRefocused, draftPreserved, independentSettings,
      calibrationShown, retryIdPreserved, restored, updateGuard, pendingReadPreserved,
      changedEnd, allRemoved: !!allRemoved, emptyRestored,
      productPreserved: model.finishedProductKey(model.finishedProductInput(view)) === original,
      inputRoles: Object.keys(localInput.parts), oneStock: actualPlan.manufacturingParts.length,
      folds: actualPlan.forming.map((fold) => fold.angle ?? fold.angleDeg), payload: finalPayload,
      operations: actualPlan.operations.length, snapshots: snapshots.length };
  }, { templates, useNative: !!nativeBridge, rectProfile });
  assert.deepEqual(errors, []);
  for (const key of ["retained", "removedNotRefocused", "draftPreserved", "independentSettings", "productPreserved",
    "calibrationShown", "retryIdPreserved", "restored", "updateGuard", "pendingReadPreserved", "changedEnd", "allRemoved", "emptyRestored"])
    assert.equal(result[key], true, `${key}: ${JSON.stringify(result)}`);
  assert.deepEqual(result.actualScroll, result.expectedScroll);
  assert.ok(result.expectedScroll.every((value) => value > 0));
  assert.equal(result.listenerCalls, 1);
  assert.equal(result.oneStock, 1);
  assert.equal(result.payload.stocks.length, 1);
  assert.equal(result.payload.instances.length, 2);
  assert.equal(result.payload.instances[1].processInput.geometry.angle, -90);
  assert.equal(result.payload.instances[1].processInput.geometry.rotation, 90);
  console.log(`Assembly process input Edge regression passed: actual Python local input/left-right folds on one stock, independent parameters, target calibration view, save/retry/update/reopen flow, zero-operation restore, pending project read preserves edits, unchanged product, retained canvas/input/listener/focus/selection, both sidebars and nested async scroll, conditional draft fields. ${nativeBridge ? "Combined preview/commit/Get use the real native SDO; viewport resource rendering is a browser fixture." : "Native geometry/persistence responses are browser fixtures."}`);
  console.log(JSON.stringify({ inputRoles: result.inputRoles, oneStock: result.oneStock, operations: result.operations,
    retained: result.retained, scroll: result.actualScroll, independentSettings: result.independentSettings }));
} finally { await browser.close(); nativeBridge?.kill(); }
