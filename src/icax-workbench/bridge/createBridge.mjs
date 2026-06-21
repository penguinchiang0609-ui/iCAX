import { HostBridgeAdapter } from "./hostBridge.mjs";
import { MockIcaxBridge } from "./mockBridge.mjs";

export async function createBridge() {
  const hostBridge = globalThis.icaxBridge ?? globalThis.icaxNativeBridge;
  if (hostBridge) {
    return new HostBridgeAdapter(hostBridge);
  }

  const products = await loadMockProductsFromLocation();
  const bridge = new MockIcaxBridge(products.length > 0 ? { products } : undefined);
  globalThis.icaxMockBridge = bridge;
  return bridge;
}

async function loadMockProductsFromLocation() {
  const location = globalThis.location;
  if (!location?.search) {
    return [];
  }

  const params = new URLSearchParams(location.search);
  const manifestEntries = params.getAll("mockProductManifest");
  const products = [];
  for (const entry of manifestEntries) {
    const product = await loadMockProduct(entry);
    if (product) {
      products.push(product);
    }
  }
  return products;
}

async function loadMockProduct(entry) {
  const moduleUrl = resolveManifestEntry(entry);
  const manifest = await import(moduleUrl);
  if (typeof manifest.createMockProduct === "function") {
    return manifest.createMockProduct();
  }
  if (typeof manifest.default === "function") {
    return manifest.default();
  }
  if (manifest.default && typeof manifest.default === "object") {
    return { ...manifest.default };
  }
  throw new Error(`Mock product manifest does not export createMockProduct(): ${entry}`);
}

function resolveManifestEntry(entry) {
  const value = String(entry ?? "").trim();
  if (!value) {
    throw new Error("mockProductManifest is empty");
  }
  if (/^(?:https?:|data:|blob:)/i.test(value) || value.startsWith("/")) {
    return value;
  }
  return `/${value.replace(/^src\//, "")}`;
}
