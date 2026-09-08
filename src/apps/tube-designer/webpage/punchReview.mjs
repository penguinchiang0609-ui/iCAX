import { resolvePunchLayout } from "./punchLayout.mjs";
import { hasPunchArrayGroups, resolvePunchArrayGroups } from "./punchArrayGroups.mjs";

const escape = value => String(value??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const label = value => typeof value==="object" ? value?.["zh-CN"]??value?.["en-US"]??"" : value??"";
const dimensionNames={diameter:"直径",width:"宽",height:"高",depth:"高",wallThickness:"壁厚",thickness:"壁厚",radius:"半径",cornerRadius:"圆角",spanAlong:"长",spanAcross:"宽",angle:"夹角",azimuth:"周向",roll:"转角",length:"拉伸长"};
const number = value => value!==null&&value!==undefined&&Number.isFinite(Number(value)) ? Number(value).toLocaleString("zh-CN",{maximumFractionDigits:3}) : "—";
function stable(value) {
  if(Array.isArray(value))return value.map(stable);
  if(value&&typeof value==="object")return Object.fromEntries(Object.keys(value).sort().map(k=>[k,stable(value[k])]));
  return value;
}
function matchingDescriptor(state,item) {
  return state?.tools?.find(tool=>tool.id===item.toolRef?.id
    &&(!item.toolRef.version||tool.version===item.toolRef.version)
    &&(!item.toolRef.digest||tool.digest===item.toolRef.digest));
}
function hasSavedFrozenCut(item) {
  const frozen=item.frozenTool;
  return frozen?.schema==="icax.frozen-punch-tool"&&frozen.schemaVersion===1&&!!frozen.geometry&&!!frozen.instance
    &&!!item.frozenCut?.url&&Number(item.frozenCut.version)>0
    &&["id","version","digest"].every(key=>frozen.ref?.[key]===item.toolRef?.[key]);
}
function featureName(item,descriptor) {
  return item.section?.name||label(descriptor?.displayName)||item.toolLabel||({circle:"圆孔",rectangle:"矩形孔",slot:"腰形孔",ellipse:"椭圆孔"}[item.type])||item.type||"孔位记录";
}
function knownAxialHalfWidth(item) {
  const parameters=item.toolParameters??item;
  const type=item.type??item.toolRef?.id;
  if(type==="circle") {
    const radius=Number(parameters.diameter??item.diameter)/2;
    return Number.isFinite(radius)&&radius>0?radius:null;
  }
  if(!["rectangle","ellipse","slot"].includes(type))return null;
  const along=Number(parameters.spanAlong??parameters.length??item.spanAlong)/2;
  const across=Number(parameters.spanAcross??parameters.width??item.spanAcross)/2;
  const rotation=Number(item.rotation??0)*Math.PI/180;
  if(![along,across,rotation].every(Number.isFinite)||along<=0||across<=0)return null;
  if(type==="ellipse")return Math.hypot(along*Math.cos(rotation),across*Math.sin(rotation));
  if(type==="slot")return Math.max(0,along-across)*Math.abs(Math.cos(rotation))+across;
  // A rectangular outer bound is conservative for rounded corners.
  return along*Math.abs(Math.cos(rotation))+across*Math.abs(Math.sin(rotation));
}

/** Count placement positions separately from cutters and predicted wall openings. */
export function buildPunchReviewSummary(state,part={}) {
  const length=Number(state?.baseLength??part.length),groups=new Map(),issues=[],unverifiedRecords=[];
  let positions=0,cutters=0,skipped=0,disabled=0,estimatedOpenings=0,unknownOpenings=false;
  let finishedDatumRecords=0;
  for(const [index,item] of (state?.features??[]).entries()) {
    if(item.enabled===false){disabled++;continue;}
    const descriptor=matchingDescriptor(state,item);
    if(item.toolRef&&!descriptor) {
      const frozen=hasSavedFrozenCut(item),ready=state?.catalogueStatus==="ready";
      const message=!ready?"刀具目录尚未就绪，布局与数量待加载后复核。"
        :frozen?"刀具版本缺失，按已保存实体回放；不按当前管长重算布局与数量，请在三维预览复核。"
        :"刀具版本缺失，无法确认布局与数量，请检查缺失刀具。";
      unverifiedRecords.push({index,name:featureName(item),frozen,message});
      unknownOpenings=true;
      // Missing frozen versions replay saved solids; even a valid-looking rule
      // must not invent a newly expanded count from the current stock length.
      continue;
    }
    const grouped=hasPunchArrayGroups(item);
    const resolved=grouped?resolvePunchArrayGroups(item,length):resolvePunchLayout(item,length);
    const layoutError=grouped?resolved.arrayGroupsError:resolved.layoutError;
    if(layoutError){issues.push({index,message:layoutError});unknownOpenings=true;continue;}
    if(item.toolTarget!=="part"&&item.layoutDatum!=="base")finishedDatumRecords++;
    const name=featureName(item,descriptor);
    const dimensions=item.section?.parameters??item.toolParameters??(item.type==="circle"?{diameter:item.diameter}:{spanAlong:item.spanAlong,spanAcross:item.spanAcross});
    const key=JSON.stringify(stable({name,ref:item.toolRef,section:item.section?.profile,dimensions}));
    const definitions=item.section?.profile?.parameterDefinitions??descriptor?.parameters??[];
    const dimensionSummary=Object.entries(dimensions??{}).filter(([,v])=>typeof v!=="object").slice(0,3).map(([k,v])=>{
      const definition=definitions.find(d=>d.key===k);
      const option=definition?.options?.find(o=>String(o.value??o)===String(v));
      return (label(definition?.displayName)||dimensionNames[k]||k)+" "+(option?.label??v)+(definition?.unit??"");
    }).join(" · ");
    const group=groups.get(key)??{name,index,indices:[],positions:0,cutters:0,skipped:0,faces:new Set(),dimensions,dimensionSummary};
    const sum=grouped?{actualCount:resolved.arrayGroupsSummary.instanceCount,
      expandedCount:resolved.arrayGroupsSummary.expandedCount,skippedCount:resolved.arrayGroupsSummary.skippedCount}:resolved.layoutSummary;
    group.indices.push(index);group.positions+=sum.actualCount;group.cutters+=sum.expandedCount;group.skipped+=sum.skippedCount;
    group.faces.add(item.face??"top");groups.set(key,group);
    positions+=sum.actualCount;cutters+=sum.expandedCount;skipped+=sum.skippedCount;
    // A part-coordinate cutter may intersect several walls or cut a notch; do not
    // fabricate a physical hole count from the number of cutter solids.
    const halfWidth=knownAxialHalfWidth(item);
    const touchesEnd=halfWidth===null||sum.requiresIntersectionCheck||sum.firstCenter-halfWidth<=0||sum.lastCenter+halfWidth>=length;
    if(grouped||item.toolTarget==="part"||item.section||touchesEnd)unknownOpenings=true;
    else estimatedOpenings+=sum.actualCount*((item.through||item.opposite||item.depthMode==="both"||item.depthMode==="through")?2:1);
  }
  const previewCurrent=!!state?.preview&&state.preview.revision===state?.revision&&!state?.previewPending&&!state?.parameterEditor&&!state?.error;
  const previewLength=Number(state?.preview?.length??state?.preview?.bounds?.width);
  const counts=state?.preview;
  const toolsOnly=state?.preview?.toolsOnly===true;
  const exactPreviewCounts=previewCurrent&&!toolsOnly&&counts.toolCountExact===true
    &&[counts.appliedPunchToolCount,counts.outsideToolCount,counts.endToolCount].every(value=>Number.isSafeInteger(value)&&value>=0);
  return {length,finishedLength:previewCurrent&&!toolsOnly&&Number.isFinite(previewLength)?previewLength:null,previewCurrent,toolsOnly,
    exactPreviewCounts,appliedPunchTools:exactPreviewCounts?counts.appliedPunchToolCount:null,
    outsideTools:exactPreviewCounts?counts.outsideToolCount:null,endTools:exactPreviewCounts?counts.endToolCount:null,
    draftInPreview:previewCurrent&&state.preview?.includesDraft===true,groups:[...groups.values()].map(g=>({...g,faces:[...g.faces]})),
    positions,cutters,skipped,disabled,estimatedOpenings:unknownOpenings?null:estimatedOpenings,issues,
    unverifiedRecords,finishedDatumRecords,countsComplete:unverifiedRecords.length===0&&issues.length===0,
    ends:["start","end"].map(key=>({key,name:key==="start"?"左端":"右端",type:state?.ends?.[key]?.type??"keep",
      label:state?.ends?.[key]?.type==="keep"?"保留原端面":(state?.ends?.[key]?.toolLabel??state?.ends?.[key]?.type??"保留原端面")
        +((state?.ends?.[key]?.toolRef?.id??state?.ends?.[key]?.type)==="end-profile"?" · "+(state?.ends?.[key]?.toolParameters?.cutMode==="convex"?"凸口":"凹口"):"")}))};
}

export function renderPunchReview(state,part,action) {
  const s=buildPunchReviewSummary(state,part);
  const itemButton=(index,content)=>'<button type="button" data-cam-action="'+escape(action("summary-select"))+'" data-tube-designer-punch-index="'+index+'">'+content+'</button>';
  return '<aside class="tube-designer-punch-review" aria-label="零件复查摘要"'+(state?.parameterEditor?' inert':'')+'><header><strong>零件复查</strong><small>统计已添加记录，不含新增行</small></header>'
    +'<dl><div><dt>主管基准长度</dt><dd>'+number(s.length)+' <small>mm</small></dd></div>'+(s.toolsOnly?'':'<div><dt>预览包络长度</dt><dd>'+number(s.finishedLength)+' <small>mm</small></dd></div>')+'</dl>'
    +(s.toolsOnly?'<p class="tube-designer-punch-review-note">编辑中只显示主管与刀具体；成品在最终确认时计算。</p>':!s.previewCurrent?'<p class="tube-designer-punch-review-note">成品尺寸在最终确认时计算</p>':"")
    +(s.draftInPreview?'<p class="tube-designer-punch-review-note">当前预览含未添加孔刀；下方统计仅含已添加记录</p>':"")
    +'<div class="tube-designer-punch-review-ends">'+s.ends.map(e=>'<span>'+e.name+'：'+escape(e.label)+'</span>').join("")+'</div>'
    +'<dl><div><dt>'+(s.countsComplete?'布孔位置':'可计算布孔位置')+'</dt><dd>'+s.positions+'</dd></div><div><dt>孔刀实例</dt><dd>'+s.cutters+'</dd></div><div><dt>跳过位置 / 停用记录</dt><dd>'+s.skipped+' / '+s.disabled+'</dd></div></dl>'
    +(s.exactPreviewCounts?'<h3>本次三维预览'+(s.draftInPreview?'（含新增行）':'')+'</h3><dl><div><dt>实际相交孔刀</dt><dd>'+s.appliedPunchTools+'</dd></div><div><dt>未接触主管，已略过</dt><dd>'+s.outsideTools+'</dd></div><div><dt>端部刀具</dt><dd>'+s.endTools+'</dd></div></dl>':"")
    +(!s.countsComplete?'<p class="tube-designer-punch-review-note">上述数量不是零件总数；另有 '+(s.unverifiedRecords.length+s.issues.length)+' 条记录未计入，需复核。</p>':"")
    +(s.finishedDatumRecords?'<p class="tube-designer-punch-review-note">'+s.finishedDatumRecords+' 条记录使用成品端面基准；实际位置以三维预览为准，不按母材绝对端距推算。</p>':"")
    +'<h3>孔型与数量</h3><div class="tube-designer-punch-review-groups">'+(s.groups.length?s.groups.map(g=>itemButton(g.index,'<strong>'+escape(g.name)+'</strong><span>'+g.positions+' 个位置 · '+g.indices.length+' 条记录</span><small>'+escape(g.dimensionSummary)+'</small>')).join(""):'<p>暂无可计算的孔位记录</p>')+'</div>'
    +(s.unverifiedRecords.length?'<h3>待复核记录 '+s.unverifiedRecords.length+' 条</h3>'+s.unverifiedRecords.map(item=>itemButton(item.index,'<strong>'+escape(item.name)+(item.frozen?' · 固化实体':'')+'</strong><span>位置数待复核</span><small>'+escape(item.message)+'</small>')).join(""):"")
    +(s.issues.length?'<h3>待处理 '+s.issues.length+' 项</h3>'+s.issues.map(e=>itemButton(e.index,'<span class="tube-designer-punch-review-issue">第 '+(e.index+1)+' 行：'+escape(e.message)+'</span>')).join(""):"")
    +'<p class="tube-designer-punch-review-note">双面孔按一个布局位置计；相贯孔、开口和合并切口不能按刀具数推算。点击统计项定位记录。</p></aside>';
}

export const punchReviewStyles=String.raw`
.tube-designer-punch-review { min-width:0; min-height:0; overflow:auto; padding:12px; background:#f8fafb; border-left:1px solid #c6d4d9; color:#294852; font-size:12px; }
.tube-designer-punch-review header { display:grid; gap:4px; margin-bottom:14px; }
.tube-designer-punch-review header strong { font-size:15px; }
.tube-designer-punch-review header small,.tube-designer-punch-review-note { color:#6b8189; font-size:11px; line-height:1.6; }
.tube-designer-punch-review dl { margin:10px 0; }
.tube-designer-punch-review dl>div { display:flex; align-items:baseline; justify-content:space-between; gap:8px; padding:5px 0; }
.tube-designer-punch-review dd { margin:0; font-weight:600; font-variant-numeric:tabular-nums; }
.tube-designer-punch-review h3 { font-size:12px; margin:16px 0 8px; }
.tube-designer-punch-review-ends { display:grid; gap:5px; padding-block:10px; border-block:1px solid #dde5e8; }
.tube-designer-punch-review button { display:grid; gap:4px; width:100%; padding:8px; margin-bottom:6px; border:1px solid #d5e0e3; border-radius:4px; background:white; color:inherit; text-align:left; cursor:pointer; }
.tube-designer-punch-review button:hover,.tube-designer-punch-review button:focus-visible { border-color:#248c82; background:#edf7f5; }
.tube-designer-punch-review button small { color:#73878c; overflow-wrap:anywhere; }
.tube-designer-punch-review-issue { color:#a44329; }
.tube-designer-punch-backdrop { z-index:980; display:grid; place-items:center; padding:28px; box-sizing:border-box; background:rgba(12,25,31,.58); pointer-events:auto; }
.tube-designer-punch-dialog { width:min(1500px,calc(100vw - 32px)); height:min(1060px,calc(100vh - 32px)); border:1px solid rgba(78,109,122,.38); border-radius:11px; background:#f7f9fa; box-shadow:0 24px 64px rgba(3,17,23,.42); }
.tube-designer-punch-sheet-dialog { width:min(1780px,calc(100vw - 24px)); height:min(1120px,calc(100vh - 24px)); }
.tube-designer-punch-dialog > .tube-designer-dialog-header[data-tube-designer-punch-window-drag] { cursor:grab; touch-action:none; user-select:none; }
.tube-designer-punch-dialog > .tube-designer-dialog-header[data-tube-designer-punch-window-drag][data-dragging="true"] { cursor:grabbing; }
.tube-designer-punch-dialog > .tube-designer-dialog-header[data-tube-designer-punch-window-drag] button { cursor:pointer; }
.tube-designer-punch-sheet-workspace { grid-template-columns:minmax(0,1fr) 250px; grid-template-rows:auto minmax(460px,1fr) auto auto; }
.tube-designer-punch-sheet-workspace > .tube-designer-nesting-punch-setup { grid-column:1 / -1; grid-row:1; }
.tube-designer-punch-sheet-scene { grid-column:1; grid-row:2; }
.tube-designer-punch-sheet-scene .tube-designer-punch-preview-host { min-height:0; }
.tube-designer-punch-sheet-errors { grid-column:1; grid-row:3; }
.tube-designer-punch-sheet { grid-column:1; grid-row:4; }
.tube-designer-punch-review { grid-column:2; grid-row:2 / 5; }
.tube-designer-punch-sheet-scroll { max-height:210px; }
.tube-designer-punch-sheet table { width:max(100%,1500px); font-size:11px; }
.tube-designer-punch-sheet header { display:flex; align-items:center; justify-content:space-between; gap:12px; }
.tube-designer-punch-sheet-toolbar { display:flex; align-items:center; justify-content:flex-end; gap:8px; flex-wrap:wrap; }
.tube-designer-punch-sheet-toolbar small { color:#6a8188; white-space:nowrap; }
.tube-designer-punch-sheet-toolbar button { min-height:30px; padding:5px 10px; }
.tube-designer-punch-row-select { display:inline-flex; align-items:center; gap:5px; cursor:pointer; }
.tube-designer-punch-row-select input { margin:0; }
.tube-designer-punch-sheet-empty { padding:28px 12px !important; text-align:center; color:#6a8188; }
.tube-designer-punch-sheet th:nth-child(5) { width:90px; }
.tube-designer-punch-sheet th:nth-child(8) { width:270px; }
.tube-designer-punch-sheet th:nth-child(9) { width:180px; }
.tube-designer-punch-sheet thead th { font-size:11px; }
.tube-designer-punch-sheet select,.tube-designer-punch-sheet input,.tube-designer-punch-sheet button { font-size:11px; }
.tube-designer-punch-layout-field > span:first-child,.tube-designer-punch-layout-result,.tube-designer-punch-layout-hint,.tube-designer-punch-layout-details { font-size:10px; }
.tube-designer-punch-source-cell { display:grid; gap:5px; overflow-wrap:anywhere; }
.tube-designer-punch-source-cell > span { font-weight:600; color:#256b77; cursor:pointer; }
.tube-designer-punch-source-cell > small { color:#6a8188; font-size:10px; }
.tube-designer-punch-layout-overview-row td { padding:5px 10px; background:#f6faf9; }
.tube-designer-punch-parameter-backdrop { position:fixed; inset:0; z-index:1400; display:flex; align-items:center; justify-content:center; padding:20px; background:rgba(12,25,31,.08); pointer-events:none; }
.tube-designer-punch-parameter-dialog { width:min(1120px,calc(100vw - 36px)); max-height:min(650px,calc(100vh - 32px)); display:flex; flex-direction:column; background:rgba(248,252,252,.86); border:0; border-radius:0; box-shadow:none; color:#294852; font-size:13px; pointer-events:auto; }
.tube-designer-punch-parameter-dialog > header { display:flex; justify-content:space-between; align-items:center; padding:8px 14px; border-bottom:1px solid #c7d8dc; cursor:grab; touch-action:none; user-select:none; }
.tube-designer-punch-parameter-dialog > header[data-dragging="true"] { cursor:grabbing; }
.tube-designer-punch-parameter-dialog > header > div { display:grid; gap:5px; }
.tube-designer-punch-parameter-dialog > header strong { font-size:17px; }
.tube-designer-punch-parameter-dialog small { color:#6a8188; }
.tube-designer-punch-parameter-content { min-height:0; overflow:auto; padding:12px 16px; }
.tube-designer-punch-parameter-content > .tube-designer-punch-sheet-parameters { display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:10px 14px; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-parameters { grid-column:1 / -1; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-parameters > .tube-designer-punch-sheet-parameters { display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:10px 14px; }
.tube-designer-punch-parameter-content label { display:grid; gap:4px; min-width:0; }
.tube-designer-punch-parameter-content .tube-designer-punch-sheet-parameters label > small { font-size:11px; }
.tube-designer-punch-parameter-content input:not([type=checkbox]),.tube-designer-punch-parameter-content select { box-sizing:border-box; min-width:0; width:100%; min-height:30px; padding:4px 8px; border:1px solid #bcced4; border-radius:2px; color:#294852; font-size:13px; }
.tube-designer-punch-parameter-content .tube-designer-punch-sheet-number { display:flex; gap:5px; align-items:center; }
.tube-designer-punch-parameter-content .tube-designer-punch-end-placement { margin:8px 0 10px; padding:8px 10px; border-radius:2px; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram { width:min(430px,100%); justify-self:start; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram > .td-profile-parameter-diagram { width:100%; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-canvas { max-height:185px; overflow:hidden; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-svg { max-height:170px; object-fit:contain; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-diagram > header { padding:6px 8px 4px; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-legend { gap:0; padding:3px; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-legend-row { gap:5px; padding:4px 5px; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-legend-row > b { width:17px; height:17px; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-legend-row strong { font-size:10px; }
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-legend-row small,
.tube-designer-punch-parameter-content .tube-designer-punch-profile-diagram .td-profile-parameter-legend-row em { font-size:9px; }
.tube-designer-punch-parameter-content .punch-pose-fields { grid-template-columns:repeat(4,minmax(0,1fr)); gap:10px; margin-bottom:10px; }
.tube-designer-punch-parameter-content .punch-array-fields { grid-template-columns:repeat(4,minmax(0,1fr)); gap:8px 10px; padding:8px; }
.tube-designer-punch-parameter-content .punch-array-group { border-radius:2px; }
.tube-designer-punch-parameter-source { margin-bottom:10px; }
.tube-designer-punch-parameter-note { display:block; margin-top:10px; line-height:1.45; }
.tube-designer-punch-parameter-dialog button { padding:5px 12px; min-height:30px; cursor:pointer; }
.tube-designer-punch-parameter-dialog > header button { min-width:30px; padding:3px 8px; border:1px solid #adc3c7; border-radius:2px; background:#fff; color:#294852; font-size:18px; line-height:1; }
.tube-designer-punch-sheet-workspace { --tube-designer-punch-rightbar-width:clamp(440px,30vw,640px); grid-template-columns:clamp(240px,17vw,300px) minmax(420px,1fr) var(--tube-designer-punch-rightbar-width); grid-template-rows:minmax(0,1fr) auto; }
.tube-designer-punch-sheet-scene { grid-column:2; grid-row:1; }
.tube-designer-punch-sheet-errors { grid-column:2; grid-row:2; }
.tube-designer-punch-sidebar { grid-column:1; grid-row:1 / 3; display:flex; flex-direction:column; min-width:0; min-height:0; overflow:auto; border-right:1px solid #c6d4d9; background:#f8fafb; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup { position:relative; flex:0 0 auto; min-height:0; max-height:none; align-content:start; overflow:visible; padding:10px; background:#f7fafb; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup .tube-designer-nesting-punch-setup-grid { grid-template-columns:repeat(2,minmax(0,1fr)); grid-auto-rows:max-content; align-content:start; align-items:start; gap:6px; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup .tube-designer-field.wide { grid-column:1 / -1; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup .tube-designer-nesting-profile-diagram { position:static; grid-column:1 / -1; min-width:0; margin-top:2px; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup .tube-designer-nesting-profile-diagram > summary { min-height:24px; padding:4px 7px; font-size:10px; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup .tube-designer-nesting-profile-diagram[open] > .td-profile-parameter-diagram { position:static; width:100%; max-height:230px; overflow:auto; box-shadow:none; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup .tube-designer-field { gap:3px; font-size:10px; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup input,
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup select { min-height:27px; padding:3px 5px; font-size:10px; }
.tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup .tube-designer-field > small { font-size:9px; }
.tube-designer-punch-end-panel,
.tube-designer-punch-sidebar > .tube-designer-punch-ends[data-punch-end-panel] { flex:0 0 auto; min-width:0; max-height:none; margin-top:6px; overflow:visible; border-top:1px solid #c6d4d9; background:#eef4f5; }
.tube-designer-punch-end-panel > header,
.tube-designer-punch-sidebar > .tube-designer-punch-ends[data-punch-end-panel] > header { display:flex; align-items:baseline; justify-content:space-between; gap:6px; padding:6px 9px 4px; }
.tube-designer-punch-end-panel > header strong,
.tube-designer-punch-sidebar > .tube-designer-punch-ends[data-punch-end-panel] > header strong { color:#304f58; font-size:11px; }
.tube-designer-punch-end-panel > header span,
.tube-designer-punch-sidebar > .tube-designer-punch-ends[data-punch-end-panel] > header span { color:#71858b; font-size:9px; }
.tube-designer-punch-end-cards { display:grid; gap:5px; padding:0 7px 7px; }
.tube-designer-punch-end-card { min-width:0; padding:6px; border:1px solid #c7d6da; background:#fff; }
.tube-designer-punch-end-card > header { display:flex; align-items:baseline; gap:5px; min-width:0; margin-bottom:5px; }
.tube-designer-punch-end-card > header strong { flex:0 0 auto; color:#315760; font-size:10px; }
.tube-designer-punch-end-card > header span { min-width:0; overflow:hidden; color:#70858c; font-size:9px; text-overflow:ellipsis; white-space:nowrap; }
.tube-designer-punch-end-card-fields { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:5px; min-width:0; }
.tube-designer-punch-end-card-fields label { display:grid; gap:2px; min-width:0; color:#688087; font-size:9px; }
.tube-designer-punch-end-card-fields select { width:100%; min-width:0; height:25px; padding:2px 3px; border:1px solid #cedbdf; background:#fff; color:#314b53; font:inherit; }
.tube-designer-punch-end-card-fields .tube-designer-punch-source-cell { min-height:25px; align-content:center; }
.tube-designer-punch-end-card-fields .tube-designer-punch-source-cell > span { display:block; overflow:hidden; color:#256b77; text-overflow:ellipsis; white-space:nowrap; }
.tube-designer-punch-end-card-actions { display:flex; flex-wrap:wrap; justify-content:flex-end; gap:4px; margin-top:5px; }
.tube-designer-punch-end-card-actions button { min-height:24px; padding:2px 6px; font-size:9px; }
.tube-designer-punch-rightbar { position:relative; grid-column:3; grid-row:1 / 3; display:grid; grid-template-rows:minmax(0,1fr) auto; min-width:0; min-height:0; overflow:hidden; border-left:1px solid #c6d4d9; background:#f8fafb; }
.tube-designer-punch-rightbar-splitter { position:absolute; z-index:12; top:0; bottom:0; left:-5px; width:10px; cursor:col-resize; touch-action:none; user-select:none; }
.tube-designer-punch-rightbar-splitter::before { content:""; position:absolute; top:0; bottom:0; left:4px; width:1px; background:#9fb6bc; opacity:.9; }
.tube-designer-punch-rightbar-splitter:hover::before,
.tube-designer-punch-rightbar-splitter:focus-visible::before { width:3px; left:3px; background:#287f79; }
.tube-designer-punch-rightbar > .tube-designer-punch-records { display:grid; grid-template-rows:minmax(0,1fr); align-content:start; min-width:0; min-height:0; gap:6px; margin:0; overflow:hidden; }
.tube-designer-punch-rightbar .tube-designer-punch-records,
.tube-designer-punch-rightbar .tube-designer-punch-ends,
.tube-designer-punch-rightbar .tube-designer-punch-sheet { grid-column:auto; grid-row:auto; }
.tube-designer-punch-rightbar .tube-designer-punch-ends { min-width:0; min-height:0; max-height:none; overflow:auto; border-left:0; border-right:0; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet { min-width:0; min-height:0; margin:0; border-left:0; border-right:0; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet-scroll { min-height:0; max-height:none; overflow:auto; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet table { width:100%; min-width:0; table-layout:fixed; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(1) { width:30px; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(2) { width:40px; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(3) { width:22%; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(4) { width:25%; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(5) { width:auto; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet th:nth-child(6) { width:50px; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet td { overflow:hidden; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet header { min-height:36px; padding:4px 7px; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet header > div { min-width:0; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet header span { display:none; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet-toolbar { gap:4px; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet-toolbar small { display:none; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet-toolbar button { min-height:26px; padding:3px 6px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends table { table-layout:fixed; font-size:10px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends caption { padding:5px 7px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends caption small { display:none; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(1) { width:40px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(2) { width:66px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(3) { width:86px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(4) { width:auto; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th:nth-child(5) { width:78px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends th,.tube-designer-punch-rightbar .tube-designer-punch-ends td { padding:3px 4px; }
.tube-designer-punch-rightbar .tube-designer-punch-ends td { overflow:hidden; }
.tube-designer-punch-rightbar .tube-designer-punch-end-summary { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.tube-designer-punch-rightbar .tube-designer-punch-end-actions { display:flex; flex-wrap:wrap; gap:3px; }
.tube-designer-punch-rightbar .tube-designer-punch-source-cell > small,
.tube-designer-punch-rightbar .punch-array-summary > small { display:none; }
.tube-designer-punch-rightbar .punch-record-cell { gap:3px; }
.tube-designer-punch-rightbar .punch-pose-summary,
.tube-designer-punch-rightbar .punch-array-summary { display:block; max-height:2.9em; overflow:hidden; font-size:10px; line-height:1.4; }
.tube-designer-punch-rightbar .punch-array-summary { white-space:nowrap; text-overflow:ellipsis; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet-row-actions { justify-content:center; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet-parameters { flex-wrap:wrap; overflow:visible; }
.tube-designer-punch-rightbar .tube-designer-punch-sheet-parameters > label { min-width:65px; }
.tube-designer-punch-rightbar .tube-designer-punch-sidebar-actions { flex:0 0 auto; }
.tube-designer-punch-sidebar-actions { display:flex; justify-content:flex-end; gap:7px; padding:8px 10px; border-top:1px solid #c6d4d9; background:#fff; }
.tube-designer-punch-sidebar-actions button { min-height:30px; padding:5px 10px; }
@media(max-width:1000px) { .tube-designer-punch-parameter-dialog { width:min(852px,calc(100vw - 24px)); max-height:calc(100vh - 24px); } .tube-designer-punch-parameter-content > .tube-designer-punch-sheet-parameters,.tube-designer-punch-parameter-content .tube-designer-punch-profile-parameters > .tube-designer-punch-sheet-parameters,.tube-designer-punch-parameter-content .punch-pose-fields,.tube-designer-punch-parameter-content .punch-array-fields { grid-template-columns:repeat(2,minmax(0,1fr)); } }
@media(max-width:1100px) { .tube-designer-punch-sheet-workspace { --tube-designer-punch-rightbar-width:clamp(360px,38vw,480px); grid-template-columns:240px minmax(360px,1fr) var(--tube-designer-punch-rightbar-width); } }
@media(max-height:820px) { .tube-designer-punch-sheet-scroll { max-height:250px; } }
@media(max-width:900px) { .tube-designer-punch-sheet-workspace { grid-template-columns:minmax(0,1fr); grid-template-rows:auto minmax(280px,1fr) auto auto; overflow:auto; } .tube-designer-punch-sidebar { grid-column:1; grid-row:1; max-height:none; overflow:visible; } .tube-designer-punch-sidebar > .tube-designer-nesting-punch-setup { max-height:none; } .tube-designer-punch-sheet-scene { grid-column:1; grid-row:2; } .tube-designer-punch-sheet-errors { grid-column:1; grid-row:3; } .tube-designer-punch-rightbar { grid-column:1; grid-row:4; max-height:none; overflow:visible; } .tube-designer-punch-rightbar-splitter { display:none; } }
.tube-designer-punch-rightbar-resizing,
.tube-designer-punch-rightbar-resizing * { cursor:col-resize !important; user-select:none !important; }
`;
