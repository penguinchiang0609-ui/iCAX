import { ensureUsableChannelId } from "./channelId.mjs";
import { makeCommandTypeCodeFromCommand } from "./commandRoute.mjs";
import { deserializeVariantText, serializeVariantText } from "./variantSerializer.mjs";

const DEFAULT_TIMEOUT_MS = 15000;
const STAMP_OK = 0;

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
    this.channelUnsubscribers = new Map();
    this.eventHandlers = new Map();
  }

  start(channelId) {
    ensureChannelId(channelId);
    if (this.startedChannelIds.has(channelId)) {
      return;
    }

    const unsubscribe = this.bridge.subscribeMail(channelId, (mail) => this.receive(channelId, mail));
    if (typeof unsubscribe !== "function") {
      throw new TypeError("bridge.subscribeMail must return an unsubscribe function");
    }
    this.channelUnsubscribers.set(channelId, unsubscribe);
    this.startedChannelIds.add(channelId);
  }

  stop(channelId, reason = "Mailbox channel stopped") {
    ensureChannelId(channelId);

    const unsubscribe = this.channelUnsubscribers.get(channelId);
    if (unsubscribe) {
      unsubscribe();
    }
    this.channelUnsubscribers.delete(channelId);
    this.startedChannelIds.delete(channelId);

    for (const key of [...this.eventHandlers.keys()]) {
      if (key.startsWith(`${channelId}:`)) {
        this.eventHandlers.delete(key);
      }
    }

    for (const [requestId, pending] of [...this.pending]) {
      if (pending.requestMail.channelId === channelId) {
        clearTimeout(pending.timeoutHandle);
        pending.reject(new MailboxError(reason, pending.requestMail));
        this.pending.delete(requestId);
      }
    }
  }

  dispose(reason = "Mailbox client disposed") {
    for (const channelId of [...this.startedChannelIds]) {
      this.stop(channelId, reason);
    }
    this.rejectAll(reason);
    this.eventHandlers.clear();
  }

  request(channelId, command, payload = {}, options = {}) {
    ensureChannelId(channelId);
    if (!command) {
      throw new TypeError("command is required");
    }

    this.start(channelId);

    const requestId = this.#allocateRequestId();
    const requestMail = {
      channelId,
      id: requestId,
      originId: 0,
      typeCode: makeCommandTypeCodeFromCommand(command),
      stamp: STAMP_OK,
      payloadText: serializeVariantText(payload),
    };

    const timeoutMs = options.timeoutMs ?? this.timeoutMs;
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
      throw new TypeError("Mailbox timeoutMs must be a positive finite number");
    }

    const promise = new Promise((resolve, reject) => {
      const timeoutHandle = setTimeout(() => {
        this.pending.delete(requestId);
        reject(new MailboxTimeoutError(`Mailbox request timed out: ${command}`, requestMail));
      }, timeoutMs);

      this.pending.set(requestId, {
        command,
        requestMail,
        resolve,
        reject,
        timeoutHandle,
      });
    });

    try {
      const postResult = this.bridge.postMail(requestMail);
      if (postResult && typeof postResult.catch === "function") {
        postResult.catch((error) => this.#rejectPending(requestId, error));
      }
    } catch (error) {
      this.#rejectPending(requestId, error);
    }

    return promise;
  }

  subscribe(channelId, command, handler) {
    ensureChannelId(channelId);
    if (!command) {
      throw new TypeError("command is required");
    }
    if (typeof handler !== "function") {
      throw new TypeError("handler must be a function");
    }

    this.start(channelId);

    const key = this.#makeEventKey(channelId, makeCommandTypeCodeFromCommand(command));
    if (!this.eventHandlers.has(key)) {
      this.eventHandlers.set(key, new Set());
    }

    const handlers = this.eventHandlers.get(key);
    handlers.add(handler);
    return () => {
      handlers.delete(handler);
      if (handlers.size === 0) {
        this.eventHandlers.delete(key);
      }
    };
  }

  subscribeAll(channelId, handler) {
    ensureChannelId(channelId);
    if (typeof handler !== "function") {
      throw new TypeError("handler must be a function");
    }

    this.start(channelId);

    const key = this.#makeEventKey(channelId, "*");
    if (!this.eventHandlers.has(key)) {
      this.eventHandlers.set(key, new Set());
    }

    const handlers = this.eventHandlers.get(key);
    handlers.add(handler);
    return () => {
      handlers.delete(handler);
      if (handlers.size === 0) {
        this.eventHandlers.delete(key);
      }
    };
  }

  receive(channelId, mail) {
    if (!mail || mail.originId === undefined || mail.originId === null || Number(mail.originId) === 0) {
      return this.#dispatchEventMail(channelId, mail);
    }

    const pending = this.pending.get(Number(mail.originId));
    if (!pending) {
      return false;
    }

    clearTimeout(pending.timeoutHandle);
    this.pending.delete(Number(mail.originId));

    const stamp = Number(mail.stamp ?? 0);
    if (stamp !== STAMP_OK) {
      const responseText = String(mail.payloadText ?? "").trim();
      pending.reject(new MailboxError(responseText || `Mailbox response failed: ${stamp}`, mail));
      return true;
    }

    try {
      pending.resolve(deserializeVariantText(mail.payloadText ?? ""));
    } catch (error) {
      pending.reject(error);
    }
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

  #dispatchEventMail(channelId, mail) {
    if (!mail) {
      return false;
    }

    const typeCode = String(mail.typeCode ?? "");
    if (!typeCode) {
      return false;
    }

    const typedHandlers = this.eventHandlers.get(this.#makeEventKey(channelId, typeCode)) ?? new Set();
    const wildcardHandlers = this.eventHandlers.get(this.#makeEventKey(channelId, "*")) ?? new Set();
    if (typedHandlers.size === 0 && wildcardHandlers.size === 0) {
      return false;
    }

    const event = this.#makeEvent(channelId, mail);
    for (const handler of [...typedHandlers, ...wildcardHandlers]) {
      handler(event);
    }
    return true;
  }

  #makeEvent(channelId, mail) {
    const stamp = Number(mail.stamp ?? 0);
    let payload = null;
    let deserializeError = null;
    try {
      payload = deserializeVariantText(mail.payloadText ?? "");
    } catch (error) {
      deserializeError = error;
    }

    return {
      channelId,
      id: Number(mail.id ?? 0),
      originId: Number(mail.originId ?? 0),
      typeCode: String(mail.typeCode ?? ""),
      stamp,
      ok: stamp === STAMP_OK && !deserializeError,
      payload,
      error: deserializeError,
      raw: mail,
    };
  }

  #makeEventKey(channelId, typeCode) {
    return `${channelId}:${typeCode}`;
  }
}

function ensureChannelId(channelId) {
  ensureUsableChannelId(channelId);
}
