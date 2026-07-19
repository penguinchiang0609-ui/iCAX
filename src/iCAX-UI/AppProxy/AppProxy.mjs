import { AppFacade } from "../SDK/Facades/facadeMethod.mjs";
import { isUsableChannelId } from "../SDK/Facades/channelId.mjs";
import { FacadeClient } from "../SDK/Facades/facadeClient.mjs";
import { ProductProxy } from "../ProductProxy/ProductProxy.mjs";

export class AppProxy {
  static async create(bridge, options = {}) {
    if (!bridge) {
      throw new TypeError("bridge is required");
    }

    const facadeClient = options.facadeClient ?? new FacadeClient(bridge, options.facades ?? {});
    const applicationChannelId = options.applicationChannelId ?? await bridge.getApplicationChannelId();
    return new AppProxy(facadeClient, applicationChannelId, { bridge });
  }

  constructor(facadeClient, applicationChannelId, options = {}) {
    if (!facadeClient) {
      throw new TypeError("facadeClient is required");
    }
    if (!applicationChannelId) {
      throw new TypeError("applicationChannelId is required");
    }

    this.facadeClient = facadeClient;
    this.bridge = options.bridge ?? facadeClient.bridge ?? null;
    this.applicationChannelId = applicationChannelId;
    this.state = null;
    this.products = new Map();
    this.unsubscribers = new Set();
  }

  async getState() {
    const state = await this.facadeClient.invoke(this.applicationChannelId, AppFacade.getState);
    await this.#syncProductsFromState(state);
    return state;
  }

  async listProducts() {
    const state = await this.facadeClient.invoke(this.applicationChannelId, AppFacade.listProducts);
    await this.#syncProductsFromState(state);
    return state;
  }

  async startProduct(productId = "") {
    const response = await this.facadeClient.invoke(
      this.applicationChannelId,
      AppFacade.startProduct,
      productId ? { productId } : {},
    );
    await this.#syncProductsFromState(response.state);
    return this.adoptProduct(response.product);
  }

  async stopProduct(productId) {
    const state = await this.facadeClient.invoke(this.applicationChannelId, AppFacade.stopProduct, { productId });
    await this.#syncProductsFromState(state);
    const product = this.products.get(productId);
    product?.dispose();
    this.products.delete(productId);
    return state;
  }

  resolveProjectFile(projectPath) {
    return this.facadeClient.invoke(this.applicationChannelId, AppFacade.resolveProjectFile, { projectPath });
  }

  openProjectFile(projectPath, options = {}) {
    return this.openProject(projectPath, options);
  }

  async openProject(projectPath, options = {}) {
    const response = await this.facadeClient.invoke(this.applicationChannelId, AppFacade.openProjectFile, {
      projectPath,
      catalogName: options.catalogName ?? "",
      projectName: options.projectName ?? "",
    });
    await this.#syncProductsFromState(response.state);

    const product = response.product ? await this.adoptProduct(response.product) : null;
    const project = product && response.catalog?.mainProject
      ? await product.adoptProject(response.catalog.mainProject)
      : null;

    return {
      ...response,
      productProxy: product,
      projectProxy: project,
      sceneProxy: project?.getMainScene() ?? null,
    };
  }

  subscribe(facadeMember, handler) {
    return this.#trackUnsubscribe(this.facadeClient.subscribe(this.applicationChannelId, facadeMember, handler));
  }

  subscribeAll(handler) {
    return this.#trackUnsubscribe(this.facadeClient.subscribeAll(this.applicationChannelId, handler));
  }

  getProduct(productId) {
    return this.products.get(productId) ?? null;
  }

  async adoptProduct(productState) {
    if (!productState || !this.#isProductRuntimeState(productState)) {
      return null;
    }

    const registeredState = await this.#registerProductChannel(productState);
    if (!isUsableChannelId(registeredState.productChannelId)) {
      throw new Error(`Started product has no usable channel id: ${registeredState.productId}`);
    }

    const existing = this.products.get(registeredState.productId);
    if (existing) {
      existing.updateState(registeredState);
      return existing;
    }

    const product = new ProductProxy(this.facadeClient, registeredState, {
      bridge: this.bridge,
      appProxy: this,
    });
    this.products.set(product.productId, product);
    return product;
  }

  dispose() {
    for (const product of this.products.values()) {
      product.dispose();
    }
    this.products.clear();

    for (const unsubscribe of [...this.unsubscribers]) {
      unsubscribe();
    }
    this.unsubscribers.clear();

    this.facadeClient.stop(this.applicationChannelId);
  }

  #trackUnsubscribe(unsubscribe) {
    this.unsubscribers.add(unsubscribe);
    return () => {
      this.unsubscribers.delete(unsubscribe);
      unsubscribe();
    };
  }

  async #registerProductChannel(productState) {
    if (productState?.isStarted !== true || !productState?.productId) {
      return productState;
    }

    if (typeof this.bridge?.registerProductChannel !== "function") {
      throw new Error("Host bridge does not support product channel registration");
    }

    const channelId = await this.bridge.registerProductChannel(productState.productId);
    if (!isUsableChannelId(channelId)) {
      throw new Error(`Host bridge returned invalid product channel id: ${productState.productId}`);
    }

    return { ...productState, productChannelId: channelId };
  }

  #isProductRuntimeState(productState) {
    return Boolean(productState?.productId)
      && (productState.isStarted === true || isUsableChannelId(productState.productChannelId));
  }

  async #syncProductsFromState(state) {
    if (!state) {
      return;
    }

    this.state = state;

    const runningProductIds = new Set();
    for (const productState of state.products ?? []) {
      if (!this.#isProductRuntimeState(productState)) {
        continue;
      }
      runningProductIds.add(productState.productId);
      await this.adoptProduct(productState);
    }

    for (const productId of [...this.products.keys()]) {
      if (!runningProductIds.has(productId)) {
        const product = this.products.get(productId);
        product?.dispose();
        this.products.delete(productId);
      }
    }
  }
}
