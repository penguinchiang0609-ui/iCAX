import { workspaceState,activeTool } from './workspaceModel.mjs';
import { escapeText as esc,escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import { waitForTaskResult } from './taskProgressStream.mjs';
import {machiningTaskTimeoutMs} from './operationTiming.mjs';
const current=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const selected=view=>workspaceState(view).selectedIntentIds||[workspaceState(view).selectedIntentId].filter(Boolean);
export function beginPathOperation(view){view.fivePathOperationPending=true;view.pending=true;}
export function endPathOperation(view){view.fivePathOperationPending=false;view.pending=!!view.sceneRefreshPending;}
export function openPathEditing(view,mode){if(!['translate','rotate'].includes(mode))return false;view.fivePathEdit={mode,x:'0',y:'0',z:'0',cx:'0',cy:'0',cz:'0',axis:'Z',angle:'0'};view.error='';return true;}
export function pathEditField(view,name,value){if(!view.pending&&view.fivePathEdit&&!view.fivePathEdit.request){view.fivePathEdit[name]=value;if(['cx','cy','cz'].includes(name))view.fivePathEdit.centerEdited=true;}}
export function renderPathEditing(view){
 const d=view.fivePathEdit;if(!d||!['translate','rotate'].includes(d.mode)||activeTool(view)!==`path-${d.mode}`)return '';const disabled=view.pending||d.request?'disabled':'';
 const fields=(names,labels)=>names.map((name,i)=>`<label>${labels[i]}<input type="number" step="any" data-five-path-field="${name}" data-five-key="path-edit-${name}" aria-label="刀路${labels[i]}" value="${attr(d[name])}" ${disabled}></label>`).join('');
 return `<section data-five-key="path-edit" class="five-cut-parameters"><header>刀路${({translate:'平移',rotate:'旋转'})[d.mode]}</header><p>已选 ${selected(view).length} 条刀路 · 工件坐标系</p>${d.mode==='translate'?fields(['x','y','z'],['X 位移 (mm)','Y 位移 (mm)','Z 位移 (mm)']):fields(['cx','cy','cz'],['中心 X (mm)','中心 Y (mm)','中心 Z (mm)'])}${d.mode==='rotate'?`<label>旋转轴<select data-five-path-field="axis" ${disabled}>${['X','Y','Z'].map(axis=>`<option ${d.axis===axis?'selected':''}>${axis}</option>`).join('')}</select></label>${fields(['angle'],['角度 (°)'])}`:''}<p>只修改刀路，CAD 保持原样。</p>${view.error?`<p role="alert">${esc(view.error)}</p>`:''}<button data-five-command="five.path.apply" ${disabled||!selected(view).length?'disabled':''}>应用</button></section>`;
}
const number=value=>{if(String(value).trim()===''||!Number.isFinite(Number(value)))throw Error('请输入有效数值。');return Number(value);};
export function pathEditMatrix(d){
 if(!['translate','rotate'].includes(d.mode))throw Error('刀路仅支持平移和旋转。');
 const m=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
 if(d.mode==='translate'){for(let i=0;i<3;i++)m[i*4+3]=number(d['xyz'[i]]);return m;}
 const center=['cx','cy','cz'].map(key=>number(d[key]));
 const angle=number(d.angle)*Math.PI/180,c=Math.cos(angle),s=Math.sin(angle),axis='XYZ'.indexOf(d.axis);if(axis<0)throw Error('请选择旋转轴。');const a=(axis+1)%3,b=(axis+2)%3;m[a*4+a]=m[b*4+b]=c;m[a*4+b]=-s;m[b*4+a]=s;
 for(let i=0;i<3;i++)m[i*4+3]=center[i]-m.slice(i*4,i*4+3).reduce((sum,v,j)=>sum+v*center[j],0);return m;
}
export function pathEditError(error){const text=error?.message||String(error);for(const [code,message] of [
 ['CAM_CUT_RIGID_TRANSFORM_REQUIRED','刀路仅支持平移和旋转；标准孔尺寸请在属性面板中修改。'],['CAM_CUT_NOT_RECOGNIZED_STANDARD_HOLE','仅已识别的标准孔可以修改尺寸。'],
 ['MERGE_REQUIRES_TWO_PATHS','请先选择至少两条刀路。'],['MERGE_OUTSIDE_TOLERANCE','所选刀路的端点超出容差，无法连接成一条刀路。'],['MERGE_CLOSED_PATH','请选择开放刀路；闭合刀路请先打断。'],['MERGE_PROCESS_CONFLICT','所选刀路的工艺或前后指令不同，请统一后再合并。'],['MERGE_CORRECTION_CONFLICT','所选刀路的几何修正不同，请统一后再合并。'],['MERGE_SAME_SETUP','请选择同一装夹、同一工件的刀路。'],['MERGE_TOLERANCE_INVALID','容差必须是 0 至 1000 mm 的有效数值。'],['LOCKED','所选刀路已锁定。'],['REVISION_CONFLICT','工程已变化，请重新选择。']])if(text.includes(code))return message;return text;}
export async function commitPathEdit(context,view,method,request,ops){
 let result=await context.sceneProxy.invoke(method,request,{timeoutMs:machiningTaskTimeoutMs});
 await saveCommittedCutEdit(context,view,ops);
 if(result.taskId){const task=await waitForTaskResult(context,result,{active:()=>current(context,view),queryMethod:'CamWorkpiece.QueryTask',cancelMethod:'CamWorkpiece.CancelTask'});if(!task||task.status!=='Completed')throw Error(task?.error||'刀路编辑结果尚未生成。');result=task.result;await saveCommittedCutEdit(context,view,ops);}
 await ops.refreshSceneState(context,view);if(current(context,view)&&result.intentIds?.length){const ui=workspaceState(view);ui.selectedIntentIds=result.intentIds;ui.selectedIntentId=result.intentIds[0];}return result;
}
export async function pathEditCommand(context,view,action,ops){
 if(view.pending)return;const d=action==='merge'?(view.fiveMergeEdit??={}):view.fivePathEdit;if(!d)return;
 try{if(!d.request){const ids=selected(view);if(action==='merge'&&ids.length<2)throw Error('请先选择至少两条刀路。');if(!ids.length)throw Error('请先选择刀路。');d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),intentIds:[...ids],...(action==='merge'?{toleranceMm:number(view.fiveMergeTolerance??'0.1')}:{matrix:pathEditMatrix(d)})};}
 if(action==='merge'&&!d.request.visibleLayerId){const first=view.scene.intentToolpaths.find(c=>c.id===d.request.intentIds[0]);if(first?.layers?.visibleLayerId)d.request.visibleLayerId=first.layers.visibleLayerId;}
 beginPathOperation(view);view.error='';ops.renderProject(context,view);await commitPathEdit(context,view,action==='merge'?'CamCut.Merge':'CamCut.Transform',d.request,ops);d.request=null;if(action!=='merge')openPathEditing(view,d.mode);ops.appendProjectLog(context,'ok',action==='merge'?'所选刀路已合并为一条；引线和闭合工艺已清除，可重新设置':'刀路修正已保存');
 }catch(error){if(current(context,view))view.error=pathEditError(error);if(/CAM_|FIVEAXIS_|TOOLPATH_/.test(error.message))d.request=null;}
 finally{endPathOperation(view);if(current(context,view))ops.renderProject(context,view);}
}
