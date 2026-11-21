import { createPlanarRibbonGroups } from "../../../iCAX-UI/SDK/Cad/planarRibbon.mjs";
const group = (title, commands) => ({ title, commands });

// AppShell owns rendering and dispatch; this module only describes the UI.
export function getRibbonDefinition(context = {}) {
  const ui = context.iplaneCamUI ?? {};
  const activeTool = ui.activeTool ?? 'plane.select';
  const command = (id, title, iconName, options = {}) => {
    const availability = ui.commandAvailability?.[id] ?? {};
    const disabled = availability.disabled ?? options.disabled ?? false;
    return {
      id, title, iconName, ...options,
      disabled,
      tooltip: availability.tooltip ?? options.tooltip ?? title,
      unavailableReason: disabled
        ? availability.unavailableReason ?? '当前版本暂不可用'
        : '',
    };
  };
  const tool = (id, title, iconName, options = {}) => command(
    id, title, iconName, { active: activeTool === id, ...options },
  );
  const business = (id, title, iconName, options = {}) => command(
    id, title, iconName, { disabled: true, ...options },
  );
  const viewCommands = () => group('视图', [
    command('view.fit', '适合窗口', 'view-fit'),
    command('view.reset-layout', '重置布局', 'display'),
  ]);

  const editingGroups = () => createPlanarRibbonGroups({ command, tool,
    ids: { draw: key => 'plane.draw-' + key, edit: key => 'plane.edit-' + key, segment: key => 'plane.polyline-kind-' + key, select: 'plane.select', layers: 'plane.layers', snap: 'plane.snap-toggle', ortho: 'plane.ortho-toggle', grid: 'plane.grid-toggle', fit: 'view.fit', resetLayout: 'view.reset-layout' },
    availablePrimitives: ui.geometryCapabilities?.primitives, availableEdits: ui.geometryCapabilities?.edits,
    snapEnabled: ui.snapEnabled, orthoEnabled: ui.orthoEnabled, gridVisible: ui.gridVisible,
    includeLayers: Boolean(ui.canManageLayers),
    polylineActive: activeTool === 'plane.draw-polyline', availableSegmentTypes: ui.geometryCapabilities?.segmentTypes, activeSegmentType: ui.polylineMode,
  });
  const sketchGroups = () => [...editingGroups(), group('草图', [command('plane.sketch-complete', '完成草图', 'apply'), command('plane.sketch-cancel', '取消草图', 'undo')])];
  return {
    tabs: [
      {
        id: 'drawing', title: '底图',
        groups: ui.activeSketchId ? sketchGroups() : [group('文件', [business('plane.import-dxf', '导入 DXF', 'import')]), ...editingGroups()],
      },
      {
        id: 'nesting', title: '下料',
        groups: ui.activeSketchId ? sketchGroups() : [
          group('零件', [
            business('plane.import-parts', '导入零件', 'import', { size: 'large' }),
            business('plane.copy-part', '复制零件', 'copy'),
            business('plane.delete-part', '删除零件', 'delete'),
            business('plane.sketch-open', '编辑草图', 'edit2d'),
          ]),
          group('板材', [
            business('plane.stock-rect', '矩形板材', 'sketch-rectangle', { size: 'large' }),
            business('plane.import-stocks', '导入板材', 'import'),
          ]),
          group('排样', [
            business('plane.nesting-run', '开始排样', 'autonest', { size: 'large' }),
            business('plane.nesting-settings', '排样参数', 'parameters'),
          ]),
          group('布局与报告', [
            tool('plane.select', '选择', 'select'),
            business('plane.layout-properties', '布局信息', 'nest'),
            business('plane.layout-report', '排样报告', 'report'),
            business('plane.layout-export', '导出布局', 'export'),
          ]),
          ...(ui.canManageLayers ? [group('图层', [business('plane.layers', '图层', 'group')])] : []),
          viewCommands(),
        ],
      },
    ],
  };
}
