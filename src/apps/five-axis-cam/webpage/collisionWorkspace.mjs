import {orderedCuts} from './cutTree.mjs';
import {rapidListModel} from './rapidListModel.mjs';
import {workspaceState,collisionDetailKind} from './workspaceModel.mjs';
import {collisionPathResults,collisionPathBadge,selectedCollisionPath} from './collisionPathResults.mjs';
import {escapeAttr as attr,escapeText as esc} from '../../_shared/workbench/utils/format.mjs';

export function renderCollisionPaths(view){
  const kind=collisionDetailKind(view),ui=workspaceState(view),cuts=orderedCuts(view),rapids=rapidListModel(view).transitions;
  const numbers=new Map(cuts.filter(c=>c.enabled!==false).map((c,i)=>[c.id,i+1]));
  const cutRows=cuts.map(c=>({key:`cut:${c.id}`,id:c.id,kind:'cutting',name:c.name||c.label||c.id,number:numbers.get(c.id)||'—',locked:c.locked,enabled:c.enabled}));
  const rapidRows=rapids.map(t=>({key:`rapid:${t.from}/${t.to}`,id:`${t.from}/${t.to}`,from:t.from,kind:'rapid',name:`空移 ${t.fromOrder||numbers.get(t.from)||'?'} → ${t.toOrder||numbers.get(t.to)||'?'}`,number:'⇢'}));
  const combined=cutRows.flatMap(c=>[c,...rapidRows.filter(r=>r.from===c.id)]);
  const rows=kind==='cutting'?cutRows:kind==='rapid'?rapidRows:[...combined,...rapidRows.filter(r=>!cuts.some(c=>c.id===r.from))];
  const query=(ui.search||'').toLocaleLowerCase(),results=collisionPathResults(view),selected=selectedCollisionPath(view);
  const title=kind==='cutting'?'切割刀路':kind==='rapid'?'空移刀路':'全部刀路';
  return `<header class="five-cam-list-heading" data-five-key="cam-list-heading"><strong>${title}</strong><span>${rows.length} 条</span></header>
    <label class="five-search" data-five-key="cam-search"><input data-five-search data-five-key="cam-search-input" aria-label="查找刀路" value="${attr(ui.search||'')}" placeholder="名称 / 序号 / 固定编号"></label>
    <div class="five-path-list five-collision-path-list" data-five-key="collision-path-list" role="tabpanel" aria-label="${title}">${rows.filter(r=>!query||`${r.name} ${r.number} ${r.id}`.toLocaleLowerCase().includes(query)).map(r=>`<button type="button" class="five-tree-row ${selected===r.key?'selected':''} ${r.enabled===false?'is-disabled':''}" data-five-key="${r.kind==='rapid'?'rapid-':'select-c:'}${attr(r.id)}" ${r.kind==='rapid'?`data-five-rapid-id="${attr(r.id)}"`:`data-five-intent="${attr(r.id)}" data-five-node="c:${attr(r.id)}"`} aria-pressed="${selected===r.key}" ${view.pending||r.locked?'disabled':''}><span class="five-cut-order">${esc(r.number)}</span><span class="five-cut-name">${esc(r.name)}</span>${collisionPathBadge(results,r.key)}</button>`).join('')||'<p class="five-empty-row">没有匹配的刀路。</p>'}</div>`;
}
