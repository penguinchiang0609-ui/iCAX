import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

export async function verifyRapidPages({page,rpc,calls,output}){
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'),setup=s=>s.setups.find(t=>t.id===s.activeSetupId);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveRapid?.running&&!fixture.view.fiveRapidBatch?.running);
  const tab=name=>page.getByRole('tab',{name,exact:true});
  const right=page.locator('.cam-info-pane'),toolbar=page.getByRole('toolbar',{name:'空移管理'});
  const editor=page.locator('[data-five-key="rapid-dialog"]');
  const stableGeometry=g=>{const {sampleGeometryRef,...stable}=g;return stable;};
  const field=name=>editor.locator(`[data-five-rapid-field="${name}"]`),button=name=>editor.locator(`[data-five-command="five.rapid.${name}"]`).last();
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  console.log('Rapid pages: tabs, native batch generation and settings');
  await tab('切割').click();assert.equal(await page.getByRole('toolbar',{name:'刀路管理'}).isVisible(),true);
  await tab('空移').click();assert.equal(await tab('空移').getAttribute('aria-selected'),'true');
  assert.deepEqual(await toolbar.getByRole('button').evaluateAll(ns=>ns.map(n=>n.getAttribute('aria-label'))),['空移策略参数','更新刀路与空移']);
  assert.equal(await toolbar.locator('svg').count(),2);for(const b of await toolbar.getByRole('button').all()){assert.equal(await b.getAttribute('title'),await b.getAttribute('aria-label'));await b.hover();}
  assert.equal(await page.getByRole('toolbar',{name:'刀路管理'}).count(),0);assert.equal(await right.locator('.five-cut-parameters').count(),0);
  const ribbon=await page.evaluate(async()=>{const {getRibbonDefinition}=await import('/src/apps/five-axis-cam/webpage/ribbon.mjs');return getRibbonDefinition(fixture.context);});
  assert.ok(!ribbon.tabs.flatMap(t=>t.groups.flatMap(g=>g.commands)).some(c=>['five.tool.generate','five.tool.settings','five.tool.rapid'].includes(c.id)));
  if(process.env.ICAX_RAPID_PARAMETERS_ONLY){
    for(const strategy of ['Fly','Frog','Quick']){await field('strategy').selectOption(strategy);assert.equal(await field('marginMm').count(),1);assert.equal(await field('liftHeightMm').count(),strategy==='Fly'?0:1);}
    const initialHeight=await field('liftHeightMm').inputValue();
    await button('preview').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
    const baseline=await page.evaluate(()=>fixture.view.fiveRapid.preview.result);
    await field('liftHeightMm').fill(String(Number(initialHeight)+30));assert.equal(await button('apply').isDisabled(),true);
    await button('preview').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
    const raised=await page.evaluate(()=>fixture.view.fiveRapid.preview.result);assert.equal(raised.geometryStatus,'Generated');assert.equal(raised.validation,'Unchecked');assert.notDeepEqual(raised.points,baseline.points);
    await button('apply').click();await idle();assert.equal(await field('liftHeightMm').inputValue(),String(Number(initialHeight)+30));
    const saved=await read(),reopened=await rpc({action:'save-reopen'});assert.deepEqual(stableGeometry(setup(reopened).generatedGeometry),stableGeometry(setup(saved).generatedGeometry));
    assert.deepEqual(await right.getByRole('button').allTextContents(),['预览','保存']);
    await toolbar.getByRole('button',{name:'空移策略参数',exact:true}).click();
    const pos=await editor.evaluate(n=>{const r=n.getBoundingClientRect(),h=n.parentElement.getBoundingClientRect();return [r.left-h.left,r.top-h.top,!!n.closest('.cam-info-pane')];});assert.deepEqual(pos,[16,64,false]);
    assert.equal(await page.getByRole('textbox',{name:'跟随高度',exact:true}).count(),1);
    assert.equal(await field('liftHeightMm').isVisible(),true);
    await page.screenshot({path:resolve(output,'facam-aligned-parameters.png')});
    writeFileSync(resolve(output,'rapid-parameter-detail-report.json'),JSON.stringify({passed:true,cases:['all three strategies expose local collision margin','Quick safety height and Frog lift visible; Fly hides unused height','right properties have preview and save, no window close or cancel','two vector tools have accessible native tooltips','global parameter dialog is in viewport upper left','default lift is visible in main global parameters'],coverage:{native:true,browser:'Edge',standaloneCef:false}},null,2));return;
  }
  await toolbar.getByRole('button',{name:'更新刀路与空移',exact:true}).click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const state=await read(),transitions=setup(state).generatedGeometry.transitions;assert.equal(transitions.length,18);assert.equal(await page.locator('[data-five-rapid-id]').count(),18);
  assert.ok(transitions.every(t=>t.geometryStatus==='Generated'&&t.validation==='Unchecked'));assert.equal(setup(state).generatedGeometry.rapidClearCount,'0');assert.match(await page.locator('[data-five-rapid-id]').first().innerText(),/已生成.*待碰撞检测/);
  assert.equal(await right.getByRole('region',{name:'空移属性'}).isVisible(),true);assert.equal(await right.getByRole('button',{name:/关闭/}).count(),0);
  await toolbar.getByRole('combobox',{name:'当前空移策略'}).selectOption('Frog');await idle();assert.equal(setup(await read()).rapidSettings.strategy,'Frog');
  await toolbar.getByRole('button',{name:'空移策略参数',exact:true}).click();assert.equal(await page.getByRole('dialog',{name:'空移策略',exact:true}).isVisible(),true);
  const pos=await editor.evaluate(n=>{const r=n.getBoundingClientRect(),h=n.parentElement.getBoundingClientRect();return [r.left-h.left,r.top-h.top,!!n.closest('.cam-info-pane')];});assert.deepEqual(pos,[16,64,false]);
  await field('strategy').selectOption('Frog');await button('save-settings').click();await idle();assert.equal(await field('strategy').inputValue(),'Frog');await button('close').click();
  const selected=transitions[2],pair=`${selected.from}/${selected.to}`;
  console.log('Rapid pages: select, edit and regenerate one segment');
  await page.locator(`[data-five-rapid-id="${pair}"]`).click();assert.equal(await field('connection').inputValue(),pair);
  assert.equal(await page.locator(`[data-five-rapid-id="${pair}"]`).getAttribute('aria-pressed'),'true');
  await field('strategy').selectOption('Quick');await field('marginMm').fill('0.35');
  const before=setup(await read()).generatedGeometry.transitions;
  await button('preview').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');await button('apply').click();await idle();
  assert.equal(await field('connection').inputValue(),pair);assert.equal(await field('strategy').inputValue(),'Quick');assert.equal(await field('marginMm').inputValue(),'0.35');
  const edited=setup(await read()).generatedGeometry.transitions;assert.equal(edited.find(t=>`${t.from}/${t.to}`===pair).policy.marginMm,0.35);
  assert.deepEqual(edited.filter(t=>`${t.from}/${t.to}`!==pair).map(t=>t.policy),before.filter(t=>`${t.from}/${t.to}`!==pair).map(t=>t.policy));
  const start=calls.length;await button('preview').click();await idle();await button('apply').click();await idle();
  const recalc=calls.slice(start);assert.equal(recalc.filter(c=>c.method==='CamRapid.Preview').length,1);assert.equal(recalc.filter(c=>c.method==='CamRapid.Apply').length,1);assert.equal(recalc.filter(c=>c.method==='CamRapid.Generate'||c.method==='CamPlan.Generate').length,0);
  assert.equal(await field('connection').inputValue(),pair);
  await page.evaluate(()=>{const v=fixture.view.viewport;v.fitView();const s=v.getCameraState();v.setCameraState({...s,radius:s.radius*1.5});});
  const pick=await page.evaluate(async()=>{const {sceneColors}=await import('/src/apps/five-axis-cam/webpage/scenePalette.mjs');const v=fixture.view.viewport,r=v.renderer.domElement.getBoundingClientRect();for(const line of fixture.view.fiveGeneratedPreview.group.children.filter(o=>o.userData.generatedRapid&&o.material.color.getHex()===sceneColors.selected)){
    const a=line.geometry.attributes.position;for(let i=1;i<a.count;i++){const p=v.camera.position.clone().set((a.getX(i-1)+a.getX(i))/2,(a.getY(i-1)+a.getY(i))/2,(a.getZ(i-1)+a.getZ(i))/2).project(v.camera),x=r.x+(p.x+1)*r.width/2,y=r.y+(1-p.y)*r.height/2;if(x>r.left+20&&x<r.right-20&&y>r.top+90&&y<r.bottom-40&&Math.abs(p.z)<1)return {x,y};}}});
  assert.ok(pick,'selected rapid must be visible for actual viewport picking');
  const other=transitions[4];await page.locator(`[data-five-rapid-id="${other.from}/${other.to}"]`).click();await page.mouse.click(pick.x,pick.y);assert.equal(await field('connection').inputValue(),pair);
  console.log('Rapid pages: viewport picking and latest input/scroll preservation');
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-rapid-body::after{content:"";display:block;height:1600px}.cam-info-pane{max-height:260px!important}.five-rapid-body{height:180px;overflow:auto}'});
  await page.evaluate(()=>{fixture.pageInput=document.querySelector('[data-five-rapid-field="marginMm"]');fixture.pageCanvas=document.querySelector('canvas');fixture.holdMethod='CamRapid.Preview';fixture.hold=true;fixture.pageEvents=0;fixture.pageInput.addEventListener('input',()=>fixture.pageEvents++);});
  await button('preview').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{fixture.pageInput.focus({preventScroll:true});fixture.pageInput.setSelectionRange(1,3);return ['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=40+i*20;return n.scrollTop;});});assert.ok(scroll.every(n=>n>0));
  await page.evaluate(()=>fixture.release());await idle();
  {const kept=await page.evaluate(()=>({canvas:fixture.pageCanvas===document.querySelector('canvas'),input:fixture.pageInput===document.querySelector('[data-five-rapid-field="marginMm"]'),focus:document.activeElement===fixture.pageInput,active:document.activeElement?.outerHTML?.slice(0,180)}));assert.ok(kept.canvas&&kept.input&&kept.focus,JSON.stringify(kept));}
  assert.deepEqual(await field('marginMm').evaluate(n=>[n.selectionStart,n.selectionEnd]),[1,3]);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(s=>document.querySelector(s).scrollTop)),scroll);
  await stress.evaluate(n=>n.remove());await field('marginMm').fill('0.4');assert.ok(await page.evaluate(()=>fixture.pageEvents>0));assert.equal(await button('apply').isDisabled(),true);
  await button('preview').click();await idle();
  const resourceStress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-rapid-body::after{content:"";display:block;height:1600px}.cam-info-pane{max-height:260px!important}.five-rapid-body{height:180px;overflow:auto}'});
  await page.evaluate(()=>{fixture.holdPathResource=true;});await button('apply').click();await page.waitForFunction(()=>fixture.heldPathResource);
  const resourceScroll=await page.evaluate(()=>{fixture.pageInput.focus({preventScroll:true});fixture.pageInput.setSelectionRange(1,3);return ['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=80+i*20;return n.scrollTop;});});assert.ok(resourceScroll.every(n=>n>0));
  await page.evaluate(()=>fixture.releasePathResource());await idle();
  {const kept=await page.evaluate(()=>({canvas:fixture.pageCanvas===document.querySelector('canvas'),input:fixture.pageInput===document.querySelector('[data-five-rapid-field="marginMm"]'),focus:document.activeElement===fixture.pageInput,active:document.activeElement?.outerHTML?.slice(0,180)}));assert.ok(kept.canvas&&kept.input&&kept.focus,JSON.stringify(kept));}
  assert.deepEqual(await field('marginMm').evaluate(n=>[n.selectionStart,n.selectionEnd]),[1,3]);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(s=>document.querySelector(s).scrollTop)),resourceScroll);
  await resourceStress.evaluate(n=>n.remove());
  await tab('切割').click();assert.equal(await page.locator('[data-five-intent]').count(),19);await tab('空移').click();assert.equal(await page.locator('[data-five-rapid-id]').count(),18);assert.equal(await field('connection').inputValue(),pair);
  const saved=await read(),reopened=await rpc({action:'save-reopen'});assert.deepEqual(stableGeometry(setup(reopened).generatedGeometry),stableGeometry(setup(saved).generatedGeometry));assert.deepEqual(setup(reopened).rapidSettings,setup(saved).rapidSettings);
  await page.screenshot({path:resolve(output,'rapid-page.png')});
  writeFileSync(resolve(output,'rapid-pages-report.json'),JSON.stringify({passed:true,cuttingPaths:19,rapidSegments:18,cases:['cutting and rapid tabs','two compact SVG tools with native hover tooltips and current-strategy selector','real native batch generation','persistent right-side properties without close or cancel buttons','global strategy selector persists without generating; parameter dialog opens at viewport upper left','selected segment preview and save preserve selection','property preview and save touch only selected segment','other segment policies unchanged','canvas input listener focus selection and latest dual sidebar nested scroll retained','typing invalidates preview','tab switching preserves project geometry','save reopen preserves strategies and paths'],coverage:{native:true,browser:'Edge',standaloneCef:false}},null,2));
}
