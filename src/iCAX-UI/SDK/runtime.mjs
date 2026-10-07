import { AppProxy } from "../AppProxy/AppProxy.mjs";
import { createBridge, validateBridge } from "./Bridge/createBridge.mjs";

export async function connectApplication(options = {}) {
  const bridge = options.bridge ? validateBridge(options.bridge) : await createBridge(options.bridgeOptions ?? {});
  return AppProxy.create(bridge, options.app ?? {});
}
