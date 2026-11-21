// Targeted new supportStyle=double coverage. Run only after the 2.5 runtime is frozen.
// The completed 2.4 thirteen-case and eight-group acceptance is not rerun here.
export function minimalGrilleDoubleSupportCases() {
  const base = { layoutStyle: 'five_face', width: 1800, height: 400,
    projectionDepth: 100, topDrop: 30, supportMaxSpacing: 800,
    handleEnabled: 'no', barLayoutMode: 'fixed_count', barCount: 3,
    supportStyle: 'double', supportPairClearGap: 20,
    jointMode: 'butt_weld', returnJointMode: 'butt_weld' };
  const complete = (id, parameters = {}, extra = {}) => ({ id, parameters: { ...base, ...parameters },
    create: true, disassemble: true, reopen: true, expectedSupportPairs: 2, expectSolidDefaultProfiles: true, ...extra });
  return { cases: [
    complete('five-double-C-flat', {}, { verifySupportPairOwnership: true }),
    complete('balcony-double-C-flat', { layoutStyle: 'balcony_closure', balconyTopDepth: 160,
      balconyBottomDepth: 240, balconyTopDrop: 20 }),
    complete('five-double-C-gap40', { supportPairClearGap: 40 }, { compareSupportGapTo: 'five-double-C-flat' }),
    complete('five-double-C-narrow', { width: 568 }, { expectedSupportPairs: 1 }),
    complete('five-single-flat-thicker-C', { supportStyle: 'single', supportDepth: 10 },
      { expectedSupportPairs: undefined }),
    complete('reject-single-flat-severed-C', { supportStyle: 'single' },
      { expectedSupportPairs: undefined, expectedDisassemblyRejection: '无效|连通|截断|断开|分离|invalid|connected' }),
    complete('five-single-hollow-compatibility', { supportStyle: 'single', frameProfileType: 'rect',
      frameWidth: 10, frameDepth: 5, innerProfileType: 'rect', innerWidth: 10, innerDepth: 5,
      supportProfileType: 'rect', supportWidth: 20, supportDepth: 10,
      supportWallThickness: 1, supportCornerRadius: 1 },
    { expectedSupportPairs: undefined, expectSolidDefaultProfiles: false }),
  ], batches: [] };
}
