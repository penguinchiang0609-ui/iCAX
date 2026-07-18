import { AppFacade, ProductFacade, ProjectFacade, makeFacadeMethodCodeFromName } from "../Mailbox/facadeMethod.mjs";
import { deserializeVariantText, serializeVariantText } from "../Mailbox/variantSerializer.mjs";

const appChannelId = "00000000-0000-4000-8000-000000000001";
const productChannelId = "00000000-0000-4000-8000-000000000101";
const sceneChannelId = "00000000-0000-4000-8000-000000000201";
const projectId = "00000000-0000-4000-8000-000000000401";
const mainSceneId = "00000000-0000-4000-8000-000000000501";
const mockProductId = "icax.mock-product";
const nilChannelId = "00000000-0000-0000-0000-000000000000";

export class MockHostBridge {
  constructor(options = {}) {
    this.delayMs = options.delayMs ?? 16;
    this.listeners = new Map();
    this.productStarted = false;
    this.projectOpened = false;
    this.projectPath = "D:/projects/mock.icax";
  }

  async getApplicationChannelId() {
    return appChannelId;
  }

  async registerProductChannel(productId) {
    if (productId && productId !== mockProductId) {
      throw new Error(`Product is not started: ${productId}`);
    }
    if (!this.productStarted) {
      throw new Error("Product is not started");
    }
    return productChannelId;
  }

  async registerSceneChannel(requestProjectId, requestSceneId) {
    if (!requestProjectId) {
      throw new TypeError("projectId is required");
    }
    if (!requestSceneId) {
      throw new TypeError("sceneId is required");
    }
    if (!this.projectOpened) {
      throw new Error("Project is not open");
    }
    if (requestProjectId !== projectId || requestSceneId !== mainSceneId) {
      throw new Error(`Scene is not open: ${requestProjectId}/${requestSceneId}`);
    }
    return sceneChannelId;
  }

  subscribeMail(channelId, handler) {
    if (!this.listeners.has(channelId)) {
      this.listeners.set(channelId, new Set());
    }
    this.listeners.get(channelId).add(handler);
    return () => this.listeners.get(channelId)?.delete(handler);
  }

  async postMail(mail) {
    const payload = deserializeVariantText(mail.payloadText);
    const responsePayload = this.#invokeFacadeMethod(String(mail.typeCode), payload);
    const response = {
      channelId: mail.channelId,
      id: Date.now(),
      originId: mail.id,
      typeCode: mail.typeCode,
      stamp: 0,
      payloadText: serializeVariantText(responsePayload),
    };

    setTimeout(() => {
      for (const listener of this.listeners.get(mail.channelId) ?? []) {
        listener(response);
      }
    }, this.delayMs);
  }

  async openFileDialog() {
    return "D:/projects/mock.icax";
  }

  async windowCommand(command) {
    this.lastWindowCommand = command;
  }

  async beginWindowDrag() {
    this.windowDragRequested = true;
  }

  emitMail(channelId, facadeMember, payload = {}, options = {}) {
    const eventMail = {
      channelId,
      id: options.id ?? Date.now(),
      originId: options.originId ?? 0,
      typeCode: makeFacadeMethodCodeFromName(facadeMember),
      stamp: options.stamp ?? 0,
      payloadText: serializeVariantText(payload),
    };

    setTimeout(() => {
      for (const listener of this.listeners.get(channelId) ?? []) {
        listener(eventMail);
      }
    }, options.delayMs ?? this.delayMs);
    return eventMail;
  }

  #invokeFacadeMethod(typeCode, payload) {
    if (typeCode === makeFacadeMethodCodeFromName(AppFacade.getState)
      || typeCode === makeFacadeMethodCodeFromName(AppFacade.listProducts)) {
      return this.#applicationState();
    }

    if (typeCode === makeFacadeMethodCodeFromName(AppFacade.startProduct)) {
      this.productStarted = true;
      return { applicationChannelId: appChannelId, product: this.#productState(), state: this.#applicationState() };
    }

    if (typeCode === makeFacadeMethodCodeFromName(AppFacade.stopProduct)) {
      this.productStarted = false;
      this.projectOpened = false;
      return this.#applicationState();
    }

    if (typeCode === makeFacadeMethodCodeFromName(AppFacade.resolveProjectFile)) {
      return {
        applicationChannelId: appChannelId,
        resolve: this.#resolveProjectFile(payload.projectPath),
      };
    }

    if (typeCode === makeFacadeMethodCodeFromName(AppFacade.openProjectFile)) {
      this.productStarted = true;
      this.projectOpened = true;
      this.projectPath = payload.projectPath || this.projectPath;
      return {
        applicationChannelId: appChannelId,
        resolve: this.#resolveProjectFile(payload.projectPath),
        product: this.#productState(),
        catalog: this.#catalogState(payload.projectPath),
        state: this.#applicationState(),
      };
    }

    if (typeCode === makeFacadeMethodCodeFromName(ProductFacade.getState)
      || typeCode === makeFacadeMethodCodeFromName(ProductFacade.listProjectCatalogs)) {
      return this.#runningProductState();
    }

    if (typeCode === makeFacadeMethodCodeFromName(ProductFacade.openProjectCatalog)) {
      this.projectOpened = true;
      this.projectPath = payload.projectPath || this.projectPath;
      return { catalog: this.#catalogState(payload.projectPath), state: this.#runningProductState() };
    }

    if (typeCode === makeFacadeMethodCodeFromName(ProjectFacade.getState)) {
      return this.#projectState();
    }

    if (typeCode === makeFacadeMethodCodeFromName(ProjectFacade.undo)
      || typeCode === makeFacadeMethodCodeFromName(ProjectFacade.redo)) {
      return this.#undoRedoState();
    }

    if (typeCode === makeFacadeMethodCodeFromName(ProjectFacade.getUndoRedoState)) {
      return this.#undoRedoState();
    }

    return { state: "Ok" };
  }

  #resolveProjectFile(projectPath = "") {
    return {
      projectPath: projectPath ?? "",
      status: projectPath ? "Resolved" : "NotFound",
      productId: projectPath ? mockProductId : "",
      candidateProductIds: projectPath ? [mockProductId] : [],
      matchedByMagic: Boolean(projectPath),
    };
  }

  #applicationState() {
    const product = this.#productState();
    return {
      applicationChannelId: appChannelId,
      state: "Running",
      phase: "Running",
      productCount: 1,
      products: [product],
      runningProductCount: this.productStarted ? 1 : 0,
      faultMessage: "",
    };
  }

  #productState() {
    const session = this.productStarted ? this.#runningProductState() : null;
    return {
      productId: mockProductId,
      productName: "Mock Product",
      productVersion: "0.1.0",
      frontendEntry: "../../../../tests/iCAX-UI/fixtures/mock-product/entry.mjs",
      defaultProjectStartupComponent: "MockStartupComponent",
      projectFile: {
        magic: "ICAX_MOCK_PRODUCT",
        formatVersion: "1.0",
        fileExtensions: [".icax"],
        magicOffset: 0,
        probeBytes: 256,
      },
      isStarted: this.productStarted,
      productChannelId: this.productStarted ? productChannelId : nilChannelId,
      recentProjects: [
        { path: "D:/projects/mock.icax", displayName: "mock.icax", lastOpenedTime: "2026-06-24T00:00:00Z" },
      ],
      session,
    };
  }

  #runningProductState() {
    return {
      productId: mockProductId,
      productChannelId,
      catalogs: this.projectOpened ? [this.#catalogState(this.projectPath)] : [],
    };
  }

  #catalogState(projectPath = "D:/projects/mock.icax") {
    return {
      catalogId: "00000000-0000-4000-8000-000000000301",
      catalogName: "Mock Catalog",
      catalogPath: projectPath,
      mainProject: this.#projectState(projectPath),
      projects: [],
    };
  }

  #projectState(projectPath = "D:/projects/mock.icax") {
    const mainScene = this.#sceneState();
    return {
      projectId,
      mainSceneId,
      mainSceneChannelId: sceneChannelId,
      projectName: "Mock Project",
      projectPath,
      state: "Running",
      isOpen: true,
      isRunning: true,
      startupComponent: "CMockProjectStartupComponent",
      faultMessage: "",
      mainScene,
      sceneCount: 1,
      scenes: [mainScene],
    };
  }

  #sceneState() {
    return {
      sceneId: mainSceneId,
      sceneChannelId,
      parentSceneId: nilChannelId,
      sceneName: "Main Scene",
      role: "Main",
      state: "Running",
      isMainScene: true,
      isTransientScene: false,
      isOpen: true,
      isRunning: true,
      startupComponent: "CMockProjectStartupComponent",
      faultMessage: "",
      undoRedo: this.#undoRedoState(),
      pdo: {
        enabled: false,
        sharedArenaName: "",
        sharedArenaSize: 0,
        declarations: [],
      },
    };
  }

  #undoRedoState() {
    return {
      canUndo: false,
      canRedo: false,
      undoSteps: [],
      redoSteps: [],
    };
  }
}
