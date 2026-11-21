import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { isDeepStrictEqual } from "node:util";
import { runNativePlaneBrowser } from "./iPlaneCAM.native.browserHarness.mjs";
import { exerciseWorktable } from "./iPlaneCAM.worktable.native.browserCase.mjs";
import { clickPlaneCommand, ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";

const phase = process.env.ICAX_PLANE_FRESH_PHASE;
assert.ok(["prepare-discard", "prepare-recovery", "reopen-discard", "reopen-recovery", "probe"].includes(phase));
const manifestPath = process.env.ICAX_PLANE_FRESH_MANIFEST;
const file = path => existsSync(path) ? { path, exists: true, bytes: statSync(path).size, sha256: createHash("sha256").update(readFileSync(path)).digest("hex") } : { path, exists: false };
const files = path => ({ project: file(path), journal: file(path + ".log"), resourceCheckpoint: file(path + ".log.resources") });
const readState = page => page.evaluate(async () => {
  const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
  const v = getProjectView(__icaxAppShell.getState().activeProjectId);
  return (await v.sceneProxy.invoke("PlaneScene.ReadState", {})).iplaneCam;
});
const business = state => Object.fromEntries(["drawings", "parts", "stocks", "results", "layers", "nestingSettings"].filter(key => key in state).map(key => [key, structuredClone(state[key])]));

async function resourceOracle(page, state) {
  return page.evaluate(async state => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const { parseRenderGeometryResource, parseRenderMaterialResource, parseRenderTextureResource } = await import("/iCAX-UI/SDK/Viewport/renderResource.mjs");
    const v = getProjectView(__icaxAppShell.getState().activeProjectId), refs = new Map();
    const visit = (value, key = "") => { if (!value || typeof value !== "object") return;if (key.endsWith("Ref") && value.url) { const render = ["previewRef", "materialRef", "textureRef"].includes(key);const previous = refs.get(value.url);refs.set(value.url, { reference: value, render: render || previous?.render || false }); }for (const [childKey, child] of Object.entries(value)) visit(child, childKey); };
    visit(state);
    const digest = async bytes => [...new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))].map(v => v.toString(16).padStart(2, "0")).join("");
    const result = [];
    for (const [url, { reference, render }] of refs) {
      const response = await v.sceneProxy.resources.get(url, { headers: { Accept: render ? "application/vnd.icax.flatbuffer" : "*/*", "ICAX-Resource-Version": String(reference.version || "0") } }), buffer = await response.arrayBuffer();
      const row = { reference, render, status: response.status, bytes: buffer.byteLength };
      if (response.status === 200) {
        row.sha256 = await digest(buffer);row.identifier = String.fromCharCode(...new Uint8Array(buffer).slice(4, 8));
        if (row.identifier === "ICRG") { const data = parseRenderGeometryResource(buffer);row.geometry = { kind: data.kind, schemaVersion: data.schemaVersion, dataVersion: data.dataVersion, coordinateCount: data.positions?.length ?? data.points?.length ?? 0 }; }
        if (row.identifier === "ICRM") row.material = parseRenderMaterialResource(buffer);
        if (row.identifier === "ICRT") { const data = parseRenderTextureResource(buffer);row.texture = { schemaVersion: data.schemaVersion, dataVersion: data.dataVersion, mimeType: data.mimeType, pixelWidth: data.pixelWidth, pixelHeight: data.pixelHeight, encodedImageSha256: await digest(data.encodedImage) }; }
      }
      result.push(row);
    }
    return result.sort((a, b) => a.reference.url.localeCompare(b.reference.url));
  }, state);
}

async function renderOracle(page, state, waitForAsync) {
  const textured = state.drawings.filter(row => row.textureRef?.url);
  await waitForAsync(async ids => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId);
    return ids.every(id => { let ready = false;v.viewport.sceneObjects.get(id)?.traverse(node => { const m = Array.isArray(node.material) ? node.material[0] : node.material;if (m?.map?.image?.width && node.geometry?.attributes?.uv) ready = true; });return ready; });
  }, textured.map(row => row.id), { timeout: 30000 });
  return page.evaluate(async state => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId), result = [];
    for (const row of state.drawings) {
      const root = v.viewport.sceneObjects.get(row.id), item = { id: row.id, present: Boolean(root), textures: [] };if (root) { const meshes = [];root.traverse(node => { const m = Array.isArray(node.material) ? node.material[0] : node.material;if (m?.map?.image?.width && node.geometry?.attributes?.uv) meshes.push({ node, m }); });
        for (const { node, m } of meshes) { const image = m.map.image, c = document.createElement("canvas");c.width = image.width;c.height = image.height;const ctx = c.getContext("2d");ctx.drawImage(image, 0, 0);const rgba = ctx.getImageData(0, 0, c.width, c.height).data, hash = [...new Uint8Array(await crypto.subtle.digest("SHA-256", rgba))].map(v => v.toString(16).padStart(2, "0")).join("");item.textures.push({ width: c.width, height: c.height, rgbaSha256: hash, materialType: m.type, flipY: m.map.flipY, uv: Array.from(node.geometry.attributes.uv.array), positions: Array.from(node.geometry.attributes.position.array) }); }
      }result.push(item);
    }return result.sort((a, b) => a.id.localeCompare(b.id));
  }, state);
}

async function geometryOracle(page, state) {
  return page.evaluate(async ids => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId), result = [];for (const id of ids) result.push({ id, geometry: await v.sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }) });return result; }, state.drawings.filter(row => !row.textureRef?.url).map(row => row.id).sort());
}

await runNativePlaneBrowser({
  processExitMode: phase === "prepare-recovery" ? "crash" : "graceful",
  scope: "One real native bridge child process and real Chromium AppShell/SDK. A separate orchestrator starts independent prepare/reopen child processes. Explicit GUI discard restores the saved snapshot; crash recovery replays unsaved ECS and all generated resources. No cache, database or journal deletion. Native GET, exact geometry and rendered texture pixels are independent read-only oracles.",
  projectName: "独立进程持久化验收", defaultOutput: "output/tests/iplane-cam-worktable/source-fresh-process",
  savedPrefix: "fresh-worktable", reportName: "fresh-process-phase-report.json", noCasesMessage: "No independent-process persistence checks ran",
  exercise: async context => {
    const { page, report, waitForAsync, passed, output } = context;
    report.phase = phase;
    if (phase.startsWith("prepare-")) {
      await exerciseWorktable(context);
      const saved = report.savedWorktableSnapshot, unsaved = report.unsavedWorktableSnapshot;
      assert.ok(saved && unsaved, "Prepare case must expose both exact business snapshots");
      const savedResources = await resourceOracle(page, saved), unsavedResources = await resourceOracle(page, unsaved);
      for (const row of [...savedResources, ...unsavedResources]) assert.equal(row.status, row.render ? 200 : 406, row.reference.url);
      const manifest = { savedProject: context.savedProject, saved, unsaved, savedResources, unsavedResources, geometry: await geometryOracle(page, unsaved), unsavedRender: await renderOracle(page, unsaved, waitForAsync), savedPixelHashes: report.savedWorktable.pixelHashes, deletedInputs: report.savedWorktable.sourceFilesDeleted };
      if (phase === "prepare-discard") {
        const id = await page.evaluate(() => __icaxAppShell.getState().activeProjectId), closing = page.evaluate(id => __icaxAppShell.closeProject(id), id);
        await page.locator("[data-project-close-dialog]").waitFor();report.discardDialogObserved = true;
        await page.locator('[data-project-close-choice="discard"]').click();await closing;
        assert.notEqual(await page.evaluate(() => __icaxAppShell.getState().activeProjectId), id);
        passed("The actual dirty project close dialog explicitly discards the newer text edit before the owned native process exits");
      } else passed("An unsaved newer text edit and all generated render/private resources exist before actual forced native process termination");
      report.projectFiles = files(context.savedProject);writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));report.manifest = manifestPath;
      return;
    }
    const manifest = phase === "probe" ? null : JSON.parse(readFileSync(manifestPath, "utf8"));
    const project = manifest?.savedProject || process.env.ICAX_PLANE_FRESH_PROJECT;
    report.filesBeforeOpen = files(project);
    try { await page.evaluate(path => __icaxAppShell.openProject(path, "icax.iplane-cam"), project); } catch (error) { report.openError = error.message; }
    const actual = await readState(page);report.actualSnapshot = actual;report.resources = await resourceOracle(page, actual);report.filesAfterOpen = files(project);
    const expected = manifest && (phase === "reopen-discard" ? manifest.saved : manifest.unsaved);
    if (expected) assert.ok(isDeepStrictEqual(business(actual), business(expected)), "Fresh native process must restore the exact intended saved or recovered business snapshot; see manifest and actualSnapshot");
    passed(phase === "reopen-discard" ? "A new native process reads the exact saved business snapshot after real GUI discard" : "A new native process reads the legitimate unsaved recovery snapshot without suppressing the journal");
    assert.equal(report.openError, undefined, report.openError);
    for (const row of report.resources) assert.equal(row.status, row.render ? 200 : 406, row.reference.url);
    if (manifest) assert.ok(isDeepStrictEqual(report.resources, phase === "reopen-discard" ? manifest.savedResources : manifest.unsavedResources), "Every real fresh GET must retain bytes, version and typed render envelope");
    for (const row of actual.drawings.filter(row => row.textureRef?.url)) {
      const material = report.resources.find(r => r.reference.url === row.materialRef?.url)?.material, texture = report.resources.find(r => r.reference.url === row.textureRef?.url)?.texture;
      assert.equal(material?.baseColorTextureUrl, row.textureRef.url);assert.ok((material.materialFlags & 3) === 3);assert.equal(texture?.pixelWidth, row.pixelWidth);assert.equal(texture?.pixelHeight, row.pixelHeight);
    }
    passed("All actual native render GETs return valid ICRG/ICRM/ICRT with retained bytes and linked texture dimensions; private algorithm resources remain available behind the binary boundary");
    report.geometry = await geometryOracle(page, actual);
    const signedZeroPaths = [];const findSignedZeros = (value, path) => { if (Object.is(value, -0)) signedZeroPaths.push(path);else if (value && typeof value === "object") for (const [key, child] of Object.entries(value)) findSignedZeros(child, path + "." + key); };findSignedZeros(report.geometry, "geometry");
    report.geometryWireNormalization = { signedZeroPaths, comparison: "Exact JSON wire values against the persisted JSON oracle; raw resource bytes are checked separately" };
    if (manifest) assert.ok(isDeepStrictEqual(JSON.parse(JSON.stringify(report.geometry)), manifest.geometry), "Exact native source curves changed across an independent process restart");
    await clickPlaneCommand(page, "view.fit");report.render = await renderOracle(page, actual, waitForAsync);
    assert.ok(report.render.every(row => row.present), "Every restored drawing must have a real SDK scene object");
    for (const row of actual.drawings.filter(value => value.textureRef?.url)) {
      const texture = report.render.find(value => value.id === row.id)?.textures[0];assert.ok(texture, "Restored artwork omitted its actual rendered bitmap");
      const a = row.angleDeg * Math.PI / 180, c = Math.cos(a), s = Math.sin(a), corners = [[0, 0], [row.width, 0], [row.width, row.height], [0, row.height]].map(([x, y]) => [row.x + x * c - y * s, row.y + x * s + y * c]);
      for (const p of corners) assert.ok(Array.from({ length: texture.positions.length / 3 }, (_, i) => texture.positions.slice(i * 3, i * 3 + 2)).some(q => Math.hypot(p[0] - q[0], p[1] - q[1]) < 2e-4), "Fresh renderer physical quad differs from typed restored artwork pose");
    }
    const expectedRender = manifest?.unsavedRender;
    if (expectedRender) for (const row of report.render) {
      const previous = expectedRender.find(value => value.id === row.id);if (phase === "reopen-recovery" || row.id !== manifest.unsaved.drawings.find(value => value.contentType === "text" && value.text.includes("Reopened"))?.id) assert.deepEqual(row, previous);
      else { assert.equal(row.textures.length, 1);assert.equal(row.textures[0].rgbaSha256, manifest.savedPixelHashes.text); }
    }
    if (manifest) for (const path of manifest.deletedInputs) assert.equal(existsSync(path), false, "Fresh reopen must not depend on original PNG/DXF input: " + path);
    await page.screenshot({ path: resolve(output, "fresh-process-restored.png") });
    passed("Exact native source curves and actual SDK scene objects, textured bitmap pixels, physical UV quads and source-free reopen survive independent process restart");
    if (manifest) {
      assert.equal(file(project).sha256, report.filesBeforeOpen.project.sha256, "Read-only reopen verification changed the saved archive");
      const image = actual.drawings.find(row => row.contentType === "image" && row.sourceName === "hole-reference.png" && row.width === 16 && row.height === 12);
      assert.ok(image, "The recovered physical hole-reference image is required for the backend bitmap-use regression");
      await page.locator('[data-planar-tool="plane.select"]').click();await ensurePlaneObjectsExpanded(page);
      await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${image.id}"]`).first().click();
      await page.locator('[data-iplane-action="artwork-open-vectorize"]').click();
      const panel = page.locator('[data-iplane-artwork-panel]');
      for (const [name, value] of Object.entries({ threshold: "128", minAreaMm2: "0", simplifyMm: "0" })) await panel.locator(`[data-iplane-form-field="${name}"]`).fill(value);
      await panel.locator('[data-iplane-form-field="invert"]').setChecked(false);
      const beforeIds = actual.drawings.map(row => row.id);await page.locator('[data-iplane-action="artwork-vectorize"]').click();
      await waitForAsync(async previous => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const v = getProjectView(__icaxAppShell.getState().activeProjectId), u = v.iplaneCamUI;if (u.operationError) throw new Error(u.operationError);return !u.busy && !u.geometryTaskId && v.scene.iplaneCam.drawings.some(row => !previous.includes(row.id) && row.sourceKind === "image-vector"); }, beforeIds);
      const tracedState = await readState(page), traced = tracedState.drawings.find(row => !beforeIds.includes(row.id));assert.equal(traced.sourceKind, "image-vector");assert.notEqual(traced.id, image.id);assert.deepEqual(traced.machiningSource.geometryRef, image.geometryRef);
      const exact = await page.evaluate(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneDrawing.ReadGeometry", { id }); }, traced.id);
      assert.equal(exact.curves.length, 2);assert.ok(exact.curves.every(curve => curve.type === "polyline" && curve.closed));
      const area = Math.abs(exact.curves.reduce((sum, curve) => sum + curve.points.reduce((area, p, i) => { const q = curve.points[(i + 1) % curve.points.length];return area + p[0] * q[1] - q[0] * p[1]; }, 0) / 2, 0));assert.ok(Math.abs(area - 88) < 1e-6);
      const a = image.angleDeg * Math.PI / 180, transform = ([x, y]) => [image.x + x * Math.cos(a) - y * Math.sin(a), image.y + x * Math.sin(a) + y * Math.cos(a)];
      for (const p of [[2, 2], [14, 2], [14, 10], [2, 10], [6, 5], [10, 5], [10, 7], [6, 7]].map(transform)) assert.ok(exact.curves.flatMap(curve => curve.points).some(q => Math.hypot(p[0] - q[0], p[1] - q[1]) < 1e-6), "Fresh backend tracing lost the retained bitmap's physical pixel-cell pose");
      assert.deepEqual(tracedState.drawings.find(row => row.id === image.id), image);
      await page.locator('.title-bar [data-action="undo-project"]').click();
      await waitForAsync(async id => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).scene.iplaneCam.drawings.some(row => row.id === id); }, traced.id);
      const afterUndo = await readState(page);assert.ok(isDeepStrictEqual(business(afterUndo), business(actual)), "Undo of fresh-process functional tracing must restore the original business objects");
      report.freshVectorization = { sourceId: image.id, created: traced, geometry: exact, netAreaMm2: area, originalBitmapUntouched: true, undoRestoredBusinessSnapshot: true };
      report.filesAfterFunctionalVerification = files(project);
      assert.equal(report.filesAfterFunctionalVerification.project.sha256, report.filesBeforeOpen.project.sha256, "Functional tracing/Undo must not rewrite the original saved project archive");
      passed("Real fresh-process UI vectorization reuses the recovered private bitmap to create independent exact outer/hole contours of net area 88; Undo restores the original business objects and the saved archive hash is unchanged");
    }
  },
});
