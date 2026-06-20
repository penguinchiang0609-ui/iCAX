export class HostBridgeAdapter {
  constructor(hostBridge) {
    if (!hostBridge) {
      throw new TypeError("hostBridge is required");
    }
    this.hostBridge = hostBridge;
  }

  async getApplicationMailId() {
    return this.hostBridge.getApplicationMailId();
  }

  postMail(mailId, mail) {
    return this.hostBridge.postMail
      ? this.hostBridge.postMail(mailId, mail)
      : this.hostBridge.sendMail(mailId, mail);
  }

  subscribeMail(mailId, handler) {
    if (!this.hostBridge.subscribeMail) {
      throw new Error("hostBridge.subscribeMail is required");
    }
    return this.hostBridge.subscribeMail(mailId, handler);
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
