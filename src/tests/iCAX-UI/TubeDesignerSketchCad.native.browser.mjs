// Production CAD controls -> real scene SDOs/BRep/GPU resources -> .ictd reopen.
// The stdio host and Chrome shell are controlled; installed CEF is outside scope.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { repositoryRoot, startSideSketchNativeBridge } from "./fixtures/sideSketchNativeBridge.mjs";

const sourceRoot = resolve(repositoryRoot, "src");
const artifacts = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repositoryRoot, "output/tests/sketch-cad-native-browser"));
mkdirSync(artifacts, { recursive: true });
const bridge = await startSideSketchNativeBridge(artifacts);
const assets = new Map(), cases = [], errors = [];
let browser, page;
const near = (actual, expected, label, tolerance = 1e-5) => assert.ok(Math.abs(actual - expected) < tolerance, `${label}: ${actual} vs ${expected}`);
try {
  const rectangle = (width, height) => {
    const points = [[-width / 2, -height / 2], [width / 2, -height / 2], [width / 2, height / 2], [-width / 2, height / 2]];
    return { kind: "path", closed: true, segments: points.map((start, index) => ({ kind: "line", start, end: points[(index + 1) % points.length] })) };
  };
  const profile = { schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "fixed-section", profileForm: "fixed",
    name: "阵列真实原生验收方管", width: 80, depth: 60, contours: [rectangle(80, 60), rectangle(76, 56)] };
  const blank = { profile, length: 200, quantity: 1, previewResourceKey: "sketch-array-native-blank" };
  const initialReport = await bridge.invoke("PreviewNestingSideSketchPart", blank);
  assert.equal(initialReport.available, true);
  const period = initialReport.unfolding.surfaces.find(surface => !surface.inner).uPeriod;
  near(period, 280, "Native blank perimeter");
  const blankVolume = (80 * 60 - 76 * 56) * 200;
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome",
    args:['--disable-background-timer-throttling','--disable-renderer-backgrounding','--disable-backgrounding-occluded-windows'] });
  page = await browser.newPage({ viewport: { width: 1680, height: 1040 } });
  page.setDefaultTimeout(30000);
  page.on("pageerror", error => errors.push(error.message));
  await page.exposeFunction("nativeRpc", bridge.rpc);
  await page.route("http://sketch-cad-native.test/**", route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><meta charset='utf-8'><body><div id='app' class='tube-designer-workspace'></div></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    const code = readFileSync(path, "utf8"); assets.set(pathname, createHash("sha256").update(code).digest("hex"));
    return route.fulfill({ contentType: "text/javascript", body: code });
  });
  await page.goto("http://sketch-cad-native.test/");
  await page.addStyleTag({ content: readFileSync(resolve(sourceRoot, "apps/_shared/workbench/styles/laser3dcam.css"), "utf8") });
  await page.evaluate(async () => {
    const sketch = await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
    const parts = await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const preview = await import("/src/apps/tube-designer/webpage/sideSketchPreview.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    const { createThreeViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    document.head.insertAdjacentHTML("beforeend", `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font:14px "Segoe UI",sans-serif;background:#13252d}</style>`);
    const mount = document.querySelector("#app"), view = { activeAreaId: "nesting", scene: { tubeDesigner: { nestingGroups: [] } } };
    const f = window.fixture = { mount, view, sketch, parts, preview, calls: [], resources: [], pending: 0, failures: [] };
    Object.defineProperty(f, "state", { get() { return view.tubeDesignerSketch; } });
    Object.defineProperty(f, "draft", { get() { return f.state.sideByPart[f.state.targetPartId]; } });
    const invoke = async (method, payload) => {
      const call = { method, payload: structuredClone(payload) }; f.calls.push(call);
      try {
        call.result = await window.nativeRpc({ action: "invoke", scope: "scene", method: method.replace(/^TubeDesigner\./, ""), payload });
        return call.result;
      } catch (error) { call.error = error.message; throw error; }
    };
    f.context = { mount, sceneProxy: { invoke, resources: { async get(url, options) {
      const version = Number(options?.headers?.get("ICAX-Resource-Version") || 0);
      const response = await window.nativeRpc({ action: "resource", payload: { url, version } });
      f.resources.push({ url, version, bytes: response.bytes });
      return new Response(Uint8Array.from(atob(response.base64), character => character.charCodeAt(0)));
    } } } };
    f.ops = { createSideSketchViewport(options) { return f.viewport = createThreeViewport(options); }, showNotice() {}, renderProject() {
      if (!sketch.patchSketchDialogDom(f.context, view, mount, f.ops)) {
        const list = parts.listNestingParts(view.scene.tubeDesigner ?? {});
        mount.innerHTML = "<main>" + list.map(part => `<button data-cam-action="tube-designer-part-open-sketch" data-tube-designer-part-id="${part.entityId}">二维编辑 ${part.entityId}</button>`).join("") + "</main>" + sketch.renderSectionSketchDialog(f.context, view);
        sketch.rememberSketchDialogDom(view, mount, f.context.sceneProxy);
      }
      sketch.attachSketchAreaInteractions(f.context, view, mount, f.ops);
    } };
    f.load = ({ payload, report, sources }) => {
      sketch.beginNewPartSideSketch(view, payload, report);
      view.tubeDesignerSketchDialogOpen = true;
      f.draft.entities = structuredClone(sources); f.draft.selectedIds = sources.map(entity => entity.id);
      f.draft.trajectoryWidth = .8; f.state.snapEnabled = false; f.state.orthoEnabled = false;
      f.ops.renderProject();
    };
    const dispatch = (action, target) => {
      f.pending++;
      const operation = action.startsWith("tube-designer-sketch-")
        ? sketch.handleSketchAreaAction(f.context, view, action, target, f.ops)
        : parts.handlePartsAreaAction(f.context, view, action, target, f.ops);
      Promise.resolve(operation).catch(error => f.failures.push(error.message)).finally(() => f.pending--);
    };
    mount.addEventListener("click", event => { const target = event.target.closest("[data-cam-action]"); if (target && !target.disabled) dispatch(target.dataset.camAction, target); });
    mount.addEventListener("change", event => { const target = event.target.closest("[data-cam-change-action]"); if (target) dispatch(target.dataset.camChangeAction, target); });
  });
  const idle = async () => {
    await page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending && !window.fixture.state?.sidePreviewPending, null, { timeout: 180000 });
    assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
  };
  const ready = async () => { await idle(); await page.locator('[data-side-sketch-preview-ready="true"]').waitFor({timeout:180000}); };
  const setField = async (key, value) => { const field = page.locator(`[data-tube-sketch-array-field="${key}"]`); await field.fill(String(value)); await field.dispatchEvent("change"); };
  const command = key => page.locator(`[data-sketch-command="sketch.${key}"]`);
  const modelPoint = async (x, y) => page.locator("[data-tube-sketch-canvas]").evaluate((svg, [x, y]) => {
    const value = key => Number(svg.getAttribute(`data-sketch-${key}`));
    const [xmin,xmax,ymin,ymax,left,right,top,bottom]=['x-min','x-max','y-min','y-max','canvas-left','canvas-right','canvas-top','canvas-bottom'].map(value);
    if (!(xmax>xmin&&ymax>ymin&&right>left&&bottom>top))throw Error('Production sketch canvas must expose its actual drawing coordinates');
    const point = svg.createSVGPoint(); point.x = left+(x-xmin)/(xmax-xmin)*(right-left); point.y = bottom-(y-ymin)/(ymax-ymin)*(bottom-top);
    const client = point.matrixTransform(svg.getScreenCTM()); return [client.x, client.y];
  }, [x, y]);
  const clickModel = async (x, y) => page.mouse.click(...await modelPoint(x, y));
  const moveModel = async (x, y) => { await page.mouse.move(...await modelPoint(x, y)); await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))); };
  const cadField = key => page.locator(`[data-tube-sketch-cad-field="${key}"]`);
  const setCad = async (key, value) => { await cadField(key).fill(String(value)); await cadField(key).dispatchEvent('change'); };
  const applyCad = async () => { await page.locator('[data-cam-action="tube-designer-sketch-cad-apply"]').click(); await idle();
    assert.equal(await page.locator('[data-tube-sketch-cad-error]').innerText().catch(()=>''),''); };
  const setNumeric = async (key, value) => { const field=page.locator(`[data-tube-sketch-numeric-field="${key}"]`); await field.fill(String(value)); await field.dispatchEvent('change'); };
  const numericPoint = async (x,y) => { await setNumeric('x',x); await setNumeric('y',y); await page.locator('[data-cam-action="tube-designer-sketch-numeric-apply"]').click(); await idle(); };
  const loadEmpty = async key => {
    const payload={...blank,previewResourceKey:key},report=await bridge.invoke('PreviewNestingSideSketchPart',payload);
    await page.evaluate(item=>window.fixture.load(item),{payload,report,sources:[]});await ready();
  };
  const loadSources = async (key, sources) => {
    const payload={...blank,previewResourceKey:key},report=await bridge.invoke('PreviewNestingSideSketchPart',payload);
    await page.evaluate(item=>window.fixture.load(item),{payload,report,sources});await ready();
  };
  const drawCircle = async (u,s,r) => { await command('circle').click(); await numericPoint(u,s); await numericPoint(u+r,s); };
  const draft = () => page.evaluate(()=>structuredClone(window.fixture.draft));
  const previewNow = async () => { await page.locator('[data-cam-action="tube-designer-sketch-preview-side"]').click();await ready();
    const call=await page.evaluate(()=>window.fixture.calls.findLast(call=>call.method==='TubeDesigner.PreviewNestingSideSketchPart'));
    assert.ok(call?.result,`Latest real native preview must succeed: ${call?.error??'No native preview response'}`);
    assert.ok(call.result.preview.baseGeometry.url);assert.ok(call.result.preview.bounds);return call; };
  const saveNow = async (expectedMethod, activeTool = false) => {
    if (!activeTool) await command('select').click();await command('commit').click();await idle();
    assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerSketchDialogOpen),false);
    const saved=await page.evaluate(method=>window.fixture.calls.findLast(call=>call.method===method),expectedMethod);
    assert.ok(saved?.result?.partEntityId);return saved;
  };
  const geometry = id => bridge.rpc({action:'geometry',payload:{partEntityId:id}});
  const sceneParts = scene => scene.tubeDesigner.nestingGroups.flatMap(group=>group.parts);
  const reopenPart = async id => {
    await page.locator(`[data-cam-action="tube-designer-part-open-sketch"][data-tube-designer-part-id="${id}"]`).click();await ready();
  };
  const savedCurves = [];
  const saveCurves = async (id, check) => {
    const current=await draft(),preview=await previewNow();
    assert.deepEqual(preview.payload.sketch.entities,current.entities);
    const saved=await saveNow('TubeDesigner.AddNestingSideSketchPart'),solid=await geometry(saved.result.partEntityId);
    assert.equal(solid.valid,true,`${id} creates a valid native BRep`);
    assert.ok(solid.volume>0&&solid.volume<blankVolume-1,`${id} removes real wall material`);
    const persisted=sceneParts(saved.result).find(part=>part.entityId===saved.result.partEntityId).properties['tubeDesigner.sideSketch'];
    assert.deepEqual(persisted.entities,current.entities,`${id} persists every editable curve parameter`);
    if(check)await check(solid,saved);
    const item={id,partEntityId:saved.result.partEntityId,sketch:saved.payload.sketch,geometry:solid};
    savedCurves.push(item);cases.push(item);console.log(`native CAD: ${id} saved`);return item;
  };

  await loadEmpty('cad-numeric-move-endcuts');
  console.log('native CAD: empty sketch opened');
  await drawCircle(20,70,5);
  console.log('native CAD: numeric circle drawn');
  let current=await draft();assert.equal(current.entities.length,1);near(current.entities[0].radius,5,'Numeric circle radius');
  await command('move').click();await setCad('dx',10);await setCad('dy',5);await applyCad();
  current=await draft();near(current.entities[0].cx,30,'Moved U');near(current.entities[0].cy,75,'Moved S');
  await command('end-cuts').click();await setCad('start',10);await setCad('end',190);await applyCad();
  const cutDraft=await draft(),cutPreview=await previewNow();
  assert.deepEqual(cutPreview.payload.sketch.endCuts,cutDraft.endCuts);assert.equal(cutPreview.result.splitPartCount,1);
  near(cutPreview.result.preview.bounds.width,180,'Native trimmed preview length',.001);
  await page.screenshot({path:resolve(artifacts,'cad-numeric-move-trimmed-preview.png')});
  await command('move').click();await setCad('dx',2);await setCad('dy',3);
  cutDraft.entities[0].cx+=2;cutDraft.entities[0].cy+=3;
  const cutSaved=await saveNow('TubeDesigner.AddNestingSideSketchPart',true),cutGeometry=await geometry(cutSaved.result.partEntityId);
  console.log('native CAD: moved circle and end planes saved');
  assert.equal(cutGeometry.valid,true);near(cutGeometry.volume,544*180-2*Math.PI*25,'Actual trimmed tube minus full circular hole',.05);
  assert.deepEqual(cutSaved.payload.sketch.entities,cutDraft.entities);assert.deepEqual(cutSaved.payload.sketch.endCuts,cutDraft.endCuts);
  cases.push({id:'numeric-circle-move-end-cuts',partEntityId:cutSaved.result.partEntityId,sketch:cutSaved.payload.sketch,geometry:cutGeometry});

  await loadEmpty('cad-circumferential-array');await drawCircle(20,60,4);
  console.log('native CAD: circumferential array source drawn');
  await command('array-circumferential').click();await setCad('count',4);await setCad('rows',1);await applyCad();
  current=await draft();assert.equal(current.entities.length,4);assert.equal(current.arrays.length,1);
  near(current.arrays[0].spec.spacingX,period/4,'Automatic circumference spacing');
  await command('array-edit').click();await setField('columns',6);await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').click();await idle();
  current=await draft();assert.equal(current.entities.length,6);assert.equal(current.arrays.length,1);near(current.arrays[0].spec.spacingX,period/6,'Associative circumference edit');
  const arrayDraft=current,arrayPreview=await previewNow();assert.deepEqual(arrayPreview.payload.sketch.arrays,arrayDraft.arrays);
  const arraySaved=await saveNow('TubeDesigner.AddNestingSideSketchPart'),arrayGeometry=await geometry(arraySaved.result.partEntityId);
  console.log('native CAD: associated array saved');
  assert.equal(arrayGeometry.valid,true);near(arrayGeometry.volume,blankVolume-6*2*Math.PI*16,'Six full circle interiors removed',.08);
  const storedArray=sceneParts(arraySaved.result).find(part=>part.entityId===arraySaved.result.partEntityId).properties['tubeDesigner.sideSketch'];
  assert.deepEqual(storedArray.arrays,arrayDraft.arrays);assert.deepEqual(storedArray.entities,arrayDraft.entities);
  cases.push({id:'circumferential-associated-array',partEntityId:arraySaved.result.partEntityId,sketch:arraySaved.payload.sketch,geometry:arrayGeometry});

  await loadEmpty('cad-real-split-parts');await command('rectangle').click();await numericPoint(0,90);await numericPoint(period,100);
  await command('split-parts').click();await cadField('splitParts').check();await applyCad();assert.equal((await draft()).splitParts,true);
  const splitPreview=await previewNow();assert.equal(splitPreview.result.splitPartCount,2);assert.equal(splitPreview.result.parts.length,2);
  const splitSaved=await saveNow('TubeDesigner.AddNestingSideSketchPart');assert.equal(splitSaved.result.partEntityIds.length,2);
  console.log('native CAD: split solids saved');
  const splitParts=splitSaved.result.partEntityIds.map(id=>sceneParts(splitSaved.result).find(part=>part.entityId===id));
  assert.deepEqual(splitParts.map(part=>Math.round(part.length)),[90,100]);assert.ok(splitParts.every(part=>part.quantity===1));
  const splitGeometries=await Promise.all(splitParts.map(part=>geometry(part.entityId)));assert.ok(splitGeometries.every(item=>item.valid));
  near(splitGeometries.reduce((sum,item)=>sum+item.volume,0),544*190,'Real independent split solids volume',.05);
  const selectedSplitIds=await page.evaluate(()=>window.fixture.view.tubeDesignerNestingSelectedPartIds);
  assert.ok(splitSaved.result.partEntityIds.every(id=>selectedSplitIds.includes(id)),'Every newly split manufacturing part remains selected');
  cases.push({id:'true-solid-split',partEntityIds:splitSaved.result.partEntityIds,sketch:splitSaved.payload.sketch,geometries:splitGeometries,lengths:splitParts.map(part=>part.length)});

  await loadSources('cad-ellipse-offset',[{id:'native-offset-ellipse',kind:'ellipse',cx:30,cy:100,radiusX:8,radiusY:5,rotation:0,closed:true}]);
  await command('offset').click();await setCad('distance',1.5);await applyCad();
  current=await draft();assert.equal(current.entities.length,2);
  assert.equal(current.entities[1].kind,'path');assert.equal(current.entities[1].closed,true);
  assert.ok(current.entities[1].segments.length>1&&current.entities[1].segments.every(segment=>segment.kind==='bezier'));
  // Simpson integration of the original analytic ellipse supplies an independent
  // perimeter; Steiner's formula gives the exact area of its outward parallel.
  const steps=4096,h=2*Math.PI/steps,speed=t=>Math.hypot(8*Math.sin(t),5*Math.cos(t));
  let perimeterSum=speed(0)+speed(2*Math.PI);
  for(let i=1;i<steps;i++)perimeterSum+=(i%2?4:2)*speed(i*h);
  const ellipsePerimeter=h*perimeterSum/3,ellipseOffsetArea=40*Math.PI+ellipsePerimeter*1.5+Math.PI*1.5**2;
  const offsetBoundaryTolerance=.01,ellipseVolumeTolerance=2*((ellipsePerimeter+2*Math.PI*1.5)*offsetBoundaryTolerance+Math.PI*offsetBoundaryTolerance**2);
  await page.screenshot({path:resolve(artifacts,'cad-ellipse-offset-bezier.png')});
  const savedEllipse=await saveCurves('ellipse-offset-native-brep',solid=>near(solid.volume,blankVolume-2*ellipseOffsetArea,'Outward ellipse offset removes its Steiner area through a 2mm wall',ellipseVolumeTolerance));
  savedEllipse.analyticCheck={ellipsePerimeter,expectedOffsetArea:ellipseOffsetArea,expectedRemovedVolume:2*ellipseOffsetArea,
    actualRemovedVolume:blankVolume-savedEllipse.geometry.volume,boundaryToleranceMm:offsetBoundaryTolerance,volumeToleranceMm3:ellipseVolumeTolerance};

  await loadSources('cad-spline-offset',[{id:'native-offset-spline',kind:'spline',points:[[20,60],[30,85],[45,65],[55,100]],closed:false}]);
  await command('offset').click();await setCad('distance',1);await applyCad();
  current=await draft();assert.equal(current.entities.length,2);
  assert.equal(current.entities[1].kind,'path');assert.equal(current.entities[1].closed,false);
  assert.ok(current.entities[1].segments.length>1&&current.entities[1].segments.every(segment=>segment.kind==='bezier'));
  await saveCurves('spline-offset-native-kerf',solid=>assert.ok(solid.volume<blankVolume-40,'Both smooth editable trajectories cut the actual tube wall'));

  await loadSources('cad-curve-fillet',[
    {id:'native-fillet-arc',kind:'circleArc',cx:30,cy:70,radius:10,startAngle:Math.PI,sweep:-Math.PI/2},
    {id:'native-fillet-line',kind:'line',x1:30,y1:80,x2:30,y2:105}]);
  await command('fillet').click();await setCad('radius',2);await applyCad();
  current=await draft();const filletSegments=current.entities.flatMap(entity=>entity.segments??[entity]);
  assert.ok(filletSegments.filter(segment=>segment.kind==='circleArc').length>=2,'Native cutting receives the retained curve and analytic tangent fillet');
  assert.ok(filletSegments.some(segment=>segment.kind==='circleArc'&&Math.abs(segment.radius-2)<1e-8));
  await saveCurves('curve-line-fillet-native-kerf',solid=>assert.ok(solid.volume<blankVolume-40));

  await loadSources('cad-overlap-repair',[
    {id:'native-overlap-long',kind:'line',x1:20,y1:100,x2:60,y2:100},
    {id:'native-overlap-short',kind:'line',x1:40,y1:100,x2:20,y2:100}]);
  await command('repair').click();await cadField('joinGaps').uncheck();await applyCad();
  current=await draft();assert.equal(current.entities.length,1);assert.equal(current.entities[0].kind,'line');
  await saveCurves('partial-reversed-overlap-native-kerf',solid=>near(solid.volume,blankVolume-2*.8*40,'Repaired overlap cuts one finite-width line exactly once',.05));

  await loadSources('cad-bezier-simplify',[{id:'native-split-bezier',kind:'path',closed:false,segments:[
    {kind:'bezier',points:[[20,60],[20,65],[25,67.5],[30,70]]},
    {kind:'bezier',points:[[30,70],[35,72.5],[40,75],[40,80]]}]}]);
  await command('repair').click();await applyCad();
  current=await draft();assert.equal(current.entities.length,1);assert.equal(current.entities[0].segments.length,1);
  assert.equal(current.entities[0].segments[0].kind,'bezier');
  assert.deepEqual(current.entities[0].segments[0].points,[[20,60],[20,70],[40,70],[40,80]],'Simplification reconstructs the original cubic exactly');
  await saveCurves('split-bezier-simplified-native-kerf',solid=>assert.ok(solid.volume<blankVolume-20));

  await loadSources('cad-self-crossing-repair',[{id:'native-bow',kind:'polyline',closed:true,points:[[20,120],[40,140],[20,140],[40,120]]}]);
  await command('repair').click();assert.equal(await cadField('trimSelfIntersections').isChecked(),false);
  await cadField('trimSelfIntersections').check();await applyCad();
  current=await draft();assert.equal(current.entities.length,2);assert.ok(current.entities.every(entity=>entity.closed));
  assert.equal(new Set(current.entities.map(entity=>entity.id)).size,2);
  await saveCurves('self-crossing-trimmed-native-brep',solid=>near(solid.volume,blankVolume-2*200,'Two repaired bow loops remove 200mm2 through a 2mm wall',.05));

  await bridge.rpc({action:'save',payload:{file:'sketch-cad-browser-native.ictd'}});
  const reopened=await bridge.rpc({action:'open',payload:{file:'sketch-cad-browser-native.ictd'}});
  await page.evaluate(scene=>{const f=window.fixture;f.view.scene=scene;f.ops.renderProject();},reopened.snapshot);
  await reopenPart(cutSaved.result.partEntityId);current=await draft();assert.deepEqual(current.entities,cutDraft.entities);assert.deepEqual(current.endCuts,cutDraft.endCuts);
  near(await page.evaluate(()=>window.fixture.state.sideReference.unfolding.length),200,'Edited trimmed part restores original source length');
  const reopenedCut=await geometry(cutSaved.result.partEntityId);assert.equal(reopenedCut.valid,true);
  near(reopenedCut.volume,cutGeometry.volume,'Trimmed BRep retains its volume after project reopen',1e-5);
  cases.find(item=>item.id==='numeric-circle-move-end-cuts').reopenVerified=true;
  await command('cancel').click();await idle();
  await reopenPart(arraySaved.result.partEntityId);current=await draft();assert.deepEqual(current.arrays,arrayDraft.arrays);assert.deepEqual(current.entities,arrayDraft.entities);
  const reopenedArray=await geometry(arraySaved.result.partEntityId);assert.equal(reopenedArray.valid,true);
  near(reopenedArray.volume,arrayGeometry.volume,'Associated array retains its real BRep after project reopen',1e-5);
  cases.find(item=>item.id==='circumferential-associated-array').reopenVerified=true;
  await clickModel(24,60);await idle();
  assert.ok((await draft()).selectedIds.includes(current.entities[0].id),'Selecting a restored original curve exposes its associated array');
  await command('array-edit').click();await setField('columns',3);await page.locator('[data-cam-action="tube-designer-sketch-array-apply"]').click();await idle();
  current=await draft();assert.equal(current.entities.length,3);near(current.arrays[0].spec.spacingX,period/3,'Reopened associative array updates exact perimeter pitch');
  await previewNow();const arrayUpdated=await saveNow('TubeDesigner.SavePartSketch');assert.equal(arrayUpdated.result.partEntityId,arraySaved.result.partEntityId);
  const updatedGeometry=await geometry(arrayUpdated.result.partEntityId);assert.equal(updatedGeometry.valid,true);near(updatedGeometry.volume,blankVolume-3*2*Math.PI*16,'Array replacement reconstructs frozen blank rather than accumulating old holes',.08);
  const updatedDraft=sceneParts(arrayUpdated.result).find(part=>part.entityId===arrayUpdated.result.partEntityId).properties['tubeDesigner.sideSketch'];
  assert.deepEqual(updatedDraft.arrays,current.arrays);assert.deepEqual(updatedDraft.entities,current.entities);
  cases.push({id:'reopen-array-edit-replace',partEntityId:arrayUpdated.result.partEntityId,sketch:arrayUpdated.payload.sketch,geometry:updatedGeometry});
  await reopenPart(splitSaved.result.partEntityIds[1]);current=await draft();assert.equal(current.splitParts,true);assert.deepEqual(current.entities,splitSaved.payload.sketch.entities);
  near(await page.evaluate(()=>window.fixture.state.sideReference.unfolding.length),200,'Second split part retains original axial drawing coordinates');
  await command('cancel').click();await idle();
  for(let i=0;i<splitSaved.result.partEntityIds.length;i++) {
    const id=splitSaved.result.partEntityIds[i],restored=sceneParts(reopened.snapshot).find(part=>part.entityId===id);
    assert.deepEqual(restored.properties['tubeDesigner.sideSketch'].entities,splitSaved.payload.sketch.entities);
    const solid=await geometry(id);assert.equal(solid.valid,true);
    near(solid.volume,splitGeometries[i].volume,'Every independent split BRep survives project reopen',1e-5);
  }
  cases.find(item=>item.id==='true-solid-split').reopenVerified=true;
  for(const saved of savedCurves) {
    const persisted=sceneParts(reopened.snapshot).find(part=>part.entityId===saved.partEntityId).properties['tubeDesigner.sideSketch'];
    assert.deepEqual(persisted.entities,saved.sketch.entities,`${saved.id} reopens exact native curve parameters`);
    const restored=await geometry(saved.partEntityId);assert.equal(restored.valid,true);
    near(restored.volume,saved.geometry.volume,`${saved.id} retains its actual BRep volume after .ictd reopen`,1e-5);
    await reopenPart(saved.partEntityId);current=await draft();assert.deepEqual(current.entities,saved.sketch.entities);
    await command('cancel').click();await idle();saved.reopenVerified=true;
  }
  await bridge.rpc({action:'save',payload:{file:'sketch-cad-browser-native-updated.ictd'}});
  const updatedProject=await bridge.rpc({action:'open',payload:{file:'sketch-cad-browser-native-updated.ictd'}});
  const finalArray=sceneParts(updatedProject.snapshot).find(part=>part.entityId===arrayUpdated.result.partEntityId).properties['tubeDesigner.sideSketch'];
  assert.deepEqual(finalArray.entities,arrayUpdated.payload.sketch.entities);assert.deepEqual(finalArray.arrays,arrayUpdated.payload.sketch.arrays);
  const finalArrayGeometry=await geometry(arrayUpdated.result.partEntityId);assert.equal(finalArrayGeometry.valid,true);
  near(finalArrayGeometry.volume,updatedGeometry.volume,'Edited array replacement persists through another actual project save/open',1e-5);
  cases.find(item=>item.id==='reopen-array-edit-replace').reopenVerified=true;
  assert.equal(cases.length,10);assert.ok(cases.every(item=>item.reopenVerified===true));
  const browserEvidence=await page.evaluate(()=>({resources:window.fixture.resources,failures:window.fixture.failures,
    calls:window.fixture.calls.map(call=>({method:call.method,error:call.error}))}));
  assert.deepEqual(errors,[]);assert.deepEqual(browserEvidence.failures,[]);assert.ok(browserEvidence.resources.length>=12);
  writeFileSync(resolve(artifacts,'report.json'),JSON.stringify({passed:true,caseCount:cases.length,reopenVerifiedCount:cases.filter(item=>item.reopenVerified===true).length,
    dllHash:bridge.dllHash,importerHash:bridge.importerHash,assets:Object.fromEntries(assets),cases,browserEvidence,nativeRequests:bridge.requests,
    coverage:'Production numeric circle drawing and group move; end-plane parameters; native GPU previews and BRep volume/validity; ellipse parallel Bezier offset verified against Steiner area, open spline offset and curve-line tangent fillet through true finite-width cuts; partial reversed overlap removal, exact split-Bezier simplification and explicit closed self-crossing trim verified by real material removal; every new editable curve parameter and BRep volume preserved by .ictd save/open and reopened production dialog; circumference array association/replacement and independent split solids. Chrome with real scene SDO stdio host, installed CEF and OS file selector outside scope.'},null,2));
  console.log(JSON.stringify({passed:true,cases:cases.length,artifacts,resourceReads:browserEvidence.resources.length}));
} catch(error) {
  if(page)await page.screenshot({path:resolve(artifacts,'failure.png'),timeout:5000}).catch(()=>{});
  const browserFailure=page?await page.evaluate(()=>({draft:window.fixture?.draft,state:window.fixture?.state?.sidePreviewError,
    calls:window.fixture?.calls.filter(call=>call.error)})).catch(()=>null):null;
  writeFileSync(resolve(artifacts,'failure.json'),JSON.stringify({error:error.stack,dllHash:bridge.dllHash,importerHash:bridge.importerHash,cases,browserFailure},null,2));throw error;
} finally {if(browser)await browser.close();await bridge.close();}
