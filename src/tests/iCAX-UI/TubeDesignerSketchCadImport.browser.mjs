// Real Chrome production dialog/DOM coverage. File chooser and native transport
// are controlled here; the native companion verifies real DWG/DXF conversion.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const repository = fileURLToPath(new URL("../../../", import.meta.url));
const sourceRoot = resolve(repository, "src");
const output = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repository, "output/tests/sketch-cad-import-browser"));
mkdirSync(output, { recursive: true });
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome" });
const assets = new Map(), cases = [], errors = [];
let page;
try {
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.setDefaultTimeout(15000);
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://sketch-cad-import.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><meta charset='utf-8'><body><div id='app'></div></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    const code = readFileSync(path, "utf8");
    assets.set(pathname, createHash("sha256").update(code).digest("hex"));
    return route.fulfill({ contentType: "text/javascript", body: code });
  });
  await page.goto("http://sketch-cad-import.test/");
  await page.evaluate(async () => {
    const sketch = await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.insertAdjacentHTML("beforeend", `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px 'Segoe UI',sans-serif}.tube-section-sketch-body>aside{height:240px;overflow:auto}.tube-sketch-preview-panel{min-height:600px}.tube-sketch-property-panel{min-height:600px}.tube-sketch-property-body{height:300px;overflow:auto}.tube-sketch-section-session{min-height:400px}.tube-sketch-property-body>.tube-sketch-property-empty{min-height:450px}</style>`);
    const mount = document.querySelector("#app"), view = { activeAreaId: "sketch", scene: { tubeDesigner: {} } };
    const f = window.importFixture = { mount, view, sketch, calls: [], errors: [], notices: [], pending: 0,
      path: "D:/fixture/中文草图.DXF", holdPicker: false, holdRead: false, failRead: "", format: "dxf", renders: 0 };
    const cadDocument = { header: {}, sourceFormat: "ascii", sections: { ENTITIES: [
      { type: "CIRCLE", values: [[10, "30"], [20, "100"], [40, "4"]] },
    ] } };
    f.context = { mount, sceneProxy: { async invoke(method) { throw Error(`Wrong scene channel: ${method}`); } },
      productProxy: { async invoke(method, payload) {
        if (method !== "TubeDesigner.ReadSketchCad") throw Error(`Unexpected product method: ${method}`);
        f.calls.push({ scope: "product", method, payload: structuredClone(payload) });
        const respond = () => {
          if (f.failRead) throw Error(f.failRead);
          return { format: f.format, sourcePath: payload.sourcePath, document: structuredClone(cadDocument) };
        };
        if (!f.holdRead) return respond();
        return new Promise((resolve, reject) => { f.resolveRead = () => { try { resolve(respond()); } catch(error) { reject(error); } }; });
      } }, appProxy: { bridge: { async openFileDialog(options) {
        f.calls.push({ scope: "picker", method: "openFileDialog", payload: structuredClone(options) });
        if (!f.holdPicker) return f.path;
        return new Promise(resolve => { f.resolvePicker = value => resolve(value); });
      } } }, actions: { async selectRibbonTab() {} } };
    f.ops = { renderProject() {
      f.renders++;
      if (!sketch.patchSketchDialogDom(f.context, view, mount, f.ops)) {
        mount.innerHTML = sketch.renderSectionSketchDialog(f.context, view);
        sketch.rememberSketchDialogDom(view, mount, f.context.sceneProxy);
      }
      sketch.attachSketchAreaInteractions(f.context, view, mount, f.ops);
    }, showNotice(_context, _view, message) { f.notices.push(message); } };
    f.open = () => { view.activeAreaId = "sketch"; sketch.beginNewSectionSketch(view); view.tubeDesignerSketchDialogOpen = true; view.error = ""; f.ops.renderProject(); };
    f.start = commandId => {
      f.pending++;
      Promise.resolve(sketch.handleSketchRibbonCommand(f.context, view, commandId, f.ops))
        .catch(error => { f.errors.push(error.message); view.error = error.message; f.ops.renderProject(); })
        .finally(() => f.pending--);
    };
    mount.addEventListener("click", event => {
      const target = event.target.closest("[data-cam-action]");
      if (!target || target.disabled) return;
      if (target.dataset.sketchCommand) return f.start(target.dataset.sketchCommand);
      f.pending++;
      Promise.resolve(sketch.handleSketchAreaAction(f.context, view, target.dataset.camAction, target, f.ops))
        .catch(error => { f.errors.push(error.message); view.error = error.message; f.ops.renderProject(); })
        .finally(() => f.pending--);
    });
    mount.addEventListener("change", event => {
      const target = event.target.closest("[data-cam-change-action]");
      if (target) void sketch.handleSketchAreaAction(f.context, view, target.dataset.camChangeAction, target, f.ops);
    });
    f.open();
  });
  const command = key => page.locator(`[data-sketch-command="sketch.${key}"]`);
  const idle = () => page.waitForFunction(() => !importFixture.pending && !importFixture.view.tubeDesignerSketch.cadImportPending);
  const entities = () => page.evaluate(() => structuredClone(importFixture.view.tubeDesignerSketch.section.entities));

  await page.evaluate(() => { importFixture.holdPicker = true; importFixture.holdRead = true; });
  await command("import").click();
  await page.waitForFunction(() => Boolean(importFixture.resolvePicker));
  assert.equal(await command("import").isDisabled(), true);
  assert.equal(await command("commit").isDisabled(), true);
  assert.equal(await page.locator('[data-cam-change-action="tube-designer-sketch-section-name"]').isDisabled(), false);
  await page.evaluate(() => importFixture.resolvePicker(importFixture.path));
  await page.waitForFunction(() => Boolean(importFixture.resolveRead));
  // Add a new figure through the live controls while reading; import must append
  // to this newest draft, never restore the request's earlier empty entity list.
  await command("circle").click();
  const numericPoint = async (x, y) => {
    for (const [key, value] of [["x", x], ["y", y]]) {
      const field = page.locator(`[data-tube-sketch-numeric-field="${key}"]`);
      await field.fill(String(value)); await field.dispatchEvent("change");
    }
    await page.locator('[data-cam-action="tube-designer-sketch-numeric-apply"]').click();
  };
  await numericPoint(-20, -20); await numericPoint(-17, -20);
  const concurrentFigure = (await entities())[0]; assert.equal(concurrentFigure.radius, 3);
  const beforeImportHistory = await page.evaluate(() => ({ history: [...importFixture.view.tubeDesignerSketch.section.history],
    selection: [...importFixture.view.tubeDesignerSketch.section.selectedIds] }));
  const retention = await page.evaluate(async () => {
    const f = importFixture, input = document.querySelector('[data-cam-change-action="tube-designer-sketch-section-name"]');
    const canvas = document.querySelector("[data-tube-sketch-canvas]"), panes = [...document.querySelectorAll(".tube-section-sketch-body>aside")];
    const nested = document.querySelector(".tube-sketch-property-body");
    let canvasEvents = 0, inputEvents = 0;
    canvas.addEventListener("import-probe", () => canvasEvents++); input.addEventListener("import-probe", () => inputEvents++);
    input.focus({ preventScroll: true }); input.value = "导入中继续编辑名称"; input.setSelectionRange(1, 6, "forward");
    panes[0].scrollTop = 80; panes[1].scrollTop = 60; nested.scrollTop = 45;
    f.ops.renderProject();
    // Move again after the request began; restoring the first position is wrong.
    panes[0].scrollTop = 130; panes[1].scrollTop = 100; nested.scrollTop = 75;
    const latest = [panes[0].scrollTop, panes[1].scrollTop, nested.scrollTop];
    f.resolveRead();
    while (f.pending) await new Promise(resolve => setTimeout(resolve, 10));
    const next = document.querySelector('[data-cam-change-action="tube-designer-sketch-section-name"]');
    canvas.dispatchEvent(new Event("import-probe")); next.dispatchEvent(new Event("import-probe"));
    return { sameInput: input === next, sameCanvas: canvas === document.querySelector("[data-tube-sketch-canvas]"),
      samePanes: panes.every((pane, i) => pane === document.querySelectorAll(".tube-section-sketch-body>aside")[i]),
      sameNested: nested === document.querySelector(".tube-sketch-property-body"), focused: document.activeElement === next,
      value: next.value, selection: [next.selectionStart, next.selectionEnd], latest,
      actual: [panes[0].scrollTop, panes[1].scrollTop, nested.scrollTop], canvasEvents, inputEvents, pending: Boolean(f.view.pending) };
  });
  assert.ok(retention.sameInput && retention.sameCanvas && retention.samePanes && retention.sameNested && retention.focused);
  assert.equal(retention.value, "导入中继续编辑名称"); assert.deepEqual(retention.selection, [1, 6]);
  assert.deepEqual(retention.actual, retention.latest); assert.ok(retention.latest.every(value => value > 0));
  assert.equal(retention.canvasEvents, 1); assert.equal(retention.inputEvents, 1); assert.equal(retention.pending, false);
  assert.deepEqual((await entities())[0], concurrentFigure);
  assert.equal((await entities()).length, 2);
  assert.equal((await entities())[1].cy, 100);
  const picker = await page.evaluate(() => importFixture.calls.find(call => call.scope === "picker").payload);
  assert.deepEqual(picker.filters[0].extensions, ["dwg", "dxf"]);
  assert.equal(await page.getByRole("button", { name: "导入 DWG / DXF", exact: true }).count(), 1);
  cases.push({ id: "dxf-direct-import-keeps-current-input-and-latest-scroll", ...retention, picker });

  const importedEntities = await entities();
  assert.equal(await page.evaluate(() => importFixture.view.tubeDesignerSketch.section.history.length), beforeImportHistory.history.length + 1);
  await command("undo").click(); await idle();
  assert.deepEqual(await entities(), [concurrentFigure]);
  assert.deepEqual(await page.evaluate(() => ({ history: [...importFixture.view.tubeDesignerSketch.section.history],
    selection: [...importFixture.view.tubeDesignerSketch.section.selectedIds] })), beforeImportHistory);
  await command("redo").click(); await idle();
  assert.deepEqual(await entities(), importedEntities);
  cases.push({ id: "import-is-one-undo-step-and-redo-preserves-entity-identities" });

  await page.evaluate(() => { importFixture.holdPicker = false; importFixture.holdRead = true; importFixture.resolveRead = null; importFixture.path = "D:/fixture/直接选择.DwG"; importFixture.format = "dwg"; });
  await command("import").click(); await page.waitForFunction(() => Boolean(importFixture.resolveRead));
  const removedField = await page.evaluate(async () => {
    const f = importFixture, input = document.querySelector('[data-sketch-field="centerX"]');
    const oldEntityId = input.dataset.sketchEntityId;
    input.focus({ preventScroll: true }); input.value = "12.75"; f.resolveRead();
    while (f.pending) await new Promise(resolve => setTimeout(resolve, 10));
    const next = document.querySelector('[data-sketch-field="centerX"]');
    return { oldEntityId, newEntityId: next.dataset.sketchEntityId, oldConnected: input.isConnected,
      sameInput: input === next, focusedNewField: document.activeElement === next, value: next.value };
  });
  await idle(); assert.equal(removedField.oldConnected, false); assert.equal(removedField.sameInput, false);
  assert.notEqual(removedField.oldEntityId, removedField.newEntityId); assert.equal(removedField.focusedNewField, false); assert.equal(removedField.value, "30");
  assert.equal((await entities()).length, 3);
  cases.push({ id: "dwg-direct-import-does-not-transfer-focus-to-another-entity", entities: 3, ...removedField });

  await command("move").click();
  const beforeCancel = await entities();
  const beforeHistory = await page.evaluate(() => ({ history: [...importFixture.view.tubeDesignerSketch.section.history],
    selection: [...importFixture.view.tubeDesignerSketch.section.selectedIds] }));
  const readsBeforeCancel = await page.evaluate(() => importFixture.calls.filter(call => call.scope === "product").length);
  await page.evaluate(() => { importFixture.holdRead = false; importFixture.path = null; });
  await command("import").click(); await idle();
  assert.deepEqual(await entities(), beforeCancel);
  assert.deepEqual(await page.evaluate(() => ({ history: [...importFixture.view.tubeDesignerSketch.section.history],
    selection: [...importFixture.view.tubeDesignerSketch.section.selectedIds] })), beforeHistory);
  assert.equal(await page.evaluate(() => importFixture.view.tubeDesignerSketch.command.tool), "cad-move");
  assert.equal(await page.evaluate(() => importFixture.calls.filter(call => call.scope === "product").length), readsBeforeCancel);
  cases.push({ id: "chooser-cancel-keeps-active-operation-and-draft" });

  await page.evaluate(() => { importFixture.path = "D:/fixture/wrong.step"; });
  await command("import").click(); await idle();
  assert.match(await page.locator("[data-sketch-save-error]").innerText(), /DWG.*DXF/);
  assert.deepEqual(await entities(), beforeCancel);
  assert.equal(await page.evaluate(() => importFixture.calls.filter(call => call.scope === "product").length), readsBeforeCancel);
  cases.push({ id: "unsupported-extension-is-rejected-before-native-read" });

  await page.evaluate(() => { importFixture.path = "D:/fixture/corrupt.dwg"; importFixture.failRead = "DWG 文件损坏，无法读取。"; });
  await command("import").click(); await idle();
  assert.match(await page.locator("[data-sketch-save-error]").innerText(), /DWG 文件损坏/);
  assert.deepEqual(await entities(), beforeCancel);
  assert.equal(await command("import").isDisabled(), false);
  assert.deepEqual(await page.evaluate(() => ({ history: [...importFixture.view.tubeDesignerSketch.section.history],
    selection: [...importFixture.view.tubeDesignerSketch.section.selectedIds] })), beforeHistory);
  cases.push({ id: "native-failure-is-logged-and-leaves-draft-editable" });

  await page.evaluate(() => { importFixture.failRead = ""; importFixture.holdRead = true; importFixture.resolveRead = null; });
  await command("import").click(); await page.waitForFunction(() => Boolean(importFixture.resolveRead));
  const beforeLate = await entities();
  page.once("dialog", dialog => dialog.accept());
  await command("cancel").click();
  await page.waitForFunction(() => !importFixture.view.tubeDesignerSketchDialogOpen);
  await page.evaluate(() => importFixture.resolveRead()); await idle();
  assert.deepEqual(await entities(), beforeLate);
  assert.equal(await page.locator("[data-tube-sketch-canvas]").count(), 0);
  cases.push({ id: "closed-dialog-discards-late-native-read-without-reopening" });

  await page.evaluate(() => { importFixture.open(); importFixture.path = "D:/fixture/late.dxf"; importFixture.resolveRead = null; });
  await command("import").click(); await page.waitForFunction(() => Boolean(importFixture.resolveRead));
  await page.evaluate(() => { const f = importFixture; f.open(); f.resolveRead(); });
  await idle(); assert.deepEqual(await entities(), []); assert.equal(await command("import").isDisabled(), false);
  cases.push({ id: "new-draft-discards-late-read-and-restores-import-availability" });

  await page.evaluate(() => { importFixture.resolveRead = null; });
  await command("import").click(); await page.waitForFunction(() => Boolean(importFixture.resolveRead));
  const newSceneRetention = await page.evaluate(async () => {
    const f = importFixture; f.context.sceneProxy = { async invoke() { throw Error("new scene"); } }; f.open();
    const input = document.querySelector('[data-cam-change-action="tube-designer-sketch-section-name"]');
    input.focus({ preventScroll: true }); input.value = "新场景正在输入"; input.setSelectionRange(1, 3);
    const renders = f.renders; f.resolveRead();
    while (f.pending) await new Promise(resolve => setTimeout(resolve, 10));
    return { sameInput: input === document.querySelector('[data-cam-change-action="tube-designer-sketch-section-name"]'),
      focused: document.activeElement === input, value: input.value, selection: [input.selectionStart, input.selectionEnd], lateRenders: f.renders - renders };
  });
  await idle(); assert.deepEqual(await entities(), []);
  assert.ok(newSceneRetention.sameInput && newSceneRetention.focused); assert.equal(newSceneRetention.value, "新场景正在输入");
  assert.deepEqual(newSceneRetention.selection, [1, 3]); assert.equal(newSceneRetention.lateRenders, 0);
  cases.push({ id: "new-scene-discards-late-read-and-does-not-steal-focus", ...newSceneRetention });
  assert.deepEqual(errors, []);
  assert.equal(await page.evaluate(() => importFixture.errors.length), 2);
  const evidence = await page.evaluate(() => ({ calls: importFixture.calls, errors: importFixture.errors, notices: importFixture.notices }));
  await page.screenshot({ path: resolve(output, "dwg-dxf-import-dialog.png") });
  writeFileSync(resolve(output, "report.json"), JSON.stringify({ passed: true, browser: "Chrome", nativeTransport: "controlled protocol; native companion required", productionAssets: Object.fromEntries(assets), cases, evidence }, null, 2));
  console.log(`DWG/DXF import UI regression passed (${cases.length} scenarios); native transport controlled.`);
} catch (error) {
  if (page) await page.screenshot({ path: resolve(output, "failure.png") }).catch(() => {});
  writeFileSync(resolve(output, "failure.json"), JSON.stringify({ error: error.stack, cases }, null, 2));
  throw error;
} finally { await browser.close(); }
