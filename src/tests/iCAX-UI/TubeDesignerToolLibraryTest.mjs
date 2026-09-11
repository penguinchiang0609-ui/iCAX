import assert from "node:assert/strict";
import {
  libraryTools,
  renderToolLibraryLeftPane,
  renderToolLibraryRightPane,
  toolLibraryState,
  visibleLibraryTools,
  buildToolLibraryPreviewPayload,
  toolPreviewKey,
  applyToolLibraryPreview,
  ensureToolLibraryCatalogue,
  handleToolLibraryAction,
} from "../../apps/tube-designer/webpage/toolLibrary.mjs";

const view = {
  tubeDesignerSystemPunchTools: [
    { id: "v-notch-sharp", displayName: "V 槽", kind: "programmatic", category: "槽口", version: "1.0.0", parameters: [{ key: "angle", displayName: "V 槽夹角" }] },
    { id: "diamond-12", displayName: "菱形孔", kind: "fixed", category: "孔", version: "1.0.0", parameters: [] },
  ],
  tubeDesignerTemplatePunchTools: [
    { id: "template-v", displayName: "模板 V 槽", kind: "programmatic", libraryScope: "template", templateId: "guardrail", templateName: "护栏模板" },
  ],
  tubeDesignerUserData: {
    punchTools: [{ id: "user-v", name: "我的 V 槽", kind: "fixed", libraryScope: "user" }],
  },
};

assert.equal(libraryTools(view).length, 4);
assert.equal(toolLibraryState(view).scope, "system");
assert.equal(visibleLibraryTools(view).length, 2);
assert.equal(visibleLibraryTools(view).filter((tool) => tool.kind === "fixed").length, 1);
const independentVView = {
  tubeDesignerSystemPunchTools: [
    { id: "v-notch-sharp", displayName: "V 槽", kind: "programmatic", target: "part", category: "槽口", version: "1.0.0", parameters: [{ key: "angle" }] },
    { id: "edge-arc-groove", displayName: "边弧槽", kind: "programmatic", target: "part", category: "槽口", version: "1.2.0", parameters: [{ key: "angle" }, { key: "leftArc" }] },
  ],
};
const independentVTools = libraryTools(independentVView);
assert.deepEqual(independentVTools.map((tool) => tool.id), ["v-notch-sharp", "edge-arc-groove"]);
assert.ok(independentVTools.every((tool) => !tool.runtimeId && !tool.variantCatalog));
assert.equal(independentVTools.find((tool) => tool.id === "edge-arc-groove").parameters.some((item) => item.key === "leftArc"), true);
const sharpPayload = buildToolLibraryPreviewPayload({
  ...independentVView,
  tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::v-notch-sharp" },
}, independentVTools[0]);
assert.deepEqual(sharpPayload.features[0].toolRef.id, "v-notch-sharp");
assert.equal(sharpPayload.features[0].toolParameters.style, undefined);
const diagramView = {
  ...independentVView,
  tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::v-notch-sharp", showToolDiagram: true },
  tubeDesignerSystemPunchTools: [{
    id: "v-notch-sharp", displayName: "V 槽", kind: "programmatic", target: "part", category: "槽口", version: "1.0.0",
    parameters: [{ key: "angle", displayName: "V 槽夹角", defaultValue: 90 }],
    illustration: { viewBox: "0 0 48 48", paths: ["M6 9 H42", "M6 9 L24 38 L42 9"] },
    parameterDiagram: { viewBox: "0 0 240 160", paths: ["M24 28 H216", "M24 28 L120 124 L216 28"], labels: [{ x: 120, y: 94, text: "夹角", parameter: "angle", unit: "°" }] },
  }],
};
const diagramHtml = renderToolLibraryRightPane({}, diagramView);
assert.match(diagramHtml, /tool-parameter-svg/);
assert.match(diagramHtml, /夹角 90°/);
view.tubeDesignerToolLibrary = { scope: "system", type: "all", category: "slot", search: "", selectedKey: "" };
assert.deepEqual(visibleLibraryTools(view).map((tool) => tool.id), ["v-notch-sharp", "diamond-12"]);
assert.match(renderToolLibraryLeftPane({}, view), /tube-tool-library-group/);
assert.match(renderToolLibraryLeftPane({}, view), /tube-tool-library-card-art/);
assert.doesNotMatch(renderToolLibraryLeftPane({}, view), /tube-tool-library-card-icon/);

view.tubeDesignerToolLibrary = { scope: "template", type: "all", search: "", selectedKey: "" };
assert.equal(visibleLibraryTools(view).length, 1);
assert.match(renderToolLibraryLeftPane({}, view), /模具库/);
assert.match(renderToolLibraryLeftPane({}, view), /系统内置/);
assert.match(renderToolLibraryLeftPane({}, view), /模板自带/);
assert.match(renderToolLibraryLeftPane({}, view), /我的/);
assert.match(renderToolLibraryLeftPane({}, view), /模板 V 槽/);
assert.doesNotMatch(renderToolLibraryRightPane({}, view), /标准拉伸体/);

// A mould preview is a placement-only blank plus a real extruded tool body.
// It must not invoke the boolean/result path while the library page is open.
const partTool = { id: "branch-profile", displayName: "支管相贯", kind: "programmatic", target: "part", requiresSection: true, category: "支管", version: "1.0.0", libraryScope: "system", defaultParameters: { angle: 90, azimuth: 0 } };
const previewView = { activeAreaId: "tools", tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::branch-profile", previewRequest: null } };
const previewPayload = buildToolLibraryPreviewPayload(previewView, { ...partTool, libraryKey: "system::branch-profile" });
assert.equal(previewPayload.toolsOnly, true);
assert.equal(previewPayload.features[0].toolTarget, "part");
assert.ok(previewPayload.features[0].section?.profile?.contours?.length);
const endPayload = buildToolLibraryPreviewPayload(previewView, { id: "end-profile", target: "end", requiresSection: true, version: "1.0.0", libraryKey: "system::end-profile" });
assert.ok(endPayload.ends.start.section?.profile?.contours?.length);
const previewRequest = {};
previewView.tubeDesignerToolLibrary.previewRequest = previewRequest;
const appliedSnapshots = [];
previewView.viewport = {
  async applyViewSnapshot(snapshot) { appliedSnapshots.push(snapshot); return { applied: true, entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [] }; },
  setStandardView() {},
  fitViewToViewport() {},
};
await applyToolLibraryPreview({}, previewView, { ...partTool, libraryKey: "system::branch-profile" }, {
  baseGeometry: { url: "resource:blank", version: 1 },
  baseMaterial: { url: "resource:blank-material", version: 1 },
  toolMaterial: { url: "resource:tool-material", version: 1 },
  toolPreviews: [{ target: "feature", key: "library-preview", geometry: { url: "resource:tool", version: 1 } }],
}, toolPreviewKey(previewView, { ...partTool, libraryKey: "system::branch-profile" }), previewRequest);
assert.equal(appliedSnapshots[0].rows.length, 2);
assert.equal(appliedSnapshots[0].rows[1].data.renderClass, 5);
assert.equal(previewView.preserveCustomViewportEntities, true);

// The mould preview owns a selectable tube profile.  Its values are sent
// together with the mould recipe so changing either side invalidates the
// previous scene instead of silently reusing the old blank.
const profileForTool = {
  id: "round", name: "圆管", libraryScope: "system", profileType: "parametric-package",
  specification: "圆管 · 40 × 2", defaultParameters: { width: 40, wallThickness: 2 },
  descriptor: { parameters: [
    { key: "width", displayName: { "zh-CN": "外径", "en-US": "Outside diameter" }, valueType: "number", defaultValue: 40, min: 1 },
    { key: "wallThickness", displayName: { "zh-CN": "壁厚", "en-US": "Wall thickness" }, valueType: "number", defaultValue: 2, min: 0.1 },
  ] },
  previewProfile: { kind: "round", contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }] },
};
const profilePreviewView = {
  activeAreaId: "tools",
  tubeDesignerSystemPunchTools: [partTool],
  tubeDesignerSystemProfiles: [profileForTool],
  tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::branch-profile", previewLength: 600 },
};
const profilePane = renderToolLibraryRightPane({}, profilePreviewView);
assert.match(profilePane, /主管 \/ 管型参数/);
assert.match(profilePane, /tube-designer-tool-library-profile-parameter-change/);
assert.match(profilePane, /管型参数示意图/);
assert.match(profilePane, /参数示意图/);
assert.match(profilePane, /显示管型参数示意图/);
assert.match(profilePane, /tube-tool-library-mold-section/);
assert.match(profilePane, /模具信息/);
assert.doesNotMatch(profilePane, /标准拉伸体/);
assert.doesNotMatch(profilePane, /data-profile-library-diagram/);

// A branch mould gets its own section profile editor.  Placement values such
// as angle/azimuth are intentionally absent here; they belong to the punch
// operation, while this page controls the branch tube cross-section.
const branchProfile = {
  id: "rect", name: "矩形管", libraryScope: "system", profileType: "parametric-package",
  specification: "矩形管 · 40 × 20 × 2", defaultParameters: { width: 40, depth: 20, wallThickness: 2 },
  descriptor: { parameters: [
    { key: "width", displayName: "宽度", valueType: "number", defaultValue: 40, min: 1 },
    { key: "depth", displayName: "深度", valueType: "number", defaultValue: 20, min: 1 },
    { key: "wallThickness", displayName: "壁厚", valueType: "number", defaultValue: 2, min: 0.1 },
  ] },
  previewProfile: { kind: "rectangle", contours: [{ kind: "rectangle", width: 40, height: 20 }, { kind: "rectangle", width: 36, height: 16 }] },
};
const branchView = {
  activeAreaId: "tools",
  tubeDesignerSystemPunchTools: [partTool],
  tubeDesignerSystemProfiles: [profileForTool, branchProfile],
  tubeDesignerToolLibrary: {
    scope: "system", selectedKey: "system::branch-profile", branchProfileKey: "system:rect",
    branchProfileDrafts: { "system:rect": { width: 60, depth: 30, wallThickness: 3 } }, previewLength: 500,
  },
};
const branchPane = renderToolLibraryRightPane({}, branchView);
assert.match(branchPane, /支管 \/ 管型参数/);
assert.match(branchPane, /支管管型/);
assert.match(branchPane, /data-tube-tool-library-profile-role="branch"/);
assert.match(branchPane, /宽度/);
assert.doesNotMatch(branchPane, /与主管轴夹角/);
assert.doesNotMatch(branchPane, /绕主管轴方位/);
const branchPayload = buildToolLibraryPreviewPayload(branchView, partTool);
assert.deepEqual(branchPayload.features[0].section.profileRef, { scope: "system", id: "rect" });
assert.deepEqual(branchPayload.features[0].section.parameters, { width: 60, depth: 30, wallThickness: 3 });
assert.deepEqual(branchPayload.features[0].section.profile.contours, branchProfile.previewProfile.contours);
let branchRenders = 0;
await handleToolLibraryAction({}, branchView, "tube-designer-tool-library-toggle-diagram", {
  dataset: { tubeToolLibraryDiagram: "branch-profile" },
}, { renderProject() { branchRenders += 1; } });
assert.equal(branchRenders, 1);
assert.equal(branchView.tubeDesignerToolLibrary.showBranchProfileDiagram, true);
await handleToolLibraryAction({}, branchView, "tube-designer-tool-library-profile-parameter-change", {
  value: "72", dataset: { tubeToolLibraryProfileKey: "system:rect", tubeToolLibraryProfileParameter: "width", tubeToolLibraryProfileRole: "branch" },
}, { renderProject() {} });
assert.equal(branchView.tubeDesignerToolLibrary.branchProfileDrafts["system:rect"].width, 72);
assert.equal(branchView.tubeDesignerToolLibrary.profileDrafts?.["system:rect"], undefined);
let diagramRenders = 0;
await handleToolLibraryAction({}, profilePreviewView, "tube-designer-tool-library-toggle-diagram", {
  dataset: { tubeToolLibraryDiagram: "profile" },
}, { renderProject() { diagramRenders += 1; } });
assert.equal(diagramRenders, 1);
assert.equal(profilePreviewView.tubeDesignerToolLibrary.showProfileDiagram, true);
assert.match(renderToolLibraryRightPane({}, profilePreviewView), /data-profile-library-diagram/);
await handleToolLibraryAction({}, profilePreviewView, "tube-designer-tool-library-select", {
  dataset: { tubeToolLibraryKey: "system::branch-profile" },
}, { renderProject() {} });
assert.equal(profilePreviewView.tubeDesignerToolLibrary.showProfileDiagram, true);

// Selecting another mould keeps the already-applied tube/scene alive while
// the next tool mesh is requested. The replacement snapshot will update the
// shared blank row and the tool row without refitting the camera.
profilePreviewView.tubeDesignerToolLibrary.preview = { key: "previous-tool" };
profilePreviewView.tubeDesignerToolLibrary.previewApplied = true;
await handleToolLibraryAction({}, profilePreviewView, "tube-designer-tool-library-select", {
  dataset: { tubeToolLibraryKey: "system::branch-profile" },
}, { renderProject() {} });
assert.equal(profilePreviewView.tubeDesignerToolLibrary.preview, null);
assert.equal(profilePreviewView.tubeDesignerToolLibrary.previewApplied, true);
assert.equal(profilePreviewView.preserveCustomViewportEntities, true);
const selectedProfilePayload = buildToolLibraryPreviewPayload(profilePreviewView, { ...partTool, libraryKey: "system::branch-profile" });
assert.deepEqual(selectedProfilePayload.profileRef, { scope: "system", id: "round" });
assert.deepEqual(selectedProfilePayload.parameters, { width: 40, wallThickness: 2 });
assert.equal(selectedProfilePayload.length, 600);
assert.equal(selectedProfilePayload.features[0].reference, "center");
assert.equal(selectedProfilePayload.features[0].station, 0);
assert.equal(selectedProfilePayload.features[0].layoutDatum, "base");
assert.deepEqual(selectedProfilePayload.features[0].section.profile.contours, profileForTool.previewProfile.contours);

view.tubeDesignerToolLibrary.type = "fixed";
assert.equal(visibleLibraryTools(view).length, 1);
const toolCards = renderToolLibraryLeftPane({}, view);
assert.match(toolCards, /<small>程式<\/small>|<small>定式<\/small>/);
assert.doesNotMatch(toolCards, /<small>程式模具<\/small>|<small>定式模具<\/small>/);

const fixedProfileView = {
  tubeDesignerSystemPunchTools: [{
    id: "fixed-profile", displayName: "定式异形孔", kind: "fixed", category: "孔型",
    geometry: { contours: [{ kind: "polygon", points: [[-10, -6], [10, -6], [6, 6], [-6, 6]] }] },
  }],
};
const fixedProfileHtml = renderToolLibraryLeftPane({}, fixedProfileView);
assert.match(fixedProfileHtml, /tube-profile-library-svg/);
assert.match(fixedProfileHtml, /polygon class="outer"/);

// The catalogue must wait for the shared startup hydration gate. Otherwise
// GetPunchTools races the scene/user-data SDO and intermittently times out.
let releaseHydration;
const hydration = new Promise((resolve) => { releaseHydration = resolve; });
let gatedCalls = 0;
const gatedView = {
  activeAreaId: "tools",
  tubeDesignerUserDataSynchronizationPromise: hydration,
};
const gatedContext = {
  sceneProxy: {
    invoke: async (method) => {
      assert.equal(method, "TubeDesigner.GetPunchTools");
      gatedCalls += 1;
      return { tools: [{ id: "circle", displayName: "圆孔", category: "孔", kind: "programmatic" }] };
    },
  },
};
const gatedRequest = ensureToolLibraryCatalogue(gatedContext, gatedView, {});
await Promise.resolve();
assert.equal(gatedCalls, 0);
releaseHydration();
await gatedRequest;
assert.equal(gatedCalls, 1);
assert.equal(toolLibraryState(gatedView).catalogueStatus, "ready");

// An error is visible to the user, but a subsequent explicit refresh must be
// able to retry it. The repaint caused by the error itself must not loop.
let retryCalls = 0;
const retryView = { activeAreaId: "tools" };
const retryContext = {
  sceneProxy: {
    invoke: async () => {
      retryCalls += 1;
      if (retryCalls <= 2) throw new Error("temporary host timeout");
      return { tools: [{ id: "circle", displayName: "圆孔", category: "孔", kind: "programmatic" }] };
    },
  },
};
await ensureToolLibraryCatalogue(retryContext, retryView, {});
assert.equal(toolLibraryState(retryView).catalogueStatus, "error");
await ensureToolLibraryCatalogue(retryContext, retryView, {});
assert.equal(toolLibraryState(retryView).catalogueStatus, "ready");
assert.equal(retryCalls, 3);

console.log("TubeDesigner tool library tests passed");
