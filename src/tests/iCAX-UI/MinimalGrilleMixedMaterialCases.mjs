export function minimalGrilleMixedMaterialCases() {
  const base = { layoutStyle: 'five_face', width: 1200, height: 400, handleEnabled: 'no',
    barLayoutMode: 'fixed_count', barCount: 3, projectionDepth: 100, topDrop: 30,
    supportStyle: 'double', supportPairClearGap: 20, supportMaxSpacing: 800,
    jointMode: 'butt_weld', returnJointMode: 'butt_weld' };
  const complete = (id, parameters, extra = {}) => ({ id, parameters, create: true, disassemble: true,
    reopen: true, mixedMaterials: true, ...extra });
  return { cases: [
    complete('mixed-five-descriptor-default', { layoutStyle: 'five_face' },
      { actualDescriptorDefaults: true, verifyDefaultDoubleGaps: true, mixedMaterialEdits: [
        { group: 'projectionVertical', changes: { projectionVerticalWidth: 18, projectionVerticalDepth: 16,
          projectionVerticalWallThickness: 1, projectionVerticalCornerRadius: 1.5 } },
        { group: 'projectionHorizontal', changes: { projectionHorizontalWidth: 14, projectionHorizontalDepth: 14,
          projectionHorizontalWallThickness: 1, projectionHorizontalCornerRadius: 1.5 } },
      ] }),
    complete('mixed-five-independent-links-3', { ...base, width: 1800,
      supportLinkCount: 3, supportLinkBottomOffset: 70, supportLinkTopOffset: 90 },
      { expectedSupportPairs: 2, verifyIndependentSupportLinks: true }),
    complete('mixed-balcony-independent-depths', { ...base, layoutStyle: 'balcony_closure',
      balconyTopDepth: 160, balconyBottomDepth: 240, balconyTopDrop: 20 }, { expectedSupportPairs: 1 }),
    complete('mixed-five-round-projection', { ...base, projectionVerticalProfileType: 'round',
      projectionVerticalWidth: 15, projectionHorizontalProfileType: 'round', projectionHorizontalWidth: 12 },
      { expectedSupportPairs: 1 }),
  ], batches: [] };
}

export function minimalGrilleMixedSingleSolidCase() {
  return { id: 'mixed-five-single-flat-weld', parameters: { layoutStyle: 'five_face', width: 1200, height: 400,
    handleEnabled: 'no', barLayoutMode: 'fixed_count', barCount: 3, supportStyle: 'single', supportMaxSpacing: 800,
    projectionDepth: 100, topDrop: 30, supportLinkCount: 0, jointMode: 'butt_weld', returnJointMode: 'butt_weld' },
  create: true, disassemble: true, reopen: true, mixedMaterials: true,
  verifySingleSolidWeld: true, verifyMixedMaterialOccupancy: true };
}
