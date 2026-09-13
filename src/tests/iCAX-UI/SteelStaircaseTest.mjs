import assert from 'node:assert/strict';
import {readFileSync,existsSync} from 'node:fs';
import {matchesParameterCondition as matches} from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import {buildCatalogEntries} from '../../apps/tube-designer/webpage/productCatalog.mjs';
const root=new URL('../../apps/tube-designer/templates/product/',import.meta.url);
const d=JSON.parse(readFileSync(new URL('straight_steel_staircase/template.json',root),'utf8'));
const defaults=Object.fromEntries(d.parameters.map(p=>[p.key,p.defaultValue]));
const visible=(key,p)=>matches(d.parameters.find(p=>p.key===key).visibleWhen,{...defaults,...p});
assert.deepEqual(buildCatalogEntries([{...d,available:true}])[0].catalogPath,['楼梯','钢楼梯']);
for(const route of ['straight','straight_landing','l_turn','u_turn']){
 assert.equal(visible('landingLength',{stairRoute:route}),route!=='straight');
 assert.equal(visible('wellGap',{stairRoute:route}),route==='u_turn');
 assert.equal(visible('turnDirection',{stairRoute:route}),['l_turn','u_turn'].includes(route));
}
assert.equal(visible('stringerSpacing',{stringerSystem:'mono'}),false);
assert.equal(visible('boltHoleDiameter',{connectionType:'weld'}),false);
assert.equal(visible('postEveryRisers',{railingSide:'none'}),false);
assert.equal(visible('treadThickness',{treadType:'none'}),false);
assert.equal(visible('stringerProfileType',{stringerConstruction:'zigzag'}),false);
assert.equal(visible('bracketType',{stringerConstruction:'zigzag'}),false);
assert.equal(visible('bracketThickness',{bracketType:'plate',stringerConstruction:'profile'}),true);
assert.equal(visible('bracketThickness',{bracketType:'tube'}),false);
assert.equal(visible('zigzagThickness',{stringerConstruction:'profile'}),false);
for(const prefix of ['stringer','handrail','post','infill']) {
 assert.equal(visible(prefix+'Depth',{[prefix+'ProfileType']:'round'}),false);
 assert.equal(visible(prefix+'CornerRadius',{[prefix+'ProfileType']:'oval'}),false);
}
for(const name of ['l_turn_steel_staircase','u_turn_steel_staircase'])
 assert.equal(existsSync(new URL(name+'/template.json',root)),false);
console.log('Unified staircase catalogue and parameter conditions passed.');
