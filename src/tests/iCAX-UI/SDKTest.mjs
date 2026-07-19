import assert from "node:assert/strict";

import {
  AppFacade,
  connectApplication,
  ensureUsableChannelId,
  isUsableChannelId,
  ProductProxy,
  ProjectProxy,
  SceneProxy,
  MockHostBridge,
  ProjectFacade,
  loadProductModule,
  mountProductModule,
  resolveFrontendEntry,
  validateBridge,
} from "../../iCAX-UI/SDK/index.mjs";
import { FacadeClient, FacadeFrameKind } from "../../iCAX-UI/SDK/Facades/facadeClient.mjs";
import { makeFacadeMethodCode, makeFacadeMethodCodeFromName, makePDOID } from "../../iCAX-UI/SDK/Facades/facadeMethod.mjs";
import { deserializeVariantText, serializeVariantText } from "../../iCAX-UI/SDK/Facades/variantSerializer.mjs";
import { PDOClient } from "../../iCAX-UI/SDK/PDO/pdoClient.mjs";

function testFacadeMethodCodes() {
  assert.equal(makeFacadeMethodCode("App", "GetState"), makeFacadeMethodCodeFromName(AppFacade.getState));
  assert.equal(makeFacadeMethodCode("Product", "OpenProjectCatalog"), "5952739237587920785");
  assert.equal(makeFacadeMethodCode("Machine", "Import"), makeFacadeMethodCodeFromName("Machine.Import"));
  assert.throws(() => makeFacadeMethodCodeFromName("Cam.Machine.Import"), /FacadeName\.MethodName/);
  assert.equal(makePDOID("PreviewMesh", "MainViewport"), makeFacadeMethodCode("PreviewMesh", "MainViewport"));
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

async function testFacadePromiseFlow() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { facades: { timeoutMs: 1000 } } });

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
  assert.deepEqual(await opened.sceneProxy.invoke(ProjectFacade.undo), undoRedoState);
}

async function testSceneChannelRegistrationFromProjectState() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { facades: { timeoutMs: 1000 } } });
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

async function testFacadeEventFlow() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const facades = new FacadeClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();

  const events = [];
  const unsubscribeStateChanged = facades.subscribe(channelId, "App.StateChanged", (event) => events.push(event));
  const unsubscribeAll = facades.subscribeAll(channelId, (event) => events.push({ wildcard: true, event }));

  bridge.emitFacadeFrame(channelId, "App.StateChanged", { state: "Running", phase: "Running" });
  await delay(10);

  assert.equal(events.length, 2);
  assert.equal(events[0].callId, 0);
  assert.equal(events[0].kind, FacadeFrameKind.Event);
  assert.equal(events[0].ok, true);
  assert.equal(events[0].payload.state, "Running");
  assert.equal(events[1].wildcard, true);
  assert.equal(events[1].event.payload.phase, "Running");

  unsubscribeStateChanged();
  unsubscribeAll();
  bridge.emitFacadeFrame(channelId, "App.StateChanged", { state: "Stopped" });
  await delay(10);
  assert.equal(events.length, 2);
}

async function testFacadeReportFlow() {
  const bridge = new MockHostBridge({ delayMs: 50 });
  const facades = new FacadeClient(bridge, { timeoutMs: 40 });
  const channelId = await bridge.getApplicationChannelId();
  const reports = [];
  let completed = false;

  const task = facades.invoke(channelId, AppFacade.getState, {}, {
    onReport: (report) => reports.push(report),
  });
  task.then(() => {
    completed = true;
  });

  assert.equal(task.callId > 0, true);
  assert.equal(typeof task.onReport, "function");
  assert.equal(facades.pending.has(String(task.callId)), true);

  bridge.emitReport(channelId, task.callId, AppFacade.getState, {
    progress: 0.25,
    state: "Recognizing",
    message: "Recognizing features",
  }, { delayMs: 20 });

  await delay(30);
  assert.equal(completed, false);
  assert.equal(facades.pending.has(String(task.callId)), true);
  assert.equal(reports.length, 1);
  assert.equal(reports[0].payload.progress, 0.25);
  assert.equal(reports[0].payload.state, "Recognizing");
  assert.equal(reports[0].kind, FacadeFrameKind.Report);
  assert.equal(task.getLatestReport(), reports[0]);

  const state = await task;
  assert.equal(state.state, "Running");
  assert.equal(facades.pending.has(String(task.callId)), false);
}

async function testFrontendFacadeExposure() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const facades = new FacadeClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();
  const callId = 7001;

  const unexpose = facades.expose(channelId, "Frontend.GetSelection", async (payload, call) => {
    await call.report({ phase: "Reading" });
    return { selection: [payload.requestedId] };
  });

  bridge.emitRequest(channelId, callId, "Frontend.GetSelection", { requestedId: "part-42" });
  await delay(15);

  const frames = bridge.postedFrames.filter((frame) => Number(frame.callId) === callId);
  assert.deepEqual(frames.map((frame) => frame.kind), [FacadeFrameKind.Report, FacadeFrameKind.Response]);
  assert.equal(frames.every((frame) => Number(frame.callId) === callId), true);
  assert.equal(deserializeVariantText(frames[0].payloadText).phase, "Reading");
  assert.deepEqual(deserializeVariantText(frames[1].payloadText).selection, ["part-42"]);

  unexpose();
  facades.dispose();
}

async function testFacadeDispose() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const facades = new FacadeClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();

  const events = [];
  facades.subscribe(channelId, "App.StateChanged", (event) => events.push(event));
  assert.equal(facades.startedChannelIds.has(channelId), true);

  facades.dispose();
  assert.equal(facades.startedChannelIds.has(channelId), false);

  bridge.emitFacadeFrame(channelId, "App.StateChanged", { state: "Running" });
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

testFacadeMethodCodes();
testVariantSerializer();
testBridgeValidation();
testChannelIdValidation();
await testFacadePromiseFlow();
await testSceneChannelRegistrationFromProjectState();
await testFacadeEventFlow();
await testFacadeReportFlow();
await testFrontendFacadeExposure();
await testFacadeDispose();
await testPDOBridgeInjection();
await testProductModuleLoader();

console.log("SDK tests passed");


