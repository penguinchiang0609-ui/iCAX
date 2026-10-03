// Product bindings are separate from catalogue examples. Only a current native
// candidate can authorize a commit; front-end topology guesses never do.
import { productAssemblyConnectionsState, productAssemblyManufacturingRevision } from "./productAssemblyConnections.mjs";
import { matchesParameterCondition } from "./parameterConditions.mjs";
import { assemblyBindingAngleDefinition, assemblyBindingAxisAngle, assemblyBindingParameterValues } from "./finishedProductModel.mjs";
const escapeHtml = (value) => String(value ?? "").replace(/[&<>"']/g, (c) => ({
  "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
})[c]);

export function productAssemblyBindingIdentity(view) {
  const designer = view?.scene?.tubeDesigner ?? {};
  const productEntityId = String(designer.activeProductId || designer.product?.entityId || "");
  const generationRunId = String(designer.generationRun?.entityId || designer.product?.activeGenerationRunId || "");
  const outdated = !!designer.product?.modelOutdated;
  const partsOutdated = !!designer.product?.partsOutdated;
  const productKey = productEntityId && generationRunId ? `${productEntityId}/${generationRunId}` : "";
  return { productEntityId, generationRunId, outdated, productKey,
    key: productKey ? `${productKey}/${outdated}/${partsOutdated}/${designer.product?.assemblyBindingsRevision ?? ""}/${productAssemblyManufacturingRevision(view)}` : "" };
}

export function productAssemblyBindingsState(view) {
  return view.tubeDesignerProductAssemblyBindings ??= {
    key: "", productKey: "", status: "idle", result: null, error: "", promise: null,
    drafts: {}, mutation: null, notice: "", epoch: 0,
  };
}

export function productAssemblyBindingDraft(view, template) {
  const state = productAssemblyBindingsState(view);
  return state.drafts[template?.id ?? ""] ??= {
    connectionKey: "", participants: {}, preview: null, previewKey: "",
    previewStatus: "idle", previewError: "", revision: 0, replaceBindingId: "", autoAssignedConnectionKey: "",
  };
}

export function invalidateProductAssemblyCandidate(view, templateId) {
  const draft = productAssemblyBindingsState(view).drafts[templateId];
  if (!draft) return;
  draft.preview = null;
  draft.previewKey = "";
  draft.previewStatus = "idle";
  draft.previewError = "";
  draft.revision += 1;
}

function nativeMembers(result) { return Array.isArray(result?.members) ? result.members : []; }
function memberId(member) { return String(member?.memberEntityId ?? ""); }
function memberName(member) { return member?.name || "未命名构件"; }
function nodeIndexForRole(role, fallbackIndex) {
  const letter = /^member([A-D])$/i.exec(String(role ?? ""))?.[1]?.toUpperCase();
  return letter ? letter.charCodeAt(0) - 65 : fallbackIndex;
}
function anchorOptions(result, template, role) {
  const declared = template?.productBinding?.anchors?.[role];
  const supported = (result?.capabilities?.anchorKinds ?? []).filter((kind) => !declared || kind === declared);
  return [
    ...(supported.includes("end") ? [{ id: "start", label: "起端" }, { id: "end", label: "末端" }] : []),
    ...(supported.includes("side") ? ["top", "bottom", "left", "right"].map((face) => ({
      id: `side:${face}`, label: `${({ top: "上", bottom: "下", left: "左", right: "右" })[face]}侧面`, requiresStation: true,
    })) : []),
  ];
}
function draftParticipant(draft, role) { return draft.participants[role] ?? { memberEntityId: "", anchorId: "", station: "" }; }
function currentResult(view) {
  const state = productAssemblyBindingsState(view);
  return state.key === productAssemblyBindingIdentity(view).key && ["ready", "refreshing"].includes(state.status) ? state.result : null;
}
function displayedResult(view) {
  const state = productAssemblyBindingsState(view);
  // A revision refresh must keep the same product's editor nodes alive until
  // the fresh list arrives. These values cannot authorize an apply.
  return state.productKey === productAssemblyBindingIdentity(view).productKey ? state.result : null;
}
function connections(view, result) {
  const connectionState = productAssemblyConnectionsState(view);
  const source = connectionState.status === "ready" ? connectionState.result : null;
  const identity = productAssemblyBindingIdentity(view);
  return result?.connections ?? (source?.productEntityId === identity.productEntityId
    && source?.generationRunId === identity.generationRunId ? source.connections : []) ?? [];
}

export function productAssemblyTemplateMatchesTopology(template, parameters, connection) {
  const topology = connection?.properties?.topology;
  const entries = template?.productBinding?.compatibleProductTopologies;
  return typeof topology === "string" && topology.length > 0 && Array.isArray(entries)
    && entries.some((entry) => entry?.topology === topology
      && matchesParameterCondition(entry.when, parameters));
}

function renderCurrent(context, view, ops) {
  if (view.activeAreaId === "assemblies") ops?.renderProject?.(context, view);
}

export function ensureProductAssemblyBindings(context, view, ops, force = false) {
  const identity = productAssemblyBindingIdentity(view);
  const state = productAssemblyBindingsState(view);
  if (state.key !== identity.key) {
    const sameProduct = state.productKey === identity.productKey;
    if (sameProduct) for (const templateId of Object.keys(state.drafts)) invalidateProductAssemblyCandidate(view, templateId);
    Object.assign(state, { key: identity.key, productKey: identity.productKey, status: "idle", result: sameProduct ? state.result : null,
      promise: null, error: "", drafts: sameProduct ? state.drafts : {}, mutation: sameProduct ? state.mutation : null, notice: sameProduct ? state.notice : "" });
    state.epoch += 1;
  }
  if (!identity.key || typeof context?.sceneProxy?.invoke !== "function") return Promise.resolve();
  if (!force && state.status !== "idle") return state.promise ?? Promise.resolve();
  const epoch = ++state.epoch;
  state.status = state.result ? "refreshing" : "loading";
  state.error = "";
  const request = context.sceneProxy.invoke("TubeDesigner.GetProductAssemblyBindings", {
    productEntityId: identity.productEntityId, generationRunId: identity.generationRunId,
  }, { timeoutMs: 30000 }).then((result) => {
    if (state.epoch !== epoch || productAssemblyBindingIdentity(view).key !== identity.key) return;
    if (result?.productEntityId !== identity.productEntityId || result?.generationRunId !== identity.generationRunId
      || !Array.isArray(result.members) || !Array.isArray(result.bindings)) throw new Error("产品工艺数据与当前生成版本不一致，请重新读取。");
    if (result.bindings.some((binding) => !binding?.connectionKey)) throw new Error("产品工艺缺少连接标识，无法读取。");
    state.result = result;
    state.status = "ready";
    renderCurrent(context, view, ops);
  }).catch((error) => {
    if (state.epoch !== epoch || productAssemblyBindingIdentity(view).key !== identity.key) return;
    state.status = "error";
    state.error = error?.message ?? String(error);
    renderCurrent(context, view, ops);
  }).finally(() => { if (state.promise === request) state.promise = null; });
  state.promise = request;
  if (state.status === "refreshing") renderCurrent(context, view, ops);
  return request;
}

function requestParticipants(view, input) {
  const draft = productAssemblyBindingDraft(view, input.template);
  const result = currentResult(view);
  if (!draft.connectionKey) throw new Error("请先选择当前产品已提交的真实连接。");
  const connection = connections(view, result).find((item) => item.key === draft.connectionKey);
  if (!connection) throw new Error("所选产品连接不属于当前生成版本，请重新选择。");
  const angleDefinition = assemblyBindingAngleDefinition(input.template);
  const actualAngle = assemblyBindingAxisAngle(input.template, connection);
  if (input.template.productBinding?.axisAngle && (actualAngle == null
      || angleDefinition && (!Number.isFinite(input.parameters?.[angleDefinition.key])
        || Math.abs(input.parameters[angleDefinition.key] - actualAngle) > 1e-6)))
    throw new Error("当前成品节点轴夹角未核验，或不在该装配工艺适用范围内。");
  if (!productAssemblyTemplateMatchesTopology(input.template, input.parameters, connection))
    throw new Error("装配工艺与此处成品连接形状不兼容，请改选工艺或连接。");
  const validMembers = new Set(nativeMembers(result).map(memberId));
  const used = new Set();
  const participants = input.template.participants.map((role) => {
    const selection = draftParticipant(draft, role.role);
    const validAnchors = new Set(anchorOptions(result, input.template, role.role).map((anchor) => anchor.id));
    if (!validMembers.has(selection.memberEntityId) || !validAnchors.has(selection.anchorId))
      throw new Error(`请为“${role.label || role.role}”选择当前产品构件和加工位置。`);
    if (used.has(selection.memberEntityId)) throw new Error("不同工艺角色请选择不同构件。");
    used.add(selection.memberEntityId);
    const side = selection.anchorId.startsWith("side:");
    const station = Number(selection.station);
    if (side && (!String(selection.station).trim() || !Number.isFinite(station) || station < 0))
      throw new Error(`请填写“${role.label || role.role}”距起端的有效位置。`);
    const committedPart = (connection.participants ?? []).find((part) => memberId(part) === selection.memberEntityId);
    const declaration = connection.properties?.participantAnchors?.find((item) => item.itemKey === committedPart?.itemKey);
    const committedAnchor = declaration?.anchor ?? declaration;
    return { role: role.role, memberEntityId: selection.memberEntityId,
      anchor: side ? { ...selection.anchorBase, kind: "side", face: selection.anchorId.slice(5), reference: selection.anchorBase?.reference ?? "start", station }
        : { ...selection.anchorBase, kind: "end", end: selection.anchorId,
          ...(committedAnchor?.kind === "end" && committedAnchor?.end === selection.anchorId
            && committedAnchor.approachFace ? { approachFace: committedAnchor.approachFace } : {}) } };
  });
  const expected = (connection.participants ?? []).map(memberId);
  const actual = participants.map((part) => part.memberEntityId);
  if (expected.length !== actual.length || expected.some((id) => !actual.includes(id)))
    throw new Error("所选产品连接与工艺角色的真实构件不一致，请重新选择连接和构件。");
  return participants;
}

function participantDraft(participant) {
  const anchor = participant.anchor ?? {};
  return { memberEntityId: participant.memberEntityId,
    anchorId: anchor.kind === "side" ? `side:${anchor.face}` : anchor.end ?? "",
    station: anchor.kind === "side" ? String(anchor.station ?? "") : "", anchorBase: structuredClone(anchor) };
}

export async function handleProductAssemblyBindingAction(context, view, action, target, ops, getInput) {
  if (!action.startsWith("tube-designer-binding-")) return { handled: false };
  const state = productAssemblyBindingsState(view);
  const input = getInput();
  if (!input?.template) return { handled: true };
  const identity = productAssemblyBindingIdentity(view);
  const draft = productAssemblyBindingDraft(view, input.template);
  const kind = action.slice("tube-designer-binding-".length);
  if (kind === "reload") {
    await ensureProductAssemblyBindings(context, view, ops, true);
    return { handled: true };
  }
  const result = currentResult(view);
  if (!result) return { handled: true };
  if (["connection", "member", "anchor", "station", "cancel-edit"].includes(kind)) {
    if (kind === "connection") {
      draft.connectionKey = String(target?.value ?? "");
      draft.participants = {};
      draft.autoAssignedConnectionKey = "";
      const library = view.tubeDesignerAssemblyLibrary;
      if (library) {
        library.selectedProductConnectionKey = draft.connectionKey;
        library.selectedProductConnectionProductKey = identity.productKey;
        library.selectedProductTemplateConnectionKey = "";
        library.selectedProductTemplateId = "";
      }
    }
    else if (kind === "cancel-edit") { draft.replaceBindingId = ""; }
    else {
      const role = String(target?.dataset?.tubeBindingRole ?? "");
      if (!input.template.participants.some((item) => item.role === role)) return { handled: true };
      const selection = { ...draftParticipant(draft, role) };
      if (kind === "member") { selection.memberEntityId = String(target?.value ?? ""); selection.anchorId = ""; selection.anchorBase = {}; }
      if (kind === "anchor") { selection.anchorId = String(target?.value ?? ""); selection.anchorBase = {}; }
      if (kind === "station") selection.station = String(target?.value ?? "");
      draft.participants[role] = selection;
    }
    invalidateProductAssemblyCandidate(view, input.template.id);
    state.notice = "";
    renderCurrent(context, view, ops);
    return { handled: true };
  }
  if (state.mutation) return { handled: true };
  if (kind === "edit") {
    const binding = result.bindings.find((item) => item.bindingId === target?.dataset?.tubeBindingId);
    const templates = view.tubeDesignerAssemblyTemplates ?? [];
    const template = templates.find((item) => item.id === binding?.templateId);
    if (binding?.connectionKey && template) {
      const library = view.tubeDesignerAssemblyLibrary;
      library.selectedId = template.id;
      library.selectedProductTemplateId = template.id;
      library.selectedProductTemplateConnectionKey = String(binding.connectionKey ?? "");
      library.parameterDrafts[template.id] = structuredClone(binding.parameters ?? {});
      library.processDrafts[template.id] = structuredClone(binding.processDrafts ?? {});
      const edit = productAssemblyBindingDraft(view, template);
      edit.connectionKey = String(binding.connectionKey ?? "");
      library.selectedProductConnectionKey = edit.connectionKey;
      library.selectedProductConnectionProductKey = identity.productKey;
      edit.replaceBindingId = binding.bindingId;
      edit.participants = Object.fromEntries((binding.participants ?? []).map((part) =>
        [part.role, participantDraft(part)]));
      edit.autoAssignedConnectionKey = edit.connectionKey;
      invalidateProductAssemblyCandidate(view, template.id);
      state.notice = "正在修改已绑定工艺。重新检查后更新，原加工在提交前保持不变。";
      renderCurrent(context, view, ops);
    }
    return { handled: true };
  }
  if (kind === "preview") {
    const key = productAssemblyBindingInputKey(view, input);
    let participants;
    try {
      if (identity.outdated || result.modelOutdated) throw new Error("产品模型已变化，请重新生成后再检查加工方案。");
      participants = requestParticipants(view, input);
    } catch (error) {
      draft.previewError = error.message;
      draft.preview = null;
      draft.previewStatus = "error";
      renderCurrent(context, view, ops);
      return { handled: true };
    }
    const revision = ++draft.revision;
    draft.previewStatus = "loading";
    draft.previewError = "";
    draft.previewKey = key;
    state.notice = "";
    renderCurrent(context, view, ops);
    try {
      const candidate = await context.sceneProxy.invoke("TubeDesigner.PreviewProductAssemblyBinding", {
        productEntityId: identity.productEntityId, generationRunId: identity.generationRunId,
        templateId: input.template.id, parameters: assemblyBindingParameterValues(input.template, input.parameters), processDrafts: input.processDrafts,
        connectionKey: draft.connectionKey, participants,
        ...(draft.replaceBindingId ? { replaceBindingId: draft.replaceBindingId } : {}),
      }, { timeoutMs: 120000 });
      if (productAssemblyBindingIdentity(view).key !== identity.key || draft.revision !== revision) return { handled: true };
      if (productAssemblyBindingInputKey(view, getInput()) !== key) {
        draft.previewStatus = "idle";
        renderCurrent(context, view, ops);
        return { handled: true };
      }
      draft.preview = candidate;
      draft.previewStatus = "ready";
    } catch (error) {
      if (productAssemblyBindingIdentity(view).key !== identity.key || draft.revision !== revision) return { handled: true };
      draft.previewStatus = "error";
      draft.previewError = error?.message ?? String(error);
    }
    renderCurrent(context, view, ops);
    return { handled: true };
  }
  if (kind === "apply" || kind === "remove") {
    if (kind === "apply" && !productAssemblyCandidateCanApply(view, input)) return { handled: true };
    const bindingId = target?.dataset?.tubeBindingId;
    if (kind === "remove" && !result.bindings.some((item) => item.bindingId === bindingId && item.canRemove !== false)) return { handled: true };
    const mutation = { kind, identity: identity.key };
    state.mutation = mutation;
    state.notice = kind === "apply" ? "正在更新实际制造件…" : "正在移除工艺并重新计算制造件…";
    renderCurrent(context, view, ops);
    let committed = false;
    try {
      const response = await context.sceneProxy.invoke(`TubeDesigner.${kind === "apply" ? "Apply" : "Remove"}ProductAssemblyBinding`, {
        productEntityId: identity.productEntityId, generationRunId: identity.generationRunId,
        ...(kind === "apply" ? { candidateToken: draft.preview.candidateToken } : { bindingId }),
      }, { timeoutMs: 120000 });
      if (productAssemblyBindingIdentity(view).key !== identity.key || state.mutation !== mutation) return { handled: true };
      if (response?.[kind === "apply" ? "applied" : "removed"] !== true)
        throw new Error(response?.detail || response?.message || "未收到加工更新成功的确认，请重新检查。");
      committed = true;
      const next = response?.tubeDesigner;
      if (!next || String(next.activeProductId || next.product?.entityId || "") !== identity.productEntityId
        || String(next.generationRun?.entityId || next.product?.activeGenerationRunId || "") !== identity.generationRunId)
        throw new Error("未返回当前产品的制造件结果，请刷新产品。");
      const changedMembers = new Set((draft.preview?.geometryChanges ?? [])
        .map((change) => String(change.memberEntityId ?? "")).filter(Boolean));
      const connectionOnly = kind === "apply" && draft.preview?.manufacturingEffect === "connection-only";
      const changedPart = kind === "apply" ? (next.parts ?? []).find((part) => changedMembers.has(String(part.sourceMemberId ?? ""))
        && part.entityId && part.thumbnailGeometryResourceId) : null;
      view.scene.tubeDesigner = next;
      if (changedPart && view.tubeDesignerAssemblyLibrary?.workMode === "product") {
        view.tubeDesignerAssemblyLibrary.exploded = true;
        view.tubeDesignerAssemblyLibrary.productPartId = String(changedPart.entityId);
      }
      for (const templateId of Object.keys(state.drafts)) invalidateProductAssemblyCandidate(view, templateId);
      if (kind === "apply" || draft.replaceBindingId === bindingId) draft.replaceBindingId = "";
      state.notice = kind === "apply" ? connectionOnly
        ? "连接已记录，装配位置待核对。"
        : changedPart
          ? `单件加工已应用，正显示“${changedPart.partNumber || changedPart.name || "已加工下料件"}”；装配位置待核对。`
          : "单件加工已应用；请切换中间的“已加工下料件”查看结果，装配位置待核对。"
        : response.manufacturingUpdated === true ? "工艺已移除，实际制造件已重新计算。"
          : response.detail || "工艺已移除，现有下料仍需重新生成。";
      const connectionState = productAssemblyConnectionsState(view);
      connectionState.status = "idle";
      await ensureProductAssemblyBindings(context, view, ops, true);
      if (typeof context.actions?.refreshActiveSceneState === "function") {
        view.tubeDesignerOwnMutation = true;
        try { await context.actions.refreshActiveSceneState(); }
        finally { view.tubeDesignerOwnMutation = false; }
      }
    } catch (error) {
      if (productAssemblyBindingIdentity(view).productKey !== identity.productKey || state.mutation !== mutation) return { handled: true };
      state.notice = `${committed ? "加工已提交，但结果读取失败" : `${kind === "apply" ? "应用" : "移除"}失败`}：${error?.message ?? error}`;
      invalidateProductAssemblyCandidate(view, input.template.id);
    } finally {
      if (state.mutation === mutation) { state.mutation = null; renderCurrent(context, view, ops); }
    }
  }
  return { handled: true };
}
function fieldData(action, role) {
  return `data-cam-change-action="tube-designer-binding-${action}" data-tube-binding-role="${escapeHtml(role)}"`;
}
function optionsHtml(options, value) {
  return options.map((option) => `<option value="${escapeHtml(option.id)}"${String(value) === String(option.id) ? " selected" : ""}>${escapeHtml(option.label)}</option>`).join("");
}

function renderParticipants(view, result, template, draft, disabled) {
  const members = nativeMembers(result);
  const connection = connections(view, result).find((item) => item.key === draft.connectionKey);
  const connectionIds = new Set((connection?.participants ?? []).map(memberId));
  const available = connection ? members.filter((member) => connectionIds.has(memberId(member))) : [];
  // A committed node supplies the actual members. Lettered roles keep their
  // node order even when the template lists its participants in another order.
  if (connection && draft.autoAssignedConnectionKey !== draft.connectionKey) {
    template.participants.forEach((role, index) => {
      const part = connection.participants[nodeIndexForRole(role.role, index)];
      if (!part || !available.some((member) => memberId(member) === memberId(part))) return;
      const selection = draftParticipant(draft, role.role);
      if (selection.memberEntityId) return;
      const declared = connection.properties?.participantAnchors?.find((anchor) => anchor.itemKey === part.itemKey);
      const placement = declared?.anchor ?? declared;
      const anchorId = placement?.kind === "end" ? placement.end
        : placement?.kind === "side" ? `side:${placement.face}` : "";
      const allowed = anchorOptions(result, template, role.role).some((anchor) => anchor.id === anchorId);
      draft.participants[role.role] = { ...selection, memberEntityId: memberId(part),
        ...(allowed ? { anchorId, station: placement.kind === "side"
          ? String(placement.station ?? declared?.localAxialStation ?? "") : "",
          anchorBase: structuredClone(placement) } : {}) };
    });
    draft.autoAssignedConnectionKey = draft.connectionKey;
  }
  return template.participants.map((role, index) => {
    const selection = draftParticipant(draft, role.role);
    const member = available.find((item) => memberId(item) === selection.memberEntityId);
    const anchors = anchorOptions(result, template, role.role);
    const anchor = anchors.find((item) => item.id === selection.anchorId);
    const station = anchor?.requiresStation === true || selection.anchorId === "middle";
    return `<fieldset class="tube-assembly-binding-role" id="tube-binding-role-${escapeHtml(role.role)}"><legend>${escapeHtml(`${String.fromCharCode(65 + nodeIndexForRole(role.role, index))} · ${role.label || role.role}${member ? ` · ${memberName(member)}` : ""}`)}</legend>
      <label class="tube-designer-field"><span>真实构件</span><select ${fieldData("member", role.role)}${disabled || !connection ? " disabled" : ""}>${optionsHtml([{ id: "", label: "选择构件" }, ...available.map((item) => ({ id: memberId(item), label: memberName(item) }))], selection.memberEntityId)}</select></label>
      <label class="tube-designer-field"><span>加工位置</span><select ${fieldData("anchor", role.role)}${disabled || !member ? " disabled" : ""}>${optionsHtml([{ id: "", label: "选择端部或侧面" }, ...anchors], selection.anchorId)}</select></label>
      ${station ? `<label class="tube-designer-field"><span>距起端位置 <small>mm</small></span><input type="text" inputmode="decimal" value="${escapeHtml(selection.station)}" ${fieldData("station", role.role)}${disabled ? " disabled" : ""}></label>` : ""}
      <small>${member ? escapeHtml([member.profileLabel || member.profile?.displayName, Number.isFinite(member.length) ? `长度 ${member.length} mm` : "", member.supportDetail].filter(Boolean).join(" · ") || "选择后检查加工方案") : "按工艺角色指定当前产品中的构件。"}</small>
    </fieldset>`;
  }).join("");
}

function renderSupport(result, template, parameters) {
  const support = result?.capabilities;
  const templateSupport = support?.templates?.find((item) => item.templateId === template.id);
  const unsupportedBranch = template.productBinding?.unsupportedBranches?.find((branch) => matchesParameterCondition(branch.when, parameters));
  const activeTopologies = template.productBinding?.compatibleProductTopologies?.filter((entry) =>
    matchesParameterCondition(entry.when, parameters)) ?? [];
  const unavailable = template.productBinding?.mode !== "incremental-cut" || templateSupport?.supported !== true
    || !!unsupportedBranch || activeTopologies.length === 0;
  const integrated = template.manufacturingPlan?.realization === "integrated";
  const detail = unsupportedBranch?.reason || (integrated ? "此路线需要将两段逻辑管件合并为一根母材下料，当前不能作为两件独立下料的增量加工应用。" : "")
    || (activeTopologies.length === 0 && template.productBinding?.mode === "incremental-cut"
      ? "当前工艺参数未声明可适用的成品连接形状。" : "")
    || templateSupport?.detail || template.productBinding?.reason || support?.detail;
  return `<div class="tube-assembly-binding-support${unavailable ? " is-unsupported" : ""}"><strong>${unavailable ? "当前方式暂不支持产品应用" : "可用加工"}</strong><p>${escapeHtml(detail || "请检查加工方案，确认该工艺是否适用。")}</p></div>`;
}

export function productAssemblyBindingInputKey(view, input) {
  const draft = productAssemblyBindingDraft(view, input.template);
  return JSON.stringify([productAssemblyBindingIdentity(view).key, input.template?.id, input.template?.version,
    assemblyBindingParameterValues(input.template, input.parameters), input.processDrafts, draft.connectionKey, draft.participants, draft.replaceBindingId,
    displayedResult(view)?.sourceRevision]);
}

export function productAssemblyCandidateCanApply(view, input) {
  const state = productAssemblyBindingsState(view);
  const draft = productAssemblyBindingDraft(view, input.template);
  const result = currentResult(view);
  const connection = connections(view, result).find((item) => item.key === draft.connectionKey);
  const actual = (input.template?.participants ?? []).map((role) => draftParticipant(draft, role.role).memberEntityId);
  const expected = (connection?.participants ?? []).map(memberId);
  return !!connection && expected.length === actual.length && expected.every((id) => actual.includes(id))
    && productAssemblyTemplateMatchesTopology(input.template, input.parameters, connection)
    && state.status === "ready" && !state.mutation && !productAssemblyBindingIdentity(view).outdated
    && draft.previewStatus === "ready" && draft.previewKey === productAssemblyBindingInputKey(view, input)
    && draft.preview?.canApply === true && draft.preview?.supportStatus !== "unsupported"
    && !currentResult(view)?.modelOutdated && !!draft.preview?.candidateToken;
}

function renderCandidate(view, input) {
  const draft = productAssemblyBindingDraft(view, input.template);
  const current = draft.previewKey === productAssemblyBindingInputKey(view, input);
  const candidate = current ? draft.preview : null;
  const checks = candidate?.checks ?? [];
  const checkRows = checks.map((check, index) => `<li id="tube-binding-check-${index}" data-status="${escapeHtml(check.status)}"><strong>${escapeHtml(check.label || ({ pass: "通过", warning: "提示", failed: "不通过", unsupported: "不支持", conflict: "冲突", "not-verified": "待核对", "requires-native-validation": "待检查" })[check.status] || "待核对")}</strong><span>${escapeHtml(check.detail || check.message || "未提供校验说明")}</span></li>`).join("");
  const message = draft.previewStatus === "loading" ? "正在检查加工方案与整件几何…"
    : draft.previewError || (!candidate ? "选择构件、加工位置并填写参数后，检查加工方案。" : candidate.detail || (candidate.canApply ? "单件加工可执行，装配位置待核对。" : typeof candidate.summary === "string" ? candidate.summary : "当前方案不能应用，请查看检查结果。"));
  const partialNotice = candidate?.manufacturingEffect === "incremental-cut"
    ? '<p class="tube-assembly-binding-warning">本次仅新增单件减料加工，装配位置和配合仍需核对。</p>' : "";
  const audit = wholeGeometryAuditLabel(candidate?.wholeGeometryAudit);
  const nodeAudit = nodeFinalAuditLabel(candidate?.nodeFinalAudit);
  return `<section class="tube-assembly-binding-candidate" id="tube-binding-candidate"><strong>4 · 检查加工方案</strong><p role="status">${escapeHtml(message)}</p>${audit ? `<p id="tube-binding-candidate-whole-audit" role="status">${escapeHtml(audit)}</p>` : ""}${nodeAudit ? `<p id="tube-binding-candidate-node-audit" role="status">${escapeHtml(nodeAudit)}</p>` : ""}${partialNotice}${candidate?.supportDetail ? `<p>${escapeHtml(candidate.supportDetail)}</p>` : ""}${candidate ? renderCandidateWorkflow(view, input, candidate) : ""}${checkRows ? `<ul>${checkRows}</ul>` : ""}
    <div class="tube-assembly-binding-actions"><button type="button" data-cam-action="tube-designer-binding-preview"${draft.previewStatus === "loading" || productAssemblyBindingsState(view).mutation || !currentResult(view) ? " disabled" : ""}>检查加工方案</button><button type="button" class="primary" data-cam-action="tube-designer-binding-apply"${productAssemblyCandidateCanApply(view, input) ? "" : " disabled"}>${draft.replaceBindingId ? "更新已绑定工艺" : "应用到产品"}</button></div></section>`;
}

function renderCandidateWorkflow(view, input, candidate) {
  if (candidate.manufacturingEffect === "connection-only") return "";
  const workflow = candidate.workflow ?? {};
  const draft = productAssemblyBindingDraft(view, input.template);
  const members = nativeMembers(displayedResult(view));
  const participantName = (role) => memberName(members.find((member) => memberId(member) === draft.participants[role]?.memberEntityId));
  const operations = workflow.partOperations ?? workflow.operations ?? [];
  const rolesOf = (operation) => operation.participantRoles ?? (operation.role ? [operation.role] : []);
  const changedRoles = new Set(operations.flatMap(rolesOf));
  const changedMembers = new Set(operations.map((operation) => operation.memberEntityId).filter(Boolean));
  const blankParts = (workflow.blankParts ?? workflow.parts ?? []).filter((part) => !operations.length
    || changedMembers.has(part.memberEntityId) || (part.participantRoles ?? []).some((role) => changedRoles.has(role)));
  const placementText = (placement = {}) => {
    if (placement.end) return placement.end === "start" ? "起端" : "末端";
    const face = ({ top: "上侧面", bottom: "下侧面", left: "左侧面", right: "右侧面" })[placement.face];
    return [face, Number.isFinite(placement.station) ? `距${placement.reference === "end" ? "末" : "起"}端 ${placement.station} mm` : "",
      Number(placement.arrayCount) > 1 ? `${placement.arrayCount} 处，间距 ${placement.arrayPitch} mm` : ""].filter(Boolean).join(" · ");
  };
  const groups = [
    ["影响零件", blankParts.map((part) => [part.label || part.name || "下料件", (part.participantRoles ?? []).map(participantName).join(" + ")].filter(Boolean).join(" · "))],
    ["孔槽与端部加工", operations.map((operation) => [operation.label || input.template.partProcesses?.find((process) => process.id === (operation.processId ?? operation.id))?.label || "单件加工", rolesOf(operation).map(participantName).join("、"), placementText(operation.placement)].filter(Boolean).join(" · "))],
    ["后续装配步骤", (workflow.assemblySteps ?? []).map((step) => [step.label || "装配步骤", step.kindLabel, Number(step.distance) ? `${step.distance}${step.unit || " mm"}` : ""].filter(Boolean).join(" · "))],
    ["辅料", (workflow.bom ?? []).map((item) => `${item.label || "辅料"} · ${item.quantity ?? "待定"}${item.unit || "件"}${String(item.specificationStatus ?? "").startsWith("requires-") ? " · 规格待确定" : ""}`)],
  ];
  return `<div class="tube-assembly-binding-workflow" id="tube-binding-workflow">${groups.map(([label, rows]) => `<section><strong>${label}</strong>${rows.length ? `<ul>${rows.map((row) => `<li>${escapeHtml(row)}</li>`).join("")}</ul>` : `<p>${label === "辅料" ? "未提供辅料明细" : "未提供明细"}</p>`}</section>`).join("")}</div>`;
}

function renderSavedBindings(result, busy) {
  const rows = (result?.bindings ?? []).map((binding, index) => {
    const label = binding.connectionLabel
      || binding.participants?.map((part) => memberName(nativeMembers(result).find((member) => memberId(member) === part.memberEntityId))).join(" + ");
    const detail = binding.statusDetail || (binding.manufacturingEffect === "connection-only"
        || (Array.isArray(binding.parts) && binding.parts.length === 0)
        ? "连接已记录，装配位置待核对" : "单件加工已应用，装配位置待核对");
    return `<li id="tube-binding-saved-${escapeHtml(binding.bindingId || index)}"><div><strong>${escapeHtml(binding.templateName || binding.label || "已绑定工艺")}</strong><span>${escapeHtml(label)}</span><small>${escapeHtml(detail)}</small></div><div class="tube-assembly-binding-actions"><button type="button" data-cam-action="tube-designer-binding-edit" data-tube-binding-id="${escapeHtml(binding.bindingId)}"${busy ? " disabled" : ""}>修改</button><button type="button" data-cam-action="tube-designer-binding-remove" data-tube-binding-id="${escapeHtml(binding.bindingId)}"${busy || !binding.bindingId || binding.canRemove === false ? " disabled" : ""}>移除</button></div></li>`;
  }).join("");
  return `<section class="tube-assembly-binding-saved" id="tube-binding-saved"><strong>已绑定工艺 · ${result?.bindings?.length ?? 0} 项</strong>${rows ? `<ul>${rows}</ul>` : "<p>当前产品没有已绑定的装配工艺。</p>"}</section>`;
}

function wholeGeometryAuditLabel(audit) {
  if (audit?.status === "measured") return "非相邻构件无穿透";
  if (audit?.status === "conflict") {
    const count = Array.isArray(audit.conflicts) ? audit.conflicts.length : 0;
    return count ? `发现 ${count} 处非相邻构件穿透` : "发现非相邻构件穿透";
  }
  if (audit?.status === "unsupported") return "整件几何检查暂不可用";
  return "";
}

function nodeFinalAuditLabel(audit) {
  const count = Number(audit?.nodeCount);
  const measured = Number(audit?.measuredNodeCount);
  if (audit?.status === "measured") return Number.isInteger(count) && count > 0
    && Number.isInteger(measured) && measured >= 0
    ? `节点复测：${measured}/${count} 处无材料穿透` : "节点复测：无材料穿透";
  if (audit?.status === "conflict") {
    const conflicts = Number(audit.conflictNodeCount);
    return Number.isInteger(conflicts) && conflicts > 0
      ? `节点复测：${conflicts} 处材料穿透` : "节点复测：发现材料穿透";
  }
  if (audit?.status === "unsupported") return "节点复测暂不可用";
  return "";
}

function renderManufacturingPlan(result, checking = false) {
  const plan = result?.manufacturingPlan;
  const stockPlan = Array.isArray(plan?.stocks);
  if (!stockPlan && !Array.isArray(plan?.members)) return "";
  const parts = stockPlan ? plan.stocks : plan.members.filter((member) => member.connections?.length);
  const selected = Number(plan.selectedConnectionCount) || 0;
  const unassigned = Number(plan.unassignedConnectionCount) || 0;
  const rows = parts.map((part) => {
    const connections = part.connections ?? [];
    const configured = connections.filter((connection) => connection.status === "applied").length;
    const partLength = Number(part.partLength);
    const hasLength = part.partLength != null && Number.isFinite(partLength) && partLength > 0;
    const length = part.partMappingStatus === "persisted" && hasLength
      ? `${partLength} mm` : part.partMappingStatus === "transient" && hasLength
        ? `${partLength} mm · 待保存` : part.partMappingStatus === "planned" && hasLength
          ? `${partLength} mm · 待生成` : "下料件未就绪";
    const detail = [length];
    if (stockPlan && part.sourceMembers?.length) detail.push(`对应 ${part.sourceMembers.length} 根成品管件`);
    if (stockPlan && connections.length) detail.push(`关联 ${connections.length} 处连接`);
    else if (!stockPlan) detail.push(`${configured}/${connections.length} 处连接`);
    if (!stockPlan || Array.isArray(part.operations)) detail.push(`${part.operations?.length ?? 0} 道加工`);
    const sources = stockPlan ? (part.sourceMembers ?? []).map((member) => member.name).filter(Boolean) : [];
    return `<li${stockPlan ? ` data-tube-manufacturing-stock="${escapeHtml(part.stockEntityId || part.itemKey)}"` : ""}><strong>${escapeHtml(part.name || part.itemKey)}</strong><span>${escapeHtml(detail.join(" · "))}</span>${sources.length ? `<small>${escapeHtml(sources.join(" + "))}</small>` : ""}</li>`;
  }).join("");
  const readiness = plan.readiness === "ready" ? "可下料" : "待验证";
  const executionStatus = plan.planningError ? "下料规划失败"
    : plan.stockExecutionStatus === "saved" ? "下料已保存"
      : plan.stockExecutionStatus === "planned" ? "下料待生成"
        : parts.length === 0 ? "暂无下料"
          : parts.every((part) => part.partMappingStatus === "persisted") ? "下料已保存" : "下料待生成";
  const summary = stockPlan ? `${parts.length} 件下料 · ${executionStatus}`
    : `${selected}/${selected + unassigned} 处连接 · ${readiness}`;
  const audit = checking ? "整件几何检查中…" : wholeGeometryAuditLabel(plan.wholeGeometryAudit);
  const nodeAudit = checking && plan.nodeFinalAudit ? "节点复测中…" : nodeFinalAuditLabel(plan.nodeFinalAudit);
  return `<details class="tube-assembly-manufacturing-plan" id="tube-binding-manufacturing-plan"><summary>当前下料汇总 · ${escapeHtml(summary)}${checking ? " · 检查中…" : ""}</summary><ul>${rows}</ul>${stockPlan && plan.planningError ? `<p class="tube-assembly-binding-warning" role="alert">${escapeHtml(plan.planningError)}</p>` : ""}${audit ? `<small id="tube-binding-whole-audit" role="status">${escapeHtml(audit)}</small>` : ""}${nodeAudit ? `<small id="tube-binding-node-audit" role="status">${escapeHtml(nodeAudit)}</small>` : ""}<small>${escapeHtml(stockPlan ? "装配配合待核对" : plan.reason || "装配配合待核对")}</small></details>`;
}

export function renderProductAssemblyBindingOverview(view, message) {
  const identity = productAssemblyBindingIdentity(view);
  const state = productAssemblyBindingsState(view);
  const result = displayedResult(view);
  const status = !identity.productEntityId || !identity.generationRunId ? "请先选中并生成产品。"
    : state.status === "error" && !result ? state.error
      : !result ? "正在读取并检查产品几何…" : "";
  return `<section class="tube-assembly-bindings" id="tube-product-assembly-bindings"><p class="tube-assembly-binding-example-note">${escapeHtml(message)}</p>${state.notice && state.productKey === identity.productKey ? `<p class="tube-assembly-binding-notice" role="status">${escapeHtml(state.notice)}</p>` : ""}${status ? `<p role="status">${escapeHtml(status)}</p>` : `${renderManufacturingPlan(result, state.status === "refreshing")}${renderSavedBindings(result, !!state.mutation || state.status !== "ready")}`}${state.status === "error" ? '<button type="button" data-cam-action="tube-designer-binding-reload">重新读取</button>' : ""}</section>`;
}

export function renderProductAssemblyBindings(view, input) {
  const identity = productAssemblyBindingIdentity(view);
  const state = productAssemblyBindingsState(view);
  const result = displayedResult(view);
  let body;
  if (!identity.productEntityId || !identity.generationRunId) body = "<p>请先选中并生成产品，再为真实构件配置工艺。</p>";
  else if (!result) body = `<p role="status">${escapeHtml(state.status === "error" ? state.error : "正在读取并检查当前产品几何…")}</p>${state.status === "error" ? '<button type="button" data-cam-action="tube-designer-binding-reload">重新读取</button>' : ""}`;
  else {
    const draft = productAssemblyBindingDraft(view, input.template);
    const memberIds = new Set(nativeMembers(result).map(memberId));
    const available = connections(view, result);
    const connection = available.find((item) => item.key === draft.connectionKey);
    const connectionMembersSupported = connection?.participants?.every((part) => memberIds.has(memberId(part)));
    const compatible = available.filter((item) => item.participants?.length === input.template.participants.length
      && item.participants.every((part) => memberIds.has(memberId(part)))
      && productAssemblyTemplateMatchesTopology(input.template, input.parameters, item));
    const offered = connection && !compatible.includes(connection) ? [...compatible, connection] : compatible;
    const connectionOptions = [{ id: "", label: "选择已提交产品连接" }, ...offered.map((item, index) => ({
      id: item.key, label: `${item.label || item.participants?.map(memberName).join(" + ") || `连接 ${index + 1}`}${compatible.includes(item) ? "" : "（不适用）"}` }))];
    const disabled = identity.outdated || !!result.modelOutdated;
    const nodeOrder = input.template.participants.map((role, index) =>
      String.fromCharCode(65 + nodeIndexForRole(role.role, index))).sort().join("、");
    body = `${renderManufacturingPlan(result, state.status === "refreshing")}${renderSavedBindings(result, !!state.mutation || state.status !== "ready")}${renderSupport(result, input.template, input.parameters)}${identity.outdated || result.modelOutdated ? '<p class="tube-assembly-binding-warning">产品模型已变化，请重新生成后再配置真实连接。</p>' : ""}
      <section class="tube-assembly-binding-selection" id="tube-binding-selection"><strong>3 · ${draft.replaceBindingId ? "修改已绑定工艺" : "核对构件角色与位置"}</strong>${draft.replaceBindingId ? '<button type="button" data-cam-action="tube-designer-binding-cancel-edit">取消修改，新增绑定</button>' : ""}<label class="tube-designer-field"><span>产品连接位置</span><select data-cam-change-action="tube-designer-binding-connection"${disabled ? " disabled" : ""}>${optionsHtml(connectionOptions, draft.connectionKey)}</select></label><small>默认按节点中的 ${escapeHtml(nodeOrder)} 顺序分配；可在下方交换构件。</small>${connection && !connectionMembersSupported ? '<p class="tube-assembly-binding-warning">所选连接含非独立下料构件，当前不能应用单件加工。</p>' : connection && connection.participants.length !== input.template.participants.length ? `<p class="tube-assembly-binding-warning">所选连接有 ${connection.participants.length} 件构件，此工艺需要 ${input.template.participants.length} 件；请改选相容工艺。</p>` : connection && !productAssemblyTemplateMatchesTopology(input.template, input.parameters, connection) ? '<p class="tube-assembly-binding-warning">所选连接的成品形状与此工艺不兼容。</p>' : !compatible.length ? '<p class="tube-assembly-binding-warning">当前产品没有与此工艺相容的连接；请改选工艺或完善产品连接关系。</p>' : ""}${renderParticipants(view, result, input.template, draft, disabled)}</section>
      ${input.parameterHtml ?? ""}${renderCandidate(view, input)}`;
  }
  return `<section class="tube-assembly-bindings" id="tube-product-assembly-bindings"><p class="tube-assembly-binding-example-note">中间画布显示当前产品或已生成下料件；检查方案在应用前不会改变产品。</p>${state.notice && state.productKey === identity.productKey ? `<p class="tube-assembly-binding-notice" role="status">${escapeHtml(state.notice)}</p>` : ""}${state.status === "error" && result ? `<p role="alert">${escapeHtml(state.error)}</p><button type="button" data-cam-action="tube-designer-binding-reload">重新读取</button>` : ""}${body}</section>`;
}
