import assert from "node:assert/strict";
import { beginPunchOperation, finishPunchOperation, previewPunch } from "../../apps/tube-designer/webpage/punchEditor.mjs";
import { createPunchWizardState, normalizePunchFeature, renderPunchWizardDialog, updatePunchWizardField } from "../../apps/tube-designer/webpage/punchWizard.mjs";
import { renderDesignerOperationOverlay } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { handlePartsAreaAction } from "../../apps/tube-designer/webpage/partsArea.mjs";

const part = { entityId: "single-flight", length: 500, independentNesting: true,
  profile: { kind: "rect", width: 40, depth: 20 }, properties: { "manufacturing.partKind": "tube" } };
const state = createPunchWizardState(part);
state.catalogueStatus = "ready";
state.features = [normalizePunchFeature({ id: "first", recordKind: "tool", type: "circle", station: 100, layoutDatum: "base", diameter: 10 })];
state.draft = normalizePunchFeature({ recordKind: "tool", type: "circle", station: 200, layoutDatum: "base", diameter: 10 });
const view = { pending: false, tubeDesignerPunchWizard: state, scene: { tubeDesigner: { nestingGroups: [{ parts: [part] }] } } };
const requests = [], renders = [];
let active = 0, maximumActive = 0;
const message = {}, phase = {}, bar = { style: {} }, attributes = {};
const track = { classList: { remove() {} }, setAttribute(name, value) { attributes[name] = value; }, querySelector() { return bar; } };
const context = { mount: { querySelector(selector) {
  return selector.includes("operation-message") ? message : selector.includes("operation-phase") ? phase : selector.includes("progress-track") ? track : null;
} }, sceneProxy: { invoke(method, payload, options) {
  active++; maximumActive = Math.max(maximumActive, active);
  let resolve, reject;
  const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
  requests.push({ method, payload, options, resolve, reject });
  return promise.finally(() => { active--; });
} } };
const ops = { renderProject() {
  renders.push({ pending: view.pending, html: renderPunchWizardDialog(part, view, { tableMode: true }) + renderDesignerOperationOverlay(context, view) });
} };
const flush = () => new Promise(resolve => setImmediate(resolve));
const fieldTarget = value => ({ value, dataset: { tubeDesignerPunchField: "station", tubeDesignerPunchIndex: "0" } });

// Quiet parameter-driven previews must use the same visible busy gate as an
// explicitly requested preview. Repeated events are rejected, never queued.
const first = previewPunch(context, view, part, ops, null, { quiet: true });
assert.equal(view.pending, true);
assert.equal(state.previewPending, true);
assert.equal(view.tubeDesignerOperation.kind, "punch");
assert.match(renders.at(-1).html, /data-tube-designer-operation-wait/);
assert.match(renders.at(-1).html, /role="progressbar"/);
assert.match(renders.at(-1).html, /data-cam-action="tube-designer-punch-parameters-open" disabled[^>]*data-tube-designer-punch-editor-mode="pose"/);
assert.equal(await previewPunch(context, view, part, ops, null, { quiet: true }), false);
assert.equal(await previewPunch(context, view, part, ops), false);
assert.equal(updatePunchWizardField(view, fieldTarget("150")), false);
await handlePartsAreaAction(context, view, "tube-designer-punch-preview", {}, ops);
await flush();
assert.equal(requests.length, 1);
assert.equal(requests[0].method, "TubeDesigner.PreviewPunchWizard");
assert.equal(typeof requests[0].options.onReport, "function");
requests[0].options.onReport({ payload: { message: "正在裁剪第 4 个刀具", completed: 4, total: 10 } });
assert.equal(view.tubeDesignerOperation.phaseLabel, "4 / 10");
assert.equal(message.textContent, "正在裁剪第 4 个刀具");
assert.equal(phase.textContent, "4 / 10");
assert.equal(bar.style.width, "40%");
assert.equal(attributes["aria-valuenow"], "4");
requests[0].resolve({toolsOnly:true,baseGeometry:{url:"isolated-base",version:1},toolGeometry:{url:"isolated-preview-1",version:1},length:500});
assert.equal(await first, true);
assert.equal(view.pending, false);
assert.equal(state.previewPending, false);
assert.equal(view.tubeDesignerOperation, null);
assert.equal(state.features[0].station, 100);
await flush();
assert.equal(requests.length, 1, "Blocked requests must not replay after unlock");

// A later ordinary edit is accepted after completion; failures also unlock and
// leave the following explicit retry usable.
assert.equal(updatePunchWizardField(view, fieldTarget("150")), true);
const second = previewPunch(context, view, part, ops, null, { quiet: true });
await flush();
assert.equal(requests.length, 2);
assert.equal(requests[1].payload.features[0].station, 150);
requests[1].reject(new Error("isolated computation failed"));
assert.equal(await second, false);
assert.match(state.error, /isolated computation failed/);
assert.equal(view.pending, false);
assert.equal(state.previewPending, false);
const third = previewPunch(context, view, part, ops);
await flush();
assert.equal(requests.length, 3);
assert.equal(state.error, "");
requests[2].resolve({toolsOnly:true,baseGeometry:{url:"isolated-base",version:1},toolGeometry:{url:"isolated-preview-3",version:1},length:500});
assert.equal(await third, true);
assert.equal(maximumActive, 1);
assert.equal(active, 0);
assert.equal(view.pending, false);
assert.equal(state.preview.toolGeometry.url, "isolated-preview-3");assert.equal(state.preview.geometry,undefined);
assert.equal(await previewPunch(context, view, part, ops, null, { quiet: true }), true);
assert.equal(requests.length, 3, "A quiet confirmation reuses the completed revision instead of recomputing identical geometry");

// The Refresh button includes the new-row draft for a new calculation, but a
// resource-only retry must reload the already computed saved rows even when
// that unrelated draft is unfinished. It must neither accept nor rewrite it.
const displayedRecipe = structuredClone(state.preview);
state.draft.arrayCount = 0;
state.previewRenderError = "三维预览显示失败：503";
state.error = state.previewRenderError;
const beforeResourceRetryRenders = renders.length;
await handlePartsAreaAction(context, view, "tube-designer-punch-preview", {}, ops);
assert.equal(requests.length, 3, "Resource retry never dispatches native geometry again");
assert.equal(renders.length, beforeResourceRetryRenders + 1, "Retry requests a fresh presentation of the stored recipe");
assert.equal(state.previewRenderError, "");
assert.equal(state.error, "");
assert.deepEqual(state.preview, displayedRecipe);
assert.equal(state.draft.arrayCount, 0, "Retry is not a transaction that accepts or repairs the unfinished draft");
assert.equal(await previewPunch(context, view, part, ops, null, { includeDraft: true }), false,
  "The same unfinished draft remains invalid when the user actually requests a new computation");
assert.match(state.error, /整数/);
assert.equal(requests.length, 3);
state.previewRenderError = "三维预览显示失败：503";
state.revision++;
assert.equal(await previewPunch(context, view, part, ops, null, { includeDraft: true }), false,
  "A preview from a different recipe revision must not take the resource-only retry shortcut");
assert.match(state.previewRenderError, /503/);
assert.equal(requests.length, 3);
state.revision--;state.draft.arrayCount = 1;state.previewRenderError = "";state.error = "";

// An untouched saved thumbnail intentionally has revision -1 while the new
// editor starts at 0. Retrying its resources must not require a missing cutter
// package or validate/add the default new-row draft.
{
  const savedPart = { ...part, thumbnailGeometryResourceId: "saved-original", thumbnailGeometryResourceVersion: 7,
    properties: { ...part.properties, "tubeDesigner.punchWizard": { baseLength: 500, features: [
      { type: "circle", recordKind: "tool", station: 100, toolRef: { id: "uninstalled-tool", version: "1", digest: "saved" } },
    ] } } };
  const savedState = createPunchWizardState(savedPart);
  savedState.catalogueStatus = "ready"; savedState.draft.arrayCount = 0;
  savedState.previewRenderError = "三维预览显示失败：503";
  const savedView = { pending: false, tubeDesignerPunchWizard: savedState }, savedRequests = [];
  let savedRenders = 0;
  const savedContext = { sceneProxy: { async invoke(method, payload) {
    savedRequests.push({ method, payload });
    return {toolsOnly:true,baseGeometry:{url:"isolated-base",version:1},toolGeometry:{url:"recomputed-edited-recipe",version:1},length:500};
  } } };
  const savedOps = { renderProject() { savedRenders++; } }, originalPreview = structuredClone(savedState.preview);
  const savedFeatures = structuredClone(savedState.features);
  assert.equal(await previewPunch(savedContext, savedView, savedPart, savedOps, null, { includeDraft: true }), true);
  assert.equal(savedRequests.length, 0); assert.equal(savedRenders, 1);
  assert.equal(savedState.previewRenderError, ""); assert.equal(savedState.error, "");
  assert.deepEqual(savedState.preview, originalPreview); assert.deepEqual(savedState.features, savedFeatures);
  assert.equal(savedState.draft.arrayCount, 0);
  savedState.revision++; savedState.previewRenderError = "三维预览显示失败：503";
  assert.equal(await previewPunch(savedContext, savedView, savedPart, savedOps, null, { includeDraft: true }), false);
  assert.match(savedState.error, /本机缺少对应程式/, "An edited recipe cannot bypass its missing-package validation by retaining the original thumbnail");
  assert.equal(savedRequests.length, 0);
  savedState.features = [];
  assert.equal(await previewPunch(savedContext, savedView, savedPart, savedOps, null, { includeDraft: true }), false);
  assert.match(savedState.error, /整数/, "The stale original must not bypass the changed draft's validation either");
  savedState.draft.arrayCount = 1;
  assert.equal(await previewPunch(savedContext, savedView, savedPart, savedOps, null, { includeDraft: true }), true);
  assert.equal(savedRequests.length, 1, "A valid modified recipe requires native computation rather than reloading the stale original");
  assert.equal(savedState.preview.toolGeometry.url, "recomputed-edited-recipe");assert.equal(savedState.preview.geometry,undefined);
  assert.equal(savedState.preview.revision, savedState.revision);
}

// Ownership guards stop a stale completion from clearing a different stage's
// progress UI. Existing no-token callers retain their public API behavior.
{
  const rejected=createPunchWizardState(part);rejected.catalogueStatus="ready";
  const rejectedView={pending:false,tubeDesignerPunchWizard:rejected};
  let requests=0;
  for(const response of [{geometry:{url:"old-boolean-result",version:1}},
    {toolsOnly:true,baseGeometry:{url:"blank",version:1},geometry:{url:"unexpected-cut",version:1}},
    {toolsOnly:true,toolGeometry:{url:"no-blank",version:1}}]) {
    const ctx={sceneProxy:{async invoke(method,payload){requests++;assert.equal(payload.toolsOnly,true);return response;}}};
    assert.equal(await previewPunch(ctx,rejectedView,part,{renderProject(){}}),false,"An old boolean or incomplete preview response must not be silently displayed");
    assert.ok(rejected.error);assert.equal(rejected.preview,null);assert.equal(rejectedView.pending,false);
  }
  assert.equal(requests,3);
}
beginPunchOperation(context, view, "stage one");
const stageOne = view.tubeDesignerOperation;
beginPunchOperation(context, view, "stage two");
const stageTwo = view.tubeDesignerOperation;
assert.equal(finishPunchOperation(view, stageOne), false);
assert.equal(view.pending, true);
assert.equal(view.tubeDesignerOperation, stageTwo);
assert.equal(finishPunchOperation(view, stageTwo), true);
assert.equal(view.pending, false);
assert.equal(view.tubeDesignerOperation, null);
beginPunchOperation(context, view, "legacy caller");
assert.equal(finishPunchOperation(view), true);

state.features[0].arrayCount = 0;
assert.equal(await previewPunch(context, view, part, ops, null, { quiet: true }), false);
assert.equal(requests.length, 3);
assert.equal(view.pending, false);
assert.match(state.error, /整数/);
console.log("Punch preview single-flight, visible progress, busy gating, error recovery and operation ownership passed.");
