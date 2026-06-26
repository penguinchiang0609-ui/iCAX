import { AppCommands, ProductCommands } from "../mailbox/commandNames.mjs";

const APPLICATION_CHANNEL_ID = "application";
const MOCK_DELAY_MS = 40;

function makeProduct() {
  return {
    productId: "sample",
    productName: "Sample Product",
    productVersion: "1.0",
    frontendEntry: "sample/frontend/entry",
    defaultProjectStartupComponent: "SampleStartup",
    projectFile: {
      magic: "ICAX_SAMPLE",
      formatVersion: "1.0",
      fileExtensions: [".icax"],
      magicOffset: 0,
      probeBytes: 256,
    },
    isStarted: false,
    productChannelId: "",
    recentProjects: [
      {
        path: "D:/projects/sample.icax",
        displayName: "sample.icax",
        lastOpenedTime: "2026-06-20T00:00:00Z",
      },
    ],
  };
}

export class MockIcaxBridge {
  constructor(options = {}) {
    this.subscribers = new Map();
    this.products = options.products ?? [makeProduct()];
    this.catalogsByProductId = new Map();
    this.projectsByChannelId = new Map();
    this.nextMailId = 1000;
    this.nextCatalogId = 1;
    this.nextProjectId = 1;
  }

  async getApplicationChannelId() {
    return APPLICATION_CHANNEL_ID;
  }

  subscribeMail(channelId, handler) {
    if (!this.subscribers.has(channelId)) {
      this.subscribers.set(channelId, new Set());
    }
    const handlers = this.subscribers.get(channelId);
    handlers.add(handler);
    return () => handlers.delete(handler);
  }

  postMail(channelId, mail) {
    setTimeout(() => {
      const response = this.#handleMail(channelId, mail);
      this.#emit(channelId, response);
    }, MOCK_DELAY_MS);
  }

  async openFileDialog() {
    const product = this.products.find((item) => item.isStarted) ?? this.products[0];
    return product?.recentProjects?.[0]?.path ?? `D:/projects/sample${product?.projectFile?.fileExtensions?.[0] ?? ".icax"}`;
  }

  onProjectFileOpen(handler) {
    this.projectFileOpenHandler = handler;
    return () => {
      if (this.projectFileOpenHandler === handler) {
        this.projectFileOpenHandler = null;
      }
    };
  }

  simulateProjectFileOpen(projectPath) {
    if (this.projectFileOpenHandler) {
      this.projectFileOpenHandler(projectPath);
    }
  }

  #handleMail(channelId, mail) {
    try {
      if (channelId === APPLICATION_CHANNEL_ID) {
        return this.#makeReply(mail, this.#handleApplicationCommand(mail));
      }

      const product = this.products.find((item) => item.productChannelId === channelId);
      if (product) {
        return this.#makeReply(mail, this.#handleProductCommand(product, mail));
      }

      const projectRuntime = this.projectsByChannelId.get(channelId);
      if (projectRuntime) {
        return this.#makeReply(mail, this.#handleProjectCommand(projectRuntime, mail));
      }

      return this.#makeReply(mail, { error: "channelId not found" }, "NoHandler");
    } catch (error) {
      return this.#makeReply(mail, { message: error.message }, "ExecutionError");
    }
  }

  #handleApplicationCommand(mail) {
    switch (mail.command) {
      case AppCommands.getState:
      case AppCommands.listProducts:
        return this.#applicationState();
      case AppCommands.startProduct:
        return this.#startProduct(mail.payload?.productId ?? "");
      case AppCommands.stopProduct:
        return this.#stopProduct(mail.payload?.productId ?? "");
      case AppCommands.resolveProjectFile:
        return {
          applicationChannelId: APPLICATION_CHANNEL_ID,
          resolve: this.#resolveProjectFile(mail.payload?.projectPath ?? ""),
        };
      case AppCommands.openProjectFile:
        return this.#openProjectFile(mail.payload?.projectPath ?? "", mail.payload ?? {});
      default:
        return { error: `No application command handler: ${mail.command}` };
    }
  }

  #handleProductCommand(product, mail) {
    switch (mail.command) {
      case ProductCommands.getState:
      case ProductCommands.listProjectCatalogs:
        return this.#productState(product);
      case ProductCommands.openProjectCatalog:
        return this.#openProjectCatalog(product, mail.payload?.projectPath ?? "", mail.payload ?? {});
      case ProductCommands.closeProjectCatalog:
        return this.#closeProjectCatalog(product, mail.payload?.catalogId ?? "");
      default:
        return { error: `No product command handler: ${mail.command}` };
    }
  }

  #applicationState() {
    return {
      applicationChannelId: APPLICATION_CHANNEL_ID,
      state: "Running",
      phase: "Running",
      productCount: this.products.length,
      products: this.products.map((product) => this.#productSummary(product)),
      runningProductCount: this.products.filter((product) => product.isStarted).length,
      faultMessage: "",
    };
  }

  #productState(product) {
    return {
      ...this.#productSummary(product),
      catalogCount: this.#catalogs(product.productId).length,
      catalogs: this.#catalogs(product.productId),
    };
  }

  #productSummary(product) {
    const { mockBackend, ...summary } = product;
    return {
      ...summary,
      runtime: product.isStarted ? {
        catalogCount: this.#catalogs(product.productId).length,
        catalogs: this.#catalogs(product.productId),
      } : undefined,
    };
  }

  #startProduct(productId) {
    const product = this.#findProduct(productId);
    if (!product.isStarted) {
      product.isStarted = true;
      product.productChannelId = `product:${product.productId}`;
    }

    return {
          applicationChannelId: APPLICATION_CHANNEL_ID,
      product: this.#productSummary(product),
      state: this.#applicationState(),
    };
  }

  #stopProduct(productId) {
    const product = this.#findProduct(productId);
    product.isStarted = false;
    product.productChannelId = "";
    this.catalogsByProductId.set(product.productId, []);
    return this.#applicationState();
  }

  #resolveProjectFile(projectPath) {
    if (!projectPath) {
      return {
        projectPath,
        status: "NotFound",
        productId: "",
        candidateProductIds: [],
        matchedByMagic: false,
      };
    }

    const product = this.#productForProjectPath(projectPath);
    if (!product) {
      return {
        projectPath,
        status: "NotResolved",
        productId: "",
        candidateProductIds: [],
        matchedByMagic: false,
      };
    }

    return {
      projectPath,
      status: "Resolved",
      productId: product.productId,
      candidateProductIds: [product.productId],
      matchedByMagic: true,
    };
  }

  #openProjectFile(projectPath, options) {
    const resolve = this.#resolveProjectFile(projectPath);
    if (resolve.status !== "Resolved") {
      throw new Error("Project file product is not resolved");
    }

    const startResult = this.#startProduct(resolve.productId);
    const product = this.#findProduct(resolve.productId);
    const openResult = this.#openProjectCatalog(product, projectPath, options);

    return {
      applicationChannelId: APPLICATION_CHANNEL_ID,
      resolve,
      product: startResult.product,
      catalog: openResult.catalog,
      state: this.#applicationState(),
    };
  }

  #openProjectCatalog(product, projectPath, options) {
    const name = options.projectName || this.#fileStem(projectPath) || "Project";
    const projectSequence = this.nextProjectId++;
    const projectId = `project:${projectSequence}`;
    const catalog = {
      catalogId: `catalog:${this.nextCatalogId++}`,
      catalogName: options.catalogName || name,
      catalogPath: options.catalogPath || projectPath,
      hasMainProject: true,
      mainProjectId: projectId,
      mainProject: {
        projectId,
        projectChannelId: projectId,
        projectName: name,
        projectPath,
        startupComponent: product.defaultProjectStartupComponent || "ProjectStartup",
        role: "Main",
        state: "Running",
        isMainProject: true,
        isTransientProject: false,
        isOpen: true,
        isRunning: true,
        faultMessage: "",
      },
    };

    this.#catalogs(product.productId).push(catalog);
    this.projectsByChannelId.set(projectId, {
      productId: product.productId,
      catalogId: catalog.catalogId,
      project: catalog.mainProject,
      state: product.mockBackend?.createProjectState?.({
        product,
        catalog,
        project: catalog.mainProject,
      }) ?? {},
    });
    product.recentProjects = [{
      path: projectPath,
      displayName: name,
      lastOpenedTime: new Date().toISOString(),
    }, ...product.recentProjects.filter((item) => item.path !== projectPath)].slice(0, 20);

    return {
      productId: product.productId,
      productChannelId: product.productChannelId,
      catalog,
      state: this.#productState(product),
    };
  }

  #closeProjectCatalog(product, catalogId) {
    const catalogs = this.#catalogs(product.productId);
    for (const catalog of catalogs) {
      if (catalog.catalogId === catalogId && catalog.mainProject?.projectChannelId) {
        this.projectsByChannelId.delete(catalog.mainProject.projectChannelId);
      }
    }
    this.catalogsByProductId.set(product.productId, catalogs.filter((catalog) => catalog.catalogId !== catalogId));
    return this.#productState(product);
  }

  #handleProjectCommand(projectRuntime, mail) {
    const product = this.#findProduct(projectRuntime.productId);
    if (!product.mockBackend?.handleProjectCommand) {
      return { error: `No project command handler: ${mail.command}` };
    }

    return product.mockBackend.handleProjectCommand({
      command: mail.command,
      payload: mail.payload ?? {},
      product,
      project: projectRuntime.project,
      state: projectRuntime.state,
    });
  }

  #findProduct(productId) {
    if (!productId && this.products.length === 1) {
      return this.products[0];
    }

    const product = this.products.find((item) => item.productId === productId);
    if (!product) {
      throw new Error(`Product not found: ${productId}`);
    }
    return product;
  }

  #catalogs(productId) {
    if (!this.catalogsByProductId.has(productId)) {
      this.catalogsByProductId.set(productId, []);
    }
    return this.catalogsByProductId.get(productId);
  }

  #productForProjectPath(projectPath) {
    const extension = this.#fileExtension(projectPath).toLowerCase();
    if (!extension) {
      return null;
    }
    return this.products.find((product) => {
      const extensions = product.projectFile?.fileExtensions ?? [];
      return extensions.some((item) => item.toLowerCase() === extension);
    }) ?? null;
  }

  #makeReply(request, payload, stamp = "Ok") {
    return {
      id: this.nextMailId++,
      originId: request.id,
      command: request.command,
      typeCode: request.typeCode ?? "",
      stamp,
      payload,
    };
  }

  #emit(channelId, mail) {
    const handlers = this.subscribers.get(channelId);
    if (!handlers) {
      return;
    }
    for (const handler of handlers) {
      handler(mail);
    }
  }

  #fileStem(projectPath) {
    const normalized = projectPath.replaceAll("\\", "/");
    const fileName = normalized.split("/").pop() ?? "";
    const dotIndex = fileName.lastIndexOf(".");
    return dotIndex > 0 ? fileName.slice(0, dotIndex) : fileName;
  }

  #fileExtension(projectPath) {
    const normalized = projectPath.replaceAll("\\", "/");
    const fileName = normalized.split("/").pop() ?? "";
    const dotIndex = fileName.lastIndexOf(".");
    return dotIndex > 0 ? fileName.slice(dotIndex) : "";
  }
}
