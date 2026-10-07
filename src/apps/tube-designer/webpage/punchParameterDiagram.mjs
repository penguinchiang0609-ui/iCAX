import { escapeAttr, escapeText } from '../../_shared/workbench/utils/format.mjs';
import { renderProfileParameterDiagram } from './profileParameterDiagram.mjs';
import { renderToolParameterDiagram } from './toolParameterDiagram.mjs';
import { libraryDiagramPositionStyle, renderDiagramResizeHandles } from './floatingParameterDiagram.mjs';
import { parameterVisible } from './parameterConditions.mjs';

export const punchMainDiagramOwner = 'punch-main';
export function punchDefinitionDiagramOwner(id) { return 'punch-definition:' + id; }
export function punchSectionDiagramOwner(item, index, end = '') {
  return 'punch-section:' + (end || (String(index) === 'draft' ? 'draft' : item.id));
}

// Diagram selection and geometry belong to the UI, never the cutting recipe.
export function handlePunchDiagramAction(view, action, target) {
  if (!['section-diagram', 'section-diagram-close'].includes(action)) return false;
  const state = view.tubeDesignerPunchWizard;
  if (!state) return true;
  if (action === 'section-diagram-close') { state.sectionDiagram = null; return true; }
  const definitionId = target?.dataset?.punchBatchDefinitionId;
  if (target?.dataset?.punchBatchDefinitionDiagram !== undefined) {
    state.sectionDiagram = view.tubeDesignerPunchBatch?.definitions?.some(definition => definition.id === definitionId)
      ? { definitionId } : null;
    return true;
  }
  const index = String(target?.dataset?.tubeDesignerPunchIndex ?? 'main');
  const end = target?.dataset?.tubeDesignerPunchEnd ?? '';
  const item = end ? state.ends[end] : index === 'draft' ? state.draft : state.features[Number(index)];
  state.sectionDiagram = index === 'main' ? { main: true }
    : item ? { index, end, featureId: item.id } : null;
  return true;
}

export function renderPunchParameterDiagram(view, options, action) {
  const state = view.tubeDesignerPunchWizard, selected = state?.sectionDiagram;
  if (!selected) return '';
  let snapshot, definitions, parameters, owner, title, tool;
  if (selected.main) {
    if (!options.mainDiagram) return '';
    ({ snapshot, definitions, parameters } = options.mainDiagram);
    owner = punchMainDiagramOwner; title = '主管参数示意图';
  } else if (selected.definitionId) {
    const batch = view.tubeDesignerPunchBatch;
    const definition = options.definitionDiagrams?.find(item => item.id === selected.definitionId);
    if (!definition || batch?.selectedDefinitionId !== definition.id) return '';
    const item = definition.recipe;
    title = '孔型参数示意图';
    if (item.section) {
      if (item.section.source === 'dxf') return '';
      const choice = options.branchProfiles?.find(profile => profile.key === item.section.key);
      snapshot = item.section.profile;
      parameters = item.section.parameters ?? choice?.defaultParameters ?? {};
      definitions = choice?.definitions ?? snapshot?.parameterDefinitions ?? [];
      owner = punchSectionDiagramOwner(item, '0');
    } else {
      tool = definition.descriptor;
      if (!tool) return '';
      definitions = tool.parameters ?? [];
      parameters = { ...Object.fromEntries(definitions.map(field => [field.key, field.defaultValue])), ...item.toolParameters };
      owner = punchDefinitionDiagramOwner(definition.id);
    }
  } else {
    const index = selected.end ? selected.end : selected.index === 'draft' ? 'draft'
      : state.features.findIndex(item => item.id === selected.featureId);
    const item = selected.end ? state.ends[selected.end] : index === 'draft' ? state.draft : state.features[index];
    const editor = state.parameterEditor;
    // A section's diagram accompanies its controls, so it cannot target an old,
    // disconnected row after switching cells or removing a feature.
    if (!item?.section || !editor?.inline || editor.index !== String(index)
      || editor.end !== selected.end || editor.mode !== 'shape') return '';
    const choice = options.branchProfiles?.find(p => p.key === item.section.key);
    snapshot = item.section.profile;
    parameters = item.section.parameters ?? choice?.defaultParameters ?? {};
    definitions = (choice?.definitions ?? snapshot?.parameterDefinitions ?? []).filter(d => parameterVisible(d, parameters));
    owner = punchSectionDiagramOwner(item, index, selected.end);
    title = selected.end ? '端面截面参数示意图' : '支管参数示意图';
  }
  const diagram = tool ? renderToolParameterDiagram({ tool, definitions, values: parameters, expanded: true, showToggle: false })
    : renderProfileParameterDiagram(snapshot, { definitions, parameters, compact: true, title });
  const key = selected.definitionId ? 'punch-definition' : 'punch-section';
  return `<div class="tube-floating-parameter-diagram-layer" data-floating-parameter-diagram-layer>
    <aside class="tube-library-diagram-dock" role="dialog" aria-modal="false" aria-label="${escapeAttr(title)}" data-library-floating-diagram="${key}" data-floating-diagram-initial="center" style="${libraryDiagramPositionStyle(view, key)}">
      ${renderDiagramResizeHandles()}<header data-floating-diagram-drag><strong>${escapeText(title)}</strong><button type="button" data-cam-action="${action('section-diagram-close')}" aria-label="关闭示意图">×</button></header>
      <div class="tube-library-diagram-content" data-parameter-diagram-for="${escapeAttr(owner)}">${diagram}</div>
    </aside></div>`;
}
