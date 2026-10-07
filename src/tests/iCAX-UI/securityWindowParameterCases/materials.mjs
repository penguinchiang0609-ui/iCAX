// Expectations come from the public descriptor and the product's tube semantics.
// Inactive drafts deliberately remain inside descriptor limits while being
// impossible as active sections: hiding a role must remove its geometry/validation.
export function buildMaterialCases(descriptor) {
  const cases = [];
  const definitions = new Map(descriptor.parameters.map(parameter => [parameter.key, parameter]));
  const base = {
    faceType: 'single', frameLayout: 'four_sides', infillPattern: 'grid',
    assemblyPlanningMode: 'builtin_rules', frameManufacturingMode: 'segment_weld',
    tubeSpecificationPreset: 'custom', accessDoorEnabled: false,
  };
  const door = { ...base, accessDoorEnabled: true,
    doorHorizontalTopCenterOffset: 200, doorHorizontalBottomCenterOffset: 200 };
  const add = (id, key, kind, parameters, compareTo, effect, notes, reasonPattern, options = {}) => {
    if (!definitions.has(key)) throw new Error(`Material test key is absent: ${key}`);
    cases.push({ id: `materials.${id}`, keys: [key], kind, parameters,
      ...(compareTo ? { compareTo } : {}), effect, notes,
      ...(reasonPattern ? { reasonPattern } : {}), ...options });
  };
  const roles = [
    { prefix: 'frame', group: 'outer_profile', active: base,
      changes: { Width: 42, Depth: 42, CornerRadius: 3, WallThickness: 1.4 } },
    { prefix: 'horizontal', group: 'horizontal_profile', active: base,
      changes: { Width: 24, Depth: 24, CornerRadius: 3, WallThickness: 1.3 } },
    { prefix: 'vertical', group: 'vertical_profile', active: base,
      changes: { Width: 18, Depth: 18, CornerRadius: 2, WallThickness: 1.2 } },
    { prefix: 'doorFrame', group: 'door_frame_profile', active: door,
      changes: { Width: 28, Depth: 28, CornerRadius: 3, WallThickness: 1.2 } },
    { prefix: 'doorLeafFrame', group: 'door_leaf_profile', active: door,
      changes: { Width: 22, Depth: 24, CornerRadius: 2, WallThickness: 1.2 } },
    { prefix: 'doorHorizontal', group: 'door_horizontal_profile', active: door,
      changes: { Width: 22, Depth: 22, CornerRadius: 2, WallThickness: 1 } },
    { prefix: 'doorVertical', group: 'door_vertical_profile', active: door,
      changes: { Width: 15, WallThickness: 1 } },
  ];

  const presetExtension = descriptor.extensions?.parameterPresets;
  if (!presetExtension || !Array.isArray(presetExtension.presets)) {
    throw new Error('The material preset extension is required for actual preset application tests');
  }
  const presetKey = presetExtension.selectorParameter;
  for (const preset of presetExtension.presets) {
    const parameters = { ...base, [presetKey]: preset.value, ...preset.values };
    // A matching custom baseline proves that every declared preset value was
    // submitted, and that the selector is not mistaken for a backend expander.
    add(`preset.${preset.value}.applied`, presetKey, 'valid', parameters,
      { ...parameters, [presetKey]: presetExtension.customValue }, 'none',
      '选择规格时提交完整扩展 values；同一尺寸的 custom 成品几何应一致。');
    add(`preset.${preset.value}.dimensions`, presetKey, 'valid', parameters,
      { ...base, frameWidth: 44, frameDepth: 44 }, 'geometry',
      '已应用的规格尺寸必须改变与另一个已知尺寸基线的截面或构件几何。');
  }
  add('preset.custom.preserves-draft', presetKey, 'valid',
    { ...base, [presetKey]: presetExtension.customValue, frameWidth: 44, frameDepth: 42 },
    { ...base, [presetKey]: 'stainless_balanced', frameWidth: 44, frameDepth: 42 }, 'none',
    '切换 custom 不重置手动值；仅选择器不同且提交尺寸相同时几何保持一致。');

  for (const role of roles) {
    const typeKey = `${role.prefix}ProfileType`;
    const typeDefinition = definitions.get(typeKey);
    const typeChoices = typeDefinition.choices.map(choice => choice.value);
    for (const type of typeChoices) {
      add(`${typeKey}.${type}`, typeKey, 'valid', { ...role.active, [typeKey]: type },
        role.active, type === typeDefinition.defaultValue ? 'none' : 'geometry',
        typeChoices.length === 1
          ? '固定管型只有一个合法选项，原生接口必须接受该值。'
          : '圆管与矩形管切换应使用对应实际截面。');
    }
    for (const [suffix, value] of Object.entries(role.changes)) {
      const key = role.prefix + suffix;
      // Round sections do not read Depth/CornerRadius. Activate the rectangular
      // branch for these two parameters rather than claiming hidden drafts work.
      const applicable = role.prefix === 'vertical' && ['Depth', 'CornerRadius'].includes(suffix)
        ? { ...role.active, verticalProfileType: 'rect' } : role.active;
      add(`${key}.changed`, key, 'valid', { ...applicable, [key]: value },
        applicable, 'geometry', '适用分支中独立修改该字段，实际截面或成品几何必须发生变化。');
      if (suffix === 'CornerRadius') {
        add(`${key}.zero`, key, 'valid', { ...applicable, [key]: 0 },
          { ...applicable, [key]: 2 }, 'geometry', '零圆角合法，应生成尖角矩形截面。');
        add(`${key}.overlaps-edges`, key, 'rejection', { ...applicable, [key]: 50 },
          null, 'none', '圆角大于相邻边允许范围，必须拒绝退化/重叠的截面。', '圆角|半径|重叠|相邻|尺寸');
      }
      if (suffix === 'WallThickness') {
        // The rect package's independent innerRadius defaults to 1 mm.
        // Use matching outer R=1 to isolate the declared wall minimum from
        // a genuinely intersecting thin-wall/default-outer-radius combination.
        const radiusKey = `${role.prefix}CornerRadius`;
        const minimumBaseline = typeDefinition.defaultValue === 'rect'
          ? { ...applicable, [radiusKey]: 1 } : applicable;
        add(`${key}.minimum`, key, 'valid',
          { ...minimumBaseline, [key]: definitions.get(key).constraints.minimum },
          minimumBaseline, 'geometry', '公开最小壁厚 0.1 mm 应可生成；矩形管同时设外圆角 R1，与固定内圆角 R1 配合，独立验证壁厚下限。');
        if (typeDefinition.defaultValue === 'rect' && definitions.get(radiusKey).defaultValue > 1) {
          add(`${key}.minimum-with-default-radius-intersection`, key, 'rejection',
            { ...applicable, [key]: definitions.get(key).constraints.minimum },
            null, 'none', '默认外圆角大于固定内圆角 R1；0.1 mm 壁厚导致内孔角部越过外轮廓，原生截面必须拒绝真实相交。Python neutral 不执行此拓扑校验。',
            'contours.*intersect or touch');
          cases[cases.length - 1].nativeOnly = true;
        }
        add(`${key}.solid-section`, key, 'rejection', { ...applicable, [key]: 50 },
          null, 'none', '壁厚超过外截面半宽，内腔消失，必须拒绝。', '壁厚|内腔|尺寸|半径|大于零');
      }
    }
  }

  for (const key of ['verticalDepth', 'verticalCornerRadius']) {
    add(`${key}.round-hidden-draft`, key, 'inactive',
      { ...base, verticalProfileType: 'round', [key]: 100 },
      { ...base, verticalProfileType: 'round' }, 'none',
      '圆管不读取矩形 depth/R 草稿；隐藏值不应改变截面或阻断生成。');
  }

  // Active geometric constraints: each dimension is descriptor-valid but the
  // receiving tube cannot contain it. A generic numeric-limit test cannot cover this.
  for (const key of ['horizontalWidth', 'horizontalDepth', 'verticalWidth', 'verticalDepth']) {
    const applicable = { ...base, ...(key === 'verticalDepth' ? { verticalProfileType: 'rect' } : {}) };
    add(`${key}.receiver-too-small`, key, 'rejection', { ...applicable, [key]: 38 },
      null, 'none', '横/竖杆宽深必须依次小于外框/横杆，并保留接收管内腔及开孔间隙。',
      '杆件|外框|横杆|竖杆|穿管|内腔|开孔|尺寸');
  }
  for (const key of ['frameWidth', 'frameDepth']) {
    add(`${key}.receiver-too-small`, key, 'rejection', { ...base, [key]: 22 },
      null, 'none', '外框截面不能等于或小于主横杆截面。', '杆件|外框|横杆|竖杆|穿管|内腔|入榫');
  }
  add('horizontalWallThickness.through-fit-boundary', 'horizontalWallThickness', 'rejection',
    { ...base, horizontalWallThickness: 1.5 }, null, 'none',
    '拆单时22 mm横管扣除双侧1.5 mm壁厚后内腔为19 mm，小于Φ19+双侧开孔间隙，必须拒绝。',
    '穿管|内腔|间隙', { purpose: 'manufacturing' });
  add('doorHorizontalWallThickness.through-fit-boundary', 'doorHorizontalWallThickness', 'rejection',
    { ...door, doorHorizontalWallThickness: 2 }, null, 'none',
    '拆单时20 mm窗内横管扣除双侧2 mm壁厚后内腔为16 mm，小于Φ16+双侧间隙，必须拒绝。',
    '穿管|内腔|间隙', { purpose: 'manufacturing' });
  for (const [key, parameters, baseline] of [
    ['horizontalWallThickness', { ...base, horizontalWallThickness: 1.5 }, base],
    ['doorHorizontalWallThickness', { ...door, doorHorizontalWallThickness: 2 }, door],
  ]) add(`${key}.through-fit-boundary.display`, key, 'valid', parameters, baseline, 'geometry',
    '成品设计保留真实截面；穿杆内腔与开孔间隙在拆单阶段验证。');

  // These are intentional regression assertions, including presently known
  // failures in multi-face validation. Never convert observed rejection to the expectation.
  for (const faceType of ['single', 'two', 'three']) {
    for (const [prefix, infillPattern] of [['horizontal', 'vertical'], ['vertical', 'horizontal']]) {
      const inactiveBase = { ...base, faceType, infillPattern,
        ...(prefix === 'vertical' ? { verticalProfileType: 'rect' } : {}) };
      const draftValues = { ProfileType: prefix === 'vertical' ? 'round' : 'rect',
        Width: 100, Depth: 100, CornerRadius: 50, WallThickness: 50 };
      for (const [suffix, value] of Object.entries(draftValues)) {
        const key = prefix + suffix;
        add(`${key}.inactive-${faceType}-${infillPattern}`, key, 'inactive',
          { ...inactiveBase, [key]: value }, inactiveBase, 'none',
          '单向格栅不使用另一方向的管材；隐藏草稿即使不能形成该管材也不应阻断当前成品。');
      }
    }
  }

  for (const faceType of ['single', 'two', 'three', 'five']) {
    for (const role of roles.filter(role => role.prefix.startsWith('door'))) {
      const inactiveBase = { ...base, faceType, accessDoorEnabled: false };
      for (const [suffix, value] of Object.entries(role.changes)) {
        const key = role.prefix + suffix;
        add(`${key}.door-disabled-${faceType}`, key, 'inactive',
          { ...inactiveBase, [key]: suffix === 'WallThickness' ? 50 : 100 }, inactiveBase, 'none',
          '关闭开启口后保留草稿值，但不加载窗框/窗扇/窗内管材，也不允许其阻断成品。');
      }
    }
    for (const [prefix, infillPattern] of [['doorHorizontal', 'vertical'], ['doorVertical', 'horizontal']]) {
      const inactiveBase = { ...door, faceType, infillPattern };
      const role = roles.find(candidate => candidate.prefix === prefix);
      for (const suffix of Object.keys(role.changes)) {
        const key = prefix + suffix;
        add(`${key}.door-single-direction-${faceType}-${infillPattern}`, key, 'inactive',
          { ...inactiveBase, [key]: suffix === 'WallThickness' ? 50 : 100 }, inactiveBase, 'none',
          '开启口内也应沿当前填充方向使用管材；未使用方向的隐藏管材草稿不参与校验。');
      }
    }
  }
  // Section/layout errors are checked by display. Tube fit is a manufacturing
  // constraint and must also reach the native manufacturing-plan endpoint.
  for (const test of cases) if (test.kind === 'rejection' && !test.nativeOnly)
    test.purpose = 'manufacturing';
  return cases;
}
