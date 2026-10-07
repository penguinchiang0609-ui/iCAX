import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applyProductControlChoice, productControlChoices, productControlFields, productControlValue }
  from '../../apps/tube-designer/webpage/productControls.mjs';

const template = JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/product/single_face_security_window/template.json', import.meta.url), 'utf8'));
const control = productControlFields(template).find(field => field.key === 'outerFrameConnection');
assert.ok(control);
const defaults = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
const choices = productControlChoices(template, control, defaults);
assert.equal(choices.some(choice => choice.value === 'insert'), false,
  'One outer-frame tube specification cannot expose whole-tube insertion into itself');
assert.equal(choices.find(choice => choice.value === 'tabs').label, '公母插接');

let values = structuredClone(defaults);
for (const selected of ['tabs', 'miter', 'tabs']) {
  const previous = structuredClone(values);
  values = applyProductControlChoice({}, template, control, values, selected);
  assert.equal(productControlValue(template, control, values), selected);
  assert.equal(values.foldedPostJoint, selected === 'tabs' ? 'tabs' : 'weld');
  assert.equal(values.frameManufacturingMode, 'segment_weld');
  for (const key of Object.keys(defaults)) {
    if (!['foldedPostJoint', 'frameManufacturingMode', 'frameJoinType', 'frameCornerJoin'].includes(key))
      assert.deepEqual(values[key], previous[key], `${selected} changed ${key}`);
  }
  assert.equal(Object.hasOwn(values, 'outerFramePostMaterial'), false);
  assert.equal(Object.hasOwn(values, 'outerFrameConnection'), false);
}
console.log('Uniform outer-frame material: public tab connection and exact mappings passed; whole-tube insertion removed.');
