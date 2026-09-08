import assert from "node:assert/strict";
import {
  standardPartProfiles,
  renderNestingStandardPartDialog,
  handleNestingStandardPartAction,
  handleNestingStandardPartRibbonCommand,
} from "../../apps/tube-designer/webpage/nestingStandardPart.mjs";
import { ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";

const tests = [];
async function test(name, run) { await run(); tests.push(name); }

function rectangleProfile(width = 40, depth = 20) {
  return {
    schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "imported-dxf",
    name: "定式矩形", width, depth, contourCount: 1,
    contours: [{ kind: "polygon", points: [
      [-width / 2, -depth / 2], [width / 2, -depth / 2],
      [width / 2, depth / 2], [-width / 2, depth / 2],
    ] }],
  };
}

const systemProfile = {
  id: "rect", name: "程式矩形管", profileType: "parametric-package",
  descriptor: { parameters: [
    { key: "width", displayName: { "zh-CN": "外宽" }, valueType: "number", defaultValue: 40, min: 1, max: 500, step: 1 },
    { key: "depth", displayName: { "zh-CN": "外高" }, valueType: "number", defaultValue: 20, min: 1, max: 500, step: 1 },
  ] },
  defaultParameters: { width: 40, depth: 20 },
  previewProfile: { ...rectangleProfile(), kind: "parametric-package", name: "程式矩形管" },
};
const userFixedProfile = { ...rectangleProfile(32, 16), id: "fixed", name: "我的定式截面" };
const templateProfile = {
  ...systemProfile, id: "template-rect", templateId: "guardrail", name: "模板专属截面",
};

function harness() {
  const calls = [], notices = [];
  let refreshCount = 0;
  const view = {
    activeAreaId: "nesting", pending: false,
    scene: { tubeDesigner: { nestingGroups: [] } },
    tubeDesignerSystemProfiles: structuredClone([systemProfile]),
    tubeDesignerTemplateProfiles: structuredClone([templateProfile]),
    tubeDesignerUserData: { profiles: structuredClone([userFixedProfile]) },
    tubeDesignerSelectedProfileId: "template:guardrail:template-rect",
    tubeDesignerNestingSelectionKind: "plan",
    tubeDesignerActiveNestingPlacementId: "old-part#1",
    tubeDesignerPartMeasurementState: { status: "ready" },
    tubeDesignerPartViewportKey: "old-part",
  };
  const context = {
    appProxy: { bridge: { async openFileDialog(options) {
      calls.push({ method: "openFileDialog", options });
      return "D:\\Profiles\\local-section.dxf";
    } } },
    productProxy: { async invoke(method, payload) {
      calls.push({ method, payload: structuredClone(payload) });
      if (method === "TubeDesigner.EvaluateProfilePackage") {
        return { profile: {
          ...rectangleProfile(payload.parameters.width, payload.parameters.depth),
          kind: "parametric-package", name: "程式矩形管", parameters: payload.parameters,
        } };
      }
      if (method === "TubeDesigner.ImportProfileDxf") {
        return { profile: { ...rectangleProfile(60, 24), name: "本地 DXF 截面", sourceFileName: "local-section.dxf" } };
      }
      throw new Error(`Unexpected product method: ${method}`);
    } },
    sceneProxy: { async invoke(method, payload) {
      calls.push({ method, payload: structuredClone(payload) });
      assert.equal(method, "TubeDesigner.AddNestingStandardPart");
      const part = {
        entityId: "standard-part-1", generationRunId: "standard-run-1", name: "标准零件",
        quantity: 1, length: payload.length, profile: payload.profile ?? rectangleProfile(),
      };
      return {
        partEntityId: part.entityId, profile: part.profile,
        tubeDesigner: {
          nestingGroups: [{ productEntityId: "standard-run-1", generationRunId: "standard-run-1", parts: [part] }],
          nestingTask: { revision: "standard-revision-1", parts: [{ partEntityId: part.entityId, generationRunId: part.generationRunId }] },
          nestingSettings: {},
        },
      };
    } },
    actions: { async refreshActiveSceneState() { refreshCount++; } },
  };
  const ops = {
    renderProject() {},
    showNotice(_context, _view, text) { notices.push(text); },
  };
  const act = (suffix, target = {}) => handleNestingStandardPartAction(
    context, view, `tube-designer-nesting-standard-${suffix}`, target, ops,
  );
  const open = () => handleNestingStandardPartRibbonCommand(context, view, "nesting.add-standard-part", ops);
  const creations = () => calls.filter((call) => call.method === "TubeDesigner.AddNestingStandardPart");
  const error = () => view.tubeDesignerNestingStandardPartDraft?.error || view.error;
  return { context, view, calls, notices, ops, act, open, creations, error, refreshCount: () => refreshCount };
}

await test("only system and personal profiles are offered, including when a template was previously selected", async () => {
  const h = harness();
  const partCommands = ribbonDefinition.tabs.find((tab) => tab.id === "nesting")?.groups
    .find((group) => group.title === "零件")?.commands ?? [];
  const command = partCommands.find((item) => item.id === "nesting.add-standard-part");
  assert.ok(command, "The standard-part workflow is reachable from the nesting parts menu");
  assert.equal(command.title, "添加标准零件");
  assert.deepEqual(standardPartProfiles(h.view).map((profile) => profile.id).sort(), ["fixed", "rect"]);
  assert.equal(await h.open(), true);
  const html = renderNestingStandardPartDialog(h.view);
  assert.match(html, /添加标准零件/);
  assert.match(html, /程式矩形管/);
  assert.match(html, /我的定式截面/);
  assert.match(html, /DXF/);
  assert.match(html, /长度/);
  assert.doesNotMatch(html, /模板专属截面|template:guardrail/);
  assert.equal(h.view.tubeDesignerNestingStandardPartDraft.profileKey, "system:rect");
  assert.equal(h.calls.length, 0, "Opening the form neither creates a part nor evaluates unchanged defaults");
  await h.act("profile-select", { value: "template:guardrail:template-rect" });
  assert.ok(h.error(), "A forced template selection reports why it is unavailable");
  assert.equal(h.creations().length, 0);
});

await test("a template-only library has no implicit eligible profile", async () => {
  const h = harness();
  h.view.tubeDesignerSystemProfiles = [];
  h.view.tubeDesignerUserData.profiles = [];
  assert.deepEqual(standardPartProfiles(h.view), []);
  await h.open();
  assert.doesNotMatch(renderNestingStandardPartDialog(h.view), /模板专属截面/);
  await h.act("confirm");
  assert.equal(h.creations().length, 0);
  assert.ok(h.error());
});

await test("editable program parameters and length produce one independent selected nesting part", async () => {
  const h = harness();
  await h.open();
  assert.match(renderNestingStandardPartDialog(h.view), /外宽/);
  await h.act("parameter", { dataset: { standardPartParameter: "width" }, value: "55" });
  const evaluation = h.calls.find((call) => call.method === "TubeDesigner.EvaluateProfilePackage");
  assert.deepEqual(evaluation.payload.profileRef, { scope: "system", id: "rect" });
  assert.deepEqual(evaluation.payload.parameters, { width: 55, depth: 20 });
  assert.equal(h.view.tubeDesignerNestingStandardPartDraft.parameters.width, 55);
  assert.equal(h.creations().length, 0);
  await h.act("length", { value: "1250.5" });
  await h.act("confirm");
  assert.equal(h.creations().length, 1);
  assert.deepEqual(h.creations()[0].payload.profileRef, { scope: "system", id: "rect" });
  assert.deepEqual(h.creations()[0].payload.parameters, { width: 55, depth: 20 });
  assert.equal(h.creations()[0].payload.length, 1250.5);
  assert.deepEqual(h.view.tubeDesignerNestingSelectedPartIds, ["standard-part-1"]);
  assert.equal(h.view.tubeDesignerActivePartId, "standard-part-1");
  assert.equal(h.view.tubeDesignerActiveNestingPartId, "standard-part-1");
  assert.equal(h.view.tubeDesignerNestingSelectionKind, "part");
  assert.equal(h.view.tubeDesignerActiveNestingPlacementId, "");
  assert.equal(h.view.tubeDesignerPartMeasurementState, null);
  assert.equal(h.view.tubeDesignerPartViewportKey, "");
  assert.equal(h.view.tubeDesignerNestingStandardPartDraft, null);
  assert.equal(h.view.pending, false);
  assert.equal(h.refreshCount(), 1);
  assert.equal(h.notices.length, 1);
  assert.equal(h.view.scene.tubeDesigner.nestingGroups[0].parts[0].length, 1250.5);
});

await test("fixed library profiles do not retain parameters from the previous program profile", async () => {
  const h = harness();
  await h.open();
  await h.act("parameter", { dataset: { standardPartParameter: "width" }, value: "55" });
  await h.act("profile-select", { value: "user:fixed" });
  assert.doesNotMatch(renderNestingStandardPartDialog(h.view), /data-standard-part-parameter="width"/);
  await h.act("length", { value: "750" });
  await h.act("confirm");
  assert.equal(h.creations().length, 1);
  assert.deepEqual(h.creations()[0].payload.profileRef, { scope: "user", id: "fixed" });
  assert.equal(h.creations()[0].payload.length, 750);
  assert.equal(Object.keys(h.creations()[0].payload.parameters ?? {}).length, 0);
});

await test("local DXF remains an embedded section until the part is confirmed", async () => {
  const h = harness();
  await h.open();
  await h.act("profile-select", { value: "__dxf__" });
  const dialogCall = h.calls.find((call) => call.method === "openFileDialog");
  assert.deepEqual(dialogCall.options.filters[0].extensions, ["dxf"]);
  const importCall = h.calls.find((call) => call.method === "TubeDesigner.ImportProfileDxf");
  assert.equal(importCall.payload.sourcePath, "D:\\Profiles\\local-section.dxf");
  assert.match(renderNestingStandardPartDialog(h.view), /本地 DXF 截面|local-section.dxf/);
  assert.equal(h.creations().length, 0);
  assert.equal(h.calls.some((call) => call.method === "TubeDesigner.SaveImportedProfile"), false);
  await h.act("length", { value: "900" });
  await h.act("confirm");
  assert.equal(h.creations().length, 1);
  assert.equal(h.creations()[0].payload.profile.name, "本地 DXF 截面");
  assert.equal(h.creations()[0].payload.profile.width, 60);
  assert.equal(h.creations()[0].payload.profileRef, undefined);
  assert.equal(h.creations()[0].payload.length, 900);
  assert.equal(h.view.tubeDesignerUserData.profiles.length, 1);
});

await test("cancel discards draft edits and creates no nesting part", async () => {
  const h = harness();
  await h.open();
  await h.act("length", { value: "2500" });
  await h.act("cancel");
  assert.equal(h.view.tubeDesignerNestingStandardPartDraft, null);
  assert.equal(renderNestingStandardPartDialog(h.view), "");
  assert.equal(h.creations().length, 0);
  assert.equal(h.view.scene.tubeDesigner.nestingGroups.length, 0);
});

await test("invalid lengths stay editable and never create geometry", async () => {
  for (const length of ["", "abc", "0", "-1", "100001", "Infinity"]) {
    const h = harness();
    await h.open();
    await h.act("length", { value: length });
    await h.act("confirm");
    assert.equal(h.creations().length, 0, `Length ${JSON.stringify(length)} must be rejected`);
    assert.ok(h.view.tubeDesignerNestingStandardPartDraft);
    assert.ok(h.error());
    assert.equal(h.view.pending, false);
  }
});

await test("invalid and failed program evaluation block confirmation without losing the draft", async () => {
  const invalid = harness();
  await invalid.open();
  await invalid.act("parameter", { dataset: { standardPartParameter: "width" }, value: "not-a-number" });
  await invalid.act("confirm");
  assert.equal(invalid.calls.length, 0);
  assert.ok(invalid.error());
  assert.ok(invalid.view.tubeDesignerNestingStandardPartDraft);

  const failed = harness();
  await failed.open();
  failed.context.productProxy.invoke = async (method, payload) => {
    failed.calls.push({ method, payload });
    throw new Error("外宽小于壁厚，无法生成截面");
  };
  await failed.act("parameter", { dataset: { standardPartParameter: "width" }, value: "2" });
  assert.match(failed.error(), /无法生成截面/);
  await failed.act("confirm");
  assert.equal(failed.creations().length, 0);
  assert.ok(failed.view.tubeDesignerNestingStandardPartDraft);
  assert.equal(failed.view.pending, false);
});

console.log(`TubeDesignerNestingStandardPartTest: ${tests.length} tests passed`);
