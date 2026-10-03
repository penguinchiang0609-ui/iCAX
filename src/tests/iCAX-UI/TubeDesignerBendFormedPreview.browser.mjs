import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const templateRoot = new URL("../../apps/tube-designer/templates/assembly/", import.meta.url);
const templates = readdirSync(templateRoot, { withFileTypes: true })
  .filter((entry) => entry.isDirectory())
  .flatMap((entry) => {
    try { return [JSON.parse(readFileSync(new URL(`${entry.name}/assembly.json`, templateRoot), "utf8"))]; }
    catch { return []; }
  });
const bend = templates.find((item) => item.id === "bend");
assert.ok(bend, "冷折弯模板必须存在");
assert.equal(bend.displayName, "冷折弯");
assert.deepEqual(bend.partProcesses, []);
assert.equal(bend.parameters.some(({ key }) => key === "bendPlane" || key === "planeRotation"), false);

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 820 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://bend-formed.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://bend-formed.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `
    body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:820px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{position:relative;min-width:0;background:#13252d}
    .cam-viewport>canvas{width:100%;height:100%}
    .tube-connection-library-list{height:110px;overflow:auto}
    .tube-connection-library-editor-body{height:140px;overflow:auto}` });

  const result = await page.evaluate(async ({ templates, bend }) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { encodeNestingGeometry } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const { parseRenderGeometryResource } = await import("/src/iCAX-UI/SDK/Viewport/renderResource.mjs");
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const resources = new Map();
    const snapshots = [];
    const annotationCalls = [];
    const previewRequests = [];
    const pendingPlans = [];
    const viewports = [];
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "bend", workMode: "example", workModeUserSelected: true,
        parameterDrafts: { bend: { bendPlane: "spatial", planeRotation: 45 } } } };

    const planFor = (payload) => {
      const values = { ...payload.parameters, ...payload.sceneParameters };
      const designParts = bend.previewScene.designParts.map((part) => ({
        id: `design-${part.role}`, role: part.role, label: part.role,
        request: { profileRef: part.profileRef, parameters: part.parameters, length: part.length,
          features: [], ends: {} }, matrix: identity, compareMatrix: identity,
      }));
      const section = designParts[0].request;
      const developedLength = designParts.reduce((length, part) => length + part.request.length, 0)
        + (values.bendRadius + values.bendFactor * section.parameters.wallThickness) * values.angle * Math.PI / 180;
      return {
        schema: "icax.assembly-preview-plan", templateId: "bend", parameters: values,
        sceneParameters: payload.sceneParameters,
        formedPreviewRecipe: {
          kind: "continuous-cold-bend", blankId: "bentBlank", roles: ["segmentA", "segmentB"],
          angle: values.angle, radius: values.bendRadius, factor: values.bendFactor,
        },
        previewAnnotations: [
          { id: "bend-angle", view: "finished", kind: "value-note", label: "折弯角", value: values.angle, unit: "°" },
          { id: "bend-factor", view: "blank", kind: "value-note", label: "折弯因子 K", value: values.bendFactor },
        ],
        resolvedWorkflow: { realization: "integrated", partOperations: [],
          blankParts: [{ id: "bentBlank", label: "连续折弯下料件", participantRoles: ["segmentA", "segmentB"] }],
          assemblySteps: [{ id: "bend", label: "冷折成形" }], bom: [], checks: [] },
        designParts,
        manufacturingParts: [{ id: "manufacturing-bentBlank", blankId: "bentBlank", label: "连续折弯下料件",
          sourceRole: "segmentA", participantRoles: ["segmentA", "segmentB"],
          request: { profileRef: section.profileRef, parameters: section.parameters,
            length: developedLength, features: [], ends: {} }, matrix: identity,
          explodedMatrix: identity, compareMatrix: identity }],
      };
    };

    // The mocked native preview returns a real encoded, hollow straight-tube
    // resource. The viewport reads it through the same resource contract used
    // for the automatically generated formed appearance.
    const straightTube = (request) => {
      const { width, depth, wallThickness: wall } = request.parameters;
      const rectangle = (halfWidth, halfDepth) => [
        [-halfWidth, -halfDepth], [halfWidth, -halfDepth],
        [halfWidth, halfDepth], [-halfWidth, halfDepth],
      ];
      const outer = rectangle(width / 2, depth / 2);
      const inner = rectangle(width / 2 - wall, depth / 2 - wall);
      const positions = [];
      for (const contour of [outer, inner]) for (const x of [0, request.length])
        for (const [y, z] of contour) positions.push(x, y, z);
      const indices = [];
      for (const [near, far, inward] of [[0, 4, false], [8, 12, true]])
        for (let index = 0; index < 4; index++) {
          const next = (index + 1) % 4;
          if (inward) indices.push(near + index, far + next, near + next,
            near + index, far + index, far + next);
          else indices.push(near + index, near + next, far + next,
            near + index, far + next, far + index);
        }
      for (let index = 0; index < 4; index++) {
        const next = (index + 1) % 4;
        indices.push(index, 8 + index, 8 + next, index, 8 + next, next);
        indices.push(4 + index, 4 + next, 12 + next, 4 + index, 12 + next, 12 + index);
      }
      return encodeNestingGeometry({ kind: 1, positions, indices });
    };
    const context = { sceneProxy: {
      resources: { async get(url) {
        const bytes = resources.get(url);
        return new Response(bytes ?? null, { status: bytes ? 200 : 404 });
      } },
      invoke(method, payload) {
        if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
          previewRequests.push(payload);
          if (previewRequests.length === 1) return Promise.resolve(planFor(payload));
          return new Promise((resolve) => pendingPlans.push(() => resolve(planFor(payload))));
        }
        if (method === "TubeDesigner.PreviewPunchWizard") {
          const number = resources.size / 2 + 1;
          const bytes = straightTube(payload);
          const baseUrl = `memory://base-${number}`, resultUrl = `memory://result-${number}`;
          resources.set(baseUrl, bytes);
          resources.set(resultUrl, bytes);
          return Promise.resolve({ previewComputed: true,
            baseGeometry: { url: baseUrl, version: 1 }, geometry: { url: resultUrl, version: 1 } });
        }
        throw new Error(`unexpected native call: ${method}`);
      },
    } };
    view.viewport = { setVisibleEntityIds() {}, setContinuousRendering() {} };
    const viewportFactory = () => {
      const root = document.createElement("div");
      root.className = "fake-three-viewport";
      const canvas = document.createElement("canvas");
      root.append(canvas);
      let camera = { revision: 1 };
      const visibleBounds = () => {
        const latest = snapshots.at(-1);
        if (!latest) return null;
        const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
        latest.snapshot.rows.forEach((row, rowIndex) => {
          if (row.data.visible === false) return;
          const matrix = row.data.localToWorldMatrix ?? identity;
          const positions = latest.meshes[rowIndex]?.positions ?? [];
          for (let index = 0; index < positions.length; index += 3) {
            const [x, y, z] = positions.slice(index, index + 3);
            const point = [matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3],
              matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7],
              matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11]];
            point.forEach((value, axis) => { min[axis] = Math.min(min[axis], value); max[axis] = Math.max(max[axis], value); });
          }
        });
        return min.every(Number.isFinite) ? { min, max } : null;
      };
      const viewport = {
        root,
        mount(host) { host.replaceChildren(root); return viewport; },
        async applyViewSnapshot(snapshot, resourceClient) {
          const meshes = [];
          for (const row of snapshot.rows) {
            const response = await resourceClient.get(row.data.geometry.url);
            if (!response.ok) throw new Error(`geometry resource unavailable: ${row.data.geometry.url}`);
            meshes.push(parseRenderGeometryResource(await response.arrayBuffer()));
          }
          snapshots.push({ snapshot, meshes });
          return { applied: true, entityIds: snapshot.rows.map((row) => row.entityId), missingGeometryEntityIds: [] };
        },
        setVisibleEntityIds() {}, setStandardView() { return true; }, fitViewToViewport() { return true; },
        getVisibleBounds: visibleBounds,
        setDimensionAnnotations(annotations) {
          annotationCalls.push((annotations ?? []).map((annotation) => ({
            ...annotation,
            start: [...annotation.start], end: [...annotation.end], offset: [...annotation.offset],
          })));
        },
        getCameraState() { return camera; }, setCameraState(value) { camera = value; }, dispose() {},
      };
      viewports.push(viewport);
      return viewport;
    };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane(context, view);
      const right = library.renderAssemblyLibraryRightPane(context, view);
      const overlay = library.renderAssemblyLibraryViewportOverlay(context, view);
      let mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        mount = document.querySelector("main");
        patch.rememberLibraryDom(view, mount, "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: "" })) {
        throw new Error("一体折弯预览刷新未走局部更新");
      }
      library.attachAssemblyLibraryViewports(context, view, mount, { viewportFactory });
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    const waitFor = async (condition) => {
      for (let index = 0; index < 100; index++) {
        if (condition()) return;
        await new Promise((resolve) => setTimeout(resolve, 0));
      }
      throw new Error(`装配视图未及时更新：${JSON.stringify({ requests: previewRequests.length,
        recipe: state.preview?.plan?.formedPreviewRecipe, formed: !!state.preview?.formedPreview,
        annotations: annotationCalls.at(-1)?.map((item) => item.id), snapshots: snapshots.length })}`);
    };
    const meshBounds = (mesh) => {
      const bounds = { xMin: Infinity, xMax: -Infinity, zMin: Infinity, zMax: -Infinity };
      for (let index = 0; index < mesh.positions.length; index += 3) {
        bounds.xMin = Math.min(bounds.xMin, mesh.positions[index]);
        bounds.xMax = Math.max(bounds.xMax, mesh.positions[index]);
        bounds.zMin = Math.min(bounds.zMin, mesh.positions[index + 2]);
        bounds.zMax = Math.max(bounds.zMax, mesh.positions[index + 2]);
      }
      return bounds;
    };
    const sameMesh = (a, b) => !!a && !!b
      && a.positions.length === b.positions.length && a.indices.length === b.indices.length
      && a.positions.every((value, index) => value === b.positions[index])
      && a.indices.every((value, index) => value === b.indices[index]);
    const blankMesh = async (preview) => {
      const url = preview.manufacturingParts[0].response.geometry.url;
      return parseRenderGeometryResource(await new Response(resources.get(url)).arrayBuffer());
    };
    render();
    await view.tubeDesignerAssemblyLibrary.previewRequest.promise;
    const mount = document.querySelector("main");
    const state = view.tubeDesignerAssemblyLibrary;
    const formedPreview = state.preview?.formedPreview;
    await waitFor(() => annotationCalls.at(-1)?.some((annotation) => annotation.id === "finished-angle")
      && snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url === state.preview?.finishedShape?.rows[0]?.data.geometry.url);
    const formedSnapshot = snapshots.at(-1);
    const initialAnnotations = annotationCalls.at(-1);
    const initialBounds = viewports.at(-1).getVisibleBounds();
    const initialBlankMesh = await blankMesh(state.preview);
    const initialWorkflowAbsent = !mount.querySelector(".tube-assembly-workflow-review");
    const initialControls = [...mount.querySelectorAll("[data-tube-assembly-view]")].map((button) => button.textContent.trim());
    const obsoleteControlsAbsent = !mount.querySelector('[data-tube-assembly-parameter="bendPlane"]')
      && !mount.querySelector('[data-tube-assembly-parameter="planeRotation"]');
    const canvas = mount.querySelector(".cam-viewport > canvas");
    const sceneCanvas = mount.querySelector("[data-tube-assembly-scene-viewport] canvas");
    const sceneRoot = mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild;
    const cube = mount.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]");
    const radiusInput = mount.querySelector('[data-tube-assembly-parameter="bendRadius"]');
    const factorInput = mount.querySelector('[data-tube-assembly-parameter="bendFactor"]');
    const search = mount.querySelector(".tube-connection-library-search input");
    const left = mount.querySelector(".tube-connection-library-list");
    const right = mount.querySelector(".tube-connection-library-editor-body");
    let canvasClicks = 0;
    sceneCanvas.addEventListener("click", () => { canvasClicks += 1; });
    search.value = "L";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-search", search, { renderProject: render });
    search.focus({ preventScroll: true });
    search.setSelectionRange(1, 1);
    left.scrollTop = 90;
    right.scrollTop = 110;
    radiusInput.value = "60";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", radiusInput, { renderProject: render });
    await waitFor(() => pendingPlans.length > 0);
    // The user keeps editing and scrolling while the native plan is in flight.
    search.focus({ preventScroll: true });
    search.setSelectionRange(1, 1);
    left.scrollTop = 120;
    right.scrollTop = 130;
    const latestScroll = { left: left.scrollTop, right: right.scrollTop };
    const updatedRequest = state.previewRequest;
    pendingPlans.splice(0).forEach((complete) => complete());
    await updatedRequest.promise;
    await waitFor(() => state.preview?.plan?.parameters?.bendRadius === 60
      && annotationCalls.at(-1)?.some((annotation) => annotation.id === "finished-angle")
      && snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url === state.preview?.finishedShape?.rows[0]?.data.geometry.url);
    const radiusAnnotations = annotationCalls.at(-1);
    const radiusBounds = viewports.at(-1).getVisibleBounds();
    const radiusBlankMesh = await blankMesh(state.preview);
    const afterRefresh = {
      formedAvailable: !!state.preview?.formedPreview,
      focus: document.activeElement === search && search.value === "L"
        && search.selectionStart === 1 && search.selectionEnd === 1,
      leftScroll: left.scrollTop, rightScroll: right.scrollTop,
      expectedScroll: latestScroll,
      radiusNodeStable: radiusInput === mount.querySelector('[data-tube-assembly-parameter="bendRadius"]'),
      factorNodeStable: factorInput === mount.querySelector('[data-tube-assembly-parameter="bendFactor"]'),
      canvasStable: canvas === mount.querySelector(".cam-viewport > canvas"),
      sceneStable: sceneCanvas === mount.querySelector("[data-tube-assembly-scene-viewport] canvas"),
      cubeStable: !!cube && cube === mount.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]"),
    };
    const updatedFormed = snapshots.at(-1);
    factorInput.value = "1";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", factorInput, { renderProject: render });
    await waitFor(() => pendingPlans.length > 0);
    // The second async response must also keep the latest interaction state.
    search.focus({ preventScroll: true });
    search.setSelectionRange(0, 1);
    left.scrollTop = 105;
    right.scrollTop = 120;
    const factorScroll = { left: left.scrollTop, right: right.scrollTop };
    const factorRequest = state.previewRequest;
    pendingPlans.splice(0).forEach((complete) => complete());
    await factorRequest.promise;
    await waitFor(() => state.preview?.plan?.parameters?.bendFactor === 1
      && annotationCalls.at(-1)?.some((annotation) => annotation.id === "finished-angle")
      && snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url === state.preview?.finishedShape?.rows[0]?.data.geometry.url);
    const factorAnnotations = annotationCalls.at(-1);
    const factorBounds = viewports.at(-1).getVisibleBounds();
    const factorFormed = snapshots.at(-1);
    const factorBlankMesh = await blankMesh(state.preview);
    const afterFactorRefresh = {
      focus: document.activeElement === search && search.value === "L"
        && search.selectionStart === 0 && search.selectionEnd === 1,
      leftScroll: left.scrollTop, rightScroll: right.scrollTop,
      expectedScroll: factorScroll,
      radiusNodeStable: radiusInput === mount.querySelector('[data-tube-assembly-parameter="bendRadius"]'),
      factorNodeStable: factorInput === mount.querySelector('[data-tube-assembly-parameter="bendFactor"]'),
      canvasStable: canvas === mount.querySelector(".cam-viewport > canvas"),
      sceneStable: sceneCanvas === mount.querySelector("[data-tube-assembly-scene-viewport] canvas"),
    };
    const manufactured = state.preview.manufacturingParts[0];
    const blankUrl = manufactured.response.geometry.url;
    const blankButton = mount.querySelector('[data-tube-assembly-view="exploded"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view", blankButton, { renderProject: render });
    await waitFor(() => snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url === blankUrl);
    await waitFor(() => annotationCalls.at(-1)?.some((annotation) => annotation.id === "total-blank"));
    const blankAnnotations = annotationCalls.at(-1);
    const blankWorkflowAbsent = !mount.querySelector(".tube-assembly-workflow-review");
    const blankSnapshot = snapshots.at(-1);
    const blankDescription = mount.querySelector(".tube-assembly-preview-pane.scene").getAttribute("aria-label");
    const blankButtonPressed = mount.querySelector('[data-tube-assembly-view="exploded"]').getAttribute("aria-pressed");
    const formedUrl = factorFormed.snapshot.rows[0].data.geometry.url;
    const formedButton = mount.querySelector('[data-tube-assembly-view="finished"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-set-view", formedButton, { renderProject: render });
    await waitFor(() => snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url === formedUrl);
    await waitFor(() => annotationCalls.at(-1)?.some((annotation) => annotation.id === "finished-angle"));
    const finalAnnotations = annotationCalls.at(-1);
    const finalBounds = viewports.at(-1).getVisibleBounds();
    const finalWorkflowAbsent = !mount.querySelector(".tube-assembly-workflow-review");
    const finalDescription = mount.querySelector(".tube-assembly-preview-pane.scene").getAttribute("aria-label");
    const finalFormedUrl = snapshots.at(-1).snapshot.rows[0].data.geometry.url;
    const finalButtonPressed = mount.querySelector('[data-tube-assembly-view="finished"]').getAttribute("aria-pressed");
    sceneCanvas.dispatchEvent(new MouseEvent("click"));
    const angleInput = mount.querySelector('[data-tube-assembly-parameter="angle"]');
    angleInput.value = "100";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", angleInput, { renderProject: render });
    await waitFor(() => pendingPlans.length > 0);
    search.focus({ preventScroll: true });
    search.setSelectionRange(0, 1);
    left.scrollTop = 125;
    right.scrollTop = 135;
    const angleScroll = { left: left.scrollTop, right: right.scrollTop };
    const angleRequest = state.previewRequest;
    pendingPlans.splice(0).forEach((complete) => complete());
    await angleRequest.promise;
    await waitFor(() => state.preview?.plan?.parameters?.angle === 100
      && annotationCalls.at(-1)?.find((annotation) => annotation.id === "finished-angle")?.label.includes("100°")
      && snapshots.at(-1)?.snapshot.rows[0]?.data.geometry.url === state.preview?.finishedShape?.rows[0]?.data.geometry.url);
    const angleFormed = snapshots.at(-1);
    const angleBounds = viewports.at(-1).getVisibleBounds();
    sceneCanvas.dispatchEvent(new MouseEvent("click"));
    const afterAngleRefresh = {
      obsoleteControlsAbsent: !mount.querySelector('[data-tube-assembly-parameter="bendPlane"]')
        && !mount.querySelector('[data-tube-assembly-parameter="planeRotation"]'),
      focus: document.activeElement === search && search.value === "L"
        && search.selectionStart === 0 && search.selectionEnd === 1,
      leftScroll: left.scrollTop, rightScroll: right.scrollTop, expectedScroll: angleScroll,
      radiusNodeStable: radiusInput === mount.querySelector('[data-tube-assembly-parameter="bendRadius"]'),
      factorNodeStable: factorInput === mount.querySelector('[data-tube-assembly-parameter="bendFactor"]'),
      angleNodeStable: angleInput === mount.querySelector('[data-tube-assembly-parameter="angle"]'),
      sceneStable: sceneCanvas === mount.querySelector("[data-tube-assembly-scene-viewport] canvas"),
      canvasListener: canvasClicks === 2,
    };
    return {
      initialControls,
      obsoleteControlsAbsent,
      obsoletePreviewParametersAbsent: previewRequests.every(({ parameters, sceneParameters }) =>
        ![parameters, sceneParameters].some((values) => values
          && (Object.hasOwn(values, "bendPlane") || Object.hasOwn(values, "planeRotation")))),
      autoBuilt: !!formedPreview && formedPreview.rows.length === 1 && formedPreview.mesh?.metadata?.bendMethod === "cold",
      plan: state.preview.plan.parameters,
      previewCalls: previewRequests.length,
      annotations: {
        initial: initialAnnotations, radius: radiusAnnotations, factor: factorAnnotations,
        blank: blankAnnotations, final: finalAnnotations, angle: annotationCalls.at(-1),
      },
      finishedBounds: { initial: initialBounds, radius: radiusBounds, factor: factorBounds,
        final: finalBounds, angle: angleBounds },
      workflowAbsent: { initial: initialWorkflowAbsent, blank: blankWorkflowAbsent, final: finalWorkflowAbsent },
      formedUrl: formedSnapshot?.snapshot.rows[0]?.data.geometry.url,
      formedRowCount: formedSnapshot?.snapshot.rows.length,
      formedKind: formedSnapshot?.meshes[0]?.kind,
      formedRowId: formedSnapshot?.snapshot.rows[0]?.entityId,
      formedBounds: meshBounds(formedSnapshot.meshes[0]),
      updatedFormedKind: updatedFormed?.meshes[0]?.kind,
      updatedFormedUrl: updatedFormed?.snapshot.rows[0]?.data.geometry.url,
      factorFormedUrl: factorFormed?.snapshot.rows[0]?.data.geometry.url,
      processChangeKeepsFinishedMesh: sameMesh(formedSnapshot.meshes[0], updatedFormed.meshes[0])
        && sameMesh(updatedFormed.meshes[0], factorFormed.meshes[0]),
      angleChangesFinishedMesh: !sameMesh(factorFormed.meshes[0], angleFormed.meshes[0]),
      processChangesBlankMesh: !sameMesh(initialBlankMesh, radiusBlankMesh)
        && !sameMesh(radiusBlankMesh, factorBlankMesh),
      blankUrl, blankRowUrl: blankSnapshot.snapshot.rows[0].data.geometry.url,
      blankRowCount: blankSnapshot.snapshot.rows.length,
      blankKind: blankSnapshot.meshes[0].kind,
      blankBounds: meshBounds(blankSnapshot.meshes[0]),
      blankRequest: manufactured.request,
      blankDescription, blankButtonPressed,
      finalDescription, finalFormedUrl, finalButtonPressed,
      angleFormedUrl: angleFormed?.snapshot.rows[0]?.data.geometry.url,
      preserved: {
        canvas: canvas === mount.querySelector(".cam-viewport > canvas"),
        sceneCanvas: sceneCanvas === mount.querySelector("[data-tube-assembly-scene-viewport] canvas"),
        sceneRoot: sceneRoot === mount.querySelector("[data-tube-assembly-scene-viewport]").firstElementChild,
        cube: !!cube && cube === mount.querySelector("[data-tube-assembly-scene-viewport] [data-cam-viewcube]"),
        radiusInput: radiusInput === mount.querySelector('[data-tube-assembly-parameter="bendRadius"]'),
        factorInput: factorInput === mount.querySelector('[data-tube-assembly-parameter="bendFactor"]'),
        search: search === mount.querySelector(".tube-connection-library-search input"),
        focus: document.activeElement === search && search.value === "L"
          && search.selectionStart === 0 && search.selectionEnd === 1,
        leftScroll: left.scrollTop, rightScroll: right.scrollTop,
        expectedScroll: angleScroll,
        canvasListener: canvasClicks === 2,
      },
      afterRefresh,
      afterFactorRefresh,
      afterAngleRefresh,
      viewportCount: viewports.length,
    };
  }, { templates, bend });

  assert.deepEqual(result.initialControls, ["成品示意", "下料件"]);
  assert.equal(result.obsoleteControlsAbsent, true, "旧草稿不得恢复已删除的折弯空间与平面转角控件");
  assert.equal(result.obsoletePreviewParametersAbsent, true, "预览请求不得夹带旧草稿中的折弯空间与平面转角");
  assert.equal(result.autoBuilt, true, "冷折有效计划必须自动生成单根折弯后示意");
  assert.equal(result.previewCalls, 4);
  assert.equal(result.plan.bendRadius, 60);
  assert.equal(result.plan.bendFactor, 1);
  assert.equal(result.plan.angle, 100);
  assert.equal(result.formedRowCount, 1);
  assert.equal(result.formedRowId, "assembly-finished-shape", "成品视图应使用工艺无关的成品网格");
  assert.equal(result.formedKind, "mesh");
  assert.equal(result.updatedFormedKind, "mesh");
  const annotated = (mode, id) => result.annotations[mode].find((item) => item.id === id);
  for (const mode of ["initial", "radius", "factor", "final", "angle"]) {
    const dimensions = result.annotations[mode];
    const bounds = result.finishedBounds[mode];
    const spans = bounds.min.map((min, axis) => bounds.max[axis] - min);
    const axes = [0, 1, 2].filter((axis) => spans[axis] >= Math.max(...spans) * 0.25);
    assert.equal(dimensions.length, axes.length + 1, `${mode} 成品图只应保留完整外廓尺寸和成品角度`);
    assert.deepEqual(dimensions.filter((item) => item.id.startsWith("finished-envelope:"))
      .map((item) => item.id), axes.map((axis) => `finished-envelope:${"xyz"[axis]}`));
    for (const axis of axes) {
      const item = annotated(mode, `finished-envelope:${"xyz"[axis]}`);
      assert.ok(Math.abs(Math.hypot(...item.start.map((value, index) => item.end[index] - value))
        - spans[axis]) < 1e-5);
      assert.match(item.label, /成品.*mm/);
    }
    assert.equal(dimensions.filter((item) => item.id === "finished-angle").length, 1);
    assert.equal(dimensions.some((item) => item.id.startsWith("design-length:")
      || /逻辑段设定长/.test(item.label)), false);
    assert.equal(dimensions.some((item) => /R\+Kt|折弯因子|K=|展开|下料|弯区|壁厚|槽距|缝距/.test(item.label)), false,
      `${mode} 成品图不得混入加工参数`);
  }
  assert.deepEqual(result.workflowAbsent, { initial: true, blank: true, final: true },
    "成品与下料视图都不应重复显示工艺明细卡");
  assert.equal(result.annotations.blank.length, 5, "下料视图应显示两段直段、弯区、K 补偿与总下料长度");
  assert.deepEqual(result.annotations.initial, result.annotations.radius,
    "只改折弯半径时成品尺寸标注应保持一致");
  assert.deepEqual(result.annotations.radius, result.annotations.factor,
    "只改 K 值时成品尺寸标注应保持一致");
  assert.match(annotated("factor", "finished-angle").label, /90°/);
  assert.match(annotated("angle", "finished-angle").label, /100°/);
  assert.match(annotated("blank", "k-contribution").label, /K=1，补偿.*3\.14 mm/);
  assert.match(annotated("blank", "developed-arc").label, /97\.39 mm/);
  assert.match(annotated("blank", "total-blank").label, /617\.39 mm/);
  assert.deepEqual(result.annotations.final, result.annotations.factor,
    "从下料切回成品后不能残留展开长、K 或弯区标注");
  for (const annotation of [...result.annotations.factor, ...result.annotations.blank]) {
    assert.ok([annotation.start, annotation.end, annotation.offset].every((point) =>
      point.length === 3 && point.every(Number.isFinite)), `标注 ${annotation.id} 的三维定位必须有效`);
  }
  assert.equal(result.blankRowCount, 1);
  assert.equal(result.blankRowUrl, result.blankUrl, "下料视图必须显示加工预览返回的制造几何");
  assert.equal(result.blankKind, "mesh");
  assert.deepEqual(result.blankRequest.features, [], "冷折下料不应带槽口加工");
  assert.ok(result.blankRequest.length > 520, "冷折下料长度应含弧段展开长度");
  assert.ok(Math.abs(result.blankBounds.xMin) < 1e-6
    && Math.abs(result.blankBounds.xMax - result.blankRequest.length) < 1e-3
    && Math.abs(result.blankBounds.zMin + 20) < 1e-6
    && Math.abs(result.blankBounds.zMax - 20) < 1e-6,
  "下料视图应加载沿单轴延伸的直管制造几何");
  assert.ok(result.formedBounds.zMax > result.blankBounds.zMax + 100,
    "折弯后网格必须抬起形成弧形，直管下料仍沿一条轴线");
  assert.equal(result.blankButtonPressed, "true");
  assert.match(result.blankDescription, /按模板加工的下料件（成形前）/);
  assert.equal(result.processChangeKeepsFinishedMesh, true,
    "R 和 K 只改变工艺时，成品网格顶点和索引必须完全相同");
  assert.equal(result.processChangesBlankMesh, true,
    "R 和 K 应改变真实下料几何");
  assert.equal(result.finalFormedUrl, result.factorFormedUrl);
  assert.equal(result.angleChangesFinishedMesh, true, "产品角度更新后应改变成品网格");
  assert.equal(result.finalButtonPressed, "true");
  assert.match(result.finalDescription, /L 形成品外形示意/);
  assert.equal(result.viewportCount, 1, "切换视图应复用同一个三维视口");
  assert.equal(result.afterRefresh.formedAvailable, true);
  assert.equal(result.afterRefresh.focus, true, "异步预览后必须保留输入焦点与选区");
  assert.deepEqual([result.afterRefresh.leftScroll, result.afterRefresh.rightScroll],
    [result.afterRefresh.expectedScroll.left, result.afterRefresh.expectedScroll.right],
    "异步预览后必须保留两侧最新滚动位置");
  assert.ok(result.afterRefresh.leftScroll > 0 && result.afterRefresh.rightScroll > 0);
  for (const key of ["radiusNodeStable", "factorNodeStable", "canvasStable", "sceneStable", "cubeStable"])
    assert.equal(result.afterRefresh[key], true, `异步预览不得替换 ${key}`);
  assert.equal(result.afterFactorRefresh.focus, true, "K 值异步预览后必须保留最新焦点与选区");
  assert.deepEqual([result.afterFactorRefresh.leftScroll, result.afterFactorRefresh.rightScroll],
    [result.afterFactorRefresh.expectedScroll.left, result.afterFactorRefresh.expectedScroll.right],
    "K 值异步预览后必须保留两侧最新滚动位置");
  for (const key of ["radiusNodeStable", "factorNodeStable", "canvasStable", "sceneStable"])
    assert.equal(result.afterFactorRefresh[key], true, `K 值异步预览不得替换 ${key}`);
  for (const key of ["obsoleteControlsAbsent", "focus", "radiusNodeStable", "factorNodeStable",
    "angleNodeStable", "sceneStable", "canvasListener"])
    assert.equal(result.afterAngleRefresh[key], true, `折弯角度变化及预览不得破坏 ${key}`);
  assert.deepEqual([result.afterAngleRefresh.leftScroll, result.afterAngleRefresh.rightScroll],
    [result.afterAngleRefresh.expectedScroll.left, result.afterAngleRefresh.expectedScroll.right],
    "折弯角度异步预览后必须保留两侧最新滚动位置");
  assert.ok(result.afterAngleRefresh.leftScroll > 0 && result.afterAngleRefresh.rightScroll > 0);
  for (const key of ["canvas", "sceneCanvas", "sceneRoot", "cube", "radiusInput", "factorInput", "search", "focus", "canvasListener"])
    assert.equal(result.preserved[key], true, `视图切换不得破坏 ${key}`);
  assert.deepEqual([result.preserved.leftScroll, result.preserved.rightScroll],
    [result.preserved.expectedScroll.left, result.preserved.expectedScroll.right],
    "视图切换必须保留左右侧栏滚动位置");
  assert.deepEqual(errors, []);
  console.log("Cold bend formed/blank browser preview and interaction preservation passed.");
} finally {
  await browser.close();
}
