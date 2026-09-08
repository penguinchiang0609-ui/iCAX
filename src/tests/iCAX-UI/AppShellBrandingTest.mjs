import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";
import { escapeAttr, escapeText } from "../../iCAX-UI/UI/html.mjs";
import { renderRibbonCommandIcon } from "../../iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs";
import { AppProxy } from "../../iCAX-UI/AppProxy/AppProxy.mjs";
import { loadProductModule } from "../../iCAX-UI/ProductProxy/productModuleLoader.mjs";
import { createWorkbench } from "../../apps/_shared/workbench/createWorkbench.mjs";

const shellUrl = new URL("../../iCAX-UI/SDK/AppShell/app/bootstrap.mjs", import.meta.url);
const source = readFileSync(shellUrl, "utf8");
// Exercise the real renderers without connecting to a native host or mounting a viewport.
const script = source
  .replace(/^import\s*\{[\s\S]*?\}\s*from\s*"[^"]+";\s*/gm, "")
  .replaceAll("import.meta.url", JSON.stringify(shellUrl.href))
  .replace(/actions\.bootstrap\(\)\.catch\([\s\S]*$/, "")
  + "\nglobalThis.shell = { state, actions, render, getApplicationTitle, makeSafeProjectFileName };";
const root = {
  innerHTML: "",
  addEventListener() {},
  querySelector() { return null; },
  insertAdjacentHTML(_position, html) { this.innerHTML += html; },
};
const document = {
  title: "",
  getElementById() { return root; },
  querySelector() { return null; },
};
const windowListeners = new Map();
const sandbox = {
  document,
  window: { addEventListener(type, listener) { windowListeners.set(type, listener); }, clearTimeout() {}, requestAnimationFrame(callback) { callback(); } },
  performance,
  HTMLElement: class HTMLElement {},
  escapeAttr,
  escapeText,
  renderRibbonCommandIcon,
};
runInNewContext(script, sandbox, { filename: shellUrl.pathname });
let contextMenuPrevented = false;
windowListeners.get("contextmenu")({ preventDefault() { contextMenuPrevented = true; }, stopPropagation() { throw new Error("Application right-click handlers must remain active"); } });
assert.equal(contextMenuPrevented, true);
const { state, actions, render, getApplicationTitle, makeSafeProjectFileName } = sandbox.shell;
const visibleText = (html) => html.replace(/<[^>]*>/g, "");
const assertNoBranding = (html) => assert.doesNotMatch(visibleText(html), /icax/i);

await render();
assert.equal(document.title, "工作台");
assertNoBranding(root.innerHTML);
assert.match(readFileSync(new URL("../../iCAX-UI/SDK/AppShell/index.html", import.meta.url), "utf8"), /<title>工作台<\/title>/);
actions.openNewProjectDialog();
assert.equal(state.newProjectName, "未命名项目");
assert.equal(makeSafeProjectFileName("  "), "未命名项目");
actions.closeNewProjectDialog();

for (const directory of ["tube-designer", "tube-one", "laser-3d-cam"]) {
  const product = JSON.parse(readFileSync(new URL(`../../apps/${directory}/product.manifest.json`, import.meta.url), "utf8"));
  state.appState = { products: [product] };
  state.selectedProductId = product.productId;
  state.activeProductState = product;
  state.activeProjectState = { projectId: "branding-test", projectName: "测试项目" };
  state.startCenterOpen = false;
  await render();
  assert.equal(document.title, product.productName);
  assert.ok(root.innerHTML.includes(`${escapeText(product.productName)} ▾`));
  assertNoBranding(root.innerHTML);
  // Runtime responses may only carry an ID; use the registered product's display name.
  state.activeProductState = { productId: product.productId };
  assert.equal(getApplicationTitle(), product.productName);
  actions.openNewProjectDialog();
  assert.equal(state.newProjectName, `${product.productName} 项目`);
  actions.closeNewProjectDialog();
}

state.appState = { products: [{ productId: "icax.unnamed" }] };
state.selectedProductId = "icax.unnamed";
state.activeProductState = { productId: "icax.unnamed" };
state.startCenterOpen = true;
await render();
assert.equal(document.title, "工作台");
assertNoBranding(root.innerHTML);

state.activeProductState.productName = '<Product & "Name">';
await render();
assert.equal(document.title, '<Product & "Name">');
assert.ok(root.innerHTML.includes(`${escapeText(document.title)} ▾`));
state.activeProjectState = null;
state.pendingCount = 1;
state.pendingOperations = [{ label: "App.GetState" }];
await render();
assert.equal(document.title, "工作台");
assert.match(root.innerHTML, /后台任务/);
assertNoBranding(root.innerHTML);

let dialogOptions;
state.pendingCount = 0;
state.pendingOperations = [];
state.appState.products[0].projectFile = { fileExtensions: [".ictd"] };
state.bridge = { async openFileDialog(options) { dialogOptions = options; return null; } };
await actions.chooseProjectFile();
assert.equal(dialogOptions.filters[0].name, "项目文件");
assert.deepEqual(Array.from(dialogOptions.filters[0].extensions), ["ictd"]);
assert.equal(state.error, "");

let saveDialogCount = 0;
let saveCount = 0;
let chosenPath = null;
const project = {
  projectId: "save-test",
  state: { projectId: "save-test", projectName: "测试项目", projectPath: "测试项目.ictd" },
  async save(path) { saveCount++; this.state = { ...this.state, projectPath: path }; },
};
state.activeProjectProxy = project;
state.activeProjectState = project.state;
state.activeProductProxy = { async getState() { return state.activeProductState; } };
state.appProxy = { async getState() { return state.appState; } };
state.bridge.saveFileDialog = async (options) => {
  saveDialogCount++;
  assert.equal(options.defaultExtension, "ictd");
  return chosenPath;
};
await actions.saveProject();
assert.equal(saveCount, 0, "cancel must not save");
assert.equal(state.savedProjectIds.has(project.projectId), false);
chosenPath = "D:/projects/测试项目.ictd";
await actions.saveProject();
assert.equal(saveCount, 1);
assert.equal(project.state.projectPath, chosenPath);
assert.equal(state.savedProjectIds.has(project.projectId), true);
await actions.saveProject();
assert.equal(saveCount, 2);
assert.equal(saveDialogCount, 2, "subsequent save must not ask for location");
chosenPath = null;
await actions.saveProject(true);
assert.equal(saveCount, 2);
assert.equal(project.state.projectPath, "D:/projects/测试项目.ictd");
chosenPath = "D:/projects/second.ictd";
await actions.saveProject(true);
assert.equal(project.state.projectPath, chosenPath);
project.save = async () => { throw new Error("模拟写入失败"); };
await assert.rejects(actions.saveProject(), /模拟写入失败/);
assert.equal(state.error, "模拟写入失败");
assert.equal(state.pendingCount, 0);
assert.equal(project.state.projectPath, chosenPath);
console.log("PASS startup, product switching, fallback names, progress and file dialog branding");

const oldDocument = globalThis.document;
try {
  globalThis.document = document;
  const mount = { innerHTML: "" };
  createWorkbench().mountProduct({ mount, product: { projectFile: { magic: "ICAX_TEST" } } });
  assertNoBranding(mount.innerHTML);
  assert.match(mount.innerHTML, /工作台/);
} finally {
  if (oldDocument === undefined) delete globalThis.document;
  else globalThis.document = oldDocument;
}

const app = new AppProxy({}, "app", {
  bridge: { async registerProductChannel() { return "00000000-0000-0000-0000-000000000000"; } },
});
await assert.rejects(app.adoptProduct({ productId: "icax.test", isStarted: true }), (error) => {
  assert.doesNotMatch(error.message, /icax/i);
  assert.match(error.message, /产品连接/);
  return true;
});
await assert.rejects(loadProductModule({
  productId: "icax.test",
  frontendEntry: "../../iCAX-UI/UI/html.mjs",
}, new Map(), { baseUrl: import.meta.url }), (error) => {
  assert.doesNotMatch(error.message, /icax/i);
  assert.match(error.message, /产品界面无法加载/);
  return true;
});
console.log("PASS product home and connection/module error branding");
