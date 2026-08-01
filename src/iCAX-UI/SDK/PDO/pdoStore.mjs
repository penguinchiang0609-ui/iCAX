function decodeEventPayload(event) {
  if (event?.payload && typeof event.payload === "object") {
    return { ...event.payload };
  }
  const text = String(event?.raw?.payloadText ?? "").trim();
  if (!text) {
    return null;
  }
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

function normalizeTextFields(payload) {
  const result = { ...payload };
  for (const name of [
    "pdoId",
    "entityId",
    "objectId",
    "transformId",
    "cameraId",
    "geometryId",
    "sceneId",
    "slotVersion",
    "payloadCapacity",
  ]) {
    if (result[name] !== undefined && result[name] !== null) {
      result[name] = String(result[name]);
    }
  }
  return result;
}

/*
 * Scene 级 PDO 描述符当前状态仓库。
 * SDO Event 只负责把 slot 声明送进这里；具体 View 是否使用该 Entity 由消费方自行判断。
 */
export class PDOStore {
  constructor() {
    this.byPDOId = new Map();
    this.listeners = new Set();
  }

  ingestEvent(event) {
    const decoded = decodeEventPayload(event);
    if (!decoded?.pdoId) {
      return false;
    }
    const payload = normalizeTextFields(decoded);
    const pdoId = payload.pdoId;
    const eventName = String(payload.event ?? "");

    if (eventName === "SlotFreed") {
      const previous = this.byPDOId.get(pdoId) ?? null;
      this.byPDOId.delete(pdoId);
      this.#emit({
        kind: "removed",
        descriptor: { ...(previous ?? {}), ...payload },
        previous,
        event,
      });
      return true;
    }

    const previous = this.byPDOId.get(pdoId) ?? null;
    const eventEntityIds = collectEntityIds(payload);
    const descriptor = Object.freeze({
      ...(previous ?? {}),
      ...payload,
      pdoId,
      id: pdoId,
      type: String(payload.payloadKind ?? previous?.type ?? ""),
      entityIds: Object.freeze(
        eventEntityIds.length
          ? eventEntityIds
          : [...(previous?.entityIds ?? [])],
      ),
    });
    this.byPDOId.set(pdoId, descriptor);
    this.#emit({
      kind: previous ? "updated" : "added",
      descriptor,
      previous,
      event,
    });
    return true;
  }

  upsert(descriptor) {
    const pdoId = String(descriptor?.pdoId ?? descriptor?.id ?? "").trim();
    if (!pdoId) {
      throw new TypeError("PDO descriptor id is required");
    }
    const previous = this.byPDOId.get(pdoId) ?? null;
    const normalized = Object.freeze({
      ...(previous ?? {}),
      ...descriptor,
      pdoId,
      id: pdoId,
      type: String(descriptor.type ?? previous?.type ?? ""),
      entityIds: Object.freeze([
        ...(descriptor.entityIds ?? previous?.entityIds ?? []),
      ].map(String)),
    });
    this.byPDOId.set(pdoId, normalized);
    this.#emit({
      kind: previous ? "updated" : "added",
      descriptor: normalized,
      previous,
      event: null,
    });
    return normalized;
  }

  remove(pdoId) {
    const key = String(pdoId ?? "").trim();
    const previous = this.byPDOId.get(key) ?? null;
    if (!previous) {
      return false;
    }
    this.byPDOId.delete(key);
    this.#emit({ kind: "removed", descriptor: previous, event: null });
    return true;
  }

  getByPDOId(pdoId) {
    return this.byPDOId.get(String(pdoId ?? "")) ?? null;
  }

  list(options = {}) {
    const type = options.type == null ? null : String(options.type);
    const entityId = options.entityId == null ? null : String(options.entityId);
    return [...this.byPDOId.values()].filter((descriptor) =>
      (type === null || descriptor.type === type)
      && (entityId === null || descriptor.entityIds.includes(entityId)));
  }

  subscribe(handler, options = {}) {
    if (typeof handler !== "function") {
      throw new TypeError("PDO store subscriber must be a function");
    }
    const listener = {
      handler,
      type: options.type == null ? null : String(options.type),
      entityId: options.entityId == null ? null : String(options.entityId),
    };
    this.listeners.add(listener);
    if (options.emitCurrent) {
      for (const descriptor of this.list(listener)) {
        handler({ kind: "current", descriptor, previous: null, event: null });
      }
    }
    return () => this.listeners.delete(listener);
  }

  clear() {
    this.byPDOId.clear();
  }

  dispose() {
    this.clear();
    this.listeners.clear();
  }

  #emit(change) {
    for (const listener of [...this.listeners]) {
      if (listener.type !== null && change.descriptor?.type !== listener.type) {
        continue;
      }
      if (listener.entityId !== null
        && !change.descriptor?.entityIds?.includes(listener.entityId)) {
        continue;
      }
      listener.handler(change);
    }
  }
}

function collectEntityIds(payload) {
  const result = [];
  for (const name of ["entityId", "objectId", "transformId", "cameraId"]) {
    const value = String(payload[name] ?? "").trim();
    if (value && value !== "0" && !result.includes(value)) {
      result.push(value);
    }
  }
  return result;
}
