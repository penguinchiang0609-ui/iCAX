import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { renderBatchExcelTemplateDialog } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";
import { batchExcelColumnsFromTemplate } from "../../apps/tube-designer/webpage/designerActions.mjs";

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
const generatedColumns = batchExcelColumnsFromTemplate(template);
assert.equal(generatedColumns.find((column) => column.key === "__instanceName")?.title, "实例名称");
assert.equal(generatedColumns.find((column) => column.key === "__instanceQuantity")?.title, "生产数量");
assert.equal(generatedColumns.find((column) => column.key === "faceType")?.title, "面型");
assert.notEqual(generatedColumns.find((column) => column.key === "faceType")?.title, "faceType");
assert.equal(new Set(generatedColumns.map((column) => column.title)).size, generatedColumns.length);
assert.equal(generatedColumns.find((column) => column.key === "faceType")?.options?.[0]?.label.length > 0, true);
const columns = [
  {
    key: "__instanceName", title: "实例名称", displayName: "实例名称", groupTitle: "实例信息",
    included: true, required: false, defaultValue: "", inputKind: "text",
  },
  {
    key: "__instanceQuantity", title: "生产数量", displayName: "生产数量", groupTitle: "实例信息",
    included: true, required: true, defaultValue: "1", inputKind: "number",
  },
  {
    key: "width", title: "产品宽度", displayName: "产品宽度", groupTitle: "尺寸参数",
    included: false, required: true, defaultValue: "1200", inputKind: "number",
  },
];
const view = {
  pending: false,
  scene: { tubeDesigner: { templates: [template] } },
  tubeDesignerExcelTemplateDialog: { templateId: template.id, columns },
};

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1500, height: 900 } });
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
  await page.setContent(`<main id="mount">${renderBatchExcelTemplateDialog({ templates: [template] }, view)}</main>`);
  const result = await page.evaluate(async ({ state, descriptor }) => {
    const { handleDesignerAreaAction } = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const mount = document.querySelector("#mount");
    const includeHeader = mount.querySelector('[data-tube-designer-excel-select-all="include"]');
    const requiredHeader = mount.querySelector('[data-tube-designer-excel-select-all="required"]');
    const rowInclude = [...mount.querySelectorAll("[data-tube-designer-excel-include]")];
    const rowRequired = [...mount.querySelectorAll("[data-tube-designer-excel-required]")];
    const ops = {
      renderProject() {},
      showNotice() {},
    };
    const requests = [];
    const context = {
      mount,
      appProxy: { bridge: { async saveFileDialog() { return "D:\\exports\\防盗窗.xlsx"; } } },
      productProxy: { async invoke(method, payload) {
        requests.push({ method, payload });
        return { templatePath: payload.targetPath };
      } },
    };

    includeHeader.checked = false;
    await handleDesignerAreaAction(context, state, "tube-designer-excel-template-toggle-all", includeHeader, ops);
    const includeNone = rowInclude.every((input) => !input.checked);
    includeHeader.checked = true;
    await handleDesignerAreaAction(context, state, "tube-designer-excel-template-toggle-all", includeHeader, ops);
    const includeAll = rowInclude.every((input) => input.checked);
    rowInclude[0].checked = false;
    await handleDesignerAreaAction(context, state, "tube-designer-excel-template-selection-change", rowInclude[0], ops);
    const includeMixed = includeHeader.indeterminate && !includeHeader.checked;

    requiredHeader.checked = false;
    await handleDesignerAreaAction(context, state, "tube-designer-excel-template-toggle-all", requiredHeader, ops);
    const requiredNone = rowRequired.every((input) => !input.checked);
    requiredHeader.checked = true;
    await handleDesignerAreaAction(context, state, "tube-designer-excel-template-toggle-all", requiredHeader, ops);
    const requiredAll = rowRequired.every((input) => input.checked);

    rowInclude[0].checked = true;
    mount.querySelector('[data-tube-designer-excel-alias="width"]').value = "成品宽度";
    state.scene.tubeDesigner.templates = [descriptor];
    await handleDesignerAreaAction(context, state, "tube-designer-excel-template-export",
      mount.querySelector('[data-cam-action="tube-designer-excel-template-export"]'), ops);
    return {
      headerCheckboxes: mount.querySelectorAll(".tube-designer-excel-column-head input[type=checkbox]").length,
      rowIncludeText: [...mount.querySelectorAll(".tube-designer-excel-include")].map((node) => node.textContent.trim()),
      rowRequiredText: [...mount.querySelectorAll(".tube-designer-excel-required")].map((node) => node.textContent.trim()),
      aliasCount: mount.querySelectorAll("[data-tube-designer-excel-alias]").length,
      includeNone, includeAll, includeMixed, requiredNone, requiredAll,
      request: requests[0],
    };
  }, { state: view, descriptor: template });

  assert.equal(result.headerCheckboxes, 2);
  assert.deepEqual(result.rowIncludeText, ["", "", ""]);
  assert.deepEqual(result.rowRequiredText, ["", "", ""]);
  assert.equal(result.aliasCount, 3);
  assert.equal(result.includeNone, true);
  assert.equal(result.includeAll, true);
  assert.equal(result.includeMixed, true);
  assert.equal(result.requiredNone, true);
  assert.equal(result.requiredAll, true);
  assert.equal(result.request.method, "TubeDesigner.ExportBatchExcelTemplate");
  assert.equal(result.request.payload.columns.find((column) => column.key === "width")?.title, "成品宽度");
  assert.equal(result.request.payload.columns.find((column) => column.key === "__instanceQuantity")?.title, "生产数量");
  console.log("Excel 导出列支持中文列名、携带/必填全选、简洁行复选框和可选列别名。");
} finally {
  await browser.close();
}
