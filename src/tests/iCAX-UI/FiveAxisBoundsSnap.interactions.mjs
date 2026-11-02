import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {frameAlignmentDriver} from './FiveAxisFrameAlignment.native.browser.mjs';

// Shared pointer acceptance used by both the native browser fixture and the
// actual standalone CEF AppShell. No features or alignment results are mocked.
export async function verifyBoundsSnapping({page,base='/src/',read,output,beforeApply=null}){
  const driver=await frameAlignmentDriver(page,base),before=await read(),cases=[];
  for(const kind of ['OBB']){
    await driver.open();
    const catalogue=await page.evaluate(()=>fixture.view.sceneSnapping.catalogs.get(fixture.view.scene.activePlacementId));
    const features=catalogue.features.filter(f=>f.boxKind===kind);
    assert.ok(!catalogue.features.some(f=>f.boxKind==='AABB'));
    assert.equal(features.filter(f=>f.mode==='plane').length,6);assert.equal(features.filter(f=>f.mode==='line').length,12);assert.equal(features.filter(f=>f.mode==='endpoint').length,8);
    assert.ok(features.filter(f=>f.mode==='plane').every(f=>f.planeCorners.length===4&&f.faceIds.length===0));
    const plane=await driver.pick('plane',{prefix:`bounds:${kind}:face:`,helperType:'plane'});
    assert.equal(plane.boxKind,kind);assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.stage),'normal');
    await driver.action('next');
    const normal=await driver.pick('direction',{prefix:`normal:bounds:${kind}:`,construction:'PlaneNormal',helperType:'direction'});assert.equal(normal.boxKind,kind);
    await driver.action('reselect');
    const direction=await driver.pick('line',{prefix:`bounds:${kind}:edge:`,helperType:'line'});
    assert.equal(direction.boxKind,kind);assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.error),'');
    await driver.action('next');
    for(const [mode,prefix] of [['datum-point',`bounds:${kind}:face-center:`],['midpoint',`bounds:${kind}:midpoint:`],['endpoint',`bounds:${kind}:corner:`]]){
      const picked=await driver.pick(mode,{prefix});assert.equal(picked.boxKind,kind);
      assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.origin?.sourceResult.featureId),picked.featureId);
      await driver.action('reselect');
    }
    const center=await driver.pick('datum-point',{prefix:`bounds:${kind}:center`});assert.equal(center.boxKind,kind);
    const result=await page.evaluate(()=>({valid:fixture.view.fiveAlignment.valid,transform:fixture.view.fiveAlignment.report.transform,error:fixture.view.fiveAlignment.error}));
    assert.equal(result.valid,true);assert.equal(result.error,'');
    const map=(p,vector=false)=>[0,1,2].map(i=>(vector?0:result.transform[i*4+3])+p.reduce((s,n,j)=>s+n*result.transform[i*4+j],0));
    map(plane.direction,true).forEach((n,i)=>assert.ok(Math.abs(n-[0,0,1][i])<1e-6));
    const actualOrigin=await page.evaluate(()=>fixture.view.fiveAlignment.origin?.sourceResult);
    map(center.point).forEach(n=>assert.ok(Math.abs(n)<1e-5,JSON.stringify({kind,center,actualOrigin,result,mapped:map(center.point)})));
    await page.screenshot({path:resolve(output,`${kind.toLowerCase()}-alignment.png`)});
    const state=await read();assert.deepEqual(state.workpieces,before.workpieces);assert.deepEqual(state.placements,before.placements);
    cases.push('OBB only: actual pointer picks a face, another face normal, an edge, a face centre, edge midpoint, corner and the whole box centre; preview is read-only');
    await beforeApply?.({page,base,driver,read,output});
    await driver.action('apply');
  }
  const after=await read();assert.notDeepEqual(after.workpieces,before.workpieces);
  const p=after.placements.find(p=>p.setupId===after.activeSetupId);assert.deepEqual(p.transform,[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]);
  assert.equal(await page.getByRole('dialog',{name:'自身坐标系重置',exact:true}).count(),0);
  cases.push('OBB frame reset commits the real geometry, leaves identity placement and closes the editor');
  return cases;
}
