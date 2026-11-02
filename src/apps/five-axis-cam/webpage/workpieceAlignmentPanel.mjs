import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {commandAvailability} from './commandAvailability.mjs';
import {sceneSnapStatus} from './sceneSnapping.mjs';
import {persistedWorkpieceFrame,frameStrings} from './workpieceFrame.mjs';

const labels={origin:'原点',x:'X 轴',y:'Y 轴',z:'Z 轴'};
const icon=kind=>`<svg viewBox="0 0 20 20" aria-hidden="true">${kind==='pick'?'<circle cx="10" cy="10" r="5"/><path d="M10 1v5m0 8v5M1 10h5m8 0h5"/>':kind==='flip'?'<path d="M3 7h12l-3-3m3 3-3 3M17 13H5l3-3m-3 3 3 3"/>':'<path d="M16 7a6 6 0 1 0 0 6M16 3v4h-4"/>'}</svg>`;
export function renderAlignment(view){
  const d=view.fiveAlignment,raw=d?.raw||frameStrings(persistedWorkpieceFrame(view)),state=commandAvailability({...view,fiveAlignment:null},'five.tool.align');
  const disabled=!!view.pending||!!state?.disabled||!!d?.request&&!d.saving,actionDisabled=disabled||!!d?.saving;
  const action=(id,label,kind,{off=actionDisabled,pressed=false}={})=>`<button type="button" data-five-key="align-${id}" data-five-command="five.align.${id}" title="${attr(label)}" aria-label="${attr(label)}" ${kind==='pick'?`aria-pressed="${pressed}"`:''} ${off?'disabled':''}>${icon(kind)}</button>`;
  const rows=Object.entries(labels).map(([key,label])=>`<div class="five-frame-row ${d?.selecting===key?'is-picking':''}" data-five-key="frame-row-${key}">
    <span class="${key==='origin'?'':'axis-'+key}" data-five-key="frame-label-${key}">${label}</span>
    ${raw[key].map((value,i)=>`<input type="text" inputmode="decimal" autocomplete="off" data-five-key="frame-${key}-${i}" data-five-frame-field="${key}-${'XYZ'[i]}" data-model-value-authoritative="true" aria-label="${label} ${'XYZ'[i]}" aria-invalid="${!!d?.fieldErrors?.[key]}" value="${attr(value)}" ${disabled||key==='y'?'readonly':''}>`).join('')}
    ${key==='y'?'<span class="five-frame-derived" title="Y 轴按 Z × X 自动计算，保持右手坐标系">自动</span>':`${action('pick.'+key,'拾取'+(key==='origin'?'原点':label+'方向'),'pick',{pressed:d?.selecting===key})}${key==='origin'?'<span aria-hidden="true"></span>':action('flip.'+key,'反向'+label,'flip',{off:actionDisabled||!!Object.keys(d?.fieldErrors||{}).length})}`}
  </div>`).join('');
  const prompt=d?.picking?'正在读取拾取结果…':d?.selecting?d.selecting==='origin'?'拾取原点：选择坐标系原点、端点、圆心或面上的点。':`拾取 ${labels[d.selecting]}方向：选择坐标轴、直边或平面法向。`:'';
  const snap=d?.selecting?sceneSnapStatus(view,d.placementId):'';
  const error=d?.error||Object.values(d?.fieldErrors||{})[0]||'';
  return `<section class="five-align-panel five-frame-properties" data-five-key="alignment-panel" aria-label="自身坐标系">
    <header><strong>自身坐标系</strong><small>原点 · mm</small></header>
    <div class="five-frame-columns" aria-hidden="true"><span></span><span>X</span><span>Y</span><span>Z</span><span></span><span></span></div>
    ${rows}
    ${prompt?`<p data-align-prompt data-five-key="align-prompt" data-align-default="${attr(prompt)}" role="status">${esc(prompt)}</p>`:''}
    ${snap?`<p data-five-key="snap-status" role="status">${esc(snap)}${action('retry-snaps','重试读取吸附特征','pick',{off:!!view.sceneSnapping?.pending.has(d.placementId)})}</p>`:''}
    ${error?`<p class="five-align-error" data-five-key="align-error" role="alert">${esc(error)}${d?.saveError?action('retry-save','重试保存坐标系','retry',{off:!!d.saving}):''}</p>`:''}
  </section>`;
}
export function renderAlignmentHint(view){
  return view.fiveAlignment?.selecting?'<div class="five-align-inference-hint" data-align-inference-hint data-five-key="align-inference-hint" hidden></div>':'';
}
