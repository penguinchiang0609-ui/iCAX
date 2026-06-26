import assert from 'node:assert/strict';
import { ProductModuleHost, resolveFrontendEntry } from '../../icax-workbench/product/productModuleHost.mjs';

function createMount() {
  return {
    dataset: {},
    innerHTML: '',
    renderedProductId: '',
    renderedProjectId: '',
  };
}

async function testResolveFrontendEntry() {
  assert.equal(resolveFrontendEntry('apps/demo/frontend/entry.mjs'), '/apps/demo/frontend/entry.mjs');
  assert.equal(resolveFrontendEntry('/apps/demo/frontend/entry.mjs'), '/apps/demo/frontend/entry.mjs');
  assert.equal(resolveFrontendEntry('src/apps/demo/frontend/entry.mjs'), '/apps/demo/frontend/entry.mjs');
}

async function testProductModuleMountsFromFrontendEntry() {
  const source = `
    export function createProductWorkspace(context) {
      return {
        render(mount) {
          mount.renderedProductId = context.product.productId;
          mount.innerHTML = 'mounted:' + context.mode;
        }
      };
    }
  `;
  const frontendEntry = `data:text/javascript,${encodeURIComponent(source)}`;
  const mount = createMount();
  const root = {
    querySelector(selector) {
      return selector === '[data-product-extension]' ? mount : null;
    },
  };
  const state = {
    activeProductId: 'demo.product',
    activeProjectId: '',
    productsById: new Map([
      ['demo.product', { productId: 'demo.product', frontendEntry }],
    ]),
    projectsById: new Map(),
  };

  const host = new ProductModuleHost();
  await host.render(state, root);

  assert.equal(mount.renderedProductId, 'demo.product');
  assert.equal(mount.innerHTML, 'mounted:product');
}

async function testProjectModuleMountsProjectWorkspace() {
  const source = `
    export function createProductWorkspace() {
      return {
        render(mount) {
          mount.innerHTML = 'product';
        }
      };
    }

    export function createProjectWorkspace(context) {
      return {
        render(mount) {
          mount.renderedProductId = context.product.productId;
          mount.renderedProjectId = context.project.projectId;
          mount.innerHTML = 'mounted:' + context.mode;
        }
      };
    }
  `;
  const frontendEntry = `data:text/javascript,${encodeURIComponent(source)}`;
  const mount = createMount();
  const root = {
    querySelector(selector) {
      return selector === '[data-product-extension]' ? mount : null;
    },
  };
  const state = {
    activeProductId: 'demo.product',
    activeProjectId: 'project-1',
    productsById: new Map([
      ['demo.product', { productId: 'demo.product', frontendEntry }],
    ]),
    projectsById: new Map([
      ['project-1', { projectId: 'project-1', projectChannelId: 'project-mail' }],
    ]),
  };

  const host = new ProductModuleHost();
  await host.render(state, root);

  assert.equal(mount.renderedProductId, 'demo.product');
  assert.equal(mount.renderedProjectId, 'project-1');
  assert.equal(mount.innerHTML, 'mounted:project');
}

async function testDefaultExportIsRejected() {
  const source = `
    export default function createWorkspace() {
      return {
        render(mount) {
          mount.innerHTML = 'default';
        }
      };
    }
  `;
  const frontendEntry = `data:text/javascript,${encodeURIComponent(source)}`;
  const mount = createMount();
  const root = {
    querySelector(selector) {
      return selector === '[data-product-extension]' ? mount : null;
    },
  };
  const state = {
    activeProductId: 'demo.product',
    activeProjectId: '',
    productsById: new Map([
      ['demo.product', { productId: 'demo.product', frontendEntry }],
    ]),
    projectsById: new Map(),
  };

  const host = new ProductModuleHost();
  await host.render(state, root);

  assert.match(mount.innerHTML, /does not export createProductWorkspace/);
  assert.notEqual(mount.innerHTML, 'default');
}

await testResolveFrontendEntry();
await testProductModuleMountsFromFrontendEntry();
await testProjectModuleMountsProjectWorkspace();
await testDefaultExportIsRejected();

console.log('icax-workbench productModuleHost tests passed');
