const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const{chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=path.resolve(process.env.ICAX_AUTOSAVE_OUTPUT||'output/tests/machine-simulation');
const mode=process.env.ICAX_REOPEN_MODE||'manual';
(async()=>{
 const b=await chromium.connectOverCDP(process.env.ICAX_CEF_DEBUG_URL||'http://127.0.0.1:9483'),p=b.contexts()[0].pages()[0];p.setDefaultTimeout(60000);
 const expected=JSON.parse(fs.readFileSync(path.join(output,`rapid-policy-${mode}.json`))),file=mode==='manual'?expected.path:path.join(output,'rapid-policy-automatic.i5cam');
 await p.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await p.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),file);
 await p.waitForFunction(async()=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.tv=getProjectView(__icaxAppShell.getState().activeProjectId);return tv?.scene?.domain?.setups.some(s=>s.generatedGeometry?.transitions?.length>1)&&!tv.pending;});
 await p.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));
 await p.waitForFunction(()=>tv.fiveAxisArea==='toolpath'&&!tv.pending);
 await p.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
 await p.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.tool.rapid'));
 await p.waitForFunction(()=>tv.fiveRapid?.open&&tv.fiveAxisUI.tools.toolpath==='rapid');
 const actual=await p.evaluate(()=>tv.scene.domain.setups[0].generatedGeometry.transitions.map(t=>({from:t.from,to:t.to,policy:t.policy,manualPath:t.manualPath,points:t.points,directions:t.directions,geometryStatus:t.geometryStatus,validation:t.validation})));
 assert.equal(JSON.stringify(actual),JSON.stringify(expected.transitions));
 const field=n=>p.locator(`[data-five-rapid-field="${n}"]`).last();
 assert.equal(await field('strategy').inputValue(),actual[0].policy.strategy);
 assert.equal(await field('marginMm').inputValue(),String(actual[0].policy.marginMm));
 const rendered=await p.evaluate(()=>({lines:tv.fiveGeneratedPreview.group.children.filter(o=>o.userData.generatedRapid).map(o=>[...o.geometry.attributes.position.array]),native:tv.scene.domain.setups[0].generatedGeometry.transitions.filter(t=>['Draft','Generated'].includes(t.geometryStatus)&&t.points?.length).map(t=>t.points.flat())}));
 assert.equal(rendered.lines.length,rendered.native.length);rendered.lines.forEach((line,i)=>{assert.equal(line.length,rendered.native[i].length);line.forEach((v,j)=>assert.ok(Math.abs(v-rendered.native[i][j])<1e-4));});
 const idle=()=>p.waitForFunction(()=>!tv.fiveHistoryBusy&&!tv.fiveRapid.savingPath&&!tv.fiveRapid.pathEdit?.changed&&!tv.fiveRapid.saveTimer);
 if(mode==='manual'){
  await p.locator('[data-five-command="five.rapid.path-edit"]').click();await p.waitForFunction(()=>tv.fiveRapid.pathEdit);
  assert.equal(await field('pathZ').inputValue(),'39');
  await field('pathZ').fill('40');await idle();
  await p.getByRole('button',{name:'撤销',exact:true}).click();await p.waitForFunction(()=>!tv.fiveHistoryBusy&&tv.fiveRapid.pathEdit?.values[2]==='39');
  const historyBefore=await p.evaluate(()=>tv.viewportSceneProxy.getUndoRedoState());assert.equal(historyBefore.canRedo,true);
  await field('pathZ').fill('abc');await p.getByRole('button',{name:'重做',exact:true}).click();await p.waitForFunction(()=>!tv.fiveHistoryBusy&&tv.fiveRapid.pathEdit?.values[2]==='40');
  assert.equal(await field('pathZ').inputValue(),'40');assert.equal(await p.evaluate(()=>tv.error),'');
  await p.getByRole('button',{name:'撤销',exact:true}).click();await p.waitForFunction(()=>!tv.fiveHistoryBusy&&tv.fiveRapid.pathEdit?.values[2]==='39');
  console.log('PASS direct native redo with invalid text after a committed coordinate undo');
 }else{assert.equal(actual[0].manualPath,undefined);assert.equal(await p.locator('[data-five-command="five.rapid.path-automatic"]').count(),0);}
 await p.evaluate(()=>__icaxAppShell.executeRibbonCommand('view.fit'));
 await p.screenshot({path:path.join(output,`rapid-policy-${mode}-reopened.png`)});
 fs.writeFileSync(path.join(output,`rapid-policy-${mode}-reopen-report.json`),JSON.stringify({passed:true,freshProcess:true,mode,nativeAndRenderedParameters:true,renderedGeometry:true,directRedoWithInvalidInput:mode==='manual'},null,2));
 console.log('PASS fresh-process reopen',mode,'parameters and 3D geometry');process.exit();
})().catch(e=>{console.error(e);process.exit(1)});
