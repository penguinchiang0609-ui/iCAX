import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { readFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const repository = fileURLToPath(new URL("../../../", import.meta.url));
const source = resolve(repository, "src");
const uiRoot = resolve(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT || source, "apps/tube-designer");
const binaryRoot = resolve(repository, "src/x64/Debug");
const bridge = spawn(process.env.ICAX_SECURITY_WINDOW_NATIVE_BRIDGE
  || resolve(repository, "tmp/security-window-frames-native/SecurityWindowFramesBridge.exe"), [], {
  cwd: repository, windowsHide: true, env: { ...process.env, PATH: `${binaryRoot};${process.env.PATH}` },
  stdio: ["pipe", "pipe", "pipe"],
});
let sequence = 0, stderr = "";
const requests = new Map();
const rejectAll = (error) => { for (const request of requests.values()) {
  clearTimeout(request.timer); request.reject(error);
} requests.clear(); };
bridge.stderr.on("data", (data) => { stderr += data; });
bridge.on("error", rejectAll);
bridge.on("exit", (code) => { if (requests.size) rejectAll(new Error(`Native bridge exited ${code}: ${stderr}`)); });
createInterface({ input: bridge.stdout }).on("line", (line) => {
  let response;
  try { response = JSON.parse(line); } catch { rejectAll(new Error(line)); return; }
  const request = requests.get(response.id);
  if (!request) return;
  requests.delete(response.id); clearTimeout(request.timer);
  response.ok ? request.resolve(response.result) : request.reject(new Error(response.error));
});
const invoke = (method, payload = {}) => new Promise((resolveRequest, reject) => {
  const id = ++sequence;
  requests.set(id, { resolve: resolveRequest, reject,
    timer: setTimeout(() => { requests.delete(id); reject(new Error(`Native timeout: ${method}`)); }, 180000) });
  bridge.stdin.write(`${JSON.stringify({ id, method, payload })}\n`);
});
const keyOf = (row) => row.stableKey || row.itemKey || row.key || "";
const outer = (rows) => rows.filter((row) => keyOf(row).startsWith("outer_frame."));
let browser;
try {
  const templateId = "single-face-security-window";
  const descriptor = (await invoke("GetTemplateDescriptor", { templateId })).template;
  const parameters = { faceType: "three", frameManufacturingMode: "spatial_v_notch", accessDoorEnabled: false,
    verticalMaximumCenterSpacing: 600, sideVerticalMaximumCenterSpacing: 600 };
  const preview = await invoke("GenerateProductTemplatePreview", { templateId, parameters });
  assert.equal(outer(preview.items).length, 10, "template preview contains logical tubes, never three blanks");
  const generated = await invoke("GeneratePreview", { templateId, ...parameters });
  assert.equal(outer(generated.tubeDesigner.members).length, 10);
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
  browser = await chromium.launch({ headless: true, channel: "msedge" });
  const page = await browser.newPage({ viewport: { width: 1300, height: 820 } });
  const output = resolve(repository, "output/tests/security-window-stock-mapping");
  mkdirSync(output, { recursive: true });
  await page.exposeFunction("captureStockMapping", async () => page.screenshot({
    path: resolve(output, process.env.ICAX_ASSEMBLY_RUNTIME_ROOT ? "spatial-stock-mapping-deployed.png" : "spatial-stock-mapping-source.png"),
    fullPage: true,
  }));
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://security-stock.test/**", async (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/native") {
      const request = JSON.parse(route.request().postData());
      const method = request.method.replace(/^TubeDesigner\./, "");
      try { return route.fulfill({ contentType: "application/json",
        body: JSON.stringify(await invoke(method === "DisassembleSelected" ? "Disassemble" : method, request.payload)) }); }
      catch (error) { return route.fulfill({ status: 500, contentType: "text/plain", body: error.message }); }
    }
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    const prefix = "/src/apps/tube-designer/";
    return route.fulfill({ contentType: "text/javascript",
      body: readFileSync(pathname.startsWith(prefix) ? resolve(uiRoot, pathname.slice(prefix.length)) : file, "utf8") });
  });
  await page.goto("http://security-stock.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:270px minmax(0,1fr) 440px;height:700px}.cam-context-pane,.cam-info-pane{height:700px;overflow:auto}.cam-viewport{background:#13252d}.tube-designer-instance-list{height:180px;overflow:auto}.parameter-nested{height:160px;overflow:auto}.tube-assembly-manufacturing-plan{max-height:160px;overflow:auto}.tube-designer-product-parts-dock{height:120px}.filler{height:600px}` });
  const result = await page.evaluate(async ({ generated, descriptor }) => {
    const binding = await import("/src/apps/tube-designer/webpage/productAssemblyBindings.mjs");
    const connection = await import("/src/apps/tube-designer/webpage/productAssemblyConnections.mjs");
    const actions = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const views = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const native = async (method, payload = {}) => {
      const response = await fetch("/native", { method: "POST", body: JSON.stringify({ method, payload }) });
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    };
    const view = { activeAreaId: "view", scene: { tubeDesigner: generated.tubeDesigner },
      tubeDesignerTemplateDescriptors: { [descriptor.id]: descriptor },
      tubeDesignerRightDraft: structuredClone(generated.tubeDesigner.product.parameters) };
    const descriptorForScene = () => {
      view.scene.tubeDesigner.templates = (view.scene.tubeDesigner.templates ?? []).map((item) => item.id === descriptor.id
        ? { ...item, ...descriptor, available: true, descriptorLoaded: true } : item);
    };
    descriptorForScene();
    let pendingParameterResult = null, holdParameters = false, parameterRequestStarted = false;
    const context = { mount: document.body, project: { projectId: "stock-mapping-test" },
      sceneProxy: { async invoke(method, payload) {
        if (method === "TubeDesigner.UpdateProductParameters") parameterRequestStarted = true;
        const response = await native(method, payload);
        if (holdParameters && method === "TubeDesigner.UpdateProductParameters")
          return new Promise((resolve) => { pendingParameterResult = () => resolve(response); });
        return response;
      } }, actions: { async refreshActiveSceneState() { descriptorForScene(); render(); } } };
    const escape = (value) => String(value ?? "").replaceAll("&", "&amp;").replaceAll('"', "&quot;").replaceAll("<", "&lt;");
    let summaryHtml = "", connectionHtml = "";
    const render = () => {
      const restore = capturePaneInteraction(document.body);
      actions.captureDesignerScrollState(context, view);
      const product = view.scene.tubeDesigner.product, values = view.tubeDesignerRightDraft ?? product.parameters;
      const left = `<div class="tube-designer-instance-list">${view.scene.tubeDesigner.members.map((member) => `<div style="height:32px">${escape(member.name || member.stableKey)}</div>`).join("")}</div><div class="filler"></div>`;
      const right = `<section data-tube-designer-parameter-form><div data-tube-designer-parameter-scroll class="parameter-nested"><div style="height:180px"></div><input data-tube-designer-parameter="productCode" value="${escape(values.productCode)}"><label>管宽<input type="number" data-tube-designer-parameter="frameWidth" value="${escape(values.frameWidth)}"></label><label>加工路线<select data-tube-designer-parameter="frameManufacturingMode">${["segment_weld", "plane_v_notch", "spatial_v_notch"].map((value) => `<option value="${value}"${values.frameManufacturingMode === value ? " selected" : ""}>${value}</option>`).join("")}</select></label><div style="height:360px"></div></div></section>${summaryHtml}${connectionHtml}<div class="filler"></div>`;
      if (!document.querySelector("main")) document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas id="retained-product"></canvas></div><aside class="cam-info-pane">${right}</aside></div><div id="dock"></div></main>`;
      else {
        document.querySelector(".cam-context-pane").innerHTML = left;
        document.querySelector(".cam-info-pane").innerHTML = right;
      }
      document.querySelector("#dock").innerHTML = views.renderDesignerProductPartsDock(context, view);
      actions.restoreDesignerScrollState(context, view); restore();
    };
    const ops = { renderProject: render, showNotice: render, appendProjectLog() {},
      async refreshActiveAreaView() { return { revision: "native-product-test",
        viewportReceipt: { applied: true, revision: "native-product-test",
          entityIds: view.scene.tubeDesigner.members.map((member) => member.entityId) } }; } };
    const key = (row) => row.stableKey || row.itemKey || row.key || "";
    const outerMembers = () => view.scene.tubeDesigner.members.filter((member) => key(member).startsWith("outer_frame."));
    const outerParts = () => view.scene.tubeDesigner.manufacturingGroups.flatMap((group) => group.parts)
      .filter((part) => key(part).startsWith("outer_frame."));
    const memberShapeKey = () => JSON.stringify(view.scene.tubeDesigner.members.map((member) => [member.entityId,
      member.previewGeometryResourceId, member.previewGeometryResourceVersion, member.transform]));
    render();
    const canvas = document.querySelector("canvas"), originalMemberShapes = memberShapeKey();
    await actions.handleDesignerAreaAction(context, view, "tube-designer-disassemble-active-product", null, ops);
    if (outerMembers().length !== 10 || outerParts().length !== 3) throw new Error("成品和连续母材数量混淆");
    const physicalIds = outerParts().map((part) => part.entityId).sort();
    const showSummaries = async () => {
      const activeArea = view.activeAreaId; view.activeAreaId = "assemblies";
      await binding.ensureProductAssemblyBindings(context, view, { renderProject() {} });
      await connection.ensureProductAssemblyConnections(context, view, { renderProject() {} });
      const result = binding.productAssemblyBindingsState(view).result;
      const stocks = result.manufacturingPlan.stocks.filter((stock) => key(stock).startsWith("outer_frame."));
      if (stocks.length !== outerParts().length) throw new Error("母材摘要未读取实际下料输出");
      const sources = stocks.flatMap((stock) => stock.sourceMembers.map((member) => member.itemKey));
      if (new Set(sources).size !== 10) throw new Error("母材没有完整覆盖十根成品管件");
      if (!stocks.every((stock) => stock.stockEntityId && stock.partMappingStatus === "persisted"))
        throw new Error("母材没有使用真实保存标识");
      summaryHtml = binding.renderProductAssemblyBindingOverview(view, "真实产品下料");
      connectionHtml = connection.renderProductAssemblyConnections(view);
      view.activeAreaId = activeArea; render();
      return stocks.map((stock) => stock.stockEntityId).sort();
    };
    const summaryIds = await showSummaries();
    document.querySelector("#tube-binding-manufacturing-plan").open = true;
    await window.captureStockMapping();
    const reopened = await native("SaveAndReopen");
    if (reopened.savedAndReopened !== true) throw new Error("没有真实保存重开");
    view.scene.tubeDesigner = reopened.tubeDesigner; descriptorForScene();
    view.tubeDesignerProductAssemblyBindings = undefined; view.tubeDesignerAssemblyProductConnections = undefined;
    if (JSON.stringify(outerParts().map((part) => part.entityId).sort()) !== JSON.stringify(physicalIds))
      throw new Error("重开改变了母材标识");
    const reopenedSummaryIds = await showSummaries();
    const interactions = [];
    const edit = async (parameter, value, expectModelDirty) => {
      const field = document.querySelector(`[data-tube-designer-parameter="${parameter}"]`);
      field.value = String(value); holdParameters = true; parameterRequestStarted = false;
      let updateError = null;
      const operation = actions.handleDesignerAreaAction(context, view, "tube-designer-parameter-change", field, ops)
        .catch((error) => { updateError = error; });
      for (let index = 0; index < 100 && !parameterRequestStarted && !updateError; index++)
        await new Promise((done) => setTimeout(done, 5));
      if (!parameterRequestStarted) throw new Error("真实参数更新未发起");
      const immediateDirty = view.tubeDesignerRightDraftDirty;
      for (let index = 0; index < 1800 && !pendingParameterResult && !updateError; index++)
        await new Promise((done) => setTimeout(done, 100));
      if (updateError) throw updateError;
      if (!pendingParameterResult) throw new Error("真实参数更新没有返回");
      const code = document.querySelector('[data-tube-designer-parameter="productCode"]');
      code.focus({ preventScroll: true }); code.value = "LATEST-UNCOMMITTED"; code.setSelectionRange(2, 7);
      const left = document.querySelector(".cam-context-pane"), innerLeft = document.querySelector(".tube-designer-instance-list");
      const right = document.querySelector(".cam-info-pane"), nested = document.querySelector(".parameter-nested");
      left.scrollTop = 130; innerLeft.scrollTop = 170; right.scrollTop = 110; nested.scrollTop = 190;
      const expected = [left.scrollTop, innerLeft.scrollTop, right.scrollTop, nested.scrollTop];
      const complete = pendingParameterResult; pendingParameterResult = null; holdParameters = false; complete();
      await operation; await new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)));
      const focused = document.querySelector('[data-tube-designer-parameter="productCode"]');
      const actual = [document.querySelector(".cam-context-pane").scrollTop, document.querySelector(".tube-designer-instance-list").scrollTop,
        document.querySelector(".cam-info-pane").scrollTop, document.querySelector(".parameter-nested").scrollTop];
      interactions.push({ parameter, immediateDirty: immediateDirty === expectModelDirty,
        dirty: view.tubeDesignerRightDraftDirty === expectModelDirty,
        focus: document.activeElement === focused && focused.selectionStart === 2 && focused.selectionEnd === 7,
        value: focused.value === "LATEST-UNCOMMITTED", scroll: JSON.stringify(actual) === JSON.stringify(expected),
        canvas: document.querySelector("canvas") === canvas, staleDock: document.querySelector(".tube-designer-product-parts-dock").classList.contains("is-outdated") });
      return { expected, actual };
    };
    const processScroll = await edit("frameManufacturingMode", "plane_v_notch", false);
    if (memberShapeKey() !== originalMemberShapes || outerMembers().length !== 10)
      throw new Error("加工路线编辑改变了已提交成品");
    summaryHtml = ""; connectionHtml = "";
    await actions.handleDesignerAreaAction(context, view, "tube-designer-disassemble-active-product", null, ops);
    if (outerParts().length !== 6 || outerMembers().length !== 10) throw new Error("加工复算没有独立更新母材");
    const recomputedSummaryIds = await showSummaries();
    if (recomputedSummaryIds.length !== 6) throw new Error("正常读取仍使用旧空间母材缓存");
    const profileScroll = await edit("frameWidth", Number(view.scene.tubeDesigner.product.parameters.frameWidth) + 2, true);
    if (view.scene.tubeDesigner.product.modelOutdated !== true) throw new Error("原生管宽修改没有标记成品过期");
    return { summaryIds, reopenedSummaryIds, physicalIds, interactions, processScroll, profileScroll,
      finishedOuterCount: outerMembers().length, planeStockCount: outerParts().length };
  }, { generated, descriptor });
  assert.deepEqual(errors, []);
  assert.deepEqual(result.summaryIds, result.physicalIds);
  assert.deepEqual(result.reopenedSummaryIds, result.physicalIds);
  assert.ok(result.interactions.every((interaction) => Object.values(interaction).every(Boolean)), JSON.stringify(result));
  assert.ok(result.processScroll.expected.every((value) => value > 0));
  assert.ok(result.profileScroll.expected.every((value) => value > 0));
  assert.equal(result.finishedOuterCount, 10); assert.equal(result.planeStockCount, 6);
  await page.screenshot({ path: resolve(output, "stock-mapping-ui.png"), fullPage: true });
  console.log("Security-window native + Edge regression passed: 10 logical outer tubes / 3 spatial stocks, native file save/reopen, many-source summaries, process-only recompute, profile geometry expiration, focus/selection/latest left-right nested scroll and retained canvas. Viewport rendering is a browser fixture.");
  console.log(JSON.stringify(result));
} finally { if (browser) await browser.close(); bridge.stdin.end(); bridge.kill(); }
