import assert from "node:assert/strict";
import test from "node:test";
import {
  productResourceSession, restoreProductResources, rememberProductResources,
  rememberProductTemplateDescriptor, forgetProductTemplateDescriptor,
  invalidateProductResources,
} from "../../apps/tube-designer/webpage/productResourceSession.mjs";
import { refreshDesignerUserData, preloadTubeDesignerTemplates } from "../../apps/tube-designer/webpage/designerActions.mjs";
import { ensureToolLibraryCatalogue } from "../../apps/tube-designer/webpage/toolLibrary.mjs";

const deferred = () => {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
};
const proxy = (invoke = async () => ({}), channel = "product-a") => ({ productChannelId: channel, invoke });
const context = (productProxy = proxy()) => ({ productProxy });
const response = (name = "current") => ({
  customers: [], parameterPresets: [], profiles: [{ id: name }], punchTools: [],
  productTemplates: [], systemProfiles: [{ id: `system-${name}` }], templateProfiles: [],
});
const loadedView = (name = "current") => ({
  tubeDesignerUserData: { customers: [], parameterPresets: [], profiles: [{ id: name }], punchTools: [], productTemplates: [] },
  tubeDesignerSystemProfiles: [{ id: `system-${name}` }], tubeDesignerTemplateProfiles: [],
  tubeDesignerSystemPunchTools: [{ id: `tool-${name}` }], tubeDesignerTemplatePunchTools: [],
});
const seed = (ctx, view = loadedView()) => {
  restoreProductResources(ctx, view);
  for (const [key, value] of Object.entries(loadedView())) if (view[key] === undefined) view[key] = value;
  rememberProductResources(view, "userData");
  rememberProductResources(view, "tools");
  return view;
};

test("catalogue copies do not share mutable values or cache scene/editor state", () => {
  const ctx = context();
  const original = seed(ctx);
  original.scene = { secret: "scene-a" };
  original.tubeDesignerPunchBatch = { draft: "local" };
  const second = { scene: { secret: "scene-b" } };
  restoreProductResources(ctx, second);
  second.tubeDesignerUserData.profiles[0].id = "edited";
  second.tubeDesignerSystemPunchTools[0].id = "edited";
  assert.equal(original.tubeDesignerUserData.profiles[0].id, "current");
  assert.equal(original.tubeDesignerSystemPunchTools[0].id, "tool-current");
  assert.deepEqual(second.scene, { secret: "scene-b" });
  assert.equal(second.tubeDesignerPunchBatch, undefined);
});

test("explicit invalidation followed by reload reaches previously hydrated views", () => {
  const ctx = context(), first = seed(ctx), second = {};
  restoreProductResources(ctx, second);
  invalidateProductResources(ctx, first, "tools");
  first.tubeDesignerSystemPunchTools = [{ id: "fresh" }];
  rememberProductResources(first, "tools");
  restoreProductResources(ctx, second);
  assert.deepEqual(second.tubeDesignerSystemPunchTools, [{ id: "fresh" }]);
});

test("descriptor invalidation is propagated to another project registry", () => {
  const ctx = context(), first = seed(ctx), second = {};
  rememberProductTemplateDescriptor(first, { id: "template", version: "1", parameters: [{ key: "old" }] });
  restoreProductResources(ctx, second);
  forgetProductTemplateDescriptor(first, "template");
  restoreProductResources(ctx, second);
  assert.equal(second.tubeDesignerTemplateDescriptors.template, undefined);
});

test("a reconnected product channel cannot leave old resources marked ready", () => {
  const ctx = context(), view = seed(ctx, loadedView("old"));
  rememberProductTemplateDescriptor(view, { id: "old", version: "1", parameters: [] });
  ctx.productProxy.productChannelId = "product-b";
  const next = restoreProductResources(ctx, view);
  assert.deepEqual(next.resources, {});
  assert.notEqual(view.tubeDesignerUserDataLoaded, true);
  assert.notEqual(view.tubeDesignerToolLibrary?.catalogueStatus, "ready");
  assert.equal(view.tubeDesignerTemplateDescriptors.old, undefined);
  assert.ok(!view.tubeDesignerUserData?.profiles?.some(item => item.id === "old"));
});

test("sequential user-data refreshes issue new product requests", async () => {
  let calls = 0;
  const ctx = context(proxy(async method => {
    assert.equal(method, "TubeDesigner.ListUserData");
    return response(`revision-${++calls}`);
  })), view = {};
  assert.equal(await refreshDesignerUserData(ctx, view), true);
  assert.equal(await refreshDesignerUserData(ctx, view), true);
  assert.equal(calls, 2);
  assert.equal(view.tubeDesignerUserData.profiles[0].id, "revision-2");
  assert.equal(productResourceSession(ctx, view).userDataPromise, null);
});

test("failed user-data refresh is retriable rather than cached as a settled failure", async () => {
  let userCalls = 0;
  const ctx = context(proxy(async method => {
    if (method === "TubeDesigner.ListSystemProfiles") return response();
    if (++userCalls === 1) throw new Error("transient");
    return response("retry");
  })), view = {};
  assert.equal(await refreshDesignerUserData(ctx, view), false);
  assert.equal(await refreshDesignerUserData(ctx, view), true);
  assert.equal(userCalls, 2);
  assert.equal(view.tubeDesignerUserData.profiles[0].id, "retry");
});

test("concurrent project hydrations share one product request and get independent data", async () => {
  const gate = deferred();
  let calls = 0;
  const ctx = context(proxy(async method => {
    assert.equal(method, "TubeDesigner.ListUserData");
    calls += 1;
    return gate.promise;
  })), first = {}, second = {};
  const firstLoad = refreshDesignerUserData(ctx, first);
  const secondLoad = refreshDesignerUserData(ctx, second);
  gate.resolve(response());
  assert.deepEqual(await Promise.all([firstLoad, secondLoad]), [true, true]);
  assert.equal(calls, 1);
  assert.notEqual(first.tubeDesignerUserData, second.tubeDesignerUserData);
  assert.deepEqual(first.tubeDesignerUserData, second.tubeDesignerUserData);
});

test("old user-data response cannot overwrite or publish to a rebound product", async () => {
  const gate = deferred(), oldContext = context(proxy(async () => gate.promise));
  const view = {};
  const loading = refreshDesignerUserData(oldContext, view);
  const newContext = context(proxy(async () => response("new"), "product-b"));
  restoreProductResources(newContext, view);
  gate.resolve(response("old"));
  await loading;
  const newSession = productResourceSession(newContext, view);
  assert.ok(!view.tubeDesignerUserData?.profiles?.some(item => item.id === "old"));
  assert.ok(!newSession.resources.userData?.data.tubeDesignerUserData.profiles.some(item => item.id === "old"));
});

test("tool refresh replaces the complete personal catalogue, including removals", async () => {
  const ctx = context();
  ctx.sceneProxy = { invoke: async method => {
    assert.equal(method, "TubeDesigner.GetPunchTools");
    return { tools: [{ id: "round", libraryScope: "system" }] };
  } };
  const view = loadedView();
  view.tubeDesignerUserData.punchTools = [{ id: "deleted-user-tool", libraryScope: "user" }];
  await ensureToolLibraryCatalogue(ctx, view, null, { skipStartupGates: true });
  assert.deepEqual(view.tubeDesignerUserData.punchTools, []);
});

test("old tool catalogue response cannot populate a rebound product", async () => {
  const gate = deferred(), oldContext = context();
  oldContext.sceneProxy = { invoke: async () => gate.promise };
  const view = {};
  const loading = ensureToolLibraryCatalogue(oldContext, view, null, { skipStartupGates: true });
  await Promise.resolve();
  const newContext = context(proxy(async () => ({}), "product-b"));
  restoreProductResources(newContext, view);
  gate.resolve({ tools: [{ id: "old", libraryScope: "system" }] });
  await loading;
  const next = productResourceSession(newContext, view);
  assert.ok(!view.tubeDesignerSystemPunchTools?.some(item => item.id === "old"));
  assert.ok(!next.resources.tools?.data.tubeDesignerSystemPunchTools.some(item => item.id === "old"));
});

test("a joining hydration cannot rebind its view to an old product after awaiting", async () => {
  const gate = deferred(), oldContext = context(proxy(async () => gate.promise));
  const source = {}, joined = {};
  const first = refreshDesignerUserData(oldContext, source);
  const second = refreshDesignerUserData(oldContext, joined);
  const nextContext = context(proxy(async () => response("new"), "product-b"));
  seed(nextContext, joined);
  joined.tubeDesignerUserData.profiles = [{ id: "new" }];
  rememberProductResources(joined, "userData");
  gate.resolve(response("old"));
  await Promise.all([first, second]);
  assert.equal(joined.tubeDesignerUserData.profiles[0].id, "new");
  assert.equal(productResourceSession(nextContext, joined).resources.userData.data.tubeDesignerUserData.profiles[0].id, "new");
});

test("fallback system-profile reply cannot update a view rebound during fallback", async () => {
  const fallback = deferred(), invoked = deferred();
  const oldContext = context(proxy(async method => {
    if (method === "TubeDesigner.ListUserData") throw new Error("user data unavailable");
    invoked.resolve();
    return fallback.promise;
  })), view = {};
  const loading = refreshDesignerUserData(oldContext, view);
  await invoked.promise;
  const nextContext = context(proxy(async () => response("new"), "product-b"));
  seed(nextContext, view);
  const expected = structuredClone(view.tubeDesignerSystemProfiles);
  fallback.resolve(response("old"));
  await loading;
  assert.deepEqual(view.tubeDesignerSystemProfiles, expected);
});

test("rebinding a pending hydration does not block the new product's request", async () => {
  const gate = deferred(), oldContext = context(proxy(async () => gate.promise));
  const view = {};
  const first = refreshDesignerUserData(oldContext, view);
  let calls = 0;
  const nextContext = context(proxy(async () => { calls += 1; return response("new"); }, "product-b"));
  restoreProductResources(nextContext, view);
  const second = refreshDesignerUserData(nextContext, view);
  gate.resolve(response("old"));
  await Promise.all([first, second]);
  assert.equal(calls, 1);
  assert.equal(view.tubeDesignerUserData.profiles[0].id, "new");
});

test("a joining tool catalogue cannot rebind its view after product replacement", async () => {
  const gate = deferred(), oldContext = context();
  oldContext.sceneProxy = { invoke: async () => gate.promise };
  const source = {}, joined = {};
  const first = ensureToolLibraryCatalogue(oldContext, source, null, { skipStartupGates: true });
  const second = ensureToolLibraryCatalogue(oldContext, joined, null, { skipStartupGates: true });
  const nextContext = context(proxy(async () => ({}), "product-b"));
  seed(nextContext, joined);
  joined.tubeDesignerSystemPunchTools = [{ id: "new" }];
  rememberProductResources(joined, "tools");
  gate.resolve({ tools: [{ id: "old", libraryScope: "system" }] });
  await Promise.all([first, second]);
  assert.deepEqual(joined.tubeDesignerSystemPunchTools, [{ id: "new" }]);
});

test("a late tool catalogue error cannot change readiness of another product", async () => {
  const gate = deferred(), oldContext = context();
  oldContext.sceneProxy = { invoke: async () => gate.promise };
  const view = {};
  const loading = ensureToolLibraryCatalogue(oldContext, view, null, { skipStartupGates: true });
  await Promise.resolve();
  const nextContext = context(proxy(async () => ({}), "product-b"));
  restoreProductResources(nextContext, view);
  seed(nextContext);
  restoreProductResources(nextContext, view);
  gate.reject(new Error("old disconnected scene"));
  await loading;
  assert.equal(view.tubeDesignerToolLibrary.catalogueStatus, "ready");
  assert.equal(view.tubeDesignerToolLibrary.error, "");
});

test("a resource mutation wins over a catalogue snapshot requested before the mutation", async () => {
  const gate = deferred(), ctx = context(proxy(async () => gate.promise));
  const view = seed(ctx);
  const loading = refreshDesignerUserData(ctx, view);
  view.tubeDesignerUserData.profiles = [{ id: "saved-after-request" }];
  rememberProductResources(view, "userData");
  gate.resolve(response("old"));
  await loading;
  assert.deepEqual(view.tubeDesignerUserData.profiles, [{ id: "saved-after-request" }]);
  const another = {};
  restoreProductResources(ctx, another);
  assert.deepEqual(another.tubeDesignerUserData.profiles, [{ id: "saved-after-request" }]);
});

test("failed template preload clears its shared flight and allows a real retry", async () => {
  let descriptors = 0;
  const invoke = async method => {
    if (method === "TubeDesigner.ListUserData") return response();
    if (method === "TubeDesigner.GetPunchTools") return { tools: [{ id: "round", libraryScope: "system" }] };
    assert.equal(method, "TubeDesigner.GetTemplateDescriptor");
    if (++descriptors === 1) throw new Error("temporary descriptor failure");
    return { template: { id: "template", version: "1", parameters: [{ key: "length" }] } };
  };
  const ctx = context(proxy(invoke));
  ctx.sceneProxy = { invoke };
  const view = { scene: { tubeDesigner: { templates: [{ id: "template", version: "1" }] } } };
  await assert.rejects(preloadTubeDesignerTemplates(ctx, view), /temporary descriptor failure/);
  assert.equal(productResourceSession(ctx, view).preloadPromise, null);
  await preloadTubeDesignerTemplates(ctx, view);
  assert.equal(descriptors, 2);
  assert.equal(view.tubeDesignerTemplateDescriptors.template.parameters[0].key, "length");
});

test("preloading two projects shares resource requests and leaves their scenes distinct", async () => {
  const counts = {}, invoke = async method => {
    counts[method] = (counts[method] ?? 0) + 1;
    await Promise.resolve();
    if (method === "TubeDesigner.ListUserData") return response();
    if (method === "TubeDesigner.GetPunchTools") return { tools: [{ id: "round", libraryScope: "system" }] };
    assert.equal(method, "TubeDesigner.GetTemplateDescriptor");
    return { template: { id: "template", version: "1", parameters: [{ key: "length" }] } };
  };
  const ctx = context(proxy(invoke));
  ctx.sceneProxy = { invoke };
  const project = id => ({ scene: { id, tubeDesigner: { templates: [{ id: "template", version: "1" }] } } });
  const first = project("first"), second = project("second");
  await Promise.all([preloadTubeDesignerTemplates(ctx, first), preloadTubeDesignerTemplates(ctx, second)]);
  assert.deepEqual(counts, { "TubeDesigner.ListUserData": 1, "TubeDesigner.GetPunchTools": 1, "TubeDesigner.GetTemplateDescriptor": 1 });
  assert.equal(first.scene.id, "first");
  assert.equal(second.scene.id, "second");
  assert.notEqual(first.scene, second.scene);
  assert.notEqual(first.tubeDesignerTemplateDescriptors.template, second.tubeDesignerTemplateDescriptors.template);
});
