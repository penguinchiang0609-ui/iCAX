import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
const root = fileURLToPath(new URL("../../", import.meta.url));
const server = createServer(async (req, res) => {
  if (req.url === "/") { res.setHeader("Content-Type", "text/html"); res.end("<!doctype html><body></body>"); return; }
  const path = resolve(root, "." + decodeURIComponent(req.url.split("?")[0]));
  if (!path.startsWith(resolve(root) + sep)) { res.writeHead(403).end(); return; }
  try {
    res.setHeader("Content-Type", /\.(mjs|js)$/.test(path) ? "text/javascript" : "text/html");
    res.end(await readFile(path));
  } catch { res.writeHead(404).end(); }
});
await new Promise(done => server.listen(0, "127.0.0.1", done));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  const result = await page.evaluate(async () => {
    const { createThreeViewport } = await import("/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { buildPunchPreviewRows } = await import("/apps/tube-designer/webpage/punchEditor.mjs");
    document.body.innerHTML = '<div id="host" style="width:800px;height:600px"></div>';
    const viewport = createThreeViewport({ continuousRender: false });
    viewport.mount(document.getElementById("host"));
    const box = new THREE.BoxGeometry(100, 20, 20);
    const geometry = { kind: "mesh", positions: [...box.attributes.position.array],
      indices: [...box.index.array] };
    const ref = url => ({ url, version: 1 });
    const cache = (url, type, data) => viewport.resourcePromises.set(url+"@1",
      Promise.resolve({ url, version: 1, type, data }));
    cache("blank", "geometry", geometry);
    cache("tool", "geometry", { ...geometry, positions: geometry.positions.map((v,i)=>i%3===0?v/8:v*1.1) });
    cache("blue", "material", { colorRGBA: 0x78AFC5B8 });
    cache("orange", "material", { colorRGBA: 0xF0A23D66 });
    const rows = buildPunchPreviewRows({ baseGeometry: ref("blank"), toolGeometry: ref("tool"),
      baseMaterial: ref("blue"), toolMaterial: ref("orange") });
    const client = { get() { throw Error("Unexpected resource read"); } };
    await viewport.applyViewSnapshot({ rows }, client);
    const blank = viewport.sceneObjects.get("punch-preview-blank");
    const tool = viewport.sceneObjects.get("punch-preview-tools");
    const checks = [];
    for (let i=0;i<8;i++) {
      const order=[];
      blank.onBeforeRender=()=>order.push("blank");
      tool.onBeforeRender=()=>order.push("tool");
      viewport.camera.position.set(150*Math.cos(i*Math.PI/4),150*Math.sin(i*Math.PI/4),40);
      viewport.camera.lookAt(0,0,0);
      viewport.renderer.render(viewport.scene,viewport.camera);
      checks.push(order.indexOf("tool") > order.lastIndexOf("blank"));
    }
    const materialOK = !tool.material.depthWrite && tool.material.depthTest && tool.material.transparent;
    delete rows[1].data.renderOrder;
    await viewport.applyViewSnapshot({ rows }, client);
    const resets = tool.renderOrder === 0;
    const { ResourceClient } = await import("/iCAX-UI/SDK/Resources/resourceClient.mjs");
    const decode = (encoded) => Uint8Array.from(atob(encoded), (character) => character.charCodeAt(0)).buffer;
    const geometryBytes = decode("JAAAAElDUkcAAAAAGAAwAAAABAAkAAgADAAQABQAGAAcACAAGAAAAAEAAAABAAAAAwAAAIQAAABYAAAAOAAAACQAAAAQAAAABwAAAAAAAAAAAAAAAwAAAAAAAAABAAAAAgAAAAMAAAD/ZjP//2Yz//9mM/8GAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/CQAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwkAAAAAAAAAAAAAAAAAAAAAACBBAAAAAAAAAAAAAAAAAAAgQQAAAAA=");
    const materialBytes = decode("IAAAAElDUk0YACgAAAAgAAQACAAMAAAAEAAUABgAHAAYAAAA/8xmM/8RERH/////AAAgQAMAAAAEAAAADAAAAAkAAAAAAAAAMQAAAGljYXgtcmVzb3VyY2U6Ly9hcHAvcHJvZHVjdC9wcm9qZWN0L3NjZW5lL3RleHR1cmUAAAA=");
    const batches = [];
    const batchedClient = new ResourceClient({ bridge: {
      async requestResources(requests) {
        batches.push(requests.map((request) => request.url));
        return requests.map((request) => ({ status: 200,
          body: (request.url === "batch-material" ? materialBytes : geometryBytes).slice(0) }));
      },
      requestResource() { throw Error("Repeated single-resource request"); },
    } });
    const batchRows = Array.from({ length: 88 }, (_, index) => ({
      entityId: `batch-part-${index}`,
      data: { geometry: { url: `batch-geometry-${index % 17}`, version: 7 },
        material: { url: "batch-material", version: 9 },
        geometryKind: 1, renderClass: 1, visible: true },
    }));
    const batchReceipt = await viewport.applyViewSnapshot({ revision: "batch-88", rows: batchRows }, batchedClient);
    const batchOK = batchReceipt.applied && batchReceipt.entityIds.length === 88
      && batchReceipt.missingGeometryEntityIds.length === 0
      && batches.length === 5 && batches.every((batch) => batch.length <= 4)
      && viewport.geometryObjects.get("batch-geometry-0") === viewport.sceneObjects.get("batch-part-0")?.geometry
      && viewport.sceneObjects.get("batch-part-0")?.geometry === viewport.sceneObjects.get("batch-part-17")?.geometry;
    viewport.dispose();
    return { checks, materialOK, resets, batchOK, batches: batches.length };
  });
  assert.ok(result.checks.every(Boolean), JSON.stringify(result));
  assert.ok(result.materialOK);
  assert.ok(result.resets);
  assert.ok(result.batchOK, JSON.stringify(result));
  console.log("Transparent preview ordering passed at eight camera angles; default order restored.");
} finally {
  await browser.close();
  await new Promise(done => server.close(done));
}
