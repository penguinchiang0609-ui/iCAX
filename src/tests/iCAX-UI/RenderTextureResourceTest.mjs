import assert from "node:assert/strict";
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { loadRenderResource, parseRenderTextureResource } from "../../iCAX-UI/SDK/Viewport/renderResource.mjs";

assert.ok(process.argv[2], "Pass the actual ICRT fixture emitted by native artwork acceptance");
const bytes = readFileSync(resolve(process.argv[2]));
const original = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
const digest = value => createHash("sha256").update(value).digest("hex");
const script = fileURLToPath(import.meta.url), parser = fileURLToPath(new URL("../../iCAX-UI/SDK/Viewport/renderResource.mjs", import.meta.url));
const reportPath = process.argv[3] ? resolve(process.argv[3]) : null;
const report = { schema: "icax.render-texture.protocol-test", pass: false,
  fixture: { path: resolve(process.argv[2]), sha256: digest(bytes) },
  script: { path: script, sha256: digest(readFileSync(script)) }, parser: { path: parser, sha256: digest(readFileSync(parser)) }, checks: [] };
function saveReport() { if (reportPath) { mkdirSync(dirname(reportPath), { recursive: true });writeFileSync(reportPath, JSON.stringify(report, null, 2)); } }
process.on("uncaughtExceptionMonitor", error => { report.error = error.stack ?? String(error);saveReport(); });
if (process.argv[4]) {
  const path = resolve(process.argv[4]), content = readFileSync(path, "utf8"), match = content.match(/^\[artwork display texture\] (.+)$/m);
  assert.ok(match, "Native final log must record its actual generated ICRT artifact");
  const originalPath = match[1].trim();assert.equal(digest(readFileSync(originalPath)), report.fixture.sha256, "Persistent fixture differs from the actual native artifact");
  const lines = content.trim().split(/\r?\n/);assert.match(lines.at(lines.at(-1).startsWith("Saved acceptance project:") ? -2 : -1), /^PASS:/);
  report.native = { log: { path, sha256: digest(readFileSync(path)) }, artifact: { path: originalPath, sha256: report.fixture.sha256 } };
}
const value = parseRenderTextureResource(original);
assert.equal(value.schemaVersion, 1);assert.equal(value.mimeType, "image/png");
assert.deepEqual([...value.encodedImage.slice(0, 8)], [137, 80, 78, 71, 13, 10, 26, 10]);
const png = new DataView(value.encodedImage.buffer, value.encodedImage.byteOffset, value.encodedImage.byteLength);
assert.equal(value.pixelWidth, png.getUint32(16));assert.equal(value.pixelHeight, png.getUint32(20));
const loaded = await loadRenderResource({ get: async () => new Response(original.slice(0), { headers: { "Content-Type": "application/vnd.icax.flatbuffer" } }) }, { url: "resource://native-fixture", version: "1" });
assert.equal(loaded.type, "texture");assert.deepEqual(loaded.data.encodedImage, value.encodedImage);

function field(view, index) {
  const root = view.getUint32(0, true), vtable = root - view.getInt32(root, true);
  const offset = view.getUint16(vtable + 4 + index * 2, true);
  assert.ok(offset, `The actual native fixture must explicitly contain field ${index}`);return root + offset;
}
function invalid(change, pattern) {
  const buffer = original.slice(0), view = new DataView(buffer);change(view, buffer);
  assert.throws(() => parseRenderTextureResource(buffer), pattern);
}
invalid((_view, buffer) => new Uint8Array(buffer, 4, 4).set([73, 67, 82, 71]), /identifier/);
invalid(view => view.setUint32(field(view, 0), 2, true), /schema/);
invalid(view => view.setUint32(field(view, 3), 0, true), /Invalid encoded/);
invalid(view => { const offset = field(view, 5), vector = offset + view.getUint32(offset, true);view.setUint32(vector, 0, true); }, /Invalid encoded/);
invalid(view => { const offset = field(view, 5), vector = offset + view.getUint32(offset, true);view.setUint32(vector, 0xffffffff, true); }, /outside the resource/);
invalid(view => { const offset = field(view, 2), string = offset + view.getUint32(offset, true);view.setUint8(string + 4, "t".charCodeAt(0)); }, /Invalid encoded/);
const truncatedImage = value.encodedImage.byteOffset + value.encodedImage.byteLength - 1;
for (const length of [0, 7, 12, truncatedImage]) assert.throws(() => parseRenderTextureResource(original.slice(0, length)));
await assert.rejects(() => loadRenderResource({ get: async () => new Response(null, { status: 404 }) }, { url: "resource://missing" }), /GET failed/);
report.checks = ["native-envelope-png-dimensions", "normal-resource-transport", "reject-identifier", "reject-schema", "reject-zero-dimensions", "reject-empty-image", "reject-oversized-vector", "reject-mime", "reject-truncated-buffer", "reject-missing-resource"];
report.pass = true;saveReport();
console.log("PASS: native ICRT/PNG dimensions, normal resource transport, invalid identifier/schema/dimensions/MIME, truncated and oversized vectors, missing resource");
