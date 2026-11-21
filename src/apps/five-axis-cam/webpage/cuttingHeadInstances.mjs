import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {button} from './workspaceControls.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {refreshMachiningState} from './domainActions.mjs';
import {axisDraft,axisInput,changeHeadStructure} from './headMotionDraft.mjs';
import {flushHeadColliderSaves} from './cuttingHeadColliders.mjs';
import {headWorkspaceSelection,hasHeadWorkspacePreview} from './headWorkspaceSelection.mjs';
import {headFitsOperation,headChoiceFitsOperation} from './headMachineCompatibility.mjs';

const setupOf=v=>v.scene?.domain?.setups?.find(s=>s.id===v.scene.domain.activeSetupId);
const asStrings=v=>v.map(String),cmd=(id,label,extras='')=>button('five.head-instance.'+id,label,extras);
function editor(view){
 const head=setupOf(view)?.headModel,instance=head?.instance,s=view.fiveHeadEditor??={entries:[],entryId:'',name:'',profileName:'',error:''};
 const key=JSON.stringify([instance?.id,instance?.revision]);
 if(key!==s.key){
  s.drafts??=new Map();
  if(s.instanceId!==instance?.id)s.renaming=false;
  if(s.instanceId&&s.instanceId!==instance?.id)s.drafts.set(s.instanceId,{revision:s.revision,draft:s.draft,dirty:s.dirty,name:s.name,nameDirty:s.nameDirty});
  const cached=s.instanceId===instance?.id?s:s.drafts.get(instance?.id),ack=s.applied;
  const keepDraft=cached?.dirty&&!(ack?.instanceId===instance?.id&&['apply','restore'].includes(ack.action)&&ack.editSerial===s.editSerial);
  const keepName=cached?.nameDirty&&!(ack?.instanceId===instance?.id&&ack.action==='rename'&&ack.nameSerial===s.nameSerial);
  s.key=key;s.instanceId=instance?.id;s.revision=instance?.revision;s.name=keepName?cached.name:instance?.name||'';s.nameDirty=!!keepName;
  const p=instance?.parameters;
  s.draft=keepDraft?cached.draft:p?{...structuredClone(p),tcp:asStrings(p.tcp),beam:asStrings(p.beam),gaugeOrigin:asStrings(p.gaugeOrigin),lengthMm:String(p.lengthMm),axes:p.axes.map((a,i)=>axisDraft({...a,name:a.name||head.assembly?.nodes[a.index]?.joint||'ABC'[i]}))}:null;s.dirty=!!keepDraft;s.applied=null;
 }return s;
}
function parseNumber(value){if(!String(value).trim()||!Number.isFinite(Number(value)))throw Error('请输入有效的标定数值。');return Number(value);}
export function headCalibrationInput(view){
 if(hasHeadWorkspacePreview(view))throw Error('请先将所选切割头用于当前工序。');
 const d=editor(view).draft;if(!d)throw Error('请先添加或选择工程刀头。');
 const p={tcpMode:d.tcpMode,tcp:d.tcp.map(parseNumber),beam:d.beam.map(parseNumber),gaugeOrigin:d.gaugeOrigin.map(parseNumber),lengthMm:parseNumber(d.lengthMm)};
 if(Math.hypot(...p.beam)<1e-9)throw Error('光束方向不能为零。');if(p.tcpMode==='Gauge'&&p.lengthMm<=0)throw Error('请填写从安装基准到切割点（TCP）的正刀长。');
 p.axes=d.axes.map(axisInput);const overrides=setupOf(view)?.headModel?.instance?.parameters?.collisionOverrides;if(overrides)p.collisionOverrides=structuredClone(overrides);return p;
}
export function renderHeadInstancePicker(view,builtinHeads=[],{list=false}={}){
 const s=editor(view),setup=setupOf(view),instance=setup?.headModel?.instance,instances=view.scene?.domain?.headInstances||[],bound=!!setup?.machineSetup?.machineId;
 const availableBuiltins=builtinHeads.filter(h=>headFitsOperation(view,h)),entries=s.entries.filter(h=>headFitsOperation(view,h));
 const busy=!!(view.pending||view.fiveSimulation?.busy||s.busy||(view.fiveCamPage==='head'&&(view.fiveHeadAssignments?.busy||view.fiveHeadAssignments?.request))),selectDisabled=!setup||bound||setup?.headModel?.collisionSource==='MachineInstance'||busy&&!view.fiveSimulation?.headChoiceBusy&&s.operation!=='select';
 const sources=s.sources??=new Map(),preferred=s.preferredHeads??=new Map();
 for(const setup of view.scene?.domain?.setups||[]){const h=setup.headModel,i=h?.instance;if(i){let kind=h.sourceAssembly?.kind||h.assembly?.kind;if(kind==='4+1')kind='A';if(kind==='3+2')kind='Fixed';sources.set(i.id,{builtinId:h.builtinDefinitionId,kind,profileId:i.profile?.id||s.profileLinks?.[i.id]});}}
 const catalogId=source=>source?.builtinId&&(builtinHeads.find(h=>h.id===source.builtinId)||builtinHeads.find(h=>h.kind===source.kind))?.id;
 const activeSource=sources.get(instance?.id),profileId=activeSource?.profileId;
 if(catalogId(activeSource)&&!profileId)preferred.set('builtin:'+catalogId(activeSource),instance.id);
 if(profileId)preferred.set('profile:'+profileId,instance.id);
 const current=profileId&&s.entries.some(e=>e.id===profileId)?'profile:'+profileId:catalogId(activeSource)&&!profileId?'builtin:'+catalogId(activeSource):instance?.id||'';
 s.currentChoice=current;
 const selected=list?(headWorkspaceSelection(view).choice??current):view.fiveSimulation?.queuedHeadChoice??view.fiveSimulation?.headChoiceValue??current;
 const option=(id,name)=>`<option value="${attr(id)}" ${id===selected?'selected':''}>${esc(name)}</option>`;
 s.choices=new Map();
 const match=(key,predicate)=>{const id=preferred.get(key),valid=i=>headFitsOperation(view,i)&&predicate(i),i=instances.find(i=>i.id===id&&valid(i))||instances.find(valid);if(i)s.choices.set(key,i.id);};
 const system=availableBuiltins.map(h=>{const key='builtin:'+h.id;match(key,i=>catalogId(sources.get(i.id))===h.id&&!sources.get(i.id)?.profileId&&!i.profile?.id);return option(key,h.name);}).join('');
 let mine=entries.map(e=>{const key='profile:'+e.id;match(key,i=>(sources.get(i.id)?.profileId||i.profile?.id)===e.id);return option(key,e.name);}).join('');
 // An import declined for the personal library remains usable for this choice.
 // Internal snapshots and copied instances are never catalog entries.
 const temporary=instance&&current===instance.id&&!profileId&&!s.deletedLibraryInstances?.has(instance.id)&&headFitsOperation(view,setup.headModel);
 if(temporary)mine+=option(instance.id,setup.headModel.name||instance.name);
 const row=(id,name,remove)=>`<div class="five-head-choice-row" data-five-key="head-choice-${attr(id)}"><button type="button" data-head-choice-value="${attr(id)}" aria-pressed="${id===selected}" ${list&&id===current?'aria-current="true"':''} ${selectDisabled||s.importPrompt?'disabled':''}><span>${esc(name)}</span>${list&&id===current?'<small class="five-head-used-badge">当前工序</small>':''}</button>${remove?cmd('delete-choice:'+encodeURIComponent(remove.id),'删除',`class="five-head-choice-delete" aria-label="删除 ${attr(name)}" ${busy||remove.disabled?'disabled':''} ${remove.disabled?'title="其他工序仍在使用此工程刀头"':''}`):''}</div>`;
 const mineRows=entries.map(e=>row('profile:'+e.id,e.name,{id:'profile:'+e.id})).join('')+(temporary?row(instance.id,setup.headModel.name||instance.name,{id:'instance:'+instance.id,disabled:instances.find(i=>i.id===instance.id)?.setupIds?.some(id=>id!==setup.id)}):'');
 if(list){
  s.choiceOpen=false;
  return `<section class="five-head-instance-picker five-head-catalog" data-five-key="head-instance-picker" aria-label="切割头列表"><select hidden aria-hidden="true" tabindex="-1" data-model-value-authoritative="true" data-five-simulation-field="head-choice" ${selectDisabled||s.importPrompt?'disabled':''}><option value="">未指定</option>${instance&&current===instance.id&&!temporary?option(instance.id,setup.headModel.name||instance.name):''}${system}${mine}<option value="import">导入切割头…</option></select>
  ${bound?`<p class="five-head-parameter-note" data-five-key="head-binding-note">当前切割头由绑定机床提供。</p>${row(instance?.id||'',headName(setup))}`:row('','未指定')}
  <section class="five-head-catalog-group" aria-label="系统" data-five-key="head-system-list"><h3>系统</h3>${availableBuiltins.map(h=>row('builtin:'+h.id,h.name)).join('')||'<p class="five-head-parameter-note">'+(view.fiveSimulation?.builtinHeadsLoading?'正在加载…':'暂无匹配本工序机型的切割头')+'</p>'}</section>
  <section class="five-head-catalog-group" aria-label="我的" data-five-key="head-personal-list"><h3>我的</h3>${mineRows||'<p class="five-head-parameter-note">暂无已保存的切割头</p>'}</section>
  ${renderHeadImportPrompt(view,s,busy)}</section>`;
 }
 const popup=s.choiceOpen?`<div id="five-head-choice-menu" class="five-head-choice-menu" data-head-choice-menu data-five-key="head-choice-menu" role="dialog" aria-label="选择切割头">${row('','未指定')}${system?`<section aria-label="系统"><h4>系统</h4>${availableBuiltins.map(h=>row('builtin:'+h.id,h.name)).join('')}</section>`:''}${mineRows?`<section aria-label="我的"><h4>我的</h4>${mineRows}</section>`:''}${row('import','导入切割头…')}</div>`:'';
 return `<div class="five-head-instance-picker" data-five-key="head-instance-picker"><label class="five-head-field-caption" for="five-current-head">切割头</label><div class="five-head-current-row"><div class="five-head-choice-picker" data-five-key="head-choice-picker"><select id="five-current-head" aria-label="切割头" aria-haspopup="dialog" aria-expanded="${!!s.choiceOpen}" aria-controls="five-head-choice-menu" data-model-value-authoritative="true" data-five-simulation-field="head-choice" ${selectDisabled||s.importPrompt?'disabled':''}><option value="">未指定</option>${instance&&current===instance.id&&!temporary?`<option value="${attr(instance.id)}" selected hidden>${esc(setup.headModel.name||instance.name)}</option>`:''}${system?`<optgroup label="系统">${system}</optgroup>`:''}${mine?`<optgroup label="我的">${mine}</optgroup>`:''}<option value="import" ${selected==='import'?'selected':''}>导入切割头…</option></select>${popup}</div>
 ${instance?`<details class="five-head-actions" data-five-key="head-instance-actions"><summary aria-label="刀头操作">⋯</summary><div role="menu">${cmd('library-open','管理刀头库',busy?'disabled':'')}</div></details>`:''}</div>
 ${renderHeadImportPrompt(view,s,busy)}</div>`;
}

const headName=setup=>setup?.headModel?.machineHeadName||setup?.headModel?.name||'机床切割头';
function renderHeadImportPrompt(view,s,busy){return s.importPrompt?`<section class="five-head-import-prompt" data-five-key="head-import-prompt" role="group" aria-label="导入后保存到我的"><strong>导入完成，是否添加到我的？</strong><label class="five-head-import-check"><input type="checkbox" data-five-head-instance-field="importSave" ${s.importPrompt.save?'checked':''} ${busy?'disabled':''}>添加到我的</label>${s.importPrompt.save?`<label>刀头名称<input data-five-key="head-import-name" data-five-head-instance-field="importName" value="${attr(s.importPrompt.name)}" ${busy?'disabled':''}></label>`:''}<div class="five-button-row">${cmd('finish-import','完成',busy||s.importPrompt.save&&!s.importPrompt.name.trim()?'disabled':'class="five-primary"')}</div></section>`:'';}
export function renderHeadInstanceEditor(view,head){
 const s=editor(view),instance=head?.instance;if(!instance)return '<p class="five-head-parameter-note">先选择切割头，再调整参数。</p>';
 const busy=!!(view.pending||view.fiveSimulation?.busy||s.busy),disabled=busy&&!s.busy&&!view.fiveHeadColliders?.saving?'disabled':'',textDisabled=disabled?'readonly':'',d=s.draft,bound=!!instance.binding?.machineId;
 const vector=(key,label,values)=>`<label class="five-head-vector" data-five-key="head-edit-${key}"><span>${esc(label)}</span>${values.map((v,i)=>`<input type="text" data-model-value-authoritative="true" inputmode="decimal" aria-label="${attr(label+' '+'XYZ'[i])}" data-five-key="head-edit-${key}-${i}" data-five-head-instance-field="${key}.${i}" value="${attr(v)}" ${textDisabled}>`).join('')}</label>`;
 const scalar=(key,label,value)=>`<label class="five-head-calibration-field" data-five-key="head-edit-${key}"><span>${esc(label)}</span><input type="text" data-model-value-authoritative="true" inputmode="decimal" aria-label="${attr(label)}" data-five-key="head-edit-${key}-input" data-five-head-instance-field="${key}" value="${attr(value)}" ${textDisabled}></label>`;
 const tool=`<label class="five-head-calibration-field"><span>TCP 定义</span><select data-five-head-instance-field="tcpMode" ${disabled}><option value="Coordinates" ${d.tcpMode==='Coordinates'?'selected':''}>局部坐标</option><option value="Gauge" ${d.tcpMode==='Gauge'?'selected':''}>安装基准＋刀长</option></select></label>
  <div class="five-head-coordinate-labels" aria-hidden="true"><span></span><span>X</span><span>Y</span><span>Z</span></div>${d.tcpMode==='Coordinates'?vector('tcp','TCP · mm',d.tcp):vector('gaugeOrigin','安装基准 · mm',d.gaugeOrigin)+scalar('lengthMm','刀长 · mm',d.lengthMm)}
  ${vector('beam','光束方向',d.beam)}<details class="five-head-help" data-five-key="head-coordinate-help"><summary>坐标与刀长说明</summary><p>${head.assembly?'坐标相对末级刀具部件。':'刚性外形使用模型坐标。'}光束从喷嘴指向工件。刀长从安装基准量至虚拟切割点。系统切割头的 TCP 位于实体喷嘴端面沿光束方向向外 0.2 mm。</p></details>`;
 const selected=d.axes[s.axis]?.target==='Head'?s.axis:d.axes.findIndex(a=>a.target==='Head');s.axis=Math.max(0,selected);const a=selected>=0?d.axes[selected]:null;
 const axisDirection=a?JSON.stringify(a.axis.map(Number)):'';
 const axes=bound?'<p class="five-head-parameter-note">当前刀头关节参数只读。</p>':a?`<section class="five-head-selected-axis" data-five-key="head-axis-fields-${a.index}"><header><strong>当前刀头关节</strong></header>
  ${scalar('axes.'+selected+'.name','关节名称',a.name)}
  <label class="five-head-calibration-field"><span>回转轴向</span><select aria-label="回转轴向" data-five-head-instance-field="axisDirection" ${disabled}>${[[[1,0,0],'绕 X'],[[0,1,0],'绕 Y'],[[0,0,1],'绕 Z'],[null,'自定义']].map(([v,label])=>`<option value='${v?JSON.stringify(v):'custom'}' ${(v?JSON.stringify(v)===axisDirection:!['[1,0,0]','[0,1,0]','[0,0,1]'].includes(axisDirection))?'selected':''}>${label}</option>`).join('')}</select></label>
  ${!['[1,0,0]','[0,1,0]','[0,0,1]'].includes(axisDirection)?vector('axes.'+selected+'.axis','轴向 XYZ',a.axis):''}
  <details class="five-head-advanced" data-five-key="head-axis-advanced-${a.index}"><summary>轴心、零偏与限位</summary><small>相对上一级刀头关节。</small>
  ${vector('axes.'+selected+'.position','轴心 · mm',a.position)}${vector('axes.'+selected+'.rotation','基准旋转 · °',a.rotation)}${scalar('axes.'+selected+'.zeroOffsetDeg','零偏 · °',a.zeroOffsetDeg)}
  <label class="five-head-calibration-field"><span>回转正向</span><select aria-label="回转正向" data-five-head-instance-field="axes.${selected}.sign" ${disabled}><option value="1" ${a.sign==='1'?'selected':''}>沿轴向</option><option value="-1" ${a.sign==='-1'?'selected':''}>反向</option></select></label>
  ${scalar('axes.'+selected+'.lowerDeg','下限 · °',a.lowerDeg)}${scalar('axes.'+selected+'.upperDeg','上限 · °',a.upperDeg)}${scalar('axes.'+selected+'.velocityDegS','速度上限 · °/s',a.velocityDegS)}
  </details></section>`:'<p class="five-head-parameter-note">当前刀头没有旋转关节。</p>';
 return `<section class="five-head-instance-editor" data-five-key="head-instance-editor">
 <details class="five-head-advanced" data-five-key="head-tcp" open><summary>切割点与刀长（TCP）</summary>${tool}</details>
 <details class="five-head-advanced" data-five-key="head-motion"><summary>刀头关节</summary>${axes}</details>
 <div class="five-head-editor-footer"><div class="five-button-row">${cmd('apply','应用修改',busy||!s.dirty?'disabled':'class="five-primary"')}${cmd('restore','恢复模型值',busy?'disabled':'')}</div><small>使用此刀头的 ${view.scene.domain.headInstances?.find(i=>i.id===instance.id)?.setupIds?.length||1} 道工序同步更新。</small></div>
 ${s.busy?'<p role="status">正在保存刀头配置…</p>':''}${s.error?`<p class="five-position-error" role="alert">${esc(s.error)}</p>${s.request?cmd('retry','重试原操作',disabled):''}`:''}

 </section>`;

}
export function renderHeadLibraryManager(view){
 const s=editor(view);if(!s.libraryOpen)return '';
 const busy=!!(view.pending||view.fiveSimulation?.busy||s.busy),disabled=busy?'disabled':'';
 const entries=s.entries.filter(h=>headFitsOperation(view,h));
 const library=`<div class="five-head-library-title"><strong>我的刀头</strong>${cmd('refresh-profiles','刷新',disabled)}</div><p class="five-head-parameter-note">这里管理匹配本工序机型、可在其他工程中重复使用的刀头。</p><label>配置<select aria-label="我的刀头配置" data-five-head-instance-field="entryId" ${disabled}><option value="">${entries.length?'请选择配置':'暂无匹配本工序机型的配置'}</option>${entries.map(e=>`<option value="${attr(e.id)}" ${s.entryId===e.id?'selected':''}>${esc(e.name)}</option>`).join('')}</select></label>
  ${entries.length?`<div class="five-button-row">${view.fiveCamPage==='head'?'':cmd('use-profile','载入配置',busy||!s.entryId||!headChoiceFitsOperation(view,'profile:'+s.entryId)||setupOf(view)?.machineSetup?.machineId?'disabled':'class="five-primary"')}${cmd('delete-profile','删除',busy||!entries.some(e=>e.id===s.entryId)?'disabled':'')}</div>`:''}
  <details class="five-head-save-profile" data-five-key="head-save-configuration"><summary>将当前刀头保存到我的</summary><label>配置名称<input data-five-key="head-profile-name" aria-label="保存的刀头配置名称" data-five-head-instance-field="profileName" value="${attr(s.profileName)}" ${disabled}></label>${cmd('save-profile','保存配置',busy||hasHeadWorkspacePreview(view)||s.dirty||!s.profileName.trim()?'disabled':'')}${s.dirty?'<small>请先应用参数。</small>':''}</details>`;

 return `<section class="five-head-library-manager" data-five-key="head-configuration-library" aria-label="管理我的刀头"><header><strong>管理我的刀头</strong>${cmd('library-close','关闭')}</header>${library}</section>`;
}
export function headInstanceField(context,view,key,value,ops){
 const s=editor(view);
 if(hasHeadWorkspacePreview(view)&&!['profileName','entryId','importSave','importName'].includes(key))return;
 if(key==='importSave'&&s.importPrompt){s.importPrompt.save=!!context.mount?.querySelector('[data-five-head-instance-field="importSave"]')?.checked;return;}
 if(key==='importName'&&s.importPrompt){s.importPrompt.name=value;return;}
 if(key==='instanceId'){
  if(s.busy||s.request){s.queuedInstance=value;return;}
  s.selectedInstance=value;void headInstanceCommand(context,view,'select',ops);return;
 }
 if(view.pending&&!s.busy&&!view.fiveHeadColliders?.saving)return;
 if(['name','profileName','entryId'].includes(key)){s[key]=value;if(key==='name'){s.nameDirty=true;s.nameSerial=(s.nameSerial||0)+1;}return;}
 if(!s.draft)return;
 if(key==='structure'){changeHeadStructure(s.draft,{assembly:setupOf(view).headModel.sourceAssembly||setupOf(view).headModel.assembly},value);s.axis=0;}
 else if(key==='axisDirection'){if(value==='custom')s.draft.axes[s.axis].axis=['0.7071','0.7071','0'];else s.draft.axes[s.axis].axis=JSON.parse(value).map(String);}
 else {const path=key.split('.');let current=s.draft;for(let i=0;i<path.length-1;i++)current=current[path[i]];if(!current||!Object.hasOwn(current,path.at(-1)))return;
 current[path.at(-1)]=value;if(path[0]==='axes'&&['position','rotation'].includes(path[2]))s.draft.axes[Number(path[1])].originDirty=true;}s.dirty=true;s.editSerial=(s.editSerial||0)+1;s.error='';
}
export async function headInstanceCommand(context,view,action,ops,{setupId:targetSetupId}={}){
 const s=editor(view),active=()=>!view.disposed&&context.isCurrentProject?.()!==false;
 if(!active())return;
 if(hasHeadWorkspacePreview(view)&&['apply','restore','save-profile'].includes(action))return;
 if(['rename-open','rename-cancel','rename','clone','references'].includes(action))return;
 if(action.startsWith('delete-choice:')){
  if(view.pending||s.busy||s.request)return;
  const choice=decodeURIComponent(action.slice('delete-choice:'.length)),id=choice.slice(choice.indexOf(':')+1);
  if(choice.startsWith('profile:')){if(!s.entries.some(e=>e.id===id))return;s.entryId=id;action='delete-profile';}
  else if(choice.startsWith('instance:')){
   if(!view.scene.domain.headInstances?.some(i=>i.id===id))return;
   if(setupOf(view)?.headInstanceId===id){s.selectedInstance='';await headInstanceCommand(context,view,'select',ops);if(!active()||s.error||s.request)return;}
   action='delete:'+id;
  }else return;
 }
 context.mount?.querySelector?.('[data-five-key="head-instance-actions"]')?.removeAttribute('open');
 if(action==='rename-open'){s.renaming=true;ops.renderProject(context,view);const input=context.mount?.querySelector?.('[data-five-head-instance-field="name"]');input?.focus({preventScroll:true});input?.select();return;}
 if(action==='rename-cancel'){s.name=setupOf(view)?.headModel?.instance?.name||'';s.nameDirty=false;s.renaming=false;ops.renderProject(context,view);return;}
  if(action.startsWith('axis:')){const target=hasHeadWorkspacePreview(view)?headWorkspaceSelection(view):s;target.axis=Number(action.slice(5));ops.renderProject(context,view);return;}
 if(action==='references'){s.references=!s.references;ops.renderProject(context,view);return;}
 if(action==='library-open'){s.libraryOpen=true;await headInstanceCommand(context,view,'refresh-profiles',ops);return;}
 if(action==='library-close'){s.libraryOpen=false;ops.renderProject(context,view);return;}
 if(action==='finish-import'){
  const prompt=s.importPrompt;if(!prompt||view.pending||s.busy)return;
  if(prompt.instanceId!==setupOf(view)?.headInstanceId){s.importPrompt=null;ops.renderProject(context,view);return;}
  if(prompt.save&&!prompt.savedEntryId){s.profileName=prompt.name;await headInstanceCommand(context,view,'save-profile',ops);if(s.error||s.request)return;}
  s.importPrompt=null;ops.renderProject(context,view);return;
 }
 if(action==='refresh-profiles'){
  if(s.libraryLoading)return;s.libraryLoading=true;
  try{const r=await context.sceneProxy.invoke('CamHead.Configurations',{action:'list'});if(active()){s.entries=r.items;s.libraryLoaded=true;}}
  catch(error){if(active())s.error=error.message;}finally{s.libraryLoading=false;if(active())ops.renderProject(context,view);}return;
 }
 if(['apply','restore','clone','save-profile','use-profile','select'].includes(action))await flushHeadColliderSaves(context,view,ops);
 if(!active()||(targetSetupId&&setupOf(view)?.id!==targetSetupId)||view.pending||s.busy)return;
 if(s.request&&action!=='retry'){s.error='上次操作结果尚未确认，请先重试原操作。';ops.renderProject(context,view);return;}
 const setupId=targetSetupId||view.scene.domain.activeSetupId;
 try{
  if(!s.request){
   if(action==='select'&&s.selectedInstance&&!headChoiceFitsOperation(view,s.selectedInstance)||action==='use-profile'&&!headChoiceFitsOperation(view,'profile:'+s.entryId))throw Error('所选切割头不适用于本工序机型。');
   const payload={commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId};let method='Instance';
   if(action==='rename')Object.assign(payload,{action:'rename',name:s.name});
   else if(action==='clone'||action==='restore')payload.action=action;
   else if(action==='select')Object.assign(payload,{action:'select',instanceId:s.selectedInstance});
   else if(action==='apply')Object.assign(payload,{action:'configure',parameters:headCalibrationInput(view)});
   else if(action.startsWith('delete:'))Object.assign(payload,{action:'delete',instanceId:action.slice(7)});
   else if(['save-profile','use-profile','delete-profile'].includes(action)){
    method='Configurations';const entry=s.entries.find(e=>e.id===s.entryId);
    Object.assign(payload,action==='save-profile'?{action:'save',name:s.profileName}:{action:action==='use-profile'?'use':'delete',entryId:s.entryId,revision:entry?.revision});
   }else if(action!=='retry')return;
   s.request={method,payload,action,instanceId:s.instanceId,editSerial:s.editSerial,nameSerial:s.nameSerial};
  }
  if(!s.request)return;const request=s.request;s.operation=request.action;s.busy=true;view.pending=true;s.error='';ops.renderProject(context,view);
  const result=await context.sceneProxy.invoke('CamHead.'+request.method,request.payload);s.request=null;s.applied=request;
  if(request.action==='save-profile'&&result?.id){s.profileLinks??={};s.profileLinks[request.instanceId]=result.id;if(s.importPrompt?.instanceId===request.instanceId)s.importPrompt.savedEntryId=result.id;}
  if(request.action==='delete-profile'){
   s.entries=s.entries.filter(e=>e.id!==request.payload.entryId);if(s.entryId===request.payload.entryId)s.entryId='';
   s.deletedLibraryInstances??=new Set();
   for(const [id,source] of s.sources||[])if(source.profileId===request.payload.entryId)s.deletedLibraryInstances.add(id);
   for(const [id,entryId] of Object.entries(s.profileLinks||{}))if(entryId===request.payload.entryId){s.deletedLibraryInstances.add(id);delete s.profileLinks[id];const source=s.sources?.get(id);if(source)delete source.profileId;}
  }
  const sceneEdit=request.method==='Instance'||request.payload.action==='use';
  if(sceneEdit)await saveCommittedProjectEdit(context,view,ops);
  if(active()){
   if(sceneEdit)await refreshMachiningState(context,view,ops);
   if(s.applied&&['apply','restore','rename'].includes(request.action)){s.key=null;editor(view);}
   if(request.action==='rename'&&!s.nameDirty)s.renaming=false;
   if(!sceneEdit||request.payload.action==='use'){s.libraryLoaded=false;await headInstanceCommand(context,view,'refresh-profiles',ops);}
   ops.appendProjectLog?.(context,'ok',request.method==='Configurations'&&request.payload.action==='save'?'刀头配置已另存到我的配置':'刀头实例已更新');
  }
 }catch(error){s.error=error.message;if(/CAM_|请输入|请填写/.test(s.error))s.request=null;}
 finally{
  s.busy=false;view.pending=false;if(active())ops.renderProject(context,view);
  if(active()&&!s.request&&s.queuedInstance!==undefined){const next=s.queuedInstance;delete s.queuedInstance;if(next!==setupOf(view)?.headInstanceId){s.selectedInstance=next;await headInstanceCommand(context,view,'select',ops);}}
  if(active()&&!s.request)view.fiveSimulation?.resumeHeadChoice?.();
 }
}
