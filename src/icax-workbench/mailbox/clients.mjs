import { AppCommands, ProductCommands } from "./commandNames.mjs";

export class ApplicationClient {
  constructor(mailboxClient, applicationMailId) {
    this.mailboxClient = mailboxClient;
    this.applicationMailId = applicationMailId;
  }

  getState() {
    return this.mailboxClient.send(this.applicationMailId, AppCommands.getState);
  }

  listProducts() {
    return this.mailboxClient.send(this.applicationMailId, AppCommands.listProducts);
  }

  startProduct(productId = "") {
    const payload = productId ? { productId } : {};
    return this.mailboxClient.send(this.applicationMailId, AppCommands.startProduct, payload);
  }

  stopProduct(productId) {
    return this.mailboxClient.send(this.applicationMailId, AppCommands.stopProduct, { productId });
  }

  resolveProjectFile(projectPath) {
    return this.mailboxClient.send(this.applicationMailId, AppCommands.resolveProjectFile, { projectPath });
  }

  openProjectFile(projectPath, options = {}) {
    return this.mailboxClient.send(this.applicationMailId, AppCommands.openProjectFile, {
      projectPath,
      catalogName: options.catalogName ?? "",
      projectName: options.projectName ?? "",
    });
  }
}

export class ProductClient {
  constructor(mailboxClient, productMailId) {
    this.mailboxClient = mailboxClient;
    this.productMailId = productMailId;
  }

  getState() {
    return this.mailboxClient.send(this.productMailId, ProductCommands.getState);
  }

  listProjectCatalogs() {
    return this.mailboxClient.send(this.productMailId, ProductCommands.listProjectCatalogs);
  }

  openProjectCatalog(projectPath, options = {}) {
    return this.mailboxClient.send(this.productMailId, ProductCommands.openProjectCatalog, {
      catalogName: options.catalogName ?? "",
      catalogPath: options.catalogPath ?? projectPath,
      projectName: options.projectName ?? "",
      projectPath,
    });
  }

  closeProjectCatalog(catalogId) {
    return this.mailboxClient.send(this.productMailId, ProductCommands.closeProjectCatalog, { catalogId });
  }
}

export class ProjectClient {
  constructor(mailboxClient, projectMailId) {
    this.mailboxClient = mailboxClient;
    this.projectMailId = projectMailId;
  }

  execute(command, payload = {}) {
    return this.mailboxClient.send(this.projectMailId, command, payload);
  }
}
