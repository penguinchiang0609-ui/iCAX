// Production renderer/actions/DOM patch in a real browser. The host update
// responses and analytic profile snapshots are controlled test fixtures.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';

const output = browserReportDirectory('minimal-grille-balcony-joint-ui-20261110/browser');
const folder = 'apps/tube-designer/templates/product/minimal_protective_grille';
const descriptor = { ...JSON.parse(readBrowserAsset(`${folder}/template.json`)),
  display: JSON.parse(readBrowserAsset(`${folder}/display.json`)) };
const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
const report = { passed: false, nativeBusinessMocked: true, standaloneNative: false,
  scope: 'Actual browser events and production DOM patch; controlled host updates and analytic section snapshots',
  viewportCoverage: 'Canvas identity sentinel; real production WebGL refresh is separately covered by MinimalGrilleMounting.browser.mjs', cases: [] };
let page;
try {
  page = await browser.newPage({ viewport: { width: 1500, height: 1040 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://insert-joint.test/**', serveBrowserAsset);
  await page.goto('http://insert-joint.test/');
  await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Arial,'Microsoft YaHei'}
    .cam-workbench{display:grid;grid-template-columns:260px 1fr 450px;gap:8px;height:1000px}
    .cam-context-pane,.cam-info-pane{height:300px;overflow:auto}.cam-viewport{position:relative;background:#152b34}
    .nested-left{height:160px;overflow:auto}.nested-left>div{min-height:1700px}
    .cam-context-pane:after,.cam-info-pane:after{content:'';display:block;height:1700px}
    .tube-designer-parameter-scroll{height:210px!important;overflow:auto!important}
    .tube-designer-parameter-scroll:after{content:'';display:block;height:1700px}
    #left-note,#right-note{width:170px}.cube{position:absolute;left:10px;top:10px}` });
  const initialParameters = { ...defaults, layoutStyle: 'balcony_closure', jointMode: 'male_female_half_hole', maleHeadWidth: 6,
    frameProfileType: 'rect', innerProfileType: 'rect', supportProfileType: 'rect', supportDepth: 10, supportStyle: 'single',
    maleHeadLength: 3.5, frameWidth: 40, frameDepth: 30, frameWallThickness: 1.5,
    innerWidth: 20, innerDepth: 10, innerWallThickness: 1.2 };
  await installProductInteractionFixture(page, descriptor, null, { initialParameters });
  await page.evaluate(async () => {
    const f = window.productFixture;
    const module = await import('/src/apps/tube-designer/webpage/insertJointParameterDetails.mjs');
    const { matchesParameterCondition } = await import('/src/apps/tube-designer/webpage/parameterConditions.mjs');
    f.jointModule = module; f.operations = [];
    f.mount.addEventListener('change', event => {
      const target = event.target.closest?.('[data-cam-change-action]');
      if (target) f.operations.push(f.actions.handleDesignerAreaAction(f.context, f.view,
        target.dataset.camChangeAction, target, f.ops));
    });
    f.jointState = () => {
      const values = f.view.scene.tubeDesigner.product.parameters;
      const physicalBefore=JSON.stringify(values);
      const result = {};
      for (const declaration of f.template.extensions.parameterGroupDetails) {
        if (!matchesParameterCondition(declaration.visibleWhen, values)) continue;
        const section = f.mount.querySelector(`[data-insert-joint-part="${declaration.part}"]`);
        const model = module.insertJointParameterDetailModel(declaration, values, { template: f.template });
        if (result[declaration.part]?.model.active && !model.active) continue;
        result[declaration.part] = { model, count: f.mount.querySelectorAll(`[data-insert-joint-part="${declaration.part}"]`).length,
          svgCount: section?.querySelectorAll('svg[data-insert-joint-artwork]').length ?? 0,
          readouts: Object.fromEntries([...(section?.querySelectorAll('[data-insert-joint-readout]') ?? [])]
            .map(node => [node.dataset.insertJointReadout, node.querySelector('dd')?.textContent ?? node.textContent])),
          artwork: section?.querySelector('svg[data-insert-joint-artwork]')?.innerHTML ?? '',
          tipArtwork: section?.querySelector('svg[data-insert-joint-artwork="male-tip"]')?.innerHTML ?? '',
          text: section?.textContent ?? '' };
      }
      if(JSON.stringify(values)!==physicalBefore)throw new Error('Local dimension rendering mutated physical parameters/profile snapshots');
      return result;
    };
    f.assertJointEditors = () => {
      const values = f.view.scene.tubeDesigner.product.parameters;
      const modes = [values.jointMode, ...(['five_face', 'balcony_closure'].includes(values.layoutStyle) ? [values.returnJointMode] : [])];
      const active = modes.some(mode => mode !== 'butt_weld');
      const full = values.maleEndForm === 'full_section';
      const half = modes.includes('male_female_half_hole');
      const state = f.jointState();
      for (const part of ['male','female']) {
        if (state[part].count !== (active ? 1 : 0)) throw new Error(`Expected one ${part} local detail for ${values.jointMode}/${values.maleEndForm}`);
        if (active && !state[part].svgCount) throw new Error(`Missing ${part} real SVG artwork`);
        if (!active && state[part].model.active) throw new Error('Butt weld retains active insert model');
      }
      if (Boolean(f.ordinary('maleHeadLength')) !== half) throw new Error('Single-wall length did not follow both applicable connection settings');
      if (Boolean(f.ordinary('maleHeadWidth')) !== (active && !full)) throw new Error('Full pipe form still asks for tongue width');
      if (Boolean(f.ordinary('holeTotalClearanceHeight')) !== (active && !full)) throw new Error('Tongue allowance escaped its form condition');
      if (Boolean(f.ordinary('fullHeadTotalClearance')) !== (active && full)) throw new Error('Full head allowance condition failed');
      f.assertPhysicalCommits();
      return state;
    };
  });
  const initial = await page.evaluate(() => window.productFixture.assertJointEditors());
  assert.equal(initial.male.model.profiles.inner.width, 20);
  assert.equal(initial.male.model.profiles.inner.depth, 10);
  assert.equal(initial.male.model.length.editable, true);
  assert.equal(initial.male.model.length.value, 3.5);
  assert(initial.male.text.includes('公头') && initial.female.text.includes('母'));
  report.cases.push({ name: 'initial-separated-head-and-socket', passed: true });

  async function change(key, value, index) {
    const selector = key.startsWith('profile:')
      ? `[data-tube-designer-profile-prefix="${key.slice(8)}"][data-cam-change-action="tube-designer-profile-selection-change"]`
      : `[data-tube-designer-parameter="${key}"]`;
    const editor = page.locator(selector);
    await editor.waitFor({ state: 'visible' });
    await editor.evaluate(node => {
      const f=window.productFixture;
      f.editedInput=node; const audit={calls:0}; f.editedInputAudit=audit;
      node.addEventListener('insert-edited-input-audit',()=>audit.calls++);
    });
    if (await editor.evaluate(node => node.tagName === 'SELECT')) await editor.selectOption(String(value));
    else { await editor.fill(String(value)); await editor.press('Tab'); }
    await page.waitForFunction(() => window.productFixture.pending.length === 1);
    return page.evaluate(async ({ key, value, side }) => {
      const f = window.productFixture;
      await f.completeHeldChange(Promise.all(f.operations), side);
      if (!key.startsWith('profile:') && f.view.scene.tubeDesigner.product.parameters[key] !== value)
        throw new Error(`Actual ${key} input was not committed`);
      const state = f.assertJointEditors();
      const retained=key.startsWith('profile:')
        ? f.mount.querySelector(`[data-tube-designer-profile-prefix="${key.slice(8)}"][data-cam-change-action="tube-designer-profile-selection-change"]`)
        : f.ordinary(key);
      if(retained!==f.editedInput)throw new Error(`Response replaced the actual edited input ${key}`);
      retained.dispatchEvent(new Event('insert-edited-input-audit'));
      if(f.editedInputAudit.calls!==1)throw new Error(`Response discarded edited-input listeners for ${key}`);
      return { key, value, state, latestTypingSelectionScrollCanvasFieldsAndListenersPreserved: true };
    }, { key, value, side: index % 2 ? 'left' : 'right' });
  }

  let last = initial;
  const operations = [['maleHeadWidth',8], ['holeTotalClearanceHeight',0.35], ['holeTotalClearanceDepth',0.25],
    ['maleHeadLength',4.25], ['maleCornerType','round'], ['maleCornerSize',0.6],
    ['maleTabCount','four'], ['maleTabAngle',27], ['maleTabAngle',0], ['maleTabCount','two']];
  for (const [index,[key,value]] of operations.entries()) {
    const result = await change(key, value, index);
    if (['maleHeadWidth','holeTotalClearanceHeight','holeTotalClearanceDepth'].includes(key))
      assert.notEqual(result.state.female.artwork, last.female.artwork, `${key}: socket drawing did not update`);
    if (key === 'maleTabCount') assert.equal(result.state.male.model.direction.count, value === 'four' ? 4 : 2);
    if (key === 'maleTabAngle') assert.equal(result.state.male.model.direction.angle, value);
    if(key==='maleTabAngle' && value===27) {
      assert.equal(result.state.female.model.singleHole.available,false);
      assert.equal(result.state.female.readouts.holeWidth,undefined,'Slanted/curved holes must not be presented as a flat rectangular hole');
    }
    if (key === 'maleHeadLength') assert.equal(result.state.male.model.length.value, value);
    if (key === 'holeTotalClearanceHeight') assert.equal(result.state.female.model.readouts.find(row=>row.key==='bandWidth')?.value,8.35);
    if (key === 'holeTotalClearanceDepth') {
      assert.equal(result.state.female.model.singleHole.available,true);
      assert.match(result.state.female.readouts.holeWidth ?? '',/8\.35/);
      assert.match(result.state.female.readouts.holeThickness ?? '',/1\.45/);
    }
    if(['maleHeadWidth','maleHeadLength','maleCornerType','maleCornerSize'].includes(key))
      assert.notEqual(result.state.male.tipArtwork,last.male.tipArtwork,`${key}: enlarged male tip did not update`);
    last = result.state;
    report.cases.push({ key,value,latestTypingSelectionScrollCanvasFieldsAndListenersPreserved:true,passed:true });
  }

  // A genuine click on a production SVG dimension must open its group and
  // focus the corresponding real input; field focus highlights that same SVG.
  const dimension = page.locator('[data-insert-joint-part="male"] [data-cam-action="tube-designer-focus-product-parameter"][data-tube-designer-parameter-key="maleHeadWidth"] text').first();
  await dimension.click();
  await page.waitForFunction(() => document.activeElement === window.productFixture.ordinary('maleHeadWidth'));
  const click = await page.evaluate(() => {
    const f=window.productFixture;
    if (!f.diagramActions.some(action => action.key === 'maleHeadWidth')) throw new Error('SVG click bypassed actual production focus action');
    if (!f.mount.querySelector('[data-insert-joint-part="male"] [data-product-diagram-parameter~="maleHeadWidth"].is-active'))
      throw new Error('Field focus did not highlight matching SVG dimension');
    return { focusedParameter:'maleHeadWidth', actualAction:true, highlighted:true };
  });
  report.cases.push({ name:'dimension-click-and-focus-highlight', ...click, passed:true });

  // Customer profile choices are real select events, using deliberately
  // controlled hollow-circle/rectangle snapshots instead of a mocked success.
  await page.evaluate(() => {
    const f=window.productFixture;
    const shape=(id,kind,width,depth,wall)=>({schema:'icax.imported-tube-profile',schemaVersion:1,
      id,profileDefinitionId:id,profileScope:'system',libraryScope:'system',profileForm:'fixed',
      kind:'fixed-section',sectionKind:kind,name:`浏览器截面 ${id}`,width,depth,wallThickness:wall,
      contourCount:2,hollow:true,contours:kind==='round'
        ? [{kind:'circle',center:[0,0],radius:width/2},{kind:'circle',center:[0,0],radius:width/2-wall}]
        : [{kind:'roundedRectangle',center:[0,0],width,height:depth,radius:2},
          {kind:'roundedRectangle',center:[0,0],width:width-2*wall,height:depth-2*wall,radius:Math.max(0,2-wall)}]});
    f.view.tubeDesignerSystemProfiles=[shape('rect','rect',20,10,1.2),shape('round','round',24,24,1.2),shape('joint-round-40','round',40,40,1.5),
      shape('joint-round-24','round',24,24,1.2),shape('joint-rect-24','rect',24,14,1.2)]
      .map(profile=>({...profile,previewProfile:structuredClone(profile)}));
    f.render();
  });
  let index=operations.length;
  for (const [role,id,kind,width] of [['frame','joint-round-40','round',40],['inner','joint-round-24','round',24],
    ['frame','joint-rect-24','rect',24],['inner','joint-rect-24','rect',24],
    ['frame','joint-round-40','round',40],['inner','joint-round-24','round',24]]) {
    const result=await change(`profile:${role}`,`system:${id}`,index++);
    const profile=result.state.male.model.profiles[role];
    assert.equal(profile.width,width,`Actual ${role} snapshot width not used`);
    assert.equal(profile.contours[0].kind,kind==='round'?'circle':'roundedRectangle');
    report.cases.push({name:'actual-profile-selection',role,id,kind,width,
      latestTypingSelectionScrollCanvasFieldsAndListenersPreserved:true,passed:true});
  }
  last=(await change('jointMode','male_female_through_hole',index++)).state;
  assert.equal(last.male.model.length.editable,false);
  assert.equal(last.male.model.length.kind,'curved-flush');
  assert.equal(Number.isFinite(last.male.model.length.value),false,'A curved flush end must not claim one constant insertion length');
  assert.match(last.male.text,/齐平|曲面/);
  const hiddenLength=await page.evaluate(()=>window.productFixture.view.scene.tubeDesigner.product.parameters.maleHeadLength);
  assert.equal(hiddenLength,4.25);
  last=(await change('maleEndForm','full_section',index++)).state;
  assert.equal(last.male.model.form,'full_section');
  assert.equal(last.male.count,1,'Full/through lost its automatic head detail when all numeric head inputs became inactive');
  last=(await change('fullHeadTotalClearance',0.4,index++)).state;
  assert.equal(last.female.model.readouts.find(row=>row.key==='fullClearance')?.value,0.4);
  assert(Math.abs(last.female.model.sections.female.contours[0].radius-12.2)<1e-9,'Full round socket must offset the actual pipe radius by half the total allowance');
  report.cases.push({name:'round-receiver-through-full-head',hiddenSingleWallLengthPreserved:true,passed:true});

  async function captureDetail(filename) {
    // Expand the fixture pane only for legible evidence; interaction checks ran
    // against scrollable panes and retained production nodes above.
    await page.addStyleTag({content:'.cam-info-pane{height:1000px!important}.tube-designer-parameter-scroll{height:940px!important}'});
    await page.evaluate(()=>{const f=window.productFixture;f.mount.querySelectorAll('details').forEach(d=>d.open=true);
      f.mount.querySelector('[data-insert-joint-part="male"]')?.scrollIntoView({block:'start'});});
    await page.locator('.cam-info-pane').screenshot({path:resolve(output,filename)});
    for(const part of ['male','female']) await page.locator(`[data-insert-joint-part="${part}"]`).screenshot({
      path:resolve(output,filename.replace(/\.png$/,`-${part}.png`))});
  }
  await captureDetail('round-through-full-head.png');
  // Restore scroll geometry before testing conditional disappearance and
  // later edits, so async retention assertions still have real overflow.
  await page.addStyleTag({content:'.cam-info-pane{height:300px!important}.tube-designer-parameter-scroll{height:210px!important}'});
  await change('jointMode','butt_weld',index++);
  await change('jointMode','male_female_half_hole',index++);
  await change('maleEndForm','tongue',index++);
  for(const [role,id] of [['frame','joint-rect-24'],['inner','joint-rect-24']]) await change(`profile:${role}`,`system:${id}`,index++);
  await captureDetail('rectangular-tongue-and-socket.png');
  await page.addStyleTag({content:'.cam-info-pane{height:300px!important}.tube-designer-parameter-scroll{height:210px!important}'});
  await change('jointMode','butt_weld',index++);
  last=(await change('returnJointMode','male_female_half_hole',index++)).state;
  assert.equal(last.male.model.active,true);
  assert.equal(last.male.model.length.receiverEnvelope,14,'Return joint must use the receiver depth');
  last=(await change('returnJointMode','male_female_through_hole',index++)).state;
  assert.equal(last.male.model.through,true);
  assert.equal(last.male.model.length.value,14);
  await change('returnJointMode','butt_weld',index++);
  report.cases.push({name:'independent-return-joint-and-depth-axis',latestTypingSelectionScrollCanvasFieldsAndListenersPreserved:true,passed:true});
  assert.deepEqual(errors,[]);
  const final=await page.evaluate(()=>{const f=window.productFixture;f.assertPhysicalCommits();return{requests:f.requests.length,physicalKeys:f.template.parameters.length,stats:f.stats(),notices:f.notices};});
  assert.equal(final.physicalKeys,79);
  assert.equal(final.requests,index);
  Object.assign(report,{passed:true,actualBrowserEvents:true,controlledProfileSnapshots:true,
    retainedInputAndCanvasNodes:true,retainedListeners:true,latestFocusSelectionAndNestedScroll:true,
    activeProductPhysicalParameterCount:79,layoutStyle:initialParameters.layoutStyle,commits:final.requests,pageErrors:errors});
  console.log(`PASS insert joint browser: ${final.requests} actual edits/profile selections, 2 local diagrams, all 79 declared inputs, async focus/selection/scroll/nodes/listeners retained`);
} catch(error) {
  report.error=error.stack;
  if(page) report.diagnostics=await page.evaluate(()=>{const f=window.productFixture;return f?{
    error:f.view.error,pending:f.pending.length,notices:f.notices,requests:f.requests.length,
    lastRequest:f.requests.at(-1),parameters:f.view.scene?.tubeDesigner?.product?.parameters,
    profileChoices:[...f.mount.querySelectorAll('[data-cam-change-action="tube-designer-profile-selection-change"]')].map(node=>({role:node.dataset.tubeDesignerProfilePrefix,value:node.value,options:[...node.options].map(o=>o.value)}))}:null;}).catch(()=>null);
  if(page) await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});
  throw error;
} finally {
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));
  await browser.close();
}
