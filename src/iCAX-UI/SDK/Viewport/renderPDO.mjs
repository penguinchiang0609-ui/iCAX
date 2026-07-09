import { makeCommandTypeCodeFromCommand } from "../Mailbox/commandRoute.mjs";

export const RenderPDOCommands = Object.freeze({
  slotAllocated: "PDORender.SlotAllocated",
  slotFreed: "PDORender.SlotFreed",
  slotMoved: "PDORender.SlotMoved",
  defragBegin: "PDORender.DefragBegin",
  defragEnd: "PDORender.DefragEnd",
});

export const RenderPDOTypeCodes = Object.freeze(Object.fromEntries(
  Object.entries(RenderPDOCommands).map(([key, command]) => [key, makeCommandTypeCodeFromCommand(command)]),
));

export const RenderPDOPayloadKind = Object.freeze({
  unknown: 0,
  mesh: 1,
  polyline: 2,
  toolpath: 3,
  instanceList: 4,
  camera: 5,
  transform: 6,
});

export const RenderGeometryKind = Object.freeze({
  unknown: 0,
  mesh: 1,
  polyline: 2,
  toolpath: 3,
});

export const RenderFlags = Object.freeze({
  visible: 1 << 0,
  selectable: 1 << 1,
  highlighted: 1 << 2,
  selected: 1 << 3,
  dirty: 1 << 4,
  disabled: 1 << 5,
  meshHasNormals: 1 << 0,
  meshHasVertexColors: 1 << 1,
});

export const RenderPDOLayout = Object.freeze({
  magic: 0x4F445052,
  version: 1,
  headerSize: 32,
  meshHeaderSize: 88,
  polylineHeaderSize: 88,
  toolpathHeaderSize: 88,
  instanceListHeaderSize: 56,
  cameraHeaderSize: 56,
  transformHeaderSize: 112,
  float3Size: 12,
  polylineRangeSize: 24,
  toolpathPointSize: 32,
  toolpathSpanSize: 24,
  instanceSize: 32,
  styleSize: 16,
  cameraSize: 32,
});

export function parseRenderPDOEvent(event) {
  if (!event) {
    return null;
  }
  if (event.payload && typeof event.payload === "object") {
    return normalizeRenderEventPayload(event.payload);
  }
  const text = event.raw?.payloadText ?? "";
  if (!text) {
    return null;
  }
  return normalizeRenderEventPayload(JSON.parse(text));
}

export function normalizeRenderEventPayload(payload) {
  const result = { ...payload };
  for (const key of ["pdoId", "geometryId", "objectId", "transformId", "sceneId", "payloadCapacity"]) {
    if (result[key] !== undefined && result[key] !== null) {
      result[key] = String(result[key]);
    }
  }
  if (result.payloadKind) {
    result.payloadKind = String(result.payloadKind);
  }
  return result;
}

export function parseRenderPDOPayload(buffer) {
  const dataView = new DataView(buffer);
  const header = readRenderHeader(dataView, 0);
  switch (header.payloadKind) {
    case RenderPDOPayloadKind.mesh:
      return parseMeshPayload(buffer, dataView, header);
    case RenderPDOPayloadKind.polyline:
      return parsePolylinePayload(buffer, dataView, header);
    case RenderPDOPayloadKind.toolpath:
      return parseToolpathPayload(buffer, dataView, header);
    case RenderPDOPayloadKind.instanceList:
      return parseInstanceListPayload(buffer, dataView, header);
    case RenderPDOPayloadKind.camera:
      return parseCameraPayload(buffer, dataView, header);
    case RenderPDOPayloadKind.transform:
      return parseTransformPayload(buffer, dataView, header);
    default:
      throw new Error(`Unsupported RenderPDO payload kind: ${header.payloadKind}`);
  }
}

export function readRenderHeader(dataView, offset) {
  const magic = dataView.getUint32(offset, true);
  const layoutVersion = dataView.getUint32(offset + 4, true);
  if (magic !== RenderPDOLayout.magic) {
    throw new Error(`Invalid RenderPDO magic: ${magic}`);
  }
  if (layoutVersion !== RenderPDOLayout.version) {
    throw new Error(`Unsupported RenderPDO layout version: ${layoutVersion}`);
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

export function parseMeshPayload(buffer, dataView, header = readRenderHeader(dataView, 0)) {
  const flags = dataView.getUint32(52, true);
  const vertexCount = dataView.getUint32(44, true);
  const indexCount = dataView.getUint32(48, true);
  const positionsOffset = readUint64Number(dataView, 56);
  const normalsOffset = readUint64Number(dataView, 64);
  const vertexColorsOffset = readUint64Number(dataView, 72);
  const indicesOffset = readUint64Number(dataView, 80);
  return {
    kind: "mesh",
    header,
    geometryId: readUint64Text(dataView, 32),
    topology: dataView.getUint32(40, true),
    vertexCount,
    indexCount,
    flags,
    positions: makeFloat32View(buffer, positionsOffset, vertexCount * 3),
    normals: (flags & RenderFlags.meshHasNormals) && normalsOffset ? makeFloat32View(buffer, normalsOffset, vertexCount * 3) : null,
    vertexColors: (flags & RenderFlags.meshHasVertexColors) && vertexColorsOffset ? makeUint32View(buffer, vertexColorsOffset, vertexCount) : null,
    indices: indexCount && indicesOffset ? makeUint32View(buffer, indicesOffset, indexCount) : null,
  };
}

export function parsePolylinePayload(buffer, dataView, header = readRenderHeader(dataView, 0)) {
  const pointCount = dataView.getUint32(64, true);
  const rangeCount = dataView.getUint32(68, true);
  const pointsOffset = readUint64Number(dataView, 72);
  const rangesOffset = readUint64Number(dataView, 80);
  const ranges = [];
  for (let index = 0; index < rangeCount; index += 1) {
    const offset = rangesOffset + index * RenderPDOLayout.polylineRangeSize;
    ranges.push({
      firstPoint: dataView.getUint32(offset, true),
      pointCount: dataView.getUint32(offset + 4, true),
      renderClass: dataView.getUint32(offset + 8, true),
      styleId: dataView.getUint32(offset + 12, true),
      flags: dataView.getUint32(offset + 16, true),
    });
  }
  return {
    kind: "polyline",
    header,
    geometryId: readUint64Text(dataView, 32),
    bounds: readAABB(dataView, 40),
    pointCount,
    rangeCount,
    points: makeFloat32View(buffer, pointsOffset, pointCount * 3),
    ranges,
  };
}

export function parseToolpathPayload(buffer, dataView, header = readRenderHeader(dataView, 0)) {
  const pointCount = dataView.getUint32(64, true);
  const spanCount = dataView.getUint32(68, true);
  const pointsOffset = readUint64Number(dataView, 72);
  const spansOffset = readUint64Number(dataView, 80);
  const points = [];
  const spans = [];
  for (let index = 0; index < pointCount; index += 1) {
    const offset = pointsOffset + index * RenderPDOLayout.toolpathPointSize;
    points.push({
      position: readFloat3(dataView, offset),
      toolAxis: readFloat3(dataView, offset + 12),
      feed: dataView.getFloat32(offset + 24, true),
      flags: dataView.getUint32(offset + 28, true),
    });
  }
  for (let index = 0; index < spanCount; index += 1) {
    const offset = spansOffset + index * RenderPDOLayout.toolpathSpanSize;
    spans.push({
      firstPoint: dataView.getUint32(offset, true),
      pointCount: dataView.getUint32(offset + 4, true),
      moveKind: dataView.getUint32(offset + 8, true),
      toolId: dataView.getUint32(offset + 12, true),
      styleId: dataView.getUint32(offset + 16, true),
      flags: dataView.getUint32(offset + 20, true),
    });
  }
  return {
    kind: "toolpath",
    header,
    geometryId: readUint64Text(dataView, 32),
    bounds: readAABB(dataView, 40),
    pointCount,
    spanCount,
    points,
    spans,
  };
}

export function parseInstanceListPayload(buffer, dataView, header = readRenderHeader(dataView, 0)) {
  const instanceCount = dataView.getUint32(32, true);
  const styleCount = dataView.getUint32(36, true);
  const instancesOffset = readUint64Number(dataView, 40);
  const stylesOffset = readUint64Number(dataView, 48);
  const instances = [];
  const styles = [];
  for (let index = 0; index < instanceCount; index += 1) {
    const offset = instancesOffset + index * RenderPDOLayout.instanceSize;
    const objectId = readUint64Text(dataView, offset);
    instances.push({
      objectId,
      geometryId: readUint64Text(dataView, offset + 8),
      geometryKind: dataView.getUint32(offset + 16, true),
      renderClass: dataView.getUint32(offset + 20, true),
      styleId: dataView.getUint32(offset + 24, true),
      flags: dataView.getUint32(offset + 28, true),
      transformId: objectId,
    });
  }
  for (let index = 0; index < styleCount; index += 1) {
    const offset = stylesOffset + index * RenderPDOLayout.styleSize;
    styles.push({
      styleId: dataView.getUint32(offset, true),
      colorRGBA: dataView.getUint32(offset + 4, true),
      lineWidth: dataView.getFloat32(offset + 8, true),
      flags: dataView.getUint32(offset + 12, true),
    });
  }
  return { kind: "instance_list", header, instanceCount, styleCount, instances, styles };
}

export function parseCameraPayload(buffer, dataView, header = readRenderHeader(dataView, 0)) {
  const cameraCount = dataView.getUint32(32, true);
  const activeCameraId = readUint64Text(dataView, 40);
  const camerasOffset = readUint64Number(dataView, 48);
  const cameras = [];
  for (let index = 0; index < cameraCount; index += 1) {
    const offset = camerasOffset + index * RenderPDOLayout.cameraSize;
    const cameraId = readUint64Text(dataView, offset);
    cameras.push({
      cameraId,
      transformId: cameraId,
      flags: dataView.getUint32(offset + 8, true),
      nearPlane: dataView.getFloat32(offset + 16, true),
      farPlane: dataView.getFloat32(offset + 20, true),
      verticalFovRadians: dataView.getFloat32(offset + 24, true),
      orthographicHeight: dataView.getFloat32(offset + 28, true),
    });
  }
  return { kind: "camera", header, cameraCount, activeCameraId, cameras };
}

export function parseTransformPayload(buffer, dataView, header = readRenderHeader(dataView, 0)) {
  return {
    kind: "transform",
    header,
    transformId: readUint64Text(dataView, 32),
    flags: dataView.getUint32(40, true),
    localToWorld: readMatrix4(dataView, 48),
  };
}

export function readFloat3(dataView, offset) {
  return {
    x: dataView.getFloat32(offset, true),
    y: dataView.getFloat32(offset + 4, true),
    z: dataView.getFloat32(offset + 8, true),
  };
}

export function readAABB(dataView, offset) {
  return {
    min: readFloat3(dataView, offset),
    max: readFloat3(dataView, offset + 12),
  };
}

export function readMatrix4(dataView, offset) {
  const values = new Array(16);
  for (let index = 0; index < 16; index += 1) {
    values[index] = dataView.getFloat32(offset + index * 4, true);
  }
  return values;
}

export function rgbaToCssColor(colorRGBA) {
  const r = (colorRGBA >>> 24) & 0xff;
  const g = (colorRGBA >>> 16) & 0xff;
  const b = (colorRGBA >>> 8) & 0xff;
  const a = (colorRGBA & 0xff) / 255;
  return { r, g, b, a };
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
    throw new Error(`RenderPDO byte offset is outside JS safe integer range: ${text}`);
  }
  return value;
}

function makeFloat32View(buffer, byteOffset, length) {
  if (!byteOffset || !length) {
    return new Float32Array();
  }
  return new Float32Array(buffer, byteOffset, length);
}

function makeUint32View(buffer, byteOffset, length) {
  if (!byteOffset || !length) {
    return new Uint32Array();
  }
  return new Uint32Array(buffer, byteOffset, length);
}
