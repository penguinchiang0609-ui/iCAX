import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {revealFaceListSelection} from './listNavigation.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { button } from './workspaceControls.mjs';
import { activeTool,selectTool } from './workspaceModel.mjs';
import {fixtureTaskRequestOptions,queryFixtureTask} from './fixtureTaskRequests.mjs';
import {faceSelectionList,selectedFaceOverlay} from './faceSelectionPresentation.mjs';
import {focusSelectedFace} from './faceSelectionFocus.mjs';
import {fixtureUnitDesign,loadFixtureCandidate,loadFixtureBreakdown} from './fixtureQueries.mjs';
const icons={manual:'m4 3 12 8-6 1-3 6-3-15Z M13 14l4 4',bulk:'M3 7h11v11H3z M7 3h11v11 M8 10v5 M5.5 12.5h5',clear:'M4 6h12 M8 3h4 M6 6l1 12h6l1-12 M9 9v6 M12 9v6',generate:'m11 2-7 10h6l-1 6 7-10h-6Z',confirm:'m4 10 4 4 9-9',cancel:'m5 5 10 10 M15 5 5 15'};
const iconButton=(command,label,icon,extra='',caption=false)=>`<button type="button" data-five-command="${attr(command)}" title="${attr(label)}" aria-label="${attr(label)}" ${extra}><svg viewBox="0 0 20 20" aria-hidden="true"><path d="${icons[icon]}"/></svg>${caption?`<span>${esc(label)}</span>`:''}</button>`;
const setup = view => view.scene?.domain?.setups?.find(s => s.id === view.scene.domain.activeSetupId);
export function attachFixtureDirectionMenu(mount,onVisibilityChange,selector='.five-support-direction-menu'){
  const doc=mount.ownerDocument,menus=()=>mount.querySelectorAll(`${selector}[open]`);
  const outside=event=>{for(const menu of menus())if(!menu.contains(event.target))menu.open=false;};
  const blur=event=>{const menu=event.target.closest?.(selector);if(menu&&!menu.contains(event.relatedTarget))menu.open=false;};
  const escape=event=>{if(event.key!=='Escape')return;const menu=event.target.closest?.(`${selector}[open]`);if(!menu)return;
    menu.open=false;menu.querySelector('summary')?.focus({preventScroll:true});event.preventDefault();event.stopPropagation();};
  const toggle=event=>{if(event.target.matches?.(selector))onVisibilityChange?.();};
  doc.addEventListener('pointerdown',outside,true);mount.addEventListener('focusout',blur);mount.addEventListener('keydown',escape,true);mount.addEventListener('toggle',toggle,true);
  return ()=>{doc.removeEventListener('pointerdown',outside,true);mount.removeEventListener('focusout',blur);mount.removeEventListener('keydown',escape,true);mount.removeEventListener('toggle',toggle,true);};
}
export const selectedFixtureUnit=view=>view.scene?.domain?.fixtureUnits?.find(u=>u.id===view.fiveAxisFixtures?.selected);
const storedDesign=view=>view.fiveFixtureEditingUnit?fixtureUnitDesign(view,view.scene?.domain?.fixtureUnits?.find(u=>u.id===view.fiveFixtureEditingUnit)):!view.fiveFixtureDesignOpen&&selectedFixtureUnit(view)?.kind==='Generated'?fixtureUnitDesign(view,selectedFixtureUnit(view)):setup(view)?.fixtureDesign||{};
export const fixtureDesign=view=>view.fiveFixtureDesignOpen&&view.fiveFixtureGenerated?view.fiveFixtureGenerated.design:storedDesign(view);
const draftKey=(view,recipe)=>JSON.stringify([selectedFixtureUnit(view)?.id,view.fiveFixtureDesignOpen,recipe]);
const supportStrategy=Object.freeze({layout:'Automatic',pattern:'Skeleton'});
const defaultRecipe=Object.freeze({...supportStrategy,spacing:220,baseShape:'Rectangle',sizing:'Automatic',areaWidth:240,areaDepth:240,areaX:0,areaY:0,postWidth:12,family:'ParallelX',material:'CarbonSteel',thickness:3,baseThickness:5,baseDrop:40,margin:15,gap:.2,minimumWeb:5,jointMode:'TabSlot',jointClearance:.2,tenonWidth:8,tenonPitch:60,jointWeb:1.5,tenonRadius:1,windowMode:'Automatic',windowWeb:10,openingReliefDepth:5,cutClearanceMode:'None',cutClearanceMm:2,beamRadiusMm:.5});
export function designDraft(view) {
  const id=setup(view)?.id, saved=storedDesign(view).recipe, key=draftKey(view,saved);
  if(!view.fixtureDesignDraft || view.fixtureDesignDraft.setupId!==id || (!view.fixtureRequest&&!view.fixtureDesignDraft.dirty && view.fixtureDesignDraft.key!==key)) {
    const r={...defaultRecipe,supportFaces:[],stations:[],...saved};
    const direction=r.supportDirection||[0,0,1];
    const stored=storedDesign(view),summary=stored.assembly?.layoutSummary;
    const supports=stored.assembly?.parts?.filter(p=>p.role==='Support').length,connections=stored.assembly?.parts?.filter(p=>p.role==='Connection').length;
    const strategyChanged=view.fiveFixtureDesignOpen&&saved&&(!saved.supportCount||Object.entries(supportStrategy).some(([k,v])=>r[k]!==v));
    view.fixtureDesignDraft={...r,...(view.fiveFixtureDesignOpen?supportStrategy:{}),supportCount:r.supportCount??supports??'',connectionCount:r.connectionCount??connections??'',supportCountEdited:false,connectionCountEdited:false,positionMode:r.supportPositions?.length||r.connectionPositions?.length?'Manual':'Automatic',supportPositionText:(r.supportPositions?.length?r.supportPositions:summary?.supportPositions||[]).join(', '),connectionPositionText:(r.connectionPositions?.length?r.connectionPositions:summary?.connectionPositions||[]).join(', '),layoutInfo:summary||null,supportX:String(direction[0]),supportY:String(direction[1]),supportZ:String(direction[2]),setupId:id,key,revision:0,dirty:!!strategyChanged,positions:r.stations.map(s=>s.coordinate).join(', ') || '-40, 0, 40',stations:r.stations.map(s=>({...s})),supportFaces:[...r.supportFaces]};
  }
  return view.fixtureDesignDraft;
}
export function designChanged(view,key,value){const d=designDraft(view);d[key]=value;if(key==='supportCount'||key==='connectionCount')d[key+'Edited']=true;d.dirty=true;++d.revision;}
export function supportDirection(d){
  const values=['supportX','supportY','supportZ'].map(k=>String(d[k]??'').trim());
  if(values.some(s=>!s||!Number.isFinite(Number(s))))throw Error('请填写有效的支撑方向向量。');
  const numbers=values.map(Number),scale=Math.max(...numbers.map(Math.abs));if(!scale)throw Error('支撑方向不能为零向量。');
  const vector=numbers.map(n=>n/scale),length=Math.hypot(...vector);return vector.map(n=>n/length);
}
const num=(d,k)=>{if(String(d[k]).trim()===''||!Number.isFinite(Number(d[k])))throw Error('请填写有效的夹具尺寸。');return Number(d[k]);};
function recipe(d,allowRecommendation=false) {
  const r={...supportStrategy,family:d.family,material:d.material,supportDirection:supportDirection(d),supportFaces:d.supportFaces,stations:[],baseShape:d.baseShape,sizing:d.sizing,jointMode:d.jointMode,windowMode:d.windowMode,cutClearanceMode:d.cutClearanceMode};
  for(const k of ['thickness','baseThickness','baseDrop','margin','gap','minimumWeb','openingReliefDepth'])r[k]=num(d,k);
  const inactive=k=>Number.isFinite(Number(d[k]))?Number(d[k]):0;
  const count=k=>{if(allowRecommendation&&(String(d[k]??'').trim()===''||Number(d[k])===0))return 0;const n=num(d,k);if(!Number.isInteger(n)||n<2||n>32)throw Error('横向板和纵向板数量须为 2～32 的整数，总数不超过 40。');return n;};
  r.supportCount=count('supportCount');r.connectionCount=count('connectionCount');if(r.supportCount+r.connectionCount>40)throw Error('横向板和纵向板总数不能超过 40。');r.postWidth=inactive('postWidth');
  const positions=(key,n)=>{if(d.positionMode!=='Manual')return [];const value=String(d[key]??'').trim();const values=value?value.split(/[,，\s]+/).map(Number):[];if(values.length!==n||values.some(v=>!Number.isFinite(v)))throw Error('板件位置数量须与指定的板件数量一致，请用逗号分隔有效坐标。');return values;};
  r.supportPositions=positions('supportPositionText',r.supportCount);r.connectionPositions=positions('connectionPositionText',r.connectionCount);
  r.areaWidth=d.sizing==='Manual'?num(d,'areaWidth'):inactive('areaWidth');r.areaDepth=d.sizing==='Manual'&&d.baseShape==='Rectangle'?num(d,'areaDepth'):inactive('areaDepth');
  r.areaX=d.sizing==='Manual'?num(d,'areaX'):inactive('areaX');r.areaY=d.sizing==='Manual'?num(d,'areaY'):inactive('areaY');
  for(const k of ['jointClearance','tenonWidth','tenonPitch','jointWeb','tenonRadius'])r[k]=d.jointMode==='TabSlot'?num(d,k):inactive(k);
  r.windowWeb=d.windowMode==='Automatic'?num(d,'windowWeb'):inactive('windowWeb');
  for(const k of ['cutClearanceMm','beamRadiusMm'])r[k]=d.cutClearanceMode==='Toolpath'?num(d,k):inactive(k);
  if(!r.supportFaces.length)throw Error('请先在工件上选择支撑面。');
  return r;
}
const errors={PLATE_SUPPORT_BRANCH_OR_GAP:'选中的面存在多个截面分支或断口，请缩小支撑面范围或调整板片位置。',PLATE_SUPPORT_NOT_SINGLE_VALUED:'该截面存在竖壁或反折，当前平行支撑板不能直接生成，请重新选择支撑面。',PLATE_STATION_NO_SUPPORT:'某块板没有穿过选中的支撑面，请调整板片位置。',PLATE_STATION_OVERLAP:'板片位置须从小到大排列，间距须大于板厚。',PLATE_MINIMUM_WEB:'底板高度不足以保留最小板高。',PLATE_CLEARANCE_UNREACHABLE:'无法同时满足工件间隙和最小板高，请增大底板下移距离或调整支撑面。',PLATE_DIMENSION_INVALID:'夹具尺寸超出范围，请检查板厚、间隙和底板位置。',CAM_FIXTURE_DESIGN_STALE:'工件、夹具或参数已变化，请重新生成并采用夹具。'};
errors.PLATE_SOLID_INVALID='支撑板实体校验失败，未生成有效板件。';
errors.PLATE_OPENING_DEPTH_INVALID='孔边避空深度须在 0.1～1000 mm 之间。';
errors.PLATE_SKELETON_INSUFFICIENT='可用承托位置无法满足最大跨度或连接要求，请增加承托面、调整跨度或避让范围。';
errors.PLATE_BASE_PHYSICAL_CONFLICT='切割头或其他实体的避让范围与实心底板冲突，请调整布局。';
errors.PLATE_CONNECTION_CLEARANCE='连接板与工件间隙不足，请调整底座高度或支撑布置。';
errors.PLATE_MESH_LIMIT='工件曲面网格超出夹具计算容量，本次预览未完成。';
errors.PLATE_SELECTED_SUPPORT_OBSTRUCTED='选中的支撑面被其他面遮挡，板片无法从底板接近它，请重新选择实际下侧支撑面。';
errors.PLATE_SUPPORT_TOO_NARROW='板片穿过支撑面的范围过窄，请调整板片位置或增加支撑面。';
errors.PLATE_KEEPOUT_NO_SUPPORT='当前支撑位置均与切割避让冲突，请调整支撑间距或区域。';
errors.PLATE_KEEPOUT_INSUFFICIENT_SUPPORT='避让后的承托分布不足，请调整支撑间距或增加可用承托面。';
errors.PLATE_JOINT_ABOVE_SUPPORT='榫卯连接板带超过支撑上沿，请减小连接板带或增大底板下移距离。';
Object.assign(errors,{PLATE_WINDOW_MODE_INVALID:'请选择板件开窗方式。',PLATE_WINDOW_WEB_INVALID:'开窗保留板带须不小于支撑厚度，且不超过 500 mm。',PLATE_WINDOW_DISCONNECTED:'开窗会切断板件，请增大保留板带或调整支撑分布。'});
Object.assign(errors,{PLATE_WINDOW_LIMIT:'通风孔数量超出范围，请增大保留板带或缩小底座。',PLATE_OPENING_MARGIN_INVALID:'孔界预避让间隙无效，请重新生成。',PLATE_OPENING_OUTER_WIRE_MISSING:'无法辨认工件面的外边界，请检查工件几何。',PLATE_OPENING_BOUNDARY_INVALID:'工件孔边界无效，请修复工件几何。',PLATE_OPENING_BOUNDARY_UNBOUNDED:'无法取得完整孔边界范围，请检查工件几何。'});
Object.assign(errors,{CAM_FIXTURE_CUT_CLEARANCE_MODE_INVALID:'请选择切割避让方式。',CAM_FIXTURE_CUT_CLEARANCE_DIMENSION_INVALID:'请填写有效的切割间隙和光束半径。',CAM_FIXTURE_CUT_CLEARANCE_LIMIT:'刀路避让规模超出当前计算范围，请缩小本次装夹的加工范围。',CAM_FIXTURE_CUT_POSES_REQUIRED:'切割刀路缺少有效姿态，请先完善刀路。',CAM_FIXTURE_CUT_SERVICES_REQUIRED:'切割避让服务未就绪，无法生成。',CAM_FIXTURE_HEAD_BOUNDS_REQUIRED:'无法取得切割头外形，不能计算安全避让。',CAM_FIXTURE_SOURCE_BOUNDS_REQUIRED:'无法取得工件范围，不能计算贯穿光路。',CAM_GENERATE_NO_CUTS:'请先创建本次装夹的切割刀路。',CAM_COLLISION_HEAD_CALIBRATION_REQUIRED:'请先标定切割头的刀尖位置和光束方向。'});
Object.assign(errors,{PLATE_KEEPOUT_INVALID:'孔洞或光路的避让范围无效，请检查工件与加工设置。',PLATE_KEEPOUT_LIMIT:'避让范围过多，请缩小本次装夹范围。',PLATE_KEEPOUT_EMPTY:'避让范围覆盖了整块板，请调整支撑布置。',PLATE_KEEPOUT_DISCONNECTED:'避让会切断板件，请调整支撑布置。',PLATE_KEEPOUT_MINIMUM_WEB:'避让后的板带不足，请调整支撑布置或底座范围。',PLATE_KEEPOUT_CONFLICT:'连接位置仍占用避让空间，请调整支撑布置。',PLATE_KEEPOUT_FOOT_UNSUPPORTED:'避让后底板无法承托支撑板，请调整底座或支撑位置。',PLATE_KEEPOUT_LAP_WEB:'避让空间与交叉接头冲突，请调整支撑间距。'});
Object.assign(errors,{CAM_FIXTURE_SUPPORT_INSUFFICIENT:'承托分布不足，请增大支撑区域或增加支撑，不能采用当前方案。',PLATE_SUPPORT_AREA_TOO_NARROW:'可用承托区域太窄，请扩大区域或调整支撑面。',PLATE_SPACING_INVALID:'支撑间距须大于支撑厚度。',PLATE_STATION_LIMIT:'支撑数量超出范围，请增大间距或缩小区域。',PLATE_AREA_INVALID:'请填写有效的底座范围。',PLATE_POST_WIDTH_INVALID:'梅花支柱宽度须不小于支撑厚度，并小于支撑间距。',PLATE_SUPPORT_NO_CONTACT:'支撑未能贴合所选承托面，请调整区域或图案。'});
const message=e=>Object.entries(errors).find(([key])=>e.message?.includes(key))?.[1]||e.message;
Object.assign(errors,{PLATE_COUNT_INVALID:'横向板和纵向板数量须为 2～32 的整数，总数不超过 40。',PLATE_POSITION_INVALID:'板件位置须按从小到大排列，并位于承托范围内，间距须大于板厚。',PLATE_POSITION_COUNT_MISMATCH:'板件位置数量须与指定数量一致。',PLATE_COUNT_LAYOUT_INFEASIBLE:'可用承托或连接位置无法满足指定数量，请调整数量、位置或承托面；未采用本次结果。'});
Object.assign(errors,{PLATE_TENON_RADIUS_INVALID:'公榫圆角 R 须为非负数，且不超过榫宽和底座厚度的一半。',PLATE_TENON_RADIUS_FAILED:'公榫圆角生成失败，请减小 R 后重新生成。'});
Object.assign(errors,{PLATE_DIRECTION_INVALID:'支撑方向须为非零方向向量。',CAM_FIXTURE_SUPPORT_DIRECTION_INVALID:'支撑方向须为有效的非零方向向量。'});
Object.assign(errors,{PLATE_JOINT_MODE_INVALID:'请选择连接方式。',PLATE_JOINT_DIMENSION_INVALID:'请检查榫宽、榫间距、配合间隙和最小保留板带。',PLATE_LAP_EDGE_WEB:'交叉槽距板边太近，请调整区域或支撑间距。',PLATE_LAP_MINIMUM_WEB:'交叉位置的板高不足以保留上下板带，请增大最小连接板带或减小榫卯保留板带。',PLATE_TENON_NO_SPACE:'部分支撑底边放不下插榫，请减小榫宽或调整支撑间距。',PLATE_MORTISE_EDGE_WEB:'榫孔距离底座边缘太近，请增大底座或边界留边。',PLATE_MORTISE_MINIMUM_WEB:'榫孔之间的板带不足，请调整支撑分布或榫宽。',PLATE_JOINT_DISCONNECTED:'开槽会切断板件，请增大板带或调整支撑间距。',PLATE_JOINT_CONTACT_LOST:'开槽后部分板件失去承托接触，请调整填充或连接参数。',PLATE_JOINT_LIMIT:'榫卯数量过多，请增大榫间距或支撑间距。'});
const alive=(ctx,view)=>!view.disposed&&ctx.isCurrentProject?.()!==false;
async function recommendLayout(ctx,view,ops,{resetCounts=false,fillPositions=false}={}){
  const d=designDraft(view);if(!d.supportFaces.length)return;
  const revision=d.revision,session=view.fiveFixtureSession||0,token=(view.fixtureLayoutSequence||0)+1;view.fixtureLayoutSequence=token;
  const draft={...d,positionMode:'Automatic',supportCount:resetCounts||!d.supportCountEdited?0:d.supportCount,connectionCount:resetCounts||!d.connectionCountEdited?0:d.connectionCount};
  const info=await ctx.sceneProxy.invoke('CamFixture.SuggestLayout',{expected:view.scene.domain.expected,setupId:d.setupId,recipe:recipe(draft,true)},fixtureTaskRequestOptions);
  if(!alive(ctx,view)||designDraft(view)!==d||d.revision!==revision||(view.fiveFixtureSession||0)!==session||view.fixtureLayoutSequence!==token)return;
  d.layoutInfo=info;
  for(const key of ['supportCount','connectionCount'])if(resetCounts||!d[key+'Edited']){d[key]=info[key];d[key+'Edited']=false;}
  if(fillPositions||d.positionMode!=='Manual'){d.supportPositionText=info.supportPositions.join(', ');d.connectionPositionText=info.connectionPositions.join(', ');}
  d.dirty=true;++d.revision;view.error='';ops.renderProject(ctx,view);
}
async function invoke(ctx,method,payload) {
  try{return await ctx.sceneProxy.invoke(`CamFixture.${method}`,payload,fixtureTaskRequestOptions);}
  catch(error){
    const receipt=await ctx.sceneProxy.invoke('CamScene.QueryCommand',{epoch:payload.expected.epoch,commandId:payload.commandId},fixtureTaskRequestOptions);
    if(receipt.status==='Completed')return ['Generate','FindSupportFaces'].includes(method)?receipt:receipt.result;
    if(receipt.status==='Running')return ctx.sceneProxy.invoke(`CamFixture.${method}`,payload,fixtureTaskRequestOptions);
    throw error;
  }
}
export async function designCommand(ctx,view,action,ops) {
  if(!alive(ctx,view))return;
  if(action==='close'||action==='cancel'){
    if(view.fiveFixtureConfirming)return;
    const task=view.fiveAxisFixtureTask||view.fiveFixtureGenerated?.task;
    view.fiveFixtureSession=(view.fiveFixtureSession||0)+1;
    view.fiveFixtureDesignOpen=false;view.fiveFixtureEditingUnit=null;view.fixtureDesignPicking=false;view.fixtureDesignDraft=null;view.fiveFixtureGenerated=null;
    view.fixtureDesignSelectedFace=null;view.fiveAxisFixtureTask=null;view.pending=false;view.progress=null;view.error='';disposeFixtureDesign(view);
    if(!selectedFixtureUnit(view))selectTool(view,'translate');ops.renderProject(ctx,view);
    if(task)await ctx.sceneProxy.invoke('CamFixture.CancelTask',{epoch:task.epoch,taskId:task.taskId}).catch(()=>{});
    return;
  }
  if(view.pending)return;
  if(action==='breakdown-close'){view.fiveFixtureBreakdown=null;ops.renderProject(ctx,view);return;}
  if(action.startsWith('page:')){view.fiveFixtureDesignPage=action.slice(5);view.fixtureDesignPicking=false;ops.renderProject(ctx,view);return;}
  const d=designDraft(view);
  if(action==='layout-positions'){await recommendLayout(ctx,view,ops,{resetCounts:false,fillPositions:true});return;}
  if(action==='reset'){view.fixtureDesignDraft=null;view.fixtureDesignPicking=false;view.fixtureDesignSelectedFace=null;view.error='';ops.renderProject(ctx,view);return;}
  if(action==='clear'){d.supportFaces=[];d.layoutInfo=null;for(const key of ['supportCount','connectionCount'])if(!d[key+'Edited'])d[key]='';view.fixtureDesignSelectedFace=null;d.dirty=true;++d.revision;view.error='';ops.renderProject(ctx,view);return;}
  if(action.startsWith('remove-face:')){
    const id=action.slice('remove-face:'.length),index=d.supportFaces.indexOf(id);if(index<0)return;
    d.supportFaces.splice(index,1);if(view.fixtureDesignSelectedFace===id)view.fixtureDesignSelectedFace=null;
    d.dirty=true;++d.revision;view.error='';ops.renderProject(ctx,view);if(view.fiveFixtureDesignOpen&&d.supportFaces.length)await recommendLayout(ctx,view,ops);return;
  }
  if(action.startsWith('select-face:')||action.startsWith('focus-face:')){
    const id=action.slice(action.indexOf(':')+1);if(!d.supportFaces.includes(id))return;
    const p=view.scene.domain.placements.find(p=>p.setupId===d.setupId);if(!p)return;
    if(!view.fixtureDesignFaces||view.fixtureDesignPlacement!==p.id){
      const revision=d.revision,result=await ctx.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:view.scene.domain.expected,placementId:p.id,setupId:d.setupId});
      if(!alive(ctx,view)||designDraft(view)!==d||d.revision!==revision)return;
      view.fixtureDesignFaces=result.faces;view.fixtureDesignPlacement=p.id;
    }
    view.fixtureDesignSelectedFace=id;
    if(action.startsWith('focus-face:'))focusSelectedFace(view,view.viewport?.sceneObjects.get(p.id),view.fixtureDesignFaces.filter(f=>String(f.faceId)===id));
    ops.renderProject(ctx,view);return;
  }
  if(action==='auto') {
    const direction=supportDirection(d);
    const p=view.scene.domain.placements.find(p=>p.setupId===d.setupId);
    if(!p)throw Error('请先导入工件。');
    const revision=d.revision,session=view.fiveFixtureSession||0;
    view.pending=true;view.error='';view.progress={title:'正在识别承托面…',detail:'沿指定支撑方向查找可接触的区域。'};ops.renderProject(ctx,view);
    try {
      let task=await invoke(ctx,'FindSupportFaces',{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:d.setupId,placementId:p.id,supportDirection:direction});
      if((view.fiveFixtureSession||0)!==session){await ctx.sceneProxy.invoke('CamFixture.CancelTask',{epoch:task.epoch,taskId:task.taskId});return;}
      view.fiveAxisFixtureTask=task;ops.renderProject(ctx,view);
      while(task.status==='Running'&&alive(ctx,view)) {
        await new Promise(r=>setTimeout(r,100));
        task=await queryFixtureTask(ctx,task);view.fiveAxisFixtureTask=task;
      }
      if(task.status==='Cancelled'||!alive(ctx,view))return;
      if(task.status!=='Completed')throw Error(task.error||'识别期间工件发生变化，请重新识别。');
      if(designDraft(view)!==d||d.revision!==revision)return;
      const [result,mapping]=await Promise.all([
        ctx.sceneProxy.invoke('CamFixture.ReadSupportSelection',{epoch:task.epoch,taskId:task.taskId}),
        ctx.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:view.scene.domain.expected,placementId:p.id,setupId:d.setupId}),
      ]);
      if(!alive(ctx,view)||(view.fiveFixtureSession||0)!==session||designDraft(view)!==d||d.revision!==revision)return;
      if(!result.supportFaces?.length)throw Error('未找到沿当前支撑方向可接触的承托面，请调整方向或手动选择。');
      d.supportFaces=[...new Set([...d.supportFaces,...result.supportFaces.map(String)])];d.dirty=true;++d.revision;
      view.fixtureDesignFaces=mapping.faces;view.fixtureDesignPlacement=p.id;
      if(view.fiveFixtureDesignOpen)await recommendLayout(ctx,view,ops);
    }catch(e){if(alive(ctx,view))view.error=message(e);}
    finally{if((view.fiveFixtureSession||0)===session){view.fiveFixtureConfirming=false;view.pending=false;view.progress=null;view.fiveAxisFixtureTask=null;if(alive(ctx,view))ops.renderProject(ctx,view);}}
    return;
  }
  if(action==='pick') {
    if(view.fixtureDesignPicking){view.fixtureDesignPicking=false;ops.renderProject(ctx,view);return;}
    const p=view.scene.domain.placements.find(p=>p.setupId===d.setupId);
    if(!p)throw Error('请先导入工件。');
    const result=await ctx.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:view.scene.domain.expected,placementId:p.id,setupId:d.setupId});
    if(!alive(ctx,view)||designDraft(view)!==d)return;
    view.fixtureDesignFaces=result.faces;view.fixtureDesignPlacement=p.id;view.fixtureDesignPicking=true;ops.renderProject(ctx,view);return;
  }
  let destination;
  if(action==='export'){
    const bridge=ctx.appProxy?.bridge??ctx.sceneProxy?.bridge;
    if(!bridge?.openDirectoryDialog)throw Error('当前宿主不支持选择导出目录。');
    destination=await bridge.openDirectoryDialog({title:'选择夹具下料包保存目录'});if(!destination||!alive(ctx,view))return;
  }
  if(action!=='generate'&&d.dirty)throw Error('参数已修改，请先重新生成。');
  const input=action==='generate'?recipe(d):null,revision=d.revision,session=view.fiveFixtureSession||0;
  view.pending=true;view.error='';view.fixtureDesignPicking=false;
  view.progress={title:({generate:'正在计算支撑…',confirm:'正在添加夹具…',breakdown:'正在拆分夹具板件…',export:'正在导出下料包…'})[action]||'正在处理夹具…',detail:action==='generate'?'正在根据承托面和布置参数生成支撑与底板。':''};
  ops.renderProject(ctx,view);
  const request=()=>({commandId:crypto.randomUUID(),expected:view.scene.domain.expected,setupId:d.setupId,...(view.fiveFixtureEditingUnit?{unitId:view.fiveFixtureEditingUnit}:!view.fiveFixtureDesignOpen&&selectedFixtureUnit(view)?.kind==='Generated'?{unitId:selectedFixtureUnit(view).id}:{})});
  let confirmedUnitId;
  try{
    if(action==='generate'){
      if(view.fiveFixtureGenerated?.task){const old=view.fiveFixtureGenerated.task;await ctx.sceneProxy.invoke('CamFixture.CancelTask',{epoch:old.epoch,taskId:old.taskId});view.fiveFixtureGenerated=null;}
      let task=await invoke(ctx,'Generate',{...request(),recipe:input,newUnit:!!view.fiveFixtureNewUnit});
      if((view.fiveFixtureSession||0)!==session||!view.fiveFixtureDesignOpen){await ctx.sceneProxy.invoke('CamFixture.CancelTask',{epoch:task.epoch,taskId:task.taskId});return;}
      view.fiveAxisFixtureTask=task;ops.renderProject(ctx,view);
      while(task.status==='Running'&&alive(ctx,view)&&(view.fiveFixtureSession||0)===session){
        await new Promise(r=>setTimeout(r,100));task=await queryFixtureTask(ctx,task);view.fiveAxisFixtureTask=task;
      }
      if(task.status==='Cancelled'||(view.fiveFixtureSession||0)!==session)return;
      if(task.status!=='Completed')throw Error(task.error||'夹具生成期间工程发生变化，请重新生成。');
      if(!view.fiveFixtureDesignOpen||(view.fiveFixtureSession||0)!==session||d.revision!==revision)return;
      const generated=await loadFixtureCandidate(ctx,view,task);
      if(!alive(ctx,view)||!view.fiveFixtureDesignOpen||(view.fiveFixtureSession||0)!==session||d.revision!==revision)return;
      d.dirty=false;d.stations=generated.design.recipe.stations;d.positions=d.stations.map(s=>s.coordinate).join(', ');
      const layout=generated.design.candidate.layoutSummary;if(layout){d.layoutInfo=layout;d.supportCount=layout.supportCount;d.connectionCount=layout.connectionCount;if(d.positionMode!=='Manual'){d.supportPositionText=layout.supportPositions.join(', ');d.connectionPositionText=layout.connectionPositions.join(', ');}}
      view.fiveFixtureGenerated=generated;
      return;
    }else if(action==='confirm'){
      const generated=view.fiveFixtureGenerated;if(!generated)throw Error('请先生成夹具。');view.fiveFixtureConfirming=true;
      const result=await invoke(ctx,'Confirm',{...request(),taskId:generated.task.taskId,candidateId:generated.design.candidate.id,supportReviewed:true});confirmedUnitId=result.unitId;
    }else if(action==='breakdown')await invoke(ctx,'Breakdown',request());
    else if(action==='export'){
      const result=await invoke(ctx,'Export',{...request(),destination});view.fixtureDeliveryDirectory=result.directory;
    }
    await saveCommittedProjectEdit(ctx,view,ops);
    if(alive(ctx,view))await ops.refreshSceneState(ctx,view);
    if(action==='breakdown'&&alive(ctx,view))view.fiveFixtureBreakdown=await loadFixtureBreakdown(ctx,view,selectedFixtureUnit(view)?.id);
    // ReadState must contain the new unit before list rendering can select it.
    if(confirmedUnitId&&alive(ctx,view)){view.fiveAxisFixtures??={selected:'',drafts:{},clearance:'2'};view.fiveAxisFixtures.selected=confirmedUnitId;view.fiveFixtureDesignOpen=false;view.fiveFixtureEditingUnit=null;view.fixtureDesignDraft=null;view.fiveFixtureGenerated=null;}
    if(action==='confirm'&&alive(ctx,view))await ops.fitViewAfterRenderPublish(ctx,view,{entityIds:[view.scene.activePlacementId,...view.scene.domain.fixtures.filter(f=>f.setupId===d.setupId).map(f=>f.id)]});
    ops.appendProjectLog(ctx,'ok',({generate:'夹具候选已生成',confirm:'支撑板与底板已采用',breakdown:'夹具板件已拆单',export:'下料包已导出'})[action]);
  }catch(e){if(alive(ctx,view)&&(view.fiveFixtureSession||0)===session)view.error=message(e);}
  finally{if((view.fiveFixtureSession||0)===session){view.fiveFixtureConfirming=false;view.pending=false;view.progress=null;view.fiveAxisFixtureTask=null;if(alive(ctx,view))ops.renderProject(ctx,view);}}
}
export function designPick(ctx,view,data,hit,_event,_hits,ops){
  if(activeTool(view)!=='fixture'||!view.fiveFixtureDesignOpen&&!view.fixtureDesignPicking)return false;
  if(data?.objectId!==view.fixtureDesignPlacement||!Number.isInteger(hit?.faceIndex))return true;
  const face=view.fixtureDesignFaces?.find(f=>hit.faceIndex>=Number(f.first)&&hit.faceIndex<Number(f.first)+Number(f.count));
  if(face){const d=designDraft(view),id=String(face.faceId),i=d.supportFaces.indexOf(id);
    if(view.fixtureDesignPicking){if(i<0){d.supportFaces.push(id);view.fixtureDesignSelectedFace=id;}else{d.supportFaces.splice(i,1);if(view.fixtureDesignSelectedFace===id)view.fixtureDesignSelectedFace=null;}d.dirty=true;++d.revision;}
    else if(i>=0)view.fixtureDesignSelectedFace=id;
    ops.renderProject(ctx,view);if(d.supportFaces.includes(id))revealFaceListSelection(ctx.mount,'support-face-list',id);
    if(view.fixtureDesignPicking&&d.supportFaces.length)void recommendLayout(ctx,view,ops).catch(e=>{if(alive(ctx,view)&&designDraft(view)===d){view.error=message(e);ops.renderProject(ctx,view);}});}return true;
}
export function highlightDesign(view){
  previewDesign(view);
  syncSupportDirectionArrow(view);
  const old=view.fixtureDesignOverlay;old?.removeFromParent();old?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});view.fixtureDesignOverlay=null;
  if(activeTool(view)!=='fixture'||!view.fiveFixtureDesignOpen&&!view.fixtureDesignPicking)return;
  const source=view.viewport?.sceneObjects.get(view.fixtureDesignPlacement);if(!source?.geometry)return;
  const selected=new Set(designDraft(view).supportFaces);
  const mesh=selectedFaceOverlay(source,(view.fixtureDesignFaces||[]).filter(f=>selected.has(String(f.faceId))).map(f=>({id:String(f.faceId),ranges:[f]})),view.fixtureDesignSelectedFace);
  source.add(mesh);view.fixtureDesignOverlay=mesh;
}
function disposeSupportDirectionArrow(view){
  const arrow=view.fixtureSupportArrow?.group;arrow?.removeFromParent();arrow?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});view.fixtureSupportArrow=null;
}
function disposeLayoutGuide(view){
  const group=view.fixtureLayoutGuide?.group;group?.removeFromParent();group?.traverse(o=>{o.geometry?.dispose();o.material?.map?.dispose();o.material?.dispose();});view.fixtureLayoutGuide=null;
}
function syncLayoutGuide(view){
  const d=view.fiveFixtureDesignOpen?designDraft(view):null,axis=d?.layoutInfo?.axis||d?.layoutAxis;
  const source=view.viewport?.sceneObjects.get(view.scene?.domain?.placements?.find(p=>p.setupId===view.scene.domain.activeSetupId)?.id);
  if(activeTool(view)!=='fixture'||!axis||!source?.geometry){disposeLayoutGuide(view);return;}
  let normal;try{normal=new THREE.Vector3(...supportDirection(d));}catch{disposeLayoutGuide(view);return;}
  source.updateWorldMatrix(true,false);const key=JSON.stringify([source.geometry.uuid,source.matrixWorld.elements,axis,normal.toArray(),d.baseDrop]);
  if(view.fixtureLayoutGuide?.key===key&&view.fixtureLayoutGuide.viewport===view.viewport)return;
  disposeLayoutGuide(view);if(!source.geometry.boundingBox)source.geometry.computeBoundingBox();
  const box=source.geometry.boundingBox.clone().applyMatrix4(source.matrixWorld),size=box.getSize(new THREE.Vector3()),length=Math.max(20,Math.min(220,size.length()*.14));
  let x=new THREE.Vector3(1,0,0).addScaledVector(normal,-normal.x);if(x.lengthSq()<1e-12)x=new THREE.Vector3(0,1,0).addScaledVector(normal,-normal.y);x.normalize();const y=new THREE.Vector3().crossVectors(normal,x).normalize();
  const origin=box.getCenter(new THREE.Vector3()).addScaledVector(normal,-size.length()*.08-Number(d.baseDrop||40)),group=new THREE.Group();group.name='fixture-layout-directions';
  const doc=view.viewport.renderer.domElement.ownerDocument;
  for(const [direction,label,color] of [[axis==='X'?x:y,'横向板',0x56bfff],[axis==='X'?y:x,'纵向板',0xffc368]]){
    const arrow=new THREE.ArrowHelper(direction,origin,length,color,length*.15,length*.07);arrow.traverse(o=>{if(o.geometry)o.geometry=o.geometry.clone();if(o.material){o.material.depthTest=false;o.material.depthWrite=false;}o.renderOrder=80;o.raycast=()=>{};});group.add(arrow);
    const canvas=doc.createElement('canvas');canvas.width=192;canvas.height=56;const ctx=canvas.getContext('2d');ctx.font='bold 28px sans-serif';ctx.fillStyle='#'+color.toString(16);ctx.textAlign='center';ctx.fillText(label,96,38);
    const sprite=new THREE.Sprite(new THREE.SpriteMaterial({map:new THREE.CanvasTexture(canvas),depthTest:false,depthWrite:false}));sprite.position.copy(origin).addScaledVector(direction,length*1.2);sprite.scale.set(length*.5,length*.15,1);sprite.renderOrder=81;sprite.raycast=()=>{};group.add(sprite);
  }
  view.viewport.scene.add(group);view.fixtureLayoutGuide={key,group,viewport:view.viewport};
}
export function syncSupportDirectionArrow(view){
  syncLayoutGuide(view);
  const menu=view.viewport?.renderer?.domElement?.closest('.five-workspace')?.querySelector('.five-support-direction-menu[open]');
  if(!menu){disposeSupportDirectionArrow(view);return;}
  const source=view.viewport?.sceneObjects.get(view.scene?.domain?.placements?.find(p=>p.setupId===view.scene.domain.activeSetupId)?.id);
  let direction;try{direction=supportDirection(designDraft(view));}catch{}
  if(activeTool(view)!=='fixture'||!source?.geometry||!direction){disposeSupportDirectionArrow(view);return;}
  source.updateWorldMatrix(true,false);
  const key=JSON.stringify([source.geometry.uuid,source.matrixWorld.elements,direction]);
  if(view.fixtureSupportArrow?.key===key&&view.fixtureSupportArrow.viewport===view.viewport)return;
  disposeSupportDirectionArrow(view);if(!source.geometry.boundingBox)source.geometry.computeBoundingBox();
  const box=source.geometry.boundingBox.clone().applyMatrix4(source.matrixWorld),extent=Math.max(box.getSize(new THREE.Vector3()).length(),1),axis=new THREE.Vector3(...direction);
  const origin=box.getCenter(new THREE.Vector3()).addScaledVector(axis,-extent*.24),length=extent*.18;
  const arrow=new THREE.ArrowHelper(axis,origin,length,0xffca63,length*.18,length*.09);arrow.name='fixture-support-direction';
  arrow.traverse(o=>{if(o.geometry)o.geometry=o.geometry.clone();if(o.material){o.material.depthTest=false;o.material.depthWrite=false;}o.renderOrder=80;o.raycast=()=>{};});
  view.viewport.scene.add(arrow);view.fixtureSupportArrow={key,group:arrow,viewport:view.viewport};
}
function disposePreview(view){
  const preview=view.fixtureCandidatePreview;preview?.group.removeFromParent();
  for(const [id,mesh] of preview?.hiddenAssembly||[]){
    if(preview.viewport?.sceneObjects.get(id)!==mesh)continue;
    const fixture=view.scene?.domain?.fixtures?.find(f=>f.id===id);
    mesh.visible=!!fixture&&fixture.visible!==false&&fixture.setupId===view.scene.domain.activeSetupId;
  }
  preview?.group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});view.fixtureCandidatePreview=null;
}
function hidePreviousAssembly(view,preview,assembly){
  if(!preview)return;
  for(const part of assembly?.parts||[]){
    const mesh=view.viewport?.sceneObjects.get(part.partId);if(!mesh)continue;
    preview.hiddenAssembly.set(part.partId,mesh);mesh.visible=false;
  }
}
export function disposeFixtureDesign(view){
  if(!view)return;disposePreview(view);disposeSupportDirectionArrow(view);disposeLayoutGuide(view);const old=view.fixtureDesignOverlay;
  old?.removeFromParent();old?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});view.fixtureDesignOverlay=null;
}
function previewDesign(view){
  const design=fixtureDesign(view),candidate=design.candidate;
  const visible=view.fiveFixtureDesignOpen&&activeTool(view)==='fixture'&&!designDraft(view).dirty&&candidate?.validity==='Current'&&candidate.id!==design.assembly?.id;
  const key=visible?candidate.id:'';
  if(view.fixtureCandidatePreview?.key===key&&view.fixtureCandidatePreview.viewport===view.viewport){hidePreviousAssembly(view,view.fixtureCandidatePreview,design.assembly);return;}
  disposePreview(view);if(!visible||!view.viewport?.scene)return;
  const group=new THREE.Group();group.name='fixture-candidate';
  for(const {data} of view.fiveFixtureGenerated?.renderParts||[]){
    if(data?.kind!=='mesh')continue;
    const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(data.positions,3));
    if(data.indices?.length)geometry.setIndex(Array.from(data.indices));
    const mesh=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({color:0x20b897,transparent:true,opacity:.48,side:THREE.DoubleSide,depthWrite:false}));
    mesh.raycast=()=>{};group.add(mesh);
  }
  view.viewport.scene.add(group);view.fixtureCandidatePreview={key,group,viewport:view.viewport,hiddenAssembly:new Map()};
  hidePreviousAssembly(view,view.fixtureCandidatePreview,design.assembly);
}
export function fixtureCutClearanceReady(design,assembly=design?.assembly){
  return (assembly?.geometryValidity??assembly?.validity)!=='Stale';
}
export function renderFixtureCutClearance(assembly,options={}){
  if(options.adopted)return '';
  const opening=assembly?.openingPreclearance;
  const openingStale=opening&&opening.validity!=='Current';
  const preclearance=opening?`<section class="five-design-group" data-five-key="opening-preclearance-result"><h3>${openingStale?'孔洞预避让已过期':opening.status==='Partial'?'部分孔洞待处理':'孔洞预避让已生成'}</h3><p>${openingStale?'工件或夹具位置、参数已变化，请重新生成。':`${Number(opening.boundaryCount)?`已按 ${Number(opening.boundaryCount)} 条平面孔边界为竖板局部开槽${Number(opening.reliefDepthMm)?`，深度 ${Number(opening.reliefDepthMm)} mm`:''}，边界外扩 ${Number(opening.marginMm)} mm。`:'未识别到可预避让的平面孔边界。'}${Number(opening.unclassifiedFaceCount)?`另有 ${Number(opening.unclassifiedFaceCount)} 个面的孔界未能自动处理，需检查支撑位置。`:''}`}</p></section>`:'';
  const c=assembly?.cutClearance,valid=c?.validity==='Current'&&c.status==='Clear';
  const title=c?.validity==='Stale'?'切割避让已过期':valid?'切割与引入引出已避让':'尚未生成切割避让';
  return preclearance+`<section class="five-design-group" data-five-key="cut-clearance-result"><h3>${title}</h3>${valid?`<p>已按 ${Number(c.operationCount)} 条加工路径为竖板让出光路，并避让切割头，保留间隙 ${Number(c.clearanceMm)} mm，光束半径 ${Number(c.beamRadiusMm)} mm。</p><p>${c.headScope?.startsWith('Builtin')?'使用内置示意头的组合碰撞盒。':'使用已标定切割头的组合碰撞盒。'}空移需在采用夹具、重新生成刀路后进行完整仿真碰撞检查。</p>`:`<p>${c?.validity==='Stale'?'刀路、姿态、工件或切割头已变化，请重新生成夹具。':'可以先生成和采用支撑；有刀路后，可按实际刀路生成避让。'}</p>`}</section>`;
}
export function renderFixtureDesign(view){
  const d=designDraft(view),design=fixtureDesign(view),busy=!!(view.pending||view.fixtureRequest),candidate=design.candidate;
  const current=!d.dirty&&candidate?.validity==='Current',supported=current&&candidate.supportCheck?.status==='Supported',clearanceReady=fixtureCutClearanceReady(design,candidate);
  const disabled=busy?'disabled':'';
  const field=(key,label,wide=false)=>`<label class="five-design-field ${wide?'is-wide':''}" data-five-key="design-${key}"><span>${label}</span><input type="text" inputmode="${key.includes('Position')?'text':key.endsWith('Count')?'numeric':'decimal'}" autocomplete="off" ${key.endsWith('Count')?'placeholder="选择承托面后推荐"':''} data-five-fixture-field="design:${key}" data-five-key="design-input-${key}" value="${attr(d[key]??'')}" aria-label="${label}"></label>`;
  const choice=(key,label,values,wide=false)=>`<label class="five-design-field ${wide?'is-wide':''}" data-five-key="design-${key}"><span>${label}</span><select data-five-fixture-field="design:${key}" data-five-key="design-select-${key}" aria-label="${label}">${values.some(([v])=>v===d[key])?'':`<option value="" disabled selected>请选择${label}</option>`}${values.map(([v,t])=>`<option value="${v}" ${d[key]===v?'selected':''}>${t}</option>`).join('')}</select></label>`;
  const group=(id,title,content)=>`<section class="five-design-group" data-five-key="design-group-${id}"><h3>${title}</h3><div class="five-design-grid">${content}</div></section>`;
  const directionMenu=`<details class="five-support-direction-menu" data-five-key="support-direction-menu"><summary aria-label="高级配置" title="高级配置" ${busy?'aria-disabled="true"':''}><span aria-hidden="true">⌄</span></summary><div class="five-support-direction-popover"><strong>支撑方向</strong><div class="five-direction-vector">${field('supportX','X')}${field('supportY','Y')}${field('supportZ','Z')}</div><p>从底座指向工件 · 当前装夹坐标</p></div></details>`;
  const support=`<section class="five-design-support ${view.fixtureDesignPicking?'is-picking':''}" data-five-key="design-support"><header><h3>承托面 <small>${d.supportFaces.length}</small></h3><div class="five-design-support-actions">${iconButton('five.fixture.design.pick','手动修改','manual',`${disabled} aria-pressed="${!!view.fixtureDesignPicking}"`)}<div class="five-support-auto">${iconButton('five.fixture.design.auto','批量添加','bulk',disabled)}${directionMenu}</div>${iconButton('five.fixture.design.clear','清空','clear',busy||!d.supportFaces.length?'disabled':'')}</div></header>${faceSelectionList({ids:d.supportFaces,selectedId:view.fixtureDesignSelectedFace,key:'support-face-list',label:'承托面列表',selectCommand:id=>'five.fixture.design.select-face:'+id,focusCommand:id=>'five.fixture.design.focus-face:'+id,removeCommand:id=>'five.fixture.design.remove-face:'+id,disabled,empty:'手动选择工件表面，或批量添加承托面。'})}${view.fixtureDesignPicking?'<p class="five-support-pick-hint">点击工件表面补选或取消选择。</p>':''}</section>`;
  const axis=d.layoutInfo?.axis||d.layoutAxis;
  const strategy=group('counts','骨架支撑',field('supportCount','横向板数量')+field('connectionCount','纵向板数量')+`<p class="five-hint is-wide">${axis?`横向承托板沿承托平面 ${axis} 分布；纵向连接板沿 ${axis==='X'?'Y':'X'} 分布。`:'选择承托面后，按工件长向推荐横向承托板数量。'}相交处局部挖槽避空，保留板件位置与数量；无法保留有效板带时提示处理。</p>`);
  const layout=group('layout','板件位置',choice('positionMode','位置分配',[['Automatic','按数量均匀分布'],['Manual','逐块指定位置']],true)+(d.positionMode==='Manual'?field('supportPositionText','横向板位置 · mm',true)+field('connectionPositionText','纵向板位置 · mm',true)+button('five.fixture.design.layout-positions','按当前数量计算位置',busy||!d.supportFaces.length?'disabled':'')+'<p class="five-hint is-wide">在上述承托平面方向填写坐标，用逗号分隔。指定位置保持固定，冲突时提示调整。</p>':''));
  const plates=group('plates','板材与底座',choice('material','板材',[['CarbonSteel','碳钢'],['StainlessSteel','不锈钢'],['Aluminum','铝合金']],true)
    +field('thickness','支撑厚度')+field('baseThickness','底座厚度')
    +choice('baseShape','底座形状',[['Rectangle','矩形'],['Circle','圆形']])+choice('sizing','底座范围',[['Automatic','随工件'],['Manual','指定尺寸']])
    +(d.sizing==='Manual'?field('areaWidth',d.baseShape==='Circle'?'底座直径':'底座宽度')+(d.baseShape==='Rectangle'?field('areaDepth','底座深度'):'')+field('areaX','中心 X')+field('areaY','中心 Y'):'')
    +field('baseDrop','底座退距')+field('margin','边界留边')
    +choice('windowMode','通风过水孔',[['Automatic','横纵板自动开孔'],['None','不开孔']],true)
    +(d.windowMode==='Automatic'?field('windowWeb','开孔保留板带')+'<p class="five-hint is-wide">横向板开一个中心大孔，纵向板开一个横向长孔；底板保持完整，保留承托上沿、底部连接板带和榫卯位置。空间不足的小区域保留实心。</p>':''));
  const options=group('options','连接与间隙',choice('jointMode','连接方式',[['TabSlot','榫卯插接'],['Weld','定位拼焊']],true)+field('gap','非承托区避空')+field('minimumWeb','最小连接板带')+(d.jointMode==='TabSlot'?field('jointClearance','总配合间隙')+field('jointWeb','最小榫卯板带')+field('tenonWidth','底部榫宽')+field('tenonPitch','榫间距')+field('tenonRadius','公榫圆角 R'):'')+'<p class="five-hint is-wide">底座退距从工件最靠近底座的位置计算；尺寸为成品尺寸。</p>');
  const clearance=group('cut-clearance','切割避让',field('openingReliefDepth','孔边避空深度')+choice('cutClearanceMode','光路避让',[['None','先按孔边界预避让'],['Toolpath','按实际切割刀路避让']],true)+(d.cutClearanceMode==='Toolpath'?field('cutClearanceMm','切割间隙')+field('beamRadiusMm','光束半径'):'')+'<p class="five-hint is-wide">孔边局部挖槽并保留下部板带。</p>');
  const advanced=`<details class="five-design-options" data-five-key="design-options"><summary><strong>高级参数</strong><i aria-hidden="true">⌄</i></summary>${layout}${plates}${options}${clearance}</details>`;
  const generatedStatus=candidate?`<p class="five-design-generated-status" role="status">${!current?'参数已修改，请重新生成。':supported?`已生成 ${candidate.parts.filter(p=>p.role==='Support').length} 块承托板${candidate.parts.some(p=>p.role==='Connection')?` · ${candidate.parts.filter(p=>p.role==='Connection').length} 块连接板`:''} · ${candidate.parts.filter(p=>p.role==='Base').length} 块底板${candidate.layoutSummary?` · 最大间距 ${Number(candidate.layoutSummary.maximumSpacingMm).toFixed(1)} mm`:''}`:'承托分布不足，请调整后重新生成。'}</p>`:'';
  return `<div class="five-fixture-generator" data-five-key="fixture-design"><header class="five-design-header"><div><h2>${view.fiveFixtureNewUnit?'新增支撑':'支撑参数'}</h2><span>${esc(setup(view)?.name||'当前装夹')} · 尺寸 mm</span></div>${iconButton('five.fixture.design.cancel','取消并关闭','cancel',view.fiveFixtureConfirming?'disabled':'')}</header><div class="five-design-body" data-five-key="design-body"><div data-five-key="design-parameters">${support}${strategy}${advanced}</div></div><footer class="five-design-footer" data-five-key="design-footer">${view.error?`<div class="five-design-error" role="alert">${esc(view.error)}</div>`:''}${generatedStatus}<div class="five-design-footer-actions">${iconButton('five.fixture.design.cancel','取消','cancel',view.fiveFixtureConfirming?'disabled':'',true)}${iconButton('five.fixture.design.generate','生成','generate',`${disabled} ${!d.supportFaces.length||!view.scene?.model?.isLoaded?'disabled':''}`,true)}${iconButton('five.fixture.design.confirm','确定','confirm',`class="five-primary" ${busy||!view.fiveFixtureGenerated||!current||!supported||!clearanceReady?'disabled':''}`,true)}</div></footer></div>`;
}
