import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyRapids({page,rpc,calls,output}){
  console.log('Rapid: selectable travel policies, per-segment editing and fixture collision');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'),geom=s=>s.setups.find(t=>t.id===s.activeSetupId).generatedGeometry;
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const button=name=>page.locator(`[data-five-command="five.rapid.${name}"]`).last();
  const field=name=>page.locator(`[data-five-rapid-field="${name}"]`).last();
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveRapid?.running);
  const preview=async strategy=>{await field('strategy').selectOption(strategy);await button('preview').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');const result=await page.evaluate(()=>fixture.view.fiveRapid.preview);console.log('Rapid preview:',strategy,result.result.validation,result.result.reason);return result;};
  const wait=async task=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await invoke('CamRapid.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  const positions=[];
  for(const tool of ['auto-order','manual-order','split','clear','path-hole','collision']){
    await command(`five.tool.${tool}`);
    const dialog=page.locator('.five-floating-dialog').last();assert.equal(await dialog.isVisible(),true,tool);
    const position=await dialog.evaluate(n=>{const r=n.getBoundingClientRect(),h=n.parentElement.getBoundingClientRect();return {left:r.left-h.left,top:r.top-h.top,rightPane:!!n.closest('.cam-info-pane')};});
    assert.equal(position.rightPane,false,tool);assert.ok(Math.abs(position.left-16)<2&&Math.abs(position.top-64)<2,JSON.stringify({tool,...position}));positions.push({tool,...position});
  }
  await command('five.tool.settings');assert.equal(await page.locator('.five-floating-dialog[aria-label="空移策略"]').isVisible(),true);
  await command('five.tool.rapid');assert.equal(await page.getByRole('region',{name:'空移属性'}).isVisible(),true);
  for(const strategy of ['Fly','Frog','Quick']){
    await command('five.tool.settings');await field('strategy').selectOption(strategy);await button('save-settings').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
    await command('five.tool.settings');assert.equal(await field('strategy').inputValue(),strategy);await button('close').click();
  }
    const generateState=await read();await wait(await invoke('CamRapid.Generate',{commandId:crypto.randomUUID(),expected:generateState.expected,setupId:generateState.activeSetupId,geometryRevision:geom(generateState).revision}));
  const complete=geom(await read());assert.equal(complete.transitions.filter(t=>t.validation==='Clear').length,complete.transitions.length);
  assert.ok(complete.transitions.every(t=>t.strategy==='Quick'&&t.policy.strategy==='Quick'));console.log('All rapid connections:',complete.transitions.length);await command('five.refresh');
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await command('five.tool.rapid');assert.equal(await page.getByRole('dialog',{name:'空移路径编辑'}).isVisible(),true);
  assert.equal(await field('strategy').count(),1);
  const initial=await preview('Quick');assert.equal(initial.result.scope,'BuiltinHeadEnvelope');assert.notEqual(initial.result.reason,'TRAVEL_ENVELOPE_REQUIRED');
  const beforeCancel=geom(await read());await button('close').click();assert.deepEqual(geom(await read()),beforeCancel);
  // Configure a deliberately synthetic conservative envelope, not a real machine.
  await command('five.tool.settings');await page.locator('[data-five-key="rapid-envelope"] summary').click();
  await field('envelopeRadiusMm').fill('1');await field('envelopeLengthMm').fill('10');await button('save-settings').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');await command('five.tool.rapid');
  const source=geom(await read()),available=source.transitions;
  const results=[];let chosen;
  for(const t of process.env.ICAX_REQUIRE_SURFACE_RAPID?available.toReversed():available){
    const start=source.operations.find(o=>o.intentId===t.from).end,end=source.operations.find(o=>o.intentId===t.to).start;
    if(Math.hypot(...start.map((v,i)=>v-end[i]))<5)continue;
    await field('connection').selectOption(`${t.from}/${t.to}`);const r=await preview('Quick');results.push({from:t.from,to:t.to,strategy:'Quick',...r.result});
    if(r.result.validation==='Clear'&&r.result.lengthMm>1){chosen=t;break;}
  }
  assert.ok(chosen,'at least one real automotive connection must pass the configured-envelope check');
  const quick=await page.evaluate(()=>fixture.view.fiveRapid.preview);
  assert.ok(quick.result.points.length>=4);assert.equal(quick.result.machineValidation,'Unchecked');assert.equal(quick.result.productionQualified,false);
  await page.screenshot({path:resolve(output,'automotive-rapid-quick.png')});
  for(const strategy of ['Fly','Frog','Quick']){const selected=await preview(strategy);assert.equal(selected.policy.strategy,strategy);}
  const original=geom(await read());
  await preview('Quick');assert.deepEqual(geom(await read()),original);
  console.log('Rapid: save, receipt retry and undo');
  // Acceptance and receipt both lost: recover the same immutable Apply command.
  await page.evaluate(()=>{fixture.loseMethod='CamRapid.Apply';const proxy=fixture.context.sceneProxy,original=proxy.invoke.bind(proxy);proxy.invoke=async(method,payload)=>{if(method==='CamScene.QueryCommand'){proxy.invoke=original;throw Error('Injected lost receipt');}return original(method,payload);};});
  const count=calls.filter(c=>c.method==='CamRapid.Apply').length;
  await button('apply').click();await idle();assert.match(await page.evaluate(()=>fixture.view.error),/尚未确认/);
  assert.equal(await button('apply').isEnabled(),true);await button('apply').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const retries=calls.filter(c=>c.method==='CamRapid.Apply').slice(count);assert.equal(retries.length,2);assert.deepEqual(retries[0].payload,retries[1].payload);
  const applied=geom(await read());assert.equal(applied.transitions.find(t=>t.from===chosen.from&&t.to===chosen.to).validation,'Clear');
  await rpc({action:'undo'});await command('five.refresh');assert.deepEqual(geom(await read()),original);
  await rpc({action:'redo'});await command('five.refresh');assert.deepEqual(geom(await read()),applied);
  // Real screen click on the raised rapid polyline selects the same segment.
  console.log('Rapid: screen picking');
  await command('five.tool.rapid');await field('connection').selectOption(`${chosen.from}/${chosen.to}`);await command('view.fit');await page.evaluate(()=>{const v=fixture.view.viewport,s=v.getCameraState();v.setCameraState({...s,radius:s.radius*3});});
  const pick=await page.evaluate(()=>{
    const v=fixture.view,g=v.fiveGeneratedPreview.group,canvas=v.viewport.renderer.domElement,r=canvas.getBoundingClientRect();
    for(const line of g.children.filter(o=>o.userData.generatedRapid&&o.material.color.getHex()===0x3583ff)){
      const a=line.geometry.attributes.position;for(let i=1;i<a.count;i++){const p=line.geometry.boundingSphere?.center?.clone()||v.viewport.camera.position.clone();p.set((a.getX(i-1)+a.getX(i))/2,(a.getY(i-1)+a.getY(i))/2,(a.getZ(i-1)+a.getZ(i))/2).project(v.viewport.camera);
        const x=r.x+(p.x+1)*r.width/2,y=r.y+(1-p.y)*r.height/2;if(x>625&&x<1380&&y>210&&y<820&&Math.abs(p.z)<1)return {x,y};}}
  });
  assert.ok(pick,'the selected automotive rapid must be visible for real mouse picking');
  const other=available.find(t=>t.from!==chosen.from||t.to!==chosen.to);assert.ok(other);await field('connection').selectOption(`${other.from}/${other.to}`);
  await page.mouse.click(pick.x,pick.y);assert.equal(await field('connection').inputValue(),`${chosen.from}/${chosen.to}`);
  // Preserve existing DOM, latest asynchronous scroll and focused text selection.
  console.log('Rapid: input and scroll preservation');
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-position-body::after{content:"";display:block;height:1000px}.five-position-body{height:170px;overflow:auto}.cam-info-pane{max-height:190px!important}'});
  await field('marginMm').fill('0.25');await page.evaluate(()=>{fixture.rapidCanvas=document.querySelector('canvas');fixture.rapidField=document.querySelector('[data-five-rapid-field="marginMm"]');fixture.rapidEvents=0;fixture.rapidField.addEventListener('input',()=>fixture.rapidEvents++);fixture.holdMethod='CamRapid.Preview';fixture.hold=true;});
  await button('preview').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const input=fixture.rapidField;input.focus({preventScroll:true});input.setSelectionRange(1,3);const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-position-body')];nodes.forEach((n,i)=>n.scrollTop=30+10*i);return nodes.map(n=>n.scrollTop);});assert.ok(scroll.every(n=>n>0));
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.rapidCanvas===document.querySelector('canvas')&&fixture.rapidField===document.querySelector('[data-five-rapid-field="marginMm"]')),true);
  assert.deepEqual(await page.evaluate(()=>[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane'),document.querySelector('.five-position-body')].map(n=>n.scrollTop)),scroll);
  assert.deepEqual(await field('marginMm').evaluate(n=>[document.activeElement===n,n.selectionStart,n.selectionEnd]),[true,1,3]);
  await stress.evaluate(n=>n.remove());await field('marginMm').fill('0.2');assert.ok(await page.evaluate(()=>fixture.rapidEvents>0));
  assert.equal(await button('apply').isDisabled(),true,'changing a parameter invalidates its preview');
  assert.equal(await field('strategy').count(),1);assert.equal(await field('liftHeightMm').count(),1);
  await page.evaluate(()=>{fixture.holdMethod='CamRapid.Preview';fixture.hold=true;});await button('preview').click();await page.waitForFunction(()=>fixture.held);
  await field('marginMm').fill('0.');await field('marginMm').evaluate(n=>n.setSelectionRange(2,2));await page.evaluate(()=>fixture.release());await page.waitForFunction(()=>!fixture.held);await idle();
  assert.equal(await field('marginMm').inputValue(),'0.');assert.equal(await page.evaluate(()=>fixture.view.fiveRapid.preview),null);assert.deepEqual(await field('marginMm').evaluate(n=>[document.activeElement===n,n.selectionStart,n.selectionEnd]),[true,2,2]);
  await field('marginMm').fill('0.2');
  await page.evaluate(()=>{fixture.holdMethod='CamRapid.Preview';fixture.hold=true;});await button('preview').click();await page.waitForFunction(()=>fixture.held);await button('close').click();await page.evaluate(()=>fixture.release());await idle();assert.deepEqual(geom(await read()),applied);
  // Introduce a real BRep fixture in the middle of the previously clear traverse.
  console.log('Rapid: fixture collision and reopen');
  const info=await rpc({action:'info'});let state=await read();const imported=await wait(await invoke('CamFixture.Import',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,sourcePath:info.fixturePath}));
  state=await read();const fixtureId=imported.result.fixtureId,p=quick.result.points[1].map((v,i)=>(v+quick.result.points[2][i])/2);
  const matrix=[1,0,0,p[0]-15,0,1,0,p[1]-15,0,0,1,p[2]-20,0,0,0,1];
  await invoke('CamFixture.Update',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,fixtureId,transform:matrix});
  assert.equal(geom(await read()).status,'Stale');state=await read();await wait(await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId}));
  await command('five.refresh');await command('five.tool.rapid');await field('connection').selectOption(`${chosen.from}/${chosen.to}`);
  const obstacle=await preview('Quick');results.push({strategy:'QuickWithFixture',...obstacle.result});
  // Safe replanning may raise above the new clamp; otherwise it must diagnose it.
  if(obstacle.result.validation==='Clear')assert.notDeepEqual(obstacle.result.points,quick.result.points);
  else assert.ok(['Collision','Unchecked'].includes(obstacle.result.validation));
  await page.screenshot({path:resolve(output,'automotive-rapid-fixture.png')});
  await button('apply').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const saved=await read(),reopened=await rpc({action:'save-reopen'});assert.deepEqual(geom(reopened),geom(saved));assert.deepEqual(reopened.fixtures,saved.fixtures);
  await rpc({action:'export-project',path:resolve(output,'Automotive-rapid-paths.i5cam')});
  writeFileSync(resolve(output,'rapid-report.json'),JSON.stringify({passed:true,positions,cases:['three selectable travel policies','default built-in head is checked without a manual envelope','settings and conditional fields','real automotive rapid envelope check','ordering estimate independent of actual travel policy','cancel preview leaves project unchanged','lost apply and receipt recover same command','transaction undo redo','screen rapid picking','canvas input listener and dual nested scroll preserved','focused selection preserved across async result','typing invalidates in-flight preview without losing decimal or caret','changed parameter invalidates preview','late cancelled preview ignored','fixture movement invalidates prior checks','fixture forces replan or diagnosis','embedded save reopen'],results,coverage:{native:true,browser:'Edge',standaloneCef:false,envelope:'built-in head plus additional radius 1 mm length 10 mm cylinder',machineMotion:false,realMachineQualified:false}},null,2));
}
