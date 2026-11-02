import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  actionLicenseFeature, allowLicenseFeature, checkPageLicense, commandLicenseFeature, ensureLicenseStatus,
  handleLicenseCommand, hasLicenseFeature, renderLicenseStatus,
  isLicenseWorkspaceReadOnly,
} from "../../apps/tube-designer/webpage/licensing.mjs";
import { renderAboutRightPane } from "../../apps/tube-designer/webpage/aboutArea.mjs";
import { getRibbonDefinition, ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";
import { installDialogPathMemory } from "../../iCAX-UI/SDK/Bridge/dialogPathMemory.mjs";

const features = JSON.parse(readFileSync(new URL('../../licensing/features.json', import.meta.url))).features;
const status = ids => ({ activated: true, kind: "永久授权", licenseId: "example", featureSchemaVersion: 1,
  featureCatalog: features.map(({ id, label, parent }) => ({ id, label, parent })),
  capabilities: Object.fromEntries(features.map(item => [item.id, ids.includes(item.id) && (!item.parent || ids.includes(item.parent))])) });
const calls = [];
const fileDialogs = [];
let selectedAuthorization = 'D:\\客户资料\\激活.tdact';
let nativeStatus = status(['page.product', 'product.design']);
const logs = [];
const context = { actions: { log: (level, message) => logs.push({ level, message }) }, productProxy: { productChannelId: 'license-product', invoke: async (name, payload) => {
  calls.push([name, payload]);
  if (name === 'TubeDesignerLicensing.CheckAccess') {
    if (!nativeStatus.capabilities[payload.featureId]) throw Error('Native grant rejected');
    return { granted: true, featureId: payload.featureId };
  }
  return structuredClone(nativeStatus);
}, bridge: { openDirectoryDialog: async () => 'D:\\授权', openFileDialog: async options => {
  fileDialogs.push(options); return selectedAuthorization;
} } } };
const dialogDirectories = new Map();
installDialogPathMemory(context.productProxy.bridge, { memory: {
  read: key => dialogDirectories.get(key) ?? '',
  write: (key, field, directory) => dialogDirectories.set(key, directory),
} });
const view = { pending: false }, otherView = { pending: false };
const ops = { renderProject: () => { throw Error('Licensing must not reconstruct the workbench'); } };

await Promise.all([ensureLicenseStatus(context, view), ensureLicenseStatus(context, otherView)]);
assert.equal(calls.filter(([name]) => name === 'TubeDesignerLicensing.Status').length, 1);
assert.equal(hasLicenseFeature(context, view, 'page.product'), true);
assert.equal(hasLicenseFeature(context, view, 'page.nesting'), false);
assert.equal(hasLicenseFeature(context, view, 'product.export'), false);
assert.equal(isLicenseWorkspaceReadOnly(context, view), false);
assert.equal(await checkPageLicense({ ...context, licenseView: view }, 'nesting'), true);
assert.equal(await checkPageLicense({ ...context, licenseView: view }, 'view'), true);
assert.equal(await checkPageLicense({ ...context, licenseView: view }, 'about'), true);
assert.equal(calls.filter(([name]) => name === 'TubeDesignerLicensing.CheckAccess').length, 0);
assert.equal(allowLicenseFeature(context, view, 'product.breakdown'), false);
assert.equal(logs.at(-1).level, 'error');
assert.match(logs.at(-1).message, /产品拆单未授权/);
assert.equal(view.tubeDesignerLicenseNotice, undefined);

const ribbon = getRibbonDefinition({ licenseContext: context, licenseView: view });
assert.deepEqual(ribbon.tabs.map(tab => tab.id), ['view', 'resources', 'about']);
assert.equal(ribbon.tabs.find(tab => tab.id === 'view').authorizationRequired, false);
assert.equal(ribbon.tabs.find(tab => tab.id === 'about').authorizationRequired, false);
const commands = ribbon.tabs.flatMap(tab => tab.groups.flatMap(group => group.commands));
assert.equal(commands.find(c => c.id === 'designer.add').authorizationRequired, false);
assert.equal(commands.find(c => c.id === 'designer.disassemble').authorizationRequired, true);
assert.match(commands.find(c => c.id === 'designer.disassemble').unavailableReason, /产品拆单未授权/);
assert.ok(commands.every(c => !c.id.startsWith('nesting.') && !c.id.startsWith('machining.')));
assert.ok(commands.filter(c => c.id.startsWith('licensing.')).every(c => !c.authorizationRequired));
assert.equal(commandLicenseFeature('nesting.start'), 'nesting.calculate');
assert.equal(commandLicenseFeature('machining.export'), 'machining.export');
assert.equal(commandLicenseFeature('simulation.run'), 'simulation.run');
assert.equal(actionLicenseFeature('tube-designer-disassemble-active-product'), 'product.breakdown');
assert.equal(actionLicenseFeature('tube-designer-nesting-export-selected', 'nesting'), 'nesting.export');
assert.equal(actionLicenseFeature('tube-designer-punch-add', 'nesting'), 'nesting.edit');

assert.equal(await handleLicenseCommand(context, view, 'other', ops), false);
await handleLicenseCommand(context, view, 'licensing.request', ops);
assert.equal(calls.at(-1)[0], 'TubeDesignerLicensing.Request');
assert.match(calls.at(-1)[1].path, /^D:\\授权\\TubeDesigner-.*\.tdreq$/);
await handleLicenseCommand(context, view, 'licensing.request-trial', ops);
assert.equal(calls.at(-1)[0], 'TubeDesignerLicensing.RequestTrial');
nativeStatus = status(['page.nesting', 'nesting.edit']);
await handleLicenseCommand(context, view, 'licensing.activate', ops);
assert.equal(calls.at(-1)[0], 'TubeDesignerLicensing.Activate');
assert.deepEqual(calls.at(-1)[1], { path: selectedAuthorization });
assert.deepEqual(fileDialogs.at(-1).filters[0].extensions, ['tdact']);
assert.equal(fileDialogs.at(-1).defaultPath, undefined, 'first authorization chooser has no default filename');
assert.equal(fileDialogs.at(-1).initialDirectory, undefined, 'first authorization chooser has no remembered directory');
selectedAuthorization = 'D:\\升级.tdact';
nativeStatus = status(['page.nesting', 'nesting.edit', 'nesting.calculate']);
await handleLicenseCommand(context, view, 'licensing.activate', ops);
assert.deepEqual(calls.at(-1), ['TubeDesignerLicensing.Activate', { path: selectedAuthorization }]);
assert.equal(fileDialogs.at(-1).initialDirectory, 'D:\\客户资料', 'authorization import receives previous directory separately');
assert.equal(fileDialogs.at(-1).defaultPath, undefined, 'authorization directory cannot become an invalid filename');
assert.equal(hasLicenseFeature(context, otherView, 'nesting.edit'), true);
assert.equal(hasLicenseFeature(context, otherView, 'nesting.calculate'), true);
const nestingRibbon = getRibbonDefinition({ licenseContext: context, licenseView: otherView });
assert.deepEqual(nestingRibbon.tabs.map(tab => tab.id), ['view', 'resources', 'about'], 'license grants do not expose unreleased pages');
nativeStatus = status(['page.nesting', 'nesting.edit']);
await handleLicenseCommand(context, view, 'licensing.status', ops);
assert.equal(view.tubeDesignerLicenseBusy, false);
assert.equal(view.pending, false);
assert.equal(hasLicenseFeature(context, otherView, 'page.product'), false);
assert.equal(hasLicenseFeature(context, otherView, 'nesting.edit'), true);
assert.equal(hasLicenseFeature(context, otherView, 'nesting.calculate'), false);
assert.match(renderAboutRightPane(context, view), /软件授权/);
assert.doesNotMatch(renderLicenseStatus(view), /data-license-feature|下料零件编辑/);
assert.ok(!renderLicenseStatus({ tubeDesignerLicense: { message: '<script>alert(1)</script>' } }).includes('<script>'));
assert.doesNotMatch(renderLicenseStatus({ tubeDesignerLicense: { releaseBypass: true } }), /免授权版本/);
assert.match(renderLicenseStatus({ tubeDesignerLicense: { developmentBypass: true } }), /开发免授权/);

context.productProxy.invoke = async () => { throw Error('TPM error'); };
await handleLicenseCommand(context, view, 'licensing.status', ops);
assert.equal(view.error, 'TPM error');
assert.equal(view.tubeDesignerLicense.activated, false);
assert.equal(hasLicenseFeature(context, view, 'nesting.edit'), false);
assert.match(view.tubeDesignerLicenseNotice, /未放宽授权校验/);
await handleLicenseCommand(context, view, 'licensing.activate', ops);
assert.equal(view.tubeDesignerLicense.activated, false);
assert.doesNotMatch(renderLicenseStatus(view), />已激活</);

let unexpectedCalls = 0;
context.productProxy.invoke = async () => { unexpectedCalls++; throw Error('Unexpected native call'); };
context.productProxy.bridge.openDirectoryDialog = async () => '';
await handleLicenseCommand(context, view, 'licensing.request', ops);
context.productProxy.bridge.openFileDialog = async () => '';
await handleLicenseCommand(context, view, 'licensing.activate', ops);
assert.equal(unexpectedCalls, 0);
assert.equal(view.tubeDesignerLicenseBusy, false);

// A channel reconnect cannot retain a previous machine/product grant.
context.productProxy.productChannelId = 'replacement-product';
view.tubeDesignerLicense = status(features.map(item => item.id));
assert.equal(hasLicenseFeature(context, view, 'page.product'), false);
const unknownProtocol = { productProxy: { invoke: async () => ({ activated: true, features: 8191, releaseBypass: true }) } };
const rejected = await ensureLicenseStatus(unknownProtocol, {});
assert.deepEqual(rejected.capabilities, {});
assert.match(rejected.message, /功能权限/);

const startupLogs = [];
const unlicensedView = {};
const unlicensedContext = {
  actions: { log: (level, message) => startupLogs.push({ level, message }) },
  productProxy: { invoke: async () => ({ ...status([]), activated: false, message: 'Cannot access license file' }) },
};
await ensureLicenseStatus(unlicensedContext, unlicensedView);
await ensureLicenseStatus(unlicensedContext, unlicensedView, { refresh: true });
assert.equal(isLicenseWorkspaceReadOnly(unlicensedContext, unlicensedView), true);
assert.equal(startupLogs.filter(item => /软件未授权/.test(item.message)).length, 1);
const readOnlyRibbon = getRibbonDefinition({ licenseContext: unlicensedContext, licenseView: unlicensedView });
assert.ok(readOnlyRibbon.tabs.every(tab => !tab.authorizationRequired));
assert.ok(readOnlyRibbon.tabs.flatMap(tab => tab.groups.flatMap(group => group.commands))
  .every(command => command.authorizationRequired === !command.id.startsWith('licensing.')));

assert.deepEqual(ribbonDefinition.tabs.find(tab => tab.id === 'about').groups.flatMap(group => group.commands).map(c => c.id), [
  'licensing.request', 'licensing.request-trial', 'licensing.activate', 'licensing.export-license', 'licensing.repair-environment',
]);
// Repair is available without activation and must never publish grants.
let finishRepair, repairCalls = 0;
const repairView = { tubeDesignerLicense: { activated: false, capabilities: {} } };
const repairContext = { productProxy: { invoke: async (method, payload) => {
  if (method === 'TubeDesignerLicensing.Status') return {activated:false, featureSchemaVersion:1, capabilities:{}};
  assert.equal(method, 'TubeDesignerLicensing.RepairEnvironment'); assert.deepEqual(payload, {});
  repairCalls++; return new Promise(resolve => { finishRepair = resolve; });
} } };
await ensureLicenseStatus(repairContext, repairView);
const pendingRepair = handleLicenseCommand(repairContext, repairView, 'licensing.repair-environment', ops);
assert.equal(repairView.tubeDesignerLicenseBusy, true);
await handleLicenseCommand(repairContext, repairView, 'licensing.repair-environment', ops);
assert.equal(repairCalls, 1);
finishRepair({requestReady:true, code:'repaired', message:'证书已获取', activated:true, capabilities:{'page.product':true}});
await pendingRepair;
assert.equal(repairView.tubeDesignerLicense.activated, false);
assert.deepEqual(repairView.tubeDesignerLicense.capabilities, {});
assert.equal(repairView.tubeDesignerLicenseBusy, false);
assert.equal(repairView.tubeDesignerLicenseNotice, '证书已获取');
repairContext.productProxy.invoke = async () => ({requestReady:false, code:'network_timeout', message:'连接超时'});
await handleLicenseCommand(repairContext, repairView, 'licensing.repair-environment', ops);
assert.equal(repairView.error, '连接超时');
repairContext.productProxy.invoke = async () => ({activated:true});
await handleLicenseCommand(repairContext, repairView, 'licensing.repair-environment', ops);
assert.match(repairView.error, /有效结果/);
assert.equal(repairView.tubeDesignerLicense.activated, false);
console.log('TubeDesigner licensing UI checks passed');
