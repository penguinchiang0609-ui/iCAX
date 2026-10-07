import {
  installWindowStateMemory, describeWindowElement, describeControlElement,
} from "../../../iCAX-UI/SDK/Forms/windowStateMemory.mjs";
import { rememberDesignerCreationWindowState } from './designerActions.mjs';

const documents = new WeakMap();
const bindings = new WeakMap();
const restoring = new WeakSet();
const DIALOGS = 'dialog,[role="dialog"],.new-project-dialog';
const PANES = '.tube-designer-workspace .cam-context-pane,.tube-designer-workspace .cam-info-pane';

function bindingFor(element) {
  const mount = element.closest?.('.cam-workbench')?.parentElement;
  if (mount && bindings.has(mount)) return bindings.get(mount);
  for (let node = element; node; node = node.parentElement) {
    if (bindings.has(node)) return bindings.get(node);
  }
  return null;
}

function partScope(binding, partId) {
  return partId && !partId.startsWith('__new')
    ? ['project', binding.context.project?.projectId ?? '', 'part', partId].join('/') : 'new';
}

function sketchScope(view, project) {
  const state = view.tubeDesignerSketch ?? {};
  if (state.mode === 'side') return [project, 'side', state.sideTargetKind,
    state.sideTargetKind === 'part' ? state.targetPartId : state.targetMemberId].join('/');
  if (view.tubeDesignerComponentCSGProfileReturn) {
    const draft = view.tubeDesignerComponentLibrary?.csgDraft;
    return ['csg-section', draft?.mode, draft?.id || 'new', view.tubeDesignerComponentCSGProfileReturn.featureId].join('/');
  }
  if (view.tubeDesignerToolSketchContext) {
    const tool = view.tubeDesignerToolSketchContext;
    return ['tool-section', tool.kind, tool.target, tool.category].join('/');
  }
  const session = state.sectionSession ?? {};
  return ['section', session.kind, session.sourceScope, session.sourceProfileKey || session.sourceProfileId || 'new'].join('/');
}

function describeWindow(element) {
  if (element.closest('[data-window-state-ignore]')) return null;
  const pane = element.matches(PANES);
  const base = describeWindowElement(element) ?? (pane
    ? { key: 'workbench-pane' }
    : element.classList.length ? { key: [...element.classList].sort().join('.') } : null);
  if (!base) return null;
  const binding = bindingFor(element);
  if (!binding?.view) return base;
  const { context, view } = binding;
  const title = element.getAttribute('aria-labelledby') ?? '';
  // These creation forms exchange whole drafts with the model before rendering.
  if (['tube-designer-add-title', 'tube-designer-excel-template-title'].includes(title)) return null;
  let ready = !view.pending && !view.tubeDesignerLoading;
  let scope = '';
  let key = typeof base === 'string' ? base : base.key;
  const project = String(context.project?.projectId ?? '');
  if (pane) {
    const side = element.matches('.cam-context-pane') ? 'left' : 'right';
    key = `area/${view.activeAreaId}/${side}`;
    if (side === 'right') {
      const ids = {
        profiles: view.tubeDesignerSelectedProfileId,
        tools: view.tubeDesignerToolLibrary?.selectedKey,
        assemblies: view.tubeDesignerAssemblyLibrary?.selectedId,
        templates: view.tubeDesignerProductTemplateLibrary?.selectedId,
        components: view.tubeDesignerComponentLibrary?.selectedKey,
      };
      scope = ['view', 'nesting', 'machining'].includes(view.activeAreaId)
        ? `${project}/${view.activeAreaId === 'view' ? view.scene?.tubeDesigner?.product?.entityId ?? '' : view.tubeDesignerActiveNestingPartId ?? ''}`
        : String(ids[view.activeAreaId] ?? '');
      if (view.activeAreaId === 'machining') scope = `${project}/job/${view.tubeDesignerMachining?.selectedId
        || view.scene?.tubeDesigner?.machiningTask?.activeJobId || ''}`;
      if (view.activeAreaId === 'sketch') scope = sketchScope(view, project);
      if (view.activeAreaId === 'components' && view.tubeDesignerComponentLibrary?.csgDraft) {
        const draft = view.tubeDesignerComponentLibrary.csgDraft;
        scope = `csg/${draft.mode}/${draft.id || 'new'}`;
      }
    } else if (['view', 'nesting', 'machining'].includes(view.activeAreaId)) scope = project;
  } else if (title === 'tube-nesting-settings-title') {
    scope = `${project}/${element.dataset.tubeNestingDialog ?? ''}`;
  } else if (title === 'tube-designer-preset-title') {
    const draft = view.tubeDesignerPresetDialog ?? {};
    const templateId = draft.mode === 'add' ? view.tubeDesignerAddTemplateId : view.scene?.tubeDesigner?.product?.templateId;
    scope = [templateId, draft.scopeKey ?? draft.scope, draft.presetId || 'new'].join('/');
  } else if (title === 'tube-designer-profile-title') {
    const draft = view.tubeDesignerProfileDialog ?? {};
    scope = draft.savedProfileId || ['new', draft.mode, draft.prefix,
      draft.profile?.contentDigest || draft.profile?.sourcePath || draft.profile?.sourceFileName || ''].join('/');
  } else if (title === 'tube-designer-punch-title' || title === 'punch-parameter-title') {
    const wizard = view.tubeDesignerPunchWizard;
    if (!wizard) return null;
    if (wizard.catalogueStatus === 'loading') ready = false;
    scope = partScope(binding, String(wizard.partId ?? ''));
    if (title === 'punch-parameter-title') {
      const editor = wizard.parameterEditor ?? {};
      const featureId = editor.index === 'draft' ? 'draft' : wizard.features?.[Number(editor.index)]?.id;
      scope += `/${editor.mode ?? element.dataset.punchEditorMode}/${editor.end ?? ''}/${featureId || wizard.editingId || 'draft'}`;
    }
  } else if (element.classList.contains('td-draw-workbench')) {
    const state = view.tubeDesignerPartDrawing?.state;
    if (!state) return null;
    if (state.catalogueStatus === 'loading') ready = false;
    const drawing = view.tubeDesignerPartDrawing;
    scope = `${partScope(binding, String(state.partId ?? ''))}/${drawing.mode ?? 'main'}/${drawing.selected || state.editingId || 'draft'}`;
  } else if (title === 'tube-excel-automation-title') {
    if (view.tubeDesignerExcelAutomationDialog?.loading) ready = false;
  } else if (title === 'component-import-title') {
    scope = String(view.tubeDesignerComponentLibrary?.importDraft?.sourcePath ?? '');
  } else if (element.classList.contains('tube-section-sketch-dialog') || title === 'tube-sketch-save-choice-title') {
    scope = sketchScope(view, project);
  } else if (/disassembl|breakdown|inspection/.test(title) || title === 'machining-source-title') scope = project;
  return { key, scope, owner: view, ready };
}

function describeControl(control, descriptor) {
  // Group/master checkboxes are derived from the individual selected records.
  // Replaying both the aggregate and its children could undo the most recent clear.
  const action = control.dataset.camAction ?? '';
  if (control.type === 'checkbox' && (/toggle-all(?:-|$)|toggle-all-instances|toggle-(?:part-)?group$/.test(action)
    || ['tube-designer-nesting-toggle-group', 'tube-designer-parts-toggle-group'].includes(action))) return null;
  let base = describeControlElement(control, descriptor);
  if (control.hasAttribute('data-window-state-field') || control.hasAttribute('data-window-state-key')) return base;
  const attributes = [...control.attributes].filter(attribute => attribute.name.startsWith('data-')
    && !attribute.name.startsWith('data-window-state-') && attribute.name !== 'data-tube-designer-punch-index'
    && attribute.name !== 'data-tube-designer-indeterminate' && attribute.name !== 'data-tube-part-category-mixed'
    && attribute.name !== 'data-tube-designer-profile-current-selection');
  // Existing forms already declare semantic action/field/resource identifiers.
  // Empty binding attributes (for example data-tube-template-create-name) also
  // identify fields; labels and array positions are not a substitute for them.
  if (attributes.length) base = { key: JSON.stringify(attributes.map(attribute => [attribute.name, attribute.value]).sort(([a], [b]) => a.localeCompare(b))) };
  if (!base) return null;
  const key = typeof base === 'string' ? base : base.key;
  const dataset = control.dataset;
  const dialog = control.closest(DIALOGS) ?? control.closest(PANES);
  const binding = bindingFor(control);
  const view = binding?.view;
  const contexts = [];
  // Parameter controls belong to the currently chosen resource, not to its
  // position in the form. Changing a profile must load its own remembered inputs.
  if (dataset.profileParameterKey || dataset.standardPartParameter || dataset.drawingParameter
      || dataset.tubeDesignerPunchParameter || dataset.tubeDesignerPunchProfileParameter) {
    const parameterScope = control.closest('[data-profile-parameter-scope]') ?? dialog;
    for (const select of parameterScope?.querySelectorAll('select') ?? []) {
      if (select === control || select.hasAttribute('data-profile-parameter-key')) continue;
      if (/profile|section|tool|template/.test(select.dataset.camChangeAction ?? '')) {
        contexts.push([select.dataset.camChangeAction, select.dataset.drawingSection ?? '', select.value]);
      }
    }
    if (dialog?.getAttribute('aria-labelledby') === 'nesting-standard-part-title') contexts.push(view?.tubeDesignerNestingStandardPartDraft?.profileKey ?? '');
    const productProfile = control.closest('.tube-designer-profile-field')
      ?.querySelector('select[data-tube-designer-profile-prefix]');
    if (productProfile) contexts.push(['product-profile', productProfile.dataset.tubeDesignerProfilePrefix, productProfile.value]);
  }
  const row = control.closest('[data-tube-designer-profile-id],[data-profile-key],[data-stock-operation-id]');
  if (row) contexts.push(row.dataset.tubeDesignerProfileId ?? row.dataset.profileKey ?? row.dataset.stockOperationId);
  if (dataset.tubeDesignerPunchIndex && view?.tubeDesignerPunchWizard) {
    const index = Number(dataset.tubeDesignerPunchIndex);
    contexts.push(Number.isInteger(index) ? view.tubeDesignerPunchWizard.features?.[index]?.id ?? 'draft' : 'draft');
  }
  for (const attribute of ['data-tube-assembly-scene-role', 'data-assembly-scene-role', 'data-csg-selected', 'data-finished-shape']) {
    const value = control.closest(`[${attribute}]`)?.getAttribute(attribute);
    if (value) contexts.push([attribute, value]);
  }
  if (dataset.csgProfileParameter && view?.tubeDesignerComponentLibrary?.csgDraft) {
    const draft = view.tubeDesignerComponentLibrary.csgDraft;
    contexts.push(['csg-profile', draft.features?.find(feature => feature.id === draft.selectedId)?.profile?.key ?? '']);
  }
  if (dataset.sketchField && view?.tubeDesignerSketch) {
    const state = view.tubeDesignerSketch;
    const draft = state.mode !== 'side' ? state.section : state.sideTargetKind === 'part'
      ? state.sideByPart?.[state.targetPartId] : state.sideByMember?.[state.targetMemberId];
    contexts.push(['sketch-entity', dataset.sketchEntityId || draft?.selectedIds?.[0] || '']);
  }
  if ((dataset.pathField || dataset.camChangeAction === 'tube-path-node-index') && view?.activeAreaId === 'machining') {
    const jobId = view.tubeDesignerMachining?.selectedId || view.scene?.tubeDesigner?.machiningTask?.activeJobId;
    const editor = view.tubeDesignerMachining?.edits?.[jobId];
    if (!['plane', 'depth'].includes(dataset.pathField)) contexts.push(['path', editor?.selectedIds?.[0] || '']);
    if (['nx', 'ny', 'nz'].includes(dataset.pathField)) contexts.push(['node', editor?.nodeIndex ?? 0]);
  }
  return { ...(typeof base === 'object' ? base : {}), key: contexts.length ? JSON.stringify([key, contexts]) : key,
    // Checkbox actions maintain their complete linked state in the model.
    // A master clear must supersede the old live value of every leaf checkbox.
    ...(control.type === 'checkbox' && (dataset.camAction || dataset.camChangeAction) ? { transfer: false } : {}) };
}

async function restoreControl(control, _value, restore) {
  if (!restore.isCurrent()) return;
  // Rendered values already came from the current model. Avoid a native preview
  // for every remembered field which is already at the requested value.
  if (JSON.stringify(restore.previousValue) === JSON.stringify(_value)) return;
  const binding = bindingFor(control);
  const view = binding?.view;
  const win = control.ownerDocument.defaultView;
  restoring.add(control);
  try {
    if (control.dataset.camChangeAction) {
      control.dispatchEvent(new win.Event('change', { bubbles: true }));
      await view?.activeAreaAction?.promise;
    } else if ((control.type === 'checkbox' && control.dataset.camAction)
        || control.dataset.camAction === 'tube-designer-excel-automation-change') {
      // These handlers read checked/value; an Event avoids native checkbox toggling.
      control.dispatchEvent(new win.Event('click', { bubbles: true }));
      await view?.activeAreaAction?.promise;
    } else {
      restore.dispatch();
    }
  } finally { restoring.delete(control); }
}

/** Capture linked selection controls after an actual group action has updated the model/DOM. */
export function rememberLinkedWindowControls(context, _view, action, target) {
  if (!target || restoring.has(target)
    || !/(?:toggle-(?:all|instance|part|group)|batch-disassembly-toggle|parts-(?:select|clear)-filtered)/.test(action)) return;
  const document = context.mount?.ownerDocument;
  const controller = document && documents.get(document);
  if (!controller) return;
  const previous = target.closest(`${DIALOGS},${PANES}`);
  let element = previous;
  if (!element?.isConnected) {
    const title = previous?.getAttribute('aria-labelledby');
    element = title ? [...context.mount.querySelectorAll(DIALOGS)].find(dialog => dialog.getAttribute('aria-labelledby') === title)
      : context.mount.querySelector(target.closest('.cam-context-pane') ? '.cam-context-pane' : '.cam-info-pane');
  }
  if (element) controller.captureWindow(element);
}

/** One controller serves dialogs, nested windows and the editable workbench panes. */
export function bindTubeDesignerWindowMemory(context, view, ops = null) {
  const document = context.mount?.ownerDocument ?? globalThis.document;
  if (!document) return null;
  if (context.mount) bindings.set(context.mount, { context, view, ops });
  let controller = documents.get(document);
  if (!controller) {
    controller = installWindowStateMemory(document, {
      namespace: 'icax.tube-designer.window-state',
      dialogSelector: `${DIALOGS},${PANES}`,
      describeWindow, describeControl, restoreControl,
    });
    documents.set(document, controller);
    const captureCreationDraft = event => {
      const binding = bindingFor(event.target);
      if (!binding?.view || !event.target.closest?.('[aria-labelledby="tube-designer-add-title"],[aria-labelledby="tube-designer-excel-template-title"]')) return;
      rememberDesignerCreationWindowState(binding.context, binding.view);
    };
    document.addEventListener('input', captureCreationDraft, true);
    document.addEventListener('change', captureCreationDraft, true);
  }
  if (ops) controller.refresh();
  return controller;
}
