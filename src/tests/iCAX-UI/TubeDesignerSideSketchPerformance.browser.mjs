// Browser performance fixture. Production SVG, snapping and WebGL; fixed native transport.
// Optional asset snapshot pins the pre-change source for a reproducible comparison.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const sourceRoot = resolve(repository, 'src');
const label = process.env.ICAX_PERFORMANCE_LABEL || 'current';
const output = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repository, 'output/tests/side-sketch-performance'), label);
const snapshotRoot = process.env.ICAX_PERFORMANCE_ASSET_SNAPSHOT && resolve(process.env.ICAX_PERFORMANCE_ASSET_SNAPSHOT);
mkdirSync(output, { recursive: true });
const assets = new Map();
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs');
const browserArgs = process.env.ICAX_BROWSER_DISABLE_GPU === '1' ? ['--disable-gpu', '--disable-gpu-compositing'] : [];
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome', args: browserArgs });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1500, height: 960 } });
  page.setDefaultTimeout(60000);
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    const perf = window.__sketchPerf = { active: false, callbacks: [], raf: [], curveEvaluations: 0, samplePoints: 0, sampleCalls: 0,
      nearestCalls: 0, innerHTML: {}, mutations: 0, added: 0, removed: 0, renderProject: 0, renderProjectMs: 0,
      resourceReads: 0, nativeCalls: 0, webglFrames: 0, axisFrames: 0, cloneCalls: 0, longTasks: [], previewResize: 0, previewApply: 0,
      previewLatency: [], previewTimes: [], lastPointerMove: 0 };
    const nativeClone = window.structuredClone;
    window.structuredClone = (...args) => { if (perf.active) perf.cloneCalls++; return nativeClone(...args); };
    new PerformanceObserver(list => {
      if (perf.active) perf.longTasks.push(...list.getEntries().map(entry => ({ start: entry.startTime, ms: entry.duration })));
    }).observe({ type: 'longtask', buffered: false });
    const originalAdd = EventTarget.prototype.addEventListener;
    EventTarget.prototype.addEventListener = function(type, callback, options) {
      if (this instanceof SVGElement && this.matches('[data-tube-sketch-canvas]')
        && ['pointermove', 'pointerdown', 'pointerup', 'wheel'].includes(type) && typeof callback === 'function') {
        const original = callback;
        callback = function(event) {
          if (type === 'pointerdown') window.__sketchPointerId = event.pointerId;
          (window.__sketchLastCanvasInput ??= {})[type] = { x: event.clientX, y: event.clientY };
          if (!perf.active) return original.call(this, event);
          const start = performance.now();
          if (type === 'pointermove') perf.lastPointerMove = start;
          try { return original.call(this, event); }
          finally { perf.callbacks.push({ type, ms: performance.now() - start }); }
        };
      }
      return originalAdd.call(this, type, callback, options);
    };
    const nativeRAF = window.requestAnimationFrame;
    window.requestAnimationFrame = callback => nativeRAF.call(window, time => {
      const start = performance.now();
      try { callback(time); }
      finally { if (perf.active) perf.raf.push(performance.now() - start); }
    });
    const html = Object.getOwnPropertyDescriptor(Element.prototype, 'innerHTML');
    Object.defineProperty(Element.prototype, 'innerHTML', { ...html, set(value) {
      if (perf.active) {
        const key = this.matches('.tube-sketch-geometry') ? 'geometry'
          : this.matches('[data-tube-sketch-guide]') ? 'guide'
            : this.matches('[data-tube-sketch-cursor]') ? 'cursor'
              : this.matches('[data-tube-sketch-draft-preview]') ? 'draftPreview' : this.tagName.toLowerCase();
        perf.innerHTML[key] ??= { calls: 0, chars: 0 };
        perf.innerHTML[key].calls++; perf.innerHTML[key].chars += String(value).length;
      }
      const result = html.set.call(this, value);
      if (perf.active && this.matches('[data-tube-sketch-draft-preview]') && perf.lastPointerMove && value) {
        const time = performance.now(); perf.previewTimes.push(time); perf.previewLatency.push(time - perf.lastPointerMove);
      }
      return result;
    } });
  });
  await page.route('http://sketch-perf.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body>' });
    if (!pathname.startsWith('/src/')) return route.abort();
    const relative = pathname.slice(5), sourcePath = resolve(sourceRoot, relative);
    if (!sourcePath.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(sourcePath)) return route.abort();
    const selectedPath = snapshotRoot && existsSync(resolve(snapshotRoot, relative)) ? resolve(snapshotRoot, relative) : sourcePath;
    let code = readFileSync(selectedPath, 'utf8');
    assets.set(relative, { path: selectedPath, sha256: createHash('sha256').update(code).digest('hex') });
    if (relative.endsWith('/sketchArea.mjs') && process.env.ICAX_PERFORMANCE_DISABLE_VIEWPORT_OVERLAY === '1') {
      const pattern = /\n    if \(pendingViewportEvent\) \{[\s\S]*?\n    \}\n  \};\n  const scheduleInteraction/;
      assert.ok(pattern.test(code), 'The read-only before-fix route must target exactly the viewport overlay repaint');
      code = code.replace(pattern, '\n  };\n  const scheduleInteraction')
        .replace('    if (pendingViewportEvent) pendingViewportEvent = event;\n', '')
        .replaceAll('pendingViewportEvent = event;', '');
    }
    if (relative.endsWith('/sketchGeometry.mjs')) {
      for (const [name, counter, resultCounter] of [['curvePoint', 'curveEvaluations'], ['pathSamples', 'sampleCalls', 'samplePoints'], ['nearestSegment', 'nearestCalls']]) {
        const declaration = new RegExp(`export function ${name}\\s*\\(`);
        if (!declaration.test(code)) continue;
        code = code.replace(declaration, `function __perf_${name}(`);
        code += `\nexport function ${name}(...args) { const p=globalThis.__sketchPerf;if(p?.active)p.${counter}++;const result=__perf_${name}(...args);${resultCounter ? `if(p?.active)p.${resultCounter}+=result.length;` : ''}return result;}\n`;
      }
    }
    return route.fulfill({ contentType: 'text/javascript', body: code });
  });
  await page.goto('http://sketch-perf.test/');
  await page.addStyleTag({ content: readFileSync(resolve(sourceRoot, 'apps/_shared/workbench/styles/laser3dcam.css'), 'utf8') });
  await page.evaluate(async () => {
    const sketch = await import('/src/apps/tube-designer/webpage/sketchArea.mjs');
    const preview = await import('/src/apps/tube-designer/webpage/sideSketchPreview.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const { encodeNestingGeometry } = await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    const { tubeDesignerCss } = await import('/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    document.head.insertAdjacentHTML('beforeend', `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px "Segoe UI",sans-serif;background:#13252d}</style>`);
    const geometry = new THREE.BoxGeometry(400, 40, 20);
    const bytes = encodeNestingGeometry({ positions: [...geometry.attributes.position.array], indices: [...geometry.index.array] }); geometry.dispose();
    const view = { activeAreaId: 'nesting', scene: { tubeDesigner: { nestingGroups: [] } } }, mount = document.querySelector('#app');
    const report = { available: true, length: 400, profile: { kind: 'rect', width: 40, depth: 20 },
      unfolding: { available: true, perimeter: 120, length: 400, surfaces: [{ inner: false, uPeriod: 120,
        rectangle: { available: true, width: 400, height: 120 }, wires: [40, 60, 100].map(u => ({ role: 'profile-junction', points: [[0, u], [400, u]] })) }] },
      preview: { baseGeometry: { url: 'icax-resource://perf-blank', version: 1 }, baseBounds: { min: [-200, -20, -10], max: [200, 20, 10] } } };
    const f = window.fixture = { view, mount, sketch, preview };
    Object.defineProperty(f, 'state', { get() { return view.tubeDesignerSketch; } });
    Object.defineProperty(f, 'draft', { get() { return f.state.sideByPart[f.state.targetPartId]; } });
    f.canvasMetrics = () => {
      const svg = f.mount.querySelector('[data-tube-sketch-canvas]'), d = svg.dataset;
      return { xMin:Number(d.sketchXMin), xMax:Number(d.sketchXMax), yMin:Number(d.sketchYMin), yMax:Number(d.sketchYMax),
        left:Number(d.sketchCanvasLeft), right:Number(d.sketchCanvasRight), top:Number(d.sketchCanvasTop), bottom:Number(d.sketchCanvasBottom) };
    };
    f.toModel = (client, m = f.canvasMetrics()) => {
      const svg = f.mount.querySelector('[data-tube-sketch-canvas]'), p = svg.createSVGPoint();
      p.x = client.x; p.y = client.y;
      const local = p.matrixTransform(svg.getScreenCTM().inverse());
      return [m.xMin + (local.x - m.left) / (m.right - m.left) * (m.xMax - m.xMin),
        m.yMin + (m.bottom - local.y) / (m.bottom - m.top) * (m.yMax - m.yMin)];
    };
    f.toCanvas = ([x,y]) => {
      const m = f.canvasMetrics();
      return [m.left + (x - m.xMin) / (m.xMax - m.xMin) * (m.right - m.left),
        m.bottom - (y - m.yMin) / (m.yMax - m.yMin) * (m.bottom - m.top)];
    };
    f.context = { mount, sceneProxy: { async invoke(method) { if (window.__sketchPerf.active) window.__sketchPerf.nativeCalls++; throw Error('Unexpected native operation: ' + method); },
      resources: { async get() { if (window.__sketchPerf.active) window.__sketchPerf.resourceReads++; return new Response(bytes); } } } };
    f.ops = { createSideSketchViewport(options) {
      const viewport = f.viewport = createThreeViewport(options), p = window.__sketchPerf;
      for (const [renderer, key] of [[viewport.renderer, 'webglFrames'], [viewport.axisRenderer, 'axisFrames']]) {
        const original = renderer.render.bind(renderer);
        renderer.render = (...args) => { if (p.active) p[key]++; return original(...args); };
      }
      for (const [method, counter] of [['resize', 'previewResize'], ['applyViewSnapshot', 'previewApply']]) {
        const original = viewport[method].bind(viewport);
        viewport[method] = (...args) => { if (p.active) p[counter]++; return original(...args); };
      }
      return viewport;
    }, renderProject() {
      const p = window.__sketchPerf, start = performance.now();
      if (p.active) p.renderProject++;
      if (!sketch.patchSketchDialogDom(f.context, view, mount, f.ops)) {
        mount.innerHTML = sketch.renderSectionSketchDialog(f.context, view);
        sketch.rememberSketchDialogDom(view, mount, f.context.sceneProxy);
      }
      sketch.attachSketchAreaInteractions(f.context, view, mount, f.ops);
      if (p.active) p.renderProjectMs += performance.now() - start;
    } };
    mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target) void sketch.handleSketchAreaAction(f.context, view, target.dataset.camAction, target, f.ops);
    });
    sketch.beginNewPartSideSketch(view, { length: 400, profile: report.profile, previewResourceKey: 'perf' }, report);
    view.tubeDesignerSketchDialogOpen = true; f.ops.renderProject();
    f.entities = count => Array.from({ length: count }, (_, index) => {
      const cx = 12 + index % 8 * 13, cy = 30 + Math.floor(index / 8) * 22;
      if (index % 3 === 0) return { id: `entity-${index}`, kind: 'circle', cx, cy, radius: 3.5, closed: true };
      if (index % 3 === 1) return { id: `entity-${index}`, kind: 'rectangle', x: cx - 4, y: cy - 5, width: 8, height: 10, closed: true };
      return { id: `entity-${index}`, kind: 'path', closed: false, segments: [{ kind: 'bezier', points: [[cx - 4, cy], [cx - 2, cy + 8], [cx + 2, cy - 8], [cx + 4, cy]] }] };
    });
    f.setup = (count, zoom, snap = true, curved = false) => {
      f.draft.entities = curved ? [f.entities(1)[0], ...Array.from({ length: 12 }, (_, index) => ({ id: `curved-${index}`, kind: 'path', closed: false,
        segments: Array.from({ length: 12 }, (_, segment) => ({ kind: 'bezier', points: [
          [4 + segment * 20, 40 + index * 25], [9 + segment * 20, 55 + index * 25],
          [19 + segment * 20, 25 + index * 25], [24 + segment * 20, 40 + index * 25]] })) }))] : f.entities(count);
      f.draft.selectedIds = []; f.draft.selectedPoints = []; f.draft.dirty = false;
      f.state.sideViewport = { offsetU: 0, offsetS: 0, zoom }; f.state.snapEnabled = snap; f.state.tool = 'select'; f.state.command = null;
      f.ops.renderProject();
    };
    const observer = new MutationObserver(records => {
      const p = window.__sketchPerf; if (!p.active) return;
      p.mutations += records.length;
      for (const record of records) { p.added += record.addedNodes.length; p.removed += record.removedNodes.length; }
    });
    observer.observe(mount, { subtree: true, childList: true });
    f.reset = () => {
      const p = window.__sketchPerf;
      Object.assign(p, { active: true, callbacks: [], raf: [], curveEvaluations: 0, samplePoints: 0, sampleCalls: 0,
        nearestCalls: 0, innerHTML: {}, mutations: 0, added: 0, removed: 0, renderProject: 0, renderProjectMs: 0,
        resourceReads: 0, nativeCalls: 0, webglFrames: 0, axisFrames: 0, cloneCalls: 0, longTasks: [], previewResize: 0, previewApply: 0,
        previewLatency: [], previewTimes: [], lastPointerMove: 0 });
      f.start = performance.now(); f.svg = mount.querySelector('[data-tube-sketch-canvas]'); f.canvas = viewportCanvas();
    };
    const viewportCanvas = () => mount.querySelector('.icax-three-viewport-canvas');
    f.stop = () => {
      const p = window.__sketchPerf; p.active = false;
      const result = structuredClone(p); result.elapsedMs = performance.now() - f.start;
      result.svgRetained = f.svg === mount.querySelector('[data-tube-sketch-canvas]'); result.canvasRetained = f.canvas === viewportCanvas();
      result.entityCount = f.draft.entities.length; result.zoom = f.state.sideViewport.zoom;
      result.geometryNodes = mount.querySelector('.tube-sketch-geometry').querySelectorAll('*').length;
      return result;
    };
  });
  assert.equal(await page.evaluate(async () => await window.fixture.preview.waitForSideSketchPreview(window.fixture.mount)), true);
  const rows = [];
  const summarize = values => {
    const sorted = values.toSorted((a, b) => a - b);
    return { count: sorted.length, total: sorted.reduce((sum, value) => sum + value, 0), max: sorted.at(-1) || 0,
      p50: sorted[Math.floor(sorted.length * .5)] || 0, p95: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * .95))] || 0 };
  };
  const point = async (x, y) => page.locator('[data-tube-sketch-canvas]').evaluate((svg, [x, y]) => {
    const p = svg.createSVGPoint(); p.x = x; p.y = y; const s = p.matrixTransform(svg.getScreenCTM()); return { x: s.x, y: s.y };
  }, [x, y]);
  const scenarios = process.env.ICAX_PERFORMANCE_CIRCLE_ONLY === '1'
    ? [{ name: 'circle-small', count: 4, zoom: 1, actions: ['draw-circle'] },
      { name: 'circle-many', count: 96, zoom: 1, actions: ['draw-circle'] },
      { name: 'circle-many-periods', count: 96, zoom: 1 / 6, actions: ['draw-circle'] }]
    : [{ name: 'small', count: 6, zoom: 1 }, { name: 'many', count: 96, zoom: 1 },
    { name: 'many-periods', count: 96, zoom: 1 / 6 }, { name: 'curved-paths', count: 13, zoom: 1, curved: true },
    { name: 'curved-no-snap', count: 13, zoom: 1, curved: true, snap: false, actions: ['hover'] }];
  for (const scenario of scenarios.filter(item => !process.env.ICAX_PERFORMANCE_SCENARIOS
    || process.env.ICAX_PERFORMANCE_SCENARIOS.split(',').includes(item.name))) {
    for (const action of scenario.actions || ['hover', 'drag', 'pan', 'wheel']) {
      await page.evaluate(scenario => window.fixture.setup(scenario.count, scenario.zoom, scenario.snap !== false, scenario.curved), scenario);
      if (action === 'draw-circle') {
        if (scenario.name === 'circle-small') await page.evaluate(() => {
          const f = window.fixture;
          f.draft.entities = f.draft.entities.map(entity => ({ id: entity.id, kind: 'circle', cx: entity.cx ?? entity.x ?? 20,
            cy: entity.cy ?? entity.y ?? 40, radius: 3.5, closed: true })); f.ops.renderProject();
        });
        await page.locator('[data-sketch-command="sketch.circle"]').click();
        const center = await point(450, 145);
        await page.mouse.click(center.x, center.y);
        assert.equal(await page.evaluate(() => window.fixture.state.command?.tool), 'circle');
      }
      if (action === 'drag') await page.evaluate(() => { const f = window.fixture; f.draft.selectedIds = ['entity-0']; f.ops.renderProject(); });
      await page.evaluate(() => window.fixture.reset());
      const start = await point(180, 300), end = await point(800, 340);
      if (action === 'draw-circle') {
        const end = await point(700, 190);
        await page.mouse.move(end.x, end.y, { steps: 30 });
      } else if (action === 'hover') {
        await page.mouse.move(start.x, start.y); await page.mouse.move(end.x, end.y, { steps: 20 });
      } else if (action === 'drag') {
        const grip = page.locator('[data-tube-sketch-grip-entity-id="entity-0"][data-tube-sketch-grip-role="move"]').first();
        const bounds = await grip.boundingBox(); assert.ok(bounds);
        await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2); await page.mouse.down();
        await page.mouse.move(bounds.x + bounds.width / 2 + 140, bounds.y + bounds.height / 2 - 25, { steps: 20 }); await page.mouse.up();
      } else if (action === 'pan') {
        await page.mouse.move(start.x, start.y); await page.mouse.down({ button: 'middle' });
        await page.mouse.move(end.x, end.y, { steps: 20 }); await page.mouse.up({ button: 'middle' });
      } else {
        await page.mouse.move(start.x, start.y);
        for (let i = 0; i < 12; i++) await page.mouse.wheel(0, i % 2 ? -40 : 40);
      }
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      const result = await page.evaluate(() => window.fixture.stop());
      const row = { scenario: scenario.name, action, ...result, handlerMs: summarize(result.callbacks.map(item => item.ms)), rafMs: summarize(result.raf) };
      row.cpuWorkMs = row.handlerMs.total + row.rafMs.total;
      if (action === 'draw-circle') {
        row.previewLatencyMs = summarize(result.previewLatency);
        row.previewIntervalMs = summarize(result.previewTimes.slice(1).map((time, index) => time - result.previewTimes[index]));
        const end = await point(700, 190);
        row.previewCircle = await page.evaluate(end => {
          const f = window.fixture, svg = f.mount.querySelector('[data-tube-sketch-canvas]');
          const circle = svg.querySelector('[data-tube-sketch-draft-preview] circle');
          const p = svg.createSVGPoint(); p.x = end.x; p.y = end.y;
          const local = p.matrixTransform(svg.getScreenCTM().inverse());
          return { preview: circle && { cx: Number(circle.getAttribute('cx')), cy: Number(circle.getAttribute('cy')), r: Number(circle.getAttribute('r')) },
            center: [...f.state.command.points[0]], expectedEnd: f.toModel(end) };
        }, end);
        assert.ok(row.previewCircle.preview?.r > 0, 'Circle command must display the actual radius rubber band');
        await page.screenshot({ path: resolve(output, `${scenario.name}-rubber-band.png`) });
        await page.evaluate(() => window.fixture.reset());
        await page.mouse.click(end.x, end.y);
        await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
        row.commit = await page.evaluate(() => ({ work: window.fixture.stop(), entity: window.fixture.draft.entities.at(-1), command: window.fixture.state.command }));
        const expected = row.previewCircle;
        assert.equal(row.commit.command, null); assert.equal(row.commit.entity.kind, 'circle');
        assert.equal(row.commit.work.entityCount, scenario.count + 1);
        assert.ok(Math.hypot(row.commit.entity.cx - expected.center[0], row.commit.entity.cy - expected.center[1]) < 1e-6);
        assert.ok(Math.abs(row.commit.entity.radius - Math.hypot(expected.expectedEnd[0] - expected.center[0], expected.expectedEnd[1] - expected.center[1])) < 1e-6,
          `The final radius click must commit exact model coordinates: ${JSON.stringify({ entity: row.commit.entity, expected })}`);
      }
      rows.push(row);
      writeFileSync(resolve(output, 'partial-results.json'), JSON.stringify({ label, rows, assets: [...assets] }, null, 2));
      console.log(JSON.stringify({ scenario: row.scenario, action, handler: row.handlerMs, raf: row.rafMs, geometryNodes: row.geometryNodes,
        curveEvaluations: row.curveEvaluations, samplePoints: row.samplePoints, renderProject: row.renderProject, innerHTML: row.innerHTML,
        added: row.added, removed: row.removed, webglFrames: row.webglFrames, cloneCalls: row.cloneCalls, longTasks: row.longTasks,
        previewLatency: row.previewLatencyMs, previewInterval: row.previewIntervalMs, commitHandlers: row.commit?.work.callbacks }));
      assert.equal(result.svgRetained, true); assert.equal(result.canvasRetained, true);
    }
  }
  await page.evaluate(() => window.fixture.setup(96, 1 / 6));
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const periodic = await page.evaluate(() => {
    const f = window.fixture, svg = f.mount.querySelector('[data-tube-sketch-canvas]');
    const { offsetU, zoom } = f.state.sideViewport;
    return { offsetU, zoom, metrics: f.canvasMetrics(), uses: [...svg.querySelectorAll('use[data-tube-sketch-periodic-copy]')].map(use => ({
      reference: Boolean(document.getElementById(use.getAttribute('href').slice(1))), pointer: getComputedStyle(use).pointerEvents,
      translateX: use.transform.baseVal.consolidate().matrix.e, translateY: use.transform.baseVal.consolidate().matrix.f,
    })), guides: [...svg.querySelectorAll('[data-tube-sketch-profile-junction]')].map(path => ({
      u: Number(path.dataset.tubeSketchProfileU), coordinates: [...path.getAttribute('d').matchAll(/[-+]?\d*\.?\d+(?:e[-+]?\d+)?/gi)].map(match => Number(match[0])),
    })) };
  });
  for (const guide of periodic.guides) {
    const m = periodic.metrics, expected = m.left + (guide.u - m.xMin) / (m.xMax - m.xMin) * (m.right - m.left);
    assert.ok(Math.abs(guide.coordinates[0] - expected) < 1e-6);
    assert.ok([40, 60, 100].some(value => Math.abs(((guide.u % 120) + 120) % 120 - value) < 1e-6));
  }
  if (process.env.ICAX_PERFORMANCE_EXPECT_OPTIMIZED === '1') {
    assert.ok(periodic.uses.length > 20 && periodic.uses.length < 100,
      'Forty-two visible periods reuse whole source groups instead of thousands of per-entity references');
    for (const use of periodic.uses) {
      assert.equal(use.reference, true); assert.equal(use.pointer, 'none'); assert.equal(use.translateY, 0);
      const m = periodic.metrics, periods = use.translateX / (120 / (m.xMax - m.xMin) * (m.right - m.left));
      assert.ok(Math.abs(periods - Math.round(periods)) < 1e-6, 'SVG references keep the actual circumference period');
    }
  }
  await page.screenshot({ path: resolve(output, 'many-periods.png') });
  const circleNavigation = [];
  if (process.env.ICAX_PERFORMANCE_EXPECT_OPTIMIZED === '1') {
    for (const action of ['wheel', 'pan', 'wheel-then-move']) {
      await page.evaluate(() => window.fixture.setup(6, 1, false));
      await page.locator('[data-sketch-command="sketch.circle"]').click();
      const center = await point(450, 145), end = await point(700, 190);
      await page.mouse.click(center.x, center.y);
      await page.mouse.move(end.x, end.y, { steps: 4 });
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      await page.evaluate(() => window.fixture.reset());
      const cursor = { ...end };
      if (action === 'wheel') {
        // No mouse movement follows zoom: repaint must update the live command itself.
        await page.mouse.wheel(0, -120);
      } else if (action === 'pan') {
        await page.mouse.down({ button: 'middle' });
        cursor.x += 80; cursor.y += 35;
        await page.mouse.move(cursor.x, cursor.y, { steps: 4 });
        await page.mouse.up({ button: 'middle' });
      } else {
        cursor.x += 35; cursor.y += 20;
        await page.evaluate(({ start, end }) => {
          const svg = window.fixture.mount.querySelector('[data-tube-sketch-canvas]');
          svg.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: -120, clientX: start.x, clientY: start.y }));
          svg.dispatchEvent(new PointerEvent('pointermove', { bubbles: true, pointerId: window.__sketchPointerId,
            pointerType: 'mouse', clientX: end.x, clientY: end.y }));
        }, { start: end, end: cursor });
      }
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      const result = await page.evaluate(({ cursor, action }) => {
        const f = window.fixture, svg = f.mount.querySelector('[data-tube-sketch-canvas]');
        const circle = svg.querySelector('[data-tube-sketch-draft-preview] circle');
        const center = [...f.state.command.points[0]], { offsetU, zoom } = f.state.sideViewport;
        const toModel = client => f.toModel(client);
        const received = window.__sketchLastCanvasInput[action === 'wheel' ? 'wheel' : 'pointermove'];
        const end = toModel(received), commitEnd = toModel(cursor);
        const radius = Math.hypot(end[0] - center[0], end[1] - center[1]);
        const renderedCenter = f.toCanvas(center), m = f.canvasMetrics();
        return { actual: circle && { cx: Number(circle.getAttribute('cx')), cy: Number(circle.getAttribute('cy')), r: Number(circle.getAttribute('r')) },
          expected: { cx: renderedCenter[0], cy: renderedCenter[1], r: radius / (m.xMax - m.xMin) * (m.right - m.left) },
          center, radius, commitRadius: Math.hypot(commitEnd[0] - center[0], commitEnd[1] - center[1]), received,
          viewport: { offsetU, offsetS:f.state.sideViewport.offsetS, zoom }, work: f.stop() };
      }, { cursor, action });
      await page.screenshot({ path: resolve(output, `circle-command-after-${action}.png`) });
      await page.mouse.click(cursor.x, cursor.y);
      result.committed = await page.evaluate(() => ({ entity: window.fixture.draft.entities.at(-1), command: window.fixture.state.command }));
      circleNavigation.push({ action, ...result });
    }
    writeFileSync(resolve(output, 'circle-navigation.json'), JSON.stringify(circleNavigation, null, 2));
    writeFileSync(resolve(output, 'circle-navigation-assets.json'), JSON.stringify({ label,
      viewportOverlayDisabled: process.env.ICAX_PERFORMANCE_DISABLE_VIEWPORT_OVERLAY === '1', assets: Object.fromEntries(assets) }, null, 2));
    for (const result of circleNavigation) {
      assert.ok(result.actual, `The active circle preview must survive ${result.action}`);
      for (const key of ['cx', 'cy', 'r']) assert.ok(Math.abs(result.actual[key] - result.expected[key]) < 1e-5,
        `Circle preview ${key} must use current ${result.action} viewport: ${JSON.stringify(result)}`);
      assert.equal(result.work.renderProject, 0, 'Navigating during a circle command must keep the modal intact');
      assert.equal(result.work.svgRetained, true); assert.equal(result.work.canvasRetained, true);
      assert.equal(result.committed.command, null); assert.equal(result.committed.entity.kind, 'circle');
      assert.ok(Math.hypot(result.committed.entity.cx - result.center[0], result.committed.entity.cy - result.center[1]) < 1e-6);
      assert.ok(Math.abs(result.committed.entity.radius - result.commitRadius) < 1e-6,
        `Circle radius must remain exact after ${result.action}`);
    }
  }
  let burst = null, freehand = null;
  if (process.env.ICAX_PERFORMANCE_EXPECT_OPTIMIZED === '1') {
    await page.evaluate(() => { const f = window.fixture; f.setup(6, 1, false); f.draft.selectedIds = ['entity-0']; f.ops.renderProject(); });
    // A pipe-length fit shows small holes at their true scale. Zoom around the
    // selected center using the real UI before testing its individual grips.
    const beforeZoom = await page.locator('[data-tube-sketch-grip-entity-id="entity-0"][data-tube-sketch-grip-role="move"]').boundingBox();
    const zoomAnchor = {x:beforeZoom.x + beforeZoom.width / 2,y:beforeZoom.y + beforeZoom.height / 2};
    const metricsBeforeZoom = await page.evaluate(() => window.fixture.canvasMetrics());
    await page.mouse.move(zoomAnchor.x,zoomAnchor.y);
    await page.mouse.wheel(0, -1000);
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    assert.ok(await page.evaluate(() => window.fixture.state.sideViewport.zoom) > 4, 'Real wheel zoom separates center and radius grips at equal physical scale');
    const zoomModels = await page.evaluate(before => {
      const received = window.__sketchLastCanvasInput.wheel;
      return {before:window.fixture.toModel(received,before),after:window.fixture.toModel(received),received};
    },metricsBeforeZoom);
    assert.ok(Math.hypot(...zoomModels.after.map((v,i)=>v-zoomModels.before[i]))<1e-6,
      `Wheel zoom keeps both model coordinates under the actual received pointer fixed: ${JSON.stringify(zoomModels)}`);
    const grip = await page.locator('[data-tube-sketch-grip-entity-id="entity-0"][data-tube-sketch-grip-role="move"]').boundingBox();
    const start = { x: grip.x + grip.width / 2, y: grip.y + grip.height / 2 };
    await page.mouse.move(start.x, start.y); await page.mouse.down();
    await page.evaluate(() => window.fixture.reset());
    burst = await page.evaluate(async start => {
      const f = window.fixture, p = window.__sketchPerf, svg = f.mount.querySelector('[data-tube-sketch-canvas]');
      const original = { cx: f.draft.entities[0].cx, cy: f.draft.entities[0].cy };
      const dispatch = (type, x, y) => svg.dispatchEvent(new PointerEvent(type, { bubbles: true, cancelable: true,
        pointerId: window.__sketchPointerId, pointerType: 'mouse', isPrimary: true, button: 0, buttons: type === 'pointerup' ? 0 : 1,
        clientX: x, clientY: y }));
      for (let index = 1; index <= 25; index++) dispatch('pointermove', start.x + index * 2, start.y - index / 4);
      const beforeFrame = p.innerHTML.geometry?.calls || 0;
      await new Promise(resolve => requestAnimationFrame(resolve));
      const afterFrame = p.innerHTML.geometry?.calls || 0;
      for (let index = 26; index <= 40; index++) dispatch('pointermove', start.x + index * 2, start.y - index / 4);
      const end = { x: start.x + 80, y: start.y - 10 };
      dispatch('pointerup', end.x, end.y);
      await new Promise(resolve => requestAnimationFrame(resolve));
      const toLocal = point => { const p = svg.createSVGPoint(); p.x = point.x; p.y = point.y; return p.matrixTransform(svg.getScreenCTM().inverse()); };
      const a = f.toModel(start), b = f.toModel(end);
      const expected = { cx: original.cx + b[0] - a[0], cy: original.cy + b[1] - a[1] };
      return { beforeFrame, afterFrame, geometryWrites: p.innerHTML.geometry?.calls || 0,
        expected, actual: { cx: f.draft.entities[0].cx, cy: f.draft.entities[0].cy }, work: f.stop() };
    }, start);
    await page.mouse.up();
    assert.equal(burst.beforeFrame, 0); assert.equal(burst.afterFrame, 1);
    assert.equal(burst.geometryWrites, 2, 'Forty moves in two bursts paint only once per burst, including release flush');
    assert.ok(Math.abs(burst.actual.cx - burst.expected.cx) < 1e-5 && Math.abs(burst.actual.cy - burst.expected.cy) < 1e-5,
      `Pointer release must commit the latest queued coordinates: ${JSON.stringify({actual:burst.actual,expected:burst.expected})}`);
    await page.evaluate(() => { const f = window.fixture; f.setup(0, 1, false); f.state.tool = 'freehand'; });
    const freehandStart = await point(180, 330);
    await page.mouse.move(freehandStart.x, freehandStart.y); await page.mouse.down();
    freehand = await page.evaluate(async start => {
      const f = window.fixture, svg = f.mount.querySelector('[data-tube-sketch-canvas]'), samples = [];
      const pointer = point => new PointerEvent('pointermove', { bubbles: true, pointerId: window.__sketchPointerId,
        pointerType: 'mouse', buttons: 1, clientX: point.x, clientY: point.y });
      for (let index = 1; index <= 20; index++) samples.push({ x: start.x + index * 6, y: start.y + Math.sin(index) * 9 });
      for (let offset = 0; offset < 20; offset += 5) {
        const coalesced = samples.slice(offset, offset + 5).map(pointer), event = pointer(samples[offset + 4]);
        Object.defineProperty(event, 'getCoalescedEvents', { value: () => coalesced }); svg.dispatchEvent(event);
      }
      const end = samples.at(-1);
      svg.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: window.__sketchPointerId,
        pointerType: 'mouse', button: 0, buttons: 0, clientX: end.x, clientY: end.y }));
      await new Promise(resolve => requestAnimationFrame(resolve));
      return { kind: f.draft.entities[0]?.kind, points: f.draft.entities[0]?.points, expected: samples.map(point => {
        return f.toModel(point);
      }) };
    }, freehandStart);
    await page.mouse.up();
    assert.equal(freehand.kind, 'freehand');
    assert.equal(freehand.points.length, 21, 'Coalescing may combine paint work but must retain all twenty freehand samples');
    for (const expected of freehand.expected)
      assert.ok(freehand.points.some(actual => Math.hypot(actual[0] - expected[0], actual[1] - expected[1]) < 1e-5));
  }
  await page.evaluate(() => window.fixture.reset());
  await page.evaluate(async () => {
    const f = window.fixture;
    for (let index = 0; index < 10; index++) {
      await f.preview.attachSideSketchPreview(f.context, f.view, f.mount, f.ops);
    }
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
  });
  const previewReuse = await page.evaluate(() => window.fixture.stop());
  assert.equal(previewReuse.previewApply, 0, 'The same accepted preview must not reapply a snapshot');
  assert.equal(previewReuse.resourceReads, 0);
  await page.evaluate(() => window.fixture.reset());
  await page.waitForTimeout(1200);
  const idle = await page.evaluate(() => window.fixture.stop());
  assert.equal(idle.nativeCalls, 0, 'Idle must not request a new native preview');
  assert.equal(idle.resourceReads, 0, 'Idle must not reload preview geometry');
  assert.equal(idle.webglFrames, 0, 'Idle side preview must not continuously render');
  assert.deepEqual(errors, []);
  if (process.env.ICAX_PERFORMANCE_EXPECT_OPTIMIZED === '1') {
    for (const row of rows) {
      if (row.action === 'wheel') assert.equal(row.renderProject, 0, 'Wheel zoom must not regenerate the whole modal');
      if (row.action === 'draw-circle') {
        assert.equal(row.renderProject, 0, 'Radius rubber-band movement must keep the modal intact');
        assert.equal(row.innerHTML.geometry?.calls || 0, 0, 'Circle preview must not repaint existing sketch geometry');
        assert.ok(row.previewLatencyMs.count > 0);
      }
      assert.equal(row.nativeCalls, 0); assert.equal(row.resourceReads, 0);
    }
    assert.equal(previewReuse.previewResize, 0, 'The same accepted preview must not force a resize');
    assert.equal(previewReuse.webglFrames, 0, 'Repeated same-preview attachment must not trigger GPU frames');
  }
  const graphics = await page.evaluate(() => {
    const gl = window.fixture.viewport.renderer.getContext(), extension = gl.getExtension('WEBGL_debug_renderer_info');
    return { renderer: gl.getParameter(gl.RENDERER), vendor: gl.getParameter(gl.VENDOR),
      unmaskedRenderer: extension ? gl.getParameter(extension.UNMASKED_RENDERER_WEBGL) : null,
      unmaskedVendor: extension ? gl.getParameter(extension.UNMASKED_VENDOR_WEBGL) : null };
  });
  const report = { passed: true, label, rows, idle, previewReuse, periodic, circleNavigation, burst, freehand, graphics, headless: true, browserArgs,
    viewportOverlayDisabled: process.env.ICAX_PERFORMANCE_DISABLE_VIEWPORT_OVERLAY === '1',
    assets: Object.fromEntries(assets), browser: process.env.ICAX_BROWSER_CHANNEL || 'chrome',
    scope: 'Headless Chrome actual mouse/wheel events, production SVG and WebGL; browser flags and GL renderer recorded. Fixed native transport; not an embedded CEF timing measurement. Preview latency measures pointer receipt through SVG DOM update, not raster presentation. Geometry counters add test-only wrappers equally to both comparison runs.' };
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  await page.evaluate(() => window.fixture.viewport.dispose());
  console.log(JSON.stringify({ passed: true, output, idle: { elapsedMs: idle.elapsedMs, webglFrames: idle.webglFrames, axisFrames: idle.axisFrames, nativeCalls: idle.nativeCalls } }));
} catch (error) {
  if (page) await page.screenshot({ path: resolve(output, 'failure.png') }).catch(() => {});
  throw error;
} finally {
  await browser.close();
}
