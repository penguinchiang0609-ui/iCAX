// Verifies the shared parameter diagram and the real standard-part dialog in Edge.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const root = fileURLToPath(new URL("../../", import.meta.url));
const runtimePath = resolve(root, "apps/tube-designer/templates/_shared/profile_package_runtime.py");
const catalogue = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
import importlib.util, json, sys
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('diagram_browser_runtime', sys.argv[1])
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
packages = runtime.generate({'action': 'list-system'}, {})['systemProfiles']
profiles = []
for package in packages:
    profile = runtime.generate({'action': 'evaluate', 'descriptor': package['descriptor'], 'scriptSource': package['scriptSource'], 'values': package['defaultParameters'], 'packageDigest': package['packageDigest']}, {})['profile']
    profiles.append({'id': package['descriptor']['id'], 'profile': profile})
polygon = next(package for package in packages if package['descriptor']['id'] == 'polygon')
for mode in ('regular', 'star'):
    values = dict(polygon['defaultParameters'], shapeMode=mode, sideCount=7, width=97, depth=63, wallThickness=0.5, starInnerRatio=0.45)
    profile = runtime.generate({'action': 'evaluate', 'descriptor': polygon['descriptor'], 'scriptSource': polygon['scriptSource'], 'values': values, 'packageDigest': polygon['packageDigest']}, {})['profile']
    profiles.append({'id': 'polygon-' + mode + '-7', 'profile': profile})
print(json.dumps(profiles, ensure_ascii=True))
`, runtimePath], { encoding: "utf8", maxBuffer: 4 * 1024 * 1024 }));
assert.equal(catalogue.length, 12, "All ten built-ins plus odd regular/star polygons must reach browser QA");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage();
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://parameter-diagram.test/**", (route) => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body><div id='app' class='tube-designer-workspace'></div></body>" });
    const file = resolve(root, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://parameter-diagram.test/");
  const commonCss = readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css", import.meta.url), "utf8");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}.diagram-test-scopes{display:grid;gap:20px;padding:16px;max-width:1000px;margin:auto}.diagram-test-scope{min-width:0;border:1px solid #adc4c5;padding:10px}.diagram-test-inputs{display:flex;flex-wrap:wrap;gap:10px}.diagram-test-inputs label{display:grid;gap:4px}.diagram-test-inputs input{width:100px}" + commonCss + tubeDesignerCss });
  await page.evaluate(async () => {
    const diagram = await import("/src/apps/tube-designer/webpage/profileParameterDiagram.mjs");
    const standard = await import("/src/apps/tube-designer/webpage/nestingStandardPart.mjs");
    const snapshot = (width = 80) => ({
      schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "parametric-package",
      name: "程式槽钢", width, depth: 60,
      contours: [{ kind: "polygon", points: [[-width/2,-30],[width/2,-30],[width/2,-24],[-width/2+6,-24],[-width/2+6,24],[width/2,24],[width/2,30],[-width/2,30]] }],
      parameters: { width, height: 60, webThickness: 6, flangeThickness: 6 },
      parameterDefinitions: [
        { key: "width", displayName: "外宽", valueType: "number", unit: "mm", defaultValue: 80, min: 20 },
        { key: "height", displayName: "外高", valueType: "number", unit: "mm", defaultValue: 60, min: 20 },
        { key: "webThickness", displayName: "腹板厚", valueType: "number", unit: "mm", defaultValue: 6, min: 1 },
        { key: "flangeThickness", displayName: "翼缘厚", valueType: "number", unit: "mm", defaultValue: 6, min: 1 },
      ],
      parameterDiagram: { schemaVersion: 1, annotations: [
        { parameter: "width", kind: "linear", axis: "x", side: "top", from: [-width/2,30], to: [width/2,30] },
        { parameter: "height", kind: "linear", axis: "y", side: "left", from: [-width/2,-30], to: [-width/2,30] },
        { parameter: "webThickness", kind: "leader", side: "left", point: [-width/2+3,0], description: "左侧腹板厚度" },
        { parameter: "flangeThickness", kind: "leader", side: "right", point: [width/4,27], description: "上下翼缘厚度" },
      ] },
    });
    const mount = document.querySelector("#app");
    const renderScopes = () => {
      mount.innerHTML = `<div class="diagram-test-scopes">${["a", "b"].map((id) => `<section id="scope-${id}" class="diagram-test-scope" data-profile-parameter-scope>${diagram.renderProfileParameterDiagram(snapshot())}<div class="diagram-test-inputs">${snapshot().parameterDefinitions.map((d) => `<label>${d.displayName}<input data-profile-parameter-key="${d.key}" value="${snapshot().parameters[d.key]}" /></label>`).join("")}</div></section>`).join("")}</div>`;
      diagram.bindProfileParameterDiagrams(mount);
      // Binding again must not duplicate state transitions/listeners.
      diagram.bindProfileParameterDiagrams(mount);
    };
    renderScopes();
    const view = {
      activeAreaId: "nesting", pending: false, scene: { tubeDesigner: { nestingGroups: [] } },
      tubeDesignerSystemProfiles: [{ id: "channel", name: "程式槽钢", profileType: "parametric-package", previewProfile: snapshot(), defaultParameters: snapshot().parameters, descriptor: { parameters: snapshot().parameterDefinitions } }],
      tubeDesignerUserData: { profiles: [] }, tubeDesignerTemplateProfiles: [],
    };
    const calls = [];
    const context = {
      productProxy: { async invoke(method, payload) { calls.push({ method, payload }); return { profile: snapshot(payload.parameters.width) }; } },
    };
    const ops = { renderProject() { mount.innerHTML = standard.renderNestingStandardPartDialog(view); diagram.bindProfileParameterDiagrams(mount); } };
    document.addEventListener("change", async (event) => {
      await standard.handleNestingStandardPartAction(context, view, event.target.dataset.camChangeAction ?? "", event.target, ops);
    });
    window.fixture = { diagram, standard, view, calls, context, ops, renderScopes };
  });

  const annotation = (scope, key) => page.locator(`#scope-${scope} svg [data-profile-annotation-key="${key}"]`).first();
  await page.locator('#scope-a input[data-profile-parameter-key="width"]').focus();
  assert.equal(await annotation("a", "width").evaluate((element) => element.classList.contains("is-active")), true);
  assert.equal(await annotation("b", "width").evaluate((element) => element.classList.contains("is-active")), false);
  await page.locator('#scope-a input[data-profile-parameter-key="height"]').hover();
  assert.equal(await annotation("a", "height").evaluate((element) => element.classList.contains("is-active")), true);
  assert.equal(await annotation("b", "height").evaluate((element) => element.classList.contains("is-active")), false);
  await annotation("b", "flangeThickness").locator(".td-profile-parameter-badge").click();
  assert.equal(await page.evaluate(() => document.activeElement?.dataset.profileParameterKey), "flangeThickness");
  assert.equal(await page.evaluate(() => document.activeElement?.closest("[data-profile-parameter-scope]")?.id), "scope-b");

  // Diagram annotations are reachable without a mouse.
  await annotation("a", "webThickness").focus();
  await page.keyboard.press("Enter");
  assert.equal(await page.evaluate(() => document.activeElement?.dataset.profileParameterKey), "webThickness");
  assert.equal(await page.evaluate(() => document.activeElement?.closest("[data-profile-parameter-scope]")?.id), "scope-a");

  for (const size of [{ width: 1440, height: 1000 }, { width: 1024, height: 768 }, { width: 620, height: 700 }]) {
    await page.setViewportSize(size);
    const layout = await page.locator("#scope-a svg").evaluate((svg) => {
      const bounds = svg.getBoundingClientRect();
      const texts = [...svg.querySelectorAll("text")].map((el) => {
        const r = el.getBoundingClientRect(); return { text: el.textContent, x: r.x, y: r.y, right: r.right, bottom: r.bottom, width: r.width, height: r.height };
      }).filter((r) => r.width && r.height);
      const clipped = texts.filter((r) => r.x < bounds.x - 1 || r.y < bounds.y - 1 || r.right > bounds.right + 1 || r.bottom > bounds.bottom + 1);
      const overlaps = texts.flatMap((a, i) => texts.slice(i + 1).filter((b) => Math.min(a.right,b.right) - Math.max(a.x,b.x) > 1 && Math.min(a.bottom,b.bottom) - Math.max(a.y,b.y) > 1).map((b) => [a.text,b.text]));
      return { clipped, overlaps, overflow: document.documentElement.scrollWidth - innerWidth, texts: texts.length };
    });
    assert.ok(layout.texts >= 4, JSON.stringify(layout));
    assert.deepEqual(layout.clipped, [], JSON.stringify({ size, layout }));
    assert.deepEqual(layout.overlaps, [], JSON.stringify({ size, layout }));
    assert.ok(layout.overflow <= 2, JSON.stringify({ size, layout }));
  }

  await page.evaluate(async () => {
    const f = window.fixture;
    await f.standard.handleNestingStandardPartRibbonCommand(f.context, f.view, "nesting.add-standard-part", f.ops);
  });
  for (const size of [{ width: 1440, height: 1000 }, { width: 1024, height: 768 }, { width: 620, height: 700 }]) {
    await page.setViewportSize(size);
    const dialog = page.locator('[role="dialog"]');
    assert.equal(await dialog.locator('svg [data-profile-annotation-key="width"]').count() > 0, true);
    assert.equal(await dialog.locator('[data-standard-part-parameter="width"]').getAttribute("data-profile-parameter-key"), "width");
    const bounds = await dialog.evaluate((element) => {
      const r = element.getBoundingClientRect(); return { x: r.x, y: r.y, right: r.right, bottom: r.bottom, overflow: element.scrollWidth - element.clientWidth };
    });
    assert.ok(bounds.x >= 0 && bounds.y >= 0 && bounds.right <= size.width && bounds.bottom <= size.height && bounds.overflow <= 2, JSON.stringify({ size, bounds }));
    if (process.env.ICAX_ARTIFACT_DIR) {
      mkdirSync(process.env.ICAX_ARTIFACT_DIR, { recursive: true });
      await page.screenshot({ path: resolve(process.env.ICAX_ARTIFACT_DIR, `profile-parameter-diagram-${size.width}.png`) });
    }
  }
  await page.setViewportSize({ width: 1024, height: 768 });
  await page.locator('[data-standard-part-parameter="width"]').fill("112");
  await page.locator('[data-standard-part-parameter="width"]').press("Tab");
  await page.waitForFunction(() => window.fixture.view.tubeDesignerNestingStandardPartDraft.parameters.width === 112 && !window.fixture.view.pending);
  assert.match(await page.locator('[role="dialog"] svg').textContent(), /112/);
  await page.locator('[role="dialog"] svg [data-profile-annotation-key="width"] .td-profile-dimension-label').first().click();
  assert.equal(await page.evaluate(() => document.activeElement?.dataset.standardPartParameter), "width");

  // Feed evaluated package snapshots directly into the renderer; no hand-built metadata.
  await page.setViewportSize({ width: 1100, height: 900 });
  await page.evaluate((profiles) => {
    const f = window.fixture;
    const mount = document.querySelector("#app");
    mount.innerHTML = `<main style="display:grid;grid-template-columns:repeat(3,300px);gap:18px;max-width:100%;width:max-content;margin:auto;padding:18px">${profiles.map(({ id, profile }) => `<article data-catalogue-profile="${id}" data-profile-parameter-scope style="min-width:0;width:300px"><h3 style="margin:0 0 8px;color:#28464f;font-size:14px">${profile.name} · ${id}</h3>${f.diagram.renderProfileParameterDiagram(profile, { compact: true })}</article>`).join("")}</main>`;
    f.diagram.bindProfileParameterDiagrams(mount);
  }, catalogue);
  for (const { id, profile } of catalogue) {
    const svg = page.locator(`[data-catalogue-profile="${id}"] svg`);
    const geometry = await svg.evaluate((element) => {
      const svgBounds = element.getBoundingClientRect();
      const labels = [...element.querySelectorAll("text")].map((label) => {
        const r = label.getBoundingClientRect();
        return { text: label.textContent, x: r.x, y: r.y, right: r.right, bottom: r.bottom, width: r.width, height: r.height };
      }).filter((r) => r.width && r.height);
      return {
        width: svgBounds.width,
        labelCount: labels.length,
        clipped: labels.filter((r) => r.x < svgBounds.x - 1 || r.y < svgBounds.y - 1 || r.right > svgBounds.right + 1 || r.bottom > svgBounds.bottom + 1),
        overlaps: labels.flatMap((a, index) => labels.slice(index + 1).filter((b) => Math.min(a.right,b.right) - Math.max(a.x,b.x) > 1 && Math.min(a.bottom,b.bottom) - Math.max(a.y,b.y) > 1).map((b) => [a.text,b.text])),
        annotationKeys: [...element.querySelectorAll("[data-profile-annotation-key]")].map((item) => item.dataset.profileAnnotationKey),
      };
    });
    assert.ok(geometry.width >= 280 && geometry.width <= 300, JSON.stringify({ id, geometry }));
    assert.ok(geometry.labelCount >= 2, JSON.stringify({ id, geometry }));
    assert.ok(geometry.annotationKeys.every((key) => profile.parameterDiagram.annotations.some((item) => item.parameter === key)), id);
    assert.deepEqual(geometry.clipped, [], JSON.stringify({ id, geometry }));
    assert.deepEqual(geometry.overlaps, [], JSON.stringify({ id, geometry }));
  }
  if (process.env.ICAX_ARTIFACT_DIR) {
    await page.screenshot({ path: resolve(process.env.ICAX_ARTIFACT_DIR, "profile-parameter-diagram-catalogue.png"), fullPage: true });
  }
  assert.deepEqual(errors, []);
  console.log("Profile parameter diagram browser: scoped focus/hover/click/keyboard, 3 layouts, live standard-part update and 12 evaluated built-in snapshots passed.");
} finally {
  await browser.close();
}
