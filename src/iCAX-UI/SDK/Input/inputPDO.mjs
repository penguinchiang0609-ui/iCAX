import { makePDOID } from "../Facades/facadeMethod.mjs";

export const InputPDOLayout = Object.freeze({
  magic: 0x4F445049,
  version: 1,
  stateKind: 1,
  payloadSize: 200,
  keyboardOffset: 72,
  pointerOffset: 152,
  keyBitCount: 512,
  keyWordCount: 8,
  instanceName: "default",
  typeName: "input.state",
});

export const InputStateFlags = Object.freeze({
  focused: 1 << 0,
  pointerInside: 1 << 1,
  pointerCaptured: 1 << 2,
  textInputActive: 1 << 3,
});

export const InputModifierFlags = Object.freeze({
  shift: 1 << 0,
  ctrl: 1 << 1,
  alt: 1 << 2,
  super: 1 << 3,
});

export const InputKeyCodes = Object.freeze({
  Escape: 27,
  Space: 32,
  PageUp: 33,
  PageDown: 34,
  ArrowLeft: 37,
  ArrowUp: 38,
  ArrowRight: 39,
  ArrowDown: 40,
  Shift: 16,
  Ctrl: 17,
  Alt: 18,
  KeyA: 65,
  KeyD: 68,
  KeyE: 69,
  KeyQ: 81,
  KeyS: 83,
  KeyW: 87,
});

const keyCodeMap = Object.freeze({
  Escape: InputKeyCodes.Escape,
  Space: InputKeyCodes.Space,
  PageUp: InputKeyCodes.PageUp,
  PageDown: InputKeyCodes.PageDown,
  ArrowLeft: InputKeyCodes.ArrowLeft,
  ArrowUp: InputKeyCodes.ArrowUp,
  ArrowRight: InputKeyCodes.ArrowRight,
  ArrowDown: InputKeyCodes.ArrowDown,
  ShiftLeft: InputKeyCodes.Shift,
  ShiftRight: InputKeyCodes.Shift,
  ControlLeft: InputKeyCodes.Ctrl,
  ControlRight: InputKeyCodes.Ctrl,
  AltLeft: InputKeyCodes.Alt,
  AltRight: InputKeyCodes.Alt,
  KeyA: InputKeyCodes.KeyA,
  KeyD: InputKeyCodes.KeyD,
  KeyE: InputKeyCodes.KeyE,
  KeyQ: InputKeyCodes.KeyQ,
  KeyS: InputKeyCodes.KeyS,
  KeyW: InputKeyCodes.KeyW,
});

export function mapKeyboardEventCode(event) {
  return keyCodeMap[event?.code] ?? 0;
}

export function makeInputStatePDOID() {
  return makePDOID(InputPDOLayout.typeName, InputPDOLayout.instanceName);
}

export async function writeInputStatePDO(sceneProxy, snapshot) {
  if (!sceneProxy?.pdo?.enabled) {
    return false;
  }

  const dataVersion = snapshot.dataVersion ?? 1n;
  return sceneProxy.pdo.withWriteDescriptor({
    id: makeInputStatePDOID(),
    version: InputPDOLayout.version,
    payloadSize: InputPDOLayout.payloadSize,
    dataVersion: dataVersion.toString(),
  }, (buffer) => {
    writeInputStatePayload(buffer, snapshot);
  });
}

export function writeInputStatePayload(buffer, snapshot) {
  const view = new DataView(buffer);
  const dataVersion = snapshot.dataVersion ?? 1n;
  const sceneId = snapshot.sceneId ?? 1n;
  const viewportId = snapshot.viewportId ?? 1n;
  const sequence = snapshot.sequence ?? dataVersion;
  const timestampUS = snapshot.timestampUS ?? BigInt(Math.floor(performance.now() * 1000));
  const pointer = snapshot.pointer ?? {};
  const keys = snapshot.keys ?? new Set();

  view.setUint32(0, InputPDOLayout.magic, true);
  view.setUint32(4, InputPDOLayout.version, true);
  view.setUint32(8, InputPDOLayout.stateKind, true);
  view.setUint32(12, InputPDOLayout.payloadSize, true);
  setUint64(view, 16, BigInt(InputPDOLayout.payloadSize));
  setUint64(view, 24, BigInt(dataVersion));

  setUint64(view, 32, BigInt(sceneId));
  setUint64(view, 40, BigInt(viewportId));
  setUint64(view, 48, BigInt(timestampUS));
  setUint64(view, 56, BigInt(sequence));
  view.setUint32(64, snapshot.flags ?? 0, true);
  view.setUint32(68, 0, true);

  const keyboardOffset = InputPDOLayout.keyboardOffset;
  view.setUint32(keyboardOffset, snapshot.modifierMask ?? 0, true);
  view.setUint32(keyboardOffset + 4, snapshot.lockMask ?? 0, true);
  view.setUint32(keyboardOffset + 8, InputPDOLayout.keyBitCount, true);
  view.setUint32(keyboardOffset + 12, 0, true);
  const words = buildKeyBitWords(keys);
  for (let index = 0; index < InputPDOLayout.keyWordCount; index += 1) {
    setUint64(view, keyboardOffset + 16 + index * 8, words[index]);
  }

  const pointerOffset = InputPDOLayout.pointerOffset;
  view.setUint32(pointerOffset, pointer.pointerKind ?? 1, true);
  view.setUint32(pointerOffset + 4, pointer.buttonMask ?? 0, true);
  view.setUint32(pointerOffset + 8, pointer.flags ?? 0, true);
  view.setUint32(pointerOffset + 12, 0, true);
  setUint64(view, pointerOffset + 16, BigInt(pointer.deviceId ?? 1));
  view.setFloat32(pointerOffset + 24, finite(pointer.x), true);
  view.setFloat32(pointerOffset + 28, finite(pointer.y), true);
  view.setFloat32(pointerOffset + 32, finite(pointer.deltaX), true);
  view.setFloat32(pointerOffset + 36, finite(pointer.deltaY), true);
  view.setFloat32(pointerOffset + 40, finite(pointer.wheelX), true);
  view.setFloat32(pointerOffset + 44, finite(pointer.wheelY), true);
}

function buildKeyBitWords(keys) {
  const words = new Array(InputPDOLayout.keyWordCount).fill(0n);
  for (const keyCode of keys) {
    const code = Number(keyCode);
    if (!Number.isInteger(code) || code < 0 || code >= InputPDOLayout.keyBitCount) {
      continue;
    }
    const word = Math.floor(code / 64);
    const bit = code % 64;
    words[word] |= 1n << BigInt(bit);
  }
  return words;
}

function setUint64(view, offset, value) {
  const bigint = BigInt(value);
  if (typeof view.setBigUint64 === "function") {
    view.setBigUint64(offset, bigint, true);
    return;
  }
  view.setUint32(offset, Number(bigint & 0xffffffffn), true);
  view.setUint32(offset + 4, Number((bigint >> 32n) & 0xffffffffn), true);
}

function finite(value) {
  const number = Number(value ?? 0);
  return Number.isFinite(number) ? number : 0;
}
