import assert from "node:assert/strict";
import { handleNestingStandardPartAction, renderNestingStandardPartDialog } from "../../apps/tube-designer/webpage/nestingStandardPart.mjs";
import { renderDesignerOperationOverlay } from "../../apps/tube-designer/webpage/designerViews.mjs";

const tests = [];
const drain = async () => { for (let index = 0; index < 8; index++) await Promise.resolve(); };
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
const profile = {
  name: "矩形管", contours: [{ kind: "polygon", points: [[-20, -10], [20, -10], [20, 10], [-20, 10]] }],
  width: 40, depth: 20,
};
const created = () => ({ partEntityId: "new-part", profile, tubeDesigner: {
  nestingGroups: [{ productEntityId: "run", generationRunId: "run", parts: [{ entityId: "new-part", generationRunId: "run", quantity: 1, length: 1000 }] }],
  nestingTask: { revision: "new", parts: [{ partEntityId: "new-part", generationRunId: "run" }] }, nestingSettings: {},
} });

async function test(name, run) {
  const previous = globalThis.requestAnimationFrame;
  const frames = [];
  globalThis.requestAnimationFrame = (callback) => { frames.push(callback); return frames.length; };
  const frame = async () => {
    assert.ok(frames.length, "A progress paint must be requested before work begins");
    frames.shift()(0);
    await drain();
  };
  try { await run({ frame, paint: async () => { await frame(); await frame(); } }); tests.push(name); }
  finally { if (previous) globalThis.requestAnimationFrame = previous; else delete globalThis.requestAnimationFrame; }
}

async function harness() {
  const calls = [], renders = [], notices = [];
  const file = deferred(), evaluation = deferred(), creation = deferred(), refresh = deferred();
  const view = { activeAreaId: "nesting", pending: false, scene: { tubeDesigner: { nestingGroups: [] } },
    tubeDesignerSystemProfiles: [{ id: "rect", name: "矩形管", profileType: "parametric-package",
      previewProfile: profile, defaultParameters: { width: 40 },
      descriptor: { parameters: [{ key: "width", displayName: "外宽", valueType: "number", defaultValue: 40, min: 1 }] },
    }], tubeDesignerUserData: { profiles: [] },
  };
  const context = {
    appProxy: { bridge: { openFileDialog() { calls.push("file"); return file.promise; } } },
    productProxy: { invoke(method) { calls.push(method); return evaluation.promise; } },
    sceneProxy: { invoke(method) { calls.push(method); return creation.promise; } },
    actions: { refreshActiveSceneState() { calls.push("refresh"); return refresh.promise; } },
  };
  const ops = { renderProject() { renders.push({ operation: structuredClone(view.tubeDesignerOperation ?? null),
    html: renderDesignerOperationOverlay(context, view), dialog: renderNestingStandardPartDialog(view) }); },
    showNotice(_context, _view, text) { notices.push(text); },
  };
  const act = (suffix, target = {}) => handleNestingStandardPartAction(context, view, `tube-designer-nesting-standard-${suffix}`, target, ops);
  await act("open");
  const progress = (phase) => {
    assert.equal(view.pending, true);
    assert.equal(view.tubeDesignerOperation?.phase, phase);
    assert.match(renders.at(-1).html, /is-indeterminate/);
    assert.match(renders.at(-1).html, /data-tube-designer-operation-phase/);
    assert.doesNotMatch(renders.at(-1).html, /aria-valuenow|\b(?:[0-9]{1,3})%\s*</);
    if (view.tubeDesignerNestingStandardPartDraft) assert.match(renders.at(-1).dialog, /data-cam-action="tube-designer-nesting-standard-confirm" disabled/);
  };
  const idle = () => {
    assert.equal(view.pending, false);
    assert.equal(view.tubeDesignerOperation, null);
    assert.equal(renders.at(-1).html, "");
  };
  return { act, context, view, calls, renders, notices, file, evaluation, creation, refresh, progress, idle };
}

await test("creation progress paints before invocation and stays through scene refresh", async ({ frame, paint }) => {
  const h = await harness();
  const result = h.act("confirm");
  h.progress("generating");
  assert.deepEqual(h.calls, []);
  await h.act("confirm");
  await frame();
  assert.deepEqual(h.calls, [], "One animation callback is not a completed paint");
  await frame();
  assert.deepEqual(h.calls, ["TubeDesigner.AddNestingStandardPart"]);
  h.progress("generating");
  h.creation.resolve(created()); await drain();
  h.progress("refreshing-scene");
  assert.equal(h.notices.length, 0, "Do not report success while the scene is still refreshing");
  assert.equal(h.view.tubeDesignerNestingStandardPartDraft, null);
  await h.act("confirm");
  await paint();
  assert.deepEqual(h.calls, ["TubeDesigner.AddNestingStandardPart", "refresh"]);
  h.progress("refreshing-scene");
  h.refresh.resolve(); await result;
  h.idle(); assert.equal(h.notices.length, 1);
});

await test("DXF picker and import each show their current waiting stage", async ({ paint }) => {
  const h = await harness();
  const result = h.act("import-dxf");
  h.progress("selecting-file"); assert.deepEqual(h.calls, []);
  await paint(); assert.deepEqual(h.calls, ["file"]);
  await h.act("import-dxf");
  h.file.resolve("D:\\Profiles\\round.dxf"); await drain();
  h.progress("importing-dxf"); assert.deepEqual(h.calls, ["file"]);
  await paint();
  assert.deepEqual(h.calls, ["file", "TubeDesigner.ImportProfileDxf"]);
  h.evaluation.resolve({ profile }); await result;
  h.idle(); assert.equal(h.view.tubeDesignerNestingStandardPartDraft.profileKey, "__dxf__");
});

await test("cancelling the DXF picker clears progress and preserves the selected profile", async ({ paint }) => {
  const h = await harness();
  const result = h.act("import-dxf"); await paint();
  h.file.resolve(""); await result;
  h.idle(); assert.deepEqual(h.calls, ["file"]);
  assert.equal(h.view.tubeDesignerNestingStandardPartDraft.profileKey, "system:rect");
});

await test("program evaluation has indeterminate progress and blocks overlapping edits", async ({ paint }) => {
  const h = await harness();
  const result = h.act("parameter", { dataset: { standardPartParameter: "width" }, value: "55" });
  h.progress("evaluating-profile"); assert.deepEqual(h.calls, []);
  await h.act("parameter", { dataset: { standardPartParameter: "width" }, value: "65" });
  await h.act("confirm");
  await paint(); assert.deepEqual(h.calls, ["TubeDesigner.EvaluateProfilePackage"]);
  h.evaluation.resolve({ profile: { ...profile, width: 55 } }); await result;
  h.idle(); assert.equal(h.view.tubeDesignerNestingStandardPartDraft.parameters.width, 55);
  assert.equal(h.view.tubeDesignerNestingStandardPartDraft.previewInvalid, false);
});

for (const operation of ["file", "dxf", "parameter", "creation"]) {
  await test(`${operation} failure clears waiting state and leaves an actionable error`, async ({ paint }) => {
    const h = await harness();
    const result = operation === "parameter"
      ? h.act("parameter", { dataset: { standardPartParameter: "width" }, value: "55" })
      : h.act(operation === "creation" ? "confirm" : "import-dxf");
    await paint();
    if (operation === "dxf") { h.file.resolve("D:\\Profiles\\round.dxf"); await drain(); await paint(); }
    (operation === "file" ? h.file : operation === "creation" ? h.creation : h.evaluation).reject(new Error("可重试的后台错误"));
    await result;
    h.idle(); assert.match(h.view.tubeDesignerNestingStandardPartDraft.error, /可重试的后台错误/);
    assert.equal(h.notices.length, 0);
  });
}

await test("refresh failure says the part exists and never offers a second creation", async ({ paint }) => {
  const h = await harness();
  const result = h.act("confirm"); await paint();
  h.creation.resolve(created()); await drain(); await paint();
  h.refresh.reject(new Error("场景暂时不可用")); await result;
  h.idle(); assert.equal(h.view.tubeDesignerNestingStandardPartDraft, null);
  assert.match(h.view.error, /已添加.*刷新失败.*不要重复添加/);
  await h.act("confirm");
  assert.equal(h.calls.filter((method) => method === "TubeDesigner.AddNestingStandardPart").length, 1);
  assert.equal(h.notices.length, 0);
});

console.log(`TubeDesignerNestingStandardPartProgressTest: ${tests.length} tests passed`);
