// Real native regression for the profile-library preview path.
// It exercises the same GenerateProfilePreview -> ResourceClient -> Three
// viewport sequence used by the product page, rather than a geometry mock.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { copyFileSync, mkdirSync, readFileSync, readdirSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../../../", import.meta.url));
const sourceRoot = resolve(root, "src");
const artifacts = resolve(root, "tmp/profile-library-native-browser");
const runtime = resolve(artifacts, "native");
mkdirSync(runtime, { recursive: true });
const sourceDlls = process.env.ICAX_PROFILE_NATIVE_DLL_DIR || resolve(sourceRoot, "x64/Debug");
for (const name of readdirSync(sourceDlls).filter(name => /\.dll$/i.test(name))) {
  copyFileSync(resolve(sourceDlls, name), resolve(runtime, name));
}
copyFileSync(
  process.env.ICAX_PROFILE_NATIVE_BRIDGE || resolve(root, "tmp/native-layout-tests/DrawingAcceptanceBridge.exe"),
  resolve(runtime, "DrawingAcceptanceBridge.exe"),
);

const bridge = spawn(resolve(runtime, "DrawingAcceptanceBridge.exe"), [], {
  cwd: root,
  windowsHide: true,
  env: { ...process.env, PATH: runtime + ";" + resolve(sourceRoot, "x64/Debug") + ";" + process.env.PATH },
  stdio: ["pipe", "pipe", "pipe"],
});
const waiting = new Map();
let sequence = 0;
let stderr = "";
bridge.stderr.on("data", bytes => { stderr += bytes; });
const failAll = error => {
  for (const request of waiting.values()) {
    clearTimeout(request.timer);
    request.reject(error);
  }
  waiting.clear();
};
bridge.on("error", failAll);
bridge.on("exit", code => {
  if (waiting.size) failAll(new Error(`Native bridge exited ${code}: ${stderr}`));
});
createInterface({ input: bridge.stdout }).on("line", line => {
  let response;
  try { response = JSON.parse(line); } catch {
    failAll(new Error("Invalid native bridge response: " + line));
    return;
  }
  const request = waiting.get(response.id);
  if (!request) return;
  waiting.delete(response.id);
  clearTimeout(request.timer);
  if (response.ok) request.resolve(response.result);
  else request.reject(new Error(response.error));
});
const rpc = message => new Promise((resolveResult, reject) => {
  const id = ++sequence;
  const timer = setTimeout(() => {
    waiting.delete(id);
    reject(new Error(`Native request timed out: ${message.action}/${message.method ?? ""}`));
  }, 120000);
  waiting.set(id, { timer, reject, resolve: resolveResult });
  bridge.stdin.write(JSON.stringify({ id, ...message }) + "\n");
});

const { chromium } = await import(
  process.env.ICAX_PLAYWRIGHT_MODULE
    || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs",
);
const browser = await chromium.launch({
  headless: true,
  ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}),
});

try {
  await rpc({ action: "reset" });
  const descriptor = JSON.parse(readFileSync(
    resolve(sourceRoot, "apps/tube-designer/templates/profile/round/profile.json"),
    "utf8",
  ));
  const defaultParameters = Object.fromEntries(
    descriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]),
  );
  const evaluated = await rpc({
    action: "invoke",
    method: "EvaluateProfilePackage",
    payload: { profileRef: { scope: "system", id: "round" }, parameters: defaultParameters },
  });
  const profile = {
    id: "round",
    name: "圆管",
    profileType: "parametric-package",
    libraryScope: "system",
    descriptor,
    defaultParameters,
    previewProfile: evaluated.profile,
  };
  const { readFileSync: readSource } = await import("node:fs");
  const page = await browser.newPage({ viewport: { width: 1200, height: 760 } });
  page.setDefaultTimeout(45000);
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.exposeFunction("profileNativeRpc", rpc);
  await page.route("**/*", route => {
    const url = new URL(route.request().url());
    if (url.origin !== "http://profile-library-native.test") return route.abort();
    if (url.pathname === "/") {
      return route.fulfill({
        contentType: "text/html",
        body: "<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\"><body><div id=\"app\"></div></body></html>",
      });
    }
    const file = resolve(sourceRoot, url.pathname.replace(/^\/src\//, ""));
    if (!url.pathname.startsWith("/src/") || !file.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(file)) {
      return route.abort();
    }
    return route.fulfill({ contentType: "text/javascript", body: readSource(file, "utf8") });
  });
  await page.goto("http://profile-library-native.test/");
  await page.evaluate(async ({ profile }) => {
    const entry = await import("/src/apps/tube-designer/webpage/entry.mjs");
    const { getProjectView } = await import("/src/apps/_shared/workbench/state/projectViewStore.mjs");
    await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const mount = document.querySelector("#app");
    mount.dataset.productSurface = "project";
    mount.innerHTML = `
      <main style="width:100%;height:100%"></main>`;
    const calls = [];
    const resources = [];
    const invoke = async (method, payload) => {
      const result = await window.profileNativeRpc({
        action: "invoke",
        method: method.replace(/^TubeDesigner\./, ""),
        payload,
      });
      calls.push({ method, payload, result });
      return result;
    };
    const resourceClient = {
      async get(url, options) {
        const version = Number(options?.headers?.get("ICAX-Resource-Version") ?? 0);
        const result = await window.profileNativeRpc({ action: "resource", payload: { url, version } });
        resources.push({ url, version, bytes: result.bytes });
        return new Response(Uint8Array.from(atob(result.base64), character => character.charCodeAt(0)));
      },
    };
    const sceneProxy = { invoke, resources: resourceClient };
    sceneProxy.pdo = { enabled: false };
    const project = { projectId: "profile-library-native-project", projectName: "管型库真实预览" };
    const view = getProjectView(project.projectId);
    view.activeAreaId = "profiles";
    view.scene = { undoRedo: {}, tubeDesigner: {} };
    view.tubeDesignerLoaded = true;
    view.tubeDesignerUserDataLoaded = true;
    view.tubeDesignerSystemProfiles = [profile];
    view.tubeDesignerTemplateProfiles = [];
    view.tubeDesignerUserData = { profiles: [] };
    view.tubeDesignerSelectedProfileId = "system:round";
    const context = {
      mount,
      sceneProxy,
      project,
      product: { productId: "icax.tube-designer", productName: "TubeDesigner" },
      scene: view.scene,
      activeRibbonTabId: "profiles",
      productProxy: { async invoke() { throw new Error("Unexpected product call in profile preview test"); } },
      actions: { log() {} },
    };
    window.profileNativeFixture = { calls, resources, errors: [], view, context, entry };
    await entry.mountProject(context);
  }, { profile });
  try {
    await page.waitForFunction(() => {
      const fixture = window.profileNativeFixture;
      return !fixture.view.tubeDesignerProfilePreviewRequest
        && fixture.view.viewport.getAppliedViewState().entityIds.includes("system:round");
    });
  } catch (error) {
    const state = await page.evaluate(() => {
      const fixture = window.profileNativeFixture;
      return {
        request: Boolean(fixture.view.tubeDesignerProfilePreviewRequest),
        preview: fixture.view.tubeDesignerProfilePreview,
        error: fixture.view.error,
        calls: fixture.calls.map(call => ({ method: call.method, error: call.error, result: call.result })),
        resources: fixture.resources,
        applied: fixture.view.viewport?.getAppliedViewState?.(),
        pageErrors: fixture.errors,
      };
    });
    throw new Error(error.message + "\n" + JSON.stringify(state));
  }
  const result = await page.evaluate(() => {
    const fixture = window.profileNativeFixture;
    const applied = fixture.view.viewport.getAppliedViewState();
    const object = fixture.view.viewport.sceneObjects.get("system:round");
    return {
      calls: fixture.calls.map(call => ({ method: call.method, geometry: call.result?.geometryResourceId })),
      resources: fixture.resources,
      error: fixture.view.error ?? "",
      revision: applied.revision,
      entityIds: applied.entityIds,
      visible: object?.visible === true,
      positionCount: object?.geometry?.getAttribute("position")?.count ?? 0,
      status: document.querySelector("[data-tube-profile-preview-status]")?.innerText ?? "",
    };
  });
  assert.deepEqual(errors, [], JSON.stringify(errors));
  assert.equal(result.error, "", JSON.stringify(result));
  assert.equal(result.calls.length, 1, JSON.stringify(result));
  assert.equal(result.calls[0].method, "TubeDesigner.GenerateProfilePreview");
  assert.ok(result.resources.some(resource => resource.bytes > 0), JSON.stringify(result));
  assert.deepEqual(result.entityIds, ["system:round"], JSON.stringify(result));
  assert.equal(result.visible, true, JSON.stringify(result));
  assert.ok(result.positionCount > 0, JSON.stringify(result));
  assert.match(result.status, /三维管型已生成|圆管/);
  console.log("TubeDesigner profile-library native browser regression passed.");
} finally {
  await browser.close();
  bridge.stdin.end();
}
