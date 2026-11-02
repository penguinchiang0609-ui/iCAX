import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { workspaceState } from './workspaceModel.mjs';
import { button } from './workspaceControls.mjs';
import { selectCut } from './cutSelection.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';

const current=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const key=view=>JSON.stringify(view.scene?.domain?.expected);
const selection=view=>(view.scene.intentToolpaths||[]).find(c=>c.id===workspaceState(view).selectedIntentId);
const dimensionFields={Circle:[['diameterMm','直径']],Ellipse:[['majorDiameterMm','长轴直径'],['minorDiameterMm','短轴直径']]};
export function openStandardHoles(view) {view.fiveHole={};view.fiveAxisHoleOpen=true;view.error='';}
function sync(view) {
  const state=view.fiveHole,cut=selection(view);
  if(state&&!state.request&&(state.intentId!==cut?.id||state.key!==key(view))) {
    state.intentId=cut?.id;state.cutType=cut?.cutType;state.key=key(view);delete state.manual;delete state.suggestions;view.error='';
  }
  return {state,cut};
}
export function renderStandardHoles(view) {
  const {state,cut}=sync(view),busy=!!view.pending||!!state?.request;
  return `<div class="five-position-dialog" data-five-key="standard-hole-dialog" data-five-tool="path-hole" role="dialog" aria-label="孔加工方式">
    <header class="five-position-title"><strong>孔加工方式</strong>${button('five.hole.close','×',`aria-label="关闭孔加工方式" ${busy?'disabled':''}`)}</header>
    <div class="five-position-body five-hole-body">
      <div class="five-field" data-five-key="hole-selection"><label for="five-hole-cut">刀路</label><select id="five-hole-cut" data-five-hole-field="intentId" data-five-key="hole-cut" ${busy?'disabled':''}>
        <option value="" ${!cut?'selected':''} disabled>请选择</option>${(view.scene.intentToolpaths||[]).map(c=>`<option value="${attr(c.id)}" ${c.id===cut?.id?'selected':''}>${esc(c.name)}</option>`).join('')}</select></div>
      <div class="five-field" data-five-key="hole-type"><label for="five-hole-type">加工方式</label><select id="five-hole-type" data-five-hole-field="cutType" data-five-key="hole-type-select" ${busy||!cut?'disabled':''}>
        <option value="StandardHole" ${state?.cutType==='StandardHole'?'selected':''} ${!cut?.canBeStandardHole&&!state?.manual?'disabled':''}>标准孔</option>
        <option value="Contour" ${state?.cutType!=='StandardHole'?'selected':''}>轮廓</option></select></div>
      ${state?.manual?`<div class="five-field"><label for="five-manual-hole-kind">孔型</label><select id="five-manual-hole-kind" data-five-hole-field="kind" ${busy?'disabled':''}>${[['Circle','圆孔'],['Ellipse','椭圆孔']].map(([id,label])=>`<option value="${id}" ${state.kind===id?'selected':''}>${label}</option>`).join('')}</select></div>${dimensionFields[state.kind].map(([field,label])=>`<div class="five-field" data-five-key="manual-hole-field-${field}"><label for="five-manual-hole-${field}">${label} (mm)</label><input id="five-manual-hole-${field}" type="number" step="any" min="0.000001" aria-label="指定标准孔${label}" data-five-hole-field="${field}" data-five-key="manual-hole-${field}" value="${attr(state.dimensions[field])}" ${busy?'disabled':''}></div>`).join('')}<p>尺寸为轮廓投影的参考值，请确认。指定孔型保留原轮廓和曲面起伏；之后可在属性中修改尺寸。</p>`:cut&&!cut.split?button('five.hole.manual:'+cut.id,'手动指定孔型',busy||cut.locked?'disabled':''):''}
      ${view.error?`<div class="five-position-error" role="alert">${esc(view.error)}</div>`:''}
    </div><footer class="five-position-actions">${button('five.hole.confirm',state?.request&&!view.pending?'重试':'确定',view.pending||!cut?'disabled':'class="five-primary"')}</footer>
  </div>`;
}
export function standardHoleChanged(view,name,value) {
  if(view.pending||view.fiveHole?.request)return;
  if(name==='intentId')selectCut(view,value);
  else if(name==='cutType'){sync(view);view.fiveHole.cutType=value;view.error='';}
  else if(view.fiveHole?.manual){const d=view.fiveHole;if(name==='kind'&&dimensionFields[value]){d.kind=value;d.dimensions={...d.suggestions[value]};}else if(dimensionFields[d.kind].some(([field])=>field===name))d.dimensions[name]=value;view.error='';}
}
export async function standardHoleCommand(context,view,action,ops) {
  if(action.startsWith('manual:')){
    if(view.pending)return;const cut=view.scene.intentToolpaths.find(c=>c.id===action.slice(7));if(!cut||cut.locked||cut.split)return;
    selectCut(view,cut.id);workspaceState(view).tools.toolpath='path-hole';openStandardHoles(view);const {state}=sync(view);view.pending=true;ops.renderProject(context,view);
    try{const result=await context.sceneProxy.invoke('CamCut.QueryStandardHole',{expected:structuredClone(view.scene.domain.expected),intentId:cut.id});if(!current(context,view)||view.fiveHole!==state)return;Object.assign(state,{manual:true,cutType:'StandardHole',kind:'Circle',suggestions:result.suggestions,dimensions:{...result.suggestions.Circle}});}
    catch(error){if(current(context,view))view.error=/CLOSED_REQUIRED/.test(error.message)?'请选择完整闭合的孔轮廓。':error.message;}
    finally{view.pending=false;if(current(context,view))ops.renderProject(context,view);}return;
  }
  if(action==='close'){if(view.pending||view.fiveHole?.request)return;view.fiveAxisHoleOpen=false;view.fiveHole=null;view.error='';ops.renderProject(context,view);return;}
  const {state,cut}=sync(view);if(action!=='confirm'||view.pending||!state||!cut)return;
  if(!state.request){
    let standardHole;
    if(state.manual&&state.cutType==='StandardHole'){
      const dimensions=Object.fromEntries(dimensionFields[state.kind].map(([field])=>[field,Number(state.dimensions[field])]));
      if(Object.values(dimensions).some(n=>!Number.isFinite(n)||n<=0)||state.kind==='Ellipse'&&dimensions.minorDiameterMm>dimensions.majorDiameterMm){view.error='请输入有效孔尺寸，椭圆长轴须大于或等于短轴。';ops.renderProject(context,view);return;}
      standardHole={kind:state.kind,dimensions};
    }
    state.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),intentIds:[cut.id],cutType:state.cutType,...(standardHole?{standardHole}:{})};
  }
  view.pending=true;view.error='';ops.renderProject(context,view);
  try {
    try{await context.sceneProxy.invoke('CamCut.SetType',state.request);}
    catch(error){
      const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:state.request.expected.epoch,commandId:state.request.commandId});
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')state.request=null;throw error;}
    }
    state.request=null;await saveCommittedCutEdit(context,view,ops);
    await ops.refreshSceneState(context,view);
    if(!current(context,view))return;
    if(view.fiveHole===state){view.fiveAxisHoleOpen=false;view.fiveHole=null;}
    ops.appendProjectLog(context,'ok','孔加工方式已保存');
  } catch(error) {
    if(current(context,view)&&view.fiveAxisHoleOpen)view.error=/NOT_ORIGINAL_STANDARD_HOLE/.test(error.message)?'这条轮廓不能转换为标准孔。'
      :/EXPECTED|STALE|SEQUENCE/.test(error.message)?'工程已变化，请重新选择。':error.message;
  } finally {view.pending=false;if(current(context,view))ops.renderProject(context,view);}
}
