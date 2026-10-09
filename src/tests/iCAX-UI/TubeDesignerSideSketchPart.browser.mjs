// Real browser interaction coverage. Native geometry/persistence is exercised
// separately; this fixture isolates DOM ownership and late-response behavior.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const root = fileURLToPath(new URL("../../", import.meta.url));
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("http://side-sketch.test/**", route => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body><div id='app'></div></body>" });
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js)$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://side-sketch.test/");
  await page.evaluate(async () => {
    const sketch = await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
    const standard = await import("/src/apps/tube-designer/webpage/nestingStandardPart.mjs");
    const { tubeDesignerCss } = await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.innerHTML = `<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif;background:#13252d;color:#e4f2ee}.tube-section-sketch-body>aside{height:200px;overflow:auto}.tube-sketch-preview-panel{height:450px}.tube-sketch-property-body{height:400px}.fixture-three-canvas{width:100%;height:100%}</style>`;
    const profile = { id: "rect", name: "矩形管", width: 40, depth: 20,
      contours: [{ kind: "polygon", points: [[-20,-10],[20,-10],[20,10],[-20,10]] }] };
    const reference = { available: true, profile, length: 300, unfolding: { available: true, method: "section-arc-length",
      surfaces: [{ inner:false, uPeriod:120, rectangle:{available:true,width:300,height:120},
        wires:[40,60,100].map(u=>({role:"profile-junction",points:[[0,u],[300,u]]})) }] },
      preview: { baseGeometry: {url:"neutral",version:1}, baseBounds:{min:[0,-20,-10],max:[300,20,10]} } };
    const calls = [], mount = document.querySelector("#app");
    const view = { activeAreaId: "nesting", scene: { tubeDesigner: { nestingGroups: [] } },
      tubeDesignerSystemProfiles: [profile], tubeDesignerUserData: { profiles: [] } };
    let pendingPreview = null, holdNextAdd = false, pendingAdd = null;
    const loggedErrors = [];
    const context = { mount, sceneProxy: { resources: {}, async invoke(method, payload) {
      calls.push({method,payload:structuredClone(payload)});
      if (method === "TubeDesigner.ReleaseNestingSideSketchPreview") return {};
      if (method === "TubeDesigner.PreviewNestingSideSketchPart") {
        if (payload.sketch) return new Promise(resolve => { pendingPreview = () => resolve({ ...reference,
          preview:{...reference.preview,baseGeometry:{url:"cut",version:calls.length}} }); });
        return reference;
      }
      if (method === "TubeDesigner.AddNestingSideSketchPart") {
        if (holdNextAdd) {
          holdNextAdd = false;
          return new Promise((resolve,reject) => { pendingAdd = {resolve,reject}; });
        }
        return { partEntityId:"created", tubeDesigner:{ nestingGroups:[{parts:[{
        entityId:"created",independentNesting:true,length:300,profile,manufacturingGeometryResourceVersion:7,
        properties:{"tubeDesigner.sideSketch":payload.sketch,"tubeDesigner.sideSketchRecipe":{profile,length:300}}
      }]}] } };
      }
      if (method === "TubeDesigner.SavePartSketch") return {tubeDesigner:view.scene.tubeDesigner};
      throw Error(`Unexpected ${method}`);
    } }, actions: {async selectRibbonTab() {}, async refreshActiveSceneState() {}} };
    const ops = { renderProject: render, showNotice() {}, createSideSketchViewport() {
      const root = document.createElement("div"), canvas = document.createElement("canvas");
      root.style.cssText = "position:absolute;inset:0"; canvas.className = "fixture-three-canvas"; root.append(canvas);
      return { root, mount(host) {host.append(root);}, setStandardView(){}, resize(){}, fitViewToViewport(){},
        async applyViewSnapshot(snapshot) {return {applied:true,entityIds:snapshot.rows.map(r=>r.entityId)};},
        retainViewResources(){},setVisibleEntityIds(){},dispose(){root.remove();} };
    } };
    function render() {
      if (view.tubeDesignerSketchDialogOpen && sketch.patchSketchDialogDom(context,view,mount,ops)) return;
      mount.innerHTML = standard.renderNestingStandardPartDialog(view) + sketch.renderSectionSketchDialog(context,view);
      sketch.rememberSketchDialogDom(view,mount,context.sceneProxy);
      standard.rememberNestingStandardPartDom(view,mount,context.sceneProxy);
      sketch.attachSketchAreaInteractions(context,view,mount,ops);
    }
    // Match the shared workbench's errorPresentation="log" policy: the
    // general view error is consumed after an action fails. The modal must
    // retain its own visible failure so the user can retry or cancel.
    function dispatchAction(action,target) {
      const operation = action.startsWith("tube-designer-sketch-")
        ? sketch.handleSketchAreaAction(context,view,action,target,ops)
        : standard.handleNestingStandardPartAction(context,view,action,target,ops);
      void Promise.resolve(operation).catch(error => {
        view.error = error.message;
        loggedErrors.push(view.error);
        view.error = "";
        render();
      });
    }
    mount.addEventListener("click", event => {
      const target=event.target.closest("[data-cam-action]");
      if(!target)return;
      const action=target.dataset.camAction;
      dispatchAction(action,target);
    });
    mount.addEventListener("change", event => {
      const target=event.target.closest("[data-cam-change-action]");if(!target)return;
      const action=target.dataset.camChangeAction;
      dispatchAction(action,target);
    });
    window.check={view,context,ops,render,sketch,standard,calls,reference,loggedErrors,
      holdNextAdd(){holdNextAdd=true;},
      rejectAdd(message){const pending=pendingAdd;pendingAdd=null;pending?.reject(new Error(message));},
      resolvePreview(){pendingPreview?.();pendingPreview=null;}, draft(){const s=view.tubeDesignerSketch;return s.sideByPart[s.targetPartId];}};
    await standard.handleNestingStandardPartRibbonCommand(context,view,"nesting.draw-2d-part",ops);
  });
  await page.getByLabel("长度（mm）",{exact:true}).fill("300");
  await page.getByRole("button",{name:"开始绘制",exact:true}).click();
  await page.getByRole("dialog",{name:"二维绘制零件",exact:true}).waitFor();
  await page.locator("[data-side-sketch-preview-ready=true]").waitFor();
  const assertJunctionGuides = async () => {
    const guide = await page.evaluate(() => {
      const svg = document.querySelector('[data-tube-sketch-canvas]');
      const lines = [...document.querySelectorAll('[data-tube-sketch-profile-junction]')];
      return { start:Number(svg.dataset.sketchXMin), end:Number(svg.dataset.sketchXMax), lines:lines.map(line=>({
        u:Number(line.dataset.tubeSketchProfileU), path:line.getAttribute('d'),
        dash:getComputedStyle(line).strokeDasharray, pointer:getComputedStyle(line).pointerEvents,
        vector:getComputedStyle(line).vectorEffect,
      })) };
    });
    assert.ok(guide.lines.length > 0);
    for (const line of guide.lines) {
      assert.ok(line.u >= guide.start - 1e-7 && line.u <= guide.end + 1e-7, 'Only visible periodic junctions are rendered');
      const canonical = ((line.u % 120) + 120) % 120;
      assert.ok([40,60,100].some(u=>Math.abs(u-canonical)<1e-7), 'Junctions keep exact physical arc-length positions across periods');
      assert.notEqual(line.dash, 'none');
      assert.equal(line.pointer, 'none');
      assert.equal(line.vector, 'non-scaling-stroke');
      const coords = line.path.match(/[-+]?\d*\.?\d+(?:e[-+]?\d+)?/gi).map(Number);
      assert.equal(coords.length, 4);
      assert.equal(coords[0], coords[2], 'Profile segment joins are vertical in U/S space');
      assert.ok(coords[1] !== coords[3]);
    }
  };
  await assertJunctionGuides();
  assert.equal(await page.evaluate(()=>window.check.calls.filter(c=>c.method.startsWith("TubeDesigner.Add")).length),0);
  await page.evaluate(()=>{
    const c=window.check,d=c.draft();
    d.entities=[{id:"hole",kind:"rectangle",x:-5,y:70,width:10,height:20,closed:true},
      {id:"slit",kind:"line",x1:115,y1:30,x2:125,y2:50}];
    d.selectedIds=["hole"];d.dirty=true;c.render();
    const svg=document.querySelector("[data-tube-sketch-canvas]");
    const width=document.querySelector("[data-cam-change-action=tube-designer-sketch-trajectory-width]");
    const previewCanvas=document.querySelector(".fixture-three-canvas");
    window.nodes={svg,width,previewCanvas,probe:0};
    svg.addEventListener("retention-probe",()=>window.nodes.probe++);
    for(let i=0;i<8;i++)c.render();
    svg.dispatchEvent(new Event("retention-probe"));
  });
  const expectedCopies = await page.locator('[data-tube-sketch-canvas]').evaluate(svg => {
    const min = Number(svg.dataset.sketchXMin), max = Number(svg.dataset.sketchXMax);
    return [[-5, 5], [115, 125]].reduce((count, [lo, hi]) => {
      const first = Math.ceil((min - hi) / 120), last = Math.floor((max - lo) / 120);
      return count + Math.max(0, last - first + 1) - (first <= 0 && last >= 0 ? 1 : 0);
    }, 0);
  });
  assert.equal(await page.locator("[data-tube-sketch-periodic-copy]").count(),expectedCopies);
  assert.deepEqual(await page.evaluate(()=>["centerX","centerY"].map(key=>
    document.querySelector(`[data-sketch-field="${key}"]`).closest("label").querySelector("span").textContent)),["中心 U","中心 S"]);
  assert.deepEqual(await page.evaluate(()=>({svg:window.nodes.svg===document.querySelector("[data-tube-sketch-canvas]"),
    width:window.nodes.width===document.querySelector("[data-cam-change-action=tube-designer-sketch-trajectory-width]"),
    preview:window.nodes.previewCanvas===document.querySelector(".fixture-three-canvas"),probe:window.nodes.probe})),
    {svg:true,width:true,preview:true,probe:1});

  // Continue one CAD command while panning across several physical periods.
  await page.evaluate(()=>{window.check.view.tubeDesignerSketch.snapEnabled=false;});
  const canvasPoint = async(x,y)=>page.locator("[data-tube-sketch-canvas]").evaluate((svg,[x,y])=>{
    const p=svg.createSVGPoint();p.x=x;p.y=y;const q=p.matrixTransform(svg.getScreenCTM());return[q.x,q.y];
  },[x,y]);
  await page.locator('[data-sketch-command="sketch.line"]').click();
  await page.mouse.click(...await canvasPoint(150,350));
  for(let turn=0;turn<4;turn++) {
    const from=await canvasPoint(900,350),to=await canvasPoint(130,350);
    await page.mouse.move(...from);await page.mouse.down({button:"middle"});
    await page.mouse.move(...to,{steps:8});await page.mouse.up({button:"middle"});
    await page.mouse.click(...await canvasPoint(900,350));
  }
  await page.locator("[data-tube-sketch-canvas]").press("Enter");
  assert.ok(await page.evaluate(()=>Math.max(...window.check.draft().entities.filter(e=>e.kind==="line").map(e=>e.x2))>360),
    "Actual pointer input may continue beyond three circumference periods");
  await assertJunctionGuides();
  await page.mouse.move(...await canvasPoint(500,300));await page.mouse.wheel(0,-300);
  assert.ok(await page.evaluate(()=>window.check.view.tubeDesignerSketch.sideViewport.zoom)>1);
  await assertJunctionGuides();
  await page.locator("[data-tube-sketch-canvas]").press("Home");
  await page.locator('[data-sketch-command="sketch.rectangle"]').click();
  await page.mouse.click(...await canvasPoint(450,535));
  await page.mouse.click(...await canvasPoint(600,450));
  assert.ok(await page.evaluate(()=>window.check.draft().entities.some(e=>e.kind==="rectangle"&&e.y<0)),
    "Vertical overflow is retained in the source drawing for native clipping");
  await page.evaluate(()=>{const c=window.check;c.draft().selectedIds=["hole"];c.render();});

  // A preview response arrives after new selection/typing and newer scrolling.
  await page.getByRole("button",{name:"预览三维",exact:true}).click();
  await page.evaluate(()=>{
    const c=window.check,d=c.draft();
    d.entities.push({id:"text",kind:"text",x:10,y:10,value:"abcd"});d.selectedIds=["text"];c.render();
    const text=document.querySelector('[data-sketch-field="value"]');text.focus({preventScroll:true});
    text.value="abcdefgh";text.setSelectionRange(2,6,"forward");
    window.nodes.text=text;
    const sides=[...document.querySelectorAll(".tube-section-sketch-body>aside")];
    sides[0].scrollTop=82;sides[1].scrollTop=66;
    const nested=document.querySelector(".tube-sketch-property-body");nested.scrollTop=45;
    window.positions={left:sides[0].scrollTop,right:sides[1].scrollTop,nested:nested.scrollTop};
    c.resolvePreview();
  });
  await page.getByRole("button",{name:"预览三维",exact:true}).waitFor({state:"visible"});
  await page.waitForFunction(()=>!window.check.view.tubeDesignerSketch.sidePreviewPending);
  const retained=await page.evaluate(()=>{
    const sides=[...document.querySelectorAll(".tube-section-sketch-body>aside")],text=document.querySelector('[data-sketch-field="value"]');
    return {same:text===window.nodes.text,focused:document.activeElement===text,value:text.value,start:text.selectionStart,end:text.selectionEnd,
      left:sides[0].scrollTop,right:sides[1].scrollTop,nested:document.querySelector(".tube-sketch-property-body").scrollTop,
      positions:window.positions,preview:window.nodes.previewCanvas===document.querySelector(".fixture-three-canvas")};
  });
  assert.equal(retained.same,true);assert.equal(retained.focused,true);assert.equal(retained.value,"abcdefgh");
  assert.equal(retained.start,2);assert.equal(retained.end,6);assert.equal(retained.preview,true);
  for(const key of ["left","right","nested"])assert.equal(retained[key],retained.positions[key]);
  await page.evaluate(()=>{const c=window.check,d=c.draft();d.entities=d.entities.filter(e=>e.id!=="text");d.selectedIds=["hole"];c.render();});
  assert.equal(await page.evaluate(()=>document.activeElement?.matches?.('[data-sketch-field="centerX"]')),false,
    "Removing a conditional property never hands its focus to a different geometry field");
  await page.getByLabel("开放线条切缝宽度（mm）",{exact:true}).fill("0.8");
  await page.evaluate(()=>{
    const c=window.check;c.holdNextAdd();
    window.nodes.saveFeedback=document.querySelector(".tube-section-sketch-feedback");
    window.nodes.saveError=document.querySelector("[data-sketch-save-error]");
    window.nodes.saveProgress=document.querySelector("[data-sketch-save-progress]");
    window.saveEntities=JSON.stringify(c.draft().entities);
  });
  await page.getByRole("button",{name:"确认",exact:true}).dblclick();
  await page.locator("[data-sketch-save-progress]").waitFor({state:"visible"});
  assert.equal(await page.getByRole("button",{name:"保存中…",exact:true}).isDisabled(),true);
  assert.equal(await page.getByRole("button",{name:"取消",exact:true}).isDisabled(),true);
  assert.match(await page.locator("[data-sketch-save-progress]").innerText(),/正在创建二维绘制零件/);
  assert.match(await page.locator("[data-sketch-save-progress]").innerText(),/完成后会自动退出绘图界面/);
  assert.equal(await page.evaluate(()=>window.check.calls.filter(c=>c.method==="TubeDesigner.AddNestingSideSketchPart").length),1,
    "Double clicking confirmation submits only one save while the response is held");
  assert.equal(await page.getByRole("dialog",{name:"二维绘制零件",exact:true}).isVisible(),true);
  await page.evaluate(()=>{
    const c=window.check,state=c.view.tubeDesignerSketch;
    state.tool="polyline";state.command={tool:"polyline",points:[[10,10],[20,20],[30,10]]};
    document.querySelector("[data-tube-sketch-canvas]").dispatchEvent(new MouseEvent("contextmenu",{bubbles:true,cancelable:true}));
    if(JSON.stringify(c.draft().entities)!==window.saveEntities)throw Error("Right click changed the submitted drawing while saving");
    state.command=null;state.tool="select";
  });
  await page.evaluate(()=>{
    const sides=[...document.querySelectorAll(".tube-section-sketch-body>aside")];
    sides[0].scrollTop=93;sides[1].scrollTop=74;
    const nested=document.querySelector(".tube-sketch-property-body");nested.scrollTop=38;
    window.savePositions={left:sides[0].scrollTop,right:sides[1].scrollTop,nested:nested.scrollTop};
    window.nodes.svg.dispatchEvent(new Event("retention-probe"));
    window.check.rejectAdd("测试：生成切割失败，请修改草图后重试。");
  });
  await page.locator("[data-sketch-save-error]").waitFor({state:"visible"});
  await page.waitForFunction(()=>window.check.loggedErrors.length===1);
  assert.equal(await page.getByRole("button",{name:"确认",exact:true}).isEnabled(),true);
  assert.equal(await page.getByRole("button",{name:"取消",exact:true}).isEnabled(),true);
  assert.equal(await page.locator("[data-sketch-save-progress]").isVisible(),false);
  assert.equal(await page.locator("[data-sketch-save-error]").innerText(),"测试：生成切割失败，请修改草图后重试。");
  const failedSave=await page.evaluate(()=>{
    const c=window.check,sides=[...document.querySelectorAll(".tube-section-sketch-body>aside")];
    window.nodes.svg.dispatchEvent(new Event("retention-probe"));
    const error=document.querySelector("[data-sketch-save-error]").getBoundingClientRect();
    const dialog=document.querySelector(".tube-section-sketch-dialog").getBoundingClientRect();
    return {generalError:c.view.error,modalError:c.view.tubeDesignerSketch.sideSaveError,
      sameDraft:JSON.stringify(c.draft().entities)===window.saveEntities,dirty:c.draft().dirty,width:Number(c.draft().trajectoryWidth),
      svg:window.nodes.svg===document.querySelector("[data-tube-sketch-canvas]"),
      input:window.nodes.width===document.querySelector("[data-cam-change-action=tube-designer-sketch-trajectory-width]"),
      canvas:window.nodes.previewCanvas===document.querySelector(".fixture-three-canvas"),
      feedback:window.nodes.saveFeedback===document.querySelector(".tube-section-sketch-feedback"),
      error:window.nodes.saveError===document.querySelector("[data-sketch-save-error]"),
      progress:window.nodes.saveProgress===document.querySelector("[data-sketch-save-progress]"),probe:window.nodes.probe,
      errorInsideDialog:error.top>=dialog.top&&error.bottom<=dialog.bottom&&error.width>0&&error.height>0,
      positions:{left:sides[0].scrollTop,right:sides[1].scrollTop,nested:document.querySelector(".tube-sketch-property-body").scrollTop},
      expectedPositions:window.savePositions};
  });
  assert.equal(failedSave.generalError,"");assert.match(failedSave.modalError,/生成切割失败/);
  assert.equal(failedSave.sameDraft,true);assert.equal(failedSave.dirty,true);assert.equal(failedSave.width,0.8);
  for(const key of ["svg","input","canvas","feedback","error","progress","errorInsideDialog"])assert.equal(failedSave[key],true,key);
  assert.equal(failedSave.probe,3);assert.deepEqual(failedSave.positions,failedSave.expectedPositions);
  await page.getByRole("button",{name:"确认",exact:true}).click();
  await page.waitForFunction(()=>!window.check.view.tubeDesignerSketchDialogOpen);
  assert.equal(await page.locator(".tube-section-sketch-dialog").count(),0,
    "A successful retry removes the modal from the visible browser DOM");
  const result=await page.evaluate(()=>({calls:window.check.calls,selected:window.check.view.tubeDesignerActiveNestingPartId}));
  assert.equal(result.calls.filter(c=>c.method==="TubeDesigner.AddNestingSideSketchPart").length,2,
    "The failed creation is retried exactly once and success exits the modal");
  assert.equal(result.calls.find(c=>c.method==="TubeDesigner.AddNestingSideSketchPart").payload.sketch.trajectoryWidth,0.8);
  assert.equal(result.calls.find(c=>c.method==="TubeDesigner.AddNestingSideSketchPart").payload.sketch.coordinateSpace,"arc-length-axial");
  assert.ok(result.calls.find(c=>c.method==="TubeDesigner.AddNestingSideSketchPart").payload.sketch.entities.some(e=>e.kind==="rectangle"&&e.y<0));
  assert.equal(result.selected,"created");
  // Reopening the same part mutates the shared sketch state object. Its old
  // controller must release its own key without clearing the new editor's key.
  for(const key of ["reopened-one","reopened-two"]) {
    await page.evaluate(key=>{
      const c=window.check,part=c.view.scene.tubeDesigner.nestingGroups[0].parts[0];
      c.sketch.beginPartSideSketch(c.view,part,{...c.reference,previewResourceKey:key});
      c.view.tubeDesignerSketchDialogOpen=true;c.render();
    },key);
    await page.locator("[data-side-sketch-preview-ready=true]").waitFor();
  }
  assert.equal(await page.evaluate(()=>window.check.view.tubeDesignerSketch.sidePreviewResourceKey),"reopened-two");
  await page.getByRole("button",{name:"取消",exact:true}).click();
  const released=await page.evaluate(()=>window.check.calls.filter(c=>c.method==="TubeDesigner.ReleaseNestingSideSketchPreview").map(c=>c.payload.previewResourceKey));
  assert.ok(released.includes("reopened-one"));assert.ok(released.includes("reopened-two"));
  assert.deepEqual(errors,[]);
  console.log("TubeDesignerSideSketchPart.browser: UI workflow, periodic drawing, canvas/listener retention, latest scroll/focus/selection, conditional removal, delayed save feedback, duplicate suppression, visible logged failure and retry/exit passed (native geometry mocked)");
} finally { await browser.close(); }
