import assert from 'node:assert/strict';
import {existsSync,readFileSync,readdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {productProfileParameterBindings} from '../../apps/tube-designer/webpage/productResourceBindings.mjs';

const root=resolve('src/apps/tube-designer/templates/product');
let templates=0,roles=0,sections=0;
for(const entry of readdirSync(root,{withFileTypes:true}).filter(entry=>entry.isDirectory()&&!entry.name.startsWith('_'))){
 const path=resolve(root,entry.name,'template.json');
 if(!existsSync(path))continue;
 const template=JSON.parse(readFileSync(path));
 templates++;
 for(const [role,declaration] of Object.entries(template.extensions.resourceRoles.profiles)){
  roles++;
  const selector=template.parameters.find(field=>field.presentation?.resourceRole===role);
  const fields=new Map(template.parameters.map(field=>[field.key,field]));
  for(const [kind,mapping] of Object.entries(declaration.parameterBindingsBySectionKind)){
   sections++;
   assert.deepEqual(Object.keys(declaration.parameterLabelsBySectionKind[kind]).sort(),Object.keys(mapping).sort());
   assert.equal(new Set(Object.values(mapping).map(key=>fields.get(key).unit)).size,1,
    `${template.id}/${role}/${kind}: a shared unit heading must describe every field`);
   const profile={sectionKind:kind,parameterDefinitions:Object.keys(mapping).map(key=>({key}))};
   const bindings=productProfileParameterBindings(template,selector,profile);
   assert.equal(bindings.length,Object.keys(mapping).length);
   for(const binding of bindings){
    assert.ok(binding.label&&binding.label.length<10);
    assert.doesNotMatch(binding.label,/mm|扶手|梯梁|立柱|管\//);
    assert.equal(binding.field,fields.get(binding.productParameter));
   }
   // Renaming the template or constituent role cannot change editor semantics.
   const renamed={...template,id:'independent-product',extensions:{...template.extensions,
    resourceRoles:{...template.extensions.resourceRoles,profiles:{'independent-role':declaration}}}};
   const renamedField={...selector,presentation:{...selector.presentation,resourceRole:'independent-role'}};
   assert.deepEqual(productProfileParameterBindings(renamed,renamedField,profile),bindings);
   // A custom label must be rendered verbatim, not chosen by a global key table.
   const changed=structuredClone(template);
   const key=Object.keys(mapping)[0];
   changed.extensions.resourceRoles.profiles[role].parameterLabelsBySectionKind[kind][key]={'zh-CN':'构件专用尺寸'};
   assert.equal(productProfileParameterBindings(changed,selector,profile)[0].label,'构件专用尺寸');
  }
 }
}
console.log(`PASS ${templates} product templates, ${roles} roles and ${sections} declared section presentations`);
