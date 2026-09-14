function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function stableValue(value) {
  if (Array.isArray(value)) return value.map(stableValue);
  if (!value || typeof value !== "object") return value;
  return Object.fromEntries(Object.keys(value).sort().map((key) => [key, stableValue(value[key])]));
}

export function productManufacturingPlanFingerprint(template, values = {}, instanceQuantity = 1) {
  return JSON.stringify(stableValue({
    templateId: String(template?.id ?? ""),
    templateVersion: String(template?.version ?? ""),
    parameters: values,
    instanceQuantity: Number(instanceQuantity) || 1,
  }));
}

function displayCell(value) {
  if (value == null) return "—";
  if (typeof value === "number") {
    if (!Number.isFinite(value)) return "—";
    return new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 3 }).format(value);
  }
  if (typeof value === "boolean") return value ? "是" : "否";
  if (typeof value === "object") return JSON.stringify(value);
  const text = String(value).trim();
  return text || "—";
}

function tableSummary(table) {
  const rows = Array.isArray(table?.rows) ? table.rows : [];
  const quantityColumn = (table?.columns ?? []).find((column) =>
    ["quantity", "count"].includes(String(column?.key ?? "").toLowerCase()));
  if (!quantityColumn) return `${rows.length} 行`;
  const quantity = rows.reduce((total, row) => {
    const value = Number(row?.values?.[quantityColumn.key]);
    return total + (Number.isFinite(value) && value > 0 ? value : 0);
  }, 0);
  return `${rows.length} 行${quantity ? ` · ${displayCell(quantity)} 件` : ""}`;
}

function renderTable(table, index) {
  const columns = Array.isArray(table?.columns) ? table.columns : [];
  const rows = Array.isArray(table?.rows) ? table.rows : [];
  const title = table?.displayName || table?.key || `加工清单 ${index + 1}`;
  return `<details class="tube-designer-plan-table" ${index === 0 ? "open" : ""}>
    <summary><span>${escapeText(title)}</span><small>${escapeText(tableSummary(table))}</small></summary>
    <div class="tube-designer-plan-table-scroll">
      <table>
        <thead><tr>${columns.map((column) => `<th>${escapeText(column.displayName || column.key)}${column.unit ? `<small>${escapeText(column.unit)}</small>` : ""}</th>`).join("")}</tr></thead>
        <tbody>${rows.length ? rows.map((row) => `<tr>${columns.map((column) => `<td>${escapeText(displayCell(row?.values?.[column.key]))}</td>`).join("")}</tr>`).join("") : `<tr><td colspan="${Math.max(1, columns.length)}">当前参数没有生成清单行</td></tr>`}</tbody>
      </table>
    </div>
  </details>`;
}

export function renderProductManufacturingPlan(state, options = {}) {
  const mode = options.mode === "add" ? "add" : "right";
  const current = state && state.fingerprint === options.fingerprint;
  const status = current ? String(state.status ?? "idle") : "stale";
  const action = `data-cam-action="tube-designer-refresh-manufacturing-plan" data-tube-designer-editor-mode="${mode}"`;
  if (status === "loading") {
    return `<div class="tube-designer-plan-state is-loading" data-tube-designer-plan-content>
      <span class="tube-designer-export-spinner" aria-hidden="true"></span>
      <strong>正在计算加工清单</strong>
      <p>正在运行模板的制造规划，不生成三维实体。</p>
    </div>`;
  }
  if (status === "error") {
    return `<div class="tube-designer-plan-state is-error" data-tube-designer-plan-content>
      <strong>加工清单计算失败</strong>
      <p>${escapeText(state?.error ?? "模板没有返回可用的加工规划。")}</p>
      <button type="button" class="tube-designer-secondary" ${action}>重新计算</button>
    </div>`;
  }
  if (status === "ready") {
    const tables = Array.isArray(state?.response?.tables) ? state.response.tables : [];
    const partCount = Number(state?.response?.partCount ?? 0);
    const instanceQuantity = Math.max(1, Number(state?.response?.instanceQuantity ?? 1));
    return `<div class="tube-designer-plan-result" data-tube-designer-plan-content>
      <header><div><strong>零件加工清单</strong><span>${partCount > 0 ? `单套 ${escapeText(displayCell(partCount))} 个制造构件` : "按模板制造规划生成"}${instanceQuantity > 1 ? ` · 生产 ${escapeText(displayCell(instanceQuantity))} 套` : ""}</span></div>
        <button type="button" class="tube-designer-secondary" ${action}>重新计算</button></header>
      ${tables.length ? `<div class="tube-designer-plan-tables">${tables.map(renderTable).join("")}</div>`
        : `<div class="tube-designer-plan-state"><strong>当前模板没有清单表</strong><p>模板已完成制造规划，但没有声明可展示的数据表。</p></div>`}
      <small class="tube-designer-plan-note">表内数量为单套产品用量；生产套数在正式拆单和下料时参与汇总。此处不创建场景实体。</small>
    </div>`;
  }
  return `<div class="tube-designer-plan-state ${status === "stale" && state?.response ? "is-stale" : ""}" data-tube-designer-plan-content>
    <strong>${status === "stale" && state?.response ? "参数已变化，清单需要更新" : "生成前核对加工清单"}</strong>
    <p>从模板的制造分支计算零件、长度、数量和工艺信息；不会生成三维实体，也不会修改当前工程。</p>
    <button type="button" class="tube-designer-primary" ${action}>${status === "stale" && state?.response ? "更新加工清单" : "计算加工清单"}</button>
  </div>`;
}
