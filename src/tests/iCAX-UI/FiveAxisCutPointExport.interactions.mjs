import assert from 'node:assert/strict';

export async function verifyExportInteraction({page,command,idle}){
  await command('five.job.close');
  await command('five.setup.edit-name');
  await page.addStyleTag({content:'.cam-context-pane{height:200px!important;overflow:auto!important}.cam-info-pane{height:240px!important;overflow:auto!important}.five-job-dialog .five-position-body{max-height:100px;overflow:auto}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1600px}'});
  await command('five.job.open');
  await page.evaluate(()=>{
    const proxy=fixture.view.viewportSceneProxy;fixture.originalInvoke=proxy.invoke.bind(proxy);
    proxy.invoke=async(method,...args)=>{const result=await fixture.originalInvoke(method,...args);if(method==='CamSetup.ExportProgram')await new Promise(r=>fixture.releaseExport=r);return result;};
    window.exportPending=__icaxAppShell.executeRibbonCommand('five.job.export');
  });
  await page.waitForFunction(()=>!!fixture.releaseExport);
  const interaction=await page.evaluate(()=>{
    const input=document.querySelector('.cam-context-pane [data-five-setup-name]');
    if(!input)throw Error('A real setup-name input must be present for focus/selection verification');
    fixture.kept={input,canvas:fixture.view.viewport.renderer.domElement};fixture.probe=0;
    input.addEventListener('export-probe',()=>fixture.probe++);input.focus({preventScroll:true});input.setSelectionRange(0,Math.min(2,input.value.length));
    return {selection:[input.selectionStart,input.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.five-job-dialog .five-position-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=30+i*13;return n.scrollTop;})};
  });
  await page.evaluate(async()=>{fixture.releaseExport();await exportPending;fixture.view.viewportSceneProxy.invoke=fixture.originalInvoke;});await idle();
  const retained=await page.evaluate(()=>{const k=fixture.kept;k.input.dispatchEvent(new Event('export-probe'));return {canvas:k.canvas===fixture.view.viewport.renderer.domElement,input:k.input.isConnected,focus:document.activeElement===k.input,selection:[k.input.selectionStart,k.input.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.five-job-dialog .five-position-body'].map(s=>document.querySelector(s).scrollTop),events:fixture.probe,error:fixture.view.fiveJobError};});
  assert.equal(retained.error,'');assert.ok(retained.canvas&&retained.input&&retained.focus);assert.equal(retained.events,1);assert.deepEqual(retained.scroll,interaction.scroll);assert.deepEqual(retained.selection,interaction.selection);
  await command('five.job.close');await command('five.setup.cancel-name');
  return retained;
}
