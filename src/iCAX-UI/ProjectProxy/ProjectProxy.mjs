import { isUsableChannelId } from "../SDK/Mailbox/channelId.mjs";
import { SceneProxy } from "../SceneProxy/SceneProxy.mjs";

export class ProjectProxy {
  constructor(mailboxClient, projectState, options = {}) {
    if (!mailboxClient) {
      throw new TypeError("mailboxClient is required");
    }
    if (!projectState?.projectId) {
      throw new TypeError("projectState.projectId is required");
    }

    this.mailboxClient = mailboxClient;
    this.bridge = options.bridge ?? mailboxClient.bridge ?? null;
    this.product = options.product ?? null;
    this.state = projectState;
    this.projectId = projectState.projectId;
    this.mainSceneId = projectState.mainSceneId ?? projectState.mainScene?.sceneId ?? "";
    this.scenes = new Map();
    this.mainSceneProxy = null;
  }

  updateState(projectState) {
    if (!projectState?.projectId) {
      throw new TypeError("projectState.projectId is required");
    }

    this.state = projectState;
    this.projectId = projectState.projectId;
    this.mainSceneId = projectState.mainSceneId ?? projectState.mainScene?.sceneId ?? "";
  }

  async syncScenes(projectState = this.state) {
    this.updateState(projectState);

    const sceneStates = collectSceneStates(projectState);
    const openSceneIds = new Set();
    for (const sceneState of sceneStates) {
      openSceneIds.add(sceneState.sceneId);
      await this.adoptScene(sceneState);
    }

    for (const sceneId of [...this.scenes.keys()]) {
      if (!openSceneIds.has(sceneId)) {
        const scene = this.scenes.get(sceneId);
        scene?.dispose();
        this.scenes.delete(sceneId);
      }
    }

    this.mainSceneProxy = this.mainSceneId ? this.getScene(this.mainSceneId) : null;
    return this;
  }

  async adoptScene(sceneState) {
    if (!sceneState?.sceneId) {
      return null;
    }

    const registeredState = await this.#registerSceneChannel(sceneState);
    if (!isUsableChannelId(registeredState.sceneChannelId)) {
      throw new Error(`Scene has no usable channel id: ${registeredState.sceneId}`);
    }

    const existing = this.scenes.get(registeredState.sceneId);
    if (existing) {
      existing.updateState(registeredState);
      return existing;
    }

    const scene = new SceneProxy(this.mailboxClient, registeredState, {
      bridge: this.bridge,
      project: this,
    });
    this.scenes.set(scene.sceneId, scene);
    if (scene.sceneId === this.mainSceneId) {
      this.mainSceneProxy = scene;
    }
    return scene;
  }

  getScene(sceneId) {
    return this.scenes.get(sceneId) ?? null;
  }

  getMainScene() {
    return this.mainSceneProxy;
  }

  dispose() {
    for (const scene of this.scenes.values()) {
      scene.dispose();
    }
    this.scenes.clear();
    this.mainSceneProxy = null;
  }

  async #registerSceneChannel(sceneState) {
    if (isUsableChannelId(sceneState?.sceneChannelId)) {
      return sceneState;
    }

    if (typeof this.bridge?.registerSceneChannel !== "function") {
      throw new Error("Host bridge does not support scene channel registration");
    }

    const channelId = await this.bridge.registerSceneChannel(this.projectId, sceneState.sceneId);
    if (!isUsableChannelId(channelId)) {
      throw new Error(`Host bridge returned invalid scene channel id: ${this.projectId}/${sceneState.sceneId}`);
    }

    return { ...sceneState, sceneChannelId: channelId };
  }
}

function collectSceneStates(projectState) {
  const result = [];
  const seen = new Set();
  const append = (scene) => {
    if (!scene?.sceneId || seen.has(scene.sceneId)) {
      return;
    }
    seen.add(scene.sceneId);
    result.push(scene);
  };

  append(projectState?.mainScene);
  for (const scene of projectState?.scenes ?? []) {
    append(scene);
  }
  return result;
}
