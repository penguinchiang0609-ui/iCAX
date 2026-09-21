import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  libraryTools,
  renderToolLibraryLeftPane,
  renderToolLibraryRightPane,
  renderToolLibraryViewportOverlay,
  toolLibraryState,
  visibleLibraryTools,
  buildToolLibraryPreviewPayload,
  toolTubeProfileEvaluationKey,
  toolPreviewKey,
  applyToolLibraryPreview,
  ensureToolLibraryCatalogue,
  handleToolLibraryAction,
} from "../../apps/tube-designer/webpage/toolLibrary.mjs";
import { buildPunchPreviewRows } from "../../apps/tube-designer/webpage/punchEditor.mjs";

const view = {
  tubeDesignerSystemPunchTools: [
    { id: "v-notch-sharp", displayName: "V 槽", kind: "programmatic", category: "槽口", version: "1.0.0", parameters: [{ key: "angle", displayName: "V 槽夹角" }] },
    { id: "fixed-fixture", displayName: "测试定式刀具", kind: "fixed", category: "测试", version: "1.0.0", parameters: [] },
  ],
  tubeDesignerTemplatePunchTools: [
    { id: "template-v", displayName: "模板 V 槽", kind: "programmatic", libraryScope: "template", templateId: "guardrail", templateName: "护栏模板" },
  ],
  tubeDesignerUserData: {
    punchTools: [{ id: "user-v", name: "我的 V 槽", kind: "fixed", libraryScope: "user" }],
  },
};

// New catalogue packages use the same generic UI and preview payload path.
for (const id of ["embedded-arc-notch", "segmented-bend"]) {
  const tool = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/mold/${id}/tool.json`, import.meta.url), "utf8"));
  tool.defaultParameters = Object.fromEntries(tool.parameters.map(p => [p.key, p.defaultValue]));
  const packageView = { tubeDesignerSystemPunchTools: [tool],
    tubeDesignerToolLibrary: { scope: "system", selectedKey: `system::${id}`, showToolDiagram: true } };
  assert.match(renderToolLibraryLeftPane({}, packageView), new RegExp(tool.displayName));
  const html = renderToolLibraryRightPane({}, packageView) + renderToolLibraryViewportOverlay({}, packageView);
  assert.match(html, /tool-parameter-svg/);
  assert.ok(html.indexOf("tube-tool-library-mold-parameters") < html.indexOf("tube-tool-library-parameter-diagram"));
  const feature = buildToolLibraryPreviewPayload(packageView, libraryTools(packageView)[0]).features[0];
  assert.equal(feature.toolRef.id, id);
  assert.equal(feature.toolParameters.angle, 90);
  if (id === "embedded-arc-notch") {
    assert.equal(feature.toolParameters.arcRadius, 10);
    assert.equal(Object.hasOwn(feature.toolParameters, "bendRadius"), false);
  } else {
    assert.equal(feature.toolParameters.bendRadius, 40);
  }
  assert.equal(feature.toolParameters.rootClearance, 1);
  if (id === "embedded-arc-notch") {
    assert.equal(feature.toolParameters.maleFemale, false);
    assert.doesNotMatch(html, /公母尺寸（mm）/);
    assert.doesNotMatch(html, /0 (?:自动取|按)壁厚/);
    tool.defaultParameters.maleFemale = true;
    const jointView = { tubeDesignerSystemPunchTools: [tool],
      tubeDesignerToolLibrary: { scope: "system", selectedKey: `system::${id}`, showToolDiagram: true } };
    const jointHtml = renderToolLibraryRightPane({}, jointView) + renderToolLibraryViewportOverlay({}, jointView);
    assert.match(jointHtml, /公母尺寸（mm）/);
    assert.doesNotMatch(jointHtml, /0 (?:自动取|按)壁厚/);
    assert.match(jointHtml, /公母台阶/);
    assert.equal(buildToolLibraryPreviewPayload(jointView, libraryTools(jointView)[0]).features[0].toolParameters.maleFemale, true);
  }
}

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
const diagramHtml = renderToolLibraryViewportOverlay({}, diagramView);
assert.match(diagramHtml, /tool-parameter-svg/);
assert.match(diagramHtml, /夹角 90°/);
assert.doesNotMatch(renderToolLibraryRightPane({}, diagramView), /data-tool-parameter-diagram/);
diagramView.tubeDesignerSystemPunchTools[0].parameterDiagram.variants = [
  { visibleWhen: { op: "eq", parameter: "angle", value: 45 }, labels: [{ x: 20, y: 20, text: "wrong-variant" }] },
  { visibleWhen: { op: "eq", parameter: "angle", value: 90 }, paths: ["M10 10 A20 20 0 0 0 30 30"], labels: [{ x: 20, y: 20, text: "declared-tangent-variant" }] },
];
const variantHtml = renderToolLibraryViewportOverlay({}, diagramView);
assert.match(variantHtml, /declared-tangent-variant/);
assert.doesNotMatch(variantHtml, /wrong-variant/);
assert.match(variantHtml, /M10 10 A20 20 0 0 0 30 30/);
view.tubeDesignerToolLibrary = { scope: "system", type: "all", category: "slot", search: "", selectedKey: "" };
assert.deepEqual(visibleLibraryTools(view).map((tool) => tool.id), ["v-notch-sharp", "fixed-fixture"]);
assert.match(renderToolLibraryLeftPane({}, view), /tube-tool-library-group/);
assert.match(renderToolLibraryLeftPane({}, view), /tube-tool-library-card-art/);
assert.doesNotMatch(renderToolLibraryLeftPane({}, view), /tube-tool-library-card-icon/);

view.tubeDesignerToolLibrary = { scope: "template", type: "all", search: "", selectedKey: "" };
const sourceTabsHtml = renderToolLibraryLeftPane({}, view);
assert.equal(toolLibraryState(view).scope, "system");
assert.equal(visibleLibraryTools(view).length, 2);
assert.match(sourceTabsHtml, /单件工艺库/);
assert.match(sourceTabsHtml, /系统内置/);
assert.doesNotMatch(sourceTabsHtml, /模板自带|模板 V 槽/);
assert.match(sourceTabsHtml, /我的/);
assert.doesNotMatch(renderToolLibraryRightPane({}, view), /标准拉伸体/);

// A mould preview is a placement-only blank plus a real extruded tool body.
// It must not invoke the boolean/result path while the library page is open.
const partTool = { id: "branch-profile", displayName: "支管相贯", kind: "programmatic", target: "part", requiresSection: true, category: "冲孔", version: "1.0.0", libraryScope: "system", defaultParameters: { angle: 90, azimuth: 0 } };
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
assert.deepEqual(buildPunchPreviewRows({
  baseGeometry: { url: "resource:blank", version: 1 },
  toolPreviews: [],
  toolGeometry: { url: "resource:combined-tool", version: 1 },
}).map(row=>row.entityId), ["punch-preview-blank", "punch-preview-tools"],
"An empty per-tool list must fall back to the valid combined tool geometry.");

// The mould preview owns a selectable tube profile.  Its values are sent
// together with the mould recipe so changing either side invalidates the
// previous scene instead of silently reusing the old blank.
const profileForTool = {
  id: "round", name: "圆管", libraryScope: "system", profileForm: "parametric", profileType: "parametric-package",
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
assert.match(profilePane, /工艺参数/);
assert.match(profilePane, /目标管型/);
assert.doesNotMatch(profilePane, /tube-tool-library-tube-summary[^>]*>[\s\S]*?<small>/);
assert.match(profilePane, /tube-designer-tool-library-profile-parameter-change/);
assert.doesNotMatch(profilePane, /data-profile-parameter-diagram|data-tool-parameter-diagram/);
assert.match(renderToolLibraryViewportOverlay({}, profilePreviewView), /目标管型示意图/);
assert.match(profilePane, /tube-profile-library-parameter-section/);
assert.doesNotMatch(profilePane, /单件工艺信息|工艺 ID|几何与加工边界/);
assert.match(profilePane, /id="tube-tool-library-main-tube-content" hidden/);
assert.doesNotMatch(profilePane, /主管预览长度/);
assert.doesNotMatch(profilePane, /标准拉伸体/);
assert.doesNotMatch(profilePane, /data-profile-library-diagram/);

// A branch mould gets its own section profile editor.  Placement values such
// as angle/azimuth are intentionally absent here; they belong to the punch
// operation, while this page controls the branch tube cross-section.
const branchProfile = {
  id: "rect", name: "矩形管", libraryScope: "system", profileForm: "parametric", profileType: "parametric-package",
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
assert.match(branchPane, /刀具截面/);
assert.match(branchPane, /截面管型/);
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
  dataset: { tubeToolLibraryDiagram: "profile" },
}, { renderProject() { branchRenders += 1; } });
assert.equal(branchRenders, 0);
assert.equal(branchView.tubeDesignerToolLibrary.showProfileDiagram, true);
const expandedBranchPane = renderToolLibraryRightPane({}, branchView);
const branchEditor = expandedBranchPane.slice(expandedBranchPane.indexOf('tube-tool-library-branch-section'));
assert.doesNotMatch(branchEditor, /data-profile-library-diagram/);
assert.match(branchEditor, /data-tube-tool-library-diagram="profile"/);
assert.match(renderToolLibraryViewportOverlay({}, branchView), /data-parameter-diagram-for="tool-library-profile:branch"/);
await handleToolLibraryAction({}, branchView, "tube-designer-tool-library-profile-parameter-change", {
  value: "72", dataset: { tubeToolLibraryProfileKey: "system:rect", tubeToolLibraryProfileParameter: "width", tubeToolLibraryProfileRole: "branch" },
}, { renderProject() {} });
assert.equal(branchView.tubeDesignerToolLibrary.branchProfileDrafts["system:rect"].width, 72);
assert.equal(branchView.tubeDesignerToolLibrary.profileDrafts?.["system:rect"], undefined);
let diagramRenders = 0;
await handleToolLibraryAction({}, profilePreviewView, "tube-designer-tool-library-toggle-diagram", {
  dataset: { tubeToolLibraryDiagram: "profile" },
}, { renderProject() { diagramRenders += 1; } });
assert.equal(diagramRenders, 0);
assert.equal(profilePreviewView.tubeDesignerToolLibrary.showProfileDiagram, true);
assert.match(renderToolLibraryViewportOverlay({}, profilePreviewView), /data-parameter-diagram-for="tool-library-profile:main"/);
const expandedMainPane = renderToolLibraryRightPane({}, profilePreviewView);
assert.doesNotMatch(expandedMainPane, /data-profile-library-diagram/);
const beforeCollapseKey = toolPreviewKey(profilePreviewView, partTool);
const beforeCollapseDrafts = structuredClone(profilePreviewView.tubeDesignerToolLibrary.profileDrafts);
assert.equal(profilePreviewView.tubeDesignerToolLibrary.mainTubeCollapsed, true);
assert.match(expandedMainPane, /id="tube-tool-library-main-tube-content" hidden/);
await handleToolLibraryAction({}, profilePreviewView, "tube-designer-tool-library-toggle-main-tube", {}, { renderProject() {} });
assert.equal(profilePreviewView.tubeDesignerToolLibrary.mainTubeCollapsed, false);
assert.doesNotMatch(renderToolLibraryRightPane({}, profilePreviewView), /id="tube-tool-library-main-tube-content" hidden/);
assert.equal(toolPreviewKey(profilePreviewView, partTool), beforeCollapseKey);
assert.deepEqual(profilePreviewView.tubeDesignerToolLibrary.profileDrafts, beforeCollapseDrafts);
await handleToolLibraryAction({}, profilePreviewView, "tube-designer-tool-library-select", {
  dataset: { tubeToolLibraryKey: "system::branch-profile" },
}, { renderProject() {} });
assert.equal(profilePreviewView.tubeDesignerToolLibrary.showProfileDiagram, true);
assert.equal(profilePreviewView.tubeDesignerToolLibrary.mainTubeCollapsed, false);
await handleToolLibraryAction({}, profilePreviewView, "tube-designer-tool-library-toggle-main-tube", {}, { renderProject() {} });
assert.equal(profilePreviewView.tubeDesignerToolLibrary.mainTubeCollapsed, true);
assert.match(renderToolLibraryRightPane({}, profilePreviewView), /id="tube-tool-library-main-tube-content" hidden/);
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
assert.equal(selectedProfilePayload.length, 500);
assert.equal(selectedProfilePayload.features[0].reference, "center");
assert.equal(selectedProfilePayload.features[0].station, 0);
assert.equal(selectedProfilePayload.features[0].layoutDatum, "base");
assert.deepEqual(selectedProfilePayload.features[0].section.profile.contours, profileForTool.previewProfile.contours);

// The floating主管 diagram uses the current evaluated profile rather than
// drawing the package's default preview with only updated number labels.
profilePreviewView.tubeDesignerToolLibrary.profileSnapshots = {
  main: {
    key: toolTubeProfileEvaluationKey(profilePreviewView, profileForTool, "main"),
    snapshot: {
      ...profileForTool.previewProfile,
      contours: [{ kind: "circle", center: [7, -3], radius: 20 }, { kind: "circle", center: [7, -3], radius: 18 }],
      parameterDiagram: { schemaVersion: 1, annotations: [{ parameter: "width", kind: "linear", axis: "x", side: "top", from: [-13, 17], to: [27, 17] }] },
    },
  },
};
const evaluatedProfileDiagram = renderToolLibraryViewportOverlay({}, profilePreviewView);
assert.match(evaluatedProfileDiagram, /cx="7" cy="-3"/);

const liveTool = { id: "live-hole", displayName: "联动孔", kind: "programmatic", target: "side", category: "孔型", parameters: [] };
const liveView = {
  activeAreaId: "tools",
  tubeDesignerSystemPunchTools: [liveTool],
  tubeDesignerSystemProfiles: [profileForTool],
  tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::live-hole", profileDrafts: { "system:round": { width: 44, wallThickness: 2 } }, showProfileDiagram: true },
  viewport: {
    async applyViewSnapshot(snapshot) { return { applied: true, entityIds: snapshot.rows.map(row => row.entityId), missingGeometryEntityIds: [] }; },
    setStandardView() {}, fitViewToViewport() {}, setVisibleEntityIds() {},
  },
};
const evaluatedRequests = [];
const liveContext = { sceneProxy: {
  resources: {},
  async invoke(method, payload) {
    if (method === "TubeDesigner.EvaluateProfilePackage") {
      evaluatedRequests.push(payload);
      return { profile: {
        ...profileForTool.previewProfile,
        contours: [{ kind: "circle", center: [9, -4], radius: 22 }, { kind: "circle", center: [9, -4], radius: 20 }],
        parameterDiagram: { schemaVersion: 1, annotations: [{ parameter: "width", kind: "linear", axis: "x", side: "top", from: [-13, 18], to: [31, 18] }] },
      } };
    }
    if (method === "TubeDesigner.PreviewPunchWizard") return {
      baseGeometry: { url: "resource:live-blank", version: 1 },
      baseMaterial: { url: "resource:live-blank-material", version: 1 },
      toolMaterial: { url: "resource:live-tool-material", version: 1 },
      toolPreviews: [{ target: "feature", key: "library-preview", geometry: { url: "resource:live-tool", version: 1 } }],
    };
    throw new Error(`unexpected method: ${method}`);
  },
} };
renderToolLibraryViewportOverlay(liveContext, liveView);
const liveRequest = liveView.tubeDesignerToolLibrary.previewRequest;
assert.ok(liveRequest);
await liveRequest.promise;
assert.deepEqual(evaluatedRequests[0].parameters, { width: 44, wallThickness: 2 });
assert.match(renderToolLibraryViewportOverlay({}, liveView), /cx="9" cy="-4"/);

// Side-hole templates explicitly declare the target cross-section as an input.
// Shape, blind-hole depth and opposite punching are resource parameters; placement is
// still supplied only by the consuming workflow.
const circleTool = JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/mold/circle/tool.json", import.meta.url), "utf8"));
circleTool.defaultParameters = Object.fromEntries(circleTool.parameters.map(parameter => [parameter.key, parameter.defaultValue]));
circleTool.defaultOperationParameters = Object.fromEntries(circleTool.operationParameters.map(parameter => [parameter.key, parameter.defaultValue]));
const circleView = {
  tubeDesignerSystemPunchTools: [circleTool],
  tubeDesignerSystemProfiles: [profileForTool],
  tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::circle" },
};
const circleHtml = renderToolLibraryRightPane({}, circleView);
assert.match(circleHtml, /盲孔/);
assert.doesNotMatch(circleHtml, /孔深/);
assert.match(circleHtml, /对冲孔/);
assert.match(circleHtml, /目标管型/);
let circlePayload = buildToolLibraryPreviewPayload(circleView, libraryTools(circleView)[0]);
assert.equal(circlePayload.features[0].blindHole, false);
assert.equal(circlePayload.features[0].cutDepth, 5);
assert.equal(circlePayload.features[0].opposite, false);
await handleToolLibraryAction({}, circleView, "tube-designer-tool-library-operation-parameter-change", {
  value: "on", checked: true,
  dataset: { tubeToolLibraryKey: "system::circle", tubeToolLibraryOperationParameter: "blindHole" },
}, { renderProject() {} });
assert.match(renderToolLibraryRightPane({}, circleView), /孔深/);
await handleToolLibraryAction({}, circleView, "tube-designer-tool-library-operation-parameter-change", {
  value: "8", checked: false,
  dataset: { tubeToolLibraryKey: "system::circle", tubeToolLibraryOperationParameter: "cutDepth" },
}, { renderProject() {} });
await handleToolLibraryAction({}, circleView, "tube-designer-tool-library-operation-parameter-change", {
  value: "on", checked: true,
  dataset: { tubeToolLibraryKey: "system::circle", tubeToolLibraryOperationParameter: "opposite" },
}, { renderProject() {} });
circlePayload = buildToolLibraryPreviewPayload(circleView, libraryTools(circleView)[0]);
assert.equal(circlePayload.features[0].blindHole, true);
assert.equal(circlePayload.features[0].cutDepth, 8);
assert.equal(circlePayload.features[0].opposite, true);

view.tubeDesignerToolLibrary.scope = "user";
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

// Grouping follows operation semantics. The compact parameter panel does not
// repeat long catalogue/process notes beside every editor.
const semanticsView = { tubeDesignerSystemPunchTools: [
  { id: "arbitrary-hole", displayName: "测试孔", target: "side", kind: "programmatic", category: "孔型",
    inputs: [{ key: "targetSection", valueType: "profile", required: true }], description: "<script>unsafe</script>" },
  { id: "arbitrary-joint", target: "part", requiresSection: true, category: "冲孔" },
  { id: "arbitrary-end", target: "end", category: "端面" },
  { id: "arbitrary-notch", target: "part", category: "槽口" },
] };
const groupsHtml = renderToolLibraryLeftPane({}, semanticsView);
assert.equal((groupsHtml.match(/data-tube-tool-library-group=/g) ?? []).length, 3);
assert.match(groupsHtml, /冲孔/);
assert.doesNotMatch(groupsHtml, /data-tube-tool-library-group="branch"/);
for (const id of ["arbitrary-hole", "arbitrary-end", "arbitrary-notch"]) {
  toolLibraryState(semanticsView).selectedKey = `system::${id}`;
  const html = renderToolLibraryRightPane({}, semanticsView);
  assert.match(html, /工艺参数/);
  if (id === "arbitrary-hole") assert.match(html, /目标管型/);
  else assert.doesNotMatch(html, /目标管型/);
  assert.doesNotMatch(html, /几何与加工边界|不是激光工艺合格结论/);
  assert.doesNotMatch(html, /<script>unsafe/);
}
console.log("TubeDesigner tool library tests passed");
