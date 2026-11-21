import { runNativePlaneBrowser } from "./iPlaneCAM.native.browserHarness.mjs";
import { exercisePolylineMenu } from "./iPlaneCAM.polyline-menu.native.browserCase.mjs";

await runNativePlaneBrowser({
  exercise: exercisePolylineMenu,
  scope: "Actual AppShell/SDK mouse context menus, five-type exact mixed polyline creation in drawing and owned sketch, current-type checks, incomplete-input protection, B-spline segment continuation, submenu pointer/keyboard lifecycle and four-corner bounds. Native geometry and ECS are real. JSON-lines transport/file dialogs are plumbing; desktop CEF and OS dialogs are outside this scope.",
  projectName: "多段线鼠标菜单原生验收", defaultOutput: "output/tests/iplane-cam-polyline-menu",
  savedPrefix: "polyline-menu", reportName: "polyline-menu-native-browser-report.json",
  noCasesMessage: "No polyline-menu acceptance cases ran",
});
