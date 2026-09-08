import { escapeAttr, escapeText } from "../../_shared/workbench/utils/format.mjs";

// Product templates are managed as portable .iPT archives.  The dialog keeps
// built-in packages read-only and exposes imported personal packages separately
// so an accidental delete can never remove an installed template.
export function renderProductTemplateManagerDialog(view) {
  const state = view?.tubeDesignerTemplateManager;
  if (!state) return "";
  const designer = view?.scene?.tubeDesigner ?? {};
  const builtins = Array.isArray(designer.templates) ? designer.templates : [];
  const custom = Array.isArray(view?.tubeDesignerUserData?.productTemplates)
    ? view.tubeDesignerUserData.productTemplates : [];
  const selectedId = String(state.selectedId ?? "");
  const create = state.mode === "create";
  const selectedCustom = custom.find((item) => String(item?.id ?? "") === selectedId);
  const selectedBuiltin = builtins.find((item) => String(item?.id ?? "") === selectedId);
  const selected = selectedCustom
    ? { ...selectedCustom, scope: "personal" }
    : selectedBuiltin ? { ...selectedBuiltin, scope: "builtin" } : null;
  if (create) {
    const baseId = String(state.baseTemplateId ?? builtins.find((item) => item?.available)?.id ?? "");
    return `<div class="tube-designer-modal-backdrop tube-designer-template-manager-backdrop" role="presentation">
      <section class="tube-designer-template-manager-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-template-create-title">
        <header class="tube-designer-dialog-header"><div><strong id="tube-template-create-title">新增产品模板</strong><span>以现有模板为基础创建个人模板包，保存后可导出为 .iPT。</span></div><button class="tube-designer-dialog-close" data-cam-action="tube-designer-template-manager-close" aria-label="关闭">×</button></header>
        <div class="tube-designer-template-manager-body">
          <label class="tube-designer-field wide"><span>基础模板</span><select data-tube-template-create-base>${builtins.filter((item) => item?.available).map((item) => `<option value="${escapeAttr(item.id)}" ${item.id === baseId ? "selected" : ""}>${escapeText(item.name ?? item.id)} · ${escapeText(item.version ?? "")}</option>`).join("")}</select><small>新模板沿用基础模板的参数和几何脚本，后续可在模板包中继续扩展。</small></label>
          <label class="tube-designer-field wide"><span>模板名称</span><input type="text" maxlength="120" data-tube-template-create-name value="${escapeAttr(state.name ?? "")}" placeholder="例如：客户 A · 直跑护栏" autofocus /></label>
          <label class="tube-designer-field wide"><span>模板说明</span><textarea rows="3" maxlength="500" data-tube-template-create-description>${escapeText(state.description ?? "")}</textarea></label>
          <p class="tube-designer-template-manager-note">创建只写入“我的模板”目录，不会修改内置模板。模板包是加密 ZIP 压缩格式，扩展名固定为 <code>.iPT</code>，密码由产品固定 magic number 自动处理。</p>
        </div>
        <footer class="tube-designer-preset-dialog-footer"><span></span><button class="tube-designer-secondary" data-cam-action="tube-designer-template-manager-close">取消</button><button class="tube-designer-primary" data-cam-action="tube-designer-template-create-confirm" ${view.pending ? "disabled" : ""}>${view.pending ? "正在保存…" : "创建模板"}</button></footer>
      </section>
    </div>`;
  }
  const rows = [
    ...builtins.filter((item) => item?.available).map((item) => ({ ...item, scope: "builtin", id: String(item.id) })),
    ...custom.map((item) => ({ ...item, scope: "personal", id: String(item.id ?? "") })),
  ];
  return `<div class="tube-designer-modal-backdrop tube-designer-template-manager-backdrop" role="presentation">
    <section class="tube-designer-template-manager-dialog" role="dialog" aria-modal="true" aria-labelledby="tube-template-manager-title">
      <header class="tube-designer-dialog-header"><div><strong id="tube-template-manager-title">产品模板管理</strong><span>内置模板 ${builtins.filter((item) => item?.available).length} 个 · 我的模板 ${custom.length} 个</span></div><button class="tube-designer-dialog-close" data-cam-action="tube-designer-template-manager-close" aria-label="关闭">×</button></header>
      <div class="tube-designer-template-manager-toolbar"><button class="tube-designer-primary" data-cam-action="tube-designer-template-create-open">＋ 新增模板</button><button class="tube-designer-secondary" data-cam-action="tube-designer-template-import">导入 .iPT</button><button class="tube-designer-secondary" data-cam-action="tube-designer-template-export" ${selected ? "" : "disabled"}>导出 .iPT</button><button class="tube-designer-danger" data-cam-action="tube-designer-template-delete" ${selected?.scope === "personal" ? "" : "disabled"}>删除模板</button></div>
      <div class="tube-designer-template-manager-body"><div class="tube-designer-template-manager-list" role="listbox" aria-label="产品模板"><table><thead><tr><th>模板</th><th>版本</th><th>来源</th><th>状态</th></tr></thead><tbody>${rows.length ? rows.map((item) => `<tr class="${item.id === selectedId ? "selected" : ""}" data-cam-action="tube-designer-template-select" data-tube-template-id="${escapeAttr(item.id)}" role="option" aria-selected="${item.id === selectedId}"><td><strong>${escapeText(item.displayName ?? item.name ?? item.id)}</strong><small>${escapeText(item.description ?? "")}</small></td><td>${escapeText(item.version ?? item.templateVersion ?? "—")}</td><td><span class="tube-designer-template-scope ${item.scope === "builtin" ? "builtin" : "personal"}">${item.scope === "builtin" ? "内置" : "我的模板"}</span></td><td>${item.scope === "builtin" ? "只读" : "可导出 / 可删除"}</td></tr>`).join("") : `<tr><td colspan="4" class="empty">还没有可管理的模板。</td></tr>`}</tbody></table></div>${selected ? `<aside class="tube-designer-template-manager-summary"><strong>${escapeText(selected.displayName ?? selected.name ?? selected.id)}</strong><span>${selected.scope === "builtin" ? "内置模板" : "个人模板包"}</span><p>${escapeText(selected.description ?? "暂无说明")}</p><dl><dt>模板 ID</dt><dd>${escapeText(selected.id)}</dd><dt>版本</dt><dd>${escapeText(selected.version ?? selected.templateVersion ?? "—")}</dd><dt>格式</dt><dd>.iPT（ZIP）</dd></dl></aside>` : `<aside class="tube-designer-template-manager-summary empty">选择一行查看模板摘要。<br />内置模板不能删除，个人模板可导出或删除。</aside>`}</div>
      <footer class="tube-designer-preset-dialog-footer"><span>建议通过加密 .iPT 在不同项目和电脑间传递模板。</span><button class="tube-designer-secondary" data-cam-action="tube-designer-template-manager-close">关闭</button></footer>
    </section>
  </div>`;
}

export function templateManagerState(view) {
  return view.tubeDesignerTemplateManager ?? null;
}
