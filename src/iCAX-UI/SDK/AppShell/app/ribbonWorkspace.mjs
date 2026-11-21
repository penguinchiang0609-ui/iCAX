const escape=value=>String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const commands=items=>(Array.isArray(items)?items:[]).filter(item=>item?.id).map(item=>({
  id:String(item.id),title:String(item.title??item.id),iconName:String(item.iconName??''),
  active:item.active===undefined?undefined:!!item.active,disabled:!!item.disabled,
  unavailableReason:String(item.unavailableReason??''),
}));

// Optional object selectors and page navigation, independent of any product.
export function normalizeRibbonWorkspace(value){
  if(!value||typeof value!=='object'||Array.isArray(value))return null;
  return {
    label:String(value.label||'工作区'),selectorsLayout:value.selectorsLayout==='stacked'?'stacked':'inline',collapsibleSelectors:!!value.collapsibleSelectors,
    selectors:(Array.isArray(value.selectors)?value.selectors:[]).filter(item=>item?.id).map(item=>({
      id:String(item.id),label:String(item.label??item.id),value:String(item.value??''),disabled:!!item.disabled,visual:!!item.visual,
      options:(Array.isArray(item.options)?item.options:[]).map(option=>({value:String(option.value??''),label:String(option.label??''),commandId:String(option.commandId??''),disabled:!!option.disabled,image:String(option.image??''),imageKey:String(option.imageKey??''),imageLabel:String(option.imageLabel??'暂无预览')})),
    })),
    actions:commands(value.actions),pages:commands(value.pages),checks:commands(value.checks),
  };
}
export function ribbonWorkspaceCommand(workspace,id,value){
  const field=workspace?.selectors?.find(item=>item.id===id),option=field?.options.find(item=>item.value===value);
  return !field?.disabled&&option&&!option.disabled?option.commandId:'';
}
export function renderRibbonWorkspaceSelectors(workspace){
  const picture=option=>`<span class="ribbon-workspace-thumbnail" data-ribbon-thumbnail="${escape(option?.imageKey||'')}"><img ${option?.image?`src="${escape(option.image)}"`:'hidden'} alt="${escape(option?.label||'对象')}缩略图"><small ${option?.image?'hidden':''}>${escape(option?.imageLabel||'暂无预览')}</small></span>`;
  const selector=field=>{
    if(!field.visual)return `<label class="ribbon-workspace-field"><span>${escape(field.label)}</span><select data-ribbon-workspace-field="${escape(field.id)}" aria-label="${escape(field.label)}" ${field.disabled||!field.options.length?'disabled':''}>${field.options.map(option=>`<option value="${escape(option.value)}" ${option.value===field.value?'selected':''} ${option.disabled?'disabled':''}>${escape(option.label)}</option>`).join('')}</select></label>`;
    const chosen=field.options.find(o=>o.value===field.value),off=field.disabled||!field.options.length;
    return `<div class="ribbon-workspace-field"><span>${escape(field.label)}</span><details class="ribbon-workspace-picker" data-ribbon-workspace-picker="${escape(field.id)}"><summary data-action="ribbon-workspace-toggle" data-ribbon-workspace-field="${escape(field.id)}" aria-label="${escape(field.label)}" aria-disabled="${off}" tabindex="${off?-1:0}" title="${escape(chosen?.label||'请选择工件')}">${picture(chosen)}<span>${escape(chosen?.label||'请选择')}</span><i aria-hidden="true">⌄</i></summary><div class="ribbon-workspace-options" role="group" aria-label="${escape(field.label)}列表">${field.options.map(o=>`<button type="button" data-action="ribbon-workspace-choice" data-ribbon-workspace-choice="${escape(field.id)}" data-value="${escape(o.value)}" aria-pressed="${o.value===field.value}" ${off||o.disabled?'disabled':''} title="${escape(o.label)}">${picture(o)}<span>${escape(o.label)}</span>${o.value===field.value?'<i aria-hidden="true">✓</i>':''}</button>`).join('')}</div></details></div>`;
  };
  return workspace.selectors.map(selector).join('');
}
export function renderRibbonWorkspace(workspace,icon=()=> '',tools=''){
  if(!workspace)return '';
  const button=(command,page=false)=>`<button type="button" class="ribbon-workspace-command ${page?'ribbon-workspace-page':''} ${command.active?'active':''}" data-action="ribbon-command" data-command-id="${escape(command.id)}" ${page?`role="tab" aria-selected="${!!command.active}"`:''} ${command.disabled?'disabled':''} title="${escape(command.unavailableReason||command.title)}">${command.iconName?icon(command.iconName):''}<span>${escape(command.title)}</span></button>`;
  const selectors=renderRibbonWorkspaceSelectors(workspace),context=!selectors?'':workspace.collapsibleSelectors?`<details class="ribbon-workspace-context is-${workspace.selectorsLayout}" data-ribbon-workspace-context open><summary aria-label="展开或折叠工件与位姿" title="展开或折叠工件与位姿"><span aria-hidden="true">‹</span></summary><div class="ribbon-workspace-selector-fields">${selectors}</div></details>`:`<div class="ribbon-workspace-context">${selectors}</div>`;
  return `<div class="ribbon-workspace-toolbar" aria-label="${escape(workspace.label)}工具栏">${context}${workspace.pages.length?`<div class="ribbon-workspace-pages" role="tablist" aria-label="${escape(workspace.label)}分区">${workspace.pages.map(c=>button(c,true)).join('')}</div>`:''}${tools}<div class="ribbon-workspace-common">${[...workspace.checks,...workspace.actions].map(c=>button(c)).join('')}</div></div>`;
}

export function ribbonWorkspaceKey(event){
  const picker=event.target.closest?.('[data-ribbon-workspace-picker]');if(!picker)return false;
  const summary=picker.querySelector('summary');if(summary.getAttribute('aria-disabled')==='true')return false;
  if(event.key==='Escape'&&picker.open){event.preventDefault();picker.open=false;summary.focus({preventScroll:true});return true;}
  if(!['ArrowDown','ArrowUp','Home','End'].includes(event.key))return false;
  event.preventDefault();picker.open=true;positionRibbonWorkspacePicker(picker);const options=[...picker.querySelectorAll('button:not(:disabled)')],index=options.indexOf(event.target);
  const next=event.key==='Home'?0:event.key==='End'?options.length-1:event.key==='ArrowDown'?(index+1)%options.length:index<0?options.length-1:(index+options.length-1)%options.length;
  options[next]?.focus({preventScroll:true});options[next]?.scrollIntoView({block:'nearest'});return true;
}
export function positionRibbonWorkspacePicker(picker){
  if(!picker?.open)return;
  const menu=picker.querySelector('.ribbon-workspace-options');if(getComputedStyle(menu).position!=='fixed')return;
  const rect=picker.querySelector('summary').getBoundingClientRect(),width=menu.getBoundingClientRect().width;
  menu.style.left=Math.max(6,Math.min(rect.left,innerWidth-width-6))+'px';
  menu.style.top=Math.min(rect.bottom+4,Math.max(6,innerHeight-menu.getBoundingClientRect().height-6))+'px';
}
export function captureRibbonWorkspace(ribbon){
  const contextOpen=ribbon?.querySelector('[data-ribbon-workspace-context]')?.open;
  const active=ribbon?.ownerDocument.activeElement,focused=ribbon?.contains(active)&&active.matches('[data-ribbon-workspace-field],[data-ribbon-workspace-choice],[data-ribbon-workspace-context]>summary')?{context:active.matches('[data-ribbon-workspace-context]>summary'),field:active.dataset.ribbonWorkspaceField,choice:active.dataset.ribbonWorkspaceChoice,value:active.dataset.value}:null;
  const open=[...(ribbon?.querySelectorAll('[data-ribbon-workspace-picker][open]')||[])].map(p=>({id:p.dataset.ribbonWorkspacePicker,scroll:p.querySelector('.ribbon-workspace-options').scrollTop}));
  return current=>{
    const context=current?.querySelector('[data-ribbon-workspace-context]');if(context&&contextOpen!==undefined)context.open=contextOpen;
    for(const saved of open){const p=[...(current?.querySelectorAll('[data-ribbon-workspace-picker]')||[])].find(p=>p.dataset.ribbonWorkspacePicker===saved.id);if(p&&p.querySelector('summary').getAttribute('aria-disabled')!=='true'){p.open=true;p.querySelector('.ribbon-workspace-options').scrollTop=saved.scroll;positionRibbonWorkspacePicker(p);}}
    if(!focused||active?.isConnected)return;
    const doc=current?.ownerDocument;if(!doc||![doc.body,doc.documentElement,null].includes(doc.activeElement))return;
    const target=focused.context?current.querySelector('[data-ribbon-workspace-context]>summary'):[...current.querySelectorAll('[data-ribbon-workspace-field],[data-ribbon-workspace-choice]')].find(n=>focused.choice?n.dataset.ribbonWorkspaceChoice===focused.choice&&n.dataset.value===focused.value:n.dataset.ribbonWorkspaceField===focused.field);
    if(target&&!target.disabled&&target.getAttribute('aria-disabled')!=='true')target.focus({preventScroll:true});
  };
}
