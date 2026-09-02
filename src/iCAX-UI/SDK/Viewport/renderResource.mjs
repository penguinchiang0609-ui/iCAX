export const RenderResourceIdentifiers = Object.freeze({
  geometry: "ICRG",
  material: "ICRM",
});
export const RenderGeometryKind = Object.freeze({ mesh: 1, polyline: 2, toolpath: 3 });
export const RenderFlags = Object.freeze({
  visible: 1 << 0,
  selectable: 1 << 1,
  highlighted: 1 << 2,
  selected: 1 << 3,
  disabled: 1 << 5,
});
export const RenderLayers = Object.freeze({ default: 1 << 0, all: 0xffffffff });

export function rgbaToCssColor(colorRGBA) {
  const value = Number(colorRGBA) >>> 0;
  return {
    r: (value >>> 24) & 0xff,
    g: (value >>> 16) & 0xff,
    b: (value >>> 8) & 0xff,
    a: (value & 0xff) / 255,
  };
}

export async function loadRenderResource(resourceClient, reference) {
  const url = String(reference?.url ?? "").trim();
  const version = String(reference?.version ?? "0");
  if (!url) {
    return null;
  }
  const headers = new Headers({ Accept: "application/vnd.icax.flatbuffer" });
  if (version !== "0") {
    headers.set("ICAX-Resource-Version", version);
  }
  const response = await resourceClient.get(url, { headers });
  if (!response.ok) {
    throw new Error(`Render resource GET failed (${response.status}): ${url}`);
  }
  const buffer = await response.arrayBuffer();
  const identifier = readIdentifier(buffer);
  if (identifier === RenderResourceIdentifiers.geometry) {
    return { type: "geometry", url, version, data: parseRenderGeometryResource(buffer) };
  }
  if (identifier === RenderResourceIdentifiers.material) {
    return { type: "material", url, version, data: parseRenderMaterialResource(buffer) };
  }
  throw new Error(`Unsupported render resource identifier: ${identifier}`);
}

export function parseRenderGeometryResource(buffer) {
  const { view, root } = openFlatBuffer(buffer, RenderResourceIdentifiers.geometry);
  const schemaVersion = readUint32Field(view, root, 0, 1);
  if (schemaVersion !== 1) {
    throw new Error(`Unsupported render geometry schema: ${schemaVersion}`);
  }
  const kindCode = readUint32Field(view, root, 1, 0);
  const positions = readFloat32Vector(view, root, 5);
  const common = {
    schemaVersion,
    dataVersion: readUint64FieldText(view, root, 2, "0"),
    topology: readUint32Field(view, root, 3, 0),
    flags: readUint32Field(view, root, 4, 0),
  };
  if (kindCode === 1) {
    return Object.freeze({
      ...common,
      kind: "mesh",
      geometryKind: 1,
      positions,
      normals: readFloat32Vector(view, root, 6),
      textureCoordinates: readFloat32Vector(view, root, 7),
      vertexColors: readUint32Vector(view, root, 8),
      indices: readUint32Vector(view, root, 9),
    });
  }
  if (kindCode === 2) {
    return Object.freeze({
      ...common,
      kind: "polyline",
      geometryKind: 2,
      points: makeLineSegmentPositions(
        positions,
        readUint32Vector(view, root, 10),
      ),
    });
  }
  if (kindCode === 3) {
    const axes = readFloat32Vector(view, root, 11);
    const feeds = readFloat32Vector(view, root, 12);
    const pointFlags = readUint32Vector(view, root, 13);
    const points = [];
    for (let offset = 0, index = 0; offset + 2 < positions.length; offset += 3, index += 1) {
      points.push(Object.freeze({
        position: { x: positions[offset], y: positions[offset + 1], z: positions[offset + 2] },
        toolAxis: { x: axes[offset] ?? 0, y: axes[offset + 1] ?? 0, z: axes[offset + 2] ?? 1 },
        feed: feeds[index] ?? 0,
        flags: pointFlags[index] ?? 0,
      }));
    }
    const packedSpans = readUint32Vector(view, root, 14);
    const spans = [];
    for (let offset = 0; offset + 5 < packedSpans.length; offset += 6) {
      spans.push(Object.freeze({
        firstPoint: packedSpans[offset],
        pointCount: packedSpans[offset + 1],
        moveKind: packedSpans[offset + 2],
        toolId: packedSpans[offset + 3],
        styleId: packedSpans[offset + 4],
        flags: packedSpans[offset + 5],
      }));
    }
    return Object.freeze({
      ...common,
      kind: "toolpath",
      geometryKind: 3,
      points: Object.freeze(points),
      spans: Object.freeze(spans),
    });
  }
  throw new Error(`Unsupported render geometry kind: ${kindCode}`);
}

export function parseRenderMaterialResource(buffer) {
  const { view, root } = openFlatBuffer(buffer, RenderResourceIdentifiers.material);
  const schemaVersion = readUint32Field(view, root, 0, 1);
  if (schemaVersion !== 1) {
    throw new Error(`Unsupported render material schema: ${schemaVersion}`);
  }
  return Object.freeze({
    schemaVersion,
    dataVersion: readUint64FieldText(view, root, 1, "0"),
    colorRGBA: readUint32Field(view, root, 2, 0xffffffff),
    ambientRGBA: readUint32Field(view, root, 3, 0xffffffff),
    specularRGBA: readUint32Field(view, root, 4, 0x000000ff),
    emissiveRGBA: readUint32Field(view, root, 5, 0x000000ff),
    lineWidth: readFloat32Field(view, root, 6, 1),
    flags: readUint32Field(view, root, 7, 0),
    materialFlags: readUint32Field(view, root, 8, 0),
    baseColorTextureUrl: readStringField(view, root, 9) ?? "",
  });
}

function makeLineSegmentPositions(points, ranges) {
  const segments = [];
  for (let offset = 0; offset + 4 < ranges.length; offset += 5) {
    const first = ranges[offset];
    const count = ranges[offset + 1];
    for (let point = first; point + 1 < first + count; point += 1) {
      const a = point * 3;
      const b = (point + 1) * 3;
      if (b + 2 >= points.length) {
        break;
      }
      segments.push(
        points[a], points[a + 1], points[a + 2],
        points[b], points[b + 1], points[b + 2],
      );
    }
  }
  return new Float32Array(segments);
}

function readIdentifier(buffer) {
  if (!(buffer instanceof ArrayBuffer) || buffer.byteLength < 8) {
    throw new Error("Render resource is smaller than a FlatBuffer root");
  }
  return String.fromCharCode(...new Uint8Array(buffer, 4, 4));
}

function openFlatBuffer(buffer, expectedIdentifier) {
  const identifier = readIdentifier(buffer);
  if (identifier !== expectedIdentifier) {
    throw new Error(`Invalid render resource identifier: ${identifier}`);
  }
  const view = new DataView(buffer);
  return { view, root: readOffset(view, 0, "root") };
}

function readUint32Field(view, table, fieldIndex, fallback) {
  const offset = readFieldOffset(view, table, fieldIndex);
  return offset === 0 ? fallback : view.getUint32(table + offset, true);
}

function readFloat32Field(view, table, fieldIndex, fallback) {
  const offset = readFieldOffset(view, table, fieldIndex);
  return offset === 0 ? fallback : view.getFloat32(table + offset, true);
}

function readUint64FieldText(view, table, fieldIndex, fallback) {
  const offset = readFieldOffset(view, table, fieldIndex);
  return offset === 0
    ? fallback
    : view.getBigUint64(table + offset, true).toString();
}

function readFloat32Vector(view, table, fieldIndex) {
  const vector = readVector(view, table, fieldIndex);
  if (!vector) {
    return new Float32Array(0);
  }
  ensureRange(view, vector.data, vector.length * 4, "float vector");
  const values = new Float32Array(vector.length);
  for (let index = 0; index < vector.length; index += 1) {
    values[index] = view.getFloat32(vector.data + index * 4, true);
  }
  return values;
}

function readUint32Vector(view, table, fieldIndex) {
  const vector = readVector(view, table, fieldIndex);
  if (!vector) {
    return new Uint32Array(0);
  }
  ensureRange(view, vector.data, vector.length * 4, "uint vector");
  const values = new Uint32Array(vector.length);
  for (let index = 0; index < vector.length; index += 1) {
    values[index] = view.getUint32(vector.data + index * 4, true);
  }
  return values;
}

function readVector(view, table, fieldIndex) {
  const offset = readFieldOffset(view, table, fieldIndex);
  if (offset === 0) {
    return null;
  }
  const start = readOffset(view, table + offset, "vector");
  ensureRange(view, start, 4, "vector length");
  return { length: view.getUint32(start, true), data: start + 4 };
}

function readStringField(view, table, fieldIndex) {
  const offset = readFieldOffset(view, table, fieldIndex);
  if (offset === 0) {
    return null;
  }
  const start = readOffset(view, table + offset, "string");
  ensureRange(view, start, 4, "string length");
  const length = view.getUint32(start, true);
  ensureRange(view, start + 4, length, "string bytes");
  return new TextDecoder().decode(
    new Uint8Array(view.buffer, view.byteOffset + start + 4, length),
  );
}

function readFieldOffset(view, table, fieldIndex) {
  ensureRange(view, table, 4, "table");
  const vtable = table - view.getInt32(table, true);
  ensureRange(view, vtable, 4, "vtable");
  const entry = 4 + fieldIndex * 2;
  if (entry + 2 > view.getUint16(vtable, true)) {
    return 0;
  }
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
    throw new Error(`Render ${label} is outside the resource buffer`);
  }
}
