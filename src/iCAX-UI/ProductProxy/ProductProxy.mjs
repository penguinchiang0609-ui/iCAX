import { isUsableChannelId } from "../SDK/Mailbox/channelId.mjs";
import { ProductCommands } from "../SDK/Mailbox/commandRoute.mjs";
import { ProjectProxy } from "../ProjectProxy/ProjectProxy.mjs";

export class ProductProxy {
  constructor(mailboxClient, productState, options = {}) {
    if (!mailboxClient) {
      throw new TypeError("mailboxClient is required");
    }
    if (!isUsableChannelId(productState?.productChannelId)) {
      throw new TypeError("productState.productChannelId must be a non-nil channel id");
    }

    this.mailboxClient = mailboxClient;
    this.bridge = options.bridge ?? mailboxClient.bridge ?? null;
    this.appProxy = options.appProxy ?? null;
    this.state = productState;
    this.productId = productState.productId;
    this.productChannelId = productState.productChannelId;
    this.projects = new Map();
    this.unsubscribers = new Set();
  }

  updateState(productState) {
    if (!isUsableChannelId(productState?.productChannelId)) {
      throw new TypeError("productState.productChannelId must be a non-nil channel id");
    }

    this.state = productState;
    this.productId = productState.productId;
    this.productChannelId = productState.productChannelId;
  }

  async getState() {
    const state = await this.mailboxClient.request(this.productChannelId, ProductCommands.getState);
    if (state?.productChannelId) {
      this.updateState({ ...this.state, ...state });
    }
    await this.#syncProjectsFromCatalogs(state?.catalogs ?? []);
    return state;
  }

  async listProjectCatalogs() {
    const response = await this.mailboxClient.request(this.productChannelId, ProductCommands.listProjectCatalogs);
    await this.#syncProjectsFromCatalogs(response?.catalogs ?? []);
    return response;
  }

  async openProjectCatalog(projectPath, options = {}) {
    const response = await this.mailboxClient.request(this.productChannelId, ProductCommands.openProjectCatalog, {
      projectPath,
      catalogPath: options.catalogPath ?? projectPath,
      catalogName: options.catalogName ?? "",
      projectName: options.projectName ?? "",
    });

    const project = response.catalog?.mainProject;
    const projectProxy = project ? await this.adoptProject(project) : null;
    return { ...response, projectProxy, sceneProxy: projectProxy?.getMainScene() ?? null };
  }

  async closeProjectCatalog(catalogId) {
    const response = await this.mailboxClient.request(this.productChannelId, ProductCommands.closeProjectCatalog, { catalogId });
    if (response?.productChannelId) {
      this.updateState({ ...this.state, ...response });
    }
    await this.#syncProjectsFromCatalogs(response?.catalogs ?? []);
    return response;
  }

  subscribe(command, handler) {
    return this.#trackUnsubscribe(this.mailboxClient.subscribe(this.productChannelId, command, handler));
  }

  subscribeAll(handler) {
    return this.#trackUnsubscribe(this.mailboxClient.subscribeAll(this.productChannelId, handler));
  }

  getProject(projectId) {
    return this.projects.get(projectId) ?? null;
  }

  async adoptProject(projectState) {
    if (!projectState?.projectId) {
      return null;
    }

    const registeredState = await this.#registerProjectMainScene(projectState);
    const existing = this.projects.get(registeredState.projectId);
    if (existing) {
      existing.updateState(registeredState);
      await existing.syncScenes(registeredState);
      return existing;
    }

    const project = new ProjectProxy(this.mailboxClient, registeredState, {
      bridge: this.bridge,
      product: this,
    });
    this.projects.set(project.projectId, project);
    await project.syncScenes(registeredState);
    return project;
  }

  dispose() {
    for (const project of this.projects.values()) {
      project.dispose();
    }
    this.projects.clear();

    for (const unsubscribe of [...this.unsubscribers]) {
      unsubscribe();
    }
    this.unsubscribers.clear();

    this.mailboxClient.stop(this.productChannelId);
  }

  #trackUnsubscribe(unsubscribe) {
    this.unsubscribers.add(unsubscribe);
    return () => {
      this.unsubscribers.delete(unsubscribe);
      unsubscribe();
    };
  }

  async #syncProjectsFromCatalogs(catalogs) {
    const openProjectIds = new Set();
    for (const catalog of catalogs) {
      if (catalog?.mainProject) {
        openProjectIds.add(catalog.mainProject.projectId);
        await this.adoptProject(catalog.mainProject);
      }
      for (const project of catalog?.projects ?? []) {
        openProjectIds.add(project.projectId);
        await this.adoptProject(project);
      }
    }

    for (const projectId of [...this.projects.keys()]) {
      if (!openProjectIds.has(projectId)) {
        const project = this.projects.get(projectId);
        project?.dispose();
        this.projects.delete(projectId);
      }
    }
  }

  async #registerProjectMainScene(projectState) {
    if (!projectState?.projectId) {
      return projectState;
    }

    const mainScene = projectState.mainScene;
    if (!mainScene?.sceneId) {
      throw new Error(`Project has no main scene: ${projectState.projectId}`);
    }
    if (typeof this.bridge?.registerSceneChannel !== "function") {
      throw new Error("Host bridge does not support scene channel registration");
    }

    const channelId = await this.bridge.registerSceneChannel(projectState.projectId, mainScene.sceneId);
    if (!isUsableChannelId(channelId)) {
      throw new Error(`Host bridge returned invalid main scene channel id: ${projectState.projectId}/${mainScene.sceneId}`);
    }

    const registeredMainScene = { ...mainScene, sceneChannelId: channelId };
    const scenes = (projectState.scenes ?? []).map((scene) =>
      scene?.sceneId === registeredMainScene.sceneId ? registeredMainScene : scene);
    return {
      ...projectState,
      mainSceneChannelId: channelId,
      mainScene: registeredMainScene,
      scenes: scenes.some((scene) => scene?.sceneId === registeredMainScene.sceneId)
        ? scenes
        : [registeredMainScene, ...scenes],
    };
  }
}
