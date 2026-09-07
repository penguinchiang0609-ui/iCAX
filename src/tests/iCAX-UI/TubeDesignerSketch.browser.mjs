// Isolated interaction regression test using shipped rendering and event handlers.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({headless:true,...(process.env.ICAX_BROWSER_CHANNEL?{channel:process.env.ICAX_BROWSER_CHANNEL}:{})});
const errors=[];
try {
  const page=await browser.newPage();
  page.on("pageerror",e=>errors.push(e.message));
  const root=fileURLToPath(new URL("../../",import.meta.url));
  await page.route("http://sketch.test/**",async route=>{
    const pathname=decodeURIComponent(new URL(route.request().url()).pathname);
    if(pathname==="/")return route.fulfill({contentType:"text/html",body:"<!doctype html><html><head></head><body></body></html>"});
    const path=resolve(root,pathname.replace(/^\/src\//,""));
    if(!pathname.startsWith("/src/")||!path.startsWith(root.replace(/[\\/]$/,"")+sep)||!path.endsWith(".mjs"))return route.abort();
    await route.fulfill({contentType:"text/javascript",body:readFileSync(path,"utf8")});
  });
  await page.goto("http://sketch.test/");
  await page.evaluate(async()=>{
    const sketch=await import("/src/apps/tube-designer/webpage/sketchArea.mjs");
    const {tubeDesignerCss}=await import("/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");
    document.head.innerHTML=`<style>${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Arial}#toolbar{height:42px;display:flex;gap:8px}#workspace{display:grid;grid-template-columns:minmax(0,1fr) 280px;height:calc(100vh - 42px)}#drawing{position:relative;min-width:0;min-height:0}#right{min-height:0}button{padding:4px 10px}</style>`;
    const view={activeAreaId:"sketch",tubeDesignerSketch:sketch.createInitialSketchState(),scene:{tubeDesigner:{}}};
    const commands=[["select","选择"],["arc","三点圆弧"],["line","直线"],["freehand","自由曲线"],["break","打断"],["insert-point","插入点"],["join","合并"],["undo","撤销"],["redo","重做"]];
    const ops={renderProject:render,showNotice:(_c,_v,message)=>{view.notice=message;render();}};
    function render(){
      document.body.innerHTML=`<div id="toolbar">${commands.map(([id,title])=>`<button data-command="${id}">${title}</button>`).join("")}</div><div id="workspace"><div id="drawing">${sketch.renderSketchViewportOverlay({},view)}</div><div id="right">${sketch.renderSketchRightPane({},view)}</div></div>`;
      document.querySelectorAll("[data-command]").forEach(b=>b.onclick=()=>sketch.handleSketchRibbonCommand({},view,`sketch.${b.dataset.command}`,ops));
      sketch.attachSketchAreaInteractions({},view,document.body,ops);
    }
    window.check={view,render,sketch};render();
  });
  const reset=async(entity=null)=>{
    await page.evaluate(entity=>{
      const {view,sketch,render}=window.check;
      view.tubeDesignerSketch=sketch.createInitialSketchState();
      view.tubeDesignerSketch.snapEnabled=false;
      if(entity){const d=view.tubeDesignerSketch.section;d.entities=Array.isArray(entity)?entity:[entity];d.selectedId=d.entities[0].id;d.selectedIds=[d.selectedId];}
      render();
    },entity);
  };
  const state=()=>page.evaluate(()=>JSON.parse(JSON.stringify(window.check.view.tubeDesignerSketch)));
  const point=async(x,y)=>page.locator("[data-tube-sketch-canvas]").evaluate((svg,[x,y])=>{
    const box=svg.getBoundingClientRect(),v=window.check.view.tubeDesignerSketch.sectionViewport;
    const scale=box.width*v.zoom/220;
    return [box.x+box.width/2+(x-v.centerX)*scale,box.y+box.height/2-(y-v.centerY)*scale];
  },[x,y]);
  const clickAt=async(x,y)=>{const [cx,cy]=await point(x,y);await page.mouse.click(cx,cy);};
  const drag=async(a,b)=>{const start=await point(...a),end=await point(...b);await page.mouse.move(...start);await page.mouse.down();await page.mouse.move(...end,{steps:12});await page.mouse.up();};
  const command=async(id)=>page.locator(`[data-command="${id}"]`).click();

  // Every part of the visible canvas must accept points at tall, wide and small sizes.
  for(const viewport of [{width:1280,height:800},{width:1000,height:1050},{width:1600,height:650},{width:900,height:480}]) {
    await page.setViewportSize(viewport);await reset();await command("line");
    const box=await page.locator("[data-tube-sketch-canvas]").boundingBox();
    await page.mouse.click(box.x+box.width*.06,box.y+box.height*.06);
    await page.mouse.click(box.x+box.width*.94,box.y+box.height*.94);
    const e=(await state()).section.entities[0];
    assert.ok(Math.abs(e.x1+96.8)<.01);
    assert.ok(Math.abs(e.y1-220*.44*box.height/box.width)<.01,"top area is not clamped to a hidden 600-unit rectangle");
    assert.ok(Math.abs(e.y2+220*.44*box.height/box.width)<.01,"bottom area is editable");
  }
  await page.setViewportSize({width:1280,height:820});await reset();await command("arc");
  await clickAt(40,0);await clickAt(0,40);await clickAt(-40,0);
  let e=(await state()).section.entities[0];
  assert.equal(e.kind,"circleArc");assert.ok(Math.abs(e.radius-40)<.01);
  assert.match(await page.locator(".tube-sketch-geometry .tube-sketch-entity").getAttribute("d"),/A/);
  await command("insert-point");await clickAt(28.284,28.284);
  e=(await state()).section.entities[0];
  assert.equal(e.kind,"path");assert.equal(e.segments.length,2);
  await drag([28.284,28.284],[30,38]);
  e=(await state()).section.entities[0];
  assert.ok(e.segments.every(s=>s.kind==="circleArc"));
  await command("undo");await command("redo");
  assert.equal((await state()).section.entities[0].kind,"path");

  // Nodes on other curves are reachable without selecting each whole curve first.
  const pair=[
    {id:"left",kind:"circleArc",cx:-25,cy:0,radius:20,startAngle:Math.PI,sweep:-Math.PI,closed:false},
    {id:"right",kind:"circleArc",cx:25,cy:0,radius:20,startAngle:Math.PI,sweep:-Math.PI,closed:false},
  ];
  await reset(pair);
  assert.equal(await page.locator('[data-tube-sketch-grip-role="vertex"]').count(),4);
  await clickAt(-5,0);
  await page.keyboard.down("Shift");await clickAt(5,0);await page.keyboard.up("Shift");
  assert.equal((await state()).section.selectedPoints.length,2);
  await command("join");
  let d=(await state()).section;
  assert.equal(d.entities.length,1);assert.equal(d.entities[0].segments.length,2);
  assert.ok(d.entities[0].segments.every(s=>s.kind==="circleArc"));
  await command("undo");assert.equal((await state()).section.entities.length,2);
  await command("redo");assert.equal((await state()).section.entities.length,1);
  for(const initiallySelected of [true,false]) {
    await reset(pair);
    if(!initiallySelected)await page.evaluate(()=>{const d=window.check.view.tubeDesignerSketch.section;d.selectedId="";d.selectedIds=[];window.check.render();});
    await drag([-8,-3],[8,3]);
    assert.equal((await state()).section.selectedPoints.length,2,"box finds endpoints on both curves");
    await command("join");assert.equal((await state()).section.entities.length,1);
  }
  await reset(pair);
  await page.evaluate(()=>{const d=window.check.view.tubeDesignerSketch.section;d.selectedId="";d.selectedIds=[];window.check.render();});
  await drag([-50,-5],[50,25]);
  assert.equal((await state()).section.selectedIds.length,2);
  assert.equal((await state()).section.selectedPoints.length,0,"whole-object box selection remains available");

  const touching=[{id:"a",kind:"line",x1:-40,y1:0,x2:0,y2:0},{id:"b",kind:"line",x1:0,y1:0,x2:40,y2:20}];
  await reset(touching);await clickAt(0,0);
  await page.keyboard.down("Shift");await page.keyboard.down("Alt");await clickAt(0,0);
  await page.keyboard.up("Alt");await page.keyboard.up("Shift");
  assert.equal((await state()).section.selectedPoints.length,2,"overlapping endpoints on different curves can both be selected");
  await command("join");assert.equal((await state()).section.entities.length,1);

  // Drawing snaps to an existing endpoint and creates one connected editable curve.
  await reset({id:"base",kind:"line",x1:-40,y1:0,x2:0,y2:0});
  await page.evaluate(()=>{window.check.view.tubeDesignerSketch.snapEnabled=true;});
  await command("line");await clickAt(.6,.3);await clickAt(20,20);
  d=(await state()).section;
  assert.equal(d.entities.length,1);assert.equal(d.entities[0].kind,"polyline");
  assert.deepEqual(d.entities[0].points.slice(0,2),[[-40,0],[0,0]]);
  assert.ok(Math.hypot(d.entities[0].points[2][0]-20,d.entities[0].points[2][1]-20)<.001);
  await command("undo");assert.equal((await state()).section.entities[0].id,"base");
  await command("redo");assert.equal((await state()).section.entities[0].points.length,3);
  await command("line");await clickAt(20.4,20.3);await clickAt(-39.5,.2);
  assert.equal((await state()).section.entities.length,1);
  assert.equal((await state()).section.entities[0].closed,true,"snapping to the other end closes the contour");

  await reset({id:"base",kind:"line",x1:-40,y1:0,x2:0,y2:0});
  await page.evaluate(()=>{window.check.view.tubeDesignerSketch.snapEnabled=true;});
  await command("freehand");await drag([.4,.2],[30,20]);
  assert.equal((await state()).section.entities.length,1,"freehand also joins its snapped starting endpoint");

  await reset({id:"circle",kind:"circle",cx:0,cy:0,radius:40,closed:true});
  await page.evaluate(()=>{window.check.view.tubeDesignerSketch.snapEnabled=true;});
  await command("break");await clickAt(40,0);
  d=(await state()).section;
  assert.equal(d.selectedPoints.length,2);
  assert.equal(await page.locator('[data-tube-sketch-grip-role="vertex"]').count(),2);
  // Alt reaches the other coincident endpoint; dragging must not move both endpoints.
  await page.keyboard.down("Alt");await drag([40,0],[45,6]);await page.keyboard.up("Alt");
  d=(await state()).section;e=d.entities[0];
  assert.equal(e.kind,"path");assert.ok(e.segments.every(s=>s.kind==="circleArc"));
  assert.equal(e.brokenStart,true);assert.equal(e.brokenEnd,true);
  await drag([36,-3],[49,10]);
  assert.equal((await state()).section.selectedPoints.length,2,"box-select the two separated endpoints");
  await command("join");
  assert.equal((await state()).section.entities[0].closed,true);
  assert.equal(await page.evaluate(()=>window.check.sketch.analyzeSectionDraft(window.check.view.tubeDesignerSketch.section).ready),true);

  // Full ellipse insert works at its parameter seam too.
  await reset({id:"ellipse",kind:"ellipse",cx:0,cy:0,radiusX:55,radiusY:25,rotation:0,closed:true});
  await page.evaluate(()=>{window.check.view.tubeDesignerSketch.snapEnabled=true;});
  await command("insert-point");await clickAt(55,0);
  assert.equal((await state()).section.selectedPoints.length,1);
  assert.equal((await state()).section.entities[0].closed,true);
  await command("break");await clickAt(55,0);
  assert.equal((await state()).section.selectedPoints.length,2);
  await command("join");
  assert.equal((await state()).section.entities[0].kind,"ellipse");

  // Zoom around the cursor and pan must update axes and geometry together.
  const centerBefore=await page.locator('[data-tube-sketch-grip-role="move"]').boundingBox();
  await page.mouse.move(centerBefore.x+4,centerBefore.y+4);await page.mouse.wheel(0,-120);
  await page.waitForFunction(()=>window.check.view.tubeDesignerSketch.sectionViewport.zoom>1);
  const centerAfter=await page.locator('[data-tube-sketch-grip-role="move"]').boundingBox();
  assert.ok(Math.abs(centerBefore.x-centerAfter.x)<2&&Math.abs(centerBefore.y-centerAfter.y)<2);
  const center=await point(0,0);
  await page.mouse.move(...center);await page.mouse.down({button:"middle"});
  await page.mouse.move(center[0]+80,center[1]+35,{steps:10});await page.mouse.up({button:"middle"});
  const shifted=await point(0,0);
  assert.ok(Math.abs(shifted[0]-center[0]-80)<1);assert.ok(Math.abs(shifted[1]-center[1]-35)<1);
  assert.deepEqual(errors,[]);
  if(process.env.ICAX_SKETCH_SCREENSHOT) {
    mkdirSync(resolve(process.env.ICAX_SKETCH_SCREENSHOT,".."),{recursive:true});
    await page.screenshot({path:process.env.ICAX_SKETCH_SCREENSHOT});
  }
  console.log("Sketch browser interaction and canvas regression tests passed");
} finally { await browser.close(); }
