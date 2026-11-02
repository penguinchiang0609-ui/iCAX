import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { activeTool } from './workspaceModel.mjs';
import { button,tooltipAttributes } from './workspaceControls.mjs';
import { extractionDraft, extractionResult } from './extractionActions.mjs';
import {faceSelectionList,faceSelectionAction} from './faceSelectionPresentation.mjs';
import {EXTRACTION_BOUNDARIES} from './extractionScope.mjs';

export function renderExtraction(view) {
  const state=extractionDraft(view).extraction,result=extractionResult(view),busy=!!view.pending;
  const existing=new Set((view.scene?.intentToolpaths||[]).map(c=>c.sourceKey));
  const existingCount=result?.candidates.filter(c=>existing.has(c.sourceKey)).length||0;
  const unresolvedCount=result?.candidates.filter(c=>!existing.has(c.sourceKey)&&c.confirmationEligibility!=='Ready').length||0;
  const disabled=busy||state.confirmationRequest?'disabled':'',values=state.observation??['0','0','-1'];
  const title=state.mode==='Edges'?'选边':state.mode==='Region'?'选加工面':'批量识别';
  const direction=`<details class="five-extraction-direction-menu five-support-direction-menu" data-five-key="extraction-direction-menu"><summary aria-label="加工观察方向" ${tooltipAttributes('设置加工观察方向；展开时在模型上显示从观察者指向工件的箭头')} ${disabled?'aria-disabled="true" tabindex="-1"':''}><span aria-hidden="true">⌄</span></summary><div class="five-extraction-direction-popover"><strong>加工观察方向</strong><div class="five-direction-vector">${values.map((value,i)=>`<label><span>${'XYZ'[i]}</span><input type="text" inputmode="decimal" data-five-key="observation-${i}" data-five-extraction-direction="${i}" aria-label="加工观察方向 ${'XYZ'[i]}" value="${attr(value)}" ${disabled}></label>`).join('')}</div><p>从观察者指向工件 · 工件坐标<br>用于批量拾取表面和确定加工侧。</p></div></details>`;
  const faceLabel=state.mode==='Region'?'加工面':'非加工面集合';
  const list=faceSelectionList({ids:state.faces.map(f=>f.localId),selectedId:state.selectedFace,key:'extraction-faces',label:faceLabel+'列表',selectCommand:id=>'five.extraction.select-face.'+id,focusCommand:id=>'five.extraction.focus-face.'+id,removeCommand:id=>'five.extraction.remove.'+id,disabled,empty:state.mode==='Faces'?'手动选择工件表面，或按观察方向批量添加。':'手动选择连续的加工面。'});
  return `<section class="five-position-dialog five-extraction-panel" data-five-key="extraction-panel" data-five-tool="${activeTool(view)}" role="region" aria-label="${title}">
    <header class="five-position-title"><strong>${title}</strong>${button('five.extraction.close','×',`aria-label="退出${title}" ${state.confirming||state.confirmationRequest?'disabled':''}`)}</header>
    <div class="five-position-body">
      ${state.mode==='Edges'?`<div class="five-extraction-row"><span>闭环边</span>${button('five.extraction.pick',`选边（${state.edges.length}）`,`${disabled} aria-pressed="${!!view.fiveAxisExtractionPick}"`)}</div>`:`<section class="five-design-support ${view.fiveAxisExtractionPick?'is-picking':''}" data-five-key="extraction-face-selection"><header><h3>${faceLabel} <small>${state.faces.length}</small></h3><div class="five-design-support-actions">${faceSelectionAction('five.extraction.pick','手动修改','manual',`${disabled} aria-pressed="${!!view.fiveAxisExtractionPick}"`)}${state.mode==='Faces'?`<div class="five-support-auto">${faceSelectionAction('five.extraction.auto','批量添加','bulk',disabled)}${direction}</div>`:''}${faceSelectionAction('five.extraction.clear','清空','clear',disabled||!state.faces.length?'disabled':'')}</div></header>${list}</section>`}
      ${state.mode==='Edges'?'<p class="five-extraction-hint">靠近边线中段单击选边，再次单击取消。所选边将自动排序成一个闭环；每条边所点的面作为加工参考面。</p>':state.mode==='Region'?'<p class="five-extraction-hint">选择连续的加工面，提取该区域边界；内部公共边不参与切割。预览后可选择外边界、内孔或全部。</p>':''}
      ${view.fiveAxisExtractionPick&&state.mode!=='Edges'?`<div class="five-extraction-box-options" data-five-key="extraction-box-options"><label><input type="checkbox" data-five-key="extraction-occlusion" data-five-command="five.extraction.through" ${state.throughSelection?'':'checked'} ${disabled}>遮挡</label></div><p class="five-support-pick-hint">点击表面补选或取消；拖动框选。<br>左→右全包，右→左相交；Ctrl 追加，Alt 移除。</p>`:''}
      ${state.mode==='Edges'&&state.edges.length?`<div class="five-extraction-faces" data-five-key="extraction-edges">${state.edges.map(f=>`<span data-five-key="Edges-${attr(f.localId)}">边 ${esc(f.localId)}${button(`five.extraction.remove-edge.${f.localId}`,'×',`aria-label="移除边 ${attr(f.localId)}" ${disabled}`)}</span>`).join('')}</div>`:''}
      ${result&&state.mode==='Region'?`<div class="five-extraction-modes" role="group" aria-label="选择区域轮廓">${[['outer','外边界'],['inner','内孔'],['all','全部']].map(([mode,label])=>{const count=result.candidates.filter(c=>!existing.has(c.sourceKey)&&c.confirmationEligibility==='Ready'&&(mode==='all'||c.boundary.regionRelation===(mode==='outer'?'OuterLoop':'InnerLoop'))).length;return button(`five.extraction.filter.${mode}`,`${label} (${count})`,busy||!count?'disabled':'');}).join('')}</div>${result.candidates.some(c=>c.boundary.regionRelation==='Unknown')?'<p class="five-extraction-hint">部分空间闭环尚不能区分内外，请在左侧逐条勾选。</p>':''}`:''}
      ${result?`<p class="five-extraction-count" role="status">切割 ${result.candidates.filter(c=>state.selected.includes(c.candidateId)&&c.machiningKind==='Cut').length} · 打标 ${result.candidates.filter(c=>state.selected.includes(c.candidateId)&&c.machiningKind==='Mark').length}${existingCount?` · ${existingCount} 条已存在`:''}${unresolvedCount?` · ${unresolvedCount} 条待判定`:''}</p>`:''}
      ${view.error?`<div class="five-position-error" role="alert">${esc(view.error)}</div>`:''}
      ${state.confirming?`<p role="status" data-five-key="creation-status">正在创建 ${state.confirmationRequest?.candidateIds.length??state.selected.length} 项切割定义…</p>`:''}
    </div>
    <footer class="five-position-actions">${state.mode==='Faces'?`<div class="five-extraction-scope" data-five-key="extraction-scope" role="group" aria-label="识别范围">${EXTRACTION_BOUNDARIES.map(([id,label])=>`<label><input type="checkbox" data-five-extraction-scope="${id}" data-five-key="extraction-scope-${id}" value="${id}" ${state.boundaryScope==='all'||state.boundaryScope===id?'checked':''} ${disabled}>${label}</label>`).join('')}</div>`:button('five.extraction.run','预览轮廓',busy||state.confirmationRequest||!(state.mode==='Edges'?state.edges.length:state.faces.length)?'disabled':'')}${button('five.extraction.confirm',state.confirming?'正在创建…':'确定',busy||state.mode==='Faces'&&state.boundaryScope==='none'||(!state.confirmationRequest&&(result?!state.selected.length:!state.faces.length&&!(state.mode==='Edges'&&state.edges.length)))?'disabled':'class="five-primary"')}${button('five.extraction.close','取消',state.confirming||state.confirmationRequest?'disabled':'')}</footer>
  </section>`;
}

export function renderExtractionCandidates(view) {
  if(!view.fiveAxisExtractionOpen||!['extract','manual-extract'].includes(activeTool(view)))return '';
  const state=extractionDraft(view).extraction,result=extractionResult(view);
  const existing=new Set((view.scene.intentToolpaths||[]).map(c=>c.sourceKey));
  const candidates=(result?.candidates||[]).filter(c=>!existing.has(c.sourceKey));
  if(!candidates.length)return '<div class="five-empty-row" data-five-key="extraction-empty">选择非加工面集合、边线或加工面后，识别出的轮廓会显示在这里。</div>';
  return `<section class="five-candidate-list" data-five-key="extraction-candidates" aria-label="待添加轮廓"><div class="five-fixture-heading"><strong>待添加</strong><span>${state.selected.length} / ${candidates.length}</span></div><p class="five-extraction-hint">勾选轮廓后，点击右侧的“确定”添加刀路。</p>${candidates.map((c,i)=>{
    const ready=c.confirmationEligibility==='Ready',selected=state.selected.includes(c.candidateId);
    const label=({OuterTrim:'外轮廓',Circle:'圆孔',Slot:'腰形孔',Rectangle:'矩形孔'})[c.boundaryRole]||({Circle:'圆孔',Slot:'腰形孔',Rectangle:'矩形孔'})[c.geometryKind]||'轮廓';
    return `<button type="button" class="five-tree-row ${selected?'selected':''}" data-five-key="candidate-${attr(c.candidateId)}" data-five-candidate="${attr(c.candidateId)}" data-five-command="five.extraction.select.${attr(c.candidateId)}" role="checkbox" aria-checked="${selected}" ${view.pending||!ready?'disabled':''}><span aria-hidden="true">${selected?'☑':'☐'}</span><span><strong>${i+1} · ${label}</strong><small>${ready?(c.machiningKind==='Mark'?'打标':'切割'):'待判定'}</small></span></button>`;
  }).join('')}</section>`;
}
