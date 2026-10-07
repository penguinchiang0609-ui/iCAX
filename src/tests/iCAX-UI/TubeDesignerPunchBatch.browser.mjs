// Actual batch dialog, DOM patcher, controls and WebGL renderer. With the
// selected scope bridge, every preview, resource and final part is real native.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, readdirSync, existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve, delimiter } from 'node:path';
import { browserAssetRoot, browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const output = browserReportDirectory('punch-batch-20261007/browser');
const nativeRuntime = process.env.ICAX_PUNCH_BATCH_NATIVE_RUNTIME;
const nativeBridge = process.env.ICAX_PUNCH_BATCH_NATIVE_BRIDGE;
const profileAdvancedOnly = process.env.ICAX_PUNCH_BATCH_FOCUS === 'profile-advanced';
const profileToolbarOnly = process.env.ICAX_PUNCH_BATCH_FOCUS === 'profile-toolbar';
const definitionLayoutOnly = process.env.ICAX_PUNCH_BATCH_FOCUS === 'definition-layout';
const rowAddApplyOnly = process.env.ICAX_PUNCH_BATCH_FOCUS === 'row-add-apply';
const branchApplyOnly = process.env.ICAX_PUNCH_BATCH_FOCUS === 'branch-profile-apply';
const rowAddResponseOnly = ['row-add-response','row-add-response-baseline'].includes(process.env.ICAX_PUNCH_BATCH_FOCUS)||rowAddApplyOnly;
const previewOnly = profileAdvancedOnly || profileToolbarOnly || definitionLayoutOnly || rowAddResponseOnly || branchApplyOnly;
assert.equal(Boolean(nativeRuntime), Boolean(nativeBridge), 'Select both the native runtime and resource-aware scope bridge');
let host, browser, page, serial = 0, nativeStderr = '';
const waiting = new Map(), nativeCalls = [], browserErrors = [];
function compactNativeRequest(request) {
  const p=request.payload??{};
  return {id:request.id,scope:request.scope,method:request.method,payload:{
    ...Object.fromEntries(Object.entries(p).filter(([key])=>!['features','parameters','drawing'].includes(key))),
    ...(p.parameters?{parameters:p.parameters}:{}),
    ...(p.features?{features:p.features.map(feature=>({id:feature.id,station:feature.station,face:feature.face,toolRef:feature.toolRef,
      toolParameters:feature.toolParameters,arrayTransforms:feature.arrayTransforms?.length??0}))}:{}),
  }};
}
function compactNativeResponse(response) {
  if(!response.ok)return {ok:false,error:response.error};
  const r=response.result??{},parts=r.tubeDesigner?.nestingGroups?.flatMap(group=>group.parts??[]);
  return {ok:true,result:{keys:Object.keys(r),...(r.base64?{url:r.url,version:r.version,bytes:r.bytes}:{}),
    ...(r.partEntityId?{partEntityId:r.partEntityId}:{}),...(r.tools?{tools:r.tools.map(t=>t.id)}:{}),
    ...(r.profile?{profile:{kind:r.profile.kind,name:r.profile.name,contourCount:r.profile.contours?.length}}:{}),
    ...(r.baseGeometry?{baseGeometry:r.baseGeometry,toolGeometry:r.toolGeometry,toolCount:r.toolCount}:{}),
    ...(parts?{parts:parts.map(part=>({entityId:part.entityId,name:part.name,length:part.length,quantity:part.quantity,
      thumbnailGeometryResourceId:part.thumbnailGeometryResourceId,manufacturingGeometryResourceId:part.manufacturingGeometryResourceId}))}:{}),
  }};
}
function rpc(scope, method, payload = {}) {
  const request = { id: ++serial, scope, method: method.replace(/^TubeDesigner\./, ''), payload };
  return new Promise((resolveResult, reject) => {
    const timer = setTimeout(() => { waiting.delete(request.id); reject(new Error('Native timeout: ' + request.method)); }, 120000);
    waiting.set(request.id, { timer, resolve: resolveResult, reject, request, started: Date.now() });
    host.stdin.write(JSON.stringify(request) + '\n');
  });
}
const definitions = readdirSync(resolve(browserAssetRoot, 'apps/tube-designer/templates/mold'), { withFileTypes: true })
  .filter(item => item.isDirectory() && existsSync(resolve(browserAssetRoot, 'apps/tube-designer/templates/mold', item.name, 'tool.json')))
  .map(item => {
    const descriptor = JSON.parse(readFileSync(resolve(browserAssetRoot, 'apps/tube-designer/templates/mold', item.name, 'tool.json'), 'utf8'));
    const display = resolve(browserAssetRoot, 'apps/tube-designer/templates/mold', item.name, 'display.json');
    if (existsSync(display)) descriptor.display = JSON.parse(readFileSync(display, 'utf8'));
    return { ...descriptor, digest: 'batch-browser-fixture', defaultParameters: Object.fromEntries(descriptor.parameters.map(p => [p.key, p.defaultValue])) };
  });
const profiles = ['round', 'rect'].map(id => {
  const descriptor = JSON.parse(readFileSync(resolve(browserAssetRoot, 'apps/tube-designer/templates/profile', id, 'profile.json'), 'utf8'));
  const display = resolve(browserAssetRoot, 'apps/tube-designer/templates/profile', id, 'display.json');
  if (existsSync(display)) descriptor.display = JSON.parse(readFileSync(display, 'utf8'));
  return { id, name: descriptor.displayName, descriptor, profileType: 'parametric-package',
    defaultParameters: Object.fromEntries(descriptor.parameters.map(p => [p.key, p.defaultValue])),
    previewProfile: { kind: id, name: descriptor.displayName, width: id === 'round' ? 40 : 60, depth: 40,
      diameter: 40, wallThickness: 2, contours: id === 'round' ? [{kind:'circle',radius:20},{kind:'circle',radius:18}]
        : [{kind:'path',closed:true,segments:[{kind:'line',start:[-30,-20],end:[30,-20]},
          {kind:'line',start:[30,-20],end:[30,20]},{kind:'line',start:[30,20],end:[-30,20]},{kind:'line',start:[-30,20],end:[-30,-20]}]}] } };
});
const report = { nativeBusinessResponsesMocked: !nativeRuntime, nativeRuntime, nativeBridge,
  scope: 'Production modal/control/DOM/WebGL modules in browser fixture. Background host is isolated; standalone CEF application was not controlled.',
  checks: [], screenshots: [] };
try {
  let initialScene = { tubeDesigner: { nestingGroups: [] } }, catalogue = definitions;
  if (nativeRuntime) {
    const userData = resolve(output, 'native-user-data'); mkdirSync(userData, { recursive: true });
    const env = { ...process.env, ICAX_AUTOMATION_USER_DATA: userData };
    const oldPath = Object.entries(env).find(([key]) => key.toLowerCase() === 'path')?.[1] ?? '';
    for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
    env.Path = resolve(nativeRuntime) + delimiter + oldPath;
    host = spawn(resolve(nativeBridge), [], { cwd: resolve(nativeRuntime), env, windowsHide: true, stdio: ['pipe','pipe','pipe'] });
    host.stderr.on('data', bytes => { nativeStderr += bytes; });
    host.on('error', error => { for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(error); } waiting.clear(); });
    createInterface({ input: host.stdout }).on('line', line => {
      let response; try { response = JSON.parse(line); } catch { nativeStderr += line + '\n'; return; }
      const job = waiting.get(response.id); if (!job) return;
      waiting.delete(response.id); clearTimeout(job.timer);
      nativeCalls.push({ request: compactNativeRequest(job.request), response: compactNativeResponse(response),
        milliseconds: Date.now() - job.started });
      response.ok ? job.resolve(response.result) : job.reject(new Error(response.error));
    });
    host.on('exit', code => { for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(new Error('Native bridge exited: ' + code)); } waiting.clear(); });
    await rpc('inspection', 'ResetScene');
    report.runtimeModules=(await rpc('inspection','GetRuntimeModules')).modules;
    if (!previewOnly) await assert.rejects(rpc('product', 'AddNestingPunchPart', {
      profileRef:{scope:'system',id:'round'},parameters:profiles[0].defaultParameters,
      length:1000,quantity:1,name:'错误通道不得创建',features:[],ends:{start:{type:'none'},end:{type:'none'}}
    }), /添加冲孔件请求无效/, 'Product channel must reject a valid small request without a scene');
    for (const profile of profiles) profile.previewProfile = (await rpc('product', 'EvaluateProfilePackage',
      { profileRef: { scope: 'system', id: profile.id }, parameters: profile.defaultParameters })).profile;
    catalogue = (await rpc('scene', 'GetPunchTools')).tools;
    initialScene = await rpc('scene', 'List');
    if (!previewOnly) report.checks.push('Native product-scope creation rejected before any parts exist');
  }
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } }); page.setDefaultTimeout(45000);
  page.on('pageerror', error => browserErrors.push(error.message));
  if (nativeRuntime) await page.exposeFunction('nativeBatchInvoke', rpc);
  await page.route('http://punch-batch.test/**', serveBrowserAsset);
  await page.goto('http://punch-batch.test/');
  const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
  await page.addStyleTag({ content: '*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}'
    + readFileSync(resolve(browserAssetRoot, 'apps/_shared/workbench/styles/laser3dcam.css'), 'utf8') + tubeDesignerCss });
  await page.evaluate(async ({ profiles, catalogue, initialScene, native }) => {
    const creation = await import('/src/apps/tube-designer/webpage/nestingPunchPart.mjs');
    const wizard = await import('/src/apps/tube-designer/webpage/punchWizard.mjs');
    const batch = await import('/src/apps/tube-designer/webpage/punchBatch.mjs');
    const editor = await import('/src/apps/tube-designer/webpage/punchEditor.mjs');
    const {bindToolParameterDiagrams} = await import('/src/apps/tube-designer/webpage/toolParameterDiagram.mjs');
    const { patchPunchDom, closePunchDom, rememberPunchDom } = await import('/src/apps/tube-designer/webpage/punchDomPatch.mjs');
    const { renderDesignerOperationOverlay } = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const { ThreeRenderViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const { encodeNestingGeometry, encodePreviewMaterial } = await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    document.body.innerHTML = '<main id="mount" class="tube-designer-workspace"><section id="background" style="height:100vh;background:#e9f0f2">'
      + '<header style="padding:12px 18px;background:#24363c;color:white">TubeDesigner　　保存</header>'
      + '<nav style="padding:12px 18px;background:white">产品　　<b>下料</b>　　资源库　　关于</nav>'
      + '<div style="padding:18px">从产品添加　　导入零件　　添加标准零件　　<button id="opener">添加冲孔件</button>　　三维绘制零件　　排样参数　　开始排样</div>'
      + '<div style="display:grid;grid-template-columns:240px 1fr 260px;height:75%"><aside style="padding:14px;background:white">零件清单</aside>'
      + '<div style="background:#13262d;color:#b1c4cc;display:grid;place-items:center">下料场景</div><aside style="padding:14px;background:white">当前选择</aside></div>'
      + '</section><div id="dialogs"></div></main>';
    const mount = document.querySelector('#mount'), dialogs = document.querySelector('#dialogs');
    const view = { pending: false, activeAreaId: 'nesting', scene: initialScene, tubeDesignerSystemProfiles: profiles,
      tubeDesignerSelectedProfileId: 'system:round', tubeDesignerUserData: { profiles: [] }, tubeDesignerTemplateProfiles: [] };
    const f = window.fixture = { view, wizard, batch, editor, creation, catalogue, profiles, pending: 0, calls: [], errors: [], patches: 0,
      background: document.querySelector('#background'), parts: [], modalClosures: 0 };
    const originalMount = ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount = function (...args) { f.viewport = this; return originalMount.apply(this, args); };
    const meshes = new Map(), material = encodePreviewMaterial(0x62a8b8ff), toolMaterial = encodePreviewMaterial(0xf0aa35ff);
    function box(length, width, depth, x) {
      const geometry = new THREE.BoxGeometry(length, width, depth).toNonIndexed(); geometry.translate(x, 0, 0);
      const bytes = encodeNestingGeometry({ positions: [...geometry.attributes.position.array],
        indices: Array.from({ length: geometry.attributes.position.count }, (_, index) => index) });
      geometry.dispose(); return bytes;
    }
    const context = f.context = { mount, project: { projectId: 'punch-batch-browser' }, sceneProxy: {
      async invoke(method, payload, options) {
        f.calls.push({ scope: 'scene', method, payload: structuredClone(payload) });
        if (method.endsWith('GetPunchTools')) return { tools: catalogue };
        if (method.endsWith('PreviewPunchWizard') && f.deferPreview) {
          f.deferPreview = false; await new Promise(resolve => { f.releasePreview = resolve; }); f.releasePreview = null;
        }
        if (native) {
          const started=performance.now(),result=await window.nativeBatchInvoke('scene', method, payload);
          if(method.endsWith('GenerateProfilePreview')&&f.holdActualProfileReplies){
            const receipt={parameters:structuredClone(payload.parameters),nativeMilliseconds:performance.now()-started,released:false};
            await new Promise(resolve=>{receipt.release=()=>{receipt.released=true;resolve();};(f.actualProfileHolds??=[]).push(receipt);});
          }
          if(method.endsWith('PreviewPunchWizard')) {
            const receipt={features:payload.features.length,toolCount:result.toolCount,nativeMilliseconds:performance.now()-started,
              completed:performance.now(),released:false};
            (f.actualPreviewResponses??=[]).push(receipt);
            if(f.holdActualPreviewReplies)await new Promise(resolve=>{
              receipt.release=()=>{receipt.released=true;resolve();};(f.actualPreviewHolds??=[]).push(receipt);
            });
          }
          return result;
        }
        if (method.endsWith('PreviewPunchWizard')) {
          const key = 'base-' + f.calls.length, tool = 'tool-' + f.calls.length, length = Number(payload.length);
          meshes.set(key, box(length, 40, 40, length / 2));
          meshes.set(tool, box(12, 52, 52, Number(payload.features[0]?.station ?? 100)));
          return { toolsOnly: true, length, baseGeometry: { url: key, version: 1 }, baseMaterial: { url: 'material', version: 1 },
            ...(payload.features.length ? { toolGeometry: { url: tool, version: 1 }, toolMaterial: { url: 'tool-material', version: 1 } } : {}),
            baseBounds: { min: [0,-20,-20], max: [length,20,20] }, toolCount: payload.features.length };
        }
        if (method.endsWith('AddNestingPunchPart')) {
          const partEntityId = 'created-' + (f.parts.length + 1); f.parts.push({ entityId: partEntityId, ...structuredClone(payload) });
          return { partEntityId, tubeDesigner: { nestingGroups: [{ parts: structuredClone(f.parts) }] } };
        }
        if (method.endsWith('List')) return { tubeDesigner: { nestingGroups: [{ parts: f.parts }] } };
        throw new Error('Unexpected scene method: ' + method);
      },
      resources: { async get(url, options) {
        if (f.deferResource) {
          f.deferResource = false; await new Promise(resolve => { f.releaseResource = resolve; }); f.releaseResource = null;
        }
        if (native) {
          const version = Number(options?.headers?.get('ICAX-Resource-Version') || 0);
          const result = await window.nativeBatchInvoke('inspection', 'ReadResource', { url, version });
          if(f.holdActualResourceReplies)await new Promise(resolve=>{
            const hold={url,version,bytes:result.bytes,released:false,release(){hold.released=true;resolve();}};
            (f.actualResourceHolds??=[]).push(hold);
          });
          return new Response(Uint8Array.from(atob(result.base64), character => character.charCodeAt(0)));
        }
        const bytes = url === 'material' ? material : url === 'tool-material' ? toolMaterial : meshes.get(url);
        if (!bytes) throw new Error('Unknown preview resource: ' + url);
        return new Response(bytes);
      } }
    }, productProxy: { async invoke(method, payload) {
      f.calls.push({ scope: 'product', method, payload: structuredClone(payload) });
      if (native) return window.nativeBatchInvoke('product', method, payload);
      if (method.endsWith('EvaluateProfilePackage')) return { profile: profiles.find(p => p.id === payload.profileRef.id).previewProfile };
      throw new Error('Unexpected product method: ' + method);
    } }, actions: { async refreshActiveSceneState() {} } };
    const ops = f.ops = { renderProject() {
      const html = creation.renderNestingPunchPartDialog(view) + renderDesignerOperationOverlay(context, view);
      if (closePunchDom(view,mount)) f.modalClosures++;
      else if (patchPunchDom(view, mount, html)) f.patches++; else dialogs.innerHTML = html;
      editor.attachPunchEditor(context, view, mount, ops);
      // Match the production entry's shared tool-diagram binding after local
      // modal patching; the controller already binds profile diagrams.
      bindToolParameterDiagrams(mount); rememberPunchDom(view, mount);
    }, showNotice() {}, appendProjectLog() {} };
    f.render = ops.renderProject;
    f.dispatch = async (name, target = {}) => {
      f.pending++; try { return await creation.handleNestingPunchPartAction(context, view,
        name.startsWith('tube-designer-') ? name : 'tube-designer-nesting-punch-create-' + name, target, ops); }
      catch (error) { f.errors.push(error.message); throw error; } finally { f.pending--; }
    };
    mount.addEventListener('change', event => {
      if (event.target.dataset.camChangeAction) void f.dispatch(event.target.dataset.camChangeAction, event.target);
    });
    mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target && !target.disabled) void f.dispatch(target.dataset.camAction, target);
    });
    const opener = document.querySelector('#opener'); opener.focus();
    await f.dispatch('open', opener);
  }, { profiles, catalogue, initialScene, native: Boolean(nativeRuntime) });
  const idle = async () => {
    await page.waitForFunction(() => {
      const f = window.fixture, s = f.view.tubeDesignerPunchWizard;
      return !f.pending && !f.view.pending && !s?.previewPending && !s?.previewRenderPending && !s?.uiPendingClick;
    });
    assert.deepEqual(await page.evaluate(() => window.fixture.errors), []);
    assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard?.previewRenderError ?? ''), '');
    assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerNestingPunchPartDraft?.diagramError ?? ''), '',
      'A failed native section evaluation must not be mistaken for a ready preview');
  };
  const ready = async () => { await idle(); await page.waitForFunction(() =>
    document.querySelector('[data-tube-designer-punch-viewport]')?.dataset.punchPreviewReady === 'true'); };
  const frame = () => page.evaluate(() => new Promise(resolveFrame => requestAnimationFrame(() => requestAnimationFrame(resolveFrame))));
  const camera = () => page.evaluate(()=>{const v=window.fixture.viewport;return {...v.getCameraState(),horizontalSpan:v.camera.right-v.camera.left};});
  const sameHorizontalView = (before,after,label) => {
    assert(Math.abs(before.horizontalSpan-after.horizontalSpan)<Math.max(1,before.horizontalSpan)*.002,label+' preserves horizontal span');
    assert.equal(after.theta,before.theta,label+' preserves camera orbit');assert.equal(after.phi,before.phi);
    assert.deepEqual(after.target,before.target,label+' preserves camera target');
  };
  const screenshot = async name => { const path = resolve(output, name + '.png'); await page.screenshot({ path }); report.screenshots.push(path); };
  const partInput = (index, field) => page.locator('[data-punch-batch-part-row]').nth(index).locator('[data-punch-batch-field="' + field + '"]');
  const click = async suffix => { await page.locator('[data-cam-action="tube-designer-nesting-punch-create-' + suffix + '"]').first().click(); await idle(); };
  await ready();
  if (previewOnly) {
    if(branchApplyOnly) {
      const {runPunchBranchApplyBrowserChecks}=await import('./punchBranchApplyBrowserChecks.mjs');
      await runPunchBranchApplyBrowserChecks(page,{idle,ready,screenshot,report});
    } else if (profileToolbarOnly) {
      const { runPunchProfileToolbarBrowserChecks } = await import('./punchProfileToolbarBrowserChecks.mjs');
      await runPunchProfileToolbarBrowserChecks(page, { idle, ready, screenshot, report });
      const { runPunchProfileAdvancedBrowserChecks } = await import('./punchProfileAdvancedBrowserChecks.mjs');
      await runPunchProfileAdvancedBrowserChecks(page, { idle, ready, screenshot, report });
    } else if (profileAdvancedOnly) {
      const { runPunchProfileAdvancedBrowserChecks } = await import('./punchProfileAdvancedBrowserChecks.mjs');
      await runPunchProfileAdvancedBrowserChecks(page, { idle, ready, screenshot, report });
    } else if(definitionLayoutOnly) {
      const { runPunchDefinitionLayoutBrowserChecks } = await import('./punchDefinitionLayoutBrowserChecks.mjs');
      await runPunchDefinitionLayoutBrowserChecks(page, { idle, ready, screenshot, report });
    } else {
      const { runPunchRowAddResponseBrowserChecks,runPunchRowAddApplyBrowserChecks } = await import('./punchRowAddResponseBrowserChecks.mjs');
      if(rowAddApplyOnly)await runPunchRowAddApplyBrowserChecks(page,{idle,ready,screenshot,report});
      else await runPunchRowAddResponseBrowserChecks(page,{idle,ready,screenshot,report,
          baseline:process.env.ICAX_PUNCH_BATCH_FOCUS==='row-add-response-baseline'});
    }
    if(nativeRuntime) {
      const snapshot=await rpc('scene','List');
      const parts=snapshot.tubeDesigner?.nestingGroups?.flatMap(group=>group.parts??[])??[];
      assert.equal(parts.length,rowAddApplyOnly||branchApplyOnly?1:0,
        'Focused verification has exactly its intended isolated native scene part count');
      if(rowAddApplyOnly||branchApplyOnly){
        assert.equal(Number(parts[0].quantity),1);
        assert(parts[0].manufacturingGeometryResourceId&&parts[0].thumbnailGeometryResourceId);
        report.savedParts=parts.map(part=>({entityId:part.entityId,name:part.name,quantity:part.quantity,length:part.length,
          manufacturingGeometryResourceId:part.manufacturingGeometryResourceId,thumbnailGeometryResourceId:part.thumbnailGeometryResourceId}));
        report.nativeApplyDuringDelayedPreview=true;
        if(branchApplyOnly){
          const saved=parts[0].properties?.['tubeDesigner.punchWizard'];
          assert.equal(saved?.features?.length,1,'Native scene query returns the persisted branch record');
          assert.equal(saved.features[0].section.parameters.width,14);
          assert.equal(saved.features[0].section.profile.width,14);
          assert(saved.features[0].section.profile.contours.length>0);
          report.savedBranch={entityId:parts[0].entityId,parameters:saved.features[0].section.parameters,
            profileWidth:saved.features[0].section.profile.width,contours:saved.features[0].section.profile.contours.length};
        }
      }
      report.nativePreviewOnly=!rowAddApplyOnly&&!branchApplyOnly;
      report.nativeResourceBytes=nativeCalls.filter(call=>call.request.method==='ReadResource').map(call=>call.response.result?.bytes);
    }
  } else {
  assert.equal(await page.locator('[aria-modal="true"]').count(), 1, 'The batch wizard remains one modal');
  assert.equal(await page.locator('[data-punch-region]').count(), 5);
  assert(await page.evaluate(() => document.querySelector('#background').inert), 'The existing nesting background is inert');
  await page.evaluate(() => {
    const modal = document.querySelector('[aria-modal="true"]');
    const controls = [...modal.querySelectorAll('button,input,select,textarea,a[href],[tabindex]')]
      .filter(n=>!n.disabled&&!n.closest('[inert]')&&n.tabIndex>=0&&n.getClientRects().length>0);
    window.fixture.modalFirst = controls[0]; window.fixture.modalLast = controls.at(-1); controls.at(-1).focus();
  });
  await page.keyboard.press('Tab');
  assert(await page.evaluate(() => document.activeElement===window.fixture.modalFirst), 'Tab wraps inside the modal');
  await page.keyboard.press('Shift+Tab');
  assert(await page.evaluate(() => document.activeElement===window.fixture.modalLast), 'Reverse Tab wraps inside the modal');
  await page.evaluate(() => { window.fixture.canvas = window.fixture.viewport.renderer.domElement; });
  await page.evaluate(async () => {
    const f = window.fixture, row = f.view.tubeDesignerPunchBatch.parts[0];
    Object.assign(row.draft, { name: '左立柱', length: '1000', quantity: '12' });
    const circle = f.wizard.normalizePunchFeature({ recordKind: 'tool', station: 100, face: 'round', layoutDatum: 'base' });
    f.wizard.selectPunchTool(row.wizard, circle, 'circle'); circle.toolParameters.diameter = 12;
    const slot = f.wizard.normalizePunchFeature({ recordKind: 'tool', station: 350, face: 'round', layoutDatum: 'base' });
    f.wizard.selectPunchTool(row.wizard, slot, 'slot'); slot.toolParameters.spanAlong = 38; slot.toolParameters.spanAcross = 10;
    row.wizard.features = [circle, slot]; row.wizard.showDraftRow = false;
    row.wizard.ends.start = { type: 'template', toolRef: { id: 'end-miter' }, toolParameters: { angle: 25 }, datum: 'long', rotation: 0, trim: 0 };
    f.batch.syncPunchBatch(f.view); f.view.tubeDesignerPunchBatch.selectedPartIds = [row.id];
    await f.dispatch('batch-copy');
  }); await ready();
  assert.equal(await page.locator('[data-punch-batch-part-row]').count(), 2);
  await partInput(1, 'name').fill('右立柱'); await partInput(1, 'name').press('Tab'); await idle();
  await partInput(1, 'length').fill('1400'); await partInput(1, 'length').press('Tab'); await ready();
  await partInput(1, 'quantity').fill('8'); await partInput(1, 'quantity').press('Tab'); await idle();
  await page.evaluate(async () => {
    const f = window.fixture; f.view.tubeDesignerPunchWizard.features[0].station = 180;
    await f.dispatch('batch-add');
    const row = f.batch.currentPunchBatchPart(f.view);
    Object.assign(row.draft, { name: '密集孔件', length: '600', quantity: '4' }); row.wizard.baseLength = 600;
    const dense = f.wizard.normalizePunchFeature({ recordKind: 'tool', face: 'round', station: 60, layoutDatum: 'base',
      arrayGroups: [{ id: 'array-group-1', type: 'linear', axis: 'X', count: 5, spacing: 100, distributionMode: 'pitch' },
        { id: 'array-group-2', type: 'polar', axis: 'X', count: 12, angleMode: 'full-circle' }], arraySkips: [] });
    f.wizard.selectPunchTool(row.wizard, dense, 'circle'); dense.toolParameters.diameter = 6;
    row.wizard.features = [dense]; row.wizard.showDraftRow = false; row.wizard.sheetColumns = { pose: true, arrays: true };
    f.batch.syncPunchBatch(f.view); await f.dispatch('preview');
  }); await ready();
  assert.equal(await page.locator('[data-punch-batch-part-row]').count(), 3);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].arrayGroups.length), 2);
  assert.equal(await page.evaluate(() => window.fixture.calls.findLast(c => c.method.endsWith('PreviewPunchWizard')).payload.features[0].arrayTransforms.length), 60);
  assert(await page.evaluate(() => window.fixture.background === document.querySelector('#background')), 'The nesting background is retained');
  assert(await page.evaluate(() => window.fixture.canvas === window.fixture.viewport.renderer.domElement), 'Switching part rows retains the renderer canvas');
  await screenshot('01-three-parts-dense-two-dimensional-array');
  report.checks.push('Three independently configured parts; copied positions, ends and quantities; dense 5 × 12 circumferential array');
  // Shared definitions modify shape only, across different selected part rows.
  await page.evaluate(async () => {
    const f = window.fixture, first = f.view.tubeDesignerPunchBatch.parts[0];
    await f.dispatch('batch-select', { dataset: { punchBatchPartId: first.id } });
    const definitionId = first.wizard.features[1].batchDefinitionId;
    await f.dispatch('batch-definition-select', { dataset: { punchBatchDefinitionId: definitionId } });
  }); await ready();
  const definition = page.locator('[data-punch-batch-definition-editor]');
  assert.equal(await definition.locator('[data-tube-designer-punch-parameter="diameter"]').count(), 0);
  assert.equal(await definition.locator('[data-tube-designer-punch-parameter="spanAlong"]').count(), 1);
  await definition.locator('[data-tube-designer-punch-parameter="spanAlong"]').fill('42');
  await definition.locator('[data-tube-designer-punch-parameter="spanAlong"]').press('Tab'); await ready();
  const shared = await page.evaluate(() => {
    const parts = window.fixture.view.tubeDesignerPunchBatch.parts;
    return parts.slice(0, 2).map(row => ({ span: row.wizard.features[1].toolParameters.spanAlong, station: row.wizard.features[1].station }));
  });
  assert.deepEqual(shared, [{ span: 42, station: 350 }, { span: 42, station: 350 }]);
  await definition.locator('[data-tube-designer-punch-parameter="spanAcross"]').evaluate(control=>window.fixture.conditionalPeer=control);
  await definition.locator('[data-tube-designer-punch-field="blindHole"]').evaluate(control=>window.fixture.conditionalCheckbox=control);
  assert.equal(await definition.locator('[data-tube-designer-punch-field="cutDepth"]').count(),0);
  await definition.locator('[data-tube-designer-punch-field="blindHole"]').check();await ready();
  assert(await definition.locator('[data-tube-designer-punch-field="blindHole"]').evaluate(control=>
    control===window.fixture.conditionalCheckbox&&control===document.activeElement),
    'A condition checkbox retains its node and focus when preview unlocks the modal');
  assert.equal(await definition.locator('[data-tube-designer-punch-field="cutDepth"]').count(),1);
  assert(await definition.locator('[data-tube-designer-punch-parameter="spanAcross"]').evaluate(control=>control===window.fixture.conditionalPeer),
    'Adding a template condition field preserves following input nodes');
  await definition.locator('[data-tube-designer-punch-field="blindHole"]').uncheck();await ready();
  assert(await definition.locator('[data-tube-designer-punch-field="blindHole"]').evaluate(control=>
    control===window.fixture.conditionalCheckbox&&control===document.activeElement),
    'Removing condition fields restores the same checkbox before modal fallback focus');
  assert.equal(await definition.locator('[data-tube-designer-punch-field="cutDepth"]').count(),0);
  assert(await definition.locator('[data-tube-designer-punch-parameter="spanAcross"]').evaluate(control=>control===window.fixture.conditionalPeer));
  await screenshot('02-shared-slot-definition-and-selected-part');
  await page.locator('[data-punch-batch-part-row]').first().locator('[data-cam-action$="batch-end-open"]').first().click(); await idle();
  const detail = page.locator('[data-punch-batch-end-detail]');
  assert.equal(await detail.count(), 1, 'End parameters remain inline under their part row');
  assert.equal(await page.locator('[data-punch-parameter-dialog]').count(), 0, 'End editing adds no parameter popup');
  await screenshot('03-part-end-parameters-inline');
  await click('batch-end-close');
  // Computation is serial in the background. All table regions stay editable
  // and retain the user's latest scroll when its native response completes.
  await page.evaluate(() => {
    const f = window.fixture, batch = f.view.tubeDesignerPunchBatch, first = batch.parts[0];
    const source = first;
    for (let index = 0; index < 12; index++) {
      const row = structuredClone(source); row.id = 'scroll-' + index; row.draft.name = '可滚动零件 ' + index;
      row.wizard.batchPartId = row.id; row.wizard.features = Array.from({ length: 18 }, (_, n) => ({ ...structuredClone(source.wizard.features[0]), id: 'many-' + index + '-' + n, station: 50 + n * 45 }));
      batch.parts.push(row);
    }
    first.wizard.features = Array.from({ length: 18 }, (_, n) => ({ ...structuredClone(first.wizard.features[0]), id: 'many-first-' + n, station: 50 + n * 45 }));
    first.wizard.sheetColumns = { pose: true, arrays: true };
    batch.punchRegionSizes = { main: 100, definitions: 150, parts: 190, holes: 220 }; f.render();
    f.deferPreview = true; void f.dispatch('preview');
  });
  await page.waitForFunction(() => typeof window.fixture.releasePreview === 'function');
  assert(await page.evaluate(() => !window.fixture.view.pending
    &&!document.querySelector('[data-punch-batch-part-row] [data-punch-batch-field="name"]').disabled
    &&!document.querySelector('[data-tube-designer-operation-wait]')),
    'Background native computation keeps the form editable without a waiting overlay');
  await page.evaluate(() => {
    const f = window.fixture;
    f.latestScroll = {};
    for (const selector of ['.punch-main-scroll','.punch-batch-definition-scroll','.punch-batch-part-scroll','.tube-designer-punch-sheet-scroll']) {
      const node = document.querySelector(selector); if (!node) continue;
      node.scrollTop = Math.min(120,node.scrollHeight-node.clientHeight); node.scrollLeft = Math.min(140,node.scrollWidth-node.clientWidth);
      f.latestScroll[selector] = [node.scrollTop,node.scrollLeft];
    }
    f.releasePreview();
  }); await idle();
  report.computeScroll=await page.evaluate(()=>Object.entries(window.fixture.latestScroll).map(([selector,wanted])=>{
    const node=document.querySelector(selector);return {selector,wanted,actual:[node.scrollTop,node.scrollLeft],extent:[node.scrollHeight,node.clientHeight,node.scrollWidth,node.clientWidth]};
  }));
  assert(await page.evaluate(() => Object.entries(window.fixture.latestScroll).every(([selector,[top,left]]) => {
    const node=document.querySelector(selector); return node.scrollTop===top && node.scrollLeft===left;
  })), 'Every region retains the latest scroll positions after native computation');
  // An asynchronous resource response can also cause a patch independently of
  // a computation. Keep the current, unsubmitted input and live selection.
  await page.evaluate(() => {
    const f=window.fixture, reference=f.view.tubeDesignerPunchWizard.preview.baseGeometry;
    f.deferResource=true;
    f.resourceReply=f.context.sceneProxy.resources.get(reference.url,{headers:new Headers({'ICAX-Resource-Version':String(reference.version)})})
      .then(response=>response.arrayBuffer()).then(()=>f.render());
  });
  await page.waitForFunction(() => typeof window.fixture.releaseResource === 'function');
  await page.evaluate(() => {
    const f=window.fixture,input=document.querySelector('[data-punch-batch-part-row] [data-punch-batch-field="name"]');
    f.typingInput=input;input.focus({preventScroll:true});input.value='继续编辑中的零件名称';input.setSelectionRange(2,7,'backward');
    f.latestScroll={};
    for(const selector of ['.punch-main-scroll','.punch-batch-definition-scroll','.punch-batch-part-scroll','.tube-designer-punch-sheet-scroll']) {
      const node=document.querySelector(selector);if(!node)continue;
      node.scrollTop=Math.min(140,node.scrollHeight-node.clientHeight);node.scrollLeft=Math.min(155,node.scrollWidth-node.clientWidth);
      f.latestScroll[selector]=[node.scrollTop,node.scrollLeft];
    }
    f.releaseResource();
  });
  await page.evaluate(()=>window.fixture.resourceReply);await idle();
  assert(await page.evaluate(() => {
    const f=window.fixture,n=f.typingInput;
    return n===document.activeElement && n===document.querySelector('[data-punch-batch-part-row] [data-punch-batch-field="name"]')
      && n.value==='继续编辑中的零件名称' && n.selectionStart===2 && n.selectionEnd===7 && n.selectionDirection==='backward';
  }), 'Asynchronous refresh retains the same input, current text and latest caret');
  assert(await page.evaluate(() => Object.entries(window.fixture.latestScroll).every(([selector,[top,left]]) => {
    const node=document.querySelector(selector); return node.scrollTop===top && node.scrollLeft===left;
  })), 'Every region retains the latest scroll positions');
  assert(await page.evaluate(() => window.fixture.canvas===window.fixture.viewport.renderer.domElement));
  report.checks.push('Serial background computation keeps tables editable; latest scroll survives computation; resource reply retains canvas/input identity, live text and caret');
  await page.evaluate(async () => {
    const f=window.fixture,b=f.view.tubeDesignerPunchBatch; b.parts=b.parts.slice(0,3);
    b.parts[0].wizard.features=b.parts[1].wizard.features.map(feature=>structuredClone(feature));
    b.parts[0].wizard.features[0].station=100; b.parts[0].draft.name='左立柱';
    b.parts[0].wizard.sheetColumns={pose:true,arrays:true}; b.selectedPartIds=[]; delete b.punchRegionSizes;
    f.batch.syncPunchBatch(f.view); f.render(); await f.dispatch('preview');
    document.querySelectorAll('.punch-main-scroll,.punch-batch-definition-scroll,.punch-batch-part-scroll,.tube-designer-punch-sheet-scroll')
      .forEach(node=>{node.scrollTop=0;node.scrollLeft=0;});
  }); await ready();
  const beforeCreate = await page.evaluate(() => window.fixture.calls.filter(c=>c.method.endsWith('AddNestingPunchPart')).length);
  assert.equal(beforeCreate,0,'Edits and previews never create any final part');
  // Resize the actual scene/holes divider without replacing the scene.
  const splitter=page.locator('[data-punch-region-splitter="holes:scene"]'), splitBox=await splitter.boundingBox();
  const beforeHeights=await page.evaluate(()=>Object.fromEntries([...document.querySelectorAll('[data-punch-region]')].map(n=>[n.dataset.punchRegion,n.getBoundingClientRect().height])));
  const beforeDragCamera=await camera();
  await page.mouse.move(splitBox.x+splitBox.width/2,splitBox.y+splitBox.height/2);
  await page.mouse.down();await page.mouse.move(splitBox.x+splitBox.width/2,splitBox.y+splitBox.height/2-50,{steps:5});await page.mouse.up();await frame();
  const resized=await page.evaluate(()=>Object.fromEntries([...document.querySelectorAll('[data-punch-region]')].map(n=>[n.dataset.punchRegion,n.getBoundingClientRect().height])));
  assert(resized.scene>beforeHeights.scene+40,'Dragging the divider increases the scene height');
  sameHorizontalView(beforeDragCamera,await camera(),'Divider drag');
  assert.equal(await page.locator('[data-punch-batch-scene-expand]').count(),0,'Scene enlargement uses its draggable divider');
  assert(await page.evaluate(()=>window.fixture.canvas===window.fixture.viewport.renderer.domElement));
  await screenshot('05-resized-wide-scene');
  report.checks.push('Divider drag changes scene height and preserves the identical canvas and camera frame; no redundant scene expand action');
  await page.evaluate(()=>{delete window.fixture.view.tubeDesignerPunchBatch.punchRegionSizes;window.fixture.render();});await frame();
  await page.setViewportSize({width:1024,height:768});await frame();
  assert(await page.evaluate(()=>{
    const modal=document.querySelector('[aria-modal="true"]'),actions=modal.querySelector('.punch-batch-main-actions');
    return modal.getBoundingClientRect().right<=innerWidth+1&&modal.getBoundingClientRect().bottom<=innerHeight+1
      &&actions.closest('[data-punch-region="main"]')&&actions.getBoundingClientRect().right<=innerWidth+1
      &&!modal.querySelector('.tube-designer-punch-footer');
  }),'At 1024 × 768 the modal and main-region actions remain inside the window without a permanent footer');
  const smallScroll=await page.evaluate(()=>{
    const region=document.querySelector('[data-punch-region="holes"]'),scroll=region.querySelector('.tube-designer-punch-sheet-scroll');
    return {regionBottom:region.getBoundingClientRect().bottom,scrollBottom:scroll.getBoundingClientRect().bottom,width:scroll.clientWidth,fullWidth:scroll.scrollWidth};
  });
  assert(smallScroll.scrollBottom<=smallScroll.regionBottom+1,'The actual horizontal scrollbar stays inside its holes region');
  assert(smallScroll.fullWidth>smallScroll.width,'Expanded columns scroll horizontally on a small screen');
  assert(await page.evaluate(()=>{const n=document.querySelector('.tube-designer-punch-sheet-scroll');n.scrollLeft=n.scrollWidth;return n.scrollLeft>0;}),
    'All right-hand array columns remain reachable by the horizontal scroll container');
  await page.evaluate(()=>document.querySelector('.tube-designer-punch-sheet-scroll').scrollLeft=0);
  report.smallScreenScroll=smallScroll;
  await screenshot('06-1024x768-batch-and-native-scene');
  await page.setViewportSize({width:1600,height:1000});await frame();
  await screenshot('04-ready-to-confirm-three-parts');
  await click('apply'); await idle();
  const created = await page.evaluate(() => window.fixture.calls.filter(c=>c.method.endsWith('AddNestingPunchPart')));
  assert.equal(created.length,3);
  assert.deepEqual(created.map(call=>[call.payload.name,call.payload.length,call.payload.quantity]),
    [['左立柱',1000,12],['右立柱',1400,8],['密集孔件',600,4]]);
  assert(created.every(call=>call.scope==='scene'));
  assert.equal(await page.locator('[aria-modal="true"]').count(),0);
  if(nativeRuntime) {
    const snapshot=await rpc('scene','List'), saved=snapshot.tubeDesigner.nestingGroups.flatMap(group=>group.parts??[]);
    assert.equal(saved.length,3);
    const quantities=saved.map(part=>Number(part.quantity)).sort((a,b)=>a-b); assert.deepEqual(quantities,[4,8,12]);
    assert(saved.every(part=>part.manufacturingGeometryResourceId && part.thumbnailGeometryResourceId),'Each persisted part has final manufacturing and display geometry');
    const resources=await Promise.all(saved.map(part=>rpc('inspection','ReadResource',{url:part.thumbnailGeometryResourceId,version:part.thumbnailGeometryResourceVersion})));
    assert(resources.every(resource=>resource.bytes>0));
    report.savedParts=saved.map(part=>({entityId:part.entityId,name:part.name,length:part.length,quantity:part.quantity,
      manufacturingGeometryResourceId:part.manufacturingGeometryResourceId,thumbnailGeometryResourceId:part.thumbnailGeometryResourceId,
      propertiesKeys:Object.keys(part.properties??{})}));
    report.finalResourceBytes=resources.map(resource=>({url:resource.url,version:resource.version,bytes:resource.bytes}));
    report.checks.push('Real native scene creates and persists three final cut parts; all quantities and final resource bytes checked');
  }
  for(const dismissal of ['cancel','escape','close']) {
    await page.evaluate(()=>window.fixture.dispatch('open',document.querySelector('#opener')));await ready();
    await click('batch-add');await ready();
    if(dismissal==='escape'){await page.keyboard.press('Escape');await idle();}
    else if(dismissal==='close'){await page.locator('.tube-designer-dialog-close').click();await idle();}
    else await click('cancel');
    assert.equal(await page.locator('[aria-modal="true"]').count(),0);
    assert.equal(await page.evaluate(()=>window.fixture.calls.filter(call=>call.method.endsWith('AddNestingPunchPart')).length),3,
      dismissal+' creates no additional final part');
    assert(await page.evaluate(()=>window.fixture.background===document.querySelector('#background')&&!document.querySelector('#background').inert),
      dismissal+' restores the unchanged nesting background');
    assert(await page.evaluate(()=>document.activeElement===document.querySelector('#opener')),dismissal+' returns focus to the opening control');
  }
  assert.equal(await page.evaluate(()=>window.fixture.modalClosures),3,'All three cancellation routes use the production local close patch');
  await page.evaluate(()=>window.fixture.dispatch('open',document.querySelector('#opener')));await ready();
  await partInput(0,'name').evaluate(control=>{
    const clipboardData=new DataTransfer();clipboardData.setData('text/plain','甲零件\t800\t3\tlong\tQ235B\n乙零件\t1200\t5\tlong\t304\n丙零件\t1500\t2\tlong\t');
    control.dispatchEvent(new ClipboardEvent('paste',{clipboardData,bubbles:true,cancelable:true}));
  });await ready();
  assert.deepEqual(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchBatch.parts.map(row=>[row.draft.name,row.draft.length,row.draft.quantity])),
    [['甲零件','800','3'],['乙零件','1200','5'],['丙零件','1500','2']],'Excel TSV paste populates three actual production rows');
  await partInput(0,'name').focus();await partInput(0,'name').press('Enter');await idle();
  assert(await partInput(1,'name').evaluate(control=>control===document.activeElement),'Enter advances to the same column on the following row');
  await click('cancel');
  report.checks.push('Excel clipboard TSV populates three production rows; Enter advances to the same column; cancel still creates nothing');
  await page.evaluate(async()=>{
    const f=window.fixture;document.querySelector('#opener').focus();f.pending++;
    try{await f.creation.handleNestingPunchPartRibbonCommand(f.context,f.view,'nesting.add-punch-part',f.ops);}
    finally{f.pending--;}
  });await ready();await click('cancel');
  assert(await page.evaluate(()=>document.activeElement===document.querySelector('#opener')),'Ribbon opening also restores focus to its actual entry');
  report.checks.push('Real Ribbon command opens the same modal and returns focus to its original entry on close');
  assert.deepEqual(browserErrors,[]);
  report.checks.push('Inert background and Tab trap; Cancel, Escape and close create no part and return focus to unchanged nesting workbench');
  }
  report.status='passed'; console.log('PASS batch modal/browser' + (nativeRuntime ? previewOnly&&!rowAddApplyOnly&&!branchApplyOnly
    ? ' + real native preview/resources (no creation)' : ' + real native preview/resources/creation' : '') + ': '+report.checks.length+' acceptance groups.');
} catch(error) {
  report.status='failed';report.failure={message:error.message,stack:error.stack};
  if(page&&definitionLayoutOnly)await page.screenshot({path:resolve(output,'definition-layout-failure.png')});
  if(page&&rowAddResponseOnly)await page.screenshot({path:resolve(output,'row-add-response-failure.png')});
  if(page&&profileAdvancedOnly) {
    await page.screenshot({path:resolve(output,'advanced-profile-failure.png')});
    report.failure.dom=await page.evaluate(()=>{
      const field=document.querySelector('[data-punch-profile-advanced-dialog] [data-tube-designer-main-profile-parameter="useOuterRadii"]'),nodes=[];
      for(let node=field;node&&nodes.length<12;node=node.parentElement){
        const c=getComputedStyle(node),r=node.getBoundingClientRect();
        nodes.push({tag:node.tagName,className:node.className,id:node.id,display:c.display,visibility:c.visibility,
          height:r.height,width:r.width,hidden:node.hidden,inert:node.inert,html:node.outerHTML.slice(0,800)});
      }
      return nodes;
    });
  }
  throw error;
} finally {
  report.browserErrors=browserErrors; report.nativeRequests=nativeCalls;
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));
  writeFileSync(resolve(output,'native-stderr.log'),nativeStderr);
  if(browser)await browser.close();
  if(host){host.stdin.end();host.kill();}
}
