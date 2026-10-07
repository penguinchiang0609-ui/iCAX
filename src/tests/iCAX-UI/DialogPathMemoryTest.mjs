import assert from "node:assert/strict";
import { installDialogPathMemory, validateBridge } from "../../iCAX-UI/SDK/Bridge/createBridge.mjs";

const data = new Map();
const storage = {
  getItem: key => data.get(key) ?? null,
  setItem: (key, value) => data.set(key, value),
  removeItem: key => data.delete(key),
};
function fixture(results = []) {
  const calls = [];
  const bridge = {
    getApplicationChannelId() {}, registerProductChannel() {}, registerSceneChannel() {},
    postSDOFrame() {}, requestResource() {}, subscribeSDOFrames() {},
    async openFileDialog(options) { assert.equal(this, bridge); calls.push({ method: "open", options }); return results.shift(); },
    async saveFileDialog(options) { assert.equal(this, bridge); calls.push({ method: "save", options }); return results.shift(); },
    async openDirectoryDialog(options) { assert.equal(this, bridge); calls.push({ method: "directory", options }); return results.shift(); },
  };
  const returned = installDialogPathMemory(bridge, { storage, namespace: "test.native-dialogs" });
  assert.equal(returned, bridge, "native bridge identity is retained");
  return { bridge, calls };
}

{
  const first = fixture(["D:\\CAD\\真实加工.step", null]);
  const options = { memoryKey: "import-parts", title: "导入零件", filters: [{ name: "STEP", extensions: ["step"] }] };
  await first.bridge.openFileDialog(options);
  const currentMethod = first.bridge.openFileDialog;
  installDialogPathMemory(first.bridge, { storage });
  validateBridge(first.bridge);
  assert.equal(first.bridge.openFileDialog, currentMethod, "validation installs wrappers once");
  const detached = first.bridge.openFileDialog;
  await detached(options);
  assert.equal(first.calls[1].options.defaultPath, "D:\\CAD\\", "open chooser starts in last successful parent directory");
  assert.equal(options.defaultPath, undefined, "caller options are not mutated");
  assert.equal(first.calls[0].options.memoryKey, undefined, "memory metadata is not sent to native");

  // A new bridge represents the next document/program using the same persisted store.
  const next = fixture(["", "E:\\other\\new.step"]);
  await next.bridge.openFileDialog({ ...options, title: "renamed caption" });
  assert.equal(next.calls[0].options.defaultPath, "D:\\CAD\\", "explicit key survives changed captions and cancellation");
  await next.bridge.openFileDialog({ title: "another operation", filters: options.filters });
  assert.equal(next.calls[1].options.defaultPath, undefined, "unrelated operation locations are isolated");
}

{
  const first = fixture(["D:/exports/old.xlsx"]);
  await first.bridge.saveFileDialog({ memoryKey: "parts-export", defaultPath: "old.xlsx" });
  const next = fixture([null, "C:/projects/current.ictd", null]);
  await next.bridge.saveFileDialog({ memoryKey: "parts-export", defaultPath: "new.xlsx" });
  assert.equal(next.calls[0].options.defaultPath, "D:/exports/new.xlsx", "new export retains new filename in remembered directory");
  await next.bridge.saveFileDialog({ memoryKey: "parts-export", defaultPath: "C:/projects/current.ictd" });
  assert.equal(next.calls[1].options.defaultPath, "C:/projects/current.ictd", "explicit entity path wins over chooser memory");
  await next.bridge.saveFileDialog({ memoryKey: "parts-export", defaultPath: "projects/current.ictd" });
  assert.equal(next.calls[2].options.defaultPath, "projects/current.ictd", "relative entity paths with their own location are preserved too");
  const reopened = fixture([null]);
  await reopened.bridge.saveFileDialog({ memoryKey: "parts-export", defaultPath: "after.xlsx" });
  assert.equal(reopened.calls[0].options.defaultPath, "C:/projects/after.xlsx");
}

{
  const first = fixture(["\\\\server\\parts", null]);
  await first.bridge.openDirectoryDialog({ memoryKey: "export-directory" });
  await first.bridge.openDirectoryDialog({ memoryKey: "export-directory" });
  assert.equal(first.calls[1].options.initialDirectory, "\\\\server\\parts");
  const next = fixture([null, null]);
  await next.bridge.openDirectoryDialog({ memoryKey: "export-directory" });
  assert.equal(next.calls[0].options.initialDirectory, "\\\\server\\parts", "folder cancellation retains location across recreated bridge");
  await next.bridge.openDirectoryDialog({ memoryKey: "export-directory", initialDirectory: "E:/configured" });
  assert.equal(next.calls[1].options.initialDirectory, "E:/configured", "explicit configured directory is preserved");
}

{
  const first = fixture(["C:\\root.step"]);
  await first.bridge.openFileDialog({ title: "root chooser" });
  const next = fixture([null]);
  await next.bridge.openFileDialog({ title: "root chooser" });
  assert.equal(next.calls[0].options.defaultPath, "C:\\", "drive root survives path normalization");

  const mapped = fixture([null]);
  await mapped.bridge.openFileDialog({ title: "explicit start", initialDirectory: "/tmp/cad", defaultPath: "chosen.step" });
  assert.deepEqual(mapped.calls[0].options, { title: "explicit start", defaultPath: "/tmp/cad/chosen.step" }, "directory maps to actual OPENFILENAMEW transport field");
}

{
  for (const method of ["openFileDialog", "saveFileDialog", "openDirectoryDialog"]) {
    const calls = [], reads = [], writes = [];
    const memory = {
      read(...args) { reads.push(args); return "D:/remembered"; },
      write(...args) { writes.push(args); },
    };
    const bridge = { async [method](options) {
      assert.equal(this, bridge, "opted-out chooser retains its native binding");
      calls.push(options);
      return calls.length === 1 ? (method === "openDirectoryDialog" ? "E:/excluded" : "E:/excluded/selected.step") : null;
    } };
    installDialogPathMemory(bridge, { memory });
    const nativeOptions = { title: "per-page chooser", defaultPath: "requested.step", initialDirectory: "C:/explicit", filters: [{ name: "STEP", extensions: ["step"] }] };
    const requested = { ...nativeOptions, memoryKey: false };
    const before = structuredClone(requested);
    const result = await bridge[method](requested);
    assert.equal(typeof result, "string", "opted-out chooser returns the original successful path");
    assert.deepEqual(calls[0], nativeOptions, `${method} opt-out passes original native options unchanged`);
    assert.deepEqual(requested, before, "opt-out does not mutate caller options");
    assert.deepEqual(reads, [], "opt-out does not read remembered locations");
    assert.deepEqual(writes, [], "opt-out does not save successful selections");
    await bridge[method]({ title: "per-page chooser", defaultPath: "next.step" });
    assert.equal(reads.length, 1, "opt-out applies to one invocation; later invocations can remember locations");
    assert.equal(method === "openDirectoryDialog" ? calls[1].initialDirectory : calls[1].defaultPath,
      method === "openDirectoryDialog" ? "D:/remembered" : "D:/remembered/next.step",
      "opted-out successful selection leaves the previous remembered location intact");
    assert.deepEqual(writes, [], "subsequent cancellation retains the old location");
  }
}

{
  const first = fixture(["D:/safe/a.step"]);
  await first.bridge.openFileDialog({ title: "error chooser" });
  const broken = { async openFileDialog() { throw new Error("native chooser failed"); } };
  installDialogPathMemory(broken, { storage, namespace: "test.native-dialogs" });
  await assert.rejects(broken.openFileDialog({ title: "error chooser" }), /native chooser failed/);
  const next = fixture([null]);
  await next.bridge.openFileDialog({ title: "error chooser" });
  assert.equal(next.calls[0].options.defaultPath, "D:/safe/", "native errors do not replace previous successful directory");
  const unsupported = { nativeValue: 1 };
  assert.equal(installDialogPathMemory(unsupported, { storage }), unsupported);
  assert.equal(unsupported.openFileDialog, undefined, "optional chooser methods are not fabricated");
}

console.log("PASS native dialog path memory: recreated document, per-invocation opt-out, cancellation/error, operation isolation, save filename, native binding and transport mapping");
