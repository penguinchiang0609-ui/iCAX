import assert from "node:assert/strict";
import { renderProductTemplateLibraryRightPane } from "../../apps/tube-designer/webpage/templateLibrary.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const parameters = [
  { key: "width", displayName: "目标通行净宽", valueType: "number", defaultValue: 800 },
  { key: "height", displayName: "目标通行净高", valueType: "number", defaultValue: 1000 },
  { key: "connection", displayName: "横杆与外框连接", valueType: "enum", defaultValue: "insert", choices: [
    { value: "insert", displayName: "插入定位后焊接并预留安装间隙" }] },
  { key: "notes", displayName: "备注", valueType: "string", defaultValue: "长文本内容保留整行" },
  ...Array.from({ length: 20 }, (_, i) => ({ key: `p${i}`, displayName: `尺寸${i}`, valueType: "number", defaultValue: 10 })),
];
const view = { scene: { tubeDesigner: { templates: [{ id: "test", name: "参数布局测试", available: true, parameters }] } },
  tubeDesignerProductTemplateLibrary: { scope: "system", selectedId: "test" } };
const html = renderProductTemplateLibraryRightPane({}, view);
assert.match(html, /tube-product-template-library-field is-wide/);
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage();
  await page.setContent(`<style>${tubeDesignerCss} #host{width:300px;height:400px} .tube-product-template-library-editor{display:flex;flex-direction:column}</style><div id="host">${html}</div>`);
  const result = await page.evaluate(async () => {
    const host = document.querySelector("#host");
    const input = key => document.querySelector(`[data-tube-template-library-parameter="${key}"]`);
    const box = key => input(key).closest("label").getBoundingClientRect();
    const a = box("width"), b = box("height"), long = box("connection"), notes = box("notes");
    const paired = Math.abs(a.top-b.top) < 1 && b.left > a.left;
    const wide = long.width > a.width*1.8 && notes.width > a.width*1.8;
    const control = input("notes"), body = document.querySelector(".tube-product-template-library-editor-body");
    control.focus({ preventScroll: true }); control.setSelectionRange(2,4); body.scrollTop = 80;
    const scroll = body.scrollTop;
    host.style.width = "420px";
    await new Promise(requestAnimationFrame);
    const stable = document.activeElement === control && control.selectionStart === 2 && body.scrollTop === scroll;
    host.style.width = "220px";
    await new Promise(requestAnimationFrame);
    const narrow = box("height").top > box("width").top && Math.abs(box("height").left-box("width").left) < 1;
    const noOverflow = body.scrollWidth <= body.clientWidth;
    return { paired, wide, stable, narrow, noOverflow };
  });
  assert.deepEqual(result, { paired: true, wide: true, stable: true, narrow: true, noOverflow: true });
  console.log("Product parameters: two columns, full-row long fields, narrow fallback, focus/selection/scroll preserved.");
} finally { await browser.close(); }
