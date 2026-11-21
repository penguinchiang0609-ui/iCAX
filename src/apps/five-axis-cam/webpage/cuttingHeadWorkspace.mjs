import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {button} from './workspaceControls.mjs';
import {renderHeadInstancePicker,renderHeadInstanceEditor,renderHeadLibraryManager} from './cuttingHeadInstances.mjs';
import {renderCuttingHeadPreview} from './cuttingHeadPreview.mjs';
import {renderHeadColliders} from './cuttingHeadColliders.mjs';
import {headWorkspaceSelection,hasHeadWorkspacePreview,headWorkspaceHead} from './headWorkspaceSelection.mjs';
import {headScopeState,headScopeLabels,headScopeCandidate,canSetHeadScope,canInheritHeadScope} from './headAssignmentScopes.mjs';

const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);

export function renderHeadWorkspaceList(view){
 const s=view.fiveSimulation||{};
 return `<header class="five-cam-list-heading" data-five-key="head-list-heading"><strong>切割头</strong></header>
  ${renderHeadInstancePicker(view,s.builtinHeads||[],{list:true})}${renderHeadLibraryManager(view)}`;
}

export function renderHeadWorkspaceParameters(view){
 const s=view.fiveSimulation||{},setup=setupOf(view),used=setup?.headModel,bound=!!setup?.machineSetup?.machineId||used?.collisionSource==='MachineInstance';
 // Initialize the editor from the effective snapshot before rendering fields.
 renderHeadInstancePicker(view,s.builtinHeads||[],{list:true});
 const selection=headWorkspaceSelection(view),preview=hasHeadWorkspacePreview(view),head=headWorkspaceHead(view),editor=view.fiveHeadEditor;
 const binding=renderHeadScopeBinding(view,setup,used,bound,selection);
 const retry=!preview&&s.request?.method==='Configure'&&s.request.payload.kind==='Builtin'?button('five.simulation.head-builtin','重试切换',view.pending||bound?'disabled':''):s.request?.method==='Import'?button('five.simulation.head-import','重试导入',view.pending||bound?'disabled':''):'';
 return `<section class="five-head-settings five-head-workspace-parameters" data-five-key="head-settings" aria-label="切割头参数"><h3 class="five-head-settings-title">切割头参数</h3>
  ${binding}<div class="five-head-name" title="${attr(head?.name||'')}"><span>${esc(preview&&selection.choice===''?'未指定':bound?head?.machineHeadName||'机床切割头':head?.name||'未选择切割头')}</span></div>
  ${preview?renderPreviewParameters(view,selection,head):renderHeadInstanceEditor(view,head)+renderHeadColliders(view)}
  ${preview&&editor?.error?`<p class="five-position-error" role="alert">${esc(editor.error)}</p>`:''}
  ${s.busy&&s.request?.method==='Configure'?'<div role="status" data-five-key="head-saving">正在更新切割头…</div>':''}
  ${s.headError?`<div class="five-position-error" role="alert">${esc(s.headError)}</div>${retry}`:''}
  ${view.pending&&s.task?.status==='Running'?`<div role="status">正在导入… ${button('five.simulation.head-cancel','取消')}</div>`:''}</section>`;
}

function renderHeadScopeBinding(view,setup,used,bound,selection){
 const state=headScopeState(view),scope=state.scope||'Setup',data=state.data,setting=data?.scopes?.[scope],effective=data?.effective;
 const busy=!!(view.pending||state.busy||view.fiveSimulation?.busy||view.fiveHeadEditor?.busy||selection.applying),requestAction=state.request?.payload?.action||state.request?.action;
 const hasUsedHead=!!(used?.instance||bound||used?.builtinDefinitionId||used?.previewRef),usedName=hasUsedHead?used.machineHeadName||used.name||effective?.name||'未指定':'未指定';
 const source=bound?'Machine':effective?.source,sourceLabels={Setup:'本工序单独指定',Workpiece:'继承本工件',Project:'继承本工程',Product:'继承本产品（本工程创建时）',None:'未指定',Machine:'由绑定机床提供'};
 const scopeName=headScopeLabels[scope]||'本工序',settingName=setting?.mode==='Instance'?setting.name||'已指定切割头':setting?.mode==='None'?'明确未指定切割头':scope==='Product'?'未设置产品默认':'继承上层设置';
 const labels={Setup:'用于本工序',Workpiece:'设为本工件默认',Project:'设为本工程默认',Product:'设为本产品默认'};
 const selectingNone=headScopeCandidate(view)?.kind==='None';
 const useLabel=requestAction==='set'?'重试原操作':selectingNone?(scope==='Product'?'新工程默认不指定刀头':scopeName+'不指定切割头'):labels[scope];
 const inheritLabel=requestAction==='inherit'?'重试原操作':scope==='Product'?'清除产品默认':'恢复继承';
 const useDisabled=busy||state.loading||!setup||!canSetHeadScope(view)||requestAction==='inherit',inheritDisabled=busy||state.loading||!setup||!canInheritHeadScope(view)||requestAction==='set';
 return `<section class="five-head-workspace-binding" data-five-key="head-workspace-current"><strong>${esc(setup?.name||'当前工序')}</strong>
  <span data-five-key="head-current-name">当前使用：${esc(usedName)}</span><small class="five-head-scope-source" data-five-key="head-current-source">来源：${esc(sourceLabels[source]||(state.loading?'正在读取…':'未指定'))}</small>
  <label class="five-head-scope-field" data-five-key="head-scope-field"><span>设置范围</span><select aria-label="刀头设置范围" data-five-key="head-scope-input" data-five-head-scope data-model-value-authoritative="true" ${busy||state.loading||state.request?'disabled':''}>${Object.entries(headScopeLabels).map(([value,label])=>`<option value="${attr(value)}" ${scope===value?'selected':''}>${esc(label)}</option>`).join('')}</select></label>
  <small class="five-head-scope-setting" data-five-key="head-scope-setting">${esc(scopeName)}设置：${esc(state.loading?'正在读取…':settingName)}</small>
  ${scope==='Product'?'<p class="five-head-scope-note" data-five-key="head-product-scope-note">仅用于以后新建工程；只供匹配机型使用，已有工程保持原配置。</p>':scope==='Setup'?'':'<p class="five-head-scope-note" data-five-key="head-parent-scope-note">保留更具体的单独指定，匹配机型的工序继承此默认。</p>'}
  <div class="five-head-scope-actions" data-five-key="head-scope-actions">${button('five.head-workspace.use',useLabel,useDisabled?'disabled':'class="five-primary"')}${button('five.head-workspace.inherit',inheritLabel,`class="five-head-inherit" ${inheritDisabled?'disabled':''}`)}</div>
  ${state.error||state.queryError?`<p class="five-position-error" role="alert" data-five-key="head-scope-error">${esc(state.error||state.queryError)}</p>${state.queryError?button('five.head-workspace.refresh','重试读取',busy?'disabled':''):''}`:''}</section>`;
}

function renderPreviewParameters(view,selection,head){
 if(selection.loading)return '<p role="status">正在加载所选刀头…</p>';
 if(selection.error)return `<p class="five-position-error" role="alert">${esc(selection.error)}</p>`;
 if(!head)return `<p class="five-head-parameter-note">点击上方按钮，将${esc(headScopeLabels[headScopeState(view).scope]||'本工序')}设置为未指定刀头。</p>`;
 const parameters=head.instance?.parameters,vector=v=>(v||[]).map(n=>String(Number(Number(n).toFixed(6)))).join('，');
 const axes=parameters?.axes||[],joints=axes.map(a=>`<div><dt>${esc(a.name||'关节')}</dt><dd>${esc(vector(a.axis))} · ${esc(a.lowerDeg)}° ～ ${esc(a.upperDeg)}°</dd></div>`).join('');
 return `<section class="five-head-readonly-preview" data-five-key="head-workspace-preview-parameters"><p class="five-head-parameter-note">正在预览。当前工序使用此刀头后，可编辑参数和碰撞盒。</p><dl><div><dt>TCP · mm</dt><dd>${esc(vector(head.tcpLocalPosition))}</dd></div><div><dt>光束方向</dt><dd>${esc(vector(head.beamLocalDirection))}</dd></div>${joints}</dl></section>`;
}

export function renderHeadWorkspacePreview(view){
 return `<div class="five-head-workspace-scene" data-five-key="head-workspace-scene">${renderCuttingHeadPreview(view)}</div>`;
}
