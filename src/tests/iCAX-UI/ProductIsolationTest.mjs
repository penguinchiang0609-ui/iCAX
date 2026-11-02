import assert from 'node:assert/strict';
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import { dirname, resolve, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createWorkbench } from '../../apps/_shared/workbench/createWorkbench.mjs';

const apps = fileURLToPath(new URL('../../apps/', import.meta.url));
function closure(path, visited = new Set()) {
  if (visited.has(path)) return visited;
  assert.ok(existsSync(path), `Missing import: ${path}`);
  visited.add(path);
  const source = readFileSync(path, 'utf8');
  for (const match of source.matchAll(/(?:from\s+|import\s*\()\s*["'](\.[^"']+)["']/g)) {
    const dependency = resolve(dirname(path), match[1]);
    if (dependency.endsWith('.mjs')) closure(dependency, visited);
  }
  return visited;
}

const products = readdirSync(apps).filter(product => existsSync(resolve(apps, product, 'product.manifest.json')));
assert.deepEqual(products, ['five-axis-cam', 'tube-designer']);
for (const retired of ['tube-one', 'laser-3d-cam']) {
  assert.ok(!existsSync(resolve(apps, retired)), `Retired product directory exists: ${retired}`);
}
for (const product of products) {
  const files = closure(resolve(apps, product, 'webpage/entry.mjs'));
  for (const file of files) {
    const path = relative(apps, file).replaceAll('\\', '/');
    assert.ok(path.startsWith('../') || path.startsWith('_shared/') || path.startsWith(product + '/'),
      `${product} imports another product: ${path}`);
    if (product === 'tube-designer') {
      assert.ok(!/_shared\/workbench\/(machine|machining|workpiece|toolpath|ribbon|view)\//.test(path),
        `TubeDesigner eagerly imports CAM feature: ${path}`);
    }
  }
  const manifest = JSON.parse(readFileSync(resolve(apps, product, 'product.manifest.json'), 'utf8'));
  assert.equal(manifest.capabilities.tubeDesigner?.downstreamProductId, undefined);
  const modules = Object.values(manifest.backend.modules).flat();
  if (product === 'tube-designer') {
    assert.equal(manifest.backend.startupComponent, 'CSceneBootstrapComponent');
    assert.ok(!modules.some(path => path.endsWith('/CamRuntime.dll')));
    for (const kind of ['components', 'behaviours']) {
      assert.ok(manifest.backend.modules[kind].some(path => path.endsWith('/SceneBootstrap.dll')));
    }
  } else {
    assert.equal(manifest.backend.startupComponent, 'CCamSceneBootstrapComponent');
    assert.ok(modules.some(path => path.endsWith('/CamRuntime.dll')));
  }
  console.log(`PASS ${product}: independent frontend graph and backend manifest`);
}

// The neutral shell must not invoke machine/workpiece handlers implicitly.
const plain = createWorkbench();
await plain.handleRibbonCommand({ project: { projectId: 'isolation-neutral' },
  handleAreaRibbonCommand: async () => true }, 'product-command');
let calls = 0;
const extended = createWorkbench({ handleMachineRibbonCommand: async () => { calls++; return true; } });
await extended.handleRibbonCommand({ project: { projectId: 'isolation-cam' } }, 'machine-command');
assert.equal(calls, 1);
console.log('PASS workbench capability injection');
