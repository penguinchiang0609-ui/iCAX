import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { renderDesignerLeftPane, renderDesignerRightPane, renderDesignerViewportOverlay } from "../../apps/tube-designer/webpage/designerViews.mjs";
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
values.width = 1200;
values.height = 2000;
const product = { entityId: "window-1", templateId: raw.id, name: "防盗窗", quantity: 1, parameters: values };
const view = {
  activeAreaId: "view",
  pending: false,
  scene: { tubeDesigner: {
    templates: [template], product, members: Array.from({ length: 53 }, () => ({})), joints: [], parts: [],
    specificationAnnotations: [
      { id: "width", parameter: "width" },
      { id: "horizontal-spacing", parameter: "horizontalMaximumCenterSpacing" },
    ],
  } },
};

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 900, height: 720 } });
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
  const leftView = structuredClone(view);
  leftView.scene.tubeDesigner.instances = [
    { entityId: "newer", name: "新实例", createdAt: "2026-09-15T22:18:19+08:00", templateId: raw.id, parameters: values },
    { entityId: "older", name: "旧实例", createdAt: "2026-09-15T21:59:21+08:00", templateId: raw.id, parameters: values },
  ];
  await page.setContent(`<style>*{box-sizing:border-box}body{margin:0}.tube-designer-workspace{display:grid;grid-template-columns:220px 420px 480px;height:720px}.cam-left-pane{height:720px}.cam-viewport{position:relative;height:720px;background:#13252d}.cam-viewcube{position:absolute;top:12px;right:12px;width:116px;height:116px}.cam-info-pane{height:720px}${tubeDesignerCss}</style><main class="tube-designer-workspace"><aside class="cam-left-pane">${renderDesignerLeftPane({}, leftView)}</aside><section class="cam-viewport"><div class="cam-viewcube"></div>${renderDesignerViewportOverlay({}, view)}</section><aside class="cam-info-pane">${renderDesignerRightPane({}, view)}</aside></main>`);
  const result = await page.evaluate(async ({ state }) => {
    const { handleDesignerAreaAction } = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const workspace = document.querySelector(".tube-designer-workspace");
    const viewport = workspace.querySelector(".cam-viewport");
    const panel = workspace.querySelector("[data-tube-designer-parameter-form]");
    const button = viewport.querySelector(".tube-designer-scene-specification-toggle");
    const status = viewport.querySelector("[data-tube-designer-runtime-status]");
    const heading = panel.querySelector(".tube-designer-heading span");
    const material = panel.querySelector('[data-tube-designer-parameter-group="section:materials"]');
    const process = panel.querySelector('[data-tube-designer-parameter-group="section:process"]');
    const initial = {
      hasSpecificationButton: Boolean(button),
      hasSpecificationTree: Boolean(viewport.querySelector("[data-tube-designer-specification-tree]")),
      specificationGroups: [...viewport.querySelectorAll("[data-tube-designer-annotation-groups]")]
        .map((node) => node.dataset.tubeDesignerAnnotationGroups),
      pressed: button?.getAttribute("aria-pressed"),
      noProductDiagramControl: !viewport.querySelector(".tube-designer-scene-product-diagram-control"),
      noProductDiagramFlyout: !viewport.querySelector(".tube-designer-scene-product-diagram-flyout"),
      noSceneInformationOverlay: !viewport.querySelector(".tube-designer-overlay"),
      heading: heading?.textContent?.trim(),
      materialOpen: material?.open,
      processOpen: process?.open,
      instanceOrder: [...workspace.querySelectorAll(".tube-designer-instance-card[data-tube-designer-instance-id]")]
        .map((node) => node.dataset.tubeDesignerInstanceId),
    };
    const collapseButton = viewport.querySelector('[data-cam-action="tube-designer-toggle-specification-tree-collapse"]');
    await handleDesignerAreaAction({ mount: workspace }, state,
      "tube-designer-toggle-specification-tree-collapse", collapseButton, {
        renderProject() { throw new Error("Collapsing the specification tree must not rebuild the workspace."); },
      });
    const collapsedTree = viewport.querySelector("[data-tube-designer-specification-tree]");
    const collapsedRect = collapsedTree.getBoundingClientRect();
    const collapsed = {
      state: state.tubeDesignerSpecificationTreeCollapsed,
      leftGap: Math.round(collapsedRect.left - viewport.getBoundingClientRect().left),
      width: Math.round(collapsedRect.width),
      hasBody: Boolean(collapsedTree.querySelector(".tube-designer-scene-specification-tree-body")),
      expanded: collapsedTree.querySelector('[data-cam-action="tube-designer-toggle-specification-tree-collapse"]')
        ?.getAttribute("aria-expanded"),
    };
    const expandButton = collapsedTree.querySelector('[data-cam-action="tube-designer-toggle-specification-tree-collapse"]');
    await handleDesignerAreaAction({ mount: workspace }, state,
      "tube-designer-toggle-specification-tree-collapse", expandButton, { renderProject() {} });
    const expandedTree = viewport.querySelector("[data-tube-designer-specification-tree]");
    const expanded = {
      state: state.tubeDesignerSpecificationTreeCollapsed,
      hasBody: Boolean(expandedTree.querySelector(".tube-designer-scene-specification-tree-body")),
      expanded: expandedTree.querySelector('[data-cam-action="tube-designer-toggle-specification-tree-collapse"]')
        ?.getAttribute("aria-expanded"),
    };
    const activeRootButton = viewport.querySelector(".tube-designer-scene-specification-toggle");
    await handleDesignerAreaAction({ mount: workspace }, state, "tube-designer-toggle-specification-annotations", activeRootButton, {
      renderProject() { throw new Error("The specification toggle must not rebuild the workspace."); },
    });
    const updatedButton = viewport.querySelector(".tube-designer-scene-specification-toggle");
    const tree = viewport.querySelector("[data-tube-designer-specification-tree]");
    const viewportRect = viewport.getBoundingClientRect();
    const buttonRect = updatedButton.getBoundingClientRect();
    const treeRect = tree.getBoundingClientRect();
    const statusRect = status.getBoundingClientRect();
    const viewCubeRect = viewport.querySelector(".cam-viewcube").getBoundingClientRect();
    const toggled = {
      pressed: updatedButton.getAttribute("aria-pressed"),
      leftGap: Math.round(treeRect.left - viewportRect.left),
      topGap: Math.round(treeRect.top - viewportRect.top),
      separateFromStatus: buttonRect.left >= statusRect.right || buttonRect.right <= statusRect.left
        || buttonRect.top >= statusRect.bottom || buttonRect.bottom <= statusRect.top,
      separateFromViewCube: buttonRect.right <= viewCubeRect.left,
    };
    await handleDesignerAreaAction({ mount: workspace }, state, "tube-designer-toggle-specification-annotations", updatedButton, { renderProject() {} });
    const restoredButton = viewport.querySelector(".tube-designer-scene-specification-toggle");
    const mainGrid = [...viewport.querySelectorAll('[data-cam-action="tube-designer-toggle-specification-annotation-group"]')]
      .find((node) => node.dataset.tubeDesignerAnnotationGroups === "main_grid");
    await handleDesignerAreaAction({ mount: workspace }, state,
      "tube-designer-toggle-specification-annotation-group", mainGrid, { renderProject() {} });
    return {
      initial,
      collapsed,
      expanded,
      toggled,
      restored: { pressed: restoredButton.getAttribute("aria-pressed") },
      partial: {
        rootState: viewport.querySelector(".tube-designer-scene-specification-toggle")?.getAttribute("aria-checked"),
        mainGridState: [...viewport.querySelectorAll('[data-cam-action="tube-designer-toggle-specification-annotation-group"]')]
          .find((node) => node.dataset.tubeDesignerAnnotationGroups === "main_grid")?.getAttribute("aria-checked"),
      },
    };
  }, { state: view });

  assert.deepEqual(result.initial, {
    hasSpecificationButton: true,
    hasSpecificationTree: true,
    specificationGroups: ["overall main_grid", "overall", "main_grid"],
    pressed: "true",
    noProductDiagramControl: true,
    noProductDiagramFlyout: true,
    noSceneInformationOverlay: true,
    heading: "1,200 × 2,000 mm · 53 个装配构件",
    materialOpen: true,
    processOpen: true,
    instanceOrder: ["older", "newer"],
  });
  assert.deepEqual(result.collapsed, {
    state: true, leftGap: 0, width: 34, hasBody: false, expanded: "false",
  });
  assert.deepEqual(result.expanded, { state: false, hasBody: true, expanded: "true" });
  assert.deepEqual(result.toggled, {
    pressed: "false", leftGap: 14, topGap: 56,
    separateFromStatus: true, separateFromViewCube: true,
  });
  assert.deepEqual(result.restored, { pressed: "true" });
  assert.deepEqual(result.partial, { rootState: "mixed", mainGridState: "false" });
  console.log("规格标注按参数组显示在场景左侧，支持根节点、分组节点和半选状态。 ");
} finally {
  await browser.close();
}
