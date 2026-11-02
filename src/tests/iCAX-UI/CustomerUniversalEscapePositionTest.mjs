import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {parameterVisible} from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import {productParameterKind} from '../../apps/tube-designer/webpage/productParameterClassification.mjs';
import {renderProductParameterDiagram,productSceneMemberIds,resolveProductSpecificationAnnotations} from '../../apps/tube-designer/webpage/productParameterDiagram.mjs';
import {validateProductPostCreationChanges} from '../../apps/tube-designer/webpage/productParameterDependencies.mjs';
const t=JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/product/single_face_security_window/template.json',import.meta.url)));
t.display=JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/product/single_face_security_window/display.json',import.meta.url)));
const defaults=Object.fromEntries(t.parameters.map(f=>[f.key,f.defaultValue]));
assert.equal(productParameterKind(t.parameters.find(f=>f.key==='doorPosition'),t),'dimension');
assert(!t.extensions.parameterDependencies.creationOnly.includes('doorPosition'));
assert(!t.extensions.parameterDependencies.manufacturingOnly.includes('doorPosition'));
const numeric=['doorLeftMargin','doorRightMargin','doorBottomMargin','doorTopMargin','doorHorizontalOffset','doorVerticalOffset'];
let cases=0;
for(const faceType of ['single','two','three','five'])for(const infillPattern of ['grid','horizontal','vertical'])
for(const x of ['left','center','right'])for(const z of ['bottom','middle','top']){
 const p={...defaults,faceType,infillPattern,doorPosition:x+'_'+z};
 assert.doesNotThrow(()=>validateProductPostCreationChanges(t,p,{...p,doorPosition:'center_middle'}));
 const annotation=resolveProductSpecificationAnnotations({templates:[t],product:{entityId:'window',templateId:t.id,parameters:p},
  specificationAnnotations:[{parameter:'doorPosition',kind:'parameter',start:[0,0,0],end:[0,0,0],generatedValue:p.doorPosition}]})[0];
 assert(annotation.editable);assert.equal(annotation.editor.type,'select');assert.equal(annotation.editor.options.length,9);
 const active=numeric.filter(k=>parameterVisible(t.parameters.find(f=>f.key===k),p));
 const expected=[{left:'doorLeftMargin',right:'doorRightMargin',center:'doorHorizontalOffset'}[x],
 {bottom:'doorBottomMargin',top:'doorTopMargin',middle:'doorVerticalOffset'}[z]];
 assert.deepEqual(active.sort(),expected.sort());
 assert(parameterVisible(t.parameters.find(f=>f.key==='doorPosition'),p));
 for(const key of ['doorLeft','doorBottom','doorUOffset','doorVOffset'])assert(!parameterVisible(t.parameters.find(f=>f.key===key),p));
 for(const key of numeric){assert.equal(productParameterKind(t.parameters.find(f=>f.key===key),t),'dimension');assert(!t.extensions.parameterDependencies.creationOnly.includes(key));}
 cases++;
}
for(const faceType of ['single','two','three','five'])for(const face of ['front','left','right','bottom']){
 if(faceType==='single'&&face!=='front'||faceType==='two'&&!['front','right'].includes(face)||faceType==='three'&&face==='bottom')continue;
 const p={...defaults,faceType,infillPattern:'vertical',doorPosition:'right_top',accessDoorFace2:face==='front'?'front':'side',accessDoorFace3:face,accessDoorFace5:face};
 const before=renderProductParameterDiagram(t,p,{mode:'add'});
 const after=renderProductParameterDiagram(t,{...p,doorRightMargin:80,doorTopMargin:90},{mode:'add'});
 assert.notEqual(before,after,faceType+'/'+face);
 assert.match(after,/opening-grid-mask/);
}
const members=[{entityId:'door',stableKey:'access_door.leaf.frame.left.0001'}, {entityId:'cap',stableKey:'cap_grid.vertical.0001'}];
for(const key of numeric)assert.deepEqual(productSceneMemberIds(t,members,key),['door','cap']);
console.log(`Universal escape positioning: ${cases} visibility combinations, face diagrams and scene bindings passed.`);
