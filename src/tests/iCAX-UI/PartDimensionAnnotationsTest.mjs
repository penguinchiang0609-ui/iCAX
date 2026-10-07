import assert from 'node:assert/strict';
import test from 'node:test';
import { partDimensionCategories, visiblePartDimensionAnnotations, partDimensionVisibilityState,
  partDimensionElementVisible, setPartDimensionMasterVisibility, setPartDimensionCategoryVisibility,
  setPartDimensionElementVisibility } from '../../apps/tube-designer/webpage/partDimensionAnnotations.mjs';

function fixture(overallOnly = false) {
  return { automaticDimensionsVisible: true, hiddenCategories: new Set(overallOnly ? partDimensionCategories.filter(([key]) => key !== 'overall').map(([key]) => key) : []),
    hiddenElementIds: new Set(), shownElementIds: new Set(), selectedElementId: '', dimensionReport: {
      holes: [{ elementId: 'hole:1' }, { elementId: 'hole:2' }], annotations: [
        { id: 'total', category: 'overall', elementIds: [] },
        { id: 'size1', category: 'opening-size', elementIds: ['hole:1'] },
        { id: 'size2', category: 'opening-size', elementIds: ['hole:2'] },
        { id: 'pitch', category: 'center-distance', elementIds: ['hole:1', 'hole:2'] },
      ] } };
}
const ids = state => visiblePartDimensionAnnotations(state).map(annotation => annotation.id);

test('inspection starts with all dimensions; its master and category states derive from effective visibility', () => {
  const state = fixture();
  assert.deepEqual(ids(state), ['total', 'size1', 'size2', 'pitch']);
  assert.deepEqual(partDimensionVisibilityState(state), { total: 4, visible: 4, checked: true, mixed: false });
  setPartDimensionMasterVisibility(state, false);
  assert.deepEqual(ids(state), []);
  for (const [key] of partDimensionCategories) {
    assert.equal(partDimensionVisibilityState(state, key).checked, false);
    assert.equal(partDimensionVisibilityState(state, key).mixed, false);
  }
  assert.equal(partDimensionElementVisible(state, 'hole:1'), false);
  assert.equal(partDimensionElementVisible(state, 'hole:2'), false);
  setPartDimensionMasterVisibility(state, true);
  assert.deepEqual(ids(state), ['total', 'size1', 'size2', 'pitch']);
});

test('an overall-only choice can reveal one opening and mark partial categories consistently', () => {
  const state = fixture(true);
  assert.deepEqual(ids(state), ['total']);
  assert.equal(partDimensionVisibilityState(state).mixed, true);
  assert.equal(partDimensionVisibilityState(state, 'overall').checked, true);
  assert.equal(partDimensionVisibilityState(state, 'opening-size').checked, false);
  setPartDimensionElementVisibility(state, 'hole:1', true);
  assert.deepEqual(ids(state), ['total', 'size1']);
  assert.equal(partDimensionElementVisible(state, 'hole:2'), false);
  assert.deepEqual(partDimensionVisibilityState(state, 'opening-size'), { total: 2, visible: 1, checked: false, mixed: true });
  setPartDimensionElementVisibility(state, 'hole:1', false);
  assert.deepEqual(ids(state), ['total']);
  assert.equal(partDimensionElementVisible(state, 'hole:1'), false);
});

test('opening one category while the master is off enables that category and keeps the others off', () => {
  const state = fixture();
  setPartDimensionMasterVisibility(state, false);
  setPartDimensionCategoryVisibility(state, 'opening-size', true);
  assert.deepEqual(ids(state), ['size1', 'size2']);
  assert.equal(partDimensionVisibilityState(state).mixed, true);
  assert.equal(partDimensionVisibilityState(state, 'opening-size').checked, true);
  assert.equal(partDimensionVisibilityState(state, 'overall').checked, false);
  assert.equal(partDimensionVisibilityState(state, 'center-distance').checked, false);
});

test('hidden openings also suppress pair annotations and group enable restores its participating openings', () => {
  const state = fixture();
  setPartDimensionElementVisibility(state, 'hole:2', false);
  assert.deepEqual(ids(state), ['total', 'size1']);
  assert.equal(partDimensionVisibilityState(state, 'opening-size').mixed, true);
  setPartDimensionCategoryVisibility(state, 'center-distance', true);
  assert.deepEqual(ids(state), ['total', 'size1', 'size2', 'pitch']);
  assert.equal(partDimensionElementVisible(state, 'hole:2'), true);
});

test('a single opening action after master off displays actual rulers and category edits override temporary opening visibility', () => {
  const state = fixture();
  setPartDimensionMasterVisibility(state, false);
  setPartDimensionElementVisibility(state, 'hole:2', true);
  assert.deepEqual(ids(state), ['total', 'size2']);
  assert.equal(partDimensionVisibilityState(state).mixed, true);
  setPartDimensionCategoryVisibility(state, 'opening-size', false);
  assert.deepEqual(ids(state), ['total']);
  setPartDimensionMasterVisibility(state, true);
  assert.deepEqual(ids(state), ['total', 'size1', 'size2', 'pitch']);
});

test('independent opening eyes reveal pair rulers only when both openings are enabled', () => {
  const state = fixture();
  state.automaticDimensionsVisible = false;
  state.hiddenCategories = new Set(partDimensionCategories.map(([key]) => key));
  setPartDimensionElementVisibility(state, 'hole:1', true);
  assert.deepEqual(ids(state), ['size1']);
  assert.equal(partDimensionElementVisible(state, 'hole:1'), true);
  assert.equal(partDimensionElementVisible(state, 'hole:2'), false);
  setPartDimensionElementVisibility(state, 'hole:2', true);
  assert.deepEqual(ids(state), ['size1', 'size2', 'pitch']);
  setPartDimensionElementVisibility(state, 'hole:1', false);
  assert.deepEqual(ids(state), ['size2']);
  assert.equal(partDimensionElementVisible(state, 'hole:2'), true);
  setPartDimensionElementVisibility(state, 'hole:1', true);
  setPartDimensionElementVisibility(state, 'hole:1', false);
  assert.deepEqual(ids(state), ['size2']);
  assert.equal(partDimensionVisibilityState(state, 'overall').visible, 0);
});

test('hiding one participant in an enabled pair category does not switch off its neighbor', () => {
  const state = fixture();
  state.hiddenCategories = new Set(partDimensionCategories.filter(([key]) => key !== 'center-distance').map(([key]) => key));
  assert.deepEqual(ids(state), ['pitch']);
  assert.equal(partDimensionElementVisible(state, 'hole:1'), true);
  assert.equal(partDimensionElementVisible(state, 'hole:2'), true);
  setPartDimensionElementVisibility(state, 'hole:1', false);
  assert.deepEqual(ids(state), []);
  assert.equal(partDimensionElementVisible(state, 'hole:1'), false);
  assert.equal(partDimensionElementVisible(state, 'hole:2'), true);
  setPartDimensionMasterVisibility(state, false);
  assert.equal(partDimensionElementVisible(state, 'hole:2'), false);
});
