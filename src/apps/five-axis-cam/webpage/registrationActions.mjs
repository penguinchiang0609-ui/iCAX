import { activeTool, currentDraft } from './workspaceModel.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import { populateRegistrationDraft, registrationTool, registrationError } from './registrationDraft.mjs';
import { registrationReferenceCandidate, resolveRegistrationCandidate } from './registrationSnapping.mjs';

const current = (context, view) => !view.disposed && context.isCurrentProject?.() !== false;
const savedKey = view => JSON.stringify([view.scene?.domain?.expected?.epoch, selectedPlacement(view)?.id, selectedPlacement(view)?.registrationRef]);

const numeric = (draft, key) => {
  const text = String(draft[key] ?? '').trim();
  if (!text || !Number.isFinite(Number(text))) throw Error('请填写完整且有效的定位坐标与误差限。');
  return Number(text);
};
const point = (draft, prefix) => ['X', 'Y', 'Z'].map(axis => numeric(draft, `${prefix}${axis}`));
export function registrationSpecification(draft, tool) {
  const value = { method: tool === 'fine' ? 'PairedPoints' : ({ point: 'Points', line: 'Lines', face: 'Plane' })[draft.roughMethod],
    modelFrame: 'Workpiece', targetFrame: 'Setup', unit: 'mm' };
  if (tool === 'fine') {
    const count = Number(draft.pointCount);
    if (!Number.isInteger(count) || count < 3 || count > 10) throw Error('实测配对需要 3 至 10 组点。');
    value.modelPoints = Array.from({ length: count }, (_, i) => point(draft, `model${i+1}`));
    value.measuredPoints = Array.from({ length: count }, (_, i) => point(draft, `measured${i+1}`));
    value.maximumResidualMm = numeric(draft, 'registrationTolerance');
    if (value.maximumResidualMm <= 0) throw Error('最大允许残差必须大于 0 mm。');
    if (draft.weightMode === 'custom') {
      value.weights = Array.from({ length: count }, (_, i) => numeric(draft, `weight${i+1}`));
      if (value.weights.some(w => w <= 0)) throw Error('每个测量点的权重必须大于 0。');
      value.weightSource = draft.weightSource.trim();
      if (!value.weightSource) throw Error('请注明测量权重的来源。');
    }
  } else {
    value.origin = point(draft, 'origin');
    value[value.method === 'Lines' ? 'xDirection' : 'xPoint'] = point(draft, 'axisX');
    if (value.method === 'Plane') value.xDirection = value.xPoint.map((v, i) => v - value.origin[i]);
    value[value.method === 'Points' ? 'planePoint' : value.method === 'Lines' ? 'planeDirection' : 'normal'] = point(draft, 'plane');
  }
  if (draft.referenceQueries && Object.keys(draft.referenceQueries).length) {
    value.referenceQueries = Object.fromEntries(Object.entries(draft.referenceQueries).filter(([key]) =>
      tool !== 'fine' || (key.startsWith('model') && Number(key.slice(5)) <= Number(draft.pointCount))));
  }
  return value;
}
export function registrationFieldChanged(view, draft, name) {
  draft.registrationEditRevision = (draft.registrationEditRevision || 0) + 1;
  draft.registrationDirty = true;
  invalidateRegistrationPreview(draft);
  if (['roughMethod', 'capture', 'pointCount'].includes(name)) view.fiveAxisRegistrationPick = null;
  if (name === 'roughMethod') {
    draft.referenceQueries = {}; draft.referenceResults = {}; draft.referenceFaces = {};
    // A point and a direction have different meanings; never reinterpret old values.
    for (const prefix of ['axisX', 'plane']) for (const axis of 'XYZ') draft[`${prefix}${axis}`] = '';
  }
  const prefix = name.replace(/[XYZ]$/, '');
  if (draft.referenceQueries?.[prefix]) delete draft.referenceQueries[prefix];
  if (draft.referenceResults?.[prefix]) delete draft.referenceResults[prefix];
  if (draft.referenceFaces?.[prefix]) delete draft.referenceFaces[prefix];
}
export function beginRegistrationPick(view, tool, prefix) {
  const draft = currentDraft(view, tool);
  if (!selectedPlacement(view)) throw Error('请先导入工件。');
  const vector = tool === 'rough' && (draft.roughMethod === 'line' && ['axisX','plane'].includes(prefix) || draft.roughMethod === 'face' && prefix === 'plane');
  const mode = vector ? (draft.roughMethod === 'face' && prefix === 'plane' ? 'plane' : 'line') : draft.capture;
  view.error = '';
  view.fiveAxisRegistrationPick = { tool, prefix, mode, placementId: selectedPlacement(view).id, draft };
}
export function registrationPickOrder(draft, tool) {
  return tool === 'fine' ? ['model1', 'model2', 'model3'] : ({ point: ['origin', 'axisX', 'plane'], line: ['axisX', 'plane', 'origin'], face: ['plane', 'origin', 'axisX'] })[draft.roughMethod];
}
export function openRegistration(view, tool, method) {
  const draft = currentDraft(view, tool);
  // A command starts a new selection session. Cancel never changes the Scene.
  view.fiveAxisRegistrationOpen = true;
  view.error = '';
  if (draft.registrationRequest) return;
  if (tool === 'rough' && method) draft.roughMethod = method;
  draft.referenceQueries = {}; draft.referenceResults = {}; draft.referenceFaces = {}; draft.registrationPreview = null; draft.registrationDirty = true;
  draft.registrationEditRevision = (draft.registrationEditRevision || 0) + 1;
  for (const prefix of registrationPickOrder(draft, tool)) for (const axis of 'XYZ') draft[`${prefix}${axis}`] = '';
  if (tool === 'fine') { draft.pointCount = '3'; draft.weightMode = 'equal'; }
  if (selectedPlacement(view)) beginRegistrationPick(view, tool, registrationPickOrder(draft, tool)[0]);
}
export async function confirmRegistration(context, view, tool, ops) {
  const draft = currentDraft(view, tool);
  const revision = draft.registrationEditRevision || 0;
  if (!draft.registrationRequest) {
    await runRegistration(context, view, tool, false, ops);
    if (!registrationPreview(view, draft, tool)?.report?.qualified || !view.fiveAxisRegistrationOpen
      || activeTool(view) !== tool || (draft.registrationEditRevision || 0) !== revision) return;
  }
  await runRegistration(context, view, tool, true, ops);
  if (!view.error && !draft.registrationRequest && activeTool(view) === tool && (draft.registrationEditRevision || 0) === revision && current(context, view)) {
    view.fiveAxisRegistrationOpen = false; view.fiveAxisRegistrationPick = null;
    ops.renderProject(context, view);
  }
}
export function flipRegistrationDirection(view, tool, prefix) {
  const draft = currentDraft(view, tool);
  for (const axis of ['X','Y','Z']) draft[`${prefix}${axis}`] = String(-numeric(draft, `${prefix}${axis}`));
  if (draft.referenceQueries?.[prefix]) draft.referenceQueries[prefix].sign = -(draft.referenceQueries[prefix].sign ?? 1);
  draft.registrationEditRevision = (draft.registrationEditRevision || 0) + 1;
  draft.registrationDirty = true; invalidateRegistrationPreview(draft);
}
export function handleRegistrationPick(context, view, userData, hit, event, _hits, ops) {
  const pick = view.fiveAxisRegistrationPick;
  if (!pick) return false;
  if (view.pending) return true;
  const placement = selectedPlacement(view);
  if (!placement || pick.placementId !== placement.id || (hit && String(userData?.objectId) !== placement.id)) {
    view.error = '请在当前加工工件上拾取。'; ops.renderProject(context, view); return true;
  }
  const candidate = registrationReferenceCandidate(view, hit, event);
  if (!candidate) return true;
  const before = pick.draft.registrationEditRevision || 0, expected = JSON.stringify(view.scene.domain.expected);
  // The clicked face is visual feedback, including when exact validation
  // rejects it as a positioning plane. It is never a registration input.
  const face = { triangleIndex:Number(candidate.request.query.triangleIndex), status:'pending' };
  const markFace = status => { pick.draft.referenceFaces={...pick.draft.referenceFaces,[pick.prefix]:{...face,status}}; };
  const stillCurrent = () => current(context,view) && view.fiveAxisRegistrationPick === pick
    && selectedPlacement(view)?.id === pick.placementId && activeTool(view) === pick.tool
    && JSON.stringify(view.scene.domain.expected) === expected && (pick.draft.registrationEditRevision || 0) === before;
  markFace('pending');
  view.pending = true; view.error = ''; ops.renderProject(context, view);
  void resolveRegistrationCandidate(context, candidate).then(result => {
    if (!stillCurrent()) return;
    markFace('accepted');
    const coordinates = ['line','plane'].includes(pick.mode) ? result.direction : result.point;
    for (const [i,axis] of ['X','Y','Z'].entries()) pick.draft[`${pick.prefix}${axis}`] = String(coordinates[i]);
    pick.draft.referenceQueries ??= {}; pick.draft.referenceQueries[pick.prefix] = result.query;
    pick.draft.referenceResults ??= {}; pick.draft.referenceResults[pick.prefix] = result;
    pick.draft.registrationEditRevision = before + 1; pick.draft.registrationDirty = true;
    invalidateRegistrationPreview(pick.draft); view.fiveAxisRegistrationPick = null;
    const order = registrationPickOrder(pick.draft, pick.tool), next = order[order.indexOf(pick.prefix) + 1];
    if (next) beginRegistrationPick(view, pick.tool, next);
  }).catch(error => {
    if (stillCurrent()) {
      markFace('rejected');
      view.error = pick.mode === 'line' && /CAD_REFERENCE_(NOT_FOUND|TOO_FAR)/.test(error.message)
        ? (pick.prefix === 'plane' ? '请选择 XY 平面内另一根不平行直线。' : '请选择作为 X 轴的直线边。')
        : registrationError(error);
      ops.appendProjectLog(context, 'error', view.error);
    }
  }).finally(() => {
    view.pending = false;
    if (!view.disposed && context.isCurrentProject?.() !== false) ops.renderProject(context, view);
  });
  return true;
}
export function selectedPlacement(view) {
  const scene = view.scene;
  return scene?.domain?.placements?.find(p => p.id === scene.activePlacementId && p.setupId === scene.domain.activeSetupId);
}
function inputKey(view, draft, tool) {
  return JSON.stringify([view.scene?.domain?.expected, selectedPlacement(view)?.id, registrationSpecification(draft, tool)]);
}
export function registrationPreview(view, draft, tool) {
  try { return draft.registrationPreview?.key === inputKey(view, draft, tool) ? draft.registrationPreview : null; }
  catch { return null; }
}
export function invalidateRegistrationPreview(draft) { draft.registrationPreview = null; }
export async function loadRegistrationDraft(context, view, ops, force = false) {
  const tool = activeTool(view), placement = selectedPlacement(view);
  if (!['rough', 'fine'].includes(tool) || !placement || !current(context, view) || view.pending) return;
  const draft = currentDraft(view, tool), snapshot = savedKey(view), revision = draft.registrationEditRevision || 0;
  if (draft.registrationRequest || draft.registrationReadingKey || (!force &&
    (draft.registrationLoadedKey === snapshot || draft.registrationReadErrorKey === snapshot || draft.registrationDirty))) return;
  if (!placement.registrationRef?.revision || registrationTool(placement.registrationMethod) !== tool) {
    draft.registrationLoadedKey = snapshot; draft.registrationSavedReport = null;
    return;
  }
  draft.registrationReadingKey = snapshot;
  if (force) { view.error = ''; ops.renderProject(context, view); }
  try {
    const response = await context.sceneProxy.invoke('CamPlacement.ReadRegistration', {
      expected: view.scene.domain.expected, setupId: placement.setupId, placementId: placement.id,
    });
    if (!current(context, view) || activeTool(view) !== tool || savedKey(view) !== snapshot || (draft.registrationEditRevision || 0) !== revision) return;
    if (registrationTool(response.report?.method) !== tool) return;
    populateRegistrationDraft(draft, response.report);
    draft.registrationSavedReport = response.report; draft.registrationLoadedKey = snapshot;
    draft.registrationDirty = false; draft.registrationReadErrorKey = null;
  } catch (error) {
    draft.registrationReadErrorKey = snapshot;
    if (current(context, view) && activeTool(view) === tool && savedKey(view) === snapshot) view.error = registrationError(error);
  } finally {
    draft.registrationReadingKey = null;
    if (current(context, view) && activeTool(view) === tool) ops.renderProject(context, view);
  }
}
export function savedRegistrationReport(view, draft, tool) {
  return draft.registrationLoadedKey === savedKey(view) && registrationTool(draft.registrationSavedReport?.method) === tool
    ? draft.registrationSavedReport : null;
}
export async function runRegistration(context, view, tool, apply, ops) {
  if (view.pending || view.disposed || context.isCurrentProject?.() === false) return;
  const placement = selectedPlacement(view), draft = currentDraft(view, tool);
  if (!placement) throw Error('请先导入加工工件。');
  const preview = registrationPreview(view, draft, tool);
  if (!apply && draft.registrationRequest) throw Error('上次应用结果尚未确认，请先核对上次应用结果。');
  if (apply && !draft.registrationRequest && !preview?.report?.qualified) throw Error('请先求解并检查有效的定位结果。');
  const key = apply ? null : inputKey(view, draft, tool);
  if (apply) draft.registrationRequest ??= { payload: { ...preview.request, commandId: crypto.randomUUID() }, revision: draft.registrationEditRevision || 0 };
  const submitted = draft.registrationRequest;
  const request = apply ? submitted.payload : {
    expected: view.scene.domain.expected, setupId: placement.setupId, placementId: placement.id,
    registration: registrationSpecification(draft, tool) };
  view.pending = true; view.error = ''; ops.renderProject(context, view);
  try {
    let report;
    try { report = await context.sceneProxy.invoke(`CamPlacement.${apply ? 'ApplyRegistration' : 'SolveRegistration'}`, request); }
    catch (error) {
      if (!apply) throw error;
      // Preserve the command ID until the Scene confirms success or non-execution.
      const receipt = await context.sceneProxy.invoke('CamScene.QueryCommand', { epoch: request.expected.epoch, commandId: request.commandId });
      if (receipt.status !== 'Completed') { if (receipt.status === 'NotFound') draft.registrationRequest = null; throw error; }
      report = receipt.result;
    }
    if (view.disposed) return;
    if (apply) {
      draft.registrationRequest = null;
      await saveCommittedProjectEdit(context,view,ops);
      if ((draft.registrationEditRevision || 0) === submitted.revision) draft.registrationDirty = false;
      invalidateRegistrationPreview(draft);
      ops.appendProjectLog(context, 'ok', '工件定位已应用，后续加工结果需要重新生成。');
      if (current(context, view)) await ops.refreshSceneState(context, view);
      if (!view.disposed && context.isCurrentProject?.() !== false)
        await ops.fitViewAfterRenderPublish(context, view, { entityIds: [placement.id] });
    } else {
      // Ignore results if the user changed input, selected another part or closed the project.
      let unchanged = false;
      try { unchanged = key === inputKey(view, draft, tool); } catch {}
      if (unchanged && current(context, view) && activeTool(view) === tool) draft.registrationPreview = { key, request, report };
    }
  } catch (error) {
    if (current(context, view) && activeTool(view) === tool) {
      view.error = draft.registrationRequest ? '应用结果尚未确认，请点击“核对上次应用结果”，不要重复创建定位。' : registrationError(error);
      ops.appendProjectLog(context, 'error', view.error);
    }
  } finally {
    view.pending = false;
    if (!view.disposed && context.isCurrentProject?.() !== false) ops.renderProject(context, view);
  }
}
