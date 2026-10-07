// Real Edge DOM; product descriptors/renderers/actions are production modules.
// Native responses are controlled so async rejection and interaction can be observed.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";
const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
const directories = ["single_face_security_window", "modular_guardrail", "modular_guardrail_glass-straight", "modular_guardrail_cross-straight", "modular_guardrail_diamond-straight"];
const templates = directories.map((directory) => {
  const raw = JSON.parse(readFileSync(resolve(sourceRoot, "apps/tube-designer/templates/product", directory, "template.json")));
  return { ...raw, available: true, name: catalogText(raw.displayName),
    groups: raw.groups.map((group) => ({ ...group, displayName: catalogText(group.displayName) })),
    parameters: raw.parameters.map((field) => ({ ...field,
      type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
      displayName: catalogText(field.displayName), groupKey: field.group,
      options: field.choices?.map((choice) => ({ ...choice, label: catalogText(choice.displayName) })),
    })) };
});
assert.equal(templates.reduce((count, template) => count + template.parameters.filter((field) => field.presentation?.editor === "profile-library").length, 0), 24);
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1180, height: 760 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://profile-constraints.test/**", async (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !file.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep) || !/\.m?js$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://profile-constraints.test/");
  const result = await page.evaluate(async (templates) => {
    const { handleDesignerAreaAction, captureDesignerScrollState } = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const { renderDesignerRightPane } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const { parameterVisible } = await import("/src/apps/tube-designer/webpage/parameterConditions.mjs");
    const { productProfileRole } = await import("/src/apps/tube-designer/webpage/productResourceBindings.mjs");
    const snapshot = (kind, scope = "system") => ({ schema: "icax.imported-tube-profile", schemaVersion: 1,
      kind: "profile-package", sectionKind: kind, profileForm: "parametric", profileScope: scope,
      profileDefinitionId: kind, name: kind, parameters: { width: 40, note: "draft" },
      parameterDefinitions: [{ key: "width", valueType: "number", defaultValue: 40 }, { key: "note", valueType: "string", defaultValue: "draft" }],
      contours: kind === "round" ? [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }]
        : [{ kind: "roundedRectangle", width: 40, height: 30, radius: 2 }, { kind: "roundedRectangle", width: 36, height: 26, radius: 1 }],
      contourCount: 2, hollow: true, width: 40, depth: 30 });
    const profiles = ["rect", "round", "oval", "channel"].map((kind) => ({ id: kind, name: kind,
      profileForm: "parametric", descriptor: { parameters: snapshot(kind).parameterDefinitions }, previewProfile: snapshot(kind) }));
    document.body.innerHTML = `<style>body{margin:0}main{display:flex;gap:10px}.cam-context-pane,.cam-info-pane{height:210px;overflow:auto;width:480px}.nested-left{height:130px;overflow:auto}.nested-left>div{height:1000px}.tube-designer-parameter-scroll{height:150px!important;overflow:auto!important}aside:after{content:"";display:block;height:1100px}input,select{min-height:22px}</style><main><aside class="cam-context-pane"><div class="nested-left"><div><input id="ongoing-note" value="unsaved typing"></div></div></aside><div class="cam-viewport"><canvas></canvas></div><aside class="cam-info-pane"></aside></main>`;
    const mount = document.querySelector("main"), canvas = mount.querySelector("canvas");
    const view = { activeAreaId: "view", tubeDesignerSystemProfiles: profiles };
    let deferred = null, commits = 0, evaluationKind = "oval";
    const context = { mount, project: { projectId: "browser-regression" },
      productProxy: { invoke: async (_method, payload) => new Promise((resolve) => { deferred = () => resolve({ profile: { ...snapshot(evaluationKind), parameters: payload.parameters } }); }) },
      sceneProxy: { invoke: async (method, payload) => {
        if (method !== "TubeDesigner.UpdateProductParameters") throw new Error(method);
        commits++; view.scene.tubeDesigner.product.parameters = structuredClone(payload.parameters);
        return { tubeDesigner: view.scene.tubeDesigner };
      } }, actions: {} };
    const render = () => {
      const restore = capturePaneInteraction(mount);
      captureDesignerScrollState(context, view);
      mount.querySelector(".cam-info-pane").innerHTML = renderDesignerRightPane(context, view);
      mount.querySelectorAll("details").forEach((node) => { node.open = true; });
      restore();
    };
    const ops = { renderProject: render, showNotice() {} };
    const rolesChecked = [];
    for (const template of templates) {
      const parameters = Object.fromEntries(template.parameters.map((field) => [field.key, field.defaultValue]));
      parameters.accessDoorEnabled = true; parameters.tubeSpecificationPreset = "custom";
      if (template.id === "single-face-security-window") parameters.foldedPostJoint = "insert";
      view.scene = { tubeDesigner: { templates: [template], product: { entityId: template.id + "-instance", templateId: template.id, parameters } } };
      view.tubeDesignerRightDraft = null;
      render();
      for (const field of template.parameters.filter((field) => field.presentation?.editor === "profile-library" && parameterVisible(field, parameters))) {
        const select = mount.querySelector(`[data-tube-designer-profile-parameter="${field.key}"]`);
        if (!select) throw new Error(template.id + ": missing profile field " + field.key);
        const kinds = field.presentation.profileConstraints.sectionKinds;
        for (const profile of profiles) {
          const found = [...select.options].some((option) => option.value === "system:" + profile.id);
          if (found !== kinds.includes(profile.id)) throw new Error(template.id + ": wrong candidates for " + field.key);
        }
        const before = JSON.stringify(view.scene.tubeDesigner.product.parameters);
        await handleDesignerAreaAction(context, view, "tube-designer-profile-selection-change", { value: "builtin:oval", dataset: {
          tubeDesignerProfilePrefix: productProfileRole(field), tubeDesignerProfileParameter: field.key, tubeDesignerProfileMode: "right",
        } }, ops);
        if (!view.error.includes("不支持") || JSON.stringify(view.scene.tubeDesigner.product.parameters) !== before) throw new Error("forged builtin changed parameters");
        rolesChecked.push(template.id + ":" + productProfileRole(field));
      }
    }
    // The guardrail's existing editable role, with a valid frozen rectangle.
    const template = templates[1], parameters = Object.fromEntries(template.parameters.map((field) => [field.key, field.defaultValue]));
    parameters.tubeDesignerProfileOverrides = { handrail: snapshot("rect") };
    view.scene = { tubeDesigner: { templates: [template], product: { entityId: "async-guardrail", templateId: template.id, parameters } } };
    view.tubeDesignerRightDraft = null; view.error = ""; render();
    const target = mount.querySelector('[data-tube-designer-profile-prefix="handrail"][data-tube-designer-profile-parameter="width"]');
    if (!target) throw new Error("missing actual embedded profile editor");
    target.value = "48";
    const pending = handleDesignerAreaAction(context, view, "tube-designer-profile-parameter-change", target, ops);
    while (!deferred) await new Promise((resolve) => setTimeout(resolve, 5));
    const note = mount.querySelector("#ongoing-note");
    note.focus({ preventScroll: true }); note.value = "latest unfinished text"; note.setSelectionRange(2, 9, "backward");
    const left = mount.querySelector(".cam-context-pane"), right = mount.querySelector(".cam-info-pane"), nestedLeft = mount.querySelector(".nested-left");
    left.scrollTop = 120; right.scrollTop = 160; nestedLeft.scrollTop = 250;
    const scroller = mount.querySelector("[data-tube-designer-parameter-scroll]"); scroller.scrollTop = 180;
    deferred(); await pending;
    const interaction = { focus: document.activeElement === note, text: note.value,
      selection: [note.selectionStart, note.selectionEnd, note.selectionDirection], left: left.scrollTop,
      right: right.scrollTop, nestedLeft: nestedLeft.scrollTop, nestedRight: mount.querySelector("[data-tube-designer-parameter-scroll]").scrollTop,
      canvas: mount.querySelector("canvas") === canvas };
    const rejectedAsync = view.error.includes("不适用") && commits === 0
      && view.scene.tubeDesigner.product.parameters.tubeDesignerProfileOverrides.handrail.sectionKind === "rect";
    evaluationKind = "rect"; deferred = null;
    const validTarget = mount.querySelector('[data-tube-designer-profile-prefix="handrail"][data-tube-designer-profile-parameter="width"]');
    validTarget.value = "52";
    const validPending = handleDesignerAreaAction(context, view, "tube-designer-profile-parameter-change", validTarget, ops);
    while (!deferred) await new Promise((resolve) => setTimeout(resolve, 5));
    note.focus({ preventScroll: true }); note.value = "still editing after response"; note.setSelectionRange(1, 7, "forward");
    left.scrollTop = 130; right.scrollTop = 170; nestedLeft.scrollTop = 270;
    mount.querySelector("[data-tube-designer-parameter-scroll]").scrollTop = 190;
    deferred(); await validPending;
    const acceptedAsync = commits === 1 && view.scene.tubeDesigner.product.parameters.tubeDesignerProfileOverrides.handrail.parameters.width === 52;
    const acceptedInteraction = { focus: document.activeElement === note, text: note.value,
      selection: [note.selectionStart, note.selectionEnd, note.selectionDirection], left: left.scrollTop,
      right: right.scrollTop, nestedLeft: nestedLeft.scrollTop, nestedRight: mount.querySelector("[data-tube-designer-parameter-scroll]").scrollTop,
      canvas: mount.querySelector("canvas") === canvas };
    return { rolesChecked, rejectedAsync, interaction, acceptedAsync, acceptedInteraction };
  }, templates);
  assert.equal(result.rolesChecked.length, 23);
  assert.equal(result.rolesChecked.some((role) => role === templates[0].id + ":foldedPost"), true);
  assert.equal(result.rolesChecked.some((role) => role === templates[2].id + ":infill"), false, "The glass design keeps its inactive infill draft hidden");
  assert.equal(result.rejectedAsync, true);
  assert.equal(result.acceptedAsync, true);
  assert.deepEqual(result.interaction, { focus: true, text: "latest unfinished text", selection: [2, 9, "backward"],
    left: 120, right: 160, nestedLeft: 250, nestedRight: 180, canvas: true });
  assert.deepEqual(result.acceptedInteraction, { focus: true, text: "still editing after response", selection: [1, 7, "forward"],
    left: 130, right: 170, nestedLeft: 270, nestedRight: 190, canvas: true });
  assert.deepEqual(errors, []);
  console.log("Window + four guardrails: 24 declared roles, 23 active roles filtered including folded-post insert (glass infill inactive); forged choices/evaluated incompatible profiles rejected; latest focus, selection and four scrolls preserved.");
} finally { await browser.close(); }
