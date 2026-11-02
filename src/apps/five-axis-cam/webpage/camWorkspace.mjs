import {workpiecePlacements,workpieceSelectionBusy} from './workpieceSelection.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {workspaceState,activeTool} from './workspaceModel.mjs';
import {commandAvailability} from './commandAvailability.mjs';
import {button} from './workspaceControls.mjs';
import {workpieceThumbnailPreview} from './workpieceThumbnails.mjs';
export const CAM_PAGES=Object.freeze([['cutting','切割刀路','cam-cut-path'],['rapid','空移刀路','cam-rapid-path'],['process','工艺','parameters'],['order','排序','sort']]);
export const camPage=view=>CAM_PAGES.some(([id])=>id===view.fiveCamPage)?view.fiveCamPage:'cutting';
export function camPageForTool(view,tool){
  if(['simulation','collision'].includes(tool))return camPage(view);
  if(['generate','rapid','settings'].includes(tool))return 'rapid';
  if(['manual-order','auto-order','order'].includes(tool))return 'order';
  if(['process','leads','overcut','compensation','clear','side','path-start','reverse'].includes(tool))return 'process';
  return 'cutting';
}
export function camRibbonWorkspace(view,area){
  if(!['toolpath','simulation'].includes(area))return null;
  const domain=view.scene?.domain,p=selectedPlacement(view),workpieces=workpiecePlacements(view),busy=workpieceSelectionBusy(view);
  const action=(id,title,iconName)=>({id,title,iconName,...(commandAvailability(view,id)||{})});
  return {
    label:area==='toolpath'?'CAM':'孪生',selectorsLayout:'stacked',collapsibleSelectors:true,
    selectors:[
      {id:'five-workpiece',label:'工件',value:p?.workpieceId||'',disabled:busy,visual:true,options:workpieces.map(x=>{const w=domain.workpieces.find(w=>w.id===x.workpieceId);return {value:x.workpieceId,label:w?.name||'未命名工件',commandId:'five.cam-context.workpiece:'+x.id,...workpieceThumbnailPreview(view,w)};})},
      {id:'five-pose',label:'位姿',value:p?.id||'',disabled:busy,options:(domain?.placements||[]).filter(x=>x.workpieceId===p?.workpieceId&&x.setupId).map(x=>{const s=domain.setups.find(s=>s.id===x.setupId);return {value:x.id,label:s?.name||'未命名位姿',disabled:s?.enabled===false,commandId:'five.cam-context.pose:'+x.id};})},
    ],
    actions:[action('five.simulation.head-open','切割头设置','machine'),action('five.job.open','工序与输出','export')],
    pages:area==='toolpath'?CAM_PAGES.map(([id,title,iconName])=>({id:'five.cam-page.'+id,title,iconName,active:camPage(view)===id,disabled:busy})):[],
    checks:area==='toolpath'?[action('five.tool.simulation','轨迹预览','simulate'),action('five.simulation.collision-check','碰撞与限位检测','collision')]:[],
  };
}
export function renderCamInspectorTabs(view){
  const chosen=view.fiveCamInspector==='collision'||activeTool(view)==='keypoint'?'collision':'properties';
  const tabs=[['properties','属性'],['collision','碰撞与限位']];
  return `<div class="five-cam-inspector-tabs" data-five-key="cam-inspector-tabs" role="tablist" aria-label="刀路参数分区">${tabs.map(([id,label])=>button('five.cam-inspector.'+id,label,`role="tab" aria-selected="${chosen===id}" ${view.pending?'disabled':''}`)).join('')}</div>`;
}
export function renderProcessTabs(view){
  const tab=view.fiveCamProcessTab||'parameters';
  return `<div class="five-cam-inspector-tabs" data-five-key="cam-process-tabs" role="tablist" aria-label="工艺参数分区">${[['parameters','切割参数'],['geometry','引线与补偿'],['commands','前后指令']].map(([id,label])=>button('five.cam-process.'+id,label,`role="tab" aria-selected="${tab===id}" ${view.pending?'disabled':''}`)).join('')}</div>`;
}
