const canonicalGuidPattern =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const safeIdPattern = /^[A-Za-z0-9._-]+$/;

export function generateResourceId() {
  if (typeof globalThis.crypto?.randomUUID === "function") {
    return globalThis.crypto.randomUUID().toLowerCase();
  }

  const bytes = new Uint8Array(16);
  if (typeof globalThis.crypto?.getRandomValues === "function") {
    globalThis.crypto.getRandomValues(bytes);
  } else {
    for (let index = 0; index < bytes.length; index += 1) {
      bytes[index] = Math.floor(Math.random() * 256);
    }
  }
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  const text = [...bytes]
    .map((value) => value.toString(16).padStart(2, "0"))
    .join("");
  return [
    text.slice(0, 8),
    text.slice(8, 12),
    text.slice(12, 16),
    text.slice(16, 20),
    text.slice(20),
  ].join("-");
}

export function makeResourceCollectionURL(scope = {}) {
  const applicationId = requireSafeId(
    scope.applicationId,
    "applicationId",
  );
  let url = `resource://${applicationId}`;

  if (scope.productId !== undefined && scope.productId !== null && scope.productId !== "") {
    url += `/products/${requireSafeId(scope.productId, "productId")}`;
  } else {
    requireAbsent(scope.projectId, "projectId");
    requireAbsent(scope.sceneId, "sceneId");
    return `${url}/resources`;
  }

  if (scope.projectId) {
    url += `/projects/${requireGuid(scope.projectId, "projectId")}`;
  } else {
    requireAbsent(scope.sceneId, "sceneId");
    return `${url}/resources`;
  }

  if (scope.sceneId) {
    url += `/scenes/${requireGuid(scope.sceneId, "sceneId")}`;
  }
  return `${url}/resources`;
}

export function makeResourceURL(scope, resourceId) {
  return `${makeResourceCollectionURL(scope)}/${requireGuid(resourceId, "resourceId")}`;
}

export function allocateResourceURL(scope) {
  const resourceId = generateResourceId();
  return {
    resourceId,
    url: makeResourceURL(scope, resourceId),
  };
}

export function parseResourceURL(value) {
  const text = String(value ?? "");
  const prefix = "resource://";
  if (!text.startsWith(prefix) || text.includes("?") || text.includes("#")) {
    throw new TypeError("resource URL is not canonical");
  }

  const remainder = text.slice(prefix.length);
  const slash = remainder.indexOf("/");
  if (slash <= 0) {
    throw new TypeError("resource URL has no resources collection");
  }
  const path = remainder.slice(slash + 1);
  if (!path || path.endsWith("/")) {
    throw new TypeError("resource URL contains an empty path segment");
  }
  const segments = path.split("/");
  if (segments.some((segment) => !segment)) {
    throw new TypeError("resource URL contains an empty path segment");
  }

  const result = {
    applicationId: requireSafeId(remainder.slice(0, slash), "applicationId"),
    productId: "",
    projectId: "",
    sceneId: "",
    resourceId: "",
  };
  let index = 0;
  if (segments[index] === "products") {
    result.productId = requireSafeId(segments[index + 1], "productId");
    index += 2;
  }
  if (segments[index] === "projects") {
    if (!result.productId) {
      throw new TypeError("project resource URL requires productId");
    }
    result.projectId = requireGuid(segments[index + 1], "projectId");
    index += 2;
  }
  if (segments[index] === "scenes") {
    if (!result.projectId) {
      throw new TypeError("scene resource URL requires projectId");
    }
    result.sceneId = requireGuid(segments[index + 1], "sceneId");
    index += 2;
  }
  if (segments[index] !== "resources") {
    throw new TypeError("resource URL has no resources collection");
  }
  index += 1;
  if (index < segments.length) {
    result.resourceId = requireGuid(segments[index], "resourceId");
    index += 1;
  }
  if (index !== segments.length) {
    throw new TypeError("resource URL has unexpected trailing segments");
  }
  result.scope = result.sceneId
    ? "scene"
    : result.projectId
      ? "project"
      : result.productId
        ? "product"
        : "application";
  result.isCollection = !result.resourceId;
  return result;
}

function requireSafeId(value, name) {
  const text = String(value ?? "");
  if (!safeIdPattern.test(text) || text === "." || text === ".." || text.includes("..")) {
    throw new TypeError(`${name} is not a URL-safe stable ID`);
  }
  return text;
}

function requireGuid(value, name) {
  const text = String(value ?? "");
  if (!canonicalGuidPattern.test(text)
    || text === "00000000-0000-0000-0000-000000000000") {
    throw new TypeError(`${name} must be a canonical non-nil GUID`);
  }
  return text;
}

function requireAbsent(value, name) {
  if (value !== undefined && value !== null && value !== "") {
    throw new TypeError(`${name} requires its parent resource scope`);
  }
}
