import { isUsableChannelId } from "../SDK/Mailbox/channelId.mjs";
import { ProjectCommands } from "../SDK/Mailbox/commandRoute.mjs";
import { PDOClient } from "../SDK/PDO/pdoClient.mjs";

export class ProjectProxy {
  constructor(mailboxClient, projectState, options = {}) {
    if (!mailboxClient) {
      throw new TypeError("mailboxClient is required");
    }
    if (!isUsableChannelId(projectState?.projectChannelId)) {
      throw new TypeError("projectState.projectChannelId must be a non-nil channel id");
    }

    this.mailboxClient = mailboxClient;
    this.bridge = options.bridge ?? mailboxClient.bridge ?? null;
    this.product = options.product ?? null;
    this.state = projectState;
    this.projectId = projectState.projectId;
    this.projectChannelId = projectState.projectChannelId;
    this.pdo = new PDOClient(projectState.pdo, this.bridge);
    this.unsubscribers = new Set();
  }

  async getState() {
    const state = await this.execute(ProjectCommands.getState);
    if (state?.projectChannelId) {
      this.updateState({ ...this.state, ...state });
    }
    return state;
  }

  updateState(projectState) {
    if (!isUsableChannelId(projectState?.projectChannelId)) {
      throw new TypeError("projectState.projectChannelId must be a non-nil channel id");
    }

    this.state = projectState;
    this.projectId = projectState.projectId;
    this.projectChannelId = projectState.projectChannelId;
    this.pdo = new PDOClient(projectState.pdo, this.bridge);
  }

  execute(command, payload = {}, options = {}) {
    return this.mailboxClient.request(this.projectChannelId, command, payload, options);
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
    return this.#trackUnsubscribe(this.mailboxClient.subscribe(this.projectChannelId, command, handler));
  }

  subscribeAll(handler) {
    return this.#trackUnsubscribe(this.mailboxClient.subscribeAll(this.projectChannelId, handler));
  }

  dispose() {
    for (const unsubscribe of [...this.unsubscribers]) {
      unsubscribe();
    }
    this.unsubscribers.clear();
    this.mailboxClient.stop(this.projectChannelId);
  }

  #trackUnsubscribe(unsubscribe) {
    this.unsubscribers.add(unsubscribe);
    return () => {
      this.unsubscribers.delete(unsubscribe);
      unsubscribe();
    };
  }
}
