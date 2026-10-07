// Product-owned compact inputs, using native evaluated profiles.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';
const root=resolve(process.env.ICAX_NATIVE_SCENE_EVIDENCE || 'output/tests/product-profile-boundary-20261005');
const catalogue=JSON.parse(readFileSync(resolve(root,'system-profiles.json')));
const evaluations=JSON.parse(readFileSync(resolve(root,'evaluations.json')));
const {tubeDesignerCss}=await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const {chromium}=await import('playwright');
const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL || 'chrome'});
const report={status:'running',cases:[],nativeTransport:'controlled responses from actual native profile evaluations'};
const output=browserReportDirectory('product-profile-labels-20261006/browser');
try {
 for(const id of Object.keys(evaluations)) {
  if (id === 'minimal-protective-grille') continue;
  const fixture=JSON.parse(readFileSync(resolve(root,`${id}.json`)));
  const page=await browser.newPage({viewport:{width:1440,height:1000}}), errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  await page.route('http://product-boundary.test/**',serveBrowserAsset);await page.goto('http://product-boundary.test/');
  await page.addStyleTag({content:tubeDesignerCss});await page.addStyleTag({content:'.cam-workbench{display:grid;grid-template-columns:280px 1fr 430px;position:relative;height:900px}.cam-context-pane,.cam-info-pane{overflow:auto;height:800px}.nested-left,[data-tube-designer-parameter-scroll]{max-height:620px;overflow:auto}.nested-left>div{min-height:1600px}.cam-viewport{position:relative;background:#152b33}canvas{width:100%;height:700px}main{height:900px}#left-note,#right-note{display:block;width:160px}.tube-designer-parameter-sections{min-height:1200px}'});
  await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{height:260px}.nested-left{height:150px}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1600px}.tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:1600px}'});
  await installProductInteractionFixture(page,fixture.descriptor,fixture);
  const result=await page.evaluate(async ({fixture,catalogue,evaluation})=>{
   const f=window.productFixture;
   f.view.scene.tubeDesigner.product.parameters=structuredClone(fixture.product.parameters);
   f.view.tubeDesignerSystemProfiles=structuredClone(catalogue);
   const firstField=f.template.parameters.find(field=>field.presentation?.editor==='profile-library');
   const role=firstField.presentation.resourceRole;
   const snapshot=f.view.scene.tubeDesigner.product.parameters.tubeDesignerProfileOverrides[role];
   const userId='f2d1f6b0-21f5-4bcc-b551-b3b604100aa1';
   f.view.tubeDesignerUserData={profiles:[{id:userId,name:'我的构件管型',profileForm:snapshot.profileForm,
    descriptor:{parameters:snapshot.parameterDefinitions},defaultParameters:snapshot.parameters,previewProfile:structuredClone(snapshot)}]};
   const evalRequests=[];let holdNextEvaluation=false,finishEvaluation;
   const invoke=f.context.sceneProxy.invoke;
   const evaluate=async(method,payload)=>{
    if(method!=='TubeDesigner.EvaluateProfilePackage')return invoke(method,payload);
    const ordered=value=>JSON.stringify(Object.fromEntries(Object.entries(value).sort(([a],[b])=>a.localeCompare(b))));
    const actual=[evaluation,evaluation.next].find(item=>ordered(payload.parameters)===ordered(item.parameters));
    if(!actual)throw new Error('Profile request differs from the actual native evaluated input');
    evalRequests.push(structuredClone(payload));
    if(holdNextEvaluation){holdNextEvaluation=false;return new Promise(resolve=>{finishEvaluation=()=>resolve({profile:structuredClone(actual.profile)});});}
    return {profile:structuredClone(actual.profile)};
   };
   f.context.sceneProxy.invoke=evaluate;f.context.productProxy={invoke:evaluate};f.render();
   if(f.mount.querySelector('[data-tube-designer-scene-settings], [data-tube-designer-scene-parameter-form], [data-cam-action="tube-designer-open-product-structure"]'))throw new Error('Creation-only structure is still editable in the scene');
   const {resolveProductSpecificationAnnotations}=await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
   if(!resolveProductSpecificationAnnotations(f.view.scene.tubeDesigner,f.view).some(item=>item.editable&&item.editor))throw new Error('Created dimensions lost annotation editing');
   if(!f.ordinary('productCode')?.closest('.cam-info-pane')||f.mount.querySelector('.tube-designer-scene-settings [data-tube-designer-parameter="productCode"]'))throw new Error('Product identity is routed differently for this template');
   const roleEditors=[];
   const sectionEditors=[];
   const views=await import('/src/apps/tube-designer/webpage/designerViews.mjs');
   for(const control of f.mount.querySelectorAll('select[data-tube-designer-profile-prefix]')){
    const currentRole=control.dataset.tubeDesignerProfilePrefix;
    const field=f.template.parameters.find(field=>field.presentation?.editor==='profile-library'&&field.presentation.resourceRole===currentRole);
    for(const option of control.options){
     if(!option.value)continue;
     const profile=option.value.startsWith('system:')?catalogue.find(item=>`system:${item.id}`===option.value):f.view.tubeDesignerUserData.profiles.find(item=>`user:${item.id}`===option.value);
     if(!profile||!field.presentation.profileConstraints.sectionKinds.includes(profile.previewProfile.sectionKind))throw new Error('Selector contains an unsupported resource in '+currentRole);
    }
    if(control.parentElement.textContent.includes('支持：'))throw new Error('Redundant supported-type text remains in '+currentRole);
    const selectedProfile=f.view.scene.tubeDesigner.product.parameters.tubeDesignerProfileOverrides[currentRole];
    const declaration=f.template.extensions.resourceRoles.profiles[currentRole];
    const mapping=declaration.parameterBindingsBySectionKind[selectedProfile.sectionKind];
    const ownInputs=[...control.parentElement.querySelectorAll('[data-profile-parameter-key]')];
    if(ownInputs.some(input=>mapping[input.dataset.profileParameterKey]!==input.dataset.tubeDesignerParameter))throw new Error('Resource management schema leaked into '+currentRole);
    const labels=declaration.parameterLabelsBySectionKind[selectedProfile.sectionKind];
    for(const input of ownInputs){
     const expected=labels[input.dataset.profileParameterKey]['zh-CN'];
     if(input.closest('label').querySelector('span').textContent!==expected)throw new Error('A product label did not use its template declaration');
    }
    const editor=control.parentElement.querySelector('[data-profile-parameter-scope]');
    if(editor.querySelector(':scope > strong').textContent!=='截面尺寸（mm）'||(editor.textContent.match(/mm/g)||[]).length!==1)throw new Error('Dimension units were repeated across product fields');
    if(control.parentElement.querySelector('svg, [data-product-profile-diagram-open], [data-parameter-diagram-owner]'))throw new Error('Product retained a resource diagram');
    roleEditors.push({role:currentRole,selectedResource:control.value,finiteProductFields:ownInputs.map(input=>input.dataset.tubeDesignerParameter),labels:ownInputs.map(input=>input.closest('label').querySelector('span').textContent)});
    for(const kind of field.presentation.profileConstraints.sectionKinds){
     const source=catalogue.find(item=>item.id===kind);
     const parameters=structuredClone(f.view.scene.tubeDesigner.product.parameters);
     parameters[field.key]=kind;
     parameters.tubeDesignerProfileOverrides[currentRole]={...structuredClone(source.previewProfile),profileScope:'system',profileDefinitionId:kind};
     const variant={...f.view,tubeDesignerRightDraft:parameters};
     const panel=document.createElement('div');panel.innerHTML=views.renderDesignerRightPane(f.context,variant);
     const scoped=panel.querySelector(`[data-tube-designer-profile-prefix="${currentRole}"]`).parentElement;
     const declaredLabels=declaration.parameterLabelsBySectionKind[kind];
     const labels=[...scoped.querySelectorAll('[data-profile-parameter-key]')].map(input=>{
      const name=input.closest('label').querySelector('span').textContent;
      if(name!==declaredLabels[input.dataset.profileParameterKey]['zh-CN'])throw new Error('Section switch did not use its own compact label');
      return name;
     });
     if(!labels.length||scoped.querySelector('svg,button,[data-parameter-diagram-owner]'))throw new Error('Product section switch reintroduced a resource diagram');
     if((scoped.textContent.match(/mm/g)||[]).length!==1)throw new Error('Section switch duplicated dimension units');
     sectionEditors.push({role:currentRole,kind,labels});
    }
   }
   const add=document.createElement('div');
   add.innerHTML=views.renderDesignerAddParameterContent(f.view.scene.tubeDesigner,{...f.view,tubeDesignerAddTemplateId:f.template.id,tubeDesignerAddDraft:structuredClone(f.view.scene.tubeDesigner.product.parameters)});
   if(add.querySelector('[data-product-profile-diagram-open], [data-floating-parameter-diagram]'))throw new Error('Add product flow retained a profile diagram');
   for(const key of Object.values(f.template.extensions.primaryDimensions)){
    if(f.ordinary(key))throw new Error('Annotated dimension is duplicated in the right editor');
   }
   const selector=f.mount.querySelector(`[data-tube-designer-profile-prefix="${role}"][data-cam-change-action="tube-designer-profile-selection-change"]`);
   const allowed=firstField.presentation.profileConstraints.sectionKinds;
   if([...selector.options].some(option=>!/^system:|^user:|^$/.test(option.value)))throw new Error('Non-library profile choice remains');
   if(selector.parentElement.textContent.includes('支持：'))throw new Error('Supported sections are repeated outside the filtered selector');
   for(const option of selector.options)if(option.value.startsWith('system:')){
    const profile=catalogue.find(profile=>`system:${profile.id}`===option.value);
    if(!allowed.includes(profile.previewProfile.sectionKind))throw new Error('An unsupported section is offered');
   }
   if(f.ordinary('materialGrade'))throw new Error('Free material grade remains');
   if(f.mount.querySelector('[data-tube-designer-profile-parameter="innerOffsetX"]'))throw new Error('Resource management input leaked into product');
   const group=selector.parentElement.querySelector('[data-profile-parameter-scope]');
   const declared=f.template.extensions.resourceRoles.profiles[role].parameterBindingsBySectionKind[snapshot.sectionKind];
   const inputs=[...group.querySelectorAll('input')];
   if(inputs.length!==Object.keys(declared).length||inputs.some(input=>declared[input.dataset.profileParameterKey]!==input.dataset.tubeDesignerParameter))throw new Error('Product does not own its finite editor schema');
   if(group.querySelector('svg,button,details.tube-designer-profile-diagram-disclosure')||f.mount.querySelector('[data-floating-parameter-diagram]'))throw new Error('Product still offers a profile diagram');
   const width=inputs.find(input=>input.dataset.profileParameterKey==='width');
   width.focus({preventScroll:true});
   await f.change(width,Number(width.value)+1,'left');
   const actual=f.view.scene.tubeDesigner.product.parameters;
   if(actual[declared.width]!==evaluation.parameters.width||actual.tubeDesignerProfileOverrides[role].parameters.width!==evaluation.parameters.width)throw new Error('Product input and geometry snapshot diverged');
   if(f.mount.querySelector('[data-floating-parameter-diagram], [data-product-profile-diagram-open]'))throw new Error('A preview response brought back a removed product diagram');
   if(!f.view.tubeDesignerRightDraftDirty || !f.view.scene.tubeDesigner.product.modelOutdated)
     throw new Error('Material edits did not request display regeneration');
   // Hold the geometry evaluation itself; submit a newer identity edit while
   // that request is pending, then return the two queued EC responses in order.
   holdNextEvaluation=true;width.value=String(evaluation.next.parameters.width);
   const geometryEdit=f.actions.handleDesignerAreaAction(f.context,f.view,width.dataset.camChangeAction,width,f.ops);
   for(let i=0;!finishEvaluation&&i<200;i++)await new Promise(resolve=>setTimeout(resolve,5));
   if(!finishEvaluation)throw new Error('Native evaluation was not reached');
   const code=f.ordinary('productCode');code.value='QUEUE-OWNED';
   const identityEdit=f.actions.handleDesignerAreaAction(f.context,f.view,code.dataset.camChangeAction,code,f.ops);
   const note=f.mount.querySelector('#right-note');note.focus({preventScroll:true});note.value='right pending draft';note.setSelectionRange(2,8,'backward');f.setScrolls([140,180,280,210]);
   finishEvaluation();await f.waitForPending();f.pending.shift()();await geometryEdit;
   if(f.ordinary('productCode').value!=='QUEUE-OWNED')throw new Error('Older geometry response overwrote a newer product draft');
   await f.waitForPending();f.pending.shift()();await identityEdit;f.verifyInteraction('right',[140,180,280,210]);
   if(f.view.scene.tubeDesigner.product.parameters.productCode!=='QUEUE-OWNED'||f.view.scene.tubeDesigner.product.parameters.tubeDesignerProfileOverrides[role].parameters.width!==evaluation.next.parameters.width)throw new Error('Queued product and geometry values diverged');
   await f.change(selector,`user:${userId}`,'right');
   if(f.view.scene.tubeDesigner.product.parameters.tubeDesignerProfileOverrides[role].profileScope!=='user')throw new Error('My profile selection lost its ownership');
   const before=structuredClone(f.view.scene.tubeDesigner.product.parameters);
   await f.actions.handleDesignerAreaAction(f.context,f.view,'tube-designer-profile-selection-change',{value:'builtin:rect',dataset:selector.dataset},f.ops);
   f.same(f.view.scene.tubeDesigner.product.parameters,before,'A removed source selection changed the product');
   if(!f.view.error.includes('只能'))throw new Error('Removed template source was not rejected');
   return {templateId:f.template.id,role,roleEditors,sectionEditors,finiteProductInputs:inputs.map(input=>input.dataset.tubeDesignerParameter),supportedSections:allowed,
    productProfileDiagramAbsent:true,conciseLabelsAndSingleUnit:true,creationOnlyStructure:true,dimensionsEditableThroughAnnotations:true,materialEditExpiresModel:true,asynchronousInteractionPreserved:true,heldEvaluationAndQueuedEditsPreserved:true,systemAndUserSources:true,removedSourcesRejected:true,
    commits:structuredClone(f.requests),evaluations:evalRequests};
  },{fixture,catalogue,evaluation:evaluations[id]});
  assert.deepEqual(errors,[]);report.cases.push(result);
  await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{height:850px}.cam-context-pane:after,.cam-info-pane:after,.tube-designer-parameter-scroll:after{display:none}.tube-designer-parameter-scroll{height:760px!important;max-height:760px!important}.tube-designer-parameter-sections{min-height:0}.nested-left>div{min-height:0}'});
  await page.evaluate(()=>{const f=window.productFixture;f.view.error='';f.render();f.mount.querySelectorAll('details').forEach(node=>{node.open=true;});f.setScrolls([0,0,0,0]);});
  await page.screenshot({path:resolve(output,`${id}.png`)});await page.close();
 }
 report.status='passed';console.log(`PASS ${report.cases.length} product profile boundaries, compact labels, no diagrams and asynchronous interactions`);
} finally {await browser.close();writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));}
