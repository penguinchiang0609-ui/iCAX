export const AppCommands = Object.freeze({
  getState: "App.GetState",
  listProducts: "App.ListProducts",
  startProduct: "App.StartProduct",
  stopProduct: "App.StopProduct",
  resolveProjectFile: "App.ResolveProjectFile",
  openProjectFile: "App.OpenProjectFile",
});

export const ProductCommands = Object.freeze({
  getState: "Product.GetState",
  listProjectCatalogs: "Product.ListProjectCatalogs",
  openProjectCatalog: "Product.OpenProjectCatalog",
  closeProjectCatalog: "Product.CloseProjectCatalog",
});

export const ProjectCommands = Object.freeze({
  getState: "Project.GetState",
  undo: "Project.Undo",
  redo: "Project.Redo",
  getUndoRedoState: "Project.GetUndoRedoState",
});

const commandNamePattern = /^[A-Z][A-Za-z0-9_]*$/;

export function fnv1a32(text) {
  let hash = 0x811c9dc5;
  for (let index = 0; index < text.length; index += 1) {
    hash ^= text.charCodeAt(index) & 0xff;
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

export function validateCommandNamePart(name, kind = "command name") {
  if (!commandNamePattern.test(name)) {
    throw new TypeError(`${kind} must match ${commandNamePattern}: ${name}`);
  }
}

export function parseCommand(command) {
  if (typeof command !== "string") {
    throw new TypeError("command must be a string");
  }

  const dotIndex = command.indexOf(".");
  if (dotIndex <= 0 || dotIndex !== command.lastIndexOf(".") || dotIndex === command.length - 1) {
    throw new TypeError(`command must use Main.Sub format: ${command}`);
  }

  const main = command.slice(0, dotIndex);
  const sub = command.slice(dotIndex + 1);
  validateCommandNamePart(main, "main command name");
  validateCommandNamePart(sub, "sub command name");
  return { main, sub };
}

export function makeCommandTypeCode(main, sub) {
  validateCommandNamePart(main, "main command name");
  validateCommandNamePart(sub, "sub command name");
  return ((BigInt(fnv1a32(main)) << 32n) | BigInt(fnv1a32(sub))).toString();
}

export function makeCommandTypeCodeFromCommand(command) {
  const { main, sub } = parseCommand(command);
  return makeCommandTypeCode(main, sub);
}

export function makePDOID(typeName, instanceName) {
  if (!typeName || !instanceName) {
    throw new TypeError("PDO typeName and instanceName are required");
  }
  return ((BigInt(fnv1a32(typeName)) << 32n) | BigInt(fnv1a32(instanceName))).toString();
}
