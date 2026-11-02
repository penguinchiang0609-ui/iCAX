import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {installBoundaryAcceptanceHost} from './TubeBoundaryCore.cefHost.mjs';

export async function verifyBoundaryRich3d({page,partId,get,check,output}) {
  const digest=value=>createHash('sha256').update(JSON.stringify(value)).digest('hex');
  const original=await get(partId,{authoringView:'drawing'});
  await page.evaluate(installBoundaryAcceptanceHost,partId);
  await page.evaluate(id=>__boundaryAcceptance.host.dispatch('tube-designer-drawing-open',{
    dataset:{tubeDesignerPartId:id}}),partId);
  const ready=()=>page.waitForFunction(()=>{
    const f=__boundaryAcceptance,s=f.view.tubeDesignerPartDrawing?.state;
    return s?.error||s?.previewRenderError||f.host.failures.length||(s&&!f.host.pending&&!f.view.pending
      &&!s.previewPending&&!s.previewRenderPending&&s.preview?.revision===s.revision
      &&document.querySelector('[data-part-drawing-preview-ready=true]'));
  },null,{timeout:180000});
  const ui=()=>page.evaluate(()=>{const f=__boundaryAcceptance,s=f.view.tubeDesignerPartDrawing?.state;
    return s?.error||s?.previewRenderError||f.host.failures[0]?.error||'';});
  await ready();check(await ui()==='','Production rich 3D page loads the native boundary core');
  const rendered=await page.evaluate(()=>{
    const h=__boundaryAcceptance.host,v=h.viewport,canvas=v?.renderer?.domElement;
    return {meshCount:v?.geometryObjects?.size,vertices:[...(v?.geometryObjects?.values()??[])].reduce((n,g)=>n+(g.getAttribute?.('position')?.count??0),0),
      width:canvas?.clientWidth,height:canvas?.clientHeight};
  });
  check(rendered.vertices>0&&rendered.width>0&&rendered.height>0,'Production rich 3D displays actual native GPU resources',rendered);
  const feature=await page.evaluate(()=>{
    const s=__boundaryAcceptance.view.tubeDesignerPartDrawing.state;
    return s.features.map(x=>({id:x.id,cadNodeId:x.cadNodeId,station:x.station,type:x.type})).find(x=>x.type==='circle');
  });
  check(!!feature,'Production rich 3D recovers the authored circular cut');
  await page.locator(`[data-drawing-node="${feature.id}"]`).click();
  const station=page.locator('[data-tube-designer-punch-field="station"]');
  await station.fill(String(feature.station+8));await station.press('Tab');await ready();
  check(await ui()==='','Production rich 3D position control previews the edited core');
  const draft=await get(partId);
  check(draft.revision===original.revision&&digest(draft.geometry)===digest(original.geometry),
    'Production rich 3D native preview leaves the committed core unchanged');
  await page.evaluate(()=>{const v=__boundaryAcceptance.host.viewport;v.setStandardView('top-front');v.fitViewToViewport(1.15);});
  await page.screenshot({path:join(output,'cef-core-rich-3d-preview.png')});
  await page.locator('[data-cam-action="tube-designer-drawing-apply"]').click();
  await page.waitForFunction(()=>!__boundaryAcceptance.host.pending&&!__boundaryAcceptance.view.pending);
  const edited=await get(partId,{authoringView:'drawing'});
  check(digest(edited.geometry)!==digest(original.geometry)&&edited.authoringView.features.some(x=>Math.abs(x.station-feature.station-8)<1e-8),
    'Production rich 3D applies the changed position to the same authoritative core');
  return {sha256:digest(edited.geometry),revision:edited.revision,featureId:feature.id,station:feature.station+8,rendered};
}
