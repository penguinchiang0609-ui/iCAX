// Real-browser regression for the Python-authored 防盗窗 annotation lanes.
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
const descriptor = JSON.parse(readFileSync(new URL("template.json", packageUrl), "utf8"));
const display = JSON.parse(readFileSync(new URL("display.json", packageUrl), "utf8"));
const generated = JSON.parse(execFileSync(process.env.ICAX_PYTHON || "python", ["-c", String.raw`
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
spec = importlib.util.spec_from_file_location("security_window_annotation_layout_browser", package / "template.py")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
document = module.generate(parameters, {"template": descriptor, "geometryPurpose": "display"})
multi_door_bar_parameters = dict(parameters)
multi_door_bar_parameters["doorHorizontalBottomCenterOffset"] = 100
multi_door_bar_parameters["doorHorizontalTopCenterOffset"] = 100
multi_door_bar_document = module.generate(
    multi_door_bar_parameters, {"template": descriptor, "geometryPurpose": "display"})
print(json.dumps({
    "parameters": parameters,
    "annotations": document["extensions"]["tubeDesigner.specificationAnnotations"],
    "multiDoorBarAnnotations": multi_door_bar_document["extensions"]["tubeDesigner.specificationAnnotations"],
}, ensure_ascii=False))
`, repositoryRoot], {
  encoding: "utf8",
  env: { ...process.env, PYTHONDONTWRITEBYTECODE: "1" },
}));

for (const parameter of [
  "horizontalMaximumCenterSpacing",
  "verticalMaximumCenterSpacing",
  "doorVerticalMaximumCenterSpacing",
]) {
  assert.ok(generated.annotations.some((item) => item.parameter === parameter),
    `防盗窗生成结果缺少中心距标注：${parameter}`);
}
assert.ok(!generated.annotations.some(
  (item) => item.parameter === "doorHorizontalMaximumCenterSpacing"),
"逃生窗只有一根横杆时不应显示不存在的杆间中心距");
assert.ok(generated.multiDoorBarAnnotations.some(
  (item) => item.parameter === "doorHorizontalMaximumCenterSpacing"),
"逃生窗存在两根以上横杆时必须显示横杆中心距");

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
  headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge",
});

try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 980 } });
  const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://security-window-annotation-layout.test/**", (route) => {
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
  await page.goto("http://security-window-annotation-layout.test/");
  const result = await page.evaluate(async ({ template, displayRules, model }) => {
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const { resolveProductSpecificationAnnotations } = await import(
      "/src/apps/tube-designer/webpage/productParameterDiagram.mjs"
    );
    document.body.innerHTML = '<div id="host" style="width:1100px;height:980px"></div>';
    const viewport = createThreeViewport({ continuousRender: false, showGrid: false });
    viewport.mount(document.querySelector("#host"));
    viewport.camera.position.set(1450, -3200, 1550);
    viewport.camera.lookAt(600, 0, 900);
    const designer = {
      templates: [{ ...template, display: displayRules }],
      product: {
        entityId: "security-window-layout",
        templateId: template.id,
        parameters: model.parameters,
      },
      specificationAnnotations: model.annotations,
    };
    const annotations = resolveProductSpecificationAnnotations(designer, {
      tubeDesignerRightDraft: {
        ...model.parameters,
        width: Number(model.parameters.width) + 200,
        verticalMaximumCenterSpacing: Number(model.parameters.verticalMaximumCenterSpacing) + 10,
      },
    });
    viewport.setSpecificationAnnotations(annotations);
    viewport.renderer.render(viewport.scene, viewport.camera);
    await new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)));
    const entries = [...document.querySelectorAll(".icax-three-specification-label:not([hidden])")]
      .map((element) => {
        const rect = element.getBoundingClientRect();
        const control = element.querySelector("[data-tube-designer-parameter-key]");
        return {
          parameter: control?.dataset?.tubeDesignerParameterKey ?? element.textContent,
          left: rect.left, right: rect.right, top: rect.top, bottom: rect.bottom,
        };
      });
    const overlaps = [];
    for (let left = 0; left < entries.length; left += 1) {
      for (let right = left + 1; right < entries.length; right += 1) {
        const a = entries[left];
        const b = entries[right];
        const overlapWidth = Math.min(a.right, b.right) - Math.max(a.left, b.left);
        const overlapHeight = Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top);
        if (overlapWidth > 2 && overlapHeight > 2) {
          overlaps.push([a.parameter, b.parameter, Math.round(overlapWidth), Math.round(overlapHeight)]);
        }
      }
    }
    const offsets = Object.fromEntries(model.annotations.map((item) => [item.parameter, item.offset]));
    viewport.dispose();
    return { count: entries.length, overlaps, offsets };
  }, { template: descriptor, displayRules: display, model: generated });

  assert.ok(result.count >= 10, "应显示完整的主体和逃生窗规格标注");
  assert.deepEqual(result.overlaps, [], `防盗窗标注发生覆盖：${JSON.stringify(result.overlaps)}`);
  assert.ok(Math.abs(result.offsets.height[0]) > Math.abs(result.offsets.doorClearHeight[0]),
    "整体高度和逃生窗净高必须使用不同的左侧通道");
  assert.ok(Math.abs(result.offsets.horizontalMaximumCenterSpacing[0]) > Math.abs(result.offsets.doorHorizontalTopCenterOffset[0]),
    "主体横杆和窗内横杆必须使用不同的右侧通道");
  const crowded = await page.evaluate(async () => {
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    document.body.innerHTML = '<div id="crowded-host" style="width:900px;height:680px"></div>';
    const viewport = createThreeViewport({ continuousRender: false, showGrid: false });
    viewport.mount(document.querySelector("#crowded-host"));
    viewport.camera.position.set(0, -1800, 0);
    viewport.camera.lookAt(0, 0, 0);
    viewport.setSpecificationAnnotations(Array.from({ length: 14 }, (_, index) => ({
      id: `crowded-${index}`,
      parameter: `crowded-${index}`,
      start: [-80, 0, -20],
      end: [80, 0, -20],
      offset: [0, 0, 0],
      label: `规格参数 ${index + 1}：1,200 mm`,
      editable: false,
    })));
    await new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)));
    const entries = [...document.querySelectorAll(".icax-three-specification-label:not([hidden])")]
      .map((element) => element.getBoundingClientRect());
    const overlaps = entries.flatMap((first, left) => entries.slice(left + 1).map((second) => ({ first, second })))
      .filter(({ first, second }) => Math.min(first.right, second.right) - Math.max(first.left, second.left) > 2
        && Math.min(first.bottom, second.bottom) - Math.max(first.top, second.top) > 2);
    viewport.dispose();
    return { count: entries.length, overlaps: overlaps.length };
  });
  assert.equal(crowded.count, 14, "拥挤的规格标注不能被丢弃");
  assert.equal(crowded.overlaps, 0, "投影重叠的规格标注必须自动避让");
  console.log("PASS 防盗窗 Python 标注通道在真实浏览器投影下无文字覆盖");
} finally {
  await browser.close();
}
