// Dedicated drawing UI -> production SDO -> real resources -> .ictd round trip.
// Native DLLs are copied to an isolated directory so this test never locks a build.
import assert from "node:assert/strict";
import { spawn, execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { createInterface } from "node:readline";
import { copyFileSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const root = fileURLToPath(new URL("../../../", import.meta.url)), sourceRoot = resolve(root, "src");
const artifacts = resolve(root, process.env.ICAX_ARTIFACT_DIR || "output/tests/part-drawing-native-browser"), runtime = resolve(artifacts, "native");
mkdirSync(runtime, { recursive: true });
const sourceDlls = process.env.ICAX_DRAWING_NATIVE_DLL_DIR || resolve(sourceRoot, "x64/Debug");
for (const name of readdirSync(sourceDlls).filter(name => /\.dll$/i.test(name))) copyFileSync(resolve(sourceDlls, name), resolve(runtime, name));
copyFileSync(process.env.ICAX_DRAWING_NATIVE_BRIDGE || resolve(root, "output/tests/side-sketch-native-browser/native/SideSketchAcceptanceBridge.exe"), resolve(runtime, "DrawingAcceptanceBridge.exe"));
const bridge = spawn(resolve(runtime, "DrawingAcceptanceBridge.exe"), [], { cwd: root, windowsHide: true,
  env: { ...process.env, PATH: runtime + ";" + resolve(sourceRoot, "x64/Debug") + ";" + process.env.PATH }, stdio: ["pipe", "pipe", "pipe"] });
const waiting = new Map(), requests = [];
let sequence = 0, stderr = "", browser, page;
bridge.stderr.on("data", bytes => { stderr += bytes; });
const failAll = error => { for (const request of waiting.values()) { clearTimeout(request.timer); request.reject(error); } waiting.clear(); };
bridge.on("error", failAll); bridge.on("exit", code => { if (waiting.size) failAll(new Error(`Native bridge exited ${code}: ${stderr}`)); });
createInterface({ input: bridge.stdout }).on("line", line => {
  let response; try { response = JSON.parse(line); } catch { failAll(new Error("Invalid native bridge response: " + line)); return; }
  const request = waiting.get(response.id); if (!request) return;
  waiting.delete(response.id); clearTimeout(request.timer);
  if (response.ok) request.resolve(response.result); else request.reject(new Error(response.error));
});
const rpc = message => new Promise((complete, reject) => {
  const id = ++sequence, record = { id, action: message.action, method: message.method, started: Date.now() }; requests.push(record);
  const timer = setTimeout(() => { waiting.delete(id); reject(new Error(`Native request timed out: ${message.action}/${message.method}`)); }, 120000);
  waiting.set(id, { timer, reject, resolve(result) { record.elapsed = Date.now() - record.started; complete(result); } });
  bridge.stdin.write(JSON.stringify({ id, ...message }) + "\n");
});
const invoke = (method, payload = {}, scope = "scene") => rpc({ action: "invoke", scope, method, payload });

try {
  await rpc({ action: "reset" });
  const loadedModules=JSON.parse(execFileSync("powershell.exe",["-NoProfile","-Command",
    `(Get-Process -Id ${bridge.pid} -ErrorAction Stop).Modules | Where-Object { $_.ModuleName -in @('TubeDesigner.dll','OpenCascadeResourceImport.dll') } | ForEach-Object { [pscustomobject]@{name=$_.ModuleName;path=$_.FileName} } | ConvertTo-Json -Compress`],{encoding:"utf8"}));
  const nativeModules=Object.fromEntries(loadedModules.map(module=>[module.name,{path:module.path,
    sha256:createHash("sha256").update(readFileSync(module.path)).digest("hex")}]));
  for(const name of ["TubeDesigner.dll","OpenCascadeResourceImport.dll"]) {
    assert.equal(resolve(nativeModules[name].path).toLowerCase(),resolve(runtime,name).toLowerCase());
    assert.equal(nativeModules[name].sha256,createHash("sha256").update(readFileSync(resolve(sourceDlls,name))).digest("hex"));
  }
  const profiles=[];
  for(const id of ["round","rect"]) {
    const descriptor = JSON.parse(readFileSync(resolve(sourceRoot, `apps/tube-designer/templates/profile/${id}/profile.json`), "utf8"));
    const defaultParameters = Object.fromEntries(descriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]));
    const evaluated = await invoke("EvaluateProfilePackage", { profileRef: { scope: "system", id }, parameters: defaultParameters }, "product");
    profiles.push({id,name:id==="round"?"圆管":"矩管",descriptor,defaultParameters,previewProfile:evaluated.profile});
  }
  await assert.rejects(() => invoke("PreviewPartDrawing", {}, "product"), /请求无效|scene/i,
    "The product proxy must reject a scene CAD edit request");
  const scene = await invoke("List");
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } }); page.setDefaultTimeout(45000);
  const errors = [], external = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.exposeFunction("drawingNativeRpc", rpc);
  await page.route("**/*", route => {
    const url = new URL(route.request().url());
    if (url.origin !== "http://drawing-native.test") { external.push(url.href); return route.abort(); }
    if (url.pathname === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body></html>' });
    const file = resolve(sourceRoot, url.pathname.replace(/^\/src\//, ""));
    if (!url.pathname.startsWith("/src/") || !file.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://drawing-native.test");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}" + readFileSync(resolve(sourceRoot, "apps/_shared/workbench/styles/workbench.css"), "utf8") + tubeDesignerCss });
  await page.evaluate(async ({ profiles, scene }) => {
    const drawing = await import("/src/apps/tube-designer/webpage/partDrawing.mjs");
    const preview = await import("/src/apps/tube-designer/webpage/partDrawingPreview.mjs");
    const dom = await import("/src/apps/tube-designer/webpage/partDrawingDom.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { renderDesignerOperationOverlay } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const mount = document.querySelector("#app");
    const sentinel = { unchanged: "independent machining state" };
    const view = { activeAreaId: "nesting", pending: false, scene, tubeDesignerSystemProfiles: profiles,
      tubeDesignerSelectedProfileId: "system:round", tubeDesignerTemplateProfiles: [], tubeDesignerUserData: { profiles: [] }, tubeDesignerPunchWizard: sentinel };
    const f = window.fixture = { view, drawing, preview, pending: 0, calls: [], resources: [], failures: [], fullRenders: 0, patches: 0, sentinel };
    f.stockFraming = () => {
      const viewport = f.viewport, object = viewport.sceneObjects.get("drawing:base"), points = object.geometry.getAttribute("position");
      const projected = Array.from({ length: points.count }, (_, index) => new THREE.Vector3(points.getX(index), points.getY(index), points.getZ(index)).applyMatrix4(object.matrixWorld).project(viewport.camera));
      const range = axis => [Math.min(...projected.map(point => point[axis])), Math.max(...projected.map(point => point[axis]))];
      const x = range("x"), y = range("y");
      return { camera: viewport.getCameraState(), x, y, widthFraction: (x[1] - x[0]) / 2, heightFraction: (y[1] - y[0]) / 2 };
    };
    Object.defineProperty(f, "state", { get() { return view.tubeDesignerPartDrawing?.state; } });
    const invoke = scope => async (method, payload, options) => {
      const record = { scope, method, payload: structuredClone(payload) }; f.calls.push(record);
      options?.onReport?.({ message: "正在运行真实三维建模", completed: 0, total: 1 });
      try { const result = await window.drawingNativeRpc({ action: "invoke", scope, method: method.replace(/^TubeDesigner\./, ""), payload }); record.result = result; return result; }
      catch (error) { record.error = error.message; throw error; }
    };
    const resources = { async get(url, options) {
      const version = Number(options?.headers?.get("ICAX-Resource-Version") || 0);
      const result = await window.drawingNativeRpc({ action: "resource", payload: { url, version } });
      f.resources.push({ url, version, bytes: result.bytes });
      return new Response(Uint8Array.from(atob(result.base64), char => char.charCodeAt(0)));
    } };
    const context = f.context = { mount, sceneProxy: { invoke:invoke("scene"), resources }, productProxy: { invoke:invoke("product") } };
    f.parts = () => view.scene.tubeDesigner?.nestingGroups?.flatMap(group => group.parts ?? []) ?? [];
    const ops = f.ops = { createPartDrawingViewport(options) { return f.viewport = createThreeViewport(options); }, showNotice() {}, renderProject() {
      const html = drawing.renderPartDrawingDialog(view) + renderDesignerOperationOverlay(context, view);
      if (dom.patchPartDrawingDom(view, mount, html)) f.patches++;
      else {
        f.fullRenders++;
        mount.innerHTML = '<main id="main-workbench"><button data-cam-action="tube-designer-drawing-open">新建三维零件</button>'
          + f.parts().map(part => `<button data-cam-action="tube-designer-drawing-open" data-tube-designer-part-id="${part.entityId}">编辑已保存零件</button>`).join("") + "</main>" + html;
        dom.rememberPartDrawingDom(view, mount);
      }
      preview.attachPartDrawingPreview(context, view, mount, ops);
      drawing.attachPartDrawingEditor(context, view, mount, ops);
    } };
    f.dispatch = async (action, target = {}) => {
      if (!action?.startsWith("tube-designer-drawing-")) return;
      f.pending++;
      try { return await drawing.handlePartDrawingAction(context, view, action, target, ops); }
      catch (error) { f.failures.push({ action, error: error.message }); }
      finally { f.pending--; }
    };
    document.addEventListener("change", event => void f.dispatch(event.target.dataset.camChangeAction, event.target));
    document.addEventListener("click", event => { const target = event.target.closest("[data-cam-action]"); if (target && !target.disabled) void f.dispatch(target.dataset.camAction, target); });
    f.render = ops.renderProject; f.render();
  }, { profiles, scene });
  const action = name => page.locator(`[data-cam-action="tube-designer-drawing-${name}"]`);
  const command = name => page.locator(`[data-drawing-command="${name}"]`);
  const ready = async () => {
    try {
      await page.waitForFunction(() => {
        const f = window.fixture, s = f.state;
        return !!s && !f.pending && !f.view.pending && !s.previewPending && !s.previewRenderPending
          && s.preview?.revision === s.revision && document.querySelector("[data-part-drawing-preview-ready=true]");
      });
    } catch (error) {
      throw new Error(error.message + "\n" + await page.evaluate(() => JSON.stringify({ state: window.fixture.state,
        calls: window.fixture.calls.map(call => ({ method: call.method, error: call.error })), failures: window.fixture.failures })), { cause: error });
    }
    assert.equal(await page.evaluate(() => window.fixture.state.error), "");
  };
  const idle = () => page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending);
  const screenshot = name => page.screenshot({ path: resolve(artifacts, name + ".png") });
  const field = name => page.locator(`[data-tube-designer-punch-field="${name}"]`);
  const parameter = name => page.locator(`[data-tube-designer-punch-parameter="${name}"]`);
  const input = async (locator, value) => { await locator.fill(String(value)); await locator.press("Tab"); await ready(); };
  const sideFraming = async () => {
    const framing = await page.evaluate(() => window.fixture.stockFraming());
    assert.equal(framing.camera.projectionMode, "orthographic");
    assert.ok(Math.abs(framing.camera.theta + Math.PI / 2) < 1e-8);
    assert.ok(Math.abs(framing.camera.phi - Math.PI / 2) < 1e-8);
    assert.ok(Math.abs(framing.widthFraction - 0.9) < 0.005, JSON.stringify(framing));
    assert.ok(framing.x[0] >= -0.905 && framing.x[1] <= 0.905 && framing.heightFraction < 0.9, JSON.stringify(framing));
    return framing;
  };

  await page.getByRole("button", { name: "新建三维零件", exact: true }).click(); await ready();
  assert.equal(await page.evaluate(()=>window.fixture.state.previewMode),"finished");
  assert.ok(await page.evaluate(()=>window.fixture.state.preview.geometry?.url));
  await page.locator('[data-drawing-preview-mode="tools"]').click();await ready();
  const initialFraming = await sideFraming();
  await page.evaluate(() => {
    const f = window.fixture; f.canvas = document.querySelector(".icax-three-viewport-canvas"); f.workbench = document.querySelector("#main-workbench");
    f.baseReference=structuredClone(f.state.preview.baseGeometry);f.baseUrl=f.baseReference.url;
    f.base = f.viewport.geometryObjects.get(f.baseUrl); f.baseObject = f.viewport.sceneObjects.get("drawing:base"); f.camera = f.viewport.getCameraState();
  });
  const beforePartWrites = await page.evaluate(() => window.fixture.calls.filter(call => /\.(Add|Apply)PartDrawing$/.test(call.method)).length);
  await command("branch").click(); await ready();
  assert.equal(await page.locator('[data-drawing-section="branch"][data-cam-change-action$="section-select"]').inputValue(), "system:round");
  assert.equal(await page.locator('[data-drawing-section="branch"][data-drawing-parameter="width"]').inputValue(), "40");
  await input(field("station"), 180);
  assert.deepEqual(await page.evaluate(() => {
    const f = window.fixture; return { canvas: f.canvas === document.querySelector(".icax-three-viewport-canvas"),
      workbench: f.workbench === document.querySelector("#main-workbench"), base: f.base === f.viewport.geometryObjects.get(f.baseUrl),
      baseObject: f.baseObject === f.viewport.sceneObjects.get("drawing:base"), camera: JSON.stringify(f.camera) === JSON.stringify(f.viewport.getCameraState()),
      noResult: f.state.preview.geometry === undefined, toolsOnly: f.state.preview.toolsOnly, liveFeatures: f.state.features.length };
  }), { canvas: true, workbench: true, base: true, baseObject: true, camera: true, noResult: false, toolsOnly: false, liveFeatures: 1 },
  JSON.stringify(await page.evaluate(() => ({ before:window.fixture.baseReference,after:window.fixture.state.preview.baseGeometry }))));
  await screenshot("01-default-round-branch40-cad-preview");
  assert.equal(await page.locator('[role="treeitem"]').count(), 2);
  assert.equal(await page.evaluate(() => window.fixture.state.features[0].id === window.fixture.state.preview.toolPreviews[0].key), true);

  // The default diameter-40 cutter is allowed throughout editing. Use a smaller
  // through hole for the final connected-part persistence acceptance.
  await page.locator('[role="treeitem"]').nth(1).click();
  await input(page.locator('[data-drawing-section="branch"][data-drawing-parameter="width"]'), 20);
  await screenshot("02-branch20-ready-for-final-confirm");

  if(process.env.ICAX_DRAWING_NATIVE_CACHE_SMOKE==="1") {
    const payloads=await page.evaluate(()=>({
      zero:structuredClone(window.fixture.calls.find(call=>call.method==="TubeDesigner.PreviewPartDrawing"&&!call.payload.features.length).payload),
      cut:structuredClone(window.fixture.calls.findLast(call=>call.method==="TubeDesigner.PreviewPartDrawing").payload)
    }));
    const key=`cache-smoke-${Date.now()}`;payloads.zero.previewResourceKey=key;payloads.cut.previewResourceKey=key;
    const snapshots=[];
    for(const payload of [payloads.zero,payloads.zero,payloads.cut,payloads.zero,payloads.zero]) {
      const result=await invoke("PreviewPartDrawing",payload);
      assert.equal(result.resultValid,true);assert.equal(result.previewComputed,true);
      assert.ok(result.topology?.edges?.length&&result.topology?.faces?.length);
      assert.ok(result.geometry?.url&&result.baseGeometry?.url);
      snapshots.push({cut:!!payload.features.length,revision:result.topology.revision,edges:result.topology.edges.length,
        faces:result.topology.faces.length,baseGeometry:result.baseGeometry,geometry:result.geometry});
    }
    for(const index of [1,3,4]) {
      assert.equal(snapshots[index].revision,snapshots[0].revision,"Cached pristine base topology survives repeated zero-cut and intervening cut previews");
      assert.equal(snapshots[index].edges,snapshots[0].edges);assert.equal(snapshots[index].faces,snapshots[0].faces);
      assert.deepEqual(snapshots[index].baseGeometry,snapshots[0].baseGeometry);
    }
    assert.notEqual(snapshots[2].revision,snapshots[0].revision);
    const released=await invoke("ReleasePartDrawingPreview",{previewResourceKey:key});assert.equal(released.released,true);
    await page.getByRole("button",{name:"取消",exact:true}).click();await idle();
    writeFileSync(resolve(artifacts,"cache-smoke-results.json"),JSON.stringify({status:"passed",nativeModules,snapshots,
      checks:["Fresh production browser starts with finished model and retains the stock GPU object, canvas, workbench and camera while editing a real branch",
        "One native scope repeats zero-cut previews, applies a cut, and returns to its pristine topology and stable base resource"],
      scope:"Actual native SDO and production DOM/WebGL browser fixture; standalone CEF application was not controlled."},null,2));
    console.log("Actual native cache + fresh drawing browser smoke passed. Artifacts: "+artifacts);
  } else {
  // Slot templates consume an actual flat wall. Continue through the main
  // section selector to a rectangle rather than supplying obsolete V enums.
  await page.locator('[data-drawing-node="main"]').click();
  await page.locator('[data-drawing-section="main"][data-cam-change-action$="section-select"]').selectOption("system:rect");await ready();
  await page.evaluate(() => {
    const f=window.fixture;f.baseReference=structuredClone(f.state.preview.baseGeometry);f.baseUrl=f.baseReference.url;
    f.base=f.viewport.geometryObjects.get(f.baseUrl);f.baseObject=f.viewport.sceneObjects.get("drawing:base");f.camera=f.viewport.getCameraState();
  });

  // Use actual native CSG, BRep persistence and GPU resources for every V type.
  // In particular, a release hole produces nested compounds in its cutter;
  // merely returning a nonempty resource URL is not evidence of visible geometry.
  await page.evaluate(() => {
    const f = window.fixture, branch = f.state.preview.toolPreviews.find(tool => tool.key === f.state.features[0].id);
    f.keptBranchUrl = branch.geometry.url; f.keptBranch = f.viewport.geometryObjects.get(f.keptBranchUrl);
    f.keptBranchObject = f.viewport.sceneObjects.get(`drawing:feature:${branch.key}`);
  });
  await command("part").click(); await idle();
  const vNativeCases = [];
  const configurations=[
    {style:"sharp_v",tool:"v-notch-sharp",parameters:{bottomStrategy:"sharp",asymmetric:false}},
    {style:"asymmetric_v",tool:"v-notch-sharp",parameters:{asymmetric:true,leftAngle:35,rightAngle:55}},
    {style:"rounded_v",tool:"v-notch-sharp",parameters:{asymmetric:false,bottomStrategy:"rounded"}},
    {style:"left_arc",tool:"edge-arc-groove",parameters:{leftArc:true}},
    {style:"right_arc",tool:"edge-arc-groove",parameters:{leftArc:false}},
    {style:"flat_v",tool:"v-notch-sharp",parameters:{bottomStrategy:"flat",flatWidth:4}},
    {style:"relief_v",tool:"v-notch-sharp",parameters:{bottomStrategy:"relief",reliefShape:"roundedRectangle"}}
  ];
  for (const {style,tool,parameters} of configurations) {
    if(await field("tool").inputValue()!==tool){await field("tool").selectOption(tool);await ready();}
    for(const [key,value] of Object.entries(parameters)) {
      if(typeof value==="boolean") {await parameter(key).setChecked(value);await ready();}
      else if(typeof value==="number") await input(parameter(key),value);
      else {await parameter(key).selectOption(value);await ready();}
    }
    const record = await page.evaluate(() => {
      const f = window.fixture, s = f.state, tool = s.preview.toolPreviews.find(tool => tool.key === s.draft.id);
      const object = f.viewport.sceneObjects.get(`drawing:feature:${s.draft.id}`), geometry = f.viewport.geometryObjects.get(tool?.geometry?.url);
      return { toolId:s.draft.toolRef.id,parameters:structuredClone(s.draft.toolParameters), reference: tool?.geometry, positionCount: geometry?.getAttribute("position")?.count ?? 0,
        indexCount: geometry?.index?.count ?? 0, objectVisible: object?.visible === true,
        baseGeometrySame: f.base === f.viewport.geometryObjects.get(f.baseUrl), baseObjectSame: f.baseObject === f.viewport.sceneObjects.get("drawing:base"),
        branchGeometrySame: f.keptBranch === f.viewport.geometryObjects.get(f.keptBranchUrl),
        branchObjectSame: f.keptBranchObject === f.viewport.sceneObjects.get(`drawing:feature:${s.features[0].id}`),
        canvasSame: f.canvas === document.querySelector(".icax-three-viewport-canvas"), workbenchSame: f.workbench === document.querySelector("#main-workbench"),
        cameraSame: JSON.stringify(f.camera) === JSON.stringify(f.viewport.getCameraState()), toolsOnly: s.preview.toolsOnly,
        hasBooleanResult: s.preview.geometry !== undefined, partWrites: f.calls.filter(call => /\.(Add|Apply)PartDrawing$/.test(call.method)).length };
    });
    record.style=style;assert.equal(record.toolId,tool);for(const [key,value] of Object.entries(parameters))assert.equal(record.parameters[key],value);
    assert.ok(record.positionCount > 0 && record.indexCount > 0, JSON.stringify(record));
    for (const property of ["objectVisible", "baseGeometrySame", "baseObjectSame", "branchGeometrySame", "branchObjectSame", "canvasSame", "workbenchSame", "cameraSame"])
      assert.equal(record[property], true, `${style}: ${property}`);
    assert.equal(record.hasBooleanResult, true);assert.equal(record.toolsOnly,false); assert.equal(record.partWrites, beforePartWrites);
    vNativeCases.push(record);
  }
  await screenshot("04-real-native-relief-v-preview");
  assert.equal(await page.locator('[role="treeitem"]').count(), 3);
  assert.equal(await page.evaluate(() => window.fixture.state.features[1].toolParameters.bottomStrategy), "relief");
  await action("apply").click(); await idle();
  const created = await page.evaluate(() => window.fixture.calls.findLast(call => call.method === "TubeDesigner.AddPartDrawing"));
  assert.ok(created?.result?.partEntityId, JSON.stringify({ method: created?.method, error: created?.error, resultKeys: Object.keys(created?.result ?? {}) })); assert.equal(created.error, undefined);
  assert.equal(created.payload.toolsOnly, undefined); assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPartDrawing), null);
  const firstPart = await page.evaluate(id => window.fixture.parts().find(part => part.entityId === id), created.result.partEntityId);
  assert.ok(firstPart.properties["tubeDesigner.cadDocument"]?.documentResourceId, "Creation publishes a neutral CAD document");
  assert.equal(firstPart.properties["tubeDesigner.partDrawing"],undefined,"The snapshot does not persist an independent drawing recipe");
  assert.equal(created.payload.features[0].section.parameters.width,20);
  assert.equal(created.payload.features[0].station,180);
  assert.equal(created.payload.features.length,2);
  assert.equal(created.payload.features[1].toolParameters.bottomStrategy,"relief");
  const createdDocument=await invoke("GetTubeCadDocument",{partEntityId:firstPart.entityId,resourceVersion:firstPart.manufacturingGeometryResourceVersion});
  assert.ok(createdDocument.geometry.SolidNodes.length>2);
  assert.ok(createdDocument.geometry.SolidNodes.some(node=>node.ID===createdDocument.geometry.BaseNodeID));

  const saved = await rpc({ action: "save", payload: { file: "dedicated-part-drawing-native.ictd", name: "独立三维绘制贯通验收" } });
  assert.equal(saved.saved, true);
  const reopened = await rpc({ action: "open", payload: { file: "dedicated-part-drawing-native.ictd" } });
  assert.equal(reopened.opened, true);
  await page.evaluate(scene => { const f = window.fixture; f.view.scene = scene; f.render(); }, reopened.snapshot);
  const reopenedPart=reopened.snapshot.tubeDesigner.nestingGroups.flatMap(group=>group.parts??[]).find(part=>part.entityId===firstPart.entityId);
  assert.ok(reopenedPart.properties["tubeDesigner.cadDocument"]?.documentResourceId);
  const reopenedDocument=await invoke("GetTubeCadDocument",{partEntityId:firstPart.entityId,resourceVersion:reopenedPart.manufacturingGeometryResourceVersion});
  assert.deepEqual(reopenedDocument.geometry,createdDocument.geometry,"SDO save/open retains the complete created branch and V-relief CAD graph");
  assert.equal(reopenedDocument.revision,createdDocument.revision);
  // This fixture covers creation and persistence only. Saved 2D/3D workbench
  // routing is covered by TubeDesignerCadExistingWorkbenches.native.browser.mjs.
  const final = await page.evaluate(() => ({ calls: window.fixture.calls.map(call => ({ method: call.method, error: call.error, toolsOnly: call.payload?.toolsOnly })),
    failures: window.fixture.failures, sentinel: window.fixture.view.tubeDesignerPunchWizard === window.fixture.sentinel,
    fullRenders: window.fixture.fullRenders, localPatches: window.fixture.patches, resourceReads: window.fixture.resources.length }));
  assert.equal(final.sentinel, true); assert.deepEqual(final.failures, []); assert.deepEqual(errors, []); assert.deepEqual(external, []);
  assert.ok(final.calls.filter(call => call.method === "TubeDesigner.PreviewPartDrawing").every(call => call.toolsOnly === false));
  assert.ok(final.calls.every(call => !/GetPunchTools|PreviewPunchWizard|AddNestingPunchPart/.test(call.method)));
  writeFileSync(resolve(artifacts, "acceptance-results.json"), JSON.stringify({ ...final,nativeModules, vNativeCases, initialFraming, createdDocument,reopenedDocument, project: saved.file, partEntityId: created.result.partEntityId, requests }, null, 2));
  console.log("Drawing creation native acceptance passed: branch edits, all 7 V cutter GPU meshes, stable canvas/camera, AddPartDrawing and complete neutral CAD .ictd round trip.");
  console.log("Artifacts: " + artifacts);
  }
} catch (error) {
  if (page) await page.screenshot({ path: resolve(artifacts, "failure.png") }).catch(() => {});
  writeFileSync(resolve(artifacts, "failure.json"), JSON.stringify({ error: error.message, stack: error.stack, requests, stderr }, null, 2));
  throw error;
} finally {
  if (browser) await browser.close();
  if (bridge.exitCode == null) { try { await rpc({ action: "exit" }); } catch {} bridge.stdin.end(); }
}
