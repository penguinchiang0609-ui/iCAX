import { isUsableChannelId } from "../SDK/Facades/channelId.mjs";
import { ProjectFacade } from "../SDK/Facades/facadeMethod.mjs";
import { PDOClient } from "../SDK/PDO/pdoClient.mjs";

export class SceneProxy {
  constructor(facadeClient, sceneState, options = {}) {
    if (!facadeClient) {
      throw new TypeError("facadeClient is required");
    }
    if (!isUsableChannelId(sceneState?.sceneChannelId)) {
      throw new TypeError("sceneState.sceneChannelId must be a non-nil channel id");
    }

    this.facadeClient = facadeClient;
    this.bridge = options.bridge ?? facadeClient.bridge ?? null;
    this.project = options.project ?? null;
    this.state = sceneState;
    this.sceneId = sceneState.sceneId;
    this.sceneChannelId = sceneState.sceneChannelId;
    this.pdo = new PDOClient(sceneState.pdo, this.bridge);
    this.unsubscribers = new Set();
  }

  updateState(sceneState) {
    if (!isUsableChannelId(sceneState?.sceneChannelId)) {
      throw new TypeError("sceneState.sceneChannelId must be a non-nil channel id");
    }

    this.state = sceneState;
    this.sceneId = sceneState.sceneId;
    this.sceneChannelId = sceneState.sceneChannelId;
    this.pdo = new PDOClient(sceneState.pdo, this.bridge);
  }

  async getState(options = {}) {
    const response = await this.invoke(ProjectFacade.getState, {}, options);
    const sceneState = response?.activeScene ?? findSceneInProjectState(response, this.sceneId);
    if (sceneState?.sceneChannelId) {
      this.updateState(sceneState);
    }
    return sceneState ?? this.state;
  }

  invoke(facadeMethod, payload = {}, options = {}) {
    return this.facadeClient.invoke(this.sceneChannelId, facadeMethod, payload, options);
  }

  undo(options = {}) {
    return this.invoke(ProjectFacade.undo, {}, options);
  }

  redo(options = {}) {
    return this.invoke(ProjectFacade.redo, {}, options);
  }

  getUndoRedoState(options = {}) {
    return this.invoke(ProjectFacade.getUndoRedoState, {}, options);
  }

  subscribe(facadeMember, handler) {
    return this.#trackUnsubscribe(this.facadeClient.subscribe(this.sceneChannelId, facadeMember, handler));
  }

  subscribeAll(handler) {
    return this.#trackUnsubscribe(this.facadeClient.subscribeAll(this.sceneChannelId, handler));
  }

  dispose() {
    for (const unsubscribe of [...this.unsubscribers]) {
      unsubscribe();
    }
    this.unsubscribers.clear();
    this.facadeClient.stop(this.sceneChannelId);
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
