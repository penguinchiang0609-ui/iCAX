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
