import {button} from './workspaceControls.mjs';
import {escapeText as esc} from '../../_shared/workbench/utils/format.mjs';

const pageSize=80;
const number=value=>Number.isFinite(Number(value))?Number(value).toFixed(3):'—';
const programOf=s=>s.report?.validity==='Current'&&s.report.playbackAllowed&&!s.dirty&&!s.postDirty&&s.programReport===s.report&&s.program?.motion?.machine?.modelKey===s.report.modelKey?s.program:null;
const instructionsOf=s=>programOf(s)?.motion?.instructions||[];
export const machineInstructionIndex=s=>Math.min(Math.max(0,(s.report?.frames?.length||1)-1),Math.ceil(s.cursor));
export function machineProgramCurrent(s){return !!programOf(s);}
export function machineProgramPage(s,action){
 const rows=instructionsOf(s);if(!rows.length)return;
 const pages=Math.ceil(rows.length/pageSize),page=Number(s.programPage)||0;
 s.programPage=Math.max(0,Math.min(pages-1,action==='current'?Math.floor(machineInstructionIndex(s)/pageSize):page+(action==='previous'?-1:1)));
}
export function renderMachineProgram(s){
 if(!s.programOpen)return '';
 const program=programOf(s),rows=instructionsOf(s);
 if(!program||!rows.length)return '';
 const axes=(program.motion?.postprocessor?.axes||[]).map(a=>a.word),page=Math.max(0,Math.min(Math.ceil(rows.length/pageSize)-1,Number(s.programPage)||0)),start=page*pageSize;
 const current=machineInstructionIndex(s);
 return `<section class="five-machine-program" data-five-key="machine-program" aria-label="机床运动指令"><header><div><strong>运动指令</strong><span>${esc(program.motion?.postprocessor?.name||'轴运动')} · ${rows.length-1} 段 · mm / ° / s</span></div>${button('five.machine.program-close','×','aria-label="关闭运动指令"')}</header><div class="five-machine-program-toolbar">${button('five.machine.program-previous','上一页',page<=0?'disabled':'')}${button('five.machine.program-current','当前执行位置')}${button('five.machine.program-next','下一页',start+pageSize>=rows.length?'disabled':'')}<span>第 ${page+1} / ${Math.ceil(rows.length/pageSize)} 页</span><span data-machine-instruction-progress>当前第 ${current} 段</span></div><div class="five-machine-program-table" data-five-key="machine-program-table"><table><thead><tr><th>运动段</th><th>动作</th><th>激光</th>${axes.map(a=>`<th>${esc(a)}</th>`).join('')}<th>用时 · s</th></tr></thead><tbody>${rows.slice(start,start+pageSize).map((row,offset)=>{const i=start+offset;return `<tr data-machine-instruction-row="${i}" ${i===current?'aria-current="step"':''}><td>${button('five.machine.program-seek:'+i,i?String(i):'初始位',`aria-label="定位到运动段 ${i}"`)}</td><td>${i===0?'初始位':row.process?'加工':row.role==='Approach'?'接近':'空移'}</td><td>${row.process?'开':'关'}</td>${axes.map(a=>`<td>${esc(number(row.axes?.[a]))}</td>`).join('')}<td>${esc(number(row.durationSeconds))}</td></tr>`;}).join('')}</tbody></table></div><p class="five-machine-program-note">指令来自本次已保存的机床轴运动计划；点击运动段可查看整机姿态。实机控制器与工艺参数尚需标定。</p></section>`;
}
export function paintMachineProgram(context,s){
 if(!s.programOpen||!programOf(s))return;
 const current=machineInstructionIndex(s),mount=context.mount;
 mount.querySelectorAll('[data-machine-instruction-row]').forEach(row=>{if(Number(row.dataset.machineInstructionRow)===current)row.setAttribute('aria-current','step');else row.removeAttribute('aria-current');});
 const progress=mount.querySelector('[data-machine-instruction-progress]');if(progress)progress.textContent=`当前第 ${current} 段`;
}
