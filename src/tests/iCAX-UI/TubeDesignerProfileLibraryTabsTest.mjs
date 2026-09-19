import assert from "node:assert/strict";
import {
  profileLibraryState, libraryProfiles, visibleLibraryProfiles, profileSelectionKey, profileRef,
  templateProfilesForProduct, renderProfileLibraryLeftPane, renderProfileLibraryRightPane,
  renderProfileLibraryViewportOverlay, handleProfileLibraryAction, resolveSelectedProfileSketchSource,
} from "../../apps/tube-designer/webpage/profileLibrary.mjs";
import { refreshDesignerUserData, handleDesignerAreaAction, handleDesignerRibbonCommand } from "../../apps/tube-designer/webpage/designerActions.mjs";
import {
  applyReusablePresetValues,
  getReusablePresetValues,
  renderDesignerAddParameterContent,
} from "../../apps/tube-designer/webpage/designerViews.mjs";

const completed = [];
async function test(name, run) { await run(); completed.push(name); }
const circle = { kind: "profile-package", profileForm:"parametric", name: "截面", specification: "Φ40 × 2", width: 40, depth: 40,
  contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }], parameters: { width: 40 },
  parameterDefinitions: [{ key: "width", valueType: "number", displayName: "外径", defaultValue: 40 }],
  parameterDiagram: { schemaVersion: 1, annotations: [{ parameter: "width", kind: "linear", from: ["-width/2", 0], to: ["width/2", 0], axis: "x", side: "top" }] } };
const packaged = { id: "round", profileType: "profile-package", profileForm:"parametric", name: "系统圆管", descriptor: {
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

await test("management has two sources and form is independent of ownership", async () => {
  const {view,act}=harness();
  const html=renderProfileLibraryLeftPane({},view);
  assert.equal((html.match(/role="tab"/g)??[]).length,2);
  assert.doesNotMatch(html,/模板自带|template:rail/);
  assert.equal(libraryProfiles(view).length,2);
  await act("scope",{dataset:{tubeProfileLibraryScope:"template"}});
  assert.equal(profileLibraryState(view).scope,"system");
  for(const scope of ["system","user"]) {
    const fixed={...packaged,id:"fixed",profileForm:"fixed"};
    const parametric={...packaged,id:"program",profileForm:"parametric"};
    if(scope==="system")view.tubeDesignerSystemProfiles=[fixed,parametric];
    else view.tubeDesignerUserData.profiles=[fixed,parametric];
    await act("scope",{dataset:{tubeProfileLibraryScope:scope}});
    const cards=renderProfileLibraryLeftPane({},view);
    assert.match(cards,/定式/);assert.match(cards,/程式/);
    view.tubeDesignerSelectedProfileId=scope+":fixed";
    assert.doesNotMatch(renderProfileLibraryRightPane({},view),/data-tube-profile-editor-parameter=/);
  }
});

await test("empty searches clear selection and stale template scope is normalized", async () => {
  const {view,act,visible,selected}=harness();
  view.tubeDesignerProfileLibrary={scope:"template"};
  assert.equal(profileLibraryState(view).scope,"system");
  await act("search",{value:"not available"});
  renderProfileLibraryLeftPane({},view);
  assert.equal(view.tubeDesignerSelectedProfileId,"");
  assert.deepEqual(visible.at(-1),[]);assert.deepEqual(selected.at(-1),[]);
});

await test("profile library opens the selected contour as an update or a copy session", async () => {
  const { view, context, ops, calls } = harness();
  let selectedTab = "";
  context.actions.selectRibbonTab = async (id) => { selectedTab = id; };
  view.tubeDesignerUserData.profiles.push({
    id: "frozen", revision: 4, name: "我的 DXF 方管", kind: "fixed-section", profileForm:"fixed",
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
  await act("scope", { dataset: { tubeProfileLibraryScope: "system" } });
  renderProfileLibraryViewportOverlay(context, view);
  await tick();
  assert.deepEqual(calls[0].payload.profileRef, { scope: "system", id: "round" });
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
  assert.equal(libraryProfiles(view).length, 1);
  assert.deepEqual(templateProfilesForProduct(view, "rail-a").map((profile) => profile.id), ["shared"]);
  assert.equal(templateProfilesForProduct(view, "other").length, 0);
});

await test("system catalog survives unrelated user-data failures and exposes catalog errors", async () => {
  const { view, context } = harness();
  view.tubeDesignerSystemProfiles = [];
  context.productProxy.invoke = async (method) => {
    if (method === "TubeDesigner.ListUserData") throw new Error("用户库不可用");
    assert.equal(method, "TubeDesigner.ListSystemProfiles");
    return { systemProfiles: [packaged] };
  };
  await refreshDesignerUserData(context, view);
  assert.equal(view.tubeDesignerSystemProfiles.length, 1);
  assert.equal(view.tubeDesignerSystemProfilesError, "");
  view.tubeDesignerSystemProfiles = [];
  context.productProxy.invoke = async () => { throw new Error("目录加载失败 <detail>"); };
  await refreshDesignerUserData(context, view);
  const html = renderProfileLibraryLeftPane(context, view);
  assert.match(html, /管型加载失败/);
  assert.match(html, /目录加载失败 &lt;detail&gt;/);
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
  await act("scope", { dataset: { tubeProfileLibraryScope: "system" } });
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

await test("product profile roles expose and evaluate the complete system profile library", async () => {
  const { view, context, calls, productAct } = productHarness();
  view.scene.tubeDesigner.templates[0].parameters.push(
    { key: "frameWidth", displayName: "模板截面宽度", type: "number", valueType: "number", defaultValue: 40 },
    { key: "frameDepth", displayName: "模板截面深度", type: "number", valueType: "number", defaultValue: 40 },
    { key: "frameWallThickness", displayName: "模板壁厚", type: "number", valueType: "number", defaultValue: 2 },
  );
  view.tubeDesignerSystemProfiles.push({
    ...packaged,
    id: "oval",
    name: "系统椭圆管",
    descriptor: { ...packaged.descriptor, id: "oval" },
    previewProfile: { ...circle, name: "系统椭圆管", profileScope: "system", profileDefinitionId: "oval" },
  });
  let html = renderDesignerAddParameterContent(view.scene.tubeDesigner, view);
  assert.match(html, /<optgroup label="管型库 · 系统内置">/);
  assert.match(html, /value="system:round"/);
  assert.match(html, /value="system:oval"/);

  await productAct("profile-selection-change", { value: "system:oval" });
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.profileScope, "system");
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.profileDefinitionId, "oval");
  assert.equal(calls.length, 0, "catalogue preview snapshot should avoid an unnecessary reevaluation");
  html = renderDesignerAddParameterContent(view.scene.tubeDesigner, view);
  assert.doesNotMatch(html, /data-tube-designer-parameter="frameWidth"|data-tube-designer-parameter="frameDepth"|data-tube-designer-parameter="frameWallThickness"/,
    "the selected profile owns its dimensions; the product's legacy shape fields must not be duplicated");

  context.productProxy.invoke = async (method, payload) => {
    calls.push({ method, payload });
    return { profile: { ...circle, parameters: payload.parameters, profileScope: "system", profileDefinitionId: "oval" } };
  };
  await handleDesignerAreaAction(context, view, "tube-designer-profile-parameter-change", {
    dataset: { tubeDesignerProfileMode: "add", tubeDesignerProfilePrefix: "frame", tubeDesignerProfileParameter: "width" }, value: "52",
  }, { renderProject() {}, showNotice() {} });
  assert.deepEqual(calls[0].payload, { profileRef: { scope: "system", id: "oval" }, parameters: { width: 52 } });
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.parameters.width, 52);
  html = renderDesignerAddParameterContent(view.scene.tubeDesigner, view);
  assert.match(html, /value="system:oval" selected/);
  assert.match(html, /管型参数示意图/);
});

await test("template resource defaults initialise a referenced profile without duplicating its fields", async () => {
  const { view, context, calls, productAct } = productHarness();
  view.scene.tubeDesigner.templates[0].extensions = {
    resourceRoles: {
      profiles: {
        frame: {
          parameter: "frameProfileType",
          defaultParametersByResource: { "system:round": { width: 52 } },
        },
      },
    },
  };
  context.productProxy.invoke = async (method, payload) => {
    calls.push({ method, payload });
    return { profile: { ...circle, parameters: payload.parameters, profileScope: "system", profileDefinitionId: "round" } };
  };
  await productAct("profile-selection-change", { value: "system:round" });
  assert.deepEqual(calls.at(-1).payload, {
    profileRef: { scope: "system", id: "round" }, parameters: { width: 52 },
  });
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerProfileOverrides.frame.parameters.width, 52);
});

await test("product tool roles select constrained library tools and persist role bindings", async () => {
  const { view, context, ops } = productHarness();
  const field = {
    key: "cornerGrooveTool", displayName: "转角开槽模具", type: "select",
    valueType: "string", defaultValue: "",
    presentation: {
      editor: "tool-library", resourceRole: "cornerGroove",
      targets: ["part"], categories: ["slot"],
    },
  };
  view.scene.tubeDesigner.templates[0] = {
    ...view.scene.tubeDesigner.templates[0],
    parameters: [{
      key: "frameProfileType", displayName: "外框管型", type: "select",
      valueType: "string", defaultValue: "rect", options: [{ value: "rect", label: "矩形管" }],
      presentation: { editor: "profile-library", resourceRole: "frame" },
    }, field],
    extensions: {
      addDialog: { structureParameters: [field.key] },
      resourceRoles: {
        profiles: { frame: { parameter: "frameProfileType" } },
        tools: { cornerGroove: {
          parameter: field.key, targetProfileRole: "frame",
          defaultParametersByResource: { "system:v-notch-sharp": { angle: 88 } },
        } },
      },
    },
  };
  view.tubeDesignerSystemPunchTools = [
    {
      id: "v-notch-sharp", version: "3.0.1", digest: "slot-digest",
      displayName: "V槽", kind: "programmatic", target: "part", category: "槽口",
      parameters: [
        { key: "angle", displayName: "折弯开口角", valueType: "number", defaultValue: 90, min: 1, max: 170 },
        { key: "derivedWall", displayName: "实测壁厚", valueType: "number", defaultValue: 0, derived: true },
      ],
      defaultParameters: { angle: 90, derivedWall: 0 },
      parameterDiagram: { schemaVersion: 2, viewBox: "0 0 24 16", paths: ["M2 2 L22 14"], labels: [] },
    },
    { id: "circle", version: "1", displayName: "圆孔", kind: "programmatic", target: "side", category: "孔型", parameters: [] },
  ];
  let html = renderDesignerAddParameterContent(view.scene.tubeDesigner, view);
  assert.match(html, /value="system:v-notch-sharp"/);
  assert.doesNotMatch(html, /value="system:circle"/);

  await handleDesignerAreaAction(context, view, "tube-designer-product-tool-selection-change", {
    dataset: { tubeDesignerToolMode: "add", tubeDesignerToolField: field.key, tubeDesignerToolRole: "cornerGroove" },
    value: "system:v-notch-sharp",
  }, ops);
  const binding = view.tubeDesignerAddDraft.tubeDesignerToolBindings.cornerGroove;
  assert.deepEqual(binding.ref, { scope: "system", id: "v-notch-sharp", version: "3.0.1", digest: "slot-digest" });
  assert.equal(binding.parameters.angle, 88, "template role defaults initialise the selected resource");
  assert.equal(binding.snapshot.targetProfileRole, "frame");
  assert.equal(view.tubeDesignerAddDraft.cornerGrooveTool, "system:v-notch-sharp");
  html = renderDesignerAddParameterContent(view.scene.tubeDesigner, view);
  assert.match(html, /模具参数/);
  assert.match(html, /槽口参数示意图/);
  assert.doesNotMatch(html, /实测壁厚/);

  await handleDesignerAreaAction(context, view, "tube-designer-product-tool-parameter-change", {
    dataset: { tubeDesignerToolMode: "add", tubeDesignerToolField: field.key, tubeDesignerToolRole: "cornerGroove", tubeDesignerToolParameter: "angle" },
    value: "72",
  }, ops);
  assert.equal(view.tubeDesignerAddDraft.tubeDesignerToolBindings.cornerGroove.parameters.angle, 72);
  const preset = getReusablePresetValues(view.scene.tubeDesigner.templates[0], view.tubeDesignerAddDraft);
  assert.equal(preset.tubeDesignerToolBindings.cornerGroove.parameters.angle, 72);
  const restored = applyReusablePresetValues(view.scene.tubeDesigner.templates[0], {}, preset);
  assert.equal(restored.tubeDesignerToolBindings.cornerGroove.ref.id, "v-notch-sharp");
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

await test("parameter edits regenerate only the model and retain camera and pane positions", async () => {
  const {view,context,ops}=harness();
  view.tubeDesignerSelectedProfileId='system:round';
  let fits=0, standards=0, renders=0, applied={revision:'profile-preview:system:round:1',entityIds:['system:round']};
  view.viewport.getAppliedViewState=()=>applied;
  view.viewport.fitViewForRevision=()=>fits++;
  view.viewport.setStandardView=()=>standards++;
  view.viewport.applyViewSnapshot=async snapshot=>{applied={...snapshot,entityIds:['system:round']};return {applied:true,entityIds:['system:round']};};
  context.sceneProxy.invoke=async()=>({geometryResourceId:'resource://profile',geometryResourceVersion:2});
  ops.renderProject=()=>renders++;
  const parent={scrollTop:270,scrollLeft:0,parentElement:null};
  const input={value:'48',dataset:{tubeProfileEditorParameter:'width',tubeProfileValueType:'number',profileParameterKey:'width'}};
  const list={querySelectorAll:()=>[input],set innerHTML(_) {throw Error('unchanged controls must survive');}};
  const editor={dataset:{tubeDesignerProfileId:'round',tubeDesignerProfileKey:'system:round',tubeDesignerProfileScope:'system'},
    scrollTop:180,scrollLeft:0,parentElement:parent,
    querySelectorAll:()=>[input],querySelector:s=>s==='.tube-profile-library-parameter-list'?list:null};
  await handleProfileLibraryAction(context,view,'tube-designer-profile-preview-change',{closest:()=>editor},ops);
  await tick();await tick();
  assert.equal(view.tubeDesignerProfileDrafts['system:round'].parameters.width,48);
  assert.equal(applied.revision,'profile-preview:system:round:2');
  assert.equal(renders,0);assert.equal(fits,0);assert.equal(standards,0);
  assert.equal(editor.scrollTop,180);assert.equal(parent.scrollTop,270);
});

await test("profile resource page uses a floating section diagram, not scene annotations", async () => {
  const {view,context,ops}=harness();
  view.tubeDesignerSelectedProfileId='system:round';
  assert.doesNotMatch(renderProfileLibraryRightPane(context,view),/data-profile-library-diagram/);
  assert.match(renderProfileLibraryRightPane(context,view),/data-profile-diagram-toggle/);
  const collapsedHtml=renderProfileLibraryViewportOverlay({},view);
  assert.match(collapsedHtml,/data-tube-profile-diagram-dock/);
  assert.match(collapsedHtml,/data-floating-diagram-drag/);
  assert.doesNotMatch(collapsedHtml,/data-profile-diagram-toggle/);
  assert.doesNotMatch(collapsedHtml,/data-tube-designer-specification-tree/);
  assert.doesNotMatch(collapsedHtml,/data-tube-profile-scene-runtime/);
});

await test("fixed profiles do not expose editable diagram controls", async () => {
  const {view,context}=harness();
  view.tubeDesignerSystemProfiles=[{...packaged,id:"fixed",profileForm:"fixed",profileType:"fixed-section"}];
  view.tubeDesignerSelectedProfileId='system:fixed';
  const html=renderProfileLibraryRightPane(context,view);
  assert.doesNotMatch(html,/data-profile-diagram-toggle/);
});

console.log(`${completed.length} profile library tab tests passed.`);
