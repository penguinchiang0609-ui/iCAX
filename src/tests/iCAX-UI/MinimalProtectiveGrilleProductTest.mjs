import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { buildTemplateGroupTree, catalogText } from '../../apps/tube-designer/webpage/productCatalog.mjs';
import { renderDesignerRightParameterContent, renderDesignerAddParameterContent,
  getDefaultParameters } from '../../apps/tube-designer/webpage/designerViews.mjs';
import { resolveProductSpecificationAnnotations, resolveProductSpecificationAnnotationTree }
  from '../../apps/tube-designer/webpage/productParameterDiagram.mjs';
import { parameterVisible } from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import { minimalGrilleDiagramStocks } from '../../apps/tube-designer/webpage/minimalGrilleDiagram.mjs';
import { creationOnlyParameterKeys, manufacturingOnlyParameterKeys,
  productDisplayParameters, validateProductPostCreationChanges } from '../../apps/tube-designer/webpage/productParameterDependencies.mjs';

const base = new URL('../../apps/tube-designer/templates/product/minimal_protective_grille/', import.meta.url);
const raw = JSON.parse(readFileSync(new URL('template.json', base)));
const display = JSON.parse(readFileSync(new URL('display.json', base)));
const template = { ...raw, display, available: true,
  groups: raw.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
  parameters: raw.parameters.map(field => ({ ...field, groupKey: field.group,
    type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
    displayName: catalogText(field.displayName),
    options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
const defaults = getDefaultParameters([template], template.id);
const fields = new Map(raw.parameters.map(field => [field.key, field]));
const manufacturing = manufacturingOnlyParameterKeys(template);
assert.equal(defaults.handleEnabled, 'yes');
assert.equal(defaults.jointMode, 'butt_weld');
assert.equal(raw.version, '2.3.1');
const jointModes = ['butt_weld', 'male_female_half_hole', 'male_female_through_hole'];
assert.deepEqual(fields.get('jointMode').choices.map(choice => choice.value), jointModes);
assert.equal(defaults.installHoleEnabled, 'no');
assert.equal(defaults.installHoleCountPerSide, 3);
assert.equal(typeof defaults.installHoleCountPerSide, 'number');
assert.equal(defaults.installHoleOrientation, 'side');
assert.equal(defaults.installHoleTopOffset, 50);
assert.equal(defaults.installHoleBottomOffset, 50);
assert.equal(defaults.installHoleLargeDiameter, 6);
assert.equal(defaults.installHoleSmallDiameter, 4);
assert.equal(defaults.installHoleMinimumWeb, 0.2);
assert.equal(fields.get('installHoleCountPerSide').valueType, 'enum');
assert.deepEqual(fields.get('installHoleCountPerSide').choices.map(choice => choice.value), [2, 3, 4, 5]);
assert.equal(fields.get('installHoleOrientation').valueType, 'enum');
assert.equal(fields.get('installHoleOrientation').group, 'installation');
assert.equal(catalogText(fields.get('installHoleOrientation').displayName), '安装孔方向');
assert.deepEqual(fields.get('installHoleOrientation').choices.map(choice => choice.value), ['side', 'front']);
assert.equal(catalogText(fields.get('installHoleLargeDiameter').displayName), '大孔直径');
assert.equal(catalogText(fields.get('installHoleSmallDiameter').displayName), '小孔直径');
assert.throws(() => minimalGrilleDiagramStocks(raw.extensions.productDiagram,
  { ...defaults, installHoleEnabled: 'yes', installHoleCountPerSide: 6 }), /请调整安装孔组数或上下端距/);
assert.deepEqual([...creationOnlyParameterKeys(template)], ['frameType']);
assert.deepEqual(raw.extensions.addDialog.structureParameters,
  ['frameType', 'handleEnabled', 'handleSide', 'handleClosure']);
assert.deepEqual([...manufacturing].sort(), raw.parameters
  .filter(field => ['joint', 'joint_connection', 'joint_male', 'joint_female', 'installation'].includes(field.group)).map(field => field.key).sort());
for (const field of raw.parameters.filter(field => field.group === 'mounting_layout')) {
  assert.ok(!manufacturing.has(field.key), `${field.key}: real hole-position anchors require regeneration`);
  const changed = field.key === 'installHoleEnabled' ? 'yes' : defaults[field.key] + 1;
  assert.notDeepEqual(productDisplayParameters(template, { ...defaults, [field.key]: changed }),
    productDisplayParameters(template, defaults));
}
for (const field of raw.parameters.filter(field => field.group === 'installation')) {
  const changed = field.valueType === 'enum'
    ? field.choices.find(choice => choice.value !== defaults[field.key]).value : defaults[field.key] + 1;
  assert.deepEqual(productDisplayParameters(template, { ...defaults, [field.key]: changed }),
    productDisplayParameters(template, defaults), `${field.key}: machining must not expire display`);
}

function validateConditions(value) {
  if (!value || typeof value !== 'object') return;
  if (value.op && value.parameter) {
    const field = fields.get(value.parameter);
    assert.ok(field, `Unknown parameter in condition: ${value.parameter}`);
    if (field.valueType === 'enum') for (const expected of value.values ?? [value.value]) {
      assert.ok(field.choices.some(choice => Object.is(choice.value, expected)),
        `${value.parameter}: condition must use a real choice`);
    }
  }
  Object.values(value).forEach(validateConditions);
}
validateConditions(raw);
validateConditions(display);

let states = 0, annotationsChecked = 0;
for (const frameType of ['two_vertical', 'closed_frame', 'bottom_rail'])
for (const handleEnabled of ['yes', 'no'])
for (const handleSide of ['left', 'right', 'center_double'])
for (const handleClosure of ['closed', 'open_slot'])
for (const barLayoutMode of ['maximum_gap', 'fixed_count'])
for (const installHoleEnabled of ['no', 'yes'])
for (const installHoleOrientation of ['side', 'front']) {
  const values = { ...defaults, frameType, handleEnabled, handleSide, handleClosure, barLayoutMode,
    installHoleEnabled, installHoleOrientation };
  const original = structuredClone(values);
  // This is a renderer unit fixture. Python verifies actual generated anchors;
  // standalone acceptance must supply native anchors to the real scene.
  const anchors = raw.extensions.sceneSpecificationAnnotations.annotations.map(annotation => ({ ...annotation,
    start: [0, 0, 0], end: annotation.kind === 'parameter' ? [0, 0, 0] : [100, 0, 0],
    generatedValue: values[annotation.parameter] }));
  const designer = { templates: [template],
    product: { entityId: 'minimal-grille-renderer-test', templateId: raw.id, parameters: values },
    specificationAnnotations: anchors };
  const view = { activeAreaId: 'view', scene: { tubeDesigner: designer },
    tubeDesignerAddTemplateId: raw.id, tubeDesignerAddDraft: values };
  const right = renderDesignerRightParameterContent(designer, view).scrollContent;
  const add = renderDesignerAddParameterContent(designer, view);
  assert.ok(add.includes('data-tube-designer-parameter="frameType"'));
  assert.ok(!right.includes('data-tube-designer-parameter="frameType"'));
  const resolved = resolveProductSpecificationAnnotations(designer, view);
  const specifications = resolveProductSpecificationAnnotationTree(designer, view);
  assert.deepEqual(specifications.children.map(group => group.key), ['overall', 'handle', 'layout', 'mounting_layout']);
  assert.ok(specifications.children.every(group => !group.children?.length),
    'Single specification groups must not be wrapped in duplicate, identically named sections');
  for (const field of raw.parameters.filter(field => ['overall', 'handle', 'layout', 'mounting_layout'].includes(field.group)
      && !['productCode', 'frameType'].includes(field.key))) {
    const annotation = resolved.find(entry => entry.parameter === field.key);
    assert.equal(Boolean(annotation), parameterVisible(field, values), `${field.key}: annotation visibility`);
    assert.ok(!manufacturing.has(field.key), `${field.key}: layout must invalidate the model`);
    if (annotation) {
      assert.ok(annotation.editable, `${field.key}: layout remains editable after creation`);
      assert.ok(!right.includes(`data-tube-designer-parameter="${field.key}"`), `${field.key}: one input owner`);
      if (field.valueType === 'enum') assert.equal(annotation.editor.type, 'select');
      if (field.key === 'installHoleCountPerSide') {
        assert.equal(typeof annotation.editor.value, 'number');
        assert.deepEqual(annotation.editor.options.map(option => option.value), [2, 3, 4, 5]);
      }
      annotationsChecked++;
    }
  }
  assert.equal(parameterVisible(fields.get('handleCenterOffset'), values),
    handleEnabled === 'yes' && handleSide === 'center_double');
  assert.equal(parameterVisible(fields.get('upperBarCount'), values),
    handleEnabled === 'yes' && barLayoutMode === 'fixed_count');
  assert.equal(parameterVisible(fields.get('barCount'), values),
    handleEnabled === 'no' && barLayoutMode === 'fixed_count');
  assert.equal(parameterVisible(fields.get('maximumClearGap'), values), barLayoutMode === 'maximum_gap');
  assert.equal(parameterVisible(fields.get('firstBarTopOffset'), values), frameType !== 'closed_frame');
  assert.equal(parameterVisible(fields.get('lastBarBottomOffset'), values), frameType === 'two_vertical');
  assert.equal(parameterVisible(fields.get('maleHeadLength'), values), false);
  for (const field of raw.parameters.filter(field => field.key.startsWith('installHole') && field.key !== 'installHoleEnabled')) {
    assert.equal(parameterVisible(field, values), installHoleEnabled === 'yes', `${field.key}: installation enable condition`);
    if (field.group === 'installation') assert.equal(right.includes(`data-tube-designer-parameter="${field.key}"`),
      installHoleEnabled === 'yes', `${field.key}: process editor visibility`);
  }
  const mounting = resolved.filter(entry => entry.groupKey === 'mounting_layout');
  assert.equal(mounting.length, installHoleEnabled === 'yes' ? 4 : 1);
  assert.equal(new Set(resolved.map(entry => entry.parameter)).size, resolved.length, 'No duplicate annotation editors');
  assert.ok(!resolved.some(entry => entry.parameter === 'installHoleOrientation'),
    'Installation direction belongs only to the process editor');
  if (installHoleEnabled === 'yes') {
    assert.match(right, /<select\b[^>]*data-tube-designer-parameter="installHoleOrientation"/);
    assert.ok(right.includes('安装孔方向'));
  }
  for (const key of ['installHoleTopOffset', 'installHoleBottomOffset']) {
    const declared = raw.extensions.sceneSpecificationAnnotations.annotations.find(entry => entry.parameter === key);
    assert.equal(declared.kind, 'linear');
    assert.equal(declared.axis, 'z');
    if (installHoleEnabled === 'yes') assert.equal(resolved.find(entry => entry.parameter === key).kind, 'linear');
  }
  assert.doesNotThrow(() => validateProductPostCreationChanges(template, values,
    { ...values, handleBottom: values.handleBottom + 10, handleSide: 'right',
      installHoleEnabled: 'yes', installHoleCountPerSide: 4, installHoleBottomOffset: 60, installHoleTopOffset: 70 }));
  assert.throws(() => validateProductPostCreationChanges(template, values,
    { ...values, frameType: 'invalid-structure' }));
  assert.deepEqual(values, original);
  states++;
}

for (const role of ['frame', 'inner']) {
  const binding = raw.extensions.resourceRoles.profiles[role];
  const selector = fields.get(`${role}ProfileType`);
  assert.equal(binding.managedParameters.length, 4);
  assert.deepEqual(binding.defaultParametersByResource['system:rect'],
    { width: 10, depth: 5, wallThickness: 0.8, cornerRadius: 1 });
  assert.deepEqual(selector.presentation.profileConstraints.sectionKinds, ['rect','round']);
  assert.deepEqual(selector.presentation.allowedScopes, ['system', 'user']);
  assert.ok(raw.parameters.filter(field => field.group === `${role}_profile`)
    .every(field => !manufacturing.has(field.key)));
}
let jointStates = 0;
const jointBaselineValues = { ...defaults, installHoleEnabled: 'yes' };
const jointLayoutBaseline = minimalGrilleDiagramStocks(raw.extensions.productDiagram, jointBaselineValues);
for (const jointMode of jointModes)
for (const maleCornerType of ['square', 'chamfer', 'round'])
for (const maleHeadLength of [2, -1])
for (const maleEndForm of ['tongue','full_section'])
for (const maleTabCount of ['two','four'])
for (const maleTabAngle of [0,27]) {
  // The stale negative length models a hidden, inapplicable draft. This is a
  // rendering/ownership check; manufacturing validates applicable values.
  const values = { ...jointBaselineValues, jointMode, maleCornerType, maleHeadLength, maleEndForm, maleTabCount, maleTabAngle };
  const original = structuredClone(values);
  const designer = { templates: [template],
    product: { entityId: 'minimal-grille-joint-test', templateId: raw.id, parameters: values } };
  const view = { activeAreaId: 'view', scene: { tubeDesigner: designer } };
  const right = renderDesignerRightParameterContent(designer, view).scrollContent;
  const inserted = jointMode !== 'butt_weld';
  const tongue=inserted&&maleEndForm==='tongue';
  const expectedVisibility = {
    jointMode: true, maleEndForm:inserted, maleTabCount:tongue, maleTabAngle:tongue, fullHeadTotalClearance:inserted&&!tongue,
    maleHeadLength: jointMode === 'male_female_half_hole',
    maleHeadWidth: tongue,
    maleCornerType: tongue,
    maleCornerSize: tongue && maleCornerType !== 'square',
    holeTotalClearanceDepth: tongue,
    holeTotalClearanceHeight: tongue,
    minimumFeatureWeb: tongue,
  };
  for (const [key, visible] of Object.entries(expectedVisibility)) {
    assert.equal(parameterVisible(fields.get(key), values), visible, `${jointMode}/${maleCornerType}/${key}: shared condition`);
    assert.equal(right.includes(`data-tube-designer-parameter="${key}"`), visible,
      `${jointMode}/${maleCornerType}/${key}: actual process editor visibility`);
    assert.ok(manufacturing.has(key), `${key}: joint changes expire only manufacturing`);
  }
  assert.deepEqual(productDisplayParameters(template, values), productDisplayParameters(template, jointBaselineValues));
  assert.deepEqual(minimalGrilleDiagramStocks(raw.extensions.productDiagram, values), jointLayoutBaseline,
    'Joint modes and hidden length drafts must not alter stocks, openings or installation layout');
  assert.doesNotThrow(() => validateProductPostCreationChanges(template, defaults, values));
  assert.deepEqual(values, original, 'Mode rendering preserves the hidden length draft');
  jointStates++;
}
const sourceRoot = new URL('../../apps/tube-designer/', import.meta.url);
const manifest = JSON.parse(readFileSync(new URL('product.manifest.json', sourceRoot)));
const listed = manifest.capabilities.tubeDesigner.templates.find(entry => entry.templateId === raw.id);
assert.equal(listed.status, 'available');
assert.equal(listed.version, raw.version);
const securityWindow = JSON.parse(readFileSync(new URL('templates/product/single_face_security_window/template.json', sourceRoot)));
const tree = buildTemplateGroupTree([securityWindow, raw].map(item => ({ ...item, available: true })));
assert.equal(tree.length, 1);
assert.equal(tree[0].title, '窗');
assert.deepEqual(tree[0].templates.map(entry => entry.templateId),
  ['single-face-security-window', 'minimal-protective-grille']);
console.log(`PASS minimal grille: ${states} UI states, ${annotationsChecked} annotation ownership checks, ${jointStates} joint-condition states and independent window catalogue entry (renderer unit fixtures)`);
