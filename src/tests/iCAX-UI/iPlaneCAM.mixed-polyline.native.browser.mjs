import { runNativePlaneBrowser } from "./iPlaneCAM.native.browserHarness.mjs";
import { exerciseMixedPolyline } from "./iPlaneCAM.mixed-polyline.native.browserCase.mjs";

await runNativePlaneBrowser({
  exercise: exerciseMixedPolyline,
  scope: "Actual AppShell and SDK mixed-polyline controls, mouse preview, exact native line/arc/ellipseArc/Bezier/B-spline EC geometry, tangent dimensions, cancellation/backtracking, layers, owned sketch completion, history, asynchronous refresh and save/reopen. JSON-lines transport and file dialog responses are test plumbing; desktop CEF and OS dialogs are outside this scope.",
  projectName: "混合多段线原生验收", defaultOutput: "output/tests/iplane-cam-mixed-polyline",
  savedPrefix: "mixed-polyline", reportName: "mixed-polyline-native-browser-report.json",
  noCasesMessage: "No mixed-polyline acceptance cases ran",
});