import assert from 'node:assert/strict';
import test from 'node:test';
import { handlePartsAreaAction, renderNestingLeftPane, restoreNestingPartListScroll } from '../../apps/tube-designer/webpage/partsArea.mjs';

function makeScroller(activeId = '', scrollTop = 0, count = 100) {
  let position = scrollTop;
  const writes = [];
  const scroller = {
    clientHeight: 200,
    scrollHeight: count * 40,
    getBoundingClientRect: () => ({ top: 100, height: 200 }),
    querySelector: () => rows.find(row => row.dataset.tubeDesignerPartId === activeId) ?? null,
    querySelectorAll: selector => selector === '[data-tube-designer-nesting-part-row]' ? rows : [],
    get scrollTop() { return position; },
    set scrollTop(value) { position = value; writes.push(value); },
    writes,
  };
  const rows = Array.from({ length: count }, (_, index) => ({
    dataset: { tubeDesignerPartId: `part-${index}` },
    getBoundingClientRect: () => ({ top: 100 + index * 40 - position, height: 40 }),
    scrollIntoView() { assert.fail('Must never animate or scroll ancestor containers'); },
  }));
  return scroller;
}

function harness() {
  const view = { activeAreaId: 'nesting', scene: { tubeDesigner: {} } };
  let scroller;
  const context = { mount: { querySelector: selector => selector === '.tube-designer-cutting-group-list' ? scroller : null } };
  return {
    view, context,
    replace(previous, selectedId, replacement) {
      scroller = previous;
      view.tubeDesignerActiveNestingPartId = selectedId;
      renderNestingLeftPane(context, view);
      scroller = replacement;
      // Mirrors the synchronous product afterProjectRender hook, before paint.
      restoreNestingPartListScroll(context, view);
      return replacement;
    },
  };
}

async function selectPlan(h, planId, previous, replacement) {
  await handlePartsAreaAction(h.context, h.view, 'tube-designer-select-nesting-plan',
    { dataset: { tubeDesignerNestingPlanId: planId } },
    { renderProject() { h.replace(previous, h.view.tubeDesignerActiveNestingPartId, replacement); } });
  return replacement;
}

test('stock selection centers badge 1 without selecting its part or placement', async () => {
  const h = harness();
  h.view.tubeDesignerNestingResult = { plans: [{ id: 'plan-a', placements: [
    { partId: 'part-50', instanceId: 'first' }, { partId: 'part-5', instanceId: 'second' },
  ] }] };
  const result = await selectPlan(h, 'plan-a', makeScroller('part-5', 120), makeScroller());
  assert.deepEqual(result.writes, [1920]);
  assert.equal(h.view.tubeDesignerActiveNestingPlanId, 'plan-a');
  assert.equal(h.view.tubeDesignerNestingSelectionKind, 'plan');
  assert.equal(h.view.tubeDesignerActiveNestingPartId, '');
  assert.equal(h.view.tubeDesignerActiveNestingPlacementId, '');
  // Subsequent paints must not recenter after the user scrolls manually.
  result.scrollTop = 2200;
  const refreshed = h.replace(result, '', makeScroller());
  assert.deepEqual(refreshed.writes, [2200]);
});

test('badge 1 is centered even if visible near an edge, but stays put in the middle band', async () => {
  const h = harness();
  h.view.tubeDesignerNestingResult = { plans: [{ id: 'plan-a', placements: [{ partId: 'part-32' }] }] };
  const edge = await selectPlan(h, 'plan-a', makeScroller('', 1120), makeScroller());
  assert.deepEqual(edge.writes, [1200]);
  const middle = await selectPlan(h, 'plan-a', makeScroller('', 1220), makeScroller());
  assert.deepEqual(middle.writes, [1220]);
  // Clicking the same result again must also locate badge 1 after manual scrolling.
  const repeated = await selectPlan(h, 'plan-a', makeScroller('', 200), makeScroller());
  assert.deepEqual(repeated.writes, [1200]);
});

test('stock switching clamps first/last parts and an empty plan preserves scroll', async () => {
  const h = harness();
  h.view.tubeDesignerNestingResult = { plans: [
    { id: 'first', placements: [{ partId: 'part-0' }] },
    { id: 'last', placements: [{ partId: 'part-99' }] },
    { id: 'empty', placements: [] },
  ] };
  const first = await selectPlan(h, 'first', makeScroller('', 1500), makeScroller());
  assert.deepEqual(first.writes, [0]);
  const last = await selectPlan(h, 'last', first, makeScroller());
  assert.deepEqual(last.writes, [3800]);
  const empty = await selectPlan(h, 'empty', last, makeScroller());
  assert.deepEqual(empty.writes, [3800]);
});

test('visible selection stays in place, including repeated/background renders', async () => {
  const h = harness();
  const original = makeScroller('part-29', 1120);
  const selected = h.replace(original, 'part-31', makeScroller('part-31'));
  assert.deepEqual(selected.writes, [1120]);
  const refreshed = h.replace(selected, 'part-31', makeScroller('part-31'));
  assert.deepEqual(refreshed.writes, [1120], 'same selected ID must not skip restoration');
  await Promise.resolve();
  assert.deepEqual(refreshed.writes, [1120], 'no deferred callback may move the list later');
});

test('empty selection and checkbox renders retain a manually scrolled position', () => {
  const h = harness();
  const result = h.replace(makeScroller('', 2160), '', makeScroller());
  assert.deepEqual(result.writes, [2160]);
});

test('external offscreen selection jumps directly to final position without animation', async () => {
  const h = harness();
  const result = h.replace(makeScroller('part-5', 120), 'part-50', makeScroller('part-50'));
  assert.deepEqual(result.writes, [1920]);
  await Promise.resolve();
  assert.deepEqual(result.writes, [1920]);
});

test('rapid selections do not leave stale animation callbacks behind', async () => {
  const h = harness();
  const first = h.replace(makeScroller('part-5', 120), 'part-50', makeScroller('part-50'));
  const second = h.replace(first, 'part-60', makeScroller('part-60'));
  assert.deepEqual(second.writes, [2320]);
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(second.writes, [2320]);
});

test('filtering clamps position and missing selected rows do not cancel restoration', () => {
  const h = harness();
  const result = h.replace(makeScroller('part-50', 1900), 'part-50', makeScroller('', 0, 10));
  assert.deepEqual(result.writes, [200]);
});

test('snapshots are consumed once and cannot move another product area', () => {
  const h = harness();
  const result = h.replace(makeScroller('part-30', 1120), 'part-31', makeScroller('part-31'));
  restoreNestingPartListScroll(h.context, h.view);
  assert.deepEqual(result.writes, [1120]);
  h.view.activeAreaId = 'profiles';
  const other = h.replace(result, 'part-50', makeScroller('part-50'));
  assert.deepEqual(other.writes, []);
});
