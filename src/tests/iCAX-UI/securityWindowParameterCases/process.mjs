import { readFileSync } from 'node:fs';

// Valid selectors must resolve to checked-in mould packages, never invented fixtures.
const MOULD_ROOT = new URL('../../../apps/tube-designer/templates/mold/', import.meta.url);
const VALID_GROOVES = ['v-notch-sharp', 'edge-arc-groove'].map(id => {
  const descriptor = JSON.parse(readFileSync(new URL(`${id}/tool.json`, MOULD_ROOT), 'utf8'));
  if (descriptor.id !== id || descriptor.target !== 'part') {
    throw new Error(`Security-window process coverage requires actual part mould ${id}`);
  }
  return `system:${id}`;
});

const SINGLE = {
  faceType: 'single', frameLayout: 'four_sides', infillPattern: 'grid',
  accessDoorEnabled: false, assemblyPlanningMode: 'builtin_rules',
};
const OPEN = { ...SINGLE, accessDoorEnabled: true };
const SMALL_RECEIVER = {
  ...SINGLE, frameWidth: 20, frameDepth: 20, frameCornerRadius: 0,
  horizontalWidth: 12, horizontalDepth: 12, horizontalCornerRadius: 0,
  verticalWidth: 8, verticalDepth: 8,
};

export function buildProcessCases(descriptor) {
  const fields = new Map(descriptor.parameters.map(field => [field.key, field]));
  const cases = [];
  const add = (id, key, parameters, options = {}) => cases.push({
    id: `process.${id}`, keys: Array.isArray(key) ? key : [key], kind: 'valid',
    parameters, purpose: 'manufacturing', ...options,
  });
  const choices = key => fields.get(key).choices.map(choice => choice.value);
  const inactive = (id, key, baseline, changed, notes = '', options = {}) => add(
    id, key, { ...baseline, ...changed },
    { kind: 'inactive', compareTo: baseline, effect: 'none', notes, ...options },
  );
  const rejectionReasons = {
    productCode: 'productCode.*(?:minimum|maximum|length)',
    assemblyPlanningMode: '独立(?:装配|.*工艺).*(?:单面|四边框|开启口|纯横杆|纯竖杆)',
    frameManufacturingMode: '空间.*(?:方管|宽深相等|方形)',
    horizontalBranchReserve: '入榫深度.*(?:管壁|大于0)',
    verticalBranchReserve: '入榫深度.*管壁',
    assemblyClearance: '安全穿管.*(?:内腔|间隙)',
    frameCornerJoin: '45.*(?:宽深相等|方管)',
  };
  const rejection = (id, key, parameters, notes, reasonPattern) => add(
    id, key, parameters, { kind: 'rejection', notes,
      reasonPattern: reasonPattern ?? rejectionReasons[key] },
  );

  for (const length of [1, 80]) {
    add(`productCode.length-${length}`, 'productCode', { ...SINGLE, productCode: 'P'.repeat(length) },
      { compareTo: SINGLE, effect: 'none', notes: '编号只改变产品和零件标识，不改变管件实体。' });
  }
  add('productCode.unicode', 'productCode', { ...SINGLE, productCode: '防盗窗-示例-02' },
    { compareTo: SINGLE, effect: 'none', notes: '中文标识须保留，实体不变。' });
  rejection('productCode.empty', 'productCode', { ...SINGLE, productCode: '' }, 'minimumLength=1。');
  rejection('productCode.too-long', 'productCode', { ...SINGLE, productCode: 'P'.repeat(81) }, 'maximumLength=80。');

  for (const pattern of ['horizontal', 'vertical']) {
    const baseline = { ...SINGLE, infillPattern: pattern };
    for (const mode of choices('assemblyPlanningMode')) {
      add(`assemblyPlanningMode.${mode}.${pattern}`, 'assemblyPlanningMode',
        { ...baseline, assemblyPlanningMode: mode },
        { compareTo: baseline, effect: mode === 'builtin_rules' ? 'none' : 'geometry',
          notes: '独立节点工艺输出未切原管和 L/T 连接节点，旧规则输出原插接构造。' });
    }
  }
  for (const [id, change] of [
    ['open-frame', { frameLayout: 'left_right' }],
    ['opening', { accessDoorEnabled: true }],
    ['cross-grid', { infillPattern: 'grid' }],
  ]) {
    inactive(`assemblyPlanningMode.unsupported-${id}`, 'assemblyPlanningMode',
      { ...SINGLE, infillPattern: 'vertical', ...change }, { assemblyPlanningMode: 'external_templates' },
      '独立装配选项不适用时按描述符 unavailableChoiceFallback 使用内置路线；原选择保留在宿主参数中。',
      { nativeDisassembly: true, nativeSolidEffect: 'none', expectedProcessParameters: { assemblyPlanningMode: 'builtin_rules' } });
  }
  for (const faceType of ['two', 'three', 'five'])
    inactive(`assemblyPlanningMode.hidden-${faceType}`, 'assemblyPlanningMode',
      { ...SINGLE, faceType }, { assemblyPlanningMode: 'external_templates' },
      '多面窗使用内置路线，隐藏的装配来源草稿不能阻断制造。');

  for (const faceType of ['single', 'two', 'three', 'five']) {
    const baseline = { ...SINGLE, faceType, frameManufacturingMode: 'segment_weld' };
    for (const mode of choices('frameManufacturingMode')) {
      const fallback = mode === 'spatial_v_notch' && ['single', 'five'].includes(faceType);
      add(`frameManufacturingMode.${faceType}.${mode}`, 'frameManufacturingMode',
        { ...baseline, frameManufacturingMode: mode },
        { compareTo: fallback ? { ...baseline, frameManufacturingMode: 'plane_v_notch' } : baseline,
          effect: mode === 'segment_weld' || fallback ? 'none' : 'geometry',
          notes: fallback ? '单面/五面不可选空间折弯，保留草稿并应用平面折弯。' : '验证实际面型分支和框件拆分。' });
    }
  }
  for (const faceType of ['single', 'five']) for (const tool of VALID_GROOVES) {
    const baseline = { ...SINGLE, faceType, frameManufacturingMode: 'spatial_v_notch', outerFrameGrooveTool: tool };
    for (const [key, value] of [['outerFrameBendKFactor', -1], ['outerFrameFoldBridge', -1],
      ['outerFrameVGrooveBottomStrategy', 'rounded'], ['outerFrameVGrooveRoundRadius', -1], ['outerFrameVGrooveMaleFemale', false]])
      inactive(`${key}.hidden-spatial-${faceType}-${tool}`, key, baseline, { [key]: value },
        '该面型不能选择空间路线，隐藏的外框工艺草稿不参与派生平面路线；宿主仍须原样保留草稿。');
  }
  rejection('frameManufacturingMode.spatial-nonsquare', 'frameManufacturingMode',
    { ...SINGLE, faceType: 'three', frameManufacturingMode: 'spatial_v_notch', frameDepth: 40 },
    '空间连续折弯要求实际闭口方形截面，宽深不等拒绝。');

  for (const join of choices('frameJoinType')) {
    const folded = join === 'v_groove_90:tool_library';
    const baseline = { ...SINGLE, frameManufacturingMode: folded ? 'plane_v_notch' : 'segment_weld' };
    add(`frameJoinType.${join}`, 'frameJoinType', { ...baseline, frameJoinType: join },
      { compareTo: baseline, purpose: 'manufacturing', effect: join === 'butt_90' ? 'geometry' : 'none',
        notes: folded ? '平面折弯由制造方式控制，旧 V 连接枚举不覆盖制造方式。' : '直拼、斜拼具有不同加工实体。' });
  }
  for (const value of choices('frameButtWrapMode')) {
    const baseline = { ...SINGLE, frameJoinType: 'butt_90' };
    add(`frameButtWrapMode.${value}`, 'frameButtWrapMode', { ...baseline, frameButtWrapMode: value },
      { compareTo: baseline, effect: value === 'side_wraps_horizontal' ? 'none' : 'geometry',
        notes: '检查包边方向改变横框/竖框端点和毛坯长度。' });
  }
  inactive('frameJoinType.stale-fold-selection', 'frameJoinType', SINGLE,
    { frameJoinType: 'v_groove_90:tool_library' }, '分段拼焊不得被保留的 V 连接草稿激活折弯。');
  inactive('frameButtWrapMode.hidden-miter', 'frameButtWrapMode', SINGLE,
    { frameButtWrapMode: 'horizontal_wraps_side' }, '45°斜拼时包边方向不适用。');
  inactive('frameJoinType.hidden-folded', 'frameJoinType', { ...SINGLE, frameManufacturingMode: 'plane_v_notch' },
    { frameJoinType: 'butt_90' }, '制造方式已确定连续折弯，隐藏直拼草稿不改变模型。');

  for (const [joinKey, wrapKey] of [
    ['doorFrameJoinType', 'doorFrameButtWrapMode'],
    ['doorLeafFrameJoinType', 'doorLeafFrameButtWrapMode'],
  ]) {
    for (const join of choices(joinKey)) {
      add(`${joinKey}.${join}`, joinKey, { ...OPEN, [joinKey]: join },
        { compareTo: OPEN, purpose: 'manufacturing', effect: join === 'miter_45' ? 'none' : 'geometry',
          notes: '固定窗框和活动窗扇框分别验证全部连接工艺。' });
    }
    for (const value of choices(wrapKey)) {
      const baseline = { ...OPEN, [joinKey]: 'butt_90' };
      add(`${wrapKey}.${value}`, wrapKey, { ...baseline, [wrapKey]: value },
        { compareTo: baseline, effect: value === 'side_wraps_horizontal' ? 'none' : 'geometry',
          notes: '两种直拼包边方向均检查框件端点。' });
    }
    inactive(`${wrapKey}.hidden-miter`, wrapKey, OPEN, { [wrapKey]: 'horizontal_wraps_side' });
    for (const faceType of ['single', 'two', 'three', 'five']) {
      const baseline = { ...SINGLE, faceType };
      inactive(`${joinKey}.disabled-opening.${faceType}`, joinKey, baseline,
        { [joinKey]: 'v_groove_90:tool_library' }, '关闭开启口后保留连接工艺草稿，不能新增框件或阻断生成。');
      inactive(`${wrapKey}.disabled-opening.${faceType}`, wrapKey, baseline,
        { [wrapKey]: 'horizontal_wraps_side' }, '关闭开启口后包边草稿不适用。');
    }
  }

  for (const [key, active] of [
    ['outerFrameGrooveTool', { ...SINGLE, frameManufacturingMode: 'plane_v_notch' }],
    ['doorFrameGrooveTool', { ...OPEN, doorFrameJoinType: 'v_groove_90:tool_library' }],
    ['doorLeafFrameGrooveTool', { ...OPEN, doorLeafFrameJoinType: 'v_groove_90:tool_library' }],
  ]) {
    for (const ref of VALID_GROOVES) {
      add(`${key}.${ref}`, key, { ...active, [key]: ref },
        { compareTo: active, purpose: 'manufacturing',
          effect: ref === 'system:v-notch-sharp' ? 'none' : 'geometry',
          notes: '硬依赖仓库真实 mold/v-notch-sharp 和 mold/edge-arc-groove；求解实际模具刀具中性图，未覆盖 BRep 布尔加工。' });
    }
    for (const ref of ['system:security-window-test-missing-mould', '']) {
      add(`${key}.invalid-${ref ? 'missing' : 'empty'}`, key, { ...active, [key]: ref },
        { kind: 'rejection', purpose: 'manufacturing',
          reasonPattern: ref ? '缺少(?:匹配)?刀具|没有固化刀具|(?:模具|刀具).*(?:不存在|缺失|缺少)'
            : '模具.*引用无效',
          notes: '激活连续框时失效资源应明确失败，不能默认替代。' });
    }
    inactive(`${key}.inactive-missing`, key, key === 'outerFrameGrooveTool' ? SINGLE : OPEN,
      { [key]: 'system:security-window-test-missing-mould' }, '未采用对应 V 槽时失效资源草稿不影响模型。');
    if (key !== 'outerFrameGrooveTool') {
      inactive(`${key}.disabled-opening`, key, SINGLE,
        { [key]: 'system:security-window-test-missing-mould' }, '关闭开启口后不读取旧槽口资源。');
    }
  }

  for (const key of ['horizontalBranchReserve', 'verticalBranchReserve']) {
    for (const value of [1.3001, 20]) {
      add(`${key}.valid-${value}`, key, { ...SINGLE, [key]: value },
        { compareTo: SINGLE, effect: 'geometry', notes: '实际穿过管壁且保留对侧壁的入榫。' });
    }
    rejection(`${key}.inner-wall-touch`, key, { ...SINGLE, [key]: 1.3 },
      '1.2 mm接收管壁＋0.1 mm间隙，等于临界值仍未穿过壁厚。');
    add(`${key}.outer-wall-near`, key, { ...SMALL_RECEIVER, [key]: 18.6999 },
      { compareTo: SMALL_RECEIVER, effect: 'geometry', notes: '20−1.2−0.1=18.7；略低于对壁临界值。' });
    rejection(`${key}.outer-wall-touch`, key, { ...SMALL_RECEIVER, [key]: 18.7 },
      '20 mm接收管的对侧壁临界值，等值接触仍须拒绝。');
  }
  const zeroInsertion = { ...SINGLE, firstHorizontalTopOffset: 1000, lastHorizontalBottomOffset: 800,
    verticalLeftCenterOffset: 562, verticalRightCenterOffset: 562 };
  add('horizontalBranchReserve.zero-active', 'horizontalBranchReserve',
    { ...zeroInsertion, horizontalBranchReserve: 0 },
    { compareTo: zeroInsertion, effect: 'geometry', nativeDisassembly: true, nativeSolidEffect: 'geometry',
      nativeHorizontalEndFit: true,
      notes: '零入榫让横杆端面停在实际接收管近侧面，仍按插接路线开间隙孔；原生实体证明杆长及终点改变、横杆未越壁、孔旁近壁和对壁保留。' });
  add('verticalBranchReserve.zero', 'verticalBranchReserve', { ...SINGLE, verticalBranchReserve: 0 },
    { compareTo: SINGLE, effect: 'geometry', notes: '零入榫允许竖杆端部不插入框腔。' });
  inactive('horizontalBranchReserve.pure-vertical', 'horizontalBranchReserve',
    { ...SINGLE, infillPattern: 'vertical' }, { horizontalBranchReserve: 0 });
  inactive('verticalBranchReserve.pure-horizontal', 'verticalBranchReserve',
    { ...SINGLE, infillPattern: 'horizontal' }, { verticalBranchReserve: 0 });
  for (const faceType of ['two', 'three', 'five']) {
    for (const key of ['horizontalBranchReserve', 'verticalBranchReserve']) {
      rejection(`${key}.multiface-inner-wall.${faceType}`, key, { ...SINGLE, faceType, [key]: 1.3 },
        '跨面型一致性：同样的接收框管、壁厚和间隙不能在多面窗忽略穿壁规则。');
    }
  }

  for (const clearance of [0, 0.4]) {
    add(`assemblyClearance.valid-${clearance}`, 'assemblyClearance', { ...SINGLE, assemblyClearance: clearance },
      { compareTo: SINGLE, purpose: 'manufacturing', effect: 'geometry', notes: '改变实际穿杆孔刀具间隙。' });
  }
  rejection('assemblyClearance.through-fit-equality', 'assemblyClearance',
    { ...SINGLE, assemblyClearance: 0.5 }, '横管22−2×1=20，竖管19＋2×0.5=20，内腔等宽不能安全穿杆。');
  for (const faceType of ['two', 'three', 'five']) {
    const baseline = { ...SINGLE, faceType };
    for (const join of choices('frameCornerJoin')) {
      add(`frameCornerJoin.${faceType}.${join}`, 'frameCornerJoin', { ...baseline, frameCornerJoin: join },
        { compareTo: baseline, purpose: 'manufacturing', effect: join === 'post_butt' ? 'none' : 'geometry',
          notes: '检查通长立柱/横框45°拼角的框件长度和加工中性图。' });
    }
    add(`frameCornerJoin.nonsquare.${faceType}`, 'frameCornerJoin',
      { ...baseline, frameCornerJoin: 'rail_miter', frameDepth: 40 },
      { compareTo: { ...baseline, frameCornerJoin: 'rail_miter' }, effect: 'geometry',
        ...(faceType === 'two' ? { nativeDisassembly: true, nativeSolidEffect: 'geometry', nativeMiterContact: true } : {}),
        notes: '当前端面斜切依据真实截面及不共线轴线，支持38×40非方管；两面型额外检查真实加工实体接触和穿透。' });
    inactive(`frameCornerJoin.hidden-folded.${faceType}`, 'frameCornerJoin',
      { ...baseline, frameManufacturingMode: 'plane_v_notch' }, { frameCornerJoin: 'rail_miter' });
  }

  const external = { ...SINGLE, infillPattern: 'vertical', assemblyPlanningMode: 'external_templates' };
  const oldDrafts = {
    frameManufacturingMode: 'spatial_v_notch', frameJoinType: 'butt_90',
    frameButtWrapMode: 'horizontal_wraps_side', outerFrameGrooveTool: 'system:security-window-test-missing-mould',
    doorFrameJoinType: 'v_groove_90:tool_library', doorFrameButtWrapMode: 'horizontal_wraps_side',
    doorLeafFrameJoinType: 'v_groove_90:tool_library', doorLeafFrameButtWrapMode: 'horizontal_wraps_side',
    doorFrameGrooveTool: 'system:security-window-test-missing-mould',
    doorLeafFrameGrooveTool: 'system:security-window-test-missing-mould',
    horizontalBranchReserve: 0, verticalBranchReserve: 0, assemblyClearance: 0.5, frameCornerJoin: 'rail_miter',
  };
  for (const [key, value] of Object.entries(oldDrafts)) {
    inactive(`${key}.external-ignores-draft`, key, external, { [key]: value },
      '独立节点工艺只以成品布局与截面作为输入；停用的旧加工草稿保持但不影响生成。');
  }

  // Actual receiving sections must govern both preview and manufacturing.
  // These fixtures isolate the post's depth axis and the leaf's own walls.
  for (const purpose of ['manufacturing']) {
    for (const faceType of ['two', 'three', 'five']) {
      const fixtures = [
        { id: 'post-depth-axis', key: 'horizontalBranchReserve', valid: 13.69, baseline: 5, rejected: [13.7, 13.71],
          parameters: { ...SINGLE, faceType, infillPattern: 'horizontal', tubeSpecificationPreset: 'custom',
            frameWidth: 38, frameDepth: 15, horizontalWidth: 8, horizontalDepth: 8,
            horizontalCornerRadius: 1, horizontalWallThickness: 0.8, verticalWidth: 6, verticalWallThickness: 0.5 },
          notes: '转角立柱沿15 mm深度接收横杆：15−1.2−0.1=13.7 mm，必须保留对侧壁。' },
        { id: 'leaf-far-wall', key: 'verticalBranchReserve', valid: 8.89, baseline: 5, rejected: [8.9, 8.91],
          parameters: { ...OPEN, faceType, infillPattern: 'vertical', tubeSpecificationPreset: 'custom',
            doorLeafFrameWidth: 10, doorLeafFrameDepth: 20, doorLeafFrameCornerRadius: 1,
            doorVerticalWidth: 6, doorVerticalWallThickness: 0.5 },
          notes: '外框可容纳该入榫，但10 mm窗扇接收框的对壁限值为10−1−0.1=8.9 mm。' },
        { id: 'leaf-near-wall', key: 'verticalBranchReserve', valid: 3.11, baseline: 10, rejected: [3.09, 3.1],
          parameters: { ...OPEN, faceType, infillPattern: 'vertical', tubeSpecificationPreset: 'custom',
            doorLeafFrameWallThickness: 3, doorVerticalWidth: 6, doorVerticalWallThickness: 0.5 },
          notes: '窗扇接收框壁厚3 mm，须超过3＋0.1 mm，不能只用外框壁厚检查。' },
      ];
      for (const fixture of fixtures) {
        add(`receiver.${fixture.id}.${faceType}.${purpose}.valid`, fixture.key,
          { ...fixture.parameters, [fixture.key]: fixture.valid },
          { purpose, compareTo: { ...fixture.parameters, [fixture.key]: fixture.baseline }, effect: 'geometry', notes: fixture.notes });
        for (const value of fixture.rejected) {
          add(`receiver.${fixture.id}.${faceType}.${purpose}.reject-${value}`, fixture.key,
            { ...fixture.parameters, [fixture.key]: value },
            fixture.id === 'post-depth-axis'
              ? { purpose, kind: 'rejection', reasonPattern: '入榫.*管壁', notes: fixture.notes }
              : { purpose, compareTo: { ...fixture.parameters, [fixture.key]: fixture.baseline }, effect: 'geometry',
                componentPrefix: 'access_door.leaf.', componentEffect: 'none',
                notes: '主格栅入榫参数只改变主格栅；窗扇竖杆固定插入其自身接收框的截面中部，保留草稿不能改变窗扇。' });
        }
      }
      const leaf = { ...OPEN, faceType, infillPattern: 'vertical', tubeSpecificationPreset: 'custom',
        doorLeafFrameWidth: 25, doorLeafFrameDepth: 25, doorVerticalWidth: 6, doorVerticalWallThickness: 0.5 };
      for (const wall of [9.4, 9.41]) add(`receiver.leaf-inner-clearance.${faceType}.reject-${wall}`,
        'doorLeafFrameWallThickness', { ...leaf, doorLeafFrameWallThickness: wall },
        { purpose, kind: 'rejection', reasonPattern: '无法安全穿管|入榫.*管壁',
          notes: '窗扇25 mm接收框内腔必须严格大于Φ6+双侧0.1 mm间隙；壁厚9.4 mm等值相碰仍拒绝。' });
      add(`receiver.leaf-inner-clearance.${faceType}.valid-9.39`, 'doorLeafFrameWallThickness',
        { ...leaf, doorLeafFrameWallThickness: 9.39 },
        { purpose, compareTo: { ...leaf, doorLeafFrameWallThickness: 9.3 }, effect: 'geometry',
          nativeDisassembly: true, nativeSolidEffect: 'geometry',
          notes: '略薄于接收内腔相碰临界值时允许拆单；验证真正提交的原生实体有效且壁厚变化实际生效。' });
    }
    for (const [infillPattern, key] of [['vertical', 'horizontalWallThickness'], ['horizontal', 'verticalWallThickness']]) {
      add(`caps.require-both-profiles.${infillPattern}.${purpose}`, key,
        { ...SINGLE, faceType: 'five', infillPattern, [key]: 50 },
        { purpose, kind: 'rejection', reasonPattern: '壁厚|内腔|杆件|尺寸必须为大于零',
          notes: '五面窗顶底网格始终使用两个方向；立面未使用的管材在顶底仍适用，必须保留截面校验。' });
    }
  }

  for (const key of ['horizontalEndConnection', 'verticalEndConnection']) {
    for (const value of choices(key)) add(`${key}.${value}`, key, { ...SINGLE, [key]: value },
      { compareTo: SINGLE, effect: value === 'insert' ? 'none' : 'geometry',
        notes: '对实际制造实体检查插入、平切贴焊、端部公母插接。' });
    const inactiveBase = { ...SINGLE, infillPattern: key.startsWith('horizontal') ? 'vertical' : 'horizontal' };
    inactive(`${key}.unused-direction`, key, inactiveBase, { [key]: 'tabs' });
    inactive(`${key}.external-draft`, key, external, { [key]: 'tabs' });
  }
  const tabs = { ...SINGLE, foldedPostJoint: 'tabs' };
  for (const value of choices('foldedPostJoint')) add(`foldedPostJoint.${value}`, 'foldedPostJoint',
    { ...SINGLE, foldedPostJoint: value }, { compareTo: SINGLE, effect: value === 'weld' ? 'none' : 'geometry' });
  for (const [key, value] of [['foldedPostTabWidth', 7], ['foldedPostTabLength', 10], ['foldedPostSideClearance', .35]]) {
    add(`${key}.changed`, key, { ...tabs, [key]: value }, { compareTo: tabs, effect: 'geometry' });
    inactive(`${key}.weld-draft`, key, SINGLE, { [key]: 100 });
  }
  for (const [prefix, active] of [
    ['outerFrame', { ...SINGLE, frameManufacturingMode: 'plane_v_notch' }],
    ['doorFrame', { ...OPEN, doorFrameJoinType: 'v_groove_90:tool_library' }],
    ['doorLeafFrame', { ...OPEN, doorLeafFrameJoinType: 'v_groove_90:tool_library' }],
  ]) {
    const toolKey = `${prefix}GrooveTool`;
    const arc = { ...active, [toolKey]: 'system:edge-arc-groove' };
    const rounded = { ...active, [`${prefix}VGrooveBottomStrategy`]: 'rounded' };
    const factorKey = `${prefix}BendKFactor`;
    for (const value of [0, .35, 1]) add(`${factorKey}.arc-${value}`, factorKey, { ...arc, [factorKey]: value },
      { compareTo: arc, effect: value === 0 ? 'none' : 'geometry', notes: '圆弧边槽使用该框的独立 K 补偿。' });
    inactive(`${factorKey}.sharp-ignores-compensation`, factorKey, active, { [factorKey]: .7 },
      'V槽圆角属于槽根避空；不能套用成形半径或K展开。');
    for (const value of [-.01, 1.01]) rejection(`${factorKey}.invalid-${value}`, factorKey,
      { ...arc, [factorKey]: value }, '启用圆弧边槽时检查K有效范围。', '折弯系数.*0.*1');
    const bridgeKey = `${prefix}FoldBridge`;
    for (const value of [.1, .5]) add(`${bridgeKey}.${value}`, bridgeKey, { ...active, [bridgeKey]: value },
      { compareTo: active, effect: 'geometry', notes: '各框的槽根留底厚度独立影响切口。' });
    rejection(`${bridgeKey}.below-domain`, bridgeKey, { ...active, [bridgeKey]: .09 },
      '留底厚度不能低于当前工艺下限。', '留底厚度.*0.1.*10');
    const strategyKey = `${prefix}VGrooveBottomStrategy`;
    for (const value of choices(strategyKey)) add(`${strategyKey}.${value}`, strategyKey,
      { ...active, [strategyKey]: value }, { compareTo: active, effect: value === 'sharp' ? 'none' : 'geometry' });
    const radiusKey = `${prefix}VGrooveRoundRadius`;
    for (const value of [0, 1.5, 3]) add(`${radiusKey}.${value}`, radiusKey, { ...rounded, [radiusKey]: value },
      { ...(value ? { compareTo: rounded, effect: 'geometry' } : {}), notes: '槽根圆弧半径只在圆角V槽分支读取。' });
    rejection(`${radiusKey}.negative-active`, radiusKey, { ...rounded, [radiusKey]: -1 },
      '圆角V槽半径不能为负。', '槽根圆弧半径.*不能小于');
    inactive(`${radiusKey}.sharp-draft`, radiusKey, active, { [radiusKey]: -1 });
    inactive(`${radiusKey}.arc-draft`, radiusKey, arc, { [radiusKey]: -1 });
    const maleKey = `${prefix}VGrooveMaleFemale`;
    for (const value of [false, true]) add(`${maleKey}.${value}`, maleKey, { ...active, [maleKey]: value },
      { compareTo: active, effect: value ? 'none' : 'geometry' });
    inactive(`${maleKey}.arc-draft`, maleKey, arc, { [maleKey]: false });
    const unused = prefix === 'outerFrame' ? SINGLE : { ...SINGLE, accessDoorEnabled: false };
    for (const [key, value] of [[factorKey, -1], [bridgeKey, -1], [strategyKey, 'rounded'], [radiusKey, -1], [maleKey, false]])
      inactive(`${key}.unselected-frame-draft`, key, unused, { [key]: value });
  }
  // A product's display is stable across valid manufacturing decisions.
  // Actual manufacture is verified above against the native plan and solids.
  const manufacturingOnly = new Set(descriptor.extensions.parameterDependencies.manufacturingOnly);
  for (const test of [...cases].filter(test => test.kind === 'valid' && test.compareTo != null && test.effect === 'geometry')) {
    const changed = [...new Set([...Object.keys(test.parameters), ...Object.keys(test.compareTo)])]
      .filter(key => !Object.is(test.parameters[key] ?? fields.get(key).defaultValue,
        test.compareTo[key] ?? fields.get(key).defaultValue));
    if (changed.every(key => manufacturingOnly.has(key))) {
      const { nativeDisassembly, nativeSolidEffect, expectedProcessParameters, nativeHorizontalEndFit, nativeMiterContact, ...display } = test;
      cases.push({ ...display, id: `${test.id}.display-independent`,
        purpose: 'display', effect: 'none', notes: '制造参数不改变成品设计构件。' });
    }
  }

  const required = descriptor.parameters.filter(field =>
    ['outer_process', 'door_process', 'infill_process', 'outerFrame_fold_process', 'doorFrame_fold_process',
      'doorLeafFrame_fold_process', 'folded_post_process', 'groove_library', 'assembly'].includes(field.group)
    || ['assemblyPlanningMode', 'productCode'].includes(field.key));
  const covered = new Set(cases.flatMap(test => test.keys));
  for (const test of cases) {
    if (test.kind === 'rejection' && !test.reasonPattern) {
      throw new Error(`Missing domain rejection reason for ${test.id}`);
    }
  }
  for (const field of required) {
    if (!covered.has(field.key)) throw new Error(`Missing process case for ${field.key}`);
  }
  return cases;
}
