// Real Edge/WebGL regression for the template-owned embedded-arc target.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const repositoryRoot = fileURLToPath(new URL("../../../", import.meta.url));
const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const templateId = "node-embedded-arc-integrated";
const processId = "embedded-arc-notch";
const assemblyRoot = resolve(sourceRoot, "apps/tube-designer/templates/assembly");
const templates = readdirSync(assemblyRoot).filter((name) =>
  existsSync(resolve(assemblyRoot, name, "assembly.json")))
  .map((name) => JSON.parse(readFileSync(resolve(assemblyRoot, name, "assembly.json"), "utf8")));
const template = templates.find((item) => item.id === templateId);
assert.ok(template);
template.partProcesses[0].resource.descriptor = JSON.parse(readFileSync(resolve(sourceRoot,
  `apps/tube-designer/templates/mold/${processId}/tool.json`), "utf8"));
const plans = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
import importlib.util
import json
from pathlib import Path
import sys

sys.stdout.reconfigure(encoding="utf-8")
root = Path(sys.argv[1])
runtime_path = root / "src/apps/tube-designer/templates/_shared/assembly_template_runtime.py"
spec = importlib.util.spec_from_file_location("embedded_arc_browser_runtime", runtime_path)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
print(json.dumps({
  "default": runtime.preview_plan("node-embedded-arc-integrated"),
  "angleDraft": runtime.preview_plan("node-embedded-arc-integrated", {"angle": 75}),
  "processDraft": runtime.preview_plan("node-embedded-arc-integrated", {"angle": 75}, {
    "node-slot": {"embedded-arc-notch": {"arcRadius": 12}},
  }),
  "maleDraft": runtime.preview_plan("node-embedded-arc-integrated", {"angle": 75}, {
    "node-slot": {"embedded-arc-notch": {"arcRadius": 12, "maleFemale": True, "maleFemaleSize": 2}},
  }),
}, ensure_ascii=False))
`, repositoryRoot], {
  encoding: "utf8", maxBuffer: 24 * 1024 * 1024,
  env: { ...process.env, PYTHONDONTWRITEBYTECODE: "1" },
}));
for (const plan of Object.values(plans)) {
  assert.equal(plan.templateId, templateId);
  assert.equal(plan.formedPreviewMesh?.metadata?.previewKind, "target-shape");
  assert.equal(plan.formedPreviewMesh?.metadata?.formingValidation, "not-performed");
  assert.equal(plan.manufacturingParts?.[0]?.request?.features?.[0]?.toolRef?.id, processId);
}

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1480, height: 920 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://embedded-arc-preview.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
        || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://embedded-arc-preview.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `
    body{margin:0}.cam-workbench{display:grid;grid-template-columns:280px minmax(0,1fr) 330px;height:920px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{position:relative;min-width:0;background:#13252d}
    .tube-connection-library-list{height:180px!important;max-height:180px!important;min-height:0!important;overflow:auto}
    .tube-connection-library-editor-body{height:110px;overflow:auto}` });
  const first = await page.evaluate(async ({ template, templates, plans, processId }) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { encodeNestingGeometry } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const resources = new Map();
    const snapshots = [], dimensionCalls = [], punchRequests = [], planRequests = [];
    const pendingPlans = [];
    let viewport, viewportCount = 0, deferNextPlan = false;
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example", workModeUserSelected: true } };
    view.viewport = { setVisibleEntityIds() {}, setContinuousRendering() {} };
    const context = { sceneProxy: {
      resources: { async get(url) {
        const bytes = resources.get(url);
        return new Response(bytes ?? null, { status: bytes ? 200 : 404 });
      } },
      invoke(method, request) {
        if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
          if (request.templateId !== template.id) throw Error("wrong template");
          planRequests.push(request);
          const draft = request.processDrafts?.["node-slot"]?.[processId] ?? {};
          const plan = draft.maleFemale === true ? plans.maleDraft
            : draft.arcRadius === 12 ? plans.processDraft
            : request.sceneParameters?.angle === 75 ? plans.angleDraft : plans.default;
          if (deferNextPlan) {
            deferNextPlan = false;
            return new Promise((resolve) => pendingPlans.push(() => resolve(plan)));
          }
          return Promise.resolve(plan);
        }
        if (method === "TubeDesigner.EvaluateProfilePackage") return Promise.resolve({ profile: {
          contours: [[[0, 0], [1, 0], [1, 1], [0, 1]]],
        } });
        if (method === "TubeDesigner.PreviewPunchWizard") {
          punchRequests.push(request);
          const number = punchRequests.length;
          // Browser rendering stub only. Native punch preview tests verify
          // the actual embedded-arc cut; this box proves scene switching.
          const box = new THREE.BoxGeometry(request.length, request.parameters.width,
            request.parameters.depth);
          const bytes = encodeNestingGeometry({ kind: 1,
            positions: [...box.attributes.position.array], indices: [...box.index.array] });
          const base = `memory://embedded-arc-base-${number}`;
          const result = `memory://embedded-arc-result-${number}`;
          resources.set(base, bytes); resources.set(result, bytes);
          return Promise.resolve({ previewComputed: true,
            baseGeometry: { url: base, version: 1 }, geometry: { url: result, version: 1 } });
        }
        throw Error(`unexpected native call ${method}`);
      },
    } };
    const viewportFactory = (options) => {
      viewportCount++;
      viewport = createThreeViewport(options);
      const apply = viewport.applyViewSnapshot.bind(viewport);
      viewport.applyViewSnapshot = async (snapshot, resourceClient) => {
        snapshots.push(snapshot);
        return apply(snapshot, resourceClient);
      };
      const setDimensions = viewport.setDimensionAnnotations.bind(viewport);
      viewport.setDimensionAnnotations = (items) => {
        dimensionCalls.push(items); setDimensions(items);
      };
      return viewport;
    };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane(context, view);
      const right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      let mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport">${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        mount = document.querySelector("main");
        patch.rememberLibraryDom(view, mount, "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) {
        throw Error("assembly refresh replaced the whole DOM");
      }
      library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory });
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    const waitFor = async (condition) => {
      for (let index = 0; index < 120; index++) {
        if (condition()) return;
        await new Promise((resolve) => setTimeout(resolve, 20));
      }
      throw Error(`preview timeout: ${JSON.stringify({ error: view.tubeDesignerAssemblyLibrary.previewError,
        sceneIds: [...(viewport?.sceneObjects?.keys() ?? [])], bounds: viewport?.getVisibleBounds?.(),
        annotationIds: dimensionCalls.at(-1)?.map((item) => item.id),
        planAngle: view.tubeDesignerAssemblyLibrary.preview?.plan?.parameters?.angle,
        previewRequest: !!view.tubeDesignerAssemblyLibrary.previewRequest,
        finishedUrl: view.tubeDesignerAssemblyLibrary.preview?.finishedShape?.rows?.[0]?.data?.geometry?.url,
        snapshotUrl: snapshots.at(-1)?.rows?.[0]?.data?.geometry?.url })}`);
    };
    render();
    await view.tubeDesignerAssemblyLibrary.previewRequest.promise;
    await waitFor(() => !!viewport?.sceneObjects?.has("assembly-finished-shape")
      && dimensionCalls.at(-1)?.some((item) => item.id?.startsWith("finished-envelope:")));
    const mount = document.querySelector("main");
    const state = view.tubeDesignerAssemblyLibrary;
    const formed = state.preview.formedPreview;
    const scene = mount.querySelector(".tube-assembly-preview-pane.scene");
    const search = mount.querySelector(".tube-connection-library-search input");
    const left = mount.querySelector(".tube-connection-library-list");
    const right = mount.querySelector(".tube-connection-library-editor-body");
    const canvas = mount.querySelector("[data-tube-assembly-scene-viewport] canvas");
    const angle = mount.querySelector('[data-tube-assembly-parameter="angle"]');
    state.search = "弯";
    search.value = "弯"; search.focus({ preventScroll: true }); search.setSelectionRange(0, 1);
    left.scrollTop = 60; right.scrollTop = 70;
    window.__embeddedArcTest = { library, context, view, render, waitFor, resources,
      snapshots, dimensionCalls, planRequests, punchRequests, pendingPlans,
      viewport, canvas, angle, search, left, right,
      setDefer() { deferNextPlan = true; } };
    return { controls: [...mount.querySelectorAll("[data-tube-assembly-view]")]
        .map((item) => item.textContent.trim()),
      description: scene.getAttribute("aria-label"),
      metadata: formed.mesh.metadata,
      finishedRowId: snapshots.at(-1).rows[0].entityId,
      finishedMeshKind: state.preview.finishedShape.mesh.metadata.previewKind,
      finishedBounds: viewport.getVisibleBounds(),
      finishedDimensions: dimensionCalls.at(-1).filter((item) => item.id?.startsWith("finished-envelope:"))
        .map((item) => ({ id: item.id, label: item.label, length: Math.hypot(...item.start.map((value, i) =>
          item.end[i] - value)) })),
      logicalDimensionAbsent: !dimensionCalls.at(-1).some((item) => /设计长|逻辑段设定长/.test(item.label)
        || item.id?.startsWith("design-length:")),
      formedUrl: snapshots.at(-1).rows[0].data.geometry.url,
      punchTool: punchRequests.at(-1)?.features?.[0]?.toolRef?.id,
      viewportCount,
      zSpan: Math.max(...formed.mesh.positions.filter((_, i) => i % 3 === 2))
        - Math.min(...formed.mesh.positions.filter((_, i) => i % 3 === 2)),
      scroll: { left: left.scrollTop, right: right.scrollTop },
    };
  }, { template, templates, plans, processId });
  assert.deepEqual(first.controls, ["成品示意", "下料件"]);
  assert.match(first.description, /成品外形/);
  assert.equal(first.metadata.previewKind, "target-shape");
  assert.equal(first.metadata.formingValidation, "not-performed");
  assert.equal(first.finishedRowId, "assembly-finished-shape");
  assert.equal(first.finishedMeshKind, "finished-shape");
  assert.equal(first.punchTool, processId);
  assert.ok(first.zSpan > 100, "target must turn through the specified angle");
  assert.equal(first.viewportCount, 1);
  const spans = first.finishedBounds.min.map((min, axis) => first.finishedBounds.max[axis] - min);
  const expectedAxes = [0, 1, 2].filter((axis) => spans[axis] >= Math.max(...spans) * 0.25);
  assert.deepEqual(first.finishedDimensions.map(({ id }) => id),
    expectedAxes.map((axis) => `finished-envelope:${"xyz"[axis]}`));
  for (const dimension of first.finishedDimensions) {
    const axis = "xyz".indexOf(dimension.id.at(-1));
    assert.ok(Math.abs(dimension.length - spans[axis]) < 1e-5);
    assert.match(dimension.label, /成品.*mm/);
  }
  assert.equal(first.logicalDimensionAbsent, true, "成品图不得标注逻辑段设定长");
  assert.ok(first.scroll.left > 0 && first.scroll.right > 0);
  const screenshot = resolve(repositoryRoot, "artifacts/embedded-arc-target-browser.png");
  await page.screenshot({ path: screenshot });
  assert.equal(await page.locator(".tube-assembly-workflow-details").count(), 0);

  const switches = await page.evaluate(async () => {
    const t = window.__embeddedArcTest;
    const state = t.view.tubeDesignerAssemblyLibrary;
    const targetUrl = state.preview.finishedShape.rows[0].data.geometry.url;
    const blankUrl = state.preview.manufacturingParts[0].response.geometry.url;
    const action = (mode) => t.library.handleAssemblyLibraryAction(t.context, t.view,
      "tube-designer-assembly-set-view", document.querySelector(`[data-tube-assembly-view="${mode}"]`),
      { renderProject: t.render });
    await action("exploded");
    await t.waitFor(() => t.snapshots.at(-1)?.rows[0]?.data.geometry.url === blankUrl);
    const blankDescription = document.querySelector(".tube-assembly-preview-pane.scene").getAttribute("aria-label");
    await action("finished");
    await t.waitFor(() => t.snapshots.at(-1)?.rows[0]?.data.geometry.url === targetUrl);
    const mount = document.querySelector("main");
    return { blankDescription,
      targetPressed: mount.querySelector('[data-tube-assembly-view="finished"]')?.getAttribute("aria-pressed"),
      canvasStable: t.canvas === mount.querySelector("[data-tube-assembly-scene-viewport] canvas"),
      inputStable: t.angle === mount.querySelector('[data-tube-assembly-parameter="angle"]'),
      focusStable: document.activeElement === t.search && t.search.selectionStart === 0
        && t.search.selectionEnd === 1,
      scrollStable: t.left.scrollTop > 0 && t.right.scrollTop > 0,
      sceneObjects: t.viewport.sceneObjects.size,
    };
  });
  assert.match(switches.blankDescription, /下料件（成形前）/);
  assert.equal(switches.targetPressed, "true");
  assert.equal(switches.sceneObjects, 1);
  for (const key of ["canvasStable", "inputStable", "focusStable", "scrollStable"])
    assert.equal(switches[key], true, `${key} must survive target/blank switch`);

  const refreshed = await page.evaluate(async () => {
    const t = window.__embeddedArcTest;
    const state = t.view.tubeDesignerAssemblyLibrary;
    const oldUrl = state.preview.finishedShape.rows[0].data.geometry.url;
    const angle = t.angle;
    angle.value = "75";
    t.search.focus({ preventScroll: true }); t.search.setSelectionRange(0, 1);
    t.setDefer();
    await t.library.handleAssemblyLibraryAction(t.context, t.view,
      "tube-designer-assembly-parameter-change", angle, { renderProject: t.render });
    await t.waitFor(() => t.pendingPlans.length === 1);
    t.left.scrollTop = 110; t.right.scrollTop = 90;
    const latestScroll = { left: t.left.scrollTop, right: t.right.scrollTop };
    t.pendingPlans.shift()();
    await t.waitFor(() => !state.previewRequest && state.preview?.plan?.parameters?.angle === 75
      && state.preview?.finishedShape?.rows?.[0]?.data?.geometry?.url !== oldUrl);
    await t.waitFor(() => t.snapshots.at(-1)?.rows[0]?.data.geometry.url
      === state.preview.finishedShape.rows[0].data.geometry.url);
    return { angle: state.preview.plan.parameters.angle,
      targetKind: state.preview.finishedShape.mesh.metadata.previewKind,
      canvasStable: t.canvas === document.querySelector("[data-tube-assembly-scene-viewport] canvas"),
      inputStable: t.angle === document.querySelector('[data-tube-assembly-parameter="angle"]'),
      focusStable: document.activeElement === t.search && t.search.selectionStart === 0
        && t.search.selectionEnd === 1,
      scrollStable: t.left.scrollTop === latestScroll.left && t.right.scrollTop === latestScroll.right,
      featureAngle: t.punchRequests.at(-1)?.features?.[0]?.toolParameters?.angle,
    };
  });
  assert.equal(refreshed.angle, 75);
  assert.equal(refreshed.featureAngle, 75);
  assert.equal(refreshed.targetKind, "finished-shape");
  for (const key of ["canvasStable", "inputStable", "focusStable", "scrollStable"])
    assert.equal(refreshed[key], true, `${key} must survive async plan refresh`);

  const processChanged = await page.evaluate(async () => {
    const t = window.__embeddedArcTest;
    const state = t.view.tubeDesignerAssemblyLibrary;
    const radius = document.querySelector('[data-tube-part-process-parameter="arcRadius"]');
    const male = document.querySelector('[data-tube-part-process-parameter="maleFemale"]');
    if (!radius || !male) throw Error("embedded arc process fields missing");
    const beforeConditional = !document.querySelector('[data-tube-part-process-parameter="maleFemaleSize"]');
    const oldProcessUrl = state.preview.formedPreview.rows[0].data.geometry.url;
    const oldBounds = t.viewport.getVisibleBounds();
    const oldDimensions = t.dimensionCalls.at(-1).map((item) => ({ id: item.id, label: item.label }));
    radius.value = "12";
    await t.library.handleAssemblyLibraryAction(t.context, t.view,
      "tube-designer-assembly-process-parameter-change", radius, { renderProject: t.render });
    await t.waitFor(() => !state.previewRequest && state.preview?.formedPreview?.mesh?.metadata?.arcRadius === 12);
    male.checked = true;
    await t.library.handleAssemblyLibraryAction(t.context, t.view,
      "tube-designer-assembly-process-parameter-change", male, { renderProject: t.render });
    await t.waitFor(() => !state.previewRequest && state.preview?.plan?.manufacturingParts?.[0]
      ?.request?.features?.[0]?.toolParameters?.maleFemale === true);
    await t.waitFor(() => t.snapshots.at(-1)?.rows[0]?.data.geometry.url
      === state.preview.finishedShape.rows[0].data.geometry.url);
    return { beforeConditional,
      conditionAppeared: !!document.querySelector('[data-tube-part-process-parameter="maleFemaleSize"]'),
      radius: state.preview.formedPreview.mesh.metadata.arcRadius,
      radiusFieldValue: document.querySelector('[data-tube-part-process-parameter="arcRadius"]')?.value,
      processTargetChanged: oldProcessUrl !== state.preview.formedPreview.rows[0].data.geometry.url,
      productBoundsStable: JSON.stringify(oldBounds) === JSON.stringify(t.viewport.getVisibleBounds()),
      productDimensionsStable: JSON.stringify(oldDimensions) === JSON.stringify(t.dimensionCalls.at(-1)
        .map((item) => ({ id: item.id, label: item.label }))),
      cutRadius: t.punchRequests.at(-1)?.features?.[0]?.toolParameters?.arcRadius,
      cutMale: t.punchRequests.at(-1)?.features?.[0]?.toolParameters?.maleFemale,
      canvasStable: t.canvas === document.querySelector("[data-tube-assembly-scene-viewport] canvas"),
      focusStable: document.activeElement === t.search && t.search.selectionStart === 0
        && t.search.selectionEnd === 1,
      scrollStable: t.left.scrollTop > 0 && t.right.scrollTop > 0,
    };
  });
  assert.equal(processChanged.beforeConditional, true);
  assert.equal(processChanged.conditionAppeared, true);
  assert.equal(processChanged.radius, 12);
  assert.equal(processChanged.cutRadius, 12);
  assert.equal(processChanged.cutMale, true);
  assert.equal(processChanged.processTargetChanged, true);
  assert.equal(processChanged.productBoundsStable && processChanged.productDimensionsStable, true,
    "仅修改开槽工艺时，成品外廓尺寸应保持一致");
  assert.equal(processChanged.radiusFieldValue, "12");
  for (const key of ["canvasStable", "focusStable", "scrollStable"])
    assert.equal(processChanged[key], true, `${key} must survive conditional process refresh`);
  assert.deepEqual(errors, []);
  console.log(`Embedded arc target/blank Edge browser preview passed: ${screenshot}`);
} finally {
  await browser.close();
}
