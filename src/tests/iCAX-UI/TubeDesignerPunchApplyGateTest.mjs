import assert from "node:assert/strict";
import { createPunchWizardState, renderPunchWizardDialog } from "../../apps/tube-designer/webpage/punchWizard.mjs";

const part = {
  entityId: "apply-gate-part",
  name: "应用门禁回归",
  length: 1000,
  independentNesting: true,
  profile: { kind: "round", diameter: 40 },
};
const state = createPunchWizardState(part);
state.catalogueStatus = "ready";
state.previewRenderError = "三维预览显示失败：503";
const view = { pending: false, tubeDesignerPunchWizard: state };
const html = renderPunchWizardDialog(part, view, { tableMode: true, showEnds: false });
const applyButton = html.match(/<button[^>]*data-cam-action="tube-designer-punch-apply"[^>]*>/)?.[0] ?? "";

assert.match(applyButton, /data-cam-action="tube-designer-punch-apply"/);
assert.doesNotMatch(applyButton, /\sdisabled(?:\s|>)/,
  "A temporary 3D resource display failure must not silently disable final Apply");
assert.match(html, /三维显示失败（不影响最终应用），请更新刀具体/);
console.log("TubeDesignerPunchApplyGateTest passed: preview display failure leaves final Apply actionable.");
