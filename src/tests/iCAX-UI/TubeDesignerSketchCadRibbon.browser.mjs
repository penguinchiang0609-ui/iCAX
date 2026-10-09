// Real Chrome layout check with the production ribbon definition, SVG icons and
// product CSS. Geometry operations / native services are covered separately.
import assert from "node:assert/strict";
import { mkdirSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { sketchRibbonGroups } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
import { renderRibbonCommandIcon } from "../../iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
const output = resolve(fileURLToPath(new URL("../../../", import.meta.url)), "output/tests/sketch-cad-ribbon");
mkdirSync(output, { recursive: true });
const browser = await chromium.launch({ headless: true, channel: "chrome" });
const results = [];
try {
  const page = await browser.newPage();
  for (const width of [1280, 1920]) {
    await page.setViewportSize({ width, height: 900 });
    await page.setContent(`<!doctype html><meta charset="utf-8"><style>${tubeDesignerCss}body{margin:0;font-family:Segoe UI,Microsoft YaHei,sans-serif;color:#294b51}main{display:grid;grid-template-rows:auto 1fr;height:100vh;min-width:0}.fixture-body{display:grid;grid-template-columns:1fr 300px;min-height:0;background:#0d2730}aside{overflow:auto;padding:12px;background:#f5f9f9}</style><main><nav class="tube-section-sketch-toolbar">${sketchRibbonGroups.map(group => `<section><div>${group.commands.map(command => `<button data-command="${command.id}">${renderRibbonCommandIcon(command.iconName)}<span>${command.title}</span></button>`).join("")}</div><small>${group.title}</small></section>`).join("")}</nav><div class="fixture-body"><div></div><aside><div class="tube-sketch-cad-panel"><header><strong>精确 CAD 操作</strong><small>输入参数或在画布中指定位置。</small></header><div class="tube-sketch-cad-fields"><label>中心 X<input value="120.000" type="text"></label><label>中心 Y<input value="250.000" type="text"></label><label class="wide">字体<select><option>Microsoft YaHei</option></select></label><label class="wide">文字<textarea>精确文字轮廓</textarea></label></div><p class="tube-sketch-cad-error"></p><footer class="tube-sketch-cad-actions"><button>应用</button><button>取消</button></footer></div></aside></div></main>`);
    const dimensions = await page.evaluate(() => {
      const nav = document.querySelector("nav"), box = nav.getBoundingClientRect();
      const visible = element => {
        const r = element.getBoundingClientRect(), hit = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2);
        return r.x >= 0 && r.right <= innerWidth && r.y >= 0 && r.bottom <= box.bottom && hit?.closest("button") === element;
      };
      return { height: box.height, width: box.width, scrollWidth: nav.scrollWidth, commit: visible(document.querySelector('[data-command="sketch.commit"]')), cancel: visible(document.querySelector('[data-command="sketch.cancel"]')) };
    });
    assert.ok(dimensions.height <= 130, `Ribbon consumes ${dimensions.height}px at ${width}px`);
    assert.equal(dimensions.commit, true); assert.equal(dimensions.cancel, true);
    const clicked = [];
    await page.evaluate(() => { window.clicks = []; document.addEventListener("click", event => { const button = event.target.closest("[data-command]"); if (button) window.clicks.push(button.dataset.command); }); });
    for (const command of sketchRibbonGroups.flatMap(group => group.commands)) {
      const button = page.locator(`[data-command="${command.id}"]`);
      await button.scrollIntoViewIfNeeded();
      await button.click({ timeout: 2000 }); clicked.push(command.id);
    }
    assert.deepEqual(await page.evaluate(() => window.clicks), clicked);
    const fields = await page.locator("aside").evaluate(aside => [...aside.querySelectorAll("input,select,textarea,button")].map(element => {
      const r = element.getBoundingClientRect(), a = aside.getBoundingClientRect();
      return r.x >= a.x && r.right <= a.right && r.width > 0;
    }));
    assert.ok(fields.every(Boolean));
    await page.screenshot({ path: resolve(output, `ribbon-${width}.png`) });
    results.push({ viewportWidth: width, ...dimensions, clickedCommands: clicked.length, fieldsInsideSidebar: true });
  }
} finally { await browser.close(); }
writeFileSync(resolve(output, "report.json"), JSON.stringify(results, null, 2));
console.log(JSON.stringify(results));
