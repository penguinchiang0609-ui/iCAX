// Production SVG and actual annotation/material editing with controlled host
// responses. Python is the independent oracle for declared physical stocks.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {importBrowserAsset,serveBrowserAsset} from './browserPackageRuntime.mjs';
import {installProductInteractionFixture} from './productInteractionFixture.mjs';
import {invisibleGrilleDiagramStocks} from '../../apps/tube-designer/webpage/invisibleGrilleDiagram.mjs';

const sourceRoot=fileURLToPath(new URL('../../',import.meta.url));
const output=resolve(process.env.ICAX_INVISIBLE_VISUAL_EVIDENCE||'output/tests/invisible-grille-20261110/browser');
mkdirSync(output,{recursive:true});const temp=resolve(output,'temp');mkdirSync(temp,{recursive:true});
const folder=resolve(sourceRoot,'apps/tube-designer/templates/product/invisible_protective_grille');
const raw={...JSON.parse(readFileSync(resolve(folder,'template.json'))),display:JSON.parse(readFileSync(resolve(folder,'display.json')))};
const defaults=Object.fromEntries(raw.parameters.map(f=>[f.key,f.defaultValue]));
const oracle=spawnSync(process.env.ICAX_PYTHON||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',
  ['-X','utf8','-c',`
import json,sys
from pathlib import Path
from itertools import product
root=Path(sys.argv[1]);sys.path.insert(0,str(root/'iCAX-Engine/framework/TemplateRuntime/python'))
from icax_template_worker import _load_template
folder=root/'apps/tube-designer/templates/product/invisible_protective_grille'
raw=json.loads((folder/'template.json').read_text(encoding='utf-8'))
template=_load_template(str(folder/'template.py'),'invisible-browser-oracle')
defaults={f['key']:f['defaultValue'] for f in raw['parameters']};cases=[]
changes=[dict(width=w,height=h,wireWidth=d,wireCenterSpacing=p,clampCount=n,clampBottomOffset=300 if n else -1,clampTopOffset=300 if n>1 else -1)
 for w,h,d,p,n in product((600,1200,1800),(900,1200),(1.5,2.,3.),(35.,50.,80.),(0,1,3))]
changes += [dict(width=100,wireCenterSpacing=2000,clampCount=0,clampBottomOffset=-1,clampTopOffset=-1),dict(clampWidth=30,clampDepth=5,clampCount=2,clampBottomOffset=250,clampTopOffset=350)]
for change in changes:
 p={**defaults,**change};state=template.layout(p)
 cases.append(dict(values=p,facts=state['facts'],members=[dict(key=m['key'],role=m['role'],profileRole=m['profileRole'],width=m['profile'].width,depth=m['profile'].depth,start=m['start'],end=m['end']) for m in state['members']]))
print(json.dumps(cases))
`,sourceRoot],{encoding:'utf8',env:{...process.env,TEMP:temp,TMP:temp,TMPDIR:temp},maxBuffer:12*1024*1024});
assert.equal(oracle.status,0,oracle.stderr);const cases=JSON.parse(oracle.stdout);
for(const test of cases) {
  const actual=invisibleGrilleDiagramStocks(raw.extensions.productDiagram,test.values);
  assert.equal(actual.members.length,test.members.length);
  actual.members.forEach((m,i)=>{
    const expected=test.members[i];for(const key of ['key','role','profileRole','width','depth'])assert.equal(m[key],expected[key]);
    for(const key of ['start','end'])m[key].forEach((v,axis)=>assert.ok(Math.abs(v-expected[key][axis])<1e-7,`${m.key}/${key}/${axis}`));
  });
  assert.deepEqual(actual.middleCenters,test.facts.middleClampCenters);
  assert.equal(actual.wireCenters.length,test.facts.wireCount);
  assert.equal(test.facts.continuousVerticalWires,true);assert.equal(test.facts.hasSideFrame,false);
}
const {tubeDesignerCss}=await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
const report={passed:false,geometryOracleCases:cases.length,nativeBusinessMocked:true,standaloneNative:false,
  anchorSource:'Controlled browser annotation fixture; native preview/disassembly is separate',cases:[],pageErrors:[]};
let page;
try {
  page=await browser.newPage({viewport:{width:1600,height:1000}});
  page.on('pageerror',error=>report.pageErrors.push(error.message));
  await page.route('http://invisible-grille.test/**',serveBrowserAsset);await page.goto('http://invisible-grille.test/');
  await page.addStyleTag({content:`${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Arial,'Microsoft YaHei'}
    .cam-workbench{display:flex;gap:8px}.cam-context-pane,.cam-info-pane{height:280px;overflow:auto;width:300px;flex:none}
    .cam-viewport{width:920px;height:600px;position:relative;flex:none}.nested-left{height:150px;overflow:auto}
    .nested-left>div{height:1600px}.cam-context-pane:after,.cam-info-pane:after{content:'';display:block;height:1600px}
    .tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}
    .tube-designer-parameter-scroll:after{content:'';display:block;height:1600px}`});
  const anchors={specificationAnnotations:raw.extensions.sceneSpecificationAnnotations.annotations.map((a,i)=>({...a,
    id:`invisible-fixture:${a.parameter}`,start:[150+(i%3)*350,0,150+Math.floor(i/3)*350],
    end:[150+(i%3)*350+(a.kind==='parameter'?0:150),0,150+Math.floor(i/3)*350],generatedValue:defaults[a.parameter]}))};
  await installProductInteractionFixture(page,raw,anchors);
  await page.evaluate(async()=>{
    const f=window.productFixture;f.diagram=await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
    f.artwork=await import('/src/apps/tube-designer/webpage/invisibleGrilleDiagram.mjs');f.operations=[];
    f.mount.addEventListener('click',event=>{const node=event.target.closest?.('[data-cam-action="tube-designer-edit-scene-specification"]');
      if(node)f.operations.push(f.actions.handleDesignerAreaAction(f.context,f.view,node.dataset.camAction,node,f.ops));});
    f.mount.addEventListener('change',event=>{const node=event.target.closest?.('[data-cam-change-action]');
      if(node)f.operations.push(f.actions.handleDesignerAreaAction(f.context,f.view,node.dataset.camChangeAction,node,f.ops));});
    if(!f.view.viewport.renderer.domElement.getContext('webgl2'))throw new Error('Production annotation WebGL missing');
    f.assertState=()=>{
      const p=f.view.scene.tubeDesigner.product.parameters;
      const annotations=f.diagram.resolveProductSpecificationAnnotations(f.view.scene.tubeDesigner,f.view);
      for(const key of ['width','height','wireWidth','wireCenterSpacing','clampCount','clampBottomOffset','clampTopOffset']){
        const visible=key==='clampBottomOffset'?p.clampCount>0:key==='clampTopOffset'?p.clampCount>1:true;
        if(annotations.some(a=>a.parameter===key)!==visible)throw new Error('Incorrect annotation visibility '+key);
        if(f.ordinary(key))throw new Error('Scene dimension duplicated in sidebar '+key);
      }
      f.assertPhysicalCommits();
      return f.artwork.invisibleGrilleDiagramStocks(f.template.extensions.productDiagram,p);
    };f.assertState();
  });
  const operations=[['wireWidth',3],['wireCenterSpacing',60],['width',1500],['height',1300],
    ['clampCount',0],['clampCount',1],['clampCount',3],['clampBottomOffset',300],['clampTopOffset',250],
    ['clampWidth',24],['clampDepth',4],['clampCount',0],['clampCount',1]];
  for(const [index,[key,value]]of operations.entries()) {
    const material=['clampWidth','clampDepth'].includes(key);
    if(!material){const hit=await page.locator(`button[data-tube-designer-parameter-key="${key}"]`).first().boundingBox();assert.ok(hit,key);await page.mouse.dblclick(hit.x+hit.width/2,hit.y+hit.height/2);}
    const editor=page.locator(material?`[data-tube-designer-parameter="${key}"]`:`[data-tube-designer-scene-specification-input="${key}"]`);
    await editor.waitFor({state:'visible'});await editor.fill(String(value));await editor.press(material?'Tab':'Enter');
    await page.waitForFunction(()=>window.productFixture.pending.length===1);
    const result=await page.evaluate(async({key,value,side})=>{
      const f=window.productFixture;await f.completeHeldChange(Promise.all(f.operations),side);
      f.diagram.bindProductSpecificationAnnotations(f.mount,f.view);
      if(f.view.scene.tubeDesigner.product.parameters[key]!==value)throw new Error('Physical edit not committed');
      if(!f.view.scene.tubeDesigner.product.modelOutdated)throw new Error('Geometry edit did not expire model');
      const state=f.assertState();return{key,value,wires:state.wireCenters.length,clampStrips:2*(state.middleCenters.length+2),
        modelOutdated:true,focusSelectionNestedScrollCanvasNodesListenersRetained:true};
    },{key,value,side:index%2?'left':'right'});report.cases.push({...result,passed:true});
  }
  // Inspect the real parametric-resource renderer with the scene-owned wire
  // gauge binding. The numeric edits above used real scene annotation input.
  report.parametricProfileDimensionOwnership=await page.evaluate(()=>{
    const f=window.productFixture,p=f.view.scene.tubeDesigner.product.parameters;
    const base={schema:'icax.imported-tube-profile',schemaVersion:1,kind:'profile-package',profileForm:'parametric',profileScope:'system',contourCount:1,hollow:false};
    p.tubeDesignerProfileOverrides={wire:{...base,profileDefinitionId:'round-bar',sectionKind:'round-bar',width:p.wireWidth,depth:p.wireWidth,
      contours:[{kind:'circle',center:[0,0],radius:p.wireWidth/2}],parameterDefinitions:[{key:'width',valueType:'number'}],parameterValues:{width:p.wireWidth}},
      clamp:{...base,profileDefinitionId:'rect-bar',sectionKind:'rect-bar',width:p.clampWidth,depth:p.clampDepth,
      contours:[{kind:'roundedRectangle',center:[0,0],width:p.clampWidth,height:p.clampDepth,radius:0}],
      parameterDefinitions:[{key:'width',valueType:'number'},{key:'depth',valueType:'number'},{key:'useHotRolled',valueType:'boolean'}],
      parameterValues:{width:p.clampWidth,depth:p.clampDepth,useHotRolled:false}}};f.render();
    if(f.ordinary('wireWidth')||!f.ordinary('clampWidth')||!f.ordinary('clampDepth'))throw new Error('Resource binding duplicated scene gauge or lost clamp dimensions');
    if(f.mount.textContent.includes('useHotRolled'))throw new Error('Resource management parameter leaked into product');
    return{wireGaugeInSceneOnly:true,clampWidthThicknessInMaterialPane:true};
  });
  assert.deepEqual(report.pageErrors,[]);
  await page.setViewportSize({width:1440,height:850});
  const gallery=await page.evaluate(async({raw,defaults})=>{
    const {renderInvisibleGrilleArtwork,invisibleGrilleDiagramStocks}=await import('/src/apps/tube-designer/webpage/invisibleGrilleDiagram.mjs');
    const samples=[0,1,3].map(clampCount=>({...defaults,clampCount,clampBottomOffset:clampCount===3?300:500,clampTopOffset:300}));
    document.body.innerHTML='<style>body{margin:0;padding:24px;background:#f2f6f7;font-family:Arial,"Microsoft YaHei"}h1{font-size:23px;color:#244450}main{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}article{padding:16px;background:white;border:1px solid #b7ced4}h2{font-size:17px;color:#244450}p{font-size:13px;line-height:1.7;color:#4c6770}svg{width:100%;height:300px;background:#17313a}</style><h1>隐形防护网 · 通长圆线与正反压条夹固</h1><main>'+samples.map(p=>`<article><h2>中间 ${p.clampCount} 组夹条</h2>${renderInvisibleGrilleArtwork(raw,raw.extensions.productDiagram,p)}<p>成品 ${p.width} × ${p.height} mm<br>实心圆线：直径 ${p.wireWidth} mm · 中心距 ${p.wireCenterSpacing} mm<br>矩形压条：宽 ${p.clampWidth} mm · 厚 ${p.clampDepth} mm<br>每组正反两片，竖线连续通过夹固位置<br>不增加左右框；上述数值均可修改</p></article>`).join('')+'</main><p>示意按普通矩形压条生成；供应商专用槽型、端部连接件与锚固能力另行确认。</p>';
    return samples.map(p=>{const s=invisibleGrilleDiagramStocks(raw.extensions.productDiagram,p);return{clampCount:p.clampCount,wireCount:s.wireCenters.length,clampStripCount:2*(p.clampCount+2)}});
  },{raw,defaults});
  await page.screenshot({path:resolve(output,'wire-clamp-live-diagrams.png'),fullPage:true});
  Object.assign(report,{passed:true,actualBrowserEvents:true,actualAnnotationAndMaterialEdits:operations.length,productionWebGL:true,
    physicalParameterCount:12,productionRendererSampleGallery:gallery});
  console.log(`PASS invisible browser: ${cases.length} exact Python layouts, ${operations.length} actual annotation/material edits, scene/material ownership and async interaction retained`);
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'preview-browser-report.json'),JSON.stringify(report,null,2));await browser.close();}
