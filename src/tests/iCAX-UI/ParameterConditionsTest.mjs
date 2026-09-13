import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { matchesParameterCondition as matches, parameterEnabled } from '../../apps/tube-designer/webpage/parameterConditions.mjs';
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
  const d = read(`mold/${id}/tool.json`), values = { ...defaults(d), useDefaultKFactor: false, bendCompensation: false };
  assert.equal(shown(d, 'kFactor', values), false);
  Object.assign(values, { bottomStrategy: 'rounded', bendCompensation: true });
  assert.equal(shown(d, 'kFactor', values), true);
  if (id === 'v-notch-sharp') {
    values.bottomStrategy = 'sharp';
    assert.equal(shown(d, 'bendCompensation', values), false);
    assert.equal(shown(d, 'kFactor', values), false);
    assert.equal(values.bendCompensation, true, 'hidden draft retained');
  }
}
const polygon = read('profile/polygon/profile.json');
for (const model of polygon.parameters.find(p => p.key === 'sectionModel').options) {
  const active = ['triangle-scalene', 'trapezoid-right', 'trapezoid-isosceles', 'parallelogram-tube'].includes(model.value);
  for (const key of ['depth', 'wallThickness']) assert.equal(shown(polygon, key, { ...defaults(polygon), sectionModel: model.value }), active);
}
const joint = read('mold/end-key-joint/tool.json');
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
for (const name of ['bulb-flat', 'p-tube', 'unequal-i']) {
  const d = read(`profile/${name}/profile.json`), p = { ...defaults(d), materialBoundary: '[retained draft]', sourceRevision: 'supplier-v1' };
  assert.equal(shown(d, 'materialBoundary', p), false);
  assert.equal(shown(d, 'width', p), true);
  p.geometrySource = 'providedBoundary';
  assert.equal(shown(d, 'materialBoundary', p), true);
  assert.equal(shown(d, 'width', p), false);
}
for (const gender of ['male', 'female']) {
  const view = { tubeDesignerSystemPunchTools: [joint], tubeDesignerToolLibrary: { scope: 'system', selectedKey: `system::${joint.id}`, parameterDrafts: { [`system::${joint.id}`]: { ...defaults(joint), gender } } } };
  const html = renderToolLibraryRightPane({}, view);
  for (const key of ['sideClearance', 'axialClearance']) assert.equal(html.includes(`data-tube-tool-library-parameter="${key}"`), gender === 'female');
}
for (const name of ['single', 'two', 'three', 'five']) {
  const d = read(`product/${name}_face_security_window/template.json`), p = { ...defaults(d), frameJoinType: 'miter_45', doorFrameJoinType: 'miter_45', doorLeafFrameJoinType: 'miter_45' };
  for (const field of d.parameters.filter(f => f.key.startsWith('vGroove'))) assert.equal(matches(field.visibleWhen, p), false, `${name}/${field.key}`);
  Object.assign(p, { accessDoorEnabled: true, doorFrameJoinType: 'v_groove_90:sharp_v', vGrooveBottomCut: false, vGrooveReliefHole: false });
  assert.equal(shown(d, 'vGrooveRadius', p), false);
  p.vGrooveBottomCut = true;
  assert.equal(shown(d, 'vGrooveRadius', p), true);
}
console.log(`Parameter condition contracts and switching regressions passed: ${packages} packages`);
