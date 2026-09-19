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
  const omegaDescriptor = JSON.parse(readFileSync(
    resolve(sourceRoot, "apps/tube-designer/templates/profile/omega/profile.json"),
    "utf8",
  ));
  const omegaHotParameters = Object.fromEntries(
    omegaDescriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]),
  );
  omegaHotParameters.useHotRolled = true;
  const omegaHotPreview = await rpc({
    action: "invoke",
    method: "GenerateProfilePreview",
    payload: {
      profileRef: { scope: "system", id: "omega" },
      parameters: omegaHotParameters,
      length: 100,
    },
  });
  assert.ok(omegaHotPreview.geometryResourceId, JSON.stringify(omegaHotPreview));
  assert.equal(omegaHotPreview.profile.parameters.useHotRolled, true);

  const angleDescriptor = JSON.parse(readFileSync(
    resolve(sourceRoot, "apps/tube-designer/templates/profile/angle/profile.json"),
    "utf8",
  ));
  const angleMirrorParameters = Object.fromEntries(
    angleDescriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]),
  );
  angleMirrorParameters.mirrorX = true;
  const angleMirrorPreview = await rpc({
    action: "invoke",
    method: "GenerateProfilePreview",
    payload: {
      profileRef: { scope: "system", id: "angle" },
      parameters: angleMirrorParameters,
      length: 100,
    },
  });
  assert.ok(angleMirrorPreview.geometryResourceId, JSON.stringify(angleMirrorPreview));
  assert.equal(angleMirrorPreview.profile.parameters.mirrorX, true);

  const angleIndependentRadiusParameters = { ...angleMirrorParameters,
    mirrorX: false,
    outerRadius: 0,
    useInnerRadius: true,
    innerRadius: 10,
  };
  const angleIndependentRadiusPreview = await rpc({
    action: "invoke",
    method: "GenerateProfilePreview",
    payload: {
      profileRef: { scope: "system", id: "angle" },
      parameters: angleIndependentRadiusParameters,
      length: 100,
    },
  });
  assert.ok(angleIndependentRadiusPreview.geometryResourceId, JSON.stringify(angleIndependentRadiusPreview));
  assert.equal(angleIndependentRadiusPreview.profile.parameters.outerRadius, 0);
  assert.equal(angleIndependentRadiusPreview.profile.parameters.innerRadius, 10);

  const polygonBarDescriptor = JSON.parse(readFileSync(
    resolve(sourceRoot, "apps/tube-designer/templates/profile/polygon-bar/profile.json"),
    "utf8",
  ));
  const polygonBarParameters = Object.fromEntries(
    polygonBarDescriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]),
  );
  Object.assign(polygonBarParameters, { radius: 37, sideCount: 7, cornerRadius: 2.5 });
  const polygonBarPreview = await rpc({
    action: "invoke",
    method: "GenerateProfilePreview",
    payload: {
      profileRef: { scope: "system", id: "polygon-bar" },
      parameters: polygonBarParameters,
      length: 100,
    },
  });
  assert.ok(polygonBarPreview.geometryResourceId, JSON.stringify(polygonBarPreview));
  assert.deepEqual(polygonBarPreview.profile.parameters, polygonBarParameters);
  assert.equal(
    polygonBarPreview.profile.contours[0].segments.filter(edge => edge.kind === "arc").length,
    7,
  );

  const descriptor = JSON.parse(readFileSync(
    resolve(sourceRoot, "apps/tube-designer/templates/profile/round/profile.json"),
    "utf8",
  ));
  const defaultParameters = Object.fromEntries(
    descriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]),
  );
  descriptor.display = JSON.parse(readFileSync(resolve(sourceRoot, 'apps/tube-designer/templates/profile/round/display.json'), 'utf8'));
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
  const result = await page.evaluate(async () => {
    const fixture = window.profileNativeFixture;
    const applied = fixture.view.viewport.getAppliedViewState();
    const object = fixture.view.viewport.sceneObjects.get("system:round");
    const parameterList = document.querySelector(".tube-profile-library-parameter-list");
    const diagramToggle = document.querySelector("[data-profile-diagram-toggle]");
    const parameterDiagram = document.querySelector("[data-profile-library-diagram]");
    const parameterListBox = parameterList?.getBoundingClientRect();
    const diagramToggleBox = diagramToggle?.getBoundingClientRect();
    const diagramToggleStyle = diagramToggle ? getComputedStyle(diagramToggle) : null;
    const diagramInitiallyHidden = parameterDiagram?.hidden === true;
    diagramToggle?.click();
    await new Promise(resolve => requestAnimationFrame(resolve));
    return {
      calls: fixture.calls.map(call => ({ method: call.method, geometry: call.result?.geometryResourceId })),
      resources: fixture.resources,
      error: fixture.view.error ?? "",
      revision: applied.revision,
      entityIds: applied.entityIds,
      visible: object?.visible === true,
      positionCount: object?.geometry?.getAttribute("position")?.count ?? 0,
      status: document.querySelector("[data-tube-profile-preview-status]")?.innerText ?? "",
      diagram: {
        initiallyHidden: diagramInitiallyHidden,
        inScene: !!diagramToggle?.closest('.cam-viewport'),
        height: Number(diagramToggleBox?.height ?? 0),
        borderWidth: diagramToggleStyle?.borderTopWidth ?? "",
        borderColor: diagramToggleStyle?.borderTopColor ?? "",
        borderRadius: diagramToggleStyle?.borderTopLeftRadius ?? "",
        backgroundColor: diagramToggleStyle?.backgroundColor ?? "",
        boxShadow: diagramToggleStyle?.boxShadow ?? "",
        expanded: diagramToggle?.getAttribute("aria-expanded") === "true" && parameterDiagram?.hidden === false,
      },
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
  assert.equal(result.diagram.inScene, false, 'Profile-library section diagram has been replaced by scene annotations');
  await page.waitForFunction(() => document.querySelectorAll('.icax-three-specification-trigger').length >= 2);
  const annotation = page.locator('[data-tube-designer-parameter-key="width"].icax-three-specification-trigger');
  const {x,y} = await annotation.evaluate(node => {
    const rect=node.closest('.icax-three-specification-label').getBoundingClientRect();
    const others=[...document.querySelectorAll('.icax-three-specification-trigger')].filter(n=>n!==node).map(n=>n.closest('.icax-three-specification-label').getBoundingClientRect());
    for(let x=rect.left+2;x<rect.right-1;x+=4) for(let y=rect.top+2;y<rect.bottom-1;y+=4) {
      if(!others.some(r=>x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom)) return {x,y};
    }
    throw Error('No unoccluded hit target for width annotation');
  });
  await page.mouse.move(x,y);
  assert.equal(await page.evaluate(({x,y})=>document.elementFromPoint(x,y)?.tagName,{x,y}), 'CANVAS', 'Resting labels pass input to the camera');
  await page.mouse.dblclick(x,y);
  const sceneInput = page.locator('[data-tube-designer-scene-specification-input="width"]');
  await sceneInput.fill('55', {timeout:6000});
  await sceneInput.press('Enter');
  await page.waitForFunction(() => {
    const f=window.profileNativeFixture;
    return f.calls.length === 2 && !f.view.tubeDesignerProfilePreviewRequest && f.view.tubeDesignerProfilePreview?.response?.profile?.parameters.width === 55;
  });
  assert.equal(await page.locator('[data-tube-profile-scene-runtime]').count(), 0);
  assert.equal(await page.locator('.icax-three-specification-label.is-pending').count(), 0);
  console.log("TubeDesigner profile-library native browser regression passed.");
} finally {
  await browser.close();
  bridge.stdin.end();
}
