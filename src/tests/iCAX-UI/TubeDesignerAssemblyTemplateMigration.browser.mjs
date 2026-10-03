import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

const assemblyRoot = fileURLToPath(new URL("../../apps/tube-designer/templates/assembly/", import.meta.url));
const templates = readdirSync(assemblyRoot, { withFileTypes: true })
  .filter((entry) => entry.isDirectory())
  .flatMap((entry) => {
    try { return [JSON.parse(readFileSync(resolve(assemblyRoot, entry.name, "assembly.json"), "utf8"))]; }
    catch { return []; }
  });
const newTemplates = structuredClone(templates);
const vNotch = newTemplates.find((template) => template.partProcesses?.some((process) => process.resource?.id === "v-notch-sharp"));
assert.ok(vNotch, "V 槽工艺必须由独立装配模板声明");

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 780 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://assembly-migration.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://assembly-migration.test/");
  await page.addStyleTag({ content: `body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 340px;height:700px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{position:relative}.tube-connection-library-list{height:120px;overflow:auto}.tube-connection-library-editor-body{height:120px;overflow:auto}.tube-connection-library-editor-body:after{content:"";display:block;height:600px}` });
  const result = await page.evaluate(({ newTemplates, targetId }) => {
    const run = async () => {
      const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
      const { finishedProductInput } = await import("/src/apps/tube-designer/webpage/finishedProductModel.mjs");
      const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
      const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
      const moldIds = ["segmented-bend", "v-notch-sharp", "embedded-arc-notch", "edge-arc-groove", "flexible-slit-bend"];
      const unchangedTargets = moldIds.map((moldId) => {
        const candidate = { tubeDesignerAssemblyTemplates: newTemplates, tubeDesignerAssemblyLibrary: {
          selectedId: "bend", workMode: "example", parameterDrafts: { bend: { bendMethod: "notched", slotProcess: moldId } },
        } };
        return [moldId, library.assemblyLibraryState(candidate).selectedId];
      });
      const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: structuredClone(newTemplates),
        tubeDesignerAssemblyLibrary: { selectedId: targetId, workMode: "example", workModeUserSelected: true,
          parameterDrafts: { [targetId]: { angle: 77 } },
          processDrafts: { [targetId]: { "node-slot": { "v-notch-sharp": { maleFemale: true } } } } } };
      finishedProductInput(view, "l").parameters.angle = 77;
      const render = () => {
        const left = library.renderAssemblyLibraryLeftPane({}, view);
        const right = library.renderAssemblyLibraryRightPane({}, view);
        const overlay = library.renderAssemblyLibraryViewportOverlay({}, view);
        let mount = document.querySelector("main");
        if (!mount) {
          document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
          mount = document.querySelector("main");
          patch.rememberLibraryDom(view, mount, "");
        } else {
          const restoreInteraction = capturePaneInteraction(mount);
          if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" }))
            throw new Error("装配目录刷新没有走局部更新");
          restoreInteraction();
        }
      };
      render();
      const mount = document.querySelector("main");
      const canvas = mount.querySelector("canvas");
      const sceneHost = mount.querySelector("[data-tube-assembly-scene-viewport]");
      const sceneRoot = document.createElement("div");
      sceneRoot.dataset.fakeScene = "";
      sceneHost.append(sceneRoot);
      const search = mount.querySelector(".tube-connection-library-search input");
      search.value = "弯";
      search.focus({ preventScroll: true });
      search.setSelectionRange(1, 1);
      const leftScroll = mount.querySelector(".tube-connection-library-list");
      const rightScroll = mount.querySelector(".tube-connection-library-editor-body");
      leftScroll.scrollTop = 40;
      rightScroll.scrollTop = 80;
      // The response arrives after the user has continued scrolling.
      view.tubeDesignerAssemblyTemplates = newTemplates;
      leftScroll.scrollTop = 70;
      rightScroll.scrollTop = 120;
      render();
      const state = library.assemblyLibraryState(view);
      const selected = library.assemblyParameterValues(view);
      const sharedDraft = state.processDrafts?.[targetId]?.["node-slot"]?.["v-notch-sharp"];
      const focusPreserved = document.activeElement === search && search.selectionStart === 1 && search.selectionEnd === 1;
      const scrollPreserved = leftScroll === mount.querySelector(".tube-connection-library-list")
        && rightScroll === mount.querySelector(".tube-connection-library-editor-body")
        && leftScroll.scrollTop === 70 && rightScroll.scrollTop === 120;
      const scenePreserved = canvas === mount.querySelector("canvas") && sceneRoot === mount.querySelector("[data-fake-scene]");
      const formedUrl = "memory://target-preview";
      state.preview = { plan: { templateId: targetId }, formedPreview: {
        rows: [{ data: { geometry: { url: formedUrl, version: 1 } } }],
        resources: new Map([[formedUrl, new ArrayBuffer(8)]]),
        mesh: { metadata: { previewKind: "target-shape" } },
      } };
      const overlay = library.renderAssemblyLibraryViewportOverlay({}, view);
      return { unchangedTargets, selectedId: state.selectedId, angle: selected.angle,
        oldDraft: state.parameterDrafts.bend, sharedDraft, focusPreserved, scrollPreserved,
        scenePreserved, formedLabel: overlay.includes("成品示意"),
        formedDescription: overlay.includes("仅显示成品外形"),
        oldMethodField: !!mount.querySelector('[data-tube-assembly-parameter="bendMethod"]'),
        oldSlotField: !!mount.querySelector('[data-tube-assembly-parameter="slotProcess"]') };
    };
    return run();
  }, { newTemplates, targetId: vNotch.id });
  assert.equal(result.selectedId, vNotch.id);
  assert.ok(result.unchangedTargets.every(([, id]) => id === "bend"), "旧槽口草稿不得迁移或偷偷切换当前工艺");
  assert.equal(result.angle, 77);
  assert.deepEqual(result.sharedDraft, { maleFemale: true });
  assert.equal(result.focusPreserved, true, `目录刷新后搜索焦点与选区须保持: ${JSON.stringify(result)}`);
  assert.equal(result.scrollPreserved, true, "目录刷新应采用最新的双侧栏滚动位置");
  assert.equal(result.scenePreserved, true, "画布和视口节点不能被替换");
  assert.equal(result.oldMethodField, false);
  assert.equal(result.oldSlotField, false);
  assert.equal(result.formedLabel, true);
  assert.equal(result.formedDescription, true);
  assert.deepEqual(errors, []);
  console.log("Current assembly catalog refresh preserves its draft, nodes and interaction without historical migration");
} finally {
  await browser.close();
}
