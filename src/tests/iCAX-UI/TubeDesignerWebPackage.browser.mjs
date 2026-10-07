// The shipped AppShell entry and all modules are served only from a relocated
// installation. The native transport is a fixture; no production module is
// substituted and no source checkout URL can be requested by the browser.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, readdirSync, statSync, unlinkSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { resolve, join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const source = fileURLToPath(new URL("../../", import.meta.url));
const output = resolve(source, "../output/tests/tube-designer-web-package");
const runtime = mkdtempSync(join(tmpdir(), "TubeDesigner web package "));
mkdirSync(output, { recursive: true });
const deployment = resolve(source, "tools/build/sync_tube_designer_web_assets.ps1");
const trees = ["apps/tube-designer/webpage", "apps/_shared/workbench", "iCAX-UI/SDK",
  "iCAX-UI/AppProxy", "iCAX-UI/ProductProxy", "iCAX-UI/ProjectProxy", "iCAX-UI/SceneProxy", "iCAX-UI/UI"];
function sync(validateOnly = false) {
  return spawnSync("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", deployment,
    "-OutputDirectory", runtime, ...(validateOnly ? ["-ValidateOnly"] : [])], { cwd: runtime, encoding: "utf8" });
}
function successfulSync(validateOnly = false) {
  const result = sync(validateOnly);
  assert.equal(result.status, 0, result.stdout + result.stderr);
  return result.stdout.trim();
}
const deploymentLog = [successfulSync(), successfulSync(true)];
const rejectionCases = [];
for (const [name, change, cleanup, expression] of [
  ["changed-sdk", () => writeFileSync(join(runtime, "iCAX-UI/SDK/runtime.mjs"), "// changed\n"), () => {}, /differs from source or is missing/],
  ["missing-proxy", () => unlinkSync(join(runtime, "iCAX-UI/ProjectProxy/ProjectProxy.mjs")), () => {}, /differs from source or is missing/],
  ["obsolete-sdk", () => writeFileSync(join(runtime, "iCAX-UI/SDK/removed-module.mjs"), "export default 0;"), () => {}, /obsolete/],
]) {
  change(); const rejected = sync(true);
  assert.notEqual(rejected.status, 0, name + " must fail ValidateOnly");
  assert.match(rejected.stdout + rejected.stderr, expression);
  cleanup(); successfulSync(); successfulSync(true); rejectionCases.push(name);
}
function filesUnder(directory) {
  return readdirSync(directory).flatMap(name => {
    const path = join(directory, name);
    return statSync(path).isDirectory() ? filesUnder(path) : [path];
  });
}
const hashes = Object.fromEntries(trees.flatMap(tree => filesUnder(join(runtime, tree))).map(path => {
  const key = relative(runtime, path).split(sep).join("/");
  const hash = bytes => createHash("sha256").update(bytes).digest("hex");
  const actual = hash(readFileSync(path));
  assert.equal(actual, hash(readFileSync(join(source, key))), key + " must match the source asset");
  return [key, actual];
}));
for (const asset of ["apps/Branding.Setting", "apps/branding/icax.ico"]) {
  assert.deepEqual(readFileSync(join(runtime, asset)), readFileSync(join(source, asset)));
}

const descriptor = JSON.parse(readFileSync(join(source, "apps/tube-designer/templates/product/single_face_security_window/template.json"), "utf8"));
const display = JSON.parse(readFileSync(join(source, "apps/tube-designer/templates/product/single_face_security_window/display.json"), "utf8"));
const requests = [], failedRequests = [], errors = [];
const mime = { ".html": "text/html", ".mjs": "text/javascript", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon", ".json": "application/json" };
const server = createServer((request, response) => {
  const pathname = decodeURIComponent(new URL(request.url, "http://package.test").pathname);
  if (pathname === "/favicon.ico") {
    response.writeHead(302, { Location: "/apps/tube-designer/webpage/assets/tube-designer.ico" });
    response.end(); return;
  }
  const path = resolve(runtime, "." + pathname);
  if (!path.startsWith(runtime + sep) || pathname.includes("/src/")) {
    failedRequests.push(pathname); response.writeHead(403); response.end(); return;
  }
  try {
    const data = readFileSync(path);
    requests.push(pathname);
    response.writeHead(200, { "Content-Type": mime[path.slice(path.lastIndexOf("."))] ?? "application/octet-stream" });
    response.end(data);
  } catch {
    failedRequests.push(pathname); response.writeHead(404); response.end();
  }
});
await new Promise(resolveReady => server.listen(0, "127.0.0.1", resolveReady));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });
let result;
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.on("pageerror", error => errors.push(error.message));
  await page.addInitScript(({ descriptor, display }) => {
    const appChannel = "42db9dda-8fea-49eb-a51b-265f70ff0001", productChannel = "42db9dda-8fea-49eb-a51b-265f70ff0002";
    const sceneChannel = "42db9dda-8fea-49eb-a51b-265f70ff0003", projectId = "42db9dda-8fea-49eb-a51b-265f70ff0004";
    const sceneId = "42db9dda-8fea-49eb-a51b-265f70ff0005", entityId = "42db9dda-8fea-49eb-a51b-265f70ff0006";
    const listeners = new Map(), nativeRequests = [], saved = [];
    const text = value => typeof value === "string" ? value : value?.["zh-CN"] ?? value?.en ?? "";
    const template = { ...descriptor, display, available: true, descriptorLoaded: true, name: text(descriptor.displayName),
      groups: descriptor.groups.map(group => ({ ...group, displayName: text(group.displayName) })),
      parameters: descriptor.parameters.map(field => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
        displayName: text(field.displayName), groupKey: field.group,
        options: field.choices?.map(choice => ({ ...choice, label: text(choice.displayName) })) })) };
    const parameters = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
    const product = { entityId, name: "防盗窗", templateId: template.id, quantity: 1, parameters };
    const scene = { sceneId, sceneChannelId: sceneChannel, undoRedo: { revision: 1, canUndo: false },
      tubeDesigner: { templates: [template], instances: [product], product, activeProductId: entityId,
        members: [], joints: [], parts: [], manufacturingGroups: [] } };
    const project = { projectId, projectName: "独立运行包验证", mainSceneId: sceneId, mainScene: scene };
    const catalog = { catalogId: "package-catalog", mainProject: project };
    const productState = { productId: "icax.tube-designer", productName: "TubeDesigner", isStarted: true,
      productChannelId: productChannel, frontendEntry: "apps/tube-designer/webpage/entry.mjs", catalogs: [catalog],
      projectFile: { fileExtensions: ["ictd"] } };
    const appState = { state: "Running", products: [productState] };
    let seeded = false;
    const fixture = { nativeRequests, saved, projectId, scene, async seed() {
      if (seeded) return; seeded = true;
      const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
      fixture.view = getProjectView(projectId);
      Object.assign(fixture.view, { scene: structuredClone(scene), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
        tubeDesignerParameterPanelProductId: entityId, tubeDesignerParameterDisclosureState: { initialized: true } });
    } };
    globalThis.__packageFixture = fixture;
    const responseFor = (method, payload) => {
      nativeRequests.push({ method, payload: structuredClone(payload) });
      if (method === "App.GetState") return appState;
      if (method === "App.StartProduct") return { state: appState, product: productState };
      if (method === "Product.GetState") return productState;
      if (method === "Product.OpenProjectCatalog") return { state: productState, catalog };
      if (method === "Project.GetState") return { project, activeScene: scene };
      if (method === "Project.GetUndoRedoState") return scene.undoRedo;
      if (method === "TubeDesigner.List") return { tubeDesigner: scene.tubeDesigner };
      if (method === "TubeDesigner.GetProductTemplateDescriptor" || method === "TubeDesigner.GetTemplateDescriptor") return { template };
      if (method === "TubeDesigner.GetPunchTools") return { tools: [] };
      if (method === "TubeDesigner.UpdateProductParameters") {
        scene.tubeDesigner.product.parameters = structuredClone(payload.parameters);
        scene.tubeDesigner.instances[0].parameters = structuredClone(payload.parameters);
        scene.undoRedo.revision++;
        return { tubeDesigner: scene.tubeDesigner };
      }
      if (method === "Project.Save") {
        project.projectPath = payload.projectPath; saved.push(structuredClone(scene));
        return { saved: true, projectPath: payload.projectPath, project };
      }
      if (method === "View.GetOrCreate") return { viewId: "package-view", resource: { url: "icax-resource://package/view", version: "1" } };
      if (method === "View.Release") return {};
      throw new Error("Unexpected package native request: " + method);
    };
    globalThis.icax = {
      async getApplicationChannelId() { await fixture.seed(); return appChannel; },
      async registerProductChannel() { return productChannel; },
      async registerSceneChannel() { return sceneChannel; },
      subscribeSDOFrames(channel, handler) { if (!listeners.has(channel)) listeners.set(channel, new Set());
        listeners.get(channel).add(handler); return () => listeners.get(channel).delete(handler); },
      async postSDOFrame(frame) {
        if (Number(frame.kind) !== 0) return;
        const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");
        const { deserializeVariantText, serializeVariantText } = await import("/iCAX-UI/SDK/SDO/variantSerializer.mjs");
        const methods = ["App.GetState", "App.StartProduct", "Product.GetState", "Product.OpenProjectCatalog", "Project.GetState",
          "Project.Save", "Project.GetUndoRedoState", "View.GetOrCreate", "View.Release", "TubeDesigner.List", "TubeDesigner.GetProductTemplateDescriptor",
          "TubeDesigner.GetTemplateDescriptor", "TubeDesigner.GetPunchTools", "TubeDesigner.UpdateProductParameters"];
        const method = methods.find(name => makeSDOMethodCodeFromName(name) === String(frame.methodCode));
        if (!method) throw new Error("Unknown package SDO code: " + frame.methodCode);
        const data = responseFor(method, deserializeVariantText(frame.payloadText));
        const response = { ...frame, kind: 2, status: 0, payloadText: serializeVariantText(data) };
        queueMicrotask(() => listeners.get(frame.channelId)?.forEach(handler => handler(response)));
      },
      async requestResource(request) {
        if (request.url !== "icax-resource://package/view") throw new Error("Unexpected package resource " + request.url);
        // Empty, schema-v2 ICVW snapshot: version 2, revision 1, zero rows.
        const bytes = new Uint8Array(48), data = new DataView(bytes.buffer);
        data.setUint32(0, 24, true); bytes.set([73, 67, 86, 87], 4);
        data.setUint16(8, 14, true); data.setUint16(10, 20, true); data.setUint16(12, 4, true); data.setUint16(16, 8, true);
        data.setInt32(24, 16, true); data.setUint32(28, 2, true); data.setBigUint64(32, 1n, true);
        return { status: 200, headers: { "ICAX-Resource-Version": "1", "Content-Type": "application/vnd.icax.flatbuffer" },
          body: request.method === "HEAD" ? new ArrayBuffer(0) : bytes.buffer };
      },
      async saveFileDialog() { return "D:/fixtures/relocated-package.ictd"; },
    };
  }, { descriptor, display });
  await page.goto(origin + "/iCAX-UI/SDK/AppShell/index.html");
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().activeProjectId
    && !globalThis.__icaxAppShell.getState().pendingCount && document.querySelector('[data-tube-designer-parameter="productCode"]'), null, { timeout: 20000 });
  assert.equal(await page.title(), "TubeDesigner");
  const tabs = page.locator('[data-action="select-ribbon-tab"]');
  assert.deepEqual((await tabs.allTextContents()).map(value => value.trim()), ["产品", "下料", "加工", "资源库", "关于"]);
  await page.evaluate(() => {
    const f = globalThis.__packageFixture; f.canvas = f.view.viewport.renderer.domElement; f.viewport = f.view.viewport;
    f.canvasEvents = 0; f.canvas.addEventListener("package-probe", () => f.canvasEvents++);
  });
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="machining"]').click();
  assert.ok(await page.locator(".tube-machining-empty-stage").isVisible());
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="about"]').click();
  assert.match(await page.locator(".cam-info-pane").innerText(), /软件授权/);
  await page.screenshot({ path: join(output, "package-about.png") });
  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="view"]').click();
  await page.locator('[data-tube-designer-parameter="productCode"]').fill("PACKAGE-SAVED");
  await page.locator('[data-tube-designer-parameter="productCode"]').dispatchEvent("change");
  await page.keyboard.press("Control+s");
  await page.waitForFunction(() => globalThis.__packageFixture.saved.length === 1
    && !document.querySelector('[data-action="save-project"]')?.disabled);
  result = await page.evaluate(() => {
    const f = globalThis.__packageFixture; f.canvas.dispatchEvent(new Event("package-probe"));
    return { sameCanvas: f.view.viewport.renderer.domElement === f.canvas && f.canvas.isConnected,
      sameViewport: f.view.viewport === f.viewport, canvasEvents: f.canvasEvents,
      savedCode: f.saved[0].tubeDesigner.product.parameters.productCode, nativeRequests: f.nativeRequests,
      activeProjectId: globalThis.__icaxAppShell.getState().activeProjectId };
  });
  assert.equal(result.savedCode, "PACKAGE-SAVED"); assert.equal(result.sameCanvas, true);
  assert.equal(result.sameViewport, true); assert.equal(result.canvasEvents, 1);
  assert.ok(result.nativeRequests.some(request => request.method === "Project.Save" && request.payload.projectPath.endsWith(".ictd")));
  assert.ok(result.nativeRequests.some(request => request.method === "TubeDesigner.UpdateProductParameters"));
  assert.equal(result.nativeRequests.some(request => request.method === "TubeDesigner.GeneratePreview"), false);
  assert.deepEqual(errors, []); assert.deepEqual(failedRequests, []);
  for (const file of ["/iCAX-UI/SDK/AppShell/index.html", "/iCAX-UI/SDK/runtime.mjs", "/iCAX-UI/AppProxy/AppProxy.mjs",
    "/iCAX-UI/ProductProxy/ProductProxy.mjs", "/iCAX-UI/ProjectProxy/ProjectProxy.mjs", "/iCAX-UI/SceneProxy/SceneProxy.mjs",
    "/iCAX-UI/UI/html.mjs", "/apps/tube-designer/webpage/aboutArea.mjs", "/apps/tube-designer/webpage/machiningArea.mjs",
    "/iCAX-UI/SDK/ThirdParty/three/three.module.js", "/iCAX-UI/SDK/ThirdParty/three/three.core.js"]) assert.ok(requests.includes(file), file + " was not loaded from the package");
  assert.ok(requests.every(path => !path.includes("/src/")));
  successfulSync(true);
  await page.screenshot({ path: join(output, "package-product-saved.png") });
  writeFileSync(join(output, "browser-report.json"), JSON.stringify({ runtime, entry: "/iCAX-UI/SDK/AppShell/index.html",
    assetCount: Object.keys(hashes).length, hashes, deploymentLog, rejectionCases,
    requests: [...new Set(requests)], errors, failedRequests, ...result, nativeTransport: "fixture; production SDK/proxies/entry unmodified" }, null, 2));
  console.log("Relocated web package passed: complete SDK/proxy asset hashes, ValidateOnly missing/changed/obsolete rejection, real AppShell startup, five tabs, about/machining, latest draft saved through SDK, same canvas/listener; no source URL fallback.");
} finally {
  await browser.close();
  await new Promise(resolveClosed => server.close(resolveClosed));
}
