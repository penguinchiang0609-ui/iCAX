import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';
const root=resolve(process.env.ICAX_NATIVE_SCENE_EVIDENCE);
const evidence=JSON.parse(readFileSync(resolve(root,'native-refresh-report.json')));
assert.equal(evidence.status,'passed');assert.equal(evidence.stairVariants.length,12);
const fixture=JSON.parse(readFileSync(resolve(root,'straight-steel-staircase.json')));
const current=JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/straight_steel_staircase/template.json'));
assert.deepEqual(current.parameters,fixture.descriptor.parameters,'Current schematic must use the native-validated physical contract');
fixture.descriptor={...current,display:JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/straight_steel_staircase/display.json'))};
const {tubeDesignerCss}=await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const {chromium}=await import('playwright');
const output=browserReportDirectory('product-material-refresh-20261006/schematic-browser');
const browser=await chromium.launch({headless:true,channel:'chrome'});
const page=await browser.newPage({viewport:{width:1500,height:1050}}), errors=[];
page.on('pageerror',e=>errors.push(e.message));
try {
 await page.route('http://creation-schematic.test/**',serveBrowserAsset);await page.goto('http://creation-schematic.test/');
 await page.addStyleTag({content:tubeDesignerCss});
 await page.addStyleTag({content:'.cam-workbench{display:flex}.cam-context-pane,.cam-info-pane{width:300px;height:400px;overflow:auto}.cam-viewport{width:900px;height:600px}.nested-left{height:150px;overflow:auto}.nested-left>div{height:1600px}'});
 await installProductInteractionFixture(page,fixture.descriptor,fixture);
 await page.evaluate(()=>{
  const f=window.productFixture;f.view.tubeDesignerAddDialogOpen=true;
  f.view.tubeDesignerAddTemplateId=f.template.id;f.view.tubeDesignerAddDraft=structuredClone(f.view.scene.tubeDesigner.product.parameters);f.render();
  f.operations=[];f.mount.addEventListener('change',event=>{
   const node=event.target.closest?.('[data-tube-designer-add-form] [data-product-control-key]');
   if(node)f.operations.push(f.actions.handleDesignerAreaAction(f.context,f.view,node.dataset.camChangeAction,node,f.ops));
  });
 });
 const artwork=new Set(),cases=[];
 for(const variant of evidence.stairVariants) {
  await page.locator('[data-tube-designer-add-form] select[data-product-control-key="stairsStringerStructure"]')
   .selectOption(`${variant.system}_${variant.construction}`);
  await page.evaluate(()=>Promise.all(window.productFixture.operations));
  await page.locator('[data-tube-designer-add-form] select[data-product-control-key="stairsTreadSupportStructure"]')
   .selectOption(variant.support);
  await page.evaluate(()=>Promise.all(window.productFixture.operations));
  const actual=await page.evaluate(()=>{
   const f=window.productFixture,art=f.mount.querySelector('[data-tube-designer-add-form]')?.closest('.tube-designer-add-dialog')
     ?.querySelector('.tube-designer-product-diagram-art') ?? f.mount.querySelector('.tube-designer-product-diagram-art');
   if(!art?.querySelector('svg')||art.querySelector('img'))throw new Error('Creation did not render declared dynamic SVG');
   return {svg:art.innerHTML,geometry:[...art.querySelectorAll('path,polygon,polyline,line,rect,circle')]
     .map(node=>node.outerHTML).join(''),parameters:structuredClone(f.view.tubeDesignerAddDraft)};
  });
  assert.equal(actual.parameters.stringerSystem,variant.system);
  assert.equal(actual.parameters.stringerConstruction,variant.construction);assert.equal(actual.parameters.treadSupport,variant.support);
  assert.ok(actual.geometry.length>0);
  artwork.add(actual.geometry);cases.push({...variant,svg:actual.svg});
 }
 assert.equal(artwork.size,12,'Every beam/support choice must change the schematic');
 const additional=[];
 for(const [control,key] of [['stairsRouteStructure','stairRoute'],['stairsDeckStructure','treadType'],['stairsGuardStructure','railingSide']]) {
  const variants=new Set();
  for(const choice of fixture.descriptor.parameters.find(field=>field.key===key).choices) {
   await page.locator(`[data-tube-designer-add-form] select[data-product-control-key="${control}"]`).selectOption(choice.value);
   await page.evaluate(()=>Promise.all(window.productFixture.operations));
   const value=await page.evaluate(key=>({parameter:window.productFixture.view.tubeDesignerAddDraft[key],
     geometry:[...document.querySelector('.tube-designer-add-quick-visual').querySelectorAll('path,polygon,polyline,line,rect,circle')]
       .map(node=>node.outerHTML).join('')}),key);
   assert.equal(value.parameter,choice.value);variants.add(value.geometry);additional.push({key,value:choice.value});
  }
  assert.equal(variants.size,fixture.descriptor.parameters.find(field=>field.key===key).choices.length);
  await page.locator(`[data-tube-designer-add-form] select[data-product-control-key="${control}"]`)
    .selectOption(fixture.descriptor.parameters.find(field=>field.key===key).defaultValue);
  await page.evaluate(()=>Promise.all(window.productFixture.operations));
 }
 assert.deepEqual(errors,[]);
 await page.screenshot({path:resolve(output,'stair-creation.png')});
 writeFileSync(resolve(output,'report.json'),JSON.stringify({status:'passed',cases,additional,nativeVariants:evidence.stairVariants},null,2));
 console.log('PASS 12 beam/support combinations and all 13 route/tread/railing choices update actual creation schematics');
} finally {await browser.close();}
