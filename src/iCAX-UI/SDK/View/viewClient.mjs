import { ViewSDO } from "../SDO/sdoMethod.mjs";
import { parseViewResource } from "./viewResource.mjs";

const DEFAULT_POLL_INTERVAL_MS = 100;

/*
 * 一个 reader 持有后端 View 的一次引用。View 是 Scene 内的动态投影对象；
 * 创建和释放走 Scene channel，快照数据只从资源池读取。
 */
export class ViewClient {
  constructor(sceneProxy, options = {}) {
    if (!sceneProxy) {
      throw new TypeError("ViewClient requires a SceneProxy");
    }
    this.sceneProxy = sceneProxy;
    this.defaultPollIntervalMs = normalizePollInterval(
      options.pollIntervalMs ?? DEFAULT_POLL_INTERVAL_MS,
    );
    this.readers = new Set();
    this.disposed = false;
  }

  async start(definition, options = {}) {
    if (this.disposed) {
      throw new Error("ViewClient is disposed");
    }

    const request = normalizeDefinition(definition);
    const handle = await this.sceneProxy.invoke(ViewSDO.getOrCreate, request, {
      timeoutMs: options.timeoutMs ?? 10000,
    });
    if (!handle?.viewId || !handle?.resource?.url) {
      throw new Error("View.GetOrCreate returned an invalid handle");
    }

    const state = {
      handle,
      snapshot: null,
      listener: typeof options.onChange === "function" ? options.onChange : null,
      waiters: new Set(),
      pollPromise: null,
      timer: null,
      closed: false,
      lastResourceVersion: "0",
    };
    const reader = {
      get viewId() {
        return handle.viewId;
      },
      get snapshot() {
        return state.snapshot;
      },
      poll: () => this.#poll(state),
      waitForSnapshot: (predicate, waitOptions = {}) =>
        this.#waitForSnapshot(state, predicate, waitOptions),
      stop: () => this.#stop(reader, state),
    };

    this.readers.add(reader);
    state.timer = setInterval(
      () => void this.#poll(state).catch((error) => {
        console.error("View polling failed", error);
      }),
      normalizePollInterval(options.pollIntervalMs ?? this.defaultPollIntervalMs),
    );
    void this.#poll(state).catch((error) => {
      console.error("Initial View poll failed", error);
    });
    return Object.freeze(reader);
  }

  async dispose() {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    await Promise.allSettled([...this.readers].map((reader) => reader.stop()));
  }

  async #poll(state) {
    if (state.closed) {
      return state.snapshot;
    }
    if (state.pollPromise) {
      return state.pollPromise;
    }
    state.pollPromise = this.#readResourceSnapshot(state)
      .finally(() => {
        state.pollPromise = null;
      });
    return state.pollPromise;
  }

  async #readResourceSnapshot(state) {
    const { url } = state.handle.resource;
    const headers = new Headers({
      Accept: "application/vnd.icax.flatbuffer",
    });
    const headResponse = await this.sceneProxy.resources.head(url, { headers });
    if (!headResponse.ok) {
      throw new Error(`View resource HEAD failed: ${headResponse.status}`);
    }
    const latestVersion = String(
      headResponse.headers.get("ICAX-Resource-Version")
        ?? state.handle.resource.version
        ?? "0",
    );
    if (latestVersion === "0" || latestVersion === state.lastResourceVersion) {
      return state.snapshot;
    }

    headers.set("ICAX-Resource-Version", latestVersion);
    const response = await this.sceneProxy.resources.get(url, { headers });
    if (!response.ok) {
      throw new Error(`View resource GET failed: ${response.status}`);
    }
    const snapshot = parseViewResource(await response.arrayBuffer());
    if (snapshot.revision === "0"
        || snapshot.revision === state.snapshot?.revision) {
      state.lastResourceVersion = latestVersion;
      return state.snapshot;
    }
    state.lastResourceVersion = latestVersion;
    state.snapshot = snapshot;
    this.#resolveSnapshotWaiters(state, snapshot);
    try {
      Promise.resolve(state.listener?.(snapshot)).catch((error) => {
        console.error("View subscriber failed", error);
      });
    } catch (error) {
      console.error("View subscriber failed", error);
    }
    return snapshot;
  }

  #waitForSnapshot(state, predicate, options = {}) {
    if (typeof predicate !== "function") {
      throw new TypeError("waitForSnapshot requires a predicate");
    }
    if (state.closed) {
      return Promise.reject(new Error("View reader is closed"));
    }
    if (matchesSnapshot(predicate, state.snapshot)) {
      return Promise.resolve(state.snapshot);
    }

    const signal = options.signal ?? null;
    if (signal?.aborted) {
      return Promise.reject(signal.reason ?? new DOMException("Aborted", "AbortError"));
    }

    return new Promise((resolve, reject) => {
      const waiter = { predicate, resolve, reject, signal, abort: null };
      waiter.abort = () => {
        state.waiters.delete(waiter);
        reject(signal?.reason ?? new DOMException("Aborted", "AbortError"));
      };
      signal?.addEventListener?.("abort", waiter.abort, { once: true });
      state.waiters.add(waiter);

      void this.#poll(state).catch((error) => {
        if (!state.waiters.delete(waiter)) {
          return;
        }
        signal?.removeEventListener?.("abort", waiter.abort);
        reject(error);
      });
    });
  }

  #resolveSnapshotWaiters(state, snapshot) {
    for (const waiter of [...state.waiters]) {
      let matched = false;
      try {
        matched = matchesSnapshot(waiter.predicate, snapshot);
      } catch (error) {
        state.waiters.delete(waiter);
        waiter.signal?.removeEventListener?.("abort", waiter.abort);
        waiter.reject(error);
        continue;
      }
      if (!matched) {
        continue;
      }
      state.waiters.delete(waiter);
      waiter.signal?.removeEventListener?.("abort", waiter.abort);
      waiter.resolve(snapshot);
    }
  }

  async #stop(reader, state) {
    if (state.closed) {
      return false;
    }
    state.closed = true;
    clearInterval(state.timer);
    state.timer = null;
    const closedError = new Error("View reader is closed");
    for (const waiter of state.waiters) {
      waiter.signal?.removeEventListener?.("abort", waiter.abort);
      waiter.reject(closedError);
    }
    state.waiters.clear();
    this.readers.delete(reader);
    const response = await this.sceneProxy.invoke(
      ViewSDO.release,
      { viewId: state.handle.viewId },
      { timeoutMs: 10000 },
    );
    return Boolean(response?.released);
  }
}

function matchesSnapshot(predicate, snapshot) {
  if (!snapshot) {
    return false;
  }
  return Boolean(predicate(snapshot));
}

function normalizeDefinition(definition) {
  if (!Array.isArray(definition?.sources) || definition.sources.length === 0) {
    throw new TypeError("View definition.sources must be a non-empty array");
  }
  const sources = definition.sources.map((source) => {
    const sourceId = String(source?.sourceId ?? "").trim();
    const where = String(source?.where ?? "").trim();
    const language = String(source?.language ?? "sql").trim().toLowerCase();
    if (!sourceId || !where) {
      throw new TypeError("View sources require sourceId and where");
    }
    if (language !== "sql" && language !== "lambda") {
      throw new TypeError("View source language must be sql or lambda");
    }
    const projection = Array.isArray(source.projection)
      ? source.projection.map(normalizeProjectionField)
      : [];
    return {
      sourceId,
      role: String(source.role ?? "").trim(),
      language,
      where,
      parameters: source.parameters && typeof source.parameters === "object"
        ? source.parameters
        : {},
      projection,
    };
  });
  if (new Set(sources.map(({ sourceId }) => sourceId)).size !== sources.length) {
    throw new TypeError("View sourceId values must be unique");
  }
  return { sources };
}

function normalizeProjectionField(field) {
  const normalized = {
    alias: String(field?.alias ?? "").trim(),
    component: String(field?.component ?? "").trim(),
    property: String(field?.property ?? "").trim(),
    resourceReference: Boolean(field?.resourceReference),
    resourceVersionProperty: String(field?.resourceVersionProperty ?? "").trim(),
  };
  if (!normalized.alias || !normalized.component || !normalized.property) {
    throw new TypeError(
      "View projection fields require alias, component, and property",
    );
  }
  if (normalized.resourceVersionProperty && !normalized.resourceReference) {
    throw new TypeError(
      "View projection resourceVersionProperty requires resourceReference",
    );
  }
  return normalized;
}

function normalizePollInterval(value) {
  const milliseconds = Number(value);
  if (!Number.isFinite(milliseconds) || milliseconds < 16) {
    throw new TypeError("View poll interval must be at least 16ms");
  }
  return Math.floor(milliseconds);
}
