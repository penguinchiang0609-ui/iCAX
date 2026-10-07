// Real production metadata and Edge: product options stay inside the product boundary.
import assert from "node:assert/strict";
import { mkdirSync } from "node:fs";
import { resolve } from "node:path";
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const { getRibbonDefinition } = await importBrowserAsset('apps/tube-designer/webpage/ribbonDefinition.mjs');

const output = browserReportDirectory('product-connection-choices');
mkdirSync(output, { recursive: true });
const read = (path) => JSON.parse(readBrowserAsset(path));
const raw = { ...read("apps/tube-designer/templates/product/single_face_security_window/template.json"),
  display: read("apps/tube-designer/templates/product/single_face_security_window/display.json") };
const tools = ["v-notch-sharp", "edge-arc-groove"].map((id) => ({
  ...read(`apps/tube-designer/templates/mold/${id}/tool.json`), libraryScope: "system",
}));
const ribbon = getRibbonDefinition();
assert.equal(ribbon.tabs.find((tab) => tab.id === "view").groups.flatMap((group) => group.commands)
  .some((command) => command.id === "designer.open-assembly-process"), false);
assert.equal(ribbon.tabs.find((tab) => tab.id === "resources").groups.flatMap((group) => group.commands)
  .some((command) => command.id === "resources.assemblies"), true);
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1350, height: 800 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://product-boundary.test/**", serveBrowserAsset);
  await page.goto("http://product-boundary.test/");
  await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0}main{display:flex;gap:10px}.cam-context-pane,.cam-info-pane{height:230px;overflow:auto;width:480px}.nested-left{height:130px;overflow:auto}.nested-left>div{height:1400px}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1400px}.tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:1400px}.cam-viewport{width:250px}.tube-designer-product-workspace{display:contents}` });
  const result = await page.evaluate(async ({ raw, tools }) => {
    const views = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const actions = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const { catalogText } = await import("/src/apps/tube-designer/webpage/productCatalog.mjs");
    const bindings = await import("/src/apps/tube-designer/webpage/productResourceBindings.mjs");
    const controls = await import("/src/apps/tube-designer/webpage/productControls.mjs");
    const { patchLibraryDom, rememberLibraryDom } = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    // Normalize catalogue fields exactly as the host catalogue does. Do not replace
    // any product option, role policy, visibility condition or group declaration.
    const template = { ...raw, available: true, descriptorLoaded: true,
      name: catalogText(raw.displayName), groups: raw.groups.map((group) => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: raw.parameters.map((field) => ({ ...field,
        type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
        displayName: catalogText(field.displayName), groupKey: field.group,
        options: field.choices?.map((choice) => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const parameters = { ...Object.fromEntries(template.parameters.map((field) => [field.key, field.defaultValue])),
      frameManufacturingMode: "plane_v_notch", accessDoorEnabled: true,
      doorFrameJoinType: "v_groove_90:tool_library", doorLeafFrameJoinType: "v_groove_90:tool_library",
      tubeDesignerToolBindings: {} };
    for (const field of template.parameters.filter(bindings.isProductToolField)) {
      if (field.presentation.editor !== "product-option") throw new Error("production field still opens a resource editor");
      const binding = bindings.makeProductToolBinding(field, tools[0], null, template);
      binding.parameters = { ...binding.parameters, angle: 45 };
      parameters[field.key] = binding.selectionKey;
      parameters.tubeDesignerToolBindings[bindings.productToolRole(field)] = binding;
    }
    const unknown = parameters.tubeDesignerToolBindings.doorLeafFrameGroove;
    unknown.selectionKey = "user:private-legacy-corner";
    unknown.ref = { scope: "user", id: "private-legacy-corner" };
    unknown.snapshot.displayName = "PRIVATE LEGACY RESOURCE NAME";
    parameters.doorLeafFrameGrooveTool = unknown.selectionKey;
    const savedUnknown = JSON.stringify(unknown);
    const savedOuter = JSON.stringify(parameters.tubeDesignerToolBindings.outerFrameGroove);
    const savedDoor = JSON.stringify(parameters.tubeDesignerToolBindings.doorFrameGroove);
    const outerDefinition = template.extensions.productControls.find(control => control.key === "outerFrameConnection");
    const staleSelector = structuredClone(parameters);
    staleSelector.outerFrameGrooveTool = "system:edge-arc-groove";
    if (controls.productControlValue(template, outerDefinition, staleSelector) !== "v-groove"
        || controls.applyProductControlChoice({}, template, outerDefinition, staleSelector, "v-groove") !== staleSelector) throw new Error("saved binding lost precedence over an older selector draft");
    document.body.innerHTML = '<main><div class="cam-workbench"><aside class="cam-context-pane"></aside><div class="cam-viewport"><canvas></canvas><button class="cube">视角</button></div><aside class="cam-info-pane"></aside></div></main>';
    const mount = document.querySelector("main"), canvas = mount.querySelector("canvas"), cube = mount.querySelector(".cube");
    const product = { entityId: "production-window", templateId: template.id, name: "防盗窗", quantity: 1, parameters };
    const manufacturingOnly = new Set(template.extensions.parameterDependencies.manufacturingOnly);
    const modelValues = values => Object.fromEntries(Object.entries(values).filter(([key]) => !manufacturingOnly.has(key))
      .sort(([first], [second]) => first.localeCompare(second)));
    const displayedValues = JSON.stringify(modelValues(parameters));
    const view = { activeAreaId: "view", tubeDesignerSystemPunchTools: tools,
      scene: { tubeDesigner: { templates: [template], product, activeProductId: product.entityId, members: [], parts: [], joints: [] } } };
    let release, requested, commits = 0, patches = 0;
    const context = { mount, project: { projectId: "product-boundary-browser" },
      sceneProxy: { invoke(method, payload) {
        if (method !== "TubeDesigner.UpdateProductParameters") throw new Error(method);
        commits++; requested = structuredClone(payload);
        return new Promise((resolve) => { release = () => resolve({ tubeDesigner: {
          ...view.scene.tubeDesigner, product: { ...view.scene.tubeDesigner.product, parameters: structuredClone(payload.parameters),
            modelOutdated: JSON.stringify(modelValues(payload.parameters)) !== displayedValues, partsOutdated: true },
        } }); });
      } }, actions: {} };
    const leftHTML = () => `<div class="nested-left"><div><input id="ongoing-note" value="unsaved text">${views.renderDesignerLeftPane(context, view)}</div></div>`;
    const render = () => {
      const restore = capturePaneInteraction(mount);
      actions.captureDesignerScrollState(context, view);
      if (!patchLibraryDom(view, mount, { left: leftHTML(), right: views.renderDesignerRightPane(context, view),
        overlay: views.renderDesignerViewportOverlay(context, view), suffix: "" })) throw new Error("product refresh did not patch existing nodes");
      patches++;
      actions.restoreDesignerScrollState(context, view, { deferred: false });
      restore();
    };
    mount.querySelector(".cam-context-pane").innerHTML = leftHTML();
    mount.querySelector(".cam-info-pane").innerHTML = views.renderDesignerRightPane(context, view);
    rememberLibraryDom(view, mount, "");
    context.actions.refreshActiveSceneState = async () => render();
    const ops = { renderProject: render };
    const option = (key) => mount.querySelector(`[data-product-control-editor="${key}"] select`);
    const ordinary = (key) => mount.querySelector(`[data-tube-designer-parameter="${key}"]`);
    function assertConnectionLayout() {
      const section = mount.querySelector('[data-tube-designer-parameter-group="section:process"]');
      if (!section || section.querySelector(':scope > summary > span')?.textContent !== "装配") throw new Error("assembly section title was removed");
      const preset = section.querySelector('.tube-designer-user-preset-bar[data-tube-designer-preset-scope="process"]');
      if (!preset || preset.querySelector('label > span')?.textContent !== "常用连接方案") throw new Error("connection preset was removed");
      const subgroups = [...section.querySelectorAll('[data-tube-designer-parameter-group], .tube-designer-parameter-subsection')];
      if (subgroups.length) throw new Error("connections retain an intermediate parameter subgroup: " + JSON.stringify(subgroups.map(node => ({
        key: node.dataset.tubeDesignerParameterGroup, title: node.querySelector(':scope > summary > span')?.textContent,
      }))));
      const editors = [...section.querySelectorAll('[data-product-control-editor]')];
      for (const key of ["outerFrameConnection", "doorFrameConnection", "doorLeafFrameConnection"]) {
        if (!editors.some(node => node.dataset.productControlEditor === key)) throw new Error("missing connection row " + key);
      }
      const rows = editors.map(node => {
        const parentGroup = node.closest('[data-tube-designer-parameter-group]');
        if (parentGroup !== section) throw new Error("connection row is wrapped in a subgroup");
        const grid = node.closest('.tube-designer-field-grid');
        if (!grid) throw new Error("connection row has no field grid");
        const rect = node.getBoundingClientRect(), gridRect = grid.getBoundingClientRect();
        const style = getComputedStyle(grid);
        const inset = Number.parseFloat(style.paddingLeft) + Number.parseFloat(style.paddingRight)
          + Number.parseFloat(style.borderLeftWidth) + Number.parseFloat(style.borderRightWidth);
        const select = node.querySelector('select').getBoundingClientRect();
        if (rect.width <= 0 || rect.height <= 0 || Math.abs(rect.width - (gridRect.width - inset)) > 1) throw new Error("connection row does not occupy the whole grid width: " + node.dataset.productControlEditor);
        return { key: node.dataset.productControlEditor, left: rect.left, width: rect.width, top: rect.top, bottom: rect.bottom,
          selectLeft: select.left, selectWidth: select.width };
      });
      for (let index = 1; index < rows.length; index++) {
        const row = rows[index], first = rows[0], previous = rows[index - 1];
        if (Math.abs(row.left - first.left) > 1 || Math.abs(row.width - first.width) > 1
            || Math.abs(row.selectLeft - first.selectLeft) > 1 || Math.abs(row.selectWidth - first.selectWidth) > 1
            || row.top < previous.bottom - 1) throw new Error("connection choices are not equal-width single rows: " + JSON.stringify(rows));
      }
      return rows;
    }
    function assertBoundary() {
      const right = mount.querySelector(".cam-info-pane");
      if (right.textContent.includes("结构摘要")) throw new Error("product repeats its visible structure in a summary card");
      if (right.querySelector('[data-tube-designer-parameter-group$="group:folded_post_profile"]')
          || right.textContent.includes("外框竖边管材")) throw new Error("outer-frame material is split into horizontal and vertical controls");
      for (const key of ["outerFramePostMaterial", "foldedPostProfileType", "foldedPostWidth", "foldedPostDepth", "foldedPostCornerRadius", "foldedPostWallThickness"]) {
        if (template.parameters.some(field => field.key === key) || ordinary(key)) throw new Error("removed independent outer-frame material parameter remains: " + key);
      }
      if (right.querySelectorAll('[data-tube-designer-parameter-group$="group:outer_profile"]').length !== 1) throw new Error("uniform outer-frame material group is missing or duplicated");
      if (right.querySelector("[data-tool-parameter-key], [data-tube-designer-product-tool-editor], [data-product-tool-diagram-open], [data-tool-parameter-scope]")) throw new Error("product exposes a resource editor");
      const exposedName = right.textContent.match(/弧边直角|标准直角|外框结构方案|槽口模具|折弯开口角|资源已解析|系统内置|单件工艺|PRIVATE LEGACY|private-legacy-corner/);
      if (exposedName) throw new Error("product exposes resource objects or misleading names: " + exposedName[0]);
      for (const key of ["assemblyPlanningMode", "horizontalBranchReserve", "verticalBranchReserve", "horizontalEndConnection", "verticalEndConnection", "frameManufacturingMode", "frameJoinType", "frameCornerJoin", "doorFrameJoinType", "doorLeafFrameJoinType"]) {
        if (ordinary(key)) throw new Error("product exposes internal assembly parameter " + key);
      }
      view.tubeDesignerFloatingToolDiagram = { open: true, productId: product.entityId, fieldKey: "outerFrameGrooveTool" };
      if (views.renderDesignerToolDiagramDock(view)) throw new Error("stale tool diagram reopened from a product option");
    }
    assertBoundary();
    assertConnectionLayout();
    const materialSection = mount.querySelector('[data-tube-designer-parameter-group="section:materials"]');
    if (!materialSection?.open || materialSection.querySelector(':scope > summary > span')?.textContent !== "用料") throw new Error("material section is not initially open or renamed");
    const materialGroups = ["outer_profile", "horizontal_profile", "vertical_profile", "door_frame_profile", "door_leaf_profile", "door_horizontal_profile", "door_vertical_profile"];
    for (const key of materialGroups) {
      const group = materialSection.querySelector(`[data-tube-designer-parameter-group$="group:${key}"]`);
      if (!group?.open || !group.querySelector("input,select")) throw new Error("material fields are initially hidden: " + key);
    }
    const escapeMaterials = materialSection.querySelector('[data-tube-designer-parameter-group="scene:materials:section-group:escape_window"]');
    if (!escapeMaterials?.open) throw new Error("escape-window materials are initially closed");
    // Display defaults are scoped to the existing right editor metadata; the
    // add dialog must retain its previous disclosure policy.
    const addView = { ...view, tubeDesignerAddTemplateId: template.id, tubeDesignerAddDraft: parameters };
    const addWithDisplay = document.createElement("div"), addWithoutDisplay = document.createElement("div");
    addWithDisplay.innerHTML = views.renderDesignerAddParameterContent(view.scene.tubeDesigner, addView);
    const descriptorWithoutDisplay = { ...template }; delete descriptorWithoutDisplay.display;
    addWithoutDisplay.innerHTML = views.renderDesignerAddParameterContent({ ...view.scene.tubeDesigner, templates: [descriptorWithoutDisplay] }, addView);
    const addDisclosure = root => [...root.querySelectorAll("details[data-tube-designer-parameter-group]")].map(node => [node.dataset.tubeDesignerParameterGroup, node.open]);
    if (JSON.stringify(addDisclosure(addWithDisplay)) !== JSON.stringify(addDisclosure(addWithoutDisplay))) throw new Error("right editor defaults changed the add dialog");
    const closedMaterialGroups = [materialSection.querySelector('[data-tube-designer-parameter-group$="group:horizontal_profile"]'), escapeMaterials];
    for (const group of closedMaterialGroups) group.open = false;
    const assertMaterialDisclosure = () => {
      for (const group of closedMaterialGroups) {
        const current = mount.querySelector(`[data-tube-designer-parameter-group="${group.dataset.tubeDesignerParameterGroup}"]`);
        if (current?.open || group.isConnected && current !== group) throw new Error("refresh reopened or replaced a manually closed material group");
      }
    };
    if (mount.querySelectorAll("[data-product-control-editor]").length < 3 || mount.querySelector("[data-product-option-editor]")) throw new Error("single-face product should expose its product-defined connection choices");
    if (option("outerFrameFoldLayout")) throw new Error("single-face product asks for a planar route");
    if (option("doorLeafFrameConnection").selectedOptions[0].textContent !== "沿用当前做法") throw new Error("legacy connection is not presented as a product setting");
    if (option("doorLeafFrameConnection").outerHTML.includes("private-legacy-corner")) throw new Error("legacy resource id leaked into its option");
    for (const key of ["horizontalEndConnectionChoice", "verticalEndConnectionChoice"]) {
      if (!option(key) || JSON.stringify([...option(key).options].map(item => [item.value, item.textContent]))
          !== JSON.stringify([["insert", "插入"], ["weld", "贴焊"], ["tabs", "公母插接"]])) throw new Error("missing product-defined bar connection choices: " + key);
    }
    for (const field of ["outerFrameConnection", "doorFrameConnection"]) {
      const labels = [...option(field).options].map((item) => item.textContent);
      for (const label of ["45°斜拼焊接", "直切拼焊", "V槽折弯", "边弧槽折弯"]) if (!labels.includes(label)) throw new Error("missing actual connection choice " + label);
      if ([...option(field).options].some(item => /system:|user:|tool_library/.test(item.value))) throw new Error("UI option exposes an internal resource reference");
    }
    const note = mount.querySelector("#ongoing-note"), code = ordinary("productCode"), frameWidth = ordinary("frameWidth");
    const gap = ordinary("assemblyClearance");
    if (!gap || !gap.closest('label').textContent.includes("穿插间隙")) throw new Error("product does not expose its insertion clearance");
    const outerControl = option("outerFrameConnection");
    if ([...outerControl.options].some(item => item.value === "insert")
        || ![...outerControl.options].some(item => item.value === "tabs")) throw new Error("uniform frame exposes whole-tube insertion or lost its tab connection");
    let noteClicks = 0, codeClicks = 0, widthClicks = 0, cornerClicks = 0, gapClicks = 0, barClicks = 0;
    note.addEventListener("click", () => noteClicks++); code.addEventListener("click", () => codeClicks++);
    frameWidth.addEventListener("click", () => widthClicks++); outerControl.addEventListener("click", () => cornerClicks++);
    gap.addEventListener("click", () => gapClicks++);
    const scrollNodes = () => [mount.querySelector(".cam-context-pane"), mount.querySelector(".cam-info-pane"),
      mount.querySelector(".nested-left"), mount.querySelector("[data-tube-designer-parameter-scroll]")];
    const setScrolls = (values) => scrollNodes().forEach((node, index) => { node.scrollTop = values[index]; });
    const scrolls = () => scrollNodes().map((node) => node.scrollTop);
    async function pendingEdit(target, value, interact) {
      release = null; requested = null;
      if (target.type === "checkbox") target.checked = value; else target.value = value;
      const pending = actions.handleDesignerAreaAction(context, view, target.dataset.camChangeAction, target, ops);
      for (let attempts = 0; !release && attempts < 200; attempts++) await new Promise((resolve) => setTimeout(resolve, 5));
      if (!release) throw new Error("product did not commit its setting");
      interact(); release(); await pending;
      assertBoundary();
      if (template.extensions.productControls) assertConnectionLayout();
      assertMaterialDisclosure();
      if ((template.extensions.productControls ?? []).some(control => Object.hasOwn(requested.parameters, control.key))) throw new Error("synthetic UI control leaked into native parameters");
      return structuredClone(requested);
    }
    async function stableEdit(target, value, focus, manufacturingOnly = false) {
      const input = focus === "left" ? note : code;
      const previousModelDirty = view.tubeDesignerRightDraftDirty === true;
      const response = await pendingEdit(target, value, () => {
        if (manufacturingOnly && (view.tubeDesignerRightDraftDirty !== previousModelDirty
            || !previousModelDirty && view.tubeDesignerPartsDraftDirty !== true)) throw new Error("manufacturing edit changed the display generation status");
        input.focus({ preventScroll: true }); input.value = "latest uncommitted text"; input.setSelectionRange(2, 9, "backward");
        setScrolls([120, 160, 260, 190]);
      });
      if (manufacturingOnly && view.tubeDesignerRightDraftDirty !== previousModelDirty) throw new Error("native response changed the display generation status");
      if (document.activeElement !== input || input.value !== "latest uncommitted text" || input.selectionStart !== 2 || input.selectionEnd !== 9 || input.selectionDirection !== "backward") throw new Error("async response lost current typing or selection");
      if (JSON.stringify(scrolls()) !== JSON.stringify([120, 160, 260, 190])) throw new Error("async response lost four current scrolls " + scrolls());
      if (note !== mount.querySelector("#ongoing-note") || code !== ordinary("productCode") || frameWidth !== ordinary("frameWidth") || outerControl !== option("outerFrameConnection") || canvas !== mount.querySelector("canvas") || cube !== mount.querySelector(".cube")) throw new Error("product edit replaced nodes");
      await new Promise((resolve) => requestAnimationFrame(resolve));
      input.value = "later typing"; input.setSelectionRange(1, 5, "forward"); setScrolls([130, 170, 270, 200]);
      await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      if (JSON.stringify(scrolls()) !== JSON.stringify([130, 170, 270, 200]) || document.activeElement !== input || input.selectionStart !== 1 || input.selectionEnd !== 5 || input.value !== "later typing") throw new Error("late restoration overwrote later interaction");
      return response;
    }
    await actions.handleDesignerAreaAction(context, view, "tube-designer-product-control-change", outerControl, ops);
    if (commits || JSON.stringify(view.scene.tubeDesigner.product.parameters.tubeDesignerToolBindings.outerFrameGroove) !== savedOuter) throw new Error("same selection rewrote a saved angle");
    await actions.handleDesignerAreaAction(context, view, "tube-designer-product-tool-parameter-change", {
      value: "90", dataset: { tubeDesignerToolMode: "right", tubeDesignerToolField: "outerFrameGrooveTool", tubeDesignerToolParameter: "angle" },
    }, ops);
    if (commits) throw new Error("forged resource edit was accepted on a product option");
    const gapRequest = await pendingEdit(gap, "0.25", () => {
      if (view.tubeDesignerRightDraftDirty !== false || view.tubeDesignerPartsDraftDirty !== true) throw new Error("clearance edit invalidated the display model");
      gap.focus({ preventScroll: true }); gap.value = "0.35";
      setScrolls([120, 160, 260, 190]);
    });
    if (gapRequest.parameters.assemblyClearance !== 0.25 || ordinary("assemblyClearance") !== gap
        || document.activeElement !== gap || gap.value !== "0.35"
        || JSON.stringify(scrolls()) !== JSON.stringify([120, 160, 260, 190])) throw new Error("clearance update lost its input, current typing, focus or scrolls");
    gap.click();
    for (const [key, nativeKey] of [["horizontalEndConnectionChoice", "horizontalEndConnection"], ["verticalEndConnectionChoice", "verticalEndConnection"]]) {
      const barControl = option(key);
      barControl.addEventListener("click", () => barClicks++);
      for (const value of ["weld", "tabs", "insert"]) {
        const mapped = await stableEdit(barControl, value, "right", true);
        if (mapped.parameters[nativeKey] !== value || Object.hasOwn(mapped.parameters, key)
            || JSON.stringify(mapped.parameters.tubeDesignerToolBindings.outerFrameGroove) !== savedOuter) throw new Error("bar connection did not map only its declared manufacturing parameter");
        if (option(key) !== barControl) throw new Error("bar connection edit replaced its select node");
      }
      barControl.click();
    }
    for (const [key, value] of [["outerFrameConnection", "tabs"], ["doorFrameConnection", "miter"], ["doorLeafFrameConnection", "miter"]]) {
      if (view.tubeDesignerRightDraftDirty !== false) throw new Error("test connection must start with a current displayed model");
      await stableEdit(option(key), value, "right", true);
    }
    const conditionParameters = structuredClone(view.scene.tubeDesigner.product.parameters);
    for (const [pattern, removedKey, keptKey] of [["vertical", "horizontalEndConnectionChoice", "verticalEndConnectionChoice"], ["horizontal", "verticalEndConnectionChoice", "horizontalEndConnectionChoice"]]) {
      const removedBar = option(removedKey);
      // Restore the grid between cases so the next disappearing control exists.
      if (!removedBar) throw new Error("missing bar connection before condition removal");
      removedBar.focus({ preventScroll: true }); setScrolls([140, 180, 280, 210]);
      view.scene.tubeDesigner.product.parameters = { ...conditionParameters, infillPattern: pattern };
      view.tubeDesignerRightDraft = null;
      render(); assertBoundary(); assertConnectionLayout();
      if (removedBar.isConnected || option(removedKey) || !option(keptKey)
          || document.activeElement.matches("input, select, textarea")
          || JSON.stringify(scrolls()) !== JSON.stringify([140, 180, 280, 210])) throw new Error("bar applicability change retained a control, stole focus or lost scrolls");
      if (view.scene.tubeDesigner.product.parameters.horizontalEndConnection !== conditionParameters.horizontalEndConnection
          || view.scene.tubeDesigner.product.parameters.verticalEndConnection !== conditionParameters.verticalEndConnection) throw new Error("inapplicable bar connection draft was discarded");
      view.scene.tubeDesigner.product.parameters = structuredClone(conditionParameters);
      render();
    }
    const reusable = views.getReusablePresetValues(template, { ...conditionParameters,
      horizontalEndConnection: "tabs", verticalEndConnection: "weld", assemblyClearance: 0.45,
      horizontalEndConnectionChoice: "weld", verticalEndConnectionChoice: "tabs" }, "process");
    if (reusable.horizontalEndConnection !== "tabs" || reusable.verticalEndConnection !== "weld" || reusable.assemblyClearance !== 0.45
        || template.extensions.productControls.some(control => Object.hasOwn(reusable, control.key))) throw new Error("connection preset scope omitted native bar settings or stored synthetic UI keys");
    view.tubeDesignerUserData = { parameterPresets: [{ id: "browser-process-preset", templateId: template.id,
      templateVersion: template.version, scopeKey: "process", name: "连接方案回归", values: { ...reusable,
        frameWidth: 999, horizontalEndConnectionChoice: "weld", verticalEndConnectionChoice: "tabs" } },
      { id: "browser-process-preset-2", templateId: template.id, templateVersion: template.version, scopeKey: "process",
        name: "另一连接方案", values: { ...reusable, horizontalEndConnection: "weld", verticalEndConnection: "tabs", assemblyClearance: 0.55 } }] };
    render();
    const preset = mount.querySelector('select[data-tube-designer-preset-selection="right"][data-tube-designer-preset-scope="process"]');
    const focusedPresetRequest = await pendingEdit(preset, "user:browser-process-preset", () => {
      if (view.tubeDesignerRightDraftDirty !== false || view.tubeDesignerPartsDraftDirty !== true) throw new Error("connection preset invalidated the display model");
      preset.focus({ preventScroll: true }); setScrolls([140, 180, 280, 210]);
    });
    if (focusedPresetRequest.parameters.horizontalEndConnection !== "tabs" || focusedPresetRequest.parameters.verticalEndConnection !== "weld"
        || focusedPresetRequest.parameters.assemblyClearance !== 0.45 || focusedPresetRequest.parameters.frameWidth !== conditionParameters.frameWidth
        || preset !== mount.querySelector('select[data-tube-designer-preset-selection="right"][data-tube-designer-preset-scope="process"]')
        || document.activeElement !== preset || JSON.stringify(scrolls()) !== JSON.stringify([140, 180, 280, 210])) throw new Error("duplicate preset mode anchor reset the nested scroll or lost the exact preset node");
    setScrolls([150, 190, 290, 220]);
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    await new Promise(resolve => setTimeout(resolve, 25));
    if (document.activeElement !== preset || JSON.stringify(scrolls()) !== JSON.stringify([150, 190, 290, 220])) throw new Error("late preset restoration overwrote later scrolling");
    const presetRequest = await stableEdit(preset, "user:browser-process-preset-2", "left", true);
    if (presetRequest.parameters.horizontalEndConnection !== "weld" || presetRequest.parameters.verticalEndConnection !== "tabs"
        || presetRequest.parameters.assemblyClearance !== 0.55 || presetRequest.parameters.frameWidth !== conditionParameters.frameWidth
        || preset !== mount.querySelector('select[data-tube-designer-preset-selection="right"][data-tube-designer-preset-scope="process"]')
        || document.activeElement !== note || JSON.stringify(scrolls()) !== JSON.stringify([130, 170, 270, 200])) throw new Error("process preset escaped its parameter scope or replaced current interaction: " + JSON.stringify({
          horizontal: presetRequest.parameters.horizontalEndConnection, vertical: presetRequest.parameters.verticalEndConnection,
          gap: presetRequest.parameters.assemblyClearance, width: presetRequest.parameters.frameWidth, expectedWidth: conditionParameters.frameWidth,
          sameNode: preset === mount.querySelector('select[data-tube-designer-preset-selection="right"][data-tube-designer-preset-scope="process"]'),
          focused: document.activeElement === note, scrolls: scrolls(),
        }));
    const ordinaryRequest = await stableEdit(frameWidth, "42", "right");
    if (view.tubeDesignerRightDraftDirty !== true) throw new Error("a changed physical frame dimension must leave the displayed model outdated");
    if (JSON.stringify(ordinaryRequest.parameters.tubeDesignerToolBindings.outerFrameGroove) !== savedOuter) throw new Error("ordinary product edit rewrote angle 45");
    if (JSON.stringify(ordinaryRequest.parameters.tubeDesignerToolBindings.doorLeafFrameGroove) !== savedUnknown) throw new Error("ordinary edit rewrote a legacy binding");
    const choiceRequest = await stableEdit(outerControl, "edge-arc", "left", true);
    if (choiceRequest.parameters.outerFrameGrooveTool !== "system:edge-arc-groove" || choiceRequest.parameters.tubeDesignerToolBindings.outerFrameGroove.parameters.angle !== 90) throw new Error("product choice did not create its internal declared binding");
    if (JSON.stringify(choiceRequest.parameters.tubeDesignerToolBindings.doorFrameGroove) !== savedDoor || JSON.stringify(choiceRequest.parameters.tubeDesignerToolBindings.doorLeafFrameGroove) !== savedUnknown) throw new Error("selecting one corner rewrote another saved corner");
    const buttRequest = await stableEdit(outerControl, "butt", "right", true);
    if (buttRequest.parameters.frameManufacturingMode !== "segment_weld" || buttRequest.parameters.frameJoinType !== "butt_90") throw new Error("product choice failed to set all related native parameters atomically");
    if (JSON.stringify(buttRequest.parameters.tubeDesignerToolBindings) !== JSON.stringify(choiceRequest.parameters.tubeDesignerToolBindings)) throw new Error("turning folding off discarded its saved draft");
    note.click(); code.click(); frameWidth.click(); outerControl.click();
    const removed = ordinary("frameButtWrapMode");
    if (!removed) throw new Error("butt connection did not reveal its applicable wrap arrangement");
    const removedRequest = await pendingEdit(outerControl, "edge-arc", () => {
      removed.focus({ preventScroll: true }); setScrolls([140, 180, 280, 210]);
    });
    if (removed.isConnected || ordinary("frameButtWrapMode")) throw new Error("inapplicable butt arrangement remained visible");
    if (document.activeElement === note || document.activeElement === code || document.activeElement.matches("input, select, textarea")) throw new Error("removed conditional field stole focus for another field");
    if (JSON.stringify(scrolls()) !== JSON.stringify([140, 180, 280, 210])) throw new Error("condition removal lost current scrolls " + scrolls());
    if (JSON.stringify(removedRequest.parameters.tubeDesignerToolBindings) !== JSON.stringify(choiceRequest.parameters.tubeDesignerToolBindings)) throw new Error("condition removal cleared stored corner drafts");
    const currentTemplate = structuredClone(template);
    // Leave a reviewable screenshot using the real product renderer and final
    // production descriptor, rather than a hand-written UI mock-up.
    view.scene.tubeDesigner.templates = [currentTemplate];
    let previewValues = { ...parameters, frameWidth: 38, productCode: "TD-SF-001" };
    for (const [key, value] of [["outerFrameConnection", "edge-arc"], ["doorFrameConnection", "miter"], ["doorLeafFrameConnection", "miter"]]) {
      const control = currentTemplate.extensions.productControls.find(item => item.key === key);
      previewValues = controls.applyProductControlChoice(view, currentTemplate, control, previewValues, value);
    }
    view.scene.tubeDesigner.product.parameters = previewValues;
    view.tubeDesignerRightDraft = null;
    view.tubeDesignerRightPresetSelections = {};
    view.tubeDesignerUserData = { parameterPresets: [] };
    render();
    globalThis.productBoundaryPreview = { view, context, render, controls, template: currentTemplate, assertConnectionLayout };
    return { commits, patches, noteClicks, codeClicks, widthClicks, cornerClicks, gapClicks, barClicks, fixed: choiceRequest.parameters.tubeDesignerToolBindings.outerFrameGroove.parameters.angle,
      savedAngle: ordinaryRequest.parameters.tubeDesignerToolBindings.outerFrameGroove.parameters.angle,
      legacyPreserved: JSON.stringify(choiceRequest.parameters.tubeDesignerToolBindings.doorLeafFrameGroove) === savedUnknown };
  }, { raw, tools });
  assert.equal(result.commits, 16); assert.ok(result.patches >= 16);
  for (const key of ["noteClicks", "codeClicks", "widthClicks", "cornerClicks", "gapClicks"]) assert.equal(result[key], 1, key);
  assert.equal(result.barClicks, 2);
  assert.equal(result.fixed, 90); assert.equal(result.savedAngle, 45); assert.equal(result.legacyPreserved, true);
  assert.deepEqual(errors, []);
  await page.addStyleTag({ content: 'body{font-family:"Microsoft YaHei",sans-serif;background:#edf3f4;padding:16px}.cam-context-pane,.cam-viewport{display:none}.cam-info-pane{width:620px;height:auto;overflow:visible}.cam-info-pane:after,.tube-designer-parameter-scroll:after{display:none}.tube-designer-parameter-scroll{height:auto!important;overflow:visible!important}.cam-workbench{display:block}.cam-info-pane .tube-designer-parameter-header{display:none}' });
  await page.evaluate(() => {
    document.querySelectorAll('details[data-tube-designer-parameter-group]').forEach(node => {
      node.open = node.dataset.tubeDesignerParameterGroup !== 'section:materials';
    });
    for (const node of document.querySelectorAll('.cam-info-pane,[data-tube-designer-parameter-scroll]')) node.scrollTop = 0;
    globalThis.productBoundaryPreview.assertConnectionLayout();
  });
  await page.locator('[data-tube-designer-parameter-group="section:process"]').screenshot({ path: resolve(output, 'connections-panel.png') });
  await page.evaluate(() => {
    document.querySelectorAll('details[data-tube-designer-parameter-group]').forEach(node => {
      const key = node.dataset.tubeDesignerParameterGroup;
      node.open = key === 'section:materials'
        || key === 'scene:materials:section-group:escape_window'
        || ['outer_profile', 'horizontal_profile', 'vertical_profile', 'door_frame_profile',
          'door_leaf_profile', 'door_horizontal_profile', 'door_vertical_profile'].some(group => key.endsWith(`group:${group}`));
    });
  });
  await page.locator('[data-tube-designer-parameter-group="section:materials"]').screenshot({ path: resolve(output, 'materials-panel.png') });
  console.log("Actual Edge product connections: equal-width full rows without intermediate subgroups at 480px and 620px; five product choices and clearance input; bar insert/weld/tabs native mapping, manufacturing-only edits and scoped presets without preview regeneration; no resource objects; saved drafts preserved; async focus/selection, four scroll positions, input/canvas/listeners stable; conditional field removal, uniform outer-frame material UI and existing model expiration preservation passed. Renamed sections 用料/装配; all seven material groups and escape-window container initially expanded; manual collapse preserved across edits and async responses; add disclosure defaults unchanged.");
} finally { await browser.close(); }
