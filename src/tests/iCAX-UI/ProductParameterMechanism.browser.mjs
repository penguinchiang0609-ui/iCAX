// Native-generated anchors, production WebGL and production parameter actions.
// EC transport is deliberately held to exercise asynchronous user interaction.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';

const ids = ['single-face-security-window', 'modular-guardrail',
  'modular-guardrail-cross-straight', 'modular-guardrail-diamond-straight',
  'modular-guardrail-glass-straight', 'straight-steel-staircase'];
const evidence = resolve(process.env.ICAX_NATIVE_SCENE_EVIDENCE || 'output/tests/product-mechanism-20261005');
const output = browserReportDirectory('product-mechanism-20261005/browser');
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless:true, channel:process.env.ICAX_BROWSER_CHANNEL || 'chrome' });
const results=[];
try {
  for (const id of ids) {
    const nativeScene = JSON.parse(readFileSync(resolve(evidence,`${id}.json`),'utf8'));
    const directory=id.startsWith('modular-guardrail-') ? id.replace('modular-guardrail-','modular_guardrail_') : id.replaceAll('-','_');
    const descriptor={...JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${directory}/template.json`)),
      display:JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${directory}/display.json`))};
    assert.deepEqual(descriptor,nativeScene.descriptor,'Browser must read the exact verified installation template');
    const page = await browser.newPage({viewport:{width:1600,height:950}});
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    await page.route('http://product-mechanism.test/**',serveBrowserAsset);
    await page.goto('http://product-mechanism.test/');
    await page.addStyleTag({content:`${tubeDesignerCss}*{box-sizing:border-box}body{margin:0}.cam-workbench{display:flex;gap:8px}.cam-context-pane,.cam-info-pane{height:260px;overflow:auto;width:320px;flex:none}.cam-viewport{width:920px;height:600px;position:relative;flex:none}.nested-left{height:150px;overflow:auto}.nested-left>div{height:1600px}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1600px}.tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:1600px}.tube-designer-scene-settings-content{height:200px!important}.tube-designer-scene-settings-content:after{content:"";display:block;height:800px}`});
    await installProductInteractionFixture(page,descriptor,nativeScene);
    const initial=await page.evaluate(async()=>{
      const f=window.productFixture;
      const diagram=await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
      f.bindAnnotations=()=>diagram.bindProductSpecificationAnnotations(f.mount,f.view);
      const annotations=diagram.resolveProductSpecificationAnnotations(f.view.scene.tubeDesigner,f.view);
      if(!annotations.length)throw new Error('Native product supplied no usable world-space annotations');
      const right=f.mount.querySelector('.cam-info-pane');
      if(right.querySelector('[data-tube-designer-parameter-group="section:structure"], [data-tube-designer-parameter-group="section:specifications"]'))throw new Error('Geometry duplicated in right pane');
      for(const a of annotations.filter(a=>a.editable)) {
        if(f.mount.querySelector(`[data-tube-designer-parameter="${a.parameter}"]`))throw new Error(`Annotation has duplicate input ${a.parameter}`);
      }
      if(f.mount.querySelector('[data-tube-designer-scene-settings], [data-cam-action="tube-designer-open-product-structure"]'))throw new Error('Structure editing remained after creation');
      const pendingActions=[];
      f.mount.addEventListener('click',event=>{
        const target=event.target.closest?.('[data-cam-action="tube-designer-edit-scene-specification"]');
        if(target)pendingActions.push(f.actions.handleDesignerAreaAction(f.context,f.view,target.dataset.camAction,target,f.ops));
      });
      f.mount.addEventListener('change',event=>{
        const target=event.target.closest?.('[data-tube-designer-scene-specification-input]');
        if(target)pendingActions.push(f.actions.handleDesignerAreaAction(f.context,f.view,target.dataset.camChangeAction,target,f.ops));
      });
      f.pendingActions=pendingActions;
      return {templateId:f.template.id,editable:annotations.filter(a=>a.editable).map(a=>a.parameter),layout:(annotations.find(a=>a.editable&&f.template.parameters.find(field=>field.key===a.parameter)?.valueType==='integer')??annotations.find(a=>a.editable&&a.kind==='parameter'))?.parameter,canvasWebGL:!!f.view.viewport.renderer.domElement.getContext('webgl2')};
    });
    // Use the actual canvas hit path, rather than calling an editor directly.
    const hit=await page.evaluate(parameter=>{
      const f=window.productFixture;
      const label=[...document.querySelectorAll('.icax-three-specification-label')].find(node=>node.querySelector('button')?.dataset.tubeDesignerParameterKey===parameter);
      const rect=label.getBoundingClientRect();
      return {x:(rect.left+rect.right)/2,y:(rect.top+rect.bottom)/2};
    },initial.editable[0]);
    await page.mouse.dblclick(hit.x,hit.y);
    const editor=page.locator(`[data-tube-designer-scene-specification-input="${initial.editable[0]}"]`);
    await editor.waitFor({state:'visible'});
    const original=Number(await editor.inputValue());
    await editor.fill(String(original+10));await editor.press('Enter');
    await page.waitForFunction(()=>window.productFixture.pending.length===1).catch(async error=>{
      throw new Error(`${id}: ${error.message}; ${JSON.stringify(await page.evaluate(()=>({
        notices:window.productFixture.notices,requests:window.productFixture.requests,parameter:document.querySelector('[data-tube-designer-scene-specification-input]')?.dataset.tubeDesignerSceneSpecificationInput})))}`);
    });
    const committed=await page.evaluate(async({parameter,value})=>{
      const f=window.productFixture;
      if(f.requests.at(-1).parameters[parameter]!==value)throw new Error('Canvas edit did not reach shared EC action');
      await f.completeHeldChange(Promise.all(f.pendingActions),'left');
      f.bindAnnotations();
      const label=[...document.querySelectorAll('.icax-three-specification-label')].find(node=>node.querySelector('button')?.dataset.tubeDesignerParameterKey===parameter);
      if(!label?.classList.contains('is-pending')||!label.textContent.includes('→'))throw new Error('Pending edit lost old/new annotation state');
      f.assertPhysicalCommits();
      return {parameter,value};
    },{parameter:initial.editable[0],value:original+10});
    let layout=null;
    if(initial.layout){
      const hit=await page.evaluate(key=>{
        const label=document.querySelector(`button[data-tube-designer-parameter-key="${key}"]`).parentElement;
        const rect=label.getBoundingClientRect();return {x:(rect.left+rect.right)/2,y:(rect.top+rect.bottom)/2};
      },initial.layout);
      await page.mouse.dblclick(hit.x,hit.y);
      const input=page.locator(`[data-tube-designer-scene-specification-input="${initial.layout}"]`);
      const value=Number(await input.inputValue())+1;
      await input.fill(String(value));await input.press('Enter');
      await page.waitForFunction(()=>window.productFixture.pending.length===1);
      layout=await page.evaluate(async({key,value})=>{
        const f=window.productFixture;await f.completeHeldChange(Promise.all(f.pendingActions),'right');f.bindAnnotations();
        if(f.view.scene.tubeDesigner.product.parameters[key]!==value)throw new Error('Layout annotation did not commit its physical field');
        return {parameter:key,value};
      },{key:initial.layout,value});
    }
    const retention=await page.evaluate(async()=>{
      const f=window.productFixture,code=f.ordinary('productCode');
      await f.change(code,'shared annotation audit','left');
      f.bindAnnotations();
      const parameter=f.template.extensions.primaryDimensions?.widthParameter??f.template.parameters.find(field=>field.valueType==='number'&&!field.readOnly)?.key;
      const button=f.mount.querySelector(`button[data-tube-designer-parameter-key="${parameter}"]`);
      if(!button)throw new Error('Editable dimension trigger is absent');
      await f.actions.handleDesignerAreaAction(f.context,f.view,button.dataset.camAction,button,f.ops);f.bindAnnotations();
      const editor=f.mount.querySelector(`[data-tube-designer-scene-specification-input="${parameter}"]`);
      if(!editor)throw new Error('Shared scene annotation editor is absent');
      code.value='held material identity update';
      const pending=f.actions.handleDesignerAreaAction(f.context,f.view,code.dataset.camChangeAction,code,f.ops);
      await f.waitForPending();editor.focus({preventScroll:true});editor.value=String(Number(editor.value)+5);
      f.setScrolls([140,180,280,210]);const draft=editor.value,canvas=f.mount.querySelector('canvas');
      f.pending.shift()();await pending;f.bindAnnotations();
      if(document.activeElement!==editor||editor.value!==draft||f.mount.querySelector('canvas')!==canvas)throw new Error('Async response destroyed current annotation editor');
      f.same(f.scrolls(),[140,180,280,210],'Scene editor response lost both sidebar scrolls');
      // Conditional material fields still retain drafts and do not steal focus.
      let conditionalRemoval=null;
      const {parameterVisible}=await import('/src/apps/tube-designer/webpage/parameterConditions.mjs');
      for(const childDefinition of f.template.parameters){
        const parentKey=childDefinition.visibleWhen?.parameter,parent=f.ordinary(parentKey),child=f.ordinary(childDefinition.key);
        if(!parentKey||parent?.tagName!=='SELECT'||!child||child.disabled||child.readOnly)continue;
        const values=f.view.scene.tubeDesigner.product.parameters;
        const off=[...parent.options].map(option=>option.value).find(value=>!parameterVisible(childDefinition,{...values,[parentKey]:value}));
        if(off==null)continue;
        const saved=values[childDefinition.key];parent.value=off;
        const operation=f.actions.handleDesignerAreaAction(f.context,f.view,parent.dataset.camChangeAction,parent,f.ops);
        await f.waitForPending();child.focus({preventScroll:true});f.setScrolls([140,180,280,210]);
        f.pending.shift()();await operation;
        if(f.ordinary(childDefinition.key)||f.view.scene.tubeDesigner.product.parameters[childDefinition.key]!==saved)throw new Error('Conditional field failed removal/draft retention');
        if(document.activeElement?.matches?.('[data-tube-designer-parameter], [data-product-control-key]'))throw new Error('Removed field focused another product parameter');
        conditionalRemoval={parent:parentKey,removed:childDefinition.key};break;
      }
      f.assertPhysicalCommits();
      return {conditionalRemoval,commits:f.requests.length,committedParameters:f.requests.map(request=>request.parameters),focusScrollCanvasAndListenersPreserved:true,currentSceneEditorPreserved:true};
    });
    assert.deepEqual(errors,[]);results.push({...initial,...committed,layoutEdit:layout,...retention,status:'passed'});
    await page.screenshot({path:resolve(output,`${id}.png`)});await page.close();
  }
  writeFileSync(resolve(output,'report.json'),JSON.stringify({status:'passed',browserChannel:process.env.ICAX_BROWSER_CHANNEL||'chrome',scope:'Native-generated anchors and actual production WebGL/actions; controlled EC transport',cases:results},null,2));
  console.log(`PASS ${results.length} products: dimension/layout annotation double-click, physical payloads, fixed structure, async focus/scroll/node retention`);
}catch(error){writeFileSync(resolve(output,'report.json'),JSON.stringify({status:'failed',cases:results,error:error.stack},null,2));throw error;}
finally{await browser.close();}
