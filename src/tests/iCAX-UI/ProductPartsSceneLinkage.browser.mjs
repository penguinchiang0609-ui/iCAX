// Real native source mappings, production dock and WebGL selection/emphasis.
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from "./browserPackageRuntime.mjs";

const { tubeDesignerCss } = await importBrowserAsset("apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
const output = browserReportDirectory("hover-selection-20261006/browser-source");
const native = JSON.parse(readFileSync(new URL("fixtures/ProductPartsScene.native.json", import.meta.url), "utf8"));
const descriptor = JSON.parse(readBrowserAsset("apps/tube-designer/templates/product/single_face_security_window/template.json"));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1360, height: 940 } });
  const errors = [], results = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://parts-scene-linkage.test/**", serveBrowserAsset);
  await page.goto("http://parts-scene-linkage.test/");
  await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Arial,"Microsoft YaHei",sans-serif;background:#eef3f4}.cam-workbench{display:grid;grid-template-columns:160px 730px 470px;grid-template-rows:690px 230px;width:1360px;height:920px}.cam-context-pane{grid-column:1;grid-row:1;height:690px;overflow:auto}.nested-left{height:420px;overflow:auto}.nested-left>div{height:1600px}.cam-info-pane{grid-column:3;grid-row:1;height:690px;overflow:auto}.cam-info-pane:after{content:"";display:block;height:400px}.cam-viewport{grid-column:2;grid-row:1;width:730px;height:690px}.tube-designer-parameter-scroll{height:620px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:400px}.tube-designer-product-parts-dock{grid-column:1/4;grid-row:2;display:grid;grid-template-rows:40px minmax(0,1fr);height:230px}` });
  for (const fixture of native.cases) {
    const result = await page.evaluate(async ({ fixture, descriptor }) => {
      const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
      const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
      const { bindProductSceneParameterHighlights, handleDesignerViewportPick, handleDesignerViewportHover } = await import("/src/apps/tube-designer/webpage/entry.mjs");
      const { bindProductParameterDiagrams } = await import("/src/apps/tube-designer/webpage/productParameterDiagram.mjs");
      const links = await import("/src/apps/tube-designer/webpage/productPartsScene.mjs");
      const views = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
      const actions = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
      const { catalogText } = await import("/src/apps/tube-designer/webpage/productCatalog.mjs");
      const { patchLibraryDom, rememberLibraryDom } = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
      const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
      const check = (actual, expected, message) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) throw new Error(message + " " + JSON.stringify({ actual, expected })); };
      const tick = () => new Promise(done => queueMicrotask(done));
      const template = { ...descriptor, available: true, descriptorLoaded: true, name: catalogText(descriptor.displayName),
        groups: descriptor.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
        parameters: descriptor.parameters.map(field => ({ ...field, type: { enum: "select", string: "text" }[field.valueType] ?? field.valueType,
          displayName: catalogText(field.displayName), groupKey: field.group,
          options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
      document.body.innerHTML = '<div id="parts-scene-mount"><main class="cam-workbench"><aside class="cam-context-pane"><div class="nested-left"><div><input id="typing-note" value="ongoing note"></div></div><div style="height:900px"></div></aside><div class="cam-viewport"></div><aside class="cam-info-pane"></aside></main></div>';
      const mount = document.querySelector("#parts-scene-mount"), viewportNode = mount.querySelector(".cam-viewport");
      const view = { activeAreaId: "view", scene: { tubeDesigner: { ...structuredClone(fixture), templates: [template], activeProductId: fixture.product.entityId } },
        tubeDesignerParameterPanelProductId: fixture.product.entityId,
        tubeDesignerExpandedParameterGroups: ["section:process", ...template.groups.map(group => `group:${group.key}`)],
        tubeDesignerParameterDisclosureState: { initialized: true } };
      let picks = 0, requests = 0, release, clickAction=Promise.resolve();
      const hoverEvents=[];
      const context = { mount, project: { projectId: "parts-scene-linkage" }, actions: {}, sceneProxy: { invoke(method, payload) {
        if (method !== "TubeDesigner.UpdateProductParameters") throw new Error("selection made a native request: " + method);
        requests++;
        return new Promise(done => { release = () => done({ tubeDesigner: { ...view.scene.tubeDesigner,
          product: { ...view.scene.tubeDesigner.product, parameters: structuredClone(payload.parameters), modelOutdated: false, partsOutdated: true } } }); });
      } } };
      const viewport = createThreeViewport({ continuousRender: false, showGrid: false,
        onPick(userData, hit, event, hits) { picks++; handleDesignerViewportPick(context, view, userData, hit, event, hits, {}); },
        onHover(userData,hit,event,hits,state) {hoverEvents.push({objectId:userData?.objectId??null,actualMeshHit:hit?.object?.isMesh===true,pointerActive:state.pointerActive});handleDesignerViewportHover(context,view,userData,hit,event,hits,state);} });
      view.viewport = viewport; viewport.mount(viewportNode); viewport.setPickingEnabled(true);
      const cache = (url, type, data) => viewport.resourcePromises.set(`${url}@1`, Promise.resolve({ url, version: 1, type, data }));
      cache("native-tube-material", "material", { colorRGBA: 0x8FB8C9FF });
      const rows = fixture.members.map(member => {
        const memberGeometry = member.properties["assemblyFrame.member"], profile = member.profile;
        const geometry = profile.kind === "round"
          ? new THREE.CylinderGeometry(profile.width / 2, profile.width / 2, memberGeometry.axisLength, 24).rotateX(Math.PI / 2).translate(0, 0, memberGeometry.axisLength / 2)
          : new THREE.BoxGeometry(profile.width, profile.depth, memberGeometry.axisLength).translate(0, 0, memberGeometry.axisLength / 2);
        cache(member.entityId, "geometry", { kind: "mesh", positions: [...geometry.attributes.position.array], indices: [...geometry.index.array] });
        const frame = memberGeometry.sectionFrame, origin = frame.originAtStart;
        return { entityId: member.entityId, data: { geometry: { url: member.entityId, version: 1 }, material: { url: "native-tube-material", version: 1 }, geometryKind: 1,
          localToWorldMatrix: [frame.xAxis[0], frame.yAxis[0], frame.zAxis[0], origin[0], frame.xAxis[1], frame.yAxis[1], frame.zAxis[1], origin[1], frame.xAxis[2], frame.yAxis[2], frame.zAxis[2], origin[2], 0, 0, 0, 1] } };
      });
      await viewport.applyViewSnapshot({ rows }, { get() { throw new Error("unexpected resource read"); } });
      viewport.setStandardView("front"); viewport.fitViewToViewport();
      mount.querySelector(".cam-info-pane").innerHTML = views.renderDesignerRightPane(context, view);
      mount.querySelector(".cam-workbench").insertAdjacentHTML("beforeend", views.renderDesignerProductPartsDock(context, view));
      mount.querySelectorAll("details").forEach(node => { node.open = node.dataset.tubeDesignerParameterGroup === "section:process"; });
      const bind = () => { bindProductSceneParameterHighlights(mount, view); bindProductParameterDiagrams(mount); };
      const suffix = () => views.renderDesignerProductPartsDock(context, view);
      rememberLibraryDom(view, mount, suffix()); bind();
      const render = () => {
        const restore = capturePaneInteraction(mount); actions.captureDesignerScrollState(context, view);
        const left = mount.querySelector(".cam-context-pane").innerHTML;
        if (!patchLibraryDom(view, mount, { left, right: views.renderDesignerRightPane(context, view), overlay: "", suffix: suffix() })) throw new Error("refresh remounted product");
        actions.restoreDesignerScrollState(context, view, { deferred: false }); restore(); bind();
      };
      context.actions.refreshActiveSceneState = async () => render();
      const ops = { renderProject: render };
      mount.addEventListener("click", event => {
        const target = event.target.closest('[data-cam-action="tube-designer-select-product-part"]');
        if (target) clickAction=actions.handleDesignerAreaAction(context, view, target.dataset.camAction, target, ops);
      });
      const parts = links.activeProductManufacturingParts(view), rowFor = part => [...mount.querySelectorAll("[data-tube-designer-product-part-row]")].find(row => row.dataset.tubeDesignerProductPartRow === part.entityId);
      const expectedFor = part => fixture.members.filter(member => (part.properties["manufacturing.sourceMembers"] ?? []).some(source => source.itemKey === member.stableKey)).map(member => member.entityId).sort();
      const emphasized = () => [...viewport.getDebugState().emphasizedObjectIds].sort(), selected = () => [...viewport.getDebugState().selectedObjectIds].sort();
      const colorOf = id => { let color; viewport.sceneObjects.get(id)?.traverse(node => { if (color == null && node.material?.color) color = node.material.color.getHex(); }); return color; };
      const originalColors = new Map(fixture.members.map(member => [member.entityId, colorOf(member.entityId)]));
      const table = mount.querySelector(".tube-designer-product-parts-table"), note = mount.querySelector("#typing-note"), canvas = viewportNode.querySelector("canvas");
      const scrollNodes = [mount.querySelector(".cam-context-pane"), mount.querySelector(".nested-left"), mount.querySelector(".cam-info-pane"), mount.querySelector(".tube-designer-parameter-scroll")];
      const setScrolls = values => scrollNodes.forEach((node, i) => { node.scrollTop = values[i]; });
      const scrolls = () => scrollNodes.map(node => node.scrollTop);
      for (const part of parts) {
        const row = rowFor(part), wanted = expectedFor(part);
        links.selectProductSceneMember(mount, view, "");
        if (!wanted.length) throw new Error("native part lacks display source: " + part.stableKey);
        check(links.productPartSceneMemberIds(view, part).sort(), wanted, "native source mapping differs from physical recipe");
        for (const id of wanted) if (!links.productPartsForSceneMember(view, id).includes(part)) throw new Error("native reverse source mapping missing part");
        const before = table.scrollTop;
        row.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
        check(emphasized(), wanted, "part hover must emphasize its source members");
        for (const id of wanted) if (colorOf(id) !== 0x39c9df) throw new Error("actual WebGL hovered source material is not cyan");
        if (table.scrollTop !== before) throw new Error("hover scrolled the parts list");
        row.dispatchEvent(new PointerEvent("pointerout", { bubbles: true, relatedTarget: viewportNode })); await tick();
        check(emphasized(), [], "leaving a row must clear transient emphasis");
        for (const id of wanted) if (colorOf(id) !== originalColors.get(id)) throw new Error("hover exit did not restore original source material");
        row.focus({ preventScroll: true }); await tick(); check(emphasized(), wanted, "keyboard focus must emphasize the row sources");
        row.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true })); check(selected(), wanted, "Enter must select all source display members");
        check(emphasized(), [], "selected keyboard row must not keep its hover emphasis");
        for (const id of wanted) if (colorOf(id) !== 0xffad1f) throw new Error("selected keyboard row must keep orange source material");
        row.blur(); await tick(); check(selected(), wanted, "blur must preserve committed scene selection");
        row.click(); await clickAction; check(selected(), wanted, "dock click must select all source members");
        if (!row.classList.contains("is-active") || row.getAttribute("aria-selected") !== "true") throw new Error("clicked row selection missing");
      }
      if (requests) throw new Error("part focus/hover/click triggered native generation");
      const merged = parts.find(part => expectedFor(part).length > 1), currentPart = merged ?? parts[0], currentRow = rowFor(currentPart);
      const sourceMember = fixture.members.find(member => member.entityId === expectedFor(currentPart)[0]);
      // True canvas ray picking will be exercised by Playwright after this evaluation.
      const center = new THREE.Vector3(...sourceMember.properties["assemblyFrame.member"].start).lerp(new THREE.Vector3(...sourceMember.properties["assemblyFrame.member"].end), 0.25).project(viewport.camera);
      const canvasRect = canvas.getBoundingClientRect();
      const pickPoint = { x: canvasRect.left + (center.x + 1) * canvasRect.width / 2, y: canvasRect.top + (1 - center.y) * canvasRect.height / 2 };
      const parameter = mount.querySelector('[data-product-control-key="horizontalEndConnectionChoice"]');
      links.selectProductSceneMember(mount, view, "");
      parameter.focus({ preventScroll: true }); await tick();
      const purpleIds = emphasized(); if (!purpleIds.length) throw new Error("parameter highlight fixture is empty");
      currentRow.dispatchEvent(new PointerEvent("pointerover", { bubbles: true })); check(emphasized(), expectedFor(currentPart), "row hover must temporarily override parameter highlight");
      currentRow.dispatchEvent(new PointerEvent("pointerout", { bubbles: true, relatedTarget: viewportNode })); await tick();
      check(emphasized(), purpleIds, "leaving row must restore current parameter highlight");
      if (colorOf(purpleIds[0]) !== 0xa855f7) throw new Error("parameter purple material not restored");
      note.focus({ preventScroll: true }); note.setSelectionRange(1, 5, "backward"); setScrolls([70, 80, 50, 120]);
      const beforeScrolls = scrolls(), beforeTableScroll = table.scrollTop;
      handleDesignerViewportPick(context, view, { objectId: sourceMember.entityId }, {}, null, [], ops);
      check(selected(), expectedFor(currentPart), "scene pick must map continuous part to all display members");
      if (document.activeElement !== note || note.selectionStart !== 1 || note.selectionEnd !== 5) throw new Error("scene selection stole text focus or selection");
      check(scrolls(), beforeScrolls, "explicit scene pick scrolled a sidebar");
      if (!currentRow.classList.contains("is-active")) throw new Error("scene pick did not mark matching row selected");
      const rowRect = currentRow.getBoundingClientRect(), tableRect = table.getBoundingClientRect();
      if (rowRect.bottom > tableRect.bottom + 1 || rowRect.top < tableRect.top) throw new Error("explicit scene pick did not reveal its offscreen row locally");
      const unknownHandled = handleDesignerViewportPick(context, view, { objectId: "not-a-product-member" }, {}, null, [], ops);
      if (unknownHandled) throw new Error("unknown hit consumed another area pick path");
      view.pending = true; handleDesignerViewportPick(context, view, null, null, null, [], ops); view.pending = false;
      check(selected(), expectedFor(currentPart), "pending operation allowed scene selection mutation");
      handleDesignerViewportPick(context, view, null, null, null, [], ops); check(selected(), [], "empty canvas pick must clear scene selection");
      if (mount.querySelector('[data-tube-designer-product-part-row][aria-selected="true"]')) throw new Error("empty canvas left an active row");
      const ghost = { ...parts[0], entityId: "missing-source-part", sourceMemberId: "missing-display", properties: { "manufacturing.sourceMembers": [{ itemKey: "absent.display.member", spans: [] }] } };
      const noMemberView = { ...view, scene: { tubeDesigner: { ...view.scene.tubeDesigner, members: [] } } };
      check(links.productPartSceneMemberIds(noMemberView, ghost), [], "missing part source must not fabricate a renderer identity");
      const foreign = { ...view, scene: { tubeDesigner: { ...view.scene.tubeDesigner, manufacturingGroups: [{ productEntityId: "other-product", parts }], parts } } };
      check(links.activeProductManufacturingParts(foreign), [], "foreign top-level parts must not cross product boundary");
      // One displayed span can supply two stock fragments. Use the real closed-frame
      // bottom half-span declarations; only the independent test part IDs are new.
      let splitSourcesChecked = false;
      if (merged) {
        const source = merged.properties["manufacturing.sourceMembers"].find(source => source.spans.length === 2);
        if (!source) throw new Error("continuous native fixture lost actual split closing span");
        const splitParts = source.spans.map((span, index) => ({ ...merged, entityId: "source-fragment-" + index, sourceMemberId: "",
          properties: { ...merged.properties, "manufacturing.sourceMembers": [{ itemKey: source.itemKey, spans: [span] }] } }));
        const splitView = { ...view, scene: { tubeDesigner: { ...view.scene.tubeDesigner, manufacturingGroups: [{ productEntityId: fixture.product.entityId, parts: splitParts }] } } };
        const member = fixture.members.find(member => member.stableKey === source.itemKey);
        check(links.productPartsForSceneMember(splitView, member.entityId).map(part => part.entityId), splitParts.map(part => part.entityId), "one source must resolve all independently allocated fragments");
        links.selectProductSceneMember(mount, splitView, member.entityId);
        check(splitView.tubeDesignerActiveProductPartIds, splitParts.map(part => part.entityId), "reverse selection discarded a source fragment");
        splitSourcesChecked = true;
      }
      const objects = new Map(viewport.sceneObjects), rowNodes = new Map(parts.map(part => [part.entityId, rowFor(part)]));
      parameter.value = "weld";
      const pending = actions.handleDesignerAreaAction(context, view, "tube-designer-product-control-change", parameter, ops);
      for (let attempt = 0; !release && attempt < 100; attempt++) await new Promise(done => setTimeout(done, 5));
      if (!release) throw new Error("async fixture did not begin native parameter update");
      note.focus({ preventScroll: true }); note.value = "latest typing"; note.setSelectionRange(2, 7, "backward"); setScrolls([100, 120, 70, 160]);
      table.scrollTop = beforeTableScroll; const latestScrolls = scrolls(), latestTableScroll = table.scrollTop;
      release(); await pending;
      check(scrolls(), latestScrolls, "async response lost four latest sidebar scrolls");
      if (table.scrollTop !== latestTableScroll || document.activeElement !== note || note.value !== "latest typing" || note.selectionStart !== 2 || note.selectionEnd !== 7) throw new Error("async response lost row scroll, current text focus or selection");
      if (canvas !== viewportNode.querySelector("canvas") || parameter !== mount.querySelector('[data-product-control-key="horizontalEndConnectionChoice"]')) throw new Error("async response replaced canvas or editor");
      for (const [id, row] of rowNodes) if (rowFor({ entityId: id }) !== row) throw new Error("refresh replaced parts row " + id);
      for (const [id, object] of objects) if (viewport.sceneObjects.get(id) !== object) throw new Error("part linkage regenerated displayed member " + id);
      currentRow.dispatchEvent(new PointerEvent("pointerover", { bubbles: true })); check(emphasized(), expectedFor(currentPart), "refreshed rows lost hover listeners");
      currentRow.dispatchEvent(new PointerEvent("pointerout", { bubbles: true, relatedTarget: viewportNode })); await tick();
      links.selectProductManufacturingPart(mount, view, currentPart);
      const savedParts = view.scene.tubeDesigner.manufacturingGroups[0].parts;
      view.scene.tubeDesigner.manufacturingGroups[0].parts = savedParts.filter(part => part.entityId !== currentPart.entityId);
      render(); check(selected(), [], "disappeared selected part kept scene selection");
      view.scene.tubeDesigner.manufacturingGroups[0].parts = savedParts; render();
      links.selectProductManufacturingPart(mount, view, currentPart);
      view.activeAreaId = "nesting"; view.selectedSceneObjectId = "another-area-object"; viewport.setSelectedObjectIds([view.selectedSceneObjectId], view.selectedSceneObjectId);
      view.scene.tubeDesigner.product = { ...view.scene.tubeDesigner.product, entityId: "another-product" };
      links.bindProductPartsScene(mount, view);
      check(selected(), ["another-area-object"], "product identity change cleared another area's selection");
      view.activeAreaId = "view"; view.scene.tubeDesigner.product = structuredClone(fixture.product); bind();
      window.partsSceneLinkage = { viewport, view, mount, currentPart, rowFor, pickPoint, get picks() { return picks; },get requests(){return requests;}, expectedFor, selected, emphasized, colorOf, originalColors, parameter, note,waitForClick:() => clickAction,
        hoverEvents,render,scrolls,setScrolls,table,pointFor(id,fraction=.25){const member=fixture.members.find(member => member.entityId===id),r=canvas.getBoundingClientRect();const center=new THREE.Vector3(...member.properties["assemblyFrame.member"].start).lerp(new THREE.Vector3(...member.properties["assemblyFrame.member"].end),fraction).project(viewport.camera);return {x:r.left+(center.x+1)*r.width/2,y:r.top+(1-center.y)*r.height/2};} };
      return { case: fixture.name, memberCount: fixture.members.length, partCount: parts.length, continuousSourceMembers: merged ? expectedFor(merged).length : 0,
        allNativeRowsMappedAndReverseMapped: true, splitSourceContractChecked: splitSourcesChecked, asyncNodesAndFocusScrollPreserved: true, parameterUpdates: requests, pickPoint };
    }, { fixture, descriptor });
    await page.mouse.click(result.pickPoint.x, result.pickPoint.y);
    const picked = await page.evaluate(() => {
      const { viewport, view, currentPart, rowFor, expectedFor, selected, picks } = window.partsSceneLinkage;
      return { picks, selected: selected(), wanted: expectedFor(currentPart), activeRows: view.tubeDesignerActiveProductPartIds,
        selectedPart: currentPart.entityId, rowSelected: rowFor(currentPart).getAttribute("aria-selected") };
    });
    assert.ok(picked.picks > 0, "must use the real canvas ray-picking callback");
    assert.deepEqual(picked.selected, picked.wanted, "canvas click must select physical source members");
    assert.ok(picked.activeRows.includes(picked.selectedPart));
    assert.equal(picked.rowSelected, "true");
    if (fixture.name === "continuous-frame") {
      await page.evaluate(() => { const { viewport } = window.partsSceneLinkage; viewport.fitViewToViewport(); });
      await page.screenshot({ path: resolve(output, "continuous-frame-list-scene-selection.png"), fullPage: true });
    }
    const aggregation = await page.evaluate(async () => {
      const f=window.partsSceneLinkage, { view, mount, viewport }=f;
      const links=await import("/src/apps/tube-designer/webpage/productPartsScene.mjs");
      const views=await import("/src/apps/tube-designer/webpage/designerViews.mjs");
      const partsArea=await import("/src/apps/tube-designer/webpage/partsArea.mjs");
      const { buildNestingRequest }=await import("/src/apps/tube-designer/webpage/nestingWorkflow.mjs");
      const { patchLibraryDom }=await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
      const { capturePaneInteraction }=await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
      const group=view.scene.tubeDesigner.manufacturingGroups[0], original=group.parts;
      const pair=["main_grid.horizontal.0001","main_grid.horizontal.0002"].map(key => original.find(part => part.properties["manufacturing.sourceMembers"].some(source => source.itemKey===key)));
      if(pair.some(part => !part) || pair[0].length!==pair[1].length || JSON.stringify(pair[0].profile)!==JSON.stringify(pair[1].profile)) throw new Error("native fixture does not contain the required same-section, same-length horizontal pair");
      // Exercise the declared native aggregation snapshot contract using two
      // preserved native source arrays; this is not a geometry equality test.
      const aggregate={...pair[0],quantity:6,unitQuantity:2,instanceQuantity:3,
        profile:{...pair[0].profile,sectionIdentity:JSON.stringify({schema:"icax.nesting-section.v1",contours:['{"edges":["native-aggregate-horizontal-quantity-double"]}']})},properties:{...pair[0].properties,
        "manufacturing.sourceMembers":pair.flatMap(part => structuredClone(part.properties["manufacturing.sourceMembers"])),
        "manufacturing.sourceStockIds":pair.map(part => part.stableKey)}};
      group.parts=original.filter(part => part!==pair[0] && part!==pair[1]).concat(aggregate);
      view.scene.tubeDesigner.product.quantity=3;
      view.tubeDesignerActivePartId="";view.tubeDesignerActiveProductPartIds=[];view.selectedSceneObjectId="";
      viewport.setSelectedObjectIds([],"");
      const restore=capturePaneInteraction(mount);
      const context={mount}, right=mount.querySelector(".cam-info-pane").innerHTML;
      if(!patchLibraryDom(view,mount,{left:mount.querySelector(".cam-context-pane").innerHTML,right,overlay:"",suffix:views.renderDesignerProductPartsDock(context,view)})) throw new Error("aggregate dock patch unexpectedly remounted the scene");
      restore();links.bindProductPartsScene(mount,view);
      const ids=links.productPartSceneMemberIds(view,aggregate).sort();
      if(ids.length!==2) throw new Error("an aggregated horizontal part must map both native assembly members");
      const material=partsArea.buildMaterialProfileGroups([aggregate]), profile=partsArea.buildProfileGroups([aggregate]);
      if(material[0].quantity!==6 || profile[0].quantity!==6 || material[0].totalLength!==aggregate.length*6 || profile[0].totalLength!==aggregate.length*6) throw new Error("aggregate frontend totals double-multiplied the production quantity");
      const linked={...aggregate,linkedNesting:true};
      const nestingView={...view,activeAreaId:"nesting",tubeDesignerNestingSelectedPartIds:[aggregate.entityId],scene:{tubeDesigner:{...view.scene.tubeDesigner,nestingGroups:[{...group,parts:[linked]}]}}};
      const request=buildNestingRequest(nestingView);
      if(request.parts.length!==1 || request.parts[0].quantity!==6 || request.parts[0].instanceQuantity!==3) throw new Error("aggregate nesting demand lost or multiplied the native snapshot quantity");
      for(const id of ids) if(links.productPartsForSceneMember(view,id).filter(part => part.entityId===aggregate.entityId).length!==1) throw new Error("aggregate reverse source mapping is incomplete");
      const member=view.scene.tubeDesigner.members.find(member => member.entityId===ids[1]);
      const center=new (await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js")).Vector3(...member.properties["assemblyFrame.member"].start);
      center.lerp(new (await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js")).Vector3(...member.properties["assemblyFrame.member"].end),.25).project(viewport.camera);
      const r=mount.querySelector("canvas").getBoundingClientRect();
      const dockRows=[...mount.querySelectorAll("[data-tube-designer-product-part-row]")], aggregateRow=f.rowFor(aggregate), aggregateIndex=dockRows.indexOf(aggregateRow);
      const peer=group.parts.filter(part => part.entityId!==aggregate.entityId && f.expectedFor(part).every(id => !ids.includes(id)))
        .sort((a,b) => Math.abs(dockRows.indexOf(f.rowFor(a))-aggregateIndex)-Math.abs(dockRows.indexOf(f.rowFor(b))-aggregateIndex))[0];
      if(!peer) throw new Error("aggregate fixture needs an independently hoverable row");
      f.aggregate=aggregate;f.aggregateIds=ids;f.aggregateRow=f.rowFor(aggregate);f.peer=peer;f.peerIds=f.expectedFor(peer);f.peerRow=f.rowFor(peer);
      return {partId:aggregate.entityId,sourceMemberIds:ids,unitQuantity:aggregate.unitQuantity,instanceQuantity:3,quantity:6,nestingQuantity:request.parts[0].quantity,totalLength:profile[0].totalLength,
        peerPartId:peer.entityId,peerSourceMemberIds:f.peerIds,
        secondSourcePoint:{x:r.left+(center.x+1)*r.width/2,y:r.top+(1-center.y)*r.height/2}};
    });
    const aggregateRow=page.locator(`[data-tube-designer-product-part-row="${aggregation.partId}"]`);
    await aggregateRow.hover();
    const aggregateHover=await page.evaluate(() => {
      const f=window.partsSceneLinkage;
      const colors=f.aggregateIds.map(id => {let color;f.viewport.sceneObjects.get(id).traverse(node => {if(color==null && node.material?.color)color=node.material.color.getHex();});return color;});
      return {emphasized:[...f.viewport.getDebugState().emphasizedObjectIds].sort(),colors,quantity:f.aggregateRow.children[3].textContent};
    });
    assert.deepEqual(aggregateHover.emphasized,aggregation.sourceMemberIds);
    assert.deepEqual(aggregateHover.colors,[0x39c9df,0x39c9df],"Both real WebGL source members receive the aggregated row hover color");
    assert.equal(aggregateHover.quantity,"6");
    await aggregateRow.click();
    assert.deepEqual(await page.evaluate(() => window.partsSceneLinkage.selected()),aggregation.sourceMemberIds,"An aggregate row selects both source members");
    const aggregateSelectedHover=await page.evaluate(() => {
      const f=window.partsSceneLinkage, style=getComputedStyle(f.aggregateRow.children[0]);
      return {colors:f.aggregateIds.map(f.colorOf),emphasized:f.emphasized(),active:f.aggregateRow.classList.contains("is-active"),preview:f.aggregateRow.classList.contains("is-preview"),
        background:style.backgroundColor,text:style.color,accent:style.boxShadow};
    });
    assert.deepEqual(aggregateSelectedHover.colors,[0xffad1f,0xffad1f],"The selected hovered aggregate keeps orange on both real WebGL sources");
    assert.deepEqual(aggregateSelectedHover.emphasized,[],"Hover must not mask selected source material with transient emphasis");
    assert.equal(aggregateSelectedHover.active,true);
    assert.equal(aggregateSelectedHover.preview,true,"Test actually exercises simultaneous selected and hovered row state");
    assert.equal(aggregateSelectedHover.background,"rgb(255, 240, 214)","The selected orange row style wins even while hovered");
    const peerRow=page.locator(`[data-tube-designer-product-part-row="${aggregation.peerPartId}"]`);
    await peerRow.hover();
    const simultaneous=await page.evaluate(() => {
      const f=window.partsSceneLinkage, selectedStyle=getComputedStyle(f.aggregateRow.children[0]), hoverStyle=getComputedStyle(f.peerRow.children[0]);
      return {selected:f.selected(),emphasized:f.emphasized(),selectedColors:f.aggregateIds.map(f.colorOf),hoverColors:f.peerIds.map(f.colorOf),
        selectedBackground:selectedStyle.backgroundColor,hoverBackground:hoverStyle.backgroundColor,
        selectedAccent:selectedStyle.boxShadow,hoverAccent:hoverStyle.boxShadow,
        selectedRow:f.aggregateRow.getAttribute("aria-selected"),hoverRow:f.peerRow.getAttribute("aria-selected")};
    });
    assert.deepEqual(simultaneous.selected,aggregation.sourceMemberIds,"Hovering another row cannot change committed selection");
    assert.deepEqual(simultaneous.emphasized,aggregation.peerSourceMemberIds);
    assert.deepEqual(simultaneous.selectedColors,[0xffad1f,0xffad1f]);
    assert.deepEqual(simultaneous.hoverColors,aggregation.peerSourceMemberIds.map(() => 0x39c9df));
    assert.equal(simultaneous.selectedBackground,"rgb(255, 240, 214)");
    assert.equal(simultaneous.hoverBackground,"rgb(227, 246, 252)");
    assert.notEqual(simultaneous.selectedAccent,simultaneous.hoverAccent);
    assert.equal(simultaneous.selectedRow,"true");
    assert.equal(simultaneous.hoverRow,"false");
    await page.screenshot({ path:resolve(output,`${fixture.name}-selected-orange-hover-cyan.png`),fullPage:true });
    await page.mouse.move(5,5);
    const leftPeer=await page.evaluate(() => {
      const f=window.partsSceneLinkage;
      return {selected:f.selected(),emphasized:f.emphasized(),selectedColors:f.aggregateIds.map(f.colorOf),hoverColors:f.peerIds.map(f.colorOf),originalColors:f.peerIds.map(id => f.originalColors.get(id))};
    });
    assert.deepEqual(leftPeer.selected,aggregation.sourceMemberIds);
    assert.deepEqual(leftPeer.emphasized,[]);
    assert.deepEqual(leftPeer.selectedColors,[0xffad1f,0xffad1f]);
    assert.deepEqual(leftPeer.hoverColors,leftPeer.originalColors,"Leaving the second row restores its original materials while keeping the first selected");
    const parameterRestoration=await page.evaluate(async () => {
      const f=window.partsSceneLinkage, tick=() => new Promise(done => queueMicrotask(done));
      f.parameter.focus({preventScroll:true});await tick();
      const purpleIds=f.emphasized(),overlappingIds=f.aggregateIds.filter(id => purpleIds.includes(id));
      if(!overlappingIds.length) throw new Error("parameter fixture must emphasize an orange selected source");
      if(overlappingIds.some(id => f.colorOf(id)!==0xa855f7)) throw new Error("parameter focus lost its existing purple guidance color");
      f.note.focus({preventScroll:true});await tick();
      return {purpleIds,overlappingIds,emphasized:f.emphasized(),selected:f.selected(),colors:f.aggregateIds.map(f.colorOf)};
    });
    assert.deepEqual(parameterRestoration.emphasized,[]);
    assert.deepEqual(parameterRestoration.selected,aggregation.sourceMemberIds);
    assert.deepEqual(parameterRestoration.colors,[0xffad1f,0xffad1f],"Leaving parameter guidance restores the committed orange selection");
    const canvasHoverSetup=await page.evaluate(() => {
      const f=window.partsSceneLinkage,r=f.mount.querySelector("canvas").getBoundingClientRect();
      f.note.focus({preventScroll:true});f.note.value="canvas hover typing";f.note.setSelectionRange(2,9,"backward");f.setScrolls([100,120,70,160]);
      f.canvasHoverBefore={requests:f.requests,picks:f.picks,scrolls:f.scrolls(),tableScroll:f.table.scrollTop,canvas:f.mount.querySelector("canvas"),parameter:f.parameter,note:f.note};
      return {peerPoint:f.pointFor(f.peerIds[0]),selectedPoint:f.pointFor(f.aggregateIds[0]),blank:{x:r.left+12,y:r.top+12}};
    });
    await page.mouse.move(canvasHoverSetup.blank.x,canvasHoverSetup.blank.y);
    await page.waitForFunction(() => window.partsSceneLinkage.viewport.hoverPointerActive===true && window.partsSceneLinkage.view.tubeDesignerHoveredSceneMemberIds.length===0);
    await page.mouse.move(canvasHoverSetup.peerPoint.x,canvasHoverSetup.peerPoint.y);
    await page.waitForFunction(ids => JSON.stringify([...window.partsSceneLinkage.view.tubeDesignerHoveredSceneMemberIds].sort())===JSON.stringify(ids),aggregation.peerSourceMemberIds);
    const canvasPeerHover=await page.evaluate(() => {
      const f=window.partsSceneLinkage,before=f.canvasHoverBefore;
      if(document.activeElement!==before.note || f.note.value!=="canvas hover typing" || f.note.selectionStart!==2 || f.note.selectionEnd!==9 || f.note.selectionDirection!=="backward") throw new Error("Canvas hover stole active editor text/focus/selection");
      if(JSON.stringify(f.scrolls())!==JSON.stringify(before.scrolls) || f.table.scrollTop!==before.tableScroll) throw new Error("Canvas hover scrolled a sidebar or parts list");
      if(f.mount.querySelector("canvas")!==before.canvas || f.parameter!==before.parameter || f.note!==before.note) throw new Error("Canvas hover replaced interaction nodes");
      if(f.requests!==before.requests || f.picks!==before.picks) throw new Error("Canvas hover selected or invoked native geometry");
      return {hit:f.hoverEvents.at(-1),selected:f.selected(),emphasized:f.emphasized(),selectedColors:f.aggregateIds.map(f.colorOf),hoverColors:f.peerIds.map(f.colorOf),hoveredRows:f.view.tubeDesignerHoveredProductPartIds,
        peerPreview:f.peerRow.classList.contains("is-preview"),peerSelected:f.peerRow.getAttribute("aria-selected"),focusScrollNodesAndNativeCallsPreserved:true};
    });
    assert.equal(canvasPeerHover.hit.actualMeshHit,true,"Canvas hover must use production raycaster against a real mesh");
    assert.ok(aggregation.peerSourceMemberIds.includes(canvasPeerHover.hit.objectId));
    assert.deepEqual(canvasPeerHover.selected,aggregation.sourceMemberIds);
    assert.deepEqual(canvasPeerHover.selectedColors,[0xffad1f,0xffad1f]);
    assert.deepEqual(canvasPeerHover.hoverColors,aggregation.peerSourceMemberIds.map(() => 0x39c9df));
    assert.deepEqual(canvasPeerHover.hoveredRows,[aggregation.peerPartId]);
    assert.equal(canvasPeerHover.peerPreview,true);
    assert.equal(canvasPeerHover.peerSelected,"false");
    await page.screenshot({path:resolve(output,`${fixture.name}-canvas-selected-orange-hover-cyan.png`),fullPage:true});
    await page.mouse.move(canvasHoverSetup.selectedPoint.x,canvasHoverSetup.selectedPoint.y);
    await page.waitForFunction(id => window.partsSceneLinkage.view.tubeDesignerHoveredProductPartIds.includes(id),aggregation.partId);
    const canvasSelectedHover=await page.evaluate(() => {
      const f=window.partsSceneLinkage;
      return {selected:f.selected(),emphasized:f.emphasized(),colors:f.aggregateIds.map(f.colorOf),active:f.aggregateRow.classList.contains("is-active"),preview:f.aggregateRow.classList.contains("is-preview")};
    });
    assert.deepEqual(canvasSelectedHover.selected,aggregation.sourceMemberIds);
    assert.deepEqual(canvasSelectedHover.emphasized,[]);
    assert.deepEqual(canvasSelectedHover.colors,[0xffad1f,0xffad1f]);
    assert.equal(canvasSelectedHover.active,true);
    assert.equal(canvasSelectedHover.preview,true,"Actual canvas hover must exercise selected and hovered source simultaneously");
    await page.mouse.move(canvasHoverSetup.blank.x,canvasHoverSetup.blank.y);
    await page.waitForFunction(() => window.partsSceneLinkage.view.tubeDesignerHoveredSceneMemberIds.length===0);
    const canvasBlank=await page.evaluate(() => {
      const f=window.partsSceneLinkage;
      return {selected:f.selected(),emphasized:f.emphasized(),selectedColors:f.aggregateIds.map(f.colorOf),peerColors:f.peerIds.map(f.colorOf),originalPeerColors:f.peerIds.map(id => f.originalColors.get(id)),previewRows:f.mount.querySelectorAll("[data-tube-designer-product-part-row].is-preview").length};
    });
    assert.deepEqual(canvasBlank.selected,aggregation.sourceMemberIds);
    assert.deepEqual(canvasBlank.emphasized,[]);
    assert.deepEqual(canvasBlank.selectedColors,[0xffad1f,0xffad1f]);
    assert.deepEqual(canvasBlank.peerColors,canvasBlank.originalPeerColors);
    assert.equal(canvasBlank.previewRows,0,"Scene blank must clear hover row independently of selection");
    await page.mouse.move(canvasHoverSetup.peerPoint.x,canvasHoverSetup.peerPoint.y);
    await page.waitForFunction(id => window.partsSceneLinkage.viewport.hoveredObjectId===id,aggregation.peerSourceMemberIds[0]);
    await page.mouse.move(5,5);
    await page.waitForFunction(() => window.partsSceneLinkage.viewport.hoverPointerActive===false);
    assert.deepEqual(await page.evaluate(() => window.partsSceneLinkage.emphasized()),[],"Leaving canvas removes scene hover");
    const rawSetup=await page.evaluate(async () => {
      const f=window.partsSceneLinkage,links=await import("/src/apps/tube-designer/webpage/productPartsScene.mjs");
      f.savedManufacturingGroups=f.view.scene.tubeDesigner.manufacturingGroups;
      links.selectProductSceneMember(f.mount,f.view,"");
      f.view.scene.tubeDesigner.manufacturingGroups=[];f.render();
      return {memberId:f.peerIds[0],point:f.pointFor(f.peerIds[0]),partRows:f.mount.querySelectorAll("[data-tube-designer-product-part-row]").length,requests:f.requests,canvasStable:f.mount.querySelector("canvas")===f.canvasHoverBefore.canvas};
    });
    assert.equal(rawSetup.partRows,0,"Raw product fixture must have no manufactured parts dock rows");
    assert.equal(rawSetup.canvasStable,true);
    await page.mouse.move(rawSetup.point.x,rawSetup.point.y);
    await page.waitForFunction(id => window.partsSceneLinkage.view.tubeDesignerHoveredSceneMemberIds.includes(id),rawSetup.memberId);
    const rawHover=await page.evaluate(id => {
      const f=window.partsSceneLinkage;
      return {hit:f.hoverEvents.at(-1),emphasized:f.emphasized(),selected:f.selected(),color:f.colorOf(id),hoveredParts:f.view.tubeDesignerHoveredProductPartIds,requests:f.requests};
    },rawSetup.memberId);
    assert.deepEqual(rawHover.emphasized,[rawSetup.memberId]);
    assert.deepEqual(rawHover.selected,[]);
    assert.deepEqual(rawHover.hoveredParts,[]);
    assert.equal(rawHover.color,0x39c9df,"Undisassembled scene member must support the same cyan hover");
    assert.equal(rawHover.hit.actualMeshHit,true);
    assert.equal(rawHover.requests,rawSetup.requests);
    await page.mouse.move(5,5);
    await page.waitForFunction(() => window.partsSceneLinkage.viewport.hoverPointerActive===false);
    const rawLeave=await page.evaluate(id => {
      const f=window.partsSceneLinkage,result={emphasized:f.emphasized(),color:f.colorOf(id),originalColor:f.originalColors.get(id)};
      f.view.scene.tubeDesigner.manufacturingGroups=f.savedManufacturingGroups;f.render();f.aggregateRow=f.rowFor(f.aggregate);f.peerRow=f.rowFor(f.peer);
      return result;
    },rawSetup.memberId);
    assert.deepEqual(rawLeave.emphasized,[]);
    assert.equal(rawLeave.color,rawLeave.originalColor,"Leaving a raw member restores its original material");
    await page.mouse.click(aggregation.secondSourcePoint.x,aggregation.secondSourcePoint.y);
    assert.deepEqual(await page.evaluate(() => window.partsSceneLinkage.selected()),aggregation.sourceMemberIds,"Ray-picking the second aggregate source selects the entire aggregate");
    results.push({ ...result, realCanvasRayPickPassed: true, aggregationContract:aggregation,selectedHoverPriority:aggregateSelectedHover,simultaneousSelectionAndHover:simultaneous,hoverExitMaterialRestoration:leftPeer,parameterGuidanceRestoration:parameterRestoration,
      realCanvasPeerHover:canvasPeerHover,realCanvasSelectedHover:canvasSelectedHover,realCanvasBlankRestoration:canvasBlank,rawMemberHover:rawHover,rawMemberLeave:rawLeave });
    await page.evaluate(() => { window.partsSceneLinkage.viewport.dispose(); delete window.partsSceneLinkage; });
  }
  assert.deepEqual(errors, []);
  writeFileSync(resolve(output, "report.json"), JSON.stringify({ passed: true, productionAssetRoot:browserAssetRoot,
    nativeTransport:"controlled UpdateProductParameters fixture; source mappings from saved native reports",geometry:"real Three.js WebGL with fixture meshes constructed from native member geometry metadata",standaloneCefEndToEnd:false,
    nativeFixtureSources: native.cases.map(fixture => fixture.source), cases: results }, null, 2));
  console.log("PASS product parts / scene bidirectional linkage: real WebGL cyan hover and orange selection priority, simultaneous different-row states, original material and parameter purple/selection restoration, actual native ordinary/continuous source mappings, local row reveal and stable async focus/scroll/nodes; aggregated two-source selection/ray pick and quantity 2 x 3 = 6 through nesting demand.");
} finally { await browser.close(); }
