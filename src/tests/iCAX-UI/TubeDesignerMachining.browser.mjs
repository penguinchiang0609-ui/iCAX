// Isolated browser regression: shipped editor, renderer, picking and handlers.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");

async function checkMachiningWorkflow(page, output) {
  await page.evaluate(async () => {
    const f = globalThis.__pageNavigation;
    f.view.tubeDesignerLicense = { activated:true, capabilities:{"page.machining":true,"machining.toolpath":true} };
    const { encodeNestingGeometry } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const { pathEditor } = await import("/src/apps/tube-designer/webpage/machiningEditor.mjs");
    // A neutral rectangular tube mesh supplies a visible browser fixture.
    const positions = [], outer = [[-20,-20],[20,-20],[20,20],[-20,20]], inner = [[-18,-18],[18,-18],[18,18],[-18,18]];
    const vertex = (x, p) => [x, ...p];
    const quad = (a,b,c,d) => positions.push(...a,...b,...c,...a,...c,...d);
    for (let i=0;i<4;i++) {
      const j=(i+1)%4;
      quad(vertex(0,outer[i]),vertex(600,outer[i]),vertex(600,outer[j]),vertex(0,outer[j]));
      quad(vertex(0,inner[j]),vertex(600,inner[j]),vertex(600,inner[i]),vertex(0,inner[i]));
      for (const x of [0,600]) quad(vertex(x,outer[i]),vertex(x,outer[j]),vertex(x,inner[j]),vertex(x,inner[i]));
    }
    f.geometry = encodeNestingGeometry({ positions });
    const paths = Array.from({length:36},(_,i)=>({id:`path-${i}`,name:`顶面开孔 ${String(i+1).padStart(2,"0")}`,origin:"analysis",closed:true,
      points:[[25+i*15,-20.05,-8],[35+i*15,-20.05,-8],[35+i*15,-20.05,8],[25+i*15,-20.05,8]]}));
    f.jobs = Array.from({length:28},(_,i)=>({id:`cam-${i}`,name:`方管 40×40×2 · ${String(i+1).padStart(2,"0")}`,sourceKind:"cad",resourceId:`model-${i}`,resourceVersion:1,sourceFileName:"方管加工样件.step",solidCount:1,
      analysis:{revision:`analysis-${i}`,independent:true,paths:structuredClone(paths)}}));
    f.view.scene.tubeDesigner.machiningTask = {revision:"cam-task-1",jobs:f.jobs};
    f.state.tubeDesigner.machiningTask = structuredClone(f.view.scene.tubeDesigner.machiningTask);
    f.previewResponse = () => new Promise(resolve => { f.releasePreview = () => resolve({geometryResourceId:"cam-fixture-mesh",geometryResourceVersion:1}); });
    await globalThis.__icaxWorkbench.executeAreaAction("tube-machining-select",{dataset:{jobId:"cam-0"}});
    f.editor = pathEditor(f.view,f.jobs[0]);
  });
  await page.waitForFunction(() => typeof globalThis.__pageNavigation.releasePreview === "function");
  assert.equal(await page.locator('[data-tube-machining-workflow] button').count(),5);
  const search = page.getByRole("searchbox",{name:"筛选加工清单"});
  await search.fill("未提交的搜索草稿");
  await search.evaluate(node => node.setSelectionRange(2,7));
  await page.evaluate(() => {
    const f=globalThis.__pageNavigation;
    f.search=document.activeElement; f.searchEvents=0; f.search.addEventListener("interaction-probe",()=>f.searchEvents++);
    const left=document.querySelector(".cam-context-pane"),right=document.querySelector(".cam-info-pane"),jobs=document.querySelector(".tube-machining-job-list"),paths=document.querySelector(".tube-machining-path-list");
    // Continue scrolling after the preview request began.
    left.scrollTop=45; right.scrollTop=210; jobs.scrollTop=350; paths.scrollTop=165;
    f.expectedScroll=[left,right,jobs,paths].map(n=>n.scrollTop);
    f.releasePreview();
  });
  await page.waitForFunction(() => globalThis.__pageNavigation.view.tubeDesignerMachining.preview?.ready);
  const interaction=await page.evaluate(() => {
    const f=globalThis.__pageNavigation,search=document.querySelector('[aria-label="筛选加工清单"]');
    search.dispatchEvent(new Event("interaction-probe"));
    return {sameInput:search===f.search,focused:document.activeElement===search,value:search.value,selection:[search.selectionStart,search.selectionEnd],
      sameCanvas:f.view.viewport.renderer.domElement===f.canvas,events:f.searchEvents,scroll:[".cam-context-pane",".cam-info-pane",".tube-machining-job-list",".tube-machining-path-list"].map(s=>document.querySelector(s).scrollTop),expected:f.expectedScroll};
  });
  assert.equal(interaction.sameInput,true); assert.equal(interaction.focused,true); assert.equal(interaction.sameCanvas,true); assert.equal(interaction.events,1);
  assert.equal(interaction.value,"未提交的搜索草稿"); assert.deepEqual(interaction.selection,[2,7]); assert.deepEqual(interaction.scroll,interaction.expected);
  assert.ok(interaction.scroll[1]>0 && interaction.scroll[2]>0 && interaction.scroll[3]>0);
  await search.fill(""); await search.press("Tab");
  await page.evaluate(() => { for (const selector of [".cam-context-pane",".cam-info-pane",".tube-machining-job-list",".tube-machining-path-list"]) document.querySelector(selector).scrollTop=0; });
  await page.screenshot({path:resolve(output,"cam-program.png")});
  for (const stage of ["process","machine","simulation","output"]) {
    await page.locator(`[data-machining-stage="${stage}"]`).click();
    await page.waitForFunction(stage=>document.querySelector(`[data-machining-stage-panel="${stage}"]`),stage);
    assert.ok(await page.locator(`[data-machining-stage-panel="${stage}"] button:disabled`).count());
    if(stage==="process") assert.equal(await page.locator(".tube-cam-process-tabs button.active").innerText(),"引线");
    await page.screenshot({path:resolve(output,`cam-${stage}.png`)});
  }
  await page.locator('[data-machining-stage="program"]').click();
  await page.locator('button[data-path-id="path-0"]').click();
  assert.ok(await page.locator('[data-path-field="dx"]').isVisible());
  await page.evaluate(async () => {
    const f=globalThis.__pageNavigation;
    f.removed=document.querySelector('[data-path-field="dx"]'); f.removed.focus();
    await globalThis.__icaxWorkbench.executeAreaAction("tube-path-select",{dataset:{pathId:""}});
  });
  assert.equal(await page.locator('[data-path-field="dx"]').count(),0);
  assert.equal(await page.evaluate(()=>document.activeElement?.hasAttribute("data-path-field")),false);
  // Patch a retained SVG repeatedly, then draw once: one click adds one point.
  await page.locator('[data-cam-action="tube-path-space"]').click();
  await page.locator('[data-path-mode="draw"]').click();
  await page.evaluate(async () => {
    const f=globalThis.__pageNavigation;f.svg=document.querySelector('[data-machining-side-canvas]');
    for(let i=0;i<3;i++)await globalThis.__icaxWorkbench.executeAreaAction("tube-machining-select",{dataset:{jobId:"cam-0"}});
    f.svg.dispatchEvent(new MouseEvent("click",{clientX:f.svg.getBoundingClientRect().x+150,clientY:f.svg.getBoundingClientRect().y+150,bubbles:true}));
  });
  assert.equal(await page.evaluate(()=>globalThis.__pageNavigation.editor.drawing.length),1);
  assert.equal(await page.evaluate(()=>globalThis.__pageNavigation.svg===document.querySelector('[data-machining-side-canvas]')),true);
  await page.locator('[data-cam-action="tube-path-cancel-draw"]').click();
  for(const width of [1100,1920]) {
    await page.setViewportSize({width,height:900});
    const bounds=await page.locator('[data-tube-machining-workflow]').boundingBox();
    assert.ok(bounds.width<=width && bounds.height<150);
    assert.equal(await page.locator('[data-tube-machining-workflow] button').count(),5);
  }
  await page.setViewportSize({width:1440,height:900});
  writeFileSync(resolve(output,"cam-workspace-report.json"),JSON.stringify({interaction,stages:5,retainedSvg:true,singleClickSinglePoint:true,responsiveWidths:[1100,1440,1920],backend:"isolated browser fixture; no production NC output"},null,2));
}

async function checkPublicPageNavigation(browser) {
  const source = fileURLToPath(new URL("../../", import.meta.url));
  const output = resolve(source, "../output/tests/tube-designer-page-navigation");
  mkdirSync(output, { recursive: true });
  const descriptor = JSON.parse(readFileSync(resolve(source, "apps/tube-designer/templates/product/single_face_security_window/template.json"), "utf8"));
  const display = JSON.parse(readFileSync(resolve(source, "apps/tube-designer/templates/product/single_face_security_window/display.json"), "utf8"));
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } }), errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://navigation.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html><head><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div></body></html>' });
    if (pathname === "/src/iCAX-UI/SDK/runtime.mjs") return route.fulfill({ contentType: "text/javascript", body: "export async function connectApplication(){return globalThis.__pageNavigation.appProxy;}" });
    const path = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    try { return route.fulfill({ contentType: pathname.endsWith(".css") ? "text/css" : "text/javascript", body: readFileSync(path, "utf8") }); } catch { return route.abort(); }
  });
  await page.goto("http://navigation.test/");
  await page.evaluate(async ({ descriptor, display }) => {
    const { getProjectView } = await import("/src/apps/_shared/workbench/state/projectViewStore.mjs");
    const { catalogText } = await import("/src/apps/tube-designer/webpage/productCatalog.mjs");
    const template = { ...descriptor, display, available: true, descriptorLoaded: true, name: catalogText(descriptor.displayName),
      groups: descriptor.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: descriptor.parameters.map(field => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
        displayName: catalogText(field.displayName), groupKey: field.group,
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const projectId = "32b5d8b2-a984-40ac-bc23-653b39a65001", sceneId = "32b5d8b2-a984-40ac-bc23-653b39a65002";
    const parameters = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
    const instance = { entityId: "32b5d8b2-a984-40ac-bc23-653b39a65003", name: "防盗窗", templateId: template.id, quantity: 1, parameters };
    const state = { sceneId, undoRedo: { revision: 1, canUndo: false }, tubeDesigner: { templates: [template],
      instances: [instance], product: instance, activeProductId: instance.entityId, members: [], joints: [], parts: [], manufacturingGroups: [] } };
    const view = getProjectView(projectId), requests = [];
    Object.assign(view, { scene: structuredClone(state), tubeDesignerLoaded: true, tubeDesignerUserDataLoaded: true,
      tubeDesignerParameterPanelProductId: instance.entityId, tubeDesignerParameterDisclosureState: { initialized: true } });
    const snapshot = { viewId: "navigation-view", revision: "1", rows: [] };
    const sceneProxy = { state, pdo: { enabled: false }, resources: { get(url) { if(url==="cam-fixture-mesh")return Promise.resolve(new Response(globalThis.__pageNavigation.geometry)); throw new Error("Unexpected geometry: "+url); } },
      views: { async start() { return { snapshot, async poll() { return snapshot; }, async stop() {} }; } },
      async getState() { return structuredClone(state); }, async invoke(method) { requests.push(method);
        if (method === "TubeDesigner.MachiningData") return globalThis.__pageNavigation.previewResponse();
        if (method === "TubeDesigner.GetProductTemplateDescriptor") return { template };
        if (method === "TubeDesigner.GetPunchTools") return { tools: [] };
        if (method === "TubeDesigner.List") return { tubeDesigner: structuredClone(state.tubeDesigner) };
        throw new Error("Unexpected native request while navigating: " + method); } };
    const projectState = { projectId, projectName: "页面导航回归", mainScene: state };
    const projectProxy = { projectId, state: projectState, getMainScene() { return sceneProxy; } };
    const productState = { productId: "icax.tube-designer", productName: "TubeDesigner", isStarted: true,
      frontendEntry: "/src/apps/tube-designer/webpage/entry.mjs", catalogs: [{ mainProject: projectState }], projectFile: { fileExtensions: ["icax"] } };
    const productProxy = { productId: productState.productId, state: productState, async getState() { return productState; },
      async invoke(method) { if(method==="TubeDesignerLicensing.Status") return {activated:true,featureSchemaVersion:1,capabilities:{"page.product":true,"page.nesting":true,"page.machining":true,"machining.toolpath":true}}; throw new Error("Unexpected product-channel request: "+method); },
      getProject() { return projectProxy; }, async openProjectCatalog() { return { projectProxy, sceneProxy, catalog: { mainProject: projectState } }; } };
    const appProxy = { bridge: {}, products: new Map([[productState.productId, productProxy]]), async getState() { return { products: [productState] }; },
      getProduct() { return productProxy; }, async startProduct() { return productProxy; } };
    globalThis.__pageNavigation = { appProxy, view, state, requests };
    await import("/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs");
  }, { descriptor, display });
  const tab = id => page.locator('[data-action="select-ribbon-tab"][data-tab-id="' + id + '"]');
  await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().activeProjectId && !globalThis.__icaxAppShell.getState().pendingCount && document.querySelector('[data-tube-designer-parameter-form]'));
  assert.deepEqual((await page.locator('[data-action="select-ribbon-tab"]').allTextContents()).map(text => text.trim()), ["产品", "下料", "加工", "资源库", "关于"]);
  await page.evaluate(() => {
    const f = globalThis.__pageNavigation;
    f.viewport = f.view.viewport; f.canvas = f.view.viewport.renderer.domElement;
    f.canvasEvents = 0; f.canvas.addEventListener("navigation-probe", () => f.canvasEvents++);
    f.parameters = JSON.stringify(f.view.scene.tubeDesigner.product.parameters);
    f.draft = { ...f.view.scene.tubeDesigner.product.parameters, productCode: "NAV-DRAFT" };
    f.view.tubeDesignerRightDraft = structuredClone(f.draft);
    f.view.tubeDesignerRightDraftsByProductId = { [f.view.scene.tubeDesigner.product.entityId]: structuredClone(f.draft) };
  });
  await tab("machining").click();
  await page.waitForFunction(() => globalThis.__pageNavigation.view.activeAreaId === "machining" && document.querySelector(".tube-machining-empty-stage"));
  assert.match(await page.locator(".cam-context-pane").innerText(), /加工清单/);
  assert.match(await page.locator(".tube-machining-empty-stage").innerText(), /准备三维加工/);
  assert.ok(await page.locator('[data-command-id="machining.receive"]').isVisible());
  await page.screenshot({ path: resolve(output, "machining.png") });
  await checkMachiningWorkflow(page,output);
  await tab("about").click();
  await page.waitForFunction(() => globalThis.__pageNavigation.view.activeAreaId === "about" && document.querySelector(".tube-designer-about-viewport"));
  assert.match(await page.locator(".cam-info-pane").innerText(), /软件授权/);
  assert.equal(await page.locator('[data-command-id="licensing.status"]').count(), 0);
  await page.screenshot({ path: resolve(output, "about.png") });
  await tab("view").click();
  await page.waitForFunction(() => globalThis.__pageNavigation.view.activeAreaId === "view" && document.querySelector('[data-tube-designer-parameter-form]'));
  assert.equal(await page.locator('[data-tube-designer-parameter="productCode"]').inputValue(), "NAV-DRAFT");
  const result = await page.evaluate(() => {
    const f = globalThis.__pageNavigation;
    f.canvas.dispatchEvent(new Event("navigation-probe"));
    return { sameViewport: f.view.viewport === f.viewport, sameCanvas: f.view.viewport.renderer.domElement === f.canvas && f.canvas.isConnected,
      canvasEvents: f.canvasEvents, productUnchanged: JSON.stringify(f.view.scene.tubeDesigner.product.parameters) === f.parameters,
      draftUnchanged: JSON.stringify(f.view.tubeDesignerRightDraft) === JSON.stringify(f.draft), nativeRequests: f.requests,
      nativeRegeneration: f.requests.some(method => /GeneratePreview|UpdateProduct/.test(method)), tabs: ["产品", "下料", "加工", "资源库", "关于"] };
  });
  assert.equal(result.sameViewport, true); assert.equal(result.sameCanvas, true); assert.equal(result.canvasEvents, 1);
  assert.equal(result.productUnchanged, true); assert.equal(result.draftUnchanged, true); assert.equal(result.nativeRegeneration, false);
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, "browser-report.json"), JSON.stringify({ ...result, errors }, null, 2));
  await page.close();
  console.log("Public page navigation passed: five tabs, existing machining/about pages, retained product draft, same viewport/canvas/listener, no native regeneration.");
}

const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });
const errors = [];
try {
  await checkPublicPageNavigation(browser);
  if (!process.argv.includes("--navigation-only")) {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.on("pageerror", error => errors.push(error.message));
  const root = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://machining.test/**", async route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><html><head></head><body></body></html>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    await route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://machining.test/");
  await page.evaluate(async () => {
    const area = await import("/src/apps/tube-designer/webpage/machiningArea.mjs");
    const editorModule = await import("/src/apps/tube-designer/webpage/machiningEditor.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.innerHTML = `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Arial,"Microsoft Yahei",sans-serif;background:#eff4f5}#workspace{display:grid;grid-template-columns:230px minmax(0,1fr) 320px;height:100vh}aside{overflow:auto}#stage{position:relative;min-width:0;min-height:0}#scene,#overlay{position:absolute;inset:0}#overlay{pointer-events:none}#error{position:absolute;bottom:0;left:0;color:#a33;z-index:15}</style>`;
    document.body.innerHTML = '<div id="workspace"><aside id="left"></aside><main id="stage"><div id="scene"></div><div id="overlay"></div><div id="error"></div></main><aside id="right"></aside></div>';
    const job = { id: "job", sourceKind: "nesting", planId: "plan", sourceRevision: "old", name: "方管排样 · 01", analysis: { revision: "a1", independent: true,
      paths: [{ id: "cut", name: "端部切断", origin: "analysis", closed: true, points: [[0,30,0],[200,30,0],[200,30,50],[0,30,50]] },
        { id: "hole", name: "开孔轮廓", origin: "analysis", closed: true, points: [[75,30,15],[125,30,15],[125,30,35],[75,30,35]] }] } };
    const view = { activeAreaId: "machining", scene: { tubeDesigner: { machiningTask: { revision: "m1", jobs: [job] } } } };
    const context = { mount: document.body, sceneProxy: { async invoke() { throw new Error("Unexpected native call"); } } };
    const ops = { renderProject: render, showNotice() {} };
    view.viewport = createThreeViewport({ continuousRender: false, onPick: (...args) => area.handleTubeMachiningViewportPick(context, view, ...args, ops) });
    view.viewport.mount(document.querySelector("#scene"));
    function render() {
      document.querySelector("#left").innerHTML = area.renderTubeMachiningLeftPane(context, view);
      document.querySelector("#right").innerHTML = area.renderTubeMachiningRightPane(context, view);
      document.querySelector("#overlay").innerHTML = area.renderTubeMachiningViewportOverlay(context, view);
      document.querySelector("#error").textContent = view.error || "";
      area.attachTubeMachining(context, view, document.body, ops);
    }
    document.body.addEventListener("click", event => { const target = event.target.closest("[data-cam-action]"); if (target) area.handleTubeMachiningAction(context, view, target.dataset.camAction, target, ops); });
    document.body.addEventListener("change", event => { const target = event.target.closest("[data-cam-change-action]"); if (target) area.handleTubeMachiningAction(context, view, target.dataset.camChangeAction, target, ops); });
    const editor = editorModule.pathEditor(view, job);
    editor.space = "2d";
    window.check = { view, context, ops, editor, area, editorModule, render, THREE };
    render();
  });
  const clickAction = action => page.locator(`[data-cam-action="tube-path-${action}"]`).click();
  const setField = async (key, value) => { const field = page.locator(`[data-path-field="${key}"]`); await field.fill(String(value)); await field.press("Tab"); };
  const read = () => page.evaluate(() => structuredClone(window.check.editor));
  const point = async xyz => page.evaluate(xyz => {
    const { editor, editorModule } = window.check, svg = document.querySelector("[data-machining-side-canvas]"), box = svg.getBoundingClientRect();
    const [x,y] = editorModule.sideLayout(editor).project(xyz), scale = Math.min(box.width/1000, box.height/600);
    return [box.x+(box.width-1000*scale)/2+x*scale,box.y+(box.height-600*scale)/2+y*scale];
  }, xyz);
  const clickAt = async xyz => page.mouse.click(...await point(xyz));
  await page.locator('button[data-path-id="hole"]').click();
  assert.deepEqual((await read()).selectedIds, ["hole"]);
  assert.match(await page.locator(".tube-machining-path-properties").innerText(), /开孔轮廓/);
  assert.equal(await page.locator('polyline[data-path-id="hole"]').getAttribute("stroke"), "#ffd400");
  await clickAction("copy");
  const copyId = (await read()).selectedIds[0];
  await setField("dx", 30); await setField("angle", 90); await clickAction("pivot"); await clickAction("transform");
  let e = await read();
  assert.deepEqual(e.paths[1].points[0], [75,30,15]);
  assert.equal(e.paths.length, 3);
  assert.notDeepEqual(e.paths.find(p => p.id === copyId).points, e.paths[1].points);
  await clickAction("undo"); await clickAction("redo");
  await page.locator('[data-path-mode="draw"]').click();
  await setField("depth", 7);
  await clickAt([35,7,20]); await clickAt([55,7,30]);
  await clickAction("finish");
  e = await read(); assert.equal(e.paths.length,4); assert.ok(e.paths.at(-1).points.every(p => Math.abs(p[1]-7)<1e-8));
  await page.locator('[data-path-mode="break"]').click();
  await clickAt([45,7,25]);
  e = await read(); assert.equal(e.paths.length,5); assert.equal(e.selectedIds.length,2);
  await clickAction("merge"); assert.equal((await read()).paths.length,4);
  await page.locator('[data-path-mode="select"]').click();
  await clickAt([100,30,0]); assert.deepEqual((await read()).selectedIds,["cut"]);
  // Real WebGL objects and real viewport ray picking, not a synthetic hit.
  await clickAction("space");
  await page.waitForFunction(() => window.check.view.tubeDesignerMachining.preview?.ready);
  await page.evaluate(() => window.check.view.viewport.setStandardView("front"));
  const threePoint = xyz => page.evaluate(xyz => {
    const { view, THREE } = window.check, v = view.viewport;
    const p = new THREE.Vector3(...xyz).applyMatrix4(v.content.matrixWorld).project(v.camera), box = v.renderer.domElement.getBoundingClientRect();
    return [box.x+(p.x+1)*box.width/2,box.y+(1-p.y)*box.height/2];
  }, xyz);
  await page.mouse.click(...await threePoint([100,30,15]));
  assert.deepEqual((await read()).selectedIds,["hole"]);
  await page.locator('[data-path-mode="draw"]').click();
  await page.mouse.click(...await threePoint([35,7,40]));
  await page.mouse.click(...await threePoint([55,7,45]));
  await clickAction("finish");
  e = await read(); assert.equal(e.paths.length,5); assert.ok(e.paths.at(-1).points.every(p => Math.abs(p[1]-7)<1e-8));
  await clickAction("space");
  await page.locator('button[data-path-id="hole"]').click();
  const output = resolve(root,"../Temp/machining-verify-0908"); mkdirSync(output,{recursive:true});
  await page.screenshot({ path: resolve(output,"machining-editor.png") });
  for (const width of [1100,1920]) {
    await page.setViewportSize({width,height:900});
    assert.ok(await page.locator("[data-machining-side-canvas]").isVisible());
    const toolbar = await page.locator(".tube-machining-edit-toolbar").boundingBox();
    assert.ok(toolbar.width > 200 && toolbar.x+toolbar.width < width-300);
  }
  assert.deepEqual(errors,[]);
  console.log("Machining browser tests passed: real 2D/3D drawing, ray picking, list/property linkage, copy, transform, split, merge, undo/redo and responsive layout.");
  }
} finally { await browser.close(); }
