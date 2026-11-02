import assert from 'node:assert/strict';
import test from 'node:test';
import { attachSideSketchPreview, disposeSideSketchPreview } from '../../apps/tube-designer/webpage/sideSketchPreview.mjs';

test('a selected piece keeps its actual display frame and live camera when its CAD preview uses the full source stock', async () => {
  const document = { createElement: () => element() };
  function element() {
    return {
      ownerDocument: document, dataset: {}, style: {}, isConnected: true, children: [],
      append(child) { this.children.push(child); child.parent = this; },
      remove() { if (this.parent) this.parent.children = this.parent.children.filter(child => child !== this); },
      setAttribute() {},
      querySelector(selector) {
        return this.children.find(child => selector === '[data-side-sketch-viewport]'
          ? Object.hasOwn(child.dataset, 'sideSketchViewport')
          : Object.hasOwn(child.dataset, 'sideSketchRenderNotice')) ?? null;
      },
    };
  }
  const mount = element(), host = element();
  host.dataset.sideSketchViewport = ''; mount.append(host);
  const reference = (url, version = 1) => ({ url, version });
  const actual = { min: [0, -30, -20], max: [490, 30, 20] };
  const proxy = { resources: {}, invokes: [], invoke(...args) { this.invokes.push(args); } };
  const state = { mode: 'side', sidePreviewPayload: {}, targetPartId: 'piece-2', sidePreviewResourceKey: 'selected-piece',
    sideCadSession: { sceneProxy: proxy },
    sidePreview: { geometry: reference('persisted-piece'), baseGeometry: reference('persisted-piece'),
      material: reference('material'), baseMaterial: reference('material'), baseBounds: actual },
  };
  const view = { tubeDesignerSketch: state, tubeDesignerSketchDialogOpen: true };
  const viewport = {
    snapshots: [], fitCount: 0, camera: { orientation: 'iso', radius: 100 },
    mount() {}, setStandardView() {}, setVisibleEntityIds() {}, retainViewResources() {}, dispose() {},
    fitViewToViewport() { this.fitCount++; },
    async applyViewSnapshot(snapshot) { this.snapshots.push(snapshot); return { applied: true, entityIds: snapshot.rows.map(row => row.entityId) }; },
  };
  const context = { sceneProxy: proxy }, ops = { createSideSketchViewport: () => viewport };
  assert.equal(await attachSideSketchPreview(context, view, mount, ops), true);
  assert.equal(viewport.snapshots[0].rows[0].data.localToWorldMatrix[3], -245);
  viewport.camera = { orientation: 'user-orbit', radius: 317, target: [12, 3, 4] };
  const camera = structuredClone(viewport.camera);
  state.sidePreview = { geometry: reference('preview-piece'), baseGeometry: reference('unchanged-source-stock'),
    material: reference('material'), baseMaterial: reference('material'), bounds: actual,
    baseBounds: { min: [-510, -30, -20], max: [490, 30, 20] },
  };
  assert.equal(await attachSideSketchPreview(context, view, mount, ops), true);
  assert.equal(viewport.snapshots[1].rows[0].data.localToWorldMatrix[3], -245,
    'Replacing a saved piece with a current CAD preview must not shift it by the removed source length');
  assert.deepEqual(viewport.camera, camera);
  assert.equal(viewport.fitCount, 1, 'Receiving a parameter change must preserve the user camera');
  state.sidePreview.geometry = reference('preview-piece', 2);
  assert.equal(await attachSideSketchPreview(context, view, mount, ops), true);
  assert.equal(viewport.snapshots.length, 3, 'A changed actual mesh must update even when the source stock reference is unchanged');
  assert.equal(viewport.snapshots[2].rows[0].data.geometry.version, 2);
  disposeSideSketchPreview(mount);
  assert.equal(proxy.invokes[0][0], 'TubeDesigner.ReleaseTubeCadDocumentPreview');
});
