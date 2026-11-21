import {workspaceState,preparationKind} from './workspaceModel.mjs';
import {buildCutTree} from './cutTreeModel.mjs';
import {revealSceneListSelection} from './listNavigation.mjs';

export function revealSceneCut(context,view,ops){
  if(view.disposed||context.isCurrentProject?.()===false||view.fiveAxisArea!=='toolpath')return;
  const ui=workspaceState(view),key=ui.selectedGroupId?`g:${ui.selectedGroupId}`:`c:${ui.selectedIntentId}`;
  const tree=buildCutTree(view),node=tree.nodes.get(key);if(!node)return;
  const collapsed=new Set(ui.collapsedCutGroups||[]),seen=new Set();let parent=node.parent,changed=false;
  while(parent&&!seen.has(parent)){
    seen.add(parent);const ancestor=tree.nodes.get(parent);if(!ancestor)break;
    changed=collapsed.delete(ancestor.id)||changed;parent=ancestor.parent;
  }
  if(changed){ui.collapsedCutGroups=[...collapsed];ops.renderProject(context,view);}
  revealSceneListSelection(context.mount,`select-${key}`);
}

export function revealSceneWorkpiece(context,view,ops,{unitId=null}={}){
  if(view.disposed||context.isCurrentProject?.()===false||!['workpiece','operation'].includes(view.fiveAxisArea))return;
  const placement=view.scene?.domain?.placements?.find(p=>p.id===view.scene.activePlacementId);if(!placement)return;
  const collapsed=view.fivePreparationCollapsed;let changed=false;
  if(collapsed){changed=collapsed.delete(`workpiece:${placement.workpieceId}`);if(unitId)changed=collapsed.delete(`setup:${placement.id}`)||changed;}
  if(changed)ops.renderProject(context,view);
  if(unitId)revealSceneListSelection(context.mount,`fixture-${unitId}`);
  else if(view.fiveAxisArea==='operation')revealSceneListSelection(context.mount,`setup-${placement.id}`);
  else if(!revealSceneListSelection(context.mount,`workpiece-${placement.id}`)){
    if(preparationKind(view)==='setup')revealSceneListSelection(context.mount,`prep-setup-${placement.id}`,':scope > .five-preparation-node');
    else revealSceneListSelection(context.mount,`prep-workpiece-${placement.workpieceId}`,'.five-preparation-root');
  }
}
