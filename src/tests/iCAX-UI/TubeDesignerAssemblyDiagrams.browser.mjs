import assert from "node:assert/strict";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const assemblyRoot = resolve(sourceRoot, "apps/tube-designer/templates/assembly");
const templates = readdirSync(assemblyRoot, { withFileTypes: true })
  .filter((entry) => entry.isDirectory())
  .map((entry) => resolve(assemblyRoot, entry.name, "assembly.json"))
  .filter(existsSync)
  .map((path) => JSON.parse(readFileSync(path, "utf8")))
  .filter((template) => template.catalogueHidden !== true);
assert.ok(templates.length >= 15);

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1840 }, deviceScaleFactor: 1 });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://assembly-diagrams.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><meta charset=\"utf-8\"><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
        || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://assembly-diagrams.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `
    body { margin: 16px; background: #f5f9f8; font-family: 'Microsoft YaHei', sans-serif; }
    main { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
    article { min-width: 0; padding: 8px; border: 1px solid #d9e6e8; background: white; }
    article > header { font-size: 12px; font-weight: bold; color: #33515a; }
    .tube-tool-library-diagram-art { min-height: 210px; padding: 5px; }
  ` });
  const inspected = await page.evaluate(async (templates) => {
    const assembly = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const { renderToolParameterDiagramSvg } = await import("/src/apps/tube-designer/webpage/toolParameterDiagram.mjs");
    const main = document.createElement("main");
    document.body.append(main);
    const results = [];
    for (const template of templates) {
      const article = document.createElement("article");
      article.dataset.id = template.id;
      const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [template],
        tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "example",
          workModeUserSelected: true, showDiagram: true } };
      article.innerHTML = `<header>${template.id}</header>${assembly.renderAssemblyLibraryViewportOverlay({}, view)}`;
      main.append(article);
      const diagram = article.querySelector('[data-tube-assembly-diagram-dock]');
      const svg = diagram.querySelector(".tool-parameter-svg");
      const a = [...svg.querySelectorAll(".tool-diagram-part-a")];
      const b = [...svg.querySelectorAll(".tool-diagram-part-b")];
      const mark = [...svg.querySelectorAll(".tool-diagram-joint,.tool-diagram-cut-line")];
      const box = svg.getBoundingClientRect();
      const allowedKeys = template.parameters.filter((parameter) =>
        parameter.scope === "scene" || parameter.scope === "product").map((parameter) => parameter.key);
      results.push({ id: template.id, a: a.length, b: b.length, mark: mark.length,
        filled: a.every((node) => getComputedStyle(node).fill !== "none"),
        visible: box.width > 180 && box.height > 100,
        allowedKeys,
        annotationKeys: [...svg.querySelectorAll("[data-assembly-annotation-key]")]
          .map((node) => node.dataset.assemblyAnnotationKey),
        legendKeys: [...diagram.querySelectorAll(".tube-tool-library-diagram-legend [data-assembly-annotation-key]")]
          .map((node) => node.dataset.assemblyAnnotationKey),
        labels: [...svg.querySelectorAll(".tool-diagram-label")].map((node) => node.textContent),
      });
    }
    const wrap = templates.find((template) => template.id === "wrap-a-over-b");
    const pairCuts = (pairCount) => {
      const values = Object.fromEntries(wrap.parameters.map((parameter) => [parameter.key, parameter.defaultValue]));
      values.maleFemale = true;
      values.pairCount = pairCount;
      const host = document.createElement("div");
      host.innerHTML = renderToolParameterDiagramSvg(wrap, values, wrap.parameters, "", "assembly");
      return host.querySelectorAll(".tool-diagram-cut-line").length;
    };
    const contact = templates.find((template) => template.id === "t-contact-fit");
    const contactView = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [contact],
      tubeDesignerAssemblyLibrary: { selectedId: contact.id, workMode: "example",
        workModeUserSelected: true, showDiagram: true } };
    const contactKeys = () => {
      const host = document.createElement("div");
      host.innerHTML = assembly.renderAssemblyLibraryViewportOverlay({}, contactView);
      return [...host.querySelectorAll(".tool-parameter-svg [data-assembly-annotation-key]")]
        .map((node) => node.dataset.assemblyAnnotationKey);
    };
    const productKeysBefore = contactKeys();
    const ops = { renderProject() {} };
    await assembly.handleAssemblyLibraryAction({}, contactView, "tube-designer-assembly-diagram-mode",
      { dataset: { tubeAssemblyDiagramMode: "process" } }, ops);
    const processKeysBefore = contactKeys();
    await assembly.handleAssemblyLibraryAction({}, contactView, "tube-designer-assembly-parameter-change",
      { dataset: { tubeAssemblyId: contact.id, tubeAssemblyParameter: "markContact" }, checked: true }, ops);
    const processKeysAfter = contactKeys();
    await assembly.handleAssemblyLibraryAction({}, contactView, "tube-designer-assembly-diagram-mode",
      { dataset: { tubeAssemblyDiagramMode: "product" } }, ops);
    const productKeysAfter = contactKeys();
    return { results, pairCuts: [pairCuts("two"), pairCuts("four")],
      contact: { productKeysBefore, processKeysBefore, processKeysAfter, productKeysAfter } };
  }, templates);
  assert.deepEqual(errors, []);
  assert.ok(inspected.results.every((row) => row.a >= 1 && row.filled && row.visible),
    `every diagram must show a real filled part: ${JSON.stringify(inspected)}`);
  assert.ok(inspected.results.every((row) => [...row.annotationKeys, ...row.legendKeys]
    .every((key) => row.allowedKeys.includes(key))),
  `finished diagrams may only show product parameters in the SVG and legend: ${JSON.stringify(inspected)}`);
  assert.deepEqual(inspected.results.find((row) => row.id === "bend").annotationKeys, ["angle"]);
  assert.deepEqual(inspected.results.find((row) => row.id === "bend").legendKeys, ["angle"]);
  assert.deepEqual(inspected.results.find((row) => row.id === "wrap-a-over-b").annotationKeys, []);
  assert.deepEqual(inspected.results.find((row) => row.id === "wrap-a-over-b").legendKeys, []);
  assert.ok(inspected.results.filter((row) => row.mark >= 1).length >= 14,
    "process-specific seam or cut mark must be visible where a joint/cut is present");
  assert.deepEqual(inspected.results.find((row) => row.id === "two-end-end-angle").labels, ["A", "B"]);
  assert.ok(inspected.results.find((row) => row.id === "two-end-end-angle").b >= 1,
    "mitered L must show two separate finished components");
  assert.ok(inspected.results.find((row) => row.id === "bend").b === 0,
    "cold bending must show a single continuous component");
  assert.ok(inspected.results.find((row) => row.id === "wrap-a-over-b").mark >= 1,
    "surviving A-over-B template must show the joint");
  assert.ok(inspected.pairCuts[0] >= 1 && inspected.pairCuts[1] > inspected.pairCuts[0],
    "four-sided tongue option must add visible cut marks to the same template diagram");
  assert.deepEqual(inspected.contact, { productKeysBefore: ["intersectionAngle"],
    processKeysBefore: ["markContact"], processKeysAfter: ["markContact", "markMode"],
    productKeysAfter: ["intersectionAngle"] },
  "成品标注和贴合工艺标注必须分开，条件工艺参数按开关显示");
  if (process.env.ICAX_ASSEMBLY_DIAGRAM_SCREENSHOT) await page.screenshot({ path: process.env.ICAX_ASSEMBLY_DIAGRAM_SCREENSHOT, fullPage: true });
  console.log(`assembly finished diagrams Edge regression passed: ${inspected.results.length} product-only parameter sketches`);
} finally {
  await browser.close();
}
