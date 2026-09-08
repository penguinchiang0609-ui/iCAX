import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {ProductProxy} from "../../iCAX-UI/ProductProxy/ProductProxy.mjs";
const calls = [];
const original = {productId: "tube-designer", productChannelId: "test-channel", recentProjects: [{path: "D:/项目.ictd"}]};
const proxy = new ProductProxy({invoke: async (...args) => {
  calls.push(args);
  return {...original, recentProjects: []};
}}, original);
await proxy.removeRecentProject("D:/项目.ictd");
assert.deepEqual(calls, [["test-channel", "Product.RemoveRecentProject", {projectPath: "D:/项目.ictd"}]]);
assert.deepEqual(proxy.state.recentProjects, []);
await assert.rejects(proxy.removeRecentProject(""));
const shell = readFileSync(new URL("../../iCAX-UI/SDK/AppShell/app/bootstrap.mjs", import.meta.url), "utf8");
assert.match(shell, /data-action="remove-recent-project"/);
assert.match(shell, /仅从最近项目中移除，不删除硬盘文件/);
assert.match(shell, /target\.dataset\.action !== "open-project"/);
console.log("Recent project removal tests passed");
