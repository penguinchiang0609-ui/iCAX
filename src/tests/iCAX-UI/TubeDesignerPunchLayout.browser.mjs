// Isolated UI test. No connection to an open project, native geometry or user files.
// ICAX_PLAYWRIGHT_MODULE may point to the bundled Playwright module URL.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync, readdirSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const artifactDir = resolve(process.env.ICAX_ARTIFACT_DIR || fileURLToPath(new URL("../../../tmp/punch-layout-browser/", import.meta.url)));
const toolsRoot = new URL("../../apps/tube-designer/templates/_shared/punch-tools/", import.meta.url);
const tools = readdirSync(toolsRoot, { withFileTypes: true }).filter(item => item.isDirectory())
  .map(item => JSON.parse(readFileSync(new URL(item.name + "/tool.json", toolsRoot))))
  .map(tool => ({ ...tool, digest: "isolated-layout-browser", defaultParameters: Object.fromEntries((tool.parameters ?? []).map(p => [p.key, p.defaultValue])) }));
const commonCss = readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css", import.meta.url), "utf8");
mkdirSync(artifactDir, { recursive: true });
const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });

try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.setDefaultTimeout(10000);
  const errors = [], externalRequests = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("**/*", route => {
    const url = new URL(route.request().url()), path = url.pathname;
    if (url.origin !== "http://punch.test") { externalRequests.push(url.href); return route.abort(); }
    if (path === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><html lang='zh-CN'><meta charset='utf-8'><body><div id='app' class='tube-designer-workspace'></div></body></html>" });
    const file = resolve(sourceRoot, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://punch.test/");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}" + commonCss + tubeDesignerCss });
  await page.evaluate(async catalogue => {
    const wizard = await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const parts = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const editor = await import("/src/apps/tube-designer/webpage/punchEditor.mjs");
    const review = await import("/src/apps/tube-designer/webpage/punchReview.mjs");
    const arrays = await import("/src/apps/tube-designer/webpage/punchArrayGroups.mjs");
    const dom = await import("/src/apps/tube-designer/webpage/punchDomPatch.mjs");
    const designerViews = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const cube = await import("/src/apps/_shared/workbench/viewport/viewCube.mjs");
    const part = { entityId: "isolated-layout", name: "冲孔布局隔离测试", partNumber: "TEST-ONLY", length: 1000,
      independentNesting: true, profile: { kind: "round", name: "圆管", diameter: 80, width: 80, depth: 80, wallThickness: 2 },
      properties: { "manufacturing.partKind": "tube" } };
    const view = { pending: false, activeAreaId: "nesting", scene: { tubeDesigner: { nestingGroups: [{ productEntityId: "test-only", parts: [part] }] } },
      tubeDesignerSystemProfiles: [{ id: "fixture-round", name: "测试圆管 Φ32", profileType: "parametric-package",
        defaultParameters: { diameter: 32, wallThickness: 2 },
        descriptor: { parameters: [
          { key: "diameter", displayName: "外径", valueType: "number", unit: "mm", defaultValue: 32, min: 1 },
          { key: "wallThickness", displayName: "壁厚", valueType: "number", unit: "mm", defaultValue: 2, min: 0.1 },
        ] }, previewProfile: { kind: "round", name: "测试圆管 Φ32", specification: "Φ32 × 2", diameter: 32, width: 32, depth: 32,
          contours: [{ kind: "circle", center: [0, 0], radius: 16 }, { kind: "circle", center: [0, 0], radius: 14 }] },
      }], tubeDesignerUserData: { profiles: [] }, tubeDesignerTemplateProfiles: [],
      tubeDesignerPunchWizard: wizard.createPunchWizardState(part) };
    const state = view.tubeDesignerPunchWizard;
    wizard.installPunchCatalogue(state, { tools: catalogue });
    const make = (id, rule) => {
      const feature = wizard.normalizePunchFeature({ recordKind: "tool", face: "top", station: 100, layoutDatum: "base", arrayCount: 5, arrayPitch: 100,
        headMargin: 100, tailMargin: 100, rowCount: 1, rowPitch: 20, ...rule });
      wizard.selectPunchTool(state, feature, id);
      return feature;
    };
    state.features = [make("circle", { distributionMode: "end-margins" }), make("rectangle", { distributionMode: "middle-fixed", arrayCount: 3, arrayPitch: 200 })];
    state.draft = make("circle", { distributionMode: "pitch", station: 500, arrayCount: 1 });
    state.selectedFeatureId = state.features[0].id;
    const mount = document.querySelector("#app");
    // Exercise the production action and focus/scroll bindings while deliberately
    // withholding the geometry host from viewport hydration. The real ViewCube
    // renderer is mounted with a deterministic camera below, not a native scene.
    const interactionMount = {
      querySelector(selector) { return selector === "[data-tube-designer-punch-viewport]" ? null : mount.querySelector(selector); },
      querySelectorAll(selector) { return mount.querySelectorAll(selector); },
      addEventListener(...args) { return mount.addEventListener(...args); },
      removeEventListener(...args) { return mount.removeEventListener(...args); },
    };
    const fileDialogs = [];
    const context = { mount, appProxy: { bridge: { async openFileDialog(options) { fileDialogs.push(options); return ""; } } } };
    const cubeView = { viewport: { getViewCubeState: () => ({ direction: { x: 1, y: -1, z: 1 } }) } };
    const fixture = window.fixture = { wizard, parts, editor, review, arrays, dom, view, part, context, fileDialogs, pending: false, failures: [], actions: [] };
    fixture.renderHtml=html=>{if(!dom.patchPunchDom(view,mount,html)){mount.innerHTML=html;dom.rememberPunchDom(view,mount);}};
    fixture.previewCalls = []; fixture.previewActive = 0; fixture.previewMaximumActive = 0; fixture.holdPreviews = false;
    context.sceneProxy = { invoke(method, payload, options) {
      if (method !== "TubeDesigner.PreviewPunchWizard") throw new Error("Unexpected backend call in isolated UI test: " + method);
      fixture.previewActive++;
      fixture.previewMaximumActive = Math.max(fixture.previewMaximumActive, fixture.previewActive);
      return new Promise(resolve => {
        const call = { method, payload, options, completed: false, resolve() {
          if (call.completed) return;
          call.completed = true; fixture.previewActive--;
          options.onReport?.({ message: "隔离界面测试：响应完成", completed: 2, total: 2 });
          resolve({baseGeometry:{url:"isolated-ui-base",version:1},toolGeometry:{url:"isolated-ui-tools-"+fixture.previewCalls.length,version:1},length:part.length,toolsOnly:true});
        } };
        fixture.previewCalls.push(call);
        options.onReport?.({ message: "隔离界面测试：等待计算响应", completed: 1, total: 2 });
        if (!fixture.holdPreviews) setTimeout(() => call.resolve(), 12);
      });
    } };
    const ops = fixture.ops = { renderProject() {
      cube.stopViewCubeAnimation(cubeView);
      fixture.renderHtml(parts.renderPunchWizardDialog(context, view) + designerViews.renderDesignerOperationOverlay(context, view));
      editor.attachPunchEditor(context, view, interactionMount, ops);
      cube.attachViewCube(cubeView, mount);
    } };
    fixture.render = () => ops.renderProject(context, view);
    const dispatch = async (action, target) => {
      if (!action?.startsWith("tube-designer-punch-")) return;
      fixture.pending = true;
      fixture.actions.push(action);
      const previousSceneProxy = context.sceneProxy;
      if (action === "tube-designer-punch-profile-parameter") {
        // A deterministic 2D section-service double satisfies the matched
        // parameter/snapshot contract without pretending to cut native geometry.
        context.sceneProxy = { async invoke(method, payload) {
          if (method !== "TubeDesigner.GenerateProfilePreview") throw new Error("Unexpected geometry call in isolated UI test: " + method);
          const diameter = Number(payload.parameters.diameter), wall = Number(payload.parameters.wallThickness);
          const profile = { ...structuredClone(view.tubeDesignerSystemProfiles[0].previewProfile), diameter, width: diameter, depth: diameter,
            contours: [{ kind: "circle", center: [0, 0], radius: diameter / 2 }, { kind: "circle", center: [0, 0], radius: diameter / 2 - wall }] };
          context.sceneProxy = previousSceneProxy;
          return { profile };
        } };
      }
      try { await parts.handlePartsAreaAction(context, view, action, target, ops); }
      catch (error) { fixture.failures.push(error.message); }
      finally { context.sceneProxy = previousSceneProxy; fixture.pending = false; }
    };
    document.addEventListener("change", event => void dispatch(event.target.dataset.camChangeAction, event.target));
    document.addEventListener("click", event => {
      const target = event.target.closest("[data-cam-action]");
      if (target) void dispatch(target.dataset.camAction, target);
    });
    fixture.render();
  }, tools);

  const idle = () => page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending && !window.fixture.view.tubeDesignerPunchWizard.uiPendingClick);
  const row = index => page.locator('[data-tube-designer-punch-row="' + index + '"]');
  const popup = page.locator("[data-punch-parameter-dialog]");
  const assertParameterCentered = async (size, mode) => {
    assert.equal(await popup.getAttribute("data-punch-editor-mode"), mode);
    assert.equal(await popup.getAttribute("aria-modal"), "false");
    const box = await popup.boundingBox();
    assert.ok(box, `${size.width} ${mode}: parameter dialog must have a visible DOM box`);
    assert.ok(box.x >= 0 && box.y >= 0 && box.x + box.width <= size.width + 1 && box.y + box.height <= size.height + 1,
      `${size.width} ${mode}: parameter dialog escapes the window: ${JSON.stringify(box)}`);
    assert.ok(Math.abs(box.x + box.width / 2 - size.width / 2) <= 1,
      `${size.width} ${mode}: parameter dialog must be horizontally centered: ${JSON.stringify(box)}`);
    assert.ok(Math.abs(box.y + box.height / 2 - size.height / 2) <= 1,
      `${size.width} ${mode}: parameter dialog must be vertically centered: ${JSON.stringify(box)}`);
    const footer = await popup.locator(':scope > footer').boundingBox();
    assert.ok(footer && footer.y >= box.y && footer.y + footer.height <= size.height + 1,
      `${size.width} ${mode}: parameter actions must remain inside the visible window`);
  };
  const group = index => popup.locator('.punch-array-group').nth(Number(index));
  const field = (name, index = "0") => group(index).locator('[data-tube-designer-punch-array-field="' + name + '"]');
  const reveal = async (name, index) => {
    if (await field(name, index).isVisible()) return;
    const detail = field(name,index).locator('xpath=ancestor::details[1]');
    if (await detail.count() && !(await detail.evaluate(element => element.open))) await detail.locator('summary').click();
  };
  const set = async (name, value, index = "0") => { await reveal(name,index); await field(name, index).fill(String(value)); await field(name, index).press("Tab"); await idle(); };
  const select = async (name, value, index = "0") => { await reveal(name,index); await field(name, index).selectOption(value); await idle(); };
  const summary = () => page.evaluate(() => window.fixture.review.buildPunchReviewSummary(window.fixture.view.tubeDesignerPunchWizard, window.fixture.part));
  const resolved = (index = 0) => page.evaluate(i => {
    const { arrays, view, part } = window.fixture;
    return arrays.resolvePunchArrayGroups(view.tubeDesignerPunchWizard.features[i], part.length);
  }, index);
  const assertNoMainParameters = async () => {
    assert.equal(await page.locator('.tube-designer-punch-sheet [data-tube-designer-punch-parameter]').count(), 0, "Detailed tool fields must stay out of the main table");
    assert.equal(await page.locator('.tube-designer-punch-sheet [data-profile-parameter-key]').count(), 0);
  };
  await assertNoMainParameters();
  assert.equal(await page.locator("[data-cam-viewcube]").count(), 1);
  assert.equal(await page.locator('[aria-label="零件复查摘要"]').count(), 1);
  assert.equal(await page.locator('.tube-designer-punch-sheet thead th').count(), 6);
  assert.equal(await page.locator('.tube-designer-punch-sheet [data-tube-designer-punch-end-row]').count(), 0);
  assert.equal(await page.locator('.tube-designer-punch-ends [data-tube-designer-punch-end-row]').count(), 2);
  assert.equal(await page.locator('.tube-designer-punch-ends input').count(), 0);
  assert.equal(await page.locator('.tube-designer-punch-sheet input:not([type="checkbox"]),.tube-designer-punch-sheet select').count(),0,"Main records contain summaries and independent editors, not combined parameter fields");
  assert.equal(await row(0).locator('[data-tube-designer-punch-editor-mode]').count(),3);
  assert.equal(await page.locator('[data-tube-designer-punch-field="allowOpen"]').count(), 0, "The obsolete open-end checkbox is not exposed");
  assert.equal((await summary()).positions, 8);
  const sceneHeader = page.locator(".tube-designer-punch-sheet-scene > header");
  assert.equal(await page.locator('[data-tube-designer-punch-preview-mode="result"]').count(),0,"Intermediate editing only places tool bodies; it does not offer a boolean cut-result mode");
  await row(0).getByRole("button",{name:"编辑阵列",exact:true}).click();await idle();
  assert.equal(await popup.getAttribute("data-punch-editor-mode"),"arrays");
  assert.equal(await field("distributionMode").locator("option").count(),9);
  assert.equal(await popup.locator('.punch-array-add button').count(),6,"All six independent translation/rotation directions are discoverable directly");
  for(const [name,type,axis] of [["＋ 沿 X","linear","X"],["＋ 绕 Y","polar","Y"],["＋ 绕 Z","polar","Z"]]) {
    await popup.getByRole("button",{name,exact:true}).click();await idle();
    assert.deepEqual(await page.evaluate(()=>{const g=window.fixture.view.tubeDesignerPunchWizard.features[0].arrayGroups.at(-1);return[g.type,g.axis];}),[type,axis]);
    await popup.locator('.punch-array-group').last().getByRole("button",{name:"删除此组",exact:true}).click();await idle();
  }
  const positions=async()=> (await resolved()).arrayGroupsSummary.groups[0].layoutSummary.positions;
  await select("distributionMode","pitch");assert.deepEqual(await positions(),[100,200,300,400,500]);
  await select("distributionMode","end-margins");assert.deepEqual(await positions(),[100,300,500,700,900]);
  await select("distributionMode","middle-fixed");assert.deepEqual(await positions(),[300,400,500,600,700]);
  await select("distributionMode","equal");assert.ok(Math.abs((await positions())[0]-1000/6)<1e-8);
  await select("distributionMode","fill");assert.equal((await positions()).length,9);
  await select("distributionMode","max-spacing");await set("maxSpacing",180);assert.deepEqual(await positions(),[100,260,420,580,740,900]);
  await select("distributionMode","positions");await set("positionList","-5\n100\n1010");
  assert.equal((await resolved()).arrayGroupsError,"");assert.deepEqual(await positions(),[-5,100,1010]);
  await set("positionList","100\n250\n750");assert.deepEqual(await positions(),[100,250,750]);
  await select("distributionMode","sequence");await set("spacingSequence","100, 150, 50*3");assert.deepEqual(await positions(),[100,200,350,400,450,500]);
  await select("distributionMode","center-out");await set("count",7);
  assert.equal(await page.evaluate(()=>document.activeElement?.dataset?.tubeDesignerPunchArrayField),"spacing","Tab continues between independent array-group fields");
  await set("count",5);await set("spacing",100);assert.deepEqual(await positions(),[300,400,500,600,700]);
  await select("centerMode","gap");assert.match((await resolved()).arrayGroupsError,/偶数/);assert.ok(await popup.getByRole("alert").count());
  await set("count",6);await set("centerFirstOffset","");assert.deepEqual(await positions(),[250,350,450,550,650,750]);
  await popup.getByRole("button",{name:"＋ 沿 Y",exact:true}).click();await idle();
  await set("count",2,"1");await set("spacing",25,"1");
  await popup.getByRole("button",{name:"＋ 沿 Z",exact:true}).click();await idle();
  await set("count",2,"2");await set("spacing",30,"2");
  await popup.getByRole("button",{name:"＋ 绕 X",exact:true}).click();await idle();
  await set("count",4,"3");await select("angleMode","full-circle","3");await set("startAngle",30,"3");
  assert.deepEqual((await resolved()).arrayGroupsSummary.groups.map(g=>[g.type,g.axis,g.instanceCount]),[["linear","X",6],["linear","Y",2],["linear","Z",2],["polar","X",4]]);
  assert.equal((await resolved()).arrayGroupsSummary.instanceCount,96,"Length, Y, Z and polar groups coexist and combine, rather than replacing one another");
  await popup.getByRole("button",{name:"确定参数",exact:true}).click();await idle();
  assert.equal(await popup.count(),0);assert.match(await row(0).textContent(),/沿主管长度.*沿 Y 直线.*沿 Z 直线.*绕 X 圆周/s);
  await row(0).getByRole("button",{name:"编辑阵列",exact:true}).click();await idle();
  assert.equal(await popup.locator('.punch-array-group').count(),4,"All independent groups survive confirm and reopening");
  await set("spacing",77,"1");await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
  assert.equal((await resolved()).arrayGroups[1].spacing,25,"Cancelling an array edit restores all groups in one transaction");
  await row(0).getByRole("button",{name:"编辑阵列",exact:true}).click();await idle();
  await group(2).getByRole("button",{name:"删除此组",exact:true}).click();await idle();
  await group(1).getByRole("button",{name:"删除此组",exact:true}).click();await idle();
  const skipDetails=popup.locator('.punch-array-skips');await skipDetails.locator('summary').click();
  const skipInput=popup.locator('[data-tube-designer-punch-array-field="arraySkips"]');await skipInput.fill("3:1, 3:2");await skipInput.press("Tab");await idle();
  assert.equal(await skipDetails.evaluate(element=>element.open),true);assert.equal((await resolved()).arrayGroupsSummary.instanceCount,22);
  await page.screenshot({path:resolve(artifactDir,"punch-array-groups.png")});
  await popup.getByRole("button",{name:"确定参数",exact:true}).click();await idle();

  // The real table action opens a single transaction, not inline detail fields.
  await row(0).getByRole("button", { name: "编辑形状", exact: true }).click();
  await idle();
  assert.equal(await popup.count(), 1);
  assert.equal(await page.locator(".tube-designer-punch-sheet-dialog").getAttribute("inert"), null, "Parameter editing must not disable the whole scene");
  assert.equal(await popup.getAttribute("aria-modal"), "false");
  assert.equal(await page.locator('.tube-designer-punch-parameter-backdrop').evaluate(element => getComputedStyle(element).pointerEvents), "none");
  assert.equal(await popup.evaluate(element => getComputedStyle(element).pointerEvents), "auto");
  for (const selector of ['.tube-designer-punch-sheet-dialog > header', '.tube-designer-nesting-punch-setup', '.tube-designer-punch-records', '.tube-designer-punch-review', '.tube-designer-punch-sheet-dialog > footer', '.tube-designer-punch-sheet-scene > header > nav']) {
    assert.equal(await page.locator(selector).evaluate(element => element.inert), true, "Other transaction edits stay locked: " + selector);
  }
  assert.equal(await page.locator('[data-tube-designer-punch-viewport]').evaluate(element => !!element.closest('[inert]')), false, "Viewport and its ViewCube/projection controls remain available");
  assert.equal(await page.evaluate(() => {
    const host = document.querySelector('[data-tube-designer-punch-viewport]'), box = host.getBoundingClientRect();
    return !!document.elementFromPoint(box.x + 8, box.y + box.height / 2)?.closest('[data-tube-designer-punch-viewport]');
  }), true, "The transparent parameter backdrop must pass pointer hits through to uncovered scene space");
  assert.equal(await popup.locator('[data-punch-parameter-drag]').count(), 1);
  assert.equal(await page.evaluate(() => !!document.activeElement?.closest("[data-punch-parameter-dialog]")), true);
  const diameter = () => popup.locator('[data-tube-designer-punch-parameter="diameter"]');
  await diameter().fill("24");
  await popup.locator('[data-cam-action$="parameters-preview"]').click(); await idle();
  assert.equal(await popup.count(), 1, "Updating tool bodies does not close the parameter transaction");
  assert.equal(await popup.locator('[data-tube-designer-punch-preview-mode="result"]').count(),0);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.parameterEditor.snapshot.features[0].toolParameters.diameter), 10);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 24);
  await page.screenshot({ path: resolve(artifactDir, "punch-tool-body-parameters.png") });
  await popup.getByRole("button", { name: "取消", exact: true }).click(); await idle();
  assert.equal(await popup.count(), 0);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 10);
  assert.deepEqual(await page.evaluate(() => [document.activeElement?.dataset?.camAction, document.activeElement?.dataset?.tubeDesignerPunchIndex]), ["tube-designer-punch-parameters-open", "0"], "Cancel returns focus to the source row's parameter button");
  await row(0).locator("[data-punch-edit-name]").dblclick(); await idle();
  assert.equal(await popup.count(), 1, "Double-clicking the source name must open parameters");
  await diameter().fill("18"); await diameter().press("Tab"); await idle();
  await popup.getByRole("button", { name: "确定参数", exact: true }).click(); await idle();
  assert.equal(await popup.count(), 0);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 18);
  await assertNoMainParameters();
  await row(0).getByRole("button", { name: "编辑形状", exact: true }).click(); await idle();
  const beforeDirtyCancelCalls = await page.evaluate(() => window.fixture.previewCalls.length);
  await diameter().fill("27");
  await popup.getByRole("button", { name: "取消", exact: true }).click(); await idle();
  assert.equal(await popup.count(), 0, "A single Cancel click must work while a parameter input is still dirty");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 18);
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeDirtyCancelCalls, "Cancelling unsubmitted text must not launch a discarded geometry calculation");
  await row(0).getByRole("button", { name: "编辑形状", exact: true }).click(); await idle();
  const beforeDirtyApplyCalls = await page.evaluate(() => window.fixture.previewCalls.length);
  await diameter().fill("19");
  await popup.getByRole("button", { name: "确定参数", exact: true }).click(); await idle();
  assert.equal(await popup.count(), 0, "A single Apply click must commit the still-focused input");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 19);
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeDirtyApplyCalls + 1, "Dirty Apply waits for one response and reuses it instead of computing twice");

  // Tool selection inside the shared editor must retain the same transaction.
  await row(0).getByRole("button", { name: "编辑形状", exact: true }).click(); await idle();
  const beforeDirtyPreviewCalls = await page.evaluate(() => window.fixture.previewCalls.length);
  await diameter().fill("20");
  await popup.locator('[data-cam-action$="parameters-preview"]').click(); await idle();
  assert.equal(await popup.count(), 1, "A dirty-input Preview click preserves the parameter transaction");
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeDirtyPreviewCalls + 1, "Explicit Preview reuses the geometry just computed by committing the dirty input");
  const beforeBlockedSelectCalls = await page.evaluate(() => window.fixture.previewCalls.length);
  await diameter().fill("21");
  await popup.locator('[data-tube-designer-punch-field="tool"]').selectOption("rectangle"); await idle();
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolRef.id), "circle", "An ordinary select event during the preceding dirty input's busy stage is rejected, not queued");
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeBlockedSelectCalls + 1, "The rejected concurrent selection does not start a second native request");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 21);
  await popup.locator('[data-tube-designer-punch-field="tool"]').selectOption("rectangle"); await idle();
  assert.equal(await popup.count(), 1);
  assert.equal(await popup.locator('[data-tube-designer-punch-parameter="spanAlong"]').count(), 1);
  await popup.getByRole("button", { name: "取消", exact: true }).click(); await idle();
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolRef.id), "circle");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), 19);
  await row(1).getByRole("button", { name: "编辑形状", exact: true }).click(); await idle();
  await popup.locator('[data-tube-designer-punch-parameter="spanAlong"]').fill("44");
  await popup.locator('[data-tube-designer-punch-parameter="spanAcross"]').click(); await idle();
  assert.equal(await page.evaluate(() => document.activeElement?.dataset?.tubeDesignerPunchParameter), "spanAcross", "Clicking another field survives the previous field's change render");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[1].toolParameters.spanAlong), 44);
  await popup.getByRole("button", { name: "取消", exact: true }).click(); await idle();
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[1].toolParameters.spanAlong), 30);
  const arraysBeforePose=await page.evaluate(()=>structuredClone(window.fixture.view.tubeDesignerPunchWizard.features[0].arrayGroups));
  await row(0).getByRole("button",{name:"编辑位置 / 姿态",exact:true}).click();await idle();
  assert.equal(await popup.getAttribute("data-punch-editor-mode"),"pose");
  assert.equal(await popup.locator('[data-tube-designer-punch-parameter="diameter"],[data-tube-designer-punch-array-field]').count(),0);
  await popup.locator('[data-tube-designer-punch-field="station"]').fill("150");
  await popup.getByRole("button",{name:"确定参数",exact:true}).click();await idle();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0].station),150);
  assert.deepEqual(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0].arrayGroups),arraysBeforePose,"Moving the seed tool must not rewrite any independent array group");
  await row(0).getByRole("button",{name:"编辑位置 / 姿态",exact:true}).click();await idle();
  const callsBeforePoseCancel=await page.evaluate(()=>window.fixture.previewCalls.length);
  await popup.locator('[data-tube-designer-punch-field="offset"]').fill("8");
  await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0].offset),0);
  assert.equal(await page.evaluate(()=>window.fixture.previewCalls.length),callsBeforePoseCancel,"Cancelling dirty pose text neither submits it nor schedules a discarded tool request");
  // Selecting an end cutter is an automatic parameter-editor entry point.
  await page.locator('[data-tube-designer-punch-end="start"][data-tube-designer-punch-field="tool"]').selectOption("end-convex"); await idle();
  assert.equal(await popup.count(), 1);
  assert.match(await popup.textContent(), /左端面参数/);
  await popup.getByRole("button", { name: "取消", exact: true }).click(); await idle();
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.ends.start.type), "keep");

  // End operations share the same source selection and transaction as side tools.
  const endRow = end => page.locator('[data-tube-designer-punch-end-row="' + end + '"]');
  const endState = end => page.evaluate(key => structuredClone(window.fixture.view.tubeDesignerPunchWizard.ends[key]), end);
  const beforeEndChanges = await page.evaluate(() => structuredClone(window.fixture.view.tubeDesignerPunchWizard.features));
  const originalStart = await endState("start");
  await endRow("start").locator('[data-cam-change-action="tube-designer-punch-record-kind-change"]').selectOption("branch"); await idle();
  assert.equal(await popup.count(), 1);
  assert.equal((await endState("start")).toolRef.id, "end-profile");
  assert.equal((await endState("start")).section.source, "library");
  const branchDiameter = popup.locator('[data-tube-designer-punch-profile-parameter="diameter"][data-tube-designer-punch-end="start"]');
  await branchDiameter.fill("36"); await branchDiameter.press("Tab"); await idle();
  assert.equal((await endState("start")).section.parameters.diameter, 36);
  await popup.locator('[data-tube-designer-punch-parameter="angle"]').fill("60");
  await page.screenshot({ path: resolve(artifactDir, "punch-end-branch-parameters.png") });
  await popup.getByRole("button", { name: "取消", exact: true }).click(); await idle();
  assert.deepEqual(await endState("start"), originalStart);
  assert.deepEqual(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features), beforeEndChanges);

  await endRow("end").locator('[data-tube-designer-punch-field="tool"]').selectOption("end-step-z"); await idle();
  assert.equal(await popup.count(), 1);
  await popup.locator('[data-tube-designer-punch-parameter="depth"]').fill("24");
  await popup.locator('[data-tube-designer-punch-parameter="depth"]').press("Tab"); await idle();
  await popup.locator('[data-tube-designer-punch-parameter="hand"]').selectOption("negative"); await idle();
  assert.equal(await popup.locator('[data-tube-designer-punch-field="datum"] option').count(), 1, "Z joints expose only their supported end datum, inside the parameter editor");
  await popup.locator('[data-tube-designer-punch-field="trim"]').fill("12");
  await popup.locator('[data-tube-designer-punch-field="trim"]').press("Tab"); await idle();
  await popup.locator('[data-tube-designer-punch-field="rotation"]').fill("15");
  await popup.getByRole("button", { name: "确定参数", exact: true }).click(); await idle();
  assert.equal((await endState("end")).toolParameters.depth, 24);
  assert.equal((await endState("end")).toolParameters.hand, "negative");
  assert.equal((await endState("end")).trim, 12);assert.equal((await endState("end")).rotation, 15);
  assert.match(await endRow("end").locator('.tube-designer-punch-end-summary').textContent(), /修剪 12 mm.*旋转 15°/);
  assert.equal(await endRow("end").locator('input').count(), 0);
  await endRow("end").locator('[data-tube-designer-punch-field="tool"]').selectOption("end-key-joint"); await idle();
  assert.equal(await popup.locator('[data-tube-designer-punch-parameter="sideClearance"]').count(), 0);
  await popup.locator('[data-tube-designer-punch-parameter="gender"]').selectOption("female"); await idle();
  assert.equal(await popup.locator('[data-tube-designer-punch-parameter="sideClearance"]').count(), 1);
  await popup.locator('[data-tube-designer-punch-parameter="width"]').fill("20");
  await popup.locator('[data-tube-designer-punch-parameter="sideClearance"]').click(); await idle();
  await popup.locator('[data-tube-designer-punch-parameter="sideClearance"]').fill("0.2");
  assert.equal(await popup.locator('[data-tube-designer-punch-field="datum"] option').count(), 1);
  await popup.getByRole("button", { name: "确定参数", exact: true }).click(); await idle();
  assert.equal((await endState("end")).toolParameters.gender, "female");
  assert.equal((await endState("end")).toolParameters.width, 20);
  assert.equal((await endState("end")).toolParameters.sideClearance, 0.2);
  const beforeDxfCancel = await endState("end");
  await endRow("end").locator('[data-cam-change-action="tube-designer-punch-record-kind-change"]').selectOption("dxf"); await idle();
  assert.equal(await popup.count(), 0, "Cancelling the DXF picker closes the uncommitted source transaction");
  assert.deepEqual(await endState("end"), beforeDxfCancel);
  assert.deepEqual(await endState("start"), originalStart);
  assert.deepEqual(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features), beforeEndChanges);
  assert.deepEqual(await page.evaluate(() => window.fixture.fileDialogs.map(dialog => dialog.filters[0].extensions)), [["dxf"]]);
  await assertNoMainParameters();

  // The actual branch/DXF source handlers and descriptor-generated parameters
  // expose concave/convex end cuts independently on the left and right ends.
  const endModeBaseline = await page.evaluate(() => structuredClone(window.fixture.view.tubeDesignerPunchWizard.ends));
  await page.evaluate(() => {
    const f=window.fixture;
    f.savedEndModeFileDialog=f.context.appProxy.bridge.openFileDialog;
    f.savedEndModeProductProxy=f.context.productProxy;
    f.context.appProxy.bridge.openFileDialog=async()=>"C:\\isolated-fixtures\\end-mode.dxf";
    f.context.productProxy={async invoke(method){
      if(method!=="TubeDesigner.ImportProfileDxf")throw new Error("Unexpected end-mode import request: "+method);
      return {profile:{name:"凹凸切端 DXF",kind:"rect",width:24,depth:18,contours:[{kind:"polygon",points:[[-12,-9],[12,-9],[12,9],[-12,9]]}]}};
    }};
  });
  for(const source of ["branch","dxf"])for(const key of ["start","end"]){
    await endRow(key).locator('[data-cam-change-action="tube-designer-punch-record-kind-change"]').selectOption(source);await idle();
    assert.equal(await popup.getAttribute("data-punch-editor-mode"),"end");
    assert.equal(await popup.getAttribute("aria-modal"),"false");
    const cutMode=popup.locator('[data-tube-designer-punch-parameter="cutMode"]');
    assert.equal(await cutMode.inputValue(),"concave");
    assert.deepEqual(await cutMode.locator('option').allTextContents(),["凹口","凸口"]);
    assert.equal(await popup.locator('[data-tube-designer-punch-parameter="cutRegion"]').count(),0);
    await cutMode.selectOption("convex");await idle();
    assert.match(await popup.textContent(),/0° 或 180°.*不能形成凸口/);
    const angle=popup.locator('[data-tube-designer-punch-parameter="angle"]');
    await angle.fill("60");await angle.press("Tab");await idle();
    await page.screenshot({path:resolve(artifactDir,"punch-end-profile-"+source+"-"+key+"-convex.png")});
    await popup.getByRole("button",{name:"确定参数",exact:true}).click();await idle();
    assert.equal((await endState(key)).toolParameters.cutMode,"convex");
    assert.equal((await endState(key)).toolParameters.angle,60,"Convex end cuts retain the independent inclined tool angle");
    assert.match(await endRow(key).locator('.tube-designer-punch-end-summary').textContent(),/凸口/);
    assert.match((await summary()).ends.find(item=>item.key===key).label,/凸口/);
    const savedModeEnd=await endState(key);
    await endRow(key).getByRole("button",{name:"编辑端面参数",exact:true}).click();await idle();
    await cutMode.selectOption("concave");await idle();
    await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
    assert.deepEqual(await endState(key),savedModeEnd,"Cancel restores the selected end's source, angle and convex mode together");
    assert.equal(await page.locator('.tube-designer-punch-records').evaluate(element=>element.inert),false);
    await page.evaluate(()=>{
      const f=window.fixture,previous=f.view.tubeDesignerPunchWizard;
      const nativeRecipe=JSON.parse(JSON.stringify({...f.wizard.getPunchWizardPayload(f.view),baseLength:previous.baseLength}));
      for(const item of Object.values(nativeRecipe.ends))delete item.recordKind;
      f.nativeEndReopenParent=previous;
      f.view.tubeDesignerPunchWizard=f.wizard.createPunchWizardState({...f.part,properties:{...f.part.properties,"tubeDesigner.punchWizard":nativeRecipe}});
      f.wizard.installPunchCatalogue(f.view.tubeDesignerPunchWizard,{tools:previous.tools});
      f.render();
    });
    assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard!==window.fixture.nativeEndReopenParent),true,"A native-style saved recipe must be read into a fresh state, not reopened on its original transaction");
    assert.equal(await endRow(key).locator('[data-cam-change-action="tube-designer-punch-record-kind-change"]').inputValue(),source);
    await endRow(key).getByRole("button",{name:"编辑端面参数",exact:true}).click();await idle();
    assert.match(await popup.textContent(),/选择截面/);assert.doesNotMatch(await popup.textContent(),/选择刀具|保留原端面/);
    const sourceSelect=popup.locator('[data-cam-change-action="tube-designer-punch-profile-select"]');
    assert.equal(await sourceSelect.inputValue(),source==="branch"?savedModeEnd.section.key:"__dxf__");
    assert.match(await sourceSelect.locator('option:checked').textContent(),source==="branch"?/测试圆管/:/凹凸切端 DXF/);
    assert.equal(await cutMode.inputValue(),"convex");
    const axial=popup.locator('[data-tube-designer-punch-parameter="axialOffset"]');
    if(source==="branch"){
      const nativeDiameter=popup.locator('[data-tube-designer-punch-profile-parameter="diameter"]');
      assert.equal(await nativeDiameter.inputValue(),String(savedModeEnd.section.parameters.diameter));
      await nativeDiameter.fill("42");await nativeDiameter.press("Tab");await idle();
      assert.equal((await endState(key)).section.parameters.diameter,42);
    }else{
      assert.equal(await popup.locator('[data-tube-designer-punch-profile-parameter]').count(),0,"A local DXF keeps its imported contour, not invented parametric dimensions");
      assert.deepEqual((await endState(key)).section.profile.contours,savedModeEnd.section.profile.contours);
    }
    await axial.fill("3");await axial.press("Tab");await idle();
    await cutMode.selectOption("concave");await idle();
    await popup.getByRole("button",{name:"确定参数",exact:true}).click();await idle();
    const nativeCommitted=await endState(key);
    assert.equal(nativeCommitted.section.source,source==="branch"?"library":"dxf");
    assert.equal(nativeCommitted.section.key,savedModeEnd.section.key);
    assert.equal(nativeCommitted.toolParameters.cutMode,"concave");assert.equal(nativeCommitted.toolParameters.axialOffset,3);
    await endRow(key).getByRole("button",{name:"编辑端面参数",exact:true}).click();await idle();
    await cutMode.selectOption("convex");await idle();
    await axial.fill("9");await axial.press("Tab");await idle();
    await page.screenshot({path:resolve(artifactDir,"punch-end-profile-native-reopened-"+source+"-"+key+".png")});
    await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
    assert.deepEqual(await endState(key),nativeCommitted,"Confirm/cancel on the reloaded form never changes the inferred processing source");
    await page.evaluate(()=>{const f=window.fixture;f.view.tubeDesignerPunchWizard=f.nativeEndReopenParent;delete f.nativeEndReopenParent;f.render();});
    await page.evaluate(end=>{const f=window.fixture;delete f.view.tubeDesignerPunchWizard.ends[end].toolParameters.cutMode;f.render();},key);
    await endRow(key).getByRole("button",{name:"编辑端面参数",exact:true}).click();await idle();
    assert.equal(await cutMode.inputValue(),"concave","A legacy saved end without cutMode defaults to concave in the real form");
    await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
    assert.equal((await endState(key)).toolParameters.cutMode,undefined,"Opening and cancelling does not silently migrate legacy data");
  }
  await page.evaluate(ends=>{
    const f=window.fixture,s=f.view.tubeDesignerPunchWizard;
    s.ends=ends;s.revision++;
    f.context.appProxy.bridge.openFileDialog=f.savedEndModeFileDialog;
    f.context.productProxy=f.savedEndModeProductProxy;
    delete f.savedEndModeFileDialog;delete f.savedEndModeProductProxy;
    f.render();
  },endModeBaseline);

  assert.equal((await resolved()).arrayGroups[1].count, 4);
  assert.equal((await resolved()).arrayGroupsSummary.skippedCount, 2);

  // Tool-shape conditional fields remain template-driven after separating pose.
  await row(1).getByRole("button",{name:"编辑形状",exact:true}).click();await idle();
  await popup.locator('[data-tube-designer-punch-field="tool"]').selectOption("v-notch");await idle();
  const style=popup.locator('[data-tube-designer-punch-parameter="style"]');
  assert.equal(await style.locator('option').count(),7);
  const parameters=()=>popup.locator('[data-tube-designer-punch-parameter]');
  for(const [value,required,absent] of [
    ["sharp_v",["angle","rootRadius"],["leftAngle","curveRadius","flatWidth","holeDiameter"]],
    ["asymmetric_v",["leftAngle","rightAngle"],["angle","curveRadius","flatWidth","holeDiameter"]],
    ["rounded_v",["angle","curveRadius"],["leftAngle","flatWidth","holeDiameter"]],
    ["left_arc",["angle","curveRadius"],["leftAngle","flatWidth","holeDiameter"]],
    ["right_arc",["angle","curveRadius"],["leftAngle","flatWidth","holeDiameter"]],
    ["flat_v",["angle","flatWidth"],["leftAngle","curveRadius","holeDiameter"]],
    ["relief_v",["angle","holeDiameter","holeLift"],["leftAngle","curveRadius","flatWidth","reliefDiameter"]],
  ]) {
    await style.selectOption(value);await idle();
    for(const key of required)assert.equal(await popup.locator('[data-tube-designer-punch-parameter="'+key+'"]').count(),1,value+": "+key);
    for(const key of absent)assert.equal(await popup.locator('[data-tube-designer-punch-parameter="'+key+'"]').count(),0,value+": hidden "+key);
    assert.equal(await popup.locator('[data-tube-designer-punch-parameter="rotation"]').count(),0,"Whole-tool rotation belongs to pose, not V angle geometry");
  }
  await popup.locator('[data-tube-designer-punch-parameter="bottomCut"]').check();await idle();
  assert.equal(await popup.locator('[data-tube-designer-punch-parameter="bottomCutWidth"]').count(),1);
  await page.screenshot({path:resolve(artifactDir,"punch-v-notch-styles.png")});
  await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[1].toolRef.id),"rectangle");

  const reviewButton = page.locator('.tube-designer-punch-review [data-cam-action="tube-designer-punch-summary-select"][data-tube-designer-punch-index="1"]');
  await reviewButton.click(); await idle();
  assert.equal(await row(1).evaluate(element => element.classList.contains("is-selected")), true);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.selectedFeatureId === window.fixture.view.tubeDesignerPunchWizard.features[1].id), true);
  await page.evaluate(() => {
    const scroller = document.querySelector(".tube-designer-punch-sheet-scroll");
    if (scroller) scroller.scrollLeft = 0;
  });

  for (const size of [{ width: 1600, height: 1000 }, { width: 1280, height: 800 }, { width: 1024, height: 768 }]) {
    await page.setViewportSize(size);
    await page.evaluate(() => { const scroller = document.querySelector('.tube-designer-punch-sheet-scroll'); scroller.scrollLeft = 0; scroller.scrollTop = 0; });
    await page.screenshot({ path: resolve(artifactDir, "punch-layout-" + size.width + ".png") });
    const boxes = await page.evaluate(() => {
      const box = element => { const r = element.getBoundingClientRect(); return { x: r.x, y: r.y, right: r.right, bottom: r.bottom, width: r.width, height: r.height }; };
      return { dialog: box(document.querySelector(".tube-designer-punch-sheet-dialog")), footer: box(document.querySelector(".tube-designer-punch-footer")),
        ends: box(document.querySelector('.tube-designer-punch-ends')), records: box(document.querySelector('.tube-designer-punch-records')),
        review: box(document.querySelector(".tube-designer-punch-review")),
        sceneFooter: box(document.querySelector(".tube-designer-punch-sheet-scene > footer")), sheet: box(document.querySelector(".tube-designer-punch-sheet")),
        sceneHost: box(document.querySelector(".tube-designer-punch-preview-host")),
        sceneHeader: box(document.querySelector(".tube-designer-punch-sheet-scene > header")),
        sceneControls: box(document.querySelector(".tube-designer-punch-sheet-scene > header > nav")),
        rootOverflow: document.documentElement.scrollWidth > innerWidth + 1 || document.documentElement.scrollHeight > innerHeight + 1 };
    });
    for (const [name, box] of Object.entries(boxes).filter(([, value]) => typeof value === "object")) {
      assert.ok(box.x >= -1 && box.y >= -1 && box.right <= size.width + 1 && box.bottom <= size.height + 1, `${size.width}: ${name} escapes viewport: ${JSON.stringify(box)}`);
    }
    assert.equal(boxes.rootOverflow, false, `${size.width}: window must not scroll around the modal`);
    assert.ok(boxes.sceneFooter.bottom <= boxes.sheet.y + 1, `${size.width}: scene legend/status is covered by the record table`);
    assert.ok(boxes.sceneFooter.bottom <= boxes.ends.y + 1 && boxes.ends.bottom <= boxes.sheet.y, `${size.width}: separate end and hole areas must not overlap`);
    assert.ok(boxes.sheet.height >= 140, `${size.width}: the hole records must retain a usable scroll area`);
    const arrayCell = await row(0).locator('[data-punch-array-summary]').boundingBox();
    assert.ok(arrayCell && arrayCell.x + arrayCell.width <= boxes.sheet.right + 1, `${size.width}: every array summary and editor must remain visible without horizontal scrolling`);
    assert.ok(boxes.sceneHost.bottom <= boxes.sceneFooter.y + 1, `${size.width}: scene viewport covers its legend/status: ${JSON.stringify(boxes)}`);
    assert.ok(boxes.sceneHeader.bottom <= boxes.sceneHost.y + 1, `${size.width}: wrapped scene controls cover the viewport`);
    assert.ok(boxes.sceneControls.x >= boxes.sceneHeader.x - 1 && boxes.sceneControls.right <= boxes.sceneHeader.right + 1, `${size.width}: display controls exceed the scene header`);
    assert.ok(boxes.review.height > 30 && boxes.review.width > 100, "Read-only review must remain reachable");
    assert.equal(await page.locator('[data-cam-action="tube-designer-punch-apply"]').isVisible(), true);
    await row(1).getByRole("button", { name: "编辑形状", exact: true }).click(); await idle();
    await assertParameterCentered(size, "shape");
    const editable = popup.locator('[data-tube-designer-punch-parameter="spanAlong"]');
    await editable.focus();
    assert.equal(await editable.evaluate(element => element === document.activeElement), true);
    await editable.fill("42"); await editable.press("Tab"); await idle();
    assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[1].toolParameters.spanAlong), 42);
    assert.equal(await page.evaluate(() => document.activeElement?.dataset?.tubeDesignerPunchParameter), "spanAcross", "Tab commits a parameter and continues to the next field after the render");
    await assertParameterCentered(size, "shape");
    await page.screenshot({ path: resolve(artifactDir, "punch-parameters-" + size.width + ".png") });
    await popup.getByRole("button", { name: "确定参数", exact: true }).focus();
    await page.keyboard.press("Tab");
    assert.equal(await page.evaluate(() => !!document.activeElement?.closest("[data-punch-parameter-dialog]")), false, "The non-modal parameter window must not trap Tab navigation");
    await editable.focus();
    await page.keyboard.press("Escape"); await idle();
    assert.equal(await popup.count(), 0, "Escape cancels the parameter transaction");
    assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[1].toolParameters.spanAlong), 30);
    await row(0).getByRole("button",{name:"编辑位置 / 姿态",exact:true}).click();await idle();
    await assertParameterCentered(size, "pose");
    await page.screenshot({path:resolve(artifactDir,"punch-pose-centered-"+size.width+".png")});
    await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
    await row(0).getByRole("button",{name:"编辑阵列",exact:true}).click();await idle();
    await assertParameterCentered(size, "arrays");
    const arrayFooterBox=await popup.locator('footer').last().boundingBox();
    assert.ok(arrayFooterBox&&arrayFooterBox.y+arrayFooterBox.height<=size.height+1,"Array editor footer remains reachable even with many controls");
    await page.screenshot({path:resolve(artifactDir,"punch-arrays-"+size.width+".png")});
    await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
    assert.equal(await page.evaluate(()=>document.activeElement?.dataset?.tubeDesignerPunchEditorMode),"arrays","Closing an array transaction restores its own row button, not the shape editor");
    const unchangedStart = await endState("start");
    await endRow("start").locator('[data-tube-designer-punch-field="tool"]').selectOption("end-convex");await idle();
    await assertParameterCentered(size, "end");
    assert.match(await popup.textContent(), /左端面参数/);
    assert.match(await popup.locator('[data-tube-designer-punch-field="tool"] option:checked').textContent(), /圆柱凸头/);
    assert.equal(await popup.locator('[data-tube-designer-punch-parameter="diameter"]').isVisible(), true);
    await page.screenshot({path:resolve(artifactDir,"punch-left-end-convex-centered-"+size.width+".png")});
    await popup.getByRole("button",{name:"取消",exact:true}).click();await idle();
    assert.deepEqual(await endState("start"), unchangedStart, "The centered left-end dialog still cancels its source transaction without changing the end");
  }
  await row(0).getByRole("button",{name:"编辑阵列",exact:true}).click();await idle();
  const beforeBusyCalls = await page.evaluate(() => {
    const f = window.fixture, count = f.previewCalls.length;
    f.holdPreviews = true;
    f.wizard.updatePunchWizardField(f.view, { value: "101", dataset: { tubeDesignerPunchField: "station", tubeDesignerPunchIndex: "0" } });
    f.heldPreviewPromise = f.editor.previewPunch(f.context, f.view, f.part, f.ops, null, { quiet: true });
    return count;
  });
  await page.waitForFunction(count => window.fixture.previewCalls.length === count + 1, beforeBusyCalls);
  assert.equal(await page.locator('[data-tube-designer-operation-wait]').isVisible(), true);
  assert.equal(await page.locator('[data-tube-designer-operation-wait] [role="progressbar"]').getAttribute("aria-valuenow"), "1");
  assert.equal(await page.locator('[data-tube-designer-operation-wait] [role="progressbar"] > i').evaluate(element => getComputedStyle(element).backgroundColor), "rgb(23, 143, 130)", "Visible determinate progress inherits the product accent colour");
  assert.equal(await field("distributionMode").isDisabled(), true);
  assert.equal(await page.locator('[data-cam-action="tube-designer-punch-apply"]').isDisabled(), true);
  assert.deepEqual(await page.evaluate(async () => {
    const f = window.fixture;
    const repeated = await f.editor.previewPunch(f.context, f.view, f.part, f.ops, null, { quiet: true });
    const changed = f.wizard.updatePunchWizardField(f.view, { value: "999", dataset: { tubeDesignerPunchField: "centerOffset", tubeDesignerPunchIndex: "0" } });
    await f.parts.handlePartsAreaAction(f.context, f.view, "tube-designer-punch-preview", {}, f.ops);
    return { repeated, changed, calls: f.previewCalls.length, pending: f.view.pending };
  }), { repeated: false, changed: false, calls: beforeBusyCalls + 1, pending: true });
  await page.screenshot({ path: resolve(artifactDir, "punch-preview-progress.png") });
  await page.evaluate(async () => { const f = window.fixture; f.previewCalls.at(-1).resolve(); await f.heldPreviewPromise; });
  await idle();
  assert.equal(await page.locator('[data-tube-designer-operation-wait]').count(), 0);
  assert.equal(await field("distributionMode").isDisabled(), false);
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeBusyCalls + 1, "Rejected busy actions are not queued for replay");
  await field("spacing").fill("90"); await field("spacing").press("Tab");
  await page.waitForFunction(count => window.fixture.previewCalls.length === count + 2, beforeBusyCalls);
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.at(-1).payload.features[0].arrayGroups[0].spacing), 90, "A fresh normal edit is accepted after the previous response");
  await page.evaluate(() => window.fixture.previewCalls.at(-1).resolve()); await idle();
  assert.equal(await page.evaluate(() => window.fixture.previewMaximumActive), 1);
  assert.equal(await page.evaluate(() => window.fixture.previewActive), 0);
  await popup.getByRole("button",{name:"确定参数",exact:true}).click();await idle();
  const { verifyPunchButtonIntents, verifyPunchCreationButtonIntents, verifyPunchHydration } = await import("./TubeDesignerPunchIntegrationDiagnostic.mjs");
  await verifyPunchButtonIntents(page);
  await verifyPunchCreationButtonIntents(page);
  await verifyPunchHydration(page, artifactDir);
  assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
  assert.deepEqual(errors, []);
  assert.deepEqual(externalRequests, []);
  console.log("Punch layout: nine modes, sequences, symmetric/circumferential layouts, skips, parameter transactions, review selection and three responsive sizes passed; shape/pose/arrays/left-end dialogs are centered and contained at all three sizes.");
  console.log("Screenshots: " + artifactDir);
} catch (error) {
  await browser.contexts()[0]?.pages()[0]?.screenshot({path:resolve(artifactDir,"punch-browser-failure.png")}).catch(()=>{});
  throw error;
} finally { await browser.close(); }
