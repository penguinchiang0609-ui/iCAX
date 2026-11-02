// Adapted OS chooser -> production product ReadSketchCad -> production parser
// and editable dialog -> real scene preview/cut/save -> .ictd reopen.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { repositoryRoot, startSideSketchNativeBridge } from "./fixtures/sideSketchNativeBridge.mjs";

const sourceRoot = resolve(repositoryRoot, "src");
const artifacts = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repositoryRoot, "output/tests/sketch-cad-import-native-browser"));
const manifestPath = resolve(process.env.ICAX_SKETCH_CAD_FIXTURES || resolve(repositoryRoot, "output/tests/sketch-cad-import/fixtures.json"));
const fixtureCases = JSON.parse(readFileSync(manifestPath, "utf8").replace(/^\uFEFF/, "")).cases;
assert.ok(Array.isArray(fixtureCases) && fixtureCases.length, "A concrete native import fixture manifest is required");
mkdirSync(artifacts, { recursive: true });
const bridge = await startSideSketchNativeBridge(artifacts);
const assets = new Map(), cases = [], errors = [], failureCases = [];
let browser, page;
const near = (actual, expected, label, tolerance = 1e-5) => assert.ok(Math.abs(actual - expected) <= tolerance, `${label}: ${actual} vs ${expected}`);
const withoutIds = entities => entities.map(({ id, ...entity }) => entity);
try {
  const rectangle = (width, height) => {
    const points = [[-width / 2, -height / 2], [width / 2, -height / 2], [width / 2, height / 2], [-width / 2, height / 2]];
    return { kind: "path", closed: true, segments: points.map((start, index) => ({ kind: "line", start, end: points[(index + 1) % 4] })) };
  };
  const profile = { schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "fixed-section", profileForm: "fixed",
    name: "二维 CAD 导入验收方管", width: 80, depth: 60, contours: [rectangle(80, 60), rectangle(76, 56)] };
  const blank = { profile, length: 200, quantity: 1 };
  const blankVolume = (80 * 60 - 76 * 56) * 200;
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome",
    args: ["--disable-background-timer-throttling", "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows"] });
  page = await browser.newPage({ viewport: { width: 1680, height: 1040 } });
  page.setDefaultTimeout(30000);
  page.on("pageerror", error => errors.push(error.message));
  await page.exposeFunction("nativeRpc", bridge.rpc);
  await page.route("http://sketch-cad-import-native.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><meta charset='utf-8'><body><div id='app' class='tube-designer-workspace'></div></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    const code = readFileSync(path, "utf8"); assets.set(pathname, createHash("sha256").update(code).digest("hex"));
    return route.fulfill({ contentType: "text/javascript", body: code });
  });
  await page.goto("http://sketch-cad-import-native.test/");
  await page.addStyleTag({ content: readFileSync(resolve(sourceRoot, "apps/_shared/workbench/styles/workbench.css"), "utf8") });
  await page.evaluate(async () => {
    const sketch = await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
    const parts = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    document.head.insertAdjacentHTML("beforeend", `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px 'Segoe UI',sans-serif;background:#13252d}</style>`);
    const mount = document.querySelector("#app"), view = { activeAreaId: "nesting", scene: { tubeDesigner: { nestingGroups: [] } } };
    const f = window.importNative = { mount, view, sketch, parts, calls: [], resources: [], failures: [], notices: [], pickers: [], pending: 0 };
    Object.defineProperty(f, "state", { get() { return view.tubeDesignerSketch; } });
    Object.defineProperty(f, "draft", { get() { return f.state.sideByPart[f.state.targetPartId]; } });
    const invoke = async (scope, method, payload) => {
      // Refuse a mixed-channel fixture even if a test-only bridge could dispatch it.
      if (scope === "product" && method !== "TubeDesigner.ReadSketchCad") throw Error(`Unexpected product method ${method}`);
      if (scope === "scene" && method === "TubeDesigner.ReadSketchCad") throw Error("CAD reading belongs to the product channel");
      const call = { scope, method, payload: structuredClone(payload) }; f.calls.push(call);
      try { call.result = await window.nativeRpc({ action: "invoke", scope, method: method.replace(/^TubeDesigner\./, ""), payload }); return call.result; }
      catch (error) { call.error = error.message; throw error; }
    };
    f.context = { mount, productProxy: { invoke: (method, payload) => invoke("product", method, payload) },
      appProxy: { bridge: { async openFileDialog(options) { f.pickers.push(structuredClone(options)); return f.sourcePath; } } },
      sceneProxy: { invoke: (method, payload) => invoke("scene", method, payload), resources: { async get(url, options) {
        const version = Number(options?.headers?.get("ICAX-Resource-Version") || 0);
        const response = await window.nativeRpc({ action: "resource", payload: { url, version } });
        f.resources.push({ url, version, bytes: response.bytes });
        return new Response(Uint8Array.from(atob(response.base64), character => character.charCodeAt(0)));
      } } } };
    f.ops = { createSideSketchViewport(options) { return f.viewport = createThreeViewport(options); },
      showNotice(_context, _view, message) { f.notices.push(message); }, renderProject() {
        if (!sketch.patchSketchDialogDom(f.context, view, mount, f.ops)) {
          const list = parts.listNestingParts(view.scene.tubeDesigner ?? {});
          mount.innerHTML = "<main>" + list.map(part => `<button data-cam-action="tube-designer-part-open-sketch" data-tube-designer-part-id="${part.entityId}">二维编辑 ${part.entityId}</button>`).join("") + "</main>" + sketch.renderSectionSketchDialog(f.context, view);
          sketch.rememberSketchDialogDom(view, mount, f.context.sceneProxy);
        }
        sketch.attachSketchAreaInteractions(f.context, view, mount, f.ops);
      } };
    f.load = ({ payload, report, sourcePath }) => {
      f.sourcePath = sourcePath; view.activeAreaId = "nesting";
      sketch.beginNewPartSideSketch(view, payload, report); view.tubeDesignerSketchDialogOpen = true;
      f.draft.trajectoryWidth = .8; f.state.snapEnabled = false; f.state.orthoEnabled = false; f.ops.renderProject();
    };
    const dispatch = (action, target) => {
      f.pending++;
      const operation = action.startsWith("tube-designer-sketch-")
        ? sketch.handleSketchAreaAction(f.context, view, action, target, f.ops)
        : parts.handlePartsAreaAction(f.context, view, action, target, f.ops);
      Promise.resolve(operation).catch(error => { f.failures.push(error.message); view.error = error.message; f.ops.renderProject(); }).finally(() => f.pending--);
    };
    mount.addEventListener("click", event => { const target = event.target.closest("[data-cam-action]"); if (target && !target.disabled) dispatch(target.dataset.camAction, target); });
    mount.addEventListener("change", event => { const target = event.target.closest("[data-cam-change-action]"); if (target) dispatch(target.dataset.camChangeAction, target); });
  });
  const idle = async () => {
    await page.waitForFunction(() => !importNative.pending && !importNative.view.pending && !importNative.state?.cadImportPending && !importNative.state?.sidePreviewPending, null, { timeout: 180000 });
    assert.deepEqual(await page.evaluate(() => importNative.failures), []);
  };
  const ready = async () => { await idle(); await page.locator('[data-side-sketch-preview-ready="true"]').waitFor({ timeout: 180000 }); };
  const command = key => page.locator(`[data-sketch-command="sketch.${key}"]`);
  const setCad = async (key, value) => { const field = page.locator(`[data-tube-sketch-cad-field="${key}"]`); await field.fill(String(value)); await field.dispatchEvent("change"); };
  const sceneParts = scene => scene.tubeDesigner.nestingGroups.flatMap(group => group.parts);
  const draft = () => page.evaluate(() => structuredClone(importNative.draft));
  const savedCases = [];
  for (const fixture of fixtureCases) {
    assert.ok(typeof fixture.sourcePath === "string" && /\.(dxf|dwg)$/i.test(fixture.sourcePath), fixture.id + " fixture path");
    if (fixture.workflow === false) {
      const response = await bridge.invoke("ReadSketchCad", { sourcePath: fixture.sourcePath }, "product");
      const parsed = await page.evaluate(async data => {
        const { mapSketchCadDocument } = await import("/src/apps/tube-designer/webpage/sketchCadExchange.mjs");
        return mapSketchCadDocument(data.document);
      }, response);
      if (fixture.expectedEntityCount != null) assert.equal(parsed.entities.length, fixture.expectedEntityCount, fixture.id);
      if (fixture.expectedKinds) assert.deepEqual(parsed.entities.map(entity => entity.kind), fixture.expectedKinds, fixture.id);
      if (fixture.expectedEntities) assert.deepEqual(withoutIds(parsed.entities), fixture.expectedEntities, fixture.id);
      cases.push({ id: fixture.id, workflow: false, format: response.format, converterVersion: response.converterVersion, sourceFormat: parsed.sourceFormat,
        entities: parsed.entities, sourceSHA: createHash("sha256").update(readFileSync(fixture.sourcePath)).digest("hex"), passed: true });
      continue;
    }
    const payload = { ...blank, previewResourceKey: `cad-import-${fixture.id.replace(/[^a-z0-9_-]/gi, "-")}` }, report = await bridge.invoke("PreviewNestingSideSketchPart", payload);
    assert.equal(report.available, true); near(report.unfolding.surfaces.find(surface => !surface.inner).uPeriod, 280, "blank perimeter");
    await page.evaluate(input => importNative.load(input), { payload, report, sourcePath: fixture.sourcePath }); await ready();
    const beforeImport = await draft();
    await command("import").click(); await idle();
    const imported = await draft(), readCall = await page.evaluate(() => importNative.calls.findLast(call => call.method === "TubeDesigner.ReadSketchCad"));
    assert.ok(readCall?.result?.document); assert.equal(readCall.scope, "product"); assert.equal(readCall.payload.sourcePath, fixture.sourcePath);
    assert.equal(readCall.result.format, /\.dwg$/i.test(fixture.sourcePath) ? "dwg" : "dxf");
    assert.ok(imported.entities.length > 0, fixture.id + " imports editable entities");
    if (fixture.expectedEntityCount != null) assert.equal(imported.entities.length, fixture.expectedEntityCount, fixture.id);
    if (fixture.expectedKinds) assert.deepEqual(imported.entities.map(entity => entity.kind), fixture.expectedKinds, fixture.id);
    if (fixture.expectedEntities) assert.deepEqual(withoutIds(imported.entities), fixture.expectedEntities, fixture.id);
    if (fixture.expectedCoordinates) {
      for (const [kind, coordinates] of Object.entries(fixture.expectedCoordinates)) {
        const entity = imported.entities.find(candidate => candidate.kind === kind);
        assert.ok(entity, `${fixture.id} preserves ${kind}`);
        for (const [key, expected] of Object.entries(coordinates)) near(entity[key], expected, `${fixture.id} ${kind}.${key}`);
      }
    }
    assert.deepEqual(imported.selectedIds, imported.entities.map(entity => entity.id));
    assert.equal(imported.history.length, beforeImport.history.length + 1, fixture.id + " import is a single undo step");
    await command("undo").click(); await idle();
    const undone = await draft();
    assert.deepEqual(undone.entities, beforeImport.entities, fixture.id + " undo restores previous geometry");
    assert.deepEqual(undone.selectedIds, beforeImport.selectedIds, fixture.id + " undo restores previous selection");
    assert.deepEqual(undone.history, beforeImport.history, fixture.id + " undo restores previous history");
    await command("redo").click(); await idle();
    assert.deepEqual((await draft()).entities, imported.entities, fixture.id + " redo restores identical imported entities");

    if (!failureCases.length) {
      // A recognized DWG signature and full minimum header reaches the isolated
      // converter; rejecting a tiny text file would only test signature validation.
      const corruptDwg = Buffer.concat([Buffer.from("AC1032"), Buffer.alloc(250)]);
      for (const [filename, content] of [["corrupt.dwg", corruptDwg], ["truncated.dxf", "0\nSECTION\n2\nENTITIES\n0\nCIRCLE\n10\n30\n20\n100\n40\n4\n"]]) {
        const sourcePath = resolve(artifacts, filename); writeFileSync(sourcePath, content);
        const beforeFailure = await draft();
        const sceneBeforeFailure = await bridge.invoke("List");
        await page.evaluate(source => { importNative.sourcePath = source; }, sourcePath);
        await command("import").click();
        await page.waitForFunction(() => !importNative.pending && !importNative.state.cadImportPending);
        const failures = await page.evaluate(() => [...importNative.failures]);
        assert.equal(failures.length, 1, filename + " rejects through production file reader");
        assert.equal(await page.locator("[data-sketch-save-error]").innerText(), failures[0]);
        assert.deepEqual(await draft(), beforeFailure, filename + " leaves draft, history, selection and dirty state unchanged");
        assert.deepEqual(await bridge.invoke("List"), sceneBeforeFailure, filename + " leaves persisted scene unchanged");
        assert.equal(await command("import").isDisabled(), false);
        failureCases.push({ id: filename, error: failures[0], draftUnchanged: true, sceneUnchanged: true, passed: true });
        await page.evaluate(source => { importNative.sourcePath = source; importNative.failures = []; importNative.view.error = ""; importNative.ops.renderProject(); }, fixture.sourcePath);
      }
    }
    await command("move").click(); await setCad("dx", 1); await setCad("dy", 0);
    await page.locator('[data-cam-action="tube-designer-sketch-cad-apply"]').click(); await idle();
    const edited = await draft(); assert.notDeepEqual(edited.entities, imported.entities, fixture.id + " remains editable after import");
    await page.locator('[data-cam-action="tube-designer-sketch-preview-side"]').click(); await ready();
    const preview = await page.evaluate(() => importNative.calls.findLast(call => call.method === "TubeDesigner.PreviewNestingSideSketchPart"));
    assert.ok(preview?.result?.preview?.baseGeometry?.url); assert.deepEqual(preview.payload.sketch.entities, edited.entities);
    await page.screenshot({ path: resolve(artifacts, `${fixture.id}-preview.png`) });
    await command("select").click(); await command("commit").click(); await idle();
    assert.equal(await page.evaluate(() => importNative.view.tubeDesignerSketchDialogOpen), false);
    const saved = await page.evaluate(() => importNative.calls.findLast(call => call.method === "TubeDesigner.AddNestingSideSketchPart"));
    assert.ok(saved?.result?.partEntityId);
    const solid = await bridge.rpc({ action: "geometry", payload: { partEntityId: saved.result.partEntityId } });
    assert.equal(solid.valid, true); assert.ok(solid.volume > 0 && solid.volume < blankVolume - 1, fixture.id + " removes real wall material");
    const persisted = sceneParts(saved.result).find(part => part.entityId === saved.result.partEntityId).properties["tubeDesigner.sideSketch"];
    assert.deepEqual(persisted.entities, edited.entities);
    const removedVolume = blankVolume - solid.volume;
    if (fixture.expectedRemovedVolume != null) near(removedVolume, fixture.expectedRemovedVolume, fixture.id + " removed volume", fixture.volumeTolerance ?? .05);
    const item = { id: fixture.id, workflow: true, format: readCall.result.format, sourceFormat: readCall.result.document.sourceFormat,
      sourceVersion: readCall.result.sourceVersion, converterVersion: readCall.result.converterVersion, sourcePath: fixture.sourcePath,
      sourceSHA: createHash("sha256").update(readFileSync(fixture.sourcePath)).digest("hex"), importedEntities: imported.entities,
      editedEntities: edited.entities, partEntityId: saved.result.partEntityId, geometry: solid, removedVolume, passed: true, undoRedoVerified: true, reopenVerified: false };
    cases.push(item); savedCases.push(item); console.log(`native CAD import: ${fixture.id} read/edit/cut/save passed`);
  }
  const projectFile = "sketch-cad-import-browser-native.ictd";
  await bridge.rpc({ action: "save", payload: { fileName: projectFile } });
  await bridge.rpc({ action: "open", payload: { fileName: projectFile } });
  const reopened = await bridge.invoke("List");
  await page.evaluate(scene => { importNative.view.scene = { tubeDesigner: scene.tubeDesigner }; importNative.ops.renderProject(); }, reopened);
  for (const item of savedCases) {
    const part = sceneParts(reopened).find(part => part.entityId === item.partEntityId);
    assert.deepEqual(part.properties["tubeDesigner.sideSketch"].entities, item.editedEntities, item.id + " reopened editable parameters");
    const solid = await bridge.rpc({ action: "geometry", payload: { partEntityId: item.partEntityId } });
    assert.equal(solid.valid, true); near(solid.volume, item.geometry.volume, item.id + " reopened BRep volume");
    await page.locator(`[data-cam-action="tube-designer-part-open-sketch"][data-tube-designer-part-id="${item.partEntityId}"]`).click(); await ready();
    assert.deepEqual((await draft()).entities, item.editedEntities, item.id + " reopened production dialog");
    await command("cancel").click(); await idle(); item.reopenVerified = true;
  }
  assert.ok(savedCases.every(item => item.reopenVerified)); assert.deepEqual(errors, []);
  const evidence = await page.evaluate(() => ({ resources: importNative.resources, failures: importNative.failures, pickers: importNative.pickers, notices: importNative.notices,
    calls: importNative.calls.map(({ result, ...call }) => ({ ...call, format: result?.format })) }));
  assert.equal(evidence.failures.length, 0); assert.ok(evidence.resources.length > 0 || savedCases.length === 0);
  writeFileSync(resolve(artifacts, "report.json"), JSON.stringify({ passed: true, caseCount: cases.length, workflowCount: savedCases.length,
    reopenVerifiedCount: savedCases.filter(item => item.reopenVerified).length, dllHash: bridge.dllHash, importerHash: bridge.importerHash,
    fixtureManifest: manifestPath, assets: Object.fromEntries(assets), cases, failureCases, evidence, nativeRequests: bridge.requests,
    coverage: "Real production file read/conversion, DXF parser, Chrome editable controls, import undo/redo, corrupt-file draft/scene atomicity, scene preview/GPU resources, native material removal and persisted .ictd reopen. OS chooser returns fixture paths; installed CEF and OS chooser interaction outside scope." }, null, 2));
  console.log(JSON.stringify({ passed: true, cases: cases.length, workflows: savedCases.length, reopenVerified: savedCases.filter(item => item.reopenVerified).length, artifacts }));
} catch (error) {
  if (page) await page.screenshot({ path: resolve(artifacts, "failure.png") }).catch(() => {});
  writeFileSync(resolve(artifacts, "failure.json"), JSON.stringify({ error: error.stack, dllHash: bridge.dllHash, importerHash: bridge.importerHash, cases,
    browser: page ? await page.evaluate(() => ({ failures: importNative.failures, calls: importNative.calls.map(({ result, ...call }) => call), state: importNative.state })).catch(() => null) : null }, null, 2));
  throw error;
} finally { if (browser) await browser.close(); await bridge.close(); }
