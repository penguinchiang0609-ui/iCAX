import { activeTool, currentDraft, workspaceState, sceneItems } from './workspaceModel.mjs';
import { section, field, choices, button, hint, facts } from './workspaceControls.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';

const selected = view => sceneItems(view).intents.find(item => item.id === workspaceState(view).selectedIntentId);
const key = view => JSON.stringify([view.scene?.domain?.expected, selected(view)?.id]);
const current = (context, view) => !view.disposed && context.isCurrentProject?.() !== false;
const titles = { side:'留料侧' };
export const hasCutEditor = view => Object.hasOwn(titles,activeTool(view)) && !!selected(view)?.curveRef?.revision;
const sideName = side => ({ Interior:'保留内部 · 外轮廓', Exterior:'保留外部 · 内孔' })[side] || '未确定';
const errorText = error => /CAD_REFERENCE_NOT_FOUND/.test(error.message) ? '附近没有找到所选刀路，请靠近轮廓重新选点。'
  : /AMBIGUOUS/.test(error.message) ? '附近有多个同样接近的位置，请换一个位置选点。'
  : /STALE|EXPECTED|REVISION/.test(error.message) ? '工程已经变化，请读取当前切割定义后重试。' : error.message;

export async function loadCutDraft(context,view,ops,force=false) {
  if(!hasCutEditor(view)||view.pending)return;
  const d=currentDraft(view),snapshot=key(view),item=selected(view);
  if(d.cutRequest || (!force && d.cutDefinitionKey===snapshot))return;
  view.pending=true;view.error='';ops.renderProject(context,view);
  try {
    const response=await context.sceneProxy.invoke('CamCut.Read',{expected:view.scene.domain.expected,intentIds:[item.id]});
    if(!current(context,view)||snapshot!==key(view))return;
    d.cutDefinition=response.items[0];d.cutDefinitionKey=snapshot;
    if(!d.cutSideDirty)d.retainedSide=d.cutDefinition.retainedSide;
  } catch(error){if(current(context,view)&&selected(view)?.id===item.id)view.error=errorText(error);}
  finally{view.pending=false;if(current(context,view))ops.renderProject(context,view);}
}
export function cutDraftChanged(view,name) {
  if(!hasCutEditor(view))return;
  const d=currentDraft(view);
  if(name==='retainedSide'){d.cutSideRevision=(d.cutSideRevision||0)+1;d.cutSideDirty=true;}
}
export async function editCutCommand(context,view,action,ops) {
  if(action==='load'){await loadCutDraft(context,view,ops,true);return;}
  if(!hasCutEditor(view)||view.pending)return;
  const d=currentDraft(view),item=selected(view),tool=activeTool(view);
  if(d.cutDefinitionKey!==key(view)&&!d.cutRequest)throw Error('请先读取当前切割定义。');
  if(!d.cutDefinition?.closed)throw Error('当前编辑操作要求闭合刀路。');
  if(action!=='apply')return;
  if(!d.cutRequest) {
    const method='CamCut.SetRetainedSide',body={retainedSide:d.retainedSide};
    if(!['Circle','Rectangle','Slot'].includes(d.cutDefinition.geometryKind))throw Error('当前仅支持圆、矩形和腰型轮廓的留料侧编辑。');
    d.cutRequest={method,tool,sideRevision:d.cutSideRevision||0,
      payload:{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,intentIds:[item.id],...body}};
  }
  const submitted=d.cutRequest;view.pending=true;view.error='';ops.renderProject(context,view);
  try {
    try{await context.sceneProxy.invoke(submitted.method,submitted.payload);}
    catch(error) {
      let receipt;try{receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:submitted.payload.expected.epoch,commandId:submitted.payload.commandId});}catch{throw error;}
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.cutRequest=null;throw error;}
    }
    d.cutRequest=null;
    await saveCommittedCutEdit(context,view,ops);
    if(submitted.tool==='side'&&(d.cutSideRevision||0)===submitted.sideRevision)d.cutSideDirty=false;
    ops.appendProjectLog(context,'ok',`切割定义${titles[submitted.tool]}已保存，需重新生成刀路`);
    if(current(context,view))await ops.refreshSceneState(context,view);
    if(!current(context,view)||selected(view)?.id!==item.id)return;
    const snapshot=key(view),response=await context.sceneProxy.invoke('CamCut.Read',{expected:view.scene.domain.expected,intentIds:[item.id]});
    if(!current(context,view)||snapshot!==key(view))return;
    d.cutDefinition=response.items[0];d.cutDefinitionKey=snapshot;
    if(!d.cutSideDirty)d.retainedSide=d.cutDefinition.retainedSide;
  } catch(error){if(current(context,view)&&selected(view)?.id===item.id)view.error=errorText(error);}
  finally{view.pending=false;if(current(context,view))ops.renderProject(context,view);}
}
export function renderCutEditor(view,d) {
  const tool=activeTool(view),definition=d.cutDefinition,ready=d.cutDefinitionKey===key(view);
  let html=section('cut-current','工程切割定义',facts([['轮廓',definition?.closed?'闭合':'尚未读取'],
    ['当前行进方向',definition?(definition.reversed?'反向':'原始方向'):'尚未读取'],['当前留料侧',sideName(definition?.retainedSide)]])
    +(!ready?hint('工程已变化或尚未读取，请先读取当前切割定义。'):'')
    +button('five.cut.load','读取切割定义',view.pending||d.cutRequest?'disabled':''));
  if(!definition)return html;
  const available=ready&&definition.closed&&!view.pending&&!d.cutRequest;
  html+=section('cut-material','材料保留侧',field(d,'retainedSide','保留区域',choices([['Interior','保留内部 · 外轮廓'],['Exterior','保留外部 · 内孔']]))
    +hint('这里确定成品区域，反转行进方向不会交换保留区域。')
    +(!['Circle','Rectangle','Slot'].includes(definition.geometryKind)?hint('当前仅支持圆、矩形和腰型轮廓的留料侧编辑。'):''));
  const allowed=available&&['Circle','Rectangle','Slot'].includes(definition.geometryKind);
  html+=section('cut-apply','保存到工程',button('five.cut.apply',d.cutRequest?`重试上次提交（${titles[d.cutRequest.tool]}）`:'应用到工程',
    !view.pending&&(allowed||d.cutRequest)?'class="five-primary"':'disabled'));
  return html;
}
