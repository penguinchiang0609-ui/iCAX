// Browser layout regression for parameter-driven product diagrams.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { renderProductParameterDiagram } from "../../apps/tube-designer/webpage/productParameterDiagram.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");

function loadTemplate(directory) {
  return JSON.parse(readFileSync(new URL(
    `../../apps/tube-designer/templates/product/${directory}/template.json`,
    import.meta.url,
  ), "utf8"));
}

function defaults(template, overrides = {}) {
  return {
    ...Object.fromEntries(template.parameters.map((field) => [field.key, field.defaultValue])),
    ...overrides,
  };
}

const security = loadTemplate("single_face_security_window");
const guardrail = loadTemplate("modular_guardrail_diamond-straight");
const cases = [
  ["防盗窗 · 单面", security, defaults(security, { faceType: "single" })],
  ["防盗窗 · 三面", security, defaults(security, { faceType: "three", leftWidth: 450, rightWidth: 800 })],
  ["防盗窗 · 五面", security, defaults(security, { faceType: "five", width: 1800, height: 1300, depth: 700 })],
  ["护栏 · 直式", guardrail, defaults(guardrail, { layout: "straight", sideBayCount1: 4 })],
  ["护栏 · 左转 L 型", guardrail, defaults(guardrail, { layout: "left_l", sideBayCount1: 3, sideBayCount2: 2 })],
  ["护栏 · U 型", guardrail, defaults(guardrail, { layout: "u", sideBayCount1: 3, sideBayCount2: 2, sideBayCount3: 2 })],
];
const cards = cases.map(([title, template, values]) => `<article><h2>${title}</h2>${renderProductParameterDiagram(template, values, { mode: "add" })}</article>`).join("");

const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 }, deviceScaleFactor: 1 });
  await page.setContent(`<style>*{box-sizing:border-box}body{margin:0;padding:20px;background:#e8eff0;font-family:Arial,"Microsoft YaHei",sans-serif}.qa-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.qa-grid article{min-width:0;padding:14px;background:#fff;border:1px solid #bfd0d4}.qa-grid h2{margin:0 0 10px;color:#294b54;font-size:15px}${tubeDesignerCss}</style><main class="qa-grid">${cards}</main>`);
  const result = await page.evaluate(() => ({
    diagrams: document.querySelectorAll(".tube-designer-product-structure-svg").length,
    labels: [...document.querySelectorAll(".tube-designer-product-structure-svg")].map((svg) => svg.getAttribute("aria-label")),
    sideFaceCounts: [...document.querySelectorAll(".tube-designer-product-structure-svg")].slice(0, 3)
      .map((svg) => svg.querySelectorAll(".product-diagram-face-side").length),
    overflow: [...document.querySelectorAll(".tube-designer-product-diagram")].filter((panel) => panel.scrollWidth > panel.clientWidth + 1).length,
    invalid: document.body.innerHTML.includes("NaN") || document.body.innerHTML.includes("undefined"),
  }));
  assert.equal(result.diagrams, 6);
  assert.deepEqual(result.sideFaceCounts, [0, 2, 2]);
  assert.ok(result.labels.includes("三面防盗窗结构与尺寸示意"));
  assert.ok(result.labels.includes("U 型护栏结构与尺寸示意"));
  assert.equal(result.overflow, 0);
  assert.equal(result.invalid, false);
  if (process.env.ICAX_PRODUCT_DIAGRAM_SCREENSHOT) {
    await page.screenshot({ path: process.env.ICAX_PRODUCT_DIAGRAM_SCREENSHOT, fullPage: true });
  }
  console.log("PASS product diagrams: single/three/five-face windows and straight/L/U guardrails render without clipping");
} finally {
  await browser.close();
}
