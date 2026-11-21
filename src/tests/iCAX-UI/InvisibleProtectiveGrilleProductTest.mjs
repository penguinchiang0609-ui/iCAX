import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {catalogText} from '../../apps/tube-designer/webpage/productCatalog.mjs';
import {renderDesignerRightParameterContent,renderDesignerAddParameterContent} from '../../apps/tube-designer/webpage/designerViews.mjs';
import {resolveProductSpecificationAnnotations,productSceneMemberIds,productParameterTargetsForSceneMember,renderProductInstanceThumbnail}
  from '../../apps/tube-designer/webpage/productParameterDiagram.mjs';
import {creationOnlyParameterKeys,manufacturingOnlyParameterKeys,productDisplayParameters,validateProductPostCreationChanges}
  from '../../apps/tube-designer/webpage/productParameterDependencies.mjs';
import {parameterVisible} from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import {invisibleGrilleDiagramStocks} from '../../apps/tube-designer/webpage/invisibleGrilleDiagram.mjs';

const folder=new URL('../../apps/tube-designer/templates/product/invisible_protective_grille/',import.meta.url);
const raw=JSON.parse(readFileSync(new URL('template.json',folder))),display=JSON.parse(readFileSync(new URL('display.json',folder)));
const template={...raw,display,available:true,groups:raw.groups.map(g=>({...g,displayName:catalogText(g.displayName)})),
  parameters:raw.parameters.map(f=>({...f,groupKey:f.group,type:{enum:'select',string:'text'}[f.valueType]??f.valueType,
    displayName:catalogText(f.displayName),options:f.choices?.map(c=>({...c,label:catalogText(c.displayName)}))}))};
const fields=new Map(raw.parameters.map(f=>[f.key,f])),defaults=Object.fromEntries(raw.parameters.map(f=>[f.key,f.defaultValue]));
assert.equal(raw.id,'invisible-protective-grille');assert.equal(raw.version,'1.0.0');assert.equal(raw.parameters.length,12);
assert.equal(defaults.wireProfileType,'round-bar');assert.equal(defaults.wireWidth,2);assert.equal(defaults.wireCenterSpacing,50);
assert.equal(defaults.clampProfileType,'rect-bar');assert.equal(defaults.clampWidth,20);assert.equal(defaults.clampDepth,3);
assert.equal(defaults.clampCount,1);assert.equal(defaults.clampBottomOffset,500);assert.equal(defaults.clampTopOffset,500);
assert.equal(defaults.width,1200);assert.equal(defaults.height,1200);
assert.deepEqual([...creationOnlyParameterKeys(template)],[]);assert.deepEqual([...manufacturingOnlyParameterKeys(template)],[]);
const parameters=['width','height','wireWidth','wireCenterSpacing','clampCount','clampBottomOffset','clampTopOffset'];
for(const count of [0,1,3]) {
  const values={...defaults,clampCount:count};
  const anchors=raw.extensions.sceneSpecificationAnnotations.annotations.map(a=>({...a,start:[0,0,0],end:[100,0,0],generatedValue:values[a.parameter]}));
  const designer={templates:[template],product:{entityId:'wire-test',templateId:raw.id,parameters:values},specificationAnnotations:anchors};
  const view={activeAreaId:'view',scene:{tubeDesigner:designer},tubeDesignerAddTemplateId:raw.id,tubeDesignerAddDraft:values};
  const resolved=resolveProductSpecificationAnnotations(designer,view),right=renderDesignerRightParameterContent(designer,view).scrollContent;
  for(const key of parameters) {
    const active=key==='clampBottomOffset'?count>0:key==='clampTopOffset'?count>1:true;
    assert.equal(parameterVisible(fields.get(key),values),active,`${key}/${count}`);
    assert.equal(resolved.some(a=>a.parameter===key),active,`${key}: active scene annotation`);
    assert.ok(!right.includes(`data-tube-designer-parameter="${key}"`),`${key}: no duplicate sidebar input`);
    const changed={...values,[key]:values[key]+1};
    assert.doesNotThrow(()=>validateProductPostCreationChanges(template,values,changed));
    if(active)assert.notDeepEqual(productDisplayParameters(template,values),productDisplayParameters(template,changed));
  }
  assert.ok(!renderDesignerAddParameterContent(designer,view).includes('data-tube-designer-parameter="wireWidth"'));
}
const wireSnapshot={schema:'icax.imported-tube-profile',schemaVersion:1,kind:'profile-package',profileForm:'parametric',
  profileDefinitionId:'round-bar',profileScope:'system',sectionKind:'round-bar',width:2,depth:2,contourCount:1,hollow:false,
  contours:[{kind:'circle',center:[0,0],radius:1}],parameterDefinitions:[{key:'width',valueType:'number'}],parameterValues:{width:2}};
const clampSnapshot={...wireSnapshot,profileDefinitionId:'rect-bar',sectionKind:'rect-bar',width:20,depth:3,
  contours:[{kind:'roundedRectangle',center:[0,0],width:20,height:3,radius:0}],
  parameterDefinitions:[{key:'width',valueType:'number'},{key:'depth',valueType:'number'},{key:'useHotRolled',valueType:'boolean'}],parameterValues:{width:20,depth:3,useHotRolled:false}};
const snapshotValues={...defaults,tubeDesignerProfileOverrides:{wire:wireSnapshot,clamp:clampSnapshot}};
const snapshotDesigner={templates:[template],product:{entityId:'bound-wire',templateId:raw.id,parameters:snapshotValues}};
const snapshotRight=renderDesignerRightParameterContent(snapshotDesigner,{activeAreaId:'view',scene:{tubeDesigner:snapshotDesigner}}).scrollContent;
assert.ok(!snapshotRight.includes('data-tube-designer-parameter="wireWidth"'),'Parametric resource binding must not reintroduce a scene-owned line diameter input');
assert.ok(snapshotRight.includes('data-tube-designer-parameter="clampWidth"')&&snapshotRight.includes('data-tube-designer-parameter="clampDepth"'));
assert.ok(!snapshotRight.includes('useHotRolled'));
for(const [role,kind,bound]of [['wire','round-bar',{width:'wireWidth'}],['clamp','rect-bar',{width:'clampWidth',depth:'clampDepth'}]]) {
  const selector=fields.get(role+'ProfileType'),declaration=raw.extensions.resourceRoles.profiles[role];
  assert.deepEqual(selector.presentation.profileConstraints.sectionKinds,[kind]);assert.equal(selector.presentation.profileConstraints.hollow,false);
  assert.deepEqual(declaration.parameterBindingsBySectionKind[kind],bound);
}
const members=[['wire.vertical.0001','wire'],['clamp.bottom.front','clamp'],['clamp.top.back','clamp'],['clamp.middle.0001.front','clamp']]
  .map(([stableKey,role])=>({entityId:stableKey,stableKey,role}));
for(const role of ['wire','clamp']) {
  assert.deepEqual(productSceneMemberIds(template,members,'',{profileRole:role}),members.filter(m=>m.role===role).map(m=>m.entityId));
  for(const member of members.filter(m=>m.role===role))assert.deepEqual(productParameterTargetsForSceneMember(template,member).profileRoles,[role]);
}
let layouts=0;
for(const wireWidth of [1.5,2,3])for(const wireCenterSpacing of [30,50,80])for(const clampCount of [0,1,3]) {
  const state=invisibleGrilleDiagramStocks(raw.extensions.productDiagram,{...defaults,wireWidth,wireCenterSpacing,clampCount,
    clampBottomOffset:clampCount?300:-1,clampTopOffset:clampCount>1?300:-1});
  assert.equal(state.members.filter(m=>m.profileRole==='clamp').length,2*(clampCount+2));
  const wires=state.members.filter(m=>m.profileRole==='wire');
  wires.forEach(m=>{assert.equal(m.start[2],0);assert.equal(m.end[2],defaults.height);assert.equal(m.width,wireWidth);});
  state.wireCenters.slice(1).forEach((v,i)=>assert.ok(Math.abs(v-state.wireCenters[i]-wireCenterSpacing)<1e-7));
  state.members.filter(m=>m.profileRole==='clamp').forEach(m=>assert.ok(Math.abs(Math.abs(m.start[1])-m.depth/2-wireWidth/2)<1e-7));
  layouts++;
}
assert.match(renderProductInstanceThumbnail(template,defaults),/data-invisible-grille-diagram/);
assert.ok(!raw.parameters.some(f=>/material|weld|tension|hole/i.test(f.key)),'No unsupported material-grade, welded-joint or fixture fields');
const manifest=JSON.parse(readFileSync(new URL('../../apps/tube-designer/product.manifest.json',import.meta.url)));
assert.equal(manifest.capabilities.tubeDesigner.templates.find(t=>t.templateId===raw.id)?.version,raw.version);
console.log(`PASS invisible grille: 12 declared fields, ${layouts} wire/clamp layouts, scene ownership, finite profile binding and independent catalogue entry`);
