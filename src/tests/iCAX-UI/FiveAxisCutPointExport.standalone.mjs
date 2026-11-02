import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync,existsSync,copyFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {verifyExportInteraction} from './FiveAxisCutPointExport.interactions.mjs';

const {chromium}=createRequire(import.meta.url)(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/cutpoint-export-20261026/standalone');
mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9548');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
page.setDefaultTimeout(45000);
const resume=process.env.ICAX_EXPORT_RESUME==='1';
const report=resume?JSON.parse(readFileSync(resolve(output,'report.json'),'utf8')):{standaloneCef:true,cases:[]};report.passed=false;report.errors=[];delete report.error;page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest&&!!fixture.view.scene?.domain,null,{timeout:180000});
const invoke=(method,payload={})=>page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000}),{method,payload});
const read=()=>invoke('CamScene.ReadState');
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
const generate=async setupId=>{
  let task=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:(await read()).expected,setupId});
  while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}
  assert.equal(task.status,'Completed',task.error);await command('five.refresh');
};
function parseProgram(text){
  const setups=[];let setup,path,axis=null,pending=null,axisUpdates=0;
  for(const line of text.trim().split(/\r?\n/)){
    if(line.startsWith('$$ SETUP ')){setup={id:line.split(' ')[3],paths:[]};setups.push(setup);axis=null;}
    if(line.startsWith('$$ PATH ')){path={id:line.split(' ')[3],rows:[],curves:[]};setup.paths.push(path);}
    if(line.startsWith('CURVE/')){assert.equal(pending,null);pending=JSON.parse(line.slice(6));assert.equal(pending.schema,'icax.cl-curve/1');assert.ok(pending.range.last>pending.range.first);}
    if(line.startsWith('FROM/')||line.startsWith('GOTO/')){
      const n=line.slice(5).split(',').map(Number);assert.ok(n.length===3||n.length===6);assert.ok(n.every(Number.isFinite));
      if(n.length===6){axis=n.slice(3);axisUpdates++;}assert.ok(axis,'the first row of each setup must establish IJK');
      assert.ok(Math.abs(Math.hypot(...axis)-1)<1e-9);path.rows.push([...n.slice(0,3),...axis]);
      if(pending){path.curves.push({record:pending,end:n.slice(0,3)});pending=null;}
    }
  }
  assert.equal(pending,null);return {setups,axisUpdates};
}
function verifyProgram(path,expected,denseText=null){
  const text=readFileSync(path,'utf8'),parsed=parseProgram(text),blocks=text.split('\nM00\n');
  assert.ok(text.includes('REFERENCE/CUTPOINT'));assert.ok(!text.includes('\nRAPID\n'));assert.ok(!text.includes('FRAME/MACHINE'));
  assert.equal(blocks.length-1,expected.length-1);assert.equal(parsed.setups.length,expected.length);
  let points=0,curves=0,sourcePoints=0;
  const near=(a,b)=>a.forEach((n,j)=>assert.ok(Math.abs(n-b[j])<1e-7,`${n} != ${b[j]}`));
  const dense=denseText?parseProgram(denseText):null;
  parsed.setups.forEach((setup,i)=>{
    assert.equal(setup.id,expected[i].id);assert.equal(setup.paths.length,expected[i].paths.length);
    assert.ok(blocks[i].indexOf('FRAME/SETUP')<blocks[i].indexOf('\nFROM/'));
    setup.paths.forEach((path,j)=>{
      const source=expected[i].paths[j];assert.equal(path.id,source.id);sourcePoints+=source.rows.length;points+=path.rows.length;curves+=path.curves.length;
      if(dense){const rows=dense.setups[i].paths[j].rows;assert.equal(rows.length,source.rows.length);rows.forEach((row,k)=>near(row,source.rows[k]));}
      if(!path.curves.length){assert.equal(path.rows.length,source.rows.length);path.rows.forEach((row,k)=>near(row,source.rows[k]));}
      else {
        source.rows.forEach(row=>near(row.slice(3),path.rows[0].slice(3)));near(path.rows[0],source.rows[0]);near(path.rows.at(-1),source.rows.at(-1));
        let at=0;for(const row of path.rows.slice(1)){const found=source.rows.findIndex((p,k)=>k>=at&&p.every((n,l)=>Math.abs(n-row[l])<1e-7));assert.ok(found>=0,'exact curve endpoints must remain original CAM endpoints');at=found;}
        for(const c of path.curves){const g=c.record.geometry;assert.ok(g.kind);if(g.poles)assert.ok(g.poles.every(p=>p.length===3&&p.every(Number.isFinite)));if(g.knots){assert.equal(g.knots.length,g.multiplicities.length);assert.ok(g.degree>0);}if(g.kind==='Circle'||g.kind==='Arc')assert.ok(g.radius>0);}
      }
    });
  });
  return {points,sourcePoints,curves,axisUpdates:parsed.axisUpdates,paths:expected.reduce((n,s)=>n+s.paths.length,0),setups:expected.length,m00:expected.length-1,text};
}
const expected=()=>page.evaluate(()=>{
  const d=fixture.view.scene.domain;
  return d.setups.filter(s=>s.enabled!==false&&d.placements.some(p=>p.setupId===s.id&&d.workpieces.some(w=>w.id===p.workpieceId&&w.enabled!==false))).map(s=>({id:s.id,paths:s.generatedGeometry.operations.map(op=>{
    const m=d.placements.find(p=>p.id===op.placementId).transform;
    const apply=(p,vector)=>[0,1,2].map(i=>m[i*4]*p[0]+m[i*4+1]*p[1]+m[i*4+2]*p[2]+(vector?0:m[i*4+3]));
    return {id:op.intentId,rows:op.parts.flatMap(part=>part.points.map((p,i)=>{const axis=apply(part.directions[i],true),length=Math.hypot(...axis);return [...apply(p,false),...axis.map(n=>n/length)];}))};
  })}));
});
const exportUI=async name=>{
  const path=resolve(output,name);await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},path);
  await command('five.job.open');const button=page.getByRole('button',{name:'输出切割点 XYZ/IJK',exact:true});assert.equal(await button.isEnabled(),true);
  await button.click();await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveJobError),'');
  const referencePath=path.replace(/\.cl$/,'-采样参照.cl');
  await invoke('CamSetup.ExportProgram',{expected:(await read()).expected,destination:referencePath,coordinateFrame:'Setup',referencePoint:'CutPoint',curveMode:'Sampled'});
  return {path,...verifyProgram(path,await expected(),readFileSync(referencePath,'utf8'))};
};
try{
  await page.waitForFunction(()=>__icaxAppShell.getState().startupPhase==='ready');
  let state,first,one;
  if(!resume){
  const sourceProject=resolve(process.env.ICAX_EXPORT_PROJECT||resolve(output,'切割点输出验收.i5cam'));
  const project=resolve(output,'切割点输出实测-'+Date.now()+'.i5cam');copyFileSync(sourceProject,project);if(existsSync(sourceProject+'.log'))copyFileSync(sourceProject+'.log',project+'.log');
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.waitForFunction(path=>__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/').toLowerCase()===path.replaceAll('\\','/').toLowerCase()&&__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId,project,{timeout:120000});
  await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};});await idle();
  console.log('Loaded real wall-lamp model');
  state=await read();first=state.placements[0];
  await invoke('CamCut.SetToolAxis',{commandId:crypto.randomUUID(),expected:state.expected,intentId:state.intentToolpaths[0].id,mode:'Fixed',frame:'Workpiece',direction:[0,0,1],scope:'WholePath'});
  await generate(first.setupId);await command('five.area.toolpath');
  one=await exportUI('单工序-XYZ-IJK.cl');report.single={...one,text:undefined};
  console.log('Fixed-axis export:',JSON.stringify(report.single));
  report.cases.push('Real CAM export preserves exact fixed-axis curves and modal IJK; its dense-reference output matches every original generated point and unit axis without follow-height offsets');
  assert.ok(one.curves>0);assert.ok(one.points<one.sourcePoints);assert.equal(one.axisUpdates,1);
  }else{await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};});await idle();state=await read();first=state.placements[0];one=report.single;}

  const baseline=resolve(process.env.ICAX_MULTI_EXPORT_PROJECT||resolve(output,'two-setup-baseline.i5cam'));
  const baselineStart=Date.now();while(!existsSync(baseline)){if(Date.now()-baselineStart>240000)throw Error('Native two-setup baseline is missing');await new Promise(r=>setTimeout(r,500));}
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),baseline);
  await page.waitForFunction(path=>__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/').toLowerCase()===path.replaceAll('\\','/').toLowerCase()&&__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId,baseline,{timeout:120000});
  await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');fixture.view=getProjectView(__icaxAppShell.getState().activeProjectId);});await idle();
  await command('five.refresh');await command('five.area.toolpath');state=await read();
  console.log('Loaded native two-setup model');
  const second={setupId:state.setups[0].id,placementId:state.placements.find(p=>p.setupId===state.setups[0].id).id};
  const original=state.placements.find(p=>p.setupId===state.setups[1].id);
  const varying=state.intentToolpaths.find(c=>c.setupId===second.setupId&&c.cutType==='Contour');
  // Closed contours share the physical start/end anchor. Use an interior
  // native reference sample so a second endpoint edit cannot replace it.
  const axisSamples=await invoke('CamCut.ReadToolAxisSamples',{expected:(await read()).expected,intentIds:[varying.id]});
  const samples=axisSamples.cuts[0].samples,interior=samples[Math.floor(samples.length/2)];
  assert.ok(samples.length>3&&interior.anchor);
  const originalAxis=interior.direction,perturbed=[originalAxis[0]+.15,originalAxis[1]+.07,originalAxis[2]];
  await invoke('CamCut.EditToolAxisPoint',{commandId:crypto.randomUUID(),expected:(await read()).expected,intentId:varying.id,pointId:crypto.randomUUID(),anchor:interior.anchor,action:'Upsert',direction:perturbed,frame:'Workpiece',transition:'ArcLength'});
  console.log('Authored interior native axis control');
  await generate(varying.setupId);
  console.log('Generated varying-axis motion');
  const variable=await exportUI('变化刀轴-模态IJK.cl');report.variable={...variable,text:undefined};
  console.log('Varying-axis export:',JSON.stringify(report.variable));
  const changedPath=parseProgram(variable.text).setups.flatMap(s=>s.paths).find(p=>p.id===varying.id);assert.equal(changedPath.curves.length,0);assert.ok(new Set(changedPath.rows.map(p=>p.slice(3).map(n=>n.toFixed(8)).join(','))).size>2);assert.ok(variable.axisUpdates>variable.setups);
  report.cases.push('A real authored axis keypoint changes the CAM pose field; varying-axis paths keep every CAM interpolation sample and update modal IJK whenever the direction changes');
  const two=await exportUI('双工序-M00-XYZ-IJK.cl');report.multiple={...two,text:undefined};
  assert.ok(two.text.indexOf(second.setupId)<two.text.indexOf(original.setupId));
  await page.screenshot({path:resolve(output,'cutpoint-export-dialog.png')});
  report.cases.push('Copied setup uses a real 90-degree rotated/translated placement; all six columns match independent transforms; saved setup order has exactly one M00 and no inter-setup or inter-path move');
  // Hold delivery of the real native export receipt while the user continues
  // to type and scroll. Geometry and output remain entirely native.
  report.interaction=await verifyExportInteraction({page,command,idle});
  report.cases.push('Real asynchronous export receipt retains the canvas, setup-name input/listener/focus/selection and latest left/right/nested scroll positions');
  await command('five.job.close');state=await read();
  await invoke('CamSetup.Manage',{commandId:crypto.randomUUID(),expected:state.expected,placementId:second.placementId,action:'disable'});await command('five.refresh');
  const disabled=await exportUI('禁用第二工序-XYZ-IJK.cl');assert.equal(disabled.setups,1);assert.equal(disabled.m00,0);report.disabled={...disabled,text:undefined};
  report.cases.push('Disabled setups are omitted and create no stray M00');
  const stale=(await read()).expected,cut=(await read()).intentToolpaths.find(c=>c.setupId===original.setupId);
  await invoke('CamCut.Reverse',{commandId:crypto.randomUUID(),expected:stale,intentIds:[cut.id]});
  await assert.rejects(()=>invoke('CamSetup.ExportProgram',{expected:stale,destination:disabled.path,coordinateFrame:'Setup',referencePoint:'CutPoint'}));
  await assert.rejects(async()=>invoke('CamSetup.ExportProgram',{expected:(await read()).expected,destination:disabled.path,coordinateFrame:'Setup',referencePoint:'CutPoint'}),/NOT_READY/);
  assert.equal(readFileSync(disabled.path,'utf8'),disabled.text);
  report.cases.push('Stale revisions and stale cutting geometry are rejected without overwriting an existing export');
  assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,cases:report.cases,error:report.error,errors:report.errors}));await browser.close();}
