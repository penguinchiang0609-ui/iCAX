import { createWindowStateMemory } from "../../Forms/windowStateMemory.mjs";

export function createNewProjectMemory(options = {}) {
  const memory = options.memory ?? createWindowStateMemory({
    namespace: "icax.app-shell.window-state",
    ...(options.storage === undefined ? {} : { storage: options.storage }),
  });
  const key = productId => `new-project:${productId ?? ""}`;
  return {
    restore(productId, defaults) {
      const name = memory.read(key(productId), "name", defaults.name);
      const path = memory.read(key(productId), "path", undefined);
      return {
        name: typeof name === "string" ? name : defaults.name,
        path: typeof path === "string" ? path : defaults.path,
        // A restored user draft must not be overwritten by name-to-path defaults.
        pathTouched: typeof path === "string",
      };
    },
    remember(productId, draft) {
      memory.write(key(productId), "name", draft.name);
      memory.write(key(productId), "path", draft.path);
    },
  };
}
