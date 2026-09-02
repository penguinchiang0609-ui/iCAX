import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import {
  AppSDO,
  allocateResourceURL,
  connectApplication,
  ensureUsableChannelId,
  isUsableChannelId,
  ProductProxy,
  ProjectProxy,
  ResourceClient,
  SceneProxy,
  MockHostBridge,
  ProjectSDO,
  ViewClient,
  loadRenderResource,
  parseViewResource,
  parseRenderGeometryResource,
  parseRenderMaterialResource,
  loadProductModule,
  mountProductModule,
  makeResourceCollectionURL,
  parseResourceURL,
  resolveFrontendEntry,
  validateBridge,
} from "../../iCAX-UI/SDK/index.mjs";
import { SDOClient, SDOFrameKind } from "../../iCAX-UI/SDK/SDO/sdoClient.mjs";
import { makeSDOMethodCode, makeSDOMethodCodeFromName, makePDOID } from "../../iCAX-UI/SDK/SDO/sdoMethod.mjs";
import { deserializeVariantText, serializeVariantText } from "../../iCAX-UI/SDK/SDO/variantSerializer.mjs";
import { PDOClient } from "../../iCAX-UI/SDK/PDO/pdoClient.mjs";
import { renderMachineRightPane } from "../../apps/laser-3d-cam/webpage/machine/machineArea.mjs";
import { activateProjectArea, getProjectArea, setProjectAreaViewContent } from "../../apps/laser-3d-cam/webpage/state/projectViewStore.mjs";
import { loadPreviewMeshResource } from "../../apps/tube-one/webpage/previewMeshResource.mjs";

function testSDOMethodCodes() {
  assert.equal(makeSDOMethodCode("App", "GetState"), makeSDOMethodCodeFromName(AppSDO.getState));
  assert.equal(makeSDOMethodCode("Product", "OpenProjectCatalog"), "5952739237587920785");
  assert.equal(makeSDOMethodCode("Machine", "Import"), makeSDOMethodCodeFromName("Machine.Import"));
  assert.throws(() => makeSDOMethodCodeFromName("Cam.Machine.Import"), /SDOName\.MethodName/);
  assert.equal(makePDOID("PreviewMesh", "MainViewport"), makeSDOMethodCode("PreviewMesh", "MainViewport"));
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

function testViewportOwnsTransientInteraction() {
  const viewportSource = readFileSync(
    new URL("../../iCAX-UI/SDK/Viewport/threeViewport.mjs", import.meta.url),
    "utf8",
  );
  assert.doesNotMatch(viewportSource, /InputPDO|writeInputStatePDO|flushInputPDO/);
  assert.match(viewportSource, /#applyLocalNavigation/);
  assert.match(viewportSource, /getCameraState\(\)/);
  assert.match(viewportSource, /setCameraState\(value = \{\}\)/);
}

function testViewResourceParsing() {
  const snapshot = parseViewResource(makeViewResourceFixture());
  assert.equal(snapshot.revision, "3");
  assert.equal(snapshot.count, 1);
  assert.deepEqual(snapshot.sources, [
    { sourceId: "scene", role: "scene" },
    { sourceId: "workpieces", role: "workpiece" },
  ]);
  assert.deepEqual(snapshot.entityIds, [
    "00112233-4455-6677-8899-aabbccddeeff",
  ]);
  assert.deepEqual(snapshot.rows[0].sourceIds, ["scene", "workpieces"]);
  assert.equal(snapshot.rows[0].data.visible, true);
  assert.equal(
    snapshot.rows[0].components.CRenderInstanceComponent.present,
    true,
  );
  assert.equal(
    snapshot.rows[0].components.CTransformComponent.present,
    false,
  );
}

function testRenderResourceParsing() {
  const geometry = parseRenderGeometryResource(makeRenderGeometryResourceFixture());
  assert.equal(geometry.kind, "mesh");
  assert.equal(geometry.dataVersion, "7");
  assert.equal(geometry.flags, 3);
  assert.deepEqual([...geometry.positions], [0, 0, 0, 10, 0, 0, 0, 10, 0]);
  assert.deepEqual([...geometry.indices], [0, 1, 2]);
  assert.deepEqual([...geometry.vertexColors], [0xff3366ff, 0xff3366ff, 0xff3366ff]);

  const material = parseRenderMaterialResource(makeRenderMaterialResourceFixture());
  assert.equal(material.dataVersion, "9");
  assert.equal(material.colorRGBA, 0x3366ccff);
  assert.equal(material.lineWidth, 2.5);
  assert.equal(material.baseColorTextureUrl,
    "icax-resource://app/product/project/scene/texture");
}

async function testRenderResourceLoaderUsesViewReferenceVersion() {
  let requestedUrl = "";
  let requestedVersion = "";
  const loaded = await loadRenderResource({
    get: async (url, { headers }) => {
      requestedUrl = url;
      requestedVersion = headers.get("ICAX-Resource-Version");
      return new Response(makeRenderGeometryResourceFixture().slice(0), { status: 200 });
    },
  }, {
    url: "icax-resource://app/product/project/scene/geometry",
    version: "7",
  });
  assert.equal(requestedUrl, "icax-resource://app/product/project/scene/geometry");
  assert.equal(requestedVersion, "7");
  assert.equal(loaded.type, "geometry");
  assert.equal(loaded.data.dataVersion, "7");
}

async function testTubePreviewLoadsStandardRenderGeometry() {
  let requestedVersion = "";
  const preview = await loadPreviewMeshResource({
    get: async (_url, { headers }) => {
      requestedVersion = headers.get("ICAX-Resource-Version");
      return new Response(makeRenderGeometryResourceFixture().slice(0), {
        status: 200,
        headers: {
          "ICAX-FlatBuffer-Identifier": "ICRG",
          "ICAX-Resource-Version": "7",
        },
      });
    },
  }, {
    url: "icax-resource://app/product/project/scene/tube-preview",
    version: 7,
  });
  assert.equal(requestedVersion, "7");
  assert.equal(preview.format, "render-geometry");
  assert.equal(preview.resourceVersion, 7);
  assert.equal(preview.vertexCount, 3);
  assert.equal(preview.triangleCount, 1);
  assert.deepEqual([...preview.indices], [0, 1, 2]);
}

function testProjectAreaMembershipIsolation() {
  const projectView = {
    activeAreaId: "",
    areas: {},
    layout: { leftWidth: 320, rightWidth: 340 },
    selectedSceneObjectId: "",
    selectedMachineInstanceId: "",
  };
  activateProjectArea(projectView, "machine");
  setProjectAreaViewContent(projectView, "machine", {
    revision: "1",
    entityIds: ["machine-axis"],
  });
  projectView.layout.leftWidth = 410;
  projectView.selectedSceneObjectId = "machine-axis";
  activateProjectArea(projectView, "machine");
  assert.equal(projectView.layout.leftWidth, 410);
  assert.equal(projectView.selectedSceneObjectId, "machine-axis");
  activateProjectArea(projectView, "workpiece");
  setProjectAreaViewContent(projectView, "workpiece", {
    revision: "4",
    entityIds: ["workpiece-1"],
  });
  assert.equal(projectView.layout.leftWidth, 320);
  assert.equal(projectView.selectedSceneObjectId, "");
  assert.deepEqual([...getProjectArea(projectView, "workpiece").viewContent.entityIds], ["workpiece-1"]);
  projectView.layout.leftWidth = 270;
  projectView.selectedSceneObjectId = "workpiece-1";
  activateProjectArea(projectView, "machine");
  assert.equal(projectView.layout.leftWidth, 410);
  assert.equal(projectView.selectedSceneObjectId, "machine-axis");
  assert.deepEqual([...getProjectArea(projectView, "machine").viewContent.entityIds], ["machine-axis"]);
}

async function testViewReadersUseSceneChannelAndResourceSnapshots() {
  const payload = makeViewResourceFixture();
  const sceneInvocations = [];
  const viewId = "11111111-2222-4333-8444-555555555555";
  const sceneProxy = {
    invoke: async (method, request) => {
      sceneInvocations.push({ method, request });
      if (method === "View.Release") {
        return { viewId: request.viewId, released: true };
      }
      return {
        viewId,
        resource: {
          url: "icax-resource://app/product/project/scene/view",
          version: "3",
          format: "ICVW",
        },
      };
    },
    resources: {
      head: async () => new Response(null, {
        status: 200,
        headers: { "ICAX-Resource-Version": "3" },
      }),
      get: async () => new Response(payload.slice(0), {
        status: 200,
        headers: { "ICAX-Resource-Version": "3" },
      }),
    },
  };
  const client = new ViewClient(sceneProxy);
  const definition = {
    sources: [
      {
        sourceId: "scene",
        role: "scene",
        where: "WHERE HAS CRenderInstanceComponent",
      },
      {
        sourceId: "workpieces",
        role: "workpiece",
        where: "WHERE HAS CWorkpieceComponent",
      },
    ],
  };
  const first = await client.start(definition);
  const second = await client.start(definition);

  assert.notEqual(first, second);
  assert.equal(first.viewId, second.viewId);
  assert.equal(sceneInvocations.length, 2);
  assert.ok(sceneInvocations.every(({ method }) =>
    method === "View.GetOrCreate"));
  assert.equal((await first.poll()).revision, "3");
  assert.equal((await second.poll()).revision, "3");
  assert.equal(await first.stop(), true);
  assert.equal(await first.stop(), false);
  assert.equal(await second.stop(), true);
  assert.deepEqual(
    sceneInvocations.map(({ method }) => method),
    [
      "View.GetOrCreate",
      "View.GetOrCreate",
      "View.Release",
      "View.Release",
    ],
  );
  assert.equal(sceneInvocations[0].request.sources.length, 2);
  assert.deepEqual(sceneInvocations[2].request, { viewId });
  assert.deepEqual(sceneInvocations[3].request, { viewId });
  await client.dispose();
}

function makeViewResourceFixture() {
  const bytes = Buffer.from(
    "GAAAAElDVlcAAA4AGAAAAAQAEAAIAAwADgAAAPABAACIAQAADAAAAAMAAAAAAAAAAQAAABAAAAAAAAoAEAAEAAgADAAKAAAANAEAAAgBAAAEAAAAAgAAAHAAAAAQAAAADAAMAAQAAAAAAAgADAAAADQAAAAEAAAAJgAAAHsiX192YXJpYW50X3R5cGUiOiJPYmplY3QiLCJ2YWx1ZSI6e319AAATAAAAQ1RyYW5zZm9ybUNvbXBvbmVudAAMABAACAAGAAcADAAMAAAAAAABAWQAAAAEAAAAVgAAAHsiX192YXJpYW50X3R5cGUiOiJPYmplY3QiLCJ2YWx1ZSI6eyJ2aXNpYmxlIjp7Il9fdmFyaWFudF90eXBlIjoiYm9vbCIsInZhbHVlIjp0cnVlfX19AAAYAAAAQ1JlbmRlckluc3RhbmNlQ29tcG9uZW50AAAAAAIAAAAYAAAABAAAAAoAAAB3b3JrcGllY2VzAAAFAAAAc2NlbmUAAAAkAAAAMDAxMTIyMzMtNDQ1NS02Njc3LTg4OTktYWFiYmNjZGRlZWZmAAAAAAIAAAA8AAAABAAAANT///8YAAAABAAAAAkAAAB3b3JrcGllY2UAAAAKAAAAd29ya3BpZWNlcwAACAAMAAQACAAIAAAAFAAAAAQAAAAFAAAAc2NlbmUAAAAFAAAAc2NlbmUAAAAkAAAAMTExMTExMTEtMjIyMi00MzMzLTg0NDQtNTU1NTU1NTU1NTU1AAAAAA==",
    "base64",
  );
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

function makeRenderGeometryResourceFixture() {
  const bytes = Buffer.from(
    "JAAAAElDUkcAAAAAGAAwAAAABAAkAAgADAAQABQAGAAcACAAGAAAAAEAAAABAAAAAwAAAIQAAABYAAAAOAAAACQAAAAQAAAABwAAAAAAAAAAAAAAAwAAAAAAAAABAAAAAgAAAAMAAAD/ZjP//2Yz//9mM/8GAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/CQAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwkAAAAAAAAAAAAAAAAAAAAAACBBAAAAAAAAAAAAAAAAAAAgQQAAAAA=",
    "base64",
  );
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

function makeRenderMaterialResourceFixture() {
  const bytes = Buffer.from(
    "IAAAAElDUk0YACgAAAAgAAQACAAMAAAAEAAUABgAHAAYAAAA/8xmM/8RERH/////AAAgQAMAAAAEAAAADAAAAAkAAAAAAAAAMQAAAGljYXgtcmVzb3VyY2U6Ly9hcHAvcHJvZHVjdC9wcm9qZWN0L3NjZW5lL3RleHR1cmUAAAA=",
    "base64",
  );
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
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

async function testSDOPromiseFlow() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { sdo: { timeoutMs: 1000 } } });

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
  assert.deepEqual(await opened.sceneProxy.invoke(ProjectSDO.undo), undoRedoState);
}

async function testSceneChannelRegistrationFromProjectState() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { sdo: { timeoutMs: 1000 } } });
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

async function testDirectResourceAccess() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const app = await connectApplication({ bridge, app: { sdo: { timeoutMs: 1000 } } });
  const opened = await app.openProjectFile("D:/projects/resources.icax");
  const resources = opened.sceneProxy.resources;
  assert.ok(app.resources instanceof ResourceClient);
  assert.ok(opened.productProxy.resources instanceof ResourceClient);
  assert.ok(opened.projectProxy.resources instanceof ResourceClient);
  assert.ok(resources instanceof ResourceClient);

  const scope = {
    applicationId: "icax",
    productId: "icax.mock-product",
    projectId: opened.projectProxy.projectId,
    sceneId: opened.sceneProxy.sceneId,
  };
  const allocated = allocateResourceURL(scope);
  const url = allocated.url;
  assert.equal(parseResourceURL(url).resourceId, allocated.resourceId);
  assert.throws(
    () => parseResourceURL(`${makeResourceCollectionURL(scope)}/`),
    /empty path segment/,
  );
  assert.throws(
    () => parseResourceURL(url.replace("/resources/", "//resources/")),
    /empty path segment/,
  );
  assert.throws(
    () => makeResourceCollectionURL({
      applicationId: "icax..unsafe",
    }),
    /URL-safe stable ID/,
  );
  const source = new Uint8Array([8, 0, 0, 0, 73, 67, 82, 83, 1, 2, 3, 4]);
  const created = await resources.create(url, source, {
    headers: {
      "Content-Type": "application/vnd.icax.flatbuffer",
      "ICAX-Resource-Type": "test.transport",
      "ICAX-Schema-Version": "1",
    },
  });
  assert.equal(created.status, 201);
  assert.equal(created.headers.get("ETag"), "\"icax-v1\"");

  const head = await resources.head(url);
  assert.equal(head.status, 200);
  assert.equal(head.headers.get("Content-Length"), String(source.byteLength));
  assert.equal(head.headers.get("ICAX-Schema-Version"), "1");
  assert.equal((await head.arrayBuffer()).byteLength, 0);

  const fetched = await opened.sceneProxy.fetchResource(url);
  assert.equal(fetched.status, 200);
  assert.deepEqual(
    [...new Uint8Array(await fetched.arrayBuffer())],
    [...source],
  );

  const cached = await resources.get(url, {
    headers: { "If-None-Match": "\"icax-v1\"" },
  });
  assert.equal(cached.status, 304);

  const removed = await resources.delete(url, {
    headers: { "If-Match": "\"icax-v1\"" },
  });
  assert.equal(removed.status, 204);
  assert.equal((await resources.get(url)).status, 404);

  const posted = await resources.post(
    makeResourceCollectionURL(scope),
    source,
    {
      headers: {
        "Content-Type": "application/vnd.icax.flatbuffer",
      },
    },
  );
  assert.equal(posted.status, 201);
  const postedURL = posted.headers.get("Location");
  assert.equal(parseResourceURL(postedURL).scope, "scene");
  assert.equal((await resources.get(postedURL)).status, 200);
}

async function testSDOEventFlow() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const sdo = new SDOClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();

  const events = [];
  const unsubscribeStateChanged = sdo.subscribe(channelId, "App.StateChanged", (event) => events.push(event));
  const unsubscribeAll = sdo.subscribeAll(channelId, (event) => events.push({ wildcard: true, event }));

  bridge.emitSDOFrame(channelId, "App.StateChanged", { state: "Running", phase: "Running" });
  await delay(10);

  assert.equal(events.length, 2);
  assert.equal(events[0].callId, 0);
  assert.equal(events[0].kind, SDOFrameKind.Event);
  assert.equal(events[0].ok, true);
  assert.equal(events[0].payload.state, "Running");
  assert.equal(events[1].wildcard, true);
  assert.equal(events[1].event.payload.phase, "Running");

  unsubscribeStateChanged();
  unsubscribeAll();
  bridge.emitSDOFrame(channelId, "App.StateChanged", { state: "Stopped" });
  await delay(10);
  assert.equal(events.length, 2);
}

async function testSDOReportFlow() {
  const bridge = new MockHostBridge({ delayMs: 50 });
  const sdo = new SDOClient(bridge, { timeoutMs: 40 });
  const channelId = await bridge.getApplicationChannelId();
  const reports = [];
  let completed = false;

  const task = sdo.invoke(channelId, AppSDO.getState, {}, {
    onReport: (report) => reports.push(report),
  });
  task.then(() => {
    completed = true;
  });

  assert.equal(task.callId > 0, true);
  assert.equal(typeof task.onReport, "function");
  assert.equal(sdo.pending.has(String(task.callId)), true);

  bridge.emitReport(channelId, task.callId, AppSDO.getState, {
    progress: 0.25,
    state: "Recognizing",
    message: "Recognizing features",
  }, { delayMs: 20 });

  await delay(30);
  assert.equal(completed, false);
  assert.equal(sdo.pending.has(String(task.callId)), true);
  assert.equal(reports.length, 1);
  assert.equal(reports[0].payload.progress, 0.25);
  assert.equal(reports[0].payload.state, "Recognizing");
  assert.equal(reports[0].kind, SDOFrameKind.Report);
  assert.equal(task.getLatestReport(), reports[0]);

  const state = await task;
  assert.equal(state.state, "Running");
  assert.equal(sdo.pending.has(String(task.callId)), false);
}

async function testFrontendSDOExposure() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const sdo = new SDOClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();
  const callId = 7001;

  const unexpose = sdo.expose(channelId, "Frontend.GetSelection", async (payload, call) => {
    await call.report({ phase: "Reading" });
    return { selection: [payload.requestedId] };
  });

  bridge.emitRequest(channelId, callId, "Frontend.GetSelection", { requestedId: "part-42" });
  await delay(15);

  const frames = bridge.postedFrames.filter((frame) => Number(frame.callId) === callId);
  assert.deepEqual(frames.map((frame) => frame.kind), [SDOFrameKind.Report, SDOFrameKind.Response]);
  assert.equal(frames.every((frame) => Number(frame.callId) === callId), true);
  assert.equal(deserializeVariantText(frames[0].payloadText).phase, "Reading");
  assert.deepEqual(deserializeVariantText(frames[1].payloadText).selection, ["part-42"]);

  unexpose();
  sdo.dispose();
}

async function testSDODispose() {
  const bridge = new MockHostBridge({ delayMs: 1 });
  const sdo = new SDOClient(bridge, { timeoutMs: 1000 });
  const channelId = await bridge.getApplicationChannelId();

  const events = [];
  sdo.subscribe(channelId, "App.StateChanged", (event) => events.push(event));
  assert.equal(sdo.startedChannelIds.has(channelId), true);

  sdo.dispose();
  assert.equal(sdo.startedChannelIds.has(channelId), false);

  bridge.emitSDOFrame(channelId, "App.StateChanged", { state: "Running" });
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

function testMachineJointTransformPanel() {
  const backendEditable = { editable: true, hasRange: false, step: 1, precision: 3 };
  const makeView = (joint) => ({
    selectedMachineInstanceId: "machine-1",
    selectedSceneObjectId: "joint-1",
    pending: false,
    scene: {
      machines: [{ entityId: "machine-1", isLoaded: true, linearJogStep: 10, angularJogStep: 1 }],
      machineElement: {
        entityId: "joint-1",
        kind: "link",
        name: "axis",
        transform: { position: [10, 20, 30], rotationRadians: [0.1, 0.2, 0.3], scale: [1, 1, 1] },
        transformEditPolicy: {
          reason: "joint",
          position: [backendEditable, backendEditable, backendEditable],
          rotationRadians: [backendEditable, backendEditable, backendEditable],
          scale: [backendEditable, backendEditable, backendEditable],
        },
        joint,
      },
    },
  });

  const linear = renderMachineRightPane({}, makeView({
    type: "prismatic",
    axis: [-1, 0, 0],
    position: 25,
    lower: -100,
    upper: 200,
  }));
  const input = (html, attribute) => html.match(new RegExp(`<input[^>]*${attribute}[^>]*>`))?.[0] ?? "";
  assert.match(input(linear, "data-cam-transform-position-x"), /data-cam-joint-position/);
  assert.doesNotMatch(input(linear, "data-cam-transform-position-x"), /\bdisabled\b/);
  for (const attribute of [
    "data-cam-transform-position-y",
    "data-cam-transform-position-z",
    "data-cam-transform-rotation-yaw",
    "data-cam-transform-rotation-pitch",
    "data-cam-transform-rotation-roll",
    "data-cam-transform-scale-x",
    "data-cam-transform-scale-y",
    "data-cam-transform-scale-z",
  ]) {
    assert.match(input(linear, attribute), /\bdisabled\b/);
  }
  assert.doesNotMatch(linear, /data-cam-joint-position-editor/);

  const rotary = renderMachineRightPane({}, makeView({
    type: "revolute",
    axis: [1, 0, 0],
    position: Math.PI / 4,
    lower: -Math.PI,
    upper: Math.PI,
  }));
  assert.match(input(rotary, "data-cam-transform-rotation-roll"), /data-cam-joint-position/);
  assert.doesNotMatch(input(rotary, "data-cam-transform-rotation-roll"), /\bdisabled\b/);
  for (const attribute of [
    "data-cam-transform-position-x",
    "data-cam-transform-position-y",
    "data-cam-transform-position-z",
    "data-cam-transform-rotation-yaw",
    "data-cam-transform-rotation-pitch",
  ]) {
    assert.match(input(rotary, attribute), /\bdisabled\b/);
  }
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

testSDOMethodCodes();
testViewportOwnsTransientInteraction();
testVariantSerializer();
testViewResourceParsing();
testRenderResourceParsing();
testProjectAreaMembershipIsolation();
await testViewReadersUseSceneChannelAndResourceSnapshots();
await testRenderResourceLoaderUsesViewReferenceVersion();
await testTubePreviewLoadsStandardRenderGeometry();
testBridgeValidation();
testChannelIdValidation();
await testSDOPromiseFlow();
await testSceneChannelRegistrationFromProjectState();
await testDirectResourceAccess();
await testSDOEventFlow();
await testSDOReportFlow();
await testFrontendSDOExposure();
await testSDODispose();
await testPDOBridgeInjection();
await testProductModuleLoader();
testMachineJointTransformPanel();

console.log("SDK tests passed");


