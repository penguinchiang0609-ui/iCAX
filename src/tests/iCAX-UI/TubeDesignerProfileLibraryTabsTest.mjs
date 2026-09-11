import assert from "node:assert/strict";
import {
  profileLibraryState, libraryProfiles, visibleLibraryProfiles, profileSelectionKey, profileRef,
  templateProfilesForProduct, renderProfileLibraryLeftPane, renderProfileLibraryRightPane,
  renderProfileLibraryViewportOverlay, handleProfileLibraryAction, resolveSelectedProfileSketchSource,
} from "../../apps/tube-designer/webpage/profileLibrary.mjs";
import { refreshDesignerUserData, handleDesignerAreaAction, handleDesignerRibbonCommand } from "../../apps/tube-designer/webpage/designerActions.mjs";
import { renderDesignerAddParameterContent } from "../../apps/tube-designer/webpage/designerViews.mjs";

const completed = [];
async function test(name, run) { await run(); completed.push(name); }
const circle = { kind: "parametric-package", name: "截面", specification: "Φ40 × 2", width: 40, depth: 40,
  contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }], parameters: { width: 40 },
  parameterDefinitions: [{ key: "width", valueType: "number", displayName: "外径", defaultValue: 40 }] };
const packaged = { id: "round", profileType: "parametric-package", name: "系统圆管", descriptor: {
  id: "round", version: "1.0.0", parameters: circle.parameterDefinitions,
}, defaultParameters: { width: 40 }, previewProfile: circle };
const templates = ["rail-a", "rail-b"].map((templateId) => ({ ...packaged, id: "shared", templateId,
  name: `专用 ${templateId}`, templateName: `护栏 ${templateId}`, profileScope: "template" }));
function harness() {
  const calls = [], snapshots = [], visible = [], selected = [];
  const view = { activeAreaId: "profiles", tubeDesignerSystemProfiles: [packaged],
    tubeDesignerTemplateProfiles: structuredClone(templates),
    tubeDesignerUserData: { profiles: [{ ...packaged, id: "mine", name: "我的圆管" }] },
    viewport: { async applyViewSnapshot(snapshot) { snapshots.push(snapshot); return { applied: true }; },
      setVisibleEntityIds(ids) { visible.push(ids); }, setSelectedObjectIds(ids) { selected.push(ids); },
      fitViewForRevision() {}, setStandardView() {} },
  };
  const context = { actions: { log() {} }, productProxy: { async invoke(method, payload) { calls.push({ method, payload }); return {}; } },
    sceneProxy: { resources: { get() {} }, async invoke(method, payload) { calls.push({ method, payload }); return { geometryResourceId: "resource://profile", geometryResourceVersion: 1 }; } } };
  const ops = { renderProject() {}, showNotice() {} };
  const act = (suffix, target = {}) => handleProfileLibraryAction(context, view, `tube-designer-profile-library-${suffix}`, target, ops);
  return { view, context, ops, calls, snapshots, visible, selected, act };
}
const tick = () => new Promise((resolve) => setTimeout(resolve, 0));

await test("source and profile-type tabs isolate the compact two-column card list", async () => {
  const { view, act } = harness();
  let html = renderProfileLibraryLeftPane({}, view);
  assert.equal((html.match(/role="tab"/g) ?? []).length, 3);
  assert.match(html, /系统内置/); assert.match(html, /模板自带/); assert.match(html, /我的/);
  assert.match(html, /tube-profile-library-list/);
  assert.match(html, /tube-profile-library-group/);
  assert.match(html, /圆管 \/ 椭圆管/);
  assert.match(html, /tube-profile-library-card-preview/);
  assert.match(html, /tube-profile-library-card-copy[\s\S]*程式/);
  assert.doesNotMatch(html, /tube-profile-library-card-type-badge/);
  assert.match(html, /class="outer"/);
  assert.match(html, /class="hole"/);
  assert.doesNotMatch(html, /tube-profile-library-card-icon/);
  assert.doesNotMatch(html, /系统内置 · 程式|程式管型包|定式管型/);
  assert.match(html, /data-tube-designer-profile-key="system:round"/);
  assert.doesNotMatch(html, /data-tube-designer-profile-key="(?:user|template):/);
  await act("scope", { dataset: { tubeProfileLibraryScope: "template" } });
  html = renderProfileLibraryLeftPane({}, view);
  assert.match(html, /专用 rail-a/); assert.match(html, /专用 rail-b/);
  assert.match(html, /template:rail-a:shared/); assert.match(html, /template:rail-b:shared/);
  assert.doesNotMatch(html, /data-tube-designer-profile-key="(?:user|system):/);
  assert.equal(profileSelectionKey(templates[0]), "template:rail-a:shared");
  assert.notEqual(profileSelectionKey(templates[0]), profileSelectionKey(templates[1]));
  assert.deepEqual(profileRef(templates[0]), { scope: "template", templateId: "rail-a", id: "shared" });
  await act("type", { dataset: { tubeProfileLibraryType: "fixed" } });
  assert.equal(visibleLibraryProfiles(view).length, 2);
  assert.doesNotMatch(renderProfileLibraryLeftPane({}, view), /role="tablist" aria-label="管型类型"/);
});

await test("search limits selection to visible results and empty tabs clear old geometry", async () => {
  const { view, act, visible, selected } = harness();
  await act("scope", { dataset: { tubeProfileLibraryScope: "template" } });
  await act("search", { value: "rail-b" });
  renderProfileLibraryLeftPane({}, view);
  assert.equal(view.tubeDesignerSelectedProfileId, "template:rail-b:shared");
  assert.equal(visibleLibraryProfiles(view).length, 1);
  await act("search", { value: "not available" });
  assert.equal(view.tubeDesignerSelectedProfileId, "");
  assert.deepEqual(visible.at(-1), []); assert.deepEqual(selected.at(-1), []);
  assert.match(renderProfileLibraryRightPane({}, view), /尚未选择管型/);
  assert.match(renderProfileLibraryLeftPane({}, view), /没有匹配的管型/);
});

await test("template metadata is read only but preview parameters remain editable", async () => {
  const { view, act, calls, context, ops } = harness();
  await act("scope", { dataset: { tubeProfileLibraryScope: "template" } });
  const html = renderProfileLibraryRightPane({}, view);
  assert.match(html, /模板自带管型/); assert.match(html, /所属模板：护栏 rail-a/);
  assert.match(html, /data-tube-profile-editor-parameter="width"/);
  assert.match(html, /data-cam-action="tube-designer-profile-library-edit-sketch"/);
  assert.match(html, /定制到我的/);
  assert.match(html, /value="500" data-tube-profile-preview-length/);
  assert.doesNotMatch(html, /data-cam-action="tube-designer-profile-export-(dxf|step)"/);
  assert.doesNotMatch(html, /data-tube-profile-editor-name|data-cam-action="tube-designer-profile-library-(save|delete)"/);
  for (const suffix of ["save", "delete"]) await handleProfileLibraryAction(context, view, `tube-designer-profile-library-${suffix}`,
    { dataset: { tubeDesignerProfileId: "shared", tubeDesignerProfileKey: "template:rail-a:shared" } }, ops);
  assert.equal(calls.length, 0);
});

await test("profile library opens the selected contour as an update or a copy session", async () => {
  const { view, context, ops, calls } = harness();
  let selectedTab = "";
  context.actions.selectRibbonTab = async (id) => { selectedTab = id; };
  view.tubeDesignerUserData.profiles.push({
    id: "frozen", revision: 4, name: "我的 DXF 方管", kind: "imported-dxf",
    contours: [{ kind: "roundedRectangle", width: 40, height: 20, radius: 0 }],
  });
  view.tubeDesignerProfileLibrary = { scope: "user", search: "", selectedByScope: {} };
  view.tubeDesignerSelectedProfileId = "user:frozen";
  await handleDesignerAreaAction(context, view, "tube-designer-profile-library-edit-sketch", {}, ops);
  assert.equal(selectedTab, "");
  assert.equal(view.tubeDesignerSketchDialogOpen, true, "editing opens the section modal without switching tabs");
  assert.equal(view.tubeDesignerSketch.sectionSession.kind, "update");
  assert.equal(view.tubeDesignerSketch.sectionSession.sourceRevision, 4);
  assert.equal(view.tubeDesignerSketch.section.entities[0].kind, "rectangle");

  view.tubeDesignerSketch.section.dirty = false;
  view.tubeDesignerProfileLibrary.scope = "system";
  view.tubeDesignerSelectedProfileId = "system:round";
  view.tubeDesignerProfileDrafts = { "system:round": { parameters: { width: 55 } } };
  await handleDesignerAreaAction(context, view, "tube-designer-profile-library-edit-sketch", {}, ops);
  assert.equal(view.tubeDesignerSketch.sectionSession.kind, "copy");
  const generated = calls.find((call) => call.method === "TubeDesigner.GenerateProfilePreview");
  assert.deepEqual(generated.payload.parameters, { width: 55 });
});

await test("new profile sketch immediately enters a blank sketch area", async () => {
  const { view, context, ops } = harness();
  let selectedTab = "";
  context.actions.selectRibbonTab = async (id) => { selectedTab = id; };
  await handleDesignerAreaAction(context, view, "tube-designer-profile-library-new-sketch", {}, ops);
  assert.equal(selectedTab, "");
  assert.equal(view.tubeDesignerSketchDialogOpen, true);
  assert.notEqual(context.activeRibbonTabId, "sketch");
  assert.equal(view.tubeDesignerSketch.sectionSession.kind, "create");
  assert.equal(view.tubeDesignerSketch.section.entities.length, 0);
});

await test("drawing entry is not duplicated at the bottom of profiles", async () => {
  const { view, act } = harness();
  assert.doesNotMatch(renderProfileLibraryLeftPane({}, view), /tube-designer-profile-library-new-sketch/);
  await act("scope", { dataset: { tubeProfileLibraryScope: "user" } });
  assert.doesNotMatch(renderProfileLibraryLeftPane({}, view), /tube-designer-profile-library-new-sketch/);
});

await test("resolving a parameterized profile uses the current evaluated preview", async () => {
  const { view, context } = harness();
  view.tubeDesignerProfileDrafts = { "system:round": { parameters: { width: 64 } } };
  context.sceneProxy.invoke = async (_method, payload) => ({
    profile: { name: "当前圆管", contours: [{ kind: "circle", radius: payload.parameters.width / 2 }] },
  });
  const source = await resolveSelectedProfileSketchSource(context, view);
  assert.equal(source.directUpdate, false);
  assert.equal(source.snapshot.contours[0].radius, 32);
});

await test("template preview uses owner-qualified reference and late responses cannot repopulate an empty source", async () => {
  const { view, context, act, calls, snapshots } = harness();
  let resolve;
  context.sceneProxy.invoke = async (method, payload) => { calls.push({ method, payload }); return new Promise((done) => { resolve = done; }); };
  await act("scope", { dataset: { tubeProfileLibraryScope: "template" } });
  renderProfileLibraryViewportOverlay(context, view);
  await tick();
  assert.deepEqual(calls[0].payload.profileRef, { scope: "template", templateId: "rail-a", id: "shared" });
  await act("search", { value: "no matches" });
  renderProfileLibraryViewportOverlay(context, view);
  resolve({ geometryResourceId: "resource://old", geometryResourceVersion: 1 });
  await tick();
  assert.equal(snapshots.length, 0); assert.equal(view.tubeDesignerSelectedProfileId, "");
});

await test("list refresh carries template packages separately from system and user profiles", async () => {
  const { view, context } = harness();
  context.productProxy.invoke = async () => ({ profiles: [], systemProfiles: [packaged], templateProfiles: templates });
  assert.equal(await refreshDesignerUserData(context, view), true);
  assert.equal(libraryProfiles(view).length, 3);
  assert.deepEqual(templateProfilesForProduct(view, "rail-a").map((profile) => profile.id), ["shared"]);
  assert.equal(templateProfilesForProduct(view, "other").length, 0);
});

await test("opening the profile resource retries a failed initial user-data load", async () => {
  const { view, context, ops } = harness();
  view.tubeDesignerSystemProfiles = [];
  view.tubeDesignerUserDataError = "第一次加载超时";
  let calls = 0;
  context.productProxy.invoke = async (method) => {
    assert.equal(method, "TubeDesigner.ListUserData");
    calls += 1;
    return { profiles: [], systemProfiles: [packaged], templateProfiles: [] };
  };
  context.actions.selectRibbonTab = async () => {};
  await handleDesignerRibbonCommand(context, view, "resources.profiles", ops);
  assert.equal(calls, 1);
  assert.equal(view.tubeDesignerSystemProfiles.length, 1);
  assert.equal(view.tubeDesignerUserDataError, "");
});

await test("switching during viewport resource hydration cannot reveal the old profile", async () => {
  const { view, context, act, visible } = harness();
  let finishHydration;
  view.viewport.applyViewSnapshot = async () => new Promise((resolve) => { finishHydration = resolve; });
  await act("scope", { dataset: { tubeProfileLibraryScope: "template" } });
  renderProfileLibraryViewportOverlay(context, view);
  await tick();
  assert.equal(typeof finishHydration, "function");
  await act("search", { value: "empty result" });
  finishHydration({ applied: true });
  await tick();
  assert.deepEqual(visible.at(-1), []);
  assert.equal(view.tubeDesignerSelectedProfileId, "");
});

function productHarness() {
  const h = harness();
  const descriptor = { id: "rail-a", available: true, name: "护栏", parameters: [{ key: "frameProfileType", displayName: "边框截面", type: "select", valueType: "enum", defaultValue: "round", options: [{ value: "round", label: "圆管" }] }] };
  h.view.scene = { tubeDesigner: { templates: [descriptor] } };
  h.view.tubeDesignerAddTemplateId = descriptor.id;
  h.view.tubeDesignerAddDraft = {};
  h.context.mount = { querySelector() { return null; }, querySelectorAll() { return []; } };
  h.productAct = (suffix, extra = {}) => handleDesignerAreaAction(h.context, h.view, `tube-designer-${suffix}`, {
    dataset: { tubeDesignerProfileMode: "add", tubeDesignerProfilePrefix: "frame", ...extra.dataset }, ...extra,
  }, h.ops);
  return h;
}

await test("products only offer their own template packages and prevent copying them into my profiles", async () => {
  const { view, productAct } = productHarness();
  await productAct("profile-selection-change", { value: "template:rail-a:shared" });
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.templateId, "rail-a");
  let html = renderDesignerAddParameterContent(view.scene.tubeDesigner, view);
  assert.match(html, /<optgroup label="模板自带">/);
  assert.match(html, /value="template:rail-a:shared" selected/);
  assert.doesNotMatch(html, /template:rail-b:shared|保存为我的管型/);
  await productAct("open-profile-dialog");
  assert.equal(view.tubeDesignerProfileDialog, undefined);
  await productAct("profile-selection-change", { value: "template:rail-b:shared" });
  assert.match(view.error, /不属于当前模板/);
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.templateId, "rail-a");
  await productAct("profile-selection-change", { value: "builtin:round" });
  assert.deepEqual(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides, {});
});

await test("template parameter reevaluation retains scoped frozen provenance", async () => {
  const { view, context, calls, productAct } = productHarness();
  await productAct("profile-selection-change", { value: "template:rail-a:shared" });
  context.productProxy.invoke = async (method, payload) => {
    calls.push({ method, payload });
    return { profile: { ...circle, parameters: payload.parameters, profileScope: "template", templateId: "rail-a", profileDefinitionId: "shared" } };
  };
  await handleDesignerAreaAction(context, view, "tube-designer-profile-parameter-change", {
    dataset: { tubeDesignerProfileMode: "add", tubeDesignerProfilePrefix: "frame", tubeDesignerProfileParameter: "width" }, value: "48",
  }, { renderProject() {}, showNotice() {} });
  assert.deepEqual(calls[0].payload, { profileRef: { scope: "template", templateId: "rail-a", id: "shared" }, parameters: { width: 48 } });
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.profileScope, "template");
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.parameters.width, 48);
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.savedProfileId, undefined);
});

console.log(`${completed.length} profile library tab tests passed.`);
