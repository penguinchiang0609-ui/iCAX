import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,readFileSync,writeFileSync,existsSync} from 'node:fs';
import {chromium} from 'playwright';
import {cutPolicyDraft,processDraftDefaults} from '../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';

const output=resolve(process.env.ICAX_DEMO_REPORT||'output/tests/bent-tube-demo'),demo=resolve(process.env.ICAX_DEMO_ROOT||'output/demos/bent-tube-setups-20261021'),runtime=resolve('output/tests/job-sequence/cef-runtime'),base=pathToFileURL(runtime+'/').href;
mkdirSync(output,{recursive:true});
const spec=JSON.parse(readFileSync(resolve(demo,'demo-spec.json'),'utf8'));
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
await page.setViewportSize({width:1600,height:950});page.setDefaultTimeout(60000);
const report={passed:false,standaloneCef:true,examples:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&!fixture.view.fiveSetupRequest);
const bind=()=>page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
const invoke=(method,payload={})=>page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000}),{method,payload});
const read=()=>invoke('CamScene.ReadState');
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const request=async(extra={})=>({commandId:crypto.randomUUID(),expected:(await read()).expected,...extra});
async function wait(task){
  for(let i=0;task.status==='Running'&&i<1800;i++){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}
  assert.equal(task.status,'Completed',JSON.stringify(task));return task;
}
function selectedCandidates(candidates,group){
  if(group.sculpture){
    const picked=candidates.filter(c=>c.confirmationEligibility==='Ready'&&c.boundary.previewPoints?.length&&c.boundary.previewPoints.every(p=>p[2]>group.minZ&&p[2]<group.maxZ&&Math.abs(Math.hypot(p[0]-group.axisX,p[1])-40)<1e-3));
    const span=(c,j)=>Math.max(...c.boundary.previewPoints.map(p=>p[j]))-Math.min(...c.boundary.previewPoints.map(p=>p[j]));
    const isTrim=c=>span(c,0)>70&&span(c,1)>70;
    const holes=picked.filter(c=>!isTrim(c)).sort((a,b)=>group.pattern==='geometric'?(span(a,2)>60)-(span(b,2)>60)||span(a,2)-span(b,2):Math.min(...a.boundary.previewPoints.map(p=>p[2]))-Math.min(...b.boundary.previewPoints.map(p=>p[2])));
    const trims=picked.filter(isTrim);assert.equal(trims.length,1,'One exact end-trim loop');assert.equal(holes.length,group.expectedPaths-1,'Expected actual aperture count');return [...holes,...trims];
  }
  if(group.radii)return group.centers.map((center,i)=>{
    const matches=candidates.filter(c=>c.confirmationEligibility==='Ready'&&c.geometryKind==='Circle'&&Math.abs(c.boundary.parameters.radiusMm-group.radii[i])<1e-5&&Math.hypot(...c.boundary.parameters.center.map((n,j)=>n-center[j]))<1e-5);
    assert.equal(matches.length,1,group.key+' circle '+i+' must resolve to one real face boundary');return matches[0];
  });
  const axis=group.side==='A'?1:2,along=group.side==='A'?2:0;
  const picked=[];
  for(const [index,target] of group.centers.entries()){
    const matches=candidates.filter(c=>{
      if(c.confirmationEligibility!=='Ready')return false;
      const points=c.boundary.previewPoints;if(!points?.length)return false;
      const center=[0,1,2].map(j=>(Math.min(...points.map(p=>p[j]))+Math.max(...points.map(p=>p[j])))/2);
      return points.every(p=>group.sign*(p[axis]-(axis===2?200:0))>27.5)
        &&Math.abs(center[along]-target[along])<.1&&Math.abs(center[group.side==='A'?0:1])<.1
        &&Math.abs((Math.max(...points.map(p=>p[along]))-Math.min(...points.map(p=>p[along])))-[12,16][index])<.2;
    });
    assert.equal(matches.length,1,group.key+' hole '+index+' must have exactly one exact outer-skin contour: '+JSON.stringify(matches));picked.push(matches[0]);
  }
  return picked;
}
async function extract(placement,group,catalog){
  const state=await read(),w=state.workpieces[0],s=state.setups.find(s=>s.id===placement.setupId),digest=crypto.randomUUID();
  const sources=await invoke('CamCut.QuerySources',{expected:state.expected,setupId:s.id,placementId:placement.id});
  const axis=group.side==='A'?[0,0,1]:[1,0,0];
  const supports=catalog.features.filter(f=>group.sculpture?f.mode==='axis'&&Math.abs(f.radius-40)<1e-6&&Math.abs(f.direction[2])>.9999&&Math.abs(f.point[0]-group.axisX)<1e-6:group.radii?f.mode==='plane'&&!f.derived&&Math.abs(f.point[2]-group.planeZ)<1e-6&&Math.abs(f.direction[2])>.9999:f.mode==='axis'&&Math.abs(f.radius-30)<1e-6&&Math.abs(f.direction.reduce((v,n,i)=>v+n*axis[i],0))>.9999).flatMap(f=>f.faceIds);
  assert.ok(supports.length,'Find actual support face from native geometry');
  const faceUses=sources.faceUses.filter(f=>supports.includes(f.localId));assert.ok(faceUses.length);
  const task=await wait(await invoke('CamCut.ExtractCandidates',await request({setupId:s.id,placementId:placement.id,sourceGeometry:w.geometryRef,extractionRuleRef:s.planningProfileRef,selection:{mode:'Faces',faceUses},geometryOnly:true,representation:'SkinSurface',workpieceFamily:'SheetSurface',side:{outwardSign:1},requestedKinds:['GeneralContour','Circle','Rectangle','Slot'],draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:digest})));
  const candidates=await invoke('CamCut.QueryCandidates',{epoch:task.epoch,candidateSet:task.result.candidateSet,pageSize:'100'});
  writeFileSync(resolve(output,group.key+'-'+spec.examples.indexOf(currentExample)+'-candidates.json'),JSON.stringify(candidates,null,2));
  return {task,candidates:candidates.candidates,digest};
}
let currentExample;
try{
  await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
  for(const example of spec.examples){
    currentExample=example;console.log('Build '+example.name);
    const workingPath=resolve(output,Date.now()+'-'+example.name+'.i5cam');
    await page.evaluate(({name,path})=>__icaxAppShell.createProject({projectName:name,projectPath:path,productId:'icax.five-axis-cam'}),{name:example.name,path:workingPath});await bind();await idle();
    await page.evaluate(path=>__icaxWorkbench.importWorkpiece(path),example.step);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
    let state=await read();assert.equal(state.workpieces.length,1);
    const first=state.placements[0],catalog=await invoke('CamScene.ReadSnapFeatures',{expected:state.expected,setupId:first.setupId,objectId:first.id});
    writeFileSync(resolve(output,'catalog-'+example.groups.length+'.json'),JSON.stringify(catalog,null,2));
    const built=[];
    for(const [i,group] of example.groups.entries()){
      console.log('  '+group.name);
      let p=first;
      if(i){const created=await invoke('CamSetup.Manage',await request({placementId:first.id,action:'create',name:group.name}));p=(await read()).placements.find(p=>p.id===created.placementId);}
      else await invoke('CamSetup.Manage',await request({placementId:p.id,action:'rename',name:group.name}));
      await invoke('CamPlacement.SetTransform',await request({setupId:p.setupId,placementId:p.id,transform:group.transform}));

      const extracted=await extract(p,group,catalog);
      if(process.env.ICAX_DEMO_INSPECT==='1'){writeFileSync(resolve(output,'inspect-state.json'),JSON.stringify(await read(),null,2));console.log('Inspection files written');process.exitCode=0;break;}
      let picked=selectedCandidates(extracted.candidates,group);
      const draft={...processDraftDefaults,cutProcessName:'弯管端部加工演示',cutMaterial:'碳钢',cutThickness:group.radii?'10':'3'};
      const policy=await invoke('CamProcess.Prepare',await request({setupId:p.setupId,placementId:p.id,...cutPolicyDraft(draft)}));
      const created=await invoke('CamCut.Create',await request({setupId:p.setupId,placementId:p.id,candidateSet:extracted.task.result.candidateSet,candidateIds:picked.map(c=>c.candidateId),draftDigest:extracted.digest,decisions:[],process:policy.process,pose:policy.pose}));
      let roundNumber=0,diamondNumber=0;
      for(const [j,id] of created.createdIntentIds.entries()){
        const c=picked[j],axialSpan=Math.max(...c.boundary.previewPoints.map(p=>p[2]))-Math.min(...c.boundary.previewPoints.map(p=>p[2]));
        const label=group.sculpture?(group.pattern==='geometric'?(j===picked.length-1?'B端 · 斜切口修边':axialSpan>60?'B端 · 菱形窗 '+(++diamondNumber):'B端 · 圆孔 '+(++roundNumber)+'（Ø'+[12,16,20][roundNumber-1]+'）'):(j===picked.length-1?'A端 · 波浪口连续修边':'A端 · 渐变叶形镂空 '+(j+1))):group.labels?.[j]||group.side+'端'+(group.sign>0?'正侧':'反侧')+' Ø'+[12,16][j]+'孔';
        await invoke('CamCut.Rename',await request({intentId:id,name:label}));
        if(!group.followNormals)await invoke('CamCut.SetToolAxis',await request({intentId:id,mode:'Fixed',frame:'Workpiece',direction:group.direction,scope:'WholePath'}));
      }
      let orderedIntentIds=[...created.createdIntentIds];
      if(group.pattern==='geometric'){
        const axes=[];for(const id of orderedIntentIds)axes.push((await invoke('CamCut.ReadToolAxis',{expected:(await read()).expected,intentId:id})).direction);
        let best=null,bestCost=Infinity;
        function arrange(prefix,remaining){
          if(remaining.length){for(const i of remaining)arrange([...prefix,i],remaining.filter(j=>j!==i));return;}
          const path=[...prefix,axes.length-1];let cost=0;
          for(let i=1;i<path.length;i++){const dot=axes[path[i-1]].reduce((v,n,j)=>v+n*axes[path[i]][j],0);if(dot<-.995)return;cost+=Math.acos(Math.min(1,Math.max(-1,dot)));}
          if(cost<bestCost){best=path;bestCost=cost;}
        }
        arrange([],axes.slice(0,-1).map((_,i)=>i));assert.ok(best,'Find a hole-first order without directly opposed tool axes');
        orderedIntentIds=best.map(i=>orderedIntentIds[i]);picked=best.map(i=>picked[i]);
      }
      if(group.sculpture)await invoke('CamPlan.SetOrder',await request({setupId:p.setupId,mode:'Manual',intentIds:orderedIntentIds}));
      if(group.radii||group.sculpture){const settings=(await read()).setups.find(s=>s.id===p.setupId).rapidSettings;await invoke('CamRapid.Configure',await request({setupId:p.setupId,settings:{...settings,strategy:'Frog',liftHeightMm:40}}));}
      await wait(await invoke('CamPlan.Generate',await request({setupId:p.setupId})));
      let setup=(await read()).setups.find(s=>s.id===p.setupId);
      await wait(await invoke('CamRapid.Generate',await request({setupId:p.setupId,geometryRevision:setup.generatedGeometry.revision})));
      setup=(await read()).setups.find(s=>s.id===p.setupId);
      assert.equal(setup.generatedGeometry.status,'Current');assert.equal(setup.generatedGeometry.operations.length,picked.length);
      assert.equal(setup.generatedGeometry.transitions.length,picked.length-1);assert.equal(setup.generatedGeometry.transitions.every(t=>t.geometryStatus==='Generated'),true,JSON.stringify(setup.generatedGeometry.transitions.map(t=>({from:t.from,to:t.to,status:t.geometryStatus,reason:t.reason,validation:t.validation}))));
      // Select real, distinct CAD features for a demonstrative registration.
      // Machine points below are synthetic; no physical measurement is claimed.
      const features=catalog.features.filter(f=>['endpoint','center'].includes(f.mode)&&Array.isArray(f.point));
      const locating=[];
      for(const f of features){
        if(locating.length===1&&Math.hypot(...f.point.map((v,j)=>v-locating[0].point[j]))<10)continue;
        if(locating.length===2){const a=f.point.map((v,j)=>v-locating[0].point[j]),b=locating[1].point.map((v,j)=>v-locating[0].point[j]);if(Math.hypot(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])<100)continue;}
        locating.push(f);if(locating.length===3)break;
      }
      assert.equal(locating.length,3);
      const queries=locating.map(f=>({frame:'Workpiece',mode:f.mode,featureId:f.featureId}));
      const modelPlan=await invoke('CamSetup.SetRegistrationPlan',await request({placementId:p.id,queries}));
      const measuredPoints=modelPlan.points.map(p=>p.point.map((v,j)=>v+[500,300,0][j]));
      const plan=await invoke('CamSetup.SetRegistrationPlan',await request({placementId:p.id,queries,measuredPoints,maximumResidualMm:.01}));assert.equal(plan.machineStatus,'Qualified');
      built.push({key:group.key,name:group.name,setupId:p.setupId,placementId:p.id,transform:group.transform,intentIds:orderedIntentIds,contours:picked.map(c=>({id:c.candidateId,sourceKey:c.sourceKey})),syntheticMeasurement:true});
    }
    if(process.env.ICAX_DEMO_INSPECT==='1')break;
    await invoke('CamSetup.SetSequence',await request({setupIds:built.map(s=>s.setupId)}));
    state=await read();assert.equal(state.setups.length,example.groups.length);assert.equal(state.intentToolpaths.length,example.groups.reduce((n,g)=>n+(g.expectedPaths||g.radii?.length||2),0));
    const program=await invoke('CamSetup.ExportProgram',{expected:state.expected,destination:example.program});assert.equal(program.machineReady,false);
    const text=readFileSync(example.program,'utf8');assert.equal(text.split('\nM00\n').length-1,example.groups.length-1);assert.equal(text.split('\nFROM/').length-1,example.groups.length);
    await command('five.refresh');await command('five.area.workpiece');
    await command('five.setup.activate:'+first.id);await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,1]);fixture.view.viewport.fitView(1.2);});
    await page.screenshot({path:resolve(demo,example.groups.length+'-setup-tree.png')});
    await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},example.project);
    await command('app.saveAs');assert.ok(existsSync(example.project));
    writeFileSync(resolve(output,'expected-'+example.groups.length+'.json'),JSON.stringify({project:example.project,state:await read(),built},null,2));
    report.examples.push({name:example.name,project:example.project,program:example.program,setups:built});
  }
  if(process.env.ICAX_DEMO_INSPECT!=='1')report.passed=true;
  assert.deepEqual(report.errors,[]);
}catch(error){report.error=error.stack;process.exitCode=1;writeFileSync(resolve(output,'failure-state.json'),JSON.stringify(await read(),null,2));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'build-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
