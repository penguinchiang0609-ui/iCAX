export function createInitialWorkbenchState() {
  return {
    bridgeStatus: "Disconnected",
    applicationChannelId: "",
    application: {
      state: "Stopped",
      phase: "None",
      products: [],
      runningProductCount: 0,
      faultMessage: "",
    },
    activeProductId: "",
    activeProjectId: "",
    productsById: new Map(),
    projectsById: new Map(),
    log: [],
    pendingCommandCount: 0,
    errorMessage: "",
  };
}

export class WorkbenchStore {
  constructor(initialState = createInitialWorkbenchState()) {
    this.state = initialState;
    this.listeners = new Set();
  }

  getState() {
    return this.state;
  }

  subscribe(listener) {
    this.listeners.add(listener);
    listener(this.state);
    return () => this.listeners.delete(listener);
  }

  patch(mutator) {
    mutator(this.state);
    this.#emit();
  }

  setBridgeStatus(status) {
    this.patch((state) => {
      state.bridgeStatus = status;
    });
  }

  beginCommand(label) {
    this.patch((state) => {
      state.pendingCommandCount += 1;
      state.log.unshift({ level: "info", message: `Command sent: ${label}`, time: new Date().toLocaleTimeString() });
      state.log = state.log.slice(0, 80);
    });
  }

  endCommand(label) {
    this.patch((state) => {
      state.pendingCommandCount = Math.max(0, state.pendingCommandCount - 1);
      state.log.unshift({ level: "ok", message: `Command completed: ${label}`, time: new Date().toLocaleTimeString() });
      state.log = state.log.slice(0, 80);
    });
  }

  failCommand(label, error) {
    this.patch((state) => {
      state.pendingCommandCount = Math.max(0, state.pendingCommandCount - 1);
      state.errorMessage = error?.message ?? String(error);
      state.log.unshift({ level: "error", message: `${label}: ${state.errorMessage}`, time: new Date().toLocaleTimeString() });
      state.log = state.log.slice(0, 80);
    });
  }

  applyApplicationState(applicationState) {
    this.patch((state) => {
      state.applicationChannelId = applicationState.applicationChannelId ?? state.applicationChannelId;
      state.application = {
        state: applicationState.state ?? state.application.state,
        phase: applicationState.phase ?? state.application.phase,
        products: applicationState.products ?? [],
        runningProductCount: applicationState.runningProductCount ?? 0,
        faultMessage: applicationState.faultMessage ?? "",
      };
      state.productsById.clear();
      for (const product of state.application.products) {
        state.productsById.set(product.productId, product);
      }
      if (!state.activeProductId) {
        const runningProduct = state.application.products.find((product) => product.isStarted);
        state.activeProductId = runningProduct?.productId ?? "";
      }
      this.#rebuildProjects(state);
    });
  }

  applyStartProductResponse(response) {
    this.applyApplicationState(response.state);
    this.patch((state) => {
      if (response.product?.productId) {
        state.activeProductId = response.product.productId;
        state.productsById.set(response.product.productId, response.product);
      }
      this.#rebuildProjects(state);
    });
  }

  applyOpenProjectFileResponse(response) {
    this.applyApplicationState(response.state);
    this.patch((state) => {
      const productId = response.product?.productId ?? "";
      if (productId) {
        state.activeProductId = productId;
        state.productsById.set(productId, response.product);
      }
      const projectId = response.catalog?.mainProject?.projectId ?? "";
      if (projectId) {
        state.activeProjectId = projectId;
      }
      this.#rebuildProjects(state);
    });
  }

  applyProductState(productId, productState) {
    this.patch((state) => {
      const current = state.productsById.get(productId) ?? {};
      state.productsById.set(productId, { ...current, ...productState });
      this.#rebuildProjects(state);
    });
  }

  #rebuildProjects(state) {
    state.projectsById.clear();
    for (const product of state.productsById.values()) {
      const catalogs = product.runtime?.catalogs ?? product.catalogs ?? [];
      for (const catalog of catalogs) {
        const project = catalog.mainProject;
        if (!project?.projectId) {
          continue;
        }
        state.projectsById.set(project.projectId, {
          ...project,
          productId: product.productId,
          catalogId: catalog.catalogId,
          catalogName: catalog.catalogName,
        });
      }
    }
  }

  #emit() {
    for (const listener of this.listeners) {
      listener(this.state);
    }
  }
}
