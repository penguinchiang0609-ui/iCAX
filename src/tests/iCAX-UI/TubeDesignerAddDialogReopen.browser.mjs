import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
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
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
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
  console.log("Add-product diagram remains parameter-driven after closing and reopening the dialog.");
} finally {
  await browser.close();
}
