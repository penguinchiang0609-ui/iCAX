import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { catalogText } from '../../apps/tube-designer/webpage/productCatalog.mjs';
import { matchesParameterCondition, parameterVisible } from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import { productControlFields, productControlChoices, productControlValue, productStructureEditors, productControlEditableAfterCreation,
  applyProductControlChoice } from '../../apps/tube-designer/webpage/productControls.mjs';
import { renderDesignerAddParameterContent, renderDesignerRightPane, renderDesignerViewportOverlay,
  applyTemplateFieldDisplay } from '../../apps/tube-designer/webpage/designerViews.mjs';
import { productSceneMemberIds, resolveProductSpecificationAnnotations } from '../../apps/tube-designer/webpage/productParameterDiagram.mjs';

const specifications = [
  ['straight_steel_staircase', '2.1.1', 67,
    { stringer: ['flight.1.beam.1'], tread: ['flight.1.step.1.cross.1', 'flight.1.step.1.edge.1', 'flight.1.step.1.support.0.0'],
      landing: ['landing.1.front', 'landing.1.cross.0', 'landing.1.column.0'],
      handrail: ['flight.1.guard.1.top', 'landing.1.guard.0.rail', 'transition.0.rise', 'transition.0.well.0'],
      post: ['flight.1.guard.1.post.0', 'landing.1.guard.0.post.0'],
      infill: ['flight.1.guard.1.infill.0.0', 'flight.1.guard.1.lower.0', 'landing.1.guard.0.fill.0.0', 'landing.1.guard.0.picket.0.0'] }],
  ['assembly_frame_lt', '1.0.1', 19,
    { frame: ['frame.left.0001', 'frame.right.0001', 'frame.top.0001', 'frame.bottom.0001'], middle: ['frame.middle.0001'] }],
  ['assembly_cross_fixture', '1.0.1', 15,
    { host: ['cross.host.0001'], through: ['cross.through.0001'] }],
  ['assembly_orthogonal_corner', '1.0.1', 22,
    { a: ['corner.a.0001'], b: ['corner.b.0001'], c: ['corner.c.0001'] }],
];
const output = [];
let routes = 0, popups = 0, roles = 0;
const sourceManifest = JSON.parse(readFileSync(new URL('../../apps/tube-designer/product.manifest.json', import.meta.url)));

// This validates referenced values only. All condition evaluation below uses
// the production shared interpreter, including inactive-draft behavior.
function checkConditionReferences(value, definitions) {
  if (!value || typeof value !== 'object') return;
  if (value.op && value.parameter) {
    const field = definitions.get(value.parameter);
    assert.ok(field, `Unknown condition parameter ${value.parameter}`);
    if (field.valueType === 'enum') for (const expected of value.values ?? [value.value]) {
      assert.ok(field.choices.some(choice => Object.is(choice.value, expected)),
        `${value.parameter}: condition value ${expected} must belong to this actual template's choices`);
    }
  }
  for (const child of Object.values(value)) checkConditionReferences(child, definitions);
}
function heading(html) {
  return html.match(/class="tube-designer-heading">\s*<strong>[^<]*<\/strong>\s*<span>([^<]*)<\/span>/)?.[1] ?? '';
}
function fixture(template, values) {
  const product = { entityId: `${template.id}:display-contract`, templateId: template.id,
    name: catalogText(template.displayName), quantity: 1, parameters: values };
  const designer = { templates: [template], product, members: [], joints: [], parts: [] };
  return { designer, view: { activeAreaId: 'view', scene: { tubeDesigner: designer } } };
}
for (const [directory, version, count, proof] of specifications) {
  const base = new URL(`../../apps/tube-designer/templates/product/${directory}/`, import.meta.url);
  const raw = JSON.parse(readFileSync(new URL('template.json', base)));
  const display = JSON.parse(readFileSync(new URL('display.json', base)));
  assert.equal(raw.version, version);
  assert.equal(raw.parameters.length, count);
  if (['minimal_protective_grille', 'straight_steel_staircase'].includes(directory)) {
    assert.equal(catalogText(raw.extensions.parameterLayout.sections.find(section => section.key === 'process')?.displayName),
      '装配', `${directory}: shared current assembly section label`);
  }
  const physical = raw.parameters.map(field => {
    const result = structuredClone(field);
    if (result.presentation) {
      delete result.presentation.visible;
      if (directory === 'assembly_cross_fixture' && ['hostProfileType', 'throughProfileType'].includes(field.key)) {
        delete result.presentation.editor;
        delete result.presentation.resourceRole;
      }
      if (!Object.keys(result.presentation).length) delete result.presentation;
    }
    return result;
  });
  assert.equal(display.schema, 'icax.template-display');
  assert.equal(display.schemaVersion, 1);
  assert.ok(Object.keys(display.views.right.sectionGroups).length >= 2);
  assert.ok(Object.keys(display.views.add.fields).length > 0);
  const template = { ...raw, display, available: true,
    groups: raw.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
    parameters: raw.parameters.map(field => ({ ...field, groupKey: field.group,
      type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
      displayName: catalogText(field.displayName), options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
  const defaults = Object.fromEntries(raw.parameters.map(field => [field.key, field.defaultValue]));
  const physicalKeys = Object.keys(defaults).sort();
  const beforeTemplate = structuredClone(template);
  const definitions = new Map(raw.parameters.map(field => [field.key, field]));
  checkConditionReferences(raw, definitions);
  for (const field of applyTemplateFieldDisplay(template, [...template.parameters, ...productControlFields(template)], 'right')) {
    const rule = display.views.right.fields[field.key];
    assert.ok(rule, `${directory}/${field.key}: explicit current right-pane field layout`);
    assert.ok(field.uiDisplay.width.min > 0 && field.uiDisplay.width.preferred >= field.uiDisplay.width.min);
    if (rule.sectionGroup) assert.ok(display.views.right.sectionGroups[rule.sectionGroup]);
  }
  const python = readFileSync(new URL('template.py', base), 'utf8');
  if (directory === 'straight_steel_staircase') {
    const shared = readFileSync(new URL('../../apps/tube-designer/templates/_shared/steel_staircase.py', import.meta.url), 'utf8');
    assert.ok(shared.includes(`template_version="${version}"`));
  } else {
    assert.ok(python.includes(`TEMPLATE_VERSION = "${version}"`));
    if (directory === 'minimal_protective_grille') {
      const shared = readFileSync(new URL('../../apps/tube-designer/templates/_shared/minimal_protective_grille.py', import.meta.url), 'utf8');
      assert.ok(shared.includes(`TEMPLATE_VERSION = "${version}"`));
    }
  }
  const catalog = sourceManifest.capabilities.tubeDesigner.templates.find(entry => entry.templateId === raw.id);
  if (['assembly_cross_fixture', 'assembly_orthogonal_corner'].includes(directory)) {
    assert.equal(catalog, undefined, `${directory}: hidden validation fixtures are not added as commercial catalogue entries`);
  } else {
    assert.ok(catalog, `${directory}: current source catalogue registers the actual template`);
    assert.equal(catalog.version, version, `${directory}: source catalogue version is synchronized`);
  }
  const controls = productControlFields(template), editors = productStructureEditors(template);
  if (directory.startsWith('assembly_')) {
    assert.equal(raw.extensions.catalog.listed, false, 'Validation-only packages remain hidden');
    assert.equal(controls.length, 0, 'Fixed topology must not acquire fictional structure choices');
    assert.equal(editors.length, 0);
  }
  const availableValues = directory === 'minimal_protective_grille' ? { ...defaults, frameType: 'closed_frame' } : defaults;
  const currentFixture = fixture(template, availableValues);
  const right = renderDesignerRightPane({}, currentFixture.view);
  const scene = renderDesignerViewportOverlay({}, currentFixture.view);
  assert.ok(!scene.includes('结构与布局'), 'Scene has no post-creation structure editor');
  const add = renderDesignerAddParameterContent(currentFixture.designer,
    { tubeDesignerAddTemplateId: template.id, tubeDesignerAddDraft: availableValues });
  for (const group of raw.extensions.addDialog.parameterGroups) {
    const fields = [...template.parameters, ...productControlFields(template)];
    if (!group.parameters.some(key => parameterVisible(fields.find(field => field.key === key), availableValues))) continue;
    assert.ok(add.includes(`data-tube-designer-parameter-group="add:${group.key}"`), `${directory}: real add group ${group.key}`);
  }
  for (const control of controls) {
    assert.ok(!physicalKeys.includes(control.key));
    assert.notEqual(control.presentation.visible, false, 'Synthetic controls must not inherit the hidden physical presentation');
    if (!parameterVisible(control, availableValues)) continue;
    if (editors.some(editor => editor.controlKey === control.key)) {
      assert.ok(add.includes(`data-product-control-editor="${control.key}"`), `${directory}: structure control is rendered in add`);
      assert.equal(right.includes(`data-product-control-editor="${control.key}"`),
        productControlEditableAfterCreation(template, control), `${control.key}: only assembly/process choices remain editable`);
      popups++;
    }
    for (const choice of productControlChoices(template, control.key, availableValues)) {
      assert.equal(choice.disabled, false);
      const before = structuredClone(availableValues);
      const next = applyProductControlChoice({}, template, control.key, availableValues, choice.value);
      assert.deepEqual(availableValues, before, 'Structure selection preserves host inputs');
      assert.deepEqual(Object.keys(next).sort(), physicalKeys, 'No synthetic fields become native parameters');
      assert.equal(productControlValue(template, control.key, next), choice.value);
      for (const key of physicalKeys) assert.deepEqual(next[key], choice.parameters[key] ?? before[key]);
      assert.equal(matchesParameterCondition(choice.matchWhen, next), true);
      routes++;
    }
  }
  const members = Object.values(proof).flat().map(key => ({ entityId: key, stableKey: key }));
  for (const [role, expected] of Object.entries(proof)) {
    const radius = `${role}CornerRadius`;
    assert.ok(raw.extensions.sceneParameterBindings.profileRoles[role].parameters.includes(radius));
    assert.deepEqual(productSceneMemberIds(template, members, radius).sort(), [...expected].sort(),
      `${directory}: ${radius} links the actual emitted role instead of an unrelated member`);
    assert.deepEqual(productSceneMemberIds(template, members, '', { profileRole: role }).sort(), [...expected].sort());
    roles++;
  }
  const sceneEntries = raw.extensions.sceneSpecificationAnnotations?.annotations ?? [];
  for(const key of Object.keys(display.views.scene.annotations))
    assert.ok(sceneEntries.some(entry => entry.parameter === key), 'Scene presentation refines a declared native annotation');
  // Bounds alone cannot invent a generated world-space annotation anchor.
  const resolved = resolveProductSpecificationAnnotations({ ...currentFixture.designer,
    generationRun: { bounds: { minX: 0, minY: -550, minZ: 0, maxX: 4680, maxY: 550, maxZ: 3000 } } }, currentFixture.view);
  assert.deepEqual(resolved, [], 'A display declaration cannot fabricate an annotation without actual generated scene evidence');
  const headingValues = { ...defaults, ...(directory === 'assembly_cross_fixture'
    ? { hostLength: 601, throughLength: 503 } : directory === 'assembly_orthogonal_corner'
      ? { aLength: 601, bLength: 503, cLength: 407 } : {}) };
  const headingBefore = structuredClone(headingValues);
  const title = heading(renderDesignerRightPane({}, fixture(template, headingValues).view));
  assert.ok(title && !title.includes('- × -'), `${directory}: actual header uses existing physical dimensions`);
  const expectedHeadingNumbers = directory === 'assembly_cross_fixture' ? ['601', '503']
    : directory === 'assembly_orthogonal_corner' ? ['601', '503', '407']
      : directory === 'straight_steel_staircase' ? ['3,000', '1,100', '18'] : ['1,200', '1,500'];
  for (const value of expectedHeadingNumbers) assert.ok(title.includes(value), `${directory}: right heading includes ${value}`);
  if (directory === 'assembly_cross_fixture') assert.equal(title, '主管 601 · 贯穿管 503 mm · 0 个构件');
  if (directory === 'assembly_orthogonal_corner') assert.equal(title, 'A 601 / B 503 / C 407 mm · 0 个构件');
  const routeHeadings = [];
  if (directory === 'straight_steel_staircase') {
    const names = { straight: '直跑', straight_landing: '同向双跑＋平台', l_turn: 'L 型平台转角', u_turn: 'U 型平台折返' };
    for (const choice of raw.parameters.find(field => field.key === 'stairRoute').choices) {
      const values = { ...defaults, stairRoute: choice.value, floorHeight: 3117, stairWidth: 1213, totalRiserCount: 17, treadDepth: 273 };
      const before = structuredClone(values);
      assert.deepEqual(Object.keys(values).sort(), physicalKeys);
      const headingText = heading(renderDesignerRightPane({}, fixture(template, values).view));
      assert.equal(headingText, `${catalogText(choice.displayName)} · 层高 3,117 × 梯宽 1,213 · 17级 · 踏步 273 mm · 0 个构件`);
      assert.deepEqual(values, before, 'Route headings do not introduce width/height or mutate host values');
      routeHeadings.push({ stairRoute: choice.value, heading: headingText });
    }
  }
  assert.deepEqual(headingValues, headingBefore);
  assert.deepEqual(template, beforeTemplate);
  output.push({ templateId: raw.id, version, parameterCount: count, listed: raw.extensions.catalog.listed !== false,
    physicalContractSha256: createHash('sha256').update(JSON.stringify(physical)).digest('hex'), roleProof: proof, controls: controls.map(control => ({ key: control.key,
      visibleWhen: control.visibleWhen, choices: control.choices.map(choice => ({ value: choice.value, parameters: choice.parameters })) })),
    structureEditors: editors.map(editor => editor.controlKey), actualRightHeading: title,
    actualStairRouteHeadings: routeHeadings,
    sceneAnnotationParameters: sceneEntries.map(entry => entry.parameter), nativeAndBrowserAcceptance: 'Separate actual native/browser tests required' });
}

// Parent options suppress inactive children through the existing interpreter,
// while every draft remains intact for a later reactivation.
for (const [directory, updates, invisible, controlsInactive] of [
  ['straight_steel_staircase', { railingSide: 'none', stringerConstruction: 'zigzag', stairRoute: 'straight', treadType: 'none', connectionType: 'weld' },
    ['railingHeight', 'handrailCornerRadius', 'postCornerRadius', 'infillCornerRadius', 'bracketThickness', 'landingLength', 'wellGap', 'treadThickness', 'boltHoleDiameter'],
    ['stairsInfillStructure', 'stairsBracketStructure', 'stairsHandrailConnection']],
]) {
  const raw = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/product/${directory}/template.json`, import.meta.url)));
  const values = { ...Object.fromEntries(raw.parameters.map(field => [field.key, field.defaultValue])), ...updates };
  const before = structuredClone(values);
  for (const key of invisible) assert.equal(parameterVisible(raw.parameters.find(field => field.key === key), values), false);
  for (const key of controlsInactive) {
    const control = productControlFields(raw).find(field => field.key === key);
    assert.equal(parameterVisible(control, values), false);
    assert.equal(productControlValue(raw, key, values), '');
    assert.throws(() => applyProductControlChoice({}, raw, key, values, control.choices[0].value));
  }
  assert.deepEqual(values, before);
}

if (process.env.ICAX_REMAINING_DISPLAY_REPORT) {
  const reportPath = resolve(process.env.ICAX_REMAINING_DISPLAY_REPORT);
  mkdirSync(resolve(reportPath, '..'), { recursive: true });
  writeFileSync(reportPath, JSON.stringify({ passed: true, routes, popups, roles, templates: output }, null, 2) + '\n');
}
console.log(`PASS ${output.length} remaining current displays, ${routes} actual choice routes, ${popups} creation-only controls, ${roles} emitted-key role mappings and physical dimension headings`);
