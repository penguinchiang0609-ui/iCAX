import { chromium } from "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs";
import { spawn } from "node:child_process";

const endpoint = process.argv[2] ?? "http://127.0.0.1:9248";
const exportDirectory = process.argv[3];
if (!exportDirectory) throw new Error("export directory is required");

const browser = await chromium.connectOverCDP(endpoint);
const context = browser.contexts()[0];
const page = context.pages()[0];
await page.waitForFunction(() => window.__icaxLaser3DCAM?.getTubeDesignerState, null, { timeout: 60000 });

await page.evaluate(() => {
  const stage = document.createElement("div");
  stage.id = "tube-designer-demo-stage";
  stage.style.cssText = "position:fixed;right:22px;top:158px;z-index:2147483646;padding:8px 14px;border-radius:7px;background:rgba(13,45,56,.9);color:#fff;font:600 15px/1.2 'Microsoft YaHei',sans-serif;box-shadow:0 4px 16px rgba(0,0,0,.28);pointer-events:none;transition:opacity .2s";
  document.body.append(stage);
  const pointer = document.createElement("div");
  pointer.id = "tube-designer-demo-pointer";
  pointer.style.cssText = "position:fixed;z-index:2147483647;width:18px;height:18px;margin:-9px;border:3px solid #ffb21c;border-radius:50%;background:rgba(255,178,28,.25);box-shadow:0 0 0 3px rgba(255,255,255,.8);pointer-events:none;transition:left .28s ease,top .28s ease,opacity .15s;opacity:0";
  document.body.append(pointer);
});

const pause = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
async function stage(text, milliseconds = 1100) {
  await page.evaluate((value) => { document.querySelector("#tube-designer-demo-stage").textContent = value; }, text);
  await pause(milliseconds);
}
async function pointAt(selector) {
  await page.locator(selector).first().waitFor({ state: "visible", timeout: 60000 });
  await page.evaluate((value) => {
    const target = document.querySelector(value);
    const pointer = document.querySelector("#tube-designer-demo-pointer");
    const rect = target.getBoundingClientRect();
    pointer.style.left = `${rect.left + rect.width / 2}px`;
    pointer.style.top = `${rect.top + rect.height / 2}px`;
    pointer.style.opacity = "1";
  }, selector);
  await pause(350);
}
async function click(selector) {
  await pointAt(selector);
  await page.locator(selector).first().click();
}
async function setParameter(key, value) {
  const selector = `[data-tube-designer-add-form] [data-tube-designer-parameter='${key}']`;
  await page.locator(selector).waitFor({ state: "attached", timeout: 60000 });
  await page.evaluate((parameterKey) => {
    const element = document.querySelector(`[data-tube-designer-add-form] [data-tube-designer-parameter='${parameterKey}']`);
    for (let parent = element?.parentElement; parent; parent = parent.parentElement) {
      if (parent.tagName === "DETAILS") parent.open = true;
    }
    element?.scrollIntoView({ block: "center" });
  }, key);
  await pause(300);
  const field = page.locator(selector);
  const type = await field.evaluate((element) => element.tagName === "SELECT" ? "select" : element.type);
  if (type === "select") await field.selectOption(String(value));
  else if (type === "checkbox") await field.setChecked(Boolean(value));
  else {
    await field.fill(String(value));
    await field.evaluate((element) => element.dispatchEvent(new Event("change", { bubbles: true })));
  }
  await pause(450);
}
async function waitForOperationToFinish() {
  await page.waitForFunction(() => !document.querySelector("[data-tube-designer-operation-wait]"), null, { timeout: 120000 });
  await page.waitForFunction(() => window.__icaxLaser3DCAM.getTubeDesignerState()?.pending !== true, null, { timeout: 120000 });
}
async function addTemplate(templateId, configure = async () => {}) {
  await click("[data-cam-action='tube-designer-open-add']");
  await page.locator(".tube-designer-config-dialog").waitFor({ state: "visible" });
  await stage("选择产品模板");
  const card = `.tube-designer-template-card[data-tube-designer-template-id='${templateId}']`;
  await click(card);
  await page.waitForFunction((id) => document.querySelector(`.tube-designer-template-card.selected[data-tube-designer-template-id='${id}']`), templateId);
  await configure();
  await stage("确认并生成三维预览", 700);
  await click("[data-cam-action='tube-designer-confirm-add']");
  await waitForOperationToFinish();
  await page.locator(".tube-designer-config-dialog").waitFor({ state: "detached", timeout: 120000 });
  await stage("实例已生成", 1300);
}

await stage("TubeDesigner · 防盗窗设计", 1400);
await addTemplate("single-face-security-window", async () => {
  await stage("单面防盗窗 · 设置外形和逃生窗", 750);
  await setParameter("width", 1500);
  await setParameter("height", 1800);
  await setParameter("frameLayout", "four_sides");
  await setParameter("accessDoorEnabled", true);
  await setParameter("doorWidth", 500);
  await setParameter("doorHeight", 650);
});

await addTemplate("two-face-security-window", async () => {
  await stage("两面防盗窗 · 右前组合", 750);
  await setParameter("frontWidth", 1400);
  await setParameter("sideWidth", 700);
  await setParameter("sidePosition", "right");
});

await addTemplate("five-face-security-window", async () => {
  await stage("五面防盗窗 · 左前右＋上下", 800);
  await setParameter("frontWidth", 1300);
  await setParameter("depth", 650);
});

await stage("左侧实例列表 · 点击即可切换场景", 900);
const instanceButtons = page.locator("[data-cam-action='tube-designer-select-instance']");
await pointAt("[data-cam-action='tube-designer-select-instance']");
await instanceButtons.nth(0).click();
await page.waitForFunction(() => window.__icaxLaser3DCAM.getTubeDesignerState()?.product?.templateId === "single-face-security-window");
await pause(1000);
await instanceButtons.nth(1).click();
await page.waitForFunction(() => window.__icaxLaser3DCAM.getTubeDesignerState()?.product?.templateId === "two-face-security-window");
await pause(1000);

await stage("导出加工 · 选择需要拆单的实例", 800);
await click("[data-cam-action='tube-designer-open-disassemble']");
await page.locator(".tube-designer-selection-dialog").waitFor({ state: "visible" });
await pause(1000);
await click("[data-cam-action='tube-designer-confirm-disassemble']");
await waitForOperationToFinish();
await page.locator(".tube-designer-breakdown-dialog").waitFor({ state: "visible", timeout: 120000 });
await stage("产品 → 零件种类 → 具体零件", 1300);

async function inspectCategory(index, caption) {
  const toggles = page.locator("[data-cam-action='tube-designer-toggle-category-tree']");
  await toggles.nth(index).scrollIntoViewIfNeeded();
  await toggles.nth(index).click();
  await page.waitForFunction((categoryIndex) => {
    const categories = [...document.querySelectorAll("[data-tube-designer-category-row]")];
    const row = categories[categoryIndex];
    if (!row) return false;
    const next = row.nextElementSibling;
    return next?.matches("[data-tube-designer-part-row]");
  }, index);
  await stage(caption, 700);
  const categoryRows = page.locator("[data-tube-designer-category-row]");
  const categoryId = await categoryRows.nth(index).getAttribute("data-tube-designer-category-id");
  const link = page.locator(`[data-tube-designer-part-row][data-tube-designer-category-id='${categoryId}'] [data-cam-action='tube-designer-open-part-inspection']`).first();
  if (!(await link.count())) {
    const fallback = page.locator("[data-tube-designer-part-row] [data-cam-action='tube-designer-open-part-inspection']").first();
    await fallback.click();
  } else {
    await link.click();
  }
  await page.waitForFunction(() => document.querySelector("[data-tube-designer-part-inspection-viewport]")?.dataset?.tubeDesignerInspectionReady === "true", null, { timeout: 120000 });
  await stage("零件三维复尺 · 自动尺寸与孔位", 1700);
  const canvas = page.locator("[data-tube-designer-part-inspection-viewport] canvas").first();
  if (await canvas.count()) {
    const box = await canvas.boundingBox();
    if (box) {
      await page.mouse.move(box.x + box.width * .55, box.y + box.height * .55);
      await page.mouse.down({ button: "right" });
      await page.mouse.move(box.x + box.width * .67, box.y + box.height * .45, { steps: 12 });
      await page.mouse.up({ button: "right" });
      await pause(800);
    }
  }
  await click("[data-cam-action='tube-designer-close-part-inspection']");
  await page.locator(".tube-designer-breakdown-dialog").waitFor({ state: "visible" });
  await pause(500);
}

await inspectCategory(0, "打开外框零件复尺");
await inspectCategory(1, "打开内部杆件复尺");

await stage("选择目录并导出 STEP＋Excel", 700);
await page.evaluate((directory) => window.__icaxLaser3DCAM.executeAreaAction(
  "tube-designer-export-selected",
  { dataset: { tubeDesignerExportDirectory: directory } },
), exportDirectory);
await page.waitForFunction(() => !document.querySelector(".tube-designer-export-wait"), null, { timeout: 180000 });
await stage("导出完成", 1200);
await page.evaluate(() => {
  document.querySelector("#tube-designer-demo-pointer")?.remove();
  document.querySelector("#tube-designer-demo-stage")?.remove();
});

spawn("explorer.exe", [exportDirectory], { detached: true, stdio: "ignore" }).unref();
await pause(3000);
await browser.close();
console.log(JSON.stringify({ ok: true, exportDirectory }));
