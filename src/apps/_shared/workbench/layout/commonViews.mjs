import { escapeText } from "../utils/format.mjs";

export function renderPanel(title, content) {
  return `
    <section class="cam-panel">
      <div class="cam-panel-title">${escapeText(title)}</div>
      <div class="cam-panel-body">${content}</div>
    </section>
  `;
}

export function renderProgress(progress = {}) {
  return `
    <div class="cam-progress-backdrop" aria-live="polite" aria-modal="true" role="dialog">
      <section class="cam-progress-dialog">
        <div class="cam-progress-gauge" aria-hidden="true">
          <div class="cam-progress-face">
            ${Array.from({ length: 32 }, (_, index) => `<span style="--tick:${index}"></span>`).join("")}
            <div class="cam-progress-sweep"></div>
            <div class="cam-progress-needle"></div>
            <div class="cam-progress-core">
              <strong>RUN</strong>
              <small>${escapeText(progress.mode ?? "CAM")}</small>
            </div>
          </div>
        </div>
        <div class="cam-progress-copy">
          <strong>${escapeText(progress.title ?? "项目任务")}</strong>
          <span>${escapeText(progress.detail ?? "正在等待后台任务完成")}</span>
          <div class="cam-progress-stage">
            <span>${escapeText(progress.stage ?? "后台执行")}</span>
            <span>${escapeText(progress.mode ?? "CAM")}</span>
          </div>
        </div>
      </section>
    </div>
  `;
}

export function renderSelection(selection = {}) {
  if (!selection.id) {
    return `<div class="cam-empty-row">未选择对象。</div>`;
  }
  return `
    <dl class="cam-facts">
      <dt>类型</dt><dd>${escapeText(selection.kind)}</dd>
      <dt>ID</dt><dd>${escapeText(selection.id)}</dd>
      <dt>名称</dt><dd>${escapeText(selection.label)}</dd>
    </dl>
  `;
}
