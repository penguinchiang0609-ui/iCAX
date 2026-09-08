import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { editPunchWizardFeature, getPunchWizardPayload, validatePunchWizard, openPunchParameters, closePunchParameters, checkpointPunchWizard, setPunchFeatureSelected, removeSelectedPunchWizardFeatures } from "./punchWizard.mjs";
import { beginPunchOperation, finishPunchOperation, previewPunch } from "./punchEditor.mjs";
import { handlePunchArrayGroupAction } from "./punchArrayGroupActions.mjs";
import {
  addPunchWizardFeature,
  createPunchWizardState,
  punchWizardProfileInfo,
  removePunchWizardFeature,
  renderPunchWizardDialog as renderPunchWizardDialogView,
  updatePunchWizardField,
} from "./punchWizard.mjs";
import { libraryProfiles, profileRef, profileSelectionKey } from "./profileLibrary.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import {
  changePunchRecordKind,
  initializePunchRecordSource,
  punchProfileChoices,
  selectPunchProfileSource,
  updatePunchProfileParameter,
} from "./punchProfileSource.mjs";
import { restoreSavedNestingTask } from "./nestingWorkflow.mjs";

const ACTION_PREFIX = "tube-designer-nesting-punch-create-";
const NEW_PART_ID = "__new_nesting_punch_part__";

function action(name) {
  return `${ACTION_PREFIX}${name}`;
}

function profileSnapshot(profile) {
  return profile?.previewProfile ?? profile?.profile ?? (profile?.contours ? profile : null) ?? {};
}

function profileName(profile) {
  const displayName = profile?.name ?? profile?.previewProfile?.name ?? profile?.descriptor?.displayName;
  if (displayName && typeof displayName === "object") {
    return String(displayName["zh-CN"] ?? displayName["en-US"] ?? Object.values(displayName)[0] ?? "未命名管型");
  }
  return String(displayName ?? "未命名管型");
}

function profileSpecification(profile) {
  const snapshot = profileSnapshot(profile);
  return String(snapshot?.specification ?? profile?.specification ?? "截面规格待确认");
}

function selectedProfile(view, profiles = libraryProfiles(view)) {
  const draft = view?.tubeDesignerNestingPunchPartDraft;
  const key = String(draft?.profileKey ?? "");
  return profiles.find((profile) => profileSelectionKey(profile) === key)
    ?? profiles.find((profile) => profileSelectionKey(profile) === String(view?.tubeDesignerSelectedProfileId ?? ""))
    ?? profiles.find((profile) => String(profile?.id ?? "") === String(view?.tubeDesignerSelectedProfileId ?? ""))
    ?? profiles[0]
    ?? null;
}

function profileParameters(view, profile) {
  const creationInput = view?.tubeDesignerPunchWizard?.creationInput;
  if (creationInput) return creationInput.profileParameters[profileSelectionKey(profile)] ?? profile?.defaultParameters ?? {};
  return view?.tubeDesignerProfileDrafts?.[profileSelectionKey(profile)]?.parameters
    ?? profile?.defaultParameters
    ?? {};
}

function initializeCreationInput(view) {
  const wizard = view.tubeDesignerPunchWizard;
  if (!wizard || wizard.creationInput) return;
  // Capture stock parameters once for this wizard. Editing/undoing a stock
  // instance must not mutate unrelated drafts in the profile-library editor.
  const parameters = Object.fromEntries(libraryProfiles(view).map(profile => [
    profileSelectionKey(profile), structuredClone(profileParameters(view, profile)),
  ]));
  wizard.creationInput = { draft: view.tubeDesignerNestingPunchPartDraft, profileParameters: parameters };
}

function mainDiagramSnapshot(view, profile) {
  const draft = view?.tubeDesignerNestingPunchPartDraft;
  return draft?.diagramProfileKey === profileSelectionKey(profile) && draft.diagramProfile
    ? draft.diagramProfile : profileSnapshot(profile);
}

async function updateMainDiagramSnapshot(context, view, profile, parameters, ops) {
  const draft = view.tubeDesignerNestingPunchPartDraft;
  const key = profileSelectionKey(profile), signature = JSON.stringify(parameters);
  if (!draft || typeof context?.productProxy?.invoke !== "function") return true;
  if(view.pending)return false;
  const snapshot = mainDiagramSnapshot(view, profile);
  const definitions = profile?.descriptor?.parameters ?? snapshot?.parameterDefinitions ?? [];
  if (!definitions.length || (draft.diagramProfileKey === key && draft.diagramParametersSignature === signature)) return true;
  if (snapshot?.parameters && definitions.every(({ key: parameter, defaultValue }) =>
    (parameters[parameter] ?? defaultValue) === snapshot.parameters[parameter])) return true;
  const requestOptions=beginPunchOperation(context,view,"正在生成主管截面");
  const operation=view.tubeDesignerOperation;
  operation.message="等待截面生成完成后，再更新三维预览";
  draft.diagramError = "";
  ops.renderProject(context, view);
  try {
    const response = await context.productProxy.invoke("TubeDesigner.EvaluateProfilePackage", {
      profileRef: profileRef(profile), parameters: structuredClone(parameters),
    }, { ...requestOptions,timeoutMs: 30000 });
    if (!response?.profile?.contours?.length) throw new Error("未返回有效截面");
    if (view.tubeDesignerNestingPunchPartDraft === draft && draft.profileKey === key
      && JSON.stringify(profileParameters(view, profile)) === signature) {
      draft.diagramProfile = structuredClone(response.profile);
      draft.diagramProfileKey = key;
      draft.diagramParametersSignature = signature;
    }
    return true;
  } catch (error) {
    if (view.tubeDesignerNestingPunchPartDraft === draft) draft.diagramError = error?.message ?? String(error);
    return false;
  } finally {
    finishPunchOperation(view,operation);
  }
}

function virtualPart(view) {
  const profile = selectedProfile(view);
  const snapshot = profileSnapshot(profile);
  const draft = view?.tubeDesignerNestingPunchPartDraft ?? {};
  return {
    entityId: NEW_PART_ID,
    name: String(draft.name ?? "").trim() || `${profileName(profile)}冲孔件`,
    partNumber: "新建冲孔件",
    length: Number(draft.length),
    profile: snapshot,
    properties: { "tubeDesigner.profile": snapshot },
  };
}

function defaultDraft(view) {
  const profiles = libraryProfiles(view);
  const current = selectedProfile(view, profiles);
  const profileKey = current ? profileSelectionKey(current) : "";
  const draft = {
    profileKey,
    length: "1000",
    name: current ? `${profileName(current)}冲孔件` : "冲孔件",
    material: "",
    quantity: "1",
    error: profiles.length ? "" : "当前没有可用管型，请先在“管型库”添加管型。",
  };
  view.tubeDesignerNestingPunchPartDraft = draft;
  view.tubeDesignerPunchWizard = createPunchWizardState({
    ...virtualPart(view),
    length: Number(draft.length),
    profile: profileSnapshot(current),
  });
  view.tubeDesignerPunchWizard.creationMode = "main-tube-punch";
  initializeCreationInput(view);
  initializePunchRecordSource(view);
  return draft;
}

function parameterName(definition) {
  const value=definition?.displayName??definition?.name??definition?.key;
  return typeof value==="object"?String(value["zh-CN"]??value["en-US"]??Object.values(value)[0]??definition?.key):String(value??"");
}

function visibleParameter(definition,values) {
  const condition=definition?.visibleWhen;
  const test=item=>!item?true
    : Array.isArray(item.conditions)?(item.op==="any"?item.conditions.some(test):item.conditions.every(test))
    : Array.isArray(item.all)?item.all.every(test)
    : Array.isArray(item.any)?item.any.some(test)
    : item.op==="ne"?values?.[item.key??item.parameter]!==item.value
    : item.op==="eq"?values?.[item.key??item.parameter]===item.value:true;
  return test(condition);
}

function renderMainProfileParameter(definition,values,disabled) {
  const key=String(definition?.key??""),value=values?.[key]??definition?.defaultValue;
  const common=`data-cam-change-action="${action("main-profile-parameter")}" data-tube-designer-main-profile-parameter="${escapeAttr(key)}" data-profile-parameter-key="${escapeAttr(key)}" data-tube-designer-main-profile-value-type="${escapeAttr(definition?.valueType??"number")}"`;
  let control;
  if(definition?.options)control=`<select ${common} ${disabled}>${definition.options.map(option=>{const optionValue=option.value??option;return `<option value="${escapeAttr(optionValue)}" ${String(optionValue)===String(value)?"selected":""}>${escapeText(option.label??optionValue)}</option>`;}).join("")}</select>`;
  else if(definition?.valueType==="boolean")control=`<input type="checkbox" ${common} ${value?"checked":""} ${disabled}/>`;
  else control=`<input type="${definition?.valueType==="string"?"text":"number"}" step="${escapeAttr(definition?.step??(definition?.valueType==="integer"?1:"any"))}" value="${escapeAttr(value??"")}" ${common} ${disabled}/>`;
  const keyClass=key.replace(/[^a-zA-Z0-9_-]/g,"-");
  const caption=definition?.unit ? `${parameterName(definition)}（${definition.unit}）` : parameterName(definition);
  return `<label class="tube-designer-field tube-designer-punch-main-parameter tube-designer-punch-setup-param-${escapeAttr(keyClass)}"><span>${escapeText(caption)}</span>${control}</label>`;
}

function renderCreationSetup(view, draft, profiles, profile) {
  const disabled = view.pending ? "disabled" : "";
  const profileKey = profile ? profileSelectionKey(profile) : "";
  const parameters = profile ? profileParameters(view, profile) : {};
  const definitions=(profile?.descriptor?.parameters??profileSnapshot(profile)?.parameterDefinitions??[])
    .filter(definition=>visibleParameter(definition,parameters));
  const diagramHtml=definitions.length ? `<details class="tube-designer-nesting-profile-diagram"><summary>主管参数示意图</summary>${renderProfileParameterDiagram(mainDiagramSnapshot(view, profile), { definitions, parameters, compact: true, title: "主管参数示意图" })}</details>` : "";
  return `<section class="tube-designer-nesting-punch-setup" data-profile-parameter-scope>
    <div class="tube-designer-nesting-punch-setup-grid">
      <label class="tube-designer-field wide"><span>管型</span><select data-cam-change-action="${action("draft-change")}" data-tube-designer-nesting-punch-field="profileKey" ${disabled}><option value="">请选择管型</option>${profiles.map((item) => `<option value="${escapeAttr(profileSelectionKey(item))}" ${profileSelectionKey(item) === profileKey ? "selected" : ""}>${escapeText(profileName(item))} · ${escapeText(profileSpecification(item))}</option>`).join("")}</select></label>
      ${definitions.map(definition=>renderMainProfileParameter(definition,parameters,disabled)).join("")}
      <label class="tube-designer-field tube-designer-punch-setup-length"><span>成品长度（mm）</span><input type="number" min="1" max="100000" step="1" value="${escapeAttr(draft.length)}" data-cam-change-action="${action("draft-change")}" data-tube-designer-nesting-punch-field="length" ${disabled} /></label>
      <label class="tube-designer-field tube-designer-punch-setup-quantity"><span>数量（件）</span><input type="number" min="1" max="1000000" step="1" inputmode="numeric" data-tube-designer-integer="true" value="${escapeAttr(draft.quantity)}" data-cam-change-action="${action("draft-change")}" data-tube-designer-nesting-punch-field="quantity" ${disabled} /></label>
      <label class="tube-designer-field tube-designer-punch-setup-name"><span>零件名称</span><input type="text" maxlength="160" value="${escapeAttr(draft.name)}" data-cam-change-action="${action("draft-change")}" data-tube-designer-nesting-punch-field="name" ${disabled} /></label>
      <label class="tube-designer-field tube-designer-punch-setup-material"><span>材料（可选）</span><input type="text" maxlength="240" placeholder="例如：Q235B、不锈钢 304" value="${escapeAttr(draft.material)}" data-cam-change-action="${action("draft-change")}" data-tube-designer-nesting-punch-field="material" ${disabled} /></label>
      ${diagramHtml}
    </div>
    ${draft.diagramError ? `<small class="tube-drawing-warning">截面示意图尚未更新：${escapeText(draft.diagramError)}</small>` : ""}
    ${draft.error ? `<div class="tube-designer-punch-error" role="alert">${escapeText(draft.error)}</div>` : ""}
  </section>`;
}

export function renderNestingPunchPartDialog(view) {
  const draft = view?.tubeDesignerNestingPunchPartDraft;
  if (!draft) return "";
  const profiles = libraryProfiles(view);
  const profile = selectedProfile(view, profiles);
  const part = virtualPart(view);
  const configuredHoleCount = view.tubeDesignerPunchWizard?.features
    ?.filter((feature) => feature.enabled !== false).length ?? 0;
  const previewingDraft = view.tubeDesignerPunchWizard?.preview?.includesDraft === true;
  return renderPunchWizardDialogView(part, view, {
    actionPrefix: ACTION_PREFIX,
    title: "新建主管冲孔件",
    subtitle: `${profileName(profile)}主管 · ${formatNumber(Number(draft.length))} mm · 创建后可直接排样`,
    applyLabel: "添加到零件",
    footerNote: configuredHoleCount
      ? "生成结果为一根完整主管扣除孔位后的实体；孔位、孔型和阵列参数会随零件保留。"
      : "可以先保存完整主管，之后再从零件详情进入冲孔向导追加孔位。",
    previewHint: "先显示完整主管；添加孔位后更新为主管扣孔结果",
    previewTitle: "主管与刀具场景",
    previewCountLabel: `${view.tubeDesignerPunchWizard?.features?.length ?? 0} 条已保存孔位${previewingDraft ? " + 当前刀具" : ""}`,
    previewStatus: "蓝色半透明实体是主管，橙色拉伸体是当前孔刀；加入清单后才会写入最终零件。",
    previewActionLabel: "更新刀具体",
    previewLegendHtml: '<div class="tube-designer-punch-preview-legend"><span><i class="is-blank"></i>主管</span><span><i class="is-tool"></i>孔刀拉伸体</span></div>',
    featureEditorTitle: "当前孔刀参数",
    featureAddLabel: view.tubeDesignerPunchWizard?.editingId ? "保存孔位修改" : "加入孔位清单",
    featureListTitle: "冲孔清单",
    featureEmptyText: "当前是完整主管，尚未添加孔位。",
    tableMode: true,
    showEnds: true,
    editorFirst: true,
    introInEditor: true,
    bodyClass: "tube-designer-punch-body--creation",
    dialogClass: "tube-designer-nesting-punch-dialog",
    partId: NEW_PART_ID,
    branchProfiles: punchProfileChoices(view),
    introHtml: renderCreationSetup(view, draft, profiles, profile),
  });
}

async function previewCreation(context, view, ops, { includeDraft = false, quiet = false } = {}) {
  if (!selectedProfile(view) || typeof context?.sceneProxy?.invoke !== "function") {
    ops.renderProject(context, view);
    return;
  }
  const state = ensureCreationState(view);
  if (!state) return;
  const wizard = view.tubeDesignerPunchWizard;
  const metadata = readCreationMetadata(view);
  if(!await updateMainDiagramSnapshot(context, view, metadata.profile, metadata.parameters, ops)) {
    ops.renderProject(context,view);return;
  }
  if (view.tubeDesignerNestingPunchPartDraft !== state.draft || view.tubeDesignerPunchWizard !== wizard
    || state.draft.profileKey !== profileSelectionKey(metadata.profile)
    || Number(state.draft.length) !== metadata.length
    || JSON.stringify(profileParameters(view, metadata.profile)) !== JSON.stringify(metadata.parameters)) return;
  await previewPunch(context, view, state.part, ops, {
    profileRef: metadata.profileRef,
    parameters: metadata.parameters,
    length: metadata.length,
  }, { includeDraft, quiet });
}

function ensureCreationState(view) {
  const draft = view?.tubeDesignerNestingPunchPartDraft;
  if (!draft || !view?.tubeDesignerPunchWizard) return null;
  return { draft, part: virtualPart(view) };
}

function readCreationMetadata(view) {
  const draft = view.tubeDesignerNestingPunchPartDraft;
  const profile = selectedProfile(view);
  if (!profile) throw new Error("请先选择一个管型。");
  const name = String(draft.name ?? "").trim();
  if (!name) throw new Error("请填写零件名称。");
  if (name.length > 160) throw new Error("零件名称不能超过 160 个字符。");
  const material = String(draft.material ?? "").trim();
  if (material.length > 240) throw new Error("材料名称不能超过 240 个字符。");
  const length = Number(draft.length);
  if (!Number.isFinite(length) || length < 1 || length > 100000) {
    throw new Error("成品长度须为 1 至 100000 mm。");
  }
  const quantity = Number(draft.quantity);
  if (!Number.isSafeInteger(quantity) || quantity < 1 || quantity > 1000000) {
    throw new Error("数量须为 1 至 1000000 之间的整数。");
  }
  return { profile, profileRef: profileRef(profile), parameters: profileParameters(view, profile), name, material, length, quantity };
}

async function confirmNestingPunchPart(context, view, ops) {
  if (view.pending) return null;
  const state = ensureCreationState(view);
  if (!state) throw new Error("添加冲孔件页面已失效，请重新打开。");
  let metadata;
  try {
    metadata = readCreationMetadata(view);
    const punchPayload=getPunchWizardPayload(view);
    const features=punchPayload.features;
    const validationError = validatePunchWizard(view,state.part);
    if (validationError) throw new Error(validationError);
    if (typeof context.sceneProxy?.invoke !== "function") throw new Error("当前项目未连接，无法添加冲孔件。");
    const invocationOptions=beginPunchOperation(context,view,"正在生成独立下料零件");
    view.pending = true;
    view.tubeDesignerNestingPunchPartDraft.error = "";
    ops.renderProject(context, view);
    await waitForPaint();
    const response = await context.sceneProxy.invoke("TubeDesigner.AddNestingPunchPart", {
      profileRef: metadata.profileRef,
      parameters: metadata.parameters,
      length: metadata.length,
      name: metadata.name,
      material: metadata.material,
      quantity: metadata.quantity,
      features,
      ends: punchPayload.ends,
    }, invocationOptions);
    if (!response?.tubeDesigner || !response.partEntityId) throw new Error("冲孔件未返回有效的下料记录。");
    view.scene ??= {};
    view.scene.tubeDesigner = response.tubeDesigner;
    restoreSavedNestingTask(view, context);
    const partId = String(response.partEntityId);
    view.tubeDesignerNestingSelectedPartIds = [partId];
    view.tubeDesignerActivePartId = partId;
    view.tubeDesignerActiveNestingPartId = partId;
    view.tubeDesignerNestingSelectionKind = "part";
    view.tubeDesignerActiveNestingPlacementId = "";
    view.tubeDesignerPartMeasurementState = null;
    view.tubeDesignerPartViewportKey = "";
    view.tubeDesignerNestingPunchPartDraft = null;
    view.tubeDesignerPunchWizard = null;
    view.tubeDesignerNestingImportDraft = null;
    view.tubeDesignerNestingStockDraft = null;
    view.tubeDesignerNestingSettingsSourceSignature = "";
    ops.showNotice?.(context, view, `已添加冲孔件“${metadata.name}”，共 ${metadata.quantity} 件，可在左侧零件清单中继续排样。`);
    await context.actions?.refreshActiveSceneState?.();
    return response;
  } catch (error) {
    const message = error?.message ?? String(error);
    if (view.tubeDesignerNestingPunchPartDraft) view.tubeDesignerNestingPunchPartDraft.error = message;
    throw error;
  } finally {
    finishPunchOperation(view);
    ops.renderProject(context, view);
  }
}

export async function handleNestingPunchPartAction(context, view, actionName, target, ops) {
  if (!actionName.startsWith(ACTION_PREFIX)) return { handled: false };
  if (view.pending) return { handled: true };
  const suffix = actionName.slice(ACTION_PREFIX.length);
  if(suffix==="preview-mode") {
    if(view.tubeDesignerPunchWizard)view.tubeDesignerPunchWizard.previewMode="tools";
    ops.renderProject(context,view);return {handled:true};
  }
  if(suffix==="parameters-open") {
    const revision=view.tubeDesignerPunchWizard?.revision;
    openPunchParameters(view,target?.dataset?.tubeDesignerPunchIndex,target?.dataset?.tubeDesignerPunchEnd,target?.dataset?.tubeDesignerPunchEditorMode);
    if(revision!==view.tubeDesignerPunchWizard?.revision)await previewCreation(context,view,ops,{includeDraft:view.tubeDesignerPunchWizard?.parameterEditor?.index==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  const arrayAction=handlePunchArrayGroupAction(view,suffix,target);
  if(arrayAction.handled) {
    if(arrayAction.changed)await previewCreation(context,view,ops,{includeDraft:arrayAction.includeDraft,quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(suffix==="summary-select") {
    const s=view.tubeDesignerPunchWizard,index=Number(target?.dataset?.tubeDesignerPunchIndex);
    if(s?.features?.[index]){s.selectedFeatureId=s.features[index].id;s.scrollToFeatureIndex=index;}
    ops.renderProject(context,view);return {handled:true};
  }
  if(suffix==="selection-change") {
    setPunchFeatureSelected(view,target?.dataset?.tubeDesignerPunchIndex,target?.checked===true);
    ops.renderProject(context,view);return {handled:true};
  }
  if(suffix==="remove-selected") {
    if(removeSelectedPunchWizardFeatures(view))await previewCreation(context,view,ops,{quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(suffix==="copy-selected") {
    const s=view.tubeDesignerPunchWizard;
    const index=s?.features?.findIndex(item=>item.id===s.selectedFeatureId)??-1;
    const changed=Number.isInteger(index)&&index>=0&&editPunchWizardFeature(view,"copy",index);
    if(changed)await previewCreation(context,view,ops,{includeDraft:true,quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(["parameters-apply","parameters-cancel","parameters-close","parameters-preview"].includes(suffix)) {
    const includeDraft=view.tubeDesignerPunchWizard?.parameterEditor?.index==="draft";
    // The parameter window is live: its only visible action is close, which
    // commits the already-previewed values. Keep the legacy preview/apply
    // actions tolerant for old integrations, but never roll back on close.
    const commit=suffix!=="parameters-preview", closeWithoutValidation=suffix==="parameters-cancel"||suffix==="parameters-close";
    if(suffix==="parameters-preview"||closePunchParameters(view,commit,virtualPart(view),closeWithoutValidation?false:true))await previewCreation(context,view,ops,{includeDraft,quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(["edit","copy","toggle","undo","redo","remove-end"].includes(suffix)) {
    const changed = editPunchWizardFeature(view,suffix,target?.dataset?.tubeDesignerPunchIndex);
    if (changed) await previewCreation(context, view, ops, {
      includeDraft: suffix === "edit" || suffix === "copy", quiet: true,
    });
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(suffix==="preview") {
    const metadata=readCreationMetadata(view);
    await previewPunch(context,view,virtualPart(view),ops,
      {profileRef:metadata.profileRef,parameters:metadata.parameters,length:metadata.length},
      {includeDraft:true});
    return {handled:true};
  }
  if (suffix === "open") {
    defaultDraft(view);
    await previewCreation(context, view, ops);
    return { handled: true };
  }
  if (suffix === "cancel") {
    view.tubeDesignerNestingPunchPartDraft = null;
    view.tubeDesignerPunchWizard = null;
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (suffix === "draft-change") {
    const draft = view.tubeDesignerNestingPunchPartDraft;
    const field = String(target?.dataset?.tubeDesignerNestingPunchField ?? "");
    if (!draft || view.tubeDesignerPunchWizard?.parameterEditor
      || !["profileKey", "length", "quantity", "name", "material"].includes(field)) return { handled: true };
    let value = String(target?.value ?? "");
    if (field === "quantity") {
      // step=1 controls the spinner, while this also handles pasted/typed
      // decimals before the draft is stored or previewed.
      value = value.match(/^\d+/)?.[0] ?? "";
      if (target && target.value !== value) target.value = value;
    }
    if (draft[field] === value) return { handled: true };
    if (!view.tubeDesignerPunchWizard) {
      view.tubeDesignerPunchWizard = createPunchWizardState(virtualPart(view));
      view.tubeDesignerPunchWizard.creationMode = "main-tube-punch";
      initializePunchRecordSource(view);
    }
    initializeCreationInput(view);
    checkpointPunchWizard(view.tubeDesignerPunchWizard,{geometryChanged:field==="profileKey"||field==="length"});
    draft[field] = value;
    draft.error = field === "profileKey" && !selectedProfile(view) ? "请先选择一个管型。" : "";
    // Keep all machining records while changing/reverting the blank geometry.
    view.tubeDesignerPunchWizard.baseLength = Number(draft.length);
    if ((field === "profileKey" || field === "length") && selectedProfile(view)) {
      await previewCreation(context, view, ops);
    } else {
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if(suffix==="main-profile-parameter") {
    if (!view.tubeDesignerPunchWizard || view.tubeDesignerPunchWizard.parameterEditor) return { handled: true };
    const profile=selectedProfile(view),key=String(target?.dataset?.tubeDesignerMainProfileParameter??"");
    const definition=(profile?.descriptor?.parameters??profileSnapshot(profile)?.parameterDefinitions??[]).find(item=>String(item?.key??"")===key);
    if(profile&&definition&&key) {
      const valueType=String(target?.dataset?.tubeDesignerMainProfileValueType??definition.valueType??"number");
      const value=valueType==="boolean"?!!target.checked:valueType==="string"?String(target.value):Number(target.value);
      initializeCreationInput(view);
      const profileKey=profileSelectionKey(profile);
      const current=profileParameters(view,profile);
      if (Object.is(current[key] ?? definition.defaultValue, value)) return { handled: true };
      checkpointPunchWizard(view.tubeDesignerPunchWizard);
      view.tubeDesignerPunchWizard.creationInput.profileParameters[profileKey]={...current,[key]:value};
      await previewCreation(context,view,ops);
    } else ops.renderProject(context,view);
    return {handled:true};
  }
  if(suffix==="record-kind-change") {
    const selected=String(target?.value??"");
    const changed=selected==="dxf"
      ? await selectPunchProfileSource(context,view,{dataset:target?.dataset??{},value:"__dxf__"},ops)
      : changePunchRecordKind(view,target);
    if(changed)await previewCreation(context,view,ops,{includeDraft:String(target?.dataset?.tubeDesignerPunchIndex??"draft")==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(suffix==="profile-select") {
    if(await selectPunchProfileSource(context,view,target,ops))await previewCreation(context,view,ops,{includeDraft:String(target?.dataset?.tubeDesignerPunchIndex??"draft")==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(suffix==="profile-parameter") {
    if(await updatePunchProfileParameter(context,view,target,ops))await previewCreation(context,view,ops,{includeDraft:String(target?.dataset?.tubeDesignerPunchIndex??"draft")==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if (suffix === "field-change") {
    if (updatePunchWizardField(view, target)) {
      const row = target?.dataset?.tubeDesignerPunchIndex;
      await previewCreation(context, view, ops, {
        includeDraft: row === undefined || row === "" || row === "draft", quiet: true,
      });
    } else ops.renderProject(context, view);
    return { handled: true };
  }
  if (suffix === "add") {
    const state = ensureCreationState(view);
    if (state && addPunchWizardFeature(view, state.part)) {
      await previewCreation(context, view, ops, { quiet: true });
    } else ops.renderProject(context, view);
    return { handled: true };
  }
  if (suffix === "remove") {
    if (removePunchWizardFeature(view, target?.dataset?.tubeDesignerPunchIndex)) {
      await previewCreation(context, view, ops, { quiet: true });
    } else ops.renderProject(context, view);
    return { handled: true };
  }
  if (suffix === "apply") return { handled: true, result: await confirmNestingPunchPart(context, view, ops) };
  return { handled: false };
}

export async function handleNestingPunchPartRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "nesting.add-punch-part") return false;
  if(view.pending)return true;
  defaultDraft(view);
  await previewCreation(context, view, ops);
  return true;
}

function waitForPaint() {
  const requestFrame = globalThis.requestAnimationFrame;
  if (typeof requestFrame !== "function") return Promise.resolve();
  return new Promise((resolve) => requestFrame(() => requestFrame(resolve)));
}

export { NEW_PART_ID };
