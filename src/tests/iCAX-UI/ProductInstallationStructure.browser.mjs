// Real browser controls against the installation's production assets. Native
// generation and rejection are verified separately with these exact inputs.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';

const ids=['modular-guardrail','modular-guardrail-cross-straight','modular-guardrail-diamond-straight','modular-guardrail-glass-straight'];
const evidence=resolve(process.env.ICAX_NATIVE_SCENE_EVIDENCE || 'output/tests/product-installation-structure-20261006');
const output=browserReportDirectory('product-installation-structure-20261006/browser');
const {tubeDesignerCss}=await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL || 'chrome'});
const cases=[];
try {
  for(const id of ids)for(const mode of ['embedded','base_plate','side_plate']) {
    const scene=JSON.parse(readFileSync(resolve(evidence,`${id}.installation.${mode}.json`),'utf8'));
    const directory=id==='modular-guardrail'?'modular_guardrail':id.replace('modular-guardrail-','modular_guardrail_');
    const descriptor={...JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${directory}/template.json`)),
      display:JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${directory}/display.json`))};
    assert.deepEqual(descriptor,scene.descriptor);
    const page=await browser.newPage({viewport:{width:1600,height:950}});
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    await page.route('http://installation-structure.test/**',serveBrowserAsset);
    await page.goto('http://installation-structure.test/');
    await page.addStyleTag({content:`${tubeDesignerCss}*{box-sizing:border-box}body{margin:0}.cam-workbench{display:flex;gap:8px}.cam-context-pane,.cam-info-pane{height:260px;overflow:auto;width:320px;flex:none}.cam-viewport{width:920px;height:600px;position:relative;flex:none}.nested-left{height:150px;overflow:auto}.nested-left>div{height:1600px}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1600px}.tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:1600px}`});
    await installProductInteractionFixture(page,descriptor,scene,{initialParameters:scene.product.parameters});
    const result=await page.evaluate(async mode=>{
      const f=window.productFixture;
      const {resolveProductSpecificationAnnotations}=await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
      const {validateProductPostCreationChanges}=await import('/src/apps/tube-designer/webpage/productParameterDependencies.mjs');
      const before=structuredClone(f.view.scene.tubeDesigner.product.parameters);
      if(before.installation!==mode)throw new Error('Fixture did not use the actual native installation instance');
      if(f.ordinary('installation'))throw new Error('Installation structure leaked into the right pane');
      const other=mode==='embedded'?'base_plate':'embedded';
      let rejected=false;
      try {validateProductPostCreationChanges(f.template,before,{...before,installation:other});}catch {rejected=true;}
      if(!rejected)throw new Error('Frontend did not enforce the declared creation boundary');
      const annotations=resolveProductSpecificationAnnotations(f.view.scene.tubeDesigner,f.view);
      const numeric=mode==='side_plate'?'sidePlateHeight':mode==='base_plate'?'basePlateSize':'sideBayCount1';
      if(!annotations.some(a=>a.parameter===numeric&&a.editable))throw new Error('Installation numeric annotation was locked or absent');
      if(f.ordinary(numeric))throw new Error('Installation dimension was duplicated on the right');
      const saved=f.views.getReusablePresetValues(f.template,before,'process');
      if(Object.hasOwn(saved,'installation'))throw new Error('Assembly preset captured the installation structure');
      const applied=f.views.applyReusablePresetValues(f.template,before,{installation:other},'process');
      if(applied.installation!==mode)throw new Error('Assembly preset changed the installation structure');
      // Keep both panes, nested scroll containers and later typing stable while
      // a genuine assembly update is in flight.
      if(mode!=='embedded') {
        const node=f.ordinary('baseBoltDiameter');
        if(!node)throw new Error('Applicable assembly hole editor is missing');
        await f.change(node,Number(node.value)+1,'right');
        if(f.view.tubeDesignerRightDraftDirty || f.view.scene.tubeDesigner.product.modelOutdated)
          throw new Error(`Assembly edit expired the structural model: ${JSON.stringify({dirty:f.view.tubeDesignerRightDraftDirty,
            outdated:f.view.scene.tubeDesigner.product.modelOutdated,changed:Object.keys(before).filter(key=>JSON.stringify(before[key])!==JSON.stringify(f.requests.at(-1)?.parameters[key]))})}`);
        if(f.ordinary('baseBoltDiameter')!==node)throw new Error('Assembly update replaced the focused editor');
      }
      f.assertPhysicalCommits();
      f.view.tubeDesignerAddDialogOpen=true;f.view.tubeDesignerAddTemplateId=f.template.id;
      f.view.tubeDesignerAddDraft=Object.fromEntries(f.template.parameters.map(field=>[field.key,field.defaultValue]));
      f.render();
      const node=f.mount.querySelector('[data-tube-designer-add-form] select[data-tube-designer-parameter="installation"]');
      if(!node || !node.closest('[data-tube-designer-parameter-group="add:guardrail-post"]'))throw new Error('Installation is absent from the creation post group');
      const choices=[...node.options].map(option=>option.value);
      f.same(choices,['embedded','base_plate','side_plate'],'Creation choices differ from the validated descriptor');
      f.addOperations=[];
      f.mount.addEventListener('change',event=>{
        const target=event.target.closest?.('[data-tube-designer-add-form] [data-tube-designer-parameter]');
        if(target)f.addOperations.push(f.actions.handleDesignerAreaAction(f.context,f.view,target.dataset.camChangeAction,target,f.ops));
      });
      return {templateId:f.template.id,installation:mode,numericAnnotation:numeric,creationChoices:choices,
        rightStructureAbsent:true,assemblyPresetCannotChangeStructure:true,assemblyPreservesModelAndInteraction:mode!=='embedded'};
    },mode);
    await page.locator('[data-tube-designer-add-form] select[data-tube-designer-parameter="installation"]').selectOption(mode);
    await page.evaluate(()=>Promise.all(window.productFixture.addOperations));
    const created=await page.evaluate(async()=>{
      const f=window.productFixture,expected=structuredClone(f.view.tubeDesignerAddDraft);
      // Add confirmation is a host protocol test, not a rendered CAD receipt.
      // The prior assembly checks retain the genuine WebGL canvas; generation
      // of the exact submitted inputs is asserted by the native acceptance.
      let sequence=1;
      f.view.viewport={fitViewForRevision:revision=>({fitted:true,revision,renderSequence:sequence++}),
        setViewDirection:()=>{sequence++;return true;},
        getAppliedViewState:()=>({revision:'fixture-1',renderSequence:sequence})};
      const target=f.mount.querySelector('[data-cam-action="tube-designer-confirm-add"]');
      await f.actions.handleDesignerAreaAction(f.context,f.view,target.dataset.camAction,target,f.ops);
      if(f.creations.length!==1)throw new Error('Creation did not submit exactly one request');
      const request=f.creations[0];
      for(const field of f.template.parameters)if(JSON.stringify(request[field.key])!==JSON.stringify(expected[field.key]))throw new Error(`Creation changed physical field ${field.key}`);
      if(f.ordinary('installation') || f.view.scene.tubeDesigner.product.parameters.installation!==expected.installation)
        throw new Error('Installation did not become fixed after creation');
      return request;
    });
    result.creationRequest=created;
    result.creationViewportTransport='controlled protocol receipt; exact physical inputs verified by native acceptance';
    assert.equal(created.installation,mode);assert.deepEqual(errors,[]);
    cases.push(result);await page.close();
  }
  writeFileSync(resolve(output,'report.json'),JSON.stringify({status:'passed',cases},null,2));
  console.log(`PASS ${cases.length} native-backed installation creation/browser cases; post-creation presets and assembly keep the declared boundary`);
} finally {await browser.close();}
