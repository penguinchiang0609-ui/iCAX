import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

// Controlled service delays make the preview request waterfall measurable in a
// real browser without depending on the speed of a local native installation.
const templateId = process.env.ICAX_ASSEMBLY_PERF_TEMPLATE === "two-end-middle"
  ? "two-end-middle" : "two-end-end-angle";
const template = JSON.parse(readFileSync(new URL(
  `../../apps/tube-designer/templates/assembly/${templateId}/assembly.json`, import.meta.url), "utf8"));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 760 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://assembly-performance.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://assembly-performance.test/");
  const profile = await page.evaluate(async (template) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [template],
      tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example", workModeUserSelected: true } };
    const events = [];
    const renders = [];
    let run = 1;
    let resource = 0;
    const after = (ms, value) => new Promise((resolve) => setTimeout(() => resolve(value), ms));
    const timed = async (kind, delay, value) => {
      const event = { run, kind, start: performance.now() };
      events.push(event);
      const result = await after(delay, value);
      event.end = performance.now();
      return result;
    };
    const makePlan = (payload) => {
      const members = template.previewScene.designParts.map((part) => {
        const request = payload.sceneParts?.[part.role] ?? part;
        return { id: `design-${part.role}`, role: part.role, label: part.role,
          request: { ...request, features: [], ends: {} }, matrix: identity };
      });
      return { schema: "icax.assembly-preview-plan", templateId: payload.templateId,
        sceneParameters: payload.sceneParameters,
        resolvedWorkflow: { realization: "separate", parameterEffects: {} },
        designParts: members,
        manufacturingParts: members.map((member) => ({ id: `blank-${member.role}`,
          sourceRole: member.role, participantRoles: [member.role], label: member.role,
          request: { ...member.request, ends: { end: { process: "miter" } } }, matrix: identity })) };
    };
    const context = { sceneProxy: { resources: {}, invoke(method, payload) {
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview")
        return timed("plan", 30, makePlan(payload));
      if (method === "TubeDesigner.PreviewPunchWizard") {
        resource += 1;
        return timed("part", 50, { previewComputed: true,
          baseGeometry: { url: `memory://base-${resource}`, version: 1 },
          geometry: { url: `memory://result-${resource}`, version: 1 } });
      }
      throw new Error(`Unexpected service: ${method}`);
    } } };
    const viewportFactory = () => {
      const root = document.createElement("div");
      const viewport = { root, mount(host) { host.replaceChildren(root); return viewport; },
        async applyViewSnapshot(snapshot) {
          return timed("scene", 20, { applied: true, entityIds: snapshot.rows.map((row) => row.entityId),
            missingGeometryEntityIds: [] });
        }, setVisibleEntityIds() {}, setDimensionAnnotations() {}, setStandardView() {},
        fitViewToViewport() {}, getCameraState() { return {}; }, setCameraState() {}, dispose() {} };
      return viewport;
    };
    const render = () => {
      const start = performance.now();
      const left = library.renderAssemblyLibraryLeftPane(context, view);
      const right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      const mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside>`
          + `<div class="cam-viewport">${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) {
        throw new Error("Assembly render did not use DOM patch");
      }
      library.attachAssemblyLibraryViewports(context, view, document.querySelector("main"), { viewportFactory });
      renders.push({ run, ms: performance.now() - start });
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    const waitForPreview = async () => {
      for (let index = 0; index < 500; index += 1) {
        const state = view.tubeDesignerAssemblyLibrary;
        if (state.preview && !state.previewRequest && state.appliedKey) return;
        await after(5);
      }
      throw new Error("Assembly preview did not complete");
    };
    const starts = [];
    starts.push(performance.now());
    render();
    await waitForPreview();
    const firstEnd = performance.now();
    run = 2;
    starts.push(performance.now());
    const input = document.querySelector('[data-tube-assembly-parameter="fitGap"]')
      ?? document.querySelector('[data-tube-assembly-parameter="intersectionAngle"]');
    input.value = String(Number(input.value) + 1);
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change",
      input, { renderProject: render });
    await waitForPreview();
    const secondEnd = performance.now();
    library.disposeAssemblyLibraryViewports(view);
    return [1, 2].map((number) => {
      const calls = events.filter((event) => event.run === number);
      const renderCalls = renders.filter((event) => event.run === number);
      return { run: number, elapsedMs: Number(((number === 1 ? firstEnd : secondEnd) - starts[number - 1]).toFixed(1)),
        planCalls: calls.filter((event) => event.kind === "plan").length,
        partCalls: calls.filter((event) => event.kind === "part").length,
        sceneCalls: calls.filter((event) => event.kind === "scene").length,
        renderCalls: renderCalls.length,
        renderMs: Number(renderCalls.reduce((sum, event) => sum + event.ms, 0).toFixed(1)),
        waterfall: calls.map((event) => ({ kind: event.kind,
          startMs: Number((event.start - starts[number - 1]).toFixed(1)),
          endMs: Number((event.end - starts[number - 1]).toFixed(1)) })) };
    });
  }, template);
  for (const run of profile) {
    assert.equal(run.planCalls, 1, "one edit should request one assembly plan");
    assert.ok(run.partCalls <= 4, "one edit should not duplicate part previews");
    assert.equal(run.sceneCalls, 1, "one edit should apply the scene once");
  }
  assert.deepEqual(errors, []);
  console.log(JSON.stringify(profile));
} finally {
  await browser.close();
}
