import { renderPanel } from "../layout/commonViews.mjs";
import { escapeAttr, escapeText } from "../utils/format.mjs";
import { renderIntentToolpathTree, renderWorkpieceList } from "./workpieceViews.mjs";

export function renderWorkpieceLeftPane(context, view) {
  const scene = view.scene ?? {};
  const model = scene.model ?? {};
  const workpieces = scene.workpieces ?? [];

  return `
    ${renderPanel("工件资源", renderWorkpieceList(workpieces, model.entityId))}
    ${renderPanel("意图刀路", `
      <div class="cam-intent-tree">${renderIntentToolpathTree(scene.intentToolpaths ?? [])}</div>
      <div class="cam-button-row">
        <button class="primary-button" type="button" data-cam-action="intent-from-selection" ${scene.selection?.kind || scene.selection?.selectedKind ? "" : "disabled"}>从当前选择创建</button>
      </div>
    `)}
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
  const inspection = scene.cadInspection ?? {};
  const hasDraft = Boolean(model.hasDraft);

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
    ${renderPanel("CAD for CAM", `
      <dl class="cam-facts">
        <dt>几何版本</dt><dd>v${escapeText(model.geometryRevision ?? 0)}</dd>
        <dt>编辑状态</dt><dd>${hasDraft ? "草稿编辑中" : "当前版本"}</dd>
        <dt>检查对象</dt><dd>${escapeText(inspection.scope || "-")}</dd>
        <dt>检查结果</dt><dd>${escapeText(inspection.status || "未检查")}</dd>
        <dt>开放 Wire</dt><dd>${escapeText(inspection.openWireCount ?? "-")}</dd>
        <dt>开放 Shell</dt><dd>${escapeText(inspection.openShellCount ?? "-")}</dd>
        <dt>退化边</dt><dd>${escapeText(inspection.degeneratedEdgeCount ?? "-")}</dd>
        <dt>Solid</dt><dd>${escapeText(inspection.solidCount ?? "-")}</dd>
      </dl>
      <div class="cam-button-row">
        <button class="tool-button" type="button" data-cam-action="cad-inspect" ${model.isLoaded ? "" : "disabled"}>模型检查</button>
        <button class="tool-button" type="button" data-cam-action="cad-begin" ${model.isLoaded && !hasDraft ? "" : "disabled"}>进入编辑</button>
        <button class="primary-button" type="button" data-cam-action="cad-commit" ${hasDraft ? "" : "disabled"}>提交版本</button>
        <button class="tool-button" type="button" data-cam-action="cad-discard" ${hasDraft ? "" : "disabled"}>放弃编辑</button>
      </div>
      <div class="cam-hint">CAD 操作只修改草稿资源；提交后才切换正式 BRep 与拓扑版本。</div>
    `)}
  `;
}
