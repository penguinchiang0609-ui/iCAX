import { ensureUsableChannelId } from "./channelId.mjs";
import { makeFacadeMethodCodeFromName } from "./facadeMethod.mjs";
import { deserializeVariantText, serializeVariantText } from "./variantSerializer.mjs";

const DEFAULT_TIMEOUT_MS = 15000;

export const FacadeFrameKind = Object.freeze({
  Request: 0,
  Report: 1,
  Response: 2,
  Event: 3,
});

export const InvocationStatus = Object.freeze({
  Ok: 0,
  FacadeNotFound: 1,
  MethodNotFound: 2,
  InvalidInvocation: 3,
});

export class FacadeError extends Error {
  constructor(message, frame = null) {
    super(message);
    this.name = "FacadeError";
    this.frame = frame;
  }
}

export class FacadeTimeoutError extends FacadeError {
  constructor(message, frame = null) {
    super(message, frame);
    this.name = "FacadeTimeoutError";
  }
}

export class FacadeClient {
  constructor(bridge, options = {}) {
    if (!bridge) {
      throw new TypeError("bridge is required");
    }

    this.bridge = bridge;
    this.timeoutMs = options.timeoutMs ?? DEFAULT_TIMEOUT_MS;
    this.nextCallId = options.initialCallId ?? 1;
    this.pending = new Map();
    this.startedChannelIds = new Set();
    this.channelUnsubscribers = new Map();
    this.eventHandlers = new Map();
    this.exposedMethods = new Map();
  }

  start(channelId) {
    ensureUsableChannelId(channelId);
    if (this.startedChannelIds.has(channelId)) {
      return;
    }

    const unsubscribe = this.bridge.subscribeFacadeFrames(
      channelId,
      (frame) => this.receive(channelId, frame),
    );
    if (typeof unsubscribe !== "function") {
      throw new TypeError("bridge.subscribeFacadeFrames must return an unsubscribe function");
    }
    this.channelUnsubscribers.set(channelId, unsubscribe);
    this.startedChannelIds.add(channelId);
  }

  stop(channelId, reason = "Facade channel stopped") {
    ensureUsableChannelId(channelId);

    this.channelUnsubscribers.get(channelId)?.();
    this.channelUnsubscribers.delete(channelId);
    this.startedChannelIds.delete(channelId);

    this.#deleteChannelEntries(this.eventHandlers, channelId);
    this.#deleteChannelEntries(this.exposedMethods, channelId);

    for (const [callKey, pending] of [...this.pending]) {
      if (pending.requestFrame.channelId === channelId) {
        clearTimeout(pending.timeoutHandle);
        pending.reject(new FacadeError(reason, pending.requestFrame));
        this.pending.delete(callKey);
      }
    }
  }

  dispose(reason = "Facade client disposed") {
    for (const channelId of [...this.startedChannelIds]) {
      this.stop(channelId, reason);
    }
    this.rejectAll(reason);
    this.eventHandlers.clear();
    this.exposedMethods.clear();
  }

  invoke(channelId, facadeMethod, payload = {}, options = {}) {
    ensureUsableChannelId(channelId);
    if (!facadeMethod) {
      throw new TypeError("facadeMethod is required");
    }
    this.start(channelId);

    const callId = this.#allocateCallId();
    const requestFrame = {
      channelId,
      callId,
      methodCode: makeFacadeMethodCodeFromName(facadeMethod),
      kind: FacadeFrameKind.Request,
      status: InvocationStatus.Ok,
      payloadText: serializeVariantText(payload),
    };

    const timeoutMs = options.timeoutMs ?? this.timeoutMs;
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
      throw new TypeError("Facade timeoutMs must be a positive finite number");
    }
    if (options.onReport !== undefined && typeof options.onReport !== "function") {
      throw new TypeError("Facade onReport must be a function");
    }

    const reportHandlers = new Set(options.onReport ? [options.onReport] : []);
    let resolvePromise;
    let rejectPromise;
    let latestReport = null;
    const promise = new Promise((resolve, reject) => {
      resolvePromise = resolve;
      rejectPromise = reject;
    });
    Object.defineProperties(promise, {
      callId: { value: callId, enumerable: true },
      onReport: {
        value: (handler) => {
          if (typeof handler !== "function") {
            throw new TypeError("Facade report handler must be a function");
          }
          reportHandlers.add(handler);
          if (latestReport) {
            handler(latestReport);
          }
          return () => reportHandlers.delete(handler);
        },
      },
      getLatestReport: { value: () => latestReport },
    });

    const callKey = this.#callKey(callId);
    const pending = {
      facadeMethod,
      requestFrame,
      resolve: resolvePromise,
      reject: rejectPromise,
      timeoutMs,
      timeoutHandle: null,
      reportHandlers,
      setLatestReport: (report) => { latestReport = report; },
    };
    this.pending.set(callKey, pending);
    this.#armPendingTimeout(callKey, pending);
    this.#postFrame(requestFrame).catch((error) => this.#rejectPending(callKey, error));
    return promise;
  }

  expose(channelId, facadeMethod, handler) {
    ensureUsableChannelId(channelId);
    if (!facadeMethod) {
      throw new TypeError("facadeMethod is required");
    }
    if (typeof handler !== "function") {
      throw new TypeError("Facade handler must be a function");
    }

    this.start(channelId);
    const key = this.#memberKey(channelId, makeFacadeMethodCodeFromName(facadeMethod));
    if (this.exposedMethods.has(key)) {
      throw new Error(`Facade method is already exposed: ${facadeMethod}`);
    }
    this.exposedMethods.set(key, { facadeMethod, handler });
    return () => this.exposedMethods.delete(key);
  }

  subscribe(channelId, facadeMember, handler) {
    ensureUsableChannelId(channelId);
    if (!facadeMember) {
      throw new TypeError("facadeMember is required");
    }
    if (typeof handler !== "function") {
      throw new TypeError("handler must be a function");
    }
    this.start(channelId);
    return this.#addEventHandler(
      this.#memberKey(channelId, makeFacadeMethodCodeFromName(facadeMember)),
      handler,
    );
  }

  subscribeAll(channelId, handler) {
    ensureUsableChannelId(channelId);
    if (typeof handler !== "function") {
      throw new TypeError("handler must be a function");
    }
    this.start(channelId);
    return this.#addEventHandler(this.#memberKey(channelId, "*"), handler);
  }

  receive(channelId, frame) {
    if (!frame) {
      return false;
    }

    const kind = Number(frame.kind);
    if (kind === FacadeFrameKind.Request) {
      void this.#dispatchRequest(channelId, frame);
      return true;
    }
    if (kind === FacadeFrameKind.Event) {
      return this.#dispatchEvent(channelId, frame);
    }
    if (kind !== FacadeFrameKind.Report && kind !== FacadeFrameKind.Response) {
      return false;
    }

    const callKey = this.#callKey(frame.callId);
    const pending = this.pending.get(callKey);
    if (!pending) {
      return false;
    }

    if (kind === FacadeFrameKind.Report) {
      this.#armPendingTimeout(callKey, pending);
      const report = this.#makeEvent(channelId, frame);
      pending.setLatestReport(report);
      for (const handler of [...pending.reportHandlers]) {
        try {
          handler(report);
        } catch (error) {
          console.error("Facade report handler failed", error);
        }
      }
      return true;
    }

    clearTimeout(pending.timeoutHandle);
    this.pending.delete(callKey);
    const status = Number(frame.status ?? InvocationStatus.Ok);
    if (status !== InvocationStatus.Ok) {
      const responseText = String(frame.payloadText ?? "").trim();
      pending.reject(new FacadeError(responseText || `Facade response failed: ${status}`, frame));
      return true;
    }

    try {
      pending.resolve(deserializeVariantText(frame.payloadText ?? ""));
    } catch (error) {
      pending.reject(error);
    }
    return true;
  }

  rejectAll(reason = "Facade client disposed") {
    for (const [callKey, pending] of this.pending) {
      clearTimeout(pending.timeoutHandle);
      pending.reject(new FacadeError(reason, pending.requestFrame));
      this.pending.delete(callKey);
    }
  }

  async #dispatchRequest(channelId, frame) {
    const methodCode = String(frame.methodCode ?? "");
    const exposed = this.exposedMethods.get(this.#memberKey(channelId, methodCode));
    if (!exposed) {
      await this.#postFrame({
        ...frame,
        channelId,
        kind: FacadeFrameKind.Response,
        status: InvocationStatus.MethodNotFound,
        payloadText: `Frontend Facade method not found: ${methodCode}`,
      });
      return;
    }

    const report = async (payload) => this.#postFrame({
      channelId,
      callId: frame.callId,
      methodCode: frame.methodCode,
      kind: FacadeFrameKind.Report,
      status: InvocationStatus.Ok,
      payloadText: serializeVariantText(payload),
    });

    try {
      const payload = deserializeVariantText(frame.payloadText ?? "");
      const result = await exposed.handler(payload, {
        channelId,
        callId: frame.callId,
        methodCode,
        facadeMethod: exposed.facadeMethod,
        report,
      });
      await this.#postFrame({
        channelId,
        callId: frame.callId,
        methodCode: frame.methodCode,
        kind: FacadeFrameKind.Response,
        status: InvocationStatus.Ok,
        payloadText: serializeVariantText(result ?? {}),
      });
    } catch (error) {
      await this.#postFrame({
        channelId,
        callId: frame.callId,
        methodCode: frame.methodCode,
        kind: FacadeFrameKind.Response,
        status: InvocationStatus.InvalidInvocation,
        payloadText: String(error?.message ?? error),
      });
    }
  }

  #dispatchEvent(channelId, frame) {
    const methodCode = String(frame.methodCode ?? "");
    if (!methodCode) {
      return false;
    }

    const typed = this.eventHandlers.get(this.#memberKey(channelId, methodCode)) ?? new Set();
    const wildcard = this.eventHandlers.get(this.#memberKey(channelId, "*")) ?? new Set();
    if (typed.size === 0 && wildcard.size === 0) {
      return false;
    }

    const event = this.#makeEvent(channelId, frame);
    for (const handler of [...typed, ...wildcard]) {
      handler(event);
    }
    return true;
  }

  #makeEvent(channelId, frame) {
    const status = Number(frame.status ?? InvocationStatus.Ok);
    let payload = null;
    let deserializeError = null;
    try {
      payload = deserializeVariantText(frame.payloadText ?? "");
    } catch (error) {
      deserializeError = error;
    }
    return {
      channelId,
      callId: frame.callId,
      methodCode: String(frame.methodCode ?? ""),
      kind: Number(frame.kind),
      status,
      ok: status === InvocationStatus.Ok && !deserializeError,
      payload,
      error: deserializeError,
      raw: frame,
    };
  }

  #addEventHandler(key, handler) {
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

  #allocateCallId() {
    const callId = this.nextCallId;
    this.nextCallId += 1;
    return callId;
  }

  #armPendingTimeout(callKey, pending) {
    clearTimeout(pending.timeoutHandle);
    pending.timeoutHandle = setTimeout(() => {
      if (this.pending.get(callKey) !== pending) {
        return;
      }
      this.pending.delete(callKey);
      pending.reject(new FacadeTimeoutError(
        `Facade invocation timed out: ${pending.facadeMethod}`,
        pending.requestFrame,
      ));
    }, pending.timeoutMs);
  }

  #rejectPending(callKey, error) {
    const pending = this.pending.get(callKey);
    if (!pending) {
      return;
    }
    clearTimeout(pending.timeoutHandle);
    this.pending.delete(callKey);
    pending.reject(error);
  }

  async #postFrame(frame) {
    await this.bridge.postFacadeFrame(frame);
  }

  #callKey(callId) {
    return String(callId ?? "");
  }

  #memberKey(channelId, methodCode) {
    return `${channelId}:${methodCode}`;
  }

  #deleteChannelEntries(map, channelId) {
    for (const key of [...map.keys()]) {
      if (key.startsWith(`${channelId}:`)) {
        map.delete(key);
      }
    }
  }
}
