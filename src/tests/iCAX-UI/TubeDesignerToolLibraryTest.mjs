import assert from "node:assert/strict";
import {
  libraryTools,
  renderToolLibraryLeftPane,
  renderToolLibraryRightPane,
  toolLibraryState,
  visibleLibraryTools,
} from "../../apps/tube-designer/webpage/toolLibrary.mjs";

const view = {
  tubeDesignerSystemPunchTools: [
    { id: "v-notch", displayName: "V 槽", kind: "programmatic", category: "槽口", version: "1.1.0", parameters: [{ key: "angle", displayName: "V 槽夹角" }] },
    { id: "diamond-12", displayName: "菱形孔", kind: "fixed", category: "孔", version: "1.0.0", parameters: [] },
  ],
  tubeDesignerTemplatePunchTools: [
    { id: "template-v", displayName: "模板 V 槽", kind: "programmatic", libraryScope: "template", templateId: "guardrail", templateName: "护栏模板" },
  ],
  tubeDesignerUserData: {
    punchTools: [{ id: "user-v", name: "我的 V 槽", kind: "fixed", libraryScope: "user" }],
  },
};

assert.equal(libraryTools(view).length, 4);
assert.equal(toolLibraryState(view).scope, "system");
assert.equal(visibleLibraryTools(view).length, 2);
assert.equal(visibleLibraryTools(view).filter((tool) => tool.kind === "fixed").length, 1);
view.tubeDesignerToolLibrary = { scope: "system", type: "all", category: "slot", search: "", selectedKey: "" };
assert.deepEqual(visibleLibraryTools(view).map((tool) => tool.id), ["v-notch"]);
assert.match(renderToolLibraryLeftPane({}, view), /模具类别/);
assert.match(renderToolLibraryLeftPane({}, view), /tube-tool-library-card-art/);

view.tubeDesignerToolLibrary = { scope: "template", type: "all", search: "", selectedKey: "" };
assert.equal(visibleLibraryTools(view).length, 1);
assert.match(renderToolLibraryLeftPane({}, view), /模具库/);
assert.match(renderToolLibraryLeftPane({}, view), /系统内置/);
assert.match(renderToolLibraryLeftPane({}, view), /模板自带/);
assert.match(renderToolLibraryLeftPane({}, view), /我的/);
assert.match(renderToolLibraryLeftPane({}, view), /模板 V 槽/);
assert.match(renderToolLibraryRightPane({}, view), /标准拉伸体/);

view.tubeDesignerToolLibrary.type = "fixed";
assert.equal(visibleLibraryTools(view).length, 0);
assert.match(renderToolLibraryLeftPane({}, view), /还没有定式模具/);

console.log("TubeDesigner tool library tests passed");
