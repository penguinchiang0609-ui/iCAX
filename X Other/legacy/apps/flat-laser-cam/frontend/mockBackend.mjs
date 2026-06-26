import { FlatLaserCommands } from './flatLaserProtocol.mjs';

export function createFlatLaserCamMockBackend() {
  return {
    createProjectState(context) {
      return createInitialFlatLaserProjectState(context);
    },

    handleProjectCommand(context) {
      return handleFlatLaserCommand(context);
    },
  };
}

function createInitialFlatLaserProjectState(context = {}) {
  return {
    projectId: context.project?.projectId ?? '',
    projectName: context.project?.projectName ?? 'Laser Project',
    activeStage: 'drawing',
    selectedPartId: '',
    operation: {
      state: 'Idle',
      message: 'Ready',
      progress: 0,
    },
    sheets: [
      {
        id: 'sheet-1500',
        name: 'Sheet 1500 x 3000',
        material: 'Q235',
        thickness: 2.0,
        width: 3000,
        height: 1500,
        utilization: 0,
      },
    ],
    parts: [],
    checks: [
      { level: 'ok', label: '等待导入图纸' },
    ],
    toolpath: {
      visible: false,
      pathCount: 0,
      totalLength: 0,
      pierceCount: 0,
      estimatedSeconds: 0,
    },
    simulation: {
      visible: false,
      frame: 0,
      warnings: [],
    },
    nc: {
      ready: false,
      outputPath: '',
    },
    log: [
      makeLog('工程已打开'),
    ],
  };
}

function handleFlatLaserCommand({ command, payload, state }) {
  switch (command) {
    case FlatLaserCommands.getState:
      return makeResponse(state);
    case FlatLaserCommands.importDrawing:
      importDrawing(state, payload);
      return makeResponse(state, ['drawing:demo']);
    case FlatLaserCommands.createSheet:
      createSheet(state, payload);
      return makeResponse(state, ['sheet-1500']);
    case FlatLaserCommands.generateNesting:
      generateNesting(state);
      return makeResponse(state, ['nesting:current']);
    case FlatLaserCommands.generateToolpath:
      generateToolpath(state);
      return makeResponse(state, ['toolpath:current']);
    case FlatLaserCommands.runSimulation:
      runSimulation(state);
      return makeResponse(state, ['simulation:latest']);
    case FlatLaserCommands.generateNc:
      generateNc(state);
      return makeResponse(state, ['nc:latest']);
    default:
      throw new Error(`No Flat Laser CAM mock command handler: ${command}`);
  }
}

function importDrawing(state, payload = {}) {
  state.activeStage = 'drawing';
  state.operation = {
    state: 'Done',
    message: `Imported ${payload.fileName ?? 'demo-nesting.dxf'}`,
    progress: 18,
  };
  state.parts = [
    { id: 'cover-plate', name: 'Cover Plate', qty: 12, placed: 0, process: 'Q235-2.0-O2' },
    { id: 'bracket-a', name: 'Bracket A', qty: 8, placed: 0, process: 'Q235-2.0-O2' },
    { id: 'spacer', name: 'Spacer', qty: 24, placed: 0, process: 'Mark + Cut' },
  ];
  state.selectedPartId = 'cover-plate';
  state.checks = [
    { level: 'ok', label: '闭合轮廓 44' },
    { level: 'ok', label: '重复线 0' },
    { level: 'warn', label: '未排零件 44' },
  ];
  state.log.unshift(makeLog('导入图纸并识别 3 类零件'));
}

function createSheet(state, payload = {}) {
  const sheet = state.sheets[0];
  sheet.name = payload.name ?? sheet.name;
  sheet.width = payload.width ?? sheet.width;
  sheet.height = payload.height ?? sheet.height;
  sheet.material = payload.material ?? sheet.material;
  sheet.thickness = payload.thickness ?? sheet.thickness;
  state.activeStage = 'nesting';
  state.operation = {
    state: 'Done',
    message: 'Sheet ready',
    progress: Math.max(state.operation.progress, 26),
  };
  state.log.unshift(makeLog('板材参数已确认'));
}

function generateNesting(state) {
  ensureParts(state);
  const placed = new Map([
    ['cover-plate', 12],
    ['bracket-a', 8],
    ['spacer', 20],
  ]);
  for (const part of state.parts) {
    part.placed = placed.get(part.id) ?? part.placed;
  }
  state.sheets[0].utilization = 78.4;
  state.activeStage = 'nesting';
  state.operation = {
    state: 'Done',
    message: 'Nesting generated',
    progress: 68,
  };
  state.checks = [
    { level: 'ok', label: '已排零件 40' },
    { level: 'warn', label: '未排零件 4' },
    { level: 'ok', label: '材料利用率 78.4%' },
  ];
  state.log.unshift(makeLog('自动排样完成'));
}

function generateToolpath(state) {
  ensureParts(state);
  state.activeStage = 'toolpath';
  state.operation = {
    state: 'Done',
    message: 'Toolpath generated',
    progress: 82,
  };
  state.toolpath = {
    visible: true,
    pathCount: 92,
    totalLength: 45280,
    pierceCount: 92,
    estimatedSeconds: 522,
  };
  state.checks = [
    { level: 'ok', label: '刀路 92 段' },
    { level: 'ok', label: '穿孔 92' },
    { level: 'ok', label: '预计 08:42' },
  ];
  state.log.unshift(makeLog('刀路已生成'));
}

function runSimulation(state) {
  state.activeStage = 'simulation';
  state.operation = {
    state: 'Running',
    message: 'Simulation running',
    progress: 92,
  };
  state.toolpath.visible = true;
  state.simulation = {
    visible: true,
    frame: 128,
    warnings: [
      { level: 'warn', label: '余料边缘距离 1 处偏小' },
    ],
  };
  state.checks = [
    { level: 'ok', label: '越界 0' },
    { level: 'warn', label: '边距告警 1' },
    { level: 'ok', label: '空移 12.4m' },
  ];
  state.log.unshift(makeLog('仿真运行到第 128 帧'));
}

function generateNc(state) {
  state.activeStage = 'output';
  state.operation = {
    state: 'Done',
    message: 'NC ready',
    progress: 100,
  };
  state.nc = {
    ready: true,
    outputPath: 'D:/Projects/laser/output/demo-sheet.nc',
  };
  state.log.unshift(makeLog('NC 文件已生成'));
}

function ensureParts(state) {
  if (state.parts.length === 0) {
    importDrawing(state);
  }
}

function makeResponse(state, changedEntityIds = []) {
  return {
    projectId: state.projectId,
    state: clone(state),
    changedEntityIds,
    warnings: state.checks.filter((item) => item.level === 'warn'),
  };
}

function makeLog(message) {
  return {
    time: new Date().toLocaleTimeString(),
    message,
  };
}

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

