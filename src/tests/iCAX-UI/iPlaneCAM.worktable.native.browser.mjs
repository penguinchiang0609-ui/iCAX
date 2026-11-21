import { runNativePlaneBrowser } from "./iPlaneCAM.native.browserHarness.mjs";
import { exerciseWorktable } from "./iPlaneCAM.worktable.native.browserCase.mjs";

await runNativePlaneBrowser({
  exercise: exerciseWorktable,
  scope: "Actual AppShell/SDK mouse and controls create/edit/place text, import textured image resources, transform/reference/vectorize images, import CAD parts, generate real nesting and load independent machining copies. Native reads are assertions only. Embedded pixels, renderer UV/material maps, reference selectability, history, asynchronous interaction and source-free save/reopen are inspected. JSON-lines transport and file-dialog responses are plumbing; desktop CEF/OS dialogs are outside scope.",
  projectName: "底图文字图片与加工副本验收", defaultOutput: "output/tests/iplane-cam-worktable/source-worktable",
  savedPrefix: "worktable", reportName: "worktable-native-browser-report.json",
  noCasesMessage: "No worktable acceptance cases ran",
});
