import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 720 } });
  const root = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://nesting-fetch.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root.replace(/[\\/]$/, "") + sep)
      || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://nesting-fetch.test/");
  const result = await page.evaluate(async () => {
    const area = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const workflow = await import("/src/apps/tube-designer/webpage/nestingWorkflow.mjs");
    const { capturePaneInteraction } = await import(
      "/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const { tubeDesignerCss } = await import(
      "/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.innerHTML = `<style>${tubeDesignerCss}
      *{box-sizing:border-box}body{margin:0;font:13px Arial}
      #mount{display:flex;gap:16px;padding:12px;height:650px}
      .cam-context-pane,.cam-info-pane{width:420px;height:430px;overflow:auto;border:1px solid #ccc}
      .tube-designer-cutting-group-list,.tube-designer-cutting-inspector-scroll
        {height:190px!important;max-height:190px!important;overflow:auto!important}
      .spacer{height:500px}</style>`;
    document.body.innerHTML = `<main id="mount"><aside class="cam-context-pane"></aside>
      <aside class="cam-info-pane"></aside></main>`;
    const { nativeSectionIdentity } = await import("/src/tests/iCAX-UI/fixtures/nestingSectionIdentity.mjs");
    const profile = { sectionIdentity: nativeSectionIdentity("rect-20-20-t1"), id: "rect", kind: "rect", width: 20, depth: 20,
      wallThickness: 1, displayName: "方管", specification: "20 × 20" };
    const parts = Array.from({ length: 30 }, (_, index) => ({
      entityId: `part-${index}`, name: `零件 ${index}`, length: 100,
      quantity: 1, profile,
    }));
    const view = { activeAreaId: "nesting", scene: { tubeDesigner: {
      manufacturingGroups: [{ productEntityId: "product-1", name: "测试产品", parts }],
    } } };
    const key = area.buildProfileGroups(parts)[0].key;
    view.tubeDesignerNestingSettings = { version: 2,
      stocks: [{ profileKey: key, rows: [{ id: "stock-1", length: 6000, quantity: 10 }] }],
      parameters: { partGap: 2 } };
    view.scene.tubeDesigner.nestingSettings = structuredClone(view.tubeDesignerNestingSettings);
    const placements = parts.map((part, index) => ({
      partId: part.entityId, instanceId: `instance-${index}`, start: index * 102,
      end: index * 102 + 100, gapBefore: index ? 2 : 0,
      nestedWithPrevious: false, reversed: false, rotationRadians: 0, variantId: "default",
    }));
    const plan = { id: "plan-1", profileKey: key, stockTypeId: "stock-1",
      stockLength: 6000, usedLength: 3058, remainingLength: 2942,
      partLength: 3000, utilization: 3058 / 6000, placements };
    let pendingNest = null;
    let nestCalls = 0;
    const calls = [];
    const context = { mount: document.querySelector("#mount"), sceneProxy: {
      async invoke(method, payload) {
        calls.push({ method, payload });
        if (method === "TubeDesigner.Nest") {
          nestCalls++;
          if (nestCalls > 1) await new Promise(resolve => { pendingNest = resolve; });
          return { ready: true, revision: `revision-${nestCalls}`, status: "feasible",
            planCount: 1, unplaced: [], metrics: { placedPartCount: 30 } };
        }
        if (method !== "TubeDesigner.ReadNestingResult") throw new Error(method);
        if (payload.planId) return plan;
        return { revision: payload.revision, profileKeys: [key], stockTypeIds: ["stock-1"],
          rows: [[plan.id, 0, 0, 6000, 3058, 2942, 3000, 30, plan.utilization]] };
      },
    } };
    let conditional = true;
    const render = () => {
      const restore = capturePaneInteraction(context.mount);
      context.mount.querySelector(".cam-context-pane").innerHTML =
        area.renderNestingLeftPane(context, view) + '<div class="spacer"></div>';
      context.mount.querySelector(".cam-info-pane").innerHTML =
        area.renderNestingRightPane(context, view)
        + (conditional ? '<input data-test-conditional aria-label="条件字段" value="abcdef" />' : "")
        + '<div class="spacer"></div>';
      area.restoreNestingPartListScroll(context, view);
      restore();
    };
    const ops = { renderProject: render, appendProjectLog() {} };
    render();
    await workflow.handleNestingRibbonCommand(context, view, "nesting.start", ops);
    if (view.error) throw new Error(view.error);
    const left = document.querySelector(".cam-context-pane");
    const right = document.querySelector(".cam-info-pane");
    const leftList = () => left.querySelector(".tube-designer-cutting-group-list");
    const rightList = () => right.querySelector(".tube-designer-cutting-inspector-scroll");
    const scrollable = [left, right, leftList(), rightList()].every(node =>
      node && node.scrollHeight > node.clientHeight);
    const next = workflow.handleNestingRibbonCommand(context, view, "nesting.start", ops);
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    if (!pendingNest) throw new Error("second Nest did not start");
    left.scrollTop = 30;
    right.scrollTop = 40;
    leftList().scrollTop = 110;
    rightList().scrollTop = 90;
    const search = left.querySelector('input[aria-label="搜索待排零件"]');
    search.focus({ preventScroll: true });
    search.value = "零件 1";
    search.setSelectionRange(1, 3, "backward");
    const before = [left.scrollTop, right.scrollTop, leftList().scrollTop, rightList().scrollTop];
    pendingNest();
    await next;
    await new Promise(resolve => requestAnimationFrame(resolve));
    const after = [left.scrollTop, right.scrollTop, leftList().scrollTop, rightList().scrollTop];
    const focusKept = document.activeElement?.getAttribute("aria-label") === "搜索待排零件"
      && document.activeElement.value === "零件 1"
      && document.activeElement.selectionStart === 1
      && document.activeElement.selectionEnd === 3
      && document.activeElement.selectionDirection === "backward";
    const third = workflow.handleNestingRibbonCommand(context, view, "nesting.start", ops);
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const field = right.querySelector("[data-test-conditional]");
    field.focus({ preventScroll: true });
    field.setSelectionRange(2, 5, "forward");
    const rightScrollBefore = [right.scrollTop, rightList().scrollTop];
    pendingNest();
    await third;
    const rightFocusKept = document.activeElement?.hasAttribute("data-test-conditional")
      && document.activeElement.selectionStart === 2
      && document.activeElement.selectionEnd === 5
      && document.activeElement.selectionDirection === "forward"
      && right.scrollTop === rightScrollBefore[0]
      && rightList().scrollTop === rightScrollBefore[1];
    const fourth = workflow.handleNestingRibbonCommand(context, view, "nesting.start", ops);
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    right.querySelector("[data-test-conditional]").focus({ preventScroll: true });
    conditional = false;
    pendingNest();
    await fourth;
    const removedFieldDidNotStealFocus = !right.querySelector("[data-test-conditional]")
      && document.activeElement?.getAttribute("aria-label") !== "搜索待排零件";
    return { scrollable, before, after, focusKept, rightFocusKept, removedFieldDidNotStealFocus,
      methods: calls.map(call => call.method), error: view.error };
  });
  assert.equal(result.scrollable, true);
  assert.deepEqual(result.after, result.before,
    "both outer panes and their nested lists keep the user's latest scroll positions");
  assert.equal(result.focusKept, true);
  assert.equal(result.rightFocusKept, true);
  assert.equal(result.removedFieldDidNotStealFocus, true);
  assert.equal(result.error, "");
  assert.deepEqual(result.methods, Array.from({ length: 4 }, () => [
    "TubeDesigner.Nest", "TubeDesigner.ReadNestingResult", "TubeDesigner.ReadNestingResult",
  ]).flat());
  console.log("Nesting result fetch browser regression passed: async refresh preserves both pane scrolls and focus.");
} finally {
  await browser.close();
}
