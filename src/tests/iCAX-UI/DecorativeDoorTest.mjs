import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {matchesParameterCondition as matches} from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import {buildCatalogEntries} from '../../apps/tube-designer/webpage/productCatalog.mjs';
const d=JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/product/decorative_door/template.json',import.meta.url),'utf8'));
const defaults=Object.fromEntries(d.parameters.map(p=>[p.key,p.defaultValue]));
const visible=(key,p)=>matches(d.parameters.find(p=>p.key===key).visibleWhen,{...defaults,...p});
assert.deepEqual(buildCatalogEntries([{...d,available:true}])[0].catalogPath,['门','雕花装饰门']);
for(const pattern of ['lines','diamond','octagon','round_scene','panels','glass_lattice']){
 assert.equal(visible('vAngle',{pattern,lineTool:'v'}),pattern==='lines');
 assert.equal(visible('trimEnabled',{pattern}),pattern==='panels');
 assert.equal(visible('glassThickness',{pattern}),pattern==='glass_lattice');
 assert.equal(visible('regionProcess',{pattern}),['diamond','octagon'].includes(pattern));
}
assert.equal(visible('cutDepth',{pattern:'diamond',regionProcess:'through'}),false);
assert.equal(visible('cutDepth',{pattern:'diamond',regionProcess:'recess'}),true);
assert.equal(visible('composition',{doorType:'single'}),false);
assert.equal(visible('lockWidth',{protectHardware:false}),false);
assert.equal(visible('trimWidth',{pattern:'panels',trimEnabled:false}),false);
assert.equal(visible('motherRatio',{doorType:'double'}),false);
assert.equal(visible('linePitch',{arrayMode:'count'}),false);
console.log('Decorative door catalogue and parameter conditions passed.');
