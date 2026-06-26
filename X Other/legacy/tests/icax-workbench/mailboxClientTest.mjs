import assert from "node:assert/strict";
import { ApplicationClient, ProjectClient } from "../../icax-workbench/mailbox/clients.mjs";
import { MailboxClient, MailboxTimeoutError } from "../../icax-workbench/mailbox/mailboxClient.mjs";
import {
  AppCommands,
  ProductCommands,
  getCommandTypeCode,
  makeCommandTypeCode,
  makeCommandTypeCodeFromCommand,
} from "../../icax-workbench/mailbox/commandNames.mjs";
import { MockIcaxBridge } from "../../icax-workbench/bridge/mockBridge.mjs";
import { createFlatLaserCamMockProduct } from "../../apps/flat-laser-cam/frontend/productManifest.mjs";
import { FlatLaserCommands } from "../../apps/flat-laser-cam/frontend/flatLaserProtocol.mjs";

async function testRequestResolvesFromResponseMailOnly() {
  const bridge = new MockIcaxBridge();
  const mailboxClient = new MailboxClient(bridge, { timeoutMs: 1000 });
  const appClient = new ApplicationClient(mailboxClient, await bridge.getApplicationChannelId());

  let settled = false;
  const promise = appClient.getState().then((state) => {
    settled = true;
    return state;
  });

  await Promise.resolve();
  assert.equal(settled, false, "mailbox request must not settle synchronously after postMail");

  const state = await promise;
  assert.equal(settled, true);
  assert.equal(state.applicationChannelId, "application");
  assert.equal(state.products.length, 1);
}

async function testOpenFlatLaserProjectFileResolvesProduct() {
  const bridge = new MockIcaxBridge({
    products: [createFlatLaserCamMockProduct()],
  });
  const mailboxClient = new MailboxClient(bridge, { timeoutMs: 1000 });
  const appClient = new ApplicationClient(mailboxClient, await bridge.getApplicationChannelId());

  const response = await appClient.openProjectFile("D:/Projects/laser/demo-sheet.ilcam");

  assert.equal(response.resolve.matchedByMagic, true);
  assert.equal(response.product.productId, "icax.flat-laser-cam");
  assert.equal(response.catalog.mainProject.startupComponent, "CFlatLaserProjectComponent");
}

async function testFlatLaserProjectCommandsMutateProjectState() {
  const bridge = new MockIcaxBridge({
    products: [createFlatLaserCamMockProduct()],
  });
  const mailboxClient = new MailboxClient(bridge, { timeoutMs: 1000 });
  const appClient = new ApplicationClient(mailboxClient, await bridge.getApplicationChannelId());

  const opened = await appClient.openProjectFile("D:/Projects/laser/demo-sheet.ilcam");
  const projectClient = new ProjectClient(mailboxClient, opened.catalog.mainProject.projectChannelId);

  const imported = await projectClient.execute(FlatLaserCommands.importDrawing, { fileName: "demo-nesting.dxf" });
  assert.equal(imported.state.parts.length, 3);
  assert.equal(imported.state.operation.progress, 18);

  const nested = await projectClient.execute(FlatLaserCommands.generateNesting);
  assert.equal(nested.state.sheets[0].utilization, 78.4);
  assert.equal(nested.state.parts.find((part) => part.id === "spacer").placed, 20);

  const toolpath = await projectClient.execute(FlatLaserCommands.generateToolpath);
  assert.equal(toolpath.state.toolpath.visible, true);
  assert.equal(toolpath.state.toolpath.pathCount, 92);
}

async function testOpenProjectFileDoesNotRequireProductId() {
  const bridge = new MockIcaxBridge();
  const mailboxClient = new MailboxClient(bridge, { timeoutMs: 1000 });
  const appClient = new ApplicationClient(mailboxClient, await bridge.getApplicationChannelId());

  const response = await appClient.openProjectFile("D:/projects/sample.icax");

  assert.equal(response.resolve.matchedByMagic, true);
  assert.equal(response.product.productId, "sample");
  assert.equal(response.catalog.mainProject.projectPath, "D:/projects/sample.icax");
}

async function testTimeoutRejectsPendingPromise() {
  const silentBridge = {
    getApplicationChannelId: async () => "application",
    subscribeMail: () => () => {},
    postMail: () => {},
  };
  const mailboxClient = new MailboxClient(silentBridge, { timeoutMs: 5 });
  const appClient = new ApplicationClient(mailboxClient, await silentBridge.getApplicationChannelId());

  await assert.rejects(() => appClient.getState(), MailboxTimeoutError);
}

async function testRequestCarriesCommandTypeCode() {
  let postedMail = null;
  const silentBridge = {
    getApplicationChannelId: async () => "application",
    subscribeMail: () => () => {},
    postMail: (_channelId, mail) => {
      postedMail = mail;
    },
  };
  const mailboxClient = new MailboxClient(silentBridge, { timeoutMs: 5 });
  const appClient = new ApplicationClient(mailboxClient, await silentBridge.getApplicationChannelId());

  await assert.rejects(() => appClient.getState(), MailboxTimeoutError);
  assert.equal(postedMail.command, "App.GetState");
  assert.equal(typeof postedMail.typeCode, "string");
  assert.notEqual(postedMail.typeCode, "");
}

function testCommandTypeCodeMatchesBackendMainSubHash() {
  assert.equal(getCommandTypeCode(AppCommands.getState), "9364420466555971288");
  assert.equal(getCommandTypeCode(ProductCommands.getState), "5952739239660054232");
  assert.equal(getCommandTypeCode(ProductCommands.openProjectCatalog), "5952739237587920785");
  assert.equal(getCommandTypeCode(FlatLaserCommands.importDrawing), "1497477665304010554");
  assert.equal(makeCommandTypeCode("FlatLaserCam", "GetState"), "1497477665319773912");
  assert.equal(makeCommandTypeCodeFromCommand("FlatLaserCam.GetState"), "1497477665319773912");

  assert.throws(() => getCommandTypeCode("FlatLaserCam.Import.Drawing"), TypeError);
  assert.throws(() => getCommandTypeCode("flatLaser.importDrawing"), TypeError);
  assert.throws(() => getCommandTypeCode("FlatLaserCam.Import-Drawing"), TypeError);
}

await testRequestResolvesFromResponseMailOnly();
await testOpenProjectFileDoesNotRequireProductId();
await testOpenFlatLaserProjectFileResolvesProduct();
await testFlatLaserProjectCommandsMutateProjectState();
await testTimeoutRejectsPendingPromise();
await testRequestCarriesCommandTypeCode();
testCommandTypeCodeMatchesBackendMainSubHash();

console.log("icax-workbench mailboxClient tests passed");
