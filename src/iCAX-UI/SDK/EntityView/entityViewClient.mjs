import { parseEntityViewPDO } from "./entityViewPDO.mjs";

const DEFAULT_POLL_INTERVAL_MS = 100;
/*
 * 每次 start 都返回一个独立的前端 reader。
 * reader 只读取公开 PDO 端口；start/stop 分别取得和释放一次后端 EntityView 使用权。
 */
export class EntityViewClient {
  constructor(sceneProxy, options = {}) {
    if (!sceneProxy) {
      throw new TypeError("EntityViewClient requires a SceneProxy");
    }
    this.sceneProxy = sceneProxy;
    this.defaultPollIntervalMs =
      normalizePollInterval(options.pollIntervalMs ?? DEFAULT_POLL_INTERVAL_MS);
    this.readers = new Set();
    this.disposed = false;
  }

  async start(query, options = {}) {
    if (this.disposed) {
      throw new Error("EntityViewClient is disposed");
    }

    const request = normalizeQuery(query);
    const handle = await this.sceneProxy.invoke("EntityView.GetOrCreate", request, {
      timeoutMs: options.timeoutMs ?? 10000,
    });
    if (!handle?.viewId || !handle?.pdo?.id) {
      throw new Error("EntityView.GetOrCreate returned an invalid handle");
    }

    const state = {
      handle,
      snapshot: null,
      listener: typeof options.onChange === "function"
        ? options.onChange
        : null,
      pollPromise: null,
      timer: null,
      closed: false,
    };
    const reader = {
      get viewId() {
        return handle.viewId;
      },
      get snapshot() {
        return state.snapshot;
      },
      poll: () => this.#poll(state),
      stop: () => this.#stop(reader, state),
    };

    this.readers.add(reader);
    state.timer = setInterval(
      () => void this.#poll(state),
      normalizePollInterval(
        options.pollIntervalMs ?? this.defaultPollIntervalMs,
      ),
    );
    void this.#poll(state);
    return Object.freeze(reader);
  }

  async dispose() {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    await Promise.allSettled(
      [...this.readers].map((reader) => reader.stop()),
    );
  }

  async #poll(state) {
    if (state.closed) {
      return state.snapshot;
    }
    if (state.pollPromise) {
      return state.pollPromise;
    }
    state.pollPromise = this.sceneProxy.pdo
      .withReadDescriptor(state.handle.pdo, (buffer) => {
        const snapshot = parseEntityViewPDO(buffer);
        if (snapshot.revision === "0"
          || snapshot.revision === state.snapshot?.revision) {
          return state.snapshot;
        }
        state.snapshot = snapshot;
        try {
          state.listener?.(snapshot);
        } catch (error) {
          console.error("EntityView subscriber failed", error);
        }
        return snapshot;
      })
      .catch(() => state.snapshot)
      .finally(() => {
        state.pollPromise = null;
      });
    return state.pollPromise;
  }

  async #stop(reader, state) {
    if (state.closed) {
      return false;
    }
    state.closed = true;
    clearInterval(state.timer);
    state.timer = null;
    this.readers.delete(reader);
    const response = await this.sceneProxy.invoke("EntityView.Release", {
      viewId: state.handle.viewId,
    }, { timeoutMs: 10000 });
    return Boolean(response?.released);
  }
}

function normalizeQuery(query) {
  const where = String(query?.where ?? "").trim();
  if (!where) {
    throw new TypeError("EntityView query.where is required");
  }
  const language = String(query.language ?? "sql").trim().toLowerCase();
  if (language !== "sql" && language !== "lambda") {
    throw new TypeError("EntityView query.language must be sql or lambda");
  }
  return {
    language,
    where,
    parameters: query.parameters && typeof query.parameters === "object"
      ? query.parameters
      : {},
  };
}

function normalizePollInterval(value) {
  const milliseconds = Number(value);
  if (!Number.isFinite(milliseconds) || milliseconds < 16) {
    throw new TypeError("EntityView poll interval must be at least 16ms");
  }
  return Math.floor(milliseconds);
}
