import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { browserAssetPath, browserReportDirectory, serveBrowserAsset } from "./browserPackageRuntime.mjs";
import { makeLibraryNavigationFixture } from "./libraryNavigationFixture.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const assemblies = ["bend", "wrap-a-over-b"].map(id => JSON.parse(readFileSync(browserAssetPath(`apps/tube-designer/templates/assembly/${id}/assembly.json`), "utf8")));
const directory = browserReportDirectory("library-navigation-memory");
const userDataDirectory = mkdtempSync(join(directory, "browser-profile-"));
const expected = { templates: "user-b", profiles: "user:user-b", tools: "user::user-b", assemblies: "wrap-a-over-b", components: "user:user-b" };
const cases = [];
let context;

async function open(loading) {
  context = await chromium.launchPersistentContext(userDataDirectory, { headless: true, channel: "msedge", viewport: { width: 1440, height: 1100 } });
  const page = context.pages()[0] ?? await context.newPage();
  await page.route("http://library-navigation-memory.test/**", serveBrowserAsset);
  await page.goto("http://library-navigation-memory.test/");
  await page.evaluate(async ({ fixture, loading }) => {
    const memory = await import("/src/apps/tube-designer/webpage/libraryNavigationMemory.mjs");
    const modules = await Promise.all(["templateLibrary", "profileLibrary", "toolLibrary", "assemblyLibrary", "componentLibrary"]
      .map(name => import(`/src/apps/tube-designer/webpage/${name}.mjs`)));
    const areas = ["templates", "profiles", "tools", "assemblies", "components"];
    const renderers = modules.map((module, index) => module[["renderProductTemplateLibraryLeftPane", "renderProfileLibraryLeftPane",
      "renderToolLibraryLeftPane", "renderAssemblyLibraryLeftPane", "renderComponentLibraryLeftPane"][index]]);
    const handlers = modules.map((module, index) => module[["handleProductTemplateLibraryAction", "handleProfileLibraryAction",
      "handleToolLibraryAction", "handleAssemblyLibraryAction", "handleComponentLibraryAction"][index]]);
    const view = loading ? { scene: { tubeDesigner: {} }, tubeDesignerUserDataLoading: true } : fixture;
    let actionCount = 0;
    const context = { sceneProxy: { resources: {}, async invoke(method, payload) {
      if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") return { schema: "icax.assembly-applicability", schemaVersion: 1,
        templateId: payload.templateId, applicable: true, reason: "" };
      if (method === "TubeDesigner.ResolveAssemblyTemplatePreview" && payload.finishedOnly) return {
        schema: "icax.finished-product-preview", schemaVersion: 1, finishedProduct: structuredClone(payload.finishedProduct),
        layoutShape: payload.finishedProduct.shapeId, sceneParameters: structuredClone(payload.finishedProduct.parameters),
        designParts: Object.entries(payload.finishedProduct.spans).map(([id, span]) => ({ id: `design-${id}`, role: id, label: id,
          request: { ...structuredClone(span), features: [], ends: {} },
          matrix: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1] })),
      };
      throw new Error(`Navigation-only fixture does not implement geometry method: ${method}`);
    } } };
    const draw = () => {
      memory.restoreLibraryNavigation(view);
      document.querySelector("main").innerHTML = areas.map((area, index) => `<section data-test-area="${area}">${renderers[index](context, view)}</section>`).join("");
    };
    document.body.innerHTML = `<style>main{display:flex;gap:8px;font:14px sans-serif}main>section{width:270px;border:1px solid #9ab5b8;padding:6px}button,input{box-sizing:border-box;max-width:100%}.selected{background:#cde9e5}button{cursor:pointer}input{width:100%}</style><main></main>`;
    const action = async target => {
      const area = target.closest("[data-test-area]")?.dataset.testArea;
      if (!area) return;
      view.activeAreaId = area;
      const id = target.dataset.camAction ?? target.dataset.camChangeAction;
      await handlers[areas.indexOf(area)](context, view, id, target, { renderProject: draw });
      memory.rememberLibraryNavigation(view);
      actionCount += 1;
    };
    document.body.addEventListener("click", event => {
      const target = event.target.closest("[data-cam-action]");
      if (target) action(target).catch(error => { window.testError = error.stack; });
    });
    document.body.addEventListener("change", event => {
      if (event.target.matches("[data-cam-change-action]")) action(event.target).catch(error => { window.testError = error.stack; });
    });
    window.navigationFixture = { view, draw, remember: () => memory.rememberLibraryNavigation(view), get actionCount() { return actionCount; },
      load() {
        Object.assign(view, { scene: fixture.scene, tubeDesignerUserDataLoading: false, tubeDesignerUserData: fixture.tubeDesignerUserData,
          tubeDesignerSystemProfiles: fixture.tubeDesignerSystemProfiles, tubeDesignerSystemPunchTools: fixture.tubeDesignerSystemPunchTools,
          tubeDesignerAssemblyTemplates: fixture.tubeDesignerAssemblyTemplates });
        view.tubeDesignerToolLibrary.catalogueStatus = "ready";
        view.tubeDesignerAssemblyLibrary.catalogueStatus = "ready";
        Object.assign(view.tubeDesignerComponentLibrary, { loadState: "loaded", models: fixture.tubeDesignerComponentLibrary.models });
        draw();
      },
      selection: () => ({ templates: view.tubeDesignerProductTemplateLibrary.selectedId, profiles: view.tubeDesignerSelectedProfileId,
        tools: view.tubeDesignerToolLibrary.selectedKey, assemblies: view.tubeDesignerAssemblyLibrary.selectedId,
        components: view.tubeDesignerComponentLibrary.selectedKey }),
    };
    draw();
  }, { fixture: makeLibraryNavigationFixture(assemblies), loading });
  return page;
}

async function act(page, operation) {
  const count = await page.evaluate(() => window.navigationFixture.actionCount);
  await operation();
  await page.waitForFunction(previous => window.testError || window.navigationFixture.actionCount > previous, count);
  const error = await page.evaluate(() => window.testError);
  assert.equal(error, undefined);
}
try {
  let page = await open(false);
  for (const [area, selector] of [
    ["templates", '[data-tube-template-library-scope="user"]'], ["profiles", '[data-tube-profile-library-scope="user"]'],
    ["tools", '[data-tube-tool-library-scope="user"]'], ["components", '[data-component-scope="user"]'],
  ]) await act(page, () => page.locator(`[data-test-area="${area}"] ${selector}`).click());
  for (const area of ["templates", "profiles", "tools", "assemblies", "components"]) {
    const search = page.locator(`[data-test-area="${area}"] input[type="search"]`);
    await act(page, async () => { await search.fill("验收"); await search.dispatchEvent("change"); });
  }
  for (const [area, selector] of [
    ["templates", '[data-tube-template-library-id="user-b"]'], ["profiles", '[data-tube-designer-profile-key="user:user-b"]'],
    ["tools", '[data-tube-tool-library-key="user::user-b"]'], ["assemblies", '[data-tube-assembly-id="wrap-a-over-b"]'],
    ["components", '[data-component-key="user:user-b"]'],
  ]) await act(page, () => page.locator(`[data-test-area="${area}"] ${selector}`).click());
  assert.deepEqual(await page.evaluate(() => window.navigationFixture.selection()), expected);
  for (const area of ["templates", "profiles", "tools", "assemblies", "components"])
    await act(page, () => page.locator(`[data-test-area="${area}"] [aria-expanded]`).first().click());
  const before = await page.evaluate(() => {
    const view = window.navigationFixture.view;
    view.tubeDesignerToolLibrary.preview = { geometry: "private-geometry" };
    view.tubeDesignerComponentLibrary.previewCache.set("private", { mesh: "private-mesh" });
    window.navigationFixture.remember();
    return { fields: JSON.parse(localStorage.getItem("icax.window-state:icax.tube-designer.library-navigation")),
      collapsed: [view.tubeDesignerProductTemplateLibrary, view.tubeDesignerProfileLibrary, view.tubeDesignerToolLibrary,
        view.tubeDesignerAssemblyLibrary, view.tubeDesignerComponentLibrary].map(state => [...state.collapsed]) };
  });
  assert.ok(!JSON.stringify(before.fields).includes("private-"));
  cases.push({ name: "actual resource tabs, searches, cards and group controls save navigation only", status: "passed" });
  await context.close(); context = null;

  page = await open(true);
  await page.evaluate(() => window.navigationFixture.remember());
  const loadingFields = await page.evaluate(() => JSON.parse(localStorage.getItem("icax.window-state:icax.tube-designer.library-navigation")));
  for (const [area, value] of Object.entries(expected)) assert.equal(loadingFields.windows.find(row => row.key === area).fields[
    ["templates", "assemblies"].includes(area) ? "selectedId" : "selectedKey"], value);
  await page.evaluate(() => window.navigationFixture.load());
  assert.deepEqual(await page.evaluate(() => window.navigationFixture.selection()), expected);
  for (const area of ["templates", "profiles", "tools", "assemblies", "components"]) {
    assert.equal(await page.locator(`[data-test-area="${area}"] input[type="search"]`).inputValue(), "验收");
    assert.equal(await page.locator(`[data-test-area="${area}"] [aria-expanded]`).first().getAttribute("aria-expanded"), "false");
  }
  assert.deepEqual(await page.evaluate(() => {
    const view = window.navigationFixture.view;
    return [view.tubeDesignerProductTemplateLibrary, view.tubeDesignerProfileLibrary, view.tubeDesignerToolLibrary,
      view.tubeDesignerAssemblyLibrary, view.tubeDesignerComponentLibrary].map(state => state.collapsed);
  }), before.collapsed);
  cases.push({ name: "same disk profile after browser exit restores all five libraries after async loading", status: "passed" });
  await page.screenshot({ path: join(directory, "library-navigation-after-restart.png"), fullPage: true });
  const report = { passed: true, cases, coverage: { browserProcesses: 2, persistentProfileDirectory: userDataDirectory,
    storageStateInjected: false, actualLibraryRenderersAndNavigationHandlers: true, controlledCatalogueAndAssemblyTransport: true,
    nativeGeometryGeneration: false, standaloneCefEndToEnd: false } };
  writeFileSync(join(directory, "report.json"), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report));
} finally { if (context) await context.close(); }
