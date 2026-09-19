// Real WebGL regression for one-way 防盗窗 material/process scene emphasis.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

const repositoryRoot = fileURLToPath(new URL("../../../", import.meta.url));
const packageUrl = new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/",
  import.meta.url,
);
const template = JSON.parse(readFileSync(new URL("template.json", packageUrl), "utf8"));
const generatedItems = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
import importlib.util
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
runtime = root / "src/iCAX-Engine/framework/TemplateRuntime/python"
package = root / "src/apps/tube-designer/templates/product/single_face_security_window"
sys.path.insert(0, str(runtime))
descriptor = json.loads((package / "template.json").read_text(encoding="utf-8"))
parameters = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
parameters["accessDoorEnabled"] = True
spec = importlib.util.spec_from_file_location("security_window_material_scene_linkage", package / "template.py")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
document = module.generate(parameters, {"template": descriptor, "geometryPurpose": "display"})
print(json.dumps([{
    "stableKey": item["key"],
    "role": item.get("properties", {}).get("group", ""),
} for item in document["items"]], ensure_ascii=False))
`, repositoryRoot], {
  encoding: "utf8",
  env: { ...process.env, PYTHONDONTWRITEBYTECODE: "1" },
}));
const members = generatedItems.map((item, index) => ({
  ...item,
  entityId: `security-member-${index + 1}`,
}));

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
  headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge",
});

try {
  const page = await browser.newPage({ viewport: { width: 1000, height: 720 } });
  const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://material-process-linkage.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") {
      return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    }
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/")
        || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep)
        || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://material-process-linkage.test/");
  const result = await page.evaluate(async ({ descriptor, sceneMembers }) => {
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const {
      bindProductSceneParameterHighlights,
      handleDesignerViewportPick,
    } = await import("/src/apps/tube-designer/webpage/entry.mjs");
    const { bindProductParameterDiagrams } = await import(
      "/src/apps/tube-designer/webpage/productParameterDiagram.mjs"
    );

    document.body.innerHTML = `
      <div id="viewport" style="width:620px;height:680px"></div>
      <aside id="panel" style="width:360px">
        <div data-tube-designer-product-parameter-scope data-tube-designer-editor-mode="right">
          <details data-tube-designer-parameter-group="section:materials" open>
            <details data-tube-designer-parameter-group="scene:materials:group:material" open>
              <input id="all-material" data-product-parameter-key="tubeSpecificationPreset" />
            </details>
            <details data-tube-designer-parameter-group="scene:materials:group:frame_profile" open>
              <input id="frame-profile" data-product-parameter-key="profile:frame:width" data-product-profile-role="frame" />
            </details>
          </details>
          <details data-tube-designer-parameter-group="section:process" open>
            <details data-tube-designer-parameter-group="scene:process:group:frame_process" open>
              <input id="frame-process" data-product-parameter-key="frameJoinType" />
            </details>
          </details>
          <button id="non-parameter" type="button">折叠</button>
        </div>
      </aside>`;
    const viewport = createThreeViewport({ continuousRender: false, showGrid: false });
    viewport.mount(document.querySelector("#viewport"));
    const geometry = new THREE.BoxGeometry(8, 8, 80);
    const geometryPayload = {
      kind: "mesh",
      positions: [...geometry.attributes.position.array],
      indices: [...geometry.index.array],
    };
    const cache = (url, type, data) => viewport.resourcePromises.set(`${url}@1`,
      Promise.resolve({ url, version: 1, type, data }));
    cache("member-geometry", "geometry", geometryPayload);
    cache("member-material", "material", { colorRGBA: 0x8FB8C9FF });
    const rows = sceneMembers.map((member, index) => ({
      entityId: member.entityId,
      data: {
        geometry: { url: "member-geometry", version: 1 },
        material: { url: "member-material", version: 1 },
        geometryKind: 1,
        localToWorldMatrix: [
          1, 0, 0, 0,
          0, 1, 0, 0,
          0, 0, 1, 0,
          (index % 10) * 12, Math.floor(index / 10) * 12, 0, 1,
        ],
      },
    }));
    await viewport.applyViewSnapshot({ rows }, { get() { throw new Error("unexpected read"); } });
    const view = {
      activeAreaId: "view",
      viewport,
      scene: { tubeDesigner: {
        product: { entityId: "security-window-1", templateId: descriptor.id },
        templates: [descriptor],
        members: sceneMembers,
      } },
    };
    const mount = document.body;
    bindProductSceneParameterHighlights(mount, view);
    bindProductParameterDiagrams(mount);
    const tick = () => new Promise((done) => queueMicrotask(done));
    const colorOf = (id) => {
      const object = viewport.sceneObjects.get(id);
      let color = null;
      object?.traverse?.((node) => {
        if (color == null && node.material?.color) color = node.material.color.getHex();
      });
      return color;
    };

    document.querySelector("#all-material").focus();
    await tick();
    const allMaterial = viewport.getDebugState();
    viewport.setEmphasizedObjectIds([]);

    const frameIds = sceneMembers
      .filter((member) => member.stableKey.startsWith("outer_frame."))
      .map((member) => member.entityId);
    const selectedFrameId = frameIds[0];
    viewport.setSelectedObjectIds([selectedFrameId], selectedFrameId);
    const orangeBeforePanel = colorOf(selectedFrameId);
    document.querySelector("#frame-profile").focus();
    await tick();
    const frameProfile = viewport.getDebugState();
    const purpleDuringPanel = colorOf(selectedFrameId);
    document.querySelector("#frame-profile").blur();
    await tick();
    const afterBlurIds = viewport.getDebugState().emphasizedObjectIds;
    const orangeAfterPanel = colorOf(selectedFrameId);
    bindProductParameterDiagrams(mount);
    await tick();
    const afterRebindIds = viewport.getDebugState().emphasizedObjectIds;

    document.querySelector("#frame-profile").dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
    const hoverIds = viewport.getDebugState().emphasizedObjectIds;
    document.querySelector("#frame-profile").dispatchEvent(new PointerEvent("pointerout", { bubbles: true, relatedTarget: document.querySelector("#viewport") }));
    await tick();
    const afterHoverIds = viewport.getDebugState().emphasizedObjectIds;

    document.querySelector("#frame-profile").focus();
    document.querySelector("#non-parameter").focus();
    await tick();
    const afterNonParameterIds = viewport.getDebugState().emphasizedObjectIds;

    document.querySelector("#frame-profile").focus();
    document.querySelector("#viewport").tabIndex = 0;
    document.querySelector("#viewport").focus();
    await tick();
    const afterOutsideFocusIds = viewport.getDebugState().emphasizedObjectIds;

    // Reproduce a late render/rebind after the user has left the inspector.
    await new Promise((done) => setTimeout(done, 20));
    bindProductParameterDiagrams(mount);
    const afterAsyncRebindIds = viewport.getDebugState().emphasizedObjectIds;

    document.querySelector("#frame-process").focus();
    await tick();
    const frameProcess = viewport.getDebugState();
    const focusBeforeScenePick = document.activeElement.id;
    const pickHandled = await handleDesignerViewportPick({}, view, { entityId: selectedFrameId }, {}, null, [], {});
    const focusAfterScenePick = document.activeElement.id;
    viewport.dispose();
    return {
      memberCount: sceneMembers.length,
      frameIds,
      allMaterialIds: allMaterial.emphasizedObjectIds,
      allMaterialColor: allMaterial.emphasisVisual.color,
      frameProfileIds: frameProfile.emphasizedObjectIds,
      frameProcessIds: frameProcess.emphasizedObjectIds,
      orangeBeforePanel,
      purpleDuringPanel,
      orangeAfterPanel,
      afterBlurIds,
      afterRebindIds,
      hoverIds,
      afterHoverIds,
      afterNonParameterIds,
      afterOutsideFocusIds,
      afterAsyncRebindIds,
      focusBeforeScenePick,
      focusAfterScenePick,
      pickHandled: Boolean(pickHandled),
    };
  }, { descriptor: template, sceneMembers: members });

  assert.ok(result.memberCount > 20, "必须使用真实生成的防盗窗构件集合");
  assert.deepEqual(result.allMaterialIds.sort(), members.map((member) => member.entityId).sort(),
    "整体材料应紫色强调全部防盗窗构件");
  assert.equal(result.allMaterialColor, 0xa855f7);
  assert.ok(result.frameIds.length >= 4);
  assert.deepEqual(result.frameProfileIds.sort(), result.frameIds.sort(),
    "外框管材只能强调真实外框构件");
  assert.deepEqual(result.frameProcessIds.sort(), result.frameIds.sort(),
    "外框工艺只能强调真实外框构件");
  assert.equal(result.orangeBeforePanel, 0xffad1f, "零件选择仍使用橙色");
  assert.equal(result.purpleDuringPanel, 0xa855f7, "面板联动应覆盖显示为紫色");
  assert.equal(result.orangeAfterPanel, 0xffad1f, "清除面板强调后应恢复橙色零件选择");
  assert.deepEqual(result.hoverIds.sort(), result.frameIds.sort());
  for (const key of ["afterBlurIds", "afterRebindIds", "afterHoverIds", "afterNonParameterIds", "afterOutsideFocusIds", "afterAsyncRebindIds"]) {
    assert.deepEqual(result[key], [], `${key}: 离开参数编辑后紫色联动高亮必须清除`);
  }
  assert.equal(result.focusBeforeScenePick, "frame-process");
  assert.equal(result.focusAfterScenePick, "frame-process", "场景选择不得反向定位参数面板");
  assert.equal(result.pickHandled, false);
  console.log("防盗窗材料/工艺单向场景联动通过：真实构件映射、紫色强调、橙色选择恢复、无反向定位。");
} finally {
  await browser.close();
}
