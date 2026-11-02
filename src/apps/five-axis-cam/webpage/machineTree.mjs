import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {revealSceneListSelection} from './listNavigation.mjs';
import {machineMounts} from './machinePlaybackContext.mjs';

const folds=(view,machine)=>{
  const states=view.fiveMachineTree??=new Map(),id=machine.entityId;
  if(!states.has(id))states.set(id,new Set());
  return states.get(id);
};
const jointTypes={fixed:'固定',prismatic:'直线',revolute:'旋转',continuous:'连续旋转',floating:'浮动',planar:'平面'};

export function renderMachineTree(view,machine,busy=''){
  if(!machine)return '';
  const collapsed=folds(view,machine),elements=machine.elements||[],nodes=new Map(elements.map(e=>[e.entityId,{element:e,children:[]}]));
  const roots=[],joints=new Map((machine.joints||[]).map(j=>[j.entityId,j]));
  for(const node of nodes.values()){
    const parent=nodes.get(node.element.parentEntityId);
    if(parent&&parent!==node)parent.children.push(node);else roots.push(node);
  }
  // These are persisted support bindings, projected into the mechanical tree;
  // the CAD entities retain their own document hierarchy and local transforms.
  for(const item of machineMounts(view,machine.entityId)){
    const parent=nodes.get(item.supportId);if(parent)parent.children.push({mount:item,element:{entityId:item.id}});
  }
  const selected=view.fiveMachineSelectedPartId;
  const visited=new Set();
  const render=node=>{
    if(node.mount){const p=node.mount;return `<li data-five-key="machine-mount-${attr(p.id)}" data-machine-mount="${attr(p.id)}"><div class="five-machine-tree-row"><span class="five-machine-tree-spacer" aria-hidden="true"></span><span class="five-machine-tree-label" title="${attr(p.setupName||'')}">${esc(p.kind+' · '+p.name)}</span><small>随动装夹</small></div></li>`;}
    const e=node.element,id=e.entityId;if(visited.has(id))return '';visited.add(id);
    const children=node.children.filter(c=>!visited.has(c.element.entityId)),open=!collapsed.has(id),joint=joints.get(id),root=e.kind==='machine';
    const label=e.name||id,description=joint?`${joint.jointName||''} · ${jointTypes[joint.type]||joint.type}`:root?'机床':'';
    return `<li data-five-key="machine-node-${attr(id)}" data-machine-node="${attr(id)}"><div class="five-machine-tree-row" data-selected="${root?!selected:selected===id}">${children.length?`<button type="button" class="five-machine-tree-toggle" data-five-command="five.machine.tree-toggle:${attr(id)}" aria-label="${open?'折叠':'展开'} ${attr(label)}" aria-expanded="${open}" aria-controls="machine-children-${attr(id)}">${open?'▾':'▸'}</button>`:'<span class="five-machine-tree-spacer" aria-hidden="true"></span>'}${root?`<button type="button" class="five-machine-tree-label" data-five-command="five.machine.part:" aria-pressed="${!selected}" ${busy}>${esc(label)}</button>`:`<button type="button" class="five-machine-tree-label" data-five-command="five.machine.part:${attr(id)}" aria-pressed="${selected===id}" title="${attr(label)}" ${busy}>${esc(label)}</button>`}${description?`<small title="${attr(description)}">${esc(description)}</small>`:''}</div>${children.length?`<ul id="machine-children-${attr(id)}" data-five-key="machine-children-${attr(id)}" ${open?'':'hidden'}>${children.map(render).join('')}</ul>`:''}</li>`;
  };
  const content=roots.map(render).join('');
  return `<ul class="five-machine-tree" aria-label="机床机械结构" data-five-key="machine-hierarchy-${attr(machine.entityId)}">${content}</ul>`;
}

export function toggleMachineTree(context,view,machine,id){
  if(!machine?.elements?.some(e=>e.entityId===id))return;
  const collapsed=folds(view,machine);if(collapsed.has(id))collapsed.delete(id);else collapsed.add(id);
  // Folding is a local disclosure: retain both panels, their listeners and inputs.
  const node=context.mount.querySelector(`[data-machine-node="${CSS.escape(id)}"]`),button=node?.querySelector(':scope > .five-machine-tree-row > .five-machine-tree-toggle'),children=node?.querySelector(':scope > ul');
  if(!button||!children)return;
  const open=!collapsed.has(id),name=machine.elements.find(e=>e.entityId===id).name||id;
  children.hidden=!open;button.setAttribute('aria-expanded',String(open));button.setAttribute('aria-label',`${open?'折叠':'展开'} ${name}`);button.textContent=open?'▾':'▸';
}

export function revealMachineTreePart(context,view,machine,id){
  const elements=new Map((machine?.elements||[]).map(e=>[e.entityId,e])),visited=new Set();
  let parent=elements.get(id)?.parentEntityId;
  while(elements.has(parent)&&!visited.has(parent)){
    visited.add(parent);if(folds(view,machine).has(parent))toggleMachineTree(context,view,machine,parent);
    parent=elements.get(parent).parentEntityId;
  }
  revealSceneListSelection(context.mount,`machine-node-${id}`,':scope > .five-machine-tree-row');
}
