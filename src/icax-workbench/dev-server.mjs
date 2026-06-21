import { createReadStream, promises as fs } from "node:fs";
import { createServer } from "node:http";
import { extname, normalize, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL(".", import.meta.url));
const workspaceRoot = fileURLToPath(new URL("../", import.meta.url));
const port = Number(process.env.PORT || process.argv[2] || 4173);
const host = process.env.HOST || "127.0.0.1";

const mimeTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".js", "application/javascript; charset=utf-8"],
  [".mjs", "application/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".svg", "image/svg+xml"],
  [".png", "image/png"],
  [".jpg", "image/jpeg"],
  [".jpeg", "image/jpeg"],
  [".ico", "image/x-icon"],
  [".wasm", "application/wasm"],
]);

const server = createServer(async (request, response) => {
  try {
    const requestUrl = new URL(request.url ?? "/", `http://${host}:${port}`);
    const filePath = resolveRequestPath(requestUrl.pathname);
    const stat = await fs.stat(filePath);

    if (!stat.isFile()) {
      sendStatus(response, 404, "Not found");
      return;
    }

    response.writeHead(200, {
      "Content-Type": mimeTypes.get(extname(filePath).toLowerCase()) ?? "application/octet-stream",
      "Cache-Control": "no-store",
    });
    createReadStream(filePath).pipe(response);
  } catch (error) {
    if (error.code === "ENOENT") {
      sendStatus(response, 404, "Not found");
      return;
    }

    sendStatus(response, 500, error.message);
  }
});

server.listen(port, host, () => {
  console.log(`iCAX Workbench dev server: http://${host}:${port}/`);
});

function resolveRequestPath(pathname) {
  const decodedPath = decodeURIComponent(pathname);
  const localPath = decodedPath === "/" ? "index.html" : decodedPath.replace(/^\/+/, "");
  const normalizedPath = normalize(localPath);
  const basePath = normalizedPath.startsWith(`apps${sep}`) || normalizedPath === "apps" ? workspaceRoot : root;
  const fullPath = resolve(basePath, normalizedPath);
  const relativePath = relative(basePath, fullPath);

  if (relativePath.startsWith("..") || relativePath.includes(`..${sep}`)) {
    throw Object.assign(new Error("Forbidden"), { code: "EACCES" });
  }

  return fullPath;
}

function sendStatus(response, statusCode, message) {
  response.writeHead(statusCode, { "Content-Type": "text/plain; charset=utf-8" });
  response.end(message);
}
