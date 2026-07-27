import { AppSDO, ProductSDO, ProjectSDO, makeSDOMethodCodeFromName } from "../SDO/sdoMethod.mjs";
import { SDOFrameKind } from "../SDO/sdoClient.mjs";
import { deserializeVariantText, serializeVariantText } from "../SDO/variantSerializer.mjs";

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
    this.postedFrames = [];
    this.productStarted = false;
    this.projectOpened = false;
    this.projectPath = "D:/projects/mock.icax";
    this.resourceRecords = new Map();
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

  subscribeSDOFrames(channelId, handler) {
    if (!this.listeners.has(channelId)) {
      this.listeners.set(channelId, new Set());
    }
    this.listeners.get(channelId).add(handler);
    return () => this.listeners.get(channelId)?.delete(handler);
  }

  async postSDOFrame(frame) {
    this.postedFrames.push(frame);
    if (Number(frame.kind) !== SDOFrameKind.Request) {
      return;
    }

    const payload = deserializeVariantText(frame.payloadText);
    const responsePayload = this.#invokeSDOMethod(String(frame.methodCode), payload);
    const response = {
      channelId: frame.channelId,
      callId: frame.callId,
      methodCode: frame.methodCode,
      kind: SDOFrameKind.Response,
      status: 0,
      payloadText: serializeVariantText(responsePayload),
    };

    setTimeout(() => {
      for (const listener of this.listeners.get(frame.channelId) ?? []) {
        listener(response);
      }
    }, this.delayMs);
  }

  async requestResource(request = {}) {
    if (!request.projectId || !request.sceneId || !request.url) {
      throw new TypeError("projectId, sceneId, and url are required");
    }

    const method = String(request.method ?? "GET").toUpperCase();
    const key = `${request.projectId}\n${request.sceneId}\n${request.url}`;
    const requestHeaders = new Headers(request.headers ?? {});
    const current = this.resourceRecords.get(key) ?? null;

    if (method === "OPTIONS") {
      return mockResourceResponse(204, {
        Allow: "HEAD, GET, PUT, DELETE, OPTIONS",
        "Accept-Put": "application/vnd.icax.flatbuffer",
      });
    }

    if (method === "PUT") {
      if (requestHeaders.get("If-None-Match") === "*" && current) {
        return mockResourceResponse(412);
      }
      if (requestHeaders.has("If-Match")
        && requestHeaders.get("If-Match") !== "*"
        && requestHeaders.get("If-Match") !== current?.etag) {
        return mockResourceResponse(412);
      }
      if (requestHeaders.get("If-Match") === "*" && !current) {
        return mockResourceResponse(412);
      }

      const body = copyArrayBuffer(request.body);
      const version = (current?.version ?? 0) + 1;
      const etag = `"icax-v${version}"`;
      const headers = Object.fromEntries(requestHeaders);
      headers["Content-Type"] = requestHeaders.get("Content-Type")
        ?? "application/vnd.icax.flatbuffer";
      headers["Content-Length"] = String(body.byteLength);
      headers["ICAX-Resource-Length"] = String(body.byteLength);
      headers.ETag = etag;
      headers["ICAX-Resource-Version"] = String(version);
      const record = { body, headers, version, etag };
      this.resourceRecords.set(key, record);
      return mockResourceResponse(current ? 204 : 201, {
        ...headers,
        "Content-Length": "0",
        ...(current ? {} : { Location: request.url }),
      });
    }

    if (!current) {
      return mockResourceResponse(404);
    }

    if (method === "DELETE") {
      const ifMatch = requestHeaders.get("If-Match");
      if (ifMatch && ifMatch !== "*" && ifMatch !== current.etag) {
        return mockResourceResponse(412);
      }
      this.resourceRecords.delete(key);
      return mockResourceResponse(204);
    }

    if (method === "HEAD" || method === "GET") {
      if (requestHeaders.get("If-None-Match") === current.etag) {
        return mockResourceResponse(304, {
          ...current.headers,
          "Content-Length": "0",
        });
      }
      return mockResourceResponse(
        200,
        current.headers,
        method === "GET" ? current.body : undefined,
      );
    }

    return mockResourceResponse(405, {
      Allow: "HEAD, GET, PUT, DELETE, OPTIONS",
    });
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

  emitSDOFrame(channelId, sdoMember, payload = {}, options = {}) {
    const frame = {
      channelId,
      callId: options.callId ?? 0,
      methodCode: makeSDOMethodCodeFromName(sdoMember),
      kind: options.kind ?? SDOFrameKind.Event,
      status: options.status ?? 0,
      payloadText: serializeVariantText(payload),
    };

    setTimeout(() => {
      for (const listener of this.listeners.get(channelId) ?? []) {
        listener(frame);
      }
    }, options.delayMs ?? this.delayMs);
    return frame;
  }

  emitReport(channelId, callId, sdoMember, payload = {}, options = {}) {
    if (!callId) {
      throw new TypeError("callId is required for a SDO report");
    }
    return this.emitSDOFrame(channelId, sdoMember, payload, {
      ...options,
      callId,
      kind: SDOFrameKind.Report,
    });
  }

  emitRequest(channelId, callId, sdoMethod, payload = {}, options = {}) {
    if (!callId) {
      throw new TypeError("callId is required for a SDO request");
    }
    return this.emitSDOFrame(channelId, sdoMethod, payload, {
      ...options,
      callId,
      kind: SDOFrameKind.Request,
    });
  }

  #invokeSDOMethod(methodCode, payload) {
    if (methodCode === makeSDOMethodCodeFromName(AppSDO.getState)
      || methodCode === makeSDOMethodCodeFromName(AppSDO.listProducts)) {
      return this.#applicationState();
    }

    if (methodCode === makeSDOMethodCodeFromName(AppSDO.startProduct)) {
      this.productStarted = true;
      return { applicationChannelId: appChannelId, product: this.#productState(), state: this.#applicationState() };
    }

    if (methodCode === makeSDOMethodCodeFromName(AppSDO.stopProduct)) {
      this.productStarted = false;
      this.projectOpened = false;
      return this.#applicationState();
    }

    if (methodCode === makeSDOMethodCodeFromName(AppSDO.resolveProjectFile)) {
      return {
        applicationChannelId: appChannelId,
        resolve: this.#resolveProjectFile(payload.projectPath),
      };
    }

    if (methodCode === makeSDOMethodCodeFromName(AppSDO.openProjectFile)) {
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

    if (methodCode === makeSDOMethodCodeFromName(ProductSDO.getState)
      || methodCode === makeSDOMethodCodeFromName(ProductSDO.listProjectCatalogs)) {
      return this.#runningProductState();
    }

    if (methodCode === makeSDOMethodCodeFromName(ProductSDO.openProjectCatalog)) {
      this.projectOpened = true;
      this.projectPath = payload.projectPath || this.projectPath;
      return { catalog: this.#catalogState(payload.projectPath), state: this.#runningProductState() };
    }

    if (methodCode === makeSDOMethodCodeFromName(ProjectSDO.getState)) {
      return this.#projectState();
    }

    if (methodCode === makeSDOMethodCodeFromName(ProjectSDO.undo)
      || methodCode === makeSDOMethodCodeFromName(ProjectSDO.redo)) {
      return this.#undoRedoState();
    }

    if (methodCode === makeSDOMethodCodeFromName(ProjectSDO.getUndoRedoState)) {
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

function copyArrayBuffer(body) {
  if (body === undefined || body === null) {
    return new ArrayBuffer(0);
  }
  if (body instanceof ArrayBuffer) {
    return body.slice(0);
  }
  if (ArrayBuffer.isView(body)) {
    return body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength);
  }
  throw new TypeError("mock resource body must be an ArrayBuffer or ArrayBufferView");
}

function mockResourceResponse(status, headers = {}, body = undefined) {
  return {
    status,
    headers,
    body: body === undefined ? new ArrayBuffer(0) : copyArrayBuffer(body),
  };
}
