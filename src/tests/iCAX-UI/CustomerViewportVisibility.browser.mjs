import assert from 'node:assert/strict';
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve,sep} from 'node:path';
import {fileURLToPath} from 'node:url';
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const root=resolve(fileURLToPath(new URL('../../',import.meta.url)));
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY || 'output/tests/customer-viewport-visibility');
mkdirSync(output,{recursive:true});
const browser=await chromium.launch({headless:true,channel:'msedge'});
const report={productionViewport:true,productionWorkbenchCss:true,businessMocked:true,cases:[]};
try {
  const page=await browser.newPage();
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://visibility.test/**',route=>{
    const name=decodeURIComponent(new URL(route.request().url()).pathname);
    if(name==='/')return route.fulfill({contentType:'text/html',body:`<!doctype html><link rel="stylesheet" href="/src/apps/_shared/workbench/styles/workbench.css"><style>html,body{margin:0;height:100%;overflow:hidden}#layout{height:100%;display:grid;grid-template-rows:148px minmax(0,1fr) 141px}.cam-viewport{margin:0;border:0;position:relative}</style><div id="layout"><header></header><div class="cam-viewport"><div class="cam-render-viewport-shell"><div class="cam-render-viewport"></div></div></div><footer></footer></div>`});
    const path=resolve(root,name.slice(5));
    if(!name.startsWith('/src/')||!path.startsWith(root+sep))return route.abort();
    return route.fulfill({contentType:path.endsWith('.css')?'text/css':'text/javascript',body:readFileSync(path,'utf8')});
  });
  await page.goto('http://visibility.test/');
  await page.evaluate(async()=>{
    const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    window.v=createThreeViewport({continuousRender:false,avoidSpecificationOverlap:true});
    v.mount(document.querySelector('.cam-render-viewport'));v.setProjectionToggleVisible(true);
    v.camera.position.set(0,-2400,900);v.camera.lookAt(0,0,900);
    window.edits=0;v.specificationLabelLayer.addEventListener('click',e=>{if(e.target.matches('.icax-three-specification-trigger'))++window.edits;});
  });
  for(const [width,height,clipped] of [[1366,768,false],[1093,614,false],[911,512,false],[911,512,true]]) {
    await page.setViewportSize({width,height});
    const result=await page.evaluate(async({clipped})=>{
      const shell=document.querySelector('.cam-render-viewport-shell');
      shell.style.height=clipped?'420px':'100%';v.resize();v.camera.updateMatrixWorld(true);
      const {Vector3}=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
      const canvas=v.renderer.domElement,c=canvas.getBoundingClientRect();
      const control=document.querySelector('.icax-three-projection-toggle').getBoundingClientRect();
      const p=new Vector3((control.x+control.width/2-c.x)/c.width*2-1,1-(control.y+control.height/2-c.y)/c.height*2,0).unproject(v.camera);
      v.setSpecificationAnnotations([{id:'width',parameter:'width',start:[p.x-1,p.y,p.z],end:[p.x+1,p.y,p.z],offset:[0,0,0],label:'正面宽度 1,200 mm',oldValue:1200,newValue:1200,cleanLabel:'正面宽度 1,200 mm',changedLabel:'正面宽度 1,200 mm'}]);
      await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));
      const label=document.querySelector('.icax-three-specification-label'),l=label.getBoundingClientRect(),parent=document.querySelector('.cam-viewport').getBoundingClientRect();
      const center={x:l.x+l.width/2,y:l.y+l.height/2};
      const hit=document.elementFromPoint(center.x,center.y)===canvas;
      const before=window.edits;
      if(hit)canvas.dispatchEvent(new MouseEvent('dblclick',{button:0,clientX:center.x,clientY:center.y,bubbles:true,cancelable:true}));
      const overlaps=l.left<control.right&&l.right>control.left&&l.top<control.bottom&&l.bottom>control.top;
      const rect=r=>({x:r.x,y:r.y,w:r.width,h:r.height});
      return {clipped,canvas:rect(c),visibleContainer:rect(parent),label:rect(l),control:rect(control),overlaps,visible:l.left>=parent.left&&l.right<=parent.right&&l.top>=parent.top&&l.bottom<=parent.bottom,canvasFits:c.bottom<=parent.bottom+.5,hit,editable:window.edits===before+1};
    },{clipped});
    assert.equal(result.overlaps,false);assert.equal(result.visible,true);assert.equal(result.hit,true);assert.equal(result.editable,true);
    if(!clipped)assert.equal(result.canvasFits,true);
    report.cases.push({width,height,...result});await page.screenshot({path:resolve(output,`visibility-${width}-${clipped}.png`)});
  }
  assert.deepEqual(errors,[]);report.passed=true;
} finally {
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();
}
console.log(JSON.stringify({passed:report.passed,cases:report.cases.length}));
