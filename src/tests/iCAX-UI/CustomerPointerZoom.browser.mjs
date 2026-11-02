// Camera framing and gestures use the production WebGL viewport, actual
// raycaster, and real mouse events. The frame meshes are test geometry.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('customer-pointer-zoom');
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
  const samples=[{face:'single',width:1200,height:1800}];
  for (const projection of ['perspective','orthographic']) {
    await page.evaluate(sample => window.cameraFit.load(sample),{...samples[0],projection});
    for (const location of ['model','blank']) {
      const anchor = await page.evaluate(async location => {
        const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
        const f=window.cameraFit,v=f.viewport,point=f.point({[location]:true});
        v.options.zoomToPointer=true;
        const rect=v.renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster();
        ray.setFromCamera(new THREE.Vector2((point.x-rect.left)/rect.width*2-1,1-(point.y-rect.top)/rect.height*2),v.camera);
        const hit=ray.intersectObjects([...v.sceneObjects.values()].filter(o=>o.visible),false)[0];
        const plane=new THREE.Plane().setFromNormalAndCoplanarPoint(v.camera.getWorldDirection(new THREE.Vector3()),hit?.point??v.cameraState.target);
        f.zoomAnchor=ray.ray.intersectPlane(plane,new THREE.Vector3());
        return point;
      },location);
      await page.mouse.move(anchor.x,anchor.y);
      for (const delta of [-120,-120,120,120]) {
        await page.mouse.wheel(0,delta);await page.waitForTimeout(40);
        const drift=await page.evaluate(anchor=>{
          const f=window.cameraFit,v=f.viewport,r=v.renderer.domElement.getBoundingClientRect();
          const p=f.zoomAnchor.clone().project(v.camera);
          return Math.hypot(r.left+(p.x*.5+.5)*r.width-anchor.x,r.top+(-p.y*.5+.5)*r.height-anchor.y);
        },anchor);
        assert(drift<0.1,`${projection} ${location} cursor anchor drift ${drift}`);
        checks.push({name:`pointer zoom ${projection} ${location}`,delta,drift});
      }
    }
  }
  await page.evaluate(sample => window.cameraFit.load(sample),samples[0]);
  assert.deepEqual(errors,[]);passed=true;console.log('PASS cursor anchored perspective/orthographic zoom, model and blank, 16 real wheel events');
} catch (error) { errors.push(error.stack || String(error)); throw error; }
finally {
  await browser.close();
  writeFileSync(resolve(output,'report.json'),JSON.stringify({ status:passed?'passed':'failed',
    actualWebGL:true,realMouseEvents:true,actualProductionRaycaster:true,
    geometry:'test frame meshes spanning wide, tall, single, two, three, and five-sided products',
    nativeTransport:'not needed for camera interaction regression',standaloneCefEndToEnd:false,checks,errors },null,2));
}
