import assert from "node:assert/strict";
import { renderProductTemplateLibraryRightPane } from "../../apps/tube-designer/webpage/templateLibrary.mjs";
import { renderDesignerAddParameterContent } from "../../apps/tube-designer/webpage/designerViews.mjs";
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

  const structuredHtml = renderProductTemplateLibraryRightPane({}, {
    scene: { tubeDesigner: { templates: [{
      id: "structured", name: "结构化参数", available: true,
      groups: [
        { key: "dimensions", displayName: "基本尺寸" },
        { key: "profiles", displayName: "管材规格" },
        { key: "assembly", displayName: "装配方式" },
      ],
      parameters: [
        { key: "w", groupKey: "dimensions", displayName: "宽度", valueType: "number", defaultValue: 1000 },
        { key: "profile", groupKey: "profiles", displayName: "管材", valueType: "string", defaultValue: "38×38" },
        { key: "joint", groupKey: "assembly", displayName: "连接", valueType: "string", defaultValue: "焊接" },
      ],
      extensions: { parameterLayout: { sections: [
        { key: "product", displayName: "产品规格", defaultOpen: true, groups: ["dimensions"] },
        { key: "materials", displayName: "管材与材料", groups: ["profiles"] },
        { key: "process", displayName: "加工与装配工艺", groups: ["assembly"] },
      ] } },
    }] } },
    tubeDesignerProductTemplateLibrary: { scope: "system", selectedId: "structured" },
  });
  await page.setContent(`<style>${tubeDesignerCss} #host{width:340px;height:520px} .tube-product-template-library-editor{display:flex;flex-direction:column}</style><div id="host">${structuredHtml}</div>`);
  const disclosureLayout = await page.evaluate(() => {
    const sections = [...document.querySelectorAll(".tube-product-template-library-parameter-section")];
    const groups = [...document.querySelectorAll(".tube-product-template-library-parameter-group")];
    const body = document.querySelector(".tube-product-template-library-editor-body");
    return {
      sectionCount: sections.length,
      openSections: sections.filter((item) => item.open).map((item) => item.dataset.tubeTemplateLibraryDisclosure),
      openGroups: groups.filter((item) => item.open).map((item) => item.dataset.tubeTemplateLibraryDisclosure),
      nested: sections.every((section) => section.querySelector(":scope > .tube-product-template-library-parameter-section-content > .tube-product-template-library-parameter-group")),
      noOverflow: body.scrollWidth <= body.clientWidth,
    };
  });
  assert.deepEqual(disclosureLayout, {
    sectionCount: 3,
    openSections: ["section:dimensions"],
    openGroups: ["group:product-kind:dimension:dimensions"],
    nested: true,
    noOverflow: true,
  });

  const scopedPresetTemplate = {
    id: "scoped-presets", name: "分区常用方案", available: true,
    groups: [
      { key: "size", displayName: "基本尺寸" },
      { key: "profile", displayName: "管材规格" },
      { key: "assembly", displayName: "装配方式" },
    ],
    parameters: [
      { key: "width", groupKey: "size", group: "基本尺寸", displayName: "宽度", type: "number", defaultValue: 1000 },
      { key: "profileSize", groupKey: "profile", group: "管材规格", displayName: "规格", type: "number", defaultValue: 38 },
      { key: "joinType", groupKey: "assembly", group: "装配方式", displayName: "连接", type: "text", defaultValue: "焊接" },
    ],
    extensions: { parameterLayout: { sections: [
      { key: "product", displayName: "产品规格", defaultOpen: true, groups: ["size"] },
      { key: "materials", displayName: "管材与材料", allowPresets: true, groups: ["profile"] },
      { key: "process", displayName: "加工与装配工艺", allowPresets: true, groups: ["assembly"] },
    ] } },
  };
  const addHtml = renderDesignerAddParameterContent({ templates: [scopedPresetTemplate] }, {
    tubeDesignerAddTemplateId: scopedPresetTemplate.id,
    tubeDesignerAddInstanceName: "测试产品",
    tubeDesignerAddDraft: { width: 1000, profileSize: 38, joinType: "焊接" },
  });
  await page.setContent(`<style>${tubeDesignerCss}</style><div class="tube-designer-config-parameters">${addHtml}</div>`);
  const presetLayout = await page.evaluate(() => {
    const material = document.querySelector('[data-tube-designer-parameter-group="section:materials"]');
    const process = document.querySelector('[data-tube-designer-parameter-group="section:process"]');
    return {
      bars: document.querySelectorAll(".tube-designer-user-preset-bar").length,
      materialScope: material?.querySelector(".tube-designer-user-preset-bar")?.dataset.tubeDesignerPresetScope,
      processScope: process?.querySelector(".tube-designer-user-preset-bar")?.dataset.tubeDesignerPresetScope,
      productHasPreset: !!document.querySelector('[data-tube-designer-parameter-group="section:structure"] .tube-designer-user-preset-bar, [data-tube-designer-parameter-group="section:dimensions"] .tube-designer-user-preset-bar'),
    };
  });
  assert.deepEqual(presetLayout, {
    bars: 2,
    materialScope: "materials",
    processScope: "process",
    productHasPreset: false,
  });
  console.log("Product parameters: two columns, full-row long fields, narrow fallback, focus/selection/scroll preserved.");
} finally { await browser.close(); }
