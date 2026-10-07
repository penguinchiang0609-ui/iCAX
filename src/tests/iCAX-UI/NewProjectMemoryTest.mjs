import assert from "node:assert/strict";
import { createNewProjectMemory } from "../../iCAX-UI/SDK/AppShell/app/newProjectMemory.mjs";

const records = new Map();
const storage = {
  getItem: key => records.get(key) ?? null,
  setItem: (key, value) => records.set(key, value),
  removeItem: key => records.delete(key),
};
const defaults = { name: "TubeDesigner 项目", path: "TubeDesigner 项目.ictd" };
const first = createNewProjectMemory({ storage });
assert.deepEqual(first.restore("icax.tube-designer", defaults), { ...defaults, pathTouched: false });
first.remember("icax.tube-designer", { name: "手动验收", path: "D:\\Projects\\验收.ictd" });
const next = createNewProjectMemory({ storage });
assert.deepEqual(next.restore("icax.tube-designer", defaults), {
  name: "手动验收", path: "D:\\Projects\\验收.ictd", pathTouched: true,
}, "a new document restores name and explicit path without auto-deriving a new path");
assert.deepEqual(next.restore("another-product", defaults), { ...defaults, pathTouched: false }, "product drafts are isolated");
next.remember("icax.tube-designer", { name: "", path: "" });
assert.deepEqual(createNewProjectMemory({ storage }).restore("icax.tube-designer", defaults), {
  name: "", path: "", pathTouched: true,
}, "deliberate cleared values are remembered as edits");
console.log("PASS new-project memory: persisted name/path, draft protection, product isolation and cleared values");
