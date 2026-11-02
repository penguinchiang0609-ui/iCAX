import assert from 'node:assert/strict';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const output = browserReportDirectory('preparation-panels-20261030/native-browser');
const nativeDirectory = resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY || resolve(root, 'src/x64/Debug'));
const processHandle = spawn(resolve(nativeDirectory, process.env.ICAX_FIVE_AXIS_NATIVE_EXECUTABLE || 'FiveAxisCAMTest.exe'),
  ['--gtest_filter=FiveAxisBridge.DISABLED_Serve', '--gtest_also_run_disabled_tests'],
  { cwd: nativeDirectory, windowsHide: true, env: { ...process.env, ICAX_FIVE_AXIS_BRIDGE_BOX: '1' }, stdio: ['pipe','pipe','pipe'] });
let sequence = 0, browser, page, failure;
const waiting = new Map(), calls = [], errors = [], nativeLog = [];
const failAll = error => { for (const entry of waiting.values()) { clearTimeout(entry.timer); entry.reject(error); } waiting.clear(); };
processHandle.on('error', failAll);
processHandle.on('exit', code => { if (waiting.size) failAll(Error(`Native bridge exited ${code}`)); });
processHandle.stderr.on('data', bytes => nativeLog.push(String(bytes)));
createInterface({ input: processHandle.stdout }).on('line', line => {
  if (!line.startsWith('__FIVE_RPC__')) { nativeLog.push(line); return; }
  const response = JSON.parse(line.slice(12)), pending = waiting.get(response.id);
  if (!pending) return;
  waiting.delete(response.id); clearTimeout(pending.timer);
  if (response.ok) pending.resolve(response.result); else pending.reject(Error(response.error));
});
const rpc = message => new Promise((resolve, reject) => {
  const id = ++sequence;
  calls.push({ id, ...message });
  const timer = setTimeout(() => { waiting.delete(id); reject(Error(`Native timeout ${message.action}`)); }, Number(process.env.ICAX_FIVE_AXIS_RPC_TIMEOUT_MS||15000));
  waiting.set(id, { resolve, reject, timer });
  const wire = message.action === 'invoke'
    ? { ...message, payload: undefined, payloadText: message.payloadText ?? serializeVariantText(message.payload ?? {}) } : message;
  processHandle.stdin.write(JSON.stringify({ id, ...wire }) + '\n');
});
try {
  const info = await rpc({ action: 'info' });
  info.validateFixtureTimeouts=!!(process.env.ICAX_FIXTURE_GRID_TIMEOUT_ONLY||process.env.ICAX_FIXTURE_SKELETON_ONLY);
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge', headless: true });
  page = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor:Number(process.env.ICAX_BROWSER_DEVICE_SCALE_FACTOR||1) });
  page.on('pageerror', error => errors.push(error.message));
  await page.exposeFunction('fiveNativeRpc', rpc);
  await page.route('**/*', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"><body style="margin:0"><div id="app" style="height:100vh"></div></body></html>' });
    if (!path.startsWith('/src/')) return route.abort();
    try { return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : path.endsWith('.svg')?'image/svg+xml':'text/javascript', body: readFileSync(browserAssetPath(path.slice(5))) }); }
    catch { return route.abort(); }
  });
  await page.goto('http://localhost/');
  await page.evaluate(async info => {
    const { serializeVariantText } = await import('/src/iCAX-UI/SDK/SDO/variantSerializer.mjs');
    const entry = await import('/src/apps/five-axis-cam/webpage/entry.mjs');
    const { ViewClient } = await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const view = getProjectView(info.projectId);
    const f = window.fixture = { entry, view, fileSelections: 0, logs: [], hold: false, held: false, release: null };
    const sceneProxy = { sceneId: info.sceneId, state: { sceneId: info.sceneId }, pdo: { enabled: false },
      async invoke(method, payload = {}, options = {}) {
        (f.methods??=[]).push({method,at:performance.now()});
        if(['CamScene.ReadSnapFeatures','CamScene.ResolvePickReference'].includes(method))f.snapCalls=(f.snapCalls||0)+1;
        if (method === (f.holdMethod || 'CamScene.ReadState') && f.hold) { f.hold = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false; }
        const result = f.invokeSdo?await f.invokeSdo(method,payload,options):await fiveNativeRpc({ action: 'invoke', scope: 'scene', method, payload, payloadText:serializeVariantText(payload) });
        if (['CamPlacement.SetTransform','CamPlacement.ApplyRegistration','CamWorkpiece.Scale'].includes(method) && f.holdAfterApply) {
          f.holdAfterApply = false; f.held = true; await new Promise(resolve => { f.release = resolve; }); f.held = false;
        }
        if (['CamPlacement.SetTransform','CamWorkpiece.Scale','CamPlacement.ApplyRegistration','CamWorkpiece.Remove','CamSetup.SetLocked','CamSetup.Manage','CamSetup.SetRegistrationPlan','CamWorkpiece.SetEnabled'].includes(method) && f.dropApply) { f.dropApply = false; throw Error('Injected lost apply receipt'); }
        if (method === 'CamScene.QueryCommand' && f.dropQuery) { f.dropQuery = false; throw Error('Injected unknown receipt'); }
        if(method==='CamWorkpiece.CancelTask')f.cancelAcknowledged=true;
        if(method==='CamWorkpiece.QueryTask'&&result.status==='Completed'&&f.holdAfterImport){f.holdAfterImport=false;f.holdMethod='CamScene.ReadState';f.hold=true;}
        return result;
      }, async getState() { return this.state; } };
    if(info.validateFixtureTimeouts){
      const {SDOClient}=await import('/src/iCAX-UI/SDK/SDO/sdoClient.mjs');
      const {makeSDOMethodCodeFromName}=await import('/src/iCAX-UI/SDK/SDO/sdoMethod.mjs');
      const methods=new Map();let receive;f.sdoTimings=[];
      f.sdoClient=new SDOClient({
        subscribeSDOFrames(channel,handler){receive=handler;return()=>{};},
        async postSDOFrame(frame){
          const method=methods.get(String(frame.methodCode)),started=performance.now();
          let result;try{result=await fiveNativeRpc({action:'invoke',scope:'scene',method,payloadText:frame.payloadText});}
          catch(error){receive({...frame,kind:2,status:3,payloadText:error.message});return;}
          const nativeMs=performance.now()-started;
          if(method==='CamFixture.QueryTask'&&result.status==='Completed'&&f.delayFixtureTerminalMs){
            const delay=f.delayFixtureTerminalMs;f.delayFixtureTerminalMs=0;f.delayingFixtureTerminal=true;
            await new Promise(r=>setTimeout(r,delay));f.delayingFixtureTerminal=false;
          }
          f.sdoTimings.push({method,status:result.status,nativeMs,elapsedMs:performance.now()-started});
          receive({...frame,kind:2,status:0,payloadText:serializeVariantText(result)});
        }
      });
      f.invokeSdo=(method,payload,options)=>{methods.set(makeSDOMethodCodeFromName(method),method);return f.sdoClient.invoke(info.sceneId,method,payload,options);};
    }
    const resource = async (url, options, head) => {
      const version = new Headers(options?.headers).get('ICAX-Resource-Version') ?? String(options?.version ?? '');
      const result = await fiveNativeRpc({ action: 'resource', url, version, head, encoding:'base64' });
      const bytes=head?null:Uint8Array.from(atob(result.base64),c=>c.charCodeAt(0));
      return new Response(bytes, { headers: { 'Content-Type': 'application/vnd.icax.flatbuffer', 'ICAX-Resource-Version': result.version } });
    };
    sceneProxy.resources = { get: (url, options) => resource(url, options, false), head: (url, options) => resource(url, options, true) };
    sceneProxy.views = new ViewClient(sceneProxy);
    const context = f.context = { mount: document.querySelector('#app'), sceneProxy,
      projectProxy:{state:{}},
      project: { projectId: info.projectId, projectName: '工件平移与旋转 · 原生验收' }, activeRibbonTabId: 'workpiece',
      appProxy: { bridge: { async openFileDialog() { ++f.fileSelections; return info.sourcePath; } } },
      productProxy: { async invoke(method, payload = {}) {
        (f.methods??=[]).push({method,at:performance.now()}); return fiveNativeRpc({ action: 'invoke', scope: 'product', method, payload }); } },
      isCurrentProject: () => true,
      actions: { refreshProductRibbon() {}, appendProjectLog(...args) { f.logs.push(args); },
        async selectRibbonTab(id) { context.activeRibbonTabId = id; entry.mountProject(context); } } };
    entry.mountProject(context);
  }, info);
  await page.waitForFunction(() => !fixture.view.pending && !!fixture.view.scene?.domain);

  const result=process.env.ICAX_FIXTURE_FLOW_ONLY
    ?await (await import('./FiveAxisFixtureFlowOpenings.acceptance.mjs')).verifyFixtureFlowOpenings({page,rpc,info,output,modelPath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL})
    :process.env.ICAX_FIXTURE_SPLIT_ONLY
    ?await (await import('./FiveAxisFixtureSplit.acceptance.mjs')).verifyFixtureSplit({page,rpc,info,output})
    :process.env.ICAX_GEOMETRY_SCALE_ONLY
    ?await (await import('./FiveAxisGeometryScale.acceptance.mjs')).verifyGeometryScale({page,rpc,info,output,modelPath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL})
    :process.env.ICAX_COMPACT_PLACEMENT_ONLY
    ?await (await import('./FiveAxisCompactPlacement.acceptance.mjs')).verifyCompactPlacement({page,rpc,info,output})
    :process.env.ICAX_UNCHECKED_ALERTS_ONLY
    ?await (await import('./FiveAxisNeverCheckedAlerts.acceptance.mjs')).verifyNeverCheckedAlerts({page,rpc,info,output,modelPath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL})
    :process.env.ICAX_FIXTURE_SKELETON_ONLY
    ?await (await import('./FiveAxisFixtureSkeleton.acceptance.mjs')).verifyFixtureSkeleton({page,rpc,info,output,modelPath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL,interactionOnly:!!process.env.ICAX_FIXTURE_INTERACTION_ONLY})
    :process.env.ICAX_FIXTURE_GRID_TIMEOUT_ONLY
    ?await (await import('./FiveAxisFixtureGridTimeout.acceptance.mjs')).verifyFixtureGridTimeout({page,rpc,info,output,modelPath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL,terminalDelayMs:Number(process.env.ICAX_FIXTURE_TERMINAL_DELAY_MS??16000)})
    :process.env.ICAX_FIXTURE_PROGRESS_ONLY
    ?await (await import('./FiveAxisFixtureProgress.acceptance.mjs')).verifyFixtureProgress({page,rpc,info,output,modelPath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL})
    :process.env.ICAX_FIXTURE_SUPPORT_VIEW_ONLY
    ?await (await import('./FiveAxisFixtureDirectionAndSolid.acceptance.mjs')).verifyFixtureDirectionAndSolid({page,rpc,info,output,modelPath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL})
    :process.env.ICAX_FRAME_CANDIDATES_ONLY
    ?await (await import('./FiveAxisFrameCandidates.acceptance.mjs')).verifyFrameCandidates({page,rpc,info,output,modelPath:process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL})
    :process.env.ICAX_FRAME_PROPERTIES_ONLY
    ?await (async()=>{const {selectWorkpieceFile}=await import('./fixtures/fiveAxisFilePicker.mjs');await selectWorkpieceFile(page,process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL||info.sourcePath);await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);return (await import('./FiveAxisObjectFrameProperties.acceptance.mjs')).verifyObjectFrameProperties({page,rpc,output});})()
    :await (await import('./FiveAxisPreparationPanels.acceptance.mjs')).verifyPreparationPanels({page,rpc,info,output});
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,...result,coverage:{native:true,realMouse:true,standaloneCef:false},calls},null,2));
  console.log(JSON.stringify({passed:true,cases:result.cases,output}));
} catch(error) {
  failure=error;writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:false,error:error.stack,calls},null,2));
  if(page){console.error(await page.evaluate(()=>{const f=window.fixture,d=f?.view.fiveAlignment;return {text:document.body.innerText.slice(0,2000),error:f?.view.error,held:f?.held,alignment:d&&{frame:d.frame,savedFrame:d.savedFrame,dirty:d.dirty,saving:d.saving,saveTimer:!!d.saveTimer,request:!!d.request,selecting:d.selecting,error:d.error,fields:d.fieldErrors}};}));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
  throw error;
} finally {
  writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();
  try{await rpc({action:'exit'});}catch{}processHandle.stdin.end();if(failure)processHandle.kill();
}

