// Representative real native flows. Both legal machining and rejected
// interference are required; no desktop/container success is implied.
export function minimalGrilleFiveFaceCases() {
  const base = { layoutStyle: 'five_face', handleEnabled: 'no', width: 568, height: 400,
    projectionDepth: 100, topDrop: 30, supportMaxSpacing: 800,
    barLayoutMode: 'fixed_count', barCount: 3, returnJointMode: 'butt_weld' };
  const complete = (id, parameters) => ({ id, parameters: { ...base, ...parameters },
    create: true, disassemble: true, reopen: true });
  return {
    cases: [
      { ...complete('five-butt', { jointMode: 'butt_weld' }), verifyParameterOwnership: true },
      complete('five-front-half', { jointMode: 'male_female_half_hole' }),
      complete('five-front-through', { jointMode: 'male_female_through_hole' }),
      complete('five-return-half', { jointMode: 'butt_weld', returnJointMode: 'male_female_half_hole' }),
      complete('five-return-through', { jointMode: 'butt_weld', returnJointMode: 'male_female_through_hole' }),
      complete('five-front-full', { jointMode: 'male_female_through_hole', maleEndForm: 'full_section', frameWidth: 30, frameDepth: 20 }),
      complete('support-rect', { width: 1800, supportProfileType: 'rect' }),
      complete('support-round', { width: 1800, supportProfileType: 'round', supportWidth: 20 }),
      { ...complete('round-A-round-B', { frameProfileType: 'round', frameWidth: 30, innerProfileType: 'round', innerWidth: 10 }),
        materialWitnesses: [
          { sourceKey: 'inner.return_left.0001', point: [10.5, 14.625, 17], inside: true },
          { sourceKey: 'frame.bottom.0001', point: [10.5, 14.625, 17], inside: false },
          { sourceKey: 'frame.left.0001', point: [10.5, 14.625, 17], inside: false },
        ] },
      complete('planar', { layoutStyle: 'planar' }),
      { id: 'planar-inactive-spatial-fields', parameters: { ...base, layoutStyle: 'planar', projectionDepth: 350,
        topDrop: 80, supportMaxSpacing: 300, supportProfileType: 'round', supportWidth: 40,
        returnJointMode: 'male_female_through_hole' }, compareGeometryTo: 'planar' },
      { ...complete('reject-overlapping-male-heads', { frameWidth: 30, frameDepth: 20,
        jointMode: 'male_female_through_hole', returnJointMode: 'male_female_through_hole' }),
        expectedDisassemblyRejection: '干涉|重叠|相撞|穿透|overlap|intersect' },
      { ...complete('reject-severed-same-section-receiver', { jointMode: 'male_female_through_hole', maleEndForm: 'full_section' }),
        expectedDisassemblyRejection: '无效|连通|截断|断开|分离|invalid|connected' },
    ],
    // 8/10/20 creation-only groups were covered separately. This batch also
    // persists actual finished part lists and final BReps for all eight rows.
    batches: [{ count: 8, disassemble: true, reopen: true, export: true,
      parameters: { height: '400', barLayoutMode: 'fixed_count', barCount: '3' } }],
  };
}
