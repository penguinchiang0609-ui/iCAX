import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {placementWorldMatrix} from './setupWorldFrame.mjs';
import {toolpathLayerColor,syncToolpathLayerMaterials} from './toolpathLayers.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { activeTool, workspaceState } from './workspaceModel.mjs';
import { button } from './workspaceControls.mjs';
import { currentCompensation } from './cutCompensation.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import {pathPreview} from './pathPreviewTask.mjs';

const selected=view=>{const ui=workspaceState(view),ids=ui.selectedIntentIds||[ui.selectedIntentId];return (view.scene.intentToolpaths||[]).filter(c=>ids.includes(c.id));};
const scope=view=>JSON.stringify([view.scene?.domain?.expected,selected(view).map(c=>c.id)]);
export const isLeadTool=view=>['leads','overcut'].includes(activeTool(view));
const savedCuts=view=>(view.scene?.intentToolpaths||[]).filter(c=>c.enabled!==false&&c.hasTracePreview!==false);
const savedScope=view=>JSON.stringify([view.scene?.domain?.expected?.epoch,savedCuts(view).map(c=>[c.id,c.processRef,c.curveRef,c.featureRef,c.poseRef,c.sourceState]).sort((a,b)=>a[0].localeCompare(b[0]))]);
export function currentLeadPreview(view){
  const saved=view.fiveSavedLeads?.scope===savedScope(view)?view.fiveSavedLeads.preview:null;
  const draft=isLeadTool(view)&&view.fiveLeads?.scope===scope(view)?view.fiveLeads:null;
  if(!draft)return saved;
  if(draft.displaySaved!==saved||draft.displayDraft!==draft.preview||!draft.displayPreview){
    draft.displaySaved=saved;draft.displayDraft=draft.preview;
    draft.displayPreview={items:[...(saved?.items||[]).filter(i=>!draft.ids.includes(i.intentId)),...(draft.preview?.items||[])]};
  }
  return draft.displayPreview;
}
function syncSavedLeads(context,view,ops){
  const key=savedScope(view);if(view.pending||view.fiveSavedLeads?.scope===key)return;
  const state={scope:key,preview:null};view.fiveSavedLeads=state;
  const ids=savedCuts(view).filter(c=>!c.poseDeferred&&c.sourceState!=='Pending').map(c=>c.id);
  if(!ids.length)return;
  const valid=()=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveSavedLeads===state&&savedScope(view)===key;
  void (async()=>{
    const items=[];
    for(let offset=0;offset<ids.length&&valid();offset+=256){
      const batch=ids.slice(offset,offset+256),expected=view.scene.domain.expected;
      try{const result=await pathPreview(context.sceneProxy,'Leads',{expected,intentIds:batch},{valid});items.push(...result.items);}
      catch(error){
        // A timed-out batch can still be executing on the Scene channel. Do not
        // enqueue hundreds of duplicate reads behind it, or retry a stale scope.
        if(!valid()||/timed out|timeout|EXPECTED|SEQUENCE|STALE|CANCEL|DISPOSED/i.test(String(error?.message||error)))return;
        // One unsupported contour must not hide valid saved leads on other cuts.
        for(const id of batch){if(!valid())return;try{const result=await pathPreview(context.sceneProxy,'Leads',{expected,intentIds:[id]},{valid});items.push(...result.items);}catch{}}
      }
    }
    if(valid()){state.preview={items};ops.renderProject(context,view);}
  })();
}
const current=(context,view,state)=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveLeads===state&&state.scope===scope(view)&&isLeadTool(view);
const empty=()=>({kind:'None',length:'1',angle:'0',radius:'1'});
function draft(lead){return {kind:lead.kind,length:String(lead.lengthMm||1),angle:String(Number((lead.angleRad*180/Math.PI).toPrecision(10))),radius:String(lead.radiusMm||1)};}
function errorText(error){
  const message=String(error?.message||error);
  if(/CROSSES_MATERIAL/.test(message))return '引线越过边界进入材料，请缩短长度或调整角度。';
  if(/LEADS_REQUIRE_CUTTING_PATH/.test(message))return '引线仅适用于切割刀路。';
  if(/TRIM_CHECK_UNAVAILABLE/.test(message))return '该位置无法检查引线边界，请调整起点。';
  if(/CURVE_BOUNDARY_JUNCTION_OUTSIDE_TOLERANCE/.test(message))return '轮廓接缝超出模型允许误差，请检查加工面的边界连接。';
  if(/CAM_LEAD_OUTSIDE_REQUIRES_OPEN_CURVE/.test(message))return '板外引线仅适用于开放轮廓。';
  if(/CAM_LEAD_OUTSIDE_NOT_IMPLEMENTED/.test(message))return '板外引线尚未接入。';
  if(/GAP_EXCEEDS/.test(message))return '缺口长度必须小于轮廓周长。';
  if(/CLOSURE_REQUIRES|CLOSED_CURVE/.test(message))return '过切和缺口仅适用于闭合轮廓。';
  if(/CLOSURE_CONFLICT/.test(message))return '过切和缺口不能同时设置。';
  if(/TRACE/.test(message))return '无法沿这条轮廓计算收尾位置，请检查轮廓连接。';
  if(/ARC_PARAMETERS/.test(message))return '圆弧长度不能超过半圆弧长。';
  if(/FRAME_DEGENERATE|BOUNDARY_MISMATCH|SOURCE_MISSING/.test(message))return '轮廓的切向或板面方向读取失败，本次计算未完成。';
  if(/KERF_SIDE_AMBIGUOUS/.test(message))return '当前位置无法确定废料侧引线方向，请调整起点或刀轴。';
  if(/EXPECTED|SEQUENCE|STALE/.test(message))return '工程已变化，请重新打开引线。';
  return message;
}
export { errorText as leadErrorText };
export function leadToolSettings(view){
  return view.fiveLeadDefaults??={entry:{kind:'Line',length:'1',angle:'0',radius:'1'},exit:{kind:'Line',length:'1',angle:'0',radius:'1'}};
}
export function openLeadSettings(view){
  const defaults=leadToolSettings(view);
  view.fiveAxisLeadsOpen=true;view.error='';
  view.fiveLeads={settings:true,loaded:true,entry:{...defaults.entry},exit:{...defaults.exit},revision:0};
}
export async function applyLeadToCut(context,view,id,ops,retry=false){
  if(activeTool(view)!=='leads'||view.fiveAxisLeadsOpen)return false;
  if(view.pending||view.fiveLeads?.saving||view.fiveLeads?.request&&!retry)return true;
  const cut=view.scene.intentToolpaths?.find(c=>c.id===id);
  if(!cut||cut.machiningKind!=='Cut'||cut.locked||cut.enabled===false)return true;
  const defaults=leadToolSettings(view),s=retry?view.fiveLeads:{ids:[id]};s.saving=true;view.fiveLeads=s;view.error='';
  view.pending=true;ops.renderProject(context,view);
  try{
    const patch={leadIn:policy(defaults.entry),leadOut:policy(defaults.exit)};
    const request={commandId:crypto.randomUUID(),expected:view.scene.domain.expected,intentIds:[id],patch};
    // Validate the exact draft before publication, so an unsuitable lead does
    // not replace a usable cutting path with a failed graph result.
    if(!s.request){await pathPreview(context.sceneProxy,'Leads',{expected:request.expected,intentIds:[id],patch},{valid:()=>!view.disposed&&context.isCurrentProject?.()!==false});if(view.disposed||context.isCurrentProject?.()===false)return true;s.request=request;}
    try{await submitLeadEdit(context,s.request);}catch(error){if(error.leadEditNotFound)s.request=null;throw error;}s.request=null;
    await saveCommittedCutEdit(context,view,ops);await ops.refreshSceneState(context,view);
    ops.appendProjectLog(context,'ok',`已为刀路 #${cut.number} 设置引线`);
  }catch(error){view.error=errorText(error);}
  finally{s.saving=false;view.pending=false;if(!s.request)view.fiveLeads=null;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
  return true;
}
function policy(d){
  if(d.kind==='None')return {kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0};
  const number=(value,label)=>{if(!String(value).trim()||!Number.isFinite(Number(value)))throw Error(`请输入${label}。`);return Number(value);};
  const length=number(d.length,'长度');if(length<0||length>100000)throw Error('长度须在 0～100000 mm 之间。');
  if(length===0)return {kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0};
  const value={kind:d.kind,placement:'ScrapSide',lengthMm:length,angleRad:0};
  if(d.kind==='Arc'){value.radiusMm=number(d.radius,'半径');if(value.radiusMm<=0||value.radiusMm>100000||length>Math.PI*value.radiusMm)throw Error('半径须在 0～100000 mm 之间，弧长不能超过半圆。');}
  else {const angle=number(d.angle,'角度');if(Math.abs(angle)>=90)throw Error('角度须在 -90° 与 90° 之间。');value.angleRad=angle*Math.PI/180;}
  return value;
}
function patch(state){
  const length=Number(state.closure.length);
  if(state.closure.kind!=='Normal'&&(!String(state.closure.length).trim()||!Number.isFinite(length)||length<0||length>100000))throw Error('收尾长度须在 0～100000 mm 之间。');
  const result={overcutLengthMm:state.closure.kind==='Overcut'?length:0,gapLengthMm:state.closure.kind==='Gap'?length:0};
  if(!state.closureOnly)Object.assign(result,{leadIn:policy(state.entry),leadOut:policy(state.exit)});
  return result;
}
export async function submitLeadEdit(context,request){
  try{return await context.sceneProxy.invoke('CamProcess.Update',request);}
  catch(error){let receipt;try{receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});}catch{throw error;}
    if(receipt.status==='Completed')return receipt.result;
    if(receipt.status==='NotFound')error.leadEditNotFound=true;
    throw error;
  }
}
export function openLeads(view){view.fiveAxisLeadsOpen=activeTool(view)==='overcut';view.fiveLeads=null;view.error='';if(activeTool(view)==='leads')leadToolSettings(view);}
export function renderLeads(view){
  const s=view.fiveLeads,items=selected(view),locked=view.pending||!!s?.saving||!!s?.request||!s?.loaded;
  const fields=(role,title)=>{const d=s?.[role]||empty(),disabled=locked?'disabled':'';
    const input=(name,label,unit)=>`<label class="five-lead-field" data-five-key="lead-${role}-${name}"><span>${label}</span><input type="text" inputmode="decimal" data-five-lead-field="${role}.${name}" data-five-key="lead-${role}-${name}-input" value="${attr(d[name])}" ${disabled}><small>${unit}</small></label>`;
    return `<fieldset class="five-lead-section" data-five-key="lead-${role}"><legend>${title}</legend><label class="five-lead-field"><span>类型</span><select data-five-lead-field="${role}.kind" data-five-key="lead-${role}-kind" ${disabled}>${[['None','无'],['Line','直线'],['Arc','圆弧']].map(([v,t])=>`<option value="${v}" ${v===d.kind?'selected':''}>${t}</option>`).join('')}</select></label>${d.kind!=='None'?input('length',d.kind==='Arc'?'弧长':'长度','mm')+(d.kind==='Arc'?input('radius','半径','mm'):input('angle','角度','°')):''}</fieldset>`;
  };
  const closure=s?.closure||{kind:'Normal',length:'1'},title=s?.settings?'引线设置':activeTool(view)==='overcut'?'过切 / 缺口':'引线';
  const finish=`<fieldset class="five-lead-section" data-five-key="lead-closure"><legend>收尾</legend><label class="five-lead-field"><span>方式</span><select data-five-lead-field="closure.kind" data-five-key="lead-closure-kind" ${locked?'disabled':''}>${[['Normal','正常'],['Overcut','过切'],['Gap','缺口']].map(([v,t])=>`<option value="${v}" ${v===closure.kind?'selected':''}>${t}</option>`).join('')}</select></label>${closure.kind!=='Normal'?`<label class="five-lead-field" data-five-key="lead-closure-length"><span>长度</span><input type="text" inputmode="decimal" data-five-lead-field="closure.length" data-five-key="lead-closure-length-input" value="${attr(closure.length)}" ${locked?'disabled':''}><small>mm</small></label>`:''}</fieldset>`;
  return `<div class="five-position-dialog" data-five-key="leads-dialog" data-five-tool="${activeTool(view)}" role="dialog" aria-label="${title}"><header class="five-position-title"><strong>${title}</strong>${button('five.leads.close','×',`aria-label="关闭引线" ${view.pending||s?.saving||s?.request?'disabled':''}`)}</header>
    <div class="five-position-body five-leads-body"><div class="five-lead-selection" data-five-key="lead-selection">${s?.settings?'采用设置后，连续点击切割刀路应用；引线位于各刀路的废料侧。':items.length?`已选 ${items.length} 条刀路`:'请选择刀路'}</div>
    ${s?.mixed?'<div class="five-lead-mixed">参数不一致，当前显示首条；确定后统一应用。</div>':''}${activeTool(view)==='leads'?fields('entry','引入线')+fields('exit','引出线'):''}${s?.settings?'':finish}
    <div class="five-lead-status" data-five-key="lead-status" aria-live="polite">${esc(s?.error||view.error||(s?.previewBusy?'正在预览…':s?.loaded||!items.length?'':'正在读取…'))}</div></div>
    <footer class="five-position-actions">${button('five.leads.confirm',s?.settings?'采用设置':s?.request?'重试':'确定',locked&&!s?.request||(!s?.settings&&!items.length)||view.pending?'disabled':'class="five-primary"')}${button('five.leads.close','取消',view.pending||s?.saving||s?.request?'disabled':'')}</footer></div>`;
}
export function leadFieldChanged(context,view,name,value,ops){
  const s=view.fiveLeads;if(!s?.loaded||view.pending||s.saving||s.request)return;
  const [role,field]=name.split('.');if(!['entry','exit','closure'].includes(role)||!['kind','length','angle','radius'].includes(field))return;
  s[role][field]=value;s.revision++;s.edited=true;s.error='';s.mixed=false;s.preview=null;ops.renderProject(context,view);
  if(s.settings)return;
  clearTimeout(s.timer);s.timer=setTimeout(()=>{if(current(context,view,s)&&view.fiveAxisLeadsOpen)void preview(context,view,s,ops);},120);
}
async function preview(context,view,s,ops){
  const revision=s.revision;let body;
  try{body=patch(s);}catch(error){s.error=errorText(error);s.previewBusy=false;ops.renderProject(context,view);return;}
  s.previewBusy=true;ops.renderProject(context,view);
  try{const result=await pathPreview(context.sceneProxy,'Leads',{expected:view.scene.domain.expected,intentIds:s.ids,patch:body},{valid:()=>current(context,view,s)&&s.revision===revision});
    if(!current(context,view,s)||s.revision!==revision)return;
    s.preview=result;s.error='';
  }catch(error){if(current(context,view,s)&&s.revision===revision){s.error=errorText(error);s.preview=null;}}
  finally{if(current(context,view,s)&&s.revision===revision){s.previewBusy=false;ops.renderProject(context,view);}}
}
export function syncLeads(context,view,ops){
  if(view.fiveAxisArea==='simulation'){disposeLeadOverlay(view);return;}
  syncSavedLeads(context,view,ops);
  updateLeadOverlay(view);
  if(!isLeadTool(view)||activeTool(view)==='leads'||view.pending||view.fiveLeads?.saving||(!view.fiveAxisLeadsOpen&&!view.fiveLeads))return;
  const key=scope(view);if(view.fiveLeads?.scope===key)return;
  const s={scope:key,ids:selected(view).map(c=>c.id),entry:empty(),exit:empty(),closure:{kind:'Normal',length:'1'},closureOnly:activeTool(view)==='overcut',revision:0,loaded:false};view.fiveLeads=s;
  if(!s.ids.length){s.error='';return;}
  void (async()=>{
    try{const result=await context.sceneProxy.invoke('CamProcess.Read',{expected:view.scene.domain.expected,intentIds:s.ids});
      if(!current(context,view,s))return;
      const first=result.items[0].assignment;
      // Do not silently convert a previously stored mode that this editor cannot preview.
      if([first.leadIn,first.leadOut].some(l=>l.kind!=='None'&&l.placement!=='ScrapSide'))throw Error('板外引线尚未接入。');
      s.entry=draft(first.leadIn);s.exit=draft(first.leadOut);s.closure={kind:first.overcutLengthMm>0?'Overcut':first.gapLengthMm>0?'Gap':'Normal',length:String(first.overcutLengthMm||first.gapLengthMm||1)};s.loaded=true;
      const edited=a=>JSON.stringify([...(s.closureOnly?[]:[a.leadIn,a.leadOut]),a.overcutLengthMm,a.gapLengthMm]);
      s.mixed=result.items.some(item=>edited(item.assignment)!==edited(first));
      if(s.mixed){ops.renderProject(context,view);return;}await preview(context,view,s,ops);
    }catch(error){if(current(context,view,s)){s.error=errorText(error);ops.renderProject(context,view);}}
  })();
}
export async function leadCommand(context,view,action,ops){
  const s=view.fiveLeads;if(view.pending||s?.saving)return;
  if(action==='retry'&&s?.request){await applyLeadToCut(context,view,s.ids[0],ops,true);return;}
  if(action==='close'){if(s?.request)return;clearTimeout(s?.timer);view.fiveAxisLeadsOpen=false;view.fiveLeads=null;view.error='';ops.renderProject(context,view);return;}
  if(action==='confirm'&&s?.settings){
    try{policy(s.entry);policy(s.exit);view.fiveLeadDefaults={entry:{...s.entry},exit:{...s.exit}};view.fiveAxisLeadsOpen=false;view.fiveLeads=null;}
    catch(error){s.error=errorText(error);}ops.renderProject(context,view);return;
  }
  if(action!=='confirm'||!s?.loaded||!current(context,view,s))return;
  try{
    if(!s.request){const body=patch(s);s.request={commandId:crypto.randomUUID(),expected:view.scene.domain.expected,intentIds:s.ids,patch:body};}
  }catch(error){s.error=errorText(error);ops.renderProject(context,view);return;}
  clearTimeout(s.timer);s.revision++;s.saving=true;view.pending=true;s.error='';ops.renderProject(context,view);
  try{
    try{await submitLeadEdit(context,s.request);}catch(error){if(error.leadEditNotFound)s.request=null;throw error;}
    s.request=null;await saveCommittedCutEdit(context,view,ops);await ops.refreshSceneState(context,view);
    if(view.disposed||context.isCurrentProject?.()===false)return;
    s.scope=scope(view);s.preview=await pathPreview(context.sceneProxy,'Leads',{expected:view.scene.domain.expected,intentIds:s.ids},{valid:()=>current(context,view,s)});
    if(!current(context,view,s))return;
    view.fiveAxisLeadsOpen=false;s.previewBusy=false;ops.appendProjectLog(context,'ok',`已保存 ${s.ids.length} 条刀路的引线`);
  }catch(error){if(!view.disposed&&view.fiveLeads===s)s.error=errorText(error);}
  finally{s.saving=false;view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
export function disposeLeadOverlay(view){
  const group=view.fiveLeadOverlay?.group;if(group){group.removeFromParent();group.traverse(n=>{n.geometry?.dispose();n.material?.dispose();});}view.fiveLeadOverlay=null;
}
function updateLeadOverlay(view){
  const preview=currentLeadPreview(view);
  const compensated=currentCompensation(view),editing=isLeadTool(view)&&view.fiveAxisLeadsOpen;
  const v=view.viewport,frameKey=JSON.stringify((view.scene?.domain?.placements||[]).map(p=>[p.id,placementWorldMatrix(view,p).elements]));if(view.fiveLeadOverlay?.preview===preview&&view.fiveLeadOverlay?.viewport===v&&view.fiveLeadOverlay?.compensated===compensated&&view.fiveLeadOverlay?.editing===editing&&view.fiveLeadOverlay.frameKey===frameKey){syncToolpathLayerMaterials(view,view.fiveLeadOverlay.group);return;}
  disposeLeadOverlay(view);if(!v||!preview)return;const root=new THREE.Group();root.name='cut-leads';
  for(const item of preview.items){const placement=view.scene.domain.placements.find(p=>p.id===item.placementId);if(!placement)continue;
    if(!editing&&compensated.some(c=>c.intentId===item.intentId))continue;
    const group=new THREE.Group(),source=v.sceneObjects?.get(placement.id);if(source){source.updateWorldMatrix(true,false);group.matrix.copy(source.matrixWorld);}else group.matrix.copy(placementWorldMatrix(view,placement));group.matrixAutoUpdate=false;group.userData.toolpathLayerId=item.intentId;root.add(group);
    if(item.closure?.points?.length){
      const isGap=item.closure.kind==='Gap',geometry=new THREE.BufferGeometry().setFromPoints(item.closure.points.map(p=>new THREE.Vector3(...p)));
      const material=isGap?new THREE.LineDashedMaterial({color:0x86929c,dashSize:.4,gapSize:.35,depthTest:false}):new THREE.LineBasicMaterial({color:0xef4141,depthTest:false});
      const line=new THREE.Line(geometry,material);if(isGap)line.computeLineDistances();line.renderOrder=44;line.raycast=()=>{};line.userData={intentId:item.intentId,leadRole:item.closure.kind};group.add(line);
    }
    for(const lead of item.leads){const geometry=new THREE.BufferGeometry().setFromPoints(lead.points.map(p=>new THREE.Vector3(...p)));
      const line=new THREE.Line(geometry,new THREE.LineBasicMaterial({color:toolpathLayerColor(view,item.intentId),depthTest:false,depthWrite:false}));
      line.renderOrder=45;line.raycast=()=>{};line.userData={intentId:item.intentId,leadRole:lead.role};group.add(line);
      const p=lead.role==='LeadIn'?lead.points[0]:lead.points.at(-1),pointGeometry=new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...p)]);
      const marker=new THREE.Points(pointGeometry,new THREE.PointsMaterial({color:line.material.color,size:7,sizeAttenuation:false,depthTest:false}));marker.renderOrder=46;marker.raycast=()=>{};group.add(marker);
    }
  }v.scene.add(root);view.fiveLeadOverlay={group:root,preview,viewport:v,compensated,editing,frameKey};syncToolpathLayerMaterials(view,root);
}
