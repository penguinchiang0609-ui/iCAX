import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { matchesParameterCondition as matches, parameterVisible, parameterEnabled, availableParameterChoices, effectiveParameterChoice } from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import { isAdvancedParameter, renderParameterLevels } from '../../apps/tube-designer/webpage/parameterPresentation.mjs';
import { renderProductParameterDiagram } from '../../apps/tube-designer/webpage/productParameterDiagram.mjs';
const demo={parameters:[{key:'width',presentation:{advanced:true}},{key:'height',presentation:{visible:false}}],extensions:{primaryDimensions:{widthParameter:'width',heightParameter:'height'}}};
const demoDiagram=renderProductParameterDiagram(demo,{width:120,height:200});
assert.ok(demoDiagram.includes('data-product-diagram-parameter="width"'));
assert.ok(demoDiagram.includes('data-parameter-level="advanced" style="display:none"'));
assert.ok(!demoDiagram.includes('data-product-diagram-parameter="height"'));
assert.equal(parameterVisible({}), true);
assert.equal(parameterVisible({presentation:{visible:false}}), false);
assert.equal(parameterVisible({presentation:{visible:true},visibleWhen:{op:'eq',parameter:'x',value:1}}, {x:2}), false);
assert.equal(parameterVisible({presentation:{advanced:true}}, {}), true);
assert.equal(isAdvancedParameter({valueType:'string',key:'anythingJSON'}), false);
assert.equal(isAdvancedParameter({presentation:{advanced:true}}), true);
assert.equal(isAdvancedParameter({level:'advanced'}), true);
const levelFields=[{key:'normal'},{key:'expert',presentation:{advanced:true}},{key:'hidden',presentation:{advanced:true,visible:false}}];
const before=JSON.stringify(levelFields);
const levels=renderParameterLevels(levelFields,d=>`<input name="${d.key}">`,{key:'test'});
assert.ok(levels.includes('高级设置'));assert.ok(!levels.includes('name="hidden"'));
assert.equal(JSON.stringify(levelFields),before);
const flattened=renderParameterLevels(levelFields,d=>`<input name="${d.key}">`,{key:'test',flattenAdvanced:true});
assert.ok(!flattened.includes('<summary>高级设置'));
assert.ok(flattened.includes('data-parameter-advanced-item="expert"'));
assert.ok(!flattened.includes('name="hidden"'));
import { renderToolLibraryRightPane } from '../../apps/tube-designer/webpage/toolLibrary.mjs';
const root = new URL('../../apps/tube-designer/templates/', import.meta.url);
const read = path => JSON.parse(readFileSync(new URL(path, root), 'utf8'));
const defaults = d => Object.fromEntries(d.parameters.map(p => [p.key, p.defaultValue]));
const shown = (d, key, values) => matches(d.parameters.find(p => p.key === key).visibleWhen, values);
for (const key of ['key', 'name', 'parameter']) {
  assert.equal(matches({ op: 'eq', [key]: 'mode', value: 'a' }, { mode: 'a' }), true);
  assert.equal(matches({ op: 'ne', [key]: 'mode', value: 'a' }, { mode: 'a' }), false);
}
for (const c of [{ all: [{ parameter: 'x', op: 'eq', value: 2 }] }, { op: 'all', conditions: [{ parameter: 'x', op: 'eq', value: 2 }] }]) {
  assert.equal(matches(c, { x: 2 }), true);
  assert.equal(matches({ not: c }, { x: 2 }), false);
}
assert.equal(matches({ op: 'unknown' }, {}), false);
assert.equal(matches({ op: 'ne', parameter: 'absent', value: 1 }, {}), false);
assert.equal(parameterEnabled({ enabledWhen: { op: 'eq', key: 'on', value: true } }, { on: false }), false);
assert.equal(parameterEnabled({ readOnly: true }, {}), false);

let packages = 0;
for (const [kind, filename] of [['product', 'template.json'], ['profile', 'profile.json'], ['mold', 'tool.json']]) {
  for (const entry of readdirSync(new URL(kind + '/', root), { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    let d;
    try { d = read(`${kind}/${entry.name}/${filename}`); } catch (e) { if (e.code === 'ENOENT') continue; throw e; }
    packages++;
    const keys = new Set(d.parameters.map(p => p.key));
    function check(c) {
      if (!c) return;
      assert.ok(['eq', 'ne', 'all', 'any', 'not'].includes(c.op));
      const key = c.parameter ?? c.key ?? c.name;
      if (key) {
        assert.ok(keys.has(key), `${d.id}: missing ${key}`);
        const target = d.parameters.find(field => field.key === key);
        if (kind === 'product' && target.valueType === 'enum') {
          assert.ok(target.choices.some(choice => Object.is(choice.value, c.value)),
            `${d.id}: condition ${key}=${c.value} is outside declared enum choices`);
        }
      }
      for (const item of c.conditions ?? []) check(item);
      if (c.condition) check(c.condition);
    }
    for (const p of d.parameters) { check(p.visibleWhen); check(p.enabledWhen); }
  }
}
for (const id of ['v-notch-sharp', 'edge-arc-groove']) {
  const d = read(`mold/${id}/tool.json`), values = { ...defaults(d), bendCompensation: false };
  assert.equal(d.parameters.some((parameter) => parameter.key === 'useDefaultKFactor'), false);
  assert.equal(shown(d, 'kFactor', values), false);
  Object.assign(values, { bottomStrategy: 'rounded', bendCompensation: true });
  assert.equal(shown(d, 'kFactor', values), true);
  if (id === 'v-notch-sharp') {
    values.bottomStrategy = 'sharp';
    assert.equal(shown(d, 'bendCompensation', values), false);
    assert.equal(shown(d, 'kFactor', values), false);
    assert.equal(values.bendCompensation, true, 'hidden draft retained');
    Object.assign(values, { bottomStrategy: 'rounded', segmentedBend: true });
    assert.equal(shown(d, 'bendCompensation', values), false);
    assert.equal(shown(d, 'kFactor', values), false);
  }
}
{
  const d = read('mold/v-notch-sharp/tool.json');
  const p = { ...defaults(d), rootSlotPattern: true, rootPatternMode: 'triple' };
  assert.equal(shown(d, 'centerSlotLength', p), true);
  assert.equal(shown(d, 'bridgeCount', p), false);
  p.rootPatternMode = 'multi';
  assert.equal(shown(d, 'centerSlotLength', p), false);
  assert.equal(shown(d, 'bridgeCount', p), true);
  p.rootSlotPattern = false;
  assert.equal(shown(d, 'rootPatternMode', p), false);
  assert.equal(shown(d, 'bridgeCount', p), false);
  assert.equal(p.bridgeCount, 2, 'disabled root pattern retains its draft');
  Object.assign(p, { bottomStrategy: 'relief', reliefShape: 'circleWrap', rootSlotPattern: true, maleFemale: true, asymmetric: true });
  for (const key of ['enclosedDiameter', 'radialClearance', 'kFactor']) assert.equal(shown(d, key, p), true, key);
  for (const key of ['rootSlotPattern', 'rootPatternMode', 'maleFemale', 'asymmetric', 'reliefLength', 'reliefDepth']) {
    assert.equal(shown(d, key, p), false, key);
  }
  assert.equal(p.rootSlotPattern, true, 'hidden root pattern draft retained');
}
{
  const d = read('mold/embedded-arc-notch/tool.json');
  const p = { ...defaults(d), maleFemale: false, fitClearance: 0.4, insertionDepth: 8, preDeflection: 1 };
  for (const key of ['fitClearance', 'insertionDepth', 'preDeflection']) assert.equal(shown(d, key, p), false);
  p.maleFemale = true;
  for (const key of ['fitClearance', 'insertionDepth', 'preDeflection']) assert.equal(shown(d, key, p), true);
}
const polygon = read('profile/polygon/profile.json');
for (const active of [false, true]) {
  assert.equal(shown(polygon, 'outerRadii', { ...defaults(polygon), useIndependentOuterRadii: active }), active);
  assert.equal(parameterVisible(polygon.parameters.find(p => p.key === 'wallThickness'), defaults(polygon)), true);
}
const joint = read('mold/end-key-joint/tool.json');
assert.equal(joint.operationParameters.some((field) => field.key === 'datum'), false);
assert.equal(joint.parameters.some((field) => field.key === 'straightDepth'), true);
assert.equal(joint.parameters.some((field) => field.key === 'cornerRadius'), false);
for (const entry of readdirSync(new URL('product/', root), { withFileTypes: true })) {
  if (!entry.name.startsWith('modular_guardrail')) continue;
  let d; try { d = read(`product/${entry.name}/template.json`); } catch (e) { if (e.code === 'ENOENT') continue; throw e; }
  for (const infillType of ['glass', 'plate']) {
    const infill = d.parameters.find(field => field.key === 'infillType');
    if (infill?.readOnly && infill.defaultValue !== infillType) continue;
    const p = { ...defaults(d), infillType, barDistribution: 'manual_count', postCapEnabled: true, largePostMode: 'none', layout: 'straight', cornerPostMode: 'double' };
    for (const key of ['barDistribution', 'fixedBarCount', 'infillWidth', 'infillProfileType', 'postCapModelReference', 'doublePostClearGap']) {
      if (!d.parameters.some(field => field.key === key)) continue;
      if (key === 'postCapModelReference' && !d.parameters.find(field => field.key === 'largePostMode')?.choices?.some(choice => choice.value === 'none')) continue;
      assert.equal(shown(d, key, p), false, `${entry.name}/${key}`);
    }
  }
}
for (const name of ['p-tube']) {
  const d = read(`profile/${name}/profile.json`), p = { ...defaults(d), outerRadius1: 5 };
  assert.equal(shown(d, 'outerRadius1', p), false);
  assert.equal(shown(d, 'width', p), true);
  p.useIndependentOuterRadii = true;
  assert.equal(shown(d, 'outerRadius1', p), true);
  assert.equal(p.outerRadius1, 5, 'hidden radius draft retained');
}
for (const gender of ['male', 'female']) {
  const view = { tubeDesignerSystemPunchTools: [joint], tubeDesignerToolLibrary: { scope: 'system', selectedKey: `system::${joint.id}`, parameterDrafts: { [`system::${joint.id}`]: { ...defaults(joint), gender } } } };
  const html = renderToolLibraryRightPane({}, view);
  assert.equal(html.includes('data-tube-tool-library-parameter="straightDepth"'), true);
  assert.equal(html.includes('data-tube-tool-library-parameter="cornerRadius"'), false);
  assert.equal(html.includes('data-tube-tool-library-operation="datum"'), false);
  for (const key of ['sideClearance', 'axialClearance']) assert.equal(html.includes(`data-tube-tool-library-parameter="${key}"`), gender === 'female');
}
{
  const d = read('product/single_face_security_window/template.json');
  const p = { ...defaults(d), frameJoinType: 'miter_45', doorFrameJoinType: 'miter_45' };
  assert.equal(shown(d, 'outerFrameGrooveTool', p), false);
  p.frameJoinType = 'v_groove_90:tool_library';
  assert.equal(shown(d, 'outerFrameGrooveTool', p), false, 'retained join draft does not activate folding');
  p.frameManufacturingMode = 'plane_v_notch';
  assert.equal(shown(d, 'outerFrameGrooveTool', p), true);
  p.accessDoorEnabled = true;
  p.doorFrameJoinType = 'v_groove_90:tool_library';
  assert.equal(shown(d, 'doorFrameGrooveTool', p), true);
  const field=d.parameters.find(f=>f.key==='frameManufacturingMode');
  for(const faceType of ['two','three','five']){
    const values={...p,faceType,frameManufacturingMode:'spatial_v_notch'};
    assert.equal(availableParameterChoices(field,values).some(c=>c.value==='spatial_v_notch'),faceType!=='five');
    assert.equal(effectiveParameterChoice(field,'spatial_v_notch',values),faceType==='five'?'plane_v_notch':'spatial_v_notch');
    assert.equal(d.parameters.some(p=>p.key==='spatialFrameMaximumStockLength'),false);
    assert.equal(values.frameManufacturingMode,'spatial_v_notch','choice fallback preserves draft');
  }
}
console.log(`Parameter condition contracts and switching regressions passed: ${packages} packages`);
