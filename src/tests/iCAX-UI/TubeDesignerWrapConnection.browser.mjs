// Render the product L and its separately manufactured wrap members in a real
// Edge viewport. The native geometry service is stubbed only for bytes: a
// missing per-request resource key deliberately reuses one URL/version stream,
// as native did before this regression, so one tube replaces the other.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const repositoryRoot = fileURLToPath(new URL("../../../", import.meta.url));
const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const ids = ["wrap-a-over-b"];
const templates = ids.map((id) => JSON.parse(readFileSync(resolve(sourceRoot,
  `apps/tube-designer/templates/assembly/${id}/assembly.json`), "utf8")));
const plans = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
import importlib.util
import json
from pathlib import Path
import sys

sys.stdout.reconfigure(encoding="utf-8")
runtime_path = Path(sys.argv[1]) / "src/apps/tube-designer/templates/_shared/assembly_template_runtime.py"
spec = importlib.util.spec_from_file_location("wrap_browser_runtime", runtime_path)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
print(json.dumps({name: {"default": runtime.preview_plan(name),
                         "maleTwo": runtime.preview_plan(name,
                             {"maleFemale": True, "pairCount": "two"}),
                         "maleLongerTabs": runtime.preview_plan(name,
                             {"maleFemale": True, "pairCount": "two", "tabLength": 18}),
                         "maleFour": runtime.preview_plan(name,
                             {"maleFemale": True, "pairCount": "four", "tabLength": 18})}
                  for name in ("wrap-a-over-b",)}, ensure_ascii=False))
`, repositoryRoot], { encoding: "utf8", maxBuffer: 8 * 1024 * 1024,
  env: { ...process.env, PYTHONDONTWRITEBYTECODE: "1" } }));

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  for (const id of ids) {
    const page = await browser.newPage({ viewport: { width: 1400, height: 850 } });
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.route("http://wrap-connection.test/**", (route) => {
      const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
      if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
      const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
      if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
          || !/\.m?js$/.test(path)) return route.abort();
      return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
    });
    await page.goto("http://wrap-connection.test/");
    await page.addStyleTag({ content: tubeDesignerCss + `
      body{margin:0}.cam-workbench{display:grid;grid-template-columns:280px minmax(0,1fr) 320px;height:850px}
      .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
      .cam-viewport{position:relative;min-width:0;background:#13252d}
      .tube-connection-library-list{height:620px;overflow:auto}
      .tube-connection-library-editor-body{height:380px;overflow:auto}` });
    const measured = await page.evaluate(async ({ id, plans, templates }) => {
      const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
      const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
      const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
      const { encodeNestingGeometry } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
      const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
      const resources = new Map(), calls = [];
      let viewport, serial = 0;
      const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
        tubeDesignerAssemblyLibrary: { selectedId: id, workMode: "example", workModeUserSelected: true } };
      view.viewport = { setVisibleEntityIds() {}, setContinuousRendering() {} };
      const context = { sceneProxy: {
        resources: { async get(url) {
          const bytes = resources.get(url);
          return new Response(bytes ?? null, { status: bytes ? 200 : 404 });
        } },
        invoke(method, request) {
          if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
            const plan = plans[request.templateId];
            return Promise.resolve(request.parameters?.maleFemale === true
              ? request.parameters?.pairCount === "four" ? plan.maleFour
                : Number(request.parameters?.tabLength) === 18 ? plan.maleLongerTabs : plan.maleTwo
              : plan.default);
          }
          if (method === "TubeDesigner.EvaluateProfilePackage") return Promise.resolve({ profile: {
            contours: [[[0, 0], [1, 0], [1, 1], [0, 1]]],
          } });
          if (method === "TubeDesigner.PreviewPunchWizard") {
            serial++;
            const key = request.previewResourceKey || "legacy-shared";
            const url = `memory://assembly/${key}`;
            const base = `memory://assembly/${key}/base`;
            const trim = Number(request.ends?.end?.trim || 0);
            const axial = Number(request.length) - trim;
            const box = new THREE.BoxGeometry(axial, Number(request.parameters.width),
              Number(request.parameters.depth));
            // The native linear-part coordinates start at the tube end datum.
            box.translate(axial / 2, 0, 0);
            const bytes = encodeNestingGeometry({ kind: 1,
              positions: [...box.attributes.position.array], indices: [...box.index.array] });
            resources.set(base, bytes);
            resources.set(url, bytes);
            calls.push({ key: request.previewResourceKey, url, version: serial,
              axial, trim, width: request.parameters.width, depth: request.parameters.depth,
              features: (request.features ?? []).filter((feature) => feature.enabled !== false)
                .map((feature) => ({ id: feature.id, tool: feature.toolRef?.id,
                  pairCount: feature.toolParameters?.pairCount,
                  tabLength: feature.toolParameters?.tabLength })),
              end: { type: request.ends?.end?.type, tool: request.ends?.end?.toolRef?.id,
                pairCount: request.ends?.end?.toolParameters?.pairCount,
                tabLength: request.ends?.end?.toolParameters?.tabLength } });
            return Promise.resolve({ previewComputed: true,
              baseGeometry: { url: base, version: serial },
              geometry: { url, version: serial } });
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
          throw Error("assembly refresh replaced the whole DOM");
        }
        library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory });
      };
      view.tubeDesignerAssemblyLibraryRenderProject = render;
      const waitFor = async (condition) => {
        for (let index = 0; index < 150; index++) {
          if (condition()) return;
          await new Promise((resolve) => setTimeout(resolve, 20));
        }
        throw Error(`preview timeout: ${view.tubeDesignerAssemblyLibrary.previewError ?? ""}`);
      };
      const measure = (role, mode) => {
        const entity = mode === "finished" ? `assembly-finished:design-${role}`
          : `assembly-blank:manufacturing-${role === "memberA" ? "longBlank" : "shortBlank"}`;
        const object = viewport.sceneObjects.get(entity);
        if (!object) throw Error(`missing rendered tube ${entity}`);
        object.updateMatrixWorld(true);
        const box = new THREE.Box3().setFromObject(object);
        return { min: box.min.toArray(), max: box.max.toArray(),
          geometryId: object.userData.geometryId };
      };
      render();
      await view.tubeDesignerAssemblyLibrary.previewRequest.promise;
      await waitFor(() => !!viewport?.sceneObjects?.has("assembly-finished:design-memberA")
        && viewport.sceneObjects.has("assembly-finished:design-memberB"));
      const initialCamera = viewport.getCameraState();
      const finished = Object.fromEntries(["memberA", "memberB"].map((role) =>
        [role, measure(role, "finished")]));
      const finishedUrls = library.assemblySceneRows(view.tubeDesignerAssemblyLibrary.preview)
        .map((row) => row.data.geometry.url);
      const initialAppliedKey = view.tubeDesignerAssemblyLibrary.appliedKey;
      const initialCalls = [...calls];
      const obsoleteSetbackAbsent = !document.querySelector(
        '[data-tube-assembly-parameter="setback"]');
      const maleInput = document.querySelector('[data-tube-assembly-parameter="maleFemale"]');
      if (!maleInput) throw Error("missing male/female parameter");
      maleInput.checked = true;
      await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change",
        maleInput, { renderProject: render });
      await waitFor(() => calls.length >= 8
        && view.tubeDesignerAssemblyLibrary.preview?.plan?.parameters?.maleFemale === true
        && view.tubeDesignerAssemblyLibrary.appliedKey !== initialAppliedKey
        && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("finished:"));
      const refreshed = Object.fromEntries(["memberA", "memberB"].map((role) =>
        [role, measure(role, "finished")]));
      const refreshedCamera = viewport.getCameraState();
      const refreshedUrls = library.assemblySceneRows(view.tubeDesignerAssemblyLibrary.preview)
        .map((row) => row.data.geometry.url);
      const maleAppliedKey = view.tubeDesignerAssemblyLibrary.appliedKey;
      const advanced = document.querySelector('details.tube-connection-library-parameter-section');
      if (!advanced) throw Error("missing advanced parameters section");
      if (!advanced.open) advanced.querySelector("summary").click();
      if (!advanced.open) throw Error("advanced parameters did not open");
      const tabLengthInput = document.querySelector('[data-tube-assembly-parameter="tabLength"]');
      if (!tabLengthInput) throw Error("missing tab length parameter");
      tabLengthInput.focus({ preventScroll: true });
      tabLengthInput.value = "18";
      await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change",
        tabLengthInput, { renderProject: render });
      await waitFor(() => calls.length >= 12
        && view.tubeDesignerAssemblyLibrary.preview?.plan?.parameters?.tabLength === 18
        && view.tubeDesignerAssemblyLibrary.appliedKey !== maleAppliedKey
        && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("finished:"));
      const stableInput = document.querySelector('[data-tube-assembly-parameter="tabLength"]') === tabLengthInput
        && document.activeElement === tabLengthInput;
      const maleLongerTabs = Object.fromEntries(["memberA", "memberB"].map((role) =>
        [role, measure(role, "finished")]));
      const maleCamera = viewport.getCameraState();
      const longerTabsAppliedKey = view.tubeDesignerAssemblyLibrary.appliedKey;
      viewport.setViewDirection([0.3, 0.8, 0.5]);
      const userCamera = viewport.getCameraState();
      const blankButton = document.querySelector('[data-tube-assembly-view="exploded"]');
      if (!blankButton) throw Error("missing 下料件 switch");
      await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view",
        blankButton, { renderProject: render });
      await waitFor(() => !!viewport.sceneObjects.has("assembly-blank:manufacturing-longBlank")
        && viewport.sceneObjects.has("assembly-blank:manufacturing-shortBlank")
        && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("exploded:"));
      const blankCamera = viewport.getCameraState();
      const blank = Object.fromEntries(["memberA", "memberB"].map((role) =>
        [role, measure(role, "blank")]));
      const blankUrls = library.assemblyBlankRows(view.tubeDesignerAssemblyLibrary.preview)
        .map((row) => row.data.geometry.url);
      const finishedButton = document.querySelector('[data-tube-assembly-view="finished"]');
      await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view",
        finishedButton, { renderProject: render });
      await waitFor(() => !!viewport.sceneObjects.has("assembly-finished:design-memberA")
        && viewport.sceneObjects.has("assembly-finished:design-memberB")
        && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("finished:"));
      const restoredCamera = viewport.getCameraState();
      const pairInput = document.querySelector('[data-tube-assembly-parameter="pairCount"]');
      if (!pairInput) throw Error("missing male/female pair count");
      const obsoleteSpacingAbsent = !document.querySelector(
        '[data-tube-assembly-parameter="pairSpacing"]');
      pairInput.value = "four";
      await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change",
        pairInput, { renderProject: render });
      await waitFor(() => calls.length >= 16
        && view.tubeDesignerAssemblyLibrary.preview?.plan?.parameters?.pairCount === "four"
        && view.tubeDesignerAssemblyLibrary.appliedKey !== longerTabsAppliedKey
        && view.tubeDesignerAssemblyLibrary.appliedKey.startsWith("finished:"));
      const maleFour = Object.fromEntries(["memberA", "memberB"].map((role) =>
        [role, measure(role, "finished")]));
      const fourCamera = viewport.getCameraState();
      return { calls: calls.slice(0, 16), initialCalls, finished, finishedUrls,
        refreshed, refreshedUrls, stableInput, obsoleteSetbackAbsent, blank, blankUrls,
        initialCamera, refreshedCamera, userCamera, blankCamera, restoredCamera, maleCamera, fourCamera,
        maleLongerTabs, maleFour, obsoleteSpacingAbsent,
        blankObjectIds: [...viewport.sceneObjects.keys()],
        finishedSwitchPresent: !!document.querySelector('[data-tube-assembly-view="finished"]') };
    }, { id, plans, templates });
    assert.deepEqual(errors, [], `${id}: browser console errors`);
    assert.equal(measured.initialCalls.length, 4, `${id}: design and manufactured members each need native preview`);
    assert.equal(measured.calls.length, 16,
      `${id}: each template parameter change must preview all four parts again`);
    assert.equal(new Set(measured.initialCalls.map((call) => call.key)).size, 4,
      `${id}: every native preview needs a distinct resource key`);
    assert.ok(measured.calls.every((call) => /^[A-Za-z0-9_-]{1,96}$/.test(call.key)),
      `${id}: keys must satisfy the native resource contract`);
    for (let offset = 4; offset < 16; offset += 4) {
      assert.deepEqual(measured.calls.slice(offset, offset + 4).map((call) => call.key),
        measured.initialCalls.map((call) => call.key),
        `${id}: an updated member should keep its URL key in branch ${offset / 4}`);
      assert.ok(measured.calls.slice(offset, offset + 4).every((call, index) =>
        call.version > measured.calls[offset - 4 + index].version));
    }
    assert.equal(new Set(measured.finishedUrls).size, 2,
      `${id}: two finished scene rows must retain distinct product geometry URLs`);
    assert.ok(Math.abs(measured.initialCamera.theta - Math.PI / 4) < 1e-8
      && Math.abs(measured.initialCamera.phi - Math.acos(1 / Math.sqrt(3))) < 1e-8,
    `${id}: template-declared back-top-right view must reveal the +Y mating wall`);
    assert.deepEqual(measured.refreshedCamera, measured.initialCamera,
      `${id}: parameter refresh must preserve the chosen camera`);
    assert.deepEqual(measured.maleCamera, measured.initialCamera,
      `${id}: tab-length refresh must preserve the chosen camera`);
    for (const [mode, camera] of [["blank", measured.blankCamera], ["finished", measured.restoredCamera],
      ["four", measured.fourCamera]]) {
      assert.deepEqual(camera, measured.userCamera,
        `${id}: ${mode} view must preserve a camera direction selected by the user`);
    }
    assert.deepEqual(measured.refreshedUrls, measured.finishedUrls,
      `${id}: same members retain their resource URLs after parameter refresh`);
    for (const [label, parts] of [["male/female", measured.refreshed],
      ["longer tabs", measured.maleLongerTabs], ["four pairs", measured.maleFour]]) {
      assert.deepEqual(parts, measured.finished,
        `${id}: ${label} must not change the displayed product L when its dimensions stay the same`);
    }
    const productRequest = (call) => ({ axial: call.axial, trim: call.trim,
      width: call.width, depth: call.depth, features: call.features, end: call.end });
    for (let offset = 4; offset < 16; offset += 4) {
      assert.deepEqual(measured.calls.slice(offset, offset + 2).map(productRequest),
        measured.initialCalls.slice(0, 2).map(productRequest),
        `${id}: assembly process branch ${offset / 4} must leave product member inputs unchanged`);
    }
    assert.deepEqual(measured.initialCalls[2].features, [],
      `${id}: default long blank has no side slots`);
    assert.equal(measured.initialCalls[3].end.type, "end-miter",
      `${id}: default short blank uses its butt-end process`);
    for (const [offset, pairCount, tabLength, processSuffix] of [
      [4, 2, 12, "two"], [8, 2, 18, "two"], [12, 4, 18, "four"],
    ]) {
      const longBlank = measured.calls[offset + 2];
      const shortBlank = measured.calls[offset + 3];
      assert.equal(longBlank.features.length, 1,
        `${id}: enabled process must reach the long manufactured blank`);
      assert.deepEqual(longBlank.features[0], {
        id: `long-side-slots-${processSuffix}`, tool: "paired-side-slots", pairCount, tabLength,
      }, `${id}: the long blank must carry the selected side slots`);
      assert.equal(shortBlank.end.type, "paired-end-tabs",
        `${id}: the short blank must carry end tabs`);
      assert.equal(shortBlank.end.tool, "paired-end-tabs");
      assert.equal(shortBlank.end.pairCount, pairCount);
      assert.equal(shortBlank.end.tabLength, tabLength);
    }
    assert.equal(measured.stableInput, true, `${id}: tab-length refresh must retain numeric input focus`);
    assert.equal(measured.obsoleteSetbackAbsent, true,
      `${id}: an L joint must not expose a virtual short-end setback`);
    assert.equal(measured.obsoleteSpacingAbsent, true,
      `${id}: one ear per edge no longer exposes same-wall pair spacing`);
    assert.equal(measured.finishedSwitchPresent, true);
    const longRole = "memberA";
    const shortRole = "memberB";
    const long = measured.finished[longRole];
    const short = measured.finished[shortRole];
    const span = (box, axis) => box.max[axis] - box.min[axis];
    assert.ok(span(long, 0) > 350 && span(long, 2) < 45,
      `${id}: actual viewport long member must be the horizontal 360 mm tube`);
    assert.ok(span(short, 2) > 175 && span(short, 2) < 185 && span(short, 0) < 45,
      `${id}: actual viewport short member must retain its 180 mm stock length`);
    assert.ok(Math.abs(short.min[2] - (long.min[2] + long.max[2]) / 2) < 0.5,
      `${id}: the short member starts at the long member's end datum`);
    assert.ok(Math.abs((short.min[0] + short.max[0]) / 2 - long.max[0]) < 0.5,
      `${id}: the short member is centered on the long member's end, forming an end-to-end L`);
    const cornerOverlap = Math.min(long.max[0], short.max[0])
      - Math.max(long.min[0], short.min[0]);
    assert.ok(cornerOverlap > 15 && cornerOverlap < 25,
      `${id}: the corner shares only half the short member width, without a T-shaped tail`);
    assert.deepEqual(measured.blankUrls,
      [measured.calls[10].url, measured.calls[11].url],
      `${id}: blank scene must select the processed long and short resources`);
    assert.equal(measured.blank[longRole].geometryId, measured.blankUrls[0]);
    assert.equal(measured.blank[shortRole].geometryId, measured.blankUrls[1]);
    assert.ok(span(measured.blank[longRole], 0) > 350
      && span(measured.blank[shortRole], 2) > 175
      && span(measured.blank[shortRole], 2) < 185,
    `${id}: 下料件 switch must still show distinct 360/180 mm parts`);
    await page.close();
  }
  console.log("wrap assembly Edge/WebGL regression passed: invariant product L, process-specific blanks, and blank switch");
} finally {
  await browser.close();
}
