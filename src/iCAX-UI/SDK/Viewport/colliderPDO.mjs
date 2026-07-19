import { makeFacadeMethodCodeFromName } from "../Facades/facadeMethod.mjs";

export const ColliderPDOEvents = Object.freeze({
  slotAllocated: "PDOCollider.SlotAllocated",
  slotFreed: "PDOCollider.SlotFreed",
  slotMoved: "PDOCollider.SlotMoved",
  defragBegin: "PDOCollider.DefragBegin",
  defragEnd: "PDOCollider.DefragEnd",
});

export const ColliderPDOTypeCodes = Object.freeze(Object.fromEntries(
  Object.entries(ColliderPDOEvents).map(([key, facadeEvent]) => [key, makeFacadeMethodCodeFromName(facadeEvent)]),
));

export const ColliderPDOPayloadKind = Object.freeze({
  unknown: 0,
  object: 1,
});

export const ColliderShapeKind = Object.freeze({
  box: 1,
  sphere: 2,
  capsule: 3,
  triangleMesh: 4,
  convexHull: 5,
  cylinder: 6,
});

export const ColliderFlags = Object.freeze({
  visible: 1 << 0,
  enabled: 1 << 1,
  selectable: 1 << 2,
});

export const ColliderPDOLayout = Object.freeze({
  magic: 0x4F445043,
  version: 1,
  headerSize: 32,
  objectHeaderSize: 64,
  shapeSize: 112,
});

export function parseColliderPDOEvent(event) {
  if (!event) {
    return null;
  }
  if (event.payload && typeof event.payload === "object") {
    return normalizeColliderEventPayload(event.payload);
  }
  const text = event.raw?.payloadText ?? "";
  if (!text) {
    return null;
  }
  return normalizeColliderEventPayload(JSON.parse(text));
}

export function normalizeColliderEventPayload(payload) {
  const result = { ...payload };
  for (const key of ["pdoId", "objectId", "entityId", "sceneId", "slotVersion", "payloadCapacity"]) {
    if (result[key] !== undefined && result[key] !== null) {
      result[key] = String(result[key]);
    }
  }
  if (result.payloadKind) {
    result.payloadKind = String(result.payloadKind);
  }
  return result;
}

export function parseColliderPDOPayload(buffer) {
  const dataView = new DataView(buffer);
  const header = readColliderHeader(dataView, 0);
  switch (header.payloadKind) {
    case ColliderPDOPayloadKind.object:
      return parseObjectColliderPayload(dataView, header);
    default:
      throw new Error(`Unsupported ColliderPDO payload kind: ${header.payloadKind}`);
  }
}

export function readColliderHeader(dataView, offset) {
  const magic = dataView.getUint32(offset, true);
  const layoutVersion = dataView.getUint32(offset + 4, true);
  if (magic !== ColliderPDOLayout.magic) {
    throw new Error(`Invalid ColliderPDO magic: ${magic}`);
  }
  if (layoutVersion !== ColliderPDOLayout.version) {
    throw new Error(`Unsupported ColliderPDO layout version: ${layoutVersion}`);
  }
  return {
    magic,
    layoutVersion,
    payloadKind: dataView.getUint32(offset + 8, true),
    headerSize: dataView.getUint32(offset + 12, true),
    payloadSize: readUint64Number(dataView, offset + 16),
    dataVersion: readUint64Text(dataView, offset + 24),
  };
}

export function parseObjectColliderPayload(dataView, header = readColliderHeader(dataView, 0)) {
  const objectId = readUuidText(dataView, 32);
  const flags = dataView.getUint32(48, true);
  const shapeCount = dataView.getUint32(52, true);
  const shapesOffset = readUint64Number(dataView, 56);
  const shapes = [];
  for (let index = 0; index < shapeCount; index += 1) {
    const offset = shapesOffset + index * ColliderPDOLayout.shapeSize;
    shapes.push(readColliderShape(dataView, offset));
  }
  return {
    kind: "colliderObject",
    header,
    objectId,
    entityId: objectId,
    flags,
    visible: Boolean(flags & ColliderFlags.visible),
    enabled: Boolean(flags & ColliderFlags.enabled),
    shapeCount,
    shapes,
  };
}

function readColliderShape(dataView, offset) {
  const parameters = [];
  for (let index = 0; index < 8; index += 1) {
    parameters.push(dataView.getFloat32(offset + 80 + index * 4, true));
  }
  return {
    shapeKind: dataView.getUint32(offset, true),
    flags: dataView.getUint32(offset + 4, true),
    layer: dataView.getUint32(offset + 8, true),
    mask: dataView.getUint32(offset + 12, true),
    localMatrix: readMatrix4(dataView, offset + 16),
    parameters,
  };
}

function readMatrix4(dataView, offset) {
  const values = new Array(16);
  for (let index = 0; index < 16; index += 1) {
    values[index] = dataView.getFloat32(offset + index * 4, true);
  }
  return values;
}

function readUint64Text(dataView, offset) {
  if (typeof dataView.getBigUint64 === "function") {
    return dataView.getBigUint64(offset, true).toString();
  }
  const lo = BigInt(dataView.getUint32(offset, true));
  const hi = BigInt(dataView.getUint32(offset + 4, true));
  return ((hi << 32n) | lo).toString();
}

function readUint64Number(dataView, offset) {
  const text = readUint64Text(dataView, offset);
  const value = Number(text);
  if (!Number.isSafeInteger(value)) {
    throw new Error(`ColliderPDO byte offset is outside JS safe integer range: ${text}`);
  }
  return value;
}

function readUuidText(dataView, offset) {
  const bytes = [];
  let nonZero = false;
  for (let index = 0; index < 16; index += 1) {
    const value = dataView.getUint8(offset + index);
    bytes.push(value.toString(16).padStart(2, "0"));
    nonZero ||= value !== 0;
  }
  if (!nonZero) {
    return "";
  }
  const hex = bytes.join("");
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20, 32)}`;
}
