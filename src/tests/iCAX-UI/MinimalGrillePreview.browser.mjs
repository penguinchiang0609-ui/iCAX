import assert from 'node:assert/strict';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, sep } from 'node:path';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { minimalGrilleDiagramStocks } from '../../apps/tube-designer/webpage/minimalGrilleDiagram.mjs';

const sourceRoot = fileURLToPath(new URL('../../', import.meta.url));
const evidence = resolve(process.env.ICAX_GRILLE_VISUAL_EVIDENCE || 'output/tests/minimal-protective-grille-balcony-20261110/browser');
mkdirSync(evidence, { recursive: true });
const temporary = resolve(evidence, 'temp');
mkdirSync(temporary, { recursive: true });
const oracleEnvironment = { ...process.env, TEMP: temporary, TMP: temporary, TMPDIR: temporary };
const python = process.env.ICAX_PYTHON || 'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const oracle = spawnSync(python, ['-X', 'utf8', '-c', `
import json, sys
from pathlib import Path
from itertools import product
root=Path(sys.argv[1])
sys.path.insert(0,str(root/'iCAX-Engine/framework/TemplateRuntime/python'))
from icax_template_worker import _load_template
folder=root/'apps/tube-designer/templates/product/minimal_protective_grille'
descriptor=json.loads((folder/'template.json').read_text(encoding='utf-8'))
template=_load_template(str(folder/'template.py'),'grille-diagram-oracle')
defaults={f['key']:f['defaultValue'] for f in descriptor['parameters']}
cases=[]
changes=[dict(frameType=f,handleEnabled=e,handleSide=s,handleClosure=c,barLayoutMode=m)
 for f,e,s,c,m in product(('two_vertical','closed_frame','bottom_rail'),('yes','no'),
 ('left','right','center_double'),('closed','open_slot'),('center_spacing','maximum_gap','fixed_count'))]
changes += [dict(firstBarTopOffset=67,lastBarBottomOffset=73,maximumClearGap=70),
 dict(handleSide='center_double',handleCenterOffset=25),
 dict(handleBottom=420,handleHeight=210,handleWidth=190),
 dict(frameWidth=40,innerWidth=20,frameDepth=30,innerDepth=10,handleWidth=150)]
changes += [dict(handleEnabled='no',frameType='closed_frame',barLayoutMode='center_spacing',innerWidth=bw,barCenterSpacing=pitch)
 for bw,pitch in product((10,18),(80,115,150))]
changes += [dict(handleEnabled='no',frameType='two_vertical',barLayoutMode='center_spacing',firstBarTopOffset=-1)]
changes += [dict(layoutStyle='planar',projectionVerticalWidth=-1,projectionVerticalDepth=-1,projectionHorizontalWidth=-1,projectionHorizontalDepth=-1)]
for change in changes:
 p={**defaults,**change};state=template.layout(p)
 cases.append(dict(values=p,stocks=[dict(key=m['key'],role=m['role'],width=m['profile'].width,
 start=[m['start'][0],m['start'][2]],end=[m['end'][0],m['end'][2]]) for m in state['members']]))
print(json.dumps(cases))
`, sourceRoot], { encoding: 'utf8', maxBuffer: 8 * 1024 * 1024, env: oracleEnvironment });
assert.equal(oracle.status, 0, oracle.stderr);
const cases = JSON.parse(oracle.stdout);
const descriptor = JSON.parse(readFileSync(resolve(sourceRoot, 'apps/tube-designer/templates/product/minimal_protective_grille/template.json')));
for (const test of cases) {
  const actual = minimalGrilleDiagramStocks(descriptor.extensions.productDiagram, test.values).stocks;
  assert.equal(actual.length, test.stocks.length);
  for (let i = 0; i < actual.length; i++) {
    assert.equal(actual[i].key, test.stocks[i].key);
    assert.equal(actual[i].role, test.stocks[i].role);
    for (const field of ['start', 'end']) for (let axis = 0; axis < 2; axis++)
      assert(Math.abs(actual[i][field][axis] - test.stocks[i][field][axis]) < 1e-7, `${actual[i].key} ${field}`);
    assert.equal(actual[i].width, test.stocks[i].width);
  }
}

const spatialOracle = spawnSync(python, ['-X', 'utf8', '-c', `
import json, sys
from pathlib import Path
from itertools import product
root=Path(sys.argv[1])
sys.path.insert(0,str(root/'iCAX-Engine/framework/TemplateRuntime/python'))
from icax_template_worker import _load_template
folder=root/'apps/tube-designer/templates/product/minimal_protective_grille'
descriptor=json.loads((folder/'template.json').read_text(encoding='utf-8'))
template=_load_template(str(folder/'template.py'),'grille-spatial-diagram-oracle')
defaults={f['key']:f['defaultValue'] for f in descriptor['parameters']}
cases=[]
for style,side,closure,depth,drop,frame_kind,inner_kind in product(
 ('five_face','balcony_closure'),('left','right','center_double'),('closed','open_slot'),(100,300),(0,30),('rect-bar',),('rect-bar',)):
 p={**defaults,'layoutStyle':style,'width':1800,'handleSide':side,'handleClosure':closure,'supportStyle':'double',
    'projectionDepth':depth,'topDrop':drop,'frameProfileType':frame_kind,'innerProfileType':inner_kind,
    'balconyTopDepth':depth,'balconyBottomDepth':depth+70,'balconyTopDrop':drop,
    'installHoleCountPerSide':2 if style=='balcony_closure' else 3,'installHoleEnabled':'yes'}
 state=template.layout(p)
 cases.append(dict(values=p,facts=state['facts'],members=[dict(key=m['key'],role=m['role'],face=m['face'],
   width=m['profile'].width,start=m['start'],end=m['end']) for m in state['members']]))
for change in ({'width':600,'handleEnabled':'no'}, {'width':2600,'supportMaxSpacing':600,'supportWidth':30},
               {'width':1900,'handleSide':'center_double','handleCenterOffset':90,'supportProfileType':'rect-bar'},
               {'width':1600,'handleSide':'right','handleClosure':'open_slot','topDrop':250,'projectionDepth':350}):
 p={**defaults,'layoutStyle':'five_face','supportStyle':'double',**change};state=template.layout(p)
 cases.append(dict(values=p,facts=state['facts'],members=[dict(key=m['key'],role=m['role'],face=m['face'],
   width=m['profile'].width,start=m['start'],end=m['end']) for m in state['members']]))
for style,support_style,gap,kind,width in product(('five_face','balcony_closure'),('single','double'),(12,40),('rect-bar','rect','round'),(600,1800)):
 p={**defaults,'layoutStyle':style,'width':width,'handleEnabled':'no','projectionDepth':150,
    'balconyTopDepth':300,'balconyBottomDepth':150,'supportProfileType':kind,'supportDepth':10 if support_style=='single' else 5,
    'supportStyle':support_style,'supportPairClearGap':gap}
 state=template.layout(p)
 cases.append(dict(values=p,facts=state['facts'],members=[dict(key=m['key'],role=m['role'],face=m['face'],
   width=m['profile'].width,start=m['start'],end=m['end']) for m in state['members']]))
for style,count,bottom,top in product(('five_face','balcony_closure'),(0,1,3),(80,130),(90,160)):
 p={**defaults,'layoutStyle':style,'width':1200,'height':600,'handleEnabled':'no','supportStyle':'double',
    'supportLinkCount':count,'supportLinkBottomOffset':bottom,'supportLinkTopOffset':top}
 state=template.layout(p)
 cases.append(dict(values=p,facts=state['facts'],members=[dict(key=m['key'],role=m['role'],face=m['face'],
   width=m['profile'].width,start=m['start'],end=m['end']) for m in state['members']]))
for style,horizontal_kind,vertical_kind in product(('five_face','balcony_closure'),('rect','round','rect-bar'),('rect','round','rect-bar')):
 p={**defaults,'layoutStyle':style,'width':1800,'handleEnabled':'no','projectionDepth':220,'topDrop':40,
    'balconyTopDepth':300,'balconyBottomDepth':180,'balconyTopDrop':40,
    'projectionHorizontalProfileType':horizontal_kind,'projectionHorizontalWidth':14,'projectionHorizontalDepth':12,
    'projectionVerticalProfileType':vertical_kind,'projectionVerticalWidth':18,'projectionVerticalDepth':16}
 state=template.layout(p)
 cases.append(dict(values=p,facts=state['facts'],members=[dict(key=m['key'],role=m['role'],face=m['face'],
   width=m['profile'].width,start=m['start'],end=m['end']) for m in state['members']]))
p={**defaults,'layoutStyle':'balcony_closure','width':1800,'handleEnabled':'no','projectionVerticalWidth':-1,'projectionVerticalDepth':-1}
state=template.layout(p)
cases.append(dict(values=p,facts=state['facts'],members=[dict(key=m['key'],role=m['role'],face=m['face'],
  width=m['profile'].width,start=m['start'],end=m['end']) for m in state['members']]))
print(json.dumps(cases))
`, sourceRoot], { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024, env: oracleEnvironment });
assert.equal(spatialOracle.status, 0, spatialOracle.stderr);
const spatialCases = JSON.parse(spatialOracle.stdout);
const near = (actual, expected, label) => assert(Math.abs(actual - expected) < 1e-7, `${label}: ${actual} != ${expected}`);
for (const test of spatialCases) {
  const actual = minimalGrilleDiagramStocks(descriptor.extensions.productDiagram, test.values);
  const expectedByKey = new Map(test.members.map(m => [m.key, m]));
  assert.equal(actual.spatial.members.length, test.members.length);
  for (const member of actual.spatial.members) {
    const expected = expectedByKey.get(member.key);
    assert.ok(expected, `Missing physical stock ${member.key}`);
    assert.equal(member.role, expected.role);
    assert.equal(member.face, expected.face);
    near(member.width, expected.width, `${member.key} width`);
    // Native end trimming uses the receiving profile skin. Compact artwork
    // joins centre lines; compare directions and physical stock identities.
    const unit = m => { const delta=m.end.map((v,i)=>v-m.start[i]);const length=Math.hypot(...delta);return delta.map(v=>v/length); };
    const a = unit(member), b = unit(expected);
    a.forEach((v, i) => near(v, b[i], `${member.key} axis ${i}`));
    if (member.face === 'front' && member.role !== 'frame.support')
      for (const field of ['start','end']) member[field].forEach((v,i)=>near(v,expected[field][i],`${member.key} ${field} ${i}`));
  }
  for (const [key, expected] of [['supportX', test.facts.supportPositions], ['supportGroupCenters', test.facts.supportGroupCenters],
      ['supportLinkCenters', test.facts.supportLinkCenters], ['returnX', test.facts.returnPositions]]) {
    assert.equal(actual.spatial[key].length, expected.length);
    actual.spatial[key].forEach((v,i)=>near(v,expected[i],`${key}[${i}]`));
  }
  near(actual.spatial.returnActualClearGap, test.facts.returnActualClearGap, 'return clear gap');
  actual.spatial.returnClearGaps.forEach((gap, i) => {
    near(gap, test.facts.returnClearGaps[i], `return clear gap ${i}`);
    const roundoff = 4 * Number.EPSILON * Math.max(1, test.values.width);
    assert(gap <= test.values.returnMaximumClearGap + 1e-7 + roundoff,
      `Return gap ${gap} exceeds ${test.values.returnMaximumClearGap}; ${JSON.stringify(test.values)}`);
  });
  for (const x of actual.spatial.returnX) for (const support of actual.spatial.supportX)
    assert(Math.abs(x - support) >= (actual.spatial.supportWidth + actual.spatial.innerDepth) / 2,
      'Top and bottom returns must clear the deeper support shoulders');
  for (const x of actual.spatial.returnX) {
    assert(x - actual.spatial.innerDepth / 2 > actual.spatial.frameWidth,
      'The whole return section must clear the left corner upright');
    assert(x + actual.spatial.innerDepth / 2 < actual.width - actual.spatial.frameWidth,
      'The whole return section must clear the right corner upright');
  }
  near(actual.spatial.rearY, test.facts.mountingPlaneY, 'rear mounting plane');
  near(actual.spatial.bottomY, test.facts.bottomMountingPlaneY, 'bottom mounting plane');
  assert.equal(actual.spatial.supportConnectionKind,test.facts.supportConnectionKind,'support material controls real endpoint connection');
  assert.equal(actual.spatial.continuousHorizontalRows,test.facts.continuousHorizontalRows,'actual split rows control continuity');
  for(const key of ['frontOuterY','topRearOuterY','bottomRearOuterY','bottomCenterZ','roofFrontCenterZ','roofRearCenterZ',
      'returnBottomZ','returnFrontTopZ','returnRearTopZ']) near(actual.spatial[key],test.facts[key],key);
  if(test.values.layoutStyle==='five_face') near(actual.spatial.rearVerticalY,test.facts.rearUprightY,'rear upright y');
  const expectedRole = key => key.startsWith('frame.rear_left.')||key.startsWith('frame.rear_right.')?'projectionVertical'
    :key.startsWith('frame.rear_top.')||key.startsWith('frame.rear_bottom.')||key.startsWith('frame.return_')?'projectionHorizontal'
      :key.startsWith('frame.support.')?'support':key.startsWith('frame.')?'frame':'inner';
  for(const member of actual.spatial.members) assert.equal(member.profileRole,expectedRole(member.key),`${member.key}: material group`);
  if(test.values.supportStyle==='single'&&test.values.supportProfileType==='rect-bar') {
    for(const center of actual.spatial.supportX) for(const row of actual.spatial.members.filter(m=>m.face==='front'&&m.role.startsWith('inner.')&&Math.abs(m.start[2]-m.end[2])<1e-7))
      assert(row.end[0]<=center-actual.spatial.supportWidth/2+1e-7||row.start[0]>=center+actual.spatial.supportWidth/2-1e-7,
        'Ordinary B rods must end on each side of a solid single C without drilling it');
  }
  if(test.values.supportStyle==='double') {
    const {supportX,supportGroupCenters,supportWidth} = actual.spatial;
    assert(supportGroupCenters.length>=1,'Double support always needs a real pair');
    assert.equal(supportX.length,2*supportGroupCenters.length);
    for(const [i,center] of supportGroupCenters.entries()) {
      near((supportX[2*i]+supportX[2*i+1])/2,center,'pair group centre');
      near(supportX[2*i+1]-supportX[2*i]-supportWidth,test.values.supportPairClearGap,'pair inner clear gap');
      const left=supportX[2*i]-supportWidth/2,right=supportX[2*i+1]+supportWidth/2;
      for(const row of actual.spatial.members.filter(m=>m.face==='front'&&m.role.startsWith('inner.')&&m.role!=='inner.support_link'&&Math.abs(m.start[2]-m.end[2])<1e-7))
        assert(row.end[0]<=left+1e-7||row.start[0]>=right-1e-7,'Ordinary horizontal B stocks must stop at the pair and leave its opening empty');
    }
    const links=actual.spatial.members.filter(m=>m.role==='inner.support_link');
    assert.equal(links.length,test.values.supportLinkCount*supportGroupCenters.length,'Independent short links must match the per-group count');
    for(const link of links) near(link.end[0]-link.start[0],test.values.supportPairClearGap,'Independent link between inner pair surfaces');
    for(const key of ['frame.top.0001','frame.bottom.0001']) {
      const frame=actual.spatial.members.find(m=>m.key===key);
      near(frame.start[0],actual.spatial.frameWidth,'continuous frame start');
      near(frame.end[0],actual.width-actual.spatial.frameWidth,'continuous frame end');
    }
  }
  if(test.values.layoutStyle==='balcony_closure') {
    assert(!actual.spatial.members.some(m=>/^frame\.rear_(left|right)\./.test(m.key)));
    assert.equal(actual.spatial.members.filter(m=>m.role.startsWith('inner.return_')).length,0);
    assert.equal(actual.spatial.members.filter(m=>m.role.startsWith('frame.return_')).length,4);
    assert(actual.installationHoles.groups.every(hole=>hole.point[1]===0&&!hole.memberKey.includes('rear_')));
  }
  assert.deepEqual(actual.installationHoles.centers, test.facts.installationHoles.centers);
  actual.installationHoles.groups.forEach((hole,i)=>{
    const expected=test.facts.installationHoles.groups[i];
    assert.equal(hole.memberKey, expected.memberKey);
    hole.point.forEach((v,axis)=>near(v,expected.point[axis],`installation hole ${i} ${axis}`));
  });
}

if (process.env.ICAX_GRILLE_GEOMETRY_ONLY === '1') {
  const report = { passed: true, geometryOracleCases: cases.length, spatialGeometryOracleCases: spatialCases.length,
    actualBrowserEvents: false, standaloneNative: false };
  writeFileSync(resolve(evidence, 'geometry-oracle-report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report));
  process.exit(0);
}

const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const browser = await chromium.launch({ headless: true, channel: 'msedge' });
try {
  const page = await browser.newPage({ viewport: { width: 1300, height: 800 } });
  page.on('requestfailed', request => console.error('Module request failed:', request.url(), request.failure()?.errorText));
  await page.route('http://grille.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><body></body>' });
    const target = resolve(sourceRoot, path.slice('/src/'.length));
    if (!path.startsWith('/src/') || !target.startsWith(sourceRoot.replace(/[\\/]$/, '') + sep) || !/\.m?js$/.test(target)) return route.abort();
    return route.fulfill({ contentType: 'text/javascript', body: readFileSync(target, 'utf8') });
  });
  await page.goto('http://grille.test/');
  const result = await page.evaluate(async ({ raw, reference }) => {
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const { renderDesignerAddDialog } = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const { handleDesignerAreaAction } = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const { bindProductParameterDiagrams, renderProductParameterDiagram } = await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
    const { tubeDesignerCss } = await import('/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
    const display = raw.display;
    const template = { ...raw, display, available: true, name: catalogText(raw.displayName),
      groups: raw.groups.map(g => ({ ...g, displayName: catalogText(g.displayName) })),
      parameters: raw.parameters.map(p => ({ ...p, groupKey: p.group, type: { enum: 'select', string: 'text' }[p.valueType] || p.valueType,
        displayName: catalogText(p.displayName), options: p.choices?.map(c => ({ ...c, label: catalogText(c.displayName) })) })) };
    const defaults = Object.fromEntries(raw.parameters.map(f => [f.key, f.defaultValue]));
    const view = { scene: { tubeDesigner: { templates: [template] } }, tubeDesignerAddTemplateId: raw.id,
      tubeDesignerAddDialogOpen: true, tubeDesignerAddDraft: { ...defaults, width:1800 } };
    document.body.innerHTML = `<style>*{box-sizing:border-box}body{margin:0;font-family:Arial,'Microsoft YaHei'}${tubeDesignerCss}</style><div class="tube-designer-workspace"></div>`;
    const mount = document.querySelector('.tube-designer-workspace'), context = { mount };
    let fullRenders = 0;
    const ops = { renderProject() { fullRenders++; mount.innerHTML = renderDesignerAddDialog(view.scene.tubeDesigner, view); bindProductParameterDiagrams(mount); } };
    ops.renderProject();
    const initialRenders = fullRenders;
    const left = mount.querySelector('.tube-designer-template-pane'), right = mount.querySelector('.tube-designer-config-parameters');
    // Supply enough genuine scroll range on both independent panes.
    left.style.height = '300px'; left.style.overflow = 'auto';
    const spacer = document.createElement('div'); spacer.style.height = '700px'; left.append(spacer);
    right.style.height = '290px'; right.style.overflow = 'auto';
    left.scrollTop = 45; right.scrollTop = 60;
    if (right.querySelectorAll('[data-tube-designer-parameter]').length !== 5 || right.querySelector('input[data-tube-designer-parameter]'))
      throw new Error('Creation must expose only the five style choices');
    const untouched = mount.querySelector('[data-tube-designer-parameter="layoutStyle"]');
    let lastAction;
    mount.addEventListener('change', event => {
      lastAction = handleDesignerAreaAction(context, view, event.target.dataset.camChangeAction, event.target, ops);
    });
    const changes = [];
    for (const [key, value] of [['frameType', 'closed_frame'], ['handleSide', 'right'], ['handleClosure', 'open_slot'],
      ['handleSide', 'center_double'], ['handleEnabled', 'no'], ['handleEnabled', 'yes'], ['frameType', 'bottom_rail'],
      ['layoutStyle', 'five_face'], ['handleSide', 'left'], ['handleSide', 'center_double'],
      ['layoutStyle','balcony_closure'],['supportStyle','double'],['supportStyle','single'],
      ['layoutStyle', 'planar']]) {
      const input = mount.querySelector(`[data-tube-designer-parameter="${key}"]`);
      input.focus({ preventScroll: true }); input.value = value;
      const leftScroll = left.scrollTop, rightScroll = right.scrollTop;
      input.dispatchEvent(new Event('change', { bubbles: true })); await lastAction;
      if (view.tubeDesignerAddDraft[key] !== value) throw new Error(`Draft did not update: ${key}`);
      const current = mount.querySelector(`[data-tube-designer-parameter="${key}"]`);
      if (input !== current || document.activeElement !== current) throw new Error(`Lost editor/focus: ${key}`);
      const expectedRight = Math.min(rightScroll, right.scrollHeight - right.clientHeight);
      if (left.scrollTop !== leftScroll || Math.abs(right.scrollTop - expectedRight) > 1)
        throw new Error(`Lost scroll: ${key} ${rightScroll} -> ${right.scrollTop}, expected ${expectedRight}`);
      const svg = mount.querySelector('[data-minimal-grille-diagram]');
      if (!svg || svg.querySelectorAll('[data-grille-stock]').length === 0) throw new Error(`Missing live stocks after ${key}=${value}: ${svg?.textContent}`);
      const fiveFace = view.tubeDesignerAddDraft.layoutStyle === 'five_face';
      const spatial = view.tubeDesignerAddDraft.layoutStyle !== 'planar';
      if (Boolean(svg.querySelector('[data-grille-stock="frame.top.0001"]')) !== (spatial || view.tubeDesignerAddDraft.frameType === 'closed_frame'))
        throw new Error('Frame artwork did not follow the selected style');
      if (svg.getAttribute('data-grille-layout') !== view.tubeDesignerAddDraft.layoutStyle)
        throw new Error('Artwork did not switch the spatial layout');
      if (Boolean(right.querySelector('[data-tube-designer-parameter="frameType"]')) === spatial)
        throw new Error('Five-face frame choice must be hidden');
      if (fiveFace && svg.querySelectorAll('[data-grille-face]').length !== 5)
        throw new Error('Five-face artwork must identify all five faces');
      if(view.tubeDesignerAddDraft.layoutStyle==='balcony_closure'&&svg.querySelectorAll('[data-grille-face]').length!==3)
        throw new Error('Balcony artwork must show only the front, top and bottom');
      if(spatial&&!svg.getAttribute('aria-label').includes(view.tubeDesignerAddDraft.supportStyle==='double'?'双竖杆':'单竖杆'))
        throw new Error('Support style label did not follow the creation choice');
      const expectedClearances = view.tubeDesignerAddDraft.handleEnabled === 'no' ? 0 : view.tubeDesignerAddDraft.handleSide === 'center_double' ? 2 : 1;
      if (svg.querySelectorAll('.minimal-grille-clearance').length !== expectedClearances)
        throw new Error('Handle artwork did not follow the selected style');
      changes.push({ key, value, label: svg.getAttribute('aria-label'), members: svg.querySelectorAll('[data-grille-stock]').length });
    }
    if (fullRenders !== initialRenders) throw new Error('Whole dialog was replaced');
    if (untouched !== mount.querySelector('[data-tube-designer-parameter="layoutStyle"]')) throw new Error('Unrelated input replaced');
    // Keep a focused input/selection through another local preview patch.
    const textProbe = document.createElement('input'); textProbe.value = 'selection-probe'; left.append(textProbe);
    textProbe.focus({ preventScroll: true }); textProbe.setSelectionRange(0, 2);
    const other = mount.querySelector('[data-tube-designer-parameter="handleSide"]'); other.value = 'left';
    other.dispatchEvent(new Event('change', { bubbles: true })); await lastAction;
    if (document.activeElement !== textProbe || textProbe.selectionStart !== 0 || textProbe.selectionEnd !== 2)
      throw new Error('Unrelated text selection was lost');
    const referenceMount = document.createElement('div'); referenceMount.style.width = '420px';
    referenceMount.innerHTML = renderProductParameterDiagram(reference,
      Object.fromEntries(reference.parameters.map(f => [f.key, f.defaultValue])), { mode: 'add', compact: true, hideDimensionCards: true });
    document.body.append(referenceMount);
    const previewHeight = mount.querySelector('.tube-designer-product-diagram-art').getBoundingClientRect().height;
    const referenceHeight = referenceMount.querySelector('.tube-designer-product-diagram-art').getBoundingClientRect().height;
    referenceMount.remove();
    if (previewHeight !== 96 || referenceHeight !== previewHeight) throw new Error('Preview size differs from security window');
    // Remove scroll/selection probes after the interaction assertions so the
    // saved documentation shows only the production dialog.
    spacer.remove(); textProbe.remove();
    for (const pane of [left, right]) { pane.style.removeProperty('height'); pane.style.removeProperty('overflow'); pane.scrollTop = 0; }
    return { changes, fullRenders, previewHeight, referenceHeight, passed: true };
  }, { raw: { ...descriptor, display: JSON.parse(readFileSync(resolve(sourceRoot, 'apps/tube-designer/templates/product/minimal_protective_grille/display.json'))) },
    reference: JSON.parse(readFileSync(resolve(sourceRoot, 'apps/tube-designer/templates/product/single_face_security_window/template.json'))) });
  await page.screenshot({ path: resolve(evidence, 'add-live-diagram.png') });
  await page.locator('[data-tube-designer-parameter="layoutStyle"]').selectOption('five_face');
  await page.waitForFunction(() => document.querySelector('[data-minimal-grille-diagram]')?.getAttribute('data-grille-layout') === 'five_face'
    && document.querySelectorAll('[data-minimal-grille-diagram] [data-grille-face]').length === 5);
  await page.screenshot({ path: resolve(evidence, 'add-five-face-live-diagram.png') });
  await page.locator('[data-tube-designer-parameter="layoutStyle"]').selectOption('balcony_closure');
  await page.locator('[data-tube-designer-parameter="supportStyle"]').selectOption('double');
  await page.waitForFunction(() => document.querySelector('[data-minimal-grille-diagram]')?.getAttribute('data-grille-layout') === 'balcony_closure');
  await page.screenshot({ path: resolve(evidence, 'add-balcony-live-diagram.png') });
  const gallery = await page.evaluate(async raw => {
    const { renderMinimalGrilleArtwork } = await import('/src/apps/tube-designer/webpage/minimalGrilleDiagram.mjs');
    const defaults = Object.fromEntries(raw.parameters.map(f => [f.key, f.defaultValue]));
    const samples = [['five_face','五面飘网 · 方管骨架与扁钢内排'],['balcony_closure','阳台上下收口 · 方管横骨架']].map(([layoutStyle,label])=>({label,
      values:{...defaults,layoutStyle,width:1800,projectionDepth:300,topDrop:30,balconyTopDepth:300,balconyBottomDepth:150,
        handleEnabled:'no',installHoleEnabled:'no',installHoleCountPerSide:2,supportStyle:'double',supportPairClearGap:60,
        supportLinkCount:0,supportDepth:5}}));
    document.body.innerHTML = '<style>body{margin:0;padding:24px;background:#f2f6f7;font-family:Arial,"Microsoft YaHei"}main{display:grid;grid-template-columns:repeat(2,1fr);gap:16px}article{background:white;padding:16px;border:1px solid #b7ced4}h2{font-size:16px;color:#244450}p{font-size:13px;color:#4c6770}svg{width:100%;height:290px;background:#172e38}h1{font-size:21px;color:#244450}</style><h1>极简防护网 · 飘面骨架独立用料</h1><main>' + samples.map(sample => `<article><h2>${sample.label}</h2>${renderMinimalGrilleArtwork(raw, raw.extensions.productDiagram, sample.values)}<p>正面 ${sample.values.width} × ${sample.values.height} mm<br>顶部进深 300 mm · 底部进深 ${sample.values.layoutStyle==='five_face'?300:150} mm<br>A/B：10×5 扁钢 · C：20×${sample.values.supportDepth} 扁钢<br>${sample.values.layoutStyle==='five_face'?'飘面竖骨架：15×15 方管 · ':''}飘面横骨架：12×12 方管<br>普通横杆中心距 ${sample.values.barCenterSpacing} mm（可修改）<br>此图双杆净距 ${sample.values.supportPairClearGap} mm（产品默认 20 mm）<br>普通横杆在双竖杆两侧分别端接</p></article>`).join('') + '</main>';
    return samples.map(s => ({ label:s.label,layoutStyle:s.values.layoutStyle,width:s.values.width,height:s.values.height,barLayoutMode:s.values.barLayoutMode,barCenterSpacing:s.values.barCenterSpacing,projectionVerticalWidth:s.values.projectionVerticalWidth,projectionHorizontalWidth:s.values.projectionHorizontalWidth,balconyTopDepth:s.values.balconyTopDepth,balconyBottomDepth:s.values.balconyBottomDepth,supportStyle:s.values.supportStyle,supportPairClearGap:s.values.supportPairClearGap,supportLinkCount:s.values.supportLinkCount }));
  }, descriptor);
  await page.screenshot({ path: resolve(evidence, 'mixed-projection-material-diagrams.png'), fullPage: true });
  writeFileSync(resolve(evidence, 'preview-browser-report.json'), JSON.stringify({ ...result, geometryOracleCases: cases.length,
    spatialGeometryOracleCases: spatialCases.length,
    productionRendererSampleGallery: gallery,
    nativeBusinessMocked: true, actualBrowserEvents: true, standaloneNative: false }, null, 2));
  console.log(JSON.stringify({ passed: true, actualGeometryOracleCases: cases.length,
    spatialGeometryOracleCases: spatialCases.length, changes: result.changes.length }));
} finally { await browser.close(); }
