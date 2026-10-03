import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { manufacturingOnlyParameterKeys, productDisplayParameters } from "../../apps/tube-designer/webpage/productParameterDependencies.mjs";
import { getDesignerRenderSignature } from "../../apps/tube-designer/webpage/designerActions.mjs";

const root = new URL("../../apps/tube-designer/templates/product/", import.meta.url);
const templates = readdirSync(root).flatMap((directory) => {
  try { return [JSON.parse(readFileSync(new URL(`${directory}/template.json`, root), "utf8"))]; }
  catch (error) { if (error.code === "ENOENT" || error.code === "ENOTDIR") return []; throw error; }
}).filter((template) => template.extensions?.catalog?.listed !== false);
assert.equal(templates.length, 10);
const independent = {
  "single-face-security-window": "assemblyClearance", "minimal-protective-grille": "installHoleEnabled",
  "straight-steel-staircase": "boltHoleDiameter", "louver-window": "slotClearance",
  "decorative-door": "finish", "aluminium-window": "glassType",
};
for (const template of templates) {
  const keys = manufacturingOnlyParameterKeys(template);
  assert(keys.has("productCode"), template.id);
  assert(keys.has(independent[template.id] ?? "materialGrade"), template.id);
  const defaults = Object.fromEntries(template.parameters.map((field) => [field.key, field.defaultValue]));
  assert.deepEqual(productDisplayParameters(template, { ...defaults, productCode: "UPDATED" }),
    productDisplayParameters(template, defaults), `${template.id}: identity cannot expire geometry`);
  const dimension = template.parameters.find((field) => ["width", "sideLength1", "floorHeight"].includes(field.key)).key;
  assert.notDeepEqual(productDisplayParameters(template, { ...defaults, [dimension]: defaults[dimension] + 10 }),
    productDisplayParameters(template, defaults), `${template.id}: geometric dimensions must expire geometry`);
  const profileDimension = template.parameters.find((field) => /^(frameWidth|postWidth|stringerWidth|glassThickness|thickness)$/.test(field.key));
  if (profileDimension) assert(!keys.has(profileDimension.key), `${template.id}: visible section dimensions cannot be ignored`);
  assert(!keys.has("tubeDesignerProfileOverrides"), `${template.id}: profile geometry cannot be ignored`);
}
assert.throws(() => manufacturingOnlyParameterKeys({ parameters: [], extensions: {
  parameterDependencies: { manufacturingOnly: ["unknown"] },
} }), /未声明/);
const member = { entityId: "member", previewGeometryResourceId: "shared", previewGeometryResourceVersion: 1 };
assert.notEqual(getDesignerRenderSignature({ members: [{ ...member, transform: [0, 0, 0] }] }),
  getDesignerRenderSignature({ members: [{ ...member, transform: [10, 0, 0] }] }),
  "Moving a shared mesh must refresh the scene even when its resource version stays unchanged");

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 760 } });
  const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://tube-designer.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep) || !/\.m?js$/.test(path))
      return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT && pathname.startsWith("/src/apps/tube-designer/") ? resolve(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT, "apps/tube-designer", pathname.slice("/src/apps/tube-designer/".length)) : path, "utf8") });
  });
  await page.goto("http://tube-designer.test/");
  const cases = templates.map((descriptor) => ({ descriptor,
    editKey: independent[descriptor.id] ?? "materialGrade", displayEdit: false }));
  cases.push({ descriptor: templates.find((item) => item.id === "single-face-security-window"),
    editKey: "frameWidth", displayEdit: true });
  for (const { descriptor, editKey, displayEdit } of cases) {
    const result = await page.evaluate(async ({ descriptor, editKey, displayEdit }) => {
      const { handleDesignerAreaAction, captureDesignerScrollState, restoreDesignerScrollState } = await import(
        "/src/apps/tube-designer/webpage/designerActions.mjs");
      const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
      const { parameterVisible } = await import("/src/apps/tube-designer/webpage/parameterConditions.mjs");
      const template = { ...descriptor, available: true, descriptorLoaded: true, parameters: descriptor.parameters.map(
        (field) => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
          groupKey: field.group, options: field.choices })) };
      const parameters = Object.fromEntries(template.parameters.map((field) => [field.key, field.defaultValue]));
      const product = { entityId: descriptor.id, templateId: descriptor.id, parameters, name: descriptor.id };
      const view = { pending: false, activeAreaId: "view", scene: { tubeDesigner: {
        product, activeProductId: product.entityId, templates: [template], members: [{ entityId: "unchanged" }],
      } } };
      const field = template.parameters.find((item) => item.key === editKey);
      const condition = template.parameters.find((item) => item.key === "installHoleCountPerSide");
      const html = () => `<aside class="cam-context-pane tube-designer-instance-list" style="height:100px;overflow:auto"><div style="height:800px"></div></aside>
        <aside class="cam-info-pane" style="height:160px;overflow:auto"><section data-tube-designer-parameter-form>
        <div data-tube-designer-parameter-scroll style="height:100px;overflow:auto"><div style="height:300px"></div>
        <input data-tube-designer-parameter="productCode" value="${parameters.productCode}">
        <input data-tube-designer-parameter="${editKey}" type="${field.valueType === "boolean" ? "checkbox" : field.valueType === "number" ? "number" : "text"}"
          value="${parameters[editKey]}" ${parameters[editKey] === true ? "checked" : ""}>
        ${condition && parameterVisible(condition, view.scene.tubeDesigner.product.parameters) ? '<input data-conditional-install-hole value="3">' : ''}
        <div style="height:600px"></div></div></section><div style="height:700px"></div></aside>`;
      document.body.innerHTML = `<main id="panes">${html()}</main><canvas></canvas>`;
      const canvas = document.querySelector("canvas");
      const left = () => document.querySelector(".cam-context-pane");
      const outer = () => document.querySelector(".cam-info-pane");
      const right = () => document.querySelector("[data-tube-designer-parameter-scroll]");
      const code = () => document.querySelector('[data-tube-designer-parameter="productCode"]');
      const conditionalBefore = Boolean(document.querySelector("[data-conditional-install-hole]"));
      let complete, requested;
      const response = new Promise((done) => { complete = done; });
      const context = { mount: document.body, sceneProxy: { async invoke(method, payload) {
        if (method !== "TubeDesigner.UpdateProductParameters") throw new Error(method);
        requested = payload;
        return response;
      } }, actions: { async refreshActiveSceneState() {
        // The same hooks used by entry/createWorkbench: capture immediately
        // before replacing panes after the asynchronous response arrives.
        captureDesignerScrollState(context, view);
        const restore = capturePaneInteraction(document.body);
        document.querySelector("#panes").innerHTML = html();
        restoreDesignerScrollState(context, view);
        restore();
      } } };
      const target = document.querySelector(`[data-tube-designer-parameter="${editKey}"]`);
      if (field.valueType === "boolean") target.checked = !target.checked;
      else target.value = field.valueType === "number" ? Number(target.value) + 0.1 : "复核材质";
      left().scrollTop = 20; right().scrollTop = 30;
      const operation = handleDesignerAreaAction(context, view, "tube-designer-parameter-change", target, {
        renderProject() { throw new Error("Parameter editing must keep the viewport mounted"); },
      });
      for (let count = 0; !requested && count < 100; count++) await new Promise((done) => setTimeout(done, 5));
      if (!requested) throw new Error("No parameter update request");
      const immediateDirty = view.tubeDesignerRightDraftDirty;
      code().focus({ preventScroll: true }); code().value = "NEW-UNCOMMITTED"; code().setSelectionRange(4, 9);
      left().scrollTop = 170; right().scrollTop = 230; outer().scrollTop = 70;
      const expected = [left().scrollTop, right().scrollTop, outer().scrollTop];
      complete({ tubeDesigner: { ...view.scene.tubeDesigner, product: {
        ...product, parameters: requested.parameters, modelOutdated: displayEdit, partsOutdated: true,
      } } });
      await operation;
      await new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)));
      return { immediateDirty, dirty: view.tubeDesignerRightDraftDirty, partsDirty: view.tubeDesignerPartsDraftDirty,
        scroll: [left().scrollTop, right().scrollTop, outer().scrollTop], expected,
        focused: document.activeElement === code(), selection: [code().selectionStart, code().selectionEnd],
        value: code().value, canvas: document.querySelector("canvas") === canvas,
        conditionalBefore, conditionalAfter: Boolean(document.querySelector("[data-conditional-install-hole]")),
      };
    }, { descriptor, editKey, displayEdit });
    assert.equal(result.immediateDirty, displayEdit, `${descriptor.id}/${editKey}`);
    assert.equal(result.dirty, displayEdit, `${descriptor.id}/${editKey}`);
    assert.equal(result.partsDirty, !displayEdit, `${descriptor.id}/${editKey}`);
    assert.deepEqual(result.scroll, result.expected, descriptor.id);
    assert.equal(result.focused, true, descriptor.id);
    assert.deepEqual(result.selection, [4, 9], descriptor.id);
    assert.equal(result.value, "NEW-UNCOMMITTED", descriptor.id);
    assert.equal(result.canvas, true, descriptor.id);
    if (descriptor.id === "minimal-protective-grille") {
      assert.equal(result.conditionalBefore, true);
      assert.equal(result.conditionalAfter, false);
    }
  }
  console.log("PASS 10 products and security-window profile dimension: manufacturing edits preserve display state; profile changes expire the product; latest pane scroll, focus, selection, canvas and conditional fields retained");
} finally { await browser.close(); }
