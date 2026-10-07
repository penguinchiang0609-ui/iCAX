import assert from "node:assert/strict";
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";

const raw = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/template.json",
  import.meta.url,
), "utf8"));
const groups = raw.groups.map((group) => ({ ...group, displayName: catalogText(group.displayName) }));
const descriptor = {
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
const compact = {
  id: descriptor.id,
  version: descriptor.version,
  name: descriptor.name,
  available: true,
  descriptorLoaded: true,
  extensions: { catalog: descriptor.extensions.catalog },
};

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const output = resolve(sourceRoot, "../output/tests/product-add-dialog-default-expanded");
mkdirSync(output, { recursive: true });
const productRoot = resolve(sourceRoot, "apps/tube-designer/templates/product");
const completeTemplates = readdirSync(productRoot, { withFileTypes: true }).filter(entry => entry.isDirectory())
  .flatMap(entry => {
    const directory = resolve(productRoot, entry.name), path = resolve(directory, "template.json");
    if (!existsSync(path)) return [];
    const raw = JSON.parse(readFileSync(path, "utf8"));
    if (raw.extensions?.catalog?.listed === false) return [];
    const groups = (raw.groups ?? []).map(group => ({ ...group, displayName: catalogText(group.displayName) }));
    const displayPath = resolve(directory, "display.json"), assetData = {};
    for (const kind of ["schematic", "icon"]) {
      const asset = resolve(directory, "resource", `${kind}.svg`);
      if (existsSync(asset)) assetData[kind] = `data:image/svg+xml;base64,${readFileSync(asset).toString("base64")}`;
    }
    return [{ ...raw, available: true, descriptorLoaded: true, name: catalogText(raw.displayName), groups,
      display: existsSync(displayPath) ? JSON.parse(readFileSync(displayPath, "utf8")) : null, assetData,
      parameters: raw.parameters.map(field => ({ ...field, displayName: catalogText(field.displayName),
        type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType, groupKey: field.group,
        group: groups.find(group => group.key === field.group)?.displayName ?? field.group,
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName), displayName: catalogText(choice.displayName) })) })) }];
  });
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://tube-designer.test/**", async (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html lang="zh-CN"><head><meta charset="UTF-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"><style>html,body{margin:0;overflow:hidden;height:100%;font-family:"Microsoft YaHei",sans-serif}#mount{height:100%;overflow:hidden}</style></head><body></body></html>' });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/")
      || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
      || !/\.(?:m?js|css)$/.test(path)) return route.abort();
    return route.fulfill({ contentType: path.endsWith(".css") ? "text/css" : "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://tube-designer.test/");
  const result = await page.evaluate(async ({ complete, catalogue }) => {
    const {
      handleDesignerAreaAction,
      handleDesignerRibbonCommand,
    } = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    document.body.innerHTML = '<main id="mount"><div class="cam-workbench"></div></main>';
    const mount = document.querySelector("#mount");
    let descriptorCalls = 0;
    const context = {
      mount,
      sceneProxy: {
        async invoke(method) {
          if (method === "TubeDesigner.GetTemplateDescriptor") {
            descriptorCalls += 1;
            return { template: structuredClone(complete) };
          }
          if (method === "TubeDesigner.GetPunchTools") {
            return { tools: [{ id: "test-tool", name: "测试模具", available: true }] };
          }
          throw new Error(`Unexpected request: ${method}`);
        },
      },
    };
    const view = {
      activeAreaId: "view",
      pending: false,
      scene: { tubeDesigner: { templates: [structuredClone(catalogue)] } },
    };
    const ops = { renderProject() {} };
    const diagramLabel = () => mount.querySelector(".tube-designer-product-structure-svg")
      ?.getAttribute("aria-label") ?? "";
    const open = () => handleDesignerRibbonCommand(context, view, "designer.add", ops);
    const close = () => handleDesignerAreaAction(
      context, view, "tube-designer-cancel-add",
      mount.querySelector('[data-cam-action="tube-designer-cancel-add"]'), ops,
    );
    const changeFace = async (value) => {
      const select = mount.querySelector('[data-tube-designer-parameter="faceType"]');
      select.value = value;
      await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", select, ops);
      return diagramLabel();
    };

    await open();
    const firstLabel = diagramLabel();
    await close();

    // This is what a normal native scene response returns after the first use:
    // a compact catalogue reference with no parameters or product diagram.
    view.scene.tubeDesigner.templates = [structuredClone(catalogue)];
    await open();
    const secondInitialLabel = diagramLabel();
    const threeLabel = await changeFace("three");
    const fiveLabel = await changeFace("five");
    return {
      descriptorCalls,
      firstLabel,
      secondInitialLabel,
      threeLabel,
      fiveLabel,
      secondHasParameters: Array.isArray(view.scene.tubeDesigner.templates[0].parameters),
      secondHasDiagram: view.scene.tubeDesigner.templates[0].extensions?.productDiagram?.kind ?? "",
    };
  }, { complete: descriptor, catalogue: compact });

  assert.equal(result.descriptorCalls, 1, "The second opening must reuse the startup-loaded descriptor.");
  assert.match(result.firstLabel, /单面防盗窗/);
  assert.match(result.secondInitialLabel, /单面防盗窗/);
  assert.match(result.threeLabel, /三面防盗窗/);
  assert.match(result.fiveLabel, /五面防盗窗/);
  assert.equal(result.secondHasParameters, true);
  assert.equal(result.secondHasDiagram, "security-window");
  const expanded = await page.evaluate(async completeTemplates => {
    const { handleDesignerAreaAction, handleDesignerRibbonCommand } = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const { renderDesignerAddDialog } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const { getCatalogGroupKeys } = await import("/src/apps/tube-designer/webpage/productCatalog.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    const style = document.createElement("style"); style.textContent = tubeDesignerCss; document.head.append(style);
    document.body.innerHTML = `<main id="mount" class="tube-designer-workspace"><div class="cam-workbench">
      <canvas id="scene-canvas" width="400" height="300"></canvas>
      <aside id="scene-left" style="height:100px;overflow:auto"><input id="left-input" value="LEFT-SCENE"><div style="height:700px"></div></aside>
      <aside id="scene-right" style="height:100px;overflow:auto"><input id="right-input" value="RIGHT-SCENE"><div style="height:700px"></div></aside>
    </div></main>`;
    const mount = document.querySelector("#mount"), canvas = document.querySelector("#scene-canvas"),
      left = document.querySelector("#left-input"), right = document.querySelector("#right-input");
    let inputEvents = 0, wholeRenders = 0, releaseDescriptor, firstDescriptor = true;
    left.addEventListener("input", () => inputEvents++); right.addEventListener("input", () => inputEvents++);
    const nested = structuredClone(completeTemplates.find(template => template.id === "minimal-protective-grille"));
    nested.id += "-nested-fixture";
    nested.extensions.catalog.categoryPath = ["嵌套示例", "子分类", "孙分类"];
    nested.extensions.catalog.groupOrder = 99;
    const allTemplates = [...completeTemplates, nested], calls = [];
    const catalogueOf = template => ({ id: template.id, version: template.version, available: true, descriptorLoaded: true,
      name: template.name, displayName: template.displayName, extensions: { catalog: template.extensions.catalog }, assetData: template.assetData });
    const view = { activeAreaId: "view", pending: false,
      scene: { tubeDesigner: { templates: allTemplates.map(catalogueOf) } }, viewport: { setContinuousRendering() {} } };
    const context = { mount, sceneProxy: { async invoke(method, payload) {
      if (method === "TubeDesigner.GetTemplateDescriptor") {
        calls.push(payload.templateId);
        if (firstDescriptor) { firstDescriptor = false; await new Promise(resolve => { releaseDescriptor = resolve; }); }
        return { template: structuredClone(allTemplates.find(template => template.id === payload.templateId)) };
      }
      if (method === "TubeDesigner.GetPunchTools") return { tools: [{ id: "test-tool", name: "测试模具", available: true }] };
      throw new Error(`Unexpected request: ${method}`);
    } } };
    const ops = { renderProject() { wholeRenders++; } };
    const require = (condition, message) => { if (!condition) throw new Error(message); };
    const frames = () => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const states = () => Object.fromEntries([...mount.querySelectorAll(".tube-designer-template-group")]
      .map(group => [group.dataset.tubeDesignerTemplateGroupId, group.classList.contains("expanded")]));
    const allOpen = () => {
      const keys = getCatalogGroupKeys(view.scene.tubeDesigner.templates), current = states();
      require(keys.length === Object.keys(current).length && keys.every(key => current[key] === true), "Catalogue roots or nested groups were not all expanded");
      return keys.length;
    };
    const assertScene = () => {
      require(document.querySelector("#scene-canvas") === canvas && document.querySelector("#left-input") === left
        && document.querySelector("#right-input") === right, "Add catalogue replaced the main canvas or pane inputs");
      require(wholeRenders === 0, "Add catalogue refreshed the whole scene");
    };
    const action = (name, target) => handleDesignerAreaAction(context, view, name, target, ops);
    const open = () => handleDesignerRibbonCommand(context, view, "designer.add", ops);
    const close = () => action("tube-designer-cancel-add", mount.querySelector('.tube-designer-dialog-footer [data-cam-action="tube-designer-cancel-add"]'));
    const toggle = key => action("tube-designer-toggle-template-group", mount.querySelector(`button[data-tube-designer-template-group-id="${key}"]`));
    const cold = open();
    while (!releaseDescriptor) await frames();
    const coldPendingCount = allOpen(); assertScene();
    releaseDescriptor(); await cold; await frames();
    const coldReadyCount = allOpen(); assertScene();
    const windowKey = "template-path:窗", nestedKey = "template-path:嵌套示例/子分类";
    await toggle(windowKey); await toggle(nestedKey); await frames();
    require(states()[windowKey] === false && states()[nestedKey] === false, "Manual collapse did not apply");
    const detached = document.createElement("template"); detached.innerHTML = renderDesignerAddDialog(view.scene.tubeDesigner, view);
    require(!detached.content.querySelector(`.tube-designer-template-group[data-tube-designer-template-group-id="${windowKey}"]`).classList.contains("expanded"), "Rendering forced the selected category open again");
    const face = mount.querySelector('[data-tube-designer-parameter="faceType"]'); face.value = "three";
    await action("tube-designer-parameter-change", face);
    require(states()[windowKey] === false && states()[nestedKey] === false, "Parameter patch lost manual collapsed groups");
    const guardrailBefore = states()["template-path:护栏"];
    await action("tube-designer-select-template", mount.querySelector('[data-tube-designer-template-id="minimal-protective-grille"]'));
    require(states()[windowKey] === false && states()[nestedKey] === false && states()["template-path:护栏"] === guardrailBefore,
      "Switching styles lost another group's expanded/collapsed state");
    assertScene();
    await close();
    view.scene.tubeDesigner.templates = allTemplates.map(catalogueOf);
    const callsBeforeWarm = calls.length;
    await open(); await frames();
    const warmCount = allOpen();
    require(calls.length === callsBeforeWarm, "Warm reopen fetched the cached default descriptor again");
    assertScene();
    await close();
    view.scene.tubeDesigner.templates = completeTemplates.map(catalogueOf);
    await open(); await frames(); allOpen(); assertScene();
    left.dispatchEvent(new Event("input", { bubbles: true })); right.dispatchEvent(new Event("input", { bubbles: true }));
    require(inputEvents === 2, "Existing main-pane event listeners were removed");
    // The screenshot and wheel checks use only the real production catalogue.
    globalThis.__expandedAddFixture = { mount, view, context, ops, allOpen, assertScene };
    return { coldPendingCount, coldReadyCount, warmCount, manuallyCollapsedCurrentGroupPreserved: true,
      nestedCollapsePreserved: true, styleSwitchPreservesOtherGroups: true, cancelReopenExpandsEverything: true,
      cachedWarmDescriptor: true, actualCatalogueCount: completeTemplates.length, sceneNodesAndListenersPreserved: true, wholeRenders };
  }, completeTemplates);
  await page.setViewportSize({ width: 1920, height: 1080 });
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const geometry = await page.evaluate(() => {
    const f = globalThis.__expandedAddFixture; f.allOpen(); f.assertScene();
    const dialog = f.mount.querySelector("[data-tube-designer-add-dialog]"), rect = dialog.getBoundingClientRect();
    const grids = [...dialog.querySelectorAll(".tube-designer-template-card-grid")].map(grid => ({
      columns: getComputedStyle(grid).gridTemplateColumns.split(" ").length,
      names: [...grid.querySelectorAll(".tube-designer-template-card strong")].map(node => node.textContent) }));
    const panes = [".tube-designer-template-list", ".tube-designer-config-parameters"].map(selector => {
      const node = dialog.querySelector(selector); node.scrollTop = 0;
      return { selector, max: node.scrollHeight - node.clientHeight, overflowY: getComputedStyle(node).overflowY,
        horizontalOverflow: node.scrollWidth > node.clientWidth + 1 };
    });
    return { width: rect.width, height: rect.height, grids, panes };
  });
  assert.ok(geometry.grids.every(grid => grid.columns === 2), "Default expanded groups retain the two-column cards");
  assert.ok(geometry.panes.every(pane => pane.max > 0 && pane.overflowY === "auto" && !pane.horizontalOverflow));
  const scrolls = [];
  for (const pane of geometry.panes) {
    const box = await page.locator(pane.selector).boundingBox();
    await page.mouse.move(box.x + 45, box.y + 50); await page.mouse.wheel(0, 240); await page.waitForTimeout(100);
    const positions = await page.evaluate(selector => ({ selected: document.querySelector(selector).scrollTop,
      other: document.querySelector(selector === ".tube-designer-template-list" ? ".tube-designer-config-parameters" : ".tube-designer-template-list").scrollTop }), pane.selector);
    assert.ok(positions.selected > 0); assert.equal(positions.other, 0, "Pane wheel scroll leaked into the other pane");
    scrolls.push({ selector: pane.selector, afterWheel: positions.selected });
    await page.locator(pane.selector).evaluate(node => { node.scrollTop = 0; });
  }
  // Keep the verification-only pane labels out of the product screenshot.
  await page.locator("#scene-left, #scene-right").evaluateAll(nodes => {
    for (const node of nodes) node.style.visibility = "hidden";
  });
  await page.screenshot({ path: resolve(output, "desktop.png") });
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, "report.json"), JSON.stringify({ diagramReopen: result, defaultExpanded: expanded, geometry, scrolls, errors }, null, 2));
  console.log("Add-product diagram remains parameter-driven after closing and reopening the dialog.");
  console.log(JSON.stringify({ defaultExpanded: expanded, geometry, scrolls, errors }, null, 2));
} finally {
  await browser.close();
}
