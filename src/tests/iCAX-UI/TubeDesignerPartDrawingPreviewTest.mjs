import assert from "node:assert/strict";
import test from "node:test";
import { attachPartDrawingPreview, buildPartDrawingPreviewRows, disposePartDrawingPreview,
  setPartDrawingPreviewView, waitForPartDrawingPreview } from "../../apps/tube-designer/webpage/partDrawingPreview.mjs";

const tick = () => new Promise(resolve => setImmediate(resolve));
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
};
async function until(check) {
  for (let index = 0; index < 100 && !check(); index++) await tick();
  assert.ok(check(), "Expected renderer state was not reached");
}

class Element {
  constructor(document, tag = "div") {
    this.ownerDocument = document; this.tagName = tag; this.dataset = {}; this.children = [];
    this.attributes = new Map(); this.style = {}; this.parentElement = null; this.listeners = new Map();
  }
  append(...children) {
    for (const child of children) { child.remove(); child.parentElement = this; this.children.push(child); }
  }
  replaceChildren(...children) { for (const child of this.children) child.parentElement = null; this.children = []; this.append(...children); }
  remove() {
    if (this.parentElement) this.parentElement.children = this.parentElement.children.filter(child => child !== this);
    this.parentElement = null;
  }
  setAttribute(key, value) { this.attributes.set(key, String(value)); }
  getAttribute(key) { return this.attributes.get(key); }
  addEventListener(name, listener) { this.listeners.set(name, listener); }
  removeEventListener(name, listener) { if (this.listeners.get(name) === listener) this.listeners.delete(name); }
  click() { this.listeners.get("click")?.({ preventDefault() {}, stopPropagation() {} }); }
  querySelector(selector) {
    const dataset = selector.match(/^\[data-([^\]]+)\]$/)?.[1]?.replace(/-([a-z])/g, (_, char) => char.toUpperCase());
    for (const child of this.children) {
      if (dataset && Object.hasOwn(child.dataset, dataset)) return child;
      if (selector === '[role="progressbar"]' && child.getAttribute("role") === "progressbar") return child;
      const found = child.querySelector(selector); if (found) return found;
    }
    return null;
  }
}

function fixture() {
  const document = { createElement(tag) { return new Element(document, tag); } };
  const mount = new Element(document), host = new Element(document); host.dataset.partDrawingViewport = ""; mount.append(host);
  const reference = (url, version = 1) => ({ url, version });
  const preview = (revision = 1, changed = {}) => ({ revision, toolsOnly: true,
    baseGeometry: reference("base", changed.base ?? 1), baseMaterial: reference("base-material"),
    toolMaterial: reference("tool-material"), baseBounds: { min: [0, -20, -10], max: [changed.length ?? 500, 20, 10] },
    toolPreviews: ["first", "second"].map(key => ({ target: "feature", key, geometry: reference(key, changed[key] ?? 1) })),
  });
  const recipe = () => ({ drawing: { length: 500, section: { source: "library", key: "round" } },
    features: [{ id: "first", station: 100 }, { id: "second", station: 200 }], ends: {} });
  const state = { revision: 1, preview: preview(), previewRecipe: recipe(), previewPending: false };
  const drawing = { state, mode: "", selected: "" }, unrelated = { marker: "main-scene-punch-state" };
  const view = { tubeDesignerPartDrawing: drawing, tubeDesignerPunchWizard: unrelated, pending: false };
  const reads = [], delays = new Map(), failures = new Set(), viewports = [];
  const resources = { async get(url, options) {
    const key = `${url}@${options.version}`; reads.push(key); await delays.get(key)?.promise;
    if (failures.has(key)) throw new Error(`unavailable ${key}`);
    return { url, version: options.version };
  } };
  const context = { mount, sceneProxy: { resources, invoke() { throw new Error("Renderer must not invoke native commands"); } } };
  const ops = { renderProject() { throw new Error("Renderer must not refresh the main workbench"); },
    createPartDrawingViewport() {
      let generation = 0;
      const viewport = { root: new Element(document), snapshots: [], visibleEntityIds: [], rows: [], geometryObjects: new Map(),
        geometryPayloads: new Map(), cache: new Map(), fitCount: 0, viewportAspect: 5 / 3, perspectiveFov: 45,
        camera: { name: "first", radius: 50, theta: 0.5, phi: 1, projectionMode: "perspective", target: { x: 0, y: 0, z: 0 } }, disposed: false,
        mount(host) { host.replaceChildren(this.root); },
        setVisibleEntityIds(ids) { this.visibleEntityIds = [...ids]; },
        retainViewResources() {},
        setProjectionMode(mode) { this.camera.projectionMode = mode; },
        setStandardView(name) { this.camera.name = name; if (name === "front") { this.camera.theta = -Math.PI / 2; this.camera.phi = Math.PI / 2; } },
        setCameraState(value) { this.fitCount++; this.camera = { ...this.camera, ...structuredClone(value) }; },
        fitViewToViewport() { this.fitCount++; this.camera.radius = this.fitCount * 100; return true; },
        getCameraState() { return structuredClone(this.camera); },
        dispose() { generation++; this.disposed = true; this.root.remove(); },
        async applyViewSnapshot(snapshot, client) {
          const active = ++generation; this.snapshots.push(snapshot);
          const refs = new Map(snapshot.rows.flatMap(row => [row.data.geometry, row.data.material]).filter(Boolean)
            .map(ref => [`${ref.url}@${ref.version}`, ref]));
          const loaded = await Promise.all([...refs].map(([key, ref]) => {
            if (!this.cache.has(key)) this.cache.set(key, client.get(ref.url, { version: ref.version }).catch(error => { this.cache.delete(key); throw error; }));
            return this.cache.get(key);
          }));
          if (active !== generation || this.disposed) return { applied: false, superseded: true };
          for (const resource of loaded) {
            if (this.geometryPayloads.get(resource.url)?.viewResourceVersion === String(resource.version)) continue;
            this.geometryPayloads.set(resource.url, { viewResourceVersion: String(resource.version) });
            this.geometryObjects.set(resource.url, { ...resource });
          }
          this.rows = snapshot.rows; this.visibleEntityIds = snapshot.rows.map(row => row.entityId);
          return { applied: true, entityIds: [...this.visibleEntityIds], missingGeometryEntityIds: [] };
        },
      };
      viewports.push(viewport); return viewport;
    },
  };
  return { mount, host, state, drawing, view, unrelated, preview, recipe, reads, delays, failures, viewports, document,
    attach() { return attachPartDrawingPreview(context, view, mount, ops); },
    replaceHost() { const next = new Element(document); next.dataset.partDrawingViewport = ""; mount.replaceChildren(next); this.host = next; return next; },
  };
}

test("drawing rows are only stable base and operand resources, never a final boolean result", () => {
  const f = fixture(), rows = buildPartDrawingPreviewRows({ ...f.state.preview, geometry: { url: "forbidden-final", version: 1 } });
  assert.deepEqual(rows.map(row => row.entityId), ["drawing:base", "drawing:feature:first", "drawing:feature:second"]);
  assert.equal(rows[0].data.localToWorldMatrix[3], -250);
  assert.deepEqual(rows.map(row => row.data.renderClass), [1, 5, 5]);
  assert.deepEqual(buildPartDrawingPreviewRows({ geometry: { url: "saved-result" } }), []);
});

test("local DOM updates retain one viewport and unchanged references cause no snapshot or resource read", async () => {
  const f = fixture(); assert.equal(await f.attach(), true);
  const viewport = f.viewports[0], canvas = viewport.root, reads = [...f.reads];
  assert.equal(viewport.fitCount, 1); setPartDrawingPreviewView(f.mount, "top");
  for (let index = 0; index < 12; index++) {
    f.replaceHost(); assert.equal(await f.attach(), true);
    assert.equal(viewport.root, canvas); assert.equal(canvas.parentElement, f.host);
  }
  assert.equal(f.viewports.length, 1); assert.equal(viewport.snapshots.length, 1);
  assert.equal(viewport.fitCount, 1); assert.equal(viewport.camera.name, "top"); assert.deepEqual(f.reads, reads);
  assert.equal(f.host.dataset.partDrawingPreviewReady, "true");
  assert.equal(f.view.pending, false); assert.equal(f.view.tubeDesignerPunchWizard, f.unrelated);
  disposePartDrawingPreview(f.mount); assert.equal(viewport.disposed, true);
});

test("first valid stock opens in orthographic side view at 90 percent canvas width without vertical clipping", async () => {
  const f = fixture(); await f.attach(); const viewport = f.viewports[0], camera = viewport.camera;
  assert.equal(camera.name, "front"); assert.equal(camera.projectionMode, "orthographic");
  assert.equal(camera.theta, -Math.PI / 2); assert.equal(camera.phi, Math.PI / 2);
  const visibleWidth = 2 * camera.radius * Math.tan(Math.PI / 8) * viewport.viewportAspect;
  assert.ok(Math.abs(500 / visibleWidth - 0.9) < 1e-10);
  assert.ok(20 / (visibleWidth / viewport.viewportAspect) <= 0.9);
  viewport.camera.projectionMode = "perspective"; viewport.camera.theta = 0.7; viewport.camera.phi = 1.1;
  f.state.revision = 2; f.state.preview = f.preview(2, { first: 2 }); await f.attach();
  assert.equal(viewport.camera.projectionMode, "perspective"); assert.equal(viewport.camera.theta, 0.7);
  assert.equal(viewport.camera.phi, 1.1); assert.equal(viewport.fitCount, 1, "Operand edits preserve a user's subsequent view choice");
  f.state.revision = 3; f.state.preview = f.preview(3, { base: 2, length: 1000 }); await f.attach();
  assert.equal(viewport.camera.projectionMode, "perspective"); assert.equal(viewport.camera.theta, 0.7);
  assert.equal(viewport.camera.phi, 1.1); assert.equal(viewport.fitCount, 2, "Changed stock refits without changing the chosen direction");
  disposePartDrawingPreview(f.mount);
});

test("a dirty operand alone is hidden during computation and updates without refitting or rereading neighbours", async () => {
  const f = fixture(); await f.attach();
  const viewport = f.viewports[0], base = viewport.geometryObjects.get("base"), second = viewport.geometryObjects.get("second");
  f.state.revision = 2; f.state.previewPending = true; f.state.pendingPreviewRecipe = f.recipe();
  f.state.pendingPreviewRecipe.features[0].station = 130;
  assert.equal(await f.attach(), false);
  assert.deepEqual(viewport.visibleEntityIds, ["drawing:base", "drawing:feature:second"]);
  assert.ok(f.host.querySelector('[role="progressbar"]'));
  const before = f.reads.length;
  f.state.preview = f.preview(2, { first: 2 }); f.state.previewRecipe = structuredClone(f.state.pendingPreviewRecipe);
  f.state.previewPending = false; const gate = deferred(); f.delays.set("first@2", gate);
  const pending = f.attach(); await until(() => f.reads.includes("first@2"));
  assert.equal(f.state.previewRenderPending, true); assert.ok(f.host.querySelector('[role="progressbar"]'));
  viewport.camera = { name: "user-orbit-while-loading", radius: 317 };
  gate.resolve(); assert.equal(await pending, true); assert.equal(await waitForPartDrawingPreview(f.mount), true);
  assert.deepEqual(f.reads.slice(before), ["first@2"]);
  assert.equal(viewport.geometryObjects.get("base"), base); assert.equal(viewport.geometryObjects.get("second"), second);
  assert.deepEqual(viewport.camera, { name: "user-orbit-while-loading", radius: 317 }); assert.equal(viewport.fitCount, 1);
  assert.equal(f.state.previewRenderPending, false); assert.equal(f.host.querySelector('[role="progressbar"]'), null);
  disposePartDrawingPreview(f.mount);
});

test("a newer edit suppresses a delayed old operand response while keeping the base and other operands", async () => {
  const f = fixture(); await f.attach(); const viewport = f.viewports[0];
  const oldGate = deferred(); f.delays.set("first@2", oldGate);
  f.state.revision = 2; f.state.preview = f.preview(2, { first: 2 });
  const old = f.attach(); await until(() => f.reads.includes("first@2"));
  f.state.revision = 3; f.state.previewPending = true; f.state.pendingPreviewRecipe = f.recipe();
  f.state.pendingPreviewRecipe.features[0].station = 180;
  await f.attach();
  assert.deepEqual(viewport.visibleEntityIds, ["drawing:base", "drawing:feature:second"]);
  oldGate.resolve(); assert.equal(await old, false);
  assert.deepEqual(viewport.visibleEntityIds, ["drawing:base", "drawing:feature:second"]);
  assert.equal(f.state.previewRenderError, "");
  f.state.preview = f.preview(3, { first: 3 }); f.state.previewPending = false;
  f.state.previewRecipe = structuredClone(f.state.pendingPreviewRecipe);
  assert.equal(await f.attach(), true); assert.equal(viewport.geometryObjects.get("first").version, 3);
  disposePartDrawingPreview(f.mount);
});

test("only a changed base bounds refits; deleting an operand removes its stable row", async () => {
  const f = fixture(); await f.attach(); const viewport = f.viewports[0];
  viewport.camera.name = "left";
  f.state.revision = 2; f.state.preview = f.preview(2, { base: 2, length: 2000 });
  await f.attach(); assert.equal(viewport.fitCount, 2); assert.equal(viewport.camera.name, "left");
  f.state.revision = 3; f.state.preview = { ...f.state.preview, revision: 3, toolPreviews: f.state.preview.toolPreviews.slice(1) };
  await f.attach(); assert.equal(viewport.fitCount, 2);
  assert.deepEqual(viewport.rows.map(row => row.entityId), ["drawing:base", "drawing:feature:second"]);
  disposePartDrawingPreview(f.mount);
});

test("resource failure retains unchanged operands, displays a local error, and retries without native recomputation", async () => {
  const f = fixture(); await f.attach(); const viewport = f.viewports[0];
  f.state.revision = 2; f.state.preview = f.preview(2, { first: 2 }); f.failures.add("first@2");
  assert.equal(await f.attach(), false); assert.match(f.state.previewRenderError, /first@2/);
  assert.deepEqual(viewport.visibleEntityIds, ["drawing:base", "drawing:feature:second"]);
  assert.equal(f.host.querySelector("[data-part-drawing-preview-notice]").getAttribute("role"), "alert");
  const requests = f.reads.length; await f.attach(); assert.equal(f.reads.length, requests, "No uncontrolled error retry loop");
  const retry = f.host.querySelector("[data-part-drawing-preview-retry]");
  assert.equal(retry.textContent, "重试显示"); assert.equal(retry.style.pointerEvents, "auto");
  f.failures.clear(); retry.click();
  assert.equal(await waitForPartDrawingPreview(f.mount), true); assert.equal(f.state.previewRenderError, ""); assert.equal(f.view.pending, false);
  disposePartDrawingPreview(f.mount);
});

test("closing or replacing an editor during loading cannot mutate the next drawing state", async () => {
  const f = fixture(); const gate = deferred(); f.delays.set("base@1", gate);
  const pending = f.attach(); await until(() => f.reads.includes("base@1"));
  const viewport = f.viewports[0]; disposePartDrawingPreview(f.mount);
  const nextState = { revision: 20, previewRenderError: "new-state" };
  f.view.tubeDesignerPartDrawing = { state: nextState }; gate.resolve();
  assert.equal(await pending, false); assert.equal(viewport.disposed, true);
  assert.equal(nextState.previewRenderError, "new-state"); assert.equal(f.state.previewRenderPending, false);
  assert.equal(await waitForPartDrawingPreview(f.mount), false);
});

test("invalid main edits cancel the first pending snapshot instead of displaying an obsolete base later", async () => {
  const f = fixture(), gate = deferred(); f.delays.set("base@1", gate);
  const old = f.attach(); await until(() => f.reads.includes("base@1"));
  const viewport = f.viewports[0];
  f.state.revision = 2; f.state.pendingPreviewRecipe = null;
  f.state.previewComputeError = { revision: 2, message: "主管长度无效" }; f.drawing.mode = "main";
  assert.equal(await f.attach(), false); assert.deepEqual(viewport.visibleEntityIds, []);
  gate.resolve(); assert.equal(await old, false); assert.deepEqual(viewport.visibleEntityIds, []);
  assert.equal(f.host.querySelector("[data-part-drawing-preview-notice]").getAttribute("role"), "alert");
  assert.equal(f.host.querySelector("[data-part-drawing-preview-retry]"), null, "Invalid parameters cannot use resource-only retry");
  assert.equal(viewport.fitCount, 0); assert.equal(f.state.previewRenderPending, false);
  disposePartDrawingPreview(f.mount);
});

test("invalid operand input without a pending recipe hides just that operand and reports the local compute phase", async () => {
  const f = fixture(); await f.attach(); const viewport = f.viewports[0];
  f.state.revision = 2; f.state.pendingPreviewRecipe = null; f.state.editingId = "first"; f.drawing.mode = "feature";
  f.state.previewComputeError = { revision: 2, message: "请输入有效尺寸" };
  assert.equal(await f.attach(), false);
  assert.deepEqual(viewport.visibleEntityIds, ["drawing:base", "drawing:feature:second"]);
  f.state.previewComputeError = null; f.state.previewPending = true; f.state.previewPhase = "正在构造当前支管拉伸体";
  await f.attach();
  const notice = f.host.querySelector("[data-part-drawing-preview-notice]");
  assert.equal(notice.children[0].textContent, f.state.previewPhase);
  assert.equal(notice.querySelector('[role="progressbar"]').getAttribute("aria-valuetext"), f.state.previewPhase);
  disposePartDrawingPreview(f.mount);
});
