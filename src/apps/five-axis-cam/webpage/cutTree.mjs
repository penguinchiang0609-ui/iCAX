import { activeTool, workspaceState } from './workspaceModel.mjs';
import { camPage } from './camWorkspace.mjs';
import { refreshMachiningState } from './domainActions.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import { button } from './workspaceControls.mjs';
import { escapeAttr as attr, escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { buildCutTree, cutGroups, selectedTreeNodes, cutSequenceNumbers } from './cutTreeModel.mjs';
import {collisionPathResults,collisionPathWarning} from './collisionPathResults.mjs';
export { cutGroups } from './cutTreeModel.mjs';
export const cutTreeBusy=(view,{ignoreExtractionPick=false}={})=>view.pending||view.fiveToolAxisRequest||view.fiveAxisPointEdit?.picking||view.fiveAxisPointEdit?.rotation?.gesture||view.fiveAxisPointInsert||view.fiveAxisPointDelete||view.fiveAxisPointEdit?.dirty||view.fiveRapidBatch?.running||view.fiveOrder?.request||view.fiveOrder?.running||(view.fiveOrder?.open&&activeTool(view)==='manual-order')||(!ignoreExtractionPick&&view.fiveAxisExtractionPick)||view.fiveSimulation?.request||view.fiveRapid?.request||view.fiveGeneration?.request||view.fiveClear?.request||view.fiveReverseRequest||view.fiveLeads?.request||view.fiveLeads?.saving||view.fiveStart?.request||view.fiveHole?.request;
export const orderedCuts=view=>buildCutTree(view).cuts;
export function constrainedCutIds(view,desired){
  const tree=buildCutTree(view),ranks=new Map(desired.map((id,i)=>[id,i]));
  const rank=b=>Math.min(...b.leafIds.map(id=>ranks.get(id)??desired.length));
  const visit=blocks=>{
    for(const block of blocks)if(!block.locked)visit(block.children);
    for(let begin=0;begin<blocks.length;){
      if(blocks[begin].containsLocked){begin++;continue;}
      let end=begin;while(end<blocks.length&&!blocks[end].containsLocked)end++;
      blocks.splice(begin,end-begin,...blocks.slice(begin,end).sort((a,b)=>rank(a)-rank(b)));begin=end;
    }
  };visit(tree.roots);
  const flatten=blocks=>blocks.flatMap(b=>b.kind==='Group'?flatten(b.children):[b.id]);
  return flatten(tree.roots);
}
function canMove(view,delta){
  const tree=buildCutTree(view),chosen=selectedTreeNodes(tree,workspaceState(view)),keys=new Set(chosen.map(n=>n.key));
  return chosen.some(n=>{const siblings=tree.nodes.get(n.parent)?.children||tree.roots,other=siblings[siblings.indexOf(n)+delta];return other&&!other.containsLocked&&!keys.has(other.key);});
}
export function renderCutTreeTools(view) {
  const preparing=view.scene?.domain?.intentGenerationTasks?.some(t=>t.status==='Running');
  return `${view.fiveTreeRequest?`<div class="five-cut-tree-tools" data-five-key="cut-tree-tools">${button('five.tree.retry','重试',view.pending?'disabled':'')}</div>`:''}${preparing?'<small data-five-key="cut-tree-preparing" role="status">正在准备刀路几何，列表可先查看。</small>':''}`;
}
export function cutTreeAvailability(view) {
  const selected=selectedTreeNodes(buildCutTree(view),workspaceState(view)),busy=!!(cutTreeBusy(view)||view.fiveTreeRequest);
  const orderBusy=view.pending||view.fiveTreeRequest||view.fiveOrder?.request||(!view.fiveOrder?.open&&cutTreeBusy(view));
  return {add:!cutTreeBusy(view,{ignoreExtractionPick:true})&&!view.fiveTreeRequest&&!!view.scene?.model?.isLoaded,remove:!busy&&selected.length>0,group:!busy&&selected.length>1,ungroup:!busy&&selected.length>0&&selected.every(n=>n.kind==='Group'),
    up:!busy&&canMove(view,-1),down:!busy&&canMove(view,1),order:!orderBusy&&orderedCuts(view).some(c=>c.enabled!==false)};
}
function rowMoveTarget(tree,item,action){
  if(!item||item.containsLocked)return null;
  const siblings=tree.nodes.get(item.parent)?.children||tree.roots,index=siblings.indexOf(item),delta=['Up','Top'].includes(action)?-1:1;
  let target=index+delta;
  if(target<0||target>=siblings.length||siblings[target].containsLocked)return null;
  if(['Top','Bottom'].includes(action))while(target+delta>=0&&target+delta<siblings.length&&!siblings[target+delta].containsLocked)target+=delta;
  const node=siblings[target];return {kind:node.kind,id:node.id,position:delta<0?'Before':'After'};
}
function controls(view,item,tree){
  const page=view.fiveAxisArea==='toolpath'?camPage(view):'';
  if(!['cutting','order'].includes(page))return '';
  const group=item.kind==='Group',locked=item.locked,enabled=item.enabled,label=item.name,id=attr(item.id),scope=group?'Group':'';
  const busy=!!(cutTreeBusy(view)||view.fiveTreeRequest);
  const icon=(kind,title,body,pressed,disabled)=>`<button type="button" class="five-cut-icon ${kind==='Enabled'&&!enabled?'is-off':''}" data-five-key="${kind}-${id}" data-five-command="five.tree.Toggle${scope}${kind}:${id}" aria-label="${attr(title)}" title="${attr(title)}" aria-pressed="${pressed}" ${disabled?'disabled':''}><svg viewBox="0 0 24 24" aria-hidden="true">${body}</svg></button>`;
  const moves=group?'':[['Up','上移','M6 13l6-6 6 6M12 7v13'],['Down','下移','M6 11l6 6 6-6M12 4v13'],['Top','置顶','M5 4h14M6 14l6-6 6 6M12 8v13'],['Bottom','置底','M5 20h14M6 10l6 6 6-6M12 3v13']].map(([action,title,path])=>`<button type="button" class="five-cut-icon five-cut-move" data-five-key="move-${action}-${id}" data-five-command="five.tree.Row${action}:${id}" aria-label="${attr(title+label)}" title="${title}" ${busy||!rowMoveTarget(tree,item,action)?'disabled':''}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="${path}"/></svg></button>`).join('');
  const content=page==='order'?moves:icon('Enabled',`${enabled?'禁用':'启用'}${label}`,'<path d="M12 3v8M6.3 5.8a8 8 0 1 0 11.4 0"/>',enabled,busy||item.containsLocked||item.inheritedDisabled)+icon('Locked',`${locked?'解锁':'锁定'}${label}`,`<rect x="5" y="10" width="14" height="11" rx="2"/><path d="${locked?'M8 10V7a4 4 0 0 1 8 0v3':'M8 10V7a4 4 0 0 1 8 0'}"/><path d="M12 14v3"/>`,locked,busy||item.inheritedLocked);
  return content?`<span class="five-cut-row-controls" data-five-key="controls-${id}">${content}</span>`:'';
}
export function renderCutTree(view,cuts){
  const tree=buildCutTree(view,cuts),ui=workspaceState(view),selected=new Set(ui.selectedIntentIds||[ui.selectedIntentId]),busy=!!(cutTreeBusy(view)||view.fiveTreeRequest);
  const sequence=cutSequenceNumbers(tree),label=n=>n.name,collisions=collisionPathResults(view);
  const query=(ui.search||'').toLocaleLowerCase(),matches=n=>!query||`${n.name} ${sequence.get(n.id)||''} ${n.number||''}`.toLocaleLowerCase().includes(query)||n.children?.some(matches);
  const row=n=>{
    if(!matches(n))return '';
    const group=n.kind==='Group',id=attr(n.id),key=attr(n.key),locked=group?n.containsLocked:n.locked;
    const checked=!locked&&n.leafIds.every(id=>selected.has(id)),partial=!checked&&n.leafIds.some(id=>selected.has(id));
    const checkbox=`<button type="button" role="checkbox" class="five-cut-checkbox" data-five-check="${key}" data-five-key="check-${key}" aria-label="选择${attr(group?n.name:label(n))}" aria-checked="${partial?'mixed':checked}" ${locked||busy?'disabled':''}>${checked?'✓':partial?'−':''}</button>`;
    const layer=group?null:view.scene?.domain?.layers?.catalog?.[n.layers?.visibleLayerId];
    const select=`<button type="button" class="five-tree-row ${n.machiningKind==='Mark'?'is-marking':''} ${checked?'selected':''}" data-five-key="select-${key}" title="${attr(group?n.name:label(n))}${layer?` · ${attr(layer.name)}`:''} · 双击聚焦 · F 聚焦选中刀路" data-five-node="${key}" ${group?`data-five-group="${id}"`:`data-five-intent="${id}"`} draggable="${!locked&&!busy}" aria-pressed="${checked}" ${locked?'disabled':''}>${layer?`<i class="five-tree-layer-color" style="background:${attr(layer.color)}" title="${attr(layer.name)}" aria-label="图层：${attr(layer.name)}"></i>`:''}<span class="five-cut-name">${esc(group?n.name:label(n))}</span>${group?`<small>${n.leafIds.length}</small>`:''}</button>`;
    if(!group)return `<div class="five-cut-tree-item ${!n.enabled?'is-disabled':''} ${locked?'is-locked':''}" data-five-key="row-${id}" data-five-drop-node="${key}"><span class="five-cut-order" aria-label="加工序号 ${sequence.get(n.id)||'未启用'}">${sequence.get(n.id)||'—'}</span>${checkbox}${select.replace('</button>',collisionPathWarning(collisions,`cut:${n.id}`)+'</button>')}${controls(view,n,tree)}</div>`;
    const collapsed=ui.collapsedCutGroups?.includes(n.id);
    return `<section class="five-cut-group" data-five-key="group-${id}"><div class="five-cut-group-heading ${!n.enabled?'is-disabled':''}" data-five-key="group-heading-${id}" data-five-drop-node="${key}"><button class="five-cut-expand" data-five-command="five.tree.Collapse:${id}" aria-label="${collapsed?'展开':'收起'}${attr(n.name)}" aria-expanded="${!collapsed}">${collapsed?'▸':'▾'}</button>${checkbox}${select}${controls(view,n,tree)}</div><div class="five-cut-group-children" data-five-key="children-${id}" ${collapsed?'hidden':''}>${n.children.map(row).join('')}</div></section>`;
  };
  return `<div class="five-cut-tree" data-five-key="cut-tree">${tree.roots.map(row).join('')}<div class="five-cut-root-drop" data-five-root-drop aria-label="移出组，放到刀路列表末尾"></div></div>`;
}
export function selectCutGroup(view,id,event={}){
  const ui=workspaceState(view),tree=buildCutTree(view),node=tree.nodes.get(`g:${id}`);if(!node||node.containsLocked)return;
  const previous=new Set(ui.selectedIntentIds||[ui.selectedIntentId]),remove=node.leafIds.every(id=>previous.has(id)),multiple=event.ctrlKey||event.metaKey||event.shiftKey;
  const next=multiple?previous:new Set(),groups=new Set(multiple?ui.selectedGroupIds||[]:[]);
  for(const id of node.leafIds){if(multiple&&remove)next.delete(id);else next.add(id);}
  if(multiple&&remove)groups.delete(id);else groups.add(id);
  ui.selectedIntentIds=tree.cuts.filter(c=>!c.locked&&next.has(c.id)).map(c=>c.id);ui.selectedIntentId=ui.selectedIntentIds[0]||'';
  view.fivePreferredCutSelection=[...ui.selectedIntentIds];
  ui.selectedGroupIds=[...groups].filter(g=>tree.nodes.get(`g:${g}`)?.leafIds.every(id=>next.has(id)));ui.selectedGroupId=ui.selectedGroupIds.length===1?ui.selectedGroupIds[0]:'';
}
export async function cutTreeCommand(context,view,action,ops,override=null) {
  const ui=workspaceState(view);
  if(action.startsWith('Collapse:')){const id=action.slice(9),set=new Set(ui.collapsedCutGroups||[]);if(set.has(id))set.delete(id);else set.add(id);ui.collapsedCutGroups=[...set];ops.renderProject(context,view);return;}
  if(cutTreeBusy(view))return;
  const tree=buildCutTree(view);let chosen=selectedTreeNodes(tree,ui);
  const move=/^Row(Up|Down|Top|Bottom):(.+)$/.exec(action);
  if(move){const node=tree.nodes.get(`c:${move[2]}`),target=rowMoveTarget(tree,node,move[1]);if(!target)return;chosen=[node];action='Move';override={target};}
  const toggle=/^Toggle(Group)?(Enabled|Locked):(.+)$/.exec(action);
  if(toggle){const node=tree.nodes.get(`${toggle[1]?'g':'c'}:${toggle[3]}`);if(!node)return;chosen=[node];action=toggle[2]==='Enabled'?(node.enabled?'Disable':'Enable'):(node.locked?'Unlock':'Lock');}
  if(!view.fiveTreeRequest){
    if((!chosen.length&&!override)||!['Up','Down','Delete','Enable','Disable','Lock','Unlock','Group','Ungroup','Move'].includes(action))return;
    view.fiveTreeRequest={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,
      intentIds:chosen.filter(n=>n.kind==='Cut').map(n=>n.id),groupIds:chosen.filter(n=>n.kind==='Group').map(n=>n.id),action,...override};
  }
  const request=view.fiveTreeRequest,current=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  view.pending=true;view.error='';ops.renderProject(context,view);
  try{
    let result;
    try{result=await context.sceneProxy.invoke('CamCut.ManageTree',request);}
    catch(error){
      let receipt;try{receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});}catch{throw error;}
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')view.fiveTreeRequest=null;throw error;}
      result=receipt.result;
    }
    view.fiveTreeRequest=null;
    await saveCommittedCutEdit(context,view,ops);
    if(current()){
      await refreshMachiningState(context,view,ops,request,result);
      const remaining=new Set((view.scene.intentToolpaths||[]).filter(c=>!c.locked).map(c=>c.id));
      ui.selectedIntentIds=(ui.selectedIntentIds||[ui.selectedIntentId]).filter(id=>remaining.has(id));
      if(!remaining.has(ui.selectedIntentId))ui.selectedIntentId=ui.selectedIntentIds[0]||'';
      if(request.action==='Group'&&result?.groupId)ui.selectedGroupIds=[result.groupId];
      const currentTree=buildCutTree(view);
      ui.selectedGroupIds=(ui.selectedGroupIds||[]).filter(id=>{const n=currentTree.nodes.get(`g:${id}`);return n&&!n.containsLocked&&n.leafIds.every(id=>ui.selectedIntentIds.includes(id));});
      ui.selectedGroupId=ui.selectedGroupIds.length===1?ui.selectedGroupIds[0]:'';
    }
  }catch(error){if(current())view.error=view.fiveTreeRequest?'操作结果尚未确认，请点击“重试”。':/EXPECTED|SEQUENCE|STALE/.test(error.message)?'工程已变化，请刷新后重试。':/LOCKED/.test(error.message)?'已锁定的刀路或组不能修改，也不能跨越排序。':/CYCLE/.test(error.message)?'不能把组移入自身或它的子组。':error.message;}
  finally{view.pending=false;if(current())ops.renderProject(context,view);}
}
