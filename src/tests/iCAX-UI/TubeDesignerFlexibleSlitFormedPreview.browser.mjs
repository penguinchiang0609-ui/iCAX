// Real Edge/WebGL regression for the template-owned flexible slit target view.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const repositoryRoot = fileURLToPath(new URL("../../../", import.meta.url));
const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const template = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/flexible-slit-bend-integrated/assembly.json", import.meta.url), "utf8"));
const assemblyRoot = resolve(sourceRoot, "apps/tube-designer/templates/assembly");
const templates = readdirSync(assemblyRoot).filter((name) =>
  existsSync(resolve(assemblyRoot, name, "assembly.json")))
  .map((name) => JSON.parse(readFileSync(resolve(assemblyRoot, name, "assembly.json"), "utf8")));
const toolDescriptor = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/mold/flexible-slit-bend/tool.json", import.meta.url), "utf8"));
for (const item of [template, ...templates.filter((candidate) => candidate.id === template.id)])
  item.partProcesses[0].resource.descriptor = toolDescriptor;
const plans = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
import importlib.util
import json
from pathlib import Path
import sys

sys.stdout.reconfigure(encoding="utf-8")
root = Path(sys.argv[1])
runtime_path = root / "src/apps/tube-designer/templates/_shared/assembly_template_runtime.py"
spec = importlib.util.spec_from_file_location("flexible_slit_browser_runtime", runtime_path)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
print(json.dumps({
  "default": runtime.preview_plan("flexible-slit-bend-integrated"),
  "straight": runtime.preview_plan("flexible-slit-bend-integrated", {}, {
    "node-slot": {"flexible-slit-bend": {"slitMode": "straight"}},
  }),
  "drafted": runtime.preview_plan("flexible-slit-bend-integrated", {}, {
    "node-slot": {"flexible-slit-bend": {"slitCount": 8, "slitMode": "straight"}},
  }),
}, ensure_ascii=False))
`, repositoryRoot], {
  encoding: "utf8", maxBuffer: 24 * 1024 * 1024,
  env: { ...process.env, PYTHONDONTWRITEBYTECODE: "1" },
}));
assert.equal(plans.default.formedPreviewMesh?.metadata?.previewKind, "target-shape");
assert.equal(plans.default.formedPreviewMesh?.metadata?.formingValidation, "calibration-required");
assert.equal(plans.drafted.formedPreviewMesh?.metadata?.slitCount, 8);

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1480, height: 920 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://flexible-slit-preview.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
        || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://flexible-slit-preview.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `
    body{margin:0}.cam-workbench{display:grid;grid-template-columns:280px minmax(0,1fr) 330px;height:920px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{position:relative;min-width:0;background:#13252d}
    .tube-connection-library-list{height:180px!important;max-height:180px!important;min-height:0!important;overflow:auto}
    .tube-connection-library-editor-body{height:110px;overflow:auto}` });
  const initial = await page.evaluate(async ({ template, templates, plans }) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { encodeNestingGeometry } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const { parseRenderGeometryResource } = await import("/src/iCAX-UI/SDK/Viewport/renderResource.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const resources = new Map();
    const snapshots = [], meshSnapshots = [], dimensionCalls = [], punchRequests = [], planRequests = [];
    const sameMesh = (a, b) => !!a && !!b
      && a.positions.length === b.positions.length && a.indices.length === b.indices.length
      && a.positions.every((value, index) => value === b.positions[index])
      && a.indices.every((value, index) => value === b.indices[index]);
    let viewport, viewportCount = 0;
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
          const draft = request.processDrafts?.["node-slot"]?.["flexible-slit-bend"] ?? {};
          return Promise.resolve(draft.slitCount === 8 ? plans.drafted
            : draft.slitMode === "straight" ? plans.straight : plans.default);
        }
        if (method === "TubeDesigner.EvaluateProfilePackage") return Promise.resolve({ profile: {
          contours: [[[0, 0], [1, 0], [1, 1], [0, 1]]],
        } });
        if (method === "TubeDesigner.PreviewPunchWizard") {
          punchRequests.push(request);
          const number = punchRequests.length;
          // Rendering stub only: the real cut contract is asserted below and
          // separately covered by the native punch preview tests.
          const box = new THREE.BoxGeometry(request.length, request.parameters.width,
            request.parameters.depth);
          const bytes = encodeNestingGeometry({ kind: 1,
            positions: [...box.attributes.position.array], indices: [...box.index.array] });
          const base = `memory://flexible-base-${number}`;
          const result = `memory://flexible-result-${number}`;
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
        const row = snapshot.rows[0];
        const mesh = row ? parseRenderGeometryResource(await (await resourceClient.get(row.data.geometry.url)).arrayBuffer()) : null;
        meshSnapshots.push(mesh);
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
        throw Error("assembly update replaced the whole DOM");
      }
      library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory });
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    const waitFor = async (condition) => {
      for (let index = 0; index < 120; index++) {
        if (condition()) return;
        await new Promise((resolve) => setTimeout(resolve, 20));
      }
      throw Error(`preview timeout: ${view.tubeDesignerAssemblyLibrary.previewError ?? ""}`);
    };
    render();
    await view.tubeDesignerAssemblyLibrary.previewRequest.promise;
    await waitFor(() => !!viewport?.sceneObjects?.has("assembly-finished-shape")
      && dimensionCalls.at(-1)?.some((item) => item.id === "finished-angle"));
    const mount = document.querySelector("main");
    const state = view.tubeDesignerAssemblyLibrary;
    const formed = state.preview.formedPreview;
    const finishedMesh = meshSnapshots.at(-1);
    const finishedUrl = snapshots.at(-1).rows[0].data.geometry.url;
    const scene = mount.querySelector(".tube-assembly-preview-pane.scene");
    const controls = [...mount.querySelectorAll("[data-tube-assembly-view]")]
      .map((item) => item.textContent.trim());
    const search = mount.querySelector(".tube-connection-library-search input");
    const left = mount.querySelector(".tube-connection-library-list");
    const right = mount.querySelector(".tube-connection-library-editor-body");
    const canvas = mount.querySelector("[data-tube-assembly-scene-viewport] canvas");
    const input = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    state.search = "折";
    search.value = "折"; search.focus({ preventScroll: true }); search.setSelectionRange(0, 1);
    left.scrollTop = 60; right.scrollTop = 70;
    window.__flexibleTest = { library, context, view, render, waitFor, resources,
      snapshots, meshSnapshots, finishedMesh, finishedUrl, sameMesh,
      dimensionCalls, planRequests, punchRequests, viewport, canvas, input, search, left, right };
    return { controls, description: scene.getAttribute("aria-label"),
      meshKind: formed.mesh.metadata.previewKind,
      validation: formed.mesh.metadata.formingValidation,
      finishedMeshKind: finishedMesh?.kind,
      finishedRowId: snapshots.at(-1).rows[0].entityId,
      annotations: dimensionCalls.at(-1).map((item) => item.label),
      annotationIds: dimensionCalls.at(-1).map((item) => item.id),
      finishedBounds: viewport.getVisibleBounds(),
      finishedDimensions: dimensionCalls.at(-1).filter((item) => item.id?.startsWith("finished-envelope:"))
        .map((item) => ({ id: item.id, label: item.label,
          length: Math.hypot(...item.start.map((value, axis) => item.end[axis] - value)) })),
      formedUrl: finishedUrl,
      slitFeatures: punchRequests.at(-1)?.features?.length ?? 0,
      viewportCount,
      zSpan: Math.max(...formed.mesh.positions.filter((_, i) => i % 3 === 2))
        - Math.min(...formed.mesh.positions.filter((_, i) => i % 3 === 2)),
      scroll: { left: left.scrollTop, right: right.scrollTop },
    };
  }, { template, templates, plans });
  assert.equal(initial.controls.length, 2);
  assert.match(initial.controls[0], /成品|目标/);
  assert.equal(initial.controls[1], "下料件");
  assert.match(initial.description, /成品/);
  assert.equal(initial.meshKind, "target-shape");
  assert.equal(initial.validation, "calibration-required");
  assert.equal(initial.finishedMeshKind, "mesh");
  assert.equal(initial.finishedRowId, "assembly-finished-shape");
  const finishedSpans = initial.finishedBounds.min.map((min, axis) => initial.finishedBounds.max[axis] - min);
  const finishedAxes = [0, 1, 2].filter((axis) => finishedSpans[axis] >= Math.max(...finishedSpans) * 0.25);
  assert.deepEqual(initial.finishedDimensions.map(({ id }) => id),
    finishedAxes.map((axis) => `finished-envelope:${"xyz"[axis]}`));
  for (const dimension of initial.finishedDimensions) {
    const axis = "xyz".indexOf(dimension.id.at(-1));
    assert.ok(Math.abs(dimension.length - finishedSpans[axis]) < 1e-5);
    assert.match(dimension.label, /成品.*mm/);
  }
  assert.equal(initial.annotationIds.filter((id) => id === "finished-angle").length, 1);
  assert.equal(initial.annotations.length, finishedAxes.length + 1, "成品图只应显示整体外廓尺寸和成品角度");
  assert.equal(initial.annotationIds.some((id) => id.startsWith("design-length:")), false);
  assert.equal(initial.annotations.some((label) => /R\+Kt|折弯因子|K=|展开|下料|槽距|缝距|缝中心距/.test(label)), false);
  assert.ok(initial.slitFeatures > 0, "blank preview must receive actual slit tool features");
  assert.ok(initial.zSpan > 100, "formed target must turn through an arc, not remain a straight blank");
  assert.equal(initial.viewportCount, 1);
  assert.ok(initial.scroll.left > 0 && initial.scroll.right > 0);
  const screenshot = resolve(repositoryRoot, "artifacts/flexible-slit-target-browser.png");
  await page.screenshot({ path: screenshot });

  const blank = await page.evaluate(async () => {
    const t = window.__flexibleTest;
    const action = async (mode) => t.library.handleAssemblyLibraryAction(t.context, t.view,
      "tube-designer-assembly-set-view", document.querySelector(`[data-tube-assembly-view="${mode}"]`),
      { renderProject: t.render });
    const blankUrl = t.view.tubeDesignerAssemblyLibrary.preview.manufacturingParts[0].response.geometry.url;
    await action("exploded");
    await t.waitFor(() => t.snapshots.at(-1)?.rows[0]?.data.geometry.url === blankUrl);
    await t.waitFor(() => t.dimensionCalls.length > 1);
    return { url: t.snapshots.at(-1).rows[0].data.geometry.url,
      description: document.querySelector(".tube-assembly-preview-pane.scene").getAttribute("aria-label"),
      pressed: document.querySelector('[data-tube-assembly-view="exploded"]').getAttribute("aria-pressed"),
      annotations: t.dimensionCalls.at(-1).map((item) => item.label) };
  });
  assert.match(blank.description, /下料件（成形前）/);
  assert.equal(blank.pressed, "true");
  assert.ok(blank.annotations.some((label) => label.includes("总下料长度")));
  // The browser's stand-in blank is deliberately uncut. The native C++
  // PreviewPunchWizard test covers the actual processed resource and modes.
  const blankScreenshot = resolve(repositoryRoot, "artifacts/flexible-slit-blank-browser-stub.png");
  await page.screenshot({ path: blankScreenshot });

  const switched = await page.evaluate(async () => {
    const t = window.__flexibleTest;
    const action = async (mode) => t.library.handleAssemblyLibraryAction(t.context, t.view,
      "tube-designer-assembly-set-view", document.querySelector(`[data-tube-assembly-view="${mode}"]`),
      { renderProject: t.render });
    const formedUrl = t.finishedUrl;
    const original = { canvas: t.canvas, input: t.input, search: t.search, left: t.left, right: t.right };
    const beforeScroll = { left: t.left.scrollTop, right: t.right.scrollTop };
    await action("finished");
    await t.waitFor(() => t.snapshots.at(-1)?.rows[0]?.data.geometry.url === formedUrl);
    await t.waitFor(() => t.dimensionCalls.at(-1)?.some((item) => item.id === "finished-angle"));
    const mount = document.querySelector("main");
    return { formedUrl: t.snapshots.at(-1).rows[0].data.geometry.url,
      finalDescription: mount.querySelector(".tube-assembly-preview-pane.scene").getAttribute("aria-label"),
      finalPressed: mount.querySelector('[data-tube-assembly-view="finished"]').getAttribute("aria-pressed"),
      annotations: t.dimensionCalls.at(-1).map((item) => ({ id: item.id, label: item.label })),
      canvasStable: original.canvas === mount.querySelector("[data-tube-assembly-scene-viewport] canvas"),
      inputStable: original.input === mount.querySelector('[data-tube-assembly-parameter="bendRadius"]'),
      searchStable: original.search === mount.querySelector(".tube-connection-library-search input"),
      focusStable: document.activeElement === original.search && original.search.value === "折"
        && original.search.selectionStart === 0 && original.search.selectionEnd === 1,
      scrollStable: original.left === mount.querySelector(".tube-connection-library-list")
        && original.right === mount.querySelector(".tube-connection-library-editor-body")
        && original.left.scrollTop === beforeScroll.left && original.right.scrollTop === beforeScroll.right,
      sceneRowCount: t.viewport.sceneObjects.size,
    };
  });
  assert.equal(switched.formedUrl, initial.formedUrl);
  assert.equal(switched.finalPressed, "true");
  assert.match(switched.finalDescription, /成品/);
  assert.equal(switched.annotations.length, initial.annotations.length, "切回成品后不得残留下料和工艺标注");
  assert.deepEqual(switched.annotations.map((item) => item.id).sort(), initial.annotationIds.slice().sort());
  assert.equal(switched.annotations.some((item) => /R\+Kt|K=|展开|下料|槽距|缝距|缝中心距/.test(item.label)), false);
  for (const key of ["canvasStable", "inputStable", "searchStable", "focusStable", "scrollStable"])
    assert.equal(switched[key], true, `${key} must survive blank/formed switching`);
  assert.equal(switched.sceneRowCount, 1);

  const drafted = await page.evaluate(async () => {
    const t = window.__flexibleTest;
    const state = t.view.tubeDesignerAssemblyLibrary;
    const oldBlankUrl = state.preview.manufacturingParts[0].response.geometry.url;
    const oldBlankFeatures = JSON.stringify(state.preview.plan.manufacturingParts[0].request.features);
    const oldPitch = state.preview.formedPreview.mesh.metadata.slitPitch;
    const mode = document.querySelector('[data-tube-part-process-parameter="slitMode"]');
    const count = document.querySelector('[data-tube-part-process-parameter="slitCount"]');
    if (!mode || !count) throw Error("flexible slit resource fields missing");
    mode.value = "straight";
    await t.library.handleAssemblyLibraryAction(t.context, t.view,
      "tube-designer-assembly-process-parameter-change", mode, { renderProject: t.render });
    await t.waitFor(() => !state.previewRequest && state.preview?.plan?.manufacturingParts?.[0]?.request
      ?.features?.[0]?.toolParameters?.slitMode === "straight");
    const straightMesh = t.meshSnapshots.at(-1);
    count.value = "8";
    await t.library.handleAssemblyLibraryAction(t.context, t.view,
      "tube-designer-assembly-process-parameter-change", count, { renderProject: t.render });
    await t.waitFor(() => !state.previewRequest && state.preview?.formedPreview?.mesh?.metadata?.slitCount === 8);
    await t.waitFor(() => t.snapshots.at(-1)?.rows[0]?.entityId === "assembly-finished-shape");
    const latest = state.preview.formedPreview;
    const lastCut = t.punchRequests.at(-1).features[0].toolParameters;
    return { mode: latest.mesh.metadata.slitMode, count: latest.mesh.metadata.slitCount,
      centerCount: latest.annotations.anchors.slitCenters.length,
      pitch: latest.mesh.metadata.slitPitch, oldPitch,
      finishedMeshStable: t.sameMesh(t.finishedMesh, straightMesh)
        && t.sameMesh(straightMesh, t.meshSnapshots.at(-1)),
      blankChanged: oldBlankUrl !== state.preview.manufacturingParts[0].response.geometry.url,
      blankRequestChanged: oldBlankFeatures !== JSON.stringify(state.preview.plan.manufacturingParts[0].request.features),
      cutMode: lastCut.slitMode, cutCount: lastCut.slitCount,
      noWorkflowCard: !document.querySelector(".tube-assembly-workflow-details"),
      finishedAnnotations: t.dimensionCalls.at(-1)?.map((item) => ({ id: item.id, label: item.label })),
      canvasStable: t.canvas === document.querySelector("[data-tube-assembly-scene-viewport] canvas"),
      inputStable: t.input === document.querySelector('[data-tube-assembly-parameter="bendRadius"]'),
      focusStable: document.activeElement === t.search && t.search.selectionStart === 0
        && t.search.selectionEnd === 1,
      scrollStable: t.left.scrollTop > 0 && t.right.scrollTop > 0,
      activeTarget: document.querySelector('[data-tube-assembly-view="finished"]')?.getAttribute("aria-pressed"),
      requestedCount: t.planRequests.at(-1)?.processDrafts?.["node-slot"]?.["flexible-slit-bend"]?.slitCount,
    };
  });
  assert.equal(drafted.mode, "straight");
  assert.equal(drafted.count, 8);
  assert.equal(drafted.centerCount, 8);
  assert.notEqual(drafted.pitch, drafted.oldPitch);
  assert.equal(drafted.cutMode, "straight");
  assert.equal(drafted.cutCount, 8);
  assert.equal(drafted.requestedCount, 8);
  assert.equal(drafted.finishedMeshStable, true,
    "修改切缝方式和数量后，成品网格顶点及索引必须保持一致");
  assert.equal(drafted.blankChanged && drafted.blankRequestChanged, true,
    "加工草稿应改变下料件请求与资源");
  assert.equal(drafted.noWorkflowCard, true);
  assert.equal(drafted.finishedAnnotations.length, initial.annotations.length);
  assert.deepEqual(drafted.finishedAnnotations.map((item) => item.id).sort(), initial.annotationIds.slice().sort());
  assert.equal(drafted.finishedAnnotations.some((item) => /R\+Kt|K=|展开|下料|槽距|缝距|缝中心距/.test(item.label)), false);
  assert.equal(drafted.activeTarget, "true");
  for (const key of ["canvasStable", "inputStable", "focusStable", "scrollStable"])
    assert.equal(drafted[key], true, `process draft refresh must preserve ${key}`);
  assert.deepEqual(errors, []);
  console.log(`Flexible slit target/blank browser preview passed: ${screenshot}; stub blank: ${blankScreenshot}`);
} finally {
  await browser.close();
}
