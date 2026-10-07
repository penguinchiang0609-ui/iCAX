import assert from "node:assert/strict";
import { createWorkbench, releaseProject } from "../../apps/_shared/workbench/createWorkbench.mjs";
import { findProjectView, getProjectArea, getProjectView, releaseProjectView } from "../../apps/_shared/workbench/state/projectViewStore.mjs";

const defer = () => {
  let resolve, reject;
  const promise = new Promise((ok, fail) => { resolve = ok; reject = fail; });
  return { promise, resolve, reject };
};
const tick = async () => { await Promise.resolve(); await Promise.resolve(); };
const snapshot = { viewId: "owned-view", revision: "1", rows: [], entityIds: [] };
const logs = [];
const makeContext = (projectId, start) => ({
  project: { projectId }, activeRibbonTabId: "view",
  sceneProxy: { views: { start } }, actions: { log: (...entry) => logs.push(entry) },
});
const setupView = (context, apply = async () => ({ applied: true, revision: "1" })) => {
  const view = getProjectView(context.project.projectId);
  view.activeAreaId = "view";
  view.sceneProxy = context.sceneProxy;
  view.scene = {};
  view.viewport = { setRenderSceneId() {}, applyViewSnapshot: apply, dispose() {} };
  return view;
};

assert.equal(findProjectView("not-open"), null);
assert.equal(await releaseProject({ project: { projectId: "not-open" } }), false);
assert.equal(findProjectView("not-open"), null, "release must not create a view for an absent project");
assert.equal(await createWorkbench().synchronizeActiveAreaView({
  project: { projectId: "stale-context" }, isCurrentProject: () => false,
}), null);
assert.equal(findProjectView("stale-context"), null, "late synchronization must not recreate a closed project view");

{
  const view = getProjectView("cleanups");
  let stopped = 0, disposed = 0, noticeFired = false;
  getProjectArea(view, "view").viewReader = { stop() { stopped++; throw new Error("closed native channel"); } };
  getProjectArea(view, "other").viewReader = { async stop() { stopped++; throw new Error("late stop rejection"); } };
  view.viewport = { dispose() { disposed++; throw new Error("already released renderer"); } };
  view.noticeDismissTimer = setTimeout(() => { noticeFired = true; }, 10);
  const release = releaseProjectView("cleanups");
  assert.equal(view.disposed, true);
  assert.equal(findProjectView("cleanups"), null, "removal happens before async resource teardown settles");
  const reopened = getProjectView("cleanups");
  assert.notEqual(reopened, view);
  assert.equal(reopened.disposed, false);
  assert.equal(reopened.scene, null);
  assert.equal(await release, true);
  assert.equal(stopped, 2);
  assert.equal(disposed, 1);
  await new Promise(resolve => setTimeout(resolve, 20));
  assert.equal(noticeFired, false);
  assert.equal(findProjectView("cleanups"), reopened, "old completion does not delete the reopened session");
  await releaseProjectView("cleanups");
}

{
  const start = defer();
  let callbacks, stopped = 0, polls = 0, applies = 0;
  const context = makeContext("late-start", (_definition, options) => { callbacks = options; return start.promise; });
  const view = setupView(context, async () => { applies++; return { applied: true, revision: "1" }; });
  const workbench = createWorkbench();
  const pending = workbench.synchronizeActiveAreaView(context);
  await tick();
  assert.ok(callbacks);
  await releaseProject(context);
  const reopened = getProjectView("late-start");
  start.resolve({ async stop() { stopped++; }, async poll() { polls++; return snapshot; } });
  assert.equal(await pending, null);
  callbacks.onChange(snapshot);
  await tick();
  assert.equal(stopped, 1, "a reader created after close is stopped exactly once");
  assert.equal(polls, 0, "the late reader is never polled after close");
  assert.equal(applies, 0);
  assert.equal(getProjectArea(view, "view").viewReader, null);
  assert.equal(findProjectView("late-start"), reopened);
  assert.deepEqual(reopened.areas, {});
  await releaseProjectView("late-start");
}

{
  const start = defer();
  const context = makeContext("late-failure", () => start.promise);
  const view = setupView(context);
  const pending = createWorkbench().synchronizeActiveAreaView(context);
  await tick();
  await releaseProject(context);
  start.reject(new Error("native view request finished after project closed"));
  assert.equal(await pending, null);
  assert.equal(view.error, "");
  assert.deepEqual(logs, [], "closed-project callbacks do not display stale errors");
}

{
  const apply = defer();
  let stopped = 0, applying = false;
  const reader = { async stop() { stopped++; }, async poll() { return snapshot; }, waitForSnapshot() {} };
  const context = makeContext("late-render", async () => reader);
  const old = setupView(context, () => { applying = true; return apply.promise; });
  const pending = createWorkbench().synchronizeActiveAreaView(context);
  while (!applying) await tick();
  await releaseProject(context);
  const reopened = setupView(context);
  apply.resolve({ applied: true, revision: "1" });
  assert.equal(await pending, null);
  assert.equal(stopped, 1);
  assert.equal(getProjectArea(old, "view").viewContent, null);
  assert.deepEqual(reopened.areas, {});
  const content = await createWorkbench().synchronizeActiveAreaView(context);
  assert.equal(content.snapshot, snapshot, "the reopened session creates and applies its own fresh reader");
  await releaseProject(context);
}

console.log("PASS project view lifecycle: fresh same-ID reopen, teardown failures, timer cancellation, late reader and render responses");
