import assert from "node:assert/strict";
import {
  PROFILE_PREVIEW_PROGRESS_DELAY_MS,
  PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS,
  handleProfileLibraryAction,
  renderProfileLibraryViewportOverlay,
} from "../../apps/tube-designer/webpage/profileLibrary.mjs";

const completed = [];
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((a, b) => { resolve = a; reject = b; });
  return { promise, resolve, reject };
};
async function flushMicrotasks() { for (let i = 0; i < 32; ++i) await Promise.resolve(); }

function virtualClock() {
  let time = 0, sequence = 0;
  const timers = new Map();
  const originals = Object.fromEntries(["setTimeout", "clearTimeout", "performance", "requestAnimationFrame", "document"]
    .map((key) => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  Object.defineProperty(globalThis, "performance", { configurable: true, value: { now: () => time } });
  globalThis.requestAnimationFrame = undefined;
  globalThis.setTimeout = (callback, milliseconds = 0, ...args) => {
    const id = ++sequence;
    timers.set(id, { time: time + Math.max(0, Number(milliseconds) || 0), callback, args });
    return id;
  };
  globalThis.clearTimeout = (id) => timers.delete(id);
  return {
    now: () => time,
    async advance(milliseconds) {
      const target = time + milliseconds;
      await flushMicrotasks();
      for (let guard = 0; guard < 1000; ++guard) {
        const next = [...timers].filter(([, timer]) => timer.time <= target)
          .sort((a, b) => a[1].time - b[1].time || a[0] - b[0])[0];
        if (!next) break;
        const [id, timer] = next;
        timers.delete(id);
        time = timer.time;
        timer.callback(...timer.args);
        await flushMicrotasks();
      }
      time = target;
      await flushMicrotasks();
    },
    restore() {
      for (const [key, descriptor] of Object.entries(originals)) {
        if (descriptor) Object.defineProperty(globalThis, key, descriptor);
        else delete globalThis[key];
      }
      timers.clear();
    },
  };
}

function progressDom(clock) {
  const events = [];
  let mounted = true, nodes;
  function node(name, children = {}) {
    let hidden = true;
    const attributes = {};
    const classes = new Set();
    return {
      name, textContent: "", attributes,
      get hidden() { return hidden; },
      set hidden(value) {
        hidden = Boolean(value);
        if (name === "wait" || name === "progress") events.push({ name, shown: !hidden, at: clock.now() });
      },
      classList: { toggle(key, value) { value ? classes.add(key) : classes.delete(key); }, remove(key) { classes.delete(key); } },
      setAttribute(key, value) { attributes[key] = value; },
      querySelector(selector) { return children[selector] ?? null; },
    };
  }
  function remount() {
    mounted = true;
    const progress = node("progress"), status = node("status");
    const wait = node("wait", {
      "[data-tube-profile-preview-wait-title]": node("title"),
      "[data-tube-profile-preview-wait-message]": node("description"),
      "[data-tube-profile-preview-wait-phase]": node("phase"),
      "[data-tube-profile-preview-wait-progress]": node("waitProgress"),
    });
    const hud = node("hud", { small: status, "[data-tube-profile-preview-progress]": progress });
    nodes = { progress, wait, status, hud };
  }
  remount();
  globalThis.document = { querySelector(selector) {
    if (!mounted) return null;
    return { "[data-tube-profile-preview-status]": nodes.hud, "[data-tube-profile-preview-wait]": nodes.wait }[selector] ?? null;
  } };
  return { events, remount, unmount() { mounted = false; },
    shown() { return mounted && (!nodes.wait.hidden || !nodes.progress.hidden); },
    shownEvents() { return events.filter((event) => event.shown); },
    wait() { return nodes.wait; }, status() { return nodes.status; },
  };
}

const profile = (id) => ({ id, name: `测试管型 ${id}`, profileType: "parametric-package",
  descriptor: { id, version: "1", parameters: [] }, defaultParameters: {},
  previewProfile: { name: `测试管型 ${id}`, width: 40, depth: 40, specification: "Φ40", contours: [{ kind: "circle", radius: 20 }] },
});
const geometry = (id = "a") => ({ geometryResourceId: `resource://preview/${id}`, geometryResourceVersion: 1 });

function harness(clock) {
  const dom = progressDom(clock), calls = [], hydration = [], visible = [], logs = [];
  let backend = async () => geometry(), hydrate = async () => ({ applied: true });
  let appliedState = { revision: "", entityIds: [] };
  const view = { activeAreaId: "profiles", tubeDesignerSystemProfiles: [profile("a"), profile("b")],
    tubeDesignerUserData: { profiles: [] }, tubeDesignerTemplateProfiles: [],
    tubeDesignerSelectedProfileId: "system:a", viewport: {
      getAppliedViewState() { return appliedState; },
      async applyViewSnapshot(snapshot) {
        hydration.push(snapshot);
        const result = await hydrate(snapshot);
        if (result?.applied) appliedState = { revision: snapshot.revision, entityIds: snapshot.rows.map((row) => row.entityId) };
        return result;
      },
      setVisibleEntityIds(ids) { visible.push(ids); }, setSelectedObjectIds() {}, fitViewForRevision() {}, setStandardView() {},
    } };
  const context = { actions: { log(level, message) { logs.push({ level, message }); } },
    sceneProxy: { async invoke(method, payload) { calls.push({ method, payload }); return backend(payload); } } };
  function render() { dom.remount(); renderProfileLibraryViewportOverlay(context, view); }
  const ops = { renderProject: render, showNotice() {} };
  return { view, context, dom, calls, hydration, visible, logs, render,
    backend(fn) { backend = fn; }, hydrate(fn) { hydrate = fn; }, invalidateViewport() { appliedState = { revision: "other", entityIds: [] }; },
    async action(suffix, target = {}) { await handleProfileLibraryAction(context, view, `tube-designer-profile-library-${suffix}`, target, ops); await flushMicrotasks(); },
  };
}

async function test(name, run) {
  const clock = virtualClock();
  try { await run(clock); await flushMicrotasks(); completed.push(name); }
  finally { clock.restore(); }
}

assert.equal(PROFILE_PREVIEW_PROGRESS_DELAY_MS, 200);
assert.equal(PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS, 500);

await test("fresh preview finishing before 200 ms never shows progress or waits for a minimum duration", async (clock) => {
  const h = harness(clock), request = deferred();
  h.backend(() => request.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(199);
  assert.equal(h.dom.shown(), false);
  request.resolve(geometry()); await flushMicrotasks();
  assert.equal(clock.now(), 199);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
  assert.equal(h.hydration.length, 1);
  assert.equal(h.dom.shownEvents().length, 0);
  await clock.advance(600);
  assert.equal(h.dom.shownEvents().length, 0);
});

await test("a cached preview rehydrated quickly never flashes progress", async (clock) => {
  const h = harness(clock);
  h.render(); await flushMicrotasks();
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
  h.invalidateViewport();
  const loading = deferred(); h.hydrate(() => loading.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(199);
  assert.equal(h.dom.shown(), false);
  loading.resolve({ applied: true }); await flushMicrotasks();
  assert.equal(h.calls.length, 1, "cache reuse must not regenerate backend geometry");
  assert.equal(h.hydration.length, 2);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
  assert.equal(h.dom.shownEvents().length, 0);
  await clock.advance(201);
  assert.equal(h.dom.shownEvents().length, 0);
});

await test("slow generation shows only at 200 ms and remains visible for 500 ms after paint", async (clock) => {
  const h = harness(clock), request = deferred();
  h.backend(() => request.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(199); assert.equal(h.dom.shown(), false);
  await clock.advance(1); assert.equal(h.dom.shown(), true);
  assert.ok(h.dom.shownEvents().every((event) => event.at >= 200));
  await clock.advance(1);
  request.resolve(geometry()); await flushMicrotasks();
  assert.equal(clock.now(), 201);
  assert.equal(h.dom.shown(), true);
  assert.notEqual(h.view.tubeDesignerProfilePreviewRequest, null);
  await clock.advance(498); assert.equal(h.dom.shown(), true);
  await clock.advance(1);
  assert.equal(h.dom.shown(), false);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
  assert.ok(h.logs.some((entry) => entry.message.includes("三维管型已生成")));
});

await test("the 200 ms threshold includes hydration after an early backend response", async (clock) => {
  const h = harness(clock), request = deferred(), loading = deferred();
  h.backend(() => request.promise); h.hydrate(() => loading.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(90); request.resolve(geometry()); await flushMicrotasks();
  assert.equal(h.hydration.length, 1);
  await clock.advance(109); assert.equal(h.dom.shown(), false);
  await clock.advance(1); assert.equal(h.dom.shown(), true);
  assert.match(h.dom.wait().querySelector("[data-tube-profile-preview-wait-title]").textContent, /装载/);
  loading.resolve({ applied: true }); await flushMicrotasks();
  assert.equal(clock.now(), 200); assert.equal(h.dom.shown(), true);
  await clock.advance(499); assert.equal(h.dom.shown(), true);
  await clock.advance(1); assert.equal(h.dom.shown(), false);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
});

await test("slow cache hydration uses the same 200 ms delay and 500 ms visible minimum", async (clock) => {
  const h = harness(clock);
  h.render(); await flushMicrotasks();
  h.invalidateViewport();
  const loading = deferred(); h.hydrate(() => loading.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(199); assert.equal(h.dom.shown(), false);
  await clock.advance(1); assert.equal(h.dom.shown(), true);
  loading.resolve({ applied: true }); await flushMicrotasks();
  assert.equal(h.dom.shown(), true);
  await clock.advance(499); assert.equal(h.dom.shown(), true);
  await clock.advance(1);
  assert.equal(h.dom.shown(), false); assert.equal(h.calls.length, 1);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
});

for (const phase of ["generation", "hydration", "cached hydration"]) {
  await test(`${phase} failure respects the visible 500 ms minimum`, async (clock) => {
    const h = harness(clock), failure = deferred();
    if (phase === "cached hydration") {
      h.render(); await flushMicrotasks(); h.invalidateViewport();
    }
    if (phase === "generation") h.backend(() => failure.promise);
    else h.hydrate(() => failure.promise);
    h.render(); await flushMicrotasks();
    await clock.advance(200); assert.equal(h.dom.shown(), true);
    failure.reject(new Error(`${phase} failed`)); await flushMicrotasks();
    assert.equal(clock.now(), 200); assert.equal(h.dom.shown(), true);
    await clock.advance(499); assert.equal(h.dom.shown(), true);
    await clock.advance(1); assert.equal(h.dom.shown(), false);
    assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
    assert.match(h.view.error, /failed/);
  });
}

await test("a fast failure never shows delayed progress after it has failed", async (clock) => {
  const h = harness(clock), failure = deferred();
  h.backend(() => failure.promise);
  h.render(); await flushMicrotasks(); await clock.advance(100);
  failure.reject(new Error("fast failure")); await flushMicrotasks();
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
  assert.equal(h.dom.shownEvents().length, 0);
  await clock.advance(500); assert.equal(h.dom.shownEvents().length, 0);
});

await test("an obsolete request cannot show or hide the replacement request's progress", async (clock) => {
  const h = harness(clock), first = deferred(), second = deferred();
  h.backend(({ profileRef }) => profileRef.id === "a" ? first.promise : second.promise);
  h.render(); await flushMicrotasks(); await clock.advance(100);
  await h.action("select", { dataset: { tubeDesignerProfileKey: "system:b" } });
  await clock.advance(100); assert.equal(h.dom.shown(), false);
  await clock.advance(99); assert.equal(h.dom.shown(), false);
  await clock.advance(1); assert.equal(h.dom.shown(), true);
  const replacement = h.view.tubeDesignerProfilePreviewRequest;
  first.resolve(geometry("a")); await flushMicrotasks();
  assert.equal(h.dom.shown(), true);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, replacement);
  assert.equal(h.hydration.length, 0);
  second.resolve(geometry("b")); await flushMicrotasks();
  assert.equal(h.dom.shown(), true); assert.equal(h.hydration.length, 1);
  await clock.advance(500); assert.equal(h.dom.shown(), false);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
});

for (const action of ["empty search", "empty source", "leave area"]) {
  await test(`${action} invalidates pending progress before its deadline`, async (clock) => {
    const h = harness(clock), request = deferred();
    h.backend(() => request.promise);
    h.render(); await flushMicrotasks(); await clock.advance(100);
    if (action === "empty search") await h.action("search", { value: "missing profile" });
    if (action === "empty source") await h.action("scope", { dataset: { tubeProfileLibraryScope: "template" } });
    if (action === "leave area") { h.view.activeAreaId = "view"; h.dom.unmount(); }
    await clock.advance(150);
    assert.equal(h.dom.shownEvents().length, 0);
    request.resolve(geometry()); await flushMicrotasks();
    assert.equal(h.hydration.length, 0);
    assert.equal(h.dom.shown(), false);
    assert.equal(h.dom.shownEvents().length, 0);
  });
}

await test("rerendering during generation preserves the original 200 ms deadline", async (clock) => {
  const h = harness(clock), request = deferred();
  h.backend(() => request.promise);
  h.render(); await flushMicrotasks();
  const original = h.view.tubeDesignerProfilePreviewRequest;
  await clock.advance(150); h.render(); await flushMicrotasks();
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, original);
  await clock.advance(49); assert.equal(h.dom.shown(), false);
  await clock.advance(1); assert.equal(h.dom.shown(), true);
  assert.equal(h.calls.length, 1);
  h.render(); await flushMicrotasks();
  assert.equal(h.dom.shown(), true, "visible progress is restored to rerendered DOM without a second delay");
  request.resolve(geometry()); await flushMicrotasks();
  assert.equal(h.dom.shown(), true);
  await clock.advance(500); assert.equal(h.dom.shown(), false);
});

await test("rerendering during hydration neither restarts the delay nor duplicates loading", async (clock) => {
  const h = harness(clock), loading = deferred();
  h.hydrate(() => loading.promise);
  h.render(); await flushMicrotasks();
  const original = h.view.tubeDesignerProfilePreviewRequest;
  await clock.advance(150); h.render(); await flushMicrotasks();
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, original);
  await clock.advance(49); assert.equal(h.dom.shown(), false);
  await clock.advance(1); assert.equal(h.dom.shown(), true);
  assert.equal(h.calls.length, 1); assert.equal(h.hydration.length, 1);
  loading.resolve({ applied: true }); await flushMicrotasks();
  assert.equal(h.dom.shown(), true);
  await clock.advance(500); assert.equal(h.dom.shown(), false);
});

await test("the minimum visibility interval begins at actual paint rather than at the timer deadline", async (clock) => {
  const h = harness(clock), request = deferred();
  globalThis.requestAnimationFrame = (callback) => setTimeout(() => callback(clock.now()), 16);
  h.backend(() => request.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(200); assert.equal(h.dom.shown(), true);
  request.resolve(geometry()); await flushMicrotasks();
  await clock.advance(32); assert.equal(h.dom.shown(), true);
  await clock.advance(499); assert.equal(h.dom.shown(), true);
  await clock.advance(1); assert.equal(h.dom.shown(), false);
  assert.equal(clock.now(), 732);
});

await test("rerendering during minimum visibility restores the HUD and hydrated entity without restarting timers", async (clock) => {
  const h = harness(clock), request = deferred();
  h.backend(() => request.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(200); assert.equal(h.dom.shown(), true);
  request.resolve(geometry()); await flushMicrotasks();
  const original = h.view.tubeDesignerProfilePreviewRequest;
  assert.equal(h.hydration.length, 1);
  await clock.advance(250);
  h.visible.length = 0;
  h.render(); await flushMicrotasks();
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, original);
  assert.equal(h.dom.shown(), true);
  assert.deepEqual(h.visible.at(-1), ["system:a"]);
  assert.equal(h.hydration.length, 1);
  await clock.advance(249); assert.equal(h.dom.shown(), true);
  await clock.advance(1); assert.equal(h.dom.shown(), false);
  assert.equal(clock.now(), 700);
});

await test("work completing after the visible minimum has already elapsed adds no further hold", async (clock) => {
  const h = harness(clock), request = deferred();
  h.backend(() => request.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(800); assert.equal(h.dom.shown(), true);
  request.resolve(geometry()); await flushMicrotasks();
  assert.equal(clock.now(), 800);
  assert.equal(h.dom.shown(), false);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, null);
});

await test("switching to an empty source clears visible progress without allowing the old request to restore it", async (clock) => {
  const h = harness(clock), request = deferred();
  h.backend(() => request.promise);
  h.render(); await flushMicrotasks();
  await clock.advance(200); assert.equal(h.dom.shown(), true);
  await h.action("scope", { dataset: { tubeProfileLibraryScope: "template" } });
  const previousShowCount = h.dom.shownEvents().length;
  assert.equal(h.dom.shown(), false);
  request.reject(new Error("old request failed")); await flushMicrotasks();
  await clock.advance(1000);
  assert.equal(h.dom.shown(), false);
  assert.equal(h.dom.shownEvents().length, previousShowCount);
  assert.equal(h.view.error, "");
});

await test("late hydration for old parameters hides its own stale mesh even when the profile ID is unchanged", async (clock) => {
  const h = harness(clock), oldLoading = deferred(), newGeneration = deferred();
  let generations = 0, loads = 0;
  h.backend(() => ++generations === 1 ? geometry() : newGeneration.promise);
  h.hydrate(() => ++loads === 1 ? oldLoading.promise : { applied: true });
  h.render(); await flushMicrotasks();
  h.view.tubeDesignerProfileDrafts = { "system:a": { parameters: { width: 60 } } };
  h.render(); await flushMicrotasks();
  const current = h.view.tubeDesignerProfilePreviewRequest;
  oldLoading.resolve({ applied: true }); await flushMicrotasks();
  assert.equal(h.view.tubeDesignerSelectedProfileId, "system:a");
  assert.deepEqual(h.visible.at(-1), []);
  assert.equal(h.view.tubeDesignerProfilePreviewRequest, current);
  newGeneration.resolve({ ...geometry(), geometryResourceVersion: 2 }); await flushMicrotasks();
  assert.deepEqual(h.visible.at(-1), ["system:a"]);
  assert.equal(h.view.viewport.getAppliedViewState().revision, "profile-preview:system:a:2");
});

await test("a stale hydration receipt cannot change visibility when a newer revision already owns the viewport", async (clock) => {
  const h = harness(clock), oldLoading = deferred();
  let generations = 0, applied = { revision: "", entityIds: [] };
  h.backend(() => ({ ...geometry(), geometryResourceVersion: ++generations }));
  h.view.viewport.getAppliedViewState = () => applied;
  h.view.viewport.applyViewSnapshot = async (snapshot) => {
    if (snapshot.revision.endsWith(":1")) return oldLoading.promise;
    applied = { revision: snapshot.revision, entityIds: snapshot.rows.map((row) => row.entityId) };
    return { applied: true };
  };
  h.render(); await flushMicrotasks();
  h.view.tubeDesignerProfileDrafts = { "system:a": { parameters: { width: 60 } } };
  h.render(); await flushMicrotasks();
  assert.equal(applied.revision, "profile-preview:system:a:2");
  assert.deepEqual(h.visible.at(-1), ["system:a"]);
  h.visible.length = 0;
  oldLoading.resolve({ applied: true }); await flushMicrotasks();
  assert.deepEqual(h.visible, [], "the stale completion must not hide or reselect the newer revision");
  assert.equal(applied.revision, "profile-preview:system:a:2");
});

await test("late hydration after leaving profiles hides only its own stale viewport revision", async (clock) => {
  const h = harness(clock), loading = deferred();
  h.hydrate(() => loading.promise);
  h.render(); await flushMicrotasks();
  h.view.activeAreaId = "view"; h.dom.unmount();
  loading.resolve({ applied: true }); await flushMicrotasks();
  assert.deepEqual(h.visible.at(-1), []);
  assert.equal(h.dom.shownEvents().length, 0);
});

for (const change of ["version", "default parameters"]) {
  await test(`catalog ${change} replacement makes a pending same-ID response stale without needing a rerender`, async (clock) => {
    const h = harness(clock), request = deferred();
    h.backend(() => request.promise);
    h.render(); await flushMicrotasks();
    const original = h.view.tubeDesignerSystemProfiles[0];
    h.view.tubeDesignerSystemProfiles[0] = change === "version"
      ? { ...original, descriptor: { ...original.descriptor, version: "2" } }
      : { ...original, defaultParameters: { width: 60 } };
    await clock.advance(250);
    assert.equal(h.dom.shownEvents().length, 0);
    request.resolve(geometry()); await flushMicrotasks();
    assert.equal(h.hydration.length, 0);
    assert.equal(h.dom.shownEvents().length, 0);
  });
}

console.log(`TubeDesignerProfilePreviewTimingTest: ${completed.length} passed`);
