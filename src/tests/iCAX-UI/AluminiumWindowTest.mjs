import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {matchesParameterCondition as matches} from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import {buildCatalogEntries} from '../../apps/tube-designer/webpage/productCatalog.mjs';
const d=JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/product/aluminium_window/template.json',import.meta.url),'utf8'));
const defaults=Object.fromEntries(d.parameters.map(p=>[p.key,p.defaultValue]));
const visible=(key,p)=>matches(d.parameters.find(p=>p.key===key).visibleWhen,{...defaults,...p});
assert.deepEqual(buildCatalogEntries([{...d,available:true}])[0].catalogPath,['窗','普通铝合金窗']);
for(const type of ['fixed','sliding','hinged']){
  assert.equal(visible('slidingCount',{windowType:type}),type==='sliding');
  assert.equal(visible('hingedCount',{windowType:type}),type==='hinged');
  assert.equal(visible('screenCount',{windowType:type}),type==='sliding');
}
assert.equal(visible('screenCount',{screenEnabled:false}),false);
assert.equal(visible('systemFile',{systemSource:'demonstration'}),false);
assert.equal(visible('systemFile',{systemSource:'file'}),true);
assert.equal(visible('openingSide',{windowType:'hinged',hingedCount:2}),false);
assert.equal(visible('columnWeight1',{columns:1}),false);
assert.equal(visible('rowWeight3',{rows:2}),false);
for(const columns of [1,2,3])for(const rows of [1,2,3])
 for(let row=1;row<=3;row++)for(let col=1;col<=3;col++)
  assert.equal(visible(`cell${row}${col}`,{windowType:'mixed',columns,rows}),row<=rows&&col<=columns);
// Inactive cells must not expose another family's fields.
assert.equal(visible('hingedCount',{windowType:'mixed',columns:1,rows:1,cell11:'fixed',cell33:'hinged'}),false);
assert.equal(visible('hingedCount',{windowType:'mixed',columns:3,rows:3,cell11:'fixed',cell33:'hinged'}),true);
console.log('Aluminium window catalogue, aperture and panel conditions passed.');
assert.equal(visible('cell21',{windowType:'mixed',columns:2,rows:2,mergeTopLight:true}),false);
assert.equal(visible('slidingCount',{windowType:'mixed',columns:2,rows:2,mergeTopLight:true,cell11:'fixed',cell12:'fixed',cell21:'sliding',cell22:'sliding'}),false);
