// Run the real AppShell first paint, routing, retry and start center. Only the
// native transport and project contents are controlled by this fixture.
import assert from 'node:assert/strict';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = resolve(root, 'output/tests/app-shell-startup-products');
mkdirSync(output, { recursive: true });
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge', headless: true });
const cases = [], errors = [];
const tube = { productId: 'icax.tube-designer', productName: 'TubeDesigner', frontendEntry: '/fixture/tube-designer.mjs' };
const cam = { productId: 'icax.five-axis-cam', productName: '三维五轴 CAM', frontendEntry: '/fixture/five-axis-cam.mjs' };
const other = { productId: 'test.other', productName: '独立产品 <A>', frontendEntry: '/fixture/other.mjs' };
async function scenario(products, query = '') {
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://startup-products.test/**', route => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/src/iCAX-UI/SDK/runtime.mjs') return route.fulfill({ contentType: 'text/javascript', body: 'export async function connectApplication(){await __fixture.connectGate;return __fixture.app;}' });
    if (path.startsWith('/fixture/')) {
      const directory = path.split('/').at(-1).replace('.mjs', '');
      return route.fulfill({ contentType: 'text/javascript', body:
        (directory === 'other' ? '' : `export {getStartupPresentation} from '/src/apps/${directory}/webpage/startup.mjs';`) +
        `export async function mountProject({mount}) {await __fixture.mountGate; mount.innerHTML='<div data-test-project>项目内容</div>';}` });
    }
    const file = resolve(root, '.' + path);
    if (!file.startsWith(resolve(root, 'src') + sep)) return route.abort();
    try {
      let body = readFileSync(file, 'utf8');
      if (path.endsWith('/index.html')) body = body.replace(/<script type="module"[^>]*><\/script>/, '');
      return route.fulfill({ contentType: path.endsWith('.html') ? 'text/html' : path.endsWith('.css') ? 'text/css' : 'text/javascript', body });
    } catch { return route.abort(); }
  });
  await page.goto('http://startup-products.test/src/iCAX-UI/SDK/AppShell/index.html' + query);
  assert.equal(await page.title(), '工作台');
  assert.equal(await page.locator('[data-startup-screen]').isVisible(), true);
  assert.equal(await page.locator('body').innerText().then(text => text.includes('TubeDesigner')), false);
  await page.evaluate(async products => {
    let releaseConnect, releaseMount;
    const fixture = globalThis.__fixture = { products, starts: [], opens: [],
      connectGate: new Promise(resolve => { releaseConnect = resolve; }),
      mountGate: new Promise(resolve => { releaseMount = resolve; }),
      releaseConnect: () => releaseConnect(), releaseMount: () => releaseMount() };
    const proxies = new Map();
    for (const product of products) {
      const scene = { sceneId: product.productId + '-scene' };
      const sceneProxy = { state: scene, async getState() { return scene; } };
      const project = { projectId: product.productId + '-project', projectName: '未命名项目', projectPath: '', mainScene: scene };
      const projectProxy = { projectId: project.projectId, state: project, getMainScene() { return sceneProxy; } };
      const proxy = { productId: product.productId, state: product, projects: new Map(),
        async getState() { return product; }, getProject(id) { return this.projects.get(id); },
        async openProjectCatalog(path) { fixture.opens.push({ productId: product.productId, path }); this.projects.set(project.projectId, projectProxy); return { projectProxy, sceneProxy, catalog: { mainProject: project } }; } };
      proxies.set(product.productId, proxy);
    }
    fixture.app = { products: new Map(), bridge: {},
      async getState() { return { products: fixture.products }; },
      getProduct(id) { return this.products.get(id); },
      async startProduct(id) { fixture.starts.push(id); const proxy = proxies.get(id); this.products.set(id, proxy); return proxy; } };
    await import('/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs');
  }, products);
  await page.evaluate(() => __fixture.releaseConnect());
  return page;
}
try {
  for (const product of [tube, cam, other]) {
    // A stale URL cannot force TubeDesigner when the configured product is CAM.
    const page = await scenario([product], '?product=icax.tube-designer');
    await page.waitForFunction(() => __fixture.opens.length === 1);
    assert.equal(await page.title(), product.productName);
    assert.equal(await page.locator('[data-startup-screen] .td-startup__brand').textContent(), product.productName);
    assert.equal(await page.locator('.start-products,.start-center').count(), 0);
    if (product === cam) {
      assert.equal(await page.locator('[data-startup-screen] .td-startup__tube').count(), 0);
      assert.equal(await page.locator('[data-startup-screen] .td-startup__eyebrow').innerText(), '三维设计与五轴加工');
      await page.screenshot({ path: resolve(output, 'cam-startup.png') });
    }
    if (product === tube) assert.equal(await page.locator('.td-startup__tube').count(), 4);
    await page.evaluate(() => __fixture.releaseMount());
    await page.waitForFunction(() => __icaxAppShell.getState().startupPhase === 'ready');
    assert.deepEqual(await page.evaluate(() => __fixture.opens), [{ productId: product.productId, path: '' }]);
    assert.equal(await page.locator('[data-test-project]').isVisible(), true);
    assert.equal(await page.locator('[data-bottom-dock-tab]').count(), 0, 'products without a dock definition keep the original log-only footer');
    assert.equal(await page.locator('.bottom-title').innerText(), '日志');
    assert.equal(await page.locator('.log-list').count(), 1);assert.equal(await page.locator('.log-list').isVisible(), true);
    await page.waitForFunction(() => !document.querySelector('[data-startup-screen]'));
    await page.locator('[data-action="open-start-center"]').click();
    assert.equal(await page.locator('.start-products').count(), 0, 'single product stays single in the project menu');
    assert.equal(await page.locator('.start-head strong').innerText(), product.productName);
    cases.push('single-product-' + product.productId);
    await page.close();
  }
  for (const products of [[tube, cam], [cam, tube]]) {
    const page = await scenario(products, '?product=icax.five-axis-cam');
    await page.waitForFunction(() => __icaxAppShell.getState().startupPhase === 'ready');
    await page.waitForFunction(() => !document.querySelector('[data-startup-screen]'));
    assert.equal(await page.title(), '工作台');
    assert.equal(await page.locator('.start-product').count(), 2);
    assert.deepEqual(await page.evaluate(() => [__fixture.starts, __fixture.opens]), [[], []]);
    for (const product of products) {
      await page.locator(`[data-action="select-product"][data-product-id="${product.productId}"]`).click();
      await page.waitForFunction(id => __icaxAppShell.getState().selectedProductId === id, product.productId);
      assert.equal(await page.locator('.start-product').count(), 2, 'selecting TubeDesigner must not hide the other product');
      assert.equal(await page.title(), '工作台');
    }
    await page.screenshot({ path: resolve(output, 'multiple-products-' + products[0].productId + '.png') });
    await page.evaluate(() => __fixture.releaseMount());
    await page.locator('[data-action="open-new-project-dialog"]').click();
    assert.equal(await page.locator('[data-field="newProjectName"]').inputValue(), products[1].productName + ' 项目');
    cases.push('multi-product-order-' + products[0].productId);
    await page.close();
  }
  const empty = await scenario([]);
  await empty.waitForFunction(() => __icaxAppShell.getState().startupPhase === 'error');
  assert.match(await empty.locator('.td-startup__error').innerText(), /没有配置可用产品/);
  assert.deepEqual(await empty.evaluate(() => __fixture.opens), []);
  cases.push('empty-configuration-visible-error');
  await empty.close();
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, cases, errors }, null, 2));
  console.log(JSON.stringify({ passed: true, cases }));
} finally { await browser.close(); }
