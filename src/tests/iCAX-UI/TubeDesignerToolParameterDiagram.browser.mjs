// Verifies every parameterized built-in mould diagram and its scoped interaction in Edge.
import assert from "node:assert/strict";
import { existsSync, mkdirSync, readFileSync, readdirSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const root = fileURLToPath(new URL("../../", import.meta.url));
const mouldRoot = resolve(root, "apps/tube-designer/templates/mold");
const tools = readdirSync(mouldRoot, { withFileTypes: true }).filter((entry) => entry.isDirectory()).flatMap((entry) => {
  const path = resolve(mouldRoot, entry.name, "tool.json");
  if (!existsSync(path)) return [];
  const tool = JSON.parse(readFileSync(path, "utf8"));
  if (!tool.parameters?.length) return [];
  tool.defaultParameters = Object.fromEntries(tool.parameters.map((definition) => [definition.key, definition.defaultValue]));
  return [tool];
});
assert.equal(tools.length, 20);
assert.ok(tools.every((tool) => tool.parameterDiagram?.schemaVersion === 2));
for (const tool of tools) {
  const parameters = new Set(tool.parameters.map((definition) => definition.key));
  const annotations = new Set(tool.parameterDiagram.annotations.map((annotation) => annotation.parameter));
  assert.deepEqual(annotations, parameters, `${tool.id} must locate every declared parameter in its own diagram metadata`);
}

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1180, height: 940 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://tool-diagram.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body><main id='app' class='tube-designer-workspace'></main></body>" });
    const file = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !file.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://tool-diagram.test/");
  await page.addStyleTag({ content: `*{box-sizing:border-box}body{margin:0;background:#e9eff1;font-family:Segoe UI,Microsoft YaHei,sans-serif}#app{width:520px;min-height:100vh;margin:auto;background:#f6f8f9}.tube-tool-library-editor-body{overflow:visible!important}${tubeDesignerCss}` });
  await page.evaluate(async (catalogue) => {
    const library = await import("/src/apps/tube-designer/webpage/toolLibrary.mjs");
    const diagram = await import("/src/apps/tube-designer/webpage/toolParameterDiagram.mjs");
    const mount = document.querySelector("#app");
    const view = { activeAreaId: "tools", pending: false, tubeDesignerSystemPunchTools: catalogue,
      tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::v-notch-sharp", showToolDiagram: true, parameterDrafts: {} } };
    const render = () => { mount.innerHTML = library.renderToolLibraryRightPane({}, view) + library.renderToolLibraryViewportOverlay({}, view); diagram.bindToolParameterDiagrams(mount); };
    const ops = { renderProject: render };
    document.addEventListener("change", async (event) => {
      const action = event.target?.dataset?.camChangeAction ?? "";
      if (action) await library.handleToolLibraryAction({}, view, action, event.target, ops);
    });
    render();
    window.fixture = { library, diagram, catalogue, view, render };
  }, tools);

  const scope = page.locator("#app");
  await scope.locator('[data-tool-parameter-key="angle"]').focus();
  assert.equal(await scope.locator('svg [data-tool-annotation-key="angle"]').evaluate((node) => node.classList.contains("is-active")), true);
  assert.equal(await scope.locator('.tube-tool-library-diagram-row[data-tool-annotation-key="angle"]').evaluate((node) => node.classList.contains("is-active")), true);
  await scope.locator('svg [data-tool-annotation-key="leaveBottom"] .tool-diagram-dimension-label').click();
  assert.equal(await page.evaluate(() => document.activeElement?.dataset.toolParameterKey), "leaveBottom");
  await scope.locator('.tube-tool-library-diagram-row[data-tool-annotation-key="bottomStrategy"]').click();
  assert.equal(await page.evaluate(() => document.activeElement?.dataset.toolParameterKey), "bottomStrategy");

  // The same shared condition rules drive the editor, legend and drawing.
  await scope.locator('[data-tool-parameter-key="bottomStrategy"]').selectOption("rounded");
  assert.equal(await scope.locator('[data-tool-parameter-key="roundRadius"]').count(), 1);
  assert.equal(await scope.locator('[data-tool-annotation-key="roundRadius"]').count() >= 2, true);
  assert.equal(await scope.locator('[data-tool-parameter-key="reliefLength"]').count(), 0);
  assert.equal(await scope.locator('[data-tool-annotation-key="reliefLength"]').count(), 0);
  await scope.locator('[data-tool-parameter-key="bendCompensation"]').check();
  assert.equal(await scope.locator('[data-tool-parameter-key="useDefaultKFactor"]').count(), 1);
  await scope.locator('[data-tool-parameter-key="useDefaultKFactor"]').uncheck();
  assert.equal(await scope.locator('[data-tool-parameter-key="kFactor"]').count(), 1);
  assert.equal(await scope.locator('[data-tool-annotation-key="kFactor"]').count() >= 2, true);
  await scope.locator('[data-tool-parameter-key="bottomStrategy"]').selectOption("relief");
  assert.equal(await scope.locator('[data-tool-parameter-key="roundRadius"]').count(), 0);
  assert.equal(await scope.locator('[data-tool-annotation-key="roundRadius"]').count(), 0);
  assert.equal(await scope.locator('[data-tool-parameter-key="reliefLength"]').count(), 1);
  assert.equal(await scope.locator('[data-tool-annotation-key="reliefLength"]').count() >= 2, true);

  // Every parameterized built-in owns its drawing metadata; the renderer has no mould-id branches.
  const catalogueResults = await page.evaluate(() => {
    const fixture = window.fixture;
    return fixture.catalogue.map((tool) => {
      fixture.view.tubeDesignerToolLibrary.selectedKey = `system::${tool.id}`;
      fixture.view.tubeDesignerToolLibrary.parameterDrafts = {};
      fixture.view.tubeDesignerToolLibrary.showToolDiagram = true;
      fixture.render();
      const root = document.querySelector("#app");
      const controls = [...root.querySelectorAll("[data-tool-parameter-key]")].map((node) => node.dataset.toolParameterKey);
      const svgAnnotations = [...root.querySelectorAll("svg [data-tool-annotation-key]")].map((node) => node.dataset.toolAnnotationKey);
      const legendAnnotations = [...root.querySelectorAll(".tube-tool-library-diagram-row[data-tool-annotation-key]")].map((node) => node.dataset.toolAnnotationKey);
      const svg = root.querySelector(".tool-parameter-svg.is-rich");
      const bounds = svg?.getBoundingClientRect();
      const clipped = !svg ? ["missing-svg"] : [...svg.querySelectorAll("text")].filter((node) => {
        const box = node.getBoundingClientRect();
        return box.width > 0 && (box.left < bounds.left - 1 || box.top < bounds.top - 1 || box.right > bounds.right + 1 || box.bottom > bounds.bottom + 1);
      }).map((node) => node.textContent);
      return { id: tool.id, controls, svgAnnotations, legendAnnotations, clipped };
    });
  });
  for (const result of catalogueResults) {
    assert.deepEqual(new Set(result.legendAnnotations), new Set(result.controls), JSON.stringify(result));
    assert.deepEqual(new Set(result.svgAnnotations), new Set(result.controls), JSON.stringify(result));
    assert.deepEqual(result.clipped, [], JSON.stringify(result));
  }
  await page.setViewportSize({ width: 420, height: 900 });
  const narrow = await page.evaluate(() => {
    const fixture = window.fixture;
    document.querySelector("#app").style.width = "380px";
    fixture.view.tubeDesignerToolLibrary.selectedKey = "system::v-notch-sharp";
    fixture.view.tubeDesignerToolLibrary.parameterDrafts = {};
    fixture.render();
    const root = document.querySelector("[data-tool-parameter-scope]");
    return { overflow: root.scrollWidth - root.clientWidth, width: root.getBoundingClientRect().width };
  });
  assert.ok(narrow.width <= 380 && narrow.overflow <= 2, JSON.stringify(narrow));
  if (process.env.ICAX_ARTIFACT_DIR) {
    mkdirSync(process.env.ICAX_ARTIFACT_DIR, { recursive: true });
    await page.screenshot({ path: resolve(process.env.ICAX_ARTIFACT_DIR, "tool-parameter-diagram-v-notch.png"), fullPage: true });
  }
  assert.deepEqual(errors, []);
  console.log("Tool parameter diagrams: 20 built-ins, scoped focus/click, live values and conditional annotations passed in Edge.");
} finally {
  await browser.close();
}
