import assert from "node:assert/strict";
import { ApplicationClient } from "../../icax-workbench/mailbox/clients.mjs";
import { MailboxClient, MailboxTimeoutError } from "../../icax-workbench/mailbox/mailboxClient.mjs";
import { MockIcaxBridge } from "../../icax-workbench/bridge/mockBridge.mjs";

async function testRequestResolvesFromResponseMailOnly() {
  const bridge = new MockIcaxBridge();
  const mailboxClient = new MailboxClient(bridge, { timeoutMs: 1000 });
  const appClient = new ApplicationClient(mailboxClient, await bridge.getApplicationMailId());

  let settled = false;
  const promise = appClient.getState().then((state) => {
    settled = true;
    return state;
  });

  await Promise.resolve();
  assert.equal(settled, false, "mailbox request must not settle synchronously after postMail");

  const state = await promise;
  assert.equal(settled, true);
  assert.equal(state.applicationMailId, "application");
  assert.equal(state.products.length, 1);
}

async function testOpenProjectFileDoesNotRequireProductId() {
  const bridge = new MockIcaxBridge();
  const mailboxClient = new MailboxClient(bridge, { timeoutMs: 1000 });
  const appClient = new ApplicationClient(mailboxClient, await bridge.getApplicationMailId());

  const response = await appClient.openProjectFile("D:/projects/sample.icax");

  assert.equal(response.resolve.matchedByMagic, true);
  assert.equal(response.product.productId, "sample");
  assert.equal(response.catalog.mainProject.projectPath, "D:/projects/sample.icax");
}

async function testTimeoutRejectsPendingPromise() {
  const silentBridge = {
    getApplicationMailId: async () => "application",
    subscribeMail: () => () => {},
    postMail: () => {},
  };
  const mailboxClient = new MailboxClient(silentBridge, { timeoutMs: 5 });
  const appClient = new ApplicationClient(mailboxClient, await silentBridge.getApplicationMailId());

  await assert.rejects(() => appClient.getState(), MailboxTimeoutError);
}

await testRequestResolvesFromResponseMailOnly();
await testOpenProjectFileDoesNotRequireProductId();
await testTimeoutRejectsPendingPromise();

console.log("icax-workbench mailboxClient tests passed");
