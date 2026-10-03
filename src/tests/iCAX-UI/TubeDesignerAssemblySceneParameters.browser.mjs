import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const source = fileURLToPath(new URL("../../", import.meta.url));
const template = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/two-end-end-angle/assembly.json", import.meta.url), "utf8"));
const bend = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/bend/assembly.json", import.meta.url), "utf8"));
template.parameters = template.parameters.map((item) => item.key === "jointAngle"
  ? { ...item, scope: "scene" } : item);
const profiles = {
  system: [{
    id: "rect", name: "矩形管", profileForm: "parametric", defaultParameters: { width: 60, depth: 40 },
    descriptor: { parameters: [
      { key: "width", displayName: "宽度", valueType: "number", defaultValue: 60, min: 1 },
      { key: "depth", displayName: "高度", valueType: "number", defaultValue: 40, min: 1 },
    ] },
  }],
  user: [{
    id: "custom", name: "我的管型", profileForm: "parametric",
    defaultParameters: { width: 50, note: "ABCD" }, descriptor: { parameters: [
      { key: "width", displayName: "宽度", valueType: "number", defaultValue: 50, min: 1 },
      { key: "note", displayName: "标记", valueType: "string", defaultValue: "ABCD" },
    ] },
  }],
};

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1180, height: 680 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://assembly-scene.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://assembly-scene.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `
    body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 340px;height:680px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{min-width:0;position:relative;background:#13252d}
    .tube-connection-library-panel .tube-connection-library-list{height:45px;max-height:45px;flex:0 0 45px;overflow:auto}
    .tube-connection-library-editor-body{height:160px;overflow:auto}
  ` });
  const result = await page.evaluate(async ({ template, bend, profiles }) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [template, bend],
      tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example", workModeUserSelected: true,
        processDrafts: { [template.id]: { draft: { amount: 7 } } } } };
    const requests = [];
    const deferred = [];
    let holdFinished = false;
    const matrixFor = (id, span, product) => {
      if (id !== "armB") return [1, 0, 0, -span.length, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
      const angle = product.parameters.angle * Math.PI / 180, rotation = product.parameters.planeRotation * Math.PI / 180;
      const cosine = Math.cos(angle), sine = Math.sin(angle), spin = Math.sin(rotation), turn = Math.cos(rotation);
      return [cosine, 0, -sine, 0, -sine * spin, turn, -cosine * spin, 0,
        sine * turn, spin, cosine * turn, 0, 0, 0, 0, 1];
    };
    const planFrom = (payload) => ({
      schema: "icax.finished-product-preview", schemaVersion: 1,
      finishedProduct: structuredClone(payload.finishedProduct), layoutShape: payload.finishedProduct.shapeId,
      sceneParameters: structuredClone(payload.finishedProduct.parameters),
      designParts: Object.entries(payload.finishedProduct.spans).map(([id, span]) => ({
        id: `design-${id}`, role: id, label: id,
        request: { ...structuredClone(span), features: [], ends: {} },
        matrix: matrixFor(id, span, payload.finishedProduct), compareMatrix: matrixFor(id, span, payload.finishedProduct),
      })),
    });
    const context = { sceneProxy: { resources: {}, invoke(method, payload) {
      if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") return Promise.resolve({ schema: "icax.assembly-applicability", schemaVersion: 1, templateId: payload.templateId, applicable: payload.templateId === bend.id || payload.processInput.geometry.jointAngle <= 160, reason: "当前工艺不支持此成品角度" });
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview" && payload.finishedOnly) {
        requests.push(payload);
        return holdFinished ? new Promise((resolve) => deferred.push({ payload, resolve: () => resolve(planFrom(payload)) })) : Promise.resolve(planFrom(payload));
      }
      if (method === "TubeDesigner.PreviewPunchWizard") {
        return Promise.resolve({ previewComputed: true,
          baseGeometry: { url: `memory://base-${Math.random()}`, version: 1 },
          geometry: { url: `memory://finished-${Math.random()}`, version: 1 } });
      }
      throw new Error(`unexpected ${method}`);
    } } };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane(context, view);
      const right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      const mount = document.querySelector("main");
      if (mount) context.mount = mount;
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas id="stable"></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) {
        throw new Error("scene edit did not use local DOM patch");
      }
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    render();
    await Promise.resolve();
    const mount = document.querySelector("main");
    const canvas = mount.querySelector("canvas");
    let canvasClicks = 0;
    canvas.addEventListener("click", () => { canvasClicks += 1; });
    const beforeHydration = mount.querySelector('[data-finished-span="armB"] select').disabled;
    view.tubeDesignerSystemProfiles = profiles.system;
    view.tubeDesignerUserData = { profiles: profiles.user };
    render();
    const profileSelect = mount.querySelector('[data-finished-span="armB"] select');
    const hydrated = [...profileSelect.options].some((option) => option.value === "user:custom");
    profileSelect.value = "user:custom";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-finished-profile-change", profileSelect, { renderProject: render });
    const width = mount.querySelector('[data-finished-span="armB"] [data-finished-profile-parameter="width"]');
    width.value = "72";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-finished-profile-parameter-change", width, { renderProject: render });
    const note = mount.querySelector('[data-finished-span="armB"] [data-finished-profile-parameter="note"]');
    note.value = "ABCDE";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-finished-profile-parameter-change", note, { renderProject: render });
    const sceneAngle = mount.querySelector('[data-finished-parameter="angle"]');
    sceneAngle.value = "110";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-finished-parameter-change", sceneAngle, { renderProject: render });
    const processGap = mount.querySelector('[data-tube-assembly-parameter="fitGap"]');
    processGap.value = "3";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-assembly-parameter-change", processGap, { renderProject: render });
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-assembly-select", { dataset: { tubeAssemblyId: bend.id } }, { renderProject: render });
    const carried = library.assemblySceneParts(view, bend).segmentB;
    const bendAngle = mount.querySelector('[data-finished-parameter="angle"]');
    const carriedAngle = Number(bendAngle?.value);
    const processRadius = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    processRadius.value = "55";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-assembly-parameter-change", processRadius, { renderProject: render });
    bendAngle.value = "165";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-finished-parameter-change", bendAngle, { renderProject: render });
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-assembly-select", { dataset: { tubeAssemblyId: template.id } }, { renderProject: render });
    const clippedAngle = Number(mount.querySelector('[data-finished-parameter="angle"]')?.value);
    const clippedWarning = view.tubeDesignerAssemblyLibrary.selectionProblem.includes("不支持");
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-assembly-select", { dataset: { tubeAssemblyId: bend.id } }, { renderProject: render });
    const retainedAngle = Number(mount.querySelector('[data-finished-parameter="angle"]')?.value);
    const resetAngle = mount.querySelector('[data-finished-parameter="angle"]');
    resetAngle.value = "110";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-finished-parameter-change", resetAngle, { renderProject: render });
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-assembly-select", { dataset: { tubeAssemblyId: template.id } }, { renderProject: render });
    view.tubeDesignerFinishedProduct.disclosure ??= {};
    view.tubeDesignerFinishedProduct.disclosure.l ??= {};
    view.tubeDesignerFinishedProduct.disclosure.l.armB = true;
    view.tubeDesignerAssemblyLibrary.showDiagram = true;
    mount.querySelector('[data-finished-span="armB"]').open = true;
    render();
    const stableNote = mount.querySelector('[data-finished-span="armB"] [data-finished-profile-parameter="note"]');
    stableNote.focus({ preventScroll: true });
    stableNote.setSelectionRange(1, 3);
    const leftScroll = mount.querySelector(".tube-connection-library-list");
    const rightScroll = mount.querySelector(".tube-connection-library-editor-body");
    leftScroll.scrollTop = 55;
    rightScroll.scrollTop = 125;
    const scrolledBefore = { left: leftScroll.scrollTop, right: rightScroll.scrollTop };
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-diagram-mode",
      { dataset: { tubeAssemblyDiagramMode: "process" } }, { renderProject: render });
    const afterDiagram = { left: leftScroll.scrollTop, right: rightScroll.scrollTop,
      sameCanvas: mount.querySelector("canvas") === canvas,
      sameInput: mount.querySelector('[data-finished-span="armB"] [data-finished-profile-parameter="note"]') === stableNote,
      focused: document.activeElement === stableNote,
      selection: [stableNote.selectionStart, stableNote.selectionEnd],
      mode: mount.querySelector('[data-tube-assembly-diagram-mode="process"]')?.getAttribute("aria-pressed") };
    const processBefore = JSON.stringify(view.tubeDesignerAssemblyLibrary.processDrafts);
    holdFinished = true;
    const length = mount.querySelector('[data-finished-span="armB"] [data-cam-change-action="tube-designer-finished-length-change"]');
    length.value = "300";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-finished-length-change", length, { renderProject: render });
    await new Promise((resolve) => setTimeout(resolve, 0));
    length.value = "310";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-finished-length-change", length, { renderProject: render });
    await Promise.resolve();
    rightScroll.scrollTop = Math.min(rightScroll.scrollTop + 20, rightScroll.scrollHeight);
    const beforeResponse = rightScroll.scrollTop;
    deferred.slice(0, -1).forEach((item) => item.resolve());
    await Promise.resolve();
    const staleIgnored = view.tubeDesignerAssemblyLibrary.preview?.plan?.finishedProduct?.spans?.armB?.length !== 260;
    deferred.at(-1).resolve();
    for (let i = 0; i < 40 && view.tubeDesignerAssemblyLibrary.previewRequest; i += 1)
      await new Promise((resolve) => setTimeout(resolve, 0));
    canvas.click();
    return {
      beforeHydration, hydrated, requests, scrolledBefore, afterDiagram, beforeResponse,
      carriedAngle, clippedAngle, clippedWarning, retainedAngle,
      leftMetrics: { scrollHeight: leftScroll.scrollHeight, clientHeight: leftScroll.clientHeight,
        cssHeight: getComputedStyle(leftScroll).height, cards: leftScroll.querySelectorAll(".tube-connection-library-card").length },
      afterScroll: { left: leftScroll.scrollTop, right: rightScroll.scrollTop },
      sameCanvas: mount.querySelector("canvas") === canvas,
      sameInput: mount.querySelector('[data-finished-span="armB"] [data-finished-profile-parameter="note"]') === stableNote,
      focused: document.activeElement === stableNote,
      selection: [stableNote.selectionStart, stableNote.selectionEnd],
      canvasClicks, staleIgnored,
      processUnchanged: JSON.stringify(view.tubeDesignerAssemblyLibrary.processDrafts) === processBefore,
      carried: { profileRef: carried.profileRef, note: carried.parameters.note },
      appliedLength: view.tubeDesignerAssemblyLibrary.preview?.plan?.finishedProduct?.spans?.armB?.length,
      sections: [!!mount.querySelector("[data-tube-assembly-scene-parameters]"),
        mount.querySelector(".tube-connection-library-editor-body")?.textContent.includes("工艺参数")],
    };
  }, { template, bend, profiles });
  assert.deepEqual(errors, []);
  assert.equal(result.beforeHydration, true);
  assert.equal(result.hydrated, true, "asynchronously loaded system and user profiles become selectable");
  assert.ok(result.requests.length >= 2);
  const latest = result.requests.at(-1);
  assert.equal(latest.finishedProduct.spans.armB.length, 310);
  assert.deepEqual(latest.finishedProduct.spans.armB.profileRef, { scope: "user", id: "custom" });
  assert.equal(latest.finishedProduct.spans.armB.parameters.width, 72);
  assert.equal(latest.finishedProduct.spans.armB.parameters.note, "ABCDE");
  assert.deepEqual(result.carried, { profileRef: { scope: "user", id: "custom" }, note: "ABCDE" });
  assert.equal(result.carriedAngle, 110, "declared sceneKey carries L angle across process templates");
  assert.equal(result.clippedAngle, 165, "工艺不适用必须保留成品角度");
  assert.equal(result.clippedWarning, true);
  assert.equal(result.retainedAngle, 165, "out-of-range angle remains saved for a compatible process");
  assert.equal(latest.finishedProduct.parameters.angle, 110);
  assert.ok(latest.finishedOnly);
  assert.ok(!Object.hasOwn(latest, "templateId") && !Object.hasOwn(latest, "parameters"));
  assert.ok(!Object.hasOwn(latest, "sceneParts") && !Object.hasOwn(latest, "sceneParameters"));
  assert.equal(result.processUnchanged, true);
  assert.equal(result.appliedLength, 310);
  assert.equal(result.staleIgnored, true);
  assert.deepEqual(result.sections, [true, true]);
  assert.ok(result.scrolledBefore.left > 0 && result.scrolledBefore.right > 0,
    `both sidebars must scroll: ${JSON.stringify([result.scrolledBefore, result.leftMetrics])}`);
  assert.deepEqual(result.afterDiagram, { ...result.scrolledBefore, sameCanvas: true,
    sameInput: true, focused: true, selection: [1, 3], mode: "true" },
  "switching product and process parameter diagrams preserves both scrollbars and the active text selection");
  assert.deepEqual(result.afterScroll, { left: result.scrolledBefore.left, right: result.beforeResponse });
  assert.equal(result.sameCanvas, true);
  assert.equal(result.sameInput, true);
  assert.equal(result.focused, true);
  assert.deepEqual(result.selection, [1, 3]);
  assert.equal(result.canvasClicks, 1);
  console.log("assembly scene parameters passed in Edge: independent inputs, async preview, focus and scroll");
} finally {
  await browser.close();
}
