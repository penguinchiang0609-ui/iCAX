import assert from 'node:assert/strict';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { catalogText } from '../../apps/tube-designer/webpage/productCatalog.mjs';
import { renderDesignerRightParameterContent,
  renderDesignerAddParameterContent, getReusablePresetValues, applyReusablePresetValues,
  getDefaultParameters } from '../../apps/tube-designer/webpage/designerViews.mjs';
import { resolveProductSpecificationAnnotations } from '../../apps/tube-designer/webpage/productParameterDiagram.mjs';
import { getDesignerDefaultViewDirection } from '../../apps/tube-designer/webpage/designerActions.mjs';
import { creationOnlyParameterKeys, manufacturingOnlyParameterKeys, validateProductPostCreationChanges } from '../../apps/tube-designer/webpage/productParameterDependencies.mjs';
import { parameterVisible } from '../../apps/tube-designer/webpage/parameterConditions.mjs';

const root=new URL('../../apps/tube-designer/templates/product/',import.meta.url);
let count=0,anchors=0,nativeCases=0,installationCases=0;
for(const dir of readdirSync(root,{withFileTypes:true}).filter(dir=>dir.isDirectory())) {
  const file=new URL(`${dir.name}/template.json`,root);if(!existsSync(file))continue;
  const raw=JSON.parse(readFileSync(file));
  const template={...raw,display:JSON.parse(readFileSync(new URL(`${dir.name}/display.json`,root))),
    groups:raw.groups.map(group=>({...group,displayName:catalogText(group.displayName)})),
    parameters:raw.parameters.map(field=>({...field,groupKey:field.group,
      type:{enum:'select',string:'text'}[field.valueType]??field.valueType,displayName:catalogText(field.displayName),
      options:field.choices?.map(choice=>({...choice,label:catalogText(choice.displayName)}))}))};
  const values=getDefaultParameters([template],template.id);
  const evidence=process.env.ICAX_NATIVE_SCENE_EVIDENCE
    ? resolve(process.env.ICAX_NATIVE_SCENE_EVIDENCE,`${template.id}.json`) : null;
  // Standalone unit cases use declared fields with test anchors. Integration
  // runs supply the actual native output; the browser test requires it.
  const nativeEvidence=evidence && existsSync(evidence);
  const sceneAnchors=nativeEvidence?JSON.parse(readFileSync(evidence)).specificationAnnotations
    : [...new Set([...Object.keys(template.display.views.scene.annotations ?? {}),
      ...(template.extensions.sceneSpecificationAnnotations?.annotations ?? []).map(a=>a.parameter)])].map(parameter=>({
      parameter,start:[0,0,0],end:[1000,0,0],offset:[0,0,-100],generatedValue:values[parameter]}));
  if(nativeEvidence)nativeCases++;
  const definitions=new Map(raw.parameters.map(field=>[field.key,field]));
  const checkConditions=value=>{
    if(!value||typeof value!=='object')return;
    if(value.op&&value.parameter){
      const field=definitions.get(value.parameter);assert.ok(field,`Unknown condition parameter ${value.parameter}`);
      if(field.valueType==='enum')for(const expected of value.values??[value.value])
        assert.ok(field.choices.some(choice=>Object.is(choice.value,expected)),`${template.id}: invalid choice in display condition`);
    }
    for(const child of Object.values(value))checkConditions(child);
  };
  checkConditions(template.display);
  checkConditions(template.extensions.productDiagram);
  const designer={templates:[template],product:{entityId:'invariant-product',templateId:template.id,parameters:values},
    specificationAnnotations:sceneAnchors};
  const view={activeAreaId:'view',scene:{tubeDesigner:designer}};
  const before=structuredClone(template),input=structuredClone(values);
  const right=renderDesignerRightParameterContent(designer,view).scrollContent;
  assert.ok(!right.includes('data-tube-designer-parameter-group="section:specifications"'));
  assert.ok(!right.includes('data-tube-designer-parameter-group="section:structure"'));
  const resolved=resolveProductSpecificationAnnotations(designer,view);
  const locked=creationOnlyParameterKeys(template),manufacturing=manufacturingOnlyParameterKeys(template);
  const category = field => template.display.views.right.fields?.[field.key]?.category
    ?? template.extensions.parameterLayout.sections.find(section=>section.groups.includes(field.group))?.key;
  for (const field of template.parameters.filter(field=>category(field)==='materials'))
    assert.ok(!manufacturing.has(field.key), `${template.id}: material must invalidate the display ${field.key}`);
  assert.ok(!manufacturing.has('tubeDesignerProfileOverrides'));
  const creationControls=template.extensions.addDialog.structureParameters;
  if(definitions.has('installation')) {
    installationCases++;
    assert.ok(locked.has('installation') && !manufacturing.has('installation'));
    assert.ok(creationControls.includes('installation'));
    const creation=renderDesignerAddParameterContent({...designer,templates:[{...template,available:true}]},
      {...view,tubeDesignerAddTemplateId:template.id,tubeDesignerAddDraft:values});
    assert.ok(creation.includes('data-tube-designer-parameter="installation"'));
    assert.ok(!right.includes('data-tube-designer-parameter="installation"'));
    const other=definitions.get('installation').choices.find(c=>c.value!==values.installation).value;
    const preset=getReusablePresetValues(template,{...values,installation:other},'process');
    assert.ok(!Object.hasOwn(preset,'installation'));
    assert.equal(applyReusablePresetValues(template,values,{installation:other},'process').installation,values.installation);
  }
  const covered=new Set(creationControls);
  for(const control of template.extensions.productControls??[])if(creationControls.includes(control.key))
    for(const choice of control.choices){
      Object.keys(choice.parameters??{}).forEach(key=>covered.add(key));
      for(const assignment of choice.assignments??[])Object.keys(assignment.parameters??{}).forEach(key=>covered.add(key));
    }
  for(const field of template.parameters.filter(f=>locked.has(f.key)&&f.presentation?.visible!==false&&(f.valueType==='boolean'||f.choices?.length>1)))
    assert.ok(covered.has(field.key),`${template.id}: creation-only choice has no creation control ${field.key}`);
  for(const field of template.parameters.filter(f=>['number','integer'].includes(f.valueType)&&!manufacturing.has(f.key)&&category(f)!=='materials'&&!f.readOnly&&parameterVisible(f,values))) {
    assert.ok(resolved.some(a=>a.parameter===field.key&&a.editable),`${template.id}: missing numeric annotation ${field.key}`);
    assert.doesNotThrow(()=>validateProductPostCreationChanges(template,values,{...values,[field.key]:Number(values[field.key])+1}));
  }
  for(const key of locked)assert.throws(()=>validateProductPostCreationChanges(template,values,{...values,[key]:'invalid-structure-change'}));
  for(const annotation of resolved.filter(annotation=>annotation.editable)) {
    assert.ok(!right.includes(`data-tube-designer-parameter="${annotation.parameter}"`),
      `${template.id}: editable annotations have a single input owner`);anchors++;
  }
  // Changing identity alone must not change fields, categories, behavior or view.
  const renamed={...template,id:'unrecognised-personal-product'};
  const other={...designer,templates:[renamed],product:{...designer.product,templateId:renamed.id}};
  const otherView={...view,scene:{tubeDesigner:other}};
  const normalize=html=>html.replaceAll(template.id,renamed.id);
  assert.equal(normalize(right),renderDesignerRightParameterContent(other,otherView).scrollContent);
  assert.deepEqual([...creationOnlyParameterKeys(template)],[...creationOnlyParameterKeys(renamed)]);
  assert.deepEqual(getDesignerDefaultViewDirection(designer.product,template),getDesignerDefaultViewDirection(other.product,renamed));
  assert.deepEqual(values,input);assert.deepEqual(template,before);
  assert.deepEqual(getDefaultParameters([template],'unknown-template'),{});
  count++;
}
assert.equal(count,9);assert.ok(anchors>=30);
assert.equal(installationCases,4);
console.log(`PASS ${count} product mechanism contracts and ${anchors} annotation ownership checks (${nativeCases} native cases); renamed IDs behave identically`);
