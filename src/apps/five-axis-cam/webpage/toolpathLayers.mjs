import {camPathKind} from './workspaceModel.mjs';
import { escapeText as esc,escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { workspaceState, activeTool } from './workspaceModel.mjs';
import { mergeDomainState } from './domainActions.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import { sceneColors } from './scenePalette.mjs';
import { syncPathSelectionStroke } from './pathSelectionStroke.mjs';
import { tooltipAttributes } from './workspaceControls.mjs';

const state=view=>view.fiveLayers??={open:false,collapsed:false,drafts:{},busy:false,error:'',deleteId:'',targetId:''};
const catalog=view=>view.scene?.domain?.layers;
const selected=view=>workspaceState(view).selectedIntentIds??[workspaceState(view).selectedIntentId].filter(Boolean);
export const visibleToolpathLayers=view=>Object.values(catalog(view)?.catalog??{}).filter(l=>l.kind==='Visible').sort((a,b)=>Number(a.order)-Number(b.order)||a.id.localeCompare(b.id));
const cutFor=(view,id)=>(view.scene?.intentToolpaths??[]).find(c=>c.id===id);
export function toolpathLayerColor(view,id){const cut=cutFor(view,id),layer=catalog(view)?.catalog?.[cut?.layers?.visibleLayerId];return state(view).drafts[layer?.id]?.color??layer?.color??cut?.layers?.color??sceneColors.cutting;}
export function toolpathLayerVisible(view,id){const cut=cutFor(view,id),layer=catalog(view)?.catalog?.[cut?.layers?.visibleLayerId];return (layer?.visible??cut?.layers?.visible)!==false;}
export function toolpathLayerRevision(view){return JSON.stringify([catalog(view),Object.entries(state(view).drafts).map(([id,d])=>[id,d.color]),(view.scene?.intentToolpaths??[]).map(c=>[c.id,c.layers])]);}
export function syncToolpathLayerMaterials(view,group){const chosen=new Set(view.fiveAxisArea==='toolpath'&&camPathKind(view)!=='rapid'?selected(view):[]);group?.traverse(object=>{
 const data=object.userData??{};
 if(data.rapidTransitionId){
  const highlight=view.fiveAxisArea==='toolpath'&&camPathKind(view)==='rapid'&&activeTool(view)==='rapid'&&view.fiveRapid?.id===data.rapidTransitionId;
  const warning=data.rapidCollision?sceneColors.error:data.rapidUnverified?sceneColors.rapidUnverified:null;
  object.material.color.set(warning??(highlight?sceneColors.selected:sceneColors.reference));
  object.material.depthTest=!highlight;syncPathSelectionStroke(object,highlight,warning??sceneColors.selected);return;
 }
 let id=data.cuttingLineId??data.compensatedIntentId??data.generatedIntentId??data.cutDirectionId??data.intentId??data.toolpathLayerId;
 for(let parent=object.parent;!id&&parent&&parent!==group;parent=parent.parent)id=parent.userData?.toolpathLayerId;
 if(!id||!cutFor(view,id))return;object.visible=toolpathLayerVisible(view,id);
 const path=!!(data.cuttingLineId||data.compensatedIntentId||data.generatedIntentId||data.cutDirectionId),highlight=path&&!data.originalPath&&chosen.has(id);
 const color=highlight?sceneColors.selected:toolpathLayerColor(view,id);
 if(object.material?.color)object.material.color.set(color);if(object.material?.uniforms?.color)object.material.uniforms.color.value.set(color);
 if(object.isLine&&path)syncPathSelectionStroke(object,highlight);
});}
const commandButton=(action,label,extra='',description=label)=>`<button type="button" data-five-command="five.layer.${attr(action)}" ${tooltipAttributes(description)} ${extra}>${esc(label)}</button>`;
export function renderToolpathLayers(view){
 if(view.fiveAxisArea!=='toolpath'||!catalog(view)||!view.scene?.domain?.activeSetupId)return '';
 const s=state(view),layers=visibleToolpathLayers(view),ids=selected(view),selectedLayers=new Set(ids.map(id=>cutFor(view,id)?.layers?.visibleLayerId)),busy=s.busy||view.pending;
 const rows=layers.map(l=>{const common=ids.length?selectedLayers.size===1&&selectedLayers.has(l.id):catalog(view).visibleLayerId===l.id,description=ids.length?`将选中的 ${ids.length} 条刀路移到“${l.name}”`:`将“${l.name}”设为新建刀路的默认图层`;return `<button type="button" data-five-command="five.layer.choose:${attr(l.id)}" class="five-layer-swatch" data-five-key="layer-${attr(l.id)}" style="--layer-color:${attr(s.drafts[l.id]?.color??l.color)}" aria-label="${attr(ids.length?`移到${l.name}`:`当前图层：${l.name}`)}" aria-pressed="${common}" ${tooltipAttributes(description+(l.locked?'；该图层已锁定':'')+(l.visible===false?'；该图层已隐藏':''))} ${busy||l.locked?'disabled':''}><i aria-hidden="true"></i><span>${esc(l.name)}</span><b aria-hidden="true">${l.locked?'▣':l.visible===false?'◌':common?'✓':''}</b></button>`;}).join('');
 const toggleLabel=s.collapsed?'展开图层':'折叠图层';
 const strip=`<aside class="five-layer-strip${s.collapsed?' is-collapsed':''}" data-five-key="layer-strip" aria-label="刀路图层"><header data-five-key="layer-header"><strong>图层</strong>${commandButton('toggle',s.collapsed?'‹':'›',`class="five-layer-toggle" data-five-key="layer-toggle" aria-label="${toggleLabel}" aria-expanded="${!s.collapsed}"`,toggleLabel)}</header><div class="five-layer-swatches" data-five-key="layer-swatches" role="toolbar" aria-label="图层颜色" aria-orientation="vertical">${rows}</div><footer>${commandButton('add','＋',`aria-label="新增图层" ${busy?'disabled':''}`,'新增图层：可设置名称和刀路显示颜色')}${commandButton('manage','⚙','aria-label="管理图层"','管理图层：修改颜色、名称、显示和锁定状态')}</footer></aside>`;
 if(!s.open)return strip;
 const editing=layers.map(l=>{const d=s.drafts[l.id]??{};return `<div class="five-layer-row" data-five-key="layer-row-${attr(l.id)}"><input type="color" value="${attr(d.color??l.color)}" data-five-layer-field="color" data-five-layer-id="${attr(l.id)}" data-five-key="layer-color-${attr(l.id)}" aria-label="${attr(l.name)}配色" ${tooltipAttributes(`修改“${l.name}”的刀路显示颜色`)}><input type="text" value="${attr(d.name??l.name)}" maxlength="120" data-five-layer-field="name" data-five-layer-id="${attr(l.id)}" data-five-key="layer-name-${attr(l.id)}" aria-label="${attr(l.name)}名称" ${tooltipAttributes(`重命名“${l.name}”`)}>${commandButton(`visible:${l.id}`,l.visible===false?'◌':'◉',`aria-label="${l.visible===false?'显示':'隐藏'}${attr(l.name)}" ${busy?'disabled':''}`,`${l.visible===false?'显示':'隐藏'}“${l.name}”的刀路；不会删除刀路`)}${commandButton(`locked:${l.id}`,l.locked?'▣':'□',`aria-label="${l.locked?'解锁':'锁定'}${attr(l.name)}" ${busy?'disabled':''}`,l.locked?`解锁“${l.name}”，允许修改其刀路`:`锁定“${l.name}”，防止修改其刀路`)}${commandButton(`delete:${l.id}`,'×',`aria-label="删除${attr(l.name)}" ${busy||l.locked?'disabled':''}`,`删除“${l.name}”；有刀路引用时须先选择接收图层并迁移`)}</div>`;}).join('');
 const receiving=layers.filter(l=>l.id!==s.deleteId&&!l.locked);
 return `${strip}<section class="five-layer-manager" data-five-key="layer-manager" role="dialog" aria-label="图层管理"><header><strong>图层管理</strong>${commandButton('close','×','aria-label="关闭图层管理"','关闭图层管理')}</header><div class="five-layer-edit-list" data-five-key="layer-edit-list">${editing}</div>${s.deleteId?`<div class="five-layer-delete" data-five-key="layer-delete"><p>将“${esc(layers.find(l=>l.id===s.deleteId)?.name)}”的刀路和默认归属移到：</p>${receiving.length?`<select data-five-layer-target aria-label="接收图层" ${tooltipAttributes('选择接收刀路和默认归属的图层')}>${receiving.map(l=>`<option value="${attr(l.id)}" ${s.targetId===l.id?'selected':''}>${esc(l.name)}</option>`).join('')}</select>${commandButton('delete-confirm','迁移并删除',busy?'disabled':'','先迁移刀路和默认归属，再删除原图层')}`:'<p>请先新增一个接收图层。</p>'}${commandButton('delete-cancel','取消','','取消删除，保留原图层')}</div>`:''}<footer>${commandButton('add','＋ 新增图层',busy?'disabled':'','新增图层：可设置名称和刀路显示颜色')}<span>${s.busy?'正在保存…':`${layers.length} 个图层`}</span></footer>${s.error?`<p class="five-layer-error" role="alert">${esc(s.error)}</p>`:''}</section>`;
}
function errorText(error){const text=error?.message??String(error);for(const [code,message] of [['LAYER_DELETE_TARGET_REQUIRED','该图层仍有引用，请选择接收图层。'],['LAYER_NAME_REQUIRED','图层名称不能为空。'],['LAYER_NAME_INVALID','请输入有效的图层名称。'],['LAYER_COLOR_INVALID','请选择有效的颜色。'],['LOCKED','刀路或图层已锁定，请先解锁。'],['STALE','工程已变化，请刷新后重试。'],['REVISION_CONFLICT','工程已变化，请刷新后重试。']])if(text.includes(code))return message;return text;}
function commit(context,view,body,ops){const s=state(view);s.queued=(s.queued??0)+1;context.actions?.refreshProjectHistoryControls?.();const next=(s.queue??Promise.resolve()).then(()=>commitNow(context,view,body,ops)).finally(()=>{s.queued--;context.actions?.refreshProjectHistoryControls?.();});s.queue=next.catch(()=>false);return next;}
export async function flushToolpathLayerEdits(context,view,ops){const s=state(view);await s.queue;for(const [id,draft] of Object.entries(s.drafts)){const layer=catalog(view)?.catalog?.[id];if(!layer)continue;for(const field of ['name','color'])if(draft[field]!==undefined&&draft[field]!==layer[field]){const raw=draft[field],value=field==='name'?raw.trim():raw;if(!await commit(context,view,{action:'Update',layerId:id,properties:{[field]:value}},ops))throw Error(s.error||'图层修改尚未保存。');if(draft[field]===raw)delete draft[field];}}await s.queue;if(s.error)throw Error(s.error);}
async function commitNow(context,view,body,ops){
 const s=state(view);if(s.busy||view.pending)return false;
 const scene=view.scene,setupId=scene?.domain?.activeSetupId;if(!setupId)return false;
 const request={commandId:crypto.randomUUID(),expected:structuredClone(scene.domain.expected),setupId,expectedLayersFingerprint:catalog(view).fingerprint,...body};
 s.busy=true;s.error='';ops.renderProject(context,view);
 try{
  const result=await context.sceneProxy.invoke('CamCut.ManageLayers',request);
  if(view.disposed||context.isCurrentProject?.()===false)return false;
  const patch=await context.sceneProxy.invoke('CamCut.QueryLayers',{setupId,expected:result.expected});
  if(view.disposed||context.isCurrentProject?.()===false)return false;
  if(view.scene===scene&&JSON.stringify(scene.domain.expected)===JSON.stringify(request.expected)){
   const updates=new Map(patch.intentToolpaths.map(c=>[c.id,c]));for(const cut of scene.domain.intentToolpaths)if(updates.has(cut.id))Object.assign(cut,updates.get(cut.id));
   Object.assign(scene.domain,{layers:patch.layers,expected:patch.expected});mergeDomainState(scene,scene.domain);
  }else await ops.refreshSceneState(context,view);
  await saveCommittedCutEdit(context,view,ops);return true;
 }catch(error){if(!view.disposed&&context.isCurrentProject?.()!==false)s.error=errorText(error);return false;}
 finally{s.busy=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
export function toolpathLayerInput(view,target){const s=state(view),id=target.dataset.fiveLayerId,field=target.dataset.fiveLayerField;if(id&&field)(s.drafts[id]??={})[field]=target.value;}
export async function toolpathLayerChange(context,view,target,ops){
 if(target.hasAttribute('data-five-layer-target')){state(view).targetId=target.value;return;}
 const id=target.dataset.fiveLayerId,field=target.dataset.fiveLayerField;if(!id||!field)return;
 toolpathLayerInput(view,target);const rawValue=target.value,value=field==='name'?rawValue.trim():rawValue;
 const okay=await commit(context,view,{action:'Update',layerId:id,properties:{[field]:value}},ops),d=state(view).drafts[id];
 if(d&&d[field]===rawValue&&(okay||field==='color'))delete d[field];if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);
}
export async function toolpathLayerCommand(context,view,action,ops){
 const s=state(view);
 if(action==='toggle'){s.collapsed=!s.collapsed;ops.renderProject(context,view);return;}
 if(['manage','close','delete-cancel'].includes(action)){s.open=action==='manage'?!s.open:action==='close'?false:s.open;s.deleteId='';s.error='';ops.renderProject(context,view);return;}
 if(s.busy||view.pending)return;const [verb,id]=action.split(':'),layers=visibleToolpathLayers(view),layer=layers.find(l=>l.id===id);
 if(verb==='choose'){if(!layer)return;const ids=[...selected(view)];await commit(context,view,ids.length?{action:'Assign',layerId:id,intentIds:ids}:{action:'Default',layerId:id},ops);}
 else if(verb==='add'){let n=1;while(layers.some(l=>l.name===`图层 ${n}`))n++;const id=crypto.randomUUID();if(await commit(context,view,{action:'Create',layerId:id,properties:{name:`图层 ${n}`,color:'#3B82F6'}},ops)){s.open=true;ops.renderProject(context,view);}}
 else if(verb==='visible'||verb==='locked'){if(layer)await commit(context,view,{action:'Update',layerId:id,properties:{[verb]:verb==='visible'?layer.visible===false:!layer.locked}},ops);}
 else if(verb==='delete'){if(!layer)return;const used=id===catalog(view).visibleLayerId||(view.scene?.intentToolpaths??[]).some(c=>c.layers?.visibleLayerId===id);if(!used&&await commit(context,view,{action:'Delete',layerId:id},ops))return;s.open=true;s.deleteId=id;s.targetId=layers.find(l=>l.id!==id&&!l.locked)?.id??'';ops.renderProject(context,view);}
 else if(verb==='delete-confirm'&&s.deleteId&&s.targetId){if(await commit(context,view,{action:'Delete',layerId:s.deleteId,targetLayerId:s.targetId},ops)){s.deleteId='';s.targetId='';ops.renderProject(context,view);}}
}
