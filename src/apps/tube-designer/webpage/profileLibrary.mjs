import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";


export const PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS = 500;
export const PROFILE_PREVIEW_CACHE_PROGRESS_DELAY_MS = 160;
const MINIMUM_PROGRESS_MS = 500;
const DEFAULT_PROFILE_PREVIEW_LENGTH = 1000;


export function renderProfileLibraryLeftPane(_context, view) {
  const profiles = libraryProfiles(view);
  const selectedId = ensureSelectedProfile(view, profiles);
  const systemProfiles = profiles.filter((profile) => profileScope(profile) === "system");
  const userProfiles = profiles.filter((profile) => profileScope(profile) === "user");
  const renderCard = (profile) => {
    const scope = profileScope(profile);
    const selectionKey = profileSelectionKey(profile);
    const editable = isParametricProfile(profile);
    return `<button type="button" class="tube-profile-library-card ${selectionKey === selectedId ? "selected" : ""}" data-cam-action="tube-designer-profile-library-select" data-tube-designer-profile-key="${escapeAttr(selectionKey)}" data-tube-designer-profile-scope="${escapeAttr(scope)}" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>
      <span class="tube-profile-library-card-icon ${scope === "system" ? "is-system" : ""}">${scope === "system" ? "S" : (editable ? "P" : "D")}</span>
      <span class="tube-profile-library-card-copy">
        <strong>${escapeText(profileName(profile))}</strong>
        <small>${scope === "system" ? "系统内置 · 参数化" : (editable ? "可编辑管型包" : "DXF 冻结截面")}</small>
        <em>${escapeText(profileSpecification(profile))}</em>
      </span>
      <i aria-hidden="true"></i>
    </button>`;
  };
  const renderGroup = (scope, title, items) => `<details class="tube-profile-library-group" data-tube-profile-library-group="${scope}" open>
    <summary><strong>${title}</strong><span>${items.length} 个</span></summary>
    <div class="tube-profile-library-group-content">
      ${items.length ? items.map(renderCard).join("") : `<div class="tube-profile-library-empty compact">
        <strong>${scope === "system" ? "没有可用的系统管型" : "还没有我的管型"}</strong>
        <span>${scope === "system" ? "请检查内置管型资源是否完整。" : "可导入可编辑管型包，或导入 DXF 冻结截面。"}</span>
      </div>`}
    </div>
  </details>`;
  return `<div class="tube-designer-panel tube-profile-library-panel">
    <div class="tube-designer-heading tube-profile-library-heading">
      <div><strong>管型</strong><span>${systemProfiles.length} 个系统内置 · ${userProfiles.length} 个我的管型</span></div>
    </div>
    <div class="tube-profile-library-list">
      ${renderGroup("system", "系统内置", systemProfiles)}
      ${renderGroup("user", "我的管型", userProfiles)}
    </div>
  </div>`;
}


export function renderProfileLibraryRightPane(_context, view) {
  const profiles = libraryProfiles(view);
  const selectedId = ensureSelectedProfile(view, profiles);
  const profile = profiles.find((item) => profileSelectionKey(item) === selectedId);
  const dialog = renderProfilePackageImportDialog(view);
  if (!profile) {
    return `<div class="tube-designer-panel tube-profile-library-editor-empty">
      <div class="tube-designer-heading"><strong>管型属性</strong><span>尚未选择管型</span></div>
      <div class="tube-designer-empty">导入后可在这里修改名称；可编辑管型包还可以修改默认参数。</div>
    </div>${dialog}`;
  }
  const system = profileScope(profile) === "system";
  const editable = isParametricProfile(profile);
  const allDefinitions = editable && Array.isArray(profile?.descriptor?.parameters)
    ? profile.descriptor.parameters : [];
  const profileKey = profileSelectionKey(profile);
  const draft = view?.tubeDesignerProfileDrafts?.[profileKey] ?? null;
  const values = draft?.parameters ?? profile.defaultParameters ?? {};
  const definitions = allDefinitions.filter((definition) => matchesParameterVisibility(
    definition?.visibleWhen, values,
  ));
  const snapshot = view?.tubeDesignerProfilePreview?.key === profilePreviewKey(view, profile)
    ? view.tubeDesignerProfilePreview.response?.profile
    : profileSnapshot(profile);
  const previewLength = normalizedPreviewLength(view);
  return `<div class="tube-designer-panel tube-profile-library-editor" data-tube-profile-library-editor data-tube-designer-profile-key="${escapeAttr(profileKey)}" data-tube-designer-profile-scope="${escapeAttr(profileScope(profile))}" data-tube-designer-profile-id="${escapeAttr(profile.id)}">
    <div class="tube-designer-heading">
      <strong>${system ? "系统内置管型" : (editable ? "可编辑管型" : "DXF 管型")}</strong>
      <span>${system ? "参数可编辑；系统原定义保持不变" : (editable ? "参数变化会生成新的默认截面" : "截面几何已冻结")}</span>
    </div>
    <div class="tube-profile-library-editor-body">
      ${system
        ? `<div class="tube-profile-library-system-name"><span>管型名称</span><strong>${escapeText(profileName(profile))}</strong><small>系统内置名称不可修改</small></div>`
        : `<label class="tube-designer-field wide"><span>管型名称</span><input type="text" data-tube-profile-editor-name value="${escapeAttr(profileName(profile))}" maxlength="120" ${view?.pending ? "disabled" : ""} /></label>`}
      <div class="tube-designer-imported-profile-summary">
        <strong>${escapeText(system ? "系统内置资源" : (profile.sourceFileName ?? "管型资源"))}</strong>
        <span>${escapeText(profileSpecification(profile))}</span>
        <small>${editable ? `${escapeText(profile?.descriptor?.id ?? "")} · 版本 ${escapeText(profile?.descriptor?.version ?? "")}` : `${escapeText(profile.sourceUnit ?? "毫米")} · ${Number(profile.contourCount ?? profile.contours?.length ?? 0)} 条轮廓`}</small>
      </div>
      ${editable ? `<section class="tube-profile-library-parameter-section">
        <header><strong>${system ? "预览参数" : "默认参数"}</strong><span>${system ? "仅影响当前预览与本次导出" : "产品选择此管型时仍可单独修改"}</span></header>
        <div class="tube-profile-library-parameter-list">${definitions.map((definition) => renderPackageParameter(definition, values, view?.pending)).join("")}</div>
      </section>` : `<p class="tube-profile-library-frozen-note">DXF 描述什么就使用什么；这里只允许修改名称。</p>`}
      ${system ? `<p class="tube-profile-library-system-note">系统内置管型不可重命名或删除。修改参数只会生成当前预览和导出结果，不会覆盖系统定义。</p>` : ""}
      <section class="tube-profile-library-preview-section">
        <header><strong>截面与标准管预览</strong><span>中央显示可旋转、缩放的三维拉伸体</span></header>
        <div class="tube-profile-library-miniature">${snapshot ? renderProfileSvg(snapshot) : ""}</div>
        <label class="tube-designer-field wide"><span>预览与 STEP 长度（mm）</span><input type="number" min="1" max="100000" step="1" value="${escapeAttr(previewLength)}" data-tube-profile-preview-length data-cam-change-action="tube-designer-profile-preview-change" ${view?.pending ? "disabled" : ""} /></label>
        <div class="tube-profile-library-export-actions">
          <button class="tube-designer-secondary" data-cam-action="tube-designer-profile-export-dxf" data-tube-designer-profile-key="${escapeAttr(profileKey)}" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>导出截面 DXF</button>
          <button class="tube-designer-secondary" data-cam-action="tube-designer-profile-export-step" data-tube-designer-profile-key="${escapeAttr(profileKey)}" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>导出管子 STEP</button>
        </div>
      </section>
    </div>
    ${system ? "" : `<footer class="tube-profile-library-editor-footer">
      <button class="tube-designer-danger" data-cam-action="tube-designer-profile-library-delete" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>删除管型</button>
      <button class="tube-designer-primary" data-cam-action="tube-designer-profile-library-save" data-tube-designer-profile-id="${escapeAttr(profile.id)}" ${view?.pending ? "disabled" : ""}>${editable ? "保存名称和默认参数" : "保存名称"}</button>
    </footer>`}
  </div>${dialog}`;
}


export function renderProfileLibraryViewportOverlay(context, view) {
  const profiles = libraryProfiles(view);
  const selectedId = ensureSelectedProfile(view, profiles);
  const profile = profiles.find((item) => profileSelectionKey(item) === selectedId);
  scheduleProfileLibraryPreview(context, view, profile);
  return `<div class="tube-profile-library-preview-hud" data-tube-profile-preview-status>
    <strong>${escapeText(profile ? profileName(profile) : "三维管型预览")}</strong>
    <span>${profile ? `${escapeText(profileSpecification(profile))} · 长度 ${escapeText(normalizedPreviewLength(view))} mm` : "请从左侧选择或新增管型"}</span>
    ${profile ? "<small>拖动旋转 · 滚轮缩放</small>" : ""}
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


export async function handleProfileLibraryAction(context, view, action, target, ops) {
  if (action === "tube-designer-profile-library-select") {
    if (!view.pending) {
      view.tubeDesignerSelectedProfileId = String(
        target?.dataset?.tubeDesignerProfileKey
        ?? `${target?.dataset?.tubeDesignerProfileScope ?? "user"}:${target?.dataset?.tubeDesignerProfileId ?? ""}`,
      );
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


function libraryProfiles(view) {
  const system = (Array.isArray(view?.tubeDesignerSystemProfiles)
    ? view.tubeDesignerSystemProfiles : [])
    .map((profile) => ({ ...profile, libraryScope: "system" }));
  const user = (Array.isArray(view?.tubeDesignerUserData?.profiles)
    ? view.tubeDesignerUserData.profiles : [])
    .map((profile) => ({ ...profile, libraryScope: "user" }));
  const byName = (left, right) => profileName(left).localeCompare(profileName(right), "zh-CN");
  return [...system.sort(byName), ...user.sort(byName)];
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
  return next;
}


function profileScope(profile) {
  return String(profile?.libraryScope ?? profile?.scope ?? "user") === "system"
    ? "system" : "user";
}


function profileSelectionKey(profile) {
  return `${profileScope(profile)}:${String(profile?.id ?? "")}`;
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


function profileRef(profile) {
  return { scope: profileScope(profile), id: String(profile?.id ?? "") };
}


function profileName(profile) {
  return String(
    profile?.name
    ?? profile?.previewProfile?.name
    ?? localizedText(profile?.descriptor?.displayName, "未命名管型"),
  );
}


function isParametricProfile(profile) {
  return profileScope(profile) === "system" || profileType(profile) === "parametric-package";
}


function profileType(profile) {
  return String(profile?.profileType ?? profile?.kind ?? "imported-dxf");
}


function profileSnapshot(profile) {
  return isParametricProfile(profile)
    ? profile?.previewProfile ?? profile?.profile ?? (profile?.contours ? profile : null)
    : profile ?? null;
}


function profileSpecification(profile) {
  return String(profileSnapshot(profile)?.specification ?? profile?.specification ?? "");
}


function normalizedPreviewLength(view) {
  const value = Number(view?.tubeDesignerProfilePreviewLength ?? DEFAULT_PROFILE_PREVIEW_LENGTH);
  const normalized = Number.isFinite(value)
    ? Math.min(100000, Math.max(1, value))
    : DEFAULT_PROFILE_PREVIEW_LENGTH;
  if (view) view.tubeDesignerProfilePreviewLength = normalized;
  return normalized;
}


function profilePreviewParameters(view, profile) {
  return view?.tubeDesignerProfileDrafts?.[profileSelectionKey(profile)]?.parameters
    ?? profile?.defaultParameters
    ?? {};
}


function profilePreviewKey(view, profile) {
  if (!profile?.id) return "";
  const versionToken = profileScope(profile) === "system"
    ? profile.packageDigest ?? profile.descriptor?.version ?? profile.version ?? ""
    : profile.revision ?? profile.packageDigest ?? "";
  return JSON.stringify([
    profileScope(profile),
    String(profile.id),
    String(versionToken),
    normalizedPreviewLength(view),
    profilePreviewParameters(view, profile),
  ]);
}


function scheduleProfileLibraryPreview(context, view, profile) {
  const key = profilePreviewKey(view, profile);
  if (!key || typeof context?.sceneProxy?.invoke !== "function") return;
  queueMicrotask(() => {
    if (!key || view.activeAreaId !== "profiles") {
      if (!key) view.viewport?.setVisibleEntityIds?.([]);
      return;
    }
    if (profilePreviewKey(view, profile) !== key) return;
    if (view.tubeDesignerProfilePreview?.key === key) {
      if (isProfilePreviewResourceApplied(view, profile, view.tubeDesignerProfilePreview.response)) {
        view.viewport?.setVisibleEntityIds?.([profileSelectionKey(profile)]);
        return;
      }
      scheduleCachedProfilePreview(context, view, profile, key, view.tubeDesignerProfilePreview.response);
      return;
    }
    if (view.tubeDesignerProfilePreviewRequest?.key === key) {
      restoreProfilePreviewRequestProgress(view.tubeDesignerProfilePreviewRequest);
      return;
    }
    logProfilePreview(context, "info", `开始生成三维管型：${profileName(profile)}`);
    const operation = Promise.resolve().then(() => invokeSceneProduct(
      context,
      "TubeDesigner.GenerateProfilePreview",
      {
        profileRef: profileRef(profile),
        parameters: profilePreviewParameters(view, profile),
        length: normalizedPreviewLength(view),
      },
      { timeoutMs: 30000 },
    ));
    const request = {
      key,
      operation,
      progressVisible: true,
      message: "正在生成三维管型…",
      phaseLabel: "计算截面与拉伸体",
      detail: "正在计算截面并创建标准拉伸体",
    };
    view.tubeDesignerProfilePreviewRequest = request;
    restoreProfilePreviewRequestProgress(request);
    const progressPaintedAt = waitForProgressPaint();
    void operation.then(async (response) => {
      if (view.tubeDesignerProfilePreviewRequest !== request) return;
      view.tubeDesignerProfilePreview = { key, response };
      request.message = "正在装载三维管型…";
      request.phaseLabel = "装载三维资源";
      request.detail = "三维几何已生成，正在写入中央预览视图";
      restoreProfilePreviewRequestProgress(request);
      await applyProfilePreviewResource(context, view, profile, key, response);
      await waitMinimum(await progressPaintedAt, PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS);
      if (view.tubeDesignerProfilePreviewRequest !== request
          || profilePreviewKey(view, profile) !== key || view.activeAreaId !== "profiles") return;
      request.progressVisible = false;
      setPreviewStatus("三维管型已生成", false, false);
      logProfilePreview(context, "ok", `三维管型已生成：${profileName(profile)}`);
    }).catch(async (error) => {
      if (view.tubeDesignerProfilePreviewRequest !== request) return;
      await waitMinimum(await progressPaintedAt, PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS);
      if (view.tubeDesignerProfilePreviewRequest !== request
          || profilePreviewKey(view, profile) !== key || view.activeAreaId !== "profiles") return;
      view.error = `三维管型预览失败：${error?.message ?? error}`;
      request.progressVisible = false;
      setPreviewStatus(view.error, true, false);
      logProfilePreview(context, "error", view.error);
    }).finally(() => {
      if (view.tubeDesignerProfilePreviewRequest === request) {
        view.tubeDesignerProfilePreviewRequest = null;
        hidePreviewProgress();
      }
    });
  });
}


function scheduleCachedProfilePreview(context, view, profile, key, response) {
  if (view.tubeDesignerProfilePreviewRequest?.key === key) return;
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
  const delayedProgress = beginDelayedCachedPreviewProgress(view, request);
  void operation.then(async () => {
    delayedProgress.cancelBeforeShow();
    if (view.tubeDesignerProfilePreviewRequest !== request) return;
    const progressPaintedAt = await delayedProgress.paintedAt;
    if (Number.isFinite(progressPaintedAt)) {
      await waitMinimum(progressPaintedAt, PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS);
    }
    if (view.tubeDesignerProfilePreviewRequest !== request
        || profilePreviewKey(view, profile) !== key || view.activeAreaId !== "profiles") return;
    request.progressVisible = false;
    setPreviewStatus("三维管型已生成", false, false);
  }).catch(async (error) => {
    delayedProgress.cancelBeforeShow();
    if (view.tubeDesignerProfilePreviewRequest !== request) return;
    const progressPaintedAt = await delayedProgress.paintedAt;
    if (Number.isFinite(progressPaintedAt)) {
      await waitMinimum(progressPaintedAt, PROFILE_PREVIEW_PROGRESS_MINIMUM_VISIBLE_MS);
    }
    if (view.tubeDesignerProfilePreviewRequest !== request
        || profilePreviewKey(view, profile) !== key || view.activeAreaId !== "profiles") return;
    view.error = `三维管型预览失败：${error?.message ?? error}`;
    request.progressVisible = false;
    setPreviewStatus(view.error, true, false);
    logProfilePreview(context, "error", view.error);
  }).finally(() => {
    if (view.tubeDesignerProfilePreviewRequest === request) {
      view.tubeDesignerProfilePreviewRequest = null;
      hidePreviewProgress();
    }
  });
}


function beginDelayedCachedPreviewProgress(view, request) {
  let timer = 0;
  let resolvePaintedAt = () => {};
  const paintedAt = new Promise((resolve) => {
    resolvePaintedAt = resolve;
    timer = setTimeout(async () => {
      timer = 0;
      if (view.tubeDesignerProfilePreviewRequest !== request
          || view.activeAreaId !== "profiles") {
        resolve(null);
        return;
      }
      request.progressVisible = true;
      restoreProfilePreviewRequestProgress(request);
      resolve(await waitForProgressPaint());
    }, PROFILE_PREVIEW_CACHE_PROGRESS_DELAY_MS);
  });
  return {
    paintedAt,
    cancelBeforeShow() {
      if (!timer) return;
      clearTimeout(timer);
      timer = 0;
      resolvePaintedAt(null);
    },
  };
}


async function applyProfilePreviewResource(context, view, profile, key, response) {
  const geometryId = String(response?.geometryResourceId ?? "");
  const geometryVersion = Number(response?.geometryResourceVersion ?? 0);
  if (!geometryId || !geometryVersion || profilePreviewKey(view, profile) !== key
      || view.activeAreaId !== "profiles" || !view.viewport) return;
  const entityId = profileSelectionKey(profile);
  const revision = `profile-preview:${entityId}:${geometryVersion}`;
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
  const receipt = await withTimeout(view.viewport.applyViewSnapshot({
    revision,
    rows: [{ entityId, data }],
  }, context.sceneProxy?.resources), 30000, "三维资源进入视口超时");
  if (!receipt?.applied || profilePreviewKey(view, profile) !== key
      || view.activeAreaId !== "profiles") return;
  view.viewport.setVisibleEntityIds?.([entityId]);
  view.viewport.setSelectedObjectIds?.([], "");
  view.viewport.fitViewForRevision?.(revision, 1.3);
  view.viewport.setStandardView?.("iso");
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
  const key = String(definition?.key ?? "");
  const label = localizedText(definition?.displayName, key);
  const value = values[key] ?? definition?.defaultValue ?? "";
  const common = `data-tube-profile-editor-parameter="${escapeAttr(key)}" data-tube-profile-value-type="${escapeAttr(definition?.valueType ?? "number")}" data-cam-change-action="tube-designer-profile-preview-change"`;
  if (definition?.valueType === "boolean") {
    return `<label class="tube-designer-field tube-designer-boolean-field"><span>${escapeText(label)}</span><input type="checkbox" ${common} ${value ? "checked" : ""} ${pending ? "disabled" : ""} /></label>`;
  }
  const options = Array.isArray(definition?.options) ? definition.options : [];
  if (options.length) {
    return `<label class="tube-designer-field"><span>${escapeText(label)}</span><select ${common} ${pending ? "disabled" : ""}>${options.map((option) => {
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
  return `<label class="tube-designer-field"><span>${escapeText(label)}</span><input ${attributes.join(" ")} /></label>`;
}


function matchesParameterVisibility(condition, values) {
  if (!condition) return true;
  const all = condition.conditions && condition.op === "all" ? condition.conditions : condition.all;
  const any = condition.conditions && condition.op === "any" ? condition.conditions : condition.any;
  if (Array.isArray(all)) return all.every((item) => matchesParameterVisibility(item, values));
  if (Array.isArray(any)) return any.some((item) => matchesParameterVisibility(item, values));
  const actual = values?.[condition.parameter ?? condition.key];
  if (condition.op === "eq") return actual === condition.value;
  if (condition.op === "ne") return actual !== condition.value;
  return true;
}


function renderProfilePackageImportDialog(view) {
  const state = view?.tubeDesignerProfilePackageImportDialog;
  if (!state) return "";
  const fileName = String(state.sourcePath ?? "").split(/[\\/]/).pop() || "管型包";
  return `<div class="tube-designer-modal-backdrop tube-designer-preset-dialog-backdrop" role="presentation">
    <section class="tube-designer-preset-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-profile-package-import-title">
      <header class="tube-designer-dialog-header">
        <div><strong id="tube-profile-package-import-title">导入可编辑管型包</strong><span>包内必须包含 profile.json 和 profile.py</span></div>
        <button class="tube-designer-dialog-close" data-cam-action="tube-designer-profile-package-import-cancel" aria-label="关闭" ${view?.pending ? "disabled" : ""}>×</button>
      </header>
      <div class="tube-designer-preset-dialog-body">
        <div class="tube-designer-imported-profile-summary"><strong>${escapeText(fileName)}</strong><span>管型压缩包</span><small>密码仅用于本次解包，不会保存</small></div>
        <label class="tube-designer-field wide"><span>包密码</span><input type="password" data-tube-profile-package-password maxlength="256" autocomplete="off" placeholder="未加密的包可留空" ${view?.pending ? "disabled" : ""} /></label>
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


function renderProfileSvg(profile) {
  const contours = Array.isArray(profile?.contours) ? profile.contours : [];
  const rendered = contours.map((contour, index) => renderSvgContour(contour, index));
  const geometryBounds = rendered.reduce((bounds, item) => includeBounds(bounds, item.bounds), emptyBounds());
  const fallbackWidth = positiveNumber(profile?.width, 100);
  const fallbackDepth = positiveNumber(profile?.depth, 100);
  const bounds = validBounds(geometryBounds) ? geometryBounds : {
    minX: -fallbackWidth / 2,
    minY: -fallbackDepth / 2,
    maxX: fallbackWidth / 2,
    maxY: fallbackDepth / 2,
  };
  const spanX = Math.max(bounds.maxX - bounds.minX, 1.0e-6);
  const spanY = Math.max(bounds.maxY - bounds.minY, 1.0e-6);
  const reference = Math.max(spanX, spanY);
  const paddingX = Math.max(spanX * 0.12, reference * 0.025, 1.0e-3);
  const paddingY = Math.max(spanY * 0.12, reference * 0.025, 1.0e-3);
  const viewBox = [
    bounds.minX - paddingX,
    -bounds.maxY - paddingY,
    spanX + paddingX * 2,
    spanY + paddingY * 2,
  ].map(svgNumber).join(" ");
  const fallback = `<rect x="${svgNumber(-fallbackWidth / 2)}" y="${svgNumber(-fallbackDepth / 2)}" width="${svgNumber(fallbackWidth)}" height="${svgNumber(fallbackDepth)}" rx="2" />`;
  return `<svg class="tube-profile-library-svg" viewBox="${viewBox}" preserveAspectRatio="xMidYMid meet" role="img" aria-label="${escapeAttr(profile?.name ?? "管型截面")}">
    <g transform="scale(1,-1)">${rendered.map((item) => item.markup).join("") || fallback}</g>
  </svg>`;
}


function renderSvgContour(contour, index) {
  const hole = index > 0;
  const className = hole ? "hole" : "outer";
  const kind = String(contour?.kind ?? "");
  if (kind === "circle") {
    const center = point(contour?.center) ?? [0, 0];
    const radius = positiveNumber(contour?.radius, 0);
    return {
      markup: radius > 0 ? `<circle class="${className}" cx="${svgNumber(center[0])}" cy="${svgNumber(center[1])}" r="${svgNumber(radius)}" />` : "",
      bounds: radius > 0 ? { minX: center[0] - radius, minY: center[1] - radius, maxX: center[0] + radius, maxY: center[1] + radius } : emptyBounds(),
    };
  }
  if (kind === "ellipse") {
    const center = point(contour?.center) ?? [0, 0];
    const rx = positiveNumber(contour?.radiusX, positiveNumber(contour?.width, 0) / 2);
    const ry = positiveNumber(contour?.radiusY, positiveNumber(contour?.height, 0) / 2);
    const rotation = finiteNumber(contour?.rotation, 0);
    const bounds = ellipseBounds(center, rx, ry, rotation);
    const transform = Math.abs(rotation) > 1.0e-12
      ? ` transform="rotate(${svgNumber(rotation * 180 / Math.PI)} ${svgNumber(center[0])} ${svgNumber(center[1])})"`
      : "";
    return {
      markup: rx > 0 && ry > 0 ? `<ellipse class="${className}" cx="${svgNumber(center[0])}" cy="${svgNumber(center[1])}" rx="${svgNumber(rx)}" ry="${svgNumber(ry)}"${transform} />` : "",
      bounds,
    };
  }
  if (kind === "roundedRectangle" || kind === "capsule") {
    const center = point(contour?.center) ?? [0, 0];
    const width = positiveNumber(contour?.width, 0);
    const height = positiveNumber(contour?.height, 0);
    const radius = kind === "capsule" ? Math.min(width, height) / 2 : Math.max(0, finiteNumber(contour?.radius, 0));
    return {
      markup: width > 0 && height > 0 ? `<rect class="${className}" x="${svgNumber(center[0] - width / 2)}" y="${svgNumber(center[1] - height / 2)}" width="${svgNumber(width)}" height="${svgNumber(height)}" rx="${svgNumber(Math.min(radius, width / 2, height / 2))}" />` : "",
      bounds: width > 0 && height > 0 ? { minX: center[0] - width / 2, minY: center[1] - height / 2, maxX: center[0] + width / 2, maxY: center[1] + height / 2 } : emptyBounds(),
    };
  }
  if (kind === "polygon") {
    const points = (Array.isArray(contour.points) ? contour.points : []).map(point).filter(Boolean);
    return {
      markup: points.length > 2 ? `<polygon class="${className}" points="${points.map(svgPoint).join(" ")}" />` : "",
      bounds: boundsFromPoints(points),
    };
  }
  if (kind === "path") {
    return renderSvgPath(contour?.segments, className);
  }
  return { markup: "", bounds: emptyBounds() };
}


function renderSvgPath(segments, className) {
  const rendered = (Array.isArray(segments) ? segments : []).map(renderSvgPathSegment).filter(Boolean);
  if (!rendered.length) return { markup: "", bounds: emptyBounds() };
  const commands = [];
  let current = null;
  let bounds = emptyBounds();
  for (const segment of rendered) {
    if (!current || pointDistance(current, segment.start) > 1.0e-7) {
      commands.push(`M ${svgPoint(segment.start)}`);
    }
    commands.push(segment.command);
    current = segment.end;
    bounds = includeBounds(bounds, segment.bounds);
  }
  return {
    markup: `<path class="${className}" d="${escapeAttr(`${commands.join(" ")} Z`)}" />`,
    bounds,
  };
}


function renderSvgPathSegment(segment) {
  const kind = String(segment?.kind ?? "");
  if (kind === "line") {
    const start = point(segment?.start);
    const end = point(segment?.end);
    if (!start || !end) return null;
    return { start, end, command: `L ${svgPoint(end)}`, bounds: boundsFromPoints([start, end]) };
  }
  if (kind === "arc") return renderSvgCircularArc(segment);
  if (kind === "ellipseArc") return renderSvgEllipseArc(segment);
  if (kind === "bezier") return renderSvgBezier(segment);
  if (kind === "bspline" || kind === "nurbs") return renderSvgSpline(segment);
  return null;
}


function renderSvgCircularArc(segment) {
  const start = point(segment?.start);
  const middle = point(segment?.middle);
  const end = point(segment?.end);
  if (!start || !middle || !end) return null;
  const ax = middle[0] - start[0];
  const ay = middle[1] - start[1];
  const bx = end[0] - start[0];
  const by = end[1] - start[1];
  const determinant = 2 * (ax * by - ay * bx);
  if (Math.abs(determinant) <= 1.0e-12) {
    return { start, end, command: `L ${svgPoint(end)}`, bounds: boundsFromPoints([start, middle, end]) };
  }
  const aSquared = ax * ax + ay * ay;
  const bSquared = bx * bx + by * by;
  const center = [
    start[0] + (by * aSquared - ay * bSquared) / determinant,
    start[1] + (ax * bSquared - bx * aSquared) / determinant,
  ];
  const radius = pointDistance(start, center);
  const startAngle = Math.atan2(start[1] - center[1], start[0] - center[0]);
  const middleAngle = Math.atan2(middle[1] - center[1], middle[0] - center[0]);
  const endAngle = Math.atan2(end[1] - center[1], end[0] - center[0]);
  const counterClockwiseSpan = positiveAngle(endAngle - startAngle);
  const middleCounterClockwiseSpan = positiveAngle(middleAngle - startAngle);
  const direction = middleCounterClockwiseSpan <= counterClockwiseSpan + 1.0e-9 ? 1 : -1;
  const sweep = direction > 0 ? counterClockwiseSpan : Math.PI * 2 - counterClockwiseSpan;
  const bounds = boundsFromPoints([start, middle, end]);
  for (const angle of [0, Math.PI / 2, Math.PI, Math.PI * 1.5]) {
    if (angleOnSweep(angle, startAngle, sweep, direction)) {
      includePoint(bounds, [center[0] + radius * Math.cos(angle), center[1] + radius * Math.sin(angle)]);
    }
  }
  return {
    start,
    end,
    command: `A ${svgNumber(radius)} ${svgNumber(radius)} 0 ${sweep > Math.PI + 1.0e-9 ? 1 : 0} ${direction > 0 ? 1 : 0} ${svgPoint(end)}`,
    bounds,
  };
}


function renderSvgEllipseArc(segment) {
  const center = point(segment?.center);
  const majorRadius = positiveNumber(segment?.majorRadius, 0);
  const minorRadius = positiveNumber(segment?.minorRadius, 0);
  const rotation = finiteNumber(segment?.rotation, 0);
  const startAngle = finiteNumber(segment?.startAngle, 0);
  const endAngle = finiteNumber(segment?.endAngle, startAngle);
  if (!center || majorRadius <= 0 || minorRadius <= 0 || Math.abs(endAngle - startAngle) <= 1.0e-12) return null;
  const delta = endAngle - startAngle;
  const direction = delta >= 0 ? 1 : -1;
  const sweep = Math.min(Math.abs(delta), Math.PI * 2);
  const start = ellipsePoint(center, majorRadius, minorRadius, rotation, startAngle);
  const end = ellipsePoint(center, majorRadius, minorRadius, rotation, startAngle + direction * sweep);
  const rotationDegrees = rotation * 180 / Math.PI;
  const arcCommand = (target, span) => `A ${svgNumber(majorRadius)} ${svgNumber(minorRadius)} ${svgNumber(rotationDegrees)} ${span > Math.PI + 1.0e-9 ? 1 : 0} ${direction > 0 ? 1 : 0} ${svgPoint(target)}`;
  let command = arcCommand(end, sweep);
  if (sweep >= Math.PI * 2 - 1.0e-9) {
    const halfway = ellipsePoint(center, majorRadius, minorRadius, rotation, startAngle + direction * Math.PI);
    command = `${arcCommand(halfway, Math.PI)} ${arcCommand(end, Math.PI)}`;
  }
  const bounds = boundsFromPoints([start, end]);
  const xExtreme = Math.atan2(-minorRadius * Math.sin(rotation), majorRadius * Math.cos(rotation));
  const yExtreme = Math.atan2(minorRadius * Math.cos(rotation), majorRadius * Math.sin(rotation));
  for (const angle of [xExtreme, xExtreme + Math.PI, yExtreme, yExtreme + Math.PI]) {
    if (angleOnSweep(angle, startAngle, sweep, direction)) {
      includePoint(bounds, ellipsePoint(center, majorRadius, minorRadius, rotation, angle));
    }
  }
  return { start, end, command, bounds };
}


function renderSvgBezier(segment) {
  const controls = (Array.isArray(segment?.controlPoints) ? segment.controlPoints : []).map(point).filter(Boolean);
  if (controls.length < 2) return null;
  const start = controls[0];
  const end = controls.at(-1);
  let command;
  if (controls.length === 2) command = `L ${svgPoint(end)}`;
  else if (controls.length === 3) command = `Q ${svgPoint(controls[1])} ${svgPoint(end)}`;
  else if (controls.length === 4) command = `C ${svgPoint(controls[1])} ${svgPoint(controls[2])} ${svgPoint(end)}`;
  else {
    const samples = sampleBezier(controls, Math.min(256, Math.max(48, controls.length * 10)));
    command = smoothSampledCurve(samples).command;
  }
  return { start, end, command, bounds: bezierBounds(controls) };
}


function renderSvgSpline(segment) {
  const samples = sampleSpline(segment);
  if (samples.length < 2) return null;
  const rendered = smoothSampledCurve(samples);
  return {
    start: samples[0],
    end: samples.at(-1),
    command: rendered.command,
    bounds: rendered.bounds,
  };
}


function smoothSampledCurve(samples) {
  const closed = samples.length > 3 && pointDistance(samples[0], samples.at(-1)) <= 1.0e-7;
  let bounds = emptyBounds();
  const commands = [];
  for (let index = 0; index + 1 < samples.length; ++index) {
    const start = samples[index];
    const end = samples[index + 1];
    const previous = index > 0
      ? samples[index - 1]
      : (closed ? samples.at(-2) : [start[0] * 2 - end[0], start[1] * 2 - end[1]]);
    const following = index + 2 < samples.length
      ? samples[index + 2]
      : (closed ? samples[1] : [end[0] * 2 - start[0], end[1] * 2 - start[1]]);
    const control1 = [start[0] + (end[0] - previous[0]) / 6, start[1] + (end[1] - previous[1]) / 6];
    const control2 = [end[0] - (following[0] - start[0]) / 6, end[1] - (following[1] - start[1]) / 6];
    commands.push(`C ${svgPoint(control1)} ${svgPoint(control2)} ${svgPoint(end)}`);
    bounds = includeBounds(bounds, bezierBounds([start, control1, control2, end]));
  }
  return { command: commands.join(" "), bounds };
}


function sampleSpline(segment) {
  const degree = Math.trunc(finiteNumber(segment?.degree, 0));
  const controls = (Array.isArray(segment?.controlPoints) ? segment.controlPoints : []).map(point).filter(Boolean);
  if (degree < 1 || controls.length < degree + 1) return [];
  const rational = String(segment?.kind ?? "") === "nurbs";
  const weights = rational
    ? (Array.isArray(segment?.weights) ? segment.weights : []).map((value) => finiteNumber(value, Number.NaN))
    : controls.map(() => 1);
  if (weights.length !== controls.length || weights.some((value) => !Number.isFinite(value) || value <= 0)) return [];
  let knots = expandedKnots(segment?.knots, segment?.multiplicities);
  if (knots.length < 2) return [];
  let splineControls = controls;
  let splineWeights = weights;
  let domainStart;
  let domainEnd;
  const periodic = segment?.periodic === true;
  if (periodic) {
    const endpointMultiplicity = knots.findIndex((value) => value !== knots[0]);
    const tailMultiplicity = knots.length - 1 - knots.findLastIndex((value) => value !== knots.at(-1));
    if (endpointMultiplicity < 1 || endpointMultiplicity !== tailMultiplicity
        || knots.length - endpointMultiplicity !== controls.length) return sampleClosedControls(controls);
    const leftCount = degree + 1 - endpointMultiplicity;
    if (leftCount < 0) return sampleClosedControls(controls);
    const positiveSteps = knots.slice(1).map((value, index) => value - knots[index]).filter((value) => value > 1.0e-12);
    const fallbackStep = positiveSteps[0] ?? 1;
    const left = [];
    let cursor = knots[0];
    for (let index = 0; index < leftCount; ++index) {
      const step = positiveSteps.at(-1 - (index % positiveSteps.length)) ?? fallbackStep;
      cursor -= step;
      left.unshift(cursor);
    }
    const right = [];
    cursor = knots.at(-1);
    for (let index = 0; index < degree; ++index) {
      const step = positiveSteps[index % positiveSteps.length] ?? fallbackStep;
      cursor += step;
      right.push(cursor);
    }
    domainStart = knots[0];
    domainEnd = knots.at(-1);
    knots = [...left, ...knots, ...right];
    splineControls = [...controls, ...controls.slice(0, degree)];
    splineWeights = [...weights, ...weights.slice(0, degree)];
  } else {
    if (knots.length !== controls.length + degree + 1) return [];
    domainStart = knots[degree];
    domainEnd = knots[controls.length];
  }
  domainStart = finiteNumber(segment?.startParameter, domainStart);
  domainEnd = finiteNumber(segment?.endParameter, domainEnd);
  if (!(domainEnd > domainStart)) return [];
  const breaks = [domainStart, ...knots.filter((value) => value > domainStart + 1.0e-12 && value < domainEnd - 1.0e-12), domainEnd]
    .filter((value, index, values) => index === 0 || value > values[index - 1] + 1.0e-12);
  const spanCount = Math.max(1, breaks.length - 1);
  const samplesPerSpan = Math.max(4, Math.min(24, Math.floor(512 / spanCount)));
  const values = [];
  for (let span = 0; span + 1 < breaks.length; ++span) {
    for (let step = 0; step < samplesPerSpan; ++step) {
      const ratio = step / samplesPerSpan;
      const parameter = breaks[span] + (breaks[span + 1] - breaks[span]) * ratio;
      const value = evaluateSpline(splineControls, splineWeights, knots, degree, parameter);
      if (value) values.push(value);
    }
  }
  const finalValue = periodic
    ? values[0]
    : evaluateSpline(splineControls, splineWeights, knots, degree, domainEnd);
  if (finalValue) values.push([...finalValue]);
  return values;
}


function evaluateSpline(controls, weights, knots, degree, parameter) {
  const lastControl = controls.length - 1;
  if (knots.length !== controls.length + degree + 1) return null;
  let span = degree;
  if (parameter >= knots[lastControl + 1] - 1.0e-12) span = lastControl;
  else {
    let low = degree;
    let high = lastControl + 1;
    while (high - low > 1) {
      const middle = Math.floor((low + high) / 2);
      if (parameter < knots[middle]) high = middle;
      else low = middle;
    }
    span = low;
  }
  const values = [];
  for (let index = 0; index <= degree; ++index) {
    const controlIndex = span - degree + index;
    const weight = weights[controlIndex];
    const control = controls[controlIndex];
    if (!control || !Number.isFinite(weight)) return null;
    values.push([control[0] * weight, control[1] * weight, weight]);
  }
  for (let level = 1; level <= degree; ++level) {
    for (let index = degree; index >= level; --index) {
      const controlIndex = span - degree + index;
      const denominator = knots[controlIndex + degree - level + 1] - knots[controlIndex];
      const alpha = Math.abs(denominator) <= 1.0e-15 ? 0 : (parameter - knots[controlIndex]) / denominator;
      values[index] = values[index - 1].map((value, component) => value * (1 - alpha) + values[index][component] * alpha);
    }
  }
  const value = values[degree];
  return Math.abs(value[2]) <= 1.0e-15 ? null : [value[0] / value[2], value[1] / value[2]];
}


function expandedKnots(rawKnots, rawMultiplicities) {
  const knots = (Array.isArray(rawKnots) ? rawKnots : []).map((value) => finiteNumber(value, Number.NaN));
  if (knots.some((value) => !Number.isFinite(value))) return [];
  if (!Array.isArray(rawMultiplicities)) return knots;
  if (rawMultiplicities.length !== knots.length) return [];
  const expanded = [];
  for (let index = 0; index < knots.length; ++index) {
    const count = Math.trunc(finiteNumber(rawMultiplicities[index], 0));
    if (count < 1) return [];
    for (let repeat = 0; repeat < count; ++repeat) expanded.push(knots[index]);
  }
  return expanded;
}


function sampleClosedControls(controls) {
  if (controls.length < 3) return controls;
  const values = [];
  for (let index = 0; index < controls.length; ++index) {
    const left = controls[(index + controls.length - 1) % controls.length];
    const center = controls[index];
    const right = controls[(index + 1) % controls.length];
    for (let step = 0; step < 16; ++step) {
      const ratio = step / 16;
      const inverse = 1 - ratio;
      values.push([
        inverse * inverse * center[0] + 2 * inverse * ratio * ((center[0] + right[0]) / 2) + ratio * ratio * right[0],
        inverse * inverse * center[1] + 2 * inverse * ratio * ((center[1] + right[1]) / 2) + ratio * ratio * right[1],
      ]);
    }
  }
  values.push([...values[0]]);
  return values;
}


function sampleBezier(controls, count) {
  const values = [];
  for (let index = 0; index <= count; ++index) {
    const points = controls.map((value) => [...value]);
    const ratio = index / count;
    for (let level = points.length - 1; level > 0; --level) {
      for (let cursor = 0; cursor < level; ++cursor) {
        points[cursor][0] = points[cursor][0] * (1 - ratio) + points[cursor + 1][0] * ratio;
        points[cursor][1] = points[cursor][1] * (1 - ratio) + points[cursor + 1][1] * ratio;
      }
    }
    values.push(points[0]);
  }
  return values;
}


function bezierBounds(controls) {
  const bounds = boundsFromPoints([controls[0], controls.at(-1)]);
  if (controls.length === 3) {
    for (let component = 0; component < 2; ++component) {
      const denominator = controls[0][component] - 2 * controls[1][component] + controls[2][component];
      if (Math.abs(denominator) > 1.0e-15) {
        const ratio = (controls[0][component] - controls[1][component]) / denominator;
        if (ratio > 0 && ratio < 1) includePoint(bounds, bezierPoint(controls, ratio));
      }
    }
  } else if (controls.length === 4) {
    for (let component = 0; component < 2; ++component) {
      const p0 = controls[0][component];
      const p1 = controls[1][component];
      const p2 = controls[2][component];
      const p3 = controls[3][component];
      const a = -p0 + 3 * p1 - 3 * p2 + p3;
      const b = 2 * (p0 - 2 * p1 + p2);
      const c = p1 - p0;
      for (const ratio of quadraticRoots(a, b, c)) {
        if (ratio > 0 && ratio < 1) includePoint(bounds, bezierPoint(controls, ratio));
      }
    }
  } else if (controls.length > 4) {
    return boundsFromPoints(sampleBezier(controls, Math.min(512, Math.max(96, controls.length * 16))));
  }
  return bounds;
}


function bezierPoint(controls, ratio) {
  const points = controls.map((value) => [...value]);
  for (let level = points.length - 1; level > 0; --level) {
    for (let index = 0; index < level; ++index) {
      points[index][0] = points[index][0] * (1 - ratio) + points[index + 1][0] * ratio;
      points[index][1] = points[index][1] * (1 - ratio) + points[index + 1][1] * ratio;
    }
  }
  return points[0];
}


function quadraticRoots(a, b, c) {
  if (Math.abs(a) <= 1.0e-15) return Math.abs(b) <= 1.0e-15 ? [] : [-c / b];
  const discriminant = b * b - 4 * a * c;
  if (discriminant < 0) return [];
  const root = Math.sqrt(discriminant);
  return [(-b - root) / (2 * a), (-b + root) / (2 * a)];
}


function ellipsePoint(center, radiusX, radiusY, rotation, angle) {
  const cosRotation = Math.cos(rotation);
  const sinRotation = Math.sin(rotation);
  const x = radiusX * Math.cos(angle);
  const y = radiusY * Math.sin(angle);
  return [center[0] + x * cosRotation - y * sinRotation, center[1] + x * sinRotation + y * cosRotation];
}


function ellipseBounds(center, radiusX, radiusY, rotation) {
  if (!(radiusX > 0 && radiusY > 0)) return emptyBounds();
  const extentX = Math.hypot(radiusX * Math.cos(rotation), radiusY * Math.sin(rotation));
  const extentY = Math.hypot(radiusX * Math.sin(rotation), radiusY * Math.cos(rotation));
  return { minX: center[0] - extentX, minY: center[1] - extentY, maxX: center[0] + extentX, maxY: center[1] + extentY };
}


function angleOnSweep(angle, start, sweep, direction) {
  const travel = direction > 0 ? positiveAngle(angle - start) : positiveAngle(start - angle);
  return travel <= sweep + 1.0e-9;
}


function positiveAngle(value) {
  const full = Math.PI * 2;
  return ((value % full) + full) % full;
}


function emptyBounds() {
  return { minX: Number.POSITIVE_INFINITY, minY: Number.POSITIVE_INFINITY, maxX: Number.NEGATIVE_INFINITY, maxY: Number.NEGATIVE_INFINITY };
}


function boundsFromPoints(points) {
  const bounds = emptyBounds();
  for (const value of points) includePoint(bounds, value);
  return bounds;
}


function includePoint(bounds, value) {
  if (!value || !value.every(Number.isFinite)) return bounds;
  bounds.minX = Math.min(bounds.minX, value[0]);
  bounds.minY = Math.min(bounds.minY, value[1]);
  bounds.maxX = Math.max(bounds.maxX, value[0]);
  bounds.maxY = Math.max(bounds.maxY, value[1]);
  return bounds;
}


function includeBounds(target, source) {
  if (!validBounds(source)) return target;
  includePoint(target, [source.minX, source.minY]);
  includePoint(target, [source.maxX, source.maxY]);
  return target;
}


function validBounds(bounds) {
  return bounds && [bounds.minX, bounds.minY, bounds.maxX, bounds.maxY].every(Number.isFinite)
    && bounds.maxX >= bounds.minX && bounds.maxY >= bounds.minY;
}


function point(value) {
  if (!Array.isArray(value) || value.length < 2) return null;
  const x = Number(value[0]);
  const y = Number(value[1]);
  return Number.isFinite(x) && Number.isFinite(y) ? [x, y] : null;
}


function pointDistance(left, right) {
  return Math.hypot(left[0] - right[0], left[1] - right[1]);
}


function finiteNumber(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}


function positiveNumber(value, fallback) {
  const number = finiteNumber(value, fallback);
  return number > 0 ? number : fallback;
}


function svgNumber(value) {
  return formatNumber(value, 6);
}


function svgPoint(value) {
  return `${svgNumber(value[0])},${svgNumber(value[1])}`;
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
    title: "选择可编辑管型包",
    filters: [{ name: "管型包", extensions: ["icaxprofile", "zip"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  view.tubeDesignerProfilePackageImportDialog = { sourcePath };
  view.error = "";
  ops.renderProject(context, view);
  queueMicrotask(() => document.querySelector("[data-tube-profile-package-password]")?.focus?.());
  return sourcePath;
}


async function confirmProfilePackageImport(context, view, ops) {
  const state = view.tubeDesignerProfilePackageImportDialog;
  if (view.pending || !state?.sourcePath) return null;
  const password = String(document.querySelector("[data-tube-profile-package-password]")?.value ?? "");
  return runProfileTask(context, view, ops, {
    title: "正在导入可编辑管型包",
    message: "正在解包、校验参数和生成默认截面",
  }, async () => {
    const response = await invokeProduct(context, "TubeDesigner.ImportProfilePackage", {
      sourcePath: state.sourcePath,
      password,
    }, { timeoutMs: 120000 });
    const profile = response?.profile;
    if (!profile?.id) throw new Error("导入管型包后没有返回记录标识。" );
    upsertProfile(view, profile);
    view.tubeDesignerSelectedProfileId = `user:${profile.id}`;
    view.tubeDesignerProfilePackageImportDialog = null;
    ops.showNotice(context, view, `已新增可编辑管型“${profile.name}”。`);
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
    title: "选择 DXF 管型截面",
    filters: [{ name: "DXF 二维截面", extensions: ["dxf"] }],
  }) ?? "").trim();
  if (!sourcePath) return null;
  return runProfileTask(context, view, ops, {
    title: "正在新增 DXF 管型",
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
      name: profile.name ?? profile.sourceFileName ?? "DXF 管型",
      profile,
    }, { timeoutMs: 120000 });
    const saved = savedResponse?.profile;
    if (!saved?.id) throw new Error("保存 DXF 管型后没有返回记录标识。" );
    upsertProfile(view, saved);
    view.tubeDesignerSelectedProfileId = `user:${saved.id}`;
    ops.showNotice(context, view, `已新增 DXF 管型“${saved.name}”。`);
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
    const lengthInput = editor?.querySelector("[data-tube-profile-preview-length]");
    if (lengthInput) {
      const length = Number(lengthInput.value);
      if (!Number.isFinite(length) || length < 1 || length > 100000) {
        lengthInput.focus?.();
        throw new Error("预览长度必须在 1 到 100000 mm 之间。");
      }
      view.tubeDesignerProfilePreviewLength = length;
    }
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
    view.tubeDesignerProfilePreview = null;
    view.error = "";
    ops.renderProject(context, view);
  } catch (error) {
    view.error = error?.message ?? String(error);
    ops.renderProject(context, view);
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
    title: editable ? "正在更新可编辑管型" : "正在保存管型名称",
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
  if (typeof globalThis.confirm === "function" && !globalThis.confirm(
    `确定删除“${profile.name}”吗？\n已经使用该管型的产品不会受影响。`,
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
    ensureSelectedProfile(view, libraryProfiles(view));
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
  let length = normalizedPreviewLength(view);
  try {
    const profileKey = profileSelectionKey(profile);
    const editor = document.querySelector(`[data-tube-profile-library-editor][data-tube-designer-profile-key="${cssEscape(profileKey)}"]`);
    if (isParametricProfile(profile)) {
      parameters = editor ? readEditorParameters(editor) : profilePreviewParameters(view, profile);
    }
    const lengthInput = editor?.querySelector("[data-tube-profile-preview-length]");
    if (lengthInput) length = Number(lengthInput.value);
    if (!Number.isFinite(length) || length < 1 || length > 100000) {
      lengthInput?.focus?.();
      throw new Error("预览与导出长度必须在 1 到 100000 mm 之间。");
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
  view.tubeDesignerProfilePreviewLength = length;
  return runProfileTask(context, view, ops, {
    title: format === "dxf" ? "正在导出截面 DXF" : "正在导出管子 STEP",
    message: format === "dxf" ? "正在写入精确二维轮廓" : `正在生成 ${length} mm 标准拉伸体`,
  }, async () => {
    const response = await invokeSceneProduct(context, "TubeDesigner.ExportProfile", {
      profileRef: profileRef(profile),
      parameters,
      length,
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
