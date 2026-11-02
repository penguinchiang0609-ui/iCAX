import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

export async function verifyPathNumbers({page,rpc,output,standaloneCef=false}){
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  let before=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  if(!before.intentToolpaths.length){
    await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
    await command('five.tool.extract');
    const sources=await rpc({action:'invoke',scope:'scene',method:'CamCut.QuerySources',payload:{expected:before.expected,setupId:before.activeSetupId,placementId:before.placements[0].id}});
    const face=sources.faceUses.find(f=>f.localId==='31');assert.ok(face,'wall lamp top processing face');
    await page.evaluate(async face=>{const e=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs'),d=e.extractionDraft(fixture.view).extraction;d.mode='Faces';d.faces=[face];e.extractionChanged(fixture.view);},face);
    await command('five.extraction.run');await command('five.extraction.confirm');
    before=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  }
  assert.ok(before.intentToolpaths.length>=4,'use the saved wall lamp workpiece with actual paths');
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await page.locator('.five-cut-tree [data-five-intent]').first().click();
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,1]);fixture.view.viewport.fitView();});
  const checks=[];
  for(const tool of ['path-translate','path-rotate','extract','generate','adjust','manual-order']){
    await command('five.tool.'+tool);
    await page.waitForFunction(()=>!fixture.view.pending);
    assert.equal(await page.evaluate(()=>fixture.view.fiveAxisUI.tools.toolpath),tool==='generate'?'rapid':tool,'the requested tool must actually be active');
    await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
    const row=await page.evaluate(()=>{
      const v=fixture.view,labels=v.viewport.specificationLabels;
      return {count:labels.length,visible:labels.filter(l=>!l.element.hidden&&getComputedStyle(l.element).visibility==='visible'&&l.element.getBoundingClientRect().width>0).length,text:labels.map(l=>l.element.textContent),enabled:v.scene.intentToolpaths.filter(c=>c.enabled!==false&&c.machiningKind!=='Mark').length};
    });
    assert.ok(row.count>=row.enabled,`${tool}: all enabled cutting paths have numbers`);
    assert.ok(row.visible>0,`${tool}: numbers must actually be visible`);
    assert.ok(row.text.every(t=>/^\d+$/.test(t)),`${tool}: compact sequence captions`);
    checks.push({tool,...row});
    if(tool==='path-rotate')await page.screenshot({path:resolve(output,'rotate-numbers-visible.png')});
  }
  await command('five.display.numbers');
  await command('five.tool.path-rotate');
  await command('five.refresh');
  assert.equal(await page.locator('.icax-three-specification-label-layer').evaluate(n=>getComputedStyle(n).visibility),'hidden');
  await command('five.display.numbers');
  await page.waitForFunction(()=>[...document.querySelectorAll('.five-path-number')].some(n=>!n.hidden&&getComputedStyle(n).visibility==='visible'));
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));
  assert.equal(await page.locator('.five-path-number').count(),0,'CAM numbers do not leak into workpiece annotations');
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await command('five.tool.path-translate');
  await page.waitForFunction(()=>[...document.querySelectorAll('.five-path-number')].some(n=>!n.hidden));
  await command('five.tool.adjust');
  await page.evaluate(async()=>{
    const base=location.protocol==='file:'?new URL('../../../',location.href).href:'/src/';
    const [{updateCutPreview},{updateGeneratedPreview}]=await Promise.all([import(base+'apps/five-axis-cam/webpage/cutPreview.mjs'),import(base+'apps/five-axis-cam/webpage/toolpathGeneration.mjs')]);
    const v=fixture.view,layers=Object.values(v.scene.domain.layers.catalog),visibility=layers.map(l=>l.visible);
    try{
      layers.forEach(l=>l.visible=false);updateCutPreview(v);updateGeneratedPreview(v);
      if(v.viewport.specificationLabels.length)throw Error('Hiding every layer must remove all path numbers');
    }finally{layers.forEach((l,i)=>l.visible=visibility[i]);updateCutPreview(v);updateGeneratedPreview(v);}
    if(!v.viewport.specificationLabels.length)throw Error('Restoring visible layers must restore path numbers');
  });
  await command('five.tool.path-rotate');
  const after=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  for(const field of ['workpieces','placements','intentToolpaths'])assert.deepEqual(after[field],before[field],`tool changes and number toggle preserve native ${field}`);
  await rpc({action:'export-project',path:resolve(output,'wall-lamp-numbers.i5cam')});
  await page.screenshot({path:resolve(output,'accepted.png')});
  writeFileSync(resolve(output,'path-number-tools-report.json'),JSON.stringify({passed:true,checks,hideRefreshShow:true,pageSwitch:true,emptyVisibleLayers:true,nativeDefinitionsUnchanged:true,standaloneCef},null,2));
}
