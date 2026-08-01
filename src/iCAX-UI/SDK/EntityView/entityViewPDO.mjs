export const EntityViewPDOLayout = Object.freeze({
  version: 1,
  headerSize: 16,
  entityIdSize: 16,
});

export function parseEntityViewPDO(buffer) {
  if (!(buffer instanceof ArrayBuffer)) {
    throw new TypeError("EntityViewPDO payload must be an ArrayBuffer");
  }
  if (buffer.byteLength < EntityViewPDOLayout.headerSize) {
    throw new Error("EntityViewPDO payload is smaller than its header");
  }

  const view = new DataView(buffer);
  const revision = readUint64Text(view, 0);
  const count = view.getUint32(8, true);
  const capacity = Math.floor(
    (buffer.byteLength - EntityViewPDOLayout.headerSize)
    / EntityViewPDOLayout.entityIdSize,
  );
  if (count > capacity) {
    throw new Error(
      `EntityViewPDO count ${count} exceeds slot capacity ${capacity}`,
    );
  }

  const entityIds = new Array(count);
  for (let index = 0; index < count; index += 1) {
    entityIds[index] = readUuidText(
      view,
      EntityViewPDOLayout.headerSize + index * EntityViewPDOLayout.entityIdSize,
    );
  }
  return Object.freeze({
    revision,
    count,
    entityIds: Object.freeze(entityIds),
  });
}

function readUint64Text(view, offset) {
  if (typeof view.getBigUint64 === "function") {
    return view.getBigUint64(offset, true).toString();
  }
  const lo = BigInt(view.getUint32(offset, true));
  const hi = BigInt(view.getUint32(offset + 4, true));
  return ((hi << 32n) | lo).toString();
}

function readUuidText(view, offset) {
  const bytes = new Uint8Array(view.buffer, view.byteOffset + offset, 16);
  const hex = [...bytes].map((value) => value.toString(16).padStart(2, "0"));
  return [
    hex.slice(0, 4).join(""),
    hex.slice(4, 6).join(""),
    hex.slice(6, 8).join(""),
    hex.slice(8, 10).join(""),
    hex.slice(10, 16).join(""),
  ].join("-");
}
