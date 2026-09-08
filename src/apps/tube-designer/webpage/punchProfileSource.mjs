import { libraryProfiles, profileRef, profileSelectionKey, profileScope } from "./profileLibrary.mjs";
import { checkpointPunchWizard, selectPunchTool, isPunchToolReadOnly } from "./punchWizard.mjs";
import { beginPunchOperation, finishPunchOperation } from "./punchEditor.mjs";

const clone = value => structuredClone(value);
const sourceRequests=new WeakMap();
const profileRequests = new WeakMap();
const localized = value => typeof value === "object"
  ? String(value?.["zh-CN"] ?? value?.["en-US"] ?? Object.values(value ?? {})[0] ?? "")
  : String(value ?? "");

async function runSourceOperation(context,view,ops,title,work) {
  const options=beginPunchOperation(context,view,title),operation=view.tubeDesignerOperation;
  operation.message=title;
  operation.phaseLabel="处理中";
  try {
    ops?.renderProject?.(context,view);
    // Paint the disabled controls and progress before starting native work.
    if(typeof requestAnimationFrame==="function")await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    return await work(options);
  } finally {
    if(view.tubeDesignerOperation===operation)finishPunchOperation(view,operation);
    ops?.renderProject?.(context,view);
  }
}

function snapshot(profile) {
  return profile?.previewProfile ?? profile?.profile ?? (profile?.contours ? profile : null);
}

function profileName(profile) {
  return localized(profile?.name ?? profile?.descriptor?.displayName ?? profile?.previewProfile?.name) || "未命名管型";
}

function profileSpecification(profile) {
  const value = snapshot(profile);
  return String(value?.specification ?? profile?.specification ?? "");
}

function visibleParameter(definition, values) {
  const test = condition => !condition ? true
    : condition.conditions ? (condition.op === "any" ? condition.conditions.some(test) : condition.conditions.every(test))
    : condition.all ? condition.all.every(test)
    : condition.any ? condition.any.some(test)
    : condition.op === "eq" ? values[condition.parameter ?? condition.key] === condition.value
    : condition.op === "ne" ? values[condition.parameter ?? condition.key] !== condition.value
    : true;
  return test(definition?.visibleWhen);
}

function profileParameters(view, profile) {
  // A library editor's unsaved draft belongs to that editor, not to this
  // snapshot. Keep initial parameter values paired with the saved geometry.
  return clone(snapshot(profile)?.parameters ?? profile?.defaultParameters ?? {});
}

function sectionFromProfile(view, profile) {
  return {
    source: "library",
    key: profileSelectionKey(profile),
    ref: profileRef(profile),
    name: profileName(profile),
    parameters: profileParameters(view, profile),
    profile: clone(snapshot(profile)),
  };
}

export function punchProfileChoices(view) {
  return libraryProfiles(view).map(profile => ({
    key: profileSelectionKey(profile),
    scope: profileScope(profile),
    name: profileName(profile),
    specification: profileSpecification(profile),
    definitions: clone(profile?.descriptor?.parameters ?? profile?.parameterDefinitions ?? snapshot(profile)?.parameterDefinitions ?? []),
    defaultParameters: profileParameters(view, profile),
    profile,
  }));
}

export function punchFeatureForTarget(view, target) {
  const state = view?.tubeDesignerPunchWizard;
  const token = String(target?.dataset?.tubeDesignerPunchIndex ?? "draft");
  if (!state) return null;
  const end=target?.dataset?.tubeDesignerPunchEnd;
  if(end==="start"||end==="end")return state.ends?.[end]??null;
  if (token === "draft" || token === "") return state.draft;
  const index = Number(token);
  return Number.isInteger(index) && index >= 0 ? state.features?.[index] ?? null : null;
}

function chooseTool(state, item, predicate) {
  const tool = state?.tools?.find(predicate);
  if (tool) selectPunchTool(state, item, tool.id);
  return tool;
}

export function initializePunchRecordSource(view, item = view?.tubeDesignerPunchWizard?.draft) {
  const state = view?.tubeDesignerPunchWizard;
  if (!state || !item) return false;
  item.recordKind ??= item.section ? (item.section.source === "dxf" ? "dxf" : "branch") : "tool";
  if (item.recordKind !== "branch" || item.section) return false;
  const first = libraryProfiles(view)[0];
  if (!first) { item.recordKind="tool"; return true; }
  item.type = "branch-profile";
  item.section = sectionFromProfile(view, first);
  chooseTool(state, item, tool => tool.id === "branch-profile");
  return true;
}

export function changePunchRecordKind(view, target) {
  if(view?.pending)return false;
  const state = view?.tubeDesignerPunchWizard;
  const item = punchFeatureForTarget(view, target);
  const kind = String(target?.value ?? "tool");
  if (!state || !item || isPunchToolReadOnly(state,item) || !["branch", "tool", "dxf"].includes(kind)) return false;
  const end=target?.dataset?.tubeDesignerPunchEnd;
  if(end&&!state.tools.some(tool=>tool.id===(kind==="tool"?"end-square":"end-profile")))throw new Error("当前刀具库缺少所选端部刀具。");
  checkpointPunchWizard(state);
  item.recordKind = kind;
  if (kind === "tool") {
    delete item.section;
    const tool = end?chooseTool(state,item,candidate=>candidate.id==="end-square"):chooseTool(state, item, candidate => candidate.target === "side")
      ?? chooseTool(state, item, candidate => candidate.target === "part" && !candidate.requiresSection);
    if (!tool) {
      item.type = "circle";
      delete item.toolRef;
      delete item.toolParameters;
      delete item.toolTarget;
    }
  } else {
    const profile = libraryProfiles(view)[0];
    item.type = end?"end-profile":"branch-profile";
    delete item.toolRef;
    delete item.toolParameters;
    delete item.toolTarget;
    if (profile) item.section = sectionFromProfile(view, profile);
    else delete item.section;
    chooseTool(state, item, candidate => candidate.id === (end?"end-profile":"branch-profile"));
  }
  if(end){item.trim??=0;item.rotation??=0;item.datum="long";}else state.selectedFeatureId = item.id;
  return true;
}

export async function selectPunchProfileSource(context, view, target, ops) {
  if(view?.pending)return false;
  const state = view?.tubeDesignerPunchWizard;
  const item = punchFeatureForTarget(view, target);
  const key = String(target?.value ?? "");
  if (!state || !item || isPunchToolReadOnly(state,item) || !key) return false;
  const end=target?.dataset?.tubeDesignerPunchEnd;
  if(end&&!state.tools.some(tool=>tool.id==="end-profile"))throw new Error("当前刀具库缺少截面切端刀具。");
  const originalSection=item.section,request={};
  sourceRequests.set(item,request);
  const current=()=>view.tubeDesignerPunchWizard===state&&punchFeatureForTarget(view,target)===item
    &&item.section===originalSection&&sourceRequests.get(item)===request;
  const applySection=(section,recordKind)=>{
    if(!current())return false;
    checkpointPunchWizard(state);
    item.recordKind = recordKind;
    item.type = end?"end-profile":"branch-profile";
    item.section = section;
    chooseTool(state, item, candidate => candidate.id === (end?"end-profile":"branch-profile"));
    if(end){item.trim??=0;item.rotation??=0;item.datum="long";}else state.selectedFeatureId = item.id;
    return true;
  };
  if (key === "__dxf__") {
    const bridge = context?.appProxy?.bridge ?? context?.productProxy?.bridge ?? context?.sceneProxy?.bridge;
    if (!bridge?.openFileDialog) throw new Error("当前宿主没有提供本地 DXF 选择能力。");
    const sourcePath = String(await bridge.openFileDialog({
      title: end?"选择端部切割刀具的 DXF 截面":"选择支管或自定义冲孔截面",
      filters: [{ name: "DXF 二维截面", extensions: ["dxf"] }],
    }) ?? "").trim();
    if(!current()||view.pending)return false;
    if (!sourcePath) return false;
    if (!/\.dxf$/i.test(sourcePath)) throw new Error("请选择 DXF 截面文件。");
    if (typeof context?.productProxy?.invoke !== "function") throw new Error("当前产品服务无法读取 DXF 截面。");
    return runSourceOperation(context,view,ops,"正在导入本地 DXF 截面",async options=>{
      try {
        if(!current())return false;
        const response = await context.productProxy.invoke("TubeDesigner.ImportProfileDxf", { sourcePath }, options);
        if(!current())return false;
        if (!response?.profile?.contours?.length) throw new Error("DXF 中没有可用的闭合截面。");
        return applySection({
          source: "dxf", key: "__dxf__",
          name: response.profile.name ?? sourcePath.split(/[\\/]/).at(-1) ?? "本地 DXF",
          profile: clone(response.profile), parameters: {},
        },"dxf");
      } catch(error) {
        if(!current())return false;
        state.error=error?.message??String(error);
        throw error;
      }
    });
  } else {
    const profile = libraryProfiles(view).find(candidate => profileSelectionKey(candidate) === key);
    if (!profile) throw new Error("所选支管管型当前不可用。");
    return applySection(sectionFromProfile(view, profile),"branch");
  }
}

export async function updatePunchProfileParameter(context, view, target, ops) {
  if(view?.pending)return false;
  const state = view?.tubeDesignerPunchWizard;
  const item = punchFeatureForTarget(view, target);
  const key = String(target?.dataset?.tubeDesignerPunchProfileParameter ?? "");
  const section = item?.section;
  const choice = punchProfileChoices(view).find(candidate => candidate.key === section?.key);
  const definition = choice?.definitions?.find(candidate => candidate.key === key);
  if (!state || !item || isPunchToolReadOnly(state,item) || !section || !choice || !definition) return false;
  if (typeof context?.sceneProxy?.invoke !== "function") {
    throw new Error("当前未连接截面生成服务，无法更新管型参数；已保留原参数与截面。");
  }
  sourceRequests.delete(item);
  const value = definition.valueType === "boolean" ? !!target.checked
    : definition.valueType === "string" ? String(target.value)
    : target.value === "" ? Number.NaN : Number(target.value);
  checkpointPunchWizard(state);
  section.parameters ??= {};
  section.parameters[key] = value;
  const parameters=clone(section.parameters),signature=JSON.stringify(parameters),request={};
  profileRequests.set(section,request);
  // Until the matching result arrives, neither Confirm nor a concurrent preview
  // may submit these new parameters with the previous contour geometry.
  section.profile=null;
  const current=()=>view.tubeDesignerPunchWizard===state
    &&punchFeatureForTarget(view,target)===item&&item.section===section
    &&profileRequests.get(section)===request&&JSON.stringify(section.parameters)===signature;
  return runSourceOperation(context,view,ops,
    target?.dataset?.tubeDesignerPunchEnd?"正在生成端部刀具截面":"正在生成冲孔截面",async options=>{
    try {
      if(!current())return false;
      const response = await context.sceneProxy.invoke("TubeDesigner.GenerateProfilePreview", {
        profileRef: profileRef(choice.profile), parameters, length: 100,
      }, options);
      if(!current())return false;
      if (!response?.profile?.contours?.length) throw new Error("支管参数没有生成有效截面。");
      section.profile = clone(response.profile);
      section.name = profileName(choice.profile);
      return true;
    } catch(error) {
      if(!current())return false;
      state.error=error?.message??String(error);
      throw error;
    }
  });
}

export function visiblePunchProfileDefinitions(choice, values) {
  return (choice?.definitions ?? []).filter(definition => visibleParameter(definition, values ?? {}));
}
