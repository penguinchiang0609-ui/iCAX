export async function startQuietOperationProbe(page){
  await page.evaluate(()=>{
    const selector='.five-blocking-progress,.five-blocking-operation,.cam-progress-backdrop,.progress-overlay';
    fixture.blockingSamples=[];
    const inspect=()=>{if(fixture.view.pending||fixture.view.sceneRefreshPending||document.querySelector(selector))fixture.blockingSamples.push({pending:fixture.view.pending,refresh:fixture.view.sceneRefreshPending,overlay:!!document.querySelector(selector)});};
    fixture.blockingObserver=new MutationObserver(records=>{for(const record of records)for(const node of record.addedNodes)if(node.nodeType===1&&(node.matches(selector)||node.querySelector(selector)))fixture.blockingSamples.push({addedOverlay:true});inspect();});
    fixture.blockingObserver.observe(document.body,{childList:true,subtree:true,attributes:true});fixture.blockingInterval=setInterval(inspect,10);inspect();
  });
}
export async function stopQuietOperationProbe(page){
  return page.evaluate(()=>{fixture.blockingObserver.disconnect();clearInterval(fixture.blockingInterval);return fixture.blockingSamples;});
}
