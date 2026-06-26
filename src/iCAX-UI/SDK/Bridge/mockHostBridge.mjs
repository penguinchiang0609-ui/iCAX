import { AppCommands, ProductCommands, ProjectCommands, makeCommandTypeCodeFromCommand } from "../Mailbox/commandRoute.mjs";
import { deserializeVariantText, serializeVariantText } from "../Mailbox/variantSerializer.mjs";

const appChannelId = "00000000-0000-4000-8000-000000000001";
const productChannelId = "00000000-0000-4000-8000-000000000101";
const projectChannelId = "00000000-0000-4000-8000-000000000201";
const nilChannelId = "00000000-0000-0000-0000-000000000000";

export class MockHostBridge {
  constructor(options = {}) {
    this.delayMs = options.delayMs ?? 16;
    this.listeners = new Map();
    this.productStarted = false;
    this.projectOpened = false;
  }

  async getApplicationChannelId() {
    return appChannelId;
  }

  async registerProductChannel(productId) {
    if (productId && productId !== "icax.flat-laser-cam") {
      throw new Error(`Product is not started: ${productId}`);
    }
    if (!this.productStarted) {
      throw new Error("Product is not started");
    }
    return productChannelId;
  }

  async registerProjectChannel(projectId) {
    if (!projectId) {
      throw new TypeError("projectId is required");
    }
    if (!this.projectOpened) {
      throw new Error("Project is not open");
    }
    return projectChannelId;
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
    const responsePayload = this.#handleCommand(String(mail.typeCode), payload);
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
    return "D:/projects/mock.ilcam";
  }

  emitMail(channelId, command, payload = {}, options = {}) {
    const eventMail = {
      channelId,
      id: options.id ?? Date.now(),
      originId: options.originId ?? 0,
      typeCode: makeCommandTypeCodeFromCommand(command),
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

  #handleCommand(typeCode, payload) {
    if (typeCode === makeCommandTypeCodeFromCommand(AppCommands.getState)
      || typeCode === makeCommandTypeCodeFromCommand(AppCommands.listProducts)) {
      return this.#applicationState();
    }

    if (typeCode === makeCommandTypeCodeFromCommand(AppCommands.startProduct)) {
      this.productStarted = true;
      return { applicationChannelId: appChannelId, product: this.#productState(), state: this.#applicationState() };
    }

    if (typeCode === makeCommandTypeCodeFromCommand(AppCommands.stopProduct)) {
      this.productStarted = false;
      this.projectOpened = false;
      return this.#applicationState();
    }

    if (typeCode === makeCommandTypeCodeFromCommand(AppCommands.resolveProjectFile)) {
      return {
        applicationChannelId: appChannelId,
        resolve: this.#resolveProjectFile(payload.projectPath),
      };
    }

    if (typeCode === makeCommandTypeCodeFromCommand(AppCommands.openProjectFile)) {
      this.productStarted = true;
      this.projectOpened = true;
      return {
        applicationChannelId: appChannelId,
        resolve: this.#resolveProjectFile(payload.projectPath),
        product: this.#productState(),
        catalog: this.#catalogState(payload.projectPath),
        state: this.#applicationState(),
      };
    }

    if (typeCode === makeCommandTypeCodeFromCommand(ProductCommands.getState)
      || typeCode === makeCommandTypeCodeFromCommand(ProductCommands.listProjectCatalogs)) {
      return this.#runningProductState();
    }

    if (typeCode === makeCommandTypeCodeFromCommand(ProductCommands.openProjectCatalog)) {
      this.projectOpened = true;
      return { catalog: this.#catalogState(payload.projectPath), state: this.#runningProductState() };
    }

    if (typeCode === makeCommandTypeCodeFromCommand(ProjectCommands.getState)) {
      return this.#projectState();
    }

    if (typeCode === makeCommandTypeCodeFromCommand(ProjectCommands.undo)
      || typeCode === makeCommandTypeCodeFromCommand(ProjectCommands.redo)) {
      return this.#undoRedoState();
    }

    if (typeCode === makeCommandTypeCodeFromCommand(ProjectCommands.getUndoRedoState)) {
      return this.#undoRedoState();
    }

    return { state: "Ok" };
  }

  #resolveProjectFile(projectPath = "") {
    return {
      projectPath: projectPath ?? "",
      status: projectPath ? "Resolved" : "NotFound",
      productId: projectPath ? "icax.flat-laser-cam" : "",
      candidateProductIds: projectPath ? ["icax.flat-laser-cam"] : [],
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
      productId: "icax.flat-laser-cam",
      productName: "Flat Laser CAM",
      productVersion: "0.1.0",
      frontendEntry: "apps/flat-laser-cam/webpage/entry.mjs",
      defaultProjectStartupComponent: "FlatLaserCamStartupComponent",
      projectFile: {
        magic: "ICAX_FLAT_LASER_CAM",
        formatVersion: "1.0",
        fileExtensions: [".ilcam"],
        magicOffset: 0,
        probeBytes: 256,
      },
      isStarted: this.productStarted,
      productChannelId: this.productStarted ? productChannelId : nilChannelId,
      recentProjects: [
        { path: "D:/projects/mock.ilcam", displayName: "mock.ilcam", lastOpenedTime: "2026-06-24T00:00:00Z" },
      ],
      session,
    };
  }

  #runningProductState() {
    return {
      productId: "icax.flat-laser-cam",
      productChannelId,
      catalogs: this.projectOpened ? [this.#catalogState("D:/projects/mock.ilcam")] : [],
    };
  }

  #catalogState(projectPath = "D:/projects/mock.ilcam") {
    return {
      catalogId: "00000000-0000-4000-8000-000000000301",
      catalogName: "Mock Catalog",
      catalogPath: projectPath,
      mainProject: this.#projectState(projectPath),
      projects: [],
    };
  }

  #projectState(projectPath = "D:/projects/mock.ilcam") {
    return {
      projectId: "00000000-0000-4000-8000-000000000401",
      projectChannelId,
      projectName: "Mock Project",
      projectPath,
      state: "Running",
      role: "Main",
      isMainProject: true,
      isTransientProject: false,
      isOpen: true,
      isRunning: true,
      startupComponent: "CFlatLaserProjectComponent",
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
