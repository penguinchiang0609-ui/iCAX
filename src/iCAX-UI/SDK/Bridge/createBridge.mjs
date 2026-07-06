import { MockHostBridge } from "./mockHostBridge.mjs";

export async function createBridge(options = {}) {
  const hostBridge = globalThis.icax ?? globalThis.icaxNativeBridge;
  if (hostBridge) {
    return validateBridge(hostBridge, "host bridge");
  }
  if (options.allowMock === false) {
    throw new Error("Host bridge is not available");
  }
  return validateBridge(new MockHostBridge(options.mock ?? {}), "mock bridge");
}

export function validateBridge(bridge, name = "bridge") {
  if (!bridge) {
    throw new TypeError(`${name} is required`);
  }

  const requiredMethods = [
    "getApplicationChannelId",
    "registerProductChannel",
    "registerSceneChannel",
    "postMail",
    "subscribeMail",
  ];
  for (const method of requiredMethods) {
    if (typeof bridge[method] !== "function") {
      throw new TypeError(`${name}.${method} must be a function`);
    }
  }

  if (bridge.pdo !== undefined && bridge.pdo !== null && typeof bridge.pdo.withRead !== "function") {
    throw new TypeError(`${name}.pdo.withRead must be a function when pdo is provided`);
  }
  if (bridge.pdo?.withWrite !== undefined && typeof bridge.pdo.withWrite !== "function") {
    throw new TypeError(`${name}.pdo.withWrite must be a function when provided`);
  }

  return bridge;
}
