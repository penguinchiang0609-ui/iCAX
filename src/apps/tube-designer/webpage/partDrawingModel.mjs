import { migratePunchRecord, migratePunchRecipe, BRANCH_PLACEMENT_DEFAULTS } from "./punchToolMigration.mjs";

// Editable three-dimensional part recipes. This module deliberately has no
// machining-wizard state, message routing, UI lifecycle or preview dependency.
const clone=value=>structuredClone(value);
const uid=()=>globalThis.crypto?.randomUUID?.()??`drawing-${Date.now()}-${Math.random().toString(36).slice(2)}`;
const number=(value,fallback)=>value===undefined?fallback:value===""||value===null?NaN:Number(value);
const label=item=>typeof item?.displayName==="string"?item.displayName:item?.displayName?.["zh-CN"]??item?.id??"";
const endLongDatum=new Set(["end-key-joint","end-step-z","end-profile"]);
const snapshot=s=>clone({features:s.features,ends:s.ends,draft:s.draft,editingId:s.editingId,drawing:s.drawing,baseLength:s.baseLength});
const changed=s=>{s.revision=(s.revision??0)+1;s.error="";};

export function normalizeDrawingFeature(value={}) {
  const feature=migratePunchRecord(value);
  delete feature.toolSnapshot;
  const defaults={station:0,offset:0,rotation:0,arrayCount:1,arrayPitch:50,rowCount:1,rowPitch:20,
    diameter:10,spanAlong:30,spanAcross:10,cornerRadius:0};
  for(const [key,fallback] of Object.entries(defaults))feature[key]=number(feature[key],fallback);
  return {...feature,id:feature.id??uid(),type:String(feature.type??"branch-profile"),
    recordKind:feature.recordKind??(feature.section?.source==="dxf"?"dxf":feature.section?"branch":"tool"),
    face:feature.face??"top",reference:feature.reference??"start",endDatum:feature.endDatum??"long",
    layoutDatum:feature.layoutDatum??"base",distributionMode:feature.distributionMode??"pitch",
    rowDistributionMode:feature.rowDistributionMode??"pitch",enabled:feature.enabled!==false,
    opposite:!!feature.opposite,through:!!feature.through,reverse:!!feature.reverse,
    depthMode:feature.depthMode??(feature.through?"through":feature.opposite?"both":feature.reverse?"reverse":"single")};
}

export function createDrawingState(part={}) {
  const recipe=migratePunchRecipe(part.properties?.["tubeDesigner.partDrawing"]??part.properties?.["tubeDesigner.punchWizard"]??{});
  const originals=recipe.features??[],features=originals.map(normalizeDrawingFeature);
  const length=Number(recipe.drawing?.length??recipe.baseLength??part.length??500);
  return {partId:String(part.entityId??""),resourceId:part.manufacturingGeometryResourceId,
    resourceVersion:part.manufacturingGeometryResourceVersion,baseLength:length,features,
    originalFeatureRecipes:Object.fromEntries(features.map((feature,index)=>[feature.id,clone(originals[index])])),
    ends:clone({start:{type:"keep"},end:{type:"keep"},...recipe.ends}),
    draft:normalizeDrawingFeature({recordKind:"branch",station:length/2}),editingId:"",history:[],future:[],revision:0,
    preview:null,previewMode:"tools",catalogueStatus:"idle",tools:[],error:""};
}

export function checkpointDrawing(state) {
  state.history??=[];state.history.push(snapshot(state));state.history=state.history.slice(-100);state.future=[];changed(state);
}
export function drawingToolDescriptor(state,item) {
  return state?.tools?.find(tool=>tool.id===item?.toolRef?.id
    &&(!item.toolRef.version||tool.version===item.toolRef.version)
    &&(!item.toolRef.digest||tool.digest===item.toolRef.digest));
}
export const getDrawingToolDescriptor=drawingToolDescriptor;
export function isDrawingToolReadOnly(state,item) {
  return state?.catalogueStatus==="ready"&&!!item?.toolRef&&!drawingToolDescriptor(state,item);
}
export function hasFrozenDrawingTool(item) {
  const frozen=item?.frozenTool;
  return ["icax.frozen-part-drawing-tool","icax.frozen-punch-tool"].includes(frozen?.schema)
    &&frozen.schemaVersion===1&&!!frozen.geometry&&!!frozen.instance
    &&!!item.frozenCut?.url&&Number(item.frozenCut.version)>0
    &&["id","version","digest"].every(key=>frozen.ref?.[key]===item.toolRef?.[key]);
}
export function selectDrawingTool(state,item,id) {
  const tool=state.tools.find(candidate=>candidate.id===id);if(!tool)return false;
  item.type=id;item.toolRef={id:tool.id,version:tool.version,digest:tool.digest};
  item.toolLabel=label(tool);item.toolKind=tool.kind;
  item.toolParameters=clone(tool.defaultParameters??Object.fromEntries((tool.parameters??[]).map(p=>[p.key,p.defaultValue])));
  item.recordKind=tool.requiresSection?(item.section?.source==="dxf"?"dxf":"branch"):"tool";
  if(tool.target==="part") {
    Object.assign(item,{toolTarget:"part",face:["top","left","round"].includes(item.face)?item.face:"top",
      offset:0,rotation:0,opposite:false,through:false,reverse:false,depthMode:"single"});
    if(tool.requiresSection) for(const [key,value] of Object.entries(BRANCH_PLACEMENT_DEFAULTS)) item[key] ??= value;
  }
  else delete item.toolTarget;
  if(!tool.requiresSection)delete item.section;
  if(tool.target==="end"&&endLongDatum.has(tool.id))item.datum="long";
  delete item.toolSnapshot;delete item.frozenTool;delete item.frozenCut;
  return true;
}
export function installDrawingCatalogue(state,result) {
  state.tools=Array.isArray(result?.tools)?clone(result.tools):[];state.catalogueStatus="ready";
  for(const item of [state.draft,...state.features,...Object.values(state.ends)]) {
    if(item.toolRef||item.type==="keep")continue;
    const tool=state.tools.find(candidate=>candidate.id===item.type||candidate.target==="end"&&candidate.id===`end-${item.type}`);
    if(!tool)continue;
    const supplied={...Object.fromEntries((tool.parameters??[]).map(parameter=>[parameter.key,item[parameter.key]??parameter.defaultValue])),...item.toolParameters};
    selectDrawingTool(state,item,tool.id);item.toolParameters=supplied;
  }
  state.catalogueErrors=clone(result?.errors??[]);
}

function finiteParameters(item) {
  return Object.values(item.toolParameters??{}).every(value=>typeof value!=="number"||Number.isFinite(value));
}
function featureCount(feature) {
  if(feature.enabled===false)return 0;
  return (feature.arrayTransforms?.length??feature.arrayCount*feature.rowCount)
    *(feature.depthMode==="both"||feature.opposite&&!feature.through?2:1);
}
export function validateDrawingFeature(feature) {
  if(feature.enabled===false)return "";
  if(![feature.station,feature.offset,feature.rotation,feature.arrayPitch,feature.rowPitch].every(Number.isFinite))return "位置和间距必须是有效数字。";
  if(![feature.arrayCount,feature.rowCount].every(value=>Number.isSafeInteger(value)&&value>=1&&value<=1000))return "阵列数量须为 1 至 1000 的整数。";
  if(feature.arrayCount>1&&Math.abs(feature.arrayPitch)<1e-7&&(feature.distributionMode==="pitch"||!feature.arrayOffsets?.length))return "多个刀具的阵列间距不能为零。";
  if(feature.rowCount>1&&Math.abs(feature.rowPitch)<1e-7&&(feature.rowDistributionMode==="pitch"||!feature.rowOffsets?.length))return "多排刀具的阵列间距不能为零。";
  if(!Number.isFinite((feature.arrayCount-1)*feature.arrayPitch)||!Number.isFinite((feature.rowCount-1)*feature.rowPitch))return "阵列偏移必须是有效数字。";
  if(featureCount(feature)>1000)return "单个零件最多支持 1000 个展开刀具。";
  if(!["start","end","center"].includes(feature.reference)||!["top","bottom","left","right","round"].includes(feature.face))return "请选择有效的定位基准与方向。";
  if((feature.recordKind==="branch"||feature.recordKind==="dxf")&&!feature.section?.profile?.contours?.length)return "请选择有效的支管截面。";
  if(feature.toolRef?.id==="branch-profile" && (![feature.angle,feature.azimuth,feature.roll,feature.offsetY,feature.offsetZ,feature.length].every(Number.isFinite)
    || !(feature.length>0) || !["through","symmetric","positive","negative"].includes(feature.direction??"through")))return "支管定位与拉伸参数无效。";
  if(!finiteParameters(feature))return "刀具参数必须是有效数字。";
  if(feature.arrayOffsets?.some(value=>!Number.isFinite(value))||feature.rowOffsets?.some(value=>!Number.isFinite(value)))return "阵列偏移必须是有效数字。";
  return "";
}
export function validateDrawing(state) {
  if(!state)return "三维绘制未打开。";
  if(state.editingId)return "当前特征尚未完成，请先修正参数。";
  if(!Number.isFinite(Number(state.baseLength))||Number(state.baseLength)<=0)return "主管长度必须是有效正数。";
  if(state.features.length>256)return "最多支持 256 个建模特征。";
  let total=0;
  for(const item of [...state.features,...Object.values(state.ends).filter(end=>end.type!=="keep")]) {
    if(isDrawingToolReadOnly(state,item)) {
      if(item.enabled!==false&&!hasFrozenDrawingTool(item))return "缺少原版建模工具且没有保存的定式实体，只能删除该节点或安装原版工具。";
      continue;
    }
    if(state.features.includes(item)) {
      const error=validateDrawingFeature(item);if(error)return error;total+=featureCount(item);
    } else {
      if(![Number(item.trim??0),Number(item.rotation??0)].every(Number.isFinite)||Number(item.trim??0)<0)return "端部定位参数无效。";
      if(!["long","center","short"].includes(item.datum??"long"))return "请选择有效的端部尺寸基准。";
      if(endLongDatum.has(item.toolRef?.id??item.type)&&(item.datum??"long")!=="long")return "当前端部工具使用长点基准。";
      if(!finiteParameters(item))return "端部刀具参数必须是有效数字。";
      if((item.toolRef?.id??item.type)==="end-profile"&&!item.section?.profile?.contours?.length)return "请选择有效的端部截面。";
    }
  }
  return total>1000?"单个零件最多支持 1000 个展开刀具。":"";
}

function transportFeature(feature) {
  const item=normalizeDrawingFeature(feature);
  const error=validateDrawingFeature(item);
  if(error)throw new Error(error);
  if(item.enabled===false&&validateDrawingFeature({...item,enabled:true})) {
    delete item.arrayOffsets;delete item.rowOffsets;delete item.arrayTransforms;
    return item;
  }
  // Drawing currently exposes regular X / transverse / circular arrays. Keep
  // imported non-regular transport recipes intact instead of reinterpreting them.
  if(!item.arrayGroups && item.distributionMode==="pitch")
    item.arrayOffsets=Array.from({length:item.arrayCount},(_,index)=>index===0?0:(item.reference==="end"?-1:1)*index*item.arrayPitch);
  if(!item.arrayGroups && item.rowDistributionMode==="pitch")
    item.rowOffsets=Array.from({length:item.rowCount},(_,index)=>index===0?0:index*item.rowPitch);
  for(const key of ["toolSnapshot","layoutSummary","layoutError","arrayGroupsSummary","arrayGroupsError","arrayInstances","arraySkipText"])delete item[key];
  return item;
}
export function getDrawingPayload(state) {
  return {partEntityId:String(state.partId??""),resourceId:state.resourceId,resourceVersion:state.resourceVersion,
    features:state.features.map(feature=>isDrawingToolReadOnly(state,feature)
      ? clone(state.originalFeatureRecipes?.[feature.id]??feature):transportFeature(feature)),ends:clone(state.ends)};
}

export function updateDrawingField(state,target) {
  const field=target?.dataset?.drawingField??target?.dataset?.tubeDesignerPunchField;
  if(!state||!field)return false;
  const end=target.dataset.drawingEnd??target.dataset.tubeDesignerPunchEnd;
  const item=end?(state.ends[end]??={type:"keep"}):state.draft;
  if(isDrawingToolReadOnly(state,item)){state.error="退化定式刀具节点只读，仅可删除。";return false;}
  checkpointDrawing(state);
  const parameter=target.dataset.drawingParameter??target.dataset.tubeDesignerPunchParameter;
  if(field==="tool") {
    if(end&&target.value==="keep")state.ends[end]={type:"keep",trim:0,datum:"long",rotation:0};
    else {
      selectDrawingTool(state,item,String(target.value));
      if(end){
        item.trim??=0;item.datum??="long";item.rotation??=0;
        // A new section-based end cut should be usable immediately. Prefer an
        // already configured branch section, then fall back to the main tube;
        // the new end-section picker remains available for changing it.
        const tool=state.tools.find(candidate=>candidate.id===String(target.value));
        if(tool?.requiresSection&&!item.section?.profile?.contours?.length) {
          const source=state.features.find(feature=>feature.section?.profile?.contours?.length)?.section
            ??state.drawing?.section;
          if(source?.profile?.contours?.length)item.section=clone(source);
        }
      }
    }
  } else if(parameter) {
    const definition=drawingToolDescriptor(state,item)?.parameters?.find(candidate=>candidate.key===parameter);
    item.toolParameters??={};item.toolParameters[parameter]=definition?.valueType==="boolean"?!!target.checked
      :definition?.valueType==="string"?String(target.value):number(target.value,NaN);
  } else {
    item[field]=["face","reference","endDatum","datum","distributionMode","depthMode","layoutDatum","rowDistributionMode","direction"].includes(field)?String(target.value)
      :["enabled","opposite","through","reverse","allowOpen"].includes(field)?!!target.checked:number(target.value,NaN);
  }
  return true;
}
export function addDrawingFeature(state,part={}) {
  const feature=normalizeDrawingFeature(state.draft),error=validateDrawingFeature(feature);
  if(error){state.error=error;return false;}
  if(isDrawingToolReadOnly(state,feature)){state.error="退化定式刀具节点只读，仅可删除。";return false;}
  const index=state.features.findIndex(item=>item.id===state.editingId);
  const total=state.features.filter((_,i)=>i!==index).reduce((sum,item)=>sum+featureCount(item),0)+featureCount(feature);
  if(total>1000||index<0&&state.features.length>=256){state.error="超过单件刀具数量限制。";return false;}
  checkpointDrawing(state);
  if(index>=0)state.features=state.features.map((item,i)=>i===index?{...feature,id:item.id}:item);
  else state.features=[...state.features,feature];
  state.editingId="";state.draft={...clone(feature),id:uid(),station:feature.station+(feature.arrayPitch||50)};
  return true;
}
export function editDrawingFeature(state,action,index) {
  if(action==="undo"||action==="redo") {
    const from=action==="undo"?state.history:state.future,to=action==="undo"?state.future:state.history;
    if(!from?.length)return false;to.push(snapshot(state));Object.assign(state,from.pop());changed(state);return true;
  }
  if(action==="remove-end"&&["start","end"].includes(index)){checkpointDrawing(state);state.ends[index]={type:"keep"};return true;}
  const i=Number(index),feature=state.features?.[i];
  if(!Number.isInteger(i)||!feature||!["edit","copy","toggle","remove"].includes(action))return false;
  if(isDrawingToolReadOnly(state,feature)&&action!=="remove"){state.error="退化定式刀具节点只读，仅可删除。";return false;}
  checkpointDrawing(state);
  if(action==="edit"){state.editingId=feature.id;state.draft=clone(feature);}
  else if(action==="copy"){state.editingId="";state.draft={...clone(feature),id:uid(),enabled:true,station:feature.station+(feature.arrayPitch||50)};}
  else if(action==="toggle")feature.enabled=feature.enabled===false;
  else {state.features=state.features.filter((_,n)=>n!==i);if(state.editingId===feature.id)state.editingId="";}
  return true;
}
