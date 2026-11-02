import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {captureBentTubeEnds} from './fixtures/bentTubeDemoPreview.mjs';

const output=resolve(process.env.ICAX_DEMO_REPORT||'output/tests/bent-tube-demo'),demo=resolve(process.env.ICAX_DEMO_ROOT||'output/demos/bent-tube-setups-20261021'),base=pathToFileURL(resolve('output/tests/job-sequence/cef-runtime')+'/').href;
mkdirSync(output,{recursive:true});
const spec=JSON.parse(readFileSync(resolve(demo,'demo-spec.json'),'utf8'));
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
await page.setViewportSize({width:1600,height:950});page.setDefaultTimeout(60000);
const report={passed:false,standaloneCef:true,freshProcessReopen:true,realMousePlayback:true,examples:[],errors:[],scope:'Geometry, independent setups, toolpaths, local rapids, synthetic machine registration, sequential trajectory preview and neutral CL export. No real-machine or fixture qualification.'};page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveWorkpieceSwitching&&!fixture.view.fiveSimulation?.request&&(!fixture.view.scene?.model?.isLoaded||fixture.view.viewport?.sceneObjects.get(fixture.view.scene.activePlacementId)?.geometry));
const bind=()=>page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
try{
  await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
  for(const example of spec.examples){
    const count=example.groups.length,expected=JSON.parse(readFileSync(resolve(output,'expected-'+count+'.json'),'utf8'));
    await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),example.project);await bind();await idle();
    let state=await read();assert.equal(state.workpieces.length,1);assert.equal(state.setups.length,count);assert.equal(state.intentToolpaths.length,example.groups.reduce((n,g)=>n+(g.expectedPaths||g.radii?.length||2),0));
    assert.deepEqual(state.workpieces,expected.state.workpieces);assert.deepEqual(state.placements,expected.state.placements);
    assert.deepEqual(state.setups.map(s=>s.registrationPlan),expected.state.setups.map(s=>s.registrationPlan));
    assert.deepEqual(state.intentToolpaths,expected.state.intentToolpaths);
    for(const built of expected.built)built.intentIds.forEach((id,j)=>assert.equal(state.intentToolpaths.find(c=>c.id===id).sourceKey,built.contours[j].sourceKey));
    assert.equal(new Set(state.placements.map(p=>JSON.stringify(p.transform))).size,count);
    for(const [i,setup] of state.setups.entries()){
      const paths=example.groups[i].expectedPaths||example.groups[i].radii?.length||2;
      assert.equal(setup.name,example.groups[i].name);assert.equal(setup.generatedGeometry.status,'Current');assert.equal(setup.generatedGeometry.operations.length,paths);
      assert.equal(setup.generatedGeometry.transitions.length,paths-1);assert.equal(setup.generatedGeometry.transitions.every(t=>t.geometryStatus==='Generated'),true);
      if(example.groups[i].sculpture)assert.deepEqual(setup.generatedGeometry.operations.map(op=>op.intentId),expected.built[i].intentIds,'Leaf apertures precede final rim trimming');
      const wanted=example.groups[i].direction;
      for(const op of setup.generatedGeometry.operations){const axis=await page.evaluate(payload=>fixture.view.viewportSceneProxy.invoke('CamCut.ReadToolAxis',payload),{expected:state.expected,intentId:op.intentId});if(example.groups[i].followNormals)assert.equal(axis.mode,'SurfaceNormal');else axis.direction.forEach((v,j)=>assert.ok(Math.abs(v-wanted[j])<1e-8));}
    }
    await command('five.area.toolpath');await command('five.job.open');
    assert.equal(await page.getByRole('dialog',{name:'装夹工序与输出'}).isVisible(),true);
    assert.equal(await page.getByRole('button',{name:'按工序顺序仿真',exact:true}).isEnabled(),true);assert.equal(await page.getByRole('button',{name:'统一输出刀位文件',exact:true}).isEnabled(),true);
    await page.screenshot({path:resolve(demo,count+'-setup-sequence.png')});
    await page.getByRole('button',{name:'按工序顺序仿真',exact:true}).click();
    const poses=[];
    for(let i=0;i<count;i++){
      await page.waitForFunction(i=>fixture.view.fiveJobPlayback?.index===i&&fixture.view.fiveSimulation?.frames?.length>0&&!fixture.view.pending,i);await command('five.simulation.pause');
      state=await read();assert.equal(state.activeSetupId,expected.built[i].setupId);
      const playback=await page.evaluate(()=>({ids:[...new Set(fixture.view.fiveSimulation.frames.filter(f=>f.laser).map(f=>f.intentId))],axes:fixture.view.fiveSimulation.frames.map(f=>f.direction.toArray()),cuts:fixture.view.fiveSimulation.frames.filter(f=>f.target&&f.laser).map(f=>({point:f.target.toArray(),axis:f.direction.toArray()})),frames:fixture.view.fiveSimulation.frames.length}));
      assert.deepEqual(new Set(playback.ids),new Set(expected.built[i].intentIds));
      if(example.groups[i].sculpture){const m=example.groups[i].transform;for(const f of playback.cuts){const p=[0,1,2].map(j=>[0,1,2].reduce((v,k)=>v+m[k*4+j]*(f.point[k]-m[k*4+3]),0)),normal=[p[0]-example.groups[i].axisX,p[1],0],n=Math.hypot(...normal),axis=[0,1,2].map(j=>[0,1,2].reduce((v,k)=>v+m[j*4+k]*normal[k]/n,0));assert.ok(axis.reduce((v,n,j)=>v+n*f.axis[j],0)>.9999);}}
      else for(const axis of playback.axes)axis.forEach((v,j)=>assert.ok(Math.abs(v-[0,0,1][j])<1e-8));
      await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,1]);fixture.view.viewport.fitView(1.2);});
      await page.screenshot({path:resolve(demo,count+'-setup-'+(i+1)+'.png')});
      const seek=page.locator('[data-five-simulation-field="seek"]');await seek.fill(String(playback.frames-2));await seek.dispatchEvent('input');await command('five.simulation.next');
      await page.waitForFunction(()=>fixture.view.fiveJobPlayback?.boundary);assert.equal((await read()).activeSetupId,expected.built[i].setupId);
      assert.match(await page.locator('[data-five-key="job-playback"]').textContent(),i<count-1?/M00/:/全部工序预览结束/);
      poses.push({name:example.groups[i].name,setupId:state.activeSetupId,frames:playback.frames,independentPaths:playback.ids.length,boundary:i<count-1?'M00':'FINI'});
      if(i<count-1){if(i===0)await page.screenshot({path:resolve(demo,count+'-setup-reclamp.png')});await page.getByRole('button',{name:'确认换装夹，继续预览',exact:true}).click();}
    }
    await command('five.job.stop');await command('five.area.workpiece');await command('five.setup.activate:'+expected.built[0].placementId);
    await command('five.area.toolpath');await command('five.job.open');
    // Save the example in a useful initial UI state with the first setup active.
    await command('app.save');
    if(example.groups[0].pattern==='leaves'&&example.groups[1].pattern==='geometric')await captureBentTubeEnds(page,base,demo,example.groups);
    report.examples.push({name:example.name,project:example.project,setups:count,totalPaths:state.intentToolpaths.length,sequenceVerified:poses,registration:'synthetic demo coordinates',machineReady:false});
  }
  assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'verify-failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'acceptance.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
