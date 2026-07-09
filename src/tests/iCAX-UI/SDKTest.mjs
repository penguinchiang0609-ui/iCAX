import assert from "node:assert/strict";

import {
  AppCommands,
  connectApplication,
  ensureUsableChannelId,
  isUsableChannelId,
  ProductProxy,
  ProjectProxy,
  SceneProxy,
  MockHostBridge,
  ProjectCommands,
  loadProductModule,
  mountProductModule,
  resolveFrontendEntry,
  validateBridge,
} from "../../iCAX-UI/SDK/index.mjs";
import { MailboxClient } from "../../iCAX-UI/SDK/Mailbox/mailboxClient.mjs";
import { makeCommandTypeCode, makeCommandTypeCodeFromCommand, makePDOID } from "../../iCAX-UI/SDK/Mailbox/commandRoute.mjs";
import { deserializeVariantText, serializeVariantText } from "../../iCAX-UI/SDK/Mailbox/variantSerializer.mjs";
import { PDOClient } from "../../iCAX-UI/SDK/PDO/pdoClient.mjs";

function testCommandCodes() {
  assert.equal(makeCommandTypeCode("App", "GetState"), makeCommandTypeCodeFromCommand(AppCommands.getState));
  assert.equal(makeCommandTypeCode("Product", "OpenProjectCatalog"), "5952739237587920785");
  assert.equal(makeCommandTypeCode("Cam.Machine", "Import"), makeCommandTypeCodeFromCommand("Cam.Machine.Import"));
  assert.equal(makePDOID("PreviewMesh", "MainViewport"), makeCommandTypeCode("PreviewMesh", "MainViewport"));
}

function testVariantSerializer() {
  const source = {
    productId: "icax.mock-product",
    enabled: true,
    count: 3,
    items: ["a", "b"],
    nested: { path: "D:/demo.ilcam" },
  };

  const text = serializeVariantText(source);
  assert.match(text, /"__variant_type":"Object"/);
  assert.deepEqual(deserializeVariantText(text), source);
}

function testBridgeValidation() {
  assert.throws(() => validateBridge({}), /getApplicationChannelId/);
  assert.ok(validateBridge(new MockHostBridge()) instanceof MockHostBridge);
}

function testChannelIdValidation() {
  assert.equal(isUsableChannelId("00000000-0000-0000-0000-000000000000"), false);
  assert.equal(isUsableChannelId("00000000-0000-4000-8000-000000000001"), true);
  assert.throws(() => ensureUsableChannelId("00000000-0000-0000-0000-000000000000"), /non-nil channel id/);
}

async function testMailboxPromiseFlow() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { mailbox: { timeoutMs: 1000 } } });

  const state = await app.getState();
  assert.equal(state.state, "Running");
  assert.equal(state.products.length, 1);
  assert.equal(app.getProduct("icax.mock-product"), null);

  const product = await app.startProduct("icax.mock-product");
  assert.ok(product instanceof ProductProxy);
  assert.equal(product.productId, "icax.mock-product");
  assert.ok(product.productChannelId);
  assert.equal(app.getProduct(product.productId), product);

  const stoppedState = await app.stopProduct(product.productId);
  assert.equal(stoppedState.runningProductCount, 0);
  assert.equal(app.getProduct(product.productId), null);

  const restartedProduct = await app.startProduct("icax.mock-product");
  assert.ok(restartedProduct instanceof ProductProxy);

  const created = await restartedProduct.openProjectCatalog("D:/projects/created.icax", {
    catalogName: "Created Catalog",
    projectName: "Created Project",
  });
  assert.ok(created.projectProxy instanceof ProjectProxy);
  assert.ok(created.sceneProxy instanceof SceneProxy);
  assert.equal(restartedProduct.getProject(created.projectProxy.projectId), created.projectProxy);
  assert.equal(restartedProduct.state.catalogs.length, 1);
  assert.equal(restartedProduct.state.catalogs[0].catalogPath, "D:/projects/created.icax");

  const resolved = await app.resolveProjectFile("D:/projects/mock.icax");
  assert.equal(resolved.resolve.status, "Resolved");
  assert.equal(resolved.resolve.productId, "icax.mock-product");

  const opened = await app.openProjectFile("D:/projects/mock.icax");
  assert.equal(opened.resolve.status, "Resolved");
  assert.equal(opened.catalog.mainProject.state, "Running");
  assert.ok(opened.projectProxy instanceof ProjectProxy);
  assert.ok(opened.sceneProxy instanceof SceneProxy);
  assert.equal(opened.productProxy.getProject(opened.projectProxy.projectId), opened.projectProxy);
  assert.equal(opened.projectProxy.getMainScene(), opened.sceneProxy);

  const sceneState = await opened.sceneProxy.getState();
  assert.equal(sceneState.sceneChannelId, opened.sceneProxy.sceneChannelId);
  assert.equal(sceneState.sceneName, "Main Scene");

  const undoRedoState = await opened.sceneProxy.getUndoRedoState();
  assert.deepEqual(undoRedoState.undoSteps, []);
  assert.deepEqual(await opened.sceneProxy.execute(ProjectCommands.undo), undoRedoState);
}

async function testSceneChannelRegistrationFromProjectState() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { mailbox: { timeoutMs: 1000 } } });
  const product = await app.startProduct("icax.mock-product");
  bridge.projectOpened = true;

  const project = await product.adoptProject({
    projectId: "00000000-0000-4000-8000-000000000401",
    projectName: "Registered Later",
    projectPath: "D:/projects/mock.icax",
    mainSceneId: "00000000-0000-4000-8000-000000000501",
    mainScene: {
      sceneId: "00000000-0000-4000-8000-000000000501",
      sceneName: "Main Scene",
    },
    scenes: [{
      sceneId: "00000000-0000-4000-8000-000000000501",
      sceneName: "Main Scene",
    }],
  });

  assert.ok(project instanceof ProjectProxy);
  assert.ok(project.getMainScene() instanceof SceneProxy);
  assert.equal(project.getMainScene().sceneChannelId, "00000000-0000-4000-8000-000000000201");
}

async function testMailboxEventFlow() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const mailbox = new MailboxClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();

  const events = [];
  const unsubscribeStateChanged = mailbox.subscribe(channelId, "App.StateChanged", (event) => events.push(event));
  const unsubscribeAll = mailbox.subscribeAll(channelId, (event) => events.push({ wildcard: true, event }));

  bridge.emitMail(channelId, "App.StateChanged", { state: "Running", phase: "Running" });
  await delay(10);

  assert.equal(events.length, 2);
  assert.equal(events[0].originId, 0);
  assert.equal(events[0].ok, true);
  assert.equal(events[0].payload.state, "Running");
  assert.equal(events[1].wildcard, true);
  assert.equal(events[1].event.payload.phase, "Running");

  unsubscribeStateChanged();
  unsubscribeAll();
  bridge.emitMail(channelId, "App.StateChanged", { state: "Stopped" });
  await delay(10);
  assert.equal(events.length, 2);
}

async function testMailboxDispose() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const mailbox = new MailboxClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();

  const events = [];
  mailbox.subscribe(channelId, "App.StateChanged", (event) => events.push(event));
  assert.equal(mailbox.startedChannelIds.has(channelId), true);

  mailbox.dispose();
  assert.equal(mailbox.startedChannelIds.has(channelId), false);

  bridge.emitMail(channelId, "App.StateChanged", { state: "Running" });
  await delay(10);
  assert.equal(events.length, 0);
}

async function testPDOBridgeInjection() {
  const calls = [];
  const bridge = {
    pdo: {
      withRead(options, reader) {
        calls.push(options);
        return reader(new DataView(new ArrayBuffer(options.payloadSize)));
      },
    },
  };
  const declaration = {
    id: makePDOID("PreviewMesh", "MainViewport"),
    version: 7,
    payloadSize: 16,
  };
  const pdo = new PDOClient({
    enabled: true,
    sharedArenaName: "icax-pdo-test",
    declarations: [declaration],
  }, bridge);

  const byteLength = await pdo.withRead("PreviewMesh", "MainViewport", (view) => view.byteLength);
  assert.equal(byteLength, 16);
  assert.deepEqual(calls[0], {
    arenaName: "icax-pdo-test",
    id: declaration.id,
    version: declaration.version,
    payloadSize: declaration.payloadSize,
  });
}

async function testProductModuleLoader() {
  const shellBaseUrl = new URL("../../iCAX-UI/SDK/AppShell/app/bootstrap.mjs", import.meta.url).href;
  const entry = "apps/laser-3d-cam/webpage/entry.mjs";
  const moduleUrl = resolveFrontendEntry(entry, shellBaseUrl);
  assert.ok(moduleUrl.endsWith("/src/apps/laser-3d-cam/webpage/entry.mjs"));
  assert.throws(() => resolveFrontendEntry("https://example.com/product.mjs", shellBaseUrl), /External product frontend entries/);
  assert.equal(
    resolveFrontendEntry("https://example.com/product.mjs", shellBaseUrl, { allowExternalEntries: true }),
    "https://example.com/product.mjs",
  );

  const cache = new Map();
  const module = await loadProductModule({
    productId: "icax.test-product",
    frontendEntry: "./fixtures/mock-product/entry.mjs",
  }, cache, { baseUrl: import.meta.url });
  assert.equal(typeof module.mountProduct, "function");
  assert.equal(cache.get("icax.test-product"), module);

  const calls = [];
  mountProductModule({
    mountProduct: (context) => calls.push(`product:${context.mode}`),
    mountProject: (context) => calls.push(`project:${context.mode}`),
  }, { mode: "product" });
  mountProductModule({
    mountProduct: (context) => calls.push(`product:${context.mode}`),
    mountProject: (context) => calls.push(`project:${context.mode}`),
  }, { mode: "project" });
  assert.deepEqual(calls, ["product:product", "project:project"]);
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

testCommandCodes();
testVariantSerializer();
testBridgeValidation();
testChannelIdValidation();
await testMailboxPromiseFlow();
await testSceneChannelRegistrationFromProjectState();
await testMailboxEventFlow();
await testMailboxDispose();
await testPDOBridgeInjection();
await testProductModuleLoader();

console.log("SDK tests passed");


