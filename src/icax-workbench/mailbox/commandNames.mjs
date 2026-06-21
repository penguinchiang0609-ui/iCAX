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

function fnv1a32(text) {
  let hash = 0x811c9dc5;
  for (let index = 0; index < text.length; index += 1) {
    hash ^= text.charCodeAt(index) & 0xff;
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

function makeCommandTypeCode(scope, name) {
  return ((BigInt(fnv1a32(scope)) << 32n) | BigInt(fnv1a32(name))).toString();
}

export const AppCommandTypeCodes = Object.freeze({
  getState: makeCommandTypeCode("iCAX.ApplicationHost", AppCommands.getState),
  listProducts: makeCommandTypeCode("iCAX.ApplicationHost", AppCommands.listProducts),
  startProduct: makeCommandTypeCode("iCAX.ApplicationHost", AppCommands.startProduct),
  stopProduct: makeCommandTypeCode("iCAX.ApplicationHost", AppCommands.stopProduct),
  resolveProjectFile: makeCommandTypeCode("iCAX.ApplicationHost", AppCommands.resolveProjectFile),
  openProjectFile: makeCommandTypeCode("iCAX.ApplicationHost", AppCommands.openProjectFile),
});

export const ProductCommandTypeCodes = Object.freeze({
  getState: makeCommandTypeCode("iCAX.Product", ProductCommands.getState),
  listProjectCatalogs: makeCommandTypeCode("iCAX.Product", ProductCommands.listProjectCatalogs),
  openProjectCatalog: makeCommandTypeCode("iCAX.Product", ProductCommands.openProjectCatalog),
  closeProjectCatalog: makeCommandTypeCode("iCAX.Product", ProductCommands.closeProjectCatalog),
});

const commandTypeCodes = new Map([
  [AppCommands.getState, AppCommandTypeCodes.getState],
  [AppCommands.listProducts, AppCommandTypeCodes.listProducts],
  [AppCommands.startProduct, AppCommandTypeCodes.startProduct],
  [AppCommands.stopProduct, AppCommandTypeCodes.stopProduct],
  [AppCommands.resolveProjectFile, AppCommandTypeCodes.resolveProjectFile],
  [AppCommands.openProjectFile, AppCommandTypeCodes.openProjectFile],
  [ProductCommands.getState, ProductCommandTypeCodes.getState],
  [ProductCommands.listProjectCatalogs, ProductCommandTypeCodes.listProjectCatalogs],
  [ProductCommands.openProjectCatalog, ProductCommandTypeCodes.openProjectCatalog],
  [ProductCommands.closeProjectCatalog, ProductCommandTypeCodes.closeProjectCatalog],
]);

export function getCommandTypeCode(command) {
  return commandTypeCodes.get(command) ?? "";
}
