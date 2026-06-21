const supportedAbsoluteProtocols = /^(?:https?:|data:|blob:)/i;

import { ProductClient, ProjectClient } from "../mailbox/clients.mjs";

export class ProductModuleHost {
  constructor(options = {}) {
    this.bridge = options.bridge ?? null;
    this.mailboxClient = options.mailboxClient ?? null;
    this.moduleCache = new Map();
  }

  async render(state, root) {
    const mount = root?.querySelector?.("[data-product-extension]");
    if (!mount) {
      return;
    }

    const product = state.productsById.get(state.activeProductId);
    if (!product?.frontendEntry) {
      mount.innerHTML = "";
      return;
    }

    const project = state.projectsById.get(state.activeProjectId) ?? null;
    const mode = project ? "project" : "product";
    const renderKey = `${product.productId}:${product.frontendEntry}:${mode}:${project?.projectId ?? ""}`;

    if (mount.dataset.productExtensionKey === renderKey) {
      return;
    }

    mount.dataset.productExtensionKey = renderKey;
    mount.innerHTML = `<div class="product-extension-loading">Loading product workspace...</div>`;

    try {
      const productModule = await this.#loadModule(product.frontendEntry);
      if (mount.dataset.productExtensionKey !== renderKey) {
        return;
      }

      mount.innerHTML = "";
      const context = {
        bridge: this.bridge,
        mailboxClient: this.mailboxClient,
        productClient: createProductClient(this.mailboxClient, product),
        projectClient: createProjectClient(this.mailboxClient, project),
        mount,
        mode,
        product,
        project,
        state,
      };

      const workspace = this.#createWorkspace(productModule, mode, context);
      await this.#mountWorkspace(workspace, mount);
    } catch (error) {
      mount.innerHTML = `<div class="product-extension-error">${escapeText(error?.message ?? error)}</div>`;
    }
  }

  async #loadModule(frontendEntry) {
    const url = resolveFrontendEntry(frontendEntry);
    if (!this.moduleCache.has(url)) {
      this.moduleCache.set(url, import(url));
    }
    return this.moduleCache.get(url);
  }

  #createWorkspace(productModule, mode, context) {
    const factory =
      mode === "project"
        ? productModule.createProjectWorkspace
        : productModule.createProductWorkspace;

    if (typeof factory === "function") {
      return factory(context);
    }
    throw new Error(`Product frontend module does not export ${mode === "project" ? "createProjectWorkspace" : "createProductWorkspace"}().`);
  }

  async #mountWorkspace(workspace, mount) {
    if (workspace?.render) {
      await workspace.render(mount);
      return;
    }
    if (workspace?.start) {
      await workspace.start();
      return;
    }
    if (typeof Node !== "undefined" && workspace instanceof Node) {
      mount.appendChild(workspace);
      return;
    }
    throw new Error("Product workspace does not provide render() or start().");
  }
}

function createProductClient(mailboxClient, product) {
  if (!mailboxClient || !product?.productMailId) {
    return null;
  }
  return new ProductClient(mailboxClient, product.productMailId);
}

function createProjectClient(mailboxClient, project) {
  if (!mailboxClient || !project?.projectMailId) {
    return null;
  }
  return new ProjectClient(mailboxClient, project.projectMailId);
}

export function resolveFrontendEntry(frontendEntry) {
  const entry = String(frontendEntry ?? "").trim();
  if (!entry) {
    throw new Error("Product frontend entry is empty.");
  }
  if (supportedAbsoluteProtocols.test(entry) || entry.startsWith("/")) {
    return entry;
  }
  return `/${entry.replace(/^src\//, "")}`;
}

function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}
