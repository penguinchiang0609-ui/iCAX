// Actual retained template declarations and production UI. Native transport is
// deliberately held; CAD, persistence and template removal are verified apart.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';

const variants = [
  ['minimal_protective_grille', 'minimal-protective-grille', true],
  ['straight_steel_staircase', 'straight-steel-staircase', true],
  ['assembly_frame_lt', 'assembly-frame-lt', false],
  ['assembly_cross_fixture', 'assembly-cross-fixture', false],
  ['assembly_orthogonal_corner', 'assembly-orthogonal-corner', false],
];
const output = browserReportDirectory('remaining-product-format/browser');
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
let browser;
const results = [];
let currentTemplate = null, stage = 'startup';
const save = (status, error) => writeFileSync(resolve(output, 'interaction-evidence.json'), JSON.stringify({
  status, scope: 'Actual production retained templates and UI with controlled protocol fixture',
  standaloneCefEndToEnd: false, browserEngine: 'Microsoft Edge via Playwright', variants: results,
  finishedAt: new Date().toISOString(), ...(error ? { failure: { templateId: currentTemplate, stage, message: error.message } } : {}),
}, null, 2));

async function exercise(page) {
  return page.evaluate(async () => {
    const f = window.productFixture;
    const controls = await import('/src/apps/tube-designer/webpage/productControls.mjs');
    const { parameterVisible } = await import('/src/apps/tube-designer/webpage/parameterConditions.mjs');
    const values = () => f.view.scene.tubeDesigner.product.parameters;
    const transitions = [], omittedInactive = [];
    const definitions = controls.productControlFields(f.template);
    const editors = new Set(controls.productStructureEditors(f.template).map(editor => editor.controlKey));
    const apply = async (key, choiceValue, side = 'right') => {
      const expected = controls.applyProductControlChoice(f.view, f.template, key, values(), choiceValue);
      const current = controls.productControlValue(f.template, key, values());
      if (current === choiceValue) return;
      if (editors.has(key)) await f.chooseStructure(key, choiceValue, side);
      else await f.change(f.mount.querySelector(`select[data-product-control-key="${key}"][data-product-control-mode="right"]`), choiceValue, side);
      f.same(values(), expected, `Actual UI choice did not preserve host input ${key}/${choiceValue}`);
      transitions.push({ control: key, choice: choiceValue, parameters: f.requests.at(-1).parameters });
    };
    const activate = async field => {
      if (parameterVisible(field, values())) return true;
      // Find a real declared parent choice, then make that choice through UI.
      // The production conditions and patches remain the sole interpreter.
      for (const parent of definitions) {
        if (parent.key === field.key || !parameterVisible(parent, values())) continue;
        for (const choice of controls.productControlChoices(f.template, parent.key, values()).filter(choice => !choice.disabled)) {
          const candidate = controls.applyProductControlChoice(f.view, f.template, parent.key, values(), choice.value);
          if (!parameterVisible(field, candidate)) continue;
          await apply(parent.key, choice.value, 'left');
          return true;
        }
      }
      return false;
    };
    for (const field of definitions) {
      if (!await activate(field)) { omittedInactive.push(field.key); continue; }
      const choices = controls.productControlChoices(f.template, field.key, values()).filter(choice => !choice.disabled);
      const original = controls.productControlValue(f.template, field.key, values());
      const sequence = [...choices.filter(choice => choice.value !== original), ...choices.filter(choice => choice.value === original)];
      for (const choice of sequence) await apply(field.key, choice.value, transitions.length % 2 ? 'left' : 'right');
    }
    if (omittedInactive.length) throw new Error(`Declared current structure controls were never reachable: ${omittedInactive.join(', ')}`);

    const permanent = f.ordinary(f.permanentKey);
    const numberDefinition = f.template.parameters.find(field => ['number', 'integer'].includes(field.valueType)
      && !field.visibleWhen && !field.readOnly && f.ordinary(field.key) && !f.ordinary(field.key).disabled);
    if (!numberDefinition) throw new Error('Actual right inspector lacks an unconditional numeric editor');
    const current = Number(values()[numberDefinition.key]);
    const increment = numberDefinition.constraints?.step ?? 1;
    const next = current + increment <= (numberDefinition.constraints?.maximum ?? Infinity) ? current + increment : current - increment;
    await f.change(f.ordinary(numberDefinition.key), next, 'left');
    await f.change(f.ordinary(numberDefinition.key), current, 'right');

    // Type in the actual product code while another physical request is held.
    const code = f.ordinary('productCode');
    if (!code || !['text', 'search', ''].includes(code.type)) throw new Error('Current template has no actual product code text input');
    const node = code, hostCode = values().productCode;
    let events = 0; code.addEventListener('remaining-text-audit', () => events++);
    const target = f.ordinary(numberDefinition.key); target.value = String(next);
    const operation = f.actions.handleDesignerAreaAction(f.context, f.view, target.dataset.camChangeAction, target, f.ops);
    await f.waitForPending();
    code.focus({ preventScroll: true }); code.value = 'current pending code draft'; code.setSelectionRange(1, 4);
    f.setScrolls([80, 110, 180, 150]); code.setSelectionRange(2, 8, 'backward'); f.setScrolls([140, 180, 280, 210]);
    f.pending.shift()(); await operation;
    if (f.ordinary('productCode') !== node || document.activeElement !== node || node.value !== 'current pending code draft'
      || node.selectionStart !== 2 || node.selectionEnd !== 8 || node.selectionDirection !== 'backward') throw new Error('Actual code input/selection was overwritten by response');
    f.same(f.scrolls(), [140, 180, 280, 210], 'Actual text draft lost latest four sidebar scrolls');
    if (values().productCode !== hostCode) throw new Error('Uncommitted actual text input was silently persisted');
    node.dispatchEvent(new Event('remaining-text-audit')); if (events !== 1) throw new Error('Actual text input listener was replaced');
    node.value = 'later code draft'; node.setSelectionRange(1, 5, 'forward'); f.setScrolls([150, 190, 290, 220]);
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    if (node.value !== 'later code draft' || node.selectionStart !== 1 || node.selectionEnd !== 5) throw new Error('Deferred restore overwrote later actual input');
    f.same(f.scrolls(), [150, 190, 290, 220], 'Deferred restore overwrote later sidebar scrolls'); node.value = hostCode;

    const conditionalDrafts = [];
    for (const [key, on, off, child] of f.template.id === 'minimal-protective-grille'
      ? [['grilleHandleStructure', 'with', 'without', 'handleHeight']]
      : f.template.id === 'straight-steel-staircase'
        ? [['stairsGuardStructure', 'both', 'none', 'railingHeight']] : []) {
      const definition = definitions.find(field => field.key === key);
      if (!definition) throw new Error(`Actual parent structure declaration is missing ${key}`);
      await apply(key, on, 'left');
      const childInput = f.ordinary(child), before = values()[child];
      if (!childInput) throw new Error(`Actual conditional field ${child} did not appear`);
      const childDefinition = f.template.parameters.find(field => field.key === child);
      const draft = Number(before) + (childDefinition.constraints?.step ?? 1);
      await f.change(childInput, draft, 'right');
      const expected = controls.applyProductControlChoice(f.view, f.template, key, values(), off);
      await f.openStructure(key);
      const selector = f.choice(key); selector.value = off;
      await f.actions.handleDesignerAreaAction(f.context, f.view, selector.dataset.camChangeAction, selector, f.ops);
      const confirm = f.mount.querySelector('[data-cam-action="tube-designer-confirm-product-structure"]');
      const departing = f.ordinary(child);
      if (!departing) throw new Error('Editing a local parent draft prematurely removed its physical child');
      departing.focus({ preventScroll: true }); f.setScrolls([80, 110, 180, 150]);
      const removal = f.actions.handleDesignerAreaAction(f.context, f.view, confirm.dataset.camAction, confirm, f.ops);
      await f.waitForPending();
      // The real structure action may optimistically hide its physical child.
      // Continue scrolling after dispatch; neither refresh may focus another field.
      if (!departing.isConnected && document.activeElement?.matches('[data-tube-designer-parameter], [data-product-control-key]')) throw new Error('Optimistic conditional removal transferred focus to another parameter');
      f.setScrolls([160, 200, 300, 230]); f.pending.shift()(); await removal;
      f.same(values(), expected, 'Removing a conditional editor changed its physical input');
      transitions.push({ control: key, choice: off, parameters: f.requests.at(-1).parameters });
      if (f.ordinary(child) || values()[child] !== draft) throw new Error('Inactive conditional draft was lost or retained its editor');
      if (document.activeElement?.matches('[data-tube-designer-parameter], [data-product-control-key]')) throw new Error('Removed conditional editor transferred focus to another parameter');
      f.same(f.scrolls(), [160, 200, 300, 230], 'Conditional removal lost the latest four sidebar scrolls');
      await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      if (document.activeElement?.matches('[data-tube-designer-parameter], [data-product-control-key]')) throw new Error('Deferred restoration focused a different conditional field');
      f.same(f.scrolls(), [160, 200, 300, 230], 'Deferred conditional restoration overwrote later scrolls');
      await apply(key, on, 'right');
      if (f.ordinary(child)?.value !== String(draft)) throw new Error('Conditional draft did not return to its actual editor');
      conditionalDrafts.push({ parent: key, child, retained: draft, removedEditorDidNotTransferFocus: true, latestFourScrollsPreserved: true });
    }
    if (f.ordinary(f.permanentKey) !== permanent) throw new Error('Persistent actual input node was replaced');
    f.assertPhysicalCommits();
    return { ...f.stats(), transitions, conditionalDrafts, actualTextInput: 'productCode',
      latestFourScrollsVerified: true, leftAndRightTypingSelectionVerified: true,
      actualTextSelectionPreserved: true, actualTextNotSilentlyCommitted: true,
      permanentNodesAndListenersPreserved: true, laterInputNotOverwritten: true,
      syntheticControlsNotPersisted: true, controlledHostFixture: true };
  });
}

async function exerciseAdd(page) {
  return page.evaluate(async () => {
    const f = window.productFixture;
    f.view.tubeDesignerAddDialogOpen = true; f.view.tubeDesignerAddTemplateId = f.template.id;
    f.view.tubeDesignerAddDraft = Object.fromEntries(f.template.parameters.map(field => [field.key, field.defaultValue]));
    f.render();
    const form = f.mount.querySelector('[data-tube-designer-add-form]');
    if (!form) throw new Error('Actual current add form absent');
    const headings = [...form.querySelectorAll('[data-tube-designer-parameter-group] > summary')].map(node => node.textContent.trim());
    const original = structuredClone(f.view.tubeDesignerAddDraft);
    const button = f.mount.querySelector('[data-cam-action="tube-designer-confirm-add"]');
    if (!button || button.disabled) throw new Error('Actual current add request is not available');
    await f.actions.handleDesignerAreaAction(f.context, f.view, button.dataset.camAction, button, f.ops);
    if (f.creations.length !== 1) throw new Error('Expected one actual add request');
    const payload = f.creations[0];
    for (const field of f.template.parameters) if (JSON.stringify(payload[field.key]) !== JSON.stringify(original[field.key])) throw new Error(`Add payload changed host field ${field.key}`);
    for (const control of f.template.extensions?.productControls ?? []) if (Object.hasOwn(payload, control.key)) throw new Error('Add request included a synthetic UI control');
    return { actualAddGroups: headings, requests: 1, unchangedPhysicalPayload: true, nativeGeometryVerified: false };
  });
}

async function captureActualUI(page, id) {
  // Crop actual production panes; omit the test shell and scroll sentinels.
  const style = await page.addStyleTag({ content: '.cam-info-pane{height:780px!important}.tube-designer-parameter-scroll{height:700px!important}.cam-info-pane:after,.tube-designer-parameter-scroll:after{display:none!important}' });
  await page.evaluate(() => {
    const f = window.productFixture; f.setScrolls([0, 0, 0, 0]);
    for (const [key, open] of f.defaultDisclosure) {
      const node = f.mount.querySelector(`[data-tube-designer-parameter-group="${key}"]`); if (node) node.open = open;
    }
  });
  const defaultRightPane = resolve(output, `${id}.right-default.png`);
  await page.locator('.cam-info-pane > [data-tube-designer-product-parameter-scope]').screenshot({ path: defaultRightPane });
  await style.evaluate(node => node.remove());
  const editor = await page.evaluate(async () => {
    const f = window.productFixture; f.mount.querySelectorAll('details').forEach(node => { node.open = true; });
    const { productStructureEditors } = await import('/src/apps/tube-designer/webpage/productControls.mjs');
    return productStructureEditors(f.template)[0]?.controlKey;
  });
  if (!editor) return { defaultRightPane, fixedTopology: true };
  await page.evaluate(async key => window.productFixture.openStructure(key), editor);
  const structureModal = resolve(output, `${id}.structure-dialog.png`);
  await page.locator('[data-tube-designer-structure-dialog]').screenshot({ path: structureModal });
  await page.evaluate(async () => {
    const f = window.productFixture, cancel = f.mount.querySelector('[data-cam-action="tube-designer-cancel-product-structure"]');
    await f.actions.handleDesignerAreaAction(f.context, f.view, cancel.dataset.camAction, cancel, f.ops);
  });
  return { defaultRightPane, structureModal };
}

try {
  for (const [directory, id, listed] of variants) {
    currentTemplate = id; stage = 'load actual descriptor';
    const raw = { ...JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${directory}/template.json`)),
      display: JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${directory}/display.json`)) };
    // Isolate each template's real browser lifecycle as well as its DOM fixture.
    browser = await chromium.launch({ headless: true, channel: 'msedge' });
    const page = await browser.newPage({ viewport: { width: 1400, height: 850 } });
    const errors = []; page.on('pageerror', error => errors.push(error.message));
    await page.route('http://remaining-products.test/**', serveBrowserAsset);
    await page.goto('http://remaining-products.test/');
    await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0}.cam-workbench{display:flex;gap:10px}.cam-context-pane,.cam-info-pane{height:250px;overflow:auto;width:500px}.nested-left{height:150px;overflow:auto}.nested-left>div{height:1600px}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1600px}.tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:1600px}.cam-viewport{width:250px}` });
    try {
      stage = 'install actual UI fixture'; const installed = await installProductInteractionFixture(page, raw);
      stage = 'display component groups';
      const declaration = await page.evaluate(() => {
        const f = window.productFixture;
        const groups = Object.keys(f.template.display.views.right.sectionGroups ?? {});
        if (!groups.length) throw new Error('Current product has no independent component display groups');
        const actual = [...f.mount.querySelectorAll('[data-tube-designer-parameter-group]')].map(node => node.dataset.tubeDesignerParameterGroup);
        if (!actual.some(key => key.includes(':section-group:'))) throw new Error('Actual UI ignored component display declarations');
        return { componentGroups: groups, actualDefaultGroups: actual, listed: f.template.extensions?.catalog?.listed !== false };
      });
      assert.equal(declaration.listed, listed);
      stage = 'actual default right pane and structure modal'; const screenshots = await captureActualUI(page, id);
      stage = 'physical controls and asynchronous editing'; const interactions = await exercise(page);
      let add = { notListedForCreation: true };
      if (listed) { await installProductInteractionFixture(page, raw); stage = 'actual add request'; add = await exerciseAdd(page); }
      assert.deepEqual(errors, []);
      results.push({ ...installed, ...declaration, ...screenshots, ...interactions, add, status: 'passed' }); save('running');
    } finally { await page.close(); await browser.close(); browser = null; }
  }
  save('passed'); console.log(`PASS ${results.length} current retained product template UI fixtures and physical input contracts`);
} catch (error) { save('failed', error); throw error; }
finally { if (browser) await browser.close(); }
