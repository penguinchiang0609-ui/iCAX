import assert from "node:assert/strict";
import { existsSync, mkdirSync, readFileSync, statSync, unlinkSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { createHash } from "node:crypto";
import pngjs from "./node_modules/pngjs/lib/png.js";
import { clickPlaneCommand, ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";

const near = (actual, expected, tolerance = 1e-7) => assert.ok(Math.abs(actual - expected) <= tolerance, `${actual} differs from ${expected}`);
const sorted = rows => [...rows].sort((a, b) => a.id.localeCompare(b.id));
const ringArea = points => points.reduce((sum, point, i) => { const next = points[(i + 1) % points.length];return sum + point[0] * next[1] - next[0] * point[1]; }, 0) / 2;
const netArea = curves => Math.abs(curves.reduce((sum, curve) => sum + ringArea(curve.points), 0));
const pixelHash = image => createHash("sha256").update(Buffer.from(image.rgba)).digest("hex");

// Every business write below is a physical control, mouse or keyboard action.
// SDO invocation is used exclusively for independent read-only assertions.
export async function exerciseWorktable({ page, report, assets, source, output, savedProject, waitForAsync, passed }) {
  report.console = [];page.on("console", message => { if (message.type() === "error") report.console.push(message.text()); });
  const inputRoot = resolve(output, "inputs");mkdirSync(inputRoot, { recursive: true });
  const fixtures = resolve(source, "tests/icax-plugins/product/iPlaneCAM/Fixtures");
  const imagePath = resolve(inputRoot, "hole-reference.png"), colorPath = resolve(inputRoot, "color-reference.png"), partPath = resolve(inputRoot, "worktable-part.dxf"), badPath = resolve(inputRoot, "invalid-image.png");
  for (const [file, destination] of [["image_rect_with_hole.png", imagePath], ["image_color_reference.png", colorPath], ["part_with_hole.dxf", partPath]]) writeFileSync(destination, readFileSync(resolve(fixtures, file)));
  writeFileSync(badPath, "This is deliberately not an encoded image.");
  const palette = id => page.locator(`[data-planar-tool="${id}"]`);
  const action = name => page.locator(`[data-iplane-action="${name}"]`);
  const panel = () => page.locator("[data-iplane-artwork-panel]");
  const field = name => panel().locator(`[data-iplane-form-field="${name}"]`);
  const read = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneScene.ReadState", {});
  });
  const model = async () => (await read()).iplaneCam;
  const geometry = id => page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }); }, id);
  const row = async id => (await model()).drawings.find(item => item.id === id);
  const state = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId), u = v.iplaneCamUI;
    return { busy: u.busy, taskId: u.geometryTaskId, error: u.operationError, tool: u.activeTool, selected: structuredClone(u.selectedIdsByKind), panel: u.panelMode, drafts: structuredClone(u.formDrafts), camera: v.viewport.getCameraState() };
  });
  const settled = () => waitForAsync(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId), u = v.iplaneCamUI;
    if (u.operationError || v.iplaneLayers?.error) throw new Error((u.operationError || v.iplaneLayers.error) + " " + document.querySelector(".log-list")?.textContent);
    return !u.busy && !u.geometryTaskId && !u.runningTaskId && !v.iplaneLayers?.queued && !v.iplaneLayers?.busy;
  }, null, { timeout: 180000 });
  const expect = async (predicate, description) => {
    const end = Date.now() + 90000;while (Date.now() < end) { if (predicate(await model())) return;const s = await state();if (s.error) throw new Error(s.error);await page.waitForTimeout(25); }
    throw new Error("Timed out: " + description);
  };
  const screen = point => page.evaluate(async point => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { Vector3 } = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const v = getProjectView(__icaxAppShell.getState().activeProjectId).viewport, b = v.root.querySelector("canvas").getBoundingClientRect();v.scene.updateMatrixWorld(true);v.camera.updateMatrixWorld(true);
    const p = new Vector3(...point, 0).applyMatrix4(v.content.matrixWorld).project(v.camera);return { x: b.left + (p.x + 1) * b.width / 2, y: b.top + (1 - p.y) * b.height / 2 };
  }, point);
  const clickPoint = async point => { const p = await screen(point);await page.mouse.click(p.x, p.y);await settled(); };
  const blank = async () => { const b = await page.locator(".icax-three-viewport canvas").boundingBox();await page.mouse.click(b.x + b.width * .83, b.y + b.height * .87);await settled(); };
  const select = async id => { await ensurePlaneObjectsExpanded(page);await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${id}"]`).first().click();await settled(); };
  const fill = async values => { for (const [name, value] of Object.entries(values)) { if (typeof value === "boolean") await field(name).setChecked(value);else await field(name).fill(String(value)); } };
  const command = async text => {
    await page.locator('[data-bottom-dock-tab="command"]').click();const input = page.locator("[data-iplane-command-input]");await input.fill(text);await input.press("Enter");
    await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const u = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (u.operationError) throw new Error(u.operationError);return !u.busy && !u.geometryTaskId && u.commandInput === ""; });
  };
  const importImage = async path => {
    const before = new Set((await model()).drawings.map(row => row.id));await page.evaluate(path => __planeNativeFileQueue.push(path), path);await palette("plane.image").click();
    await expect(m => m.drawings.some(row => !before.has(row.id) && row.contentType === "image"), "image imported");await settled();const image = (await model()).drawings.find(row => !before.has(row.id) && row.contentType === "image");
    await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const u = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (u.operationError) throw new Error(u.operationError + " " + document.querySelector(".log-list")?.textContent);return u.panelMode === "artwork:image-edit:" + id; }, image.id);return image;
  };
  const artworkPixels = async (id, previousTexture = "") => {
    await waitForAsync(async ({ id, previousTexture }) => {
      const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const root = getProjectView(__icaxAppShell.getState().activeProjectId).viewport.sceneObjects.get(id);let loaded = false;
      root?.traverse(node => { const material = Array.isArray(node.material) ? node.material[0] : node.material;if (material?.map?.image?.width > 0 && node.geometry?.attributes?.uv?.count > 0 && material.map.uuid !== previousTexture) loaded = true; });return loaded;
    }, { id, previousTexture });
    return page.evaluate(async id => {
      const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { LinearSRGBColorSpace } = await import("/iCAX-UI/SDK/ThirdParty/three/three.module.js");
      const v = getProjectView(__icaxAppShell.getState().activeProjectId).viewport, root = v.sceneObjects.get(id);let result;
      root.traverse(node => { const material = Array.isArray(node.material) ? node.material[0] : node.material, image = material?.map?.image;if (!image?.width || !node.geometry?.attributes?.uv) return;
        const canvas = document.createElement("canvas");canvas.width = image.width;canvas.height = image.height;const c = canvas.getContext("2d");c.drawImage(image, 0, 0);const rgba = Array.from(c.getImageData(0, 0, canvas.width, canvas.height).data);
        result = { width: image.width, height: image.height, rgba, uv: Array.from(node.geometry.attributes.uv.array), positions: Array.from(node.geometry.attributes.position.array), textureUuid: material.map.uuid, materialType: material.type, color: material.color.getHexString(LinearSRGBColorSpace), flipY: material.map.flipY, textureColorSpace: material.map.colorSpace, selectable: root.userData?.instance?.selectable };
      });return result;
    }, id);
  };
  const sampleTexture = (image, x, y) => image.rgba.slice((y * image.width + x) * 4, (y * image.width + x) * 4 + 4);
  const textureEnvelope = row => page.evaluate(async reference => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { parseRenderTextureResource } = await import("/iCAX-UI/SDK/Viewport/renderResource.mjs");
    if (!reference?.url) throw new Error("Artwork row omitted its typed render texture reference");
    const v = getProjectView(__icaxAppShell.getState().activeProjectId), response = await v.sceneProxy.resources.get(reference.url, { headers: { Accept: "application/vnd.icax.flatbuffer" } });
    if (!response.ok) throw new Error("Actual ICRT oracle GET failed: " + response.status);
    const resource = parseRenderTextureResource(await response.arrayBuffer()), digest = new Uint8Array(await crypto.subtle.digest("SHA-256", resource.encodedImage));
    return { reference, schemaVersion: resource.schemaVersion, dataVersion: resource.dataVersion, mimeType: resource.mimeType, pixelWidth: resource.pixelWidth, pixelHeight: resource.pixelHeight, encodedImageSha256: [...digest].map(value => value.toString(16).padStart(2, "0")).join("") };
  }, row.textureRef);
  const shotSamples = async (filename, samples) => {
    const bytes = await page.screenshot({ path: resolve(output, filename) }), image = pngjs.PNG.sync.read(bytes), values = [];
    for (const sample of samples) { const p = await screen(sample.point), x = Math.round(p.x), y = Math.round(p.y), offset = (y * image.width + x) * 4, color = Array.from(image.data.slice(offset, offset + 3));
      for (let i = 0; i < 3; i++) near(color[i], sample.rgb[i], 9);values.push({ ...sample, screen: p, rgb: color }); }
    return values;
  };

  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam.capabilities.enabledMethods.includes("PlaneText.Create"); });
  assert.equal(await page.locator(".cam-workbench").getAttribute("data-iplane-objects-open"), "false");
  for (const id of ["plane.text", "plane.image", "plane.erase", "plane.objects-toggle"]) assert.equal(await palette(id).isEnabled(), true);
  await palette("plane.text").click();await panel().waitFor();assert.equal(await panel().getAttribute("data-iplane-artwork-panel"), "text-create");
  const textValue = "平面 CAM\nHello A8";await fill({ text: textValue, fontFamily: "Microsoft YaHei", fontSizeMm: 10 });
  const b = await page.locator(".icax-three-viewport canvas").boundingBox(), placed = { x: b.x + b.width * .33, y: b.y + b.height * .35 };
  const point = await page.evaluate(async p => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.pointOnWorkPlane(p.x, p.y, [0, 0, 1]).slice(0, 2); }, placed);
  await page.mouse.click(placed.x, placed.y);await settled();near(Number(await field("x").inputValue()), point[0], 1e-4);near(Number(await field("y").inputValue()), point[1], 1e-4);assert.equal(await field("text").inputValue(), textValue);assert.equal((await model()).drawings.length, 0);
  await action("artwork-create-text").click();await expect(m => m.drawings.some(row => row.contentType === "text"), "text created");await settled();
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const u = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;if (u.operationError) throw new Error(u.operationError + " " + document.querySelector(".log-list")?.textContent);return u.activeTool === "plane.select"; });
  let text = (await model()).drawings.find(row => row.contentType === "text");assert.equal(text.text, textValue);near(text.x, point[0], 1e-4);near(text.y, point[1], 1e-4);assert.equal((await state()).tool, "plane.select");
  const firstTextPixels = await artworkPixels(text.id);assert.ok(firstTextPixels.rgba.some((value, i) => i % 4 === 3 && value > 0));assert.ok(firstTextPixels.rgba.some((value, i) => i % 4 === 3 && value === 0));assert.equal(firstTextPixels.materialType, "MeshBasicMaterial");assert.equal((await geometry(text.id)).curves.length, 0);
  const firstTextEnvelope = await textureEnvelope(text);assert.equal(firstTextEnvelope.schemaVersion, 1);assert.equal(firstTextEnvelope.mimeType, "image/png");assert.equal(firstTextEnvelope.pixelWidth, firstTextPixels.width);assert.equal(firstTextEnvelope.pixelHeight, firstTextPixels.height);
  assert.ok(firstTextPixels.positions.filter((v, i) => i % 3 === 2).every(v => v > 0), "Text must remain above the reference-image background plane");
  await clickPlaneCommand(page, "view.fit");await clickPoint([text.x + text.width * .5, text.y + text.height * .5]);assert.deepEqual((await state()).selected.drawing, [text.id]);
  report.textPlacement = { id: text.id, content: textValue, point, pixels: { width: firstTextPixels.width, height: firstTextPixels.height, materialType: firstTextPixels.materialType } };
  passed("The initially collapsed Objects panel and vertical SDK palette create editable Chinese/English multiline text only after confirmation; a real canvas placement preserves its draft, and real textured glyphs are selectable");

  await action("artwork-open-text").click();await fill({ text: "修改后的文字\nEdited 123", fontSizeMm: 12 });await action("artwork-update-text").click();await settled();let updated = await row(text.id);
  assert.equal(updated.id, text.id);assert.equal(updated.text, "修改后的文字\nEdited 123");near(updated.fontSizeMm, 12);assert.notDeepEqual(updated.imageRef, text.imageRef);assert.equal(BigInt(updated.geometryVersion), BigInt(text.geometryVersion) + 1n);const updatedTextPixels = await artworkPixels(text.id, firstTextPixels.textureUuid);assert.notEqual(pixelHash(updatedTextPixels), pixelHash(firstTextPixels));
  const updatedTextEnvelope = await textureEnvelope(updated);assert.notEqual(updatedTextEnvelope.encodedImageSha256, firstTextEnvelope.encodedImageSha256);
  await action("artwork-cancel").click();await command("移动 4,3");let moved = await row(text.id);near(moved.x, updated.x + 4);near(moved.y, updated.y + 3);assert.deepEqual(moved.imageRef, updated.imageRef);
  await command("旋转 15");const rotatedText = await row(text.id);near(rotatedText.angleDeg, moved.angleDeg + 15);
  await command("缩放 1.5");const scaledText = await row(text.id);near(scaledText.scaleX, rotatedText.scaleX * 1.5);
  await clickPlaneCommand(page, "plane.edit-copy");for (const [key, value] of Object.entries({ dx: "40", dy: "0" })) await page.locator(`[data-iplane-form="drawing-edit"][data-iplane-form-field="${key}"]`).fill(value);await action("apply-drawing-edit").click();await settled();
  assert.equal((await model()).drawings.filter(row => row.contentType === "text").length, 2);const textCopy = (await model()).drawings.find(row => row.contentType === "text" && row.id !== text.id);assert.equal(textCopy.text, scaledText.text);assert.deepEqual(textCopy.imageRef, scaledText.imageRef);
  await page.locator('.title-bar [data-action="undo-project"]').click();await expect(m => !m.drawings.some(row => row.id === textCopy.id), "copy undo");await settled();await page.locator('.title-bar [data-action="redo-project"]').click();await expect(m => m.drawings.some(row => row.id === textCopy.id), "copy redo");await settled();
  await palette("plane.select").click();text = await row(text.id);report.textEdited = text;
  passed("Text content and font size update the same typed entity and bitmap; genuine move, rotate, scale, copy and framework Undo/Redo preserve editable content and independent identity");

  const image = await importImage(imagePath);assert.equal(image.pixelWidth, 16);assert.equal(image.pixelHeight, 12);assert.equal((await geometry(image.id)).curves.length, 0);
  const imageEnvelope = await textureEnvelope(image);assert.equal(imageEnvelope.pixelWidth, 16);assert.equal(imageEnvelope.pixelHeight, 12);
  await fill({ widthMm: 16, heightMm: 12, x: 10, y: 20, angleDeg: 0 });await action("artwork-update-image").click();await settled();await action("artwork-cancel").click();await clickPlaneCommand(page, "view.fit");
  await blank();let imagePixels = await artworkPixels(image.id);assert.equal(imagePixels.materialType, "MeshBasicMaterial");assert.equal(imagePixels.color, "ffffff");assert.deepEqual(sampleTexture(imagePixels, 0, 0), [255, 255, 255, 255]);assert.deepEqual(sampleTexture(imagePixels, 3, 3), [0, 0, 0, 255]);assert.deepEqual(sampleTexture(imagePixels, 7, 6), [255, 255, 255, 255]);assert.ok(imagePixels.positions.filter((v, i) => i % 3 === 2).every(v => v < 0));
  const uv = Array.from({ length: imagePixels.uv.length / 2 }, (_, i) => imagePixels.uv.slice(i * 2, i * 2 + 2)), quad = Array.from({ length: imagePixels.positions.length / 3 }, (_, i) => imagePixels.positions.slice(i * 3, i * 3 + 2));
  for (const p of [[0, 0], [1, 0], [1, 1], [0, 1]]) assert.ok(uv.some(q => Math.hypot(q[0] - p[0], q[1] - p[1]) < 1e-7));
  for (const p of [[10, 20], [26, 20], [26, 32], [10, 32]]) assert.ok(quad.some(q => Math.hypot(q[0] - p[0], q[1] - p[1]) < 1e-6), "Rendered physical quad differs from Image.Update dimensions/position");
  const imageCenterScreen = await screen([18, 26]), beforeImageZoom = (await state()).camera.radius;await page.mouse.move(imageCenterScreen.x, imageCenterScreen.y);await page.mouse.wheel(0, -480);
  await waitForAsync(async previous => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.getCameraState().radius !== previous; }, beforeImageZoom);
  const canvasBounds = await page.locator(".icax-three-viewport canvas").boundingBox();await page.mouse.move(canvasBounds.x + canvasBounds.width * .7, canvasBounds.y + canvasBounds.height * .9);
  report.whiteImageScreenPixels = await shotSamples("worktable-white-reference.png", [{ point: [11, 21], rgb: [255, 255, 255] }, { point: [13.5, 23.5], rgb: [0, 0, 0] }]);
  await clickPoint([18, 26]);assert.deepEqual((await state()).selected.drawing, [image.id]);await command("移动 5,7");const imageMoved = await row(image.id);near(imageMoved.x, 15);near(imageMoved.y, 27);await command("旋转 30");const imageRotated = await row(image.id);near(imageRotated.angleDeg, 30);
  await action("artwork-open-image").click();await fill({ widthMm: 16, heightMm: 12, x: 10, y: 20, angleDeg: 30 });await action("artwork-update-image").click();await settled();await action("artwork-cancel").click();const imageForTrace = await row(image.id);assert.deepEqual(imageForTrace.imageRef, image.imageRef);
  const beforeTraceCount = (await model()).drawings.length;await action("artwork-open-vectorize").click();await fill({ threshold: 128, invert: false, minAreaMm2: 0, simplifyMm: 0 });assert.equal((await model()).drawings.length, beforeTraceCount);
  await action("artwork-vectorize").click();await expect(m => m.drawings.some(row => row.sourceKind === "image-vector"), "explicit image trace");await settled();
  const vector = (await model()).drawings.find(row => row.sourceKind === "image-vector"), vectorGeometry = await geometry(vector.id);assert.equal(vectorGeometry.curves.length, 2);assert.ok(vectorGeometry.curves.every(curve => curve.type === "polyline" && curve.closed));near(netArea(vectorGeometry.curves), 88, 1e-6);assert.deepEqual(await row(image.id), imageForTrace);assert.deepEqual(vector.machiningSource.geometryRef, imageForTrace.geometryRef);
  await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.selectedIdsByKind.drawing.includes(id); }, vector.id);
  const angle = Math.PI / 6, projected = ([x, y]) => [10 + x * Math.cos(angle) - y * Math.sin(angle), 20 + x * Math.sin(angle) + y * Math.cos(angle)];
  const expectedCorners = [[2, 2], [14, 2], [14, 10], [2, 10], [6, 5], [10, 5], [10, 7], [6, 7]].map(projected), actualCorners = vectorGeometry.curves.flatMap(curve => curve.points);
  for (const p of expectedCorners) assert.ok(actualCorners.some(q => Math.hypot(q[0] - p[0], q[1] - p[1]) < 1e-6), "Trace lost the exact physical pixel-cell pose");
  report.imageTrace = { image: imageForTrace, vector, geometry: vectorGeometry, pixels: { ...imagePixels, rgba: undefined } };
  passed("A genuine PNG file queue imports a selectable unlit UV-textured reference with white pixels preserved; mouse selection and real transforms precede explicit threshold tracing into two correctly posed closed contours of net area 88, while the original image/ref remain intact");

  const colorImage = await importImage(colorPath);await fill({ widthMm: 80, heightMm: 60, x: 70, y: 10, angleDeg: 0 });await action("artwork-update-image").click();await settled();await action("artwork-cancel").click();await clickPlaneCommand(page, "view.fit");await blank();const colorPixels = await artworkPixels(colorImage.id), colorEnvelope = await textureEnvelope(await row(colorImage.id));await page.waitForTimeout(100);
  report.colorTexturePixels = await shotSamples("worktable-text-and-images.png", [
    { point: [90, 55], rgb: [230, 40, 30] }, { point: [130, 55], rgb: [40, 200, 60] },
    { point: [90, 25], rgb: [40, 80, 220] }, { point: [130, 25], rgb: [230, 190, 30] },
  ]);
  passed("Actual screenshot pixels preserve the image's red/green top and blue/yellow bottom quadrants, confirming original-color rendering, UV orientation and image background placement");

  let heldTexture, releaseTexture;
  const textureGate = new Promise(resolve => { releaseTexture = resolve; });
  const delayTexture = async route => {
    let request;try { request = route.request().postDataJSON(); } catch { await route.continue();return; }
    if (request?.action !== "resource" || request.request?.method !== "GET") { await route.continue();return; }
    const actual = await route.fetch(), payload = await actual.json(), resource = payload.result;
    const bytes = resource?.bodyBase64 ? Buffer.from(resource.bodyBase64, "base64") : Buffer.alloc(0);
    if (!heldTexture && resource?.status === 200 && bytes.length > 8 && bytes.toString("ascii", 4, 8) === "ICRT") {
      heldTexture = { url: request.request.url, status: resource.status, identifier: "ICRT", bytes: bytes.length };
      await textureGate;
    }
    await route.fulfill({ response: actual });
  };
  await page.route("**/native-rpc", delayTexture);
  try {
    const before = new Set((await model()).drawings.map(row => row.id));await page.evaluate(path => __planeNativeFileQueue.push(path), imagePath);await palette("plane.image").click();
    const deadline = Date.now() + 45000;while (!heldTexture && Date.now() < deadline) await page.waitForTimeout(25);assert.ok(heldTexture, "No actual ICRT texture response was observed");
    await expect(m => m.drawings.some(row => !before.has(row.id) && row.contentType === "image"), "native image exists before its real delayed texture response");
    const pendingImage = (await model()).drawings.find(row => !before.has(row.id) && row.contentType === "image");
    await page.locator('.title-bar [data-action="undo-project"]').click();await expect(m => !m.drawings.some(row => row.id === pendingImage.id), "Undo removes image while ICRT response is held");
    releaseTexture();await settled();
    await waitForAsync(async ({ id, url }) => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), v = view.viewport;return view.sceneProxy.sdoClient.pending.size === 0 && !v.texturePromises.has(url) && !v.sceneObjects.has(id) && !v.texturePayloads.has(url); }, { id: pendingImage.id, url: heldTexture.url });
    assert.equal((await state()).selected.drawing.includes(pendingImage.id), false, "Late image completion selected an entity already removed by Undo");
    await page.locator('.title-bar [data-action="redo-project"]').click();await expect(m => m.drawings.some(row => row.id === pendingImage.id), "Redo restores the same immutable image");await settled();
    const reloaded = await artworkPixels(pendingImage.id);assert.equal(pixelHash(reloaded), pixelHash(imagePixels));report.delayedTexture = { ...heldTexture, imageId: pendingImage.id, droppedAfterUndo: true, reloadedAfterRedo: true };
  } finally { releaseTexture();await page.unroute("**/native-rpc", delayTexture); }
  passed("Delaying an actual ICRT texture response while Undo removes its target cannot attach stale texture to the scene; Redo reloads the same immutable resource URL and real pixels");

  await page.evaluate(path => __planeNativeFileQueue.push(path), badPath);const beforeBad = sorted((await model()).drawings);await palette("plane.image").click();await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const u = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return !u.busy && !u.geometryTaskId && Boolean(u.operationError); });
  assert.match((await state()).error, /图片|读取/);assert.doesNotMatch((await state()).error, /PLANE_|HRESULT/);assert.deepEqual(sorted((await model()).drawings), beforeBad);await palette("plane.select").click();await settled();
  passed("A real decoder failure exposes a concise actionable Chinese message, leaves every drawing unchanged and exits the asynchronous task without publishing a placeholder object");

  await select(text.id);await clickPlaneCommand(page, "plane.layers");await page.locator(".icax-layer-manager").waitFor();
  const layerId = text.layerId, layerToggle = value => page.locator(`[data-layer-ui-action="update"][data-layer-ui-id="${layerId}"][data-layer-ui-field="locked"][data-layer-ui-value="${value}"]`);
  await layerToggle(true).click();await expect(m => m.layers.find(row => row.id === layerId)?.locked === true, "layer locked");await settled();assert.equal(await action("artwork-open-text").isEnabled(), false);
  const lockedDrawings = sorted((await model()).drawings);await palette("plane.text").click();await field("text").fill("Locked creation must be rejected");await action("artwork-create-text").click();
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const u = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return !u.busy && Boolean(u.operationError); });
  assert.match((await state()).error, /锁定|解锁/);assert.equal(await field("text").inputValue(), "Locked creation must be rejected");assert.deepEqual(sorted((await model()).drawings), lockedDrawings);
  await layerToggle(false).click();await expect(m => m.layers.find(row => row.id === layerId)?.locked === false, "layer unlocked");await settled();await page.locator('[data-layer-ui-action="close"]').click();await action("artwork-cancel").click();
  passed("Actual layer locking disables editing existing text and rejects new text without publishing any drawing, preserving the entered draft and recovering normally after Unlock");

  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();await page.evaluate(path => __planeNativeFileQueue.push(path), partPath);await clickPlaneCommand(page, "plane.import-parts");await expect(m => m.parts.length === 1, "CAD import");await settled();
  let part = (await model()).parts[0];await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${part.id}"]`).first().click();await action("artwork-open-load-part").click();await fill({ x: 30, y: 40 });await action("artwork-load-part").click();await expect(m => m.drawings.some(row => row.sourceKind === "part"), "independent part load");await settled();
  await page.waitForFunction(() => document.querySelector(".cam-workbench")?.dataset.iplaneArea === "drawing");const partCopy = (await model()).drawings.find(row => row.sourceKind === "part"), copyGeometry = await geometry(partCopy.id);assert.equal(partCopy.sourceObjectId, part.id);assert.equal(copyGeometry.curves.length, 2);const copyHole = copyGeometry.curves.find(curve => curve.type === "circle");near(copyHole.center[0], 50);near(copyHole.center[1], 60);
  await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.selectedIdsByKind.drawing.includes(id); }, partCopy.id);
  await command("移动 3,4");const editedCopy = await geometry(partCopy.id);assert.notDeepEqual(editedCopy, copyGeometry);assert.deepEqual((await model()).parts[0], part);
  const editedPartCopy = await row(partCopy.id);assert.equal(editedPartCopy.holeCount, 1, "Moving a machining part copy must retain its typed hole count");
  report.partCopy = { source: part, original: partCopy, edited: editedPartCopy, editedGeometry: editedCopy };
  passed("Importing real DXF CAD and loading it through the UI creates one independent translated machining copy; secondary editing changes that copy while preserving the complete source CAD row");

  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${part.id}"]`).first().click();await page.locator('[data-iplane-field="quantity"]').fill("2");await action("apply-properties").click();await settled();part = (await model()).parts[0];
  await clickPlaneCommand(page, "plane.stock-rect");for (const [name, value] of Object.entries({ name: "加工副本验收板", width: "220", height: "120", quantity: "1" })) await page.locator(`[data-iplane-form="stock-rect"][data-iplane-form-field="${name}"]`).fill(value);await action("create-rectangle").click();await settled();
  await clickPlaneCommand(page, "plane.nesting-settings");for (const [name, value] of Object.entries({ partGap: "2", stockMargin: "5", rotationStepDeg: "90", timeLimitSeconds: "1" })) await page.locator(`[data-iplane-form="nesting-settings"][data-iplane-form-field="${name}"]`).fill(value);await action("apply-nesting-settings").click();await settled();
  await clickPlaneCommand(page, "plane.nesting-run");await expect(m => m.results.length === 1, "real nesting result");await settled();const result = (await model()).results[0];
  const nesting = await page.evaluate(async resultId => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneNesting.ReadReport", { resultId }); }, result.id);
  assert.equal(nesting.placed, 2);assert.equal(nesting.boards.length, 1);const board = nesting.boards[0];await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${board.id}"]`).first().click();await action("artwork-open-load-result").click();await action("artwork-load-result").click();await expect(m => m.drawings.some(row => row.sourceKind === "result-board"), "result board loaded");await settled();
  const copies = (await model()).drawings.filter(row => row.machiningSource?.boardId === board.id), background = copies.find(row => row.referenceOnly), placements = copies.filter(row => row.sourceKind === "result-placement");assert.ok(background);assert.equal(placements.length, 2);assert.equal((await state()).selected.drawing.includes(background.id), false);
  await waitForAsync(async ids => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const u = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return ids.some(id => u.selectedIdsByKind.drawing.includes(id)); }, placements.map(row => row.id));
  for (const placement of placements) {
    const link = placement.machiningSource, original = board.placements.find(item => item.id === link.placementId);assert.ok(original);near(link.x, original.x);near(link.y, original.y);near(link.angleDeg, original.angleDeg);assert.equal(Boolean(link.mirror), Boolean(original.flip));
    const radians = original.angleDeg * Math.PI / 180, transform = ([x, y]) => { const a = x * Math.cos(radians) - y * Math.sin(radians), b = x * Math.sin(radians) + y * Math.cos(radians);return [a + original.x, (original.flip ? -b : b) + original.y]; };
    const exact = await geometry(placement.id), circle = exact.curves.find(curve => curve.type === "circle"), expected = transform([20, 20]);near(circle.center[0], expected[0]);near(circle.center[1], expected[1]);near(circle.radius, 5);
    const boundary = exact.curves.find(curve => curve.type === "polyline");assert.equal(boundary.closed, true);for (const p of [[0, 0], [80, 0], [80, 50], [0, 50]].map(transform)) assert.ok(boundary.points.some(q => Math.hypot(q[0] - p[0], q[1] - p[1]) < 1e-6));
  }
  await ensurePlaneObjectsExpanded(page);assert.equal(await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${background.id}"]`).isEnabled(), false);await clickPlaneCommand(page, "view.fit");await palette("plane.select").click();await clickPoint([0, board.height * .5]);assert.equal((await state()).selected.drawing.includes(background.id), false);
  await select(placements[0].id);await command("移动 7,0");const nestingAfter = await page.evaluate(async resultId => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneNesting.ReadReport", { resultId }); }, result.id);assert.deepEqual(nestingAfter, nesting);assert.deepEqual((await model()).parts[0], part);
  const editedPlacement = await row(placements[0].id);assert.equal(editedPlacement.holeCount, 1, "Moving a result placement copy must retain its typed hole count");
  report.resultCopies = { board, background, placements, editedPlacement, report: nesting };
  passed("Real nesting loads the selected result board as one unselectable reference and one independent EC per placement; exact rectangle/hole curves follow each actual rotation/mirror/translation and later copy edits preserve CAD and the nesting report");

  await select(text.id);await action("artwork-open-text").click();await field("text").fill("未提交异步草稿\nUnsubmitted draft");await page.setViewportSize({ width: 1360, height: 520 });
  const probe = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId), f = document.querySelector('[data-iplane-artwork-panel="text-edit"] textarea'), left = document.querySelector("[data-iplane-list]"), right = document.querySelector(".cam-info-pane");f.focus({ preventScroll: true });f.setSelectionRange(2, 7);left.scrollTop = left.scrollHeight;right.scrollTop = right.scrollHeight * .4;globalThis.__worktableProbe = { f, left, right, canvas: v.viewport.root.querySelector("canvas"), camera: v.viewport.getCameraState(), events: 0 };f.addEventListener("worktable-probe", () => __worktableProbe.events++);return { selection: [f.selectionStart, f.selectionEnd], text: f.value }; });
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneScene.ReadState"))); });
  const refresh = page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { refreshPlaneState } = await import("/apps/iplane-cam/webpage/nativeOperations.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId);await refreshPlaneState(v.iplaneLive.context, v, v.iplaneLive.ops, { refreshView: true }); });
  await waitForAsync(() => __planeNativeBridge.isResponseHeld());const scroll = await page.evaluate(() => { const p = __worktableProbe;p.left.scrollTop = p.left.scrollHeight * .6;p.right.scrollTop = p.right.scrollHeight * .8;return [p.left.scrollTop, p.right.scrollTop]; });assert.ok(scroll.every(value => value > 0), "Both actual sidebars must scroll in this refresh acceptance");await page.evaluate(() => __planeNativeBridge.releaseResponse());await refresh;
  const retained = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId), p = __worktableProbe, f = document.querySelector('[data-iplane-artwork-panel="text-edit"] textarea');f.dispatchEvent(new Event("worktable-probe"));return { same: f === p.f && p.left === document.querySelector("[data-iplane-list]") && p.right === document.querySelector(".cam-info-pane") && p.canvas === v.viewport.root.querySelector("canvas"), focused: document.activeElement === f, text: f.value, selection: [f.selectionStart, f.selectionEnd], events: p.events, scroll: [p.left.scrollTop, p.right.scrollTop], camera: v.viewport.getCameraState(), beforeCamera: p.camera }; });
  assert.equal(retained.same, true);assert.equal(retained.focused, true);assert.equal(retained.events, 1);assert.equal(retained.text, probe.text);assert.deepEqual(retained.selection, probe.selection);assert.deepEqual(retained.scroll, scroll);assert.deepEqual(retained.camera, retained.beforeCamera);report.asyncTextRefresh = retained;await action("artwork-cancel").click();await page.setViewportSize({ width: 1600, height: 1000 });
  passed("A held genuine native refresh preserves the text draft, exact editor/canvas/list nodes, event listener, caret/focus, latest pane scrolling and camera without writing text");

  const saved = await model(), machiningCopyIds = [partCopy.id, ...placements.map(row => row.id)];
  for (const id of machiningCopyIds) assert.equal(saved.drawings.find(row => row.id === id)?.holeCount, 1, "The saved EC snapshot must retain every machining copy's typed hole count");
  report.savedWorktableSnapshot = structuredClone(saved);await page.locator('.title-bar [data-command-id="app.save"]').click();const deadline = Date.now() + 30000;while (!existsSync(savedProject) && Date.now() < deadline) await page.waitForTimeout(25);assert.ok(existsSync(savedProject) && statSync(savedProject).size > 100);await waitForAsync(() => !document.querySelector('.title-bar [data-command-id="app.save"]')?.disabled);
  for (const path of [imagePath, colorPath, partPath, badPath]) unlinkSync(path);
  const projectId = await page.evaluate(() => __icaxAppShell.getState().activeProjectId), closing = page.evaluate(id => __icaxAppShell.closeProject(id), projectId);await waitForAsync(id => __icaxAppShell.getState().activeProjectId !== id || Boolean(document.querySelector("[data-project-close-dialog]")), projectId);if (await page.locator("[data-project-close-dialog]").count()) await page.locator('[data-project-close-choice="discard"]').click();await closing;
  await page.evaluate(path => __icaxAppShell.openProject(path, "icax.iplane-cam"), savedProject);await expect(m => m.drawings.length === saved.drawings.length, "reopened drawings");await settled();const reopened = await model();assert.deepEqual(sorted(reopened.drawings), sorted(saved.drawings));
  for (const id of machiningCopyIds) assert.equal(reopened.drawings.find(row => row.id === id)?.holeCount, 1, "Reopening the saved project must retain every machining copy's typed hole count");
  const reopenedTextPixels = await artworkPixels(text.id);assert.equal(pixelHash(reopenedTextPixels), pixelHash(updatedTextPixels));assert.equal(pixelHash(await artworkPixels(image.id)), pixelHash(imagePixels));assert.equal(pixelHash(await artworkPixels(colorImage.id)), pixelHash(colorPixels));
  assert.equal((await textureEnvelope(await row(text.id))).encodedImageSha256, updatedTextEnvelope.encodedImageSha256);assert.equal((await textureEnvelope(await row(image.id))).encodedImageSha256, imageEnvelope.encodedImageSha256);assert.equal((await textureEnvelope(await row(colorImage.id))).encodedImageSha256, colorEnvelope.encodedImageSha256);
  await ensurePlaneObjectsExpanded(page);await select(text.id);await action("artwork-open-text").click();await field("text").fill("重新打开后仍可编辑\nReopened");await action("artwork-update-text").click();await settled();assert.equal((await row(text.id)).text, "重新打开后仍可编辑\nReopened");assert.notEqual(pixelHash(await artworkPixels(text.id, reopenedTextPixels.textureUuid)), pixelHash(reopenedTextPixels));
  report.unsavedWorktableSnapshot = await model();
  report.savedWorktable = { path: savedProject, bytes: statSync(savedProject).size, sourceFilesDeleted: [imagePath, colorPath, partPath], drawingCount: saved.drawings.length, pixelHashes: { text: pixelHash(updatedTextPixels), image: pixelHash(imagePixels), color: pixelHash(colorPixels) }, textureEnvelopes: [updatedTextEnvelope, imageEnvelope, colorEnvelope] };
  await page.screenshot({ path: resolve(output, "worktable-reopened.png") });
  passed("Saving, closing and reopening after deleting the actual PNG/DXF inputs retains every drawing identity and embedded bitmap resource, and the same reopened text entity remains editable");
  assert.ok(Object.keys(assets).some(path => path.endsWith("/artworkOperations.mjs")));assert.ok(Object.keys(assets).some(path => path.endsWith("/planarToolPalette.mjs")));
}
