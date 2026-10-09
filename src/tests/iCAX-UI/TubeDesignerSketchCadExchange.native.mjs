// Real registered SDO services, installed system fonts, and file exchange.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { copyFileSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { createInterface } from "node:readline";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { buildSketchDxf, parseSketchDxf } from "../../apps/tube-designer/webpage/sketchCadExchange.mjs";

const root = fileURLToPath(new URL("../../../", import.meta.url));
const output = resolve(root, "output/tests/sketch-cad-exchange-native"), runtime = resolve(output, "runtime");
mkdirSync(runtime, { recursive: true });
const dllDirectory = process.env.ICAX_SIDE_SKETCH_NATIVE_DLL_DIR || resolve(root, "src/x64/Debug");
const executable = resolve(runtime, "SideSketchAcceptanceBridge.exe");
copyFileSync(process.env.ICAX_SIDE_SKETCH_NATIVE_BRIDGE || resolve(root, "output/tests/side-sketch-native-browser/native/SideSketchAcceptanceBridge.exe"), executable);
const processBridge = spawn(executable, [], { cwd: root, windowsHide: true, env: { ...process.env, PATH: dllDirectory + ";" + process.env.PATH }, stdio: ["pipe", "pipe", "pipe"] });
const pending = new Map(), results = []; let sequence = 0, stderr = "";
processBridge.stderr.on("data", buffer => { stderr += buffer; });
createInterface({ input: processBridge.stdout }).on("line", line => {
  const response = JSON.parse(line), request = pending.get(response.id); if (!request) return;
  pending.delete(response.id); clearTimeout(request.timer);
  if (response.ok) request.resolve(response.result); else request.reject(new Error(response.error));
});
processBridge.on("error", error => { for (const value of pending.values()) { clearTimeout(value.timer); value.reject(error); } pending.clear(); });
const rpc = value => new Promise((resolve, reject) => {
  const id = ++sequence, timer = setTimeout(() => { pending.delete(id); reject(new Error("Native test timeout: " + JSON.stringify(value))); }, 30000);
  pending.set(id, { resolve, reject, timer }); processBridge.stdin.write(JSON.stringify({ id, ...value }) + "\n");
});
const invoke = (method, payload) => rpc({ action: "invoke", scope: "product", method, payload });
try {
  for (const text of ["BO8", "管材", "测试 A"] ) {
    const response = await invoke("GenerateSketchTextOutline", { text, fontFamily: "Microsoft YaHei", height: 20, x: 30, y: 100, rotation: 0.25, letterSpacing: 1 });
    assert.equal(response.bOK, true, text + ": " + response.message);
    assert.ok(response.entities.length > 0); assert.ok(response.advanceX > 0);
    assert.ok(response.entities.every(entity => entity.kind === "path" && entity.closed && entity.fillRule === "evenodd"));
    assert.ok(response.entities.flatMap(entity => entity.segments).some(segment => segment.kind === "bezier"), "real curved glyphs have exact Bézier outlines");
    const groups = new Set(response.entities.map(entity => entity.fillGroup));
    if (text === "BO8") assert.ok(response.entities.length > groups.size, "glyphs retain their internal contours");
    const dxf = buildSketchDxf(response.entities), imported = parseSketchDxf(dxf);
    assert.ok(imported.entities.length > 0);
    results.push({ case: "installed system font exact outline", text, contours: response.entities.length, glyphGroups: groups.size, advanceX: response.advanceX });
  }
  for (const payload of [{ text: "", fontFamily: "Arial" }, { text: "O", fontFamily: "does-not-exist-ICAX" }, { text: "O", height: 0 }, { text: "line\nbreak" }]) {
    const response = await invoke("GenerateSketchTextOutline", payload); assert.equal(response.bOK, false); assert.ok(response.message.length > 0);
  }
  results.push({ case: "font failure is explicit", invalidCases: 4 });
  const sources = [{ kind: "circleArc", cx: -10, cy: 80, radius: 4, startAngle: 0.3, sweep: -4.1 }, { kind: "ellipseArc", cx: 30, cy: 100, radiusX: 3, radiusY: 7, rotation: 0.4, startAngle: -2, sweep: -0.9 }, { kind: "line", x1: 0, y1: 0, x2: 10, y2: 10 }];
  const content = buildSketchDxf(sources), targetPath = resolve(output, "roundtrip.dxf");
  await invoke("ExportSketchDxf", { targetPath, content });
  assert.equal(readFileSync(targetPath, "utf8"), content);
  const response = await invoke("ReadSketchDxf", { sourcePath: targetPath });
  assert.equal(response.content, content); assert.equal(parseSketchDxf(response.content).entities.length, 3);
  results.push({ case: "real native DXF file write read", entities: 3 });
  await rpc({ action: "exit" }); processBridge.stdin.end();
  writeFileSync(resolve(output, "report.json"), JSON.stringify({ dllSha256: createHash("sha256").update(readFileSync(resolve(dllDirectory, "TubeDesigner.dll"))).digest("hex"), nativeTransport: "production SDO", results, stderr }, null, 2));
  console.log(`CAD exchange native regression passed (${results.length} scenarios).`);
} finally { if (!processBridge.stdin.destroyed) processBridge.stdin.end(); }
