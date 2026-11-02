import { createWindowStateMemory } from "../Forms/windowStateMemory.mjs";

const installed = new WeakSet();
const methods = ["openFileDialog", "saveFileDialog", "openDirectoryDialog"];

function dialogKey(method, options) {
  const explicit = String(options.memoryKey ?? "").trim();
  return `${method}:${explicit || JSON.stringify({
    title: String(options.title ?? ""),
    filters: options.filters ?? [],
    extension: String(options.defaultExtension ?? ""),
  })}`;
}

function isAbsolute(path) {
  return /^[A-Za-z]:[\\/]/.test(path) || /^[\\/]/.test(path);
}

function parentDirectory(path) {
  const index = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  if (index < 0) return "";
  // Preserve both Windows drive roots and POSIX roots.
  return path.slice(0, index || 1) + (index === 2 && /^[A-Za-z]:/.test(path) ? path[index] : "");
}

/**
 * Remember native chooser locations through the shared user window-state store.
 * Install on the bridge itself so its identity, prototypes and native bindings
 * remain unchanged, including consumers that access globalThis.icax directly.
 */
export function installDialogPathMemory(bridge, options = {}) {
  if (!bridge || installed.has(bridge)) return bridge;
  const memory = options.memory ?? createWindowStateMemory({
    namespace: options.namespace ?? "icax.native-dialogs",
    ...(options.storage === undefined ? {} : { storage: options.storage }),
  });
  for (const method of methods) {
    const original = bridge[method];
    if (typeof original !== "function") continue;
    bridge[method] = async function (dialogOptions = {}) {
      const requested = dialogOptions ?? {};
      const prepared = { ...requested };
      delete prepared.memoryKey;
      // A page can exclude one chooser from its user preference policy entirely.
      // Preserve its native options without reading or updating the shared store.
      if (requested.memoryKey === false) return original.call(bridge, prepared);
      const key = dialogKey(method, requested);
      const remembered = String(memory.read(key, "directory", "") ?? "");
      if (method === "openDirectoryDialog") {
        if (!prepared.initialDirectory && remembered) prepared.initialDirectory = remembered;
      } else {
        const defaultPath = String(prepared.defaultPath ?? "");
        // An entity path keeps its own location. A bare filename (or an empty
        // open chooser) follows the last folder through the directory field.
        if (!isAbsolute(defaultPath) && !/[\\/]/.test(defaultPath)) {
          if (!prepared.initialDirectory && remembered) prepared.initialDirectory = remembered;
        }
      }
      const result = await original.call(bridge, prepared);
      if (typeof result === "string" && result.trim()) {
        const directory = method === "openDirectoryDialog" ? result : parentDirectory(result);
        if (directory) memory.write(key, "directory", directory);
      }
      return result;
    };
  }
  installed.add(bridge);
  return bridge;
}
