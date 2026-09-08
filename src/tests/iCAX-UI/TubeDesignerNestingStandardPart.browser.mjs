// Exercises the rendered dialog with native browser inputs and the production styles.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const root = fileURLToPath(new URL("../../", import.meta.url));
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage();
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://standard-part.test/**", (route) => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body><div id='app' class='tube-designer-workspace'></div></body>" });
    const file = resolve(root, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://standard-part.test/");
  const commonCss = readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css", import.meta.url), "utf8");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}" + commonCss + tubeDesignerCss });
  await page.evaluate(async () => {
    const standard = await import("/src/apps/tube-designer/webpage/nestingStandardPart.mjs");
    const profile = (width = 40) => ({
      schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "imported-dxf",
      name: "矩形管", width, depth: 20,
      contours: [
        { kind: "polygon", points: [[-width / 2, -10], [width / 2, -10], [width / 2, 10], [-width / 2, 10]] },
        { kind: "polygon", points: [[-width / 2 + 2, -8], [width / 2 - 2, -8], [width / 2 - 2, 8], [-width / 2 + 2, 8]] },
      ],
    });
    const program = {
      id: "rect", name: "程式矩形管", profileType: "parametric-package", previewProfile: profile(),
      defaultParameters: { width: 40, depth: 20, wallThickness: 2, cornerRadius: 2 },
      descriptor: { parameters: [
        { key: "width", displayName: "外宽", valueType: "number", defaultValue: 40, min: 1 },
        { key: "depth", displayName: "外高", valueType: "number", defaultValue: 20, min: 1 },
        { key: "wallThickness", displayName: "壁厚", valueType: "number", defaultValue: 2, min: 0.1 },
        { key: "cornerRadius", displayName: "外圆角", valueType: "number", defaultValue: 2, min: 0 },
      ] },
    };
    const calls = [];
    const view = {
      activeAreaId: "nesting", pending: false, scene: { tubeDesigner: { nestingGroups: [] } },
      tubeDesignerSystemProfiles: [program],
      tubeDesignerUserData: { profiles: [{ ...profile(24), id: "fixed", name: "我的定式管型" }] },
      tubeDesignerTemplateProfiles: [{ ...program, id: "template", templateId: "guard", name: "模板不可出现" }],
    };
    const context = {
      appProxy: { bridge: { async openFileDialog() { return "D:\\Profiles\\fixture.dxf"; } } },
      productProxy: { async invoke(method, payload) {
        calls.push({ method, payload: structuredClone(payload) });
        return { profile: method === "TubeDesigner.ImportProfileDxf" ? { ...profile(60), name: "本地定式 DXF" } : profile(payload.parameters.width) };
      } },
      sceneProxy: { async invoke(method, payload) {
        calls.push({ method, payload: structuredClone(payload) });
        return { partEntityId: "added-standard-part", tubeDesigner: { nestingGroups: [] } };
      } },
    };
    const ops = { renderProject() { document.querySelector("#app").innerHTML = standard.renderNestingStandardPartDialog(view); } };
    window.fixture = { view, calls, standard, context, ops };
    document.addEventListener("change", async (event) => {
      await standard.handleNestingStandardPartAction(context, view, event.target.dataset.camChangeAction ?? "", event.target, ops);
    });
    document.addEventListener("click", async (event) => {
      const target = event.target.closest("[data-cam-action]");
      if (target) await standard.handleNestingStandardPartAction(context, view, target.dataset.camAction, target, ops);
    });
    await standard.handleNestingStandardPartRibbonCommand(context, view, "nesting.add-standard-part", ops);
  });

  const control = (name) => page.locator(`[data-cam-change-action="tube-designer-nesting-standard-${name}"]`);
  const action = (name) => page.locator(`[data-cam-action="tube-designer-nesting-standard-${name}"]`);
  for (const size of [{ width: 1600, height: 1000 }, { width: 1024, height: 768 }, { width: 620, height: 700 }]) {
    await page.setViewportSize(size);
    assert.equal(await page.locator('option[value^="template:"]').count(), 0);
    assert.equal(await page.locator(".tube-nesting-standard-part-preview svg").count(), 1);
    const bounds = await page.locator('[role="dialog"]').evaluate((element) => {
      const rect = element.getBoundingClientRect();
      return { left: rect.left, top: rect.top, right: rect.right, bottom: rect.bottom, overflow: element.scrollWidth - element.clientWidth };
    });
    assert.ok(bounds.left >= 0 && bounds.top >= 0 && bounds.right <= size.width && bounds.bottom <= size.height, JSON.stringify({ size, bounds }));
    assert.ok(bounds.overflow <= 2, JSON.stringify(bounds));
    const previewBounds = await page.locator(".tube-nesting-standard-part-preview").evaluate((element) => {
      const section = element.querySelector(".tube-nesting-standard-part-section, .td-profile-parameter-canvas").getBoundingClientRect();
      const svg = element.querySelector("svg").getBoundingClientRect();
      const caption = element.querySelector("figcaption, .td-profile-parameter-legend").getBoundingClientRect();
      return { sectionBottom: section.bottom, svgBottom: svg.bottom, captionTop: caption.top };
    });
    assert.ok(previewBounds.svgBottom <= previewBounds.sectionBottom + 1 && previewBounds.svgBottom <= previewBounds.captionTop, JSON.stringify({ size, previewBounds }));
    assert.equal(await action("confirm").isVisible(), true);
    if (process.env.ICAX_ARTIFACT_DIR) {
      mkdirSync(process.env.ICAX_ARTIFACT_DIR, { recursive: true });
      await page.screenshot({ path: resolve(process.env.ICAX_ARTIFACT_DIR, `nesting-standard-part-${size.width}.png`) });
    }
  }
  await page.setViewportSize({ width: 1024, height: 768 });
  await page.locator('[data-standard-part-parameter="width"]').fill("55");
  await page.locator('[data-standard-part-parameter="width"]').press("Tab");
  await page.waitForFunction(() => window.fixture.view.tubeDesignerNestingStandardPartDraft.parameters.width === 55 && !window.fixture.view.pending);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerNestingStandardPartDraft.profile.width), 55);
  await control("profile-select").selectOption("user:fixed");
  assert.equal(await page.locator("[data-standard-part-parameter]").count(), 0);
  await control("profile-select").selectOption("__dxf__");
  await page.waitForFunction(() => window.fixture.view.tubeDesignerNestingStandardPartDraft.importedProfile?.name === "本地定式 DXF");
  await control("length").fill("875");
  await action("confirm").click();
  await page.waitForFunction(() => window.fixture.view.tubeDesignerNestingStandardPartDraft === null);
  const submission = await page.evaluate(() => window.fixture.calls.find((call) => call.method === "TubeDesigner.AddNestingStandardPart"));
  assert.equal(submission.payload.length, 875);
  assert.equal(submission.payload.profile.name, "本地定式 DXF");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerActiveNestingPartId), "added-standard-part");
  assert.deepEqual(errors, []);
  console.log("Nesting standard part browser: 3 layouts, eligible choices, editable parameters, DXF and confirmed selection passed.");
} finally {
  await browser.close();
}
