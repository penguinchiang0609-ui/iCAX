import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {toolAxisErrorText} from './toolAxisErrors.mjs';
import {collisionCounts} from './collisionLocations.mjs';

const providers=new Map();
// Features contribute facts and their own existing editor command. The alert
// surface never owns their parameters, tasks, or result windows.
export function registerWorkspaceAlertProvider(id,collect){
  if(!id||providers.has(id)||typeof collect!=='function')throw Error('Invalid or duplicate alert provider');
  providers.set(id,collect);return ()=>{if(providers.get(id)===collect)providers.delete(id);};
}
function coreAlerts(view,setup){
  const report=setup?.simulationCheck;
  const items=[];
  for(const task of view.scene?.domain?.intentGenerationErrors||[])
    items.push({id:`intent-generation:${task.taskId}`,title:'刀路生成失败',detail:toolAxisErrorText(task.error)||'请检查当前刀路参数后重新生成。',severity:'error',target:{commandId:'five.tool.logs'}});
  const collision={commandId:'five.tool.collision'};
  const scope=report?.scope==='BuiltinSchematicWorkpieceAndFixtures'?'示意切割头检测':'切割头检测';
  if(view.error)items.push({id:'operation',title:'操作未完成',detail:String(view.error),severity:'error',target:{commandId:'five.tool.logs'}});
  if(report?.validity==='Stale')items.push({id:'collision-stale',title:`${scope}结果已过期`,detail:'刀路或装夹已变化，点击打开对应切割头的碰撞检测',severity:'warning',target:collision});
  else if(report?.validity==='Current') {
    const structured=Array.isArray(report.keyposeChecks),counts=collisionCounts(report);
    const collisions=structured?counts.collisions:Number(report.collisionCount)||0,unchecked=structured?counts.unchecked:Number(report.uncheckedCount)||0;
    if(collisions)items.push({id:'collision',title:`${scope}：${collisions} 处间隙告警`,detail:'打开对应切割头的碰撞检测，定位并处理',severity:'error',target:collision});
    if(counts.limits)items.push({id:'limit',title:`${scope}：${counts.limits} 处限位内无解`,detail:'打开碰撞与限位，调整相关关键点',severity:'error',target:collision});
    if(unchecked)items.push({id:'unchecked',title:`${unchecked} 处运动未验证`,detail:'打开碰撞检测，查看未验证区段',severity:'warning',target:collision});
  }
  const units=(view.scene?.domain?.fixtureUnits||[]).filter(unit=>unit.setupId===setup?.id&&unit.enabled!==false);
  const actual=view.scene?.domain?.fixtures;
  const enabled=Array.isArray(actual)?actual.filter(part=>part.setupId===setup?.id&&part.enabled!==false&&part.collisionEnabled!==false&&!view.scene.domain.fixtureUnits?.some(unit=>unit.id===part.unitId&&unit.enabled===false)):null;
  if(enabled&&!enabled.length)return items;
  const geometrySeen=new Set();
  const geometry=(design,unit)=>{
    const assembly=design?.assembly;
    if(assembly?.geometryValidity!=='Stale'||geometrySeen.has(assembly.id)||enabled&&!assembly.parts?.some(part=>enabled.some(actual=>actual.id===part.partId)))return;
    geometrySeen.add(assembly.id);
    items.push({id:`fixture-geometry:${unit?.id||assembly.id}`,title:`${unit?.name||'夹具'}：夹具设计已过期`,detail:'工件、设计参数或板件位置已变化，打开对应夹具处理',severity:'warning',target:{commandId:unit?`five.fixture.select:${unit.id}`:'five.fixture.focus.design'}});
  };
  for(const unit of units)geometry(unit.design,unit);
  geometry(setup?.fixtureDesign);
  return items;
}
export function workspaceAlertItems(view){
  const setup=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId),items=coreAlerts(view,setup);
  for(const [provider,collect] of providers)for(const item of collect(view,setup)||[])items.push({...item,id:`${provider}:${item.id}`});
  return items.filter(item=>item.id&&item.target?.commandId).map(item=>({...item,disabled:!!view.pending&&item.target.commandId!=='five.tool.collision',commandId:`five.alerts.open:${encodeURIComponent(item.id)}`}));
}
export function workspaceAlerts(view){
  return {label:'告警',commandId:'five.alerts.toggle',expanded:!!view.fiveAlertsOpen,items:workspaceAlertItems(view)};
}
export function workspaceAlertTarget(view,id){return workspaceAlertItems(view).find(item=>item.id===id)?.target;}
export function renderWorkspaceAlerts(view){
  if(!view.fiveAlertsOpen)return '';
  const items=workspaceAlertItems(view);
  return `<section class="five-workspace-alert-panel" data-five-alert-panel data-five-key="workspace-alerts" role="region" aria-label="告警概要"><header><strong>告警</strong><button type="button" data-five-command="five.alerts.close" aria-label="收起告警">收起</button></header><div class="five-workspace-alert-list">${items.length?items.map(item=>`<button type="button" data-five-key="alert-${attr(item.id)}" data-five-command="${attr(item.commandId)}" data-alert-id="${attr(item.id)}" data-severity="${attr(item.severity)}" ${item.disabled?'disabled title="当前操作完成后可打开处理页面"':''}><strong>${esc(item.title)}</strong><span>${esc(item.detail||'点击打开对应处理窗体')}</span></button>`).join(''):'<p>暂无告警</p>'}</div></section>`;
}
export function installAlertStyle(document){
  if(document.getElementById('five-axis-alert-style'))return;
  const link=document.createElement('link');Object.assign(link,{id:'five-axis-alert-style',rel:'stylesheet',href:new URL('./alerts.css',import.meta.url).href});document.head.append(link);
}

// The panel stays in its product mount so event routing and incremental patches
// retain their owner. Only its client-window position follows the ribbon anchor.
export function syncAlertPanel(context,view,binding) {
  const document=context.mount.ownerDocument,window=document.defaultView;
  if(!binding.alertPanel) {
    const state={anchor:null,frame:0};
    state.place=()=>{
      if(binding.view.disposed||binding.context.isCurrentProject?.()===false
        ||binding.area!==undefined&&binding.area!==binding.view.activeAreaId)return;
      const panel=binding.context.mount.querySelector('[data-five-alert-panel]'),anchor=document.querySelector('[data-ribbon-alerts]');
      if(state.anchor!==anchor){state.observer.disconnect();if(anchor)state.observer.observe(anchor);state.anchor=anchor;}
      if(!panel)return;
      const ribbon=anchor?.closest('.product-ribbon')?.getBoundingClientRect(),rect=anchor?.getBoundingClientRect();
      const top=Math.min(ribbon?.bottom+6||150,Math.max(12,window.innerHeight-180)),right=Math.max(12,window.innerWidth-(rect?.right??window.innerWidth)+4);
      panel.style.setProperty('--five-alert-top',`${top}px`);
      panel.style.setProperty('--five-alert-right',`${right}px`);
      panel.style.setProperty('--five-alert-height',`${Math.max(120,window.innerHeight-top-12)}px`);
    };
    state.schedule=()=>{if(!state.frame)state.frame=window.requestAnimationFrame(()=>{state.frame=0;state.place();});};
    state.observer=new window.ResizeObserver(state.schedule);
    window.addEventListener('resize',state.schedule);
    state.dispose=()=>{window.cancelAnimationFrame(state.frame);state.observer.disconnect();window.removeEventListener('resize',state.schedule);};
    binding.alertPanel=state;
  }
  binding.alertPanel.place();binding.alertPanel.schedule();
}
export function disposeAlertPanel(binding){binding?.alertPanel?.dispose();if(binding)binding.alertPanel=null;}
