import { renderViewCube } from "../../_shared/workbench/viewport/viewCube.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import { normalizePunchLayout, resolvePunchLayout, punchLayoutInstanceCount } from "./punchLayout.mjs";
import { migrateLegacyPunchArrays, resolvePunchArrayGroups, punchArraySkipText, hasPunchArrayGroups, punchArrayGroupInstanceCount } from "./punchArrayGroups.mjs";
import { renderPunchArrayGroupsControls, renderPunchArrayGroupsSummary } from "./punchArrayGroupsView.mjs";
import { migratePunchRecord, migratePunchRecipe, BRANCH_PLACEMENT_DEFAULTS } from "./punchToolMigration.mjs";

const PLANE_FACES = [
  { value: "top", label: "上方（+Z）" }, { value: "bottom", label: "下方（-Z）" },
  { value: "left", label: "左方（-Y）" }, { value: "right", label: "右方（+Y）" },
  { value: "round", label: "周向角度" },
];
const refs = [{ value: "start", label: "距起点" }, { value: "end", label: "距终点" }, { value: "center", label: "距中心" }];
const datums = [{ value: "long", label: "长点 / 包络" }, { value: "center", label: "端面中心" }, { value: "short", label: "短点" }];
const longDatumEndTools=new Set(["end-key-joint","end-step-z","end-profile"]);
const clone = value => structuredClone(value);
const recipeItem = value => { const item=clone(value); delete item.toolSnapshot; return item; };
const uid = () => globalThis.crypto?.randomUUID?.() ?? ("punch-" + Date.now() + "-" + Math.random().toString(36).slice(2));
const num = (value, fallback) => value === undefined ? fallback : value === "" || value === null ? NaN : Number(value);
const label = item => typeof item?.displayName === "string" ? item.displayName : item?.displayName?.["zh-CN"] ?? item?.id ?? "";
// Native end recipes retain the section but do not require the UI-only source
// tag. Infer it consistently in both the record row and its parameter window.
// Do not write the inferred tag into an immutable saved/frozen recipe.
const punchRecordKind = item => String(item?.recordKind ?? (item?.section ? (item.section.source === "dxf" ? "dxf" : "branch") : "tool"));

export function normalizePunchFeature(feature = {}) {
  feature = migratePunchRecord(feature);
  const recordKind = punchRecordKind(feature);
  const depthMode = String(feature.depthMode ?? (feature.through ? "through" : feature.opposite ? "both" : feature.reverse ? "reverse" : "single"));
  return { ...normalizePunchLayout(recipeItem(feature)), id: feature.id ?? uid(), type: String(feature.type ?? "circle"), recordKind, depthMode,
    face: String(feature.face ?? "top"), reference: feature.reference ?? "start", endDatum: feature.endDatum ?? "long",
    enabled: feature.enabled !== false, opposite: !!feature.opposite, through: !!feature.through, reverse: !!feature.reverse, allowOpen: !!feature.allowOpen,
    station: num(feature.station, 0), offset: num(feature.offset, 0), diameter: num(feature.diameter, 10),
    spanAlong: num(feature.spanAlong, 30), spanAcross: num(feature.spanAcross, 10), cornerRadius: num(feature.cornerRadius, 0),
    rotation: num(feature.rotation, 0), arrayCount: num(feature.arrayCount, 1), arrayPitch: num(feature.arrayPitch, 50),
    distributionMode: String(feature.distributionMode ?? "pitch"),
    headMargin: num(feature.headMargin, feature.station ?? 0), tailMargin: num(feature.tailMargin, 0),
    rowCount: num(feature.rowCount, 1), rowPitch: num(feature.rowPitch, 20),
  };
}
export function readPunchWizardFeatures(part = {}) {
  return (part?.properties?.["tubeDesigner.punchWizard"]?.features ?? []).map(normalizePunchFeature);
}
export function createPunchWizardState(part = {}) {
  const config = migratePunchRecipe(part?.properties?.["tubeDesigner.punchWizard"] ?? {});
  const features=(config.features ?? []).map(normalizePunchFeature);
  return { partId: String(part.entityId ?? ""), resourceId: part.manufacturingGeometryResourceId,
    resourceVersion: part.manufacturingGeometryResourceVersion, baseLength: Number(config.baseLength ?? part.length ?? 1000),
    features, originalFeatureRecipes:Object.fromEntries(features.map((feature,index)=>[feature.id,recipeItem(config.features[index])])),
    ends: Object.fromEntries(Object.entries(config.ends ?? { start: { type: "keep" }, end: { type: "keep" } }).map(([key,item])=>[key,recipeItem(item)])),
    draft: normalizePunchFeature({ recordKind: "branch", face: punchWizardProfileInfo(part).round ? "round" : "top",
      station: Number(config.baseLength ?? part.length ?? 1000) / 2, layoutDatum: "base" }),
    editingId: "", history: [], future: [], revision: 0,
    // Saved thumbnails are still in the original blank's 0..L coordinates.
    // End cuts may shorten part.length; preserve the recipe's blank midpoint.
    preview: part.thumbnailGeometryResourceId ? {geometry:{url:part.thumbnailGeometryResourceId,version:part.thumbnailGeometryResourceVersion},
      length:Number(config.baseLength ?? part.length ?? 0),revision:-1,isOriginal:true} : null,
    catalogueStatus: "idle", tools: [], error: "", selectedFeatureIds: [], parameterEditor: null,
    // UI-only width for the punch list. It is deliberately not part of the
    // persisted recipe, but survives the wizard's incremental DOM renders.
    rightbarWidth: null, wizardWindowPosition: null };
}
export function punchWizardProfileInfo(part = {}) {
  const p = part.profile ?? part.properties?.["tubeDesigner.profile"] ?? {};
  const width = Number(p.width ?? p.outerWidth ?? p.diameter ?? 0);
  const depth = Number(p.depth ?? p.height ?? p.outerDepth ?? p.diameter ?? 0);
  const round = /round|circle|circular|圆/.test(String(p.kind ?? p.shape ?? p.type ?? ""))
    || (p.contours?.length && p.contours.every(c => c.kind === "circle") && Math.abs(width-depth)<0.02);
  return { round: !!round, width, depth, diameter: Number(p.diameter ?? p.outerDiameter ?? (round ? width : 0)) };
}
export function getPunchWizardPayload(view) {
  const s = view.tubeDesignerPunchWizard;
  return { partEntityId: String(s?.partId ?? ""), resourceId: s?.resourceId, resourceVersion: s?.resourceVersion,
    features: (s?.features ?? []).map(feature=>{
      // Missing templates are immutable. Even harmless UI defaults change their
      // signed recipe and must not be sent when editing a different record.
      if(isPunchToolReadOnly(s,feature))return recipeItem(s.originalFeatureRecipes?.[feature.id]
        ?? (feature.frozenTool?.instance?{...feature.frozenTool.instance,frozenTool:feature.frozenTool,frozenCut:feature.frozenCut}:feature));
      const resolved=resolvePunchDistribution(feature,s?.baseLength);
      if(resolved.enabled===false&&resolved.layoutError) {
        delete resolved.arrayOffsets;delete resolved.rowOffsets;delete resolved.skippedInstances;
        delete resolved.arrayTransforms;
      }
      return stripPunchLayoutDerivedFields(resolved);
    }), ends: Object.fromEntries(Object.entries(s?.ends ?? {}).map(([key,item])=>[key,recipeItem(item)])) };
}

export function stripPunchLayoutDerivedFields(resolved) {
  const result={...resolved};
  // Persist editable array recipes and transport transforms, never UI summaries.
  for(const key of ["layoutSummary","layoutError","arrayGroupsSummary","arrayGroupsError","arrayInstances","arraySkipText"])delete result[key];
  return result;
}

export function resolvePunchDistribution(feature = {}, baseLength = 0) {
  const normalized=normalizePunchFeature(feature);
  if(hasPunchArrayGroups(normalized)) {
    const resolved=resolvePunchArrayGroups(normalized,baseLength);
    return {...resolved,layoutError:resolved.arrayGroupsError};
  }
  return resolvePunchLayout(normalized,baseLength);
}
// Native budgets candidates before suppressions, including the opposite wall.
// Frozen recipes are counted from their saved metadata, never recomputed.
function punchCandidateCount(feature) {
  if(feature?.enabled===false)return 0;
  const count=hasPunchArrayGroups(feature)
    ? Number(feature.arrayCandidateCount ?? feature.arrayTransforms?.length ?? 1)
    : Number(feature.arrayCount ?? 1)*Number(feature.rowCount ?? 1);
  const both=feature.depthMode==="both"||(feature.opposite&&!feature.through&&feature.depthMode!=="through");
  return count*(both?2:1);
}
function historySnapshot(s) {
  return clone({ features: s.features, ends: s.ends, draft: s.draft, editingId: s.editingId, drawing: s.drawing, baseLength: s.baseLength,
    ...(s.creationInput ? { creationInput: s.creationInput } : {}) });
}
function restoreHistorySnapshot(view, s, snapshot) {
  Object.assign(s, snapshot);
  // Creation uses the same stock input in the form, layout solver and native
  // request. Rebind the form after a cloned transaction replaces that input.
  if (s.creationInput?.draft) {
    view.tubeDesignerNestingPunchPartDraft = s.creationInput.draft;
    s.baseLength = Number(s.creationInput.draft.length);
  }
}
function checkpoint(s) {
  // Parameter edits form one transaction; cancelling must not leak edits or undo entries.
  if(s.parameterEditor)return;
  s.history ??= []; s.future = [];
  s.history.push(historySnapshot(s));
  if (s.history.length > 100) s.history.shift();
}
function changed(s) { s.revision = (s.revision ?? 0) + 1; s.error = ""; }
export function checkpointPunchWizard(s, { geometryChanged = true } = {}) {
  checkpoint(s);
  // Names, quantities and material labels belong to undo history but cannot
  // invalidate an unchanged stock/tool scene or clear a geometry failure.
  if(geometryChanged)changed(s);
}
export function openPunchParameters(view, index = "draft", end = "", mode = "shape") {
  const s=view?.tubeDesignerPunchWizard;
  if(!s||view.pending||s.parameterEditor)return false;
  const key=String(index??"draft");
  const item=end?s.ends?.[end]:key==="draft"?s.draft:s.features?.[Number(key)];
  if(!item||isPunchToolReadOnly(s,item))return false;
  delete s.uiFocus;
  mode=end?"shape":["shape","pose","arrays"].includes(mode)?mode:"shape";
  s.parameterEditor={index:key,end,mode,snapshot:historySnapshot(s),error:""};
  if(mode==="arrays"&&!hasPunchArrayGroups(item)) {
    const migrated=migrateLegacyPunchArrays(item,s.baseLength);
    if(migrated.arrayGroupsError)s.parameterEditor.error=migrated.arrayGroupsError;
    else {Object.assign(item,migrated);changed(s);}
  }
  if(!end&&key!=="draft")s.selectedFeatureId=item.id;
  return true;
}
export function closePunchParameters(view, commit, part, validate = true) {
  const s=view?.tubeDesignerPunchWizard,editor=s?.parameterEditor;
  if(!editor||view.pending)return false;
  const rollbackChangesGeometry=!commit&&JSON.stringify(historySnapshot(s))!==JSON.stringify(editor.snapshot);
  if(commit && validate) {
    const item=editor.end?s.ends?.[editor.end]:editor.index==="draft"?s.draft:s.features?.[Number(editor.index)];
    const descriptor=punchToolDescriptor(s,item);
    let error=editor.end?validatePunchEnd(item):validatePunchFeature({...part,length:s.baseLength??part?.length},item);
    for(const definition of descriptor?.parameters??[]) {
      if(!matchesVisibility(definition.visibleWhen,item.toolParameters??{}))continue;
      const value=item.toolParameters?.[definition.key]??definition.defaultValue;
      if(definition.valueType!=="string"&&definition.valueType!=="boolean"&&(!Number.isFinite(value)
        ||(definition.min!==undefined&&value<definition.min)||(definition.max!==undefined&&value>definition.max))) {
        error ||= label(definition)+"不在允许的数值范围内。";
      }
    }
    if(error){editor.error=error;return false;}
  }
  if(commit) {
    s.history??=[];s.history.push(editor.snapshot);if(s.history.length>100)s.history.shift();s.future=[];
  } else restoreHistorySnapshot(view,s,editor.snapshot);
  s.parameterEditor=null;s.error="";s.restoreParameterFocus={index:editor.index,end:editor.end,mode:editor.mode??"shape"};delete s.uiFocus;
  // Field edits already advanced the geometry revision. Confirming only commits
  // the transaction, so its completed preview can be reused; rollback changes it.
  if(rollbackChangesGeometry)changed(s);
  return true;
}
export function punchToolDescriptor(s, item) {
  return s?.tools?.find(t => t.id === item?.toolRef?.id
    && (!item.toolRef.version || t.version===item.toolRef.version)
    && (!item.toolRef.digest || t.digest===item.toolRef.digest));
}
export function isPunchToolReadOnly(s,item) {
  return s?.catalogueStatus==="ready" && !!item?.toolRef && !punchToolDescriptor(s,item);
}
export function hasFrozenPunchTool(item) {
  const f=item?.frozenTool;
  return f?.schema==="icax.frozen-punch-tool" && f.schemaVersion===1 && !!f.geometry && !!f.instance
    && !!item.frozenCut?.url && Number(item.frozenCut.version)>0
    && ["id","version","digest"].every(k=>f.ref?.[k]===item.toolRef?.[k]);
}
function unavailableItems(s) {
  return [...(s?.features??[]),...Object.values(s?.ends??{}).filter(e=>e.type!=="keep")]
    .filter(item=>isPunchToolReadOnly(s,item));
}
export function missingPunchTools(s) {
  if(s?.catalogueStatus!=="ready")return [];
  return [...s.features.filter(f=>f.enabled!==false),...Object.values(s.ends??{}).filter(e=>e.type!=="keep")]
    .filter(item=>isPunchToolReadOnly(s,item)&&!hasFrozenPunchTool(item))
    .map(item=>({id:item.toolRef.id,version:item.toolRef.version,label:item.toolLabel??item.toolRef.id}));
}
export function selectPunchTool(s, item, id) {
  const tool = s.tools.find(t => t.id === id);
  if (!tool) return;
  item.type = id; item.toolRef = { id: tool.id, version: tool.version, digest: tool.digest };
  item.toolLabel=label(tool);item.toolKind=tool.kind;
  item.toolParameters = clone(tool.defaultParameters ?? {});
  item.recordKind=tool.requiresSection?(item.section?.source==="dxf"?"dxf":"branch"):"tool";
  if(tool.target==="part") {
    item.toolTarget="part";item.face=["top","left","round"].includes(item.face)?item.face:"top";item.offset=0;item.rotation=0;item.opposite=false;item.through=false;item.reverse=false;item.depthMode="single";
    if (tool.requiresSection) for (const [key, value] of Object.entries(BRANCH_PLACEMENT_DEFAULTS)) item[key] ??= value;
  } else delete item.toolTarget;
  if(!tool.requiresSection)delete item.section;
  if(tool.target==="end"&&longDatumEndTools.has(tool.id))item.datum="long";
  delete item.toolSnapshot;
  delete item.frozenTool;
  delete item.frozenCut;
}
export function updatePunchWizardField(view, target) {
  const s = view?.tubeDesignerPunchWizard, field = target?.dataset?.tubeDesignerPunchField;
  if (!s || !field || view.pending) return false;
  const end = target.dataset.tubeDesignerPunchEnd;
  const row = target.dataset.tubeDesignerPunchIndex;
  const rowIndex = row !== undefined && row !== "" && row !== "draft" ? Number(row) : -1;
  const rowFeature = Number.isInteger(rowIndex) && rowIndex >= 0 ? s.features?.[rowIndex] : null;
  const item = end ? (s.ends[end] ??= { type: "keep" }) : rowFeature ?? s.draft;
  if (!item) return false;
  if(isPunchToolReadOnly(s,item)) { s.error="退化定式刀具节点只读，仅可删除。"; return false; }
  checkpoint(s);
  const parameter = target.dataset.tubeDesignerPunchParameter;
  if (field === "tool") {
    if (end && target.value === "keep") s.ends[end] = { type: "keep", trim: 0, datum: "long", rotation: 0 };
    else {
      selectPunchTool(s, item, String(target.value));
      if(!end||!punchToolDescriptor(s,item)?.requiresSection)item.recordKind="tool";
      if (end) { item.trim ??= 0; item.datum ??= "long"; item.rotation ??= 0; }
    }
  } else if (parameter) {
    const def = punchToolDescriptor(s, item)?.parameters?.find(p => p.key === parameter);
    item.toolParameters ??= {};
    item.toolParameters[parameter] = def?.valueType === "boolean" ? !!target.checked : def?.valueType === "string" ? String(target.value) : num(target.value, NaN);
  } else {
    item[field] = ["face", "reference", "endDatum", "datum", "recordKind", "distributionMode", "depthMode", "layoutDatum", "centerMode", "fillAlign", "spacingSequence", "positionList", "skipInstancesText", "rowDistributionMode", "direction"].includes(field) ? String(target.value)
      : ["enabled", "opposite", "through", "reverse", "allowOpen"].includes(field) ? !!target.checked : num(target.value, NaN);
    if(field==="centerFirstOffset"&&target.value==="")item[field]=null;
    if(field==="distributionMode")item.layoutDatum="base";
    if(field==="face"&&item.face!=="round")item.rowDistributionMode="pitch";
    if(field==="depthMode") {
      item.opposite=item.depthMode==="both";
      item.through=item.depthMode==="through";
      item.reverse=item.depthMode==="reverse";
    }
  }
  if (rowFeature) s.selectedFeatureId = rowFeature.id;
  changed(s); return true;
}
export function validatePunchFeature(part, feature) {
  const f = resolvePunchDistribution(feature,part?.length);
  if (!f.enabled) return "";
  if (f.layoutError) return f.layoutError;
  if (!(Number(part?.length) > 0)) return "零件长度不可用。";
  if (![f.station,f.offset,f.rotation,f.arrayPitch,f.rowPitch].every(Number.isFinite)) return "位置和间距必须是有效数字。";
  if (!refs.some(r=>r.value===f.reference) || !datums.some(r=>r.value===f.endDatum) || !PLANE_FACES.some(r=>r.value===f.face)) return "请选择有效的定位基准与方向。";
  if (![f.arrayCount,f.rowCount].every(n=>Number.isInteger(n)&&n>=1&&n<=1000)) return "阵列数量须为 1 至 1000 的整数。";
  if ((!f.arrayOffsets?.length && f.arrayCount>1 && Math.abs(f.arrayPitch)<1e-6) || (!f.rowOffsets?.length && f.rowCount>1 && Math.abs(f.rowPitch)<1e-6)) return "多个孔的阵列间距不能为零。";
  if (punchCandidateCount(f)>1000) return "单个零件最多支持 1000 个展开刀具。";
  if (f.toolRef) {
    if (!f.toolRef.id) return "请选择刀具模板。";
    if ((f.recordKind==="branch"||f.recordKind==="dxf") && !f.section?.profile?.contours?.length) return "请选择支管管型或导入有效的本地 DXF 截面。";
    if (f.toolRef.id==="branch-profile" && (![f.angle,f.azimuth,f.roll,f.offsetY,f.offsetZ,f.length].every(Number.isFinite) || !(f.length>0)
      || !["through","symmetric","positive","negative"].includes(f.direction??"through"))) return "支管定位与拉伸参数无效。";
    if (Object.values(f.toolParameters ?? {}).some(v => typeof v === "number" && !Number.isFinite(v))) return "刀具参数必须是有效数字。";
    return ""; // Actual footprint/curved walls are checked against the native BRep.
  }
  // Legacy feature migration keeps its original dimensions, never converts unknown tools to circles.
  if (!["circle","rectangle","slot","ellipse","custom"].includes(f.type)) return "旧孔类型无法识别，请选择刀具模板。";
  if (f.type==="circle" ? !(f.diameter>0) : !(f.spanAlong>0&&f.spanAcross>0)) return "刀具尺寸必须大于零。";
  if (f.type==="slot" && f.spanAlong<=f.spanAcross) return "腰形孔的长度必须大于宽度。";
  return "";
}
export function validatePunchWizard(view, part) {
  const s = view.tubeDesignerPunchWizard;
  if (!s) return "冲孔向导未打开。";
  // Rendering is an inspection aid. Final application is calculated from the
  // saved recipe by the native operation and must not be silently disabled by
  // a stale/temporary viewport resource failure.
  if (s.parameterEditor) return "请先关闭当前参数编辑。";
  if (s.editingId) return "请先保存当前特征的修改。";
  const missing=missingPunchTools(s);
  if(missing.length)return "无法重新计算：本机缺少对应程式且没有固化刀具："+missing.map(t=>t.label+" @ "+t.version).join("、")+"。已保存实体不受影响，只能删除缺失节点或安装原版刀具。";
  if (s.features.length > 256) return "最多支持 256 条刀具配置。";
  let total = 0;
  for (const f of s.features) {
    if(isPunchToolReadOnly(s,f)) {
      total+=punchCandidateCount(f);
      continue;
    }
    const error = validatePunchFeature({ ...part, length: s.baseLength ?? part.length }, f);
    if (error) return error;
    if (f.enabled !== false) {
      const resolved=resolvePunchDistribution(f,s.baseLength??part.length);
      total += punchCandidateCount(resolved);
    }
  }
  if (total > 1000) return "单个零件最多支持 1000 个展开刀具。";
  for (const e of Object.values(s.ends ?? {})) if (e.type !== "keep") {
    if(isPunchToolReadOnly(s,e))continue;
    const error=validatePunchEnd(e);if(error)return error;
  }
  return "";
}
function validatePunchEnd(item) {
  if(!item||item.type==="keep")return "";
  if (![Number(item.trim??0),Number(item.rotation??0)].every(Number.isFinite)||Number(item.trim??0)<0)return "端部定位参数无效。";
  if(!datums.some(d=>d.value===(item.datum??"long")))return "请选择有效的端部定位基准。";
  if(longDatumEndTools.has(item.toolRef?.id??item.type)&&(item.datum??"long")!=="long")return "此端部刀具按长点 / 包络定位。";
  if((item.toolRef?.id??item.type)==="end-profile"&&!item.section?.profile?.contours?.length)return "请选择切端用支管管型或导入有效的本地 DXF 截面。";
  const id=item.toolRef?.id??item.type;
  const placementKeys=id==="end-profile"?["angle","azimuth","roll","axialOffset","offsetY","offsetZ"]
    :id==="end-convex"||id==="end-cope"?["angle","offset"]:id==="end-key-joint"?["offset"]:[];
  if(placementKeys.some(key=>item[key]!==undefined&& !Number.isFinite(Number(item[key]))))return "端部姿态参数无效。";
  if(Object.values(item.toolParameters??{}).some(v=>typeof v==="number"&&!Number.isFinite(v)))return "端部刀具参数无效。";
  return "";
}
export function addPunchWizardFeature(view, part) {
  const s = view?.tubeDesignerPunchWizard;
  if (!s || view.pending) return false;
  if(s.parameterEditor){s.parameterEditor.error="请先关闭参数编辑，再添加记录。";return false;}
  const f = resolvePunchDistribution(s.draft,s.baseLength ?? part.length), error = validatePunchFeature({ ...part, length: s.baseLength ?? part.length },f);
  if (error) { s.error = error; return false; }
  if(f.toolRef && s.catalogueStatus==="ready"&&!punchToolDescriptor(s,f)) {
    s.error="退化定式刀具节点只读，仅可删除。";return false;
  }
  const index = s.features.findIndex(item=>item.id===s.editingId);
  const count = s.features.filter((_,i)=>i!==index).reduce((n,item)=>{
    const resolved=isPunchToolReadOnly(s,item)?item:resolvePunchDistribution(item,s.baseLength??part.length);
    return n+punchCandidateCount(resolved);
  },0)
    + punchCandidateCount(f);
  if (count>1000 || (index<0&&s.features.length>=256)) { s.error="超过单件刀具数量限制。"; return false; }
  checkpoint(s);
  if (index>=0) s.features = s.features.map((item,i)=>i===index?{...f,id:item.id}:item);
  else s.features = [...s.features,{...f,id:uid()}];
  s.editingId = ""; s.draft = { ...clone(f), id:uid(), station: f.station+(f.arrayPitch||50) };
  changed(s); return true;
}
export function removePunchWizardFeature(view, index) {
  return editPunchWizardFeature(view,"remove",index);
}

/** Toggle the transient row-selection state used by the table toolbar. */
export function setPunchFeatureSelected(view, index, selected) {
  const s = view?.tubeDesignerPunchWizard;
  const i = Number(index), item = s?.features?.[i];
  if (!s || !Number.isInteger(i) || !item) return false;
  const ids = new Set(Array.isArray(s.selectedFeatureIds) ? s.selectedFeatureIds : []);
  if (selected) {
    ids.add(item.id);
    // The last checked row is also the current row for the toolbar's copy
    // command, while the set still supports multi-row deletion.
    s.selectedFeatureId = item.id;
  } else {
    ids.delete(item.id);
    if (s.selectedFeatureId === item.id) s.selectedFeatureId = [...ids].at(-1) ?? "";
  }
  s.selectedFeatureIds = [...ids];
  return true;
}

/** Remove all rows selected in the table toolbar in one undoable transaction. */
export function removeSelectedPunchWizardFeatures(view) {
  const s = view?.tubeDesignerPunchWizard;
  if (!s || view.pending) return false;
  const selected = new Set(Array.isArray(s.selectedFeatureIds) ? s.selectedFeatureIds : []);
  if (!selected.size) return false;
  const removed = s.features.filter(item => selected.has(item.id));
  if (!removed.length) {
    s.selectedFeatureIds = [];
    return false;
  }
  checkpoint(s);
  s.features = s.features.filter(item => !selected.has(item.id));
  s.selectedFeatureIds = [];
  if (s.selectedFeatureId && removed.some(item => item.id === s.selectedFeatureId)) delete s.selectedFeatureId;
  if (s.editingId && removed.some(item => item.id === s.editingId)) {
    s.editingId = "";
    s.draft = normalizePunchFeature({ recordKind: "branch", station: Number(s.baseLength ?? 1000) / 2, layoutDatum: "base" });
  }
  changed(s);
  return true;
}
export function editPunchWizardFeature(view, action, index) {
  const s = view?.tubeDesignerPunchWizard;
  if (!s || view.pending) return false;
  if (action === "undo" || action === "redo") {
    const from = action==="undo"?s.history:s.future, to = action==="undo"?s.future:s.history;
    if (!from?.length) return false;
    to.push(historySnapshot(s));
    restoreHistorySnapshot(view,s,from.pop()); changed(s); return true;
  }
  if(action==="remove-end" && ["start","end"].includes(index)) {
    checkpoint(s);s.ends[index]={type:"keep"};changed(s);return true;
  }
  const i = Number(index), f = s.features?.[i];
  if (!Number.isInteger(i) || !f) return false;
  if(isPunchToolReadOnly(s,f)&&action!=="remove") { s.error="退化定式刀具节点只读，仅可删除。";return false; }
  checkpoint(s);
  if (action==="edit") { s.editingId=f.id; s.draft=clone(f); }
  else if (action==="copy") { s.editingId=""; s.draft={...clone(f),id:uid(),enabled:true,station:f.station+(f.arrayPitch||50)}; }
  else if (action==="toggle") f.enabled = f.enabled===false;
  else if (action==="remove") {
    s.features=s.features.filter((_,n)=>n!==i);
    s.selectedFeatureIds=(s.selectedFeatureIds??[]).filter(id=>id!==f.id);
    if(s.selectedFeatureId===f.id)delete s.selectedFeatureId;
    if(s.editingId===f.id)s.editingId="";
  }
  else return false;
  changed(s); return true;
}
export function installPunchCatalogue(s, result) {
  s.tools = Array.isArray(result?.tools) ? result.tools : [];
  s.catalogueStatus = "ready";
  for(const item of [s.draft,...s.features]) if(!item.toolRef) {
    const tool=s.tools.find(t=>t.id===item.type);
    if(tool) {
      const supplied=Object.fromEntries((tool.parameters??[]).map(p=>[p.key,item[p.key]??p.defaultValue]));
      selectPunchTool(s,item,tool.id);item.toolParameters=supplied;
    }
  }
  for(const item of Object.values(s.ends??{})) if(item.type!=="keep"&&!item.toolRef) {
    const tool=s.tools.find(t=>t.target==="end"&&t.id==="end-"+item.type);
    if(tool) {
      const supplied=Object.fromEntries((tool.parameters??[]).map(p=>[p.key,item[p.key]??p.defaultValue]));
      selectPunchTool(s,item,tool.id);item.toolParameters=supplied;
    }
  }
  s.catalogueErrors = result?.errors ?? [];
}

function tableFieldAttrs(action, field, index, parameter = "", end = "") {
  return ' data-cam-change-action="'+action("field-change")+'" data-tube-designer-punch-field="'+field+'"'
    +' data-tube-designer-punch-index="'+escapeText(index)+'"'
    +(parameter?' data-tube-designer-punch-parameter="'+escapeText(parameter)+'"':"")
    +(end?' data-tube-designer-punch-end="'+escapeText(end)+'"':"");
}

function tableSelect(action, field, value, items, index, disabled = false, end = "") {
  return '<select'+tableFieldAttrs(action,field,index,"",end)+(disabled?' disabled':'')+'>'
    +items.map(item=>'<option value="'+escapeText(item.value)+'" '+(String(item.value)===String(value)?'selected':'')+'>'+escapeText(item.label)+'</option>').join("")
    +'</select>';
}

function tableNumber(action, field, value, index, unit = "", disabled = false, end = "") {
  return '<span class="tube-designer-punch-sheet-number"><input type="number" step="any" value="'+escapeText(value)+'"'
    +tableFieldAttrs(action,field,index,"",end)+(disabled?' disabled':'')+'/></span>';
}

function tableCheck(action, field, checked, index, labelText, disabled = false) {
  return '<label class="tube-designer-punch-sheet-check"><input type="checkbox"'
    +tableFieldAttrs(action,field,index)+' '+(checked?'checked ':'')+(disabled?'disabled':'')+'/><span>'+escapeText(labelText)+'</span></label>';
}

function tableActionSelect(action, suffix, value, items, index, disabled = false, end = "") {
  return '<select data-cam-change-action="'+action(suffix)+'" data-tube-designer-punch-index="'+escapeText(index)+'"'+(end?' data-tube-designer-punch-end="'+escapeText(end)+'"':"")+(disabled?' disabled':'')+'>'
    +items.map(item=>'<option value="'+escapeText(item.value)+'" '+(String(item.value)===String(value)?'selected':'')+'>'+escapeText(item.label)+'</option>').join("")+'</select>';
}

function tableToolSelect(action, s, item, index, disabled) {
  const available=punchToolDescriptor(s,item);
  const unresolved=!!item.toolRef&&!available;
  const id=unresolved?'__unavailable__':item.toolRef?.id??"";
  const items=[{value:"",label:"选择标准刀具"},
    ...(unresolved?[{value:"__unavailable__",label:"缺少："+(item.toolLabel??item.toolRef.id)}]:[]),
    ...s.tools.filter(tool=>tool.target==="side"||(tool.target==="part"&&!tool.requiresSection)).map(tool=>({value:tool.id,label:label(tool)}))];
  return tableSelect(action,"tool",id,items,index,disabled);
}
function tableEndToolSelect(action,s,item,end,disabled) {
  const unavailable=!!item.toolRef&&!punchToolDescriptor(s,item);
  const id=unavailable?"__unavailable__":item.toolRef?.id??"keep";
  const choices=[{value:"keep",label:"保留原端面"},...(unavailable?[{value:"__unavailable__",label:"缺少："+(item.toolLabel??item.toolRef.id)}]:[]),
    ...s.tools.filter(tool=>tool.target==="end"&&!tool.requiresSection).map(tool=>({value:tool.id,label:label(tool)}))];
  return tableSelect(action,"tool",id,choices,end,disabled,end);
}

function tableParameterFields(action, descriptor, item, index, disabled, end = "") {
  if(!descriptor) {
    if(item.toolRef)return '<span class="tube-designer-punch-sheet-readonly">固化刀具</span>';
    const dimensions=item.type==="circle"
      ? [["diameter","直径",item.diameter,"mm"]]
      : [["spanAlong","长",item.spanAlong,"mm"],["spanAcross","宽",item.spanAcross,"mm"],
        ...((item.type==="rectangle")?[["cornerRadius","R",item.cornerRadius,"mm"]]:[])];
    return dimensions.map(([field,name,value,unit])=>'<label><small>'+name+(unit?'（'+unit+'）':"")+'</small>'+tableNumber(action,field,value,index,"",disabled,end)+'</label>').join("");
  }
  if(descriptor.kind==="fixed")return '<span class="tube-designer-punch-sheet-readonly">定式尺寸</span>';
  return (descriptor.parameters??[]).filter(definition=>matchesVisibility(definition.visibleWhen,item.toolParameters??{})).map(definition=>{
    const value=item.toolParameters?.[definition.key]??definition.defaultValue;
    const attrs=tableFieldAttrs(action,"parameter",index,definition.key,end);
    let input;
    if(definition.options)input='<select'+attrs+(disabled?' disabled':'')+'>'+definition.options.map(option=>{
      const optionValue=option.value??option;
      return '<option value="'+escapeText(optionValue)+'" '+(String(optionValue)===String(value)?'selected':'')+'>'+escapeText(option.label??optionValue)+'</option>';
    }).join("")+'</select>';
    else if(definition.valueType==="boolean")input='<input type="checkbox"'+attrs+' '+(value?'checked ':'')+(disabled?'disabled':'')+'/>';
    else input='<span class="tube-designer-punch-sheet-number"><input type="'+(definition.valueType==="string"?'text':'number')+'" value="'+escapeText(value??"")+'"'+attrs+(disabled?' disabled':'')+'/></span>';
    return '<label><small>'+escapeText(label(definition)+(definition.unit?'（'+definition.unit+'）':""))+'</small>'+input+'</label>';
  }).join("");
}

function tableProfileSelect(action, item, index, choices, disabled, end = "") {
  const key=item.section?.source==="dxf"?"__dxf__":item.section?.key??"";
  const items=[{value:"",label:"选择管型库截面"},...choices.map(choice=>({value:choice.key,label:choice.name+(choice.specification?" · "+choice.specification:"")})),
    {value:"__dxf__",label:item.section?.source==="dxf"?("本地 DXF · "+(item.section.name??"已导入")):"导入本地 DXF…"}];
  return tableActionSelect(action,"profile-select",key,items,index,disabled,end);
}

function tableProfileParameters(action,item,index,choices,disabled,end = "") {
  if(item.section?.source==="dxf")return '<span class="tube-designer-punch-sheet-readonly">DXF 定式截面</span>';
  const choice=choices.find(candidate=>candidate.key===item.section?.key);
  const values=item.section?.parameters??choice?.defaultParameters??{};
  const definitions=(choice?.definitions??[]).filter(definition=>matchesVisibility(definition?.visibleWhen,values));
  const fields=definitions.map(definition=>{
    const value=values[definition.key]??definition.defaultValue;
    const attrs=' data-cam-change-action="'+action("profile-parameter")+'" data-tube-designer-punch-index="'+escapeText(index)+'" data-tube-designer-punch-profile-parameter="'+escapeText(definition.key)+'" data-profile-parameter-key="'+escapeText(definition.key)+'"'+(end?' data-tube-designer-punch-end="'+escapeText(end)+'"':"");
    let input;
    if(definition.options)input='<select'+attrs+(disabled?' disabled':'')+'>'+definition.options.map(option=>{const optionValue=option.value??option;return '<option value="'+escapeText(optionValue)+'" '+(String(optionValue)===String(value)?'selected':'')+'>'+escapeText(option.label??optionValue)+'</option>';}).join("")+'</select>';
    else if(definition.valueType==="boolean")input='<input type="checkbox"'+attrs+' '+(value?'checked ':'')+(disabled?'disabled':'')+'/>';
    else input='<span class="tube-designer-punch-sheet-number"><input type="'+(definition.valueType==="string"?'text':'number')+'" step="'+escapeText(definition.step??(definition.valueType==="integer"?1:"any"))+'" value="'+escapeText(value??"")+'"'+attrs+(disabled?' disabled':'')+'/></span>';
    return '<label><small>'+escapeText(label(definition)+(definition.unit?'（'+definition.unit+'）':""))+'</small>'+input+'</label>';
  }).join("");
  const diagram=renderProfileParameterDiagram(item.section?.profile,{definitions:definitions.length?definitions:undefined,parameters:values,compact:true,title:"支管参数示意图"});
  return '<div class="tube-designer-punch-profile-parameters" data-profile-parameter-scope><div class="tube-designer-punch-sheet-parameters">'+fields+'</div>'
    +(diagram?'<details class="tube-designer-punch-profile-diagram"><summary>支管参数示意图</summary>'+diagram+'</details>':"")+'</div>';
}

function matchesVisibility(condition,values) {
  if(!condition)return true;
  if(Array.isArray(condition.conditions))return condition.op==="any"
    ? condition.conditions.some(item=>matchesVisibility(item,values))
    : condition.conditions.every(item=>matchesVisibility(item,values));
  if(Array.isArray(condition.all))return condition.all.every(item=>matchesVisibility(item,values));
  if(Array.isArray(condition.any))return condition.any.some(item=>matchesVisibility(item,values));
  const value=values?.[condition.key??condition.parameter];
  return condition.op==="ne"?value!==condition.value:condition.op==="eq"?value===condition.value:true;
}

function punchSourceName(item,descriptor) {
  return item.section?.name || label(descriptor) || item.toolLabel || ({circle:"圆孔",rectangle:"矩形孔",slot:"腰形孔",ellipse:"椭圆孔"}[item.type]) || item.type;
}
function punchParameterSummary(item, descriptor, choices=[]) {
  if(item.section?.source==="dxf")return "DXF 定式截面 · 姿态可编辑";
  const choice=choices.find(candidate=>candidate.key===item.section?.key);
  const definitions=item.section?(choice?.definitions??[]):descriptor?.parameters??[];
  const values=item.section?item.section.parameters:item.toolParameters;
  const parts=definitions.filter(d=>matchesVisibility(d.visibleWhen,values??{})).slice(0,3).map(d=>{
    const value=values?.[d.key]??d.defaultValue;
    const option=d.options?.find(o=>String(o.value??o)===String(value));
    return label(d)+" "+(option?.label??(typeof value==="boolean"?(value?"是":"否"):value??"—"))+(d.unit??"");
  });
  return parts.join(" · ")||item.section?.profile?.specification||(item.toolKind==="fixed"?"定式尺寸":item.type==="circle"?"直径 "+item.diameter+" mm":"点击编辑参数");
}
function previewModeButtons(s,btn) {
  return '<span class="tube-designer-punch-preview-modes" aria-label="三维显示内容"><span>主管 + 刀具体</span></span>';
}
function punchPoseDescriptor(descriptor, item, pose) {
  return pose ? undefined : descriptor;
}
function punchArrayViewState(item,baseLength) {
  const feature=migrateLegacyPunchArrays(item,baseLength);
  return {feature,groups:feature.arrayGroups??[],result:resolvePunchArrayGroups(feature,baseLength)};
}
function renderPunchPoseFields(action,item,index,disabled,descriptor) {
  const wrap=(name,control,unit="")=>'<label class="punch-pose-field"><span>'+name+(unit?'（'+unit+'）':"")+'</span>'+control+'</label>';
  const location=wrap("长度方向基准",tableSelect(action,"reference",item.reference,refs,index,disabled))
    +wrap("单个刀具起始位置",tableNumber(action,"station",item.station,index,"",disabled),"mm");
  const side=item.toolTarget==="part" ? "" : wrap("切入面",tableSelect(action,"face",item.face,PLANE_FACES,index,disabled))
    +wrap(item.face==="round"?"刀具周向角度":"面内偏移",tableNumber(action,"offset",item.offset,index,"",disabled),item.face==="round"?"°":"mm")
    +wrap("孔形旋转",tableNumber(action,"rotation",item.rotation,index,"",disabled),"°")
    +wrap("切深",tableSelect(action,"depthMode",item.depthMode,[{value:"single",label:"当前面"},{value:"reverse",label:"反向面"},{value:"both",label:"当前面 + 对面"},{value:"through",label:"贯穿两侧"}],index,disabled))
    +(item.layoutDatum!=="base"?wrap("成品端面基准",tableSelect(action,"endDatum",item.endDatum??"long",datums,index,disabled)):"");
  const partPlacement = descriptor?.requiresSection
    ? wrap("轴夹角",tableNumber(action,"angle",item.angle??90,index,"",disabled),"°")
      +wrap("方位角",tableNumber(action,"azimuth",item.azimuth??0,index,"",disabled),"°")
      +wrap("绕轴旋转",tableNumber(action,"roll",item.roll??0,index,"",disabled),"°")
      +wrap("横向偏移",tableNumber(action,"offsetY",item.offsetY??0,index,"",disabled),"mm")
      +wrap("高度偏移",tableNumber(action,"offsetZ",item.offsetZ??0,index,"",disabled),"mm")
      +wrap("拉伸方向",tableSelect(action,"direction",item.direction??"through",[
        {value:"through",label:"贯穿主管"},{value:"symmetric",label:"对称"},{value:"positive",label:"正向"},{value:"negative",label:"反向"}],index,disabled))
      +wrap("拉伸长度",tableNumber(action,"length",item.length??120,index,"",disabled),"mm")
    : descriptor?.target === "part" ? wrap("绕主管旋转",tableNumber(action,"rotation",item.rotation??0,index,"",disabled),"°") : "";
  return '<div class="punch-pose-fields">'+location+partPlacement+side+'</div>';
}
function punchPoseSummary(item) {
  const position=(refs.find(ref=>ref.value===item.reference)?.label??"距起点")+" "+(item.station??0)+" mm";
  if(item.toolTarget!=="part")return position+" · "+(PLANE_FACES.find(face=>face.value===item.face)?.label??item.face)
    +" · "+(item.face==="round"?"周向 "+(item.offset??0)+"°":"偏移 "+(item.offset??0)+" mm")+" · 旋转 "+(item.rotation??0)+"°";
  return position+(item.toolRef?.id==="branch-profile"?" · 轴夹角 "+(item.angle??90)+"° · 方位 "+(item.azimuth??0)+"°":" · 刀具坐标定位");
}
function renderPunchParameterDialog(action,s,view,options,btn) {
  const editor=s.parameterEditor;if(!editor)return "";
  const index=editor.index,end=editor.end;
  const item=end?s.ends?.[end]:index==="draft"?s.draft:s.features?.[Number(index)];
  if(!item)return "";
  const descriptor=punchToolDescriptor(s,item),disabled=view.pending,mode=end?"end":editor.mode??"shape";
  const kind=punchRecordKind(item);
  const selector=kind==="tool"?(end?tableEndToolSelect(action,s,item,end,disabled):tableToolSelect(action,s,item,index,disabled))
    :tableProfileSelect(action,item,index,options.branchProfiles??[],disabled,end);
  const shapeDescriptor=end?descriptor:punchPoseDescriptor(descriptor,item,false);
  const fields=end&&item.type==="keep"?"":kind==="tool"?tableParameterFields(action,shapeDescriptor,item,index,disabled,end)
    :tableProfileParameters(action,item,index,options.branchProfiles??[],disabled,end)+tableParameterFields(action,shapeDescriptor,item,index,disabled,end);
  const title=end?(end==="start"?"左端面参数":"右端面参数"):({shape:"刀具形状",pose:"位置 / 姿态",arrays:"阵列"}[mode]??"刀具参数")+" · "+punchSourceName(item,descriptor);
  const endHint=end?(item.toolRef?.id==="end-key-joint"?"矩形插舌 / 插槽：两件使用相同名义宽度、深度，配合间隙在母口设置。"
    :item.toolRef?.id==="end-step-z"?"单台阶 Z 搭接口：一侧保留端部，另一侧后退，方向可翻转。"
    :item.toolRef?.id==="end-profile"?"截面拉伸为端部刀具，固定填实外轮廓，忽略内孔。"
      +(item.toolParameters?.cutMode==="convex"?"凸口按名义包络向内定位并反向切除，可调向内偏移。轴夹角为 0° 或 180° 时不能形成凸口，请调整角度；斜姿态仍受母材与修剪边界限制。":"凹口按截面向内切除。轴夹角相对朝管内的轴线；左、右端各自定位，查看三维确认切除侧。")
    :"端部刀具仅在场景中显示切除位置，最终确认时才执行切割。"):"";
  const endDatums=longDatumEndTools.has(item.toolRef?.id??item.type)?datums.slice(0,1):datums;
  const endToolId=item.toolRef?.id??item.type;
  const endShapePlacement=endToolId==="end-profile"
    ?'<label><span>轴夹角（°）</span>'+tableNumber(action,"angle",item.angle??90,end,"",disabled,end)+'</label>'
      +'<label><span>方位角（°）</span>'+tableNumber(action,"azimuth",item.azimuth??0,end,"",disabled,end)+'</label>'
      +'<label><span>绕轴旋转（°）</span>'+tableNumber(action,"roll",item.roll??0,end,"",disabled,end)+'</label>'
      +'<label><span>轴向偏移（mm）</span>'+tableNumber(action,"axialOffset",item.axialOffset??0,end,"",disabled,end)+'</label>'
      +'<label><span>横向偏移（mm）</span>'+tableNumber(action,"offsetY",item.offsetY??0,end,"",disabled,end)+'</label>'
      +'<label><span>高度偏移（mm）</span>'+tableNumber(action,"offsetZ",item.offsetZ??0,end,"",disabled,end)+'</label>'
    :endToolId==="end-convex"||endToolId==="end-cope"
      ?'<label><span>轴夹角（°）</span>'+tableNumber(action,"angle",item.angle??90,end,"",disabled,end)+'</label>'
        +'<label><span>轴向偏移（mm）</span>'+tableNumber(action,"offset",item.offset??0,end,"",disabled,end)+'</label>'
      :endToolId==="end-key-joint"?'<label><span>轴向偏移（mm）</span>'+tableNumber(action,"offset",item.offset??0,end,"",disabled,end)+'</label>':"";
  const endPlacement=end&&item.type!=="keep"?'<fieldset class="tube-designer-punch-end-placement"><legend>端部定位</legend><div>'
    +'<label><span>定位基准</span>'+tableSelect(action,"datum",item.datum??"long",endDatums,end,disabled,end)+'</label>'
    +'<label><span>端部修剪量（mm）</span>'+tableNumber(action,"trim",item.trim??0,end,"",disabled,end)+'</label>'
     +'<label><span>绕主管轴旋转（°）</span>'+tableNumber(action,"rotation",item.rotation??0,end,"",disabled,end)+'</label>'+endShapePlacement
    +'</div></fieldset>':"";
  const arrayState=mode==="arrays"?punchArrayViewState(item,s.baseLength):null;
  const shapeContent=(mode==="shape"?'<label class="tube-designer-punch-parameter-source"><span>加工来源</span>'+tableActionSelect(action,"record-kind-change",kind,[{value:"branch",label:"支管相贯"},{value:"tool",label:"刀具冲孔"},{value:"dxf",label:"本地 DXF"}],index,disabled)+'</label>':"")
    +(selector?'<label class="tube-designer-punch-parameter-source"><span>'+(kind==="tool"?"选择刀具":"选择截面")+'</span>'+selector+'</label>':"")
    +endPlacement+'<div class="tube-designer-punch-sheet-parameters">'+fields+'</div>';
  const content=mode==="arrays"?renderPunchArrayGroupsControls(action,arrayState.feature,index,disabled,s.baseLength,arrayState.groups,arrayState.result,punchArraySkipText(arrayState.feature))
    :mode==="pose"?renderPunchPoseFields(action,item,index,disabled,descriptor):shapeContent;
  const note=endHint||(mode==="arrays"?"阵列不修改单个刀具的形状与姿态；这里只显示摆放位置，最终确认时才执行切割。"
    :mode==="pose"?"这里定位一个刀具。多个孔的方向、数量和间距，在“编辑阵列”中独立设置。":"这里设置单个刀具的形状。位置、姿态与多组阵列使用记录表中的独立入口。");
  const position=editor.windowPosition;
  const windowStyle=Number.isFinite(position?.left)&&Number.isFinite(position?.top)
    ?' style="position:fixed;left:'+position.left+'px;top:'+position.top+'px;margin:0"':"";
  return '<div class="tube-designer-punch-parameter-backdrop"><section class="tube-designer-punch-parameter-dialog" role="dialog" aria-modal="false" aria-labelledby="punch-parameter-title" aria-describedby="punch-parameter-window-hint" data-punch-parameter-dialog data-punch-editor-mode="'+mode+'"'+windowStyle+'>'
    +'<header data-punch-parameter-drag><div><strong id="punch-parameter-title">'+escapeText(title)+'</strong><small id="punch-parameter-window-hint">修改即时预览；可拖动标题移开窗口，关闭返回场景</small></div>'+btn("parameters-cancel","×",'aria-label="关闭参数编辑"')+'</header>'
    +'<div class="tube-designer-punch-parameter-content">'+content
    +((editor.error||s.error)?'<div class="tube-designer-punch-error" role="alert">'+escapeText(editor.error||s.error)+'</div>':"")
    +'<small class="tube-designer-punch-parameter-note">'+escapeText(note)+'</small>'
    +previewModeButtons(s,btn)+'</div>'
    +'</section></div>';
}

function renderPunchTableRow(action, s, view, item, index, btn, options) {
  const descriptor=punchToolDescriptor(s,item);
  const locked=isPunchToolReadOnly(s,item), disabled=view.pending||locked;
  const selected=new Set(Array.isArray(s.selectedFeatureIds)?s.selectedFeatureIds:[]).has(item.id);
  const rowClass=[item.enabled===false?'is-suppressed':'',s.selectedFeatureId===item.id?'is-selected':''].filter(Boolean).join(" ");
  const sourceSelect='<div class="tube-designer-punch-source-cell"><span '+(!locked?'tabindex="0" role="button" data-punch-edit-name aria-label="编辑 '+escapeText(punchSourceName(item,descriptor))+' 形状" title="双击名称或点击编辑形状"':'')+'>'+escapeText(punchSourceName(item,descriptor))+'</span><small>'+escapeText(punchParameterSummary(item,punchPoseDescriptor(descriptor,item,false),options.branchProfiles))+'</small></div>';
  const edit=(mode,title)=>locked?'':btn("parameters-open",title,'data-tube-designer-punch-index="'+index+'" data-tube-designer-punch-editor-mode="'+mode+'"');
  const arrayState=punchArrayViewState(item,s.baseLength);
  return '<tr class="'+rowClass+'" data-tube-designer-punch-row="'+escapeText(index)+'">'
    +'<th scope="row" data-cam-action="'+action("summary-select")+'" data-tube-designer-punch-index="'+escapeText(index)+'"><label class="tube-designer-punch-row-select" title="选择此行"><input type="checkbox" data-cam-change-action="'+action("selection-change")+'" data-tube-designer-punch-index="'+escapeText(index)+'" '+(selected?'checked ':'')+(view.pending?'disabled':'')+'/><span>'+(index+1)+'</span></label></th>'
    +'<td>'+tableCheck(action,"enabled",item.enabled!==false,index,"",disabled)+'</td>'
    +'<td><div class="punch-record-cell">'+sourceSelect+(locked?'<small>固化参数 · 只读</small>':edit("shape","编辑形状"))+'</div></td>'
    +'<td><div class="punch-record-cell"><span class="punch-pose-summary">'+escapeText(punchPoseSummary(item))+'</span>'+edit("pose","编辑位置 / 姿态")+'</div></td>'
    +'<td><div class="punch-record-cell">'+renderPunchArrayGroupsSummary(arrayState.groups,arrayState.result)+edit("arrays","编辑阵列")+'</div></td>'
    +'</tr>';
}

function renderEndRow(action,s,view,end,btn,options) {
  const item=s.ends[end]??{type:"keep"}, descriptor=punchToolDescriptor(s,item), disabled=view.pending||isPunchToolReadOnly(s,item);
  const endName=end==="start"?"左端面":"右端面";
  const kind=punchRecordKind(item);
  const kindSelect=tableActionSelect(action,"record-kind-change",kind,[{value:"tool",label:"端部刀具"},{value:"branch",label:"支管切端"},{value:"dxf",label:"DXF 切端"}],end,disabled,end);
  const source=kind==="tool"?tableEndToolSelect(action,s,item,end,disabled)
    :'<div class="tube-designer-punch-source-cell"><span '+(!disabled?'tabindex="0" role="button" data-punch-edit-name title="双击名称编辑截面与刀具姿态"':'')+'>'+escapeText(punchSourceName(item,descriptor))+'</span><small>'+escapeText(punchParameterSummary(item,descriptor,options.branchProfiles))+'</small></div>';
  const controls=item.type==="keep"?'<span class="tube-designer-punch-sheet-readonly">不修改</span>'
    :disabled?'<span class="tube-designer-punch-sheet-readonly">固化端部 · 只读</span>':btn("parameters-open","编辑端面参数",'data-tube-designer-punch-index="'+end+'" data-tube-designer-punch-end="'+end+'"');
  const summary=item.type==="keep"?"保留原端面":(datums.find(d=>d.value===(item.datum??"long"))?.label??item.datum)
    +((item.toolRef?.id??item.type)==="end-profile"?" · "+(item.toolParameters?.cutMode==="convex"?"凸口":"凹口"):"")
    +" · 修剪 "+(item.trim??0)+" mm · 旋转 "+(item.rotation??0)+"°";
  return '<tr data-tube-designer-punch-end-row="'+end+'"><th scope="row">'+endName+'</th><td>'+kindSelect+'</td><td>'+source+'</td>'
    +'<td class="tube-designer-punch-end-summary">'+escapeText(summary)+'</td><td><nav class="tube-designer-punch-end-actions">'+controls+(item.type==="keep"?'':btn("remove-end","恢复",'data-tube-designer-punch-index="'+end+'"'))+'</nav></td></tr>';
}

function renderEndCard(action,s,view,end,btn,options) {
  const item=s.ends[end]??{type:"keep"}, descriptor=punchToolDescriptor(s,item), disabled=view.pending||isPunchToolReadOnly(s,item);
  const endName=end==="start"?"左端面":"右端面";
  const kind=punchRecordKind(item);
  const kindSelect=tableActionSelect(action,"record-kind-change",kind,[{value:"tool",label:"端部刀具"},{value:"branch",label:"支管切端"},{value:"dxf",label:"DXF 切端"}],end,disabled,end);
  const source=kind==="tool"?tableEndToolSelect(action,s,item,end,disabled)
    :'<div class="tube-designer-punch-source-cell"><span '+(!disabled?'tabindex="0" role="button" data-punch-edit-name title="双击名称编辑截面与刀具姿态"':'')+'>'+escapeText(punchSourceName(item,descriptor))+'</span></div>';
  const controls=item.type==="keep"?'<span class="tube-designer-punch-sheet-readonly">不修改</span>'
    :disabled?'<span class="tube-designer-punch-sheet-readonly">固化端部 · 只读</span>':btn("parameters-open","编辑",'data-tube-designer-punch-index="'+end+'" data-tube-designer-punch-end="'+end+'"');
  const summary=item.type==="keep"?"保留原端面":(datums.find(d=>d.value===(item.datum??"long"))?.label??item.datum)
    +((item.toolRef?.id??item.type)==="end-profile"?" · "+(item.toolParameters?.cutMode==="convex"?"凸口":"凹口"):"")
    +" · 修剪 "+(item.trim??0)+" mm · 旋转 "+(item.rotation??0)+"°";
  return '<article class="tube-designer-punch-end-card" data-tube-designer-punch-end-row="'+end+'"><header><strong>'+endName+'</strong><span class="tube-designer-punch-end-summary">'+escapeText(summary)+'</span></header>'
    +'<div class="tube-designer-punch-end-card-fields"><label><span>加工来源</span>'+kindSelect+'</label><label><span>切形 / 截面</span>'+source+'</label></div>'
    +'<nav class="tube-designer-punch-end-card-actions">'+controls+'</nav></article>';
}

function renderPunchEndsPanel(action,s,view,options,btn) {
  if(options.showEnds===false)return "";
  return '<section class="tube-designer-punch-ends" data-punch-end-panel aria-label="端面加工"><header><strong>端面加工</strong><span>两端独立设置，不参与孔数与阵列</span></header><div class="tube-designer-punch-end-cards">'
    +renderEndCard(action,s,view,"start",btn,options)+renderEndCard(action,s,view,"end",btn,options)+'</div></section>';
}

function renderPunchSpreadsheet(action, s, view, options, btn) {
  const rows=s.features.map((item,index)=>renderPunchTableRow(action,s,view,item,index,btn,options)).join("");
  const ends=options.showEnds===false?"":'<section class="tube-designer-punch-ends" aria-label="端面加工"><table><caption>端面加工 <small>两端独立设置，不参与孔数与阵列</small></caption>'
    +'<thead><tr><th>端面</th><th>加工来源</th><th>切形 / 截面</th><th>当前设置</th><th>操作</th></tr></thead><tbody>'
    +renderEndRow(action,s,view,"start",btn,options)+renderEndRow(action,s,view,"end",btn,options)+'</tbody></table></section>';
  const selectedCount=(s.selectedFeatureIds??[]).filter(id=>s.features.some(item=>item.id===id)).length;
  const addButton=btn("add","＋ 添加行",(s.catalogueStatus==="ready"?'':'disabled ')+'data-tube-designer-punch-new');
  const selectedIndex=s.features.findIndex(item=>item.id===s.selectedFeatureId);
  const selectedItem=selectedIndex>=0?s.features[selectedIndex]:null;
  const copyButton=btn("copy-selected","复制",selectedItem&&!isPunchToolReadOnly(s,selectedItem)?'':'disabled aria-label="复制当前高亮行"');
  const deleteButton=btn("remove-selected","删除所选",(selectedCount?'':'disabled')+' aria-label="删除所选行"');
  return '<div class="tube-designer-punch-records has-ends"'+(s.parameterEditor?' inert':'')+'>'+ends+'<section class="tube-designer-punch-sheet"><header><div><strong>'+escapeText(options.featureListTitle??"冲孔清单")+'</strong><span>刀具、位置姿态、多组阵列分别设置</span></div><nav class="tube-designer-punch-sheet-toolbar"><small>'+s.features.length+' 条冲孔记录</small>'+addButton+copyButton+deleteButton+'</nav></header>'
    +'<div class="tube-designer-punch-sheet-scroll"><table><thead><tr><th>#</th><th>启用</th><th>截面 / 刀具</th><th>位置 / 姿态</th><th>阵列 <small>长度 · 直线 · 圆周可组合</small></th></tr></thead><tbody>'
    +(rows||'<tr><td class="tube-designer-punch-sheet-empty" colspan="5">暂无冲孔记录，点击“＋ 添加行”开始。</td></tr>')+'</tbody></table></div></section></div>';
}

export function renderPunchWizardDialog(part, view, options = {}) {
  const s = view?.tubeDesignerPunchWizard;
  if (!s || !part || String(s.partId)!==String(part.entityId)) return "";
  const action = name => (options.actionPrefix ?? "tube-designer-punch-")+name;
  const disabled = view.pending ? "disabled" : "";
  const btn = (name,text,extra="") => '<button class="tube-designer-secondary" data-cam-action="'+action(name)+'" '+(disabled?"disabled":"")+' '+extra+'>'+escapeText(text)+'</button>';
  const total = s.features.reduce((n,f)=>n+punchArrayGroupInstanceCount(f,s.baseLength??part.length),0);
  const draft=s.draft, tool=punchToolDescriptor(s,draft);
  const missing=missingPunchTools(s);
  const degraded=unavailableItems(s).filter(hasFrozenPunchTool);
  const missingHtml=(missing.length?'<div class="tube-designer-punch-error" role="alert">本机缺少匹配刀具且无固化表达：'+escapeText(missing.map(t=>t.label+" @ "+t.version).join("、"))+'。已有实体仍可查看、排样和导出；删除缺失节点或安装原版刀具后可重算。</div>':"")
    +(degraded.length?'<div class="tube-designer-punch-fixed-note">'+degraded.length+' 个刀具已退化为定式：使用图纸保存的固化几何继续显示和重放。节点只读，仅可删除；原程式号与参数保留。</div>':"");
  const select = (field,title,value,items,end="") => fieldControl(action,field,title,value,{options:items},end);
  const input = (field,title,value,unit="",end="") => fieldControl(action,field,title,value,{unit},end);
  const title = options.title ?? "冲孔向导";
  const previewTitle = options.previewTitle ?? "实体预览";
  const countsCurrent=s.preview?.revision===s.revision&&!s.previewPending&&!s.previewRenderPending&&!s.error&&s.preview?.toolsOnly===true&&Number.isSafeInteger(s.preview.placedToolCount);
  const previewCountLabel = countsCurrent?`${s.preview.placedToolCount} 个刀具实例${s.preview.includesDraft?"（含当前新增）":""}`
    :degraded.length?"含固化记录 · 数量待复核":options.previewCountLabel ?? `${total} 个已添加刀具实例`;
  const previewStatus = (options.previewStatus
    ?? "可从任意方向观察。预览和确认只处理清单及端部设置；正在填写的刀具请先添加或保存。")
    +(s.previewMode!=="result"&&s.preview?.toolDisplayClipped?" 刀具体仅显示主管附近区域，实际切割使用完整刀具。":"");
  const featureEditorTitle = options.featureEditorTitle
    ?? (s.editingId ? "编辑刀具实例" : "添加刀具实例");
  const featureAddLabel = options.featureAddLabel
    ?? (s.editingId ? "保存特征修改" : "添加到清单");
  const featureListTitle = options.featureListTitle ?? "刀具实例清单";
  const featureEmptyText = options.featureEmptyText ?? "尚未添加侧面刀具；可以仅处理端部。";
  const rows = s.features.map((f,i)=>{
    const d=punchToolDescriptor(s,f), locked=isPunchToolReadOnly(s,f);
    return '<article '+(f.enabled===false?'class="is-suppressed"':'')+'><div><strong>'+escapeText((i+1)+". "+(label(d)||f.toolLabel||f.type)+(locked?(hasFrozenPunchTool(f)?"（退化定式 · 只读）":"（缺失 · 只读）"):""))+'</strong><span>'+escapeText((refs.find(r=>r.value===f.reference)?.label??"距起点")+" "+f.station+" mm · "+(f.face==="round"?"周向 "+f.offset+"°":PLANE_FACES.find(r=>r.value===f.face)?.label??f.face))+'</span><small>'+escapeText(((d?.kind??f.toolKind)==="fixed"?"定式刀具":"程式刀具")+" · "+f.arrayCount+" × "+f.rowCount+(f.enabled===false?" · 已停用":""))+'</small>'+(locked?frozenProvenance(f):"")+'</div><nav>'+(locked?[["remove","删除"]]:[["edit","编辑"],["copy","复制"],["toggle",f.enabled===false?"启用":"停用"],["remove","删除"]]).map(([a,t])=>btn(a,t,'data-tube-designer-punch-index="'+i+'"')).join("")+'</nav></article>';
  }).join("");
  const endCards = options.showEnds === false ? "" : ["start","end"].map(end=>{
    const e=s.ends[end]??{type:"keep"}, d=punchToolDescriptor(s,e), locked=isPunchToolReadOnly(s,e);
    return '<section class="tube-designer-punch-card"><h3>'+(end==="start"?"起点端部":"终点端部")+'</h3><fieldset class="tube-designer-punch-controls" '+(view.pending||locked?'disabled':'')+'><div class="tube-designer-punch-field-grid">'+toolSelect(action,s,e,"end",end)
      +(e.type!=="keep"?input("trim","向内修剪量",e.trim??0,"mm",end)+input("rotation","绕管轴旋转",e.rotation??0,"°",end)
        +select("datum","尺寸基准",e.datum??"long",datums,end)+parameterFields(action,d,e,end):"")+'</div></fieldset>'+(e.type!=="keep"?btn("remove-end","删除端部刀具",'data-tube-designer-punch-index="'+end+'"'):"")+'</section>';
  }).join("");
  const previewStateLabel = s.previewRenderError
    ? "三维显示失败（不影响最终应用），请更新刀具体"
    : s.previewRenderPending
    ? "正在加载并显示三维结果"
    : s.previewPending
    ? "正在更新主管与刀具位置"
    : s.previewComputeError?.revision===s.revision
    ? "当前参数预览未完成，请检查下方提示"
    : s.preview?.revision===s.revision
      ? (s.preview.toolsOnly ? "当前刀具位置已更新 · 确认时才计算切除" : s.preview.includesDraft ? "当前孔刀参数已进入预览" : "当前显示参数已更新")
      : s.preview?.isOriginal
        ? (s.revision===0?"已保存的零件实体（无需本机刀具）":"参数已改变，旧预览已隐藏")
        : s.preview
          ? "参数已改变，旧预览已隐藏"
          : options.previewHint??"编辑中显示主管与刀具体，确认时才计算切除";
  const wizardWindowPosition=s.wizardWindowPosition;
  const wizardWindowStyle=Number.isFinite(Number(wizardWindowPosition?.left))&&Number.isFinite(Number(wizardWindowPosition?.top))
    ?' style="position:fixed;left:'+Math.round(Number(wizardWindowPosition.left))+'px;top:'+Math.round(Number(wizardWindowPosition.top))+'px;margin:0"':"";
  if(options.tableMode) {
    const transactionLock=s.parameterEditor?' inert':'';
    const rawSetup=options.introHtml??options.mainSetupHtml??"";
    const setup=s.parameterEditor?rawSetup.replace(/(<[a-z][\w:-]*)(?=[\s>])/i,'$1 inert'):rawSetup;
    const spreadsheet=renderPunchSpreadsheet(action,s,view,{...options,showEnds:false},btn);
    const endsPanel=renderPunchEndsPanel(action,s,view,options,btn);
    const errors=missingHtml
      +(s.catalogueStatus==="loading"?'<div class="tube-designer-punch-sheet-message">正在加载孔型…</div>':"")
      +(s.catalogueErrors?.length?'<div class="tube-designer-punch-error">'+escapeText(s.catalogueErrors.join("；"))+'</div>':"")
      +(s.error?'<div class="tube-designer-punch-error" role="alert">'+escapeText(s.error)+'</div>':"");
    const leftSidebar='<aside class="tube-designer-punch-sidebar">'+setup+endsPanel+'</aside>';
    const rightbarWidth=Number.isFinite(Number(s.rightbarWidth))&&Number(s.rightbarWidth)>0
      ?' style="--tube-designer-punch-rightbar-width:'+Math.round(Number(s.rightbarWidth))+'px"':"";
    const rightSidebar='<aside class="tube-designer-punch-rightbar"><div class="tube-designer-punch-rightbar-splitter" data-tube-designer-punch-rightbar-splitter role="separator" aria-orientation="vertical" aria-label="调整冲孔清单宽度" tabindex="0"></div>'+spreadsheet+'<nav class="tube-designer-punch-sidebar-actions"'+transactionLock+'>'+btn("cancel","取消")+btn("apply",view.pending?"正在计算…":options.applyLabel??"应用到零件",'data-tube-designer-part-id="'+escapeText(options.partId??part.entityId)+'"')+'</nav></aside>';
    return '<div class="tube-designer-modal-backdrop tube-designer-punch-backdrop" role="presentation"><section class="tube-designer-punch-dialog '+escapeText(options.dialogClass??"")+' tube-designer-punch-sheet-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-designer-punch-title"'+wizardWindowStyle+'>'
      +'<header class="tube-designer-dialog-header" data-tube-designer-punch-window-drag'+transactionLock+'><div><strong id="tube-designer-punch-title">'+escapeText(title)+'</strong></div>'+btn("cancel","×",'aria-label="关闭向导"')+'</header>'
      +'<div class="tube-designer-punch-sheet-workspace"'+rightbarWidth+'>'+leftSidebar
      +'<section class="tube-designer-punch-sheet-scene"><header><div><strong>'+escapeText(previewTitle)+'</strong><span>'+escapeText(previewCountLabel)+'</span></div><nav'+transactionLock+'>'+previewModeButtons(s,btn)+btn("undo","撤销",s.history.length?"":"disabled")+btn("redo","重做",s.future.length?"":"disabled")+'</nav></header>'
      +'<div class="tube-designer-punch-preview-host" data-tube-designer-punch-viewport>'+renderViewCube()+'</div>'
      +'<footer>'+(s.previewMode==="result"?'<span>切割后的零件</span>':options.previewLegendHtml??"")+'<span><strong>'+escapeText(previewStateLabel)+'</strong> · <span data-punch-preview-status>'+escapeText(previewStatus)+'</span></span></footer></section>'
      +'<div class="tube-designer-punch-sheet-errors">'+errors+'</div>'+rightSidebar+'</div>'
      +'</section></div>'+renderPunchParameterDialog(action,s,view,options,btn);
  }
  const previewPane = '<main class="tube-designer-punch-preview-pane">'+(options.mainSetupHtml??"")
    +'<div class="tube-designer-pane-title"><strong>'+escapeText(previewTitle)+'</strong><span>'+escapeText(previewCountLabel)+'</span></div>'
    +'<div class="tube-designer-punch-preview-host" data-tube-designer-punch-viewport></div>'+(options.previewLegendHtml??"")
    +'<div class="tube-designer-punch-preview-note"><strong>'+escapeText(previewStateLabel)+'</strong><span data-punch-preview-status>'+escapeText(previewStatus)+'</span></div>'
    +'<nav class="tube-designer-punch-preview-actions">'+btn("preview",options.previewActionLabel??"更新实体预览")+btn("undo","撤销",s.history.length?"":"disabled")+btn("redo","重做",s.future.length?"":"disabled")+'</nav>'+endCards+'</main>';
  const editorPane = '<aside class="tube-designer-punch-editor">'+(options.introInEditor?options.introHtml??"":"")+missingHtml
    +'<fieldset class="tube-designer-punch-controls" '+(view.pending||isPunchToolReadOnly(s,draft)?'disabled':'')+'><section class="tube-designer-punch-card"><h3>'+escapeText(featureEditorTitle)+'</h3><div class="tube-designer-punch-field-grid">'
    +toolSelect(action,s,draft,options.partTools?["side","part"]:"side")+(options.branchSetupHtml??"")+parameterFields(action,tool,draft)
    +(draft.toolTarget==="part"?select("face","阵列方式",draft.face,[{value:"top",label:"沿 Y 横向"},{value:"round",label:"绕主管 X 轴"}]):select("face","切入方向",draft.face,PLANE_FACES))
    +select("reference","沿长度定位",draft.reference,refs)+input("station",draft.toolTarget==="part"?"距原始主管基准":"定位距离",draft.station,"mm")
    +(draft.toolTarget==="part"?"":select("endDatum","端面尺寸基准",draft.endDatum,datums)+input("offset",draft.face==="round"?"周向角度":"横向偏移",draft.offset,draft.face==="round"?"°":"mm")+input("rotation","刀具面内旋转",draft.rotation,"°"))
    +input("arrayCount","沿长度数量",draft.arrayCount,"个")+input("arrayPitch","沿长度间距",draft.arrayPitch,"mm")
    +input("rowCount",draft.face==="round"?"周向数量":"横向数量",draft.rowCount,"个")+input("rowPitch",draft.face==="round"?"周向间隔":"横向间距",draft.rowPitch,draft.face==="round"?"°":"mm")
    +(draft.toolTarget==="part"?"":[["opposite","同时加工对面"],["through","贯穿两侧"]].map(([k,t])=>fieldControl(action,k,t,draft[k],{valueType:"boolean"})).join(""))
    +'</div>'+btn("add",featureAddLabel,s.catalogueStatus==="ready"?"":"disabled")+'</section></fieldset>'
    +'<section class="tube-designer-punch-card tube-designer-punch-feature-card"><h3><span>'+escapeText(featureListTitle)+'</span><small>'+s.features.length+' 条</small></h3><div class="tube-designer-punch-feature-list">'+(rows||'<div class="tube-designer-punch-empty">'+escapeText(featureEmptyText)+'</div>')+'</div></section>'
    +(s.catalogueStatus==="loading"?'<p>正在加载刀具模板…</p>':"")
    +(s.catalogueErrors?.length?'<div class="tube-designer-punch-error">'+escapeText(s.catalogueErrors.join("；"))+'</div>':"")
    +(s.error?'<div class="tube-designer-punch-error" role="alert">'+escapeText(s.error)+'</div>':"")+'</aside>';
  const panes = options.editorFirst ? editorPane+previewPane : previewPane+editorPane;
  const outerIntro = options.introInEditor ? "" : options.introHtml??"";
  return '<div class="tube-designer-modal-backdrop tube-designer-punch-backdrop" role="presentation"><section class="tube-designer-punch-dialog '+escapeText(options.dialogClass??"")+'" role="dialog" aria-modal="true" aria-labelledby="tube-designer-punch-title"'+wizardWindowStyle+'>'
    +'<header class="tube-designer-dialog-header" data-tube-designer-punch-window-drag><div><strong id="tube-designer-punch-title">'+escapeText(title)+'</strong><span>'+escapeText(options.subtitle??part.name??"独立下料零件")+'</span></div>'+btn("cancel","×",'aria-label="关闭向导"')+'</header>'
    +outerIntro+'<div class="tube-designer-punch-body '+escapeText(options.bodyClass??"")+'">'+panes+'</div>'
    +'<footer class="tube-designer-punch-footer"><span>'+escapeText(options.footerNote??"程式刀具由脚本生成，定式刀具保留固定几何。修改仅影响当前下料零件。")+'</span>'+btn("cancel","取消")+btn("apply",view.pending?"正在计算…":options.applyLabel??"应用到零件",'data-tube-designer-part-id="'+escapeText(options.partId??part.entityId)+'"')+'</footer></section></div>';
}
function toolSelect(action,s,item,target,end="") {
  const available=punchToolDescriptor(s,item);
  const unresolved=!!item.toolRef&&!available;
  const id=unresolved?"__unavailable__":item.toolRef?.id??(end?"keep":"");
  const tools=s.tools.filter(t=>(Array.isArray(target)?target.includes(t.target):t.target===target)
    && t.hidden!==true);
  const opts=[{value: end?"keep":"",label:end?"保留原端面":"请选择刀具"},...(unresolved?[{value:"__unavailable__",label:"缺少："+(item.toolLabel??item.toolRef.id)+" @ "+(item.toolRef.version??"旧版")}]:[]),...tools.map(t=>({value:t.id,label:(t.kind==="fixed"?"定式 · ":"程式 · ")+label(t)}))];
  return fieldControl(action,"tool","刀具模板",id,{options:opts},end);
}
export function parameterFields(action,descriptor,item,end="") {
  if(!descriptor)return item.toolRef?'<div class="tube-designer-punch-fixed-note">'+(hasFrozenPunchTool(item)?"退化定式刀具":"缺失刀具")+'：节点只读，仅可删除。'+frozenProvenance(item)+'</div>':"";
  if(descriptor.kind==="fixed")return '<div class="tube-designer-punch-fixed-note">定式刀具：形状尺寸固定，只调整定位与阵列。</div>';
  return (descriptor.parameters??[]).filter(d=>{
    const c=d.visibleWhen;if(!c)return true;
    const value=item.toolParameters?.[c.key??c.parameter];return c.op==="ne"?value!==c.value:c.op==="eq"?value===c.value:true;
  }).map(d=>fieldControl(action,"parameter",label(d),item.toolParameters?.[d.key]??d.defaultValue,d,end,d.key)).join("");
}
function frozenProvenance(item) {
  return '<small>'+escapeText('原刀具号：'+item.toolRef.id+' @ '+(item.toolRef.version??'')+'；原参数：'+JSON.stringify(item.toolParameters??{}))+'</small>';
}
export function fieldControl(action,field,title,value,definition={},end="",parameter="") {
  const attrs=' data-cam-change-action="'+action("field-change")+'" data-tube-designer-punch-field="'+field+'"'
    +(end?' data-tube-designer-punch-end="'+end+'"':"")+(parameter?' data-tube-designer-punch-parameter="'+escapeText(parameter)+'"':"");
  let control;
  if(definition.options) control='<select'+attrs+'>'+definition.options.map(o=>'<option value="'+escapeText(o.value)+'" '+(String(o.value)===String(value)?"selected":"")+'>'+escapeText(o.label??o.displayName??o.value)+'</option>').join("")+'</select>';
  else if(definition.valueType==="boolean")control='<input type="checkbox"'+attrs+' '+(value?"checked":"")+'/>';
  else control='<input type="'+(definition.valueType==="string"?"text":"number")+'" step="'+(definition.valueType==="integer"?1:definition.step??"any")+'" value="'+escapeText(typeof value==="number"&&!Number.isFinite(value)?"":value??"")+'"'+attrs+'/>';
  return '<label><span>'+escapeText(title+(definition.unit?'（'+definition.unit+'）':""))+'</span>'+control+'</label>';
}
function escapeText(value) { return String(value??"").replaceAll("&","&amp;").replaceAll("<","&lt;").replaceAll(">","&gt;").replaceAll('"',"&quot;").replaceAll("'","&#39;"); }
