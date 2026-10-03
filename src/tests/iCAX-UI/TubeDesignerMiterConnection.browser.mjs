// Exercise the miter template in a real browser viewport. The geometry service
// returns a simple solid with the same planar end cut as the requested tool;
// native BRep coverage lives in AssemblyTemplateSDOTest.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const repositoryRoot = fileURLToPath(new URL("../../../", import.meta.url));
const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const templateId = "two-end-end-angle";
const template = JSON.parse(readFileSync(resolve(sourceRoot,
  `apps/tube-designer/templates/assembly/${templateId}/assembly.json`), "utf8"));
const plans = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
import importlib.util
import json
from pathlib import Path
import sys

sys.stdout.reconfigure(encoding="utf-8")
path = Path(sys.argv[1]) / "src/apps/tube-designer/templates/_shared/assembly_template_runtime.py"
spec = importlib.util.spec_from_file_location("miter_browser_runtime", path)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
print(json.dumps({str(gap): runtime.preview_plan("two-end-end-angle", {
    "jointAngle": 90, "fitGap": gap, "planeRotation": 0,
}) for gap in (0, 12)}, ensure_ascii=False))
`, repositoryRoot], { encoding: "utf8", maxBuffer: 4 * 1024 * 1024,
  env: { ...process.env, PYTHONDONTWRITEBYTECODE: "1" } }));

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://miter-connection.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
        || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://miter-connection.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `
    body{margin:0}.cam-workbench{display:grid;grid-template-columns:280px minmax(0,1fr) 360px;height:1000px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{position:relative;min-width:0;background:#13252d}
    .tube-connection-library-list{height:620px;overflow:auto}
    .tube-connection-library-editor-body{height:915px;overflow:auto}` });
  const result = await page.evaluate(async ({ template, plans }) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { encodeNestingGeometry } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const resources = new Map(), calls = [];
    let viewport, serial = 0;
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [template],
      tubeDesignerSystemProfiles: [{ id: "rect", name: "矩形管", profileForm: "parametric",
        defaultParameters: { width: 60, depth: 40, wallThickness: 2, cornerRadius: 3, innerRadius: 1 },
        descriptor: { parameters: [
          { key: "width", displayName: "宽度", valueType: "number", defaultValue: 60, unit: "mm" },
          { key: "depth", displayName: "高度", valueType: "number", defaultValue: 40, unit: "mm" },
          { key: "wallThickness", displayName: "壁厚", valueType: "number", defaultValue: 2, unit: "mm" },
        ] } }],
      tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example", workModeUserSelected: true } };
    view.viewport = { setVisibleEntityIds() {}, setContinuousRendering() {} };

    // Local X is the length axis. Keep the cut end's four local vertices so the
    // viewport test can verify both downstock cuts after a fit gap change.
    const wedge = (request) => {
      const length = Number(request.length);
      const width = Number(request.parameters.width);
      const depth = Number(request.parameters.depth);
      const start = request.ends?.start?.type === "end-miter" ? request.ends.start : null;
      const end = request.ends?.end?.type === "end-miter" ? request.ends.end : null;
      const cut = start || end;
      const slope = Math.tan(Number(cut?.toolParameters?.angle || 0) * Math.PI / 180);
      if (cut && Number(cut.rotation) !== 90) throw Error("this browser fixture expects a vertical miter plane");
      const halfDepth = depth / 2;
      const low = Math.min(-slope * halfDepth, slope * halfDepth);
      const high = Math.max(-slope * halfDepth, slope * halfDepth);
      const trim = Number(cut?.trim ?? 0);
      const datum = start ? -low + trim : length - high - trim;
      const yz = [[-width / 2, -halfDepth], [width / 2, -halfDepth],
        [width / 2, halfDepth], [-width / 2, halfDepth]];
      const points = [
        ...yz.map(([y, z]) => [start ? datum + slope * z : 0, y, z]),
        ...yz.map(([y, z]) => [end ? datum + slope * z : length, y, z]),
      ];
      const positions = points.flat();
      const indices = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5,
        2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7];
      return { positions, indices, cutLocal: (start ? points.slice(0, 4) : points.slice(4, 8)) };
    };
    const context = { sceneProxy: {
      resources: { async get(url) {
        const bytes = resources.get(url);
        return new Response(bytes ?? null, { status: bytes ? 200 : 404 });
      } },
      invoke(method, request) {
        if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
          const selected = plans[String(request.parameters.fitGap ?? 0)];
          if (!selected) throw Error(`unexpected fit gap ${request.parameters.fitGap}`);
          return Promise.resolve(selected);
        }
        if (method === "TubeDesigner.EvaluateProfilePackage") return Promise.resolve({ profile: {
          contours: [[[0, 0], [1, 0], [1, 1], [0, 1]]],
        } });
        if (method === "TubeDesigner.PreviewPunchWizard") {
          const key = request.previewResourceKey;
          if (!key) throw Error("native preview resource key missing");
          const baseUrl = `memory://miter/${key}/base`;
          const cutUrl = `memory://miter/${key}/cut`;
          const base = new THREE.BoxGeometry(Number(request.length), Number(request.parameters.width),
            Number(request.parameters.depth));
          base.translate(Number(request.length) / 2, 0, 0);
          resources.set(baseUrl, encodeNestingGeometry({ kind: 1,
            positions: [...base.attributes.position.array], indices: [...base.index.array] }));
          const manufactured = wedge(request);
          resources.set(cutUrl, encodeNestingGeometry({ kind: 1,
            positions: manufactured.positions, indices: manufactured.indices }));
          calls.push({ key, cutUrl, cutLocal: manufactured.cutLocal,
            trim: Number(request.ends?.start?.trim ?? request.ends?.end?.trim ?? 0),
            processed: request.ends?.start?.type === "end-miter" || request.ends?.end?.type === "end-miter" });
          return Promise.resolve({ previewComputed: true,
            baseGeometry: { url: baseUrl, version: ++serial },
            geometry: { url: cutUrl, version: serial } });
        }
        throw Error(`unexpected method ${method}`);
      },
    } };
    const viewportFactory = (options) => {
      viewport = createThreeViewport(options);
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
        throw Error("assembly view refresh replaced the DOM");
      }
      library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory });
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    const waitFor = async (condition) => {
      for (let index = 0; index < 150; index++) {
        if (condition()) return;
        await new Promise((resolve) => setTimeout(resolve, 20));
      }
      throw Error(`miter preview timeout: ${JSON.stringify({
        error: view.tubeDesignerAssemblyLibrary.previewError,
        appliedKey: view.tubeDesignerAssemblyLibrary.appliedKey,
        sceneIds: [...(viewport?.sceneObjects?.keys() ?? [])],
      })}`);
    };
    const finishedId = "assembly-finished-shape";
    const blankIds = ["blankA", "blankB"].map((id) => `assembly-blank:manufacturing-${id}`);
    const measure = (id) => {
      const object = viewport.sceneObjects.get(id);
      if (!object) throw Error(`missing miter viewport object ${id}`);
      object.updateMatrixWorld(true);
      const box = new THREE.Box3().setFromObject(object);
      return { min: box.min.toArray(), max: box.max.toArray(), geometryId: object.userData.geometryId };
    };
    render();
    await view.tubeDesignerAssemblyLibrary.previewRequest.promise;
    await waitFor(() => viewport?.sceneObjects?.has(finishedId)
      && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("finished:"));
    const preview = view.tubeDesignerAssemblyLibrary.preview;
    const finishedRows = library.assemblySceneRows(preview);
    const finishedShapeUrl = preview.finishedShape?.rows?.[0]?.data?.geometry?.url;
    const finishedUsesProductShape = finishedRows.length === 1
      && finishedRows[0].entityId === finishedId
      && finishedRows[0].data.geometry.url === finishedShapeUrl
      && !preview.manufacturingParts.some((part) => part.response.geometry.url === finishedShapeUrl);
    const finishedPositions = preview.finishedShape.mesh.positions.slice();
    const finished = measure(finishedId);
    const cutCalls = calls.filter((call) => call.processed);
    const cutFaceDepths = cutCalls.map((call) => {
      const axialPositions = call.cutLocal.map((point) => point[0]);
      return Math.max(...axialPositions) - Math.min(...axialPositions);
    });
    const viewportRoot = document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild;
    const blankButton = document.querySelector('[data-tube-assembly-view="exploded"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view",
      blankButton, { renderProject: render });
    await waitFor(() => blankIds.every((id) => viewport.sceneObjects.has(id))
      && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("exploded:"));
    const blankRows = library.assemblyBlankRows(view.tubeDesignerAssemblyLibrary.preview);
    const blankUsesCuts = blankRows.every((row) => row.data.geometry.url
      === view.tubeDesignerAssemblyLibrary.preview.manufacturingParts
        .find((part) => part.id === row.entityId.replace("assembly-blank:", ""))?.response.geometry.url);
    const blank = blankIds.map(measure);
    const rootRetained = viewportRoot
      === document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild;
    const finishedButton = document.querySelector('[data-tube-assembly-view="finished"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view",
      finishedButton, { renderProject: render });
    await waitFor(() => viewport.sceneObjects.has(finishedId)
      && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("finished:"));
    const fitGap = document.querySelector('[data-tube-assembly-parameter="fitGap"]');
    fitGap.value = "12";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change",
      fitGap, { renderProject: render });
    await waitFor(() => view.tubeDesignerAssemblyLibrary.preview?.key !== preview.key
      && viewport.sceneObjects.has(finishedId)
      && view.tubeDesignerAssemblyLibrary.appliedKey
        === `finished:${view.tubeDesignerAssemblyLibrary.preview?.key}`);
    const revisedPreview = view.tubeDesignerAssemblyLibrary.preview;
    const revisedFinished = measure(finishedId);
    const productShapeUnchanged = JSON.stringify(revisedPreview.finishedShape.mesh.positions)
      === JSON.stringify(finishedPositions);
    const revisedFinishedUsesProductShape = revisedFinished.geometryId
      === revisedPreview.finishedShape.rows[0].data.geometry.url;
    const revisedBlankButton = document.querySelector('[data-tube-assembly-view="exploded"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view",
      revisedBlankButton, { renderProject: render });
    await waitFor(() => blankIds.every((id) => viewport.sceneObjects.has(id))
      && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("exploded:"));
    const revisedBlank = blankIds.map(measure);
    const revisedBlankUsesCuts = library.assemblyBlankRows(revisedPreview).every((row) =>
      row.data.geometry.url === revisedPreview.manufacturingParts
        .find((part) => part.id === row.entityId.replace("assembly-blank:", ""))?.response.geometry.url);
    const revisedFinishedButton = document.querySelector('[data-tube-assembly-view="finished"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view",
      revisedFinishedButton, { renderProject: render });
    await waitFor(() => viewport.sceneObjects.has(finishedId)
      && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("finished:"));
    return { calls, finishedUsesProductShape, blankUsesCuts, revisedBlankUsesCuts,
      finished, revisedFinished, blank, revisedBlank, cutFaceDepths,
      productShapeUnchanged, revisedFinishedUsesProductShape,
      rootRetained: rootRetained && viewportRoot
        === document.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild,
      finishedShapeUrl,
      manufacturedUrls: preview.manufacturingParts.map((part) => part.response.geometry.url),
      finishedLabel: document.querySelector('[data-tube-assembly-view="finished"]')?.textContent.trim(),
      blankLabel: blankButton.textContent.trim() };
  }, { template, plans });
  assert.deepEqual(errors, [], "miter viewport must not throw");
  assert.equal(result.calls.length, 8, "each fit gap needs two design and two manufactured previews");
  assert.equal(result.calls.filter((call) => call.processed).length, 4,
    "both manufactured members must retain their end cut across fit gap changes");
  assert.equal(result.finishedUsesProductShape, true,
    "成品示意 must render one product L shape independent of the miter process");
  assert.equal(result.blankUsesCuts, true, "下料件 must render the same two manufactured cuts");
  assert.equal(result.finished.geometryId, result.finishedShapeUrl,
    "成品示意 WebGL object must contain the product shape resource");
  assert.equal(result.revisedFinishedUsesProductShape, true);
  assert.equal(result.productShapeUnchanged, true,
    "fitGap machining change must not alter the displayed product L geometry");
  assert.deepEqual({ min: result.revisedFinished.min, max: result.revisedFinished.max },
    { min: result.finished.min, max: result.finished.max });
  assert.deepEqual(result.blank.map((part) => part.geometryId), result.manufacturedUrls,
    "下料件 WebGL objects must contain the same manufactured cut resources");
  assert.equal(result.revisedBlankUsesCuts, true);
  assert.equal(result.rootRetained, true, "view switch must retain the canvas and its listeners");
  assert.equal(result.finishedLabel, "成品示意");
  assert.equal(result.blankLabel, "下料件");
  const close = (left, right, tolerance = 0.05) => Math.abs(left - right) < tolerance;
  const product = result.finished;
  assert.ok(close(product.min[0], -260) && close(product.max[2], 260)
    && product.max[0] - product.min[0] > 250 && product.max[2] - product.min[2] > 250,
    "90° product shape needs full horizontal and upright L legs");
  assert.ok(result.cutFaceDepths.every((depth) => depth > 30),
    "both 下料件 meshes must retain their sloping miter cuts");
  assert.ok(result.blank[0].max[0] < product.max[0] - 90,
    "下料件 A must move away from the finished joint");
  assert.ok(result.blank[1].min[2] > product.min[2] + 60,
    "下料件 B must move away from the finished joint");
  assert.ok(result.calls.slice(0, 4).filter((call) => call.processed).every((call) => close(call.trim, 0))
    && result.calls.slice(4).filter((call) => call.processed).every((call) => call.trim > 8),
    "fitGap must change both native miter trim requests");
  assert.ok(result.revisedBlank[0].max[0] < result.blank[0].max[0] - 5
    && result.revisedBlank[1].min[2] > result.blank[1].min[2] + 5,
    "increasing fitGap must move both displayed cut faces into their downstock");
  if (process.env.ICAX_MITER_SCENE_SCREENSHOT) {
    await page.screenshot({ path: process.env.ICAX_MITER_SCENE_SCREENSHOT });
  }
  console.log("miter Edge/WebGL regression passed: product L shape unchanged by fitGap, cut blanks updated");
} finally {
  await browser.close();
}
