import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

export async function verifyPassiveTabs({page,rpc,calls,output}){
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const before=await read();
  await page.getByRole('toolbar',{name:'刀路管理'}).getByRole('button',{name:'新增',exact:true}).click();await idle();
  assert.equal(await page.getByRole('dialog',{name:'添加刀路'}).isVisible(),true);
  await page.getByRole('tab',{name:'空移',exact:true}).click();await idle();
  const start=calls.length;
  for(let i=0;i<3;i++){
    await page.getByRole('tab',{name:'切割',exact:true}).click();await idle();
    assert.equal(await page.getByRole('dialog').count(),0);
    assert.equal(await page.getByRole('toolbar',{name:'刀路管理'}).isVisible(),true);
    assert.equal(await page.locator('.cam-context-pane .five-fixture-heading').count(),0);
    await page.getByRole('tab',{name:'空移',exact:true}).click();await idle();
  }
  assert.equal(calls.slice(start).filter(c=>/CamCut\.(ExtractCandidates|Create)|CamPlan\.(SetOrder|Generate)|CamRapid\.Generate/.test(c.method||'')).length,0);
  await page.getByRole('tab',{name:'切割',exact:true}).click();await idle();assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);
  await page.getByRole('toolbar',{name:'刀路管理'}).getByRole('button',{name:'新增',exact:true}).click();await idle();assert.equal(await page.getByRole('dialog',{name:'添加刀路'}).isVisible(),true);
  await page.getByRole('tab',{name:'切割',exact:true}).click();await idle();assert.equal(await page.getByRole('dialog').count(),0);
  await page.screenshot({path:resolve(output,'passive-cutting-tab.png')});
  writeFileSync(resolve(output,'passive-tabs-report.json'),JSON.stringify({passed:true,cases:['cutting tab never replays previously used add action','repeated tab switches invoke no extraction creation sorting or generation','only explicit add opens the dialog','clicking cutting tab dismisses active extraction without creating cuts','redundant tree title and count row removed','actual model cuts unchanged'],coverage:{native:true,browser:'Edge',standaloneCef:false}},null,2));
}
