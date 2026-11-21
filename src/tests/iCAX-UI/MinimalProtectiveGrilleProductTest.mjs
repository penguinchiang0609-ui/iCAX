import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { buildTemplateGroupTree, catalogText } from '../../apps/tube-designer/webpage/productCatalog.mjs';
import { renderDesignerRightParameterContent, renderDesignerAddParameterContent,
  getDefaultParameters } from '../../apps/tube-designer/webpage/designerViews.mjs';
import { resolveProductSpecificationAnnotations, resolveProductSpecificationAnnotationTree,
  productSceneMemberIds, productParameterTargetsForSceneMember }
  from '../../apps/tube-designer/webpage/productParameterDiagram.mjs';
import { parameterVisible } from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import { minimalGrilleDiagramStocks } from '../../apps/tube-designer/webpage/minimalGrilleDiagram.mjs';
import { insertJointParameterDetailModel } from '../../apps/tube-designer/webpage/insertJointParameterDetails.mjs';
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
assert.equal(raw.version, '2.5.0');
assert.equal(defaults.layoutStyle, 'planar');
assert.deepEqual(fields.get('layoutStyle').choices.map(choice => choice.value), ['planar', 'five_face', 'balcony_closure']);
assert.equal(defaults.projectionDepth, 100);
assert.equal(defaults.topDrop, 30);
assert.equal(defaults.supportMaxSpacing, 800);
assert.equal(defaults.returnMaximumClearGap, 100);
assert.equal(defaults.balconyTopDepth, 100);
assert.equal(defaults.balconyBottomDepth, 100);
assert.equal(defaults.balconyTopDrop, 0);
assert.equal(defaults.supportStyle, 'double');
assert.equal(catalogText(fields.get('supportStyle').choices.find(c=>c.value==='single').displayName),'单竖杆');
for(const supportProfileType of ['rect-bar','rect','round']) {
  const state=minimalGrilleDiagramStocks(raw.extensions.productDiagram,{...defaults,layoutStyle:'five_face',width:1800,
    handleEnabled:'no',supportStyle:'single',supportProfileType});
  const rows=state.spatial.members.filter(m=>m.face==='front'&&m.role==='inner.full');
  assert.equal(state.spatial.supportConnectionKind,supportProfileType==='rect-bar'?'segmented-weld':'through');
  for(const center of state.spatial.supportX) {
    const crossing=rows.some(row=>row.start[0]<center&&row.end[0]>center);
    assert.equal(crossing,supportProfileType!=='rect-bar','Only hollow C may receive a continuous crossing B');
  }
}
assert.equal(defaults.supportPairClearGap, 20);
assert.equal(defaults.supportLinkCount, 0);
assert.equal(defaults.supportLinkBottomOffset, 100);
assert.equal(defaults.supportLinkTopOffset, 100);
assert.equal(defaults.dryingRodEnabled, 'no');
assert.equal(defaults.dryingRodRearOffset, 25);
assert.equal(defaults.dryingRodDrop, 80);
assert.equal(defaults.dryingProfileType, 'rect');
assert.equal(defaults.dryingWidth, 15);
assert.equal(defaults.dryingDepth, 15);
assert.equal(defaults.dryingWallThickness, .8);
assert.equal(defaults.dryingCornerRadius, 1);
for (const layoutStyle of ['planar', 'five_face', 'balcony_closure']) {
  for (const dryingRodEnabled of ['no', 'yes']) {
    const values = { ...defaults, layoutStyle, dryingRodEnabled };
    const active = layoutStyle !== 'planar' && dryingRodEnabled === 'yes';
    const anchors = raw.extensions.sceneSpecificationAnnotations.annotations.map(annotation => ({ ...annotation,
      start: [0, 0, 0], end: [0, 0, 100], generatedValue: values[annotation.parameter] }));
    const designer = { templates: [template], product: { entityId: 'drying-contract', templateId: raw.id,
      parameters: values }, specificationAnnotations: anchors };
    const view = { scene: { tubeDesigner: designer }, activeAreaId: 'view' };
    const right = renderDesignerRightParameterContent(designer, view).scrollContent;
    const annotations = resolveProductSpecificationAnnotations(designer, view);
    for (const key of ['dryingRodEnabled', 'dryingRodRearOffset', 'dryingRodDrop']) {
      const visible = key === 'dryingRodEnabled' ? layoutStyle !== 'planar' : active;
      assert.equal(parameterVisible(fields.get(key), values), visible, key);
      assert.equal(annotations.some(a => a.parameter === key), visible, key);
      assert.ok(!right.includes(`data-tube-designer-parameter="${key}"`), `${key}: no repeated right editor`);
      assert.ok(!creationOnlyParameterKeys(template).has(key));
      assert.ok(!manufacturing.has(key));
    }
    assert.equal(right.includes('data-tube-designer-profile-parameter="dryingProfileType"'), active);
    const state = minimalGrilleDiagramStocks(raw.extensions.productDiagram, values);
    const rods = state.spatial?.members.filter(m => m.profileRole === 'drying') ?? [];
    assert.equal(rods.length, active ? 3 : 0);
    if (active) {
      assert.deepEqual(rods.map(m => m.key).sort(),
        ['frame.drying.left.0001', 'frame.drying.right.0001', 'frame.drying.rod.0001']);
      const rod = rods.find(m => m.key === 'frame.drying.rod.0001');
      assert.equal(rod.start[1], state.spatial.topRearOuterY - values.dryingRodRearOffset);
      assert.equal(rod.start[2], values.height - values.dryingRodDrop);
      assert.equal(rod.end[1], rod.start[1]);
      assert.equal(rod.end[2], rod.start[2]);
      assert.equal(rod.start[0], values.dryingWidth);
      assert.equal(rod.end[0], values.width - values.dryingWidth);
    } else {
      const invalidDraft = { ...values, dryingProfileType: 'inactive-invalid', dryingWidth: -100,
        dryingDepth: -100, dryingRodRearOffset: -100, dryingRodDrop: -100 };
      assert.deepEqual(minimalGrilleDiagramStocks(raw.extensions.productDiagram, invalidDraft), state,
        'Inactive drying drafts cannot alter geometry or block drawing');
    }
  }
}
for (const layoutStyle of ['five_face', 'balcony_closure']) for (const kind of ['rect', 'rect-bar', 'round']) {
  const values = { ...defaults, layoutStyle, dryingRodEnabled: 'yes', dryingProfileType: kind };
  const designer = { templates: [template], product: { entityId: 'drying-material-contract', templateId: raw.id, parameters: values } };
  const view = { activeAreaId: 'view', scene: { tubeDesigner: designer } };
  const right = renderDesignerRightParameterContent(designer, view).scrollContent;
  assert.ok(right.includes('data-tube-designer-profile-parameter="dryingProfileType"'));
  for (const suffix of ['Width', 'Depth', 'WallThickness', 'CornerRadius']) {
    const key = 'drying' + suffix;
    const visible = suffix === 'Depth' ? kind !== 'round' : suffix === 'WallThickness' ? kind !== 'rect-bar'
      : suffix === 'CornerRadius' ? kind === 'rect' : true;
    assert.equal(parameterVisible(fields.get(key), values), visible, `${layoutStyle}/${kind}/${key}`);
    assert.ok(!creationOnlyParameterKeys(template).has(key) && !manufacturing.has(key));
    const changed = { ...values, [key]: values[key] + 1 };
    assert.doesNotThrow(() => validateProductPostCreationChanges(template, values, changed));
    if (visible) assert.notDeepEqual(productDisplayParameters(template, values), productDisplayParameters(template, changed));
  }
}
assert.equal(defaults.barLayoutMode, 'center_spacing');
assert.equal(defaults.barCenterSpacing, 115);
assert.deepEqual(fields.get('barLayoutMode').choices.map(c=>c.value), ['center_spacing','maximum_gap','fixed_count']);
for(const innerWidth of [10,18]) {
  const stocks=minimalGrilleDiagramStocks(raw.extensions.productDiagram,{...defaults,frameType:'closed_frame',handleEnabled:'no',innerWidth}).stocks;
  const centers=stocks.filter(s=>s.role==='inner.full').map(s=>s.start[1]);
  assert.ok(centers.length>2);
  centers.slice(1).forEach((v,i)=>assert.equal(v-centers[i],115,'Center pitch remains exact when the bar width changes'));
}
assert.equal(fields.get('supportLinkCount').valueType, 'integer');
assert.ok(!raw.extensions.primaryDimensions.depthParameter, 'Independent depths must not display the inactive five-face depth');
assert.ok(!fields.has('bottomDrop'), 'The confirmed five-face design has a level bottom');
for (const projectionHorizontalDepth of [90, 200]) assert.throws(() => minimalGrilleDiagramStocks(raw.extensions.productDiagram,
  { ...defaults, layoutStyle: 'five_face', width: 100, projectionDepth:500, handleEnabled: 'no', barLayoutMode: 'fixed_count', barCount: 0, projectionHorizontalDepth }),
  /左右边界回杆之间须保留有效净空/);
assert.doesNotThrow(() => minimalGrilleDiagramStocks(raw.extensions.productDiagram,
  { ...defaults, layoutStyle: 'five_face', handleEnabled: 'no', barLayoutMode: 'fixed_count', barCount: 0 }));
const jointModes = ['butt_weld', 'male_female_half_hole', 'male_female_through_hole'];
assert.deepEqual(fields.get('jointMode').choices.map(choice => choice.value), jointModes);
assert.deepEqual(fields.get('returnJointMode').choices.map(choice => choice.value), jointModes);
assert.equal(defaults.returnJointMode, 'butt_weld');
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
assert.deepEqual([...creationOnlyParameterKeys(template)], ['layoutStyle', 'frameType', 'supportStyle']);
assert.deepEqual(raw.extensions.addDialog.structureParameters,
  ['layoutStyle', 'frameType', 'handleEnabled', 'handleSide', 'handleClosure', 'supportStyle']);
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
  // TemplateCodec::ParseCondition supports this smaller host contract even
  // though the frontend interpreter also understands membership operators.
  if (value.op) assert.ok(['eq', 'ne', 'all', 'any', 'not'].includes(value.op),
    `Unsupported native condition operator: ${value.op}`);
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

for (const layoutStyle of ['planar', 'five_face', 'balcony_closure']) {
  const values = { ...defaults, layoutStyle };
  const anchors = raw.extensions.sceneSpecificationAnnotations.annotations.map(annotation => ({ ...annotation,
    start: [0, 0, 0], end: annotation.kind === 'parameter' ? [0, 0, 0] : [0, 100, 0],
    generatedValue: values[annotation.parameter] }));
  const designer = { templates: [template], product: { entityId: 'five-face-contract', templateId: raw.id, parameters: values },
    specificationAnnotations: anchors };
  const view = { activeAreaId: 'view', scene: { tubeDesigner: designer } };
  const annotations = resolveProductSpecificationAnnotations(designer, view);
  const right = renderDesignerRightParameterContent(designer, view).scrollContent;
  assert.equal(parameterVisible(fields.get('frameType'), values), layoutStyle === 'planar');
  for (const key of ['projectionDepth', 'topDrop', 'balconyTopDepth', 'balconyBottomDepth', 'balconyTopDrop', 'supportMaxSpacing', 'returnMaximumClearGap']) {
    const visible = ['projectionDepth','topDrop'].includes(key) ? layoutStyle === 'five_face'
      : key.startsWith('balcony') ? layoutStyle === 'balcony_closure' : layoutStyle !== 'planar';
    const annotation = annotations.find(entry => entry.parameter === key);
    assert.equal(Boolean(annotation), visible, `${key}: layout annotations only`);
    assert.equal(parameterVisible(fields.get(key), values), visible);
    assert.ok(!right.includes(`data-tube-designer-parameter="${key}"`), `${key}: no duplicate right editor`);
    assert.ok(!creationOnlyParameterKeys(template).has(key));
    assert.ok(!manufacturing.has(key));
    if (annotation) {
      assert.equal(annotation.editable, true);
      assert.equal(annotation.editor.type, 'number');
    }
    assert.doesNotThrow(() => validateProductPostCreationChanges(template, values, { ...values, [key]: values[key] + 10 }));
  }
  assert.throws(() => validateProductPostCreationChanges(template, values,
    { ...values, layoutStyle: layoutStyle === 'planar' ? 'five_face' : 'planar' }));
}

let states = 0, annotationsChecked = 0;
for (const layoutStyle of ['planar','five_face','balcony_closure'])
for (const supportStyle of ['single','double']) {
  const values = { ...defaults, layoutStyle, supportStyle };
  const anchors = raw.extensions.sceneSpecificationAnnotations.annotations.map(annotation => ({ ...annotation,
    start:[0,0,0],end:[0,0,0],generatedValue:values[annotation.parameter] }));
  const designer = { templates:[template],product:{entityId:'support-contract',templateId:raw.id,parameters:values},specificationAnnotations:anchors };
  const view={activeAreaId:'view',scene:{tubeDesigner:designer},tubeDesignerAddTemplateId:raw.id,tubeDesignerAddDraft:values};
  const right=renderDesignerRightParameterContent(designer,view).scrollContent;
  const add=renderDesignerAddParameterContent(designer,view);
  assert.equal(add.includes('data-tube-designer-parameter="supportStyle"'),layoutStyle!=='planar');
  assert.ok(!right.includes('data-tube-designer-parameter="supportStyle"'));
  const annotations=resolveProductSpecificationAnnotations(designer,view);
  for(const [key,visible] of [['supportPairClearGap',layoutStyle!=='planar'&&supportStyle==='double']]) {
    assert.equal(parameterVisible(fields.get(key),values),visible);
    assert.equal(annotations.some(a=>a.parameter===key),visible);
    assert.ok(!right.includes(`data-tube-designer-parameter="${key}"`));
    assert.ok(!manufacturing.has(key));
    assert.doesNotThrow(()=>validateProductPostCreationChanges(template,values,{...values,[key]:values[key]+1}));
  }
  assert.throws(()=>validateProductPostCreationChanges(template,values,
    {...values,supportStyle:supportStyle==='single'?'double':'single'}));
}
for (const layoutStyle of ['planar','five_face','balcony_closure'])
for (const supportStyle of ['single','double'])
for (const supportLinkCount of [0,1,3]) {
  const values={...defaults,layoutStyle,supportStyle,supportLinkCount};
  const active=layoutStyle!=='planar'&&supportStyle==='double';
  const anchors=raw.extensions.sceneSpecificationAnnotations.annotations.map(a=>({...a,start:[0,0,0],end:[0,0,100],generatedValue:values[a.parameter]}));
  const designer={templates:[template],product:{entityId:'support-links',templateId:raw.id,parameters:values},specificationAnnotations:anchors};
  const view={activeAreaId:'view',scene:{tubeDesigner:designer}};
  const annotations=resolveProductSpecificationAnnotations(designer,view);
  const right=renderDesignerRightParameterContent(designer,view).scrollContent;
  for (const [key,visible] of [['supportLinkCount',active],['supportLinkBottomOffset',active&&supportLinkCount>0],['supportLinkTopOffset',active&&supportLinkCount>1]]) {
    assert.equal(parameterVisible(fields.get(key),values),visible);
    assert.equal(annotations.some(a=>a.parameter===key),visible);
    assert.ok(!right.includes(`data-tube-designer-parameter="${key}"`),'Link layout is edited through scene dimensions');
    assert.ok(!manufacturing.has(key)&&!creationOnlyParameterKeys(template).has(key));
    assert.doesNotThrow(()=>validateProductPostCreationChanges(template,values,{...values,[key]:values[key]+1}));
    assert.notDeepEqual(productDisplayParameters(template,values),productDisplayParameters(template,{...values,[key]:values[key]+1}));
  }
}
for (const frameType of ['two_vertical', 'closed_frame', 'bottom_rail'])
for (const handleEnabled of ['yes', 'no'])
for (const handleSide of ['left', 'right', 'center_double'])
for (const handleClosure of ['closed', 'open_slot'])
for (const barLayoutMode of ['center_spacing', 'maximum_gap', 'fixed_count'])
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
  assert.ok(add.includes('data-tube-designer-parameter="layoutStyle"'));
  assert.ok(!right.includes('data-tube-designer-parameter="frameType"'));
  assert.ok(!right.includes('data-tube-designer-parameter="layoutStyle"'));
  const resolved = resolveProductSpecificationAnnotations(designer, view);
  const specifications = resolveProductSpecificationAnnotationTree(designer, view);
  assert.deepEqual(specifications.children.map(group => group.key), ['overall', 'handle', 'layout', 'mounting_layout']);
  assert.ok(specifications.children.every(group => !group.children?.length),
    'Single specification groups must not be wrapped in duplicate, identically named sections');
  for (const field of raw.parameters.filter(field => ['overall', 'handle', 'layout', 'mounting_layout'].includes(field.group)
      && field.key !== 'productCode' && !creationOnlyParameterKeys(template).has(field.key))) {
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
  assert.equal(parameterVisible(fields.get('barCenterSpacing'), values), barLayoutMode === 'center_spacing');
  assert.equal(parameterVisible(fields.get('firstBarTopOffset'), values), frameType !== 'closed_frame'
    && (barLayoutMode !== 'center_spacing' || handleEnabled === 'yes' || frameType === 'bottom_rail'));
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

for (const role of ['frame', 'inner', 'support']) {
  const binding = raw.extensions.resourceRoles.profiles[role];
  const selector = fields.get(`${role}ProfileType`);
  assert.equal(binding.managedParameters.length, 4);
  assert.deepEqual(binding.defaultParametersByResource['system:rect'],
    role === 'support' ? { width: 20, depth: 10, wallThickness: 1, cornerRadius: 1 }
      : { width: 10, depth: 5, wallThickness: 0.8, cornerRadius: 1 });
  assert.deepEqual(selector.presentation.profileConstraints.sectionKinds, ['rect-bar','rect','round']);
  assert.equal(selector.defaultValue, 'rect-bar');
  assert.equal(selector.presentation.profileConstraints.minimumContourCount, 1);
  assert.equal(selector.presentation.profileConstraints.hollow, undefined);
  assert.deepEqual(binding.defaultParametersByResource['system:rect-bar'], { width: role==='support'?20:10, depth:5 });
  assert.deepEqual(binding.parameterBindingsBySectionKind['rect-bar'], { width:role+'Width', depth:role+'Depth' });
  assert.equal(binding.parameterLabelsBySectionKind['rect-bar'].depth['zh-CN'], '厚');
  assert.equal(parameterVisible(fields.get(role+'WallThickness'), { ...defaults, layoutStyle:'balcony_closure' }), false);
  assert.equal(parameterVisible(fields.get(role+'CornerRadius'), { ...defaults, layoutStyle:'balcony_closure' }), false);
  assert.deepEqual(selector.presentation.allowedScopes, ['system', 'user']);
  assert.ok(raw.parameters.filter(field => field.group === `${role}_profile`)
    .every(field => !manufacturing.has(field.key)));
}
for (const layoutStyle of ['planar', 'five_face', 'balcony_closure']) {
  const values = { ...defaults, layoutStyle };
  const designer = { templates: [template], product: { entityId: 'support-profile-test', templateId: raw.id, parameters: values } };
  const view = { activeAreaId: 'view', scene: { tubeDesigner: designer } };
  const right = renderDesignerRightParameterContent(designer, view).scrollContent;
  assert.equal(right.includes('data-tube-designer-profile-parameter="supportProfileType"'), layoutStyle !== 'planar');
  for (const field of raw.parameters.filter(f => f.group === 'support_profile'))
    assert.equal(parameterVisible(field, values), layoutStyle !== 'planar' && !['supportWallThickness','supportCornerRadius'].includes(field.key));
}
assert.deepEqual(raw.extensions.sceneParameterBindings.profileRoles.support.memberStableKeyPrefixes, ['frame.support.']);
assert.ok(!raw.extensions.sceneParameterBindings.profileRoles.frame.memberStableKeyPrefixes.includes('frame.'));
assert.notDeepEqual(productDisplayParameters(template, { ...defaults, layoutStyle: 'five_face', supportWidth: 25 }),
  productDisplayParameters(template, { ...defaults, layoutStyle: 'five_face' }), 'C profile edits expire the actual model');
const projectionMaterials = [
  ['projectionVertical', 'projection_vertical_profile', 15, ['frame.rear_left.0001', 'frame.rear_right.0001']],
  ['projectionHorizontal', 'projection_horizontal_profile', 12, ['frame.rear_top.0001', 'frame.rear_bottom.0001',
    'frame.return_left.bottom', 'frame.return_left.top', 'frame.return_right.bottom', 'frame.return_right.top']],
];
for (const [role, group, size] of projectionMaterials) {
  const binding = raw.extensions.resourceRoles.profiles[role], selector = fields.get(`${role}ProfileType`);
  assert.equal(selector.defaultValue, 'rect');
  assert.deepEqual(selector.choices.map(c=>c.value), ['rect','rect-bar','round']);
  assert.deepEqual(selector.presentation.profileConstraints.sectionKinds, ['rect','rect-bar','round']);
  assert.deepEqual(binding.defaultParametersByResource['system:rect'], {width:size,depth:size,wallThickness:.8,cornerRadius:1});
  assert.deepEqual(binding.parameterBindingsBySectionKind['rect-bar'], {width:role+'Width',depth:role+'Depth'});
  assert.deepEqual(binding.parameterBindingsBySectionKind.round, {width:role+'Width',wallThickness:role+'WallThickness'});
  assert.equal(binding.managedParameters.length,4,'Only finite product section parameters are exposed');
  for(const layoutStyle of ['planar','five_face','balcony_closure']) for(const kind of ['rect','rect-bar','round']) {
    const active = layoutStyle === 'five_face' || role === 'projectionHorizontal' && layoutStyle === 'balcony_closure';
    const values={...defaults,layoutStyle,[role+'ProfileType']:kind};
    const designer={templates:[template],product:{entityId:'projection-profile-test',templateId:raw.id,parameters:values}};
    const view={activeAreaId:'view',scene:{tubeDesigner:designer}};
    const right=renderDesignerRightParameterContent(designer,view).scrollContent;
    assert.equal(right.includes(`data-tube-designer-profile-parameter="${role}ProfileType"`),active);
    for(const field of raw.parameters.filter(f=>f.group===group)) {
      const suffix=field.key.slice(role.length);
      const visible=active && (suffix==='Depth'?kind!=='round':suffix==='WallThickness'?kind!=='rect-bar':suffix==='CornerRadius'?kind==='rect':true);
      assert.equal(parameterVisible(field,values),visible,`${field.key}/${layoutStyle}/${kind}`);
      assert.ok(!manufacturing.has(field.key)&&!creationOnlyParameterKeys(template).has(field.key));
      const next={...values,[field.key]:suffix==='ProfileType'?(kind==='rect'?'round':'rect'):values[field.key]+1};
      assert.doesNotThrow(()=>validateProductPostCreationChanges(template,values,next));
      if(visible) assert.notDeepEqual(productDisplayParameters(template,values),productDisplayParameters(template,next),`${field.key}: material changes expire the model`);
    }
  }
}
const ownershipSamples = [
  ['frame',['frame.left.0001','frame.right.0001','frame.top.0001','frame.bottom.0001']],
  ['inner',['inner.full.0001.01.span.0001','inner.return_left.0002','inner.top_return.0001','inner.support_link.0001.0001']],
  ['support',['frame.support.0001']],
  ['drying',['frame.drying.rod.0001','frame.drying.left.0001','frame.drying.right.0001']],
  ...projectionMaterials.map(([role,, ,keys])=>[role,keys]),
];
const roleMembers=ownershipSamples.flatMap(([role,keys])=>keys.map(key=>({entityId:key,stableKey:key,role})));
for(const [role,keys] of ownershipSamples) {
  assert.deepEqual(productSceneMemberIds(template,roleMembers,'',{profileRole:role}).sort(),[...keys].sort(),`${role}: select exactly the assigned material members`);
  for(const key of keys) assert.deepEqual(productParameterTargetsForSceneMember(template,roleMembers.find(m=>m.stableKey===key)).profileRoles,[role],`${key}: only one material role`);
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
let returnJointStates = 0;
for (const layoutStyle of ['planar', 'five_face', 'balcony_closure'])
for (const jointMode of jointModes)
for (const returnJointMode of jointModes)
for (const maleEndForm of ['tongue', 'full_section']) {
  const values = { ...defaults, layoutStyle, jointMode, returnJointMode, maleEndForm };
  const activeModes = [jointMode, ...(layoutStyle !== 'planar' ? [returnJointMode] : [])];
  const inserted = activeModes.some(mode => mode !== 'butt_weld');
  assert.equal(parameterVisible(fields.get('returnJointMode'), values), layoutStyle !== 'planar');
  const expected = { maleEndForm: inserted,
    maleHeadLength: activeModes.includes('male_female_half_hole'),
    maleHeadWidth: inserted && maleEndForm === 'tongue',
    maleTabAngle: inserted && maleEndForm === 'tongue',
    holeTotalClearanceDepth: inserted && maleEndForm === 'tongue',
    fullHeadTotalClearance: inserted && maleEndForm === 'full_section' };
  for (const [key, visible] of Object.entries(expected))
    assert.equal(parameterVisible(fields.get(key), values), visible, `${layoutStyle}/${jointMode}/${returnJointMode}/${key}`);
  assert.deepEqual(productDisplayParameters(template, values),
    productDisplayParameters(template, { ...defaults, layoutStyle }), 'Both connection settings expire manufacturing only');
  if (layoutStyle !== 'planar' && jointMode === 'butt_weld' && inserted) {
    const detail = raw.extensions.parameterGroupDetails.find(d => d.part === 'male' && d.receiverAxis === 'depth');
    const model = insertJointParameterDetailModel(detail, values, { template });
    assert.equal(model.active, true);
    assert.equal(model.available, false, 'Solid flat bar must not be drawn as a hollow tube tongue');
    assert.match(model.message, /中空管/);
    assert.equal(model.through, returnJointMode === 'male_female_through_hole');
  }
  returnJointStates++;
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
console.log(`PASS minimal grille: ${states} UI states, ${annotationsChecked} annotation ownership checks, ${jointStates} joint-condition states, ${returnJointStates} return-joint states and independent window catalogue entry (renderer unit fixtures)`);
