const VIEW_IDENTIFIER = "ICVW";

export function parseViewResource(buffer) {
  if (!(buffer instanceof ArrayBuffer)) {
    throw new TypeError("View resource must be an ArrayBuffer");
  }
  if (buffer.byteLength < 12) {
    throw new Error("View resource is smaller than a FlatBuffer root");
  }

  const view = new DataView(buffer);
  const identifier = String.fromCharCode(
    view.getUint8(4),
    view.getUint8(5),
    view.getUint8(6),
    view.getUint8(7),
  );
  if (identifier !== VIEW_IDENTIFIER) {
    throw new Error(`Invalid View resource identifier: ${identifier}`);
  }

  const root = readOffset(view, 0, "root");
  const schemaVersion = readUint32Field(view, root, 0, 2);
  if (schemaVersion !== 2) {
    throw new Error(`Unsupported View schema version: ${schemaVersion}`);
  }
  const viewId = readStringField(view, root, 1) ?? "";
  const revision = readUint64FieldText(view, root, 2, "0");
  const sources = readTableVector(view, root, 3).map((table) => Object.freeze({
    sourceId: readStringField(view, table, 0) ?? "",
    role: readStringField(view, table, 1) ?? "",
  }));
  const rows = readTableVector(view, root, 4).map((table) => {
    const components = {};
    const data = {};
    for (const componentTable of readTableVector(view, table, 2)) {
      const componentClass = readStringField(view, componentTable, 0) ?? "";
      const present = readBoolField(view, componentTable, 1, false);
      const enabled = readBoolField(view, componentTable, 2, false);
      const dataJson = readStringField(view, componentTable, 3) ?? "";
      const componentData = dataJson
        ? (unwrapVariant(JSON.parse(dataJson)) ?? {})
        : {};
      const component = Object.freeze({
        componentClass,
        present,
        enabled,
        data: Object.freeze(componentData),
      });
      if (componentClass) {
        components[componentClass] = component;
      }
      if (present) {
        Object.assign(data, componentData);
      }
    }
    return Object.freeze({
      entityId: readStringField(view, table, 0) ?? "",
      sourceIds: Object.freeze(readStringVector(view, table, 1)),
      components: Object.freeze(components),
      data: Object.freeze(data),
    });
  });

  return Object.freeze({
    format: VIEW_IDENTIFIER,
    schemaVersion,
    viewId,
    revision,
    sources: Object.freeze(sources),
    count: rows.length,
    entityIds: Object.freeze(rows.map(({ entityId }) => entityId)),
    rows: Object.freeze(rows),
  });
}

export function unwrapVariant(value) {
  if (Array.isArray(value)) {
    return value.map(unwrapVariant);
  }
  if (!value || typeof value !== "object") {
    return value;
  }
  if (typeof value.__variant_type === "string"
      && Object.hasOwn(value, "value")) {
    return unwrapVariant(value.value);
  }
  return Object.fromEntries(
    Object.entries(value).map(([key, item]) => [key, unwrapVariant(item)]),
  );
}

function readTableVector(view, table, fieldIndex) {
  const vector = readVectorField(view, table, fieldIndex);
  if (!vector) {
    return [];
  }
  const count = view.getUint32(vector, true);
  ensureRange(view, vector + 4, count * 4, "table vector");
  const tables = [];
  for (let index = 0; index < count; index += 1) {
    tables.push(readOffset(view, vector + 4 + index * 4, `table ${index}`));
  }
  return tables;
}

function readStringVector(view, table, fieldIndex) {
  const vector = readVectorField(view, table, fieldIndex);
  if (!vector) {
    return [];
  }
  const count = view.getUint32(vector, true);
  ensureRange(view, vector + 4, count * 4, "string vector");
  const strings = [];
  for (let index = 0; index < count; index += 1) {
    strings.push(readStringAtOffset(view, vector + 4 + index * 4));
  }
  return strings;
}

function readStringAtOffset(view, location) {
  const stringStart = readOffset(view, location, "string");
  ensureRange(view, stringStart, 4, "string length");
  const length = view.getUint32(stringStart, true);
  ensureRange(view, stringStart + 4, length, "string bytes");
  return new TextDecoder().decode(
    new Uint8Array(view.buffer, view.byteOffset + stringStart + 4, length),
  );
}

function readUint32Field(view, table, fieldIndex, fallback) {
  const offset = readFieldOffset(view, table, fieldIndex);
  return offset === 0 ? fallback : view.getUint32(table + offset, true);
}

function readUint64FieldText(view, table, fieldIndex, fallback) {
  const offset = readFieldOffset(view, table, fieldIndex);
  if (offset === 0) {
    return fallback;
  }
  ensureRange(view, table + offset, 8, "uint64 field");
  if (typeof view.getBigUint64 === "function") {
    return view.getBigUint64(table + offset, true).toString();
  }
  const lo = BigInt(view.getUint32(table + offset, true));
  const hi = BigInt(view.getUint32(table + offset + 4, true));
  return ((hi << 32n) | lo).toString();
}

function readBoolField(view, table, fieldIndex, fallback) {
  const offset = readFieldOffset(view, table, fieldIndex);
  return offset === 0 ? fallback : view.getUint8(table + offset) !== 0;
}

function readStringField(view, table, fieldIndex) {
  const fieldOffset = readFieldOffset(view, table, fieldIndex);
  return fieldOffset === 0
    ? null
    : readStringAtOffset(view, table + fieldOffset);
}

function readVectorField(view, table, fieldIndex) {
  const fieldOffset = readFieldOffset(view, table, fieldIndex);
  return fieldOffset === 0
    ? null
    : readOffset(view, table + fieldOffset, "vector");
}

function readFieldOffset(view, table, fieldIndex) {
  ensureRange(view, table, 4, "table");
  const vtable = table - view.getInt32(table, true);
  ensureRange(view, vtable, 4, "vtable");
  const vtableSize = view.getUint16(vtable, true);
  const entry = 4 + fieldIndex * 2;
  if (entry + 2 > vtableSize) {
    return 0;
  }
  ensureRange(view, vtable + entry, 2, "vtable field");
  return view.getUint16(vtable + entry, true);
}

function readOffset(view, location, label) {
  ensureRange(view, location, 4, label);
  const target = location + view.getUint32(location, true);
  ensureRange(view, target, 1, label);
  return target;
}

function ensureRange(view, offset, length, label) {
  if (!Number.isSafeInteger(offset)
      || !Number.isSafeInteger(length)
      || offset < 0
      || length < 0
      || offset + length > view.byteLength) {
    throw new Error(`View ${label} is outside the resource buffer`);
  }
}
