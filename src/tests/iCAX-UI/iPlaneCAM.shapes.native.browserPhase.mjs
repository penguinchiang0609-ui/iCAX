import assert from "node:assert/strict";
import { runNativePlaneBrowser } from "./iPlaneCAM.native.browserHarness.mjs";
import { exerciseShapesPrepare, exerciseShapesReopen } from "./iPlaneCAM.shapes.native.browserCase.mjs";

const phase = process.env.ICAX_PLANE_SHAPES_PHASE;
assert.ok(["prepare", "reopen"].includes(phase));
await runNativePlaneBrowser({
  exercise: phase === "prepare" ? exerciseShapesPrepare : exerciseShapesReopen,
  scope: "Actual SDK left drawing group arrows, original direct buttons, physical pointer, coordinate entry and dimension fields create seven independent new shapes plus the existing arc in native drawing and owned CAD sketch entities. Native geometry/resource calls are read-only oracles. An external orchestrator uses separate Node/native/Chromium processes for source-free saved-project reopening; no business mocks, private handlers, cache deletion or UI writes through SDO calls. Desktop CEF and OS dialogs remain outside scope.",
  projectName: "独立形状与精确曲线验收", defaultOutput: "output/tests/iplane-cam-worktable/source-shapes",
  savedPrefix: "standalone-shapes", reportName: "shapes-phase-native-browser-report.json", noCasesMessage: "No standalone shape native browser checks ran",
});
