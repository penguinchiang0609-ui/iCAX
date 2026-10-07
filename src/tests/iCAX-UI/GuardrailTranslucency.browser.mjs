// Render the exact FlatBuffers exported by actual native product previews,
// including saved/reopened scene render components. No substitute geometry.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, sep } from 'node:path';
import { browserReportDirectory, serveBrowserAsset } from './browserPackageRuntime.mjs';

const fixtures = resolve(process.env.ICAX_GUARDRAIL_FIXTURES_ROOT ||
  'output/tests/guardrail-visual-20261005/native-source');
const output = browserReportDirectory('guardrail-translucency');
const nativeReport = JSON.parse(readFileSync(resolve(fixtures, 'report.json'), 'utf8'));
assert.equal(nativeReport.status, 'passed');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
const results = [], errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 700 } });
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://guardrail-translucency.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body:
      '<!doctype html><meta charset="utf-8"><style>body{margin:0}#viewport{width:1200px;height:700px}</style><div id="viewport"></div>' });
    if (pathname.startsWith('/native/')) {
      const file = resolve(fixtures, pathname.slice('/native/'.length));
      assert.ok(file.startsWith(fixtures + sep) && file.endsWith('.bin'));
      return route.fulfill({ contentType: 'application/vnd.icax.flatbuffer', body: readFileSync(file) });
    }
    return serveBrowserAsset(route);
  });
  await page.goto('http://guardrail-translucency.test/');
  for (const { phase } of nativeReport.phases) {
    const fixture = JSON.parse(readFileSync(resolve(fixtures, phase, 'fixture.json'), 'utf8'));
    const result = await page.evaluate(async ({ fixture, phase }) => {
      const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
      const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
      const host = document.querySelector('#viewport');
      window.boardViewport?.dispose();
      const viewport = window.boardViewport = createThreeViewport({ continuousRender: false, showGrid: false });
      viewport.mount(host);
      const resourceClient = { get(url, init) {
        const resource = fixture.resources[url];
        if (!resource) throw new Error('Requested render resource was not exported by native: ' + url);
        if (String(resource.version) !== init.headers.get('ICAX-Resource-Version')) throw new Error('Wrong native resource version');
        return fetch(`/native/${phase}/${resource.file}`);
      } };
      await viewport.applyViewSnapshot({ rows: fixture.rows }, resourceClient);
      viewport.setStandardView('front'); viewport.fitViewToViewport();
      const boards = fixture.rows.filter(row => row.name.includes('挡板'));
      if (boards.length !== 3) throw new Error('Actual native scene must contain three boards');
      const flags = row => {
        const values = [];
        viewport.sceneObjects.get(row.entityId)?.traverse(node => {
          if (node.isMesh) values.push({ transparent: node.material.transparent,
            opacity: node.material.opacity, depthWrite: node.material.depthWrite });
        });
        if (!values.length) throw new Error('Native geometry was not rendered as a mesh');
        return values;
      };
      const check = () => {
        for (const row of fixture.rows) {
          const panel = boards.includes(row);
          const rgba = viewport.materialPayloads.get(row.data.material.url)?.colorRGBA;
          if ((rgba & 255) !== (panel ? 128 : 255)) throw new Error('Native material alpha is wrong');
          for (const material of flags(row)) {
            if (material.transparent !== panel || Math.abs(material.opacity-(panel ? 128/255 : 1)) > 1e-8
              || material.depthWrite === panel) throw new Error('Rendered opacity/depth state is wrong');
          }
          if (phase !== 'library' && panel && (row.properties['manufacturing.partKind'] !== 'plate'
            || row.properties['manufacturing.materialCategory'] !== 'plate')) throw new Error('Display changed manufacturing identity');
        }
      };
      check();
      viewport.setSelectedObjectId(boards[0].entityId);
      viewport.setSelectedObjectId(null);
      viewport.setEmphasizedObjectIds([boards[1].entityId]);
      viewport.setEmphasizedObjectIds([]); check();
      const canvas = host.querySelector('canvas');
      await viewport.applyViewSnapshot({ rows: fixture.rows }, resourceClient); check();
      if (host.querySelector('canvas') !== canvas) throw new Error('Refreshing display replaced the viewport canvas');
      // Pixel proof: the board surface responds to the background behind it.
      // An opaque negative control at the same point must not respond.
      const object = viewport.sceneObjects.get(boards[0].entityId);
      const point = new THREE.Box3().setFromObject(object).getCenter(new THREE.Vector3()).project(viewport.camera);
      const x = Math.round((point.x+1)/2*canvas.width), y = Math.round((1-point.y)/2*canvas.height);
      const sample = color => {
        viewport.setBackgroundColor(color); viewport.renderer.render(viewport.scene,viewport.camera);
        const copy=document.createElement('canvas'); copy.width=canvas.width;copy.height=canvas.height;
        const context=copy.getContext('2d');context.drawImage(canvas,0,0);
        return [...context.getImageData(x,y,1,1).data].slice(0,3);
      };
      const difference = (a,b) => Math.max(...a.map((value,index)=>Math.abs(value-b[index])));
      const transparentDelta=difference(sample('#000000'),sample('#ffffff'));
      object.traverse(node => { if(node.isMesh){node.material.transparent=false;node.material.opacity=1;node.material.depthWrite=true;} });
      const opaqueDelta=difference(sample('#000000'),sample('#ffffff'));
      object.traverse(node => { if(node.isMesh){node.material.transparent=true;node.material.opacity=128/255;node.material.depthWrite=false;} });
      viewport.setBackgroundColor('#182128');check();
      if (transparentDelta < 20 || opaqueDelta > 3) throw new Error('Framebuffer does not show real translucency: '+JSON.stringify({transparentDelta,opaqueDelta}));
      return { phase, boards: boards.length, opacity:128/255, transparentDelta, opaqueDelta,
        nativeFlatBuffers: true, actualWebGL: true, refreshAndSelectionRetainOpacity:true, canvasRetained:true };
    }, { fixture, phase });
    const screenshot=resolve(output, `${phase}.png`);
    await page.locator('#viewport').screenshot({ path:screenshot });
    results.push({...result,screenshot});
  }
  assert.deepEqual(errors,[]);
  console.log(`PASS ${results.length} native render phases, board transparency pixels, opaque frames, refresh and selection`);
} finally {
  await browser.close();
  writeFileSync(resolve(output,'report.json'),JSON.stringify({status:results.length===4&&!errors.length?'passed':'failed',
    actualNativeResources:true,phases:results,errors,nativeModules:nativeReport.modules},null,2));
}
