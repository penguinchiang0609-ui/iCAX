// Actual deployed CEF host -> production scene SDOs -> new tube boundary core.
// This creates only its own project under the supplied evidence directory.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {existsSync,mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
import {createHash} from 'node:crypto';
import {installBoundaryAcceptanceHost} from './TubeBoundaryCore.cefHost.mjs';
import {verifyBoundaryRich3d} from './TubeBoundaryCore.rich3dChecks.mjs';

const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const output=resolve(process.env.ICAX_BOUNDARY_EVIDENCE||'output/tests/tube-boundary-core-20261030/runtime-acceptance');
const endpoint=process.env.ICAX_BOUNDARY_CDP||'http://127.0.0.1:9223';
const projectPath=join(output,'tube-boundary-core-runtime.ictd');
mkdirSync(output,{recursive:true});
const report={schema:1,passed:false,standaloneCef:true,nativeBusinessMocked:false,endpoint,
  editorHost:'Temporary acceptance mount rendering production editors',formalRibbonEntryCovered:false,
  processId:Number(process.env.ICAX_BOUNDARY_PROCESS_ID||37008),checks:[],pageErrors:[],editorSessions:[],projectPath};
const digest=value=>createHash('sha256').update(JSON.stringify(value)).digest('hex');
const save=()=>writeFileSync(join(output,'cef-core-acceptance.json'),JSON.stringify(report,null,2));
const check=(condition,name,evidence={})=>{report.checks.push({name,passed:!!condition,...evidence});save();assert(condition,name);};
const browser=await chromium.connectOverCDP(endpoint);
const page=browser.contexts().flatMap(context=>context.pages()).find(p=>p.url().includes('/AppShell/index.html'));
assert(page,'Deployed CEF AppShell page is present');page.setDefaultTimeout(180000);
page.on('pageerror',error=>report.pageErrors.push(error.message));
let fixture;
const bind=async()=>page.evaluate(async()=>{
  const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
  const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:180000,initialCallId:1740000000}});
  await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();
  const project=product.getProject(window.__icaxWorkbench.getState().projectId);
  const {getProjectView}=await import(new URL('../../../apps/_shared/workbench/state/projectViewStore.mjs',location.href));
  window.__boundaryAcceptance={app,product,project,scene:project.getMainScene(),view:getProjectView(project.projectId)};
});
const invoke=(method,payload={})=>page.evaluate(({method,payload})=>window.__boundaryAcceptance.scene.invoke('TubeDesigner.'+method,payload),{method,payload});
const get=(id,extra={})=>invoke('GetTubeCadDocument',{partEntityId:id,...extra});
const boundary=read=>!!read.geometry?.Section?.Boundaries?.length&&!!read.geometry?.MachiningFaces?.Faces?.length
  &&Array.isArray(read.construction?.SolidNodes)&&!!read.placement;
const circularCut=read=>read.construction.SolidNodes.find(node=>node.ID!==read.construction.BaseNodeID
  &&node.Data.type==='SExtrudedRegionNode'&&node.Data.value.Section.Boundaries.length===1
  &&node.Data.value.Section.Boundaries[0].Curves[0]?.Segment.Curve.type==='Circle2');
const idle=()=>page.waitForFunction(()=>{
  const f=window.__boundaryAcceptance,s=f?.view?.tubeDesignerCadEditor;
  return !__icaxAppShell.getState().pendingCount&&!f?.host?.pending&&!f?.view?.pending&&!s?.loading&&!s?.saving
    &&!f?.view?.tubeDesignerSketch?.sidePreviewPending&&!f?.view?.tubeDesignerSketch?.sideSavePending;
});
const readyDraft=()=>page.waitForFunction(()=>{
  const s=window.__boundaryAcceptance.view.tubeDesignerCadEditor;
  return s?.error||s?.previewRenderError||(s&&!s.previewPending&&s.preview?.draftRevision===s.draftRevision);
});
const viewError=()=>page.evaluate(()=>{
  const v=window.__boundaryAcceptance.view;return v.error||v.tubeDesignerCadEditor?.error||v.tubeDesignerPartDrawing?.state?.error
    ||v.tubeDesignerSketch?.sideSaveError||v.tubeDesignerSketch?.sidePreviewError||window.__boundaryAcceptance.host?.failures?.[0]?.error||'';
});
const collectHost=async()=>{
  const session=await page.evaluate(()=>{const h=__boundaryAcceptance.host;if(!h||h.collected)return null;h.collected=true;
    return {partEntityId:h.view.tubeDesignerActivePartId,failures:h.failures,patches:h.patches,renders:h.renders,
      calls:h.calls.map(c=>({method:c.method,partEntityId:c.payload?.partEntityId,revision:c.payload?.revision,
        payloadFields:Object.keys(c.payload??{}),ok:c.ok,error:c.error}))};});
  if(session){report.editorSessions.push(session);save();}
};
const reopen=async()=>{
  await collectHost();
  await page.evaluate(()=>{const f=window.__boundaryAcceptance;if(f.host){f.host.dispose();f.host=null;}});
  const closingId=await page.evaluate(async path=>{
    const f=window.__boundaryAcceptance,state=__icaxAppShell.getState();
    if(state.activeProjectId!==f.project.projectId||state.activeProjectPath.replaceAll('\\','/').toLowerCase()!==path.replaceAll('\\','/').toLowerCase())
      throw new Error('The acceptance project no longer owns the active UI');
    const saved=await f.project.save(path);if(saved.saved!==true)throw new Error('Actual project save failed');
    f.closePromise=__icaxAppShell.closeProject(state.activeProjectId);return state.activeProjectId;
  },projectPath);
  assert(existsSync(projectPath),'Native save created the project file');
  // The explicit native save above is complete; close this saved test project
  // without opening the host's first-save file picker a second time.
  await page.locator(`[data-project-close-dialog="${closingId}"] [data-project-close-choice="discard"]`).click();
  await page.evaluate(()=>window.__boundaryAcceptance.closePromise);
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.tube-designer'),projectPath);
  await page.waitForFunction(()=>__icaxWorkbench?.getState().projectId===__icaxAppShell.getState().activeProjectId
    &&!__icaxAppShell.getState().pendingCount);
  await bind();await idle();
};
const openPunch=async id=>{
  await page.evaluate(installBoundaryAcceptanceHost,id);
  await page.evaluate(id=>window.__boundaryAcceptance.host.dispatch('tube-designer-punch-open',{dataset:{tubeDesignerPartId:id}}),id);await idle();
  check(await viewError()==='','Production punch editor opens without error');
};
const field=name=>page.locator(`[data-cad-field="${name}"]`);
const setField=async(name,value)=>{
  await field(name).fill(String(value));await field(name).dispatchEvent('change');await idle();await readyDraft();
  check(await viewError()==='','Native preview updates '+name);
};
try{
  await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
  await page.evaluate(()=>{const f=window.__boundaryAcceptance;if(f?.host){f.host.dispose();f.host=null;}});
  report.runtimeUrl=page.url();report.beforeAppState=await page.evaluate(()=>__icaxAppShell.getState());save();
  await page.evaluate(options=>__icaxAppShell.createProject(options),{
    productId:'icax.tube-designer',projectName:'管材核心真实运行验收',projectPath});
  await page.waitForFunction(()=>__icaxWorkbench?.getState().projectId===__icaxAppShell.getState().activeProjectId);
  await bind();
  const sourceRoot=resolve('src');
  const descriptor=JSON.parse(readFileSync(join(sourceRoot,'apps/tube-designer/templates/profile/rect/profile.json'),'utf8'));
  const parameters={...Object.fromEntries(descriptor.parameters.map(p=>[p.key,p.defaultValue])),
    width:80,depth:60,wallThickness:3,cornerRadius:0,innerRadius:0};
  const evaluated=await page.evaluate(({parameters})=>window.__boundaryAcceptance.product.invoke('TubeDesigner.EvaluateProfilePackage',{
    profileRef:{scope:'system',id:'rect'},parameters}),{parameters});
  const circle=(await invoke('GetPartDrawingTools')).tools.find(tool=>tool.id==='circle');assert(circle);
  const creationPayload={
    name:'核心三维编辑方管',quantity:1,
    drawing:{schemaVersion:1,length:400,section:{source:'library',profile:evaluated.profile}},ends:{},
    features:[{id:'core-hole',type:'circle',toolTarget:circle.target,toolRef:{scope:'system',id:'circle'},
      toolParameters:{...circle.defaultParameters,diameter:10},station:140,face:'top',offset:0,layoutDatum:'base'}]};
  report.creationPayload=creationPayload;save();
  fixture=await invoke('AddNestingPunchPart',creationPayload);
  const first=await get(fixture.partEntityId);check(boundary(first),'Actual native creation publishes core + construction + placement');
  const cut=circularCut(first);assert(cut);report.partEntityId=fixture.partEntityId;
  report.firstCore={sha256:digest(first.geometry),faces:first.geometry.MachiningFaces.Faces.length,revision:first.revision};save();
  // Reload through a real project repository before entering the production UI.
  await reopen();check(digest((await get(fixture.partEntityId)).geometry)===digest(first.geometry),'Actual project close/reopen retains the complete core');
  await openPunch(fixture.partEntityId);
  await page.locator(`[data-cam-action="tube-designer-cad-select"][data-cad-node="${cut.ID}"]`).click();await idle();
  check(Math.abs(Number(await field('radius').inputValue())-10)<1e-8,'Production diameter control addresses the authored construction');
  await setField('radius',12);
  const frame=(await get(fixture.partEntityId,{nodeId:cut.ID})).sketch.constructionFrame;
  await setField('location-X',frame.Location.X+12);
  const draft=await get(fixture.partEntityId);
  check(digest(draft.geometry)===digest(first.geometry)&&draft.revision===first.revision,'Native preview does not publish a draft core');
  await page.locator('[data-cam-action="tube-designer-cad-save"]').click();await idle();
  check(await viewError()==='','Production punch UI applies the edited native core');
  const edited=await get(fixture.partEntityId,{nodeId:cut.ID});
  check(boundary(edited)&&digest(edited.geometry)!==digest(first.geometry),'Diameter and three-dimensional position edit changes the authoritative core');
  check(edited.sketch.geometry.value.Boundaries[0].Curves.every(c=>Math.abs(c.Segment.Curve.value.Radius-6)<1e-8)
    &&Math.abs(edited.sketch.constructionFrame.Location.X-frame.Location.X-12)<1e-8,'Native stored construction retains edited diameter and position');
  await page.screenshot({path:join(output,'cef-core-3d-edit.png')});
  await page.getByRole('button',{name:'完成',exact:true}).click();await idle();
  const savedCore=digest(edited.geometry);await reopen();
  const stored=await get(fixture.partEntityId);check(digest(stored.geometry)===savedCore&&stored.revision===edited.revision,'Edited core persists through actual disk save, close and fresh repository reopen');
  await openPunch(fixture.partEntityId);
  await page.locator(`[data-cam-action="tube-designer-cad-select"][data-cad-node="${cut.ID}"]`).click();await idle();
  await setField('radius',14);await page.locator('[data-cam-action="tube-designer-cad-save"]').click();await idle();
  const continued=await get(fixture.partEntityId,{nodeId:cut.ID});
  check(continued.sketch.geometry.value.Boundaries[0].Curves.every(c=>Math.abs(c.Segment.Curve.value.Radius-7)<1e-8)
    &&digest(continued.geometry)!==savedCore,'Reopened production UI continues editing the same native core');
  await page.getByRole('button',{name:'完成',exact:true}).click();await idle();
  report.finalCore={sha256:digest(continued.geometry),revision:continued.revision,faces:continued.geometry.MachiningFaces.Faces.length};
  await collectHost();
  report.rich3d=await verifyBoundaryRich3d({page,partId:fixture.partEntityId,get,check,output});
  await reopen();
  check(digest((await get(fixture.partEntityId)).geometry)===report.rich3d.sha256,
    'Production rich 3D edited core persists through actual disk save and fresh project reopen');

  // A second native part exercises the actual unfolded two-dimensional page.
  const sidePayload={profileRef:{scope:'system',id:'rect'},parameters,length:400,quantity:1,name:'核心二维编辑方管',
    sketch:{schema:'icax.tube-sketch',schemaVersion:1,kind:'side',unit:'mm',coordinateSpace:'arc-length-axial',
      length:400,faceHeight:280,perimeter:280,trajectoryWidth:1,
      entities:[{id:'core-side-circle',kind:'circle',cx:40,cy:125,radius:8,closed:true}]}};
  report.sideCreationPayload=sidePayload;save();
  const side=await invoke('AddNestingSideSketchPart',sidePayload);
  const sideBefore=await get(side.partEntityId,{authoringView:'side'});
  check(boundary(sideBefore)&&sideBefore.authoringView?.mode==='side','Actual two-dimensional creation publishes the same boundary-core contract');
  const sideCircle=sideBefore.authoringView.sketch.entities.find(entity=>entity.kind==='circle');assert(sideCircle);
  await reopen();
  await page.evaluate(installBoundaryAcceptanceHost,side.partEntityId);
  await page.evaluate(id=>window.__boundaryAcceptance.host.dispatch('tube-designer-part-open-sketch',{dataset:{tubeDesignerPartId:id}}),side.partEntityId);
  await page.locator('[data-tube-sketch-canvas]').waitFor();await idle();
  await page.locator('[data-sketch-command="sketch.select"]').click();
  const position=await page.evaluate(({u,s})=>{
    const svg=document.querySelector('[data-tube-sketch-canvas]'),d=svg.dataset,p=svg.createSVGPoint();
    p.x=Number(d.sketchCanvasLeft)+(u-Number(d.sketchXMin))/(Number(d.sketchXMax)-Number(d.sketchXMin))*(Number(d.sketchCanvasRight)-Number(d.sketchCanvasLeft));
    p.y=Number(d.sketchCanvasBottom)-(s-Number(d.sketchYMin))/(Number(d.sketchYMax)-Number(d.sketchYMin))*(Number(d.sketchCanvasBottom)-Number(d.sketchCanvasTop));
    const screen=p.matrixTransform(svg.getScreenCTM());return {x:screen.x,y:screen.y};
  },{u:sideCircle.cx+sideCircle.radius,s:sideCircle.cy});
  await page.mouse.click(position.x,position.y);await idle();
  const diameter=page.locator('[data-sketch-field="diameter"]');await diameter.waitFor();
  await diameter.fill('18');await diameter.press('Tab');
  await page.locator('[data-cam-action="tube-designer-sketch-preview-side"]').click();
  await page.locator('[data-side-sketch-preview-ready=true]').waitFor();await idle();
  check(await viewError()==='','Original two-dimensional page previews a changed native cutout');
  const sideDraft=await get(side.partEntityId);
  check(sideDraft.revision===sideBefore.revision&&digest(sideDraft.geometry)===digest(sideBefore.geometry),'Two-dimensional native preview leaves committed core unchanged');
  await page.screenshot({path:join(output,'cef-core-2d-preview.png')});
  await page.locator('[data-sketch-command="sketch.commit"]').click();await idle();
  const sideAfter=await get(side.partEntityId,{authoringView:'side'});
  check(boundary(sideAfter)&&sideAfter.authoringView.sketch.entities.some(entity=>entity.kind==='circle'&&Math.abs(entity.radius-9)<1e-8)
    &&digest(sideAfter.geometry)!==digest(sideBefore.geometry),'Original two-dimensional UI applies geometry into the same authoritative core');
  const sideCore=digest(sideAfter.geometry);await reopen();
  const sideReopened=await get(side.partEntityId,{authoringView:'side'});
  check(digest(sideReopened.geometry)===sideCore&&sideReopened.authoringView.sketch.entities.some(entity=>
    entity.kind==='circle'&&Math.abs(entity.radius-9)<1e-8),
    'Two-dimensional edited core and editable sketch persist through real project close/reopen');
  report.sidePartEntityId=side.partEntityId;report.sideFinalCore={sha256:sideCore,revision:sideAfter.revision,faces:sideAfter.geometry.MachiningFaces.Faces.length};
  await page.evaluate(path=>window.__boundaryAcceptance.project.save(path),projectPath);
  check(report.pageErrors.length===0,'Actual CEF page has no script errors');
  report.passed=true;report.completedAt=new Date().toISOString();save();
}catch(error){report.error=error.stack;await collectHost().catch(()=>{});save();await page.screenshot({path:join(output,'cef-core-failure.png')}).catch(()=>{});throw error;}
finally{
  await collectHost().catch(()=>{});
  await page.evaluate(()=>{const f=window.__boundaryAcceptance;if(f?.host){f.host.dispose();f.host=null;}}).catch(()=>{});
  save();await browser.close();
}
