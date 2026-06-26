import { getCommandTypeCode } from "./commandNames.mjs";

const DEFAULT_TIMEOUT_MS = 15000;

export class MailboxError extends Error {
  constructor(message, mail = null) {
    super(message);
    this.name = "MailboxError";
    this.mail = mail;
  }
}

export class MailboxTimeoutError extends MailboxError {
  constructor(message, mail = null) {
    super(message, mail);
    this.name = "MailboxTimeoutError";
  }
}

export class MailboxClient {
  constructor(bridge, options = {}) {
    if (!bridge) {
      throw new TypeError("bridge is required");
    }

    this.bridge = bridge;
    this.timeoutMs = options.timeoutMs ?? DEFAULT_TIMEOUT_MS;
    this.nextRequestId = options.initialRequestId ?? 1;
    this.pending = new Map();
    this.startedChannelIds = new Set();
  }

  start(channelId) {
    this.#ensureChannelId(channelId);
    if (this.startedChannelIds.has(channelId)) {
      return;
    }

    this.bridge.subscribeMail(channelId, (mail) => {
      this.receive(channelId, mail);
    });
    this.startedChannelIds.add(channelId);
  }

  send(channelId, command, payload = {}, options = {}) {
    this.#ensureChannelId(channelId);
    if (!command) {
      throw new TypeError("command is required");
    }

    this.start(channelId);

    const requestId = this.#allocateRequestId();
    const timeoutMs = options.timeoutMs ?? this.timeoutMs;
    const requestMail = {
      id: requestId,
      originId: 0,
      command,
      typeCode: getCommandTypeCode(command),
      stamp: "Ok",
      payload,
    };

    const promise = new Promise((resolve, reject) => {
      const timeoutHandle = setTimeout(() => {
        this.pending.delete(requestId);
        reject(new MailboxTimeoutError(`Mailbox request timed out: ${command}`, requestMail));
      }, timeoutMs);

      this.pending.set(requestId, {
        resolve,
        reject,
        timeoutHandle,
        command,
        channelId,
        requestMail,
      });
    });

    try {
      const postResult = this.bridge.postMail(channelId, requestMail);
      if (postResult && typeof postResult.catch === "function") {
        postResult.catch((error) => {
          this.#rejectPending(requestId, error);
        });
      }
    } catch (error) {
      this.#rejectPending(requestId, error);
    }

    return promise;
  }

  receive(_channelId, mail) {
    if (!mail || mail.originId === undefined || mail.originId === null || mail.originId === 0) {
      return false;
    }

    const pending = this.pending.get(mail.originId);
    if (!pending) {
      return false;
    }

    clearTimeout(pending.timeoutHandle);
    this.pending.delete(mail.originId);

    if (mail.stamp && mail.stamp !== "Ok") {
      pending.reject(new MailboxError(`Mailbox response failed: ${mail.stamp}`, mail));
      return true;
    }

    pending.resolve(mail.payload);
    return true;
  }

  rejectAll(reason = "Mailbox client disposed") {
    for (const [requestId, pending] of this.pending) {
      clearTimeout(pending.timeoutHandle);
      pending.reject(new MailboxError(reason, pending.requestMail));
      this.pending.delete(requestId);
    }
  }

  #allocateRequestId() {
    const requestId = this.nextRequestId;
    this.nextRequestId += 1;
    return requestId;
  }

  #rejectPending(requestId, error) {
    const pending = this.pending.get(requestId);
    if (!pending) {
      return;
    }

    clearTimeout(pending.timeoutHandle);
    this.pending.delete(requestId);
    pending.reject(error);
  }

  #ensureChannelId(channelId) {
    if (!channelId) {
      throw new TypeError("channelId is required");
    }
  }
}
