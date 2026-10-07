// Camera framing and gestures use the production WebGL viewport, actual
// raycaster, and real mouse events. The frame meshes are test geometry.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('designer-camera-fit');
const template = JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/product/single_face_security_window/template.json', import.meta.url), 'utf8'));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
const checks = [], errors = [];
let passed = false;
try {
  const page = await browser.newPage({ viewport: { width: 1140, height: 900 } });
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://designer-camera-fit.test/**', route => {
    if (new URL(route.request().url()).pathname === '/') return route.fulfill({
      contentType: 'text/html', body: '<!doctype html><meta charset="utf-8">' +
        '<style>body{margin:0}#viewport{width:840px;height:620px}#outside{height:180px}</style>' +
        '<div id="viewport"></div><div id="outside"></div>' +
        '<div id="left" style="position:absolute;left:860px;top:0;width:220px;height:300px;overflow:auto"><div style="height:1100px"><input id="left-input" value="left draft"></div></div>' +
        '<div id="right" style="position:absolute;left:860px;top:340px;width:220px;height:300px;overflow:auto"><div id="nested" style="height:200px;overflow:auto"><div style="height:900px"><input id="right-input" value="right draft"></div></div><div style="height:1100px"></div></div>',
    });
    return serveBrowserAsset(route);
  });
  await page.goto('http://designer-camera-fit.test/');
  await page.evaluate(async template => {
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const { fitDesignerDefaultView } = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const f = window.cameraFit = { picks: [], cameras: [], edits: 0, revision: 0 };
    f.fit = fitDesignerDefaultView;
    const viewport = f.viewport = createThreeViewport({ continuousRender: false, showGrid: false, blankDoubleClickFitEnabled: true,
      onCameraChange(state, detail) { f.cameras.push(detail.reason); },
      onPick(data, hit) { f.picks.push(data?.objectId ?? null); viewport.setSelectedObjectId(data?.objectId ?? null); },
    });
    viewport.mount(document.querySelector('#viewport'));
    f.view = { viewport, scene: { tubeDesigner: { templates: [template] } } };
    f.framing = () => {
      const bounds = viewport.getVisibleBounds(), points = [];
      for (const x of [bounds.min[0], bounds.max[0]]) for (const y of [bounds.min[1], bounds.max[1]])
        for (const z of [bounds.min[2], bounds.max[2]]) points.push(new THREE.Vector3(x, y, z).project(viewport.camera));
      return { left: Math.min(...points.map(p => p.x)), right: Math.max(...points.map(p => p.x)),
        bottom: Math.min(...points.map(p => p.y)), top: Math.max(...points.map(p => p.y)),
        depthVisible: points.every(p => p.z >= -1 && p.z <= 1), state: viewport.getCameraState(),
        selected: [...viewport.selectedObjectIds], picks: [...f.picks], cameras: [...f.cameras], edits: f.edits };
    };
    f.load = async ({ face, width, height, depth = 32, hostWidth = 840, hostHeight = 620, projection = 'perspective' }) => {
      viewport.clearSpecificationAnnotations();
      viewport.setSelectedObjectId(null);
      const host = document.querySelector('#viewport'); host.style.width = `${hostWidth}px`; host.style.height = `${hostHeight}px`; viewport.resize();
      viewport.setProjectionMode(projection);
      const rows = [], bar = (size, position) => {
        const geometry = new THREE.BoxGeometry(...size).translate(...position);
        const id = `frame.${rows.length}`;
        viewport.resourcePromises.set(`${id}@${f.revision + 1}`, Promise.resolve({ url: id, version: f.revision + 1, type: 'geometry', data: {
          kind: 'mesh', positions: [...geometry.attributes.position.array], indices: [...geometry.index.array], normals: [...geometry.attributes.normal.array],
        } }));
        rows.push({ entityId: id, data: { geometry: { url: id, version: f.revision + 1 }, flags: 3 } });
      };
      const front = y => {
        bar([32,32,height], [16,y,height/2]); bar([32,32,height], [width-16,y,height/2]);
        bar([width,32,32], [width/2,y,16]); bar([width,32,32], [width/2,y,height-16]);
        for (let x = 150; x < width-80; x += 180) bar([12,12,height-64], [x,y,height/2]);
      };
      const side = x => {
        bar([32,32,height], [x,depth-16,height/2]);
        bar([32,depth,32], [x,depth/2,16]); bar([32,depth,32], [x,depth/2,height-16]);
        for (let y = 150; y < depth-80; y += 180) bar([12,12,height-64], [x,y,height/2]);
      };
      front(0); if (face !== 'single') side(width-16); if (['three','five'].includes(face)) side(16);
      if (face === 'five') {
        bar([width,32,32], [width/2,depth-16,16]); bar([width,32,32], [width/2,depth-16,height-16]);
        for (const z of [16,height-16]) for (let x = 150; x < width-80; x += 180) bar([12,depth,12], [x,depth/2,z]);
      }
      const revision = String(++f.revision);
      await viewport.applyViewSnapshot({ revision, rows }, { get() { throw new Error('Missing test frame resource'); } });
      f.product = { templateId: template.id, parameters: { faceType: face, sidePosition: 'right' } };
      const receipt = fitDesignerDefaultView(f.view, revision, f.product);
      f.picks.length = 0; f.cameras.length = 0;
      return { receipt, actualWebGL: viewport.renderer.getContext() instanceof WebGL2RenderingContext, ...f.framing() };
    };
    f.point = ({ blank = false, model = false, avoidLabels = true } = {}) => {
      const canvas = viewport.renderer.domElement, rect = canvas.getBoundingClientRect(), raycaster = new THREE.Raycaster();
      for (let y = 30; y < rect.height-110; y += 17) for (let x = 30; x < rect.width-30; x += 17) {
        const client = { x: rect.left+x, y: rect.top+y };
        if (document.elementFromPoint(client.x,client.y) !== canvas) continue;
        if (avoidLabels && viewport.specificationLabels.some(({ element }) => {
          const b = element.getBoundingClientRect(); return !element.hidden && client.x >= b.left && client.x <= b.right && client.y >= b.top && client.y <= b.bottom;
        })) continue;
        raycaster.setFromCamera(new THREE.Vector2(x/rect.width*2-1,1-y/rect.height*2),viewport.camera);
        const hits = raycaster.intersectObjects([...viewport.sceneObjects.values()].filter(o => o.visible),false);
        if ((blank && !hits.length) || (model && hits.length)) return { ...client, hit: hits[0]?.object.userData.objectId ?? null };
      }
      throw new Error('No requested pointer target');
    };
    viewport.specificationLabelLayer.addEventListener('click', event => {
      if (!event.target.matches('.icax-three-specification-trigger')) return;
      f.edits++;
    });
  }, template);
  const samples = [
    { name:'single', face:'single', width:1200,height:1800 },
    { name:'wide-portrait', face:'single', width:4800,height:650,hostWidth:420,hostHeight:760 },
    { name:'tall', face:'single', width:650,height:3600 },
    { name:'two', face:'two', width:1400,height:2000,depth:800 },
    { name:'three', face:'three', width:1400,height:1600,depth:1100 },
    { name:'five', face:'five', width:1800,height:2200,depth:1400 },
    { name:'single-orthographic', face:'single', width:1200,height:1800,projection:'orthographic' },
    { name:'five-orthographic', face:'five', width:1800,height:2200,depth:1400,projection:'orthographic' },
  ];
  for (const sample of samples) {
    const result = await page.evaluate(sample => window.cameraFit.load(sample),sample);
    assert.equal(result.actualWebGL,true);
    assert.equal(result.depthVisible,true);
    assert(Math.max(Math.abs(result.left),Math.abs(result.right),Math.abs(result.bottom),Math.abs(result.top)) <= 1/1.35+1e-6,
      `${sample.name}: complete frame must retain at least 13 percent margin on each limiting edge`);
    assert(Math.max(result.right-result.left,result.top-result.bottom) > 1.0, `${sample.name}: model must remain comfortably visible`);
    assert.equal(result.receipt.defaultViewRenderSequence,result.receipt.fitRenderSequence);
    checks.push({ name:`default framing ${sample.name}`, sample, result });
    await page.locator('#viewport').screenshot({ path:resolve(output,`${sample.name}.png`) });
  }
  await page.evaluate(sample => window.cameraFit.load(sample),samples[0]);
  const preserved = await page.evaluate(async () => {
    const f = window.cameraFit, viewport = f.viewport, host = document.querySelector('#viewport');
    const input = document.querySelector('#right-input'), left = document.querySelector('#left'), right = document.querySelector('#right'), nested = document.querySelector('#nested');
    const nodes = [left,right,nested,input,viewport.renderer.domElement];
    input.focus({ preventScroll:true }); input.setSelectionRange(2,7);
    left.scrollTop=125;right.scrollTop=85;nested.scrollTop=35;
    let signal; const response = new Promise(done => signal=done);
    const apply = (async () => { await response; return f.fit(f.view,String(f.revision),f.product); })();
    // Scroll again while the response is pending. Fitting must preserve the
    // latest sidebar interaction and use the new layout before ResizeObserver.
    left.scrollTop=240;right.scrollTop=160;nested.scrollTop=65;
    host.style.width='510px';
    signal(); await apply;
    return { focus:document.activeElement === input, selection:[input.selectionStart,input.selectionEnd],
      scrolls:[left.scrollTop,right.scrollTop,nested.scrollTop],
      nodesRetained:nodes.every(node => node.isConnected), aspect:viewport.viewportAspect,
      bounds:f.framing() };
  });
  assert.equal(preserved.focus,true);assert.deepEqual(preserved.selection,[2,7]);assert.deepEqual(preserved.scrolls,[240,160,65]);
  assert.equal(preserved.nodesRetained,true);assert.equal(preserved.aspect,510/620);
  assert(Math.max(Math.abs(preserved.bounds.left),Math.abs(preserved.bounds.right),Math.abs(preserved.bounds.bottom),Math.abs(preserved.bounds.top)) <= 1/1.35+1e-6);
  checks.push({ name:'fitting uses current layout and preserves latest sidebar interaction',result:preserved });
  await page.evaluate(sample => window.cameraFit.load(sample),samples[0]);
  const model = await page.evaluate(() => window.cameraFit.point({ model:true }));
  await page.mouse.click(model.x,model.y);
  let state = await page.evaluate(() => window.cameraFit.framing());
  assert.deepEqual(state.selected,[model.hit]);
  const selected = state.selected, fitted = state.state;
  await page.mouse.move(model.x,model.y); await page.mouse.wheel(0,-380);
  const close = await page.evaluate(() => window.cameraFit.framing());
  assert(close.state.radius < fitted.radius);
  const blank = await page.evaluate(() => window.cameraFit.point({ blank:true }));
  await page.mouse.dblclick(blank.x,blank.y,{ delay:50 });
  await page.waitForTimeout(550);
  state = await page.evaluate(() => window.cameraFit.framing());
  assert(Math.abs(state.state.radius-fitted.radius)<1e-8);
  assert.deepEqual(state.selected,selected,'Blank double click preserves component selection');
  assert.deepEqual(state.picks,[model.hit]);
  assert.equal(state.cameras.filter(reason => reason === 'fit-viewport').length,1);
  checks.push({ name:'blank double click fits without selecting or clearing',result:state });
  const modelAgain = await page.evaluate(() => window.cameraFit.point({ model:true }));
  const beforeModelDouble = state.state;
  await page.mouse.dblclick(modelAgain.x,modelAgain.y,{ delay:50 });
  state = await page.evaluate(() => window.cameraFit.framing());
  assert.deepEqual(state.state,beforeModelDouble,'Double clicking a real mesh must not fit');
  assert.equal(state.cameras.filter(reason => reason === 'fit-viewport').length,1);
  assert.deepEqual(state.selected,[modelAgain.hit]);
  checks.push({ name:'model double click keeps view',result:state });
  const labelPoint = await page.evaluate(() => {
    const f = window.cameraFit;
    f.viewport.setSpecificationAnnotations([{ id:'width',parameter:'width',start:[300,-40,900],end:[900,-40,900],label:'正面宽度 1,200 mm' }]);
    const b = document.querySelector('.icax-three-specification-label').getBoundingClientRect();
    return { x:(b.left+b.right)/2,y:(b.top+b.bottom)/2 };
  });
  const beforeAnnotation = state;
  await page.mouse.dblclick(labelPoint.x,labelPoint.y,{ delay:50 });
  await page.waitForTimeout(550);
  state = await page.evaluate(() => window.cameraFit.framing());
  assert.equal(state.edits,1);
  assert.deepEqual(state.state,beforeAnnotation.state);
  assert.deepEqual(state.picks,beforeAnnotation.picks);
  assert.deepEqual(state.selected,beforeAnnotation.selected);
  checks.push({ name:'annotation double click edits without fit or selection change',result:state });
  await page.evaluate(() => window.cameraFit.viewport.setSpecificationAnnotations([
    { id:'width',parameter:'width',start:[300,-40,900],end:[900,-40,900],label:'正面宽度 1,200 mm',editable:false },
  ]));
  await page.mouse.dblclick(labelPoint.x,labelPoint.y,{ delay:50 });
  await page.waitForTimeout(550);
  state = await page.evaluate(() => window.cameraFit.framing());
  assert.equal(state.edits,1);assert.deepEqual(state.state,beforeAnnotation.state);assert.deepEqual(state.picks,beforeAnnotation.picks);
  checks.push({ name:'read only annotation double click does not fit',result:state });
  const axisPoint = await page.locator('.icax-three-axis-gizmo').boundingBox();
  await page.mouse.dblclick(axisPoint.x+20,axisPoint.y+20,{ delay:50 });
  await page.waitForTimeout(550);
  state = await page.evaluate(() => window.cameraFit.framing());
  assert.deepEqual(state.state,beforeAnnotation.state); assert.deepEqual(state.picks,beforeAnnotation.picks);
  checks.push({ name:'axis overlay double click is not blank scene',result:state });

  await page.evaluate(() => window.cameraFit.viewport.clearSpecificationAnnotations());
  for (const kind of ['sprite', 'dimension-line', 'specification-line', 'outside-label', 'outside-leader', 'specification-leader']) {
    const graphicPoint = await page.evaluate(async kind => {
      const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
      const f = window.cameraFit, viewport = f.viewport, rect = viewport.renderer.domElement.getBoundingClientRect();
      viewport.clearDimensionAnnotations(); viewport.clearSpecificationAnnotations();
      const blank = f.point({ blank:true });
      const raycaster = new THREE.Raycaster();
      raycaster.setFromCamera(new THREE.Vector2((blank.x-rect.left)/rect.width*2-1,
        1-(blank.y-rect.top)/rect.height*2),viewport.camera);
      const normal = viewport.camera.getWorldDirection(new THREE.Vector3());
      const anchor = raycaster.ray.intersectPlane(new THREE.Plane().setFromNormalAndCoplanarPoint(normal,viewport.cameraState.target),new THREE.Vector3());
      const right = new THREE.Vector3(1,0,0).applyQuaternion(viewport.camera.quaternion);
      const start = anchor.clone().addScaledVector(right,-70), end = anchor.clone().addScaledVector(right,70);
      const annotation = {id:kind,start:start.toArray(),end:end.toArray(),label:kind==='dimension-line'||kind==='specification-line'?'':'尺寸示例'};
      if (kind === 'specification-line') viewport.setSpecificationAnnotations([annotation]);
      else if (kind === 'specification-leader') viewport.setSpecificationAnnotations([{...annotation,kind:'parameter',parameter:'fixture',editable:false}]);
      else viewport.setDimensionAnnotations([{...annotation,placement:kind.startsWith('outside')?'outside':undefined}]);
      let point = {x:blank.x,y:blank.y};
      if (kind==='outside-label') {
        const b=viewport.outsideDimensionLabels[0].element.getBoundingClientRect();
        point={x:(b.left+b.right)/2,y:(b.top+b.bottom)/2};
      } else if (kind==='outside-leader'||kind==='specification-leader') {
        const path=(kind==='outside-leader'?viewport.dimensionLeaderSvg:viewport.specificationLeaderSvg).querySelector('path,line');
        const local=path.getPointAtLength(path.getTotalLength()*0.37),screen=new DOMPoint(local.x,local.y).matrixTransform(path.getScreenCTM());
        point={x:screen.x,y:screen.y};
      }
      return {...point,actualTargetIsCanvas:document.elementFromPoint(point.x,point.y)===viewport.renderer.domElement};
    },kind);
    assert.equal(graphicPoint.actualTargetIsCanvas,true,`${kind}: passive graphics use the actual canvas beneath`);
    const beforeGraphic = await page.evaluate(() => window.cameraFit.framing());
    await page.mouse.dblclick(graphicPoint.x,graphicPoint.y,{delay:50});
    await page.waitForTimeout(550);
    state = await page.evaluate(() => window.cameraFit.framing());
    assert.deepEqual(state.state,beforeGraphic.state,`${kind}: annotation graphics cannot trigger blank fitting`);
    assert.equal(state.cameras.filter(reason=>reason==='fit-viewport').length,beforeGraphic.cameras.filter(reason=>reason==='fit-viewport').length);
    checks.push({name:`${kind} double click does not fit`,point:graphicPoint,result:state});
  }
  await page.evaluate(() => { window.cameraFit.viewport.clearDimensionAnnotations(); window.cameraFit.viewport.clearSpecificationAnnotations(); });
  await page.evaluate(() => {
    const viewport=window.cameraFit.viewport;
    viewport.setProjectionToggleVisible(true);
    const cube=document.createElement('div');cube.className='cam-viewcube';cube.id='fixture-cube';
    cube.style.cssText='position:absolute;right:24px;top:22px;width:90px;height:90px;pointer-events:none;background:#b8812344';
    viewport.root.appendChild(cube);
    const toolbar=document.createElement('button');toolbar.id='fixture-tool';toolbar.textContent='场景工具';
    toolbar.style.cssText='position:absolute;left:15px;top:15px;z-index:6';viewport.root.appendChild(toolbar);
  });
  for (const selector of ['#fixture-cube','#fixture-tool','.icax-three-projection-toggle button']) {
    const box=await page.locator(selector).first().boundingBox();
    const beforeOverlay=await page.evaluate(()=>window.cameraFit.framing());
    await page.mouse.dblclick(box.x+box.width/2,box.y+box.height/2,{delay:50});
    await page.waitForTimeout(550);
    state=await page.evaluate(()=>window.cameraFit.framing());
    assert.equal(state.cameras.filter(reason=>reason==='fit-viewport').length,beforeOverlay.cameras.filter(reason=>reason==='fit-viewport').length);
    assert.deepEqual(state.picks,beforeOverlay.picks);
    checks.push({name:`${selector} overlay double click does not fit`,result:state});
  }
  await page.evaluate(()=>{document.querySelector('#fixture-cube').remove();document.querySelector('#fixture-tool').remove();window.cameraFit.viewport.setProjectionToggleVisible(false);});

  for (const projection of ['perspective','orthographic']) for (const pickingEnabled of [true,false]) {
    await page.evaluate(async ({projection,pickingEnabled})=>{
      const f=window.cameraFit,viewport=f.viewport;
      await f.load({face:'three',width:1400,height:1600,depth:1100,projection});
      viewport.setPickingEnabled(pickingEnabled);
      const camera=viewport.getCameraState();
      viewport.setCameraState({...camera,theta:camera.theta+0.27,phi:camera.phi+0.13});
      viewport.fitViewToViewport(1.35);f.expectedFit=viewport.getCameraState();
      viewport.setSelectedObjectId('frame.0');
      viewport.setCameraState({...f.expectedFit,radius:f.expectedFit.radius*0.58,
        target:{x:f.expectedFit.target.x+120,y:f.expectedFit.target.y-70,z:f.expectedFit.target.z+90}});
      f.picks.length=0;f.cameras.length=0;f.originalCanvas=viewport.renderer.domElement;
      f.canvasSignals=0;f.canvasListener=()=>f.canvasSignals++;f.originalCanvas.addEventListener('camera-test-signal',f.canvasListener);
    },{projection,pickingEnabled});
    const point=await page.evaluate(()=>window.cameraFit.point({blank:true}));
    const beforeNonLeft=await page.evaluate(()=>window.cameraFit.framing());
    for (const button of ['right','middle']) {
      await page.mouse.dblclick(point.x,point.y,{button,delay:50});
      state=await page.evaluate(()=>window.cameraFit.framing());
      assert.deepEqual(state.state,beforeNonLeft.state,`${projection}/${pickingEnabled}: ${button} cannot fit`);
      assert.deepEqual(state.picks,[]);
    }
    await page.mouse.dblclick(point.x,point.y,{delay:50});
    await page.waitForTimeout(550);
    const result=await page.evaluate(()=>{
      const f=window.cameraFit;f.originalCanvas.dispatchEvent(new Event('camera-test-signal'));
      return {...f.framing(),expected:f.expectedFit,canvasRetained:f.originalCanvas===f.viewport.renderer.domElement&&f.originalCanvas.isConnected,listenerRetained:f.canvasSignals===1};
    });
    assert.equal(result.state.theta,result.expected.theta);assert.equal(result.state.phi,result.expected.phi);
    assert.equal(result.state.projectionMode,result.expected.projectionMode);assert.deepEqual(result.state.target,result.expected.target);
    assert(Math.abs(result.state.radius-result.expected.radius)<1e-8,`${projection}/${pickingEnabled}: fit preserves user's orbit and resets only target/distance`);
    assert.deepEqual(result.selected,['frame.0']);assert.deepEqual(result.picks,[]);
    assert.equal(result.cameras.filter(reason=>reason==='fit-viewport').length,1);
    assert.equal(result.canvasRetained,true);assert.equal(result.listenerRetained,true);
    assert.equal(result.depthVisible,true);
    assert(Math.max(Math.abs(result.left),Math.abs(result.right),Math.abs(result.bottom),Math.abs(result.top))<=1/1.35+1e-6);
    const meshPoint=await page.evaluate(()=>window.cameraFit.point({model:true}));
    await page.mouse.dblclick(meshPoint.x,meshPoint.y,{delay:50});
    state=await page.evaluate(()=>window.cameraFit.framing());
    assert.deepEqual(state.state,result.state,`${projection}/${pickingEnabled}: actual model hit blocks fit independently of picking`);
    assert.equal(state.cameras.filter(reason=>reason==='fit-viewport').length,1);
    checks.push({name:`${projection}, picking=${pickingEnabled}: blank fits; right/middle/model do not`,result});
    await page.evaluate(()=>window.cameraFit.originalCanvas.removeEventListener('camera-test-signal',window.cameraFit.canvasListener));
  }
  await page.evaluate(async sample=>{await window.cameraFit.load(sample);window.cameraFit.viewport.setPickingEnabled(true);},samples[0]);
  state=await page.evaluate(()=>window.cameraFit.framing());
  const beforeBlankSingle=state.state;
  const blankSingle = await page.evaluate(() => window.cameraFit.point({ blank:true }));
  await page.mouse.click(blankSingle.x,blankSingle.y);
  await page.waitForFunction(() => window.cameraFit.picks.at(-1) === null);
  state = await page.evaluate(() => window.cameraFit.framing());
  assert.deepEqual(state.selected,[]); assert.deepEqual(state.state,beforeBlankSingle);
  checks.push({ name:'blank single click still clears selection',result:state });
  await page.mouse.click(modelAgain.x,modelAgain.y);
  const beforeDrag = await page.evaluate(() => window.cameraFit.framing());
  await page.mouse.move(blankSingle.x,blankSingle.y); await page.mouse.down();
  await page.mouse.move(blankSingle.x+12,blankSingle.y+12,{ steps:4 }); await page.mouse.up();
  await page.waitForTimeout(550);
  state = await page.evaluate(() => window.cameraFit.framing());
  assert.deepEqual(state.picks,beforeDrag.picks); assert.deepEqual(state.selected,beforeDrag.selected);
  checks.push({ name:'blank drag does not emit delayed selection clearing',result:state });
  await page.evaluate(() => window.cameraFit.viewport.setBlankDoubleClickFitEnabled(false));
  const beforeDisabled = await page.evaluate(() => window.cameraFit.framing());
  await page.mouse.dblclick(blankSingle.x,blankSingle.y,{ delay:50 });
  state = await page.evaluate(() => window.cameraFit.framing());
  assert.deepEqual(state.state,beforeDisabled.state);assert.deepEqual(state.picks.slice(-2),[null,null]);
  checks.push({ name:'explicit SDK opt-out retains immediate blank picks and does not fit',result:state });
  await page.evaluate(() => window.cameraFit.viewport.setBlankDoubleClickFitEnabled(true));
  await page.mouse.click(blankSingle.x,blankSingle.y);
  await page.evaluate(() => window.cameraFit.viewport.dispose());
  await page.waitForTimeout(550);
  const disposed = await page.evaluate(() => ({ pending:window.cameraFit.viewport.pendingBlankPick,picks:window.cameraFit.picks }));
  assert.equal(disposed.pending,null); assert.deepEqual(disposed.picks,state.picks);
  checks.push({ name:'dispose cancels delayed blank selection',result:disposed });
  const empty=await page.evaluate(()=>{
    const f=window.cameraFit,viewport=f.viewport;
    // Reuse the existing host with a fresh, empty production viewport.
    const Constructor=viewport.constructor;
    f.emptyViewport=new Constructor({continuousRender:false,showGrid:false,pickingEnabled:false,blankDoubleClickFitEnabled:true,
      onCameraChange(camera,detail){f.emptyCameraChanges??=[];f.emptyCameraChanges.push(detail.reason);}});
    f.emptyViewport.mount(document.querySelector('#viewport'));
    return {before:f.emptyViewport.getCameraState(),canvas:!!f.emptyViewport.renderer.domElement};
  });
  const emptyCanvas=await page.locator('#viewport .icax-three-viewport-canvas').boundingBox();
  await page.mouse.dblclick(emptyCanvas.x+emptyCanvas.width/2,emptyCanvas.y+emptyCanvas.height/2,{delay:50});
  const emptyAfter=await page.evaluate(()=>({camera:window.cameraFit.emptyViewport.getCameraState(),changes:window.cameraFit.emptyCameraChanges??[]}));
  assert.deepEqual(emptyAfter.camera,empty.before);assert.deepEqual(emptyAfter.changes,[]);
  checks.push({name:'empty scene double click is safe and keeps camera',result:emptyAfter});
  await page.evaluate(()=>window.cameraFit.emptyViewport.dispose());
  assert.deepEqual(errors,[]);
  passed = true;
  console.log(`PASS ${checks.length} default camera and blank double-click checks with actual WebGL and real mouse events`);
} catch (error) { errors.push(error.stack || String(error)); throw error; }
finally {
  await browser.close();
  writeFileSync(resolve(output,'report.json'),JSON.stringify({ status:passed?'passed':'failed',
    actualWebGL:true,realMouseEvents:true,actualProductionRaycaster:true,
    geometry:'test frame meshes spanning wide, tall, single, two, three, and five-sided products',
    nativeTransport:'not needed for camera interaction regression',standaloneCefEndToEnd:false,checks,errors },null,2));
}
