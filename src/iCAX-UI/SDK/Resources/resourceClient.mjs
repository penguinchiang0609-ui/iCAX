export class ResourceClient {
  constructor(options = {}) {
    this.bridge = options.bridge ?? globalThis.icax ?? null;
  }

  updateScope(options = {}) {
    if (options.bridge !== undefined) {
      this.bridge = options.bridge;
    }
    return this;
  }

  async fetch(url, init = {}) {
    if (typeof this.bridge?.requestResource !== "function") {
      throw new Error("Host bridge does not provide direct resource access");
    }
    const method = String(init.method ?? "GET").toUpperCase();
    const requestHeaders = normalizeHeaders(init.headers);
    const body = await normalizeBody(init.body);
    const result = await this.bridge.requestResource({
      method,
      url: String(url ?? ""),
      headers: requestHeaders,
      body,
    });

    const status = Number(result?.status ?? 500);
    const responseBody = method === "HEAD"
      || status === 204
      || status === 205
      || status === 304
      ? null
      : normalizeResponseBody(result?.body);
    return new Response(responseBody, {
      status,
      headers: result?.headers ?? {},
    });
  }

  head(url, init = {}) {
    return this.fetch(url, { ...init, method: "HEAD" });
  }

  get(url, init = {}) {
    return this.fetch(url, { ...init, method: "GET" });
  }

  async getMany(requests) {
    if (!Array.isArray(requests) || !requests.length || requests.length > 4) {
      throw new TypeError("resource batch must contain 1 to 4 requests");
    }
    if (typeof this.bridge?.requestResources !== "function") {
      return Promise.all(requests.map(({ url, init }) => this.get(url, init)));
    }
    const payload = requests.map(({ url, init = {} }) => ({
      method: "GET",
      url: String(url ?? ""),
      headers: normalizeHeaders(init.headers),
      body: new ArrayBuffer(0),
    }));
    const results = await this.bridge.requestResources(payload);
    if (!Array.isArray(results) || results.length !== requests.length) {
      throw new Error("Host bridge returned an incomplete resource batch");
    }
    return results.map((result) => new Response(normalizeResponseBody(result?.body), {
      status: Number(result?.status ?? 500),
      headers: result?.headers ?? {},
    }));
  }

  post(collectionUrl, body, init = {}) {
    return this.fetch(collectionUrl, {
      ...init,
      method: "POST",
      body,
    });
  }

  create(url, body, init = {}) {
    const headers = new Headers(init.headers);
    headers.set("If-None-Match", "*");
    return this.fetch(url, {
      ...init,
      method: "PUT",
      headers,
      body,
    });
  }

  put(url, body, init = {}) {
    return this.fetch(url, { ...init, method: "PUT", body });
  }

  delete(url, init = {}) {
    return this.fetch(url, { ...init, method: "DELETE" });
  }

  options(url, init = {}) {
    return this.fetch(url, { ...init, method: "OPTIONS" });
  }
}

function normalizeHeaders(headers) {
  if (headers === undefined || headers === null) {
    return {};
  }
  const normalized = {};
  for (const [name, value] of new Headers(headers)) {
    normalized[name] = value;
  }
  return normalized;
}

async function normalizeBody(body) {
  if (body === undefined || body === null) {
    return new ArrayBuffer(0);
  }
  if (body instanceof ArrayBuffer) {
    return body.slice(0);
  }
  if (ArrayBuffer.isView(body)) {
    return body.buffer.slice(
      body.byteOffset,
      body.byteOffset + body.byteLength,
    );
  }
  if (typeof Blob !== "undefined" && body instanceof Blob) {
    return body.arrayBuffer();
  }
  throw new TypeError(
    "resource body must be an ArrayBuffer, ArrayBufferView, or Blob",
  );
}

function normalizeResponseBody(body) {
  if (body === undefined || body === null) {
    return new ArrayBuffer(0);
  }
  if (body instanceof ArrayBuffer) {
    return body;
  }
  if (ArrayBuffer.isView(body)) {
    return body.buffer.slice(
      body.byteOffset,
      body.byteOffset + body.byteLength,
    );
  }
  throw new TypeError("Host bridge returned an invalid resource body");
}
