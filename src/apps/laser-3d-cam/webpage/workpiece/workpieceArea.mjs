import { renderPanel } from "../layout/commonViews.mjs";
import { escapeAttr, escapeText } from "../utils/format.mjs";
import { renderWorkpieceList } from "./workpieceViews.mjs";

export function renderWorkpieceLeftPane(context, view) {
  const scene = view.scene ?? {};
  const model = scene.model ?? {};
  const workpieces = scene.workpieces ?? [];

  return `
    ${renderPanel("工件资源", renderWorkpieceList(workpieces, model.entityId))}
    ${renderPanel("模型检查", `
      <dl class="cam-facts">
        <dt>拓扑</dt><dd>${scene.topology?.hasTopology ? "可用" : "未生成"}</dd>
        <dt>面</dt><dd>${escapeText(scene.topology?.faceCount ?? 0)}</dd>
        <dt>Loop</dt><dd>${escapeText(scene.topology?.loopCount ?? 0)}</dd>
        <dt>边</dt><dd>${escapeText(scene.topology?.edgeCount ?? 0)}</dd>
      </dl>
    `)}
  `;
}

export function renderWorkpieceRightPane(context, view) {
  const scene = view.scene ?? {};
  const model = scene.model ?? {};
  const workpieces = scene.workpieces ?? [];

  return `
    ${renderPanel("导入工件", `
      <div class="cam-file-strip">
        <input class="cam-path-input" type="text" data-cam-model-path value="${escapeAttr(view.sourcePath || model.sourcePath || "")}" placeholder="STEP/STP/IGS/IGES 文件路径" />
        <button class="tool-button" type="button" data-cam-action="choose-model" ${view.pending ? "disabled" : ""}>浏览</button>
        <button class="primary-button" type="button" data-cam-action="import-model" ${view.pending ? "disabled" : ""}>导入</button>
      </div>
    `)}
    ${renderPanel("工件属性", `
      <dl class="cam-facts">
        <dt>数量</dt><dd>${escapeText(workpieces.length)}</dd>
        <dt>当前</dt><dd>${escapeText(model.entityId || "-")}</dd>
        <dt>文件</dt><dd>${model.isLoaded ? escapeText(model.sourcePath) : "未导入"}</dd>
      </dl>
    `)}
    ${renderPanel("修复编辑", `
      <div class="cam-list-row"><strong>模型检查</strong><small>检查拓扑闭合、退化边、缝隙和法向</small></div>
      <div class="cam-list-row"><strong>修复会话</strong><small>后续打开独立 WorkpieceEditScene，提交后生成新 BRep 资源</small></div>
      <div class="cam-button-row">
        <button class="tool-button" type="button" data-cam-action="workpiece-placeholder" ${model.isLoaded ? "" : "disabled"}>模型检查</button>
        <button class="tool-button" type="button" data-cam-action="workpiece-placeholder" ${model.isLoaded ? "" : "disabled"}>进入修复</button>
      </div>
    `)}
    ${renderPanel("支撑/夹具", `
      <button class="tool-button" type="button" data-cam-action="support-placeholder">生成支架</button>
      <button class="tool-button" type="button" data-cam-action="support-placeholder">导出支架 DXF</button>
      <div class="cam-hint">支架拆单与排样交给平面激光 CAM 处理。</div>
    `)}
  `;
}
