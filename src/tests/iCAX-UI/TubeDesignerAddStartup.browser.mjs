import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

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
const metadata = { ...descriptor, descriptorLoaded: false };
delete metadata.parameters;

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
  headless: true,
  ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}),
});
try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 760 } });
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
  const result = await page.evaluate(async ({ templateMetadata, templateDescriptor, css }) => {
    const { handleDesignerRibbonCommand } = await import(
      "/src/apps/tube-designer/webpage/designerActions.mjs"
    );
    document.body.innerHTML = `<style>${css}</style><div data-product-surface="project"><div class="cam-workbench"></div></div>`;
    const mount = document.querySelector("[data-product-surface='project']");
    let resolveStartup;
    let resolveUserData;
    let resolveDescriptor;
    const startup = new Promise((resolvePromise) => { resolveStartup = resolvePromise; });
    const userData = new Promise((resolvePromise) => { resolveUserData = resolvePromise; });
    const descriptorRequest = new Promise((resolvePromise) => { resolveDescriptor = resolvePromise; });
    let descriptorCalls = 0;
    const view = {
      pending: true,
      tubeDesignerLoading: true,
      tubeDesignerSynchronizationPromise: startup,
      tubeDesignerUserDataSynchronizationPromise: userData,
      scene: {},
      viewport: { setContinuousRendering() {} },
    };
    const context = {
      mount,
      sceneProxy: {
        invoke(method) {
          if (method !== "TubeDesigner.GetTemplateDescriptor") throw new Error(`Unexpected ${method}`);
          descriptorCalls += 1;
          return descriptorRequest;
        },
      },
    };
    const ops = { renderProject() {}, showNotice() {}, appendProjectLog() {} };
    const first = handleDesignerRibbonCommand(context, view, "designer.add", ops);
    const second = handleDesignerRibbonCommand(context, view, "designer.add", ops);
    await Promise.resolve();
    const ignoredBeforeStartup = !mount.querySelector("[data-tube-designer-add-dialog]")
      && descriptorCalls === 0;

    view.scene.tubeDesigner = { templates: [templateMetadata] };
    view.pending = false;
    view.tubeDesignerLoading = false;
    resolveStartup();
    await new Promise((done) => setTimeout(done, 20));
    const visibleOnFirstClick = Boolean(mount.querySelector("[data-tube-designer-add-dialog]"));
    const progressVisible = mount.querySelector("[data-tube-designer-template-switch-progress]")
      ?.textContent.includes("正在打开产品目录") === true;
    const disabledWhileLoading = mount.querySelector("[data-cam-action='tube-designer-confirm-add']")?.disabled === true;

    const third = handleDesignerRibbonCommand(context, view, "designer.add", ops);
    resolveUserData();
    await new Promise((done) => setTimeout(done, 20));
    const oneColdRequest = descriptorCalls === 1;
    resolveDescriptor({ template: templateDescriptor });
    await Promise.all([first, second, third]);
    const dialog = mount.querySelector("[data-tube-designer-add-dialog]");
    return {
      ignoredBeforeStartup,
      visibleOnFirstClick,
      progressVisible,
      disabledWhileLoading,
      oneColdRequest,
      ready: Boolean(dialog)
        && !dialog.querySelector("[data-tube-designer-template-switch-progress]")
        && dialog.querySelector("[data-cam-action='tube-designer-confirm-add']")?.disabled === false
        && Boolean(dialog.querySelector("[data-tube-designer-parameter]")),
      openingSettled: view.tubeDesignerAddOpeningPromise == null,
    };
  }, { templateMetadata: metadata, templateDescriptor: descriptor, css: tubeDesignerCss });
  assert.equal(result.ignoredBeforeStartup, true);
  assert.equal(result.visibleOnFirstClick, true);
  assert.equal(result.progressVisible, true);
  assert.equal(result.disabledWhileLoading, true);
  assert.equal(result.oneColdRequest, true);
  assert.equal(result.ready, true);
  assert.equal(result.openingSettled, true);
  console.log("PASS startup add: one click is queued, visible during cold load, and issues one descriptor request");
} finally {
  await browser.close();
}
