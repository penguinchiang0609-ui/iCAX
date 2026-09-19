import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 820 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://connection-library.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://connection-library.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:820px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{position:relative;min-width:0;background:#13252d}.cam-viewport canvas{width:100%;height:100%}` });
  const result = await page.evaluate(async () => {
    const library = await import("/src/apps/tube-designer/webpage/connectionLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const view = { activeAreaId: "connections" };
    const context = {};
    const render = () => {
      const left = library.renderConnectionLibraryLeftPane(context, view);
      const right = library.renderConnectionLibraryRightPane(context, view);
      const overlay = library.renderConnectionLibraryViewportOverlay(context, view);
      if (!document.querySelector("main")) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, document.querySelector("main"), { left, right, overlay, suffix: "" })) {
        throw new Error("连接库参数更新没有走局部更新");
      }
    };
    render();
    const mount = document.querySelector("main");
    const canvas = mount.querySelector("canvas");
    const stage = mount.querySelector(".tube-connection-library-stage");
    const viewport = mount.querySelector(".cam-viewport");
    const initialLayout = stage.getBoundingClientRect().width <= viewport.getBoundingClientRect().width - 60
      && stage.getBoundingClientRect().height <= viewport.getBoundingClientRect().height - 90;
    const select = mount.querySelector('[data-tube-connection-id="slot-bolt-adjustable"]');
    await library.handleConnectionLibraryAction(context, view, "tube-designer-connection-select", select, { renderProject: render });
    const input = mount.querySelector('[data-tube-connection-parameter="adjustment"]');
    const rightScroll = mount.querySelector(".tube-connection-library-editor-body");
    input.focus({ preventScroll: true });
    input.value = "25";
    rightScroll.scrollTop = 180;
    await library.handleConnectionLibraryAction(context, view, "tube-designer-connection-parameter-change", input, { renderProject: render });
    const updated = mount.querySelector('[data-tube-connection-parameter="adjustment"]');
    return {
      initialLayout,
      canvasStable: canvas === mount.querySelector("canvas"),
      inputStable: input === updated,
      focused: document.activeElement === input,
      scroll: rightScroll.scrollTop,
      selected: view.tubeDesignerConnectionLibrary.selectedId,
      value: view.tubeDesignerConnectionLibrary.parameterDrafts["slot-bolt-adjustable"].adjustment,
      hasSlot: mount.querySelector(".cam-info-pane").textContent.includes("模具：长孔"),
      hasCircle: mount.querySelector(".cam-info-pane").textContent.includes("模具：圆孔"),
      stageText: mount.querySelector(".tube-connection-library-stage").textContent,
    };
  });
  await page.screenshot({ path: "tmp/connection-library-layout.png", fullPage: true });
  assert.equal(result.initialLayout, true, JSON.stringify(result));
  assert.equal(result.canvasStable, true);
  assert.equal(result.inputStable, true);
  assert.equal(result.focused, true);
  assert.ok(result.scroll > 0);
  assert.equal(result.selected, "slot-bolt-adjustable");
  assert.equal(result.value, 25);
  assert.equal(result.hasSlot, true);
  assert.equal(result.hasCircle, true);
  assert.match(result.stageText, /连接关系示意/);
  assert.deepEqual(errors, []);
  console.log("Connection library layout, mould composition, live parameters, focus/selection/scroll preservation passed.");
} finally {
  await browser.close();
}
