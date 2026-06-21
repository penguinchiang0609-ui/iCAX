import { createBridge } from "../bridge/createBridge.mjs";
import { ApplicationClient, ProductClient } from "../mailbox/clients.mjs";
import { MailboxClient } from "../mailbox/mailboxClient.mjs";
import { ProductModuleHost } from "../product/productModuleHost.mjs";
import { WorkbenchStore } from "../state/workbenchStore.mjs";
import { mountWorkbench } from "../layout/workbenchView.mjs";

async function main() {
  const root = document.getElementById("app");
  const store = new WorkbenchStore();
  const bridge = await createBridge();
  const mailboxClient = new MailboxClient(bridge);
  const productModuleHost = new ProductModuleHost({ bridge, mailboxClient });

  const actions = createActions({ bridge, mailboxClient, store });
  const view = mountWorkbench(root, actions, { productModuleHost });
  store.subscribe((state) => view.render(state));

  await actions.bootstrap();
}

function createActions({ bridge, mailboxClient, store }) {
  let appClient = null;
  const productClients = new Map();

  async function trackedCommand(label, command) {
    store.beginCommand(label);
    try {
      const result = await command();
      store.endCommand(label);
      return result;
    } catch (error) {
      store.failCommand(label, error);
      throw error;
    }
  }

  function productClientFor(product) {
    const productMailId = product?.productMailId;
    if (!productMailId) {
      throw new Error("Product is not started");
    }

    if (!productClients.has(productMailId)) {
      productClients.set(productMailId, new ProductClient(mailboxClient, productMailId));
    }
    return productClients.get(productMailId);
  }

  return {
    async bootstrap() {
      store.setBridgeStatus("Connecting");
      const applicationMailId = await bridge.getApplicationMailId();
      appClient = new ApplicationClient(mailboxClient, applicationMailId);
      store.patch((state) => {
        state.applicationMailId = applicationMailId;
      });
      store.setBridgeStatus("Connected");

      bridge.onProjectFileOpen?.((projectPath) => {
        this.openProjectFile(projectPath);
      });

      await this.refresh();
    },

    async refresh() {
      const state = await trackedCommand("App.GetState", () => appClient.getState());
      store.applyApplicationState(state);
    },

    async startProduct(productId) {
      const response = await trackedCommand("App.StartProduct", () => appClient.startProduct(productId));
      store.applyStartProductResponse(response);
    },

    activateProduct(productId) {
      store.patch((state) => {
        state.activeProductId = productId;
        state.activeProjectId = "";
      });

      const product = store.getState().productsById.get(productId);
      if (product?.isStarted) {
        const client = productClientFor(product);
        trackedCommand("Product.GetState", () => client.getState())
          .then((productState) => store.applyProductState(productId, productState))
          .catch(() => {});
      }
    },

    activateProject(projectId) {
      store.patch((state) => {
        state.activeProjectId = projectId;
      });
    },

    async openProjectFile(projectPath) {
      const trimmedPath = projectPath.trim();
      if (!trimmedPath) {
        store.failCommand("App.OpenProjectFile", new Error("Project path is empty"));
        return;
      }

      const response = await trackedCommand("App.OpenProjectFile", () => appClient.openProjectFile(trimmedPath));
      store.applyOpenProjectFileResponse(response);
    },

    async chooseAndOpenProjectFile() {
      const activeProduct = store.getState().productsById.get(store.getState().activeProductId);
      const extensions = activeProduct?.projectFile?.fileExtensions?.map((item) => item.replace(".", "")) ?? ["icax"];
      const projectPath = await bridge.openFileDialog?.({ filters: [{ name: "iCAX Project", extensions }] });
      if (projectPath) {
        await this.openProjectFile(projectPath);
      }
    },
  };
}

main().catch((error) => {
  const root = document.getElementById("app");
  root.innerHTML = `<div class="startup-error"><h1>Failed to start iCAX Workbench</h1><pre>${error.message}</pre></div>`;
  console.error(error);
});
