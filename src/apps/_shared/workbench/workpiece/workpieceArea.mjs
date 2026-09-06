import { renderPanel } from "../layout/commonViews.mjs";
import { escapeAttr, escapeText } from "../utils/format.mjs";
import { renderIntentToolpathTree, renderWorkpieceList } from "./workpieceViews.mjs";

export function renderWorkpieceLeftPane(context, view) {
  const scene = view.scene ?? {};
  const model = scene.model ?? {};
  const workpieces = scene.workpieces ?? [];
  const workpieceLabel = context.terminology?.workpiece ?? "工件";
  const loops = scene.loops ?? [];
  const tubeGeometry = scene.tubeGeometry ?? {};
  const holeCount = loops.filter((item) => String(item.role ?? item.label ?? "").toLowerCase().includes("hole")).length;
  const cutCount = Math.max(0, loops.length - holeCount);

  return `
    ${renderPanel(`${workpieceLabel}资源`, renderWorkpieceList(workpieces, model.entityId))}
    ${renderPanel("管材中性几何", `
      <dl class="cam-facts">
        <dt>识别状态</dt><dd>${escapeText(formatRecognitionStatus(tubeGeometry.status))}</dd>
        <dt>截面边界</dt><dd>${escapeText(tubeGeometry.boundaryCount ?? 0)}</dd>
        <dt>截面内腔</dt><dd>${escapeText(tubeGeometry.innerBoundaryCount ?? 0)}</dd>
        <dt>CSG 节点</dt><dd>${escapeText(tubeGeometry.solidNodeCount ?? 0)}</dd>
        <dt>待选解释</dt><dd>${escapeText(tubeGeometry.unresolvedAlternativeCount ?? 0)}</dd>
      </dl>
    `)}
    ${renderPanel("加工特征", `
      <dl class="cam-facts">
        <dt>侧壁孔</dt><dd>${escapeText(holeCount)}</dd>
        <dt>切口/轮廓</dt><dd>${escapeText(cutCount)}</dd>
        <dt>已生成刀路</dt><dd>${escapeText((scene.toolpaths ?? []).length)}</dd>
      </dl>
    `)}
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
  const tubeGeometry = scene.tubeGeometry ?? {};
  const workpieceLabel = context.terminology?.workpiece ?? "工件";

  return `
    ${renderPanel(`导入${workpieceLabel}`, `
      <div class="cam-file-strip">
        <input class="cam-path-input" type="text" data-cam-model-path value="${escapeAttr(view.sourcePath || model.sourcePath || "")}" placeholder="STEP/STP/IGS/IGES 文件路径" />
        <button class="tool-button" type="button" data-cam-action="choose-model" ${view.pending ? "disabled" : ""}>浏览</button>
        <button class="primary-button" type="button" data-cam-action="import-model" ${view.pending ? "disabled" : ""}>导入</button>
      </div>
    `)}
    ${renderPanel(`${workpieceLabel}属性`, `
      <dl class="cam-facts">
        <dt>数量</dt><dd>${escapeText(workpieces.length)}</dd>
        <dt>当前</dt><dd>${escapeText(model.entityId || "-")}</dd>
        <dt>文件</dt><dd>${model.isLoaded ? escapeText(model.sourcePath) : "未导入"}</dd>
      </dl>
    `)}
    ${renderPanel("标准拉伸体识别", `
      <dl class="cam-facts">
        <dt>状态</dt><dd>${escapeText(formatRecognitionStatus(tubeGeometry.status))}</dd>
        <dt>置信度</dt><dd>${escapeText(formatPercent(tubeGeometry.confidence))}</dd>
        <dt>拉伸长度</dt><dd>${escapeText(formatLength(tubeGeometry.extrusionLength))}</dd>
        <dt>轴向</dt><dd>${escapeText(formatAxis(tubeGeometry.axis))}</dd>
        <dt>截面边界</dt><dd>${escapeText(tubeGeometry.boundaryCount ?? 0)}</dd>
        <dt>其中内腔</dt><dd>${escapeText(tubeGeometry.innerBoundaryCount ?? 0)}</dd>
        <dt>实体表达</dt><dd>${escapeText(tubeGeometry.solidNodeCount ?? 0)} 个 CSG 节点</dd>
        <dt>候选解释组</dt><dd>${escapeText(tubeGeometry.alternativeCount ?? 0)}</dd>
        <dt>尚未选择</dt><dd>${escapeText(tubeGeometry.unresolvedAlternativeCount ?? 0)}</dd>
        <dt>重建误差</dt><dd>${escapeText(formatPercent(tubeGeometry.relativeVolumeError))}</dd>
        <dt>Residual 占比</dt><dd>${escapeText(formatPercent(tubeGeometry.relativeUnparameterizedVolume))}</dd>
      </dl>
      <div class="cam-hint">${escapeText(firstDiagnostic(tubeGeometry))}</div>
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

function formatRecognitionStatus(status) {
  return ({
    Exact: "标准拉伸体",
    EquivalentButAmbiguous: "等价 CSG（历史不唯一）",
    Partial: "部分参数化",
    Failed: "未识别",
    Unavailable: "未导入",
  })[status] ?? "未导入";
}

function formatPercent(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? `${(numeric * 100).toFixed(1)}%` : "-";
}

function formatLength(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? `${numeric.toFixed(3)} mm` : "-";
}

function formatAxis(axis) {
  return Array.isArray(axis) && axis.length >= 3
    ? `[${axis.slice(0, 3).map((value) => Number(value).toFixed(4)).join(", ")}]`
    : "-";
}

function firstDiagnostic(tubeGeometry) {
  return tubeGeometry?.diagnostics?.[0]?.message
    ?? "导入 BRep 后，将从平移不变的侧壁恢复虚拟截面并构造等价 CSG。";
}
