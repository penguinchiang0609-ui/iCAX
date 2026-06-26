export function resolveFrontendEntry(entry, baseUrl, options = {}) {
  const value = String(entry ?? "").trim();
  if (!value) {
    throw new Error("Product frontend entry is empty");
  }

  if (/^(https?:|file:)/i.test(value)) {
    if (!options.allowExternalEntries) {
      throw new Error("External product frontend entries are not allowed");
    }
    return value;
  }
  if (value.startsWith("/")) {
    return value;
  }
  if (value.startsWith("apps/")) {
    if (!baseUrl) {
      throw new Error("baseUrl is required for app relative product frontend entry");
    }
    return new URL(`../../../../${value}`, baseUrl).href;
  }
  if (!baseUrl) {
    throw new Error("baseUrl is required for relative product frontend entry");
  }
  return new URL(value, baseUrl).href;
}

export async function loadProductModule(product, cache, options = {}) {
  if (!product?.productId) {
    throw new TypeError("product.productId is required");
  }
  if (!product.frontendEntry) {
    throw new TypeError("product.frontendEntry is required");
  }

  if (cache?.has(product.productId)) {
    return cache.get(product.productId);
  }

  const moduleUrl = resolveFrontendEntry(product.frontendEntry, options.baseUrl, options);
  const module = await import(moduleUrl);
  if (typeof module.mountProduct !== "function" && typeof module.mountProject !== "function") {
    throw new TypeError(`Product module does not export mountProduct or mountProject: ${product.productId}`);
  }

  cache?.set(product.productId, module);
  return module;
}

export function mountProductModule(module, context) {
  if (!module) {
    return undefined;
  }

  if (context.mode === "project" && typeof module.mountProject === "function") {
    return module.mountProject(context);
  }

  if (context.mode === "product" && typeof module.mountProduct === "function") {
    return module.mountProduct(context);
  }
  return undefined;
}
