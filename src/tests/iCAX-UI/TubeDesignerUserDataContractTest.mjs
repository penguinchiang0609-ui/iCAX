import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const manifest = JSON.parse(readFileSync(new URL('../../apps/tube-designer/product.manifest.json', import.meta.url), 'utf8'));
const backend = readFileSync(new URL('../../iCAX-Plugins/product/TubeDesigner/TubeDesignerSDO.cpp', import.meta.url), 'utf8');
function constant(name) {
  const match = backend.match(new RegExp(`constexpr const char\\* ${name} = "([^"]+)";`));
  assert.ok(match, `Missing backend constant ${name}`);
  return match[1];
}

// Check the real writer's record names, not a duplicated set of test-only names.
for (const [featureName, recordName, subject] of [
  ['kProfileFeatureID', 'kImportedProfileRecordType', 'product'],
  ['kProfileFeatureID', 'kParametricProfileRecordType', 'profile-definition'],
  ['kCustomerFeatureID', 'kCustomerRecordType', 'product'],
  ['kTemplateFeatureID', 'kParameterPresetRecordType', 'template-definition'],
  ['kProductTemplateFeatureID', 'kProductTemplateRecordType', 'product-template-definition'],
  ['kPunchToolFeatureID', 'kFixedPunchToolRecordType', 'punch-tool-definition'],
  ['kPunchToolFeatureID', 'kParametricPunchToolRecordType', 'punch-tool-definition'],
]) {
  const feature = constant(featureName), type = constant(recordName);
  const record = manifest.userData.features.find(item => item.featureId === feature)
    ?.recordTypes.find(item => item.recordType === type);
  assert.ok(record, `Undeclared writer: ${feature}/${type}`);
  assert.ok(record.subjectTypes.includes(subject), `Invalid subject: ${feature}/${type}`);
  assert.equal(record.schemaVersion, 1);
  assert.equal(record.cardinality, 'multiple');
}
console.log('PASS: 7 user-data writers match product manifest declarations');
