// Production nesting viewport and real WebGL. Native geometry transport is controlled;
// automatic measurement must not be requested even when a previous inspection is cached.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('nesting-part-viewport-20261007');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 980 } });
  page.setDefaultTimeout(15000);
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://part-viewport.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body></body>' });
    if (!pathname.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: pathname.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(browserAssetPath(pathname.slice('/src/'.length)), 'utf8') });
  });
  await page.goto('http://part-viewport.test/');
  await page.evaluate(async () => {
    const area = await import('/src/apps/tube-designer/webpage/partsArea.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const { encodeNestingGeometry } = await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    const { buildAutomaticDimensionReport } = await import('/src/apps/tube-designer/webpage/partInspection.mjs');
    const { patchParameterContent } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const { tubeDesignerCss } = await import('/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
    document.head.innerHTML = `<link rel="stylesheet" href="/src/apps/_shared/workbench/styles/laser3dcam.css"><style>${tubeDesignerCss}
      *{box-sizing:border-box}body.tube-designer-workspace{display:block;width:100%;height:100vh;margin:0;font:14px Arial,"Microsoft Yahei",sans-serif;background:#eff4f5}
      #workspace{display:grid;grid-template-columns:240px minmax(0,1fr) 350px;height:100vh}
      #stage{position:relative;min-width:0;min-height:0}#scene,#overlay{position:absolute;inset:0}#overlay{pointer-events:none}
      #left,#stage,#right{grid-area:auto;width:auto}#left,#right{display:block;min-height:0;overflow:auto}
      #left{padding:15px}#left .nested{height:150px;overflow:auto}#left .spacer{height:1500px}
      #right .tube-designer-cutting-inspector{height:auto}#right .tube-designer-cutting-inspector-scroll{height:auto;overflow:visible}
      #right::after{content:"";display:block;height:1500px}
    </style>`;
    document.body.classList.add('tube-designer-workspace');
    document.body.innerHTML = '<div id="workspace"><aside id="left" class="cam-context-pane"><input name="search" value="搜索草稿"><div class="nested"><div class="spacer"></div></div><div class="spacer"></div></aside><main id="stage" class="cam-viewport"><div id="scene"></div><div id="overlay"></div></main><aside id="right" class="cam-info-pane"></aside></div>';
    const part = (id, process) => ({ entityId:id, name:'带槽口的零件', length:2320, quantity:1,
      profile:{ sectionIdentity:JSON.stringify({schema:'icax.nesting-section.v1',contours:['{"edges":["rect-38-t1"]}']}),
        kind:'rect',width:38,depth:38,wallThickness:1,displayName:'方管',specification:'38 × 38 × 1' },
      thumbnailGeometryResourceId:`icax-resource://${id}`,thumbnailGeometryResourceVersion:1,
      manufacturingGeometryResourceId:`icax-final://${id}`,manufacturingGeometryResourceVersion:1,
      properties:{'manufacturing.partKind':'tube','manufacturing.imported':process==='imported',
        'manufacturing.process':process,'manufacturing.geometryMeasurement':{openingCount:34}} });
    const shape = new THREE.Shape();
    [[0,-19],[2320,-19],[2320,19],[1350,19],[1350,0],[1000,0],[1000,19],[0,19]].forEach(([x,y],i)=>i?shape.lineTo(x,y):shape.moveTo(x,y));
    shape.closePath();
    const geometry = new THREE.ExtrudeGeometry(shape,{depth:38,bevelEnabled:false});
    geometry.rotateX(Math.PI/2);
    const bytes = encodeNestingGeometry({positions:[...geometry.attributes.position.array],indices:Array.from({length:geometry.attributes.position.count},(_,i)=>i)});
    geometry.dispose();
    const parts=[part('slot-1','imported'),part('slot-2','part-drawing'),part('slot-3','punch')];
    const view={activeAreaId:'nesting',tubeDesignerNestingSelectionKind:'part',tubeDesignerActivePartId:parts[0].entityId,
      scene:{tubeDesigner:{nestingGroups:[{name:'零件',parts}]}}};
    // Seed a legitimate old inspection report to verify it cannot leak into nesting.
    const report=buildAutomaticDimensionReport({geometryMeasurement:{available:true,source:'final-brep',length:2320,
      section:{shape:'rectangle',width:38,height:38,wallThickness:1},
      linearReference:{start:[0,0,0],end:[2320,0,0],sectionAxes:[[0,1,0],[0,0,1]],dimensionOffsetDirection:[0,0,1]},
      features:[{kind:'through-opening',station:200,center:[200,0,19],shape:'circle',diameter:20,openingSpanAlong:20,openingSpanAcross:20}]}});
    view.tubeDesignerPartMeasurementState={key:'previous-inspection',status:'ready',report};
    const counters={reads:0,measures:0,renders:0,inputEvents:0,canvasEvents:0};
    const pending=new Map();
    const context={mount:document.body,sceneProxy:{resources:{async get(url){counters.reads++;await new Promise(resolve=>pending.set(String(url),resolve));return new Response(bytes);}},
      async invoke(method){counters.measures++;throw Error('Nesting must not request '+method);}}};
    const render=()=>{
      document.querySelector('#overlay').innerHTML=area.renderNestingViewportOverlay(context,view);
      patchParameterContent(document.body,document.querySelector('#right'),area.renderNestingRightPane(context,view));
    };
    const ops={renderProject(){counters.renders++;render();}};
    view.viewport=createThreeViewport({continuousRender:false,pickingEnabled:true,blankDoubleClickFitEnabled:true,
      onPick(userData,hit,event){area.handlePartsAreaViewportPick(context,view,userData,hit,event);}});
    view.viewport.mount(document.querySelector('#scene'));
    window.fixture={area,view,context,counters,render,ops,pending,release(id){pending.get(`icax-resource://${id}`)?.();}};
    render();
    const nodes={canvas:view.viewport.renderer.domElement,left:document.querySelector('#left'),nested:document.querySelector('#left .nested'),right:document.querySelector('#right'),name:document.querySelector('[data-tube-designer-part-field="name"]')};
    nodes.name.addEventListener('fixture-input',()=>counters.inputEvents++);
    nodes.canvas.addEventListener('fixture-canvas',()=>counters.canvasEvents++);
    fixture.nodes=nodes;
  });
  await page.waitForFunction(()=>fixture.counters.reads===1);
  await page.evaluate(()=>{
    const f=fixture,n=f.nodes;n.left.scrollTop=65;n.nested.scrollTop=80;n.right.scrollTop=55;
    n.name.focus({preventScroll:true});n.name.value='加载中继续输入';n.name.setSelectionRange(2,6,'backward');
    n.left.scrollTop=105;n.nested.scrollTop=145;n.right.scrollTop=95;
    f.release('slot-1');
  });
  await page.waitForFunction(()=>fixture.view.viewport.getVisibleBounds()&&!fixture.view.tubeDesignerPartProgress);
  const first=await page.evaluate(()=>{
    const f=fixture,n=f.nodes;n.name.dispatchEvent(new Event('fixture-input'));n.canvas.dispatchEvent(new Event('fixture-canvas'));
    return {retained:{canvas:n.canvas===f.view.viewport.renderer.domElement,name:n.name===document.querySelector('[data-tube-designer-part-field="name"]'),focused:document.activeElement===n.name,
      value:n.name.value,selection:[n.name.selectionStart,n.name.selectionEnd,n.name.selectionDirection],scroll:[n.left.scrollTop,n.nested.scrollTop,n.right.scrollTop]},
      counters:{...f.counters},debug:f.view.viewport.getDebugState(),bounds:f.view.viewport.getVisibleBounds(),
      fields:[...document.querySelectorAll('[data-tube-designer-part-field]')].map(n=>n.dataset.tubeDesignerPartField)};
  });
  assert.deepEqual(first.retained,{canvas:true,name:true,focused:true,value:'加载中继续输入',selection:[2,6,'backward'],scroll:[105,145,95]});
  assert.equal(first.counters.measures,0);assert.equal(first.counters.inputEvents,1);assert.equal(first.counters.canvasEvents,1);
  assert.equal(first.debug.dimensionAnnotationCount,0);assert(first.bounds);
  assert.deepEqual(first.fields,['name','material','quantity','nestingPriority']);
  for (const selector of ['[data-tube-designer-part-dimension-tree]','[data-tube-designer-part-measurement]','[data-tube-inspection-element-row]'])
    assert.equal(await page.locator(selector).count(),0,selector);
  const cached=await page.evaluate(async()=>{
    const f=fixture,pose=f.view.viewport.getCameraState();f.view.viewport.setCameraState({...pose,theta:pose.theta+0.4});
    const before=f.view.viewport.getCameraState();f.render();await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    return {before,after:f.view.viewport.getCameraState(),reads:f.counters.reads,measures:f.counters.measures};
  });
  assert.deepEqual(cached.after,cached.before);assert.equal(cached.reads,1);assert.equal(cached.measures,0);
  // Switching while a slow resource is loading must cancel it; it cannot replace the newer part.
  await page.evaluate(()=>fixture.area.handlePartsAreaAction(fixture.context,fixture.view,'tube-designer-parts-select-part',{dataset:{tubeDesignerPartId:'slot-2'}},fixture.ops));
  await page.waitForFunction(()=>fixture.counters.reads===2);
  await page.evaluate(()=>fixture.area.handlePartsAreaAction(fixture.context,fixture.view,'tube-designer-parts-select-part',{dataset:{tubeDesignerPartId:'slot-3'}},fixture.ops));
  await page.waitForFunction(()=>fixture.counters.reads===3);
  await page.evaluate(()=>fixture.release('slot-3'));
  await page.waitForFunction(()=>fixture.view.tubeDesignerPartViewportKey.startsWith('slot-3@')&&!fixture.view.tubeDesignerPartProgress);
  await page.evaluate(()=>fixture.release('slot-2'));
  await page.waitForFunction(()=>!fixture.view.viewport.getDebugState().snapshotLoading);
  const final=await page.evaluate(async()=>{
    const f=fixture;await f.area.handlePartsAreaAction(f.context,f.view,'tube-designer-parts-toggle-dimensions',{type:'checkbox',checked:true},f.ops);
    const pick=f.area.handlePartsAreaViewportPick(f.context,f.view,{objectId:'slot-3'},true,{clientX:800,clientY:500});
    return {counters:{...f.counters},key:f.view.tubeDesignerPartViewportKey,selected:f.view.tubeDesignerActiveNestingPartId,
      annotations:f.view.viewport.getDebugState().dimensionAnnotationCount,pick,
      fields:[...document.querySelectorAll('[data-tube-designer-part-field]')].map(n=>n.dataset.tubeDesignerPartField)};
  });
  assert.equal(final.counters.measures,0);assert.equal(final.annotations,0);assert.equal(final.pick,false);
  assert.equal(final.selected,'slot-3');assert(final.key.startsWith('slot-3@'));
  assert.deepEqual(errors,[]);
  await page.evaluate(()=>{document.querySelector('#left').scrollTop=0;document.querySelector('#right').scrollTop=0;});
  await page.screenshot({path:resolve(output,'nesting-part-no-annotations.png')});
  await page.evaluate(()=>{
    const part=fixture.view.scene.tubeDesigner.nestingGroups[0].parts[2];
    part.thumbnailGeometryResourceId='';fixture.render();
  });
  await page.waitForFunction(()=>fixture.view.tubeDesignerPartProgress?.phase==='error');
  const failed=page.locator('[data-tube-designer-part-progress]');
  assert.equal(await failed.isVisible(),true);
  assert.equal(await failed.getAttribute('aria-busy'),'false');
  assert.equal(await failed.locator('[role="progressbar"]').isVisible(),false);
  assert.match(await failed.innerText(),/三维模型加载失败/);
  assert.equal(await page.evaluate(()=>fixture.counters.measures),0);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,first,cached,final,errors,geometryFailureVisible:true,transport:'controlled resource transport; production DOM and WebGL'},null,2));
  console.log(JSON.stringify({passed:true,automaticMeasurements:0,annotations:0,retainedInteraction:true,staleGeometryRejected:true}));
} finally {await browser.close();}
