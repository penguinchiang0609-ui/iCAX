import { isUsableChannelId } from "../SDK/Mailbox/channelId.mjs";
import { ProjectCommands } from "../SDK/Mailbox/commandRoute.mjs";
import { PDOClient } from "../SDK/PDO/pdoClient.mjs";

export class SceneProxy {
  constructor(mailboxClient, sceneState, options = {}) {
    if (!mailboxClient) {
      throw new TypeError("mailboxClient is required");
    }
    if (!isUsableChannelId(sceneState?.sceneChannelId)) {
      throw new TypeError("sceneState.sceneChannelId must be a non-nil channel id");
    }

    this.mailboxClient = mailboxClient;
    this.bridge = options.bridge ?? mailboxClient.bridge ?? null;
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
    const response = await this.execute(ProjectCommands.getState, {}, options);
    const sceneState = response?.activeScene ?? findSceneInProjectState(response, this.sceneId);
    if (sceneState?.sceneChannelId) {
      this.updateState(sceneState);
    }
    return sceneState ?? this.state;
  }

  execute(command, payload = {}, options = {}) {
    return this.mailboxClient.request(this.sceneChannelId, command, payload, options);
  }

  undo(options = {}) {
    return this.execute(ProjectCommands.undo, {}, options);
  }

  redo(options = {}) {
    return this.execute(ProjectCommands.redo, {}, options);
  }

  getUndoRedoState(options = {}) {
    return this.execute(ProjectCommands.getUndoRedoState, {}, options);
  }

  subscribe(command, handler) {
    return this.#trackUnsubscribe(this.mailboxClient.subscribe(this.sceneChannelId, command, handler));
  }

  subscribeAll(handler) {
    return this.#trackUnsubscribe(this.mailboxClient.subscribeAll(this.sceneChannelId, handler));
  }

  dispose() {
    for (const unsubscribe of [...this.unsubscribers]) {
      unsubscribe();
    }
    this.unsubscribers.clear();
    this.mailboxClient.stop(this.sceneChannelId);
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
