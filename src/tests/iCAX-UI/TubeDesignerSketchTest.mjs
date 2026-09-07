import assert from "node:assert/strict";

import {
  analyzeSectionDraft,
  beginNewSectionSketch,
  beginProfileSectionSketch,
  breakEntityAtPoint,
  buildProfileFromSectionDraft,
  createInitialSketchState,
  entitiesFromProfile,
  ensureSketchState,
  findSnapCandidate,
  finishCadCommand,
  handleCadPoint,
  handleSketchAreaAction,
  insertPointOnEntity,
  mergeSelectedEntities,
  renderSketchViewportOverlay,
  selectControlPointsInWindow,
  renderSketchRightPane,
  selectEntitiesInWindow,
  updateEntityFromGrip,
  validateSideSketchDraft,
} from "../../apps/tube-designer/webpage/sketchArea.mjs";

const line = (id, start, end) => ({
  id,
  kind: "line",
  x1: start[0],
  y1: start[1],
  x2: end[0],
  y2: end[1],
});
const metrics = { sampleStep: 0.01, snapTolerance: 0.25 };

{
  const state = { tool: "line", command: null };
  const draft = { entities: [], selectedId: "", selectedIds: [], history: [], future: [], dirty: false };
  handleCadPoint(state, draft, [0, 0], metrics);
  assert.equal(draft.entities.length, 0, "the first click starts a CAD command");
  handleCadPoint(state, draft, [20, 0], metrics);
  handleCadPoint(state, draft, [20, 10], metrics);
  assert.equal(draft.entities.length, 2, "subsequent clicks create a continuous line chain");
  assert.deepEqual([draft.entities[0].x2, draft.entities[1].x1], [20, 20]);
  finishCadCommand(state, draft, metrics);
  assert.equal(state.tool, "select");
  assert.equal(state.command, null);

  state.tool = "polyline";
  for (const point of [[0, 0], [30, 0], [30, 20], [0, 20], [0, 0]]) handleCadPoint(state, draft, point, metrics);
  const polyline = draft.entities.at(-1);
  assert.equal(polyline.kind, "polyline");
  assert.equal(polyline.closed, true, "clicking the first point closes a polyline");
}

{
  const draft = { entities: [
    line("horizontal", [0, 5], [10, 5]),
    line("vertical", [5, 0], [5, 10]),
  ] };
  assert.equal(findSnapCandidate(draft, [5.05, 5.04], 0.5).type, "intersection");
  assert.equal(findSnapCandidate({ entities: [line("edge", [0, 0], [10, 0])] }, [5, 0.1], 0.5).type, "midpoint");
}

{
  const entities = [line("inside", [2, 2], [8, 8]), line("crossing", [-2, 5], [4, 5]), line("outside", [20, 20], [30, 30])];
  assert.deepEqual(selectEntitiesInWindow(entities, [0, 0], [10, 10]), ["inside"], "left-to-right selection requires full containment");
  assert.deepEqual(selectEntitiesInWindow(entities, [10, 10], [0, 0]), ["inside", "crossing"], "right-to-left selection includes crossing objects");

  const selectedLine = line("grip", [0, 0], [10, 0]);
  updateEntityFromGrip(selectedLine, { role: "vertex", index: 1, original: structuredClone(selectedLine), start: [10, 0] }, [12, 4]);
  assert.deepEqual([selectedLine.x2, selectedLine.y2], [12, 4], "dragging an endpoint grip stretches the line");
}

{
  const view = { activeAreaId: "sketch", tubeDesignerSketch: createInitialSketchState(), scene: { tubeDesigner: {} } };
  const html = renderSketchViewportOverlay({}, view);
  assert.match(html, /F3 捕捉/);
  assert.match(html, /F8 正交/);
  assert.match(html, /data-tube-sketch-coordinate-status/);
  assert.match(html, /tabindex="0"/);
  assert.match(html, /无限模型空间/);
  const right = renderSketchRightPane({}, view);
  assert.doesNotMatch(right, /tube-designer-sketch-cancel-section/);
  assert.doesNotMatch(right, /tube-designer-sketch-delete-selected/);
  assert.doesNotMatch(right, /tube-designer-sketch-commit/);
}

{
  const draft = { entities: [
    line("bottom", [0, 0], [40, 0]),
    line("right", [40.1, 0], [40, 20]),
    line("top", [40, 20], [0, 20]),
    line("left", [0, 20], [0, 0]),
  ] };
  const analysis = analyzeSectionDraft(draft);
  assert.equal(analysis.ready, true, "line entities within the join tolerance form a section");
  assert.equal(analysis.loops.length, 1);
  const profile = buildProfileFromSectionDraft(draft, "直线闭合截面");
  assert.equal(profile.width, 40);
  assert.equal(profile.depth, 20);
  assert.equal(profile.contours[0].segments.length, 4);
}

{
  const draft = { entities: [
    line("bottom", [0, 0], [40, 0]),
    line("right", [41, 0], [40, 20]),
    line("top", [40, 20], [0, 20]),
    line("left", [0, 20], [0, 0]),
  ] };
  const analysis = analyzeSectionDraft(draft);
  assert.equal(analysis.ready, false);
  assert.match(analysis.message, /开放端点/);
}

{
  const crossed = { entities: [{
    id: "crossed",
    kind: "polyline",
    points: [[0, 0], [20, 20], [0, 20], [20, 0]],
    closed: true,
  }] };
  const analysis = analyzeSectionDraft(crossed);
  assert.equal(analysis.ready, false);
  assert.match(analysis.message, /自相交/);
}

{
  const nested = { entities: [
    { id: "outer", kind: "rectangle", x: -30, y: -20, width: 60, height: 40, closed: true },
    { id: "hole", kind: "circle", cx: 0, cy: 0, radius: 8, closed: true },
  ] };
  const analysis = analyzeSectionDraft(nested);
  assert.equal(analysis.ready, true);
  assert.equal(analysis.holeCount, 1);
  const profile = buildProfileFromSectionDraft(nested, "带孔截面");
  assert.equal(profile.contourCount, 2);
  assert.equal(profile.hollow, true);
  assert.ok(profile.wallThickness > 0);
  assert.equal(profile.contours[0].kind, "path");
  assert.equal(profile.contours[1].kind, "circle", "analytic holes remain analytic when saving a mixed profile");

  const disjoint = { entities: [
    { id: "left", kind: "rectangle", x: 0, y: 0, width: 10, height: 10, closed: true },
    { id: "right", kind: "rectangle", x: 20, y: 0, width: 10, height: 10, closed: true },
  ] };
  assert.match(analyzeSectionDraft(disjoint).message, /一个外轮廓/);
  assert.throws(() => buildProfileFromSectionDraft(disjoint), /一个外轮廓/);
}

{
  const member = { entityId: "member-1", stableKey: "frame.left", length: 100, profile: { depth: 40 } };
  assert.equal(validateSideSketchDraft({ entities: [
    { id: "hole", kind: "circle", cx: 50, cy: 20, radius: 5 },
  ] }, member).ready, true);
  assert.match(validateSideSketchDraft({ entities: [
    { id: "outside", kind: "circle", cx: 98, cy: 20, radius: 5 },
  ] }, member).message, /超出/);
}

{
  const converted = entitiesFromProfile({ contours: [
    { kind: "circle", center: [2, 3], radius: 10 },
    { kind: "roundedRectangle", center: [0, 0], width: 40, height: 20, radius: 3 },
    { kind: "path", segments: [
      { kind: "line", start: [-20, 0], end: [0, 20] },
      { kind: "arc", start: [0, 20], middle: [15, 15], end: [20, 0] },
    ] },
  ] });
  assert.equal(converted[0].kind, "circle", "analytic circles remain editable circles");
  assert.equal(converted[1].kind, "rectangle", "rounded rectangles remain compact editable primitives");
  assert.equal(converted[1].radius, 3);
  assert.equal(converted[2].kind, "path", "a connected contour retains shared editing nodes");
  assert.equal(converted[2].segments[0].kind, "line");
  assert.equal(converted[2].segments[1].kind, "circleArc");
  assert.equal("points" in converted[2].segments[1], false, "circular arcs remain analytic instead of becoming sampled vertices");
}

{
  const draft = { entities: [line("whole", [0, 0], [40, 0])], selectedId: "whole", history: [], future: [], dirty: false };
  const split = breakEntityAtPoint(draft, "whole", [15, 2]);
  assert.equal(split.changed, true);
  assert.equal(draft.entities.length, 2);
  assert.deepEqual([draft.entities[0].x2, draft.entities[1].x1], [15, 15]);
  assert.equal(draft.selectedPoints.length, 2, "a break exposes two independently selectable coincident endpoints");
  assert.equal(draft.dirty, true);
  const joinedLine = mergeSelectedEntities(draft);
  assert.equal(joinedLine.changed, true);
  assert.equal(draft.entities.length, 1, "merge reverses a line break");
  assert.equal(draft.entities[0].kind, "line");
  assert.deepEqual([draft.entities[0].x1, draft.entities[0].x2], [0, 40]);

  const circleDraft = { entities: [{ id: "round", kind: "circle", cx: 0, cy: 0, radius: 10, closed: true }], selectedId: "round", history: [], future: [], dirty: false };
  const opened = breakEntityAtPoint(circleDraft, "round", [10, 0]);
  assert.equal(opened.changed, true);
  assert.equal(circleDraft.entities[0].kind, "circleArc");
  assert.equal("points" in circleDraft.entities[0], false, "breaking a circle keeps an analytic circular arc");
  assert.equal(circleDraft.entities[0].closed, false, "the first break opens a closed contour");
  assert.equal(circleDraft.selectedPoints.length, 2);
  assert.equal(analyzeSectionDraft(circleDraft).ready, false, "coincident break endpoints still represent an open topology");
  const framedBreakPoints = selectControlPointsInWindow(
    circleDraft.entities,
    [circleDraft.entities[0].id],
    [9.5, -0.5],
    [10.5, 0.5],
  );
  assert.equal(framedBreakPoints.length, 2, "a window around the break selects both coincident endpoints");
  const openedEntity = circleDraft.entities[0];
  const undraggedArc = structuredClone(openedEntity);
  updateEntityFromGrip(openedEntity, {
    role: "vertex", index: 1,
    original: structuredClone(openedEntity), start: [10, 0],
  }, [0, 10]);
  assert.equal(openedEntity.kind, "path");
  assert.ok(openedEntity.segments.every(s=>s.kind==="circleArc"), "endpoint drag keeps circular segments");
  assert.equal("points" in openedEntity, false, "dragging an arc endpoint does not create polyline vertices");
  circleDraft.entities[0] = undraggedArc;
  circleDraft.selectedPoints = framedBreakPoints;
  const restoredCircle = mergeSelectedEntities(circleDraft);
  assert.equal(restoredCircle.changed, true);
  assert.equal(circleDraft.entities[0].kind, "circle");
  assert.equal(circleDraft.entities[0].closed, true, "merging the two break endpoints restores the closed contour");
  const reopened = breakEntityAtPoint(circleDraft, circleDraft.entities[0].id, [10, 0]);
  assert.equal(reopened.changed, true);
  const second = breakEntityAtPoint(circleDraft, circleDraft.entities[0].id, [-10, 0]);
  assert.equal(second.changed, true);
  assert.equal(circleDraft.entities.length, 2, "a second break creates two independently deletable pieces");
  const joinedAtSecondBreak = mergeSelectedEntities(circleDraft);
  assert.equal(joinedAtSecondBreak.changed, true);
  assert.equal(circleDraft.entities.length, 1);
  assert.equal(circleDraft.entities[0].kind, "circleArc");
  assert.equal(analyzeSectionDraft(circleDraft).ready, false, "merging one cut still leaves the original cut open");
  circleDraft.selectedPoints = [
    { entityId: circleDraft.entities[0].id, index: 0 },
    { entityId: circleDraft.entities[0].id, index: 1 },
  ];
  const joinedCircle = mergeSelectedEntities(circleDraft);
  assert.equal(joinedCircle.changed, true);
  assert.equal(circleDraft.entities[0].kind, "circle", "merging the remaining endpoints restores the analytic circle");
  assert.equal(circleDraft.entities[0].closed, true);

  const disconnected = { entities: [line("a", [0, 0], [5, 0]), line("b", [10, 0], [15, 0])], selectedIds: ["a", "b"], history: [], future: [] };
  assert.equal(mergeSelectedEntities(disconnected).changed, false, "disconnected geometry is not silently bridged");
  assert.equal(disconnected.entities.length, 2);
}

{
  const [ellipse] = entitiesFromProfile({ contours: [
    { kind: "ellipse", center: [3, -2], radiusX: 20, radiusY: 8, rotation: 0.3 },
  ] });
  assert.equal(ellipse.kind, "ellipse");
  assert.equal("points" in ellipse, false, "profile ellipses enter the sketch as analytic ellipses");
  const draft = { entities: [ellipse], selectedId: ellipse.id, history: [], future: [], dirty: false };
  const opened = breakEntityAtPoint(draft, ellipse.id, [23, -2]);
  assert.equal(opened.changed, true);
  assert.equal(draft.entities[0].kind, "ellipseArc");
  assert.equal("points" in draft.entities[0], false, "breaking an ellipse keeps an analytic ellipse arc");
  assert.equal(draft.selectedPoints.length, 2);
  assert.equal(analyzeSectionDraft(draft).ready, false);
  const restored = mergeSelectedEntities(draft);
  assert.equal(restored.changed, true);
  assert.equal(draft.entities[0].kind, "ellipse");
}

{
  const draft = { entities: [line("edge", [0, 0], [20, 0])], selectedId: "edge", selectedIds: ["edge"], selectedPoints: [], history: [], future: [], dirty: false };
  const inserted = insertPointOnEntity(draft, "edge", [8, 2]);
  assert.equal(inserted.changed, true);
  assert.equal(draft.entities.length, 1, "inserting a point does not split the entity");
  assert.equal(draft.entities[0].kind, "polyline");
  assert.deepEqual(draft.entities[0].points, [[0, 0], [8, 0], [20, 0]]);
  assert.deepEqual(draft.selectedPoints, [{ entityId: "edge", index: 1 }]);
  updateEntityFromGrip(draft.entities[0], {
    role: "vertex", index: 1, original: structuredClone(draft.entities[0]), start: [8, 0],
  }, [8, 5]);
  assert.deepEqual(draft.entities[0].points, [[0, 0], [8, 5], [20, 0]], "the inserted point can reshape the original contour");
}

{
  const view = { tubeDesignerSketch: createInitialSketchState() };
  const source = {
    snapshot: { contours: [{ kind: "circle", radius: 20 }] },
    profileId: "dxf-1", profileKey: "user:dxf-1", scope: "user", revision: 7,
    name: "我的圆管", directUpdate: true,
  };
  let state = beginProfileSectionSketch(view, source);
  assert.equal(state.sectionSession.kind, "update");
  assert.equal(state.sectionSession.sourceRevision, 7);
  assert.equal(state.sectionName, "我的圆管");
  state = beginProfileSectionSketch(view, { ...source, directUpdate: false, scope: "system" });
  assert.equal(state.sectionSession.kind, "copy");
  assert.match(state.sectionName, /草图副本/);
  state.section.dirty = false;
  assert.equal(beginNewSectionSketch(view).sectionSession.kind, "create");
  assert.equal(view.tubeDesignerSketch.section.entities.length, 0);
}

{
  const original = { id: "dxf-1", revision: 7, name: "我的方管" };
  const view = {
    activeAreaId: "sketch",
    tubeDesignerUserData: { profiles: [original] },
    tubeDesignerSketch: createInitialSketchState(),
  };
  const state = beginProfileSectionSketch(view, {
    snapshot: { contours: [{ kind: "roundedRectangle", width: 40, height: 20, radius: 0 }] },
    profileId: original.id, profileKey: `user:${original.id}`, scope: "user",
    revision: original.revision, name: original.name, directUpdate: true,
  });
  state.sectionName = "修改后的方管";
  state.section.dirty = true;
  let request = null;
  let selectedTab = "";
  const context = {
    actions: { async selectRibbonTab(id) { selectedTab = id; } },
    productProxy: { async invoke(method, payload) {
      request = { method, payload };
      return { profile: { ...original, ...payload.profile, id: original.id, revision: 8, name: payload.name } };
    } },
  };
  await handleSketchAreaAction(context, view, "tube-designer-sketch-commit", {}, {
    renderProject() {}, showNotice() {},
  });
  assert.equal(request, null, "editing a DXF asks whether to overwrite or save a copy before writing");
  assert.equal(state.saveChoiceDialogOpen, true);
  assert.match(renderSketchViewportOverlay({}, view), /覆盖原管型/);
  await handleSketchAreaAction(context, view, "tube-designer-sketch-save-overwrite", {}, {
    renderProject() {}, showNotice() {},
  });
  assert.equal(request.method, "TubeDesigner.SaveImportedProfile");
  assert.equal(request.payload.id, original.id, "editing my DXF profile updates the same record");
  assert.equal(request.payload.revision, 7);
  assert.equal(view.tubeDesignerUserData.profiles.length, 1);
  assert.equal(view.tubeDesignerSelectedProfileId, "user:dxf-1");
  assert.equal(view.tubeDesignerProfileLibrary.scope, "user");
  assert.equal(selectedTab, "profiles");
  assert.equal(view.activeAreaId, "profiles");
}

{
  const view = { activeAreaId: "sketch", tubeDesignerUserData: { profiles: [] }, tubeDesignerSketch: createInitialSketchState() };
  const state = beginProfileSectionSketch(view, {
    snapshot: { contours: [{ kind: "circle", radius: 20 }] },
    profileId: "system-round", profileKey: "system:system-round", scope: "system",
    revision: 0, name: "系统圆管", directUpdate: false,
  });
  state.sectionName = "系统圆管副本";
  let payload = null;
  const context = {
    actions: { async selectRibbonTab() {} },
    productProxy: { async invoke(_method, value) {
      payload = value;
      return { profile: { ...value.profile, id: "copy-1", revision: 1, name: value.name } };
    } },
  };
  await handleSketchAreaAction(context, view, "tube-designer-sketch-commit", {}, { renderProject() {}, showNotice() {} });
  assert.equal("id" in payload, false, "parameterized and system profiles are saved as new user records");
  assert.equal(view.tubeDesignerSelectedProfileId, "user:copy-1");
}

{
  const view = { activeAreaId: "sketch", tubeDesignerUserData: { profiles: [] }, tubeDesignerSketch: createInitialSketchState() };
  const state = beginProfileSectionSketch(view, {
    snapshot: { contours: [{ kind: "circle", radius: 12 }] },
    profileId: "existing-dxf", profileKey: "user:existing-dxf", scope: "user",
    revision: 2, name: "已有 DXF", directUpdate: true,
  });
  state.section.dirty = true;
  let payload = null;
  const context = {
    actions: { async selectRibbonTab() {} },
    productProxy: { async invoke(_method, value) {
      payload = value;
      return { profile: { ...value.profile, id: "saved-copy", revision: 1, name: value.name } };
    } },
  };
  const ops = { renderProject() {}, showNotice() {} };
  await handleSketchAreaAction(context, view, "tube-designer-sketch-commit", {}, ops);
  await handleSketchAreaAction(context, view, "tube-designer-sketch-save-copy", {}, ops);
  assert.equal("id" in payload, false, "choosing save-as for a DXF creates a new record instead of overwriting");
  assert.equal(view.tubeDesignerSelectedProfileId, "user:saved-copy");
}

{
  const stored = {
    schema: "icax.tube-sketch",
    schemaVersion: 1,
    kind: "side",
    targetMemberId: "old-member-id",
    targetMemberKey: "frame.left",
    length: 100,
    faceHeight: 40,
    unit: "mm",
    entities: [{ id: "cut", kind: "circle", cx: 50, cy: 20, radius: 5 }],
  };
  const view = {
    activeAreaId: "sketch",
    scene: { tubeDesigner: {
      product: { entityId: "product-1", sketches: { side: { "old-member-id": stored } } },
      members: [{ entityId: "new-member-id", stableKey: "frame.left", name: "左框", length: 100, profile: { depth: 40 } }],
    } },
    tubeDesignerSketch: createInitialSketchState(),
  };
  const state = ensureSketchState(view);
  state.mode = "side";
  state.targetMemberId = "new-member-id";
  const draft = state.sideByMember["new-member-id"];
  assert.equal(draft.persisted, true, "stored sketches reattach through the stable member key");
  draft.entities = [];
  draft.selectedId = "";
  draft.dirty = true;
  assert.match(renderSketchRightPane({}, view), /确认后将移除已应用的侧面草图/);

  let request = null;
  let notice = "";
  const context = {
    sceneProxy: {
      async invoke(method, payload) {
        request = { method, payload };
        return { tubeDesigner: view.scene.tubeDesigner };
      },
    },
  };
  const result = await handleSketchAreaAction(context, view, "tube-designer-sketch-commit", {}, {
    renderProject() {},
    showNotice(_context, _view, message) { notice = message; },
  });
  assert.equal(result.handled, true);
  assert.equal(request.method, "TubeDesigner.SaveSketch");
  assert.equal(request.payload.sketch.entities.length, 0);
  assert.equal(request.payload.sketch.targetMemberKey, "frame.left");
  assert.equal(draft.persisted, false);
  assert.match(notice, /移除/);
}

console.log("TubeDesigner sketch tests passed");
