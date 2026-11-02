// Real standalone CEF/native acceptance. Only file/directory picker paths are supplied.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const output = resolve(process.env.ICAX_CUSTOMER_JOINT_EVIDENCE || 'output/tests/customer-joints-merge-20261023/verified');
mkdirSync(output, { recursive: true });
const browser = await chromium.connectOverCDP('http://127.0.0.1:19425');
const page = browser.contexts()[0].pages().find(p => p.url().includes('/AppShell/index.html'));
page.setDefaultTimeout(120000); await page.setViewportSize({width:1600,height:1000});
await page.waitForFunction(() => window.__icaxWorkbench?.getState().projectId, null, { timeout:120000 });
const remaining = process.argv.includes('--remaining');
const report = remaining ? JSON.parse(readFileSync(join(output,'acceptance-report.json'),'utf8'))
  : { standaloneNative: true, nativeBusinessMocked: false, viewportMocked: false, pickerPathsControlled: true, cases: [] };
if (remaining) { report.automationCorrections = [report.error, 'Use explicit arc-length-axial drawing coordinates for the real hole control.']; delete report.error; }
const init = () => page.evaluate(async () => {
  const { AppProxy } = await import('../../AppProxy/AppProxy.mjs');
  const app = await AppProxy.create(window.icax ?? window.icaxNativeBridge); await app.getState();
  const product = app.getProduct('icax.tube-designer'); await product.getState();
  const project = product.getProject(window.__icaxWorkbench.getState().projectId);
  window.__customerNative = { app, product, project, scene: project.getMainScene() };
});
const invoke = (method, payload = {}) => page.evaluate(({ method, payload }) => window.__customerNative.scene.invoke('TubeDesigner.' + method, payload), { method, payload });
const list = async () => (await invoke('List')).tubeDesigner;
const walk = dir => readdirSync(dir).flatMap(n => { const p = join(dir, n); return statSync(p).isDirectory() ? walk(p) : [p]; });
async function showSaved(path, id) {
  await page.reload();
  await page.evaluate(path => { (window.icax ?? window.icaxNativeBridge).openFileDialog = async () => path; }, path);
  await page.locator('[data-action="open-start-center"]').click();
  await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(id => window.__icaxWorkbench.getTubeDesignerState().product?.entityId === id, id);
  await init();
}
async function createCase(name, templateId, parameters, expectedTypes, expectedQuantity) {
  // AppShell creates a new untitled native project on reload. Isolate every
  // fixture so autosave never changes a previously delivered saved project.
  await page.reload();
  await page.waitForFunction(() => window.__icaxWorkbench?.getState().projectId);
  await init();
  assert.equal((await list()).instances.length, 0);
  const preview = await invoke('GenerateProductTemplatePreview', { templateId, parameters });
  const normalized = preview.parameters;
  const created = (await invoke('GeneratePreview', { ...normalized, templateId })).tubeDesigner;
  assert.deepEqual(created.product.parameters, normalized);
  const id = created.product.entityId;
  const disassembled = (await invoke('DisassembleSelected', { productEntityIds: [id] })).tubeDesigner;
  const parts = disassembled.parts;
  assert.equal(disassembled.members.length, expectedQuantity ?? disassembled.members.length);
  assert(parts.length > 0);
  if (expectedTypes) assert.equal(parts.length, expectedTypes);
  const quantity = parts.reduce((n, p) => n + p.quantity, 0);
  if (expectedQuantity) assert.equal(quantity, expectedQuantity);
  for (const part of parts) {
    assert.equal(part.properties['tubeDesigner.manufacturingValidation']?.status, 'verified');
    assert.equal(part.properties['manufacturing.sourceStockIds'].length, part.unitQuantity);
  }
  const repeated = (await invoke('DisassembleSelected', { productEntityIds: [id] })).tubeDesigner;
  assert.equal(repeated.parts.length, parts.length);
  assert.equal(repeated.parts.reduce((n, p) => n + p.quantity, 0), quantity);
  const path = join(output, name + '.ictd');
  assert.equal((await page.evaluate(path => window.__customerNative.project.save(path), path)).saved, true);
  await showSaved(path, id);
  const target = join(output, name + '-export'); mkdirSync(target, { recursive: true });
  await page.evaluate(target => { (window.icax ?? window.icaxNativeBridge).openDirectoryDialog = async () => target; }, target);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  await page.waitForFunction(target => document.body.innerText.includes(target), target);
  const actual = walk(target);
  assert.equal(actual.filter(p => p.endsWith('.step')).length, parts.length);
  assert(actual.some(p => p.endsWith('零件清单.xlsx')));
  const repeatedExport=await invoke('ExportSelected',{partEntityIds:parts.map(p=>p.entityId),targetDirectory:target});
  assert.equal(repeatedExport.exportedCount,parts.length);
  assert.equal(repeatedExport.draftOnly,false);
  assert.equal(walk(target).filter(p=>p.endsWith('.step')).length,parts.length);
  assert.equal((await list()).members.length, disassembled.members.length);
  await page.screenshot({ path: join(output, name + '.png') });
  const result = { name, path, target, members: disassembled.members.length, types: parts.length, physicalQuantity: quantity,
    model: disassembled, reopened: await list(), files: actual, repeated: true, passed: true };
  report.cases.push(result);
  writeFileSync(join(output, name + '-report.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify({ name, types: parts.length, quantity, passed: true }));
  return result;
}
try {
  if (!remaining) {
  const windowCase = await createCase('防盗窗-同件合并', 'single-face-security-window', {}, 14, 41);
  for (const length of [870, 808, 988, 1050, 1200, 1800])
    assert(windowCase.model.parts.some(p => Math.abs(p.length-length) < .001 && p.unitQuantity === 2));
  const id = windowCase.model.product.entityId;
  const multiplied = (await invoke('SetInstanceQuantity', { productEntityId: id, quantity: 3 })).tubeDesigner;
  assert.equal(multiplied.parts.length, 14);
  assert.equal(multiplied.parts.reduce((n, p) => n+p.quantity, 0), 123);
  const quantityTarget = join(output, '防盗窗-生产3套-export'); mkdirSync(quantityTarget, { recursive: true });
  report.quantityExport = await invoke('ExportSelected', { partEntityIds: multiplied.parts.map(p => p.entityId), targetDirectory: quantityTarget });
  assert.equal(report.quantityExport.exportedCount, 14);
  assert.equal(report.quantityExport.draftOnly, false);
  report.quantityCheck = { types: 14, quantity: 123, passed: true };
  await invoke('SetInstanceQuantity', { productEntityId:id, quantity:1 });
  await page.evaluate(path=>window.__customerNative.project.save(path),windowCase.path);

  const equal = { accessDoorEnabled: false, frameWidth:25, frameDepth:25, horizontalWidth:25, horizontalDepth:25,
    verticalWidth:16, verticalDepth:16, frameWallThickness:1.2, horizontalWallThickness:1.2,
    verticalWallThickness:1, frameManufacturingMode:'segment_weld', verticalEndConnection:'weld' };
  await createCase('同规格25-贴焊', 'single-face-security-window', { ...equal, horizontalEndConnection:'weld' });
  await createCase('同规格25-公母插接', 'single-face-security-window', { ...equal, horizontalEndConnection:'tabs' });
  for (const faceType of ['two', 'three', 'five'])
    await createCase('同规格25-' + faceType + '-贴焊', 'single-face-security-window', { ...equal, faceType, horizontalEndConnection:'weld' });

  await page.reload(); await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId); await init();
  const bad = (await invoke('GeneratePreview', { ...equal, templateId:'single-face-security-window', horizontalEndConnection:'insert', horizontalBranchReserve:5 })).tubeDesigner.product;
  try { await invoke('DisassembleSelected', { productEntityIds:[bad.entityId] }); assert.fail('equal full-section insertion must not fit into its own hollow cavity'); }
  catch (e) { assert.match(e.message, /插|内|尺寸|放入|适用|穿/); report.equalInsertionError = e.message; }
  await invoke('DeleteProduct', { productEntityId:bad.entityId });

  await createCase('方管护栏-同件合并', 'modular-guardrail', { handrailWidth:50, handrailDepth:50 }, 4, 32);
  } else {
    const previous=report.cases.find(x=>x.name==='方管护栏-同件合并');
    await showSaved(previous.path,previous.model.product.entityId);
  }
  // The hole control runs in its own project via TubeDesignerCustomerDifferentHole.native.cef.mjs.
  const round={}; for (const [prefix,size] of [['handrail',50],['post',40],['rail',30],['infill',20]]) {
    round[prefix+'ProfileType']='round'; round[prefix+'Width']=size;
  }
  await createCase('圆管护栏-相贯与合并','modular-guardrail',round,4,32);
  report.passed=true;
} catch (error) { report.error=error.stack;process.exitCode=1;await page.screenshot({path:join(output,'failure.png')}).catch(()=>{}); }
finally { writeFileSync(join(output,'acceptance-report.json'),JSON.stringify(report,null,2)); console.log(JSON.stringify({passed:report.passed,error:report.error})); await browser.close(); }
