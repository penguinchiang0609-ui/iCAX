import { HostBridgeAdapter } from "./hostBridge.mjs";
import { MockIcaxBridge } from "./mockBridge.mjs";

export function createBridge() {
  const hostBridge = globalThis.icaxBridge ?? globalThis.icaxNativeBridge;
  if (hostBridge) {
    return new HostBridgeAdapter(hostBridge);
  }

  const bridge = new MockIcaxBridge();
  globalThis.icaxMockBridge = bridge;
  return bridge;
}
