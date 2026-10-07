// Focused production SVG acceptance. No native preview or geometry claims.
import assert from 'node:assert/strict';
import { existsSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetRoot, browserAssetPath, browserReportDirectory, importBrowserAsset } from './browserPackageRuntime.mjs';

const catalogueRoot = resolve(browserAssetRoot, 'apps/tube-designer/templates/mold');
const catalogue = readdirSync(catalogueRoot, { withFileTypes: true }).filter(entry => entry.isDirectory())
  .flatMap(entry => {
    const relative = `apps/tube-designer/templates/mold/${entry.name}/tool.json`;
    if (!existsSync(resolve(browserAssetRoot, relative))) return [];
    const descriptor = JSON.parse(readFileSync(browserAssetPath(relative), 'utf8'));
    return [{ ...descriptor, libraryScope: 'system', defaultParameters: Object.fromEntries((descriptor.parameters ?? []).map(field => [field.key, field.defaultValue])) }];
  });
assert.equal(catalogue.length, 24, 'Audit every current built-in tool, including tools without editable parameters');
const targets = ['contact-outline-mark', 'curve-pocket'];
assert(targets.every(id => catalogue.some(tool => tool.id === id)));
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const output = browserReportDirectory('tool-contour-illustrations-20261006');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
try {
  const page = await browser.newPage({ viewport: { width: 1480, height: 1000 } });
  const pageErrors = [];
  page.on('pageerror', error => pageErrors.push(error.message));
  await page.route('http://tool-contour.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"></head><body><main id="app" class="tube-designer-workspace"></main></body></html>' });
    if (!pathname.startsWith('/src/') || !/\.m?js$/.test(pathname)) return route.abort();
    const file = browserAssetPath(pathname.slice('/src/'.length));
    return route.fulfill({ contentType: 'text/javascript', body: readFileSync(file, 'utf8') });
  });
  await page.goto('http://tool-contour.test/');
  await page.addStyleTag({ content: `*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Microsoft YaHei,sans-serif;background:#eef3f4}#app{height:100vh}.cam-workbench{display:grid;grid-template-columns:390px minmax(0,1fr) 410px;height:100vh}.cam-context-pane,.cam-info-pane{display:block;overflow:auto;min-width:0;height:100vh}.cam-viewport{position:relative;min-width:0;background:#13252d}.cam-context-pane .tube-designer-panel,.cam-info-pane .tube-designer-panel{height:100%}.tube-tool-library-editor-body{overflow:visible!important}${tubeDesignerCss}` });
  await page.evaluate(async catalogue => {
    const library = await import('/src/apps/tube-designer/webpage/toolLibrary.mjs');
    const patch = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const diagrams = await import('/src/apps/tube-designer/webpage/toolParameterDiagram.mjs');
    const mount = document.querySelector('#app');
    const view = { activeAreaId: 'tools', pending: false, tubeDesignerSystemPunchTools: catalogue, tubeDesignerUserData: { punchTools: [], profiles: [] },
      tubeDesignerToolLibrary: { scope: 'system', type: 'all', category: 'all', search: '', catalogueStatus: 'ready', selectedKey: 'system::contact-outline-mark', showToolDiagram: false } };
    const context = { mount };
    const fixture = globalThis.__contour = { view, catalogue, actionCount: 0, actionErrors: [] };
    const render = () => {
      const left = library.renderToolLibraryLeftPane(context, view), right = library.renderToolLibraryRightPane(context, view), overlay = library.renderToolLibraryViewportOverlay(context, view);
      if (!mount.querySelector('.cam-workbench')) {
        mount.innerHTML = `<div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><section class="cam-viewport">${overlay}</section><aside class="cam-info-pane">${right}</aside></div>`;
        patch.rememberLibraryDom(view, mount, '', null);
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: '', sceneProxy: null })) throw new Error('SVG fixture repaint unexpectedly needs workbench replacement');
      diagrams.bindToolParameterDiagrams(mount);
    };
    mount.addEventListener('click', async event => {
      const target = event.target.closest('[data-cam-action]');
      if (!target || target.disabled) return;
      fixture.actionCount++;
      try { await library.handleToolLibraryAction(context, view, target.dataset.camAction, target, { renderProject: render }); }
      catch (error) { fixture.actionErrors.push(error.message); }
      finally { fixture.actionCount--; }
    });
    render();
  }, catalogue);
  const card = id => page.locator(`[data-cam-action="tube-designer-tool-library-select"][data-tube-tool-library-key="system::${id}"]`);
  const allCards = await page.locator('.tube-tool-library-card').evaluateAll(nodes => nodes.map(node => {
    const svg = node.querySelector('.tube-tool-library-card-art svg');
    const shapes = [...(svg?.querySelectorAll('path,circle,ellipse,rect,polygon,polyline,line') ?? [])];
    const paths = shapes.filter(shape => shape.localName === 'path').map(shape => shape.getAttribute('d')?.replace(/\s+/g, ' ').trim());
    return { key: node.dataset.tubeToolLibraryKey, title: node.querySelector('strong')?.textContent,
      hasSvg: !!svg, shapes: shapes.length, placeholderCross: shapes.length === 1 && paths[0] === 'M10 24 H38 M24 10 V38' };
  }));
  assert.equal(allCards.length, 24);
  assert(allCards.every(result => result.hasSvg && result.shapes > 0 && !result.placeholderCross), JSON.stringify(allCards));
  const selectedStyle = await card('contact-outline-mark').locator('svg').evaluate(svg => ({ stroke: getComputedStyle(svg).stroke, fill: getComputedStyle(svg).fill, lineWidth: getComputedStyle(svg).strokeWidth }));
  const neighbouringStyle = await card('circle').locator('svg').evaluate(svg => ({ stroke: getComputedStyle(svg).stroke, fill: getComputedStyle(svg).fill, lineWidth: getComputedStyle(svg).strokeWidth }));
  assert.deepEqual(selectedStyle, neighbouringStyle, 'New thumbnails inherit the existing catalogue icon style');
  const thumbnails = await Promise.all(targets.map(async id => {
    const svg = card(id).locator('svg');
    const result = await svg.evaluate(node => ({ id: node.closest('[data-tube-tool-library-key]').dataset.tubeToolLibraryKey,
      paths: [...node.querySelectorAll('path')].map(shape => shape.getAttribute('d')),
      circles: [...node.querySelectorAll('circle')].map(shape => ({ cx: shape.getAttribute('cx'), cy: shape.getAttribute('cy'), r: shape.getAttribute('r') })),
      box: { width: node.getBoundingClientRect().width, height: node.getBoundingClientRect().height } }));
    assert(result.box.width > 20 && result.box.height > 20);
    assert(result.paths.every(d => /Z\s*$/i.test(d)), `${id}: thumbnail uses a closed contour, not a plus marker`);
    return result;
  }));
  assert.equal(thumbnails[0].paths.length, 2, 'Contact mark depicts the two boundaries of a contour band');
  assert.equal(thumbnails[0].circles.length, 0);
  assert.equal(thumbnails[1].paths.length, 1, 'Curve pocket depicts a closed irregular cutting outline');
  assert.equal(thumbnails[1].circles.length, 1, 'Curve pocket shows its retained inner island');
  assert.notDeepEqual(thumbnails[0].paths, thumbnails[1].paths);
  await page.locator('.cam-context-pane').screenshot({ path: resolve(output, 'catalogue-concrete-contour-thumbnails.png') });

  const results = [];
  for (const id of targets) {
    await card(id).click();
    await page.waitForFunction(id => !__contour.actionCount && __contour.view.tubeDesignerToolLibrary.selectedKey === `system::${id}`, id);
    const show = page.locator('[data-cam-action="tube-designer-tool-library-toggle-diagram"][data-tube-tool-library-diagram="tool"]');
    assert.equal(await show.getAttribute('aria-expanded'), 'false');
    await show.click();
    await page.waitForFunction(() => !__contour.actionCount && __contour.view.tubeDesignerToolLibrary.showToolDiagram);
    const dock = page.locator('[data-tube-tool-diagram-dock]');
    assert.equal(await dock.isVisible(), true);
    assert.equal(await show.getAttribute('aria-expanded'), 'true');
    const svg = dock.locator('.tool-parameter-svg.is-rich');
    assert.equal(await svg.isVisible(), true);
    const diagram = await svg.evaluate(node => {
      const shape = node.querySelector('.tool-diagram-shape');
      const bounds = node.getBoundingClientRect();
      const overflow = [...node.querySelectorAll('text')].filter(text => {
        const rect = text.getBoundingClientRect();
        return rect.width && (rect.left < bounds.left - 1 || rect.top < bounds.top - 1 || rect.right > bounds.right + 1 || rect.bottom > bounds.bottom + 1);
      }).map(text => text.textContent);
      return { title: node.getAttribute('aria-label'), text: node.textContent, overflow,
        shapes: [...shape.children].map(child => ({ kind: child.localName, class: child.getAttribute('class'), d: child.getAttribute('d'),
          fill: getComputedStyle(child).fill, stroke: getComputedStyle(child).stroke,
          bounds: { x: child.getBBox().x, y: child.getBBox().y, width: child.getBBox().width, height: child.getBBox().height } })),
        annotations: [...node.querySelectorAll('[data-tool-annotation-key]')].map(annotation => annotation.dataset.toolAnnotationKey) };
    });
    assert.match(diagram.title, new RegExp(catalogue.find(tool => tool.id === id).displayName));
    assert.deepEqual(diagram.overflow, []);
    assert(diagram.shapes.every(shape => shape.bounds.width > 0 && shape.bounds.height > 0));
    if (id === 'contact-outline-mark') {
      assert.equal(diagram.shapes.length, 2);
      assert.deepEqual(diagram.annotations, ['markMode', 'lineWidth']);
      assert.match(diagram.text, /实际外轮廓打标/);
      assert.match(diagram.text, /标记带宽/);
    } else {
      const cut = diagram.shapes.find(shape => shape.class === 'tool-diagram-cut');
      const island = diagram.shapes.find(shape => shape.kind === 'circle' && shape.class === 'tool-diagram-material');
      const wall = diagram.shapes.find(shape => shape.kind === 'path' && shape.class === 'tool-diagram-material');
      assert(cut && island && wall, 'Curve diagram distinctly renders target wall, cutting region and retained island');
      assert.match(cut.d, /C/); assert.match(cut.d, /Z\s*$/i);
      assert.notEqual(cut.fill, island.fill); assert.notEqual(cut.stroke, island.stroke);
      assert(island.bounds.x > cut.bounds.x && island.bounds.y > cut.bounds.y && island.bounds.x + island.bounds.width < cut.bounds.x + cut.bounds.width && island.bounds.y + island.bounds.height < cut.bounds.y + cut.bounds.height);
      assert.match(diagram.text, /目标管壁/); assert.match(diagram.text, /切除区域/); assert.match(diagram.text, /保留岛/);
      assert.equal(await page.locator('[data-tube-tool-library-operation-parameter="cutDepth"]').isEnabled(), true, 'Curve cutting depth remains editable');
      assert.doesNotMatch(await dock.innerText(), /此模具没有可编辑参数/);
      assert.match(await dock.innerText(), /图中未标注尺寸参数/);
    }
    results.push({ id, actualShowDiagramButtonClick: true, diagram });
    await dock.screenshot({ path: resolve(output, `${id}-illustration.png`) });
    await page.screenshot({ path: resolve(output, `${id}-library.png`) });
    await dock.locator('[data-cam-action="tube-designer-tool-library-close-diagram"]').click();
    await page.waitForFunction(() => !__contour.actionCount && !__contour.view.tubeDesignerToolLibrary.showToolDiagram);
    assert.equal(await dock.isVisible(), false);
  }
  const actionErrors = await page.evaluate(() => __contour.actionErrors);
  assert.deepEqual(pageErrors, []); assert.deepEqual(actionErrors, []);
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, auditedBuiltIns: allCards, thumbnails, inheritedCatalogueStyle: selectedStyle,
    diagrams: results, pageErrors, actionErrors, nativeTransport: 'not invoked; descriptor-only SVG rendering', geometry: 'production SVG shapes; no BRep claim', standaloneCefEndToEnd: false }, null, 2));
  console.log('PASS contour illustrations: all 24 concrete catalogue SVGs, distinct mark/pocket thumbnails, real diagram buttons, closed cutting region/retained island and shared styling.');
} finally { await browser.close(); }
