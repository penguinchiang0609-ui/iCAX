import { escapeAttr, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { renderPunchWizardDialog, punchToolDescriptor } from "./punchWizard.mjs";
import { punchArrayGroupInstanceCount } from "./punchArrayGroups.mjs";
import { parameterVisible } from "./parameterConditions.mjs";
import { punchDefinitionDiagramOwner } from "./punchParameterDiagram.mjs";

const DEFAULT_PREFIX = "tube-designer-nesting-punch-create-";
const REGIONS = ["main", "definitions", "parts", "holes", "scene"];

// Compose the batch shell from the ordinary wizard's actual template editors
// and renderer host. This deliberately leaves tool conditions, arrays, end
// recipes and native geometry in their existing implementation.
function elementAt(html, marker, from = 0) {
  const mark = html.indexOf(marker, from);
  if (mark < 0) return null;
  const start = html.lastIndexOf("<", mark);
  const opening = /^<([a-z][\w:-]*)\b[^>]*>/i.exec(html.slice(start));
  if (!opening) return null;
  const tag = opening[1];
  const tokens = new RegExp("</?" + tag + "\\b[^>]*>", "gi");
  tokens.lastIndex = start;
  let depth = 0;
  for (let token; (token = tokens.exec(html));) {
    depth += token[0].startsWith("</") ? -1 : token[0].endsWith("/>") ? 0 : 1;
    if (depth === 0) return {
      start, end: tokens.lastIndex, html: html.slice(start, tokens.lastIndex),
      inner: html.slice(start + opening[0].length, token.index),
    };
  }
  return null;
}

function replaceElement(html, marker, replacement) {
  const element = elementAt(html, marker);
  return element ? html.slice(0, element.start) + replacement + html.slice(element.end) : html;
}

function displayName(value) {
  return typeof value === "string" ? value : value?.["zh-CN"] ?? value?.["en-US"] ?? "";
}

function definitionName(definition, wizard) {
  const descriptor = punchToolDescriptor(wizard, definition.recipe);
  return displayName(descriptor?.displayName) || definition.recipe?.section?.name || definition.recipe?.toolLabel || definition.label || "未选择孔型";
}

function definitionSummary(definition, wizard) {
  const recipe = definition.recipe ?? {}, descriptor = punchToolDescriptor(wizard, recipe);
  if (recipe.section) return recipe.section.profile?.specification || recipe.section.name || "截面刀具";
  const values = recipe.toolParameters ?? {};
  return (descriptor?.parameters ?? []).filter(field => parameterVisible(field, values)).slice(0, 3).map(field => {
    const value = values[field.key] ?? field.defaultValue;
    const choice = (field.options ?? field.choices ?? []).find(option => String(option.value ?? option) === String(value));
    const text = choice?.label ?? (typeof value === "boolean" ? value ? "是" : "否" : value ?? "—");
    return displayName(field.uiTitle ?? field.displayName) + " " + text + (field.unit ?? "");
  }).join(" · ");
}

function configuredCount(row) {
  try {
    return (row.wizard?.features ?? []).reduce((count, feature) => count + punchArrayGroupInstanceCount(feature, Number(row.draft?.length)), 0);
  } catch {
    return null;
  }
}

function totals(batch) {
  return {
    types: batch.parts.length,
    pieces: batch.parts.reduce((sum, row) => sum + (Number.isFinite(Number(row.draft?.quantity)) ? Number(row.draft.quantity) : 0), 0),
  };
}

function button(action, name, label, attrs = "", disabled = false) {
  return '<button type="button" class="tube-designer-secondary" data-cam-action="' + action(name) + '" ' + attrs + (disabled ? " disabled" : "") + ">" + escapeText(label) + "</button>";
}

function splitProfileAdvanced(html, replacement = "") {
  let main = html, advanced = "", count = 0;
  for (;;) {
    const marker = /\bdata-parameter-advanced(?=[\s=>])/.exec(main);
    if (!marker) break;
    const block = elementAt(main, marker[0], marker.index);
    if (!block) break;
    const summary = elementAt(block.inner, "<summary");
    const content = summary ? block.inner.slice(0, summary.start) + block.inner.slice(summary.end) : block.inner;
    // The modal owns disclosure; an old inline collapse state must never hide
    // its fields behind a summary that is no longer part of this interaction.
    const attributes = /^<details\b([^>]*)>/i.exec(block.html)?.[1] ?? "";
    advanced += '<section' + attributes
      .replace(/\sopen(?:=(?:"[^"]*"|'[^']*'|[^\s>]+))?/gi, "")
      .replace(/\sdata-parameter-advanced(?:=(?:"[^"]*"|'[^']*'|[^\s>]+))?(?=\s|$)/gi, "") + '>' + content + '</section>';
    main = main.slice(0, block.start) + (count++ === 0 ? replacement : "") + main.slice(block.end);
  }
  return { main, advanced, count };
}

function renderProfileAdvanced(view, content, action) {
  const editor = view.tubeDesignerPunchBatch.profileAdvancedEditor;
  if (!editor) return "";
  const disabled = view.pending ? " disabled" : "";
  return '<div id="punch-batch-profile-advanced-backdrop" class="tube-designer-modal-backdrop punch-batch-profile-advanced-backdrop" role="presentation">'
    + '<section id="punch-batch-profile-advanced-dialog" class="punch-batch-profile-advanced-dialog" data-punch-profile-advanced-dialog data-punch-batch-profile-advanced-dialog role="dialog" aria-modal="true" aria-labelledby="punch-batch-profile-advanced-title">'
    + '<header class="tube-designer-dialog-header"><div><strong id="punch-batch-profile-advanced-title">管型高级设置</strong></div>'
    + '<button type="button" class="tube-designer-dialog-close" data-cam-action="' + action("batch-profile-advanced-cancel") + '" aria-label="关闭管型高级设置"' + disabled + '>×</button></header>'
    + '<div class="punch-batch-profile-advanced-body tube-designer-punch-main-profile-parameters" data-profile-parameter-scope data-parameter-advanced-open="true">' + content
    + (editor.error ? '<div class="tube-designer-punch-error" role="alert">' + escapeText(editor.error) + "</div>" : "") + '</div>'
    + '<footer class="tube-designer-dialog-footer">' + button(action, "batch-profile-advanced-cancel", "取消", "", view.pending)
    + '<button type="button" class="tube-designer-primary" data-cam-action="' + action("batch-profile-advanced-apply") + '"' + disabled + '>确定</button></footer></section></div>';
}

function renderDefinitions(view, part, options, action) {
  const batch = view.tubeDesignerPunchBatch, wizard = view.tubeDesignerPunchWizard;
  const selected = batch.definitions.find(definition => definition.id === batch.selectedDefinitionId);
  const tabs = batch.definitions.map((definition, index) => button(action, "batch-definition-select",
    "H" + (index + 1) + " · " + definitionName(definition, wizard),
    'data-punch-batch-definition-id="' + escapeAttr(definition.id) + '" aria-pressed="' + (definition.id === batch.selectedDefinitionId) + '" aria-expanded="' + (definition.id === batch.selectedDefinitionId && !batch.definitionEditorCollapsed) + '" title="' + escapeAttr(definitionSummary(definition, wizard)) + '"', view.pending)).join("");
  let editor = "";
  if (selected && !batch.definitionEditorCollapsed) {
    const recipe = { ...selected.recipe, id: selected.recipe.id ?? selected.id };
    const editorState = { ...wizard, features: [recipe], selectedFeatureIds: [], selectedFeatureId: recipe.id,
      editingId: "", showDraftRow: false,
      parameterEditor: { inline: true, index: "0", end: "", mode: "shape", error: "" } };
    const definitionHtml = renderPunchWizardDialog(part, { ...view, tubeDesignerPunchWizard: editorState },
      { ...options, tableMode: true, showEnds: false, introHtml: "", sharedDefinitionLayout: true });
    editor = elementAt(definitionHtml, 'data-punch-inline-editor')?.html ?? "";
    // The shared definition strip owns its illustration entry. Keep the real
    // template controls and use their keys for the existing diagram binding.
    editor = replaceElement(editor, 'class="punch-inline-actions"', "");
    editor = editor.replace(/data-tube-designer-punch-parameter="([^"]+)"/g, '$& data-tool-parameter-key="$1"');
    editor = '<div class="punch-batch-definition-editor" data-punch-batch-definition-editor="' + escapeAttr(selected.id)
      + '" data-tool-parameter-scope data-parameter-diagram-owner="' + escapeAttr(punchDefinitionDiagramOwner(selected.id)) + '">' + editor + "</div>";
  }
  return '<section class="punch-batch-definitions" data-punch-region="definitions" aria-label="整批共用孔型">'
    + '<div class="punch-batch-definition-scroll"><div class="punch-batch-definition-strip"><span>孔型</span><div class="punch-batch-definition-tabs">' + tabs + "</div>"
    + button(action, "batch-definition-add", "＋ 孔型", "", view.pending || wizard.catalogueStatus !== "ready")
    + (selected ? button(action, "batch-definition-remove", "删除孔型", 'data-punch-batch-definition-id="' + escapeAttr(selected.id) + '"', view.pending) : "")
    + (batch.definitions.length ? "" : '<small class="punch-batch-muted">添加整批共用的孔型，再在零件孔位表中选用。</small>')
    + button(action, "section-diagram", "参数示意图", 'data-punch-batch-definition-diagram data-punch-batch-definition-id="' + escapeAttr(selected?.id ?? "") + '"', view.pending || !selected || selected.recipe?.section?.source === "dxf")
    + "</div>" + editor + "</div></section>";
}

function endName(row, end) {
  const recipe = row.wizard?.ends?.[end];
  if (!recipe || recipe.type === "keep") return "保留原端面";
  return recipe.section?.name || recipe.toolLabel || displayName(punchToolDescriptor(row.wizard, recipe)?.displayName) || "端面加工";
}

function partControl(action, row, field, label, attrs = "") {
  const value = row.draft?.[field] ?? "";
  return '<input aria-label="' + escapeAttr(label) + '" value="' + escapeAttr(value) + '" data-cam-change-action="' + action("batch-change")
    + '" data-punch-batch-part-id="' + escapeAttr(row.id) + '" data-punch-batch-field="' + field + '" ' + attrs + "/>";
}

function renderParts(view, endsHtml, action) {
  const batch = view.tubeDesignerPunchBatch, selectedIds = new Set(batch.selectedPartIds ?? []), count = totals(batch);
  const selectedRow = batch.parts.find(row => row.id === batch.selectedPartId);
  const rows = batch.parts.map((row, index) => {
    const selected = row.id === batch.selectedPartId, tone = index % 2 ? "odd" : "even", completed = !!row.completedPartId;
    const identity = 'data-punch-batch-part-id="' + escapeAttr(row.id) + '"';
    const disabled = view.pending || completed ? " disabled" : "";
    const holeCount = configuredCount(row);
    const endButton = end => button(action, "batch-end-open", endName(row, end), identity + ' data-tube-designer-punch-end="' + end + '"', view.pending || completed);
    const datum = '<select aria-label="第 ' + (index + 1) + ' 行长度基准" data-cam-change-action="' + action("batch-change") + '" '
      + identity + ' data-punch-batch-field="lengthDatum"' + disabled + '>' + [["long", "长点 / 包络"], ["center", "端面中心"], ["short", "短点"]]
        .map(([value, label]) => '<option value="' + value + '"' + ((row.draft?.lengthDatum ?? "long") === value ? " selected" : "") + ">" + label + "</option>").join("") + "</select>";
    const controlsDisabled = disabled ? ' disabled' : "";
    const control = (field, label, attrs) => partControl(action, row, field, label, attrs + controlsDisabled);
    let html = '<tr data-punch-batch-part-row="' + escapeAttr(row.id) + '" data-punch-row-tone="' + tone + '"' + (completed ? ' data-punch-batch-completed="true"' : '') + ' class="' + (selected ? "is-current" : "") + '">'
      + '<td class="punch-batch-check"><input type="checkbox" aria-label="选择第 ' + (index + 1) + ' 个零件" data-cam-change-action="' + action("batch-selection") + '" ' + identity + (selectedIds.has(row.id) ? " checked" : "") + disabled + "/></td>"
      + '<th scope="row">' + (completed ? '<span class="punch-batch-receipt" title="该零件已生成">已生成</span>' : button(action, "batch-select", "P" + String(index + 1).padStart(3, "0"), identity + ' aria-pressed="' + selected + '"', view.pending)) + "</th>"
      + "<td>" + control("name", "第 " + (index + 1) + " 行零件名称", 'type="text" maxlength="160"') + "</td>"
      + "<td>" + control("length", "第 " + (index + 1) + " 行长度", 'type="number" min="1" max="100000" step="1"') + "</td>"
      + "<td>" + control("quantity", "第 " + (index + 1) + " 行数量", 'type="number" min="1" max="1000000" step="1" data-tube-designer-integer="true"') + "</td>"
      + "<td>" + datum + "</td><td>" + endButton("start") + "</td><td>" + endButton("end") + "</td>"
      + "<td>" + button(action, "batch-select", (row.wizard?.features?.length ?? 0) + " 行 · " + (holeCount === null ? "待核对" : holeCount + " 孔"), identity, view.pending || completed) + "</td>"
      + "<td>" + control("material", "第 " + (index + 1) + " 行材料", 'type="text" maxlength="240"') + "</td></tr>";
    if (!completed && selected && (batch.endEditorPartId === row.id || view.tubeDesignerPunchWizard?.parameterEditor?.end)) {
      const detail = elementAt(endsHtml, 'class="tube-designer-punch-ends"')?.html ?? "";
      html += '<tr class="punch-batch-end-row' + (selected ? " is-current" : "") + '" data-punch-row-tone="' + tone + '" data-punch-batch-end-detail="' + escapeAttr(row.id) + '"><td colspan="10">'
        + '<div class="punch-batch-end-heading"><span>' + escapeText(row.draft?.name || "当前零件") + " · 端面</span>"
        + button(action, "batch-end-close", "收起", identity, view.pending) + "</div>" + detail + "</td></tr>";
    }
    if (row.draft?.error) html += '<tr data-punch-row-tone="' + tone + '" data-punch-batch-error-row="' + escapeAttr(row.id) + '"><td colspan="10"><span class="tube-designer-punch-error" role="alert">' + escapeText(row.draft.error) + "</span></td></tr>";
    return html;
  }).join("");
  return '<section class="punch-batch-parts" data-punch-region="parts" aria-label="批量零件表"><header class="punch-batch-localbar"><strong>零件</strong><span class="punch-batch-muted">'
    + count.types + " 种 · " + formatNumber(count.pieces) + ' 件</span><nav>' + button(action, "batch-add", "＋ 零件", "", view.pending)
    + button(action, "batch-copy", "复制", "", view.pending || !selectedRow || !!selectedRow.completedPartId)
    + button(action, "batch-remove", "删除所选", "", view.pending || !selectedIds.size || batch.parts.some(row => selectedIds.has(row.id) && row.completedPartId)) + '</nav></header><div class="punch-batch-part-scroll"><table class="punch-batch-part-table" aria-label="批量零件直接录入表">'
    + '<colgroup><col style="width:30px"><col style="width:55px"><col style="width:155px"><col style="width:78px"><col style="width:58px"><col style="width:84px"><col style="width:120px"><col style="width:120px"><col style="width:70px"><col style="width:115px"></colgroup>'
    + '<thead><tr><th aria-label="选择"></th><th>编号</th><th>名称</th><th>长度 / mm</th><th>数量 / 件</th><th>长度基准</th><th>左端面</th><th>右端面</th><th>孔位</th><th>材料</th></tr></thead><tbody>' + rows + "</tbody></table></div></section>";
}

function renderHoleDefinitionControls(html, view, action) {
  const batch = view.tubeDesignerPunchBatch, wizard = view.tubeDesignerPunchWizard;
  let from = 0;
  for (const [index, feature] of wizard.features.entries()) {
    const row = elementAt(html, 'data-tube-designer-punch-row="' + index + '"', from);
    if (!row) continue;
    const cell = elementAt(row.html, 'data-punch-frozen-column="shape"');
    if (!cell) continue;
    const disabled = view.pending ? " disabled" : "";
    const options = batch.definitions.map((definition, number) => '<option value="' + escapeAttr(definition.id) + '"' + (feature.batchDefinitionId === definition.id ? " selected" : "") + ">H" + (number + 1) + " · " + escapeText(definitionName(definition, wizard)) + "</option>").join("");
    const control = '<td data-punch-frozen-column="shape" data-punch-record-column="shape"><select aria-label="第 ' + (index + 1) + ' 行孔型" data-cam-change-action="' + action("batch-hole-definition")
      + '" data-punch-batch-definition-feature-id="' + escapeAttr(feature.id) + '" data-tube-designer-punch-index="' + index + '"' + disabled + '>'
      + (feature.batchDefinitionId ? "" : '<option value="" selected disabled>选择孔型</option>') + options + "</select></td>";
    const nextRow = row.html.slice(0, cell.start) + control + row.html.slice(cell.end);
    html = html.slice(0, row.start) + nextRow + html.slice(row.end);
    from = row.start + nextRow.length;
  }
  return html;
}

function splitter(before, after) {
  return '<div class="tube-designer-punch-region-splitter" data-punch-region-splitter="' + before + ":" + after + '" role="separator" aria-orientation="horizontal" aria-label="调整区域高度" tabindex="0"></div>';
}

export function renderPunchBatchWizardDialog(view, { part, sharedSetupHtml = "", sharedAdvancedSetupHtml = "", options = {} } = {}) {
  const batch = view?.tubeDesignerPunchBatch;
  if (!batch || !part || !view.tubeDesignerPunchWizard) return "";
  const action = name => (options.actionPrefix ?? DEFAULT_PREFIX) + name;
  const current = batch.parts.find(row => row.id === batch.selectedPartId);
  const wizard = view.tubeDesignerPunchWizard;
  const shellView = { ...view, tubeDesignerPunchWizard: { ...wizard, wizardWindowPosition: batch.wizardWindowPosition, punchRegionSizes: undefined } };
  const definitionDiagrams = batch.definitions.map(definition => ({ ...definition,
    recipe: { ...definition.recipe, id: definition.recipe.id ?? definition.id },
    descriptor: punchToolDescriptor(wizard, definition.recipe) }));
  let html = renderPunchWizardDialog(part, shellView, { ...options, definitionDiagrams, title: "批量冲孔向导", tableMode: true, introHtml: sharedSetupHtml });
  if (!html) return "";
  const advancedButton = button(action, "batch-profile-advanced-open", "高级设置", 'data-punch-profile-advanced-open aria-haspopup="dialog" aria-expanded="' + !!batch.profileAdvancedEditor + '"', view.pending);
  const mainProfile = splitProfileAdvanced(elementAt(html, 'data-punch-region="main"')?.html ?? "", advancedButton);
  const count = totals(batch);
  const footer = elementAt(html, 'class="tube-designer-punch-footer"');
  const actions = footer && elementAt(footer.html, 'class="tube-designer-punch-sidebar-actions"')?.html || "";
  const mainActions = '<div class="punch-batch-main-actions">' + actions + "</div>";
  const originalMain = mainProfile.main.replace(/<\/section>$/, mainActions + "</section>");
  const originalEnds = elementAt(html, 'data-punch-region="ends"')?.html ?? "";
  let holes = elementAt(html, 'data-punch-region="holes"')?.html ?? "";
  let scene = elementAt(html, 'data-punch-region="scene"')?.html ?? "";
  const sceneHeader = elementAt(scene, "<header");
  const history = sceneHeader && elementAt(sceneHeader.html, "<nav");
  if (history) scene = scene.replace(history.html, "");
  scene = replaceElement(scene, "<footer", "");
  const header = elementAt(html, 'class="tube-designer-dialog-header"');
  const toolbar = header && elementAt(header.html, 'class="tube-designer-punch-sheet-toolbar"');
  if (header && toolbar) html = html.replace(header.html, header.html.replace(toolbar.html, ""));
  html = html.replace('</strong></div>', '</strong><span class="punch-batch-header-count">' + count.types + " 种零件 · " + formatNumber(count.pieces) + " 件</span>"
    + (history ? history.html.replace("<nav", '<nav class="punch-batch-history" aria-label="撤销与重做"') : "") + "</div>");
  html = html.replace('tube-designer-punch-sheet-dialog"', 'tube-designer-punch-sheet-dialog tube-designer-punch-batch-dialog"');
  holes = renderHoleDefinitionControls(holes, view, action).replace('data-punch-region="holes"', 'data-punch-region="holes" data-punch-batch-current-part="' + escapeAttr(current?.id ?? "") + '"');
  holes = holes.replace('data-punch-frozen-column="shape">截面 / 刀具</th>', 'data-punch-frozen-column="shape">孔型</th>');
  const holesHeading = '<header class="punch-batch-localbar"><strong>' + escapeText(current?.draft?.name || "当前零件") + '</strong><span class="punch-batch-muted">' + wizard.features.length + " 行孔位</span>"
    + (toolbar ? toolbar.html : "") + "</header>";
  holes = holes.replace(/(<div[^>]*data-punch-region="holes"[^>]*>)/, "$1" + holesHeading);
  const sizes = REGIONS.slice(0, -1).filter(name => Number.isFinite(batch.punchRegionSizes?.[name])).map(name => "--punch-" + name + "-height:" + batch.punchRegionSizes[name] + "px").join(";");
  const panes = [originalMain, renderDefinitions(view, part, options, action), renderParts(view, originalEnds, action), holes, scene];
  const workspace = '<div class="tube-designer-punch-sheet-workspace punch-batch-workspace"' + (sizes ? ' style="' + sizes + '"' : "") + ">"
    + panes.map((pane, index) => pane + (index < panes.length - 1 ? splitter(REGIONS[index], REGIONS[index + 1]) : "")).join("") + "</div>";
  html = replaceElement(html, 'class="tube-designer-punch-sheet-workspace"', workspace);
  if (footer) {
    const errors = elementAt(footer.html, 'class="tube-designer-punch-sheet-errors"')?.inner.trim() ?? "";
    html = html.replace(footer.html, errors || batch.error ? '<footer class="tube-designer-punch-footer punch-batch-footer"><div class="tube-designer-punch-sheet-errors">'
      + errors + (batch.error ? '<span class="tube-designer-punch-error" role="alert">' + escapeText(batch.error) + "</span>" : "") + "</div></footer>" : "");
  }
  if (batch.profileAdvancedEditor) {
    const advanced = sharedAdvancedSetupHtml ? splitProfileAdvanced(sharedAdvancedSetupHtml).advanced : mainProfile.advanced;
    const dialog = elementAt(html, "tube-designer-punch-batch-dialog");
    if (dialog) {
      const closing = dialog.end - "</section>".length;
      html = html.slice(0, closing) + renderProfileAdvanced(view, advanced, action) + html.slice(closing);
    }
  }
  return html;
}
