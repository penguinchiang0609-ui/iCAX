import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const template = JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/assembly/mechanical-fastener/assembly.json", import.meta.url), "utf8"));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 820 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://assembly-hydration.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://assembly-hydration.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:820px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{position:relative;min-width:0}.test-left-scroll,.test-right-scroll{height:360px;overflow:auto}.test-nested-scroll{height:60px;overflow:auto}` });
  const result = await page.evaluate(async (template) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const scene = { tubeDesigner: {
      activeProductId: "product-1", generationRun: { entityId: "run-1" },
      product: { entityId: "product-1", name: "实机下料件样例" }, members: [{ entityId: "member-1" }],
      parts: [
        { entityId: "part-1", partNumber: "边框横管-01", thumbnailGeometryResourceId: "resource://cut-1", thumbnailGeometryResourceVersion: 2 },
        { entityId: "part-2", partNumber: "边框竖管-01", thumbnailGeometryResourceId: "resource://cut-2", thumbnailGeometryResourceVersion: 3 },
      ],
    } };
    const view = { activeAreaId: "assemblies", scene, tubeDesignerAssemblyTemplates: [template],
      tubeDesignerAssemblyLibrary: { workMode: "product", selectedId: template.id, exploded: true } };
    const requests = [];
    const snapshots = [];
    let restored = false;
    const context = { sceneProxy: {
      resources: {},
      invoke(method, payload, options) {
        requests.push({ method, payload, options });
        if (method !== "TubeDesigner.List") throw new Error(`unexpected ${method}`);
        return new Promise((resolve) => { requests.at(-1).resolve = () => { restored = true; resolve({ tubeDesigner: { activeProductId: "stale-result" } }); }; });
      },
      views: { start: async () => ({ snapshot: { rows: [{ entityId: "member-1", data: { geometry: { url: "resource://member", version: 1 } } }] },
        stop: async () => {}, poll: async () => null }) },
    } };
    const viewportFactory = () => {
      const root = document.createElement("div");
      root.className = "test-viewport";
      const viewport = { root,
        mount(host) { host.replaceChildren(root); return viewport; },
        async applyViewSnapshot(snapshot) {
          if (snapshot.rows.length && !restored) throw new Error("Render resource GET failed (404)");
          snapshots.push(snapshot);
          return { applied: true, entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [] };
        },
        setVisibleEntityIds() {}, setStandardView() {}, fitViewToViewport() {}, getCameraState() { return {}; },
        setCameraState() {}, setContinuousRendering() {}, dispose() {},
      };
      return viewport;
    };
    const filler = Array.from({ length: 50 }, (_, i) => `<p>可滚动项目 ${i + 1}</p>`).join("");
    const render = () => {
      const left = `<div class="test-left-scroll">${library.renderAssemblyLibraryLeftPane(context, view)}${filler}</div>`;
      const right = `<div class="test-right-scroll"><input data-test-editor value="加工位置"><div class="test-nested-scroll">${filler}</div>${library.renderAssemblyLibraryRightPane(context, view)}${filler}</div>`;
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      const mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) throw new Error("hydration did not patch DOM");
      library.attachAssemblyLibraryViewports(context, view, document.querySelector("main"), { viewportFactory });
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    render();
    for (let i = 0; i < 10 && !requests.length; i++) await new Promise((resolve) => setTimeout(resolve, 0));
    const mount = document.querySelector("main");
    const canvas = mount.querySelector("canvas");
    const viewport = mount.querySelector(".test-viewport");
    const editor = mount.querySelector("[data-test-editor]");
    const left = mount.querySelector(".test-left-scroll");
    const right = mount.querySelector(".test-right-scroll");
    const nested = mount.querySelector(".test-nested-scroll");
    editor.focus({ preventScroll: true }); editor.setSelectionRange(1, 3);
    left.scrollTop = 80; right.scrollTop = 90; nested.scrollTop = 20;
    const waitCard = mount.querySelector(".tube-assembly-preview-wait .tube-designer-export-progress-card");
    const waitTrack = waitCard?.querySelector(".tube-designer-export-progress-track");
    const before = { snapshots: snapshots.length, loading: waitCard?.textContent.includes("正在恢复"),
      sharedStyle: !!waitCard?.querySelector(".tube-designer-export-spinner")
        && waitTrack?.classList.contains("is-indeterminate") && getComputedStyle(waitTrack).height === "7px"
        && getComputedStyle(waitCard).backgroundColor === "rgb(255, 255, 255)" };
    // The user can keep editing and scrolling while the native restore runs.
    left.scrollTop = 145; right.scrollTop = 165; nested.scrollTop = 42;
    requests[0].resolve();
    for (let i = 0; i < 30 && !snapshots.some((snapshot) => snapshot.rows.length); i++) await new Promise((resolve) => setTimeout(resolve, 0));
    const after = { url: snapshots.find((snapshot) => snapshot.rows.length)?.rows[0]?.data?.geometry?.url,
      editorSame: editor === mount.querySelector("[data-test-editor]"), focused: document.activeElement === editor,
      selection: [editor.selectionStart, editor.selectionEnd], leftSame: left === mount.querySelector(".test-left-scroll"),
      rightSame: right === mount.querySelector(".test-right-scroll"), nestedSame: nested === mount.querySelector(".test-nested-scroll"),
      scrolls: [left.scrollTop, right.scrollTop, nested.scrollTop], canvasSame: canvas === mount.querySelector("canvas"),
      viewportSame: viewport === mount.querySelector(".test-viewport"), designerSame: scene.tubeDesigner === view.scene.tubeDesigner,
      hudError: mount.querySelector(".tube-connection-library-hud.error")?.textContent ?? "",
      progressAbsent: !mount.querySelector(".tube-assembly-preview-wait") };
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-product-part", { value: "part-2" }, { renderProject: render });
    for (let i = 0; i < 30 && snapshots.at(-1)?.rows[0]?.entityId !== "part-2"; i++) await new Promise((resolve) => setTimeout(resolve, 0));
    const part = { url: snapshots.at(-1)?.rows[0]?.data?.geometry?.url, calls: requests.length,
      canvasSame: canvas === mount.querySelector("canvas"), editorSame: editor === mount.querySelector("[data-test-editor]") };
    library.disposeAssemblyLibraryViewports(view);
    return { before, after, part, calls: requests.map(({ method, payload }) => ({ method, payload })) };
  }, template);
  assert.deepEqual(result.before, { snapshots: 0, loading: true, sharedStyle: true }, "use the shared progress card while restoring native resources");
  assert.equal(result.after.url, "resource://cut-1");
  assert.equal(result.after.editorSame && result.after.focused && result.after.leftSame && result.after.rightSame
    && result.after.nestedSame && result.after.canvasSame && result.after.viewportSame && result.after.designerSame, true);
  assert.deepEqual(result.after.selection, [1, 3]);
  assert.ok(result.after.scrolls.every((value) => value > 0), `hydration must preserve both sidebars and nested scroll: ${result.after.scrolls}`);
  assert.equal(result.after.hudError, "");
  assert.equal(result.after.progressAbsent, true);
  assert.deepEqual(result.part, { url: "resource://cut-2", calls: 1, canvasSame: true, editorSame: true });
  assert.deepEqual(result.calls, [{ method: "TubeDesigner.List", payload: { includeManufacturingGeometry: true } }]);
  assert.deepEqual(errors, []);
  console.log("Assembly reopened resource hydration, part switching, and focus/scroll preservation passed.");
} finally {
  await browser.close();
}
