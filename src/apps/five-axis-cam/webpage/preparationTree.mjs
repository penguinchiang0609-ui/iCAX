import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {section,facts} from './workspaceControls.mjs';
import {preparationKind,selectTool} from './workspaceModel.mjs';
import {activeWorkpiece} from './workpieceState.mjs';
import {workpieceSelectionBusy,selectWorkpiece} from './workpieceSelection.mjs';
import {workpieceThumbnail} from './workpieceThumbnails.mjs';
import {renderSetupRow} from './setupManagement.mjs';
import {renderFixtureList} from './fixtureViews.mjs';
import {preparationMutation} from './preparationMutations.mjs';
import {toggleWorkpieceEnabled} from './workpieceActions.mjs';
import {fixtureCommand} from './fixtureActions.mjs';
import {renderAlignment} from './workpieceAlignmentPanel.mjs';

const collapsed=view=>view.fivePreparationCollapsed??=new Set();
const branch=(view,key,label)=>`<button type="button" class="five-preparation-toggle" data-five-command="five.preparation.toggle:${attr(key)}" aria-label="${collapsed(view).has(key)?'展开':'收起'}${attr(label)}" aria-expanded="${!collapsed(view).has(key)}">${collapsed(view).has(key)?'›':'⌄'}</button>`;
const workpieceIcons=(w,busy)=>`<div class="five-setup-icons">${[['lock',w.locked?'解锁工件':'锁定工件',w.locked?'M7 10V7a5 5 0 0 1 10 0v3 M5 10h14v11H5z M12 14v3':'M7 10V7a5 5 0 0 1 9-3 M5 10h14v11H5z M12 14v3'],['enable',w.enabled===false?'启用工件':'禁用工件','M12 2v10 M6 5a9 9 0 1 0 12 0']].map(([action,label,path])=>`<button type="button" class="five-setup-icon" data-five-command="five.preparation.workpiece-action:${action}:${attr(w.id)}" title="${label}" aria-label="${label}" aria-pressed="${action==='lock'?!!w.locked:w.enabled===false}" ${busy}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="${path}"/></svg></button>`).join('')}</div>`;
export function renderPreparationTree(view){
 const d=view.scene?.domain;if(!d)return '';
 const busy=workpieceSelectionBusy(view)?'disabled':'',kind=preparationKind(view),current=activeWorkpiece(view);
 const roots=d.workpieces.map(w=>{
  const placements=d.setups.flatMap(s=>d.placements.filter(p=>p.workpieceId===w.id&&p.setupId===s.id));
  const p=d.placements.find(p=>p.workpieceId===w.id&&p.id===view.scene.activePlacementId)||d.placements.find(p=>p.workpieceId===w.id),wk='workpiece:'+w.id;
  const children=placements.map(p=>{
   const s=d.setups.find(s=>s.id===p.setupId),sk='setup:'+p.id,units=(d.fixtureUnits||[]).filter(u=>u.setupId===s.id),active=p.id===view.scene.activePlacementId;
   return `<div class="five-preparation-setup" data-five-key="prep-setup-${attr(p.id)}" role="treeitem" aria-level="2"><div class="five-preparation-node" data-five-setup-drag="${attr(s.id)}" data-five-setup-workpiece="${attr(w.id)}" draggable="${!busy}" title="拖动调整装夹工序顺序">${branch(view,sk,s.name)}${renderSetupRow(view,p,active&&kind==='setup')}</div><div class="five-preparation-units" data-five-key="prep-units-${attr(p.id)}" role="group" ${collapsed(view).has(sk)?'hidden':''}><div class="five-preparation-caption">夹具单元<span>${units.length}</span></div>${renderFixtureList(view,units,true)}</div></div>`;
  }).join('');
  return `<div class="five-preparation-workpiece" data-five-key="prep-workpiece-${attr(w.id)}" role="treeitem" aria-level="1"><div class="five-preparation-node five-preparation-root ${kind==='workpiece'&&current?.id===w.id?'selected':''} ${w.enabled===false?'is-disabled':''}">${branch(view,wk,w.name)}<button type="button" class="five-preparation-part" data-five-command="five.preparation.workpiece:${attr(w.id)}" ${busy}>${p?workpieceThumbnail(view,p):''}<span><strong>${esc(w.name)}</strong><small>${placements.length} 个装夹姿态${w.enabled===false?' · 已禁用':''}</small></span></button>${workpieceIcons(w,busy)}</div><div class="five-preparation-setups" data-five-key="prep-setups-${attr(w.id)}" role="group" ${collapsed(view).has(wk)?'hidden':''}>${children}</div></div>`;
 }).join('');
 return `<section data-five-key="preparation-tree"><div class="five-fixture-heading"><strong>工件</strong><span>${d.workpieces.length}</span></div><div class="five-preparation-tree" role="tree" aria-label="工件、装夹与夹具单元">${roots||'<div class="five-empty-row">尚未导入工件</div>'}</div></section>`;
}
export function renderWorkpieceParameters(view){
 const w=activeWorkpiece(view);if(!w)return '';
 const count=view.scene.domain.placements.filter(p=>p.workpieceId===w.id&&p.setupId).length;
 const name=view.fiveWorkpieceNames?.get(w.id);
 const editor=`<label class="five-workpiece-name">名称<input type="text" data-five-workpiece-name="${attr(w.id)}" data-five-key="workpiece-name-${attr(w.id)}" aria-label="工件名称" maxlength="128" value="${attr(name?.value??w.name)}" ${view.pending?'readonly':''}></label>${name?.error?`<p role="alert">${esc(name.error)} <button data-five-command="five.workpiece-name.save:${attr(w.id)}">重试</button></p>`:''}`;
 return `<div data-five-key="workpiece-parameters"><header class="five-fixture-heading"><strong>工件</strong></header>${section('workpiece-information','模型信息',editor+facts([['装夹姿态',`${count} 个`],['状态',w.enabled===false?'已禁用':'已启用']]))}${renderAlignment(view)}</div>`;
}
export async function preparationCommand(context,view,action,ops){
 if(action.startsWith('copy:'))return preparationMutation(context,view,action,ops);
 if(action.startsWith('toggle:')){const key=action.slice(7),set=collapsed(view);if(set.has(key))set.delete(key);else set.add(key);ops.renderProject(context,view);return;}
 if(workpieceSelectionBusy(view)||view.viewport?.transformManipulator?.pointerId!=null)return;
 const d=view.scene.domain,[verb,...args]=action.split(':'),id=args.at(-1);
 const placement=verb==='workpiece'||verb==='workpiece-action'?d.placements.find(p=>p.workpieceId===id&&p.id===view.scene.activePlacementId)||d.placements.find(p=>p.workpieceId===id)
   :verb==='unit'||verb==='unit-action'?d.placements.find(p=>p.setupId===d.fixtureUnits.find(u=>u.id===id)?.setupId):d.placements.find(p=>p.id===id);
 if(!placement)return;
 await selectWorkpiece(context,view,placement.id,ops,{kind:verb==='workpiece'||verb==='workpiece-action'?'workpiece':'setup'});
 if(view.disposed||context.isCurrentProject?.()===false||view.scene.activePlacementId!==placement.id||view.error)return;
 collapsed(view).delete('workpiece:'+placement.workpieceId);collapsed(view).delete('setup:'+placement.id);
 if(verb==='workpiece-action')return args[0]==='lock'?preparationMutation(context,view,'lock:workpiece',ops):toggleWorkpieceEnabled(context,view,ops);
 if(['unit','unit-action','add-unit'].includes(verb)){
  view.fiveAxisFixtures??={selected:'',drafts:{},clearance:'2'};
  if(verb!=='add-unit')view.fiveAxisFixtures.selected=id;
  selectTool(view,'fixture');
  if(verb==='unit-action')return fixtureCommand(context,view,args[0],ops);
 }
 ops.renderProject(context,view);
 if(verb==='add-unit')await fixtureCommand(context,view,'add',ops);
}
