import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';
const root=resolve(process.env.ICAX_NATIVE_SCENE_EVIDENCE);
const {tubeDesignerCss}=await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const {chromium}=await import('playwright');
const output=browserReportDirectory('product-material-refresh-20261006/callout-browser');
const browser=await chromium.launch({headless:true,channel:'chrome'}),cases=[];
try {
 for(const id of ['modular-guardrail-cross-straight','single-face-security-window']) {
  const scene=JSON.parse(readFileSync(resolve(root,`${id}.json`)));
  const page=await browser.newPage({viewport:{width:1600,height:1000}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://callout.test/**',serveBrowserAsset);await page.goto('http://callout.test/');
  await page.addStyleTag({content:`${tubeDesignerCss}*{box-sizing:border-box}.cam-workbench{display:flex}.cam-context-pane,.cam-info-pane{width:300px;height:450px;overflow:auto}.cam-viewport{width:920px;height:600px;position:relative}.cam-viewcube{position:absolute;top:8px;right:8px;width:116px;height:116px;background:#c88e18}.nested-left{height:150px;overflow:auto}.nested-left>div{height:1600px}`});
  await installProductInteractionFixture(page,scene.descriptor,scene);
  const result=await page.evaluate(async()=>{
   const f=window.productFixture,v=f.view.viewport;
   const {renderViewCube}=await import('/src/apps/_shared/workbench/viewport/viewCube.mjs');
   f.mount.querySelector('.cam-viewport').insertAdjacentHTML('beforeend',renderViewCube());
   const {resolveProductSpecificationAnnotations}=await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
   const annotations=resolveProductSpecificationAnnotations(f.view.scene.tubeDesigner,f.view);
   v.setSpecificationAnnotations(annotations);
   const cube=f.mount.querySelector('.cam-viewcube').getBoundingClientRect();
   const overlaps=(a,b)=>a.left<b.right&&a.right>b.left&&a.top<b.bottom&&a.bottom>b.top;
   for(const label of v.specificationLabels.filter(label=>label.parameterCallout&&!label.element.hidden))
    if(overlaps(label.element.getBoundingClientRect(),cube))throw new Error('Actual caption overlaps orientation cube');
   const original=v.specificationLabels.filter(label=>label.parameterCallout&&!label.element.hidden);
   if(!original.length)throw new Error('Native numeric callout did not reach the real viewport');
   // Force one collision at an actual native anchor; isolate it from crowded
   // dimensions to check the common placement algorithm, not template strings.
   const real=annotations.find(a=>['parameter','count'].includes(a.kind));
   const duplicate={...structuredClone(real),id:'collision-at-native-anchor',parameter:'collision-at-native-anchor',label:'邻近标注'};
   v.setSpecificationAnnotations([real,duplicate]);
   const canvas=v.renderer.domElement.getBoundingClientRect();
   const visible=v.specificationLabels.filter(l=>!l.element.hidden);
   if(visible.length!==2)throw new Error('Collision fixture lost its native projected anchor');
   const boxes=visible.map(l=>l.element.getBoundingClientRect());
   if(overlaps(boxes[0],boxes[1]))throw new Error('Nearby captions were not separated');
   for(const label of visible){
    const anchor=label.worldPoint.clone().project(v.camera),box=label.element.getBoundingClientRect();
    const distance=Math.hypot((box.left+box.right)/2-canvas.left-(anchor.x*.5+.5)*canvas.width,
       (box.top+box.bottom)/2-canvas.top-(-anchor.y*.5+.5)*canvas.height);
    if(distance>140)throw new Error(`A local collision moved the caption to a distant corner (${distance})`);
   }
   v.setSpecificationAnnotations(annotations);
   return {templateId:f.template.id,nativeCallouts:original.length,localCollisionResolved:true,orientationCubeAvoided:true};
  });
  assert.deepEqual(errors,[]);cases.push(result);
  await page.screenshot({path:resolve(output,`${id}.png`)});await page.close();
 }
 writeFileSync(resolve(output,'report.json'),JSON.stringify({status:'passed',cases},null,2));
 console.log('PASS actual native callouts avoid the orientation cube and resolve caption collisions locally in production WebGL');
} finally {await browser.close();}
