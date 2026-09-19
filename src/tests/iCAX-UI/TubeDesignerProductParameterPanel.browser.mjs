import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { renderDesignerRightPane } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const raw = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/template.json",
  import.meta.url,
), "utf8"));
const display = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/display.json",
  import.meta.url,
), "utf8"));
const groups = raw.groups.map((group) => ({ ...group, displayName: catalogText(group.displayName) }));
const template = {
  ...raw,
  display,
  available: true,
  name: catalogText(raw.displayName),
  groups,
  parameters: raw.parameters.map((field) => ({
    ...field,
    readOnly: field.key.endsWith("ProfileType") ? false : field.readOnly,
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
const values = Object.fromEntries(raw.parameters.map((field) => [field.key, field.defaultValue]));
values.tubeSpecificationPreset = "custom";
values.accessDoorEnabled = true;
values.infillPattern = "grid";
const profileOverride = (role, width, depth = width, extraDefinitions = []) => ({
    profileForm: "parametric",
    profileScope: "system",
    profileDefinitionId: `qa-${role}-profile`,
    name: `QA ${role} 参数管型`,
    specification: `${width} × ${depth} × 1 mm`,
    parameters: {
      width, depth, cornerRadius: 2, wallThickness: 1,
      ...Object.fromEntries(extraDefinitions.map((item) => [item.key, item.defaultValue])),
    },
    parameterDefinitions: [
      { key: "width", displayName: "截面宽度", valueType: "number", defaultValue: width },
      { key: "depth", displayName: "截面深度", valueType: "number", defaultValue: depth },
      { key: "cornerRadius", displayName: "外圆角 R", valueType: "number", defaultValue: 2 },
      { key: "wallThickness", displayName: "壁厚", valueType: "number", defaultValue: 1 },
      ...extraDefinitions,
    ],
  });
values.tubeDesignerProfileOverrides = {
  frame: profileOverride("frame", 38),
  horizontal: profileOverride("horizontal", 22, 22, [
    { key: "note", displayName: "备注", valueType: "string", defaultValue: "横杆截面" },
  ]),
  vertical: profileOverride("vertical", 19),
  doorFrame: profileOverride("doorFrame", 25),
  doorLeafFrame: profileOverride("doorLeafFrame", 20),
  doorHorizontal: profileOverride("doorHorizontal", 20),
  doorVertical: profileOverride("doorVertical", 16),
};
const repositoryRoot = fileURLToPath(new URL("../../../", import.meta.url));
const generatedItems = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
import importlib.util
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
runtime = root / "src/iCAX-Engine/framework/TemplateRuntime/python"
package = root / "src/apps/tube-designer/templates/product/single_face_security_window"
sys.path.insert(0, str(runtime))
descriptor = json.loads((package / "template.json").read_text(encoding="utf-8"))
parameters = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
spec = importlib.util.spec_from_file_location("security_window_scene_linkage_browser", package / "template.py")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
document = module.generate(parameters, {"template": descriptor, "geometryPurpose": "display"})
print(json.dumps([{
    "stableKey": item["key"],
    "role": item.get("properties", {}).get("group", ""),
    "name": item.get("name", item["key"]),
} for item in document["items"]], ensure_ascii=False))
`, repositoryRoot], { encoding: "utf8", env: { ...process.env, PYTHONDONTWRITEBYTECODE: "1" } }));
const generatedMembers = generatedItems.map((item, index) => ({
  ...item,
  entityId: `generated-member-${index + 1}`,
}));
const product = { entityId: "window-1", templateId: raw.id, name: "防盗窗", quantity: 1, parameters: values };
const view = {
  activeAreaId: "view",
  scene: { tubeDesigner: {
    templates: [template], product, activeProductId: product.entityId,
    members: generatedMembers,
    joints: [], parts: [],
  } },
};

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 760 } });
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
  await page.setContent(`<style>*{box-sizing:border-box}body{margin:0;display:flex}.viewport-host{width:360px;height:360px}.cam-info-pane{width:520px;height:760px}${tubeDesignerCss}</style><div class="viewport-host"></div><aside class="cam-info-pane">${renderDesignerRightPane({}, view)}</aside>`);
  const result = await page.evaluate(async ({ state }) => {
    const { bindProductParameterDiagrams } = await import("/src/apps/tube-designer/webpage/productParameterDiagram.mjs");
    const { captureDesignerScrollState, restoreDesignerScrollState } = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const { renderDesignerRightPane: renderRightPane } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const pane = document.querySelector(".cam-info-pane");
    const completeTemplate = state.scene.tubeDesigner.templates[0];
    state.tubeDesignerTemplateDescriptors = { [completeTemplate.id]: completeTemplate };
    state.scene.tubeDesigner.templates = [{
      id: completeTemplate.id,
      version: completeTemplate.version,
      name: completeTemplate.name,
      available: true,
      extensions: { catalog: completeTemplate.extensions?.catalog ?? {} },
    }];
    const context = { mount: pane };
    const sectionTitles = [...pane.querySelectorAll(".tube-designer-parameter-section")]
      .map((node) => node.querySelector(":scope > summary > span")?.textContent?.trim())
      .filter(Boolean);
    const topCards = [
      pane.querySelector(".tube-designer-instance-quantity"),
      pane.querySelector(".tube-designer-scene-product-identity"),
      pane.querySelector(".tube-designer-structure-summary"),
      pane.querySelector(".tube-designer-parameter-sections"),
    ];
    const topCardGaps = topCards.slice(1).map((node, index) => Math.round(
      node.getBoundingClientRect().top - topCards[index].getBoundingClientRect().bottom,
    ));
    pane.querySelectorAll(".tube-designer-parameter-section,.tube-designer-parameter-subsection")
      .forEach((details) => { details.open = true; });
    const fixedFrameField = pane.querySelector('[data-tube-designer-parameter="doorFrameJoinType"]')
      .closest(".tube-designer-field");
    const leafFrameField = pane.querySelector('[data-tube-designer-parameter="doorLeafFrameJoinType"]')
      .closest(".tube-designer-field");
    const doorProcessGroup = fixedFrameField.closest("details");
    const mergedDoorProcess = {
      sameGroup: doorProcessGroup === leafFrameField.closest("details"),
      title: doorProcessGroup.querySelector(":scope > summary")?.textContent.trim(),
      directFields: !doorProcessGroup.querySelector(".tube-designer-parameter-subsection-list"),
      separateRows: leafFrameField.getBoundingClientRect().top >= fixedFrameField.getBoundingClientRect().bottom,
      fullRows: [fixedFrameField, leafFrameField].every((field) => field.classList.contains("is-line-full")),
      oldGroupCount: [...pane.querySelectorAll("details > summary")]
        .filter((node) => /^(固定窗框工艺|活动窗扇框工艺)/.test(node.textContent.trim())).length,
    };
    const diagram = pane.querySelector(".tube-designer-profile-diagram-disclosure");
    const diagramDefaultClosed = diagram ? !diagram.open : true;
    if (diagram) diagram.open = true;
    bindProductParameterDiagrams(pane);
    const longChoice = pane.querySelector('.tube-designer-field.is-line-full.has-control-size.is-choice select');
    for (let node = longChoice?.parentElement; node; node = node.parentElement) {
      if (node.tagName === "DETAILS") node.open = true;
    }
    const longField = longChoice?.closest(".tube-designer-field");
    const longGrid = longField?.parentElement;
    const wideChoice = Boolean(longField?.classList.contains("wide")
      && longField.getBoundingClientRect().width >= longGrid.getBoundingClientRect().width * 0.9);
    const productCodeField = pane.querySelector('[data-tube-designer-parameter="productCode"]')
      ?.closest(".tube-designer-field");
    const productCodeInput = productCodeField?.querySelector("input");
    const numberInput = pane.querySelector('[data-tube-designer-profile-parameter="width"]');
    const materialSection = [...pane.querySelectorAll(".tube-designer-parameter-section")]
      .find((node) => node.querySelector(":scope > summary > span")?.textContent?.trim() === "材料");
    const materialGrid = materialSection?.querySelector(".tube-designer-field-grid");
    const structureSummary = pane.querySelector(".tube-designer-structure-summary");
    const forbiddenKeys = [
      "faceType", "frameLayout", "width", "height",
      "doorClearWidth", "doorClearHeight", "doorGap", "doorHingeSide", "doorHingeCount",
      "doorHorizontalTopCenterOffset", "doorHorizontalBottomCenterOffset",
      "doorHorizontalMaximumCenterSpacing", "doorVerticalLeftCenterOffset",
      "doorVerticalRightCenterOffset", "doorVerticalMaximumCenterSpacing",
    ];
    const displayLayout = {
      defaultColumnCount: materialGrid
        ? getComputedStyle(materialGrid).gridTemplateColumns.split(" ").filter(Boolean).length
        : 0,
      productCodeFullLine: productCodeField?.classList.contains("is-line-full") ?? false,
      productCodeHasWidthRange: productCodeField?.classList.contains("has-control-size") ?? false,
      productCodeCardWidth: productCodeField?.getBoundingClientRect().width ?? 0,
      stringInputWidth: productCodeInput?.getBoundingClientRect().width ?? 0,
      numberInputWidth: numberInput?.getBoundingClientRect().width ?? 0,
      structureSummaryText: structureSummary?.textContent?.replace(/\s+/g, " ").trim() ?? "",
      structureSummaryControlCount: structureSummary?.querySelectorAll("input,select,textarea,button").length ?? -1,
      forbiddenControlCount: forbiddenKeys
        .filter((key) => pane.querySelector(`[data-tube-designer-parameter="${key}"]`)).length,
      removedParameterCount: ["installationMode", "projectRuleReference", "projectEscapeMinWidth", "projectEscapeMinHeight", "doorUse", "doorHingeSide", "doorHingeCount"]
        .filter((key) => pane.querySelector(`[data-tube-designer-parameter="${key}"],[data-tube-designer-parameter-summary="${key}"]`)).length,
    };
    const profileInput = pane.querySelector('[data-tube-designer-profile-parameter="note"]');
    profileInput.focus({ preventScroll: true });
    profileInput.setSelectionRange(1, 4, "backward");
    captureDesignerScrollState(context, state);
    state.scene.tubeDesigner.templates[0] = {
      ...state.scene.tubeDesigner.templates[0],
      ...state.tubeDesignerTemplateDescriptors[completeTemplate.id],
    };
    pane.innerHTML = renderRightPane({}, state);
    restoreDesignerScrollState(context, state);
    await new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)));
    const restoredDiagram = pane.querySelector(".tube-designer-profile-diagram-disclosure");
    const restoredInput = pane.querySelector('[data-tube-designer-profile-parameter="note"]');
    const outcome = {
      sectionTitles,
      topCardGaps,
      diagramDefaultClosed,
      diagramOpenAfterRefresh: restoredDiagram?.open,
      wideChoice,
      mergedDoorProcess,
      displayLayout,
      generatedMemberCount: state.scene.tubeDesigner.members.length,
      generatedStableKeys: state.scene.tubeDesigner.members.map((member) => member.stableKey),
      focusRestored: document.activeElement === restoredInput,
      selectionRestored: [restoredInput?.selectionStart, restoredInput?.selectionEnd, restoredInput?.selectionDirection],
    };
    return outcome;
  }, { state: view });
  const compactProfiles = await page.evaluate(async () => {
    const pane = document.querySelector(".cam-info-pane");
    pane.style.width = "320px";
    pane.querySelectorAll("details").forEach((details) => { details.open = true; });
    await new Promise(requestAnimationFrame);
    const roles = ["frame", "horizontal", "doorFrame", "doorLeafFrame", "doorHorizontal"];
    const pairs = roles.map((role) => {
      const control = (key) => pane.querySelector(`[data-tube-designer-profile-prefix="${role}"][data-tube-designer-profile-parameter="${key}"]`);
      const width = control("width").closest("label");
      const depth = control("depth").closest("label");
      const radius = control("cornerRadius").closest("label");
      const wall = control("wallThickness").closest("label");
      return {
        role,
        paired: Math.abs(width.getBoundingClientRect().top - depth.getBoundingClientRect().top) < 1
          && Math.abs(radius.getBoundingClientRect().top - wall.getBoundingClientRect().top) < 1,
        noOverflow: [width, depth, radius, wall].every((field) => field.scrollWidth <= field.clientWidth),
        labelGap: control("depth").getBoundingClientRect().left - depth.querySelector("span").getBoundingClientRect().right,
      };
    });
    pane.style.width = "260px";
    await new Promise(requestAnimationFrame);
    const width = pane.querySelector('[data-tube-designer-profile-prefix="doorLeafFrame"][data-tube-designer-profile-parameter="width"]').closest("label");
    const depth = pane.querySelector('[data-tube-designer-profile-prefix="doorLeafFrame"][data-tube-designer-profile-parameter="depth"]').closest("label");
    return { pairs, narrowSingleColumn: depth.getBoundingClientRect().top > width.getBoundingClientRect().top };
  });
  for (const pair of compactProfiles.pairs) {
    assert.equal(pair.paired, true, `${pair.role}: 默认窄侧栏的数值字段应两项一行`);
    assert.equal(pair.noOverflow, true, `${pair.role}: 紧凑字段不得溢出`);
    assert.ok(pair.labelGap >= 0 && pair.labelGap <= 6, `${pair.role}: 标签与输入框不应留大空隙`);
  }
  assert.equal(compactProfiles.narrowSingleColumn, true);
  const cornerLayout = await page.evaluate(async ({ state }) => {
    const { renderDesignerRightPane } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const pane = document.querySelector(".cam-info-pane");
    pane.style.width = "320px";
    state.scene.tubeDesigner.product.parameters.faceType = "two";
    pane.innerHTML = renderDesignerRightPane({}, state);
    pane.querySelectorAll("details").forEach((details) => { details.open = true; });
    const input = pane.querySelector('[data-tube-designer-parameter="frameCornerJoin"]');
    const field = input.closest(".tube-designer-field");
    return {
      fullLine: field.classList.contains("is-line-full"),
      width: field.getBoundingClientRect().width,
      gridWidth: field.parentElement.getBoundingClientRect().width,
      noOverflow: field.scrollWidth <= field.clientWidth,
    };
  }, { state: view });
  assert.equal(cornerLayout.fullLine, true, "外框转角连接必须独占一行");
  assert.ok(cornerLayout.width >= cornerLayout.gridWidth * 0.9);
  assert.equal(cornerLayout.noOverflow, true);
  assert.deepEqual(result.sectionTitles, ["材料", "工艺"],
    "防盗窗实例右侧的可编辑参数区只应显示材料和工艺");
  assert.ok(result.topCardGaps.every((gap) => gap >= 0 && gap <= 8),
    `右侧顶部卡片之间不应浪费纵向空间: ${result.topCardGaps.join(", ")}`);
  assert.equal(result.diagramDefaultClosed, true);
  assert.equal(result.diagramOpenAfterRefresh, true);
  assert.equal(result.wideChoice, true);
  assert.equal(result.mergedDoorProcess.sameGroup, true);
  assert.match(result.mergedDoorProcess.title, /^逃生窗\s*2 项$/);
  assert.equal(result.mergedDoorProcess.directFields, true);
  assert.equal(result.mergedDoorProcess.separateRows, true);
  assert.equal(result.mergedDoorProcess.fullRows, true);
  assert.equal(result.mergedDoorProcess.oldGroupCount, 0);
  assert.equal(result.displayLayout.defaultColumnCount, 2);
  assert.equal(result.displayLayout.productCodeFullLine, true);
  assert.equal(result.displayLayout.productCodeHasWidthRange, true);
  assert.ok(result.displayLayout.stringInputWidth > result.displayLayout.numberInputWidth);
  assert.ok(result.displayLayout.stringInputWidth < result.displayLayout.productCodeCardWidth * 0.8,
    "a full-line field must not stretch its input to the full row");
  assert.equal(result.displayLayout.structureSummaryControlCount, 0,
    "实例页结构摘要必须只读");
  assert.match(result.displayLayout.structureSummaryText, /结构摘要/);
  assert.match(result.displayLayout.structureSummaryText, /单面/);
  assert.match(result.displayLayout.structureSummaryText, /逃生窗/);
  assert.equal(result.displayLayout.forbiddenControlCount, 0,
    "结构和规格参数不得继续出现在右侧编辑区");
  assert.equal(result.displayLayout.removedParameterCount, 0);
  assert.ok(result.generatedMemberCount > 20);
  for (const prefix of [
    "outer_frame.", "main_grid.horizontal.", "main_grid.vertical.",
    "access_door.fixed_frame.", "access_door.leaf.frame.",
    "access_door.leaf.horizontal.", "access_door.leaf.vertical.",
  ]) assert.ok(result.generatedStableKeys.some((key) => key.startsWith(prefix)), `missing generated ${prefix}`);
  assert.equal(result.focusRestored, true);
  assert.deepEqual(result.selectionRestored, [1, 4, "backward"]);
  console.log("Product parameter panel browser regression passed: material/process-only editor, structure summary, responsive fields, focus and selection.");
} finally {
  await browser.close();
}
