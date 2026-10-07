import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 850 } });
  const root = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://nesting-dock.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root.replace(/[\\/]$/, "") + sep)
      || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://nesting-dock.test/");
  await page.evaluate(async () => {
    const area = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const dock = await import("/src/apps/tube-designer/webpage/nestingDock.mjs");
    const workflow = await import("/src/apps/tube-designer/webpage/nestingWorkflow.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.innerHTML = `<style>${tubeDesignerCss}
      *{box-sizing:border-box}body{margin:0}#mount{height:740px}
      .cam-workbench{position:relative;display:grid;width:100%;height:100%;background:#dfe4e7}
      .cam-context-pane{grid-area:left}.cam-info-pane{grid-area:right}
      .cam-viewer{grid-area:viewer;min-width:0;min-height:0;background:#17313a}
      .cam-splitter-left{grid-area:left-resize}.cam-splitter-right{grid-area:right-resize}
      .cam-splitter{background:#c7d1d4;min-width:5px}
      .tube-designer-cutting-group-list,.tube-designer-cutting-inspector-scroll{overflow:auto}
      </style>`;
    document.body.innerHTML = '<div id="mount"></div>';
    const { nativeSectionIdentity } = await import("/src/tests/iCAX-UI/fixtures/nestingSectionIdentity.mjs");
    const profile = { sectionIdentity: nativeSectionIdentity("rect-20-20-t1"), id: "rect", kind: "rect", width: 20, depth: 20,
      wallThickness: 1, displayName: "方管", specification: "20 × 20" };
    const parts = Array.from({ length: 25 }, (_, index) => ({
      entityId: `part-${index}`, name: `零件 ${index}`, length: 100,
      quantity: 1, profile,
    }));
    const view = { activeAreaId: "nesting", scene: { tubeDesigner: {
      manufacturingGroups: [{ productEntityId: "product-1", name: "测试产品", parts }],
    } }, layout: { leftWidth: 330, rightWidth: 420, bottomHeight: 240 } };
    const key = area.buildProfileGroups(parts)[0].key;
    view.tubeDesignerNestingSettings = { version: 2,
      stocks: [{ profileKey: key, rows: [{ id: "stock", length: 6000, quantity: 10 }] }],
      parameters: { partGap: 2 } };
    view.scene.tubeDesigner.nestingSettings = structuredClone(view.tubeDesignerNestingSettings);
    const placements = parts.map((part, index) => ({ partId: part.entityId,
      instanceId: `instance-${index}`, start: index * 102, end: index * 102 + 100,
      gapBefore: index ? 2 : 0, nestedWithPrevious: false, reversed: false,
      rotationRadians: 0, variantId: "default" }));
    view.tubeDesignerNestingResult = { revision: "dock-1", status: "feasible",
      metrics: { placedPartCount: 25 }, unplaced: [], inputSignature: workflow.getNestingInputSignature(view),
      plans: [{ id: "plan-1", profileKey: key, stockTypeId: "stock", name: "方管-1",
        profile: "方管 20 × 20", stockLength: 6000, usedLength: 2548,
        remainingLength: 3452, partLength: 2500, partCount: 25,
        utilization: 2500 / 6000, placements }] };
    view.tubeDesignerActiveNestingPlanId = "plan-1";
    view.tubeDesignerNestingSelectionKind = "plan";
    const context = { mount: document.querySelector("#mount"),
      project: { projectId: "browser-nesting-dock-test" } };
    localStorage.removeItem("tube-designer.nesting-dock.v1:browser-nesting-dock-test");
    view.extra = true;
    const render = () => {
      dock.captureNestingDockInteraction(context.mount, view);
      const counts = dock.nestingDockZoneCounts(view, context);
      const classes = Object.entries(counts).filter(([, count]) => !count)
        .map(([zone]) => `tube-designer-dock-${zone}-empty`).join(" ");
      const columns = `${counts.left ? "var(--cam-left-width) 5px" : "0px 0px"} minmax(0,1fr) ${counts.right ? "5px var(--cam-right-width)" : "0px 0px"}`;
      const rows = `0px minmax(0,1fr) ${counts.bottom ? "5px var(--cam-bottom-height)" : "0px 0px"}`;
      const right = area.renderNestingRightPane(context, view).replace("</header>",
        `</header>${view.extra ? '<input data-dock-conditional aria-label="条件字段" value="abcdef" />' : ""}`);
      context.mount.innerHTML = `<main class="cam-workbench tube-designer-workspace tube-designer-production-workspace ${classes}"
        style="--cam-left-width:330px;--cam-right-width:420px;--cam-bottom-height:240px;
          grid-template-columns:${columns};grid-template-rows:${rows};
          grid-template-areas:'notice notice notice notice notice' 'left left-resize viewer right-resize right'
          'bottom-resize bottom-resize bottom-resize bottom-resize bottom-resize' 'bottom bottom bottom bottom bottom'">
        <aside class="cam-context-pane">${area.renderNestingLeftPane(context, view)}</aside>
        <div class="cam-splitter cam-splitter-left"></div><section class="cam-viewer"></section>
        <div class="cam-splitter cam-splitter-right"></div><aside class="cam-info-pane">${right}</aside>
        ${area.renderNestingResultDock(context, view)}</main>`;
      dock.attachNestingDocking(context, view, context.mount, ops);
      area.restoreNestingPartListScroll(context, view);
      dock.restoreNestingDockInteraction(view);
    };
    const ops = { renderProject: render };
    window.fixture = { area, dock, view, context, render };
    render();
    window.fixture.viewer = document.querySelector(".cam-viewer");
    window.fixture.search = document.querySelector('input[aria-label="搜索待排零件"]');
    window.fixture.inputEvents = 0;
    window.fixture.search.addEventListener("input", () => window.fixture.inputEvents++);
  });
  const panelZone = id => page.locator(`[data-tube-designer-dock-panel="${id}"]`)
    .evaluate(panel => panel.closest("[data-tube-designer-dock-zone]")?.dataset.tubeDesignerDockZone);
  assert.equal(await panelZone("parts"), "left");
  assert.equal(await panelZone("inspector"), "right");
  assert.equal(await panelZone("results"), "bottom");
  const drag = async (id, zone) => {
    const tab = page.locator(`[data-tube-designer-dock-tab="${id}"]`);
    const from = await tab.boundingBox();
    await page.mouse.move(from.x + from.width / 2, from.y + from.height / 2);
    await page.mouse.down();
    await page.mouse.move(from.x + from.width / 2 + 12, from.y + from.height / 2 + 10, { steps: 4 });
    const target = page.locator(`[data-tube-designer-dock-target="${zone}"]`);
    await target.waitFor({ state: "visible" });
    const to = await target.boundingBox();
    await page.mouse.move(to.x + to.width / 2, to.y + to.height / 2, { steps: 9 });
    await page.mouse.up();
  };
  await page.evaluate(() => {
    const left = document.querySelector(".tube-designer-cutting-group-list");
    left.scrollTop = 130;
  });
  await drag("parts", "bottom");
  assert.equal(await panelZone("parts"), "bottom");
  assert.equal(await page.locator(".tube-designer-cutting-group-list").evaluate(node => node.scrollTop), 130);
  assert.equal(await page.evaluate(() => {
    const fixture = window.fixture;
    fixture.search.dispatchEvent(new Event("input", { bubbles: true }));
    return fixture.viewer === document.querySelector(".cam-viewer")
      && fixture.search === document.querySelector('input[aria-label="搜索待排零件"]')
      && fixture.inputEvents === 1;
  }), true, "docking keeps the viewport, input node and listener");
  await page.evaluate(() => {
    const search = document.querySelector('input[aria-label="搜索待排零件"]');
    search.focus({ preventScroll: true });
    search.value = "零件 1";
    search.setSelectionRange(1, 3, "backward");
    window.fixture.render();
  });
  const retained = await page.evaluate(() => {
    const search = document.querySelector('input[aria-label="搜索待排零件"]');
    return { focused: document.activeElement === search, value: search.value,
      start: search.selectionStart, end: search.selectionEnd,
      direction: search.selectionDirection,
      scroll: document.querySelector(".tube-designer-cutting-group-list").scrollTop };
  });
  assert.deepEqual(retained, { focused: true, value: "零件 1", start: 1, end: 3,
    direction: "backward", scroll: 130 }, "moving a panel preserves live input and nested scroll");
  await drag("results", "right");
  assert.equal(await panelZone("results"), "right");
  assert.equal(await page.locator('.cam-info-pane [data-tube-designer-dock-tab]').count(), 2);
  assert.equal(await page.locator('.cam-info-pane [data-tube-designer-dock-panel="results"]').isVisible(), true);
  assert.equal(await page.locator('.cam-info-pane [data-tube-designer-dock-panel="inspector"]').isVisible(), false);
  assert.match(await page.locator('.cam-info-pane .tube-designer-dock-compact-plan').first().innerText(), /母材 6,000 mm/);
  await page.locator('[data-tube-designer-dock-tab="inspector"]').click();
  assert.equal(await page.locator('.cam-info-pane [data-tube-designer-dock-panel="inspector"]').isVisible(), true);
  assert.equal(await page.locator('.cam-info-pane [data-tube-designer-dock-panel="results"]').isVisible(), false);
  const persisted = await page.evaluate(() => {
    const saved = JSON.parse(localStorage.getItem("tube-designer.nesting-dock.v1:browser-nesting-dock-test"));
    const fresh = { activeAreaId: "nesting" };
    const loaded = window.fixture.dock.getNestingDockLayout(fresh, window.fixture.context);
    return { saved, loaded };
  });
  assert.equal(persisted.saved.positions.results, "right");
  assert.equal(persisted.loaded.positions.parts, "bottom");
  await drag("parts", "left");
  assert.equal(await panelZone("parts"), "left");
  assert.equal(await page.locator(".tube-designer-bottom-splitter").isVisible(), false);
  await page.evaluate(() => {
    const left = document.querySelector(".tube-designer-cutting-group-list");
    const right = document.querySelector(".tube-designer-cutting-inspector-scroll");
    left.scrollTop = 105;
    right.scrollTop = 85;
    const search = document.querySelector('input[aria-label="搜索待排零件"]');
    search.focus({ preventScroll: true });
    search.value = "正在输入";
    search.setSelectionRange(2, 4, "forward");
    window.fixture.render();
  });
  const movedRefresh = await page.evaluate(() => {
    const search = document.querySelector('input[aria-label="搜索待排零件"]');
    return { left: document.querySelector(".tube-designer-cutting-group-list").scrollTop,
      right: document.querySelector(".tube-designer-cutting-inspector-scroll").scrollTop,
      focused: document.activeElement === search, value: search.value,
      start: search.selectionStart, end: search.selectionEnd };
  });
  assert.deepEqual(movedRefresh, { left: 105, right: 85, focused: true,
    value: "正在输入", start: 2, end: 4 }, "refresh preserves both docked panes");
  await page.evaluate(() => {
    const conditional = document.querySelector("[data-dock-conditional]");
    conditional.focus({ preventScroll: true });
    conditional.setSelectionRange(1, 3);
    window.fixture.view.extra = false;
    window.fixture.render();
  });
  assert.equal(await page.locator("[data-dock-conditional]").count(), 0);
  assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("aria-label")), null,
    "removed conditional field does not transfer focus to another input");
  await page.locator(".tube-designer-dock-reset").click();
  assert.equal(await panelZone("parts"), "left");
  assert.equal(await panelZone("inspector"), "right");
  assert.equal(await panelZone("results"), "bottom");
  assert.equal(await page.locator(".tube-designer-dock-overlay").count(), 0);
  console.log("Nesting VS-style dock browser regression passed: drag, tabs, compact side layout, state and reset.");
} finally {
  await browser.close();
}
