export const AppSDO = Object.freeze({
  getState: "App.GetState",
  listProducts: "App.ListProducts",
  startProduct: "App.StartProduct",
  stopProduct: "App.StopProduct",
  resolveProjectFile: "App.ResolveProjectFile",
  openProjectFile: "App.OpenProjectFile",
});

export const ProductSDO = Object.freeze({
  getState: "Product.GetState",
  listProjectCatalogs: "Product.ListProjectCatalogs",
  openProjectCatalog: "Product.OpenProjectCatalog",
  closeProjectCatalog: "Product.CloseProjectCatalog",
});

export const ProjectSDO = Object.freeze({
  getState: "Project.GetState",
  undo: "Project.Undo",
  redo: "Project.Redo",
  getUndoRedoState: "Project.GetUndoRedoState",
});

const identifierPattern = /^[A-Z][A-Za-z0-9_]*$/;

export function fnv1a32(text) {
  let hash = 0x811c9dc5;
  for (let index = 0; index < text.length; index += 1) {
    hash ^= text.charCodeAt(index) & 0xff;
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

export function validateSDOIdentifier(name, kind) {
  if (typeof name !== "string" || !identifierPattern.test(name)) {
    throw new TypeError(`${kind} must match ${identifierPattern}: ${name}`);
  }
}

export function parseSDOMethod(sdoMethod) {
  if (typeof sdoMethod !== "string") {
    throw new TypeError("sdo method must be a string");
  }

  const parts = sdoMethod.split(".");
  if (parts.length !== 2) {
    throw new TypeError(`sdo method must use SDOName.MethodName format: ${sdoMethod}`);
  }

  const [sdoName, methodName] = parts;
  validateSDOIdentifier(sdoName, "sdo name");
  validateSDOIdentifier(methodName, "method name");
  return { sdoName, methodName };
}

export function makeSDOMethodCode(sdoName, methodName) {
  validateSDOIdentifier(sdoName, "sdo name");
  validateSDOIdentifier(methodName, "method name");
  return ((BigInt(fnv1a32(sdoName)) << 32n) | BigInt(fnv1a32(methodName))).toString();
}

export function makeSDOMethodCodeFromName(sdoMethod) {
  const { sdoName, methodName } = parseSDOMethod(sdoMethod);
  return makeSDOMethodCode(sdoName, methodName);
}

export function makePDOID(typeName, instanceName) {
  if (!typeName || !instanceName) {
    throw new TypeError("PDO typeName and instanceName are required");
  }
  return ((BigInt(fnv1a32(typeName)) << 32n) | BigInt(fnv1a32(instanceName))).toString();
}
