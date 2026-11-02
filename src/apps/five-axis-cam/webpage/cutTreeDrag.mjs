import { buildCutTree, selectedTreeNodes } from './cutTreeModel.mjs';
import { workspaceState } from './workspaceModel.mjs';
import { cutTreeBusy, cutTreeCommand, selectCutGroup } from './cutTree.mjs';
import { selectCut } from './cutSelection.mjs';

export function attachCutTreeDrag(mount,binding){
  let drag=null,cue=null;
  const clear=()=>{cue?.removeAttribute('data-five-drop-position');cue=null;};
  const finish=()=>{clear();drag=null;mount.classList.remove('five-tree-dragging');};
  mount.addEventListener('dragstart',event=>{
    if(event.target.closest?.('[data-five-setup-drag]'))return;
    const element=event.target.closest?.('[data-five-node]'),{context,view}=binding;
    if(!element||!element.draggable||context.isCurrentProject?.()===false||cutTreeBusy(view)||view.fiveTreeRequest){event.preventDefault();return;}
    const tree=buildCutTree(view),node=tree.nodes.get(element.dataset.fiveNode);if(!node||node.containsLocked){event.preventDefault();return;}
    let chosen=selectedTreeNodes(tree,workspaceState(view));
    if(!chosen.some(n=>n.key===node.key)){
      if(node.kind==='Group')selectCutGroup(view,node.id);else selectCut(view,node.id);
      chosen=selectedTreeNodes(tree,workspaceState(view));
    }
    drag={view,expected:structuredClone(view.scene.domain.expected),intentIds:chosen.filter(n=>n.kind==='Cut').map(n=>n.id),groupIds:chosen.filter(n=>n.kind==='Group').map(n=>n.id),keys:new Set(chosen.map(n=>n.key))};
    event.dataTransfer.effectAllowed='move';event.dataTransfer.setData('text/plain',chosen.map(n=>n.id).join(','));mount.classList.add('five-tree-dragging');
  });
  const target=event=>{
    const {context,view}=binding;
    if(!drag||drag.view!==view||context.isCurrentProject?.()===false||cutTreeBusy(view)||JSON.stringify(drag.expected)!==JSON.stringify(view.scene.domain.expected))return null;
    const element=event.target.closest?.('[data-five-drop-node], [data-five-root-drop]');if(!element)return null;
    if(element.hasAttribute('data-five-root-drop'))return {element,request:{kind:'Root',id:'',position:'Inside'}};
    const tree=buildCutTree(view),node=tree.nodes.get(element.dataset.fiveDropNode);if(!node||node.containsLocked)return null;
    for(let n=node;n;n=tree.nodes.get(n.parent))if(drag.keys.has(n.key))return null;
    const box=element.getBoundingClientRect(),fraction=(event.clientY-box.top)/box.height;
    const position=node.kind==='Group'&&fraction>=.25&&fraction<=.75?'Inside':fraction<.5?'Before':'After';
    return {element,request:{kind:node.kind,id:node.id,position}};
  };
  mount.addEventListener('dragover',event=>{
    clear();const hit=target(event);if(!hit)return;
    event.preventDefault();event.dataTransfer.dropEffect='move';cue=hit.element;cue.setAttribute('data-five-drop-position',hit.request.position);
    const pane=cue.closest('.five-path-list');if(pane){const r=pane.getBoundingClientRect();if(event.clientY<r.top+45)pane.scrollTop-=16;else if(event.clientY>r.bottom-45)pane.scrollTop+=16;}
  });
  mount.addEventListener('drop',event=>{
    const hit=target(event),pending=drag;finish();if(!hit||!pending)return;
    event.preventDefault();const {context,view,ops}=binding;
    void cutTreeCommand(context,view,'Move',ops,{expected:pending.expected,intentIds:pending.intentIds,groupIds:pending.groupIds,target:hit.request});
  });
  mount.addEventListener('dragend',()=>{const wasDragging=!!drag;finish();if(wasDragging&&binding.context.isCurrentProject?.()!==false)binding.ops.renderProject(binding.context,binding.view);});
}
