import { renderParameterLevels } from './parameterPresentation.mjs';
import { parameterVisible } from './parameterConditions.mjs';
import { matchesParameterCondition, parameterEnabled } from "./parameterConditions.mjs";
import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { confirmWithoutTitle } from "./confirmDialog.mjs";
import { renderProfileSvg } from "./profileSvg.mjs";
import { bindProfileParameterDiagrams, renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import { applyTemplateFieldDisplay, templateDisplayView, fieldDisplayClass, fieldDisplayStyle,
  } from "./designerViews.mjs";
import { floatingParameterDiagramHost, libraryDiagramPositionStyle, renderDiagramResizeHandles } from './floatingParameterDiagram.mjs';
import { patchDomNode } from "./punchDomPatch.mjs";
export { renderProfileSvg } from "./profileSvg.mjs";


export const PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS = 500;
export const PROFILE_PREVIEW_PROGRESS_DELAY_MS = 200;
const MINIMUM_PROGRESS_MS = 500;


export function renderProfileLibraryLeftPane(_context, view) {
  const state = profileLibraryState(view);
  const all = libraryProfiles(view);
  const profiles = visibleLibraryProfiles(view);
  const selectedId = ensureSelectedProfile(view, profiles);
  const renderCard = (profile) => {
    const scope = profileScope(profile);
    const selectionKey = profileSelectionKey(profile);
    const editable = isParametricProfile(profile);
    const specification = profileSpecification(profile);
    // The card already sits inside 管型库; keep the type marker short so the
    // distinction is visible at a glance without repeating the resource name.
    const typeLabel = editable ? "程式" : "定式";
    const sourceLabel = scope === "template" ? `模板自带 · ${profile.templateName || profile.templateId}` : scope === "system" ? "系统内置" : "我的管型";
    return `<button type="button" class="tube-profile-library-card ${selectionKey === selectedId ? "selected" : ""}" title="${escapeAttr(`${profileName(profile)}${specification ? ` · ${specification}` : ""}`)}" aria-label="${escapeAttr(`${profileName(profile)}，${typeLabel}，${sourceLabel}${specification ? `，${specification}` : ""}`)}" data-cam-action="tube-designer-profile-library-select" data-tube-designer-profile-key="${escapeAttr(selectionKey)}" data-tube-designer-profile-scope="${escapeAttr(scope)}" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>
      <span class="tube-profile-library-card-preview${profile?.descriptor?.manufacturing?.topology === "solid" ? " is-solid" : ""}" aria-hidden="true">${renderProfileCardPreview(profile)}</span>
      <span class="tube-profile-library-card-copy">
        <strong>${escapeText(profileName(profile))}</strong>
        <small>${escapeText(typeLabel)}</small>
      </span>
    </button>`;
  };
  const groupedProfiles = new Map();
  for (const profile of profiles) {
    const group = profileGroupLabel(profile);
    if (!groupedProfiles.has(group)) groupedProfiles.set(group, []);
    groupedProfiles.get(group).push(profile);
  }
  const renderGroup = ([group, entries]) => {
    const groupKey = `${state.scope}:${group}`;
    const expanded = !state.collapsed.includes(groupKey);
    return `<section class="tube-profile-library-group">
      <button type="button" class="tube-profile-library-group-heading" data-cam-action="tube-designer-profile-library-toggle-category" data-tube-profile-library-group="${escapeAttr(groupKey)}" aria-expanded="${expanded}"><span>${expanded ? "▾" : "▸"} ${escapeText(group)}</span><small>${escapeText(scopeLabel(state.scope))} · ${entries.length}</small></button>
      <div class="tube-profile-library-group-content" ${expanded ? "" : "hidden"}>${entries.map(renderCard).join("")}</div>
    </section>`;
  };
  const loadError = state.scope === "system" ? view.tubeDesignerSystemProfilesError
    : view.tubeDesignerUserDataError;
  const emptyTitle = loadError ? "管型加载失败" : state.search
    ? "没有匹配的管型"
    : { system: "没有可用的系统管型", template: "还没有模板自带管型", user: "还没有我的管型" }[state.scope];
  const emptyNote = loadError ? String(loadError) : state.search
    ? "请调整搜索内容。"
    : state.scope === "template" ? "模板自带管型按所属模板管理，仅供该模板使用。"
      : state.scope === "user" ? "可导入程式管型包，或导入定式管型。"
        : "请检查内置管型资源是否完整。";
  const orderedGroups = [...groupedProfiles.entries()].sort(([left], [right]) => {
    const leftKey = profileGroupSortKey(left);
    const rightKey = profileGroupSortKey(right);
    return leftKey.localeCompare(rightKey, "zh-CN");
  });
  return `<div class="tube-designer-panel tube-profile-library-panel">
    <div class="tube-designer-heading tube-profile-library-heading">
      <div><strong>管型库</strong><span>${all.length} 个管型 · 当前显示 ${profiles.length} 个</span></div>
    </div>
    <div class="tube-component-library-filters tube-profile-library-filters">
      <input type="search" aria-label="搜索管型" placeholder="搜索名称、规格、所属模板" value="${escapeAttr(state.search)}" data-cam-change-action="tube-designer-profile-library-search" />
      <div class="tube-profile-library-tab-row" role="tablist" aria-label="管型来源">${[["system", "系统内置"], ["user", "我的"]].map(([scope, title]) => `<button type="button" role="tab" aria-selected="${scope === state.scope}" class="${scope === state.scope ? "selected" : ""}" data-cam-action="tube-designer-profile-library-scope" data-tube-profile-library-scope="${scope}" ${view?.pending ? "disabled" : ""}>${title}<small>${all.filter((profile) => profileScope(profile) === scope).length}</small></button>`).join("")}</div>
    </div>
    <div class="tube-profile-library-list" role="tabpanel">
      ${profiles.length ? orderedGroups.map(renderGroup).join("") : `<div class="tube-profile-library-empty compact"><strong>${escapeText(emptyTitle)}</strong><span>${escapeText(emptyNote)}</span></div>`}
    </div>
  </div>`;
}


export function renderProfileLibraryRightPane(_context, view) {
  const profiles = visibleLibraryProfiles(view);
  const selectedId = ensureSelectedProfile(view, profiles);
  const profile = profiles.find((item) => profileSelectionKey(item) === selectedId);
  const dialog = renderProfilePackageImportDialog(view);
  if (!profile) {
    return `<div class="tube-designer-panel tube-profile-library-editor-empty">
      <div class="tube-designer-heading"><strong>管型属性</strong><span>尚未选择管型</span></div>
      <div class="tube-designer-empty">从左侧选择管型，中央显示三维预览；模板自带资源仅供所属模板使用。</div>
    </div>${dialog}`;
  }
  const system = profileScope(profile) === "system";
  const template = profileScope(profile) === "template";
  const readOnly = system || template;
  const editable = isParametricProfile(profile);
  const allDefinitions = editable && Array.isArray(profile?.descriptor?.parameters)
    ? profile.descriptor.parameters : [];
  const profileKey = profileSelectionKey(profile);
  const draft = view?.tubeDesignerProfileDrafts?.[profileKey] ?? null;
  const values = draft?.parameters ?? profile.defaultParameters ?? {};
  const definitions = allDefinitions.filter((definition) => parameterVisible(definition, values));
  return `<div class="tube-designer-panel tube-profile-library-editor" data-profile-parameter-scope data-parameter-diagram-owner="profile-library:${escapeAttr(profileKey)}" data-tube-profile-library-editor data-tube-designer-profile-key="${escapeAttr(profileKey)}" data-tube-designer-profile-scope="${escapeAttr(profileScope(profile))}" data-tube-designer-profile-id="${escapeAttr(profile.id)}">
    <div class="tube-profile-library-editor-body">
      ${readOnly
        ? ""
        : `<label class="tube-designer-field wide"><span>管型名称</span><input type="text" data-tube-profile-editor-name value="${escapeAttr(profileName(profile))}" maxlength="120" ${view?.pending ? "disabled" : ""} /></label>`}
      ${editable ? `<section class="tube-profile-library-parameter-section">
        <header><div><strong>${readOnly ? "预览参数" : "默认参数"}</strong>${readOnly ? "" : "<span>产品选择此管型时仍可单独修改</span>"}</div><button type="button" class="tube-profile-library-diagram-toggle" data-profile-diagram-toggle data-cam-action="tube-designer-profile-library-toggle-diagram" aria-expanded="${!!profileLibraryState(view).showDiagram}">显示管型示意图</button></header>
        <div class="tube-profile-library-parameter-list">${renderProfileParameterGroups(profile, definitions, values, view?.pending)}</div>
      </section>` : `<p class="tube-profile-library-frozen-note">使用已保存的固定截面；不提供形状参数修改。</p>`}
      ${template ? `<p class="tube-profile-library-system-note">此管型随所属模板提供，不可重命名或删除。进入草图编辑时会创建独立副本，不会覆盖模板资源。</p>` : ""}
    </div>
    <footer class="tube-profile-library-editor-footer">
      <div>
        ${readOnly ? "" : `<button class="tube-designer-danger" data-cam-action="tube-designer-profile-library-delete" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>删除管型</button>`}
        <button class="tube-designer-secondary" data-cam-action="tube-designer-profile-library-edit-sketch" data-tube-designer-profile-key="${escapeAttr(profileKey)}" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>定制到我的</button>
      </div>
      ${readOnly ? "" : `<button class="tube-designer-primary" data-cam-action="tube-designer-profile-library-save" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>${editable ? "保存名称和默认参数" : "保存名称"}</button>`}
    </footer>
  </div>${dialog}`;
}


export function renderProfileLibraryViewportOverlay(context, view) {
  const profiles = visibleLibraryProfiles(view);
  const selectedId = ensureSelectedProfile(view, profiles);
  const profile = profiles.find((item) => profileSelectionKey(item) === selectedId);
  scheduleProfileLibraryPreview(context, view, profile);
  const version = String(profile?.descriptor?.version ?? profile?.version ?? "").trim() || "—";
  return `${renderProfileLibraryDiagramDock(view,profile)}
  <div class="tube-profile-library-preview-hud" data-tube-profile-preview-status>
    <strong>${profile ? escapeText(profileName(profile)) : "管型预览"}</strong>
    <span data-tube-profile-preview-version>${profile ? `版本 ${escapeText(version)}` : ""}</span>
    <small></small>
    <div class="tube-profile-library-preview-progress" data-tube-profile-preview-progress role="progressbar" aria-label="正在生成三维管型" aria-valuetext="处理中" aria-hidden="true" hidden><i></i></div>
  </div>
  <div class="tube-profile-library-preview-wait" data-tube-profile-preview-wait role="dialog" aria-modal="true" aria-live="polite" aria-labelledby="tube-profile-preview-wait-title" aria-hidden="true" hidden>
    <div class="tube-designer-export-progress-card">
      <span class="tube-designer-export-spinner" aria-hidden="true"></span>
      <strong id="tube-profile-preview-wait-title" data-tube-profile-preview-wait-title>正在生成三维管型</strong>
      <span data-tube-profile-preview-wait-message>正在计算截面并创建标准拉伸体</span>
      <div class="tube-designer-export-progress-track is-indeterminate" data-tube-profile-preview-wait-progress role="progressbar" aria-label="三维管型生成进度" aria-valuetext="处理中"><i style="width:36%"></i></div>
      <small data-tube-profile-preview-wait-phase>生成三维资源</small>
      <em>完成或失败后，界面会自动恢复。</em>
    </div>
  </div>`;
}

export function renderProfileLibraryDiagramDock(view,profile=findLibraryProfile(view,view.tubeDesignerSelectedProfileId)) {
  if(!profile)return '';
  const state=profileLibraryState(view), values=profilePreviewParameters(view,profile);
  const definitions=(profile.descriptor?.parameters ?? []).filter(d=>parameterVisible(d,values));
  const snapshot=generatedProfileSnapshot(view,profile) ?? profileSnapshot(profile);
  const owner=`profile-library:${profileSelectionKey(profile)}`;
  return `<aside class="tube-library-diagram-dock" data-tube-profile-diagram-dock data-library-floating-diagram="profiles" ${state.showDiagram?'':'hidden'} style="${libraryDiagramPositionStyle(view,'profiles')}">
    ${renderDiagramResizeHandles()}
    <header class="tube-library-diagram-drag" data-floating-diagram-drag><strong>管型参数示意图</strong><button type="button" data-library-diagram-close data-cam-action="tube-designer-profile-library-close-diagram" aria-label="关闭示意图">×</button></header>
    <div class="tube-library-diagram-content" data-parameter-diagram-for="${escapeAttr(owner)}" ${state.showDiagram?'':'hidden'}>${definitions.length?renderProfileParameterDiagram(snapshot,{definitions,parameters:values,compact:true,title:'参数示意图'}):renderProfileSvg(snapshot)}</div>
  </aside>`;
}


export async function handleProfileLibraryAction(context, view, action, target, ops) {
  if(action==='tube-designer-profile-library-toggle-diagram'||action==='tube-designer-profile-library-close-diagram'){
    const state=profileLibraryState(view);state.showDiagram=action!=='tube-designer-profile-library-close-diagram';
    const dock=context.mount?.querySelector('[data-tube-profile-diagram-dock]');
    if(dock){dock.hidden=!state.showDiagram;dock.querySelector('.tube-library-diagram-content').hidden=!state.showDiagram;}
    for(const button of context.mount?.querySelectorAll('[data-profile-diagram-toggle]') ?? []){
      button.setAttribute('aria-expanded',String(state.showDiagram));
    }
    return {handled:true};
  }
  if (view.activeAreaId === "profiles") {
    const sceneResult = handleProfileSceneAction(context, view, action, target, ops);
    if (sceneResult) return sceneResult;
  }
  if (action === "tube-designer-profile-library-toggle-category") {
    if (!view.pending) {
      const state = profileLibraryState(view);
      const group = String(target?.dataset?.tubeProfileLibraryGroup ?? "");
      if (group) {
        state.collapsed = state.collapsed.includes(group)
          ? state.collapsed.filter((value) => value !== group)
          : [...state.collapsed, group];
        ops.renderProject(context, view);
      }
    }
    return { handled: true };
  }
  if (action === "tube-designer-profile-library-scope" || action === "tube-designer-profile-library-type" || action === "tube-designer-profile-library-search") {
    if (!view.pending) {
      const state = profileLibraryState(view);
      if (action.endsWith("scope")) {
        const scope = String(target?.dataset?.tubeProfileLibraryScope ?? "");
        if (!["system", "user"].includes(scope)) return { handled: true };
        state.selectedByScope[state.scope] = view.tubeDesignerSelectedProfileId;
        state.scope = scope;
        view.tubeDesignerSelectedProfileId = state.selectedByScope[scope] ?? "";
      } else if (action.endsWith("type")) {
        const type = String(target?.dataset?.tubeProfileLibraryType ?? "");
        if (!["all", "parametric", "fixed"].includes(type)) return { handled: true };
        state.type = type;
      } else state.search = String(target?.value ?? "");
      clearProfilePreviewRequest(view);
      ensureSelectedProfile(view, visibleLibraryProfiles(view));
      view.error = "";
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-profile-library-select") {
    if (!view.pending) {
      view.tubeDesignerSelectedProfileId = String(
        target?.dataset?.tubeDesignerProfileKey
        ?? `${target?.dataset?.tubeDesignerProfileScope ?? "user"}:${target?.dataset?.tubeDesignerProfileId ?? ""}`,
      );
      clearProfilePreviewRequest(view);
      view.error = "";
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-profile-package-import-cancel") {
    if (!view.pending) {
      view.tubeDesignerProfilePackageImportDialog = null;
      view.error = "";
      ops.renderProject(context, view);
    }
    return { handled: true };
  }
  if (action === "tube-designer-profile-package-import-confirm") {
    return { handled: true, result: await confirmProfilePackageImport(context, view, ops) };
  }
  if (action === "tube-designer-profile-library-save") {
    return { handled: true, result: await saveLibraryProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-profile-library-delete") {
    return { handled: true, result: await deleteLibraryProfile(context, view, target, ops) };
  }
  if (action === "tube-designer-profile-preview-change") {
    updateProfilePreviewDraft(context, view, target, ops);
    return { handled: true };
  }
  if (action === "tube-designer-profile-export-dxf" || action === "tube-designer-profile-export-step") {
    return {
      handled: true,
      result: await exportLibraryProfile(
        context, view, target,
        action.endsWith("dxf") ? "dxf" : "step",
        ops,
      ),
    };
  }
  return { handled: false };
}


export async function handleProfileLibraryRibbonCommand(context, view, commandId, ops) {
  if (commandId === "profiles.import-package") {
    await chooseProfilePackage(context, view, ops);
    return true;
  }
  if (commandId === "profiles.import-dxf") {
    await importDxfIntoLibrary(context, view, ops);
    return true;
  }
  if (commandId === "profiles.export-dxf" || commandId === "profiles.export-step") {
    await exportLibraryProfile(
      context, view, null,
      commandId.endsWith("dxf") ? "dxf" : "step",
      ops,
    );
    return true;
  }
  return false;
}


export function profileLibraryState(view) {
  const current = String(view.tubeDesignerSelectedProfileId ?? "");
  const initialScope = current.startsWith("user:")
    || (view.tubeDesignerUserData?.profiles ?? []).some((profile) => String(profile?.id ?? "") === current) ? "user" : "system";
  const state = view.tubeDesignerProfileLibrary ??= { scope: initialScope, type: "all", search: "", selectedByScope: {}, collapsed: [] };
  if (!["system", "user"].includes(state.scope)) state.scope = "system";
  if (!["all", "parametric", "fixed"].includes(state.type)) state.type = "all";
  state.selectedByScope ??= {};
  state.collapsed ??= [];
  state.showDiagram ??= true;
  return state;
}


export function libraryProfiles(view) {
  const system = (Array.isArray(view?.tubeDesignerSystemProfiles)
    ? view.tubeDesignerSystemProfiles : [])
    .map((profile) => ({ ...profile, libraryScope: "system" }));
  const user = (Array.isArray(view?.tubeDesignerUserData?.profiles)
    ? view.tubeDesignerUserData.profiles : [])
    .map((profile) => ({ ...profile, libraryScope: "user" }));
  const byName = (left, right) => profileName(left).localeCompare(profileName(right), "zh-CN");
  const byCatalog = (left, right) => {
    const a = left.descriptor?.catalog ?? {}, b = right.descriptor?.catalog ?? {};
    return (Number(a.groupOrder ?? 999) - Number(b.groupOrder ?? 999))
      || (Number(a.order ?? 999) - Number(b.order ?? 999)) || byName(left, right);
  };
  return [...system.sort(byCatalog), ...user.sort(byName)];
}


function profileCategory(profile) {
  const category = localizedText(profile?.category ?? profile?.descriptor?.category, isParametricProfile(profile) ? "程式管型" : "定式管型");
  return ({ "参数化管型": "程式管型", "DXF 截面": "定式管型", "DXF管型": "定式管型", "DXF 管型": "定式管型" })[category] ?? category;
}


export function visibleLibraryProfiles(view) {
  const state = profileLibraryState(view);
  const query = String(state.search ?? "").trim().toLocaleLowerCase();
  return libraryProfiles(view).filter((profile) => profileScope(profile) === state.scope
    && [profileName(profile), profileSpecification(profile), profileCategory(profile), profile.templateName, profile.templateId]
      .some((value) => String(value ?? "").toLocaleLowerCase().includes(query)));
}


export function profileLibraryType(profile) {
  return isParametricProfile(profile) ? "parametric" : "fixed";
}


export function templateProfilesForProduct(view, templateId) {
  return (view?.tubeDesignerTemplateProfiles ?? []).map(profile => ({...profile, libraryScope:"template"})).filter((profile) => profileScope(profile) === "template"
    && String(profile.templateId) === String(templateId ?? ""));
}


function ensureSelectedProfile(view, profiles) {
  const current = String(view?.tubeDesignerSelectedProfileId ?? "");
  if (profiles.some((item) => profileSelectionKey(item) === current)) return current;
  const legacy = profiles.find((item) => String(item?.id ?? "") === current);
  if (legacy) {
    const normalized = profileSelectionKey(legacy);
    view.tubeDesignerSelectedProfileId = normalized;
    return normalized;
  }
  const next = profiles[0] ? profileSelectionKey(profiles[0]) : "";
  view.tubeDesignerSelectedProfileId = next;
  if (!next) {
    clearProfilePreviewRequest(view);
    view.viewport?.setVisibleEntityIds?.([]);
    view.viewport?.setSelectedObjectIds?.([], "");
    hidePreviewProgress();
  }
  return next;
}


export function profileScope(profile) {
  const scope = String(profile?.libraryScope ?? profile?.profileScope ?? profile?.scope ?? "user");
  return ["system", "template"].includes(scope) ? scope : "user";
}


export function profileSelectionKey(profile) {
  const scope = profileScope(profile);
  return scope === "template" ? `template:${encodeURIComponent(String(profile?.templateId ?? ""))}:${encodeURIComponent(String(profile?.id ?? ""))}` : `${scope}:${String(profile?.id ?? "")}`;
}


function findLibraryProfile(view, keyOrId, scope = "") {
  const profiles = libraryProfiles(view);
  const key = String(keyOrId ?? "");
  if (key.includes(":")) {
    const exact = profiles.find((profile) => profileSelectionKey(profile) === key);
    if (exact) return exact;
  }
  const expectedScope = String(scope ?? "");
  return profiles.find((profile) => String(profile?.id ?? "") === key
    && (!expectedScope || profileScope(profile) === expectedScope)) ?? null;
}


export function profileRef(profile) {
  const scope = profileScope(profile);
  return { scope, ...(scope === "template" ? { templateId: String(profile?.templateId ?? "") } : {}), id: String(profile?.id ?? "") };
}


export function profileName(profile) {
  return String(
    profile?.name
    ?? profile?.previewProfile?.name
    ?? localizedText(profile?.descriptor?.displayName, "未命名管型"),
  );
}


export function isParametricProfile(profile) {
  const form=profile?.profileForm ?? profile?.descriptor?.profileForm ?? profile?.previewProfile?.profileForm;
  if(form!=="parametric" && form!=="fixed")throw new Error("管型缺少有效的形式字段，请先升级迁移数据");
  return form==="parametric";
}


function profileType(profile) {
  return String(profile?.profileType ?? profile?.kind ?? "fixed-section");
}

const PROFILE_GROUP_ALIASES = new Map([
  ["管", "管"],
  ["单腔体", "管"],
  ["矩形 / 多边形管", "管"],
  ["圆管 / 椭圆管", "管"],
  ["型材", "型材"],
  ["型材（0腔体）", "型材"],
  ["型钢", "型材"],
  ["多腔体", "多腔体"],
]);

function normalizedProfileGroup(value) {
  const text = String(value ?? "").trim();
  return PROFILE_GROUP_ALIASES.get(text) ?? "";
}

function profileGroupLabel(profile) {
  const categoryPath = profile?.descriptor?.catalog?.categoryPath;
  if (Array.isArray(categoryPath)) {
    const path = categoryPath
      .filter((part) => typeof part === "string" && part.trim())
      .map((part) => part.trim());
    const group = path.map(normalizedProfileGroup).find(Boolean);
    if (group) return group;
  }
  const declared=localizedText(profile?.category ?? profile?.descriptor?.category, "");
  const declaredGroup = normalizedProfileGroup(declared);
  if (declaredGroup) return declaredGroup;
  const id = String(profile?.id ?? "").toLocaleLowerCase();
  const name = profileName(profile).toLocaleLowerCase();
  if (id.includes("multi-cell") || /多腔|二腔|双腔/.test(name)) return "多腔体";
  if (["angle", "channel", "i-section", "t-section", "z-section", "bulb-flat", "omega",
       "open-tube", "polygon-bar", "rect-bar", "round-bar", "sigma", "u-section"]
      .some((part) => id.includes(part))
      || /角钢|槽钢|工字钢|h型钢|t型钢|z型钢|球扁钢|帽钢|开缝|开槽|棒|扁钢|sigma|u肋/.test(name)) {
    return "型材";
  }
  const contourCount = profileSnapshot(profile)?.contours?.length;
  if (Number.isInteger(contourCount)) {
    if (contourCount > 2) return "多腔体";
    if (contourCount < 2) return "型材";
  }
  return "管";
}


function profileGroupSortKey(label) {
  const groupOrder = { "管": 1, "型材": 2, "多腔体": 3 }[String(label)] ?? 9;
  return `${String(groupOrder).padStart(2, "0")}.${label}`;
}

function scopeLabel(scope) {
  return ({ system: "系统内置", template: "模板自带", user: "我的" })[scope] ?? "";
}


export function profileSnapshot(profile) {
  return isParametricProfile(profile)
    ? profile?.previewProfile ?? profile?.profile ?? (profile?.contours ? profile : null)
    : profile ?? null;
}


function renderProfileCardPreview(profile) {
  const snapshot = profileSnapshot(profile);
  if (snapshot?.contours?.length) return renderProfileSvg(snapshot);
  return `<span class="tube-profile-library-preview-unavailable" title="${escapeAttr(profile?.error || '截面尚未生成')}">暂无截面</span>`;
}

export async function resolveSelectedProfileSketchSource(context, view) {
  const profiles = visibleLibraryProfiles(view);
  const selectedKey = ensureSelectedProfile(view, profiles);
  const profile = profiles.find((item) => profileSelectionKey(item) === selectedKey) ?? null;
  if (!profile) throw new Error("请先选择一个要编辑的管型。");
  const key = profilePreviewKey(view, profile);
  let response = view?.tubeDesignerProfilePreview?.key === key
    ? view.tubeDesignerProfilePreview.response
    : null;
  if (!response?.profile && view?.tubeDesignerProfilePreviewRequest?.key === key) {
    response = await view.tubeDesignerProfilePreviewRequest.operation;
  }
  if (!response?.profile && isParametricProfile(profile)
      && typeof context?.sceneProxy?.invoke === "function") {
    response = await invokeSceneProduct(
      context,
      "TubeDesigner.GenerateProfilePreview",
      {
        profileRef: profileRef(profile),
        parameters: profilePreviewParameters(view, profile),
      },
      { timeoutMs: 30000 },
    );
  }
  const snapshot = response?.profile ?? profileSnapshot(profile);
  if (!snapshot?.contours?.length) throw new Error("所选管型没有可编辑的截面轮廓。");
  return {
    snapshot,
    profileId: String(profile.id ?? ""),
    profileKey: profileSelectionKey(profile),
    scope: profileScope(profile),
    revision: Number(profile.revision ?? 0),
    name: profileName(profile),
    directUpdate: profileScope(profile) === "user" && !isParametricProfile(profile),
  };
}


export function profileSpecification(profile) {
  return String(profileSnapshot(profile)?.specification ?? profile?.specification ?? "");
}


export function profilePreviewParameters(view, profile) {
  return view?.tubeDesignerProfileDrafts?.[profileSelectionKey(profile)]?.parameters
    ?? profile?.defaultParameters
    ?? {};
}

function generatedProfileSnapshot(view, profile) {
  if (!profile) return null;
  const preview = view.tubeDesignerProfileGeneratedPreviews?.[profileSelectionKey(profile)] ?? view.tubeDesignerProfilePreview;
  let identity = [];
  try { identity = JSON.parse(preview?.key ?? "[]"); } catch { /* no completed preview */ }
  return identity[0] === profileScope(profile) && identity[1] === String(profile.templateId ?? "") && identity[2] === String(profile.id)
    ? preview.response?.profile : profileSnapshot(profile);
}

function profileModelOutdated(view, profile) {
  const snapshot = generatedProfileSnapshot(view, profile);
  const old = snapshot?.parameters ?? profile?.defaultParameters ?? {};
  const values = profilePreviewParameters(view, profile);
  return !!snapshot && [...new Set([...Object.keys(old), ...Object.keys(values)])]
    .some(key => JSON.stringify(old[key]) !== JSON.stringify(values[key]));
}

// Library previews are deliberately unannotated; use the floating section diagram.
export function bindProfileSpecificationAnnotations(_mount, view) {
  if(view.activeAreaId==='profiles' || view.activeAreaId==='tools')view.viewport?.clearSpecificationAnnotations?.();
  return [];
}

function handleProfileSceneAction(context, view, action, target, ops) {
  if(action!=='tube-designer-profile-regenerate')return null;
  const profile=findLibraryProfile(view,view.tubeDesignerSelectedProfileId);
  if(profile&&!view.pending){
    view.tubeDesignerProfileGenerationKey=profilePreviewKey(view,profile);
    scheduleProfileLibraryPreview(context,view,profile);
  }
  return {handled:true};
}


function profilePreviewKey(view, profile) {
  if (!profile?.id) return "";
  const versionToken = profileScope(profile) !== "user"
    ? profile.packageDigest ?? profile.descriptor?.version ?? profile.version ?? ""
    : profile.revision ?? profile.packageDigest ?? "";
  return JSON.stringify([
    profileScope(profile),
    String(profile.templateId ?? ""),
    String(profile.id),
    String(versionToken),
    profilePreviewParameters(view, profile),
  ]);
}


function scheduleProfileLibraryPreview(context, view, profile) {
  if (profile?.available === false) {
    clearProfilePreviewRequest(view);
    view.viewport?.setVisibleEntityIds?.([]);
    hidePreviewProgress();
    setPreviewStatus(profile.error || "管型包不可用", true, false);
    return;
  }
  const key = profilePreviewKey(view, profile);
  if (profileModelOutdated(view, profile) && view.tubeDesignerProfileGenerationKey !== key) {
    const cached = view.tubeDesignerProfileGeneratedPreviews?.[profileSelectionKey(profile)];
    if (cached && !isProfilePreviewResourceApplied(view, profile, cached.response)) scheduleCachedProfilePreview(context, view, profile, key, cached.response);
    bindProfileSpecificationAnnotations(context.mount, view);
    return;
  }
  if (!key) {
    clearProfilePreviewRequest(view);
    view.viewport?.setVisibleEntityIds?.([]);
    view.viewport?.setSelectedObjectIds?.([], "");
    hidePreviewProgress();
    return;
  }
  if (typeof context?.sceneProxy?.invoke !== "function") return;
  queueMicrotask(() => {
    if (!key || view.activeAreaId !== "profiles") {
      if (!key) view.viewport?.setVisibleEntityIds?.([]);
      return;
    }
    if (!isCurrentProfilePreview(view, profile, key)) return;
    if (view.tubeDesignerProfilePreviewRequest?.key === key) {
      restoreProfilePreviewRequestProgress(view.tubeDesignerProfilePreviewRequest);
      if (view.tubeDesignerProfilePreview?.key === key
          && isProfilePreviewResourceApplied(view, profile, view.tubeDesignerProfilePreview.response)) {
        view.viewport?.setVisibleEntityIds?.([profileSelectionKey(profile)]);
      }
      return;
    }
    if (view.tubeDesignerProfilePreview?.key === key) {
      if (isProfilePreviewResourceApplied(view, profile, view.tubeDesignerProfilePreview.response)) {
        view.viewport?.setVisibleEntityIds?.([profileSelectionKey(profile)]);
        return;
      }
      scheduleCachedProfilePreview(context, view, profile, key, view.tubeDesignerProfilePreview.response);
      return;
    }
    clearProfilePreviewRequest(view);
    logProfilePreview(context, "info", `开始生成三维管型：${profileName(profile)}`);
    const operation = Promise.resolve().then(() => invokeSceneProduct(
      context,
      "TubeDesigner.GenerateProfilePreview",
      {
        profileRef: profileRef(profile),
        parameters: profilePreviewParameters(view, profile),
      },
      { timeoutMs: 30000 },
    ));
    const request = {
      key,
      operation,
      progressVisible: false,
      message: "正在生成三维管型…",
      phaseLabel: "计算截面与拉伸体",
      detail: "正在计算截面并创建标准拉伸体",
    };
    view.tubeDesignerProfilePreviewRequest = request;
    const delayedProgress = beginDelayedProfilePreviewProgress(view, profile, request);
    void operation.then(async (response) => {
      if (view.tubeDesignerProfilePreviewRequest !== request
          || !isCurrentProfilePreview(view, profile, key)) return;
      view.tubeDesignerProfilePreview = { key, response };
      request.message = "正在装载三维管型…";
      request.phaseLabel = "装载三维资源";
      request.detail = "三维几何已生成，正在写入中央预览视图";
      restoreProfilePreviewRequestProgress(request);
      await applyProfilePreviewResource(context, view, profile, key, response);
      if (!isCurrentProfilePreview(view, profile, key)) return;
      view.tubeDesignerProfileGeneratedPreviews ??= {};
      view.tubeDesignerProfileGeneratedPreviews[profileSelectionKey(profile)] = { key, response };
      view.tubeDesignerProfileGenerationKey = "";
      bindProfileSpecificationAnnotations(context.mount, view);
      refreshProfileSceneRuntime(context, view, profile);
      await finishProfilePreviewProgress(delayedProgress);
      if (view.tubeDesignerProfilePreviewRequest !== request
          || !isCurrentProfilePreview(view, profile, key)) return;
      request.progressVisible = false;
      setPreviewStatus("三维管型已生成", false, false);
      logProfilePreview(context, "ok", `三维管型已生成：${profileName(profile)}`);
    }).catch(async (error) => {
      if (view.tubeDesignerProfilePreviewRequest !== request) return;
      await finishProfilePreviewProgress(delayedProgress);
      if (view.tubeDesignerProfilePreviewRequest !== request
          || !isCurrentProfilePreview(view, profile, key)) return;
      view.error = `三维管型预览失败：${error?.message ?? error}`;
      request.progressVisible = false;
      setPreviewStatus(view.error, true, false);
      logProfilePreview(context, "error", view.error);
    }).finally(() => {
      delayedProgress.cancelBeforeShow();
      if (view.tubeDesignerProfilePreviewRequest === request) {
        view.tubeDesignerProfilePreviewRequest = null;
        hidePreviewProgress();
      }
    });
  });
}


function refreshProfileSceneRuntime(context, view, profile) {
  const document = context.mount?.ownerDocument;
  const active = document?.activeElement;
  const selection = active && typeof active.selectionStart === "number"
    ? [active.selectionStart, active.selectionEnd] : null;
  const host=floatingParameterDiagramHost(context.mount);
  const dock=host?.querySelector('[data-tube-profile-diagram-dock]');
  const fragment=document?.createElement('template');
  if(fragment){
    fragment.innerHTML=renderProfileLibraryDiagramDock(view,profile);
    const next=fragment.content.firstElementChild;
    if(dock&&next)patchDomNode(dock,next);
    else if(dock)dock.remove();
    else if(next)host?.append(next);
    bindProfileParameterDiagrams(context.mount);
    if (active?.isConnected && active !== document.body) {
      active.focus?.({ preventScroll: true });
      if (selection && typeof active.setSelectionRange === "function") active.setSelectionRange(...selection);
    }
  }
}

// Capture at the moment of a DOM update, not when an asynchronous request starts:
// users may continue scrolling while the model is being generated.
function preserveProfileScroll(element) {
  const positions = [];
  for (let node = element; node; node = node.parentElement) {
    positions.push([node, node.scrollTop, node.scrollLeft]);
  }
  return () => { for (const [node, top, left] of positions) {
    if (Number.isFinite(top)) node.scrollTop = top;
    if (Number.isFinite(left)) node.scrollLeft = left;
  } };
}

function scheduleCachedProfilePreview(context, view, profile, key, response) {
  if (view.tubeDesignerProfilePreviewRequest?.key === key) return;
  clearProfilePreviewRequest(view);
  const operation = Promise.resolve().then(() => applyProfilePreviewResource(
    context, view, profile, key, response,
  ));
  const request = {
    key,
    operation,
    progressVisible: false,
    message: "正在装载三维管型…",
    phaseLabel: "装载缓存资源",
    detail: "正在把已有三维资源写入中央预览视图",
  };
  view.tubeDesignerProfilePreviewRequest = request;
  const delayedProgress = beginDelayedProfilePreviewProgress(view, profile, request);
  void operation.then(async () => {
    if (view.tubeDesignerProfilePreviewRequest !== request) return;
    await finishProfilePreviewProgress(delayedProgress);
    if (view.tubeDesignerProfilePreviewRequest !== request
        || !isCurrentProfilePreview(view, profile, key)) return;
    request.progressVisible = false;
    setPreviewStatus("三维管型已生成", false, false);
  }).catch(async (error) => {
    if (view.tubeDesignerProfilePreviewRequest !== request) return;
    await finishProfilePreviewProgress(delayedProgress);
    if (view.tubeDesignerProfilePreviewRequest !== request
        || !isCurrentProfilePreview(view, profile, key)) return;
    view.error = `三维管型预览失败：${error?.message ?? error}`;
    request.progressVisible = false;
    setPreviewStatus(view.error, true, false);
    logProfilePreview(context, "error", view.error);
  }).finally(() => {
    delayedProgress.cancelBeforeShow();
    if (view.tubeDesignerProfilePreviewRequest === request) {
      view.tubeDesignerProfilePreviewRequest = null;
      hidePreviewProgress();
    }
  });
}


function beginDelayedProfilePreviewProgress(view, profile, request) {
  let timer = 0;
  let resolvePaintedAt = () => {};
  const paintedAt = new Promise((resolve) => {
    resolvePaintedAt = resolve;
    timer = setTimeout(async () => {
      timer = 0;
      if (view.tubeDesignerProfilePreviewRequest !== request
          || !isCurrentProfilePreview(view, profile, request.key)) {
        resolve(null);
        return;
      }
      request.progressVisible = true;
      restoreProfilePreviewRequestProgress(request);
      resolve(await waitForProgressPaint());
    }, PROFILE_PREVIEW_PROGRESS_DELAY_MS);
  });
  const cancelBeforeShow = () => {
    if (!timer) return;
    clearTimeout(timer);
    timer = 0;
    resolvePaintedAt(null);
  };
  request.cancelProgress = cancelBeforeShow;
  return { paintedAt, cancelBeforeShow };
}


async function finishProfilePreviewProgress(delayedProgress) {
  // Fast previews never wait for the delay or the minimum display duration.
  delayedProgress.cancelBeforeShow();
  const paintedAt = await delayedProgress.paintedAt;
  if (Number.isFinite(paintedAt)) {
    await waitMinimum(paintedAt, PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS);
  }
}


function clearProfilePreviewRequest(view) {
  view.tubeDesignerProfilePreviewRequest?.cancelProgress?.();
  view.tubeDesignerProfilePreviewRequest = null;
  hidePreviewProgress();
}


async function applyProfilePreviewResource(context, view, profile, key, response) {
  const geometryId = String(response?.geometryResourceId ?? "");
  const geometryVersion = Number(response?.geometryResourceVersion ?? 0);
  if (!isCurrentProfilePreview(view, profile, key)) return;
  if (!geometryId || !Number.isFinite(geometryVersion) || geometryVersion <= 0) {
    throw new Error("没有返回有效的三维几何资源。");
  }
  if (!view.viewport?.applyViewSnapshot) {
    throw new Error("三维视口尚未准备好。");
  }
  const entityId = profileSelectionKey(profile);
  const revision = `profile-preview:${entityId}:${geometryVersion}`;
  const previousView = view.viewport.getAppliedViewState?.();
  const sameModel = String(previousView?.revision ?? "").startsWith(`profile-preview:${entityId}:`)
    && (previousView?.entityIds ?? []).includes(entityId);
  if (isProfilePreviewResourceApplied(view, profile, response)) {
    view.viewport.setVisibleEntityIds?.([entityId]);
    return;
  }
  const materialId = String(response?.materialResourceId ?? "");
  const materialVersion = Number(response?.materialResourceVersion ?? 0);
  const data = {
    geometry: { url: geometryId, version: geometryVersion },
    geometryKind: 1,
    renderClass: 1,
    visible: true,
    selectable: true,
  };
  if (materialId && materialVersion) {
    data.material = { url: materialId, version: materialVersion };
  }
  const resources = context.sceneProxy?.resources
    ?? view.sceneProxy?.resources
    ?? context.projectProxy?.resources;
  if (typeof resources?.get !== "function") {
    throw new Error("当前场景没有提供三维资源读取能力。");
  }
  const receipt = await withTimeout(view.viewport.applyViewSnapshot({
    revision,
    rows: [{ entityId, data }],
  }, resources), 30000, "三维资源进入视口超时");
  if (!receipt?.applied || receipt.missingGeometryEntityIds?.length
      || (Array.isArray(receipt.entityIds) && !receipt.entityIds.includes(entityId))) {
    throw new Error("三维几何资源未完整进入管型预览视图。");
  }
  if (!isCurrentProfilePreview(view, profile, key)) {
    // The same profile ID can represent different parameter values. Only hide
    // this stale snapshot; never change the visibility of a newer scene.
    if (String(view.viewport.getAppliedViewState?.()?.revision ?? "") === revision) {
      view.viewport.setVisibleEntityIds?.([]);
    }
    return;
  }
  view.viewport.setVisibleEntityIds?.([entityId]);
  if (!sameModel) {
    view.viewport.setSelectedObjectIds?.([], "");
    view.viewport.fitViewForRevision?.(revision, 1.3);
    view.viewport.setStandardView?.("iso");
  }
}


function isCurrentProfilePreview(view, profile, key) {
  return view.activeAreaId === "profiles"
    && view.tubeDesignerSelectedProfileId === profileSelectionKey(profile)
    && profilePreviewKey(view, profile) === key
    && visibleLibraryProfiles(view).some((item) => profileSelectionKey(item) === profileSelectionKey(profile)
      && profilePreviewKey(view, item) === key);
}


function isProfilePreviewResourceApplied(view, profile, response) {
  const geometryVersion = Number(response?.geometryResourceVersion ?? 0);
  if (!geometryVersion || !view?.viewport) return false;
  const entityId = profileSelectionKey(profile);
  if (!entityId) return false;
  const applied = view.viewport.getAppliedViewState?.();
  return String(applied?.revision ?? "") === `profile-preview:${entityId}:${geometryVersion}`
    && (applied?.entityIds ?? []).includes(entityId);
}


function restoreProfilePreviewRequestProgress(request) {
  if (!request?.progressVisible) return;
  setPreviewStatus(
    request.message,
    false,
    true,
    request.phaseLabel,
    request.detail,
  );
}


function setPreviewStatus(
  message,
  isError = false,
  isBusy = false,
  phaseLabel = "生成三维资源",
  detail = "正在计算截面并创建标准拉伸体",
) {
  if (typeof document === "undefined") return;
  const hud = document.querySelector("[data-tube-profile-preview-status]");
  const target = hud?.querySelector("small");
  if (target) {
    target.textContent = message;
    target.classList.toggle("error", Boolean(isError));
  }
  hud?.classList.toggle("is-busy", Boolean(isBusy));
  const progress = hud?.querySelector("[data-tube-profile-preview-progress]");
  if (progress) {
    progress.hidden = !isBusy;
    progress.setAttribute("aria-hidden", isBusy ? "false" : "true");
  }
  const wait = document.querySelector("[data-tube-profile-preview-wait]");
  if (!wait) return;
  wait.hidden = !isBusy;
  wait.setAttribute("aria-hidden", isBusy ? "false" : "true");
  if (!isBusy) return;
  const title = wait.querySelector("[data-tube-profile-preview-wait-title]");
  const description = wait.querySelector("[data-tube-profile-preview-wait-message]");
  const phase = wait.querySelector("[data-tube-profile-preview-wait-phase]");
  const waitProgress = wait.querySelector("[data-tube-profile-preview-wait-progress]");
  if (title) title.textContent = phaseLabel.includes("装载") ? "正在装载三维管型" : "正在生成三维管型";
  if (description) description.textContent = detail || message;
  if (phase) phase.textContent = phaseLabel;
  waitProgress?.setAttribute("aria-valuetext", phaseLabel);
}


function hidePreviewProgress() {
  if (typeof document === "undefined") return;
  const hud = document.querySelector("[data-tube-profile-preview-status]");
  hud?.classList.remove("is-busy");
  const progress = hud?.querySelector("[data-tube-profile-preview-progress]");
  if (progress) {
    progress.hidden = true;
    progress.setAttribute("aria-hidden", "true");
  }
  const wait = document.querySelector("[data-tube-profile-preview-wait]");
  if (wait) {
    wait.hidden = true;
    wait.setAttribute("aria-hidden", "true");
  }
}


function logProfilePreview(context, level, message) {
  const text = String(message ?? "").trim();
  if (!text) return;
  if (typeof context?.onProjectLog === "function") {
    context.onProjectLog(context, level, text);
  }
  if (typeof context?.actions?.log === "function") {
    context.actions.log(level, text);
    return;
  }
  console[level === "error" ? "error" : "log"](text);
}


async function withTimeout(operation, milliseconds, message) {
  let timer = 0;
  try {
    return await Promise.race([
      operation,
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error(message)), milliseconds);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}


function localizedText(value, fallback = "参数") {
  if (typeof value === "string" && value.trim()) return value.trim();
  if (value && typeof value === "object") {
    for (const locale of ["zh-CN", "zh", "en-US", "en"]) {
      if (typeof value[locale] === "string" && value[locale].trim()) return value[locale].trim();
    }
    const text = Object.values(value).find((item) => typeof item === "string" && item.trim());
    if (text) return text.trim();
  }
  return fallback;
}


function renderPackageParameter(definition, values, pending) {
  return renderPackageParameterControl(definition, values, pending).replace('<label', `<label id="tube-profile-field-${escapeAttr(definition.key)}"`);
}

function renderPackageParameterControl(definition, values, pending) {
  pending = pending || !parameterEnabled(definition, values);
  const key = String(definition?.key ?? "");
  const label = localizedText(definition?.uiTitle ?? definition?.displayName, key);
  const value = values[key] ?? definition?.defaultValue ?? "";
  const common = `data-profile-parameter-key="${escapeAttr(key)}" data-tube-profile-editor-parameter="${escapeAttr(key)}" data-tube-profile-value-type="${escapeAttr(definition?.valueType ?? "number")}" data-cam-change-action="tube-designer-profile-preview-change"`;
  if (definition?.valueType === "boolean") {
    return `<label class="tube-designer-field tube-designer-boolean-field${fieldDisplayClass(definition)}"${fieldDisplayStyle(definition)}><span>${escapeText(label)}</span><input type="checkbox" ${common} ${value ? "checked" : ""} ${pending ? "disabled" : ""} /></label>`;
  }
  const options = Array.isArray(definition?.options) ? definition.options : [];
  if (options.length) {
    return `<label class="tube-designer-field is-choice${fieldDisplayClass(definition)}"${fieldDisplayStyle(definition)}><span>${escapeText(label)}</span><select ${common} ${pending ? "disabled" : ""}>${options.map((option) => {
      const optionValue = typeof option === "object" ? option?.value : option;
      const optionLabel = typeof option === "object" ? localizedText(option?.displayName ?? option?.label, optionValue) : option;
      return `<option value="${escapeAttr(optionValue)}" ${String(optionValue) === String(value) ? "selected" : ""}>${escapeText(optionLabel)}</option>`;
    }).join("")}</select></label>`;
  }
  const type = definition?.valueType === "string" ? "text" : "number";
  const attributes = [`type="${type}"`, `value="${escapeAttr(value)}"`, common];
  if (definition?.min != null || definition?.minimum != null) attributes.push(`min="${escapeAttr(definition.min ?? definition.minimum)}"`);
  if (definition?.max != null || definition?.maximum != null) attributes.push(`max="${escapeAttr(definition.max ?? definition.maximum)}"`);
  if (type === "number") attributes.push(`step="${escapeAttr(definition?.step ?? (definition?.valueType === "integer" ? 1 : "any"))}"`);
  if (pending) attributes.push("disabled");
  return `<label class="tube-designer-field ${type === 'text' ? 'is-string' : 'is-number'}${fieldDisplayClass(definition)}"${fieldDisplayStyle(definition)} title="${escapeAttr(localizedText(definition.uiHelp, label))}"><span>${escapeText(label)}</span><input ${attributes.join(" ")} /></label>`;
}

function renderProfileParameterGroups(profile, definitions, values, pending) {
  const descriptor = profile.descriptor ?? {};
  const display = templateDisplayView(descriptor, 'right');
  const fields = applyTemplateFieldDisplay(descriptor, definitions, 'right').map(d => ({ ...d,
    uiTitle: display.fields[d.key]?.title, uiHelp: display.fields[d.key]?.help,
    group: display.fields[d.key]?.group ?? '' }));
  const keys = [...new Set(fields.map(d => d.group))].sort((a,b) => (display.groups[a]?.order ?? 0) - (display.groups[b]?.order ?? 0));
  return keys.map(key => {
    const group = display.groups[key];
    const groupFields = fields.filter(d => d.group === key).sort((a,b)=>(a.order??0)-(b.order??0));
    const explicitAdvancedGroup = key === 'advanced' && !!group;
    const groupKey = `advanced:profile:${profile.id}:${key}`;
    const content = `<div class="tube-profile-library-field-grid">${renderParameterLevels(groupFields, d=>renderPackageParameter(d, values, pending), {key:`profile:${profile.id}:${key}`,gridClass:'tube-profile-library-field-grid',flattenAdvanced:explicitAdvancedGroup})}</div>`;
    const advancedAttributes = explicitAdvancedGroup
      ? ` data-parameter-advanced data-parameter-advanced-key="${escapeAttr(groupKey)}"`
      : '';
    return group ? `<details id="tube-profile-display-${escapeAttr(key)}" class="tube-profile-library-parameter-group" data-profile-display-group="${escapeAttr(key)}"${advancedAttributes} ${group.defaultOpen === false ? '' : 'open'}><summary>${escapeText(localizedText(group.title,key))}</summary>${content}</details>` : content;
  }).join('');
}


function matchesParameterVisibility(condition, values) {
  return matchesParameterCondition(condition, values);
}


function renderProfilePackageImportDialog(view) {
  const state = view?.tubeDesignerProfilePackageImportDialog;
  if (!state) return "";
  const fileName = String(state.sourcePath ?? "").split(/[\\/]/).pop() || "管型包";
  return `<div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop" role="presentation">
    <section class="tube-designer-preset-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-profile-package-import-title">
      <header class="tube-designer-dialog-header">
        <div><strong id="tube-profile-package-import-title">导入程式管型包</strong><span>一个 .ittt 对应一个管型，至少包含 profile.json 和 profile.py</span></div>
        <button class="tube-designer-dialog-close" data-cam-action="tube-designer-profile-package-import-cancel" aria-label="关闭" ${view?.pending ? "disabled" : ""}>×</button>
      </header>
      <div class="tube-designer-preset-dialog-body">
        <div class="tube-designer-imported-profile-summary"><strong>${escapeText(fileName)}</strong><span>.ittt 程式管型包</span><small>使用产品固定 magic number 自动校验密码</small></div>
        <p>管型包包含可执行脚本，请只导入可信来源。</p>
      </div>
      <footer class="tube-designer-preset-dialog-footer">
        <span></span>
        <button class="tube-designer-secondary" data-cam-action="tube-designer-profile-package-import-cancel" ${view?.pending ? "disabled" : ""}>取消</button>
        <button class="tube-designer-primary" data-cam-action="tube-designer-profile-package-import-confirm" ${view?.pending ? "disabled" : ""}>导入管型</button>
      </footer>
    </section>
  </div>`;
}




function resolveBridge(context) {
  return context.appProxy?.bridge ?? context.productProxy?.bridge ?? context.sceneProxy?.bridge ?? null;
}


async function chooseProfilePackage(context, view, ops) {
  if (view.pending) return null;
  const bridge = resolveBridge(context);
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力。";
    ops.renderProject(context, view);
    return null;
  }
  const sourcePath = String(await bridge.openFileDialog({
    title: "选择程式管型包",
    filters: [{ name: "管型包", extensions: ["ittt"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  view.tubeDesignerProfilePackageImportDialog = { sourcePath };
  view.error = "";
  ops.renderProject(context, view);
  return sourcePath;
}


async function confirmProfilePackageImport(context, view, ops) {
  const state = view.tubeDesignerProfilePackageImportDialog;
  if (view.pending || !state?.sourcePath) return null;
  return runProfileTask(context, view, ops, {
    title: "正在导入程式管型包",
    message: "正在解包、校验参数和生成默认截面",
  }, async () => {
    const response = await invokeProduct(context, "TubeDesigner.ImportProfilePackage", {
      sourcePath: state.sourcePath,
    }, { timeoutMs: 120000 });
    const profile = response?.profile;
    if (!profile?.id) throw new Error("导入管型包后没有返回记录标识。" );
    upsertProfile(view, profile);
    const libraryState = profileLibraryState(view);
    libraryState.scope = "user";
    libraryState.type = profileLibraryType(profile);
    libraryState.search = "";
    view.tubeDesignerSelectedProfileId = `user:${profile.id}`;
    view.tubeDesignerProfilePackageImportDialog = null;
    ops.showNotice(context, view, `已新增程式管型“${profile.name}”。`);
    return profile;
  });
}


async function importDxfIntoLibrary(context, view, ops) {
  if (view.pending) return null;
  const bridge = resolveBridge(context);
  if (typeof bridge?.openFileDialog !== "function") {
    view.error = "当前宿主没有提供文件选择能力。";
    ops.renderProject(context, view);
    return null;
  }
  const sourcePath = String(await bridge.openFileDialog({
    title: "选择 定式管型截面",
    filters: [{ name: "DXF 二维截面", extensions: ["dxf"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  return runProfileTask(context, view, ops, {
    title: "正在新增 定式管型",
    message: "正在识别精确轮廓并保存到我的管型",
  }, async () => {
    const imported = await invokeProduct(context, "TubeDesigner.ImportProfileDxf", {
      sourcePath,
    }, { timeoutMs: 120000 });
    const profile = imported?.profile;
    if (!profile?.contours?.length) throw new Error("DXF 没有返回有效截面。" );
    const savedResponse = await invokeProduct(context, "TubeDesigner.SaveImportedProfile", {
      id: "",
      revision: 0,
      name: profile.name ?? profile.sourceFileName ?? "定式管型",
      profile,
    }, { timeoutMs: 120000 });
    const saved = savedResponse?.profile;
    if (!saved?.id) throw new Error("保存 定式管型后没有返回记录标识。" );
    upsertProfile(view, saved);
    const libraryState = profileLibraryState(view);
    libraryState.scope = "user";
    libraryState.type = profileLibraryType(saved);
    libraryState.search = "";
    view.tubeDesignerSelectedProfileId = `user:${saved.id}`;
    ops.showNotice(context, view, `已新增 定式管型“${saved.name}”。`);
    return saved;
  });
}


function readEditor(context, profile) {
  const editor = document.querySelector(`[data-tube-profile-library-editor][data-tube-designer-profile-id="${cssEscape(profile.id)}"]`);
  const name = String(editor?.querySelector("[data-tube-profile-editor-name]")?.value ?? "").trim();
  if (!name) throw new Error("请填写管型名称。");
  const parameters = readEditorParameters(editor);
  void context;
  return { name, parameters };
}


function readEditorParameters(editor) {
  const parameters = {};
  for (const input of editor?.querySelectorAll?.("[data-tube-profile-editor-parameter]") ?? []) {
    const key = String(input.dataset?.tubeProfileEditorParameter ?? "");
    const valueType = String(input.dataset?.tubeProfileValueType ?? "number");
    if (valueType === "boolean") parameters[key] = Boolean(input.checked);
    else if (valueType === "number" || valueType === "integer") {
      const number = Number(input.value);
      if (!Number.isFinite(number) || (valueType === "integer" && !Number.isInteger(number))) {
        input.focus?.();
        throw new Error(`管型参数“${key}”不是有效数值。`);
      }
      parameters[key] = number;
    } else parameters[key] = String(input.value ?? "");
  }
  return parameters;
}


function updateProfilePreviewDraft(context, view, target, ops) {
  if (view.pending) return;
  const editor = target?.closest?.("[data-tube-profile-library-editor]")
    ?? document.querySelector("[data-tube-profile-library-editor]");
  const id = String(editor?.dataset?.tubeDesignerProfileId ?? "");
  const key = String(editor?.dataset?.tubeDesignerProfileKey ?? "");
  const profile = findLibraryProfile(
    view, key || id, editor?.dataset?.tubeDesignerProfileScope ?? "",
  );
  if (!profile) return;
  try {
    view.tubeDesignerProfileDrafts ??= {};
    const profileKey = profileSelectionKey(profile);
    view.tubeDesignerProfileDrafts[profileKey] = {
      name: String(editor?.querySelector("[data-tube-profile-editor-name]")?.value ?? profileName(profile)),
      parameters: {
        ...(profile.defaultParameters ?? {}),
        ...(view.tubeDesignerProfileDrafts[profileKey]?.parameters ?? {}),
        ...readEditorParameters(editor),
      },
    };
    view.error = "";
    // Keep the viewport and both pane DOM trees alive. Only dependent parameter
    // controls need replacing when switching a submodel/manufacturing route.
    const restoreScroll = preserveProfileScroll(editor);
    const values = view.tubeDesignerProfileDrafts[profileKey].parameters;
    const definitions = (profile.descriptor?.parameters ?? []).filter(d => parameterVisible(d, values));
    const list = editor?.querySelector?.(".tube-profile-library-parameter-list");
    if (list?.cloneNode) {
      const next = list.cloneNode(false);
      next.innerHTML = renderProfileParameterGroups(profile, definitions, values, false);
      patchDomNode(list, next);
    }
    const hud = context.mount?.querySelector?.("[data-tube-profile-preview-status] span");
    if (hud) hud.textContent = `版本 ${String(profile?.descriptor?.version ?? profile?.version ?? "").trim() || "—"}`;
    restoreScroll();
    clearProfilePreviewRequest(view);
    bindProfileSpecificationAnnotations(context.mount, view);
    view.tubeDesignerProfileGenerationKey = profilePreviewKey(view, profile);
    refreshProfileSceneRuntime(context, view, profile);
    scheduleProfileLibraryPreview(context, view, profile);
  } catch (error) {
    view.error = error?.message ?? String(error);
    setPreviewStatus(view.error, true, false);
  }
}


async function saveLibraryProfile(context, view, target, ops) {
  if (view.pending) return null;
  const id = String(target?.dataset?.tubeDesignerProfileId ?? "");
  const profile = findLibraryProfile(view, target?.dataset?.tubeDesignerProfileKey ?? id, "user");
  if (!profile || profileScope(profile) !== "user") return null;
  let editor;
  try {
    editor = readEditor(context, profile);
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops.renderProject(context, view);
    return null;
  }
  const editable = isParametricProfile(profile);
  return runProfileTask(context, view, ops, {
    title: editable ? "正在更新程式管型" : "正在保存管型名称",
    message: editable ? "正在校验参数并重新生成默认截面" : "正在更新我的管型",
  }, async () => {
    const response = await invokeProduct(
      context,
      editable ? "TubeDesigner.UpdateProfilePackage" : "TubeDesigner.RenameProfile",
      editable
        ? { id, revision: Number(profile.revision ?? 0), name: editor.name, parameters: editor.parameters }
        : { id, revision: Number(profile.revision ?? 0), name: editor.name },
      { timeoutMs: 120000 },
    );
    const saved = response?.profile;
    if (!saved?.id) throw new Error("保存管型后没有返回记录标识。" );
    upsertProfile(view, saved);
    if (view.tubeDesignerProfileDrafts) delete view.tubeDesignerProfileDrafts[profileSelectionKey(profile)];
    view.tubeDesignerProfilePreview = null;
    ops.showNotice(context, view, `已保存“${saved.name}”。`);
    return saved;
  });
}


async function deleteLibraryProfile(context, view, target, ops) {
  if (view.pending) return null;
  const id = String(target?.dataset?.tubeDesignerProfileId ?? "");
  const profile = findLibraryProfile(view, target?.dataset?.tubeDesignerProfileKey ?? id, "user");
  if (!profile || profileScope(profile) !== "user") return null;
  if (!await confirmWithoutTitle(
    `确定删除“${profile.name}”吗？\n已经使用该管型的产品不会受影响。`,
    "删除",
  )) return null;
  return runProfileTask(context, view, ops, {
    title: "正在删除管型",
    message: "正在从我的管型中移除记录",
  }, async () => {
    const response = await invokeProduct(context, "TubeDesigner.DeleteProfile", {
      id,
      revision: Number(profile.revision ?? 0),
    });
    if (!response?.deleted) throw new Error("管型未能删除。" );
    view.tubeDesignerUserData.profiles = view.tubeDesignerUserData.profiles
      .filter((item) => String(item?.id ?? "") !== id);
    if (view.tubeDesignerProfileDrafts) delete view.tubeDesignerProfileDrafts[profileSelectionKey(profile)];
    view.tubeDesignerProfilePreview = null;
    ensureSelectedProfile(view, visibleLibraryProfiles(view));
    ops.showNotice(context, view, `已删除“${profile.name}”；历史产品不受影响。`);
    return response;
  });
}


async function exportLibraryProfile(context, view, target, format, ops) {
  if (view.pending) return null;
  const key = String(target?.dataset?.tubeDesignerProfileKey
    ?? view.tubeDesignerSelectedProfileId
    ?? "");
  const profile = findLibraryProfile(
    view,
    key || target?.dataset?.tubeDesignerProfileId,
    target?.dataset?.tubeDesignerProfileScope ?? "",
  );
  if (!profile) {
    view.error = "请先选择一个管型。";
    ops.renderProject(context, view);
    return null;
  }
  let parameters = {};
  try {
    const profileKey = profileSelectionKey(profile);
    const editor = document.querySelector(`[data-tube-profile-library-editor][data-tube-designer-profile-key="${cssEscape(profileKey)}"]`);
    if (isParametricProfile(profile)) {
      parameters = editor ? readEditorParameters(editor) : profilePreviewParameters(view, profile);
    }
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops.renderProject(context, view);
    return null;
  }
  const bridge = resolveBridge(context);
  if (typeof bridge?.openDirectoryDialog !== "function") {
    view.error = "当前宿主没有提供目录选择能力。";
    ops.renderProject(context, view);
    return null;
  }
  const targetDirectory = String(await bridge.openDirectoryDialog({
    title: format === "dxf" ? "选择截面 DXF 导出目录" : "选择管子 STEP 导出目录",
    initialDirectory: view.tubeDesignerProfileExportDirectory ?? "",
  }) ?? "").trim();
  if (!targetDirectory) return null;
  view.tubeDesignerProfileExportDirectory = targetDirectory;
  return runProfileTask(context, view, ops, {
    title: format === "dxf" ? "正在导出截面 DXF" : "正在导出管子 STEP",
    message: format === "dxf" ? "正在写入精确二维轮廓" : "正在生成标准拉伸体",
  }, async () => {
    const response = await invokeSceneProduct(context, "TubeDesigner.ExportProfile", {
      profileRef: profileRef(profile),
      parameters,
      targetDirectory,
      format,
    }, { timeoutMs: 120000 });
    if (!response?.path) throw new Error("导出完成但没有返回文件路径。" );
    ops.showNotice(
      context,
      view,
      `${format === "dxf" ? "截面 DXF" : "管子 STEP"} 已导出：${response.path}`,
    );
    return response;
  });
}


async function runProfileTask(context, view, ops, progress, operation) {
  if (view.pending) return null;
  view.pending = true;
  view.error = "";
  view.progress = {
    completed: 0,
    total: 1,
    ...progress,
    detail: progress?.detail ?? progress?.message ?? "",
  };
  ops.renderProject(context, view);
  const progressPaintedAt = waitForProgressPaint();
  try {
    return await operation();
  } catch (error) {
    view.error = error?.message ?? String(error);
    throw error;
  } finally {
    await waitMinimum(await progressPaintedAt, MINIMUM_PROGRESS_MS);
    view.pending = false;
    view.progress = null;
    ops.renderProject(context, view);
  }
}


function upsertProfile(view, profile) {
  view.tubeDesignerUserData ??= { customers: [], parameterPresets: [], profiles: [], profileId: "" };
  const profiles = Array.isArray(view.tubeDesignerUserData.profiles)
    ? view.tubeDesignerUserData.profiles : [];
  const index = profiles.findIndex((item) => String(item?.id ?? "") === String(profile?.id ?? ""));
  if (index >= 0) profiles[index] = profile;
  else profiles.push(profile);
  view.tubeDesignerUserData.profiles = profiles;
}


function invokeProduct(context, method, payload, options = {}) {
  if (typeof context.productProxy?.invoke !== "function") {
    throw new Error(`${method} requires an active product`);
  }
  return context.productProxy.invoke(method, payload, { timeoutMs: 30000, ...options });
}


function invokeSceneProduct(context, method, payload, options = {}) {
  if (typeof context.sceneProxy?.invoke !== "function") {
    throw new Error(`${method} requires an active project scene`);
  }
  return context.sceneProxy.invoke(method, payload, { timeoutMs: 30000, ...options });
}


function performanceNow() {
  return typeof globalThis.performance?.now === "function" ? globalThis.performance.now() : Date.now();
}


async function waitForProgressPaint() {
  if (typeof globalThis.requestAnimationFrame !== "function") return performanceNow();
  await new Promise((resolve) => {
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      clearTimeout(fallback);
      resolve();
    };
    const fallback = setTimeout(finish, 100);
    globalThis.requestAnimationFrame(() => globalThis.requestAnimationFrame(finish));
  });
  return performanceNow();
}


async function waitMinimum(startedAt, minimum) {
  const remaining = minimum - (performanceNow() - startedAt);
  if (remaining > 0) await new Promise((resolve) => setTimeout(resolve, remaining));
}


function cssEscape(value) {
  if (typeof globalThis.CSS?.escape === "function") return globalThis.CSS.escape(String(value));
  return String(value).replaceAll("\\", "\\\\").replaceAll('"', '\\"');
}
