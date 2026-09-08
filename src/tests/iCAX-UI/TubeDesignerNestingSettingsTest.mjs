import assert from "node:assert/strict";
import { buildProfileGroups } from "../../apps/tube-designer/webpage/partsArea.mjs";
import {
  getNestingParameters,
  getNestingStockInputs,
  handleNestingSettingsAction,
  handleNestingSettingsRibbonCommand,
  renderNestingSettingsDialogs,
} from "../../apps/tube-designer/webpage/nestingSettings.mjs";

const originalStorage = Object.getOwnPropertyDescriptor(globalThis, "localStorage");
const savedProjects = new Map();
Object.defineProperty(globalThis, "localStorage", { configurable: true, get() {
  throw new Error("Nesting settings must use the project, not browser storage");
} });

const rectangularProfile = {
  id: "rect", kind: "rect", packageVersion: "1.0.0", displayName: "矩形管",
  specification: "40 × 20 × R2 × 1.5", width: 40, depth: 20,
  wallThickness: 1.5, cornerRadius: 2, hollow: true,
};
const roundProfile = {
  id: "round", kind: "round", packageVersion: "1.0.0", displayName: "圆管",
  specification: "⌀19 × 1", width: 19, depth: 19,
  wallThickness: 1, cornerRadius: 0, hollow: true,
};

function createPart(entityId, profile, material = "Q235B") {
  return {
    entityId, name: entityId, partNumber: entityId, length: 1200, quantity: 1,
    profile: structuredClone(profile),
    properties: { "manufacturing.material": material },
  };
}

function createView() {
  return {
    scene: { tubeDesigner: { manufacturingGroups: [{
      productEntityId: "product-a", name: "产品 A", parts: [
        createPart("rect-a", rectangularProfile, "Q235B"),
        createPart("rect-b", rectangularProfile, "304"),
        createPart("round-a", roundProfile, "304"),
      ],
    }] } },
  };
}

function partsIn(view) {
  return view.scene.tubeDesigner.manufacturingGroups.flatMap((group) => group.parts);
}

const completed = [];
async function test(name, run) {
  await run();
  completed.push(name);
}

let projectSequence = 0;
function createHarness(options = {}) {
  const view = createView();
  const context = { project: {
    projectId: options.projectId ?? `nesting-settings-test-${++projectSequence}`,
    projectPath: options.projectPath ?? "",
  } };
  const identity = context.project.projectPath || context.project.projectId;
  view.scene.tubeDesigner.nestingSettings = structuredClone(savedProjects.get(identity) ?? {});
  const saves = [];
  let refreshCount = 0;
  context.sceneProxy = { async invoke(method, { settings }) {
    assert.equal(method, "TubeDesigner.SaveNestingSettings");
    assert.equal(settings.version, 2);
    assert.ok(settings.stocks.every((group) => group.rows.every((row) =>
      Number.isFinite(row.length) && row.length > 0 && Number.isSafeInteger(row.quantity) && row.quantity >= -1)));
    saves.push(structuredClone(settings));
    savedProjects.set(identity, structuredClone(settings));
    return { settings: structuredClone(settings) };
  } };
  context.actions = { async refreshActiveSceneState() { refreshCount++; } };
  const ops = { renderProject() {} };
  return {
    view,
    context,
    saves,
    refreshCount: () => refreshCount,
    async open(kind = "stock") {
      await handleNestingSettingsRibbonCommand(context, view,
        kind === "stock" ? "nesting.stock-settings" : "nesting.parameters", ops);
    },
    async act(suffix, target = {}) {
      return handleNestingSettingsAction(context, view, `tube-designer-nesting-${suffix}`, target, ops);
    },
    async changeRow(section, row, field, value) {
      return this.act("stock-change", { dataset: { profileKey: section.profileKey, rowId: row.id, field }, value });
    },
  };
}

function rectangleDraft(view) {
  const group = buildProfileGroups(partsIn(view)).find((item) => item.profile.includes("矩形管"));
  return view.tubeDesignerNestingStockDraft.find((section) => section.profileKey === group.key);
}

async function fillRow(harness, section, row, length, quantity) {
  await harness.changeRow(section, row, "length", length);
  await harness.changeRow(section, row, "quantity", quantity);
}

await test("Cross-material parts share their section without merging different dimensions", () => {
  const parts = partsIn(createView());
  const groups = buildProfileGroups(parts);
  assert.equal(groups.length, 2);
  assert.equal(groups.find((group) => group.profile.includes("矩形管")).quantity, 2);
  assert.equal(buildProfileGroups([
    parts[0],
    { ...parts[1], profile: { ...parts[1].profile, width: 50 } },
  ]).length, 2);
});

await test("Opening stock settings derives sections from manufacturing parts", async () => {
  const harness = createHarness();
  await harness.open();
  assert.equal(harness.view.tubeDesignerNestingSettingsDialog, "stock");
  assert.equal(harness.view.tubeDesignerNestingStockDraft.length, 2);
  assert.deepEqual(new Set(harness.view.tubeDesignerNestingStockDraft.map((section) => section.profileKey)),
    new Set(buildProfileGroups(partsIn(harness.view)).map((group) => group.key)));
  const section = rectangleDraft(harness.view);
  assert.ok(section);
  assert.equal(section.rows.length, 1);
  assert.ok(getNestingStockInputs(harness.view).every((row) => row.length === 6000 && row.quantity === -1));
  assert.equal(getNestingStockInputs(harness.view).length, 2);
  assert.equal(section.rows[0].length, 6000);
  assert.equal(section.rows[0].quantity, -1);
  const firstIds = getNestingStockInputs(harness.view).map((row) => row.id);
  renderNestingSettingsDialogs(harness.context, harness.view);
  assert.deepEqual(getNestingStockInputs(harness.view).map((row) => row.id), firstIds);
  assert.equal(harness.saves.length, 0, "opening/rendering does not save");
});

await test("Multiple lengths and quantities persist; zero stock stays out of usable inputs", async () => {
  const harness = createHarness();
  await harness.open();
  const section = rectangleDraft(harness.view);
  await fillRow(harness, section, section.rows[0], "6000", "8");
  await harness.act("stock-add", { dataset: { profileKey: section.profileKey } });
  await fillRow(harness, section, section.rows.at(-1), "4500", "3");
  await harness.act("stock-add", { dataset: { profileKey: section.profileKey } });
  await fillRow(harness, section, section.rows.at(-1), "3000", "0");
  await harness.act("stock-save");
  assert.ok(!harness.view.tubeDesignerNestingSettingsError);
  assert.ok(!harness.view.tubeDesignerNestingSettingsDialog);
  const inputs = getNestingStockInputs(harness.view).filter((row) => row.quantity > 0);
  assert.deepEqual(inputs.map(({ length, quantity }) => ({ length, quantity })), [
    { length: 6000, quantity: 8 }, { length: 4500, quantity: 3 },
  ]);
  assert.ok(inputs.every((input) => input.profile.width === 40 && input.profileKey === section.profileKey));
  await harness.open();
  assert.deepEqual(rectangleDraft(harness.view).rows.map(({ length, quantity }) => [Number(length), Number(quantity)]),
    [[6000, 8], [4500, 3], [3000, 0]]);
});

await test("Invalid stock lengths cannot save", async () => {
  for (const length of ["", " ", "0", "-1", "NaN", "Infinity", "1e999", "not-a-length"]) {
    const harness = createHarness();
    await harness.open();
    const section = rectangleDraft(harness.view);
    await fillRow(harness, section, section.rows[0], length, "2");
    await harness.act("stock-save");
    assert.equal(harness.view.tubeDesignerNestingSettingsDialog, "stock", `Invalid length ${JSON.stringify(length)} closed the dialog`);
    assert.ok(harness.view.tubeDesignerNestingSettingsError, `Invalid length ${JSON.stringify(length)} has no error`);
    assert.deepEqual(getNestingStockInputs(harness.view).filter((row) => row.quantity > 0), []);
    assert.equal(harness.saves.length, 0);
  }
});

await test("Stock quantities allow -1, zero, or positive safe integers", async () => {
  for (const quantity of ["", " ", "-2", "-1.5", "1.5", "NaN", "Infinity", "1e999", "9007199254740992"]) {
    const harness = createHarness();
    await harness.open();
    const section = rectangleDraft(harness.view);
    await fillRow(harness, section, section.rows[0], "6000", quantity);
    await harness.act("stock-save");
    assert.equal(harness.view.tubeDesignerNestingSettingsDialog, "stock", `Invalid quantity ${JSON.stringify(quantity)} closed the dialog`);
    assert.ok(harness.view.tubeDesignerNestingSettingsError, `Invalid quantity ${JSON.stringify(quantity)} has no error`);
    assert.deepEqual(getNestingStockInputs(harness.view).filter((row) => row.quantity > 0), []);
    assert.equal(harness.saves.length, 0);
  }
});

await test("Default sections save 6000 mm unlimited inventory to the project", async () => {
  const harness = createHarness();
  await harness.open();
  await harness.act("stock-save");
  assert.ok(!harness.view.tubeDesignerNestingSettingsError);
  assert.ok(!harness.view.tubeDesignerNestingSettingsDialog);
  assert.equal(harness.saves.length, 1);
  assert.equal(harness.refreshCount(), 1);
  assert.ok(getNestingStockInputs(harness.view).every((row) => row.length === 6000 && row.quantity === -1));
  assert.deepEqual(harness.view.scene.tubeDesigner.nestingSettings, harness.saves[0]);
});

await test("Cancel discards stock edits and add/remove actions affect only the draft", async () => {
  const harness = createHarness();
  await harness.open();
  let section = rectangleDraft(harness.view);
  await fillRow(harness, section, section.rows[0], "6000", "4");
  await harness.act("stock-save");
  const saved = structuredClone(harness.view.tubeDesignerNestingSettings);
  await harness.open();
  section = rectangleDraft(harness.view);
  const originalRowId = section.rows[0].id;
  await harness.act("stock-add", { dataset: { profileKey: section.profileKey } });
  const added = section.rows.at(-1);
  assert.notEqual(added.id, originalRowId);
  assert.equal(section.rows.length, 2);
  await fillRow(harness, section, added, "9000", "12");
  await harness.act("stock-remove", { dataset: { profileKey: section.profileKey, rowId: originalRowId } });
  assert.equal(section.rows.length, 1);
  assert.equal(section.rows[0].id, added.id);
  assert.deepEqual(harness.view.tubeDesignerNestingSettings, saved);
  await harness.act("settings-cancel");
  assert.ok(!harness.view.tubeDesignerNestingSettingsDialog);
  assert.deepEqual(harness.view.tubeDesignerNestingSettings, saved);
  assert.equal(harness.saves.length, 1, "cancel/add/remove/change never save");
  await harness.open();
  section = rectangleDraft(harness.view);
  assert.equal(section.rows.length, 1);
  assert.equal(Number(section.rows[0].length), 6000);
  assert.equal(Number(section.rows[0].quantity), 4);
});

await test("Section changes preserve saved settings for sections which later return", async () => {
  const harness = createHarness();
  await harness.open();
  const section = rectangleDraft(harness.view);
  await fillRow(harness, section, section.rows[0], "6000", "7");
  await harness.act("stock-save");
  const originalParts = structuredClone(partsIn(harness.view));
  harness.view.scene.tubeDesigner.manufacturingGroups[0].parts = [originalParts[2],
    createPart("new-profile", { ...rectangularProfile, width: 60, specification: "60 × 20 × R2 × 1.5" })];
  await harness.open();
  const html = renderNestingSettingsDialogs(harness.context, harness.view);
  assert.doesNotMatch(html, /40 × 20 × R2 × 1\.5/);
  assert.match(html, /60 × 20 × R2 × 1\.5/);
  await harness.act("stock-save");
  assert.deepEqual(getNestingStockInputs(harness.view).filter((row) => row.quantity > 0), []);
  harness.view.scene.tubeDesigner.manufacturingGroups[0].parts = originalParts;
  await harness.open();
  const restored = rectangleDraft(harness.view);
  assert.equal(Number(restored.rows[0].length), 6000);
  assert.equal(Number(restored.rows[0].quantity), 7);
});

await test("Part gap accepts finite nonnegative values and rejects invalid input", async () => {
  for (const partGap of ["", " ", "-0.1", "NaN", "Infinity", "1e999", "no-gap"]) {
    const harness = createHarness();
    await harness.open("parameters");
    await harness.act("parameters-change", { dataset: { field: "partGap" }, value: partGap });
    await harness.act("parameters-save");
    assert.equal(harness.view.tubeDesignerNestingSettingsDialog, "parameters", `Invalid gap ${JSON.stringify(partGap)} closed the dialog`);
    assert.ok(harness.view.tubeDesignerNestingSettingsError);
    assert.equal(getNestingParameters(harness.view).partGap, 0);
  }
  const harness = createHarness();
  for (const gap of [0, 2.5]) {
    await harness.open("parameters");
    await harness.act("parameters-change", { dataset: { field: "partGap" }, value: String(gap) });
    await harness.act("parameters-save");
    assert.ok(!harness.view.tubeDesignerNestingSettingsError);
    assert.ok(!harness.view.tubeDesignerNestingSettingsDialog);
    assert.equal(getNestingParameters(harness.view).partGap, gap);
    await harness.open("parameters");
    assert.equal(Number(harness.view.tubeDesignerNestingParameterDraft.partGap), gap);
  }
});

await test("Canceling gap edits preserves saved parameters and stock settings", async () => {
  const harness = createHarness();
  await harness.open();
  const section = rectangleDraft(harness.view);
  await fillRow(harness, section, section.rows[0], "6000", "5");
  await harness.act("stock-save");
  await harness.open("parameters");
  await harness.act("parameters-change", { dataset: { field: "partGap" }, value: "3" });
  await harness.act("parameters-save");
  const saved = structuredClone(harness.view.tubeDesignerNestingSettings);
  await harness.open("parameters");
  await harness.act("parameters-change", { dataset: { field: "partGap" }, value: "9" });
  assert.equal(getNestingParameters(harness.view).partGap, 3);
  await harness.act("settings-cancel");
  assert.deepEqual(harness.view.tubeDesignerNestingSettings, saved);
  await harness.open("parameters");
  assert.equal(Number(harness.view.tubeDesignerNestingParameterDraft.partGap), 3);
  assert.equal(getNestingStockInputs(harness.view).find((row) => row.profile.width === 40).quantity, 5);
});

await test("Saved settings reload through the runtime project context and remain isolated by project", async () => {
  const projectA = { projectId: "persisted-project-a", projectPath: "D:/fixtures/project-a.ictd" };
  const first = createHarness(projectA);
  await first.open();
  const section = rectangleDraft(first.view);
  await fillRow(first, section, section.rows[0], "6000", "6");
  await first.act("stock-save");
  await first.open("parameters");
  await first.act("parameters-change", { dataset: { field: "partGap" }, value: "4.5" });
  await first.act("parameters-save");

  const reopened = createHarness({ ...projectA, projectId: "new-runtime-id-for-project-a" });
  await reopened.open();
  assert.deepEqual(getNestingStockInputs(reopened.view).filter((row) => row.quantity > 0).map(({ length, quantity }) => [length, quantity]), [[6000, 6]]);
  assert.equal(getNestingParameters(reopened.view).partGap, 4.5);

  const other = createHarness({ projectId: "persisted-project-b", projectPath: "D:/fixtures/project-b.ictd" });
  await other.open();
  assert.ok(getNestingStockInputs(other.view).every((row) => row.length === 6000 && row.quantity === -1));
  assert.equal(getNestingParameters(other.view).partGap, 0);
  const otherSection = rectangleDraft(other.view);
  await fillRow(other, otherSection, otherSection.rows[0], "4500", "2");
  await other.act("stock-save");
  const untouched = createHarness(projectA);
  await untouched.open();
  assert.deepEqual(getNestingStockInputs(untouched.view).filter((row) => row.quantity > 0).map(({ length, quantity }) => [length, quantity]), [[6000, 6]]);
  assert.equal(getNestingParameters(untouched.view).partGap, 4.5);
});

await test("Scene refresh and undo replace the in-memory settings instead of using old cache", async () => {
  const harness = createHarness();
  await harness.open();
  const section = rectangleDraft(harness.view);
  await fillRow(harness, section, section.rows[0], "4500", "5");
  await harness.act("stock-save");
  const saved = structuredClone(harness.view.scene.tubeDesigner.nestingSettings);
  harness.view.scene.tubeDesigner.nestingSettings = {};
  assert.ok(getNestingStockInputs(harness.view).every((row) => row.length === 6000 && row.quantity === -1));
  harness.view.scene.tubeDesigner.nestingSettings = saved;
  assert.equal(getNestingStockInputs(harness.view).find((row) => row.profile.width === 40).length, 4500);
  assert.equal(getNestingStockInputs(harness.view).find((row) => row.profile.width === 40).quantity, 5);
});

await test("Failed project save keeps the committed settings and the user's draft", async () => {
  const harness = createHarness();
  await harness.open();
  const section = rectangleDraft(harness.view);
  await fillRow(harness, section, section.rows[0], "4500", "7");
  harness.context.sceneProxy.invoke = async () => { throw new Error("项目写入失败"); };
  const before = structuredClone(harness.view.tubeDesignerNestingSettings);
  const response = await harness.act("stock-save");
  assert.equal(response.result, false);
  assert.deepEqual(harness.view.tubeDesignerNestingSettings, before);
  assert.equal(harness.view.tubeDesignerNestingSettingsDialog, "stock");
  assert.match(harness.view.tubeDesignerNestingSettingsError, /项目写入失败/);
  assert.equal(Number(rectangleDraft(harness.view).rows[0].quantity), 7);
  assert.equal(harness.view.tubeDesignerNestingSettingsSaving, false);
});

if (originalStorage) Object.defineProperty(globalThis, "localStorage", originalStorage);
else delete globalThis.localStorage;
console.log(`TubeDesigner nesting settings tests passed (${completed.length} cases)`);
