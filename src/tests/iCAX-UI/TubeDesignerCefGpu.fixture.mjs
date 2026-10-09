// Production sketch and WebGL modules with a fixed blank transport for native CEF smoke.
export async function mountCefGpuFixture(runtime) {
  const metrics = window.__cefGpuMetrics = { active: false, moves: [], callbacks: [], frames: [], updates: [], latency: [], longTasks: [], nativeCalls: 0, resourceReads: 0 };
  const add = EventTarget.prototype.addEventListener;
  EventTarget.prototype.addEventListener = function(type, callback, options) {
    if (type === 'pointermove' && this instanceof SVGElement && this.matches('[data-tube-sketch-canvas]') && typeof callback === 'function') {
      const original = callback;
      callback = function(event) {
        if (!metrics.active) return original.call(this, event);
        const start = performance.now(); metrics.moves.push(start);
        try { return original.call(this, event); } finally { metrics.callbacks.push(performance.now() - start); }
      };
    }
    return add.call(this, type, callback, options);
  };
  const raf = window.requestAnimationFrame;
  window.requestAnimationFrame = callback => raf.call(window, time => {
    const start = performance.now();
    try { callback(time); } finally { if (metrics.active) metrics.frames.push(performance.now() - start); }
  });
  const html = Object.getOwnPropertyDescriptor(Element.prototype, 'innerHTML');
  Object.defineProperty(Element.prototype, 'innerHTML', { ...html, set(value) {
    const result = html.set.call(this, value);
    if (metrics.active && value && this.matches('[data-tube-sketch-draft-preview]')) {
      const now = performance.now(); metrics.updates.push(now);
      if (metrics.moves.length) metrics.latency.push(now - metrics.moves.at(-1));
    }
    return result;
  } });
  new PerformanceObserver(list => { if (metrics.active) metrics.longTasks.push(...list.getEntries().map(e => e.duration)); }).observe({ type: 'longtask' });
  const sketch = await import(runtime + 'apps/tube-designer/webpage/sketchArea.mjs');
  const preview = await import(runtime + 'apps/tube-designer/webpage/sideSketchPreview.mjs');
  const { createThreeViewport } = await import(runtime + 'iCAX-UI/SDK/Viewport/threeViewport.mjs');
  const { encodeNestingGeometry } = await import(runtime + 'apps/tube-designer/webpage/nestingPreview.mjs');
  const { tubeDesignerCss } = await import(runtime + 'apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
  const THREE = await import(runtime + 'iCAX-UI/SDK/ThirdParty/three/three.module.js');
  document.head.insertAdjacentHTML('beforeend', `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px "Segoe UI",sans-serif;background:#13252d}</style>`);
  const geometry = new THREE.BoxGeometry(400, 40, 20);
  const bytes = encodeNestingGeometry({ positions: [...geometry.attributes.position.array], indices: [...geometry.index.array] }); geometry.dispose();
  const view = { activeAreaId: 'nesting', scene: { tubeDesigner: { nestingGroups: [] } } }, mount = document.querySelector('#app');
  const report = { available: true, length: 400, profile: { kind: 'rect', width: 40, depth: 20 }, unfolding: { available: true, perimeter: 120, length: 400,
    surfaces: [{ inner: false, uPeriod: 120, rectangle: { available: true, width: 400, height: 120 },
      wires: [40, 60, 100].map(u => ({ role: 'profile-junction', points: [[0, u], [400, u]] })) }] },
    preview: { baseGeometry: { url: 'icax-resource://cef-gpu-blank', version: 1 }, baseBounds: { min: [-200, -20, -10], max: [200, 20, 10] } } };
  const fixture = window.__cefGpuFixture = { view, mount, sketch, preview };
  Object.defineProperty(fixture, 'state', { get() { return view.tubeDesignerSketch; } });
  Object.defineProperty(fixture, 'draft', { get() { return fixture.state.sideByPart[fixture.state.targetPartId]; } });
  const context = { mount, sceneProxy: { async invoke(method) { metrics.nativeCalls++; throw Error('Unexpected operation: ' + method); },
    resources: { async get() { if (metrics.active) metrics.resourceReads++; return new Response(bytes); } } } };
  const ops = { createSideSketchViewport(options) { return createThreeViewport(options); }, renderProject() {
    if (!sketch.patchSketchDialogDom(context, view, mount, ops)) { mount.innerHTML = sketch.renderSectionSketchDialog(context, view); sketch.rememberSketchDialogDom(view, mount, context.sceneProxy); }
    sketch.attachSketchAreaInteractions(context, view, mount, ops);
  } };
  mount.addEventListener('click', event => { const target = event.target.closest('[data-cam-action]');
    if (target) void sketch.handleSketchAreaAction(context, view, target.dataset.camAction, target, ops); });
  sketch.beginNewPartSideSketch(view, { length: 400, profile: report.profile, previewResourceKey: 'cef-gpu' }, report);
  view.tubeDesignerSketchDialogOpen = true;
  fixture.draft.entities = Array.from({ length: 96 }, (_, index) => ({ id: `circle-${index}`, kind: 'circle',
    cx: 12 + index % 8 * 13, cy: 30 + Math.floor(index / 8) * 22, radius: 3.5, closed: true }));
  fixture.state.sideViewport = { offsetU: 0, zoom: 1 / 6 }; ops.renderProject();
  if (!await preview.waitForSideSketchPreview(mount)) throw Error('Production WebGL preview did not initialize');
  window.__cefGpuReady = true;
}
