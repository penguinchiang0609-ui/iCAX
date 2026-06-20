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
    this.startedMailIds = new Set();
  }

  start(mailId) {
    this.#ensureMailId(mailId);
    if (this.startedMailIds.has(mailId)) {
      return;
    }

    this.bridge.subscribeMail(mailId, (mail) => {
      this.receive(mailId, mail);
    });
    this.startedMailIds.add(mailId);
  }

  send(mailId, command, payload = {}, options = {}) {
    this.#ensureMailId(mailId);
    if (!command) {
      throw new TypeError("command is required");
    }

    this.start(mailId);

    const requestId = this.#allocateRequestId();
    const timeoutMs = options.timeoutMs ?? this.timeoutMs;
    const requestMail = {
      id: requestId,
      originId: 0,
      command,
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
        mailId,
        requestMail,
      });
    });

    try {
      const postResult = this.bridge.postMail(mailId, requestMail);
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

  receive(_mailId, mail) {
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

  #ensureMailId(mailId) {
    if (!mailId) {
      throw new TypeError("mailId is required");
    }
  }
}
