// Actual production inputs/actions/DOM patch in a browser. Host update replies
// and fixed resource snapshots are controlled; native generation is separate.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';

const output=browserReportDirectory('minimal-grille-mixed-projection-20261110/material-browser');
const folder='apps/tube-designer/templates/product/minimal_protective_grille';
const raw={...JSON.parse(readBrowserAsset(`${folder}/template.json`)),display:JSON.parse(readBrowserAsset(`${folder}/display.json`))};
const defaults=Object.fromEntries(raw.parameters.map(f=>[f.key,f.defaultValue]));
const {tubeDesignerCss}=await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
const report={passed:false,nativeBusinessMocked:true,standaloneNative:false,cases:[],pageErrors:[]};
let page;
try {
  page=await browser.newPage({viewport:{width:1500,height:1040}});
  page.on('pageerror',error=>report.pageErrors.push(error.message));
  await page.route('http://projection-materials.test/**',serveBrowserAsset);
  await page.goto('http://projection-materials.test/');
  await page.addStyleTag({content:`${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Arial,'Microsoft YaHei'}
    .cam-workbench{display:grid;grid-template-columns:260px 1fr 480px;gap:8px;height:1000px}
    .cam-context-pane,.cam-info-pane{height:300px;overflow:auto}.cam-viewport{position:relative;background:#152b34}
    .nested-left{height:160px;overflow:auto}.nested-left>div{min-height:1700px}
    .cam-context-pane:after,.cam-info-pane:after{content:'';display:block;height:1700px}
    .tube-designer-parameter-scroll{height:210px!important;overflow:auto!important}
    .tube-designer-parameter-scroll:after{content:'';display:block;height:1700px}`});
  await installProductInteractionFixture(page,raw,null,{initialParameters:{...defaults,layoutStyle:'five_face',width:1800,handleEnabled:'no'}});
  await page.evaluate(()=>{
    const f=window.productFixture;f.operations=[];
    f.mount.addEventListener('change',event=>{
      const target=event.target.closest?.('[data-cam-change-action]');
      if(target)f.operations.push(f.actions.handleDesignerAreaAction(f.context,f.view,target.dataset.camChangeAction,target,f.ops));
    });
  });
  async function edit(key,value,profile=false) {
    const selector=profile?`[data-tube-designer-profile-prefix="${key}"][data-cam-change-action="tube-designer-profile-selection-change"]`
      :`[data-tube-designer-parameter="${key}"]`;
    const input=page.locator(selector);
    await input.waitFor({state:'visible'});
    await input.evaluate(node=>{
      const f=window.productFixture;f.edited=node;f.editedEvents=0;
      node.addEventListener('mixed-material-audit',()=>f.editedEvents++);
      f.before=structuredClone(f.view.scene.tubeDesigner.product.parameters);
    });
    if(profile)await input.selectOption(String(value));
    else {await input.fill(String(value));await input.press('Tab');}
    await page.waitForFunction(()=>window.productFixture.pending.length===1);
    const result=await page.evaluate(async({key,value,profile,selector,side})=>{
      const f=window.productFixture;
      await f.completeHeldChange(Promise.all(f.operations),side);
      const parameters=f.view.scene.tubeDesigner.product.parameters;
      if(!f.view.scene.tubeDesigner.product.modelOutdated)throw new Error('Material edit did not expire the model');
      const role=profile?key:key.startsWith('projectionVertical')?'projectionVertical':'projectionHorizontal';
      for(const other of ['frame','inner','support','projectionVertical','projectionHorizontal'].filter(r=>r!==role))
        for(const suffix of ['ProfileType','Width','Depth','WallThickness','CornerRadius'])
          if(parameters[other+suffix]!==f.before[other+suffix])throw new Error(`Unrelated material changed: ${other+suffix}`);
      if(!profile&&parameters[key]!==value)throw new Error('Numeric material value not committed');
      if(profile&&!parameters.tubeDesignerProfileOverrides?.[role])throw new Error('Selected material did not create its own resource snapshot');
      const after=f.mount.querySelector(selector);
      if(after!==f.edited)throw new Error('Material edit replaced the actual editor');
      after.dispatchEvent(new Event('mixed-material-audit'));
      if(f.editedEvents<1)throw new Error('Material edit discarded the input listener');
      f.assertPhysicalCommits();
      return {key,value,profile,modelOutdated:true,otherMaterialsPreserved:true,focusSelectionNestedScrollNodesListenersPreserved:true};
    },{key,value,profile,selector,side:report.cases.length%2?'left':'right'});
    report.cases.push({...result,passed:true});
  }
  for(const [key,value]of [['projectionHorizontalWidth',14],['projectionVerticalWidth',18],['projectionHorizontalDepth',14],['projectionVerticalDepth',18]])
    await edit(key,value);
  await page.evaluate(()=>{
    const f=window.productFixture;
    const shape=(id,kind,width,depth)=>({schema:'icax.imported-tube-profile',schemaVersion:1,id,profileDefinitionId:id,
      profileScope:'system',libraryScope:'system',profileForm:'fixed',kind:'fixed-section',sectionKind:kind,
      name:`浏览器截面 ${id}`,width,depth,wallThickness:kind==='rect-bar'?0:.8,contourCount:kind==='rect-bar'?1:2,hollow:kind!=='rect-bar',
      contours:kind==='round'?[{kind:'circle',center:[0,0],radius:width/2},{kind:'circle',center:[0,0],radius:width/2-.8}]
        :[{kind:'roundedRectangle',center:[0,0],width,height:depth,radius:kind==='rect-bar'?0:1},
          ...(kind==='rect-bar'?[]:[{kind:'roundedRectangle',center:[0,0],width:width-1.6,height:depth-1.6,radius:.2}])]});
    f.view.tubeDesignerSystemProfiles=[shape('rect','rect',15,15),shape('round','round',14,14),shape('rect-bar','rect-bar',10,5),
      shape('mixed-rect','rect',14,12),shape('mixed-round','round',14,14),shape('mixed-flat','rect-bar',14,5)]
      .map(p=>({...p,previewProfile:structuredClone(p)}));
    f.render();
  });
  for(const role of ['projectionVertical','projectionHorizontal'])for(const id of ['mixed-round','mixed-rect','mixed-flat'])
    await edit(role,`system:${id}`,true);
  assert.deepEqual(report.pageErrors,[]);
  const result=await page.evaluate(()=>{
    const f=window.productFixture;f.assertPhysicalCommits();
    return{actualEdits:f.requests.length,physicalParameterCount:f.template.parameters.length};
  });
  assert.equal(result.actualEdits,10);assert.equal(result.physicalParameterCount,79);
  Object.assign(report,result,{passed:true,actualBrowserEvents:true});
  console.log('PASS projection material browser: 10 actual edits/selections, independent resources, model expiry and async interaction retained');
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
