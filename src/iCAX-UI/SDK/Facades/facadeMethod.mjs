export const AppFacade = Object.freeze({
  getState: "App.GetState",
  listProducts: "App.ListProducts",
  startProduct: "App.StartProduct",
  stopProduct: "App.StopProduct",
  resolveProjectFile: "App.ResolveProjectFile",
  openProjectFile: "App.OpenProjectFile",
});

export const ProductFacade = Object.freeze({
  getState: "Product.GetState",
  listProjectCatalogs: "Product.ListProjectCatalogs",
  openProjectCatalog: "Product.OpenProjectCatalog",
  closeProjectCatalog: "Product.CloseProjectCatalog",
});

export const ProjectFacade = Object.freeze({
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

export function validateFacadeIdentifier(name, kind) {
  if (typeof name !== "string" || !identifierPattern.test(name)) {
    throw new TypeError(`${kind} must match ${identifierPattern}: ${name}`);
  }
}

export function parseFacadeMethod(facadeMethod) {
  if (typeof facadeMethod !== "string") {
    throw new TypeError("facade method must be a string");
  }

  const parts = facadeMethod.split(".");
  if (parts.length !== 2) {
    throw new TypeError(`facade method must use FacadeName.MethodName format: ${facadeMethod}`);
  }

  const [facadeName, methodName] = parts;
  validateFacadeIdentifier(facadeName, "facade name");
  validateFacadeIdentifier(methodName, "method name");
  return { facadeName, methodName };
}

export function makeFacadeMethodCode(facadeName, methodName) {
  validateFacadeIdentifier(facadeName, "facade name");
  validateFacadeIdentifier(methodName, "method name");
  return ((BigInt(fnv1a32(facadeName)) << 32n) | BigInt(fnv1a32(methodName))).toString();
}

export function makeFacadeMethodCodeFromName(facadeMethod) {
  const { facadeName, methodName } = parseFacadeMethod(facadeMethod);
  return makeFacadeMethodCode(facadeName, methodName);
}

export function makePDOID(typeName, instanceName) {
  if (!typeName || !instanceName) {
    throw new TypeError("PDO typeName and instanceName are required");
  }
  return ((BigInt(fnv1a32(typeName)) << 32n) | BigInt(fnv1a32(instanceName))).toString();
}
