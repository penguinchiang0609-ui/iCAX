import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createWindowStateMemory } from "../../iCAX-UI/SDK/Forms/windowStateMemory.mjs";
import { makeLibraryNavigationFixture } from "./libraryNavigationFixture.mjs";
import { renderProductTemplateLibraryLeftPane } from "../../apps/tube-designer/webpage/templateLibrary.mjs";
import { renderProfileLibraryLeftPane } from "../../apps/tube-designer/webpage/profileLibrary.mjs";
import { renderToolLibraryLeftPane } from "../../apps/tube-designer/webpage/toolLibrary.mjs";
import { renderAssemblyLibraryLeftPane } from "../../apps/tube-designer/webpage/assemblyLibrary.mjs";
import { renderComponentLibraryLeftPane } from "../../apps/tube-designer/webpage/componentLibrary.mjs";

const data = new Map();
globalThis.localStorage = { getItem: key => data.get(key) ?? null, setItem: (key, value) => data.set(key, value) };
const { restoreLibraryNavigation, rememberLibraryNavigation } = await import("../../apps/tube-designer/webpage/libraryNavigationMemory.mjs");
const memory = createWindowStateMemory({ namespace: "icax.tube-designer.library-navigation" });
const assemblies = ["bend", "wrap-a-over-b"].map(id => JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/assembly/${id}/assembly.json`, import.meta.url), "utf8")));
const fixture = () => makeLibraryNavigationFixture(assemblies);
const render = view => [renderProductTemplateLibraryLeftPane, renderProfileLibraryLeftPane,
  renderToolLibraryLeftPane, renderAssemblyLibraryLeftPane, renderComponentLibraryLeftPane].map(fn => fn({}, view));
const selected = view => ({ templates: view.tubeDesignerProductTemplateLibrary.selectedId,
  profiles: view.tubeDesignerSelectedProfileId, tools: view.tubeDesignerToolLibrary.selectedKey,
  assemblies: view.tubeDesignerAssemblyLibrary.selectedId, components: view.tubeDesignerComponentLibrary.selectedKey });
const expected = { templates: "user-b", profiles: "user:user-b", tools: "user::user-b", assemblies: "wrap-a-over-b", components: "user:user-b" };

const view = fixture();
restoreLibraryNavigation(view);
Object.assign(view.tubeDesignerProductTemplateLibrary, { scope: "user", search: "验收", selectedId: expected.templates, collapsed: ["custom-group"] });
Object.assign(view.tubeDesignerProfileLibrary, { scope: "user", search: "验收", type: "fixed", selectedByScope: { user: expected.profiles, system: "system:system-b" }, collapsed: ["user:方矩管"] });
view.tubeDesignerSelectedProfileId = expected.profiles;
Object.assign(view.tubeDesignerToolLibrary, { scope: "user", search: "验收", type: "fixed", category: "slot", selectedKey: expected.tools, collapsed: ["slot"],
  preview: { geometry: "must-not-be-saved" }, parameterDrafts: { private: { width: 101 } } });
Object.assign(view.tubeDesignerAssemblyLibrary, { search: "验收", selectedId: expected.assemblies, collapsed: ["l"],
  preview: { geometry: "must-not-be-saved" }, sceneProfileDiagramCache: new Map([["private", "mesh"]]) });
Object.assign(view.tubeDesignerComponentLibrary, { scope: "user", search: "验收", selectedKey: expected.components,
  selectedByScope: { user: expected.components, system: "system:system-a" }, collapsed: ['["user","","连接"]'] });
render(view);
rememberLibraryNavigation(view);
assert.deepEqual(selected(view), expected);
const allowed = new Set(["scope", "search", "type", "category", "selectedId", "selectedKey", "selectedByScope", "collapsed"]);
for (const row of JSON.parse([...data.values()][0]).windows) {
  assert.ok(Object.keys(row.fields).every(key => allowed.has(key)), `${row.key} contains non-navigation state`);
  assert.ok(!JSON.stringify(row.fields).includes("must-not-be-saved"));
}
assert.equal(view.tubeDesignerToolLibrary.parameterDrafts.private.width, 101);

const reopened = fixture();
restoreLibraryNavigation(reopened);
render(reopened);
assert.deepEqual(selected(reopened), expected, "all five actual catalogue renderers restore the selected resource");
for (const property of ["tubeDesignerProductTemplateLibrary", "tubeDesignerProfileLibrary", "tubeDesignerToolLibrary", "tubeDesignerComponentLibrary"]) {
  assert.equal(reopened[property].scope, "user");
  assert.equal(reopened[property].search, "验收");
}
assert.equal(reopened.tubeDesignerProfileLibrary.type, "fixed");
assert.equal(reopened.tubeDesignerToolLibrary.category, "slot");
assert.deepEqual(reopened.tubeDesignerAssemblyLibrary.collapsed, ["l"]);
reopened.tubeDesignerToolLibrary.selectedKey = "user::user-a";
restoreLibraryNavigation(reopened);
assert.equal(reopened.tubeDesignerToolLibrary.selectedKey, "user::user-a", "ordinary rerenders cannot reset a newer selection");

const loading = { scene: { tubeDesigner: {} }, tubeDesignerUserDataLoading: true };
restoreLibraryNavigation(loading);
render(loading);
rememberLibraryNavigation(loading);
for (const [area, value] of Object.entries(expected))
  assert.equal(memory.read(area, ["templates", "assemblies"].includes(area) ? "selectedId" : "selectedKey"), value,
    `${area}: the initial empty catalogue must not erase disk selection`);
const ready = fixture();
Object.assign(loading, { scene: ready.scene, tubeDesignerUserDataLoading: false, tubeDesignerUserData: ready.tubeDesignerUserData,
  tubeDesignerSystemProfiles: ready.tubeDesignerSystemProfiles, tubeDesignerSystemPunchTools: ready.tubeDesignerSystemPunchTools,
  tubeDesignerAssemblyTemplates: ready.tubeDesignerAssemblyTemplates });
loading.tubeDesignerToolLibrary.catalogueStatus = "ready";
loading.tubeDesignerAssemblyLibrary.catalogueStatus = "ready";
Object.assign(loading.tubeDesignerComponentLibrary, { loadState: "loaded", models: ready.tubeDesignerComponentLibrary.models });
restoreLibraryNavigation(loading);
render(loading);
assert.deepEqual(selected(loading), expected, "remembered cards are restored only after async catalogue completion");

const filteredWhileLoading = { scene: { tubeDesigner: {} }, tubeDesignerUserDataLoading: true };
restoreLibraryNavigation(filteredWhileLoading);
filteredWhileLoading.tubeDesignerToolLibrary.scope = "system";
filteredWhileLoading.tubeDesignerToolLibrary.search = "System";
rememberLibraryNavigation(filteredWhileLoading);
Object.assign(filteredWhileLoading, { tubeDesignerSystemPunchTools: ready.tubeDesignerSystemPunchTools });
filteredWhileLoading.tubeDesignerToolLibrary.catalogueStatus = "ready";
restoreLibraryNavigation(filteredWhileLoading);
renderToolLibraryLeftPane({}, filteredWhileLoading);
assert.equal(filteredWhileLoading.tubeDesignerToolLibrary.selectedKey, "system::system-a", "a newer filter wins over an old card waiting on loading");

for (const area of Object.keys(expected)) memory.write(area, ["templates", "assemblies"].includes(area) ? "selectedId" : "selectedKey", "resource-removed");
const removed = fixture();
restoreLibraryNavigation(removed);
render(removed);
assert.ok(Object.values(selected(removed)).every(value => value && value !== "resource-removed"), "existing library validation chooses a current card after a resource was removed");
console.log("LibraryNavigationMemoryTest: passed; five library selections/filters, async loading, user changes, valid fallback, navigation-only storage.");
