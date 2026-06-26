export class HostBridgeAdapter {
  constructor(hostBridge) {
    if (!hostBridge) {
      throw new TypeError("hostBridge is required");
    }
    this.hostBridge = hostBridge;
  }

  async getApplicationChannelId() {
    return this.hostBridge.getApplicationChannelId();
  }

  postMail(channelId, mail) {
    return this.hostBridge.postMail
      ? this.hostBridge.postMail(channelId, mail)
      : this.hostBridge.sendMail(channelId, mail);
  }

  subscribeMail(channelId, handler) {
    if (!this.hostBridge.subscribeMail) {
      throw new Error("hostBridge.subscribeMail is required");
    }
    return this.hostBridge.subscribeMail(channelId, handler);
  }

  openFileDialog(options = {}) {
    if (!this.hostBridge.openFileDialog) {
      return Promise.resolve(null);
    }
    return this.hostBridge.openFileDialog(options);
  }

  onProjectFileOpen(handler) {
    if (!this.hostBridge.onProjectFileOpen) {
      return () => {};
    }
    return this.hostBridge.onProjectFileOpen(handler);
  }
}
