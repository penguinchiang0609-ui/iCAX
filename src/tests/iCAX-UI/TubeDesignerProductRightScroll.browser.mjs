import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { renderDesignerRightPane } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const raw = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/template.json",
  import.meta.url,
), "utf8"));
const groups = raw.groups.map((group) => ({ ...group, displayName: catalogText(group.displayName) }));
const template = {
  ...raw,
  available: true,
  name: catalogText(raw.displayName),
  groups,
  parameters: raw.parameters.map((field) => ({
    ...field,
    displayName: catalogText(field.displayName),
    type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
    groupKey: field.group,
    group: groups.find((group) => group.key === field.group)?.displayName ?? field.group,
    options: field.choices?.map((choice) => ({
      ...choice,
      label: catalogText(choice.displayName),
      displayName: catalogText(choice.displayName),
    })),
  })),
};
const values = Object.fromEntries(raw.parameters.map((field) => [field.key, field.defaultValue]));
Object.assign(values, { frameLayout: "four_sides", frameJoinType: "v_groove_90:sharp_v" });
const product = { entityId: "window-1", templateId: raw.id, name: "已拆单防盗窗", quantity: 1, parameters: values };
const view = {
  scene: { tubeDesigner: {
    templates: [template], product, activeProductId: product.entityId,
    members: Array.from({ length: 41 }, () => ({})), joints: [],
    parts: Array.from({ length: 41 }, (_, index) => ({ entityId: `part-${index}` })),
  } },
  tubeDesignerParameterPanelProductId: product.entityId,
  tubeDesignerExpandedParameterGroups: groups.map((group) => group.key),
};
const rightHtml = renderDesignerRightPane({}, view);
assert.match(rightHtml, /data-tube-designer-parameter-scroll/);

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1000, height: 640 } });
  const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://tube-designer.test/**", async (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/")
      || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
      || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://tube-designer.test/");
  await page.setContent(`<style>body{margin:0}.tube-designer-workspace{height:640px}.cam-info-pane{width:360px;height:640px;box-sizing:border-box}${tubeDesignerCss}</style><main class="tube-designer-workspace"><aside class="cam-info-pane">${rightHtml}</aside></main>`);
  const result = await page.evaluate(async ({ renderView }) => {
    const { captureDesignerScrollState, restoreDesignerScrollState } = await import(
      "/src/apps/tube-designer/webpage/designerActions.mjs"
    );
    const { renderDesignerRightPane: renderRightPane } = await import(
      "/src/apps/tube-designer/webpage/designerViews.mjs"
    );
    const pane = document.querySelector(".cam-info-pane");
    const context = { mount: pane };
    const state = renderView;
    pane.querySelectorAll(".tube-designer-parameter-section,.tube-designer-parameter-subsection")
      .forEach((details) => { details.open = true; });
    let scroller = pane.querySelector("[data-tube-designer-parameter-scroll]");
    const overflow = getComputedStyle(scroller).overflowY;
    const scrollable = scroller.scrollHeight > scroller.clientHeight;
    scroller.scrollTop = scroller.scrollHeight;
    const last = scroller.querySelector(".tube-designer-parameter-section:last-child");
    const reachable = last.getBoundingClientRect().bottom <= scroller.getBoundingClientRect().bottom + 1;
    const sectionTitles = [...scroller.querySelectorAll(".tube-designer-parameter-section")]
      .map((node) => node.querySelector(":scope > summary > span")?.textContent?.trim())
      .filter(Boolean);
    const hasEmbeddedProductDiagram = Boolean(pane.querySelector("[data-tube-designer-product-diagram]"));

    scroller.scrollTop = Math.min(420, scroller.scrollHeight - scroller.clientHeight);
    const input = [...scroller.querySelectorAll('[data-tube-designer-parameter]')]
      .find((field) => field.type === "text");
    input.focus({ preventScroll: true });
    input.setSelectionRange(1, Math.min(4, input.value.length), "backward");
    const oldTop = scroller.scrollTop;
    captureDesignerScrollState(context, state);

    pane.innerHTML = renderRightPane({}, state);
    scroller = pane.querySelector("[data-tube-designer-parameter-scroll]");
    restoreDesignerScrollState(context, state);
    await Promise.resolve();
    await new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)));
    const restored = document.activeElement;
    return {
      overflow,
      scrollable,
      reachable,
      sectionTitles,
      hasEmbeddedProductDiagram,
      oneScrollbar: getComputedStyle(scroller.querySelector(".tube-designer-parameter-sections")).overflowY === "visible",
      stable: Math.abs(scroller.scrollTop - oldTop) < 1,
      focus: restored?.getAttribute("data-tube-designer-parameter") === input.getAttribute("data-tube-designer-parameter"),
      selection: [restored?.selectionStart, restored?.selectionEnd, restored?.selectionDirection],
      expectedSelection: [1, Math.min(4, input.value.length), "backward"],
    };
  }, { renderView: view });
  assert.equal(result.overflow, "auto");
  assert.equal(result.scrollable, true);
  assert.equal(result.reachable, true);
  assert.deepEqual(result.sectionTitles, ["材料", "工艺"]);
  assert.equal(result.hasEmbeddedProductDiagram, false,
    "产品示意图已从实例编辑页移除，不应占用右侧参数滚动区");
  assert.equal(result.oneScrollbar, true);
  assert.equal(result.stable, true);
  assert.equal(result.focus, true);
  assert.deepEqual(result.selection, result.expectedSelection);
  console.log("Disassembled product right pane remains scrollable and preserves scroll, focus and selection after refresh.");
} finally {
  await browser.close();
}
