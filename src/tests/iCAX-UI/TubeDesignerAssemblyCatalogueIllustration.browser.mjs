import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
import { normalizeAssemblyCatalogue } from "../../apps/tube-designer/webpage/assemblyCatalog.mjs";
import { renderAssemblyCatalogueIllustration } from "../../apps/tube-designer/webpage/assemblyCatalogueIllustration.mjs";

const source = fileURLToPath(new URL("../../", import.meta.url));
const root = new URL("../../apps/tube-designer/templates/assembly/", import.meta.url);
const templates = normalizeAssemblyCatalogue(readdirSync(root, { withFileTypes: true })
  .filter((entry) => entry.isDirectory())
  .map((entry) => {
    try { return JSON.parse(readFileSync(new URL(`${entry.name}/assembly.json`, root), "utf8")); }
    catch { return null; }
  }));

assert.ok(templates.length >= 15);
for (const template of templates) {
  assert.ok(template.catalogueDiagram?.paths?.length >= 2,
    `${template.id} must declare its own finished-joint thumbnail`);
  assert.match(renderAssemblyCatalogueIllustration(template), /<svg viewBox="0 0 64 64"/);
}
const miter = templates.find((item) => item.id === "two-end-end-angle");
const arbitrary = { ...miter, id: "new-process-from-file" };
assert.equal(renderAssemblyCatalogueIllustration(arbitrary), renderAssemblyCatalogueIllustration(miter),
  "thumbnail geometry must come from template data, not an ID switch");
assert.match(renderAssemblyCatalogueIllustration({ catalogueDiagram: { paths: [{ d: 'M1 2" onload="evil()', tone: "a" }] } }),
  /&quot; onload=&quot;/, "template SVG attributes must be escaped");

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://assembly-icons.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://assembly-icons.test/");
  await page.addStyleTag({ content: tubeDesignerCss +
    "body{margin:0;width:390px;height:800px}.tube-profile-library-panel{height:100%;display:flex;flex-direction:column}" });
  const result = await page.evaluate(async (catalogue) => {
    const { renderAssemblyLibraryLeftPane } = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const view = { tubeDesignerAssemblyTemplates: catalogue,
      tubeDesignerAssemblyLibrary: { selectedId: "two-end-end-angle", workMode: "example" } };
    document.body.innerHTML = renderAssemblyLibraryLeftPane({}, view);
    const cards = [...document.querySelectorAll(".tube-connection-library-card")];
    const icons = cards.map((card) => ({
      id: card.dataset.tubeAssemblyId,
      viewBox: card.querySelector("svg")?.getAttribute("viewBox"),
      pathCount: card.querySelectorAll("svg path").length,
      width: card.querySelector(".tube-connection-library-card-art")?.getBoundingClientRect().width,
    }));
    const getPath = (id) => document.querySelector(`[data-tube-assembly-id="${id}"] svg`)?.innerHTML;
    const miterSvg = document.querySelector('[data-tube-assembly-id="two-end-end-angle"] svg');
    const miterParts = [...miterSvg.querySelectorAll("path")];
    return { icons, miter: getPath("two-end-end-angle"), t: getPath("t-contact-fit"),
      cross: getPath("cross-through"),
      miterOuterCornerFilled: miterParts.slice(0, 2).some((path) =>
        path.isPointInFill(new DOMPoint(44, 55))),
      miterJoint: miterParts[2]?.getAttribute("d") };
  }, templates);
  assert.equal(result.icons.length, templates.length);
  assert.ok(result.icons.every((icon) => icon.viewBox === "0 0 64 64" && icon.pathCount >= 2 && icon.width >= 48));
  assert.notEqual(result.miter, result.t, "L end-to-end and T end-to-middle must read differently");
  assert.notEqual(result.t, result.cross, "T and cross must read differently");
  assert.equal(result.miterOuterCornerFilled, true, "miter thumbnail must keep the L outer corner filled");
  assert.equal(result.miterJoint, "M33 45 L45 57", "miter thumbnail must show the diagonal inside the corner");
  assert.deepEqual(miter.catalogueDiagram.paths.map((path) => path.d),
    miter.parameterDiagram.paths.map((path) => path.d), "catalogue and parameter diagrams must agree on the miter silhouette");
  assert.deepEqual(errors, []);
  if (process.env.ICAX_ICON_SCREENSHOT) await page.screenshot({ path: process.env.ICAX_ICON_SCREENSHOT });
  console.log(`assembly catalogue illustrations passed: ${result.icons.length} template-defined thumbnails in Edge`);
} finally {
  await browser.close();
}
