import {
  RenderResourceIdentifiers,
  parseRenderGeometryResource,
} from "../../../iCAX-UI/SDK/Viewport/renderResource.mjs";

const PREVIEW_IDENTIFIER = "T1PM";
const PREVIEW_MEDIA_TYPE = "application/vnd.icax.flatbuffer";
const SUPPORTED_SCHEMA_VERSION = 1;

export async function loadPreviewMeshResource(resourceClient, reference) {
  const url = String(reference?.url ?? "").trim();
  if (!url) {
    throw new TypeError("预览资源 URL 为空");
  }
  if (!resourceClient || typeof resourceClient.get !== "function") {
    throw new TypeError("当前场景没有可用的资源客户端");
  }

  const headers = new Headers({ Accept: PREVIEW_MEDIA_TYPE });
  const version = Number(reference?.version ?? 0);
  if (Number.isSafeInteger(version) && version > 0) {
    headers.set("ICAX-Resource-Version", String(version));
  }
  const response = await resourceClient.get(url, { headers });
  if (!response.ok) {
    throw new Error(`预览资源读取失败（HTTP ${response.status}）`);
  }

  const buffer = await response.arrayBuffer();
  const identifier = readFlatBufferIdentifier(buffer);
  const headerIdentifier = response.headers.get("ICAX-FlatBuffer-Identifier");
  if (headerIdentifier && headerIdentifier !== identifier) {
    throw new Error(`预览资源标识不一致：${headerIdentifier}/${identifier}`);
  }

  let mesh;
  if (identifier === RenderResourceIdentifiers.geometry) {
    const geometry = parseRenderGeometryResource(buffer);
    if (geometry.kind !== "mesh") {
      throw new Error(`节点预览必须是网格资源，实际为：${geometry.kind}`);
    }
    validatePreviewMesh(geometry.positions, geometry.indices);
    mesh = {
      positions: geometry.positions,
      indices: geometry.indices,
      vertexCount: geometry.positions.length / 3,
      triangleCount: geometry.indices.length / 3,
      schemaVersion: geometry.schemaVersion,
      dataVersion: geometry.dataVersion,
      format: "render-geometry",
    };
  } else if (identifier === PREVIEW_IDENTIFIER) {
    const schemaVersion = Number(response.headers.get("ICAX-Schema-Version") ?? 1);
    if (!Number.isInteger(schemaVersion)
        || schemaVersion < 1
        || schemaVersion > SUPPORTED_SCHEMA_VERSION) {
      throw new Error(`不支持的预览资源版本：${schemaVersion}`);
    }
    mesh = {
      ...parsePreviewMeshFlatBuffer(buffer),
      format: "legacy-tube-preview",
    };
  } else {
    throw new Error(`不支持的预览资源格式：${identifier}`);
  }
  return {
    ...mesh,
    resourceUrl: url,
    resourceVersion: Number(
      response.headers.get("ICAX-Resource-Version") ?? version ?? 0,
    ),
  };
}

export function parsePreviewMeshFlatBuffer(buffer) {
  if (!(buffer instanceof ArrayBuffer)) {
    throw new TypeError("预览资源必须是 ArrayBuffer");
  }
  const view = new DataView(buffer);
  const identifier = readFlatBufferIdentifier(buffer);
  if (identifier !== PREVIEW_IDENTIFIER) {
    throw new Error(`预览资源标识无效：${identifier}`);
  }

  const table = view.getUint32(0, true);
  requireRange(view, table, 4);
  const schemaVersion = readScalarUint32(view, table, 0, 1);
  if (schemaVersion < 1 || schemaVersion > SUPPORTED_SCHEMA_VERSION) {
    throw new Error(`不支持的预览资源版本：${schemaVersion}`);
  }
  const positions = readVector(view, table, 1, Float32Array, 4);
  const indices = readVector(view, table, 2, Uint32Array, 4);
  const nodeId = readString(view, table, 3);

  validatePreviewMesh(positions, indices);
  const vertexCount = positions.length / 3;
  return {
    nodeId,
    positions,
    indices,
    vertexCount,
    triangleCount: indices.length / 3,
    schemaVersion,
  };
}

function readFlatBufferIdentifier(buffer) {
  if (!(buffer instanceof ArrayBuffer) || buffer.byteLength < 8) {
    throw new Error("预览资源小于 FlatBuffer 根对象");
  }
  return String.fromCharCode(...new Uint8Array(buffer, 4, 4));
}

function validatePreviewMesh(positions, indices) {
  if (positions.length < 9 || positions.length % 3 !== 0) {
    throw new Error("预览资源的顶点数据无效");
  }
  if (indices.length < 3 || indices.length % 3 !== 0) {
    throw new Error("预览资源的三角形索引无效");
  }
  const vertexCount = positions.length / 3;
  for (const index of indices) {
    if (index >= vertexCount) {
      throw new Error("预览资源包含越界索引");
    }
  }
}

function readScalarUint32(view, table, fieldIndex, fallback) {
  const offset = getFieldOffset(view, table, fieldIndex);
  if (!offset) {
    return fallback;
  }
  requireRange(view, table + offset, 4);
  return view.getUint32(table + offset, true);
}

function readVector(view, table, fieldIndex, TypedArray, elementSize) {
  const fieldOffset = getFieldOffset(view, table, fieldIndex);
  if (!fieldOffset) {
    return new TypedArray();
  }
  const reference = table + fieldOffset;
  requireRange(view, reference, 4);
  const vector = reference + view.getUint32(reference, true);
  requireRange(view, vector, 4);
  const length = view.getUint32(vector, true);
  const byteLength = length * elementSize;
  if (!Number.isSafeInteger(byteLength)) {
    throw new Error("预览资源向量长度无效");
  }
  const begin = vector + 4;
  requireRange(view, begin, byteLength);
  return new TypedArray(view.buffer.slice(begin, begin + byteLength));
}

function readString(view, table, fieldIndex) {
  const fieldOffset = getFieldOffset(view, table, fieldIndex);
  if (!fieldOffset) {
    return "";
  }
  const reference = table + fieldOffset;
  requireRange(view, reference, 4);
  const stringOffset = reference + view.getUint32(reference, true);
  requireRange(view, stringOffset, 4);
  const length = view.getUint32(stringOffset, true);
  const begin = stringOffset + 4;
  requireRange(view, begin, length);
  return new TextDecoder().decode(
    new Uint8Array(view.buffer, begin, length),
  );
}

function getFieldOffset(view, table, fieldIndex) {
  requireRange(view, table, 4);
  const vtable = table - view.getInt32(table, true);
  requireRange(view, vtable, 4);
  const vtableLength = view.getUint16(vtable, true);
  const slot = vtable + 4 + fieldIndex * 2;
  if (slot + 2 > vtable + vtableLength) {
    return 0;
  }
  requireRange(view, slot, 2);
  return view.getUint16(slot, true);
}

function requireRange(view, offset, length) {
  if (!Number.isSafeInteger(offset)
      || !Number.isSafeInteger(length)
      || offset < 0
      || length < 0
      || offset + length > view.byteLength) {
    throw new Error("预览资源已损坏或不完整");
  }
}
