import { parameterVisible, parameterEnabled, availableParameterChoices } from "./parameterConditions.mjs";
import { isAdvancedParameter } from "./parameterPresentation.mjs";
import { templateDisplayView } from "./designerViews.mjs";
import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { editPunchWizardFeature, validatePunchWizard, punchToolDescriptor, openPunchParameters, closePunchParameters, checkpointPunchWizard, setPunchFeatureSelected, removeSelectedPunchWizardFeatures, togglePunchRowParameters, handlePunchColumnAction } from "./punchWizard.mjs";
import { beginPunchOperation, finishPunchOperation, previewPunch, waitForPunchPreview } from "./punchEditor.mjs";
import { handlePunchArrayGroupAction } from "./punchArrayGroupActions.mjs";
import {
  addPunchWizardFeature,
  createPunchWizardState,
  punchWizardProfileInfo,
  removePunchWizardFeature,
  updatePunchWizardField,
} from "./punchWizard.mjs";
import { libraryProfiles, profileRef, profileSelectionKey, renderProfileParameterControl, renderProfileParameterGroups } from "./profileLibrary.mjs";
import { handlePunchDiagramAction, punchMainDiagramOwner } from "./punchParameterDiagram.mjs";
import {
  changePunchRecordKind,
  initializePunchRecordSource,
  punchProfileChoices,
  selectPunchProfileSource,
  updatePunchProfileParameter,
  waitForPunchProfileSources,
} from "./punchProfileSource.mjs";
import { restoreSavedNestingTask } from "./nestingWorkflow.mjs";
import { renderPunchBatchWizardDialog } from "./punchBatchView.mjs";
import {
  createPunchBatch, syncPunchBatch, selectPunchBatchPart, addPunchBatchPart,
  copyPunchBatchParts, removePunchBatchParts, setPunchBatchSelection, updatePunchBatchPart, pastePunchBatchParts,
  advancePunchBatchPart, selectPunchBatchDefinition, addPunchBatchDefinition, removePunchBatchDefinition,
  setPunchBatchHoleDefinition, punchBatchPartPayload, punchBatchTotals,
  openPunchBatchProfileAdvanced, updatePunchBatchProfileAdvanced,
  closePunchBatchProfileAdvanced, punchBatchProfileAdvancedChanged,
} from "./punchBatch.mjs";

const ACTION_PREFIX = "tube-designer-nesting-punch-create-";
const NEW_PART_ID = "__new_nesting_punch_part__";
const creationPreviewQueues = new WeakMap();

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

async function updateMainDiagramSnapshot(context, view, profile, parameters, ops, background = false) {
  const draft = view.tubeDesignerNestingPunchPartDraft;
  const key = profileSelectionKey(profile), signature = JSON.stringify(parameters);
  if (!draft || typeof context?.productProxy?.invoke !== "function") return true;
  if(view.pending)return false;
  const snapshot = mainDiagramSnapshot(view, profile);
  const definitions = profile?.descriptor?.parameters ?? snapshot?.parameterDefinitions ?? [];
  if (!definitions.length || (draft.diagramProfileKey === key && draft.diagramParametersSignature === signature)) return true;
  if (snapshot?.parameters && definitions.every(({ key: parameter, defaultValue }) =>
    (parameters[parameter] ?? defaultValue) === snapshot.parameters[parameter])) return true;
  const requestOptions=background?{timeoutMs:30000}:beginPunchOperation(context,view,"正在生成主管截面");
  const operation=background?{}:view.tubeDesignerOperation;
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
    if (view.tubeDesignerNestingPunchPartDraft === draft && draft.profileKey === key
      && JSON.stringify(profileParameters(view, profile)) === signature) draft.diagramError = error?.message ?? String(error);
    return false;
  } finally {
    if(!background)finishPunchOperation(view,operation);
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
    lengthDatum: "long",
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
  createPunchBatch(view);
  return draft;
}

function rememberBatchOpener(context, view, target = null) {
  const batch = view.tubeDesignerPunchBatch;
  const doc = context.mount?.ownerDocument;
  const opener = target?.nodeType === 1 ? target : doc?.activeElement;
  if (!batch || !opener || opener === doc?.body || opener === doc?.documentElement) return;
  batch.openerElement = opener;
  const data = { ...opener.dataset };
  batch.openerData = Object.keys(data).length ? data : null;
}

function visibleParameter(definition, values = {}) {
  return parameterVisible(definition, values);
}

function renderMainProfileParameter(definition,values,disabled) {
  const control=renderProfileParameterControl(definition,values,disabled,{
    changeAction:action("main-profile-parameter"),
    attributes:{"data-tube-designer-main-profile-parameter":definition.key,
      "data-tube-designer-main-profile-value-type":definition.valueType??"number"},
  });
  return control.replace('class="tube-designer-field','class="tube-designer-field tube-designer-punch-main-parameter')
    .replace('</span>',(definition.unit?' / '+escapeText(definition.unit):'')+'</span>');
}

function renderCreationSetup(view, draft, profiles, profile, parametersOverride = null) {
  const disabled = view.pending ? "disabled" : "";
  const profileKey = profile ? profileSelectionKey(profile) : "";
  const parameters = parametersOverride ?? (profile ? profileParameters(view, profile) : {});
  const definitions=(profile?.descriptor?.parameters??profileSnapshot(profile)?.parameterDefinitions??[])
    .filter(definition=>visibleParameter(definition,parameters));
  return `<section class="tube-designer-nesting-punch-setup punch-batch-profile-toolbar" data-profile-parameter-scope data-parameter-diagram-owner="${punchMainDiagramOwner}">
    <div class="tube-designer-nesting-punch-setup-grid">
      <label class="tube-designer-field wide"><span>管型</span><select data-cam-change-action="${action("draft-change")}" data-tube-designer-nesting-punch-field="profileKey" ${disabled}><option value="">请选择管型</option>${profiles.map((item) => `<option value="${escapeAttr(profileSelectionKey(item))}" ${profileSelectionKey(item) === profileKey ? "selected" : ""}>${escapeText(profileName(item))}</option>`).join("")}</select></label>
    </div>
    <div class="tube-designer-punch-main-profile-parameters">${profile?renderProfileParameterGroups(profile,definitions,parameters,view.pending,{
      keyPrefix:"punch-main",renderControl:renderMainProfileParameter,inlineBasicGroups:true,
    }):""}</div>
    ${definitions.length?`<button class="tube-designer-secondary punch-main-diagram-button" data-cam-action="${action('section-diagram')}" data-tube-designer-punch-index="main" ${disabled}>参数示意图</button>`:''}
    ${draft.diagramError ? `<small class="tube-drawing-warning">截面示意图尚未更新：${escapeText(draft.diagramError)}</small>` : ""}
    ${draft.error ? `<div class="tube-designer-punch-error" role="alert">${escapeText(draft.error)}</div>` : ""}
  </section>`;
}

export function renderNestingPunchPartDialog(view) {
  syncPunchBatch(view);
  const draft = view?.tubeDesignerNestingPunchPartDraft;
  if (!draft) return "";
  const profiles = libraryProfiles(view);
  const profile = selectedProfile(view, profiles);
  const part = virtualPart(view);
  const configuredHoleCount = view.tubeDesignerPunchWizard?.features
    ?.filter((feature) => feature.enabled !== false).length ?? 0;
  const previewingDraft = view.tubeDesignerPunchWizard?.preview?.includesDraft === true;
  const options = {
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
    mainDiagram: profile ? { snapshot: mainDiagramSnapshot(view, profile),
      definitions: (profile.descriptor?.parameters ?? profileSnapshot(profile)?.parameterDefinitions ?? []).filter(d=>visibleParameter(d,profileParameters(view,profile))),
      parameters: profileParameters(view,profile) } : null,
    introHtml: renderCreationSetup(view, draft, profiles, profile),
  };
  const advancedEditor = view.tubeDesignerPunchBatch?.profileAdvancedEditor;
  const sharedAdvancedSetupHtml = advancedEditor && profileSelectionKey(profile) === advancedEditor.profileKey
    ? renderCreationSetup(view, draft, profiles, profile, advancedEditor.parameters) : "";
  return renderPunchBatchWizardDialog(view, { part, sharedSetupHtml: options.introHtml, sharedAdvancedSetupHtml, options });
}

function profileDefinitions(profile) {
  return profile?.descriptor?.parameters ?? profileSnapshot(profile)?.parameterDefinitions ?? [];
}

function profileAdvancedDefinitions(profile) {
  const display = templateDisplayView(profile?.descriptor ?? {}, "right");
  // Match the original profile renderer, including its explicit advanced group.
  return profileDefinitions(profile).filter(definition => isAdvancedParameter(definition)
    || display.groups.advanced && display.fields[definition.key]?.group === "advanced");
}

function validateProfileAdvancedDraft(profile, editor) {
  const definitions = profileDefinitions(profile);
  const values = { ...Object.fromEntries(definitions.map(definition => [definition.key, definition.defaultValue])), ...editor.parameters };
  for (const definition of profileAdvancedDefinitions(profile)) {
    if (!parameterVisible(definition, values) || !parameterEnabled(definition, values)) continue;
    const value = values[definition.key];
    const label = definition.displayName;
    const name = typeof label === "object" ? String(label?.["zh-CN"] ?? label?.["en-US"] ?? definition.key) : String(label ?? definition.key);
    const choices = availableParameterChoices(definition, values);
    if ((definition.options?.length || definition.choices?.length)
      && !choices.some(choice => String(choice?.value ?? choice) === String(value))) return `${name}不是有效选项。`;
    if (["string", "boolean"].includes(definition.valueType)) continue;
    const number = Number(value), minimum = definition.min ?? definition.minimum, maximum = definition.max ?? definition.maximum;
    if (!Number.isFinite(number) || minimum != null && number < Number(minimum)
      || maximum != null && number > Number(maximum)
      || definition.valueType === "integer" && !Number.isSafeInteger(number)) return `${name}不在允许的数值范围内。`;
  }
  return "";
}

async function previewCreation(context, view, ops, { includeDraft = false, quiet = false } = {}) {
  const batch=view.tubeDesignerPunchBatch;
  if(!batch)return computeCreationPreview(context,view,ops,{includeDraft,quiet});
  syncPunchBatch(view);
  const state=view.tubeDesignerPunchWizard;
  if(!state)return;
  let queue=creationPreviewQueues.get(view);
  if(!queue||queue.owner!==batch) {
    queue={owner:batch,latest:null,flight:null};creationPreviewQueues.set(view,queue);
  }
  // Table edits take effect immediately. Keep only the latest preview intent
  // while a native calculation/resource load is running; never overlap jobs.
  queue.latest={state,draft:view.tubeDesignerNestingPunchPartDraft,includeDraft:false,quiet};
  state.requestedPreviewIncludesDraft=false;
  state.previewPending=true;
  ops.renderProject(context,view);
  if(!queue.flight) {
    queue.flight=Promise.resolve().then(async()=>{
      while(queue.latest&&view.tubeDesignerPunchBatch===batch) {
        if(view.pending) {
          const operation=view.tubeDesignerOperation;
          if(!operation?.finished)break;
          await operation.finished;
          continue;
        }
        await waitForPunchPreview(view);
        if(view.tubeDesignerPunchBatch!==batch||view.pending)continue;
        const request=queue.latest;queue.latest=null;
        if(view.tubeDesignerPunchWizard!==request.state
          ||view.tubeDesignerNestingPunchPartDraft!==request.draft)continue;
        try {
          request.state.previewPending=false;
          await computeCreationPreview(context,view,ops,{...request,background:true});
        } catch(error) {
          if(view.tubeDesignerPunchWizard===request.state&&!queue.latest)
            request.state.error=error?.message??String(error);
        } finally {
          request.state.previewPending=false;
        }
      }
    }).finally(()=>{
      queue.flight=null;
      if(view.tubeDesignerPunchBatch===batch)ops.renderProject(context,view);
    });
  }
  // Ordinary sheet actions must not wait for computation. Opening/applying
  // stock settings may still await the drain without disabling the form.
  if(!quiet)await queue.flight;
}

async function computeCreationPreview(context, view, ops, { includeDraft = false, quiet = false, background = false } = {}) {
  syncPunchBatch(view);
  if (!selectedProfile(view) || typeof context?.sceneProxy?.invoke !== "function") {
    ops.renderProject(context, view);
    return;
  }
  const state = ensureCreationState(view);
  if (!state) return;
  const wizard = view.tubeDesignerPunchWizard;
  const revision=wizard.revision;
  const metadata = readCreationMetadata(view);
  if(!await updateMainDiagramSnapshot(context, view, metadata.profile, metadata.parameters, ops, background)) {
    ops.renderProject(context,view);return;
  }
  if (view.pending||view.tubeDesignerNestingPunchPartDraft !== state.draft || view.tubeDesignerPunchWizard !== wizard || wizard.revision!==revision
    || state.draft.profileKey !== profileSelectionKey(metadata.profile)
    || Number(state.draft.length) !== metadata.length
    || JSON.stringify(profileParameters(view, metadata.profile)) !== JSON.stringify(metadata.parameters)) return;
  await previewPunch(context, view, state.part, ops, {
    profileRef: metadata.profileRef,
    parameters: metadata.parameters,
    length: metadata.length,
  }, { includeDraft, quiet, background });
}

function ensureCreationState(view) {
  const draft = view?.tubeDesignerNestingPunchPartDraft;
  if (!draft || !view?.tubeDesignerPunchWizard) return null;
  return { draft, part: virtualPart(view) };
}

function readCreationMetadata(view, batchPart = null) {
  const draft = batchPart?.draft ?? view.tubeDesignerNestingPunchPartDraft;
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
  if (!["long", "center", "short"].includes(String(draft.lengthDatum ?? "long"))) throw new Error("请选择有效的成品长度基准。");
  return { profile, profileRef: profileRef(profile), parameters: profileParameters(view, profile), name, material, length, quantity };
}

function validateBatchDescriptorValues(part) {
  const localized = value => typeof value === "object" ? String(value?.["zh-CN"] ?? value?.["en-US"] ?? "参数") : String(value ?? "参数");
  for (const item of [...(part.wizard.features ?? []).filter(feature => feature.enabled !== false),
    ...Object.values(part.wizard.ends ?? {}).filter(end => end.type !== "keep")]) {
    const descriptor = punchToolDescriptor(part.wizard, item);
    if (!descriptor) continue; // Existing missing/frozen-tool validation owns this case.
    for (const [definitions, supplied] of [[descriptor.parameters ?? [], item.toolParameters ?? {}], [descriptor.operationParameters ?? [], item]]) {
      const values = { ...Object.fromEntries(definitions.map(definition => [definition.key, definition.defaultValue])), ...supplied };
      for (const definition of definitions) {
        if (!parameterVisible(definition, values) || !parameterEnabled(definition, values)) continue;
        const value = values[definition.key], name = localized(definition.displayName ?? definition.key);
        const options = definition.options ?? definition.choices;
        if (Array.isArray(options) && !options.some(option => String(option?.value ?? option) === String(value))) return name + "不是有效选项。";
        if (["string", "boolean"].includes(definition.valueType)) continue;
        const number = Number(value);
        if (!Number.isFinite(number) || definition.min !== undefined && number < Number(definition.min)
          || definition.max !== undefined && number > Number(definition.max)
          || definition.valueType === "integer" && !Number.isSafeInteger(number)) return name + "不在允许的数值范围内。";
      }
    }
  }
  return "";
}

async function confirmNestingPunchPart(context, view, ops) {
  if (view.pending) return null;
  const state = ensureCreationState(view);
  if (!state) throw new Error("添加冲孔件页面已失效，请重新打开。");
  const batch = view.tubeDesignerPunchBatch;
  if (!batch?.parts?.length) throw new Error("请至少添加一行零件。");
  let operation = null, failedPart = null;
  try {
    if (typeof context.sceneProxy?.invoke !== "function") throw new Error("当前项目未连接，无法添加冲孔件。");
    const invocationOptions=beginPunchOperation(context,view,"正在生成整批冲孔零件");
    operation = view.tubeDesignerOperation;
    batch.error = "";
    for (const part of batch.parts) part.draft.error = "";
    const previewQueue=creationPreviewQueues.get(view);
    if(previewQueue?.owner===batch)previewQueue.latest=null;
    ops.renderProject(context, view);
    // Hold the save lock while matching section contours finish. Background
    // generators may publish valid results, but cannot schedule another preview.
    await waitForPunchProfileSources(view);
    if (view.tubeDesignerPunchBatch !== batch || view.tubeDesignerOperation !== operation) return null;
    syncPunchBatch(view);
    const requests = batch.parts.filter(part => !part.completedPartId).map(part => {
      failedPart = part;
      const metadata = readCreationMetadata(view, part);
      const validationError = validatePunchWizard({ ...view, tubeDesignerPunchWizard: part.wizard }, { ...state.part, length: metadata.length });
      if (validationError) throw new Error(validationError);
      const descriptorError = validateBatchDescriptorValues(part);
      if (descriptorError) throw new Error(descriptorError);
      const punchPayload = punchBatchPartPayload(part);
      return { part, metadata, request: { profileRef: metadata.profileRef, parameters: structuredClone(metadata.parameters),
        length: metadata.length, name: metadata.name, material: metadata.material, quantity: metadata.quantity,
        features: punchPayload.features, ends: punchPayload.ends } };
    });
    // Finish an already-running native preview before manufacturing starts.
    // The save lock is already held, and no queued intermediate edit is run.
    await previewQueue?.flight;
    await waitForPunchPreview(view);
    await waitForPaint();
    let response = null;
    for (let index = 0; index < requests.length; index++) {
      const item = requests[index]; failedPart = item.part;
      operation.message = "生成 " + item.metadata.name;
      operation.phaseLabel = (index + 1) + " / " + requests.length;
      operation.completed = index; operation.total = requests.length;
      ops.renderProject(context, view);
      response = await context.sceneProxy.invoke("TubeDesigner.AddNestingPunchPart", item.request, invocationOptions);
      if (!response?.tubeDesigner || !response.partEntityId) throw new Error("冲孔件未返回有效的下料记录。");
      // Record success before refreshing presentation. A later row failure or
      // refresh failure may be retried without recreating an accepted row.
      item.part.completedPartId = String(response.partEntityId);
      view.scene ??= {}; view.scene.tubeDesigner = response.tubeDesigner;
    }
    failedPart = null;
    await restoreSavedNestingTask(view, context);
    const createdPartIds = batch.parts.map(part => part.completedPartId).filter(Boolean);
    const partId = createdPartIds.at(-1);
    view.tubeDesignerNestingSelectedPartIds = createdPartIds;
    view.tubeDesignerActivePartId = partId;
    view.tubeDesignerActiveNestingPartId = partId;
    view.tubeDesignerNestingSelectionKind = "part";
    view.tubeDesignerActiveNestingPlacementId = "";
    view.tubeDesignerPartMeasurementState = null;
    view.tubeDesignerPartViewportKey = "";
    view.tubeDesignerNestingPunchPartDraft = null;
    view.tubeDesignerPunchWizard = null;
    view.tubeDesignerPunchBatch = null;
    view.tubeDesignerNestingStockDraft = null;
    view.tubeDesignerNestingSettingsSourceSignature = "";
    const totals = punchBatchTotals(batch);
    ops.showNotice?.(context, view, `已添加 ${totals.kinds} 种冲孔零件，共 ${totals.quantity} 件，可在零件清单中继续排样。`);
    await context.actions?.refreshActiveSceneState?.();
    return response;
  } catch (error) {
    const message = error?.message ?? String(error);
    const label = failedPart?.draft?.name || "未命名零件";
    batch.error = failedPart ? `${label}：${message}` : message;
    if (failedPart) {
      failedPart.draft.error = message;
      selectPunchBatchPart(view, failedPart.id);
    }
    throw error;
  } finally {
    if (operation) finishPunchOperation(view, operation);
    ops.renderProject(context, view);
  }
}

export async function handleNestingPunchPartAction(context, view, actionName, target, ops) {
  if (!actionName.startsWith(ACTION_PREFIX)) return { handled: false };
  if (view.pending) return { handled: true };
  const suffix = actionName.slice(ACTION_PREFIX.length);
  const batch = view.tubeDesignerPunchBatch;
  if (suffix === "batch-profile-advanced-open") {
    const profile = selectedProfile(view);
    if (!batch || !profile) return { handled: true };
    if (view.tubeDesignerPunchWizard?.parameterEditor?.inline) closePunchParameters(view, true, virtualPart(view), false);
    syncPunchBatch(view);
    const opener = target?.nodeType === 1 ? target : context.mount?.ownerDocument?.activeElement;
    openPunchBatchProfileAdvanced(view, profileSelectionKey(profile), profileParameters(view, profile), opener);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (suffix === "batch-profile-advanced-cancel") {
    closePunchBatchProfileAdvanced(view);
    ops.renderProject(context, view);
    return { handled: true };
  }
  if (suffix === "batch-profile-advanced-apply") {
    const editor = batch?.profileAdvancedEditor, profile = selectedProfile(view);
    if (!editor) return { handled: true };
    editor.error = !profile || profileSelectionKey(profile) !== editor.profileKey
      ? "当前管型已改变，请关闭高级设置后重新打开。" : validateProfileAdvancedDraft(profile, editor);
    if (editor.error) { ops.renderProject(context, view); return { handled: true }; }
    const changed = punchBatchProfileAdvancedChanged(view);
    if (changed) {
      initializeCreationInput(view);
      checkpointPunchWizard(view.tubeDesignerPunchWizard);
      view.tubeDesignerPunchWizard.creationInput.profileParameters[editor.profileKey] = structuredClone(editor.parameters);
    }
    closePunchBatchProfileAdvanced(view);
    if (changed) await previewCreation(context, view, ops);
    else ops.renderProject(context, view);
    return { handled: true };
  }
  if (batch?.profileAdvancedEditor) {
    // The nested modal is the only active edit scope. Its draft never reaches
    // stock synchronization or the native preview until the single apply step.
    if (suffix === "main-profile-parameter") {
      const editor = batch.profileAdvancedEditor, profile = selectedProfile(view);
      const key = String(target?.dataset?.tubeDesignerMainProfileParameter ?? "");
      const definition = profileAdvancedDefinitions(profile).find(item => item.key === key);
      if (profile && profileSelectionKey(profile) === editor.profileKey && definition
        && parameterVisible(definition, editor.parameters) && parameterEnabled(definition, editor.parameters)) {
        const valueType = definition.valueType ?? "number";
        const value = valueType === "boolean" ? !!target.checked : valueType === "string" ? String(target.value) : Number(target.value);
        if (updatePunchBatchProfileAdvanced(view, key, value)) ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  const definitionEditor = target?.closest?.("[data-punch-batch-definition-editor]");
  if (definitionEditor && view.tubeDesignerPunchBatch) {
    if (view.tubeDesignerPunchWizard?.parameterEditor?.inline) closePunchParameters(view, true, virtualPart(view), false);
    selectPunchBatchDefinition(view, definitionEditor.dataset.punchBatchDefinitionEditor);
    target = { value: target.value, checked: target.checked,
      dataset: { ...target.dataset, tubeDesignerPunchIndex: "draft" } };
  }
  if (suffix.startsWith("batch-")) {
    const batch = view.tubeDesignerPunchBatch;
    if (!batch) return { handled: true };
    const partId = String(target?.dataset?.punchBatchPartId ?? batch.selectedPartId);
    const definitionId = String(target?.dataset?.punchBatchDefinitionId ?? batch.selectedDefinitionId);
    const field = String(target?.dataset?.punchBatchField ?? "");
    if (view.tubeDesignerPunchWizard?.parameterEditor?.inline) closePunchParameters(view, true, virtualPart(view), false);
    syncPunchBatch(view);
    if (suffix === "batch-selection") {
      setPunchBatchSelection(view, partId, target?.checked === true);
      ops.renderProject(context, view); return { handled: true };
    }
    if (suffix === "batch-end-close") {
      batch.endEditorPartId = ""; batch.endEditorEnd = "";
      ops.renderProject(context, view); return { handled: true };
    }
    if (suffix === "batch-definition-collapse") {
      batch.definitionEditorCollapsed = true;
      ops.renderProject(context, view); return { handled: true };
    }
    if (suffix === "batch-change") {
      const switching = batch.selectedPartId !== partId;
      const row = batch.parts.find(part => part.id === partId);
      if (!row || row.completedPartId) return { handled: true };
      if (switching) selectPunchBatchPart(view, partId);
      const changed = updatePunchBatchPart(view, partId, field, target?.value);
      if (switching || changed && ["length", "lengthDatum"].includes(field)) await previewCreation(context, view, ops, { quiet: true });
      else ops.renderProject(context, view);
      return { handled: true };
    }
    if (suffix === "batch-select" || suffix === "batch-end-open") {
      if (!selectPunchBatchPart(view, partId)) return { handled: true };
      if (suffix === "batch-end-open") {
        const end = String(target?.dataset?.tubeDesignerPunchEnd ?? "start");
        batch.endEditorPartId = partId; batch.endEditorEnd = end;
        if (view.tubeDesignerPunchWizard.ends?.[end]?.type !== "keep") togglePunchRowParameters(view, end, end, "shape");
      }
      await previewCreation(context, view, ops, { quiet: true });
      return { handled: true };
    }
    if (suffix === "batch-add") { addPunchBatchPart(view); initializePunchRecordSource(view); }
    else if (suffix === "batch-copy") copyPunchBatchParts(view);
    else if (suffix === "batch-remove") removePunchBatchParts(view);
    else if (suffix === "batch-paste") pastePunchBatchParts(view, partId, field, target?.value);
    else if (suffix === "batch-enter") advancePunchBatchPart(view, partId, field);
    else if (suffix === "batch-definition-select") selectPunchBatchDefinition(view, definitionId);
    else if (suffix === "batch-definition-add") addPunchBatchDefinition(view);
    else if (suffix === "batch-definition-remove") removePunchBatchDefinition(view, definitionId);
    else if (suffix === "batch-hole-definition") {
      const index = String(target?.dataset?.tubeDesignerPunchIndex ?? "draft");
      const featureId = target?.dataset?.punchBatchDefinitionFeatureId
        ?? (index === "draft" ? "draft" : view.tubeDesignerPunchWizard.features?.[Number(index)]?.id);
      setPunchBatchHoleDefinition(view, featureId, String(target?.value ?? ""));
    } else return { handled: false };
    await previewCreation(context, view, ops, { includeDraft: suffix.startsWith("batch-definition-"), quiet: true });
    return { handled: true };
  }
  if(handlePunchDiagramAction(view,suffix,target)) {
    ops.renderProject(context,view);return {handled:true};
  }
  if(handlePunchColumnAction(view,suffix,target)) {
    ops.renderProject(context,view);return {handled:true};
  }
  if(view.tubeDesignerPunchWizard?.parameterEditor?.inline
    &&(target?.dataset?.punchSheetField!==undefined||target?.dataset?.punchSheetArrayField!==undefined))
    closePunchParameters(view,true,virtualPart(view),false);
  // Moving to another region finishes the live row edit before its next action.
  if(view.tubeDesignerPunchWizard?.parameterEditor?.inline
      && ["draft-change","main-profile-parameter","remove-selected","edit","copy","toggle","undo","redo","remove-end"].includes(suffix))
    closePunchParameters(view,true,virtualPart(view),false);
  if(suffix==="preview-mode") {
    if(view.tubeDesignerPunchWizard)view.tubeDesignerPunchWizard.previewMode="tools";
    ops.renderProject(context,view);return {handled:true};
  }
  if(suffix==="parameters-open") {
    const editor=view.tubeDesignerPunchWizard?.parameterEditor;
    if(target?.dataset?.punchCellActivate!==undefined && editor?.inline
      && editor.index===String(target.dataset.tubeDesignerPunchIndex)
      && editor.end===(target.dataset.tubeDesignerPunchEnd??'')
      && editor.mode===(target.dataset.tubeDesignerPunchEditorMode??'shape'))return {handled:true};
    if(target?.dataset?.tubeDesignerPunchInline!==undefined) {
      const revision=view.tubeDesignerPunchWizard?.revision;
      togglePunchRowParameters(view,target?.dataset?.tubeDesignerPunchIndex,target?.dataset?.tubeDesignerPunchEnd,target?.dataset?.tubeDesignerPunchEditorMode);
      if(revision!==view.tubeDesignerPunchWizard?.revision)await previewCreation(context,view,ops,{includeDraft:view.tubeDesignerPunchWizard?.parameterEditor?.index==="draft",quiet:true});
      else ops.renderProject(context,view);return {handled:true};
    }
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
    if(s?.parameterEditor?.inline)closePunchParameters(view,true,virtualPart(view),false);
    const index=s?.features?.findIndex(item=>item.id===s.selectedFeatureId)??-1;
    const changed=Number.isInteger(index)&&index>=0&&editPunchWizardFeature(view,"copy",index);
    if(changed){s.showDraftRow=true;togglePunchRowParameters(view,'draft');}
    if(changed)await previewCreation(context,view,ops,{includeDraft:true,quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if(suffix === "parameters-close") {
    const includeDraft=view.tubeDesignerPunchWizard?.parameterEditor?.index==="draft";
    // The parameter window is live: its only visible action is close, which
    // commits the already-previewed values into one undoable edit.
    if(closePunchParameters(view,true,virtualPart(view),false))await previewCreation(context,view,ops,{includeDraft,quiet:true});
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
    await previewCreation(context,view,ops,{includeDraft:true});
    return {handled:true};
  }
  if (suffix === "open") {
    defaultDraft(view);
    rememberBatchOpener(context, view, target);
    await previewCreation(context, view, ops);
    return { handled: true };
  }
  if (suffix === "cancel") {
    view.tubeDesignerNestingPunchPartDraft = null;
    view.tubeDesignerPunchWizard = null;
    view.tubeDesignerPunchBatch = null;
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
    const changed = await updatePunchProfileParameter(context,view,target,ops);
    if(changed&&(!view.tubeDesignerPunchBatch||!view.pending))await previewCreation(context,view,ops,{includeDraft:String(target?.dataset?.tubeDesignerPunchIndex??"draft")==="draft",quiet:true});
    else ops.renderProject(context,view);
    return {handled:true};
  }
  if (suffix === "field-change") {
    if (updatePunchWizardField(view, target)) {
      const end = target?.dataset?.tubeDesignerPunchEnd;
      if (end && target?.dataset?.tubeDesignerPunchField === "tool" && view.tubeDesignerNestingPunchPartDraft?.lengthDatum) {
        view.tubeDesignerPunchWizard.ends[end].datum = view.tubeDesignerNestingPunchPartDraft.lengthDatum;
      }
      const row = target?.dataset?.tubeDesignerPunchIndex;
      await previewCreation(context, view, ops, {
        includeDraft: row === undefined || row === "" || row === "draft", quiet: true,
      });
    } else ops.renderProject(context, view);
    return { handled: true };
  }
  if (suffix === "add") {
    if(view.tubeDesignerPunchWizard?.parameterEditor?.inline)closePunchParameters(view,true,virtualPart(view),false);
    const state = ensureCreationState(view);
    if (state && addPunchWizardFeature(view, state.part)) {
      view.tubeDesignerPunchWizard.showDraftRow=false;
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
  if (suffix === "apply") {
    if(view.tubeDesignerPunchWizard?.parameterEditor?.inline)closePunchParameters(view,true,virtualPart(view),false);
    return { handled: true, result: await confirmNestingPunchPart(context, view, ops) };
  }
  return { handled: false };
}

export async function handleNestingPunchPartRibbonCommand(context, view, commandId, ops) {
  if (commandId !== "nesting.add-punch-part") return false;
  if(view.pending)return true;
  defaultDraft(view);
  rememberBatchOpener(context, view);
  await previewCreation(context, view, ops);
  return true;
}

function waitForPaint() {
  const requestFrame = globalThis.requestAnimationFrame;
  if (typeof requestFrame !== "function") return Promise.resolve();
  return new Promise((resolve) => requestFrame(() => requestFrame(resolve)));
}

export { NEW_PART_ID };
