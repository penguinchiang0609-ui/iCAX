import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const template = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/t-contact-fit/assembly.json", import.meta.url), "utf8"));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 780 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://assembly-condition.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><meta charset=\"utf-8\"><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
        || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://assembly-condition.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:780px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{min-width:0;position:relative;background:#13252d}.tube-connection-library-list,.tube-connection-library-editor-body{height:180px;overflow:auto}` });
  const result = await page.evaluate(async (template) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [template],
      tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example", workModeUserSelected: true,
        showDiagram: true, parameterDrafts: {} } };
    const context = { sceneProxy: { resources: {}, invoke() { return new Promise(() => {}); } } };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane(context, view);
      const right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      const mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) {
        throw new Error("conditional parameter refresh did not use the app's DOM patch");
      }
    };
    const snapshot = () => {
      const mount = document.querySelector("main");
      const field = mount.querySelector('[data-tube-assembly-parameter="markMode"]');
      const diagram = mount.querySelector('[data-tube-assembly-diagram-dock]');
      const annotationKeys = [...diagram.querySelectorAll('.tool-parameter-svg [data-assembly-annotation-key]')]
        .map((node) => node.dataset.assemblyAnnotationKey);
      const legendKeys = [...diagram.querySelectorAll('.tube-tool-library-diagram-legend [data-assembly-annotation-key]')]
        .map((node) => node.dataset.assemblyAnnotationKey);
      const angle = diagram.querySelector('.tool-parameter-svg [data-assembly-annotation-key="intersectionAngle"]');
      return { field: !!field, value: field?.value ?? null, annotationKeys, legendKeys,
        angleLabel: angle?.getAttribute("aria-label") ?? null,
        diagram: !!mount.querySelector('[data-tube-assembly-diagram-dock] .tool-parameter-svg') };
    };
    const change = async (key, value) => {
      const input = document.querySelector(`[data-tube-assembly-parameter="${key}"]`);
      if (input.type === "checkbox") input.checked = value;
      else input.value = value;
      await library.handleAssemblyLibraryAction(context, view,
        "tube-designer-assembly-parameter-change", input, { renderProject: render });
      return snapshot();
    };
    render();
    const productBefore = snapshot();
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-diagram-mode",
      { dataset: { tubeAssemblyDiagramMode: "process" } }, { renderProject: render });
    const states = [snapshot(), await change("markContact", true), await change("markMode", "contour"),
      await change("markContact", false), await change("markContact", true)];
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-diagram-mode",
      { dataset: { tubeAssemblyDiagramMode: "product" } }, { renderProject: render });
    return { productBefore, productAfter: snapshot(), states,
      draft: view.tubeDesignerAssemblyLibrary.parameterDrafts[template.id]?.markMode,
      values: library.assemblyParameterValues(view, template) };
  }, template);
  assert.deepEqual(errors, []);
  assert.ok(result.states.every((state) => state.diagram), "actual parameter diagram remains open");
  assert.deepEqual(result.states.map((state) => state.field),
    [false, true, true, false, true], "conditional process field follows its declared condition");
  assert.deepEqual(result.states.map((state) => state.annotationKeys),
    [["markContact"], ["markContact", "markMode"], ["markContact", "markMode"],
      ["markContact"], ["markContact", "markMode"]],
  "工艺参数图随贴合线开关显示标注");
  assert.deepEqual(result.states.map((state) => state.legendKeys),
    result.states.map((state) => state.annotationKeys));
  assert.equal(result.draft, "contour", "hidden process draft is retained");
  assert.equal(result.values.markMode, "contour");
  assert.equal(result.states.at(-1).value, "contour");
  assert.deepEqual(result.productBefore.annotationKeys, ["intersectionAngle"]);
  assert.deepEqual(result.productAfter.annotationKeys, ["intersectionAngle"]);
  assert.match(result.productAfter.angleLabel, /固定直角：90 °。主管与支管轴线夹角/);
  console.log("assembly parameter diagrams passed in Edge: finished and conditional process labels remain separate");
} finally {
  await browser.close();
}
