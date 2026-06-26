import { AppCommands, ProductCommands } from "./commandNames.mjs";

export class ApplicationClient {
  constructor(mailboxClient, applicationChannelId) {
    this.mailboxClient = mailboxClient;
    this.applicationChannelId = applicationChannelId;
  }

  getState() {
    return this.mailboxClient.send(this.applicationChannelId, AppCommands.getState);
  }

  listProducts() {
    return this.mailboxClient.send(this.applicationChannelId, AppCommands.listProducts);
  }

  startProduct(productId = "") {
    const payload = productId ? { productId } : {};
    return this.mailboxClient.send(this.applicationChannelId, AppCommands.startProduct, payload);
  }

  stopProduct(productId) {
    return this.mailboxClient.send(this.applicationChannelId, AppCommands.stopProduct, { productId });
  }

  resolveProjectFile(projectPath) {
    return this.mailboxClient.send(this.applicationChannelId, AppCommands.resolveProjectFile, { projectPath });
  }

  openProjectFile(projectPath, options = {}) {
    return this.mailboxClient.send(this.applicationChannelId, AppCommands.openProjectFile, {
      projectPath,
      catalogName: options.catalogName ?? "",
      projectName: options.projectName ?? "",
    });
  }
}

export class ProductClient {
  constructor(mailboxClient, productChannelId) {
    this.mailboxClient = mailboxClient;
    this.productChannelId = productChannelId;
  }

  getState() {
    return this.mailboxClient.send(this.productChannelId, ProductCommands.getState);
  }

  listProjectCatalogs() {
    return this.mailboxClient.send(this.productChannelId, ProductCommands.listProjectCatalogs);
  }

  openProjectCatalog(projectPath, options = {}) {
    return this.mailboxClient.send(this.productChannelId, ProductCommands.openProjectCatalog, {
      catalogName: options.catalogName ?? "",
      catalogPath: options.catalogPath ?? projectPath,
      projectName: options.projectName ?? "",
      projectPath,
    });
  }

  closeProjectCatalog(catalogId) {
    return this.mailboxClient.send(this.productChannelId, ProductCommands.closeProjectCatalog, { catalogId });
  }
}

export class ProjectClient {
  constructor(mailboxClient, projectChannelId) {
    this.mailboxClient = mailboxClient;
    this.projectChannelId = projectChannelId;
  }

  execute(command, payload = {}) {
    return this.mailboxClient.send(this.projectChannelId, command, payload);
  }
}
