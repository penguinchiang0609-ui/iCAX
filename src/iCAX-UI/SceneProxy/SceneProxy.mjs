import { isUsableChannelId } from "../SDK/SDO/channelId.mjs";
import { ProjectSDO } from "../SDK/SDO/sdoMethod.mjs";
import { PDOClient } from "../SDK/PDO/pdoClient.mjs";
import { PDOStore } from "../SDK/PDO/pdoStore.mjs";
import { EntityViewClient } from "../SDK/EntityView/entityViewClient.mjs";
import { ResourceClient } from "../SDK/Resources/resourceClient.mjs";

export class SceneProxy {
  constructor(sdoClient, sceneState, options = {}) {
    if (!sdoClient) {
      throw new TypeError("sdoClient is required");
    }
    if (!isUsableChannelId(sceneState?.sceneChannelId)) {
      throw new TypeError("sceneState.sceneChannelId must be a non-nil channel id");
    }

    this.sdoClient = sdoClient;
    this.bridge = options.bridge ?? sdoClient.bridge ?? null;
    this.project = options.project ?? null;
    this.state = sceneState;
    this.sceneId = sceneState.sceneId;
    this.sceneChannelId = sceneState.sceneChannelId;
    this.pdo = new PDOClient(sceneState.pdo, this.bridge);
    this.pdoStore = new PDOStore();
    this.resources = new ResourceClient({
      bridge: this.bridge,
    });
    this.unsubscribers = new Set();
    this.pdoStoreUnsubscribe = this.subscribeAll(
      (event) => this.pdoStore.ingestEvent(event),
    );
    this.entityViews = new EntityViewClient(this);
  }

  updateState(sceneState) {
    if (!isUsableChannelId(sceneState?.sceneChannelId)) {
      throw new TypeError("sceneState.sceneChannelId must be a non-nil channel id");
    }

    this.state = sceneState;
    this.sceneId = sceneState.sceneId;
    this.sceneChannelId = sceneState.sceneChannelId;
    this.pdo = new PDOClient(sceneState.pdo, this.bridge);
    this.resources.updateScope({
      bridge: this.bridge,
    });
  }

  async getState(options = {}) {
    const response = await this.invoke(ProjectSDO.getState, {}, options);
    const sceneState = response?.activeScene ?? findSceneInProjectState(response, this.sceneId);
    if (sceneState?.sceneChannelId) {
      this.updateState(sceneState);
    }
    return sceneState ?? this.state;
  }

  invoke(sdoMethod, payload = {}, options = {}) {
    return this.sdoClient.invoke(this.sceneChannelId, sdoMethod, payload, options);
  }

  fetchResource(url, init = {}) {
    return this.resources.fetch(url, init);
  }

  undo(options = {}) {
    return this.invoke(ProjectSDO.undo, {}, options);
  }

  redo(options = {}) {
    return this.invoke(ProjectSDO.redo, {}, options);
  }

  getUndoRedoState(options = {}) {
    return this.invoke(ProjectSDO.getUndoRedoState, {}, options);
  }

  subscribe(sdoMember, handler) {
    return this.#trackUnsubscribe(this.sdoClient.subscribe(this.sceneChannelId, sdoMember, handler));
  }

  subscribeAll(handler) {
    return this.#trackUnsubscribe(this.sdoClient.subscribeAll(this.sceneChannelId, handler));
  }

  dispose() {
    void this.entityViews.dispose();
    for (const unsubscribe of [...this.unsubscribers]) {
      unsubscribe();
    }
    this.unsubscribers.clear();
    this.pdoStore.dispose();
    this.sdoClient.stop(this.sceneChannelId);
  }

  #trackUnsubscribe(unsubscribe) {
    this.unsubscribers.add(unsubscribe);
    return () => {
      this.unsubscribers.delete(unsubscribe);
      unsubscribe();
    };
  }
}

function findSceneInProjectState(projectState, sceneId) {
  if (!projectState || !sceneId) {
    return null;
  }
  if (projectState.mainScene?.sceneId === sceneId) {
    return projectState.mainScene;
  }
  return (projectState.scenes ?? []).find((scene) => scene?.sceneId === sceneId) ?? null;
}
