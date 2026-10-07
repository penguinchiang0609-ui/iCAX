// Render native GenerateProductTemplatePreview meshes with the production SDK.
// Geometry, transforms and parameters come from the native corner regression.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";

const root = fileURLToPath(new URL("../../../", import.meta.url));
const source = resolve(root, "src");
const output = resolve(root, "output/tests/preview-frame-corners");
mkdirSync(output, { recursive: true });
const inputFiles = process.argv.slice(2);
assert(inputFiles.length, "Provide native mesh snapshot JSON files");
const browser = await chromium.launch({ channel: "msedge", headless: true });
const report = [];
try {
  const page = await browser.newPage({ viewport: { width: 1050, height: 850 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://native-frame.test/**", route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === "/") return route.fulfill({ contentType: "text/html", body:
      '<!doctype html><html><head><style>html,body,#viewport{margin:0;width:100%;height:100%;overflow:hidden}</style></head><body><div id="viewport"></div></body></html>' });
    const file = resolve(source, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(source + sep)) return route.abort();
    try { return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") }); }
    catch { return route.abort(); }
  });
  for (const inputFile of inputFiles) {
    const native = JSON.parse(readFileSync(resolve(root, inputFile), "utf8"));
    assert(native.preview?.items?.length && native.meshes, "Native preview and meshes required");
    await page.goto("http://native-frame.test/");
    const receipt = await page.evaluate(async native => {
      const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
      const viewport = createThreeViewport({ backgroundColor: 0x12262d, pickingEnabled: false,
        continuousRendering: false, projectionMode: "orthographic" });
      viewport.mount(document.getElementById("viewport"));
      const rows = native.preview.items.map(item => {
        const mesh = native.meshes[item.geometry.url];
        if (!mesh?.vertices?.length || !mesh?.triangles?.length)
          throw new Error("Missing native triangles: " + item.key);
        const version = String(item.geometry.version ?? 0);
        viewport.resourcePromises.set(item.geometry.url + "@" + version, Promise.resolve({
          url: item.geometry.url, version, type: "geometry", data: {
            kind: "mesh", positions: mesh.vertices.flat(), indices: mesh.triangles.flat(),
            normals: mesh.normals.flat(),
          } }));
        return { entityId: item.entityId, data: { geometry: item.geometry,
          localToWorldMatrix: item.transform, flags: 3 } };
      });
      const receipt = await viewport.applyViewSnapshot({ viewId: "native-frame", revision: "1", rows },
        { get() { throw new Error("Native fixture did not supply a resource"); } });
      viewport.setViewDirection([.13, -1, .08]);
      viewport.fitViewToViewport(1.12);
      globalThis.__nativeFrameViewport = viewport;
      return { ...receipt, bounds: viewport.getVisibleBounds(), items: rows.length };
    }, native);
    assert.equal(receipt.applied, true);
    assert.equal(receipt.items, native.preview.items.length);
    assert(receipt.bounds && receipt.bounds.min.every(Number.isFinite));
    const face = native.preview.parameters.faceType;
    await page.screenshot({ path: resolve(output, `${face}-frame.png`) });
    if (face === "single") {
      await page.evaluate(() => {
        const viewport = globalThis.__nativeFrameViewport;
        const bounds = viewport.getVisibleBounds();
        viewport.setCameraState({ ...viewport.getCameraState(), projectionMode: "orthographic",
          target: { x: bounds.max[0] - 43, y: (bounds.min[1] + bounds.max[1]) / 2, z: bounds.max[2] - 43 },
          radius: 370, theta: -Math.PI / 2 - .15, phi: Math.PI / 2 - .08 });
      });
      await page.screenshot({ path: resolve(output, "single-corner-detail.png") });
    }
    report.push({ face, ...receipt });
  }
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, "viewport-report.json"), JSON.stringify({ report, errors }, null, 2));
  console.log(JSON.stringify(report.map(({ face, applied, items, bounds, missingGeometryEntityIds }) =>
    ({ face, applied, items, bounds, missingGeometryEntityIds }))));
} finally { await browser.close(); }
