import {workpiecePlacements,workpieceSelectionBusy} from './workpieceSelection.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {workspaceState,activeTool} from './workspaceModel.mjs';
import {commandAvailability} from './commandAvailability.mjs';
import {button} from './workspaceControls.mjs';
import {workpieceThumbnailPreview} from './workpieceThumbnails.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
export const CAM_PAGES=Object.freeze([['cutting','切割刀路','cam-cut-path'],['rapid','空移刀路','cam-rapid-path'],['process','工艺','parameters'],['order','排序','sort'],['collision','碰撞与超限','collision'],['simulation','仿真','simulate'],['head','切割头','machine']]);
export const camPage=view=>CAM_PAGES.some(([id])=>id===view.fiveCamPage)?view.fiveCamPage:'cutting';
export function camPageForTool(view,tool){
  if(tool==='simulation')return 'simulation';
  if(tool==='collision')return camPage(view);
  if(camPage(view)==='collision'&&['keypoint','rapid'].includes(tool))return 'collision';
  if(['generate','rapid','settings'].includes(tool))return 'rapid';
  if(['manual-order','auto-order','order'].includes(tool))return 'order';
  if(['process','leads','overcut','compensation','clear','side','path-start','reverse'].includes(tool))return 'process';
  return 'cutting';
}
export function camContextSelectors(view,area){
  if(!['operation','toolpath','simulation'].includes(area))return [];
  const domain=view.scene?.domain,p=selectedPlacement(view)||domain?.placements?.find(p=>p.id===domain.activePlacementId),workpieces=workpiecePlacements(view),busy=workpieceSelectionBusy(view),reference=p?.workpieceId;
  return area==='operation'?[
    {id:'five-workpiece',label:'工件',value:reference||'',disabled:busy,visual:true,options:(domain?.workpieces||[]).map(w=>{
      const source=domain.placements.find(p=>p.id===w.sourcePlacementId)||domain.placements.find(p=>p.workpieceId===w.id&&!p.setupId)||domain.placements.find(p=>p.workpieceId===w.id);
      return {value:w.id,label:(w.name||'未命名工件')+(w.enabled===false?'（已禁用）':''),disabled:!source,commandId:source?'five.cam-context.workpiece:'+source.id:'',...workpieceThumbnailPreview(view,w)};
    })},
  ]:[
    {id:'five-workpiece',label:'工件',value:reference||'',disabled:true,visual:true,options:workpieces.filter(x=>x.workpieceId===reference).map(x=>{const w=domain.workpieces.find(w=>w.id===x.workpieceId);return {value:x.workpieceId,label:w?.name||'未命名工件',...workpieceThumbnailPreview(view,w)};})},
    {id:'five-pose',label:'工序',value:p?.setupId?p.id:'',disabled:busy,options:(domain?.setups||[]).flatMap(s=>{const x=domain.placements.find(p=>p.setupId===s.id);if(!x)return [];const w=domain.workpieces.find(w=>w.id===x.workpieceId);return [{value:x.id,label:`${s.name||'未命名工序'} · ${w?.name||'工件'}`,disabled:s.enabled===false||w?.enabled===false,commandId:'five.cam-context.pose:'+x.id}];})},
  ];
}
export function camContextCommand(view,area,id,value){
  if(area!==view.fiveAxisArea)return '';
  const field=camContextSelectors(view,area).find(field=>field.id===id),option=field?.options.find(option=>option.value===value);
  return !field?.disabled&&option&&!option.disabled?option.commandId||'':'';
}
export function renderCamContext(view,area){
  if(!['operation','toolpath'].includes(area))return '';
  const selectors=camContextSelectors(view,area),scope=area+':'+(selectors[0]?.value||'');
  const picture=option=>`<span class="five-cam-context-thumbnail five-workpiece-thumbnail" data-five-thumbnail="${attr(option?.imageKey||'')}"><img ${option?.image?`src="${attr(option.image)}"`:'hidden'} alt="${attr(option?.label||'工件')}缩略图"><span ${option?.image?'hidden':''}>${esc(option?.imageLabel||'暂无预览')}</span></span>`;
  const fields=selectors.map(field=>{
    const off=field.disabled||!field.options.length,key=attr(field.id);
    if(!field.visual)return `<label class="five-cam-context-field" data-five-key="cam-context-field-${key}"><span>${esc(field.label)}</span><select data-five-cam-context-field="${key}" data-five-key="cam-context-input-${key}" data-model-value-authoritative="true" aria-label="${esc(field.label)}" ${off?'disabled':''}>${!field.value?'<option value="" selected disabled>请选择工序</option>':''}${field.options.map(option=>`<option value="${attr(option.value)}" ${field.value===option.value?'selected':''} ${option.disabled?'disabled':''}>${esc(option.label)}</option>`).join('')}</select></label>`;
    const chosen=field.options.find(option=>option.value===field.value);
    return `<div class="five-cam-context-field" data-five-key="cam-context-field-${key}"><span>${esc(field.label)}</span><details class="five-cam-context-picker" data-five-cam-context-picker="${key}" data-five-key="cam-context-picker-${key}-${attr(scope)}"><summary data-five-cam-context-field="${key}" data-five-key="cam-context-input-${key}" aria-label="${esc(field.label)}" aria-disabled="${off}" tabindex="${off?-1:0}" title="${attr(chosen?.label||'请选择工件')}">${picture(chosen)}<span>${esc(chosen?.label||'请选择工件')}</span><i aria-hidden="true">⌄</i></summary><div class="five-cam-context-options" role="group" aria-label="${esc(field.label)}列表" data-five-key="cam-context-options-${key}">${field.options.map(option=>`<button type="button" data-five-cam-context-choice="${key}" data-value="${attr(option.value)}" data-five-key="cam-context-choice-${key}-${attr(option.value)}" aria-pressed="${option.value===field.value}" ${off||option.disabled?'disabled':''} title="${attr(option.label)}">${picture(option)}<span>${esc(option.label)}</span>${option.value===field.value?'<i aria-hidden="true">✓</i>':''}</button>`).join('')}</div></details></div>`;
  }).join('');
  return `<section class="five-cam-context" data-five-key="cam-context" data-five-cam-context-area="${attr(area)}" data-pane-interaction-scope="${attr(scope)}" aria-label="${area==='operation'?'工序':'刀路'}上下文">${fields}</section>`;
}
export function positionCamContextPicker(picker){
  if(!picker?.open)return;
  const menu=picker.querySelector('.five-cam-context-options'),summary=picker.querySelector('summary'),doc=picker.ownerDocument;
  if(!menu||!summary)return;
  const viewport=doc.documentElement,rect=summary.getBoundingClientRect(),width=viewport.clientWidth,height=viewport.clientHeight;
  menu.style.maxHeight=Math.min(360,Math.max(0,height-12))+'px';const bounds=menu.getBoundingClientRect();
  menu.style.left=Math.max(6,Math.min(rect.left,width-bounds.width-6))+'px';
  menu.style.top=Math.max(6,Math.min(rect.bottom+4,height-bounds.height-6))+'px';
}
export function camContextKey(event,disabled=false){
  const picker=event.target.closest?.('[data-five-cam-context-picker]');if(!picker)return false;
  const summary=picker.querySelector('summary');
  if(event.key==='Escape'&&picker.open){event.preventDefault();picker.open=false;summary.focus({preventScroll:true});return true;}
  const navigation=['ArrowDown','ArrowUp','Home','End'].includes(event.key),toggle=['Enter',' '].includes(event.key)&&event.target===summary;
  if(!navigation&&!toggle)return false;
  event.preventDefault();if(disabled||summary.getAttribute('aria-disabled')==='true')return true;
  if(toggle){picker.open=!picker.open;positionCamContextPicker(picker);return true;}
  picker.open=true;positionCamContextPicker(picker);const options=[...picker.querySelectorAll('[data-five-cam-context-choice]:not(:disabled)')],index=options.indexOf(event.target);
  const next=event.key==='Home'?0:event.key==='End'?options.length-1:event.key==='ArrowDown'?(index+1)%options.length:index<0?options.length-1:(index+options.length-1)%options.length;
  options[next]?.focus({preventScroll:true});options[next]?.scrollIntoView({block:'nearest'});return true;
}
export function attachCamContextPickers(mount){
  const doc=mount.ownerDocument,win=doc.defaultView,pickers=()=>mount.querySelectorAll('[data-five-cam-context-picker][open]');
  const outside=event=>{for(const picker of pickers())if(!picker.contains(event.target))picker.open=false;};
  const position=()=>{for(const picker of pickers())positionCamContextPicker(picker);};
  const toggle=event=>{if(event.target.matches?.('[data-five-cam-context-picker]'))positionCamContextPicker(event.target);};
  const blur=event=>{const picker=event.target.closest?.('[data-five-cam-context-picker]');if(picker&&!picker.contains(event.relatedTarget))picker.open=false;};
  doc.addEventListener('pointerdown',outside,true);doc.addEventListener('scroll',position,true);win?.addEventListener('resize',position);mount.addEventListener('toggle',toggle,true);mount.addEventListener('focusout',blur);
  return ()=>{doc.removeEventListener('pointerdown',outside,true);doc.removeEventListener('scroll',position,true);win?.removeEventListener('resize',position);mount.removeEventListener('toggle',toggle,true);mount.removeEventListener('focusout',blur);};
}
export function camRibbonWorkspace(view,area){
  if(!['operation','toolpath','simulation'].includes(area))return null;
  const busy=workpieceSelectionBusy(view);
  const action=(id,title,iconName)=>({id,title,iconName,...(commandAvailability(view,id)||{})});
  return {
    label:area==='toolpath'?'刀路':area==='operation'?'工序':'孪生',selectorsLayout:'stacked',collapsibleSelectors:area!=='operation',
    selectors:area==='simulation'?camContextSelectors(view,area):[],
    actions:[action('five.job.open','工序与输出','export')],
    pages:area==='toolpath'?CAM_PAGES.map(([id,title,iconName])=>({id:'five.cam-page.'+id,title,iconName,active:camPage(view)===id,disabled:busy})):[],
    checks:[],
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
