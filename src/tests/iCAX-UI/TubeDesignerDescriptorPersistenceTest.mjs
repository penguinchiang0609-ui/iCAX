import assert from "node:assert/strict";
import { refreshDesignerState } from "../../apps/tube-designer/webpage/designerActions.mjs";
import { renderDesignerRightPane } from "../../apps/tube-designer/webpage/designerViews.mjs";

const catalogueItem = {
  id: "security-window", version: "1.0.0", name: "防盗窗", available: true,
};
const descriptor = {
  ...catalogueItem,
  descriptorLoaded: true,
  groups: [{ key: "structure", displayName: "结构参数", order: 1 }],
  parameters: [{
    key: "faceType", displayName: "面型", type: "select", groupKey: "structure",
    group: "结构参数", defaultValue: "single",
    options: [{ value: "single", label: "单面" }, { value: "five", label: "五面" }],
  }],
};
const snapshot = () => ({ tubeDesigner: {
  // The production disassembly response marks its template as catalog-loaded
  // but does not include the parameter schema.  It must not replace the full
  // descriptor saved from the first response.
  templates: [{ ...catalogueItem, descriptorLoaded: true }],
  product: { entityId: "product-1", name: "当前防盗窗", templateId: "security-window", parameters: { faceType: "five" } },
  members: [], joints: [], parts: [], manufacturingGroups: [],
} });
let descriptorCalls = 0;
const context = {
  sceneProxy: {
    async invoke(method) {
      if (method === "TubeDesigner.List") return snapshot();
      if (method === "TubeDesigner.GetTemplateDescriptor") {
        descriptorCalls += 1;
        return { template: structuredClone(descriptor) };
      }
      throw new Error(`Unexpected request: ${method}`);
    },
  },
};
const view = { activeAreaId: "view", scene: {} };
const ops = { renderProject() {} };

await refreshDesignerState(context, view, ops);
assert.equal(descriptorCalls, 1);
assert.equal(view.scene.tubeDesigner.templates[0].descriptorLoaded, true);
assert.match(renderDesignerRightPane({}, view), /结构参数/);
assert.match(renderDesignerRightPane({}, view), /面型/);

// The next state response deliberately returns the catalogue without fields.
// The full descriptor must be restored before the parameter panel is rendered.
await refreshDesignerState(context, view, ops);
assert.equal(descriptorCalls, 1, "The detail descriptor should survive a normal scene refresh.");
assert.match(renderDesignerRightPane({}, view), /结构参数/);
assert.doesNotMatch(renderDesignerRightPane({}, view), /正在载入产品参数/);

console.log("Product parameter descriptors survive lightweight catalogue refreshes.");
