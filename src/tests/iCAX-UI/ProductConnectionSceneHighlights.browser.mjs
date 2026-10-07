// Production connection rows, generated members and the real WebGL emphasis path.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const root = fileURLToPath(new URL("../../../", import.meta.url));
const source = resolve(root, "src"), output = resolve(root, "output/tests/product-connection-highlights");
mkdirSync(output, { recursive: true });
const descriptor = JSON.parse(readFileSync(resolve(source, "apps/tube-designer/templates/product/single_face_security_window/template.json"), "utf8"));
const cases = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
import importlib.util, json, sys
from pathlib import Path
root = Path(sys.argv[1]); package = root / 'src/apps/tube-designer/templates/product/single_face_security_window'
sys.path.insert(0, str(root / 'src/iCAX-Engine/framework/TemplateRuntime/python'))
d = json.loads((package / 'template.json').read_text(encoding='utf8'))
spec = importlib.util.spec_from_file_location('connection_scene_highlights', package / 'template.py')
module = importlib.util.module_from_spec(spec); sys.modules[spec.name] = module; spec.loader.exec_module(module)
cases = []
for face in ('single', 'three', 'five'):
    parameters = {field['key']: field['defaultValue'] for field in d['parameters']}
    parameters.update(faceType=face, accessDoorEnabled=True)
    document = module.display(parameters)
    items = [{'stableKey': item['key'], 'role': item.get('properties',{}).get('group',''),
              'profile': item.get('properties',{}).get('tubeDesigner.profile'),
              'member': item.get('properties',{}).get('assemblyFrame.member'),
              'placement': item.get('geometry',{}).get('placement')}
             for item in document['items'] if item.get('properties',{}).get('partKind') == 'tube']
    cases.append({'name': face, 'parameters': parameters, 'members': items})
print(json.dumps(cases))
`, root], { encoding: "utf8", env: { ...process.env, PYTHONDONTWRITEBYTECODE: "1" } }));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1360, height: 900 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://connection-highlights.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !file.startsWith(source + sep) || !/\.m?js$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://connection-highlights.test/");
  await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;background:#eef3f4;font-family:Arial,"Microsoft YaHei",sans-serif}main{display:flex;height:880px}.cam-context-pane{width:160px;height:880px;overflow:auto}.nested-left{height:450px;overflow:auto}.nested-left>div{height:1600px}.cam-info-pane{width:470px;height:880px;overflow:auto}.cam-info-pane:after{content:"";display:block;height:500px}.cam-viewport{width:730px;height:880px}.tube-designer-parameter-scroll{height:800px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:300px}` });
  const results = [];
  for (const testCase of cases) {
    const result = await page.evaluate(async ({ descriptor, testCase }) => {
      const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
      const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
      const { bindProductSceneParameterHighlights } = await import("/src/apps/tube-designer/webpage/entry.mjs");
      const { bindProductParameterDiagrams } = await import("/src/apps/tube-designer/webpage/productParameterDiagram.mjs");
      const { catalogText } = await import("/src/apps/tube-designer/webpage/productCatalog.mjs");
      const views = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
      const actions = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
      const { patchLibraryDom, rememberLibraryDom } = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
      const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
      const template = { ...descriptor, available: true, descriptorLoaded: true, name: catalogText(descriptor.displayName),
        groups: descriptor.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
        parameters: descriptor.parameters.map(field => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
          displayName: catalogText(field.displayName), groupKey: field.group,
          options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
      const members = testCase.members.map((member, index) => ({ ...member, entityId: `${testCase.name}-member-${index}` }));
      document.body.innerHTML = '<main><aside class="cam-context-pane"><div class="nested-left"><div><input id="draft-note" value="ongoing note"></div></div></aside><div class="cam-viewport"></div><aside class="cam-info-pane"></aside></main>';
      const mount = document.querySelector("main"), viewportNode = mount.querySelector(".cam-viewport");
      const viewport = createThreeViewport({ continuousRender: false, showGrid: false });
      viewport.mount(viewportNode);
      const cache = (url, type, data) => viewport.resourcePromises.set(`${url}@1`, Promise.resolve({ url, version: 1, type, data }));
      cache("tube-material", "material", { colorRGBA: 0x8FB8C9FF });
      const rows = members.map(member => {
        const length = member.member.axisLength, profile = member.profile;
        // Display dimensions and transforms come directly from the generated product.
        // Mesh tessellation is sufficient here: the regression checks render emphasis, not machining solids.
        const geometry = profile.kind === "circle"
          ? new THREE.CylinderGeometry(profile.width / 2, profile.width / 2, length, 24).rotateX(Math.PI / 2).translate(0, 0, length / 2)
          : new THREE.BoxGeometry(profile.width, profile.depth, length).translate(0, 0, length / 2);
        const indexed = geometry.index ? geometry : geometry.toNonIndexed();
        cache(member.entityId, "geometry", { kind: "mesh", positions: [...indexed.attributes.position.array],
          ...(indexed.index ? { indices: [...indexed.index.array] } : {}) });
        const frame = member.placement ?? member.member.sectionFrame;
        const origin = frame.origin ?? frame.originAtStart;
        return { entityId: member.entityId, data: { geometry: { url: member.entityId, version: 1 },
          material: { url: "tube-material", version: 1 }, geometryKind: 1,
          localToWorldMatrix: [
            frame.xAxis[0], frame.yAxis[0], frame.zAxis[0], origin[0],
            frame.xAxis[1], frame.yAxis[1], frame.zAxis[1], origin[1],
            frame.xAxis[2], frame.yAxis[2], frame.zAxis[2], origin[2],
            0, 0, 0, 1,
          ] } };
      });
      await viewport.applyViewSnapshot({ rows }, { get() { throw new Error("unexpected resource request"); } });
      viewport.setStandardView("front");
      viewport.fitView();
      const product = { entityId: "highlight-product", templateId: template.id, name: "防盗窗", quantity: 1, parameters: structuredClone(testCase.parameters) };
      const view = { activeAreaId: "view", viewport,
        scene: { tubeDesigner: { templates: [template], product, activeProductId: product.entityId, members, parts: [], joints: [] } },
        tubeDesignerParameterPanelProductId: product.entityId,
        tubeDesignerExpandedParameterGroups: ["section:materials", "section:process", ...template.groups.map(group => `group:${group.key}`)],
        tubeDesignerParameterDisclosureState: { initialized: true } };
      const requests = [];
      let release;
      const context = { mount, project: { projectId: "connection-highlights" }, actions: {},
        sceneProxy: { invoke(method, payload) {
          if (method !== "TubeDesigner.UpdateProductParameters") throw new Error("highlight generated a native request: " + method);
          requests.push(structuredClone(payload));
          return new Promise(resolve => { release = () => resolve({ tubeDesigner: { ...view.scene.tubeDesigner,
            product: { ...view.scene.tubeDesigner.product, parameters: structuredClone(payload.parameters), modelOutdated: false, partsOutdated: true } } }); });
        } } };
      mount.querySelector(".cam-info-pane").innerHTML = views.renderDesignerRightPane(context, view);
      mount.querySelectorAll("details").forEach(node => { node.open = true; });
      rememberLibraryDom(view, mount, "");
      const bind = () => { bindProductSceneParameterHighlights(mount, view); bindProductParameterDiagrams(mount); };
      const render = () => {
        const restore = capturePaneInteraction(mount);
        actions.captureDesignerScrollState(context, view);
        const left = mount.querySelector(".cam-context-pane").innerHTML;
        if (!patchLibraryDom(view, mount, { left, right: views.renderDesignerRightPane(context, view), overlay: "", suffix: "" })) throw new Error("refresh replaced pane root");
        actions.restoreDesignerScrollState(context, view, { deferred: false });
        restore(); bind();
      };
      context.actions.refreshActiveSceneState = async () => render();
      const ops = { renderProject: render };
      bind();
      const option = key => mount.querySelector(`[data-product-control-editor="${key}"] select`);
      const ids = () => [...viewport.getDebugState().emphasizedObjectIds].sort();
      const expected = prefixes => members.filter(member => prefixes.some(prefix => member.stableKey.startsWith(prefix))).map(member => member.entityId).sort();
      const check = (actual, wanted, message) => { if (JSON.stringify(actual) !== JSON.stringify(wanted)) throw new Error(message + " " + JSON.stringify({ actual, wanted })); };
      const mappings = [
        ["outerFrameConnection", ["outer_frame."]],
        ["doorFrameConnection", ["access_door.fixed_frame."]],
        ["doorLeafFrameConnection", ["access_door.leaf.frame."]],
        ["horizontalEndConnectionChoice", ["main_grid.horizontal.", "side_grid.horizontal.", "cap_grid.horizontal."]],
        ["verticalEndConnectionChoice", ["main_grid.vertical.", "side_grid.vertical.", "cap_grid.vertical."]],
      ];
      const tick = () => new Promise(done => queueMicrotask(done));
      const colorOf = id => {
        let color;
        viewport.sceneObjects.get(id)?.traverse(node => { if (color == null && node.material?.color) color = node.material.color.getHex(); });
        return color;
      };
      const counts = {};
      for (const [key, prefixes] of mappings) {
        const field = option(key), wanted = expected(prefixes);
        if (!field || !wanted.length) throw new Error("missing production connection or members: " + key);
        counts[key] = wanted.length;
        field.focus({ preventScroll: true }); await tick();
        check(ids(), wanted, key + " focus must emphasize only its generated members");
        for (const id of wanted) if (colorOf(id) !== 0xa855f7) throw new Error(key + " actual WebGL material was not purple");
        field.blur(); await tick(); check(ids(), [], key + " blur must clear emphasis");
        field.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
        check(ids(), wanted, key + " hover must use the same scene binding");
        field.dispatchEvent(new PointerEvent("pointerout", { bubbles: true, relatedTarget: viewportNode }));
        await tick(); check(ids(), [], key + " pointerout must clear emphasis");
      }
      // Reserve values are hidden manufacturing drafts in the current template.
      // Exercise their production event-to-WebGL bindings without inventing UI controls.
      for (const [key, prefixes] of [
        ["horizontalBranchReserve", ["outer_frame.", "main_grid.horizontal.", "cap_grid.horizontal."]],
        ["verticalBranchReserve", ["outer_frame.", "main_grid.vertical.", "cap_grid.vertical."]],
      ]) {
        if (mount.querySelector(`[data-tube-designer-parameter="${key}"]`)) throw new Error(key + " must remain a hidden manufacturing draft");
        const wanted = expected(prefixes);
        if (!wanted.length) throw new Error("missing generated members: " + key);
        counts[key] = wanted.length;
        mount.dispatchEvent(new CustomEvent("tube-designer-product-parameter-focus", {
          detail: { key, mode: "right", category: "process" },
        }));
        check(ids(), wanted, key + " production binding must emphasize only its generated members");
        for (const id of wanted) if (colorOf(id) !== 0xa855f7) throw new Error(key + " actual WebGL material was not purple");
        mount.dispatchEvent(new CustomEvent("tube-designer-product-parameter-focus", { detail: { mode: "right", category: "process" } }));
        check(ids(), [], key + " clearing the parameter must clear emphasis");
      }
      if (requests.length) throw new Error("focus and hover sent a native request");
      const outer = option("outerFrameConnection"), selected = expected(["outer_frame."])[0];
      viewport.setSelectedObjectIds([selected], selected);
      if (colorOf(selected) !== 0xffad1f) throw new Error("initial selection must be orange");
      outer.focus({ preventScroll: true }); await tick();
      if (colorOf(selected) !== 0xa855f7) throw new Error("inspector emphasis did not override selected orange");
      outer.blur(); await tick();
      if (colorOf(selected) !== 0xffad1f) throw new Error("blur did not restore orange selection");
      const horizontal = option("horizontalEndConnectionChoice"), vertical = option("verticalEndConnectionChoice");
      const note = mount.querySelector("#draft-note"), canvas = viewportNode.querySelector("canvas"), sceneObjects = new Map(viewport.sceneObjects);
      let clicks = 0; vertical.addEventListener("click", () => clicks++);
      const scrollNodes = [mount.querySelector(".cam-context-pane"), mount.querySelector(".nested-left"), mount.querySelector(".cam-info-pane"), mount.querySelector(".tube-designer-parameter-scroll")];
      // Outer left also needs its own overflow in addition to the nested scroll.
      mount.querySelector(".cam-context-pane").insertAdjacentHTML("beforeend", '<div style="height:900px"></div>');
      const setScrolls = values => scrollNodes.forEach((node, i) => { node.scrollTop = values[i]; });
      const scrolls = () => scrollNodes.map(node => node.scrollTop);
      async function edit(value, latestInteraction) {
        horizontal.value = value;
        const pending = actions.handleDesignerAreaAction(context, view, "tube-designer-product-control-change", horizontal, ops);
        for (let tries = 0; !release && tries < 100; tries++) await new Promise(done => setTimeout(done, 5));
        if (!release) throw new Error("connection change did not reach native parameter update");
        latestInteraction();
        const expectedScrolls = scrolls(), current = document.activeElement;
        const finish = release; release = null; finish(); await pending; bind();
        check(scrolls(), expectedScrolls, "response lost latest independent pane scrolls");
        if (document.activeElement !== current || option("horizontalEndConnectionChoice") !== horizontal || option("verticalEndConnectionChoice") !== vertical
            || mount.querySelector("#draft-note") !== note || viewportNode.querySelector("canvas") !== canvas) throw new Error("response replaced current controls, canvas or focus");
        if (view.tubeDesignerRightDraftDirty !== false || view.tubeDesignerPartsDraftDirty !== true) throw new Error("connection edit made the display model outdated");
        for (const [id, object] of sceneObjects) if (viewport.sceneObjects.get(id) !== object) throw new Error("connection edit regenerated render objects");
        await new Promise(done => requestAnimationFrame(() => requestAnimationFrame(done)));
        check(scrolls(), expectedScrolls, "late callback restored stale scrolls");
      }
      await edit("weld", () => {
        note.focus({ preventScroll: true }); note.value = "latest typing"; note.setSelectionRange(2, 8, "backward"); setScrolls([80, 90, 50, 150]);
      });
      if (note.value !== "latest typing" || note.selectionStart !== 2 || note.selectionEnd !== 8 || note.selectionDirection !== "backward") throw new Error("response lost latest text or selection");
      check(ids(), [], "response revived highlight after user left inspector");
      await edit("tabs", () => { vertical.focus({ preventScroll: true }); setScrolls([100, 120, 70, 180]); });
      check(ids(), expected(mappings[4][1]), "async response must retain the currently focused vertical connection highlight");
      vertical.click(); if (clicks !== 1) throw new Error("parameter patch replaced event listeners");
      if (requests.length !== 2 || requests.some(request => request.parameters.horizontalEndConnectionChoice !== undefined)) throw new Error("synthetic connection keys leaked into host parameters");
      if (testCase.name === "single") {
        horizontal.focus({ preventScroll: true }); setScrolls([100, 120, 70, 180]);
        const beforeConditions = structuredClone(view.scene.tubeDesigner.product.parameters), beforeScrolls = scrolls();
        view.scene.tubeDesigner.product.parameters.infillPattern = "vertical";
        view.tubeDesignerRightDraft = null;
        render(); await tick();
        if (horizontal.isConnected || option("horizontalEndConnectionChoice") || document.activeElement.matches("input, select, textarea")) throw new Error("removed conditional connection kept or stole focus");
        check(ids(), [], "removed focused connection must clear stale scene emphasis");
        check(scrolls(), beforeScrolls, "condition removal lost pane scrolls");
        if (view.scene.tubeDesigner.product.parameters.horizontalEndConnection !== beforeConditions.horizontalEndConnection) throw new Error("condition removal discarded the hidden connection draft");
        view.scene.tubeDesigner.product.parameters = beforeConditions;
        render();
      }
      // Publicly retain this case long enough to save a concrete scene screenshot.
      window.connectionHighlightTest = { viewport, view, mount, option, counts };
      return { face: testCase.name, memberCount: members.length, highlightedCounts: counts, nativeParameterUpdates: requests.length,
        canvasPreserved: true, sceneObjectsPreserved: true, focusSelectionAndFourScrollsPreserved: true };
    }, { descriptor, testCase });
    results.push(result);
    if (testCase.name === "single") {
      await page.evaluate(() => {
        const { mount, option, viewport } = window.connectionHighlightTest;
        mount.querySelectorAll("details").forEach(node => { node.open = node.dataset.tubeDesignerParameterGroup === "section:process"; });
        mount.querySelector(".tube-designer-parameter-scroll").scrollTop = 0;
        mount.querySelector(".cam-info-pane").scrollTop = 0;
        viewport.setSelectedObjectIds([]);
        viewport.setStandardView("front");
        viewport.fitViewToViewport();
        option("verticalEndConnectionChoice").focus({ preventScroll: true });
      });
      await page.screenshot({ path: resolve(output, "vertical-connection-highlight.png"), fullPage: true });
    }
    await page.evaluate(() => { window.connectionHighlightTest.viewport.dispose(); delete window.connectionHighlightTest; });
  }
  assert.deepEqual(errors, []);
  assert.equal(results.length, 3);
  assert.ok(results.find(result => result.face === "five").highlightedCounts.horizontalEndConnectionChoice > results[0].highlightedCounts.horizontalEndConnectionChoice);
  writeFileSync(resolve(output, "report.json"), JSON.stringify({ passed: true, cases: results }, null, 2));
  console.log("PASS production connection scene highlights: single/three/five faces, actual WebGL purple materials, exact member scopes, focus/hover clearing, two native parameter-only updates, preserved scene/control nodes and current interaction.");
} finally {
  await browser.close();
}
