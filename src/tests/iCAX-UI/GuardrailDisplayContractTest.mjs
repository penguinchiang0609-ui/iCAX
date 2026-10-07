import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { catalogText } from '../../apps/tube-designer/webpage/productCatalog.mjs';
import { parameterVisible } from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import { applyProductControlChoice, productControlChoices, productControlFields, productControlValue,
  productStructureEditors } from '../../apps/tube-designer/webpage/productControls.mjs';
import { productSceneMemberIds, renderProductParameterDiagram,
  renderProductInstanceThumbnail } from '../../apps/tube-designer/webpage/productParameterDiagram.mjs';
import { renderDesignerAddParameterContent, renderDesignerRightPane } from '../../apps/tube-designer/webpage/designerViews.mjs';

const directories = ['modular_guardrail', 'modular_guardrail_cross-straight',
  'modular_guardrail_diamond-straight', 'modular_guardrail_glass-straight'];
const sourceManifest = JSON.parse(readFileSync(new URL('../../apps/tube-designer/product.manifest.json', import.meta.url), 'utf8'));
const mappings = {
  guardrailHandrailStructure: { field: 'handrailMode', choices: { per_bay: 'per_bay', continuous: 'continuous' } },
  guardrailPostStructure: { field: 'largePostMode', choices: { none: 'none', middle: 'middle', ends: 'ends' } },
  guardrailRailStructure: { field: 'railCount', choices: { two: 2, three: 3 } },
  guardrailInfillStructure: { field: 'barOrientation', choices: { vertical: 'vertical', horizontal: 'horizontal' } },
};
let routes = 0;
let diagramCases = 0;
let rightHeadingCases = 0;
const expectedHeadingLayouts = {
  straight: { label: '直式', lengths: ['3,217'] },
  left_l: { label: '左转 L 型', lengths: ['3,217', '1,843'] },
  right_l: { label: '右转 L 型', lengths: ['3,217', '1,843'] },
  u: { label: 'U 型', lengths: ['3,217', '1,843', '2,671'] },
};
// Component groups contain SVG primitives, so extract each named group's own
// primitives without interpreting unrelated outer layout or dimension groups.
function componentGroups(artwork, className) {
  const escaped = className.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const expression = new RegExp(`<g\\b(?=[^>]*\\bclass="[^"]*\\b${escaped}\\b)([^>]*)>([\\s\\S]*?)<\\/g>`, 'g');
  return [...artwork.matchAll(expression)].map(match => ({ attributes: match[1], content: match[2] }));
}
function attribute(source, name) {
  return source.match(new RegExp(`\\b${name}="([^"]*)"`))?.[1] ?? '';
}
function svgLines(content, className) {
  return [...content.matchAll(/<line\b([^>]*)\/?\s*>/g)].filter(match =>
    !className || attribute(match[1], 'class').split(/\s+/).includes(className)).map(match => {
    const values = ['x1', 'y1', 'x2', 'y2'].map(key => Number(attribute(match[1], key)));
    assert.ok(values.every(Number.isFinite), 'The rendered line must have actual numeric SVG endpoints');
    return { x1: values[0], y1: values[1], x2: values[2], y2: values[3],
      dx: values[2] - values[0], dy: values[3] - values[1] };
  });
}
function assertGuideSeparatesHandrail(handrail, guide) {
  const length = Math.hypot(handrail.dx, handrail.dy);
  assert.ok(length > 0, 'The actual handrail segment must have nonzero length');
  assert.ok(handrail.dx * guide.dx + handrail.dy * guide.dy > 0,
    'The slope guide must point in the same direction as its handrail');
  assert.ok(Math.abs(handrail.dx * guide.dy - handrail.dy * guide.dx) / length < 1e-6,
    'The slope guide must remain parallel to its actual handrail segment');
  const startGap = (handrail.dx * (guide.y1 - handrail.y1) - handrail.dy * (guide.x1 - handrail.x1)) / length;
  const endGap = (handrail.dx * (guide.y2 - handrail.y2) - handrail.dy * (guide.x2 - handrail.x2)) / length;
  assert.ok(Math.abs(startGap) > 1e-6 && Math.abs(endGap) > 1e-6,
    'The interactive slope guide must not occupy the handrail line and intercept its editor target');
  assert.ok(Math.abs(startGap - endGap) < 1e-6,
    'Both guide endpoints must stay on the same separate parallel line');
}
function assertComponentTarget(group, primary, secondary = null) {
  assert.equal(attribute(group.attributes, 'data-tube-designer-parameter-key'), primary);
  assert.equal(attribute(group.attributes, 'data-cam-action'), 'tube-designer-focus-product-parameter');
  assert.equal(attribute(group.attributes, 'role'), 'button');
  const linked = attribute(group.attributes, 'data-product-diagram-parameter').split(/\s+/);
  assert.ok(linked.includes(primary));
  if (secondary) assert.ok(linked.includes(secondary), 'The post group must retain its separate bay-count link');
}
for (const directory of directories) {
  const base = new URL(`../../apps/tube-designer/templates/product/${directory}/`, import.meta.url);
  const descriptor = JSON.parse(readFileSync(new URL('template.json', base), 'utf8'));
  const display = JSON.parse(readFileSync(new URL('display.json', base), 'utf8'));
  const catalogEntry = sourceManifest.capabilities.tubeDesigner.templates.find(entry => entry.templateId === descriptor.id);
  assert.ok(catalogEntry, `${directory}: real source manifest must register this product template`);
  assert.equal(catalogEntry.version, descriptor.version, `${directory}: source catalogue version must match the current descriptor`);
  assert.equal(display.schema, 'icax.template-display');
  assert.equal(display.schemaVersion, 1);
  const template = { ...descriptor, display, available: true,
    groups: descriptor.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
    parameters: descriptor.parameters.map(field => ({ ...field, groupKey: field.group,
      type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
      displayName: catalogText(field.displayName),
      options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })),
  };
  const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
  const physicalKeys = Object.keys(defaults).sort();
  const fields = productControlFields(template);
  const expectedControls = Object.keys(mappings).filter(key => directory === 'modular_guardrail' || key !== 'guardrailInfillStructure');
  assert.deepEqual(fields.map(field => field.key).sort(), expectedControls.sort());
  assert.deepEqual(productStructureEditors(template).map(editor => editor.controlKey).sort(), expectedControls);
  const addParameters = descriptor.extensions.addDialog.structureParameters;
  assert.ok(expectedControls.every(key => addParameters.includes(key)), `${directory}: new product dialog lost a structure editor`);
  assert.ok(!Object.values(mappings).some(mapping => addParameters.includes(mapping.field)), `${directory}: dialog still references a hidden raw selector`);
  const addHtml = renderDesignerAddParameterContent({ templates: [template] }, { activeAreaId: 'view',
    tubeDesignerAddTemplateId: descriptor.id, tubeDesignerAddDraft: { ...defaults } });
  const levelHandrail = svgLines(addHtml, 'product-diagram-handrail');
  const levelRails = svgLines(addHtml, 'product-diagram-rail');
  const levelPosts = svgLines(addHtml, 'product-diagram-post');
  assert.equal(levelHandrail.length, 1);
  assert.ok(levelHandrail.concat(levelRails).every(line => Math.abs(line.dy) < 0.01),
    `${directory}: horizontal structure must be drawn horizontally`);
  assert.equal(svgLines(addHtml, 'product-diagram-slope-guide').length, 0,
    'Compact structure preview must not show an auxiliary line resembling another handrail');
  const shownRatio = Math.abs(levelHandrail[0].dx / levelPosts[0].dy);
  const actualRatio = defaults.sideLength1 / (defaults.guardHeight - defaults.handrailDepth / 2);
  assert.ok(Math.abs(shownRatio - actualRatio) < 0.01, 'Structure preview preserves its width/height proportions');
  assert.ok(levelRails.every(line => line.y1 < levelPosts[0].y2), 'Bottom rails must leave the declared clearance above the post feet');
  const round = { ...defaults, handrailProfileType: 'round', railProfileType: 'round' };
  assert.equal(renderProductParameterDiagram(template, { ...round, handrailDepth: 123, railDepth: 147 }),
    renderProductParameterDiagram(template, round),
    'Inactive rectangular depth drafts must not affect round handrails or rails');
  if (directory === 'modular_guardrail_glass-straight') {
    const panels = [...addHtml.matchAll(/<polygon\b([^>]*)>/g)]
      .filter(match => attribute(match[1], 'class').includes('product-diagram-glass'));
    assert.equal(panels.length, defaults.sideBayCount1);
    for (const panel of panels) {
      const ys = attribute(panel[1], 'points').trim().split(/\s+/).map(point => Number(point.split(',')[1]));
      assert.ok(Math.min(...ys) > levelHandrail[0].y1 && Math.max(...ys) < levelRails[0].y1,
        'Board panels fit between the handrail and bottom rail, above the foot clearance');
    }
  }
  for (const key of expectedControls) assert.ok(addHtml.includes(`data-product-control-editor="${key}"`), `${directory}: ${key} not rendered in the real add form`);
  const roles = descriptor.extensions.productDiagram.profileRoles;
  const members = [
    { entityId: 'handrail', role: 'guardrail.handrail' },
    { entityId: 'post', role: 'guardrail.post' },
    { entityId: 'rail', role: 'guardrail.cross_rail' },
    { entityId: 'infill', role: 'guardrail.vertical_bar' },
  ];
  for (const role of ['handrail', 'post', 'rail', 'infill']) {
    const radius = `${role}CornerRadius`;
    assert.ok(roles[role].parameters.includes(radius), `${directory}: ${radius} must target its actual member role`);
    assert.deepEqual(productSceneMemberIds(template, members, radius), [role]);
  }
  for (const key of expectedControls) {
    const mapping = mappings[key];
    const control = fields.find(field => field.key === key);
    const physical = descriptor.parameters.find(field => field.key === mapping.field);
    assert.equal(parameterVisible(physical, defaults), false, `${directory}: replaced raw selector remains hidden`);
    const choices = productControlChoices(template, key, defaults);
    assert.deepEqual(choices.filter(choice => !choice.disabled).map(choice => choice.value).sort(), Object.keys(mapping.choices).sort());
    assert.ok(catalogText(control.displayName));
    for (const choice of choices.filter(choice => !choice.disabled)) {
      const before = structuredClone(defaults);
      const next = applyProductControlChoice({}, template, key, defaults, choice.value);
      assert.deepEqual(defaults, before, `${directory}: editing ${key} mutated host input`);
      assert.deepEqual(Object.keys(next).sort(), physicalKeys, `${directory}: synthetic UI key was persisted`);
      assert.equal(next[mapping.field], mapping.choices[choice.value]);
      assert.equal(productControlValue(template, key, next), choice.value);
      assert.deepEqual(applyProductControlChoice({}, template, key, next, choice.value), next);
      for (const field of descriptor.parameters.filter(field => field.valueType === 'enum')) {
        assert.ok(field.choices.some(item => Object.is(item.value, next[field.key])), `${directory}: invalid ${field.key} assignment`);
      }
      routes++;
    }
  }
  const stepped = { ...defaults, pathMode: 'stepped', handrailMode: 'continuous' };
  assert.equal(parameterVisible(fields.find(field => field.key === 'guardrailHandrailStructure'), stepped), false);
  assert.equal(stepped.handrailMode, 'continuous', 'An inactive structure draft remains owned by the physical parameters');
  assert.throws(() => applyProductControlChoice({}, template, 'guardrailHandrailStructure', stepped, 'per_bay'));
  if (directory !== 'modular_guardrail') {
    assert.equal(descriptor.parameters.find(field => field.key === 'infillType').choices.length, 1);
  }
  const templateBeforeArtwork = structuredClone(template);
  for (const layout of descriptor.parameters.find(field => field.key === 'layout').choices.map(choice => choice.value)) {
    const expectedHeading = expectedHeadingLayouts[layout];
    assert.ok(expectedHeading, `${directory}: exercise every real layout in the right-pane heading contract`);
    const headingParameters = { ...defaults, layout, sideLength1: 3217, sideLength2: 1843,
      sideLength3: 2671, guardHeight: 1097 };
    const headingParametersBefore = structuredClone(headingParameters);
    assert.deepEqual(Object.keys(headingParameters).sort(), physicalKeys,
      'The heading uses the public side lengths and guard height without inventing width/height fields');
    const rightHtml = renderDesignerRightPane({}, { scene: { tubeDesigner: { templates: [template],
      product: { entityId: `${descriptor.id}:heading`, templateId: descriptor.id,
        name: catalogText(descriptor.displayName), parameters: headingParameters }, members: [] } } });
    const headingText = rightHtml.match(/class="tube-designer-heading">\s*<strong>[^<]*<\/strong>\s*<span>([^<]*)<\/span>/)?.[1];
    assert.equal(headingText,
      `${expectedHeading.label} · 各段 ${expectedHeading.lengths.join(' / ')} · 高 1,097 mm · 0 个构件`,
      `${directory}/${layout}: actual right-pane heading must show only the active side lengths and real guard height`);
    const dimensions = headingText.match(/各段 ([^·]+) · 高 ([^ ]+) mm/);
    assert.deepEqual(dimensions[1].trim().split(' / '), expectedHeading.lengths,
      `${directory}/${layout}: each active segment appears once and inactive segments are omitted`);
    assert.equal(dimensions[2], '1,097');
    assert.deepEqual(headingParameters, headingParametersBefore, 'Rendering the heading must not mutate physical inputs');
    rightHeadingCases++;
    const segments = layout === 'straight' ? 1 : layout === 'u' ? 3 : 2;
    for (const railCount of descriptor.parameters.find(field => field.key === 'railCount').choices.map(choice => choice.value)) {
      for (const mode of ['add', 'right']) {
        for (const pathMode of ['level', 'continuous']) {
          const values = { ...defaults, layout, railCount, pathMode,
            slopeAngle: 17, slopeAngle2: -11, slopeAngle3: 29 };
          const before = structuredClone(values);
          const artwork = renderProductParameterDiagram(template, values,
            { mode, activeParameter: 'guardrailPostStructure' });
          const handrails = componentGroups(artwork, 'product-diagram-guardrail-handrail');
          const rails = componentGroups(artwork, 'product-diagram-guardrail-rails');
          const posts = componentGroups(artwork, 'product-diagram-guardrail-bays');
          const slopeGuides = componentGroups(artwork, 'product-diagram-guardrail-slope');
          assert.equal(handrails.length, segments, `${directory}: each segment needs its own handrail editor link`);
          assert.equal(rails.length, segments);
          assert.equal(posts.length, segments);
          assert.equal(slopeGuides.length, segments, `${directory}: every segment needs a separate slope guide`);
          for (let segment = 0; segment < segments; segment++) {
            assertComponentTarget(handrails[segment], 'guardrailHandrailStructure');
            assertComponentTarget(rails[segment], 'guardrailRailStructure');
            assertComponentTarget(posts[segment], 'guardrailPostStructure', `sideBayCount${segment + 1}`);
            assert.ok(attribute(posts[segment].attributes, 'class').split(/\s+/).includes('is-active'));
            const handrailLines = svgLines(handrails[segment].content, 'product-diagram-handrail');
            const guideLines = svgLines(slopeGuides[segment].content, 'product-diagram-slope-guide');
            assert.equal(handrailLines.length, 1);
            assert.equal(guideLines.length, 1);
            assertGuideSeparatesHandrail(handrailLines[0], guideLines[0]);
            assert.equal(svgLines(rails[segment].content, 'product-diagram-rail').length, railCount - 1,
              'Synthetic interaction keys must not replace the physical numeric rail-count geometry input');
          }
          assert.ok(!/data-product-diagram-parameter="railCount"/.test(artwork), 'Hidden railCount is no longer an editor target');
          if (directory === 'modular_guardrail') {
            const infill = componentGroups(artwork, 'product-diagram-guardrail-infill');
            assert.equal(infill.length, segments);
            infill.forEach(group => assertComponentTarget(group, 'guardrailInfillStructure'));
          }
          assert.deepEqual(values, before, `${directory}: diagram interaction routing mutated physical inputs`);
          diagramCases++;
        }
      }
    }
  }
  const steppedBeforeArtwork = structuredClone(stepped);
  const steppedArtwork = renderProductParameterDiagram(template, stepped, { mode: 'right' });
  const steppedHandrails = componentGroups(steppedArtwork, 'product-diagram-guardrail-handrail');
  assert.equal(steppedHandrails.length, 1);
  for (const group of steppedHandrails) {
    assert.equal(attribute(group.attributes, 'data-cam-action'), '');
    assert.equal(attribute(group.attributes, 'data-tube-designer-parameter-key'), '');
    assert.equal(attribute(group.attributes, 'data-product-diagram-parameter'), '');
    assert.equal(attribute(group.attributes, 'tabindex'), '');
    assert.equal(attribute(group.attributes, 'role'), '');
    assert.ok(!/display\s*:\s*none/.test(group.attributes), 'An inactive handrail editor must not hide the drawn component');
    const steps = svgLines(group.content, 'product-diagram-handrail');
    assert.equal(steps.length, stepped.sideBayCount1);
    assert.ok(steps.every(line => Math.abs(line.dy) < 1e-6), 'Stepped bays must retain horizontal handrails');
  }
  assert.deepEqual(stepped, steppedBeforeArtwork);
  diagramCases++;
  if (directory === 'modular_guardrail') {
    for (const render of [values => renderProductParameterDiagram(template, values, { mode: 'right' }),
      values => renderProductInstanceThumbnail(template, values)]) {
      const vertical = { ...defaults, layout: 'straight', sideBayCount1: 2, barOrientation: 'vertical' };
      const horizontal = { ...vertical, barOrientation: 'horizontal' };
      const verticalBefore = structuredClone(vertical), horizontalBefore = structuredClone(horizontal);
      const verticalLines = componentGroups(render(vertical), 'product-diagram-guardrail-infill')
        .flatMap(group => svgLines(group.content, 'product-diagram-infill'));
      const horizontalLines = componentGroups(render(horizontal), 'product-diagram-guardrail-infill')
        .flatMap(group => svgLines(group.content, 'product-diagram-infill'));
      assert.ok(verticalLines.length > 0 && horizontalLines.length > 0);
      assert.ok(verticalLines.every(line => Math.abs(line.dx) < 1e-6 && Math.abs(line.dy) > 1e-6),
        'Vertical public orientation must produce vertical fill geometry');
      assert.ok(horizontalLines.every(line => Math.abs(line.dx) > Math.abs(line.dy) && Math.abs(line.dx) > 1e-6),
        'Horizontal public orientation must actually produce across-bay fill geometry in both diagram and instance thumbnail');
      assert.equal(horizontal.infillType, 'bars', 'The fixed public infillType remains bars');
      assert.deepEqual(vertical, verticalBefore);
      assert.deepEqual(horizontal, horizontalBefore, 'Internal SVG pattern selection must not rewrite host parameters');
      diagramCases += 2;
    }
  } else {
    const before = structuredClone(defaults);
    const artwork = renderProductInstanceThumbnail(template, defaults);
    const infill = componentGroups(artwork, 'product-diagram-guardrail-infill');
    assert.ok(infill.length > 0 && infill.every(group => group.content.length > 0));
    if (directory.endsWith('glass-straight')) assert.ok(infill.every(group => /class="product-diagram-glass"/.test(group.content)));
    if (directory.endsWith('cross-straight')) assert.ok(infill.every(group => {
      const lines = svgLines(group.content, 'product-diagram-infill');
      return lines.length >= 2 && lines.every(line => Math.abs(line.dx) > 1e-6 && Math.abs(line.dy) > 1e-6)
        && lines.some(line => line.dx * line.dy > 0) && lines.some(line => line.dx * line.dy < 0);
    }), 'The fixed cross pattern must retain both actual diagonal directions');
    if (directory.endsWith('diamond-straight')) assert.ok(infill.every(group => /<polygon\b[^>]*class="product-diagram-infill"/.test(group.content)));
    assert.deepEqual(defaults, before, `${directory}: fixed-pattern thumbnail mutated public parameters`);
    diagramCases++;
  }
  assert.deepEqual(template, templateBeforeArtwork, `${directory}: artwork must not rewrite the descriptor`);
}
console.log(`PASS ${directories.length} guardrail display contracts, ${routes} actual structure-choice routes, ${diagramCases} diagram/thumbnail cases, ${rightHeadingCases} real right-pane dimension headings and member-specific radius targets`);
