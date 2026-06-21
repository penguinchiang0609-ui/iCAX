import { flatLaserCamProductManifest } from './productManifest.mjs';
import { FlatLaserCommands } from './flatLaserProtocol.mjs';

const stages = [
  { id: 'drawing', label: '图纸' },
  { id: 'nesting', label: '排样' },
  { id: 'technology', label: '工艺' },
  { id: 'toolpath', label: '刀路' },
  { id: 'simulation', label: '仿真' },
  { id: 'output', label: '输出' },
];

const operations = [
  [FlatLaserCommands.importDrawing, '导入图纸'],
  [FlatLaserCommands.createSheet, '创建板材'],
  [FlatLaserCommands.generateNesting, '自动排样'],
  [FlatLaserCommands.generateToolpath, '生成刀路'],
  [FlatLaserCommands.runSimulation, '运行仿真'],
  [FlatLaserCommands.generateNc, '导出 NC'],
];

let styleInstalled = false;

export function createProductWorkspace(context) {
  return createFlatLaserCamFrontend({ ...context, mode: 'product' });
}

export function createProjectWorkspace(context) {
  return createFlatLaserCamFrontend({ ...context, mode: 'project' });
}

export function createFlatLaserCamFrontend(context) {
  const mount = context?.mount ?? null;
  const mode = context?.mode ?? 'product';
  const projectClient = context?.projectClient ?? null;
  let state = createEmptyProjectState(context?.project);
  let selectedPartId = '';
  let busyCommand = '';
  let errorMessage = '';

  async function refresh() {
    if (!projectClient) {
      return;
    }
    const response = await projectClient.execute(FlatLaserCommands.getState);
    applyResponse(response);
  }

  async function execute(command) {
    if (!projectClient) {
      errorMessage = 'Project mailbox is not available.';
      render();
      return;
    }

    busyCommand = command;
    errorMessage = '';
    render();

    try {
      const response = await projectClient.execute(command, payloadFor(command));
      applyResponse(response);
    } catch (error) {
      errorMessage = error?.message ?? String(error);
    } finally {
      busyCommand = '';
      render();
    }
  }

  function applyResponse(response) {
    if (response?.state) {
      state = response.state;
      selectedPartId = state.selectedPartId || state.parts?.[0]?.id || selectedPartId;
    }
  }

  function render() {
    if (!mount) {
      return;
    }
    if (mode === 'project') {
      renderProjectWorkspace(mount, state, {
        selectedPartId,
        busyCommand,
        errorMessage,
        onCommand: execute,
        onStage(stageId) {
          state = { ...state, activeStage: stageId };
          render();
        },
        onPart(partId) {
          selectedPartId = partId;
          render();
        },
      });
      return;
    }
    renderProductDashboard(mount);
  }

  return {
    productId: flatLaserCamProductManifest.productId,
    productName: flatLaserCamProductManifest.productName,

    async start() {
      installStyle();
      if (mode === 'project') {
        await refresh().catch((error) => {
          errorMessage = error?.message ?? String(error);
        });
      }
      render();
    },
  };
}

function payloadFor(command) {
  if (command === FlatLaserCommands.importDrawing) {
    return { fileName: 'demo-nesting.dxf' };
  }
  if (command === FlatLaserCommands.createSheet) {
    return {
      name: 'Sheet 1500 x 3000',
      material: 'Q235',
      thickness: 2.0,
      width: 3000,
      height: 1500,
    };
  }
  return {};
}

function renderProductDashboard(mount) {
  mount.innerHTML = `
    <section class="flc-product-dashboard">
      <div class="flc-dashboard-main">
        <div>
          <div class="flc-kicker">Flat Laser CAM</div>
          <h2>平面激光 CAM 工作台</h2>
          <p>产品前端入口已按 <code>frontendEntry</code> 动态加载。打开 <code>.ilcam</code> 工程后进入项目工作台。</p>
        </div>
        <div class="flc-dashboard-stats">
          <div><span>Mailbox</span><label>命令请求</label></div>
          <div><span>Project</span><label>状态归属</label></div>
          <div><span>.ilcam</span><label>工程格式</label></div>
        </div>
      </div>
      <div class="flc-pipeline">
        ${stages.map((stage, index) => `
          <button type="button" class="flc-pipeline-step ${index === 0 ? 'active' : ''}">
            <span>${String(index + 1).padStart(2, '0')}</span>
            ${stage.label}
          </button>
        `).join('')}
      </div>
    </section>
  `;
}

function renderProjectWorkspace(mount, state, handlers) {
  const selectedPartId = handlers.selectedPartId || state.selectedPartId || state.parts?.[0]?.id || '';
  const selectedPart = state.parts?.find((part) => part.id === selectedPartId) ?? null;
  const sheet = state.sheets?.[0] ?? createEmptyProjectState().sheets[0];
  const activeStage = state.activeStage ?? 'drawing';
  const operation = state.operation ?? { message: 'Ready', progress: 0, state: 'Idle' };

  mount.innerHTML = `
    <section class="flc-cam-workspace">
      <header class="flc-toolbar">
        <div class="flc-toolbar-group">
          ${operations.map(([command, label]) => `
            <button type="button"
                    class="flc-command"
                    data-flc-command="${command}"
                    ${handlers.busyCommand ? 'disabled' : ''}>
              ${handlers.busyCommand === command ? '执行中...' : label}
            </button>
          `).join('')}
        </div>
        <div class="flc-run-state">
          <span>${escapeText(operation.message ?? operation.state)}</span>
          <div class="flc-progress"><i style="width:${Number(operation.progress ?? 0)}%"></i></div>
        </div>
      </header>

      <div class="flc-stage-tabs">
        ${stages.map((stage) => `
          <button type="button"
                  class="${stage.id === activeStage ? 'active' : ''}"
                  data-flc-stage="${stage.id}">
            ${stage.label}
          </button>
        `).join('')}
      </div>

      ${handlers.errorMessage ? `<div class="flc-error">${escapeText(handlers.errorMessage)}</div>` : ''}

      <div class="flc-body">
        <aside class="flc-tree">
          <div class="flc-panel-title">工程</div>
          <button class="flc-tree-item active" type="button">${escapeText(sheet.name)}</button>
          <button class="flc-tree-item" type="button">${escapeText(sheet.material)} / ${sheet.thickness}mm</button>
          <div class="flc-panel-title flc-gap">零件</div>
          ${(state.parts ?? []).length === 0 ? `
            <div class="flc-empty">先点击“导入图纸”。</div>
          ` : state.parts.map((part) => `
            <button class="flc-tree-item ${part.id === selectedPartId ? 'active' : ''}"
                    type="button"
                    data-flc-part="${escapeText(part.id)}">
              <span>${escapeText(part.name)}</span>
              <small>${part.placed}/${part.qty}</small>
            </button>
          `).join('')}
        </aside>

        <main class="flc-canvas-zone">
          ${renderCanvas(state, selectedPartId)}
        </main>

        <aside class="flc-property">
          ${renderPropertyPanel(state, selectedPart)}
        </aside>
      </div>

      <footer class="flc-bottom">
        <div class="flc-panel-title">检查</div>
        <div class="flc-checks">
          ${(state.checks ?? []).map((check) => `
            <span class="${escapeText(check.level)}">${escapeText(check.label)}</span>
          `).join('')}
        </div>
      </footer>
    </section>
  `;

  mount.querySelectorAll('[data-flc-command]').forEach((button) => {
    button.addEventListener('click', () => handlers.onCommand(button.dataset.flcCommand));
  });
  mount.querySelectorAll('[data-flc-stage]').forEach((button) => {
    button.addEventListener('click', () => handlers.onStage(button.dataset.flcStage));
  });
  mount.querySelectorAll('[data-flc-part]').forEach((button) => {
    button.addEventListener('click', () => handlers.onPart(button.dataset.flcPart));
  });
}

function renderCanvas(state, selectedPartId) {
  const sheet = state.sheets?.[0] ?? createEmptyProjectState().sheets[0];
  const parts = state.parts ?? [];
  const hasParts = parts.length > 0;
  const showToolpath = state.toolpath?.visible || state.activeStage === 'toolpath' || state.activeStage === 'simulation' || state.activeStage === 'output';
  const showSimulation = state.simulation?.visible || state.activeStage === 'simulation';

  return `
    <div class="flc-canvas-header">
      <div>
        <strong>${escapeText(sheet.name)}</strong>
        <span>利用率 ${Number(sheet.utilization ?? 0).toFixed(1)}%</span>
      </div>
      <div>
        <span>${sheet.width} x ${sheet.height} mm</span>
        <span>${escapeText(state.projectName)}</span>
      </div>
    </div>
    <svg class="flc-sheet" viewBox="0 0 900 450" role="img" aria-label="Flat laser CAM sheet preview">
      <defs>
        <pattern id="flc-grid" width="30" height="30" patternUnits="userSpaceOnUse">
          <path d="M 30 0 L 0 0 0 30" fill="none" stroke="#dce4ea" stroke-width="1"/>
        </pattern>
        <marker id="flc-arrow" markerWidth="8" markerHeight="8" refX="6" refY="3" orient="auto">
          <path d="M0,0 L0,6 L7,3 z" fill="#d1495b"/>
        </marker>
      </defs>
      <rect x="24" y="22" width="852" height="404" rx="3" fill="#f8fbfd" stroke="#7f96a8" stroke-width="2"/>
      <rect x="24" y="22" width="852" height="404" fill="url(#flc-grid)" opacity="0.8"/>
      ${hasParts ? renderPlacedParts(selectedPartId) : `
        <text x="450" y="225" text-anchor="middle" fill="#6b7b8b" font-size="20">等待导入 DXF/DWG/SVG 图纸</text>
      `}
      ${showToolpath ? `
        <path d="M62 56 H756 V352 H82 V142 H734" fill="none" stroke="#d1495b" stroke-width="3" marker-end="url(#flc-arrow)" stroke-dasharray="8 7"/>
        <path d="M742 326 C790 312 818 266 796 226" fill="none" stroke="#f28f3b" stroke-width="2"/>
      ` : ''}
      ${showSimulation ? `
        <circle cx="742" cy="326" r="11" fill="#1f7a8c"/>
        <circle cx="742" cy="326" r="24" fill="none" stroke="#1f7a8c" stroke-width="2" opacity="0.35"/>
      ` : ''}
    </svg>
  `;
}

function renderPlacedParts(selectedPartId) {
  return `
    ${partRect(70, 70, 150, 78, 'cover-plate', selectedPartId)}
    ${partRect(244, 70, 150, 78, 'cover-plate', selectedPartId)}
    ${partRect(418, 70, 150, 78, 'cover-plate', selectedPartId)}
    ${partRect(592, 70, 150, 78, 'cover-plate', selectedPartId)}
    ${partBracket(88, 195, 'bracket-a', selectedPartId)}
    ${partBracket(276, 195, 'bracket-a', selectedPartId)}
    ${partBracket(464, 195, 'bracket-a', selectedPartId)}
    ${partBracket(652, 195, 'bracket-a', selectedPartId)}
    ${partCircle(110, 344, 'spacer', selectedPartId)}
    ${partCircle(188, 344, 'spacer', selectedPartId)}
    ${partCircle(266, 344, 'spacer', selectedPartId)}
    ${partCircle(344, 344, 'spacer', selectedPartId)}
    ${partCircle(422, 344, 'spacer', selectedPartId)}
    ${partCircle(500, 344, 'spacer', selectedPartId)}
    ${partCircle(578, 344, 'spacer', selectedPartId)}
    ${partCircle(656, 344, 'spacer', selectedPartId)}
  `;
}

function partRect(x, y, width, height, id, selected) {
  const active = id === selected ? ' flc-svg-active' : '';
  return `
    <g class="flc-svg-part${active}">
      <rect x="${x}" y="${y}" width="${width}" height="${height}" rx="8"/>
      <circle cx="${x + 30}" cy="${y + 26}" r="10"/>
      <circle cx="${x + width - 30}" cy="${y + height - 26}" r="10"/>
    </g>
  `;
}

function partBracket(x, y, id, selected) {
  const active = id === selected ? ' flc-svg-active' : '';
  return `
    <path class="flc-svg-part${active}"
          d="M${x} ${y} h126 v48 h-44 v48 h-82 z
             M${x + 22} ${y + 22} h38 v28 h-38 z"/>
  `;
}

function partCircle(cx, cy, id, selected) {
  const active = id === selected ? ' flc-svg-active' : '';
  return `
    <g class="flc-svg-part${active}">
      <circle cx="${cx}" cy="${cy}" r="28"/>
      <circle cx="${cx}" cy="${cy}" r="10"/>
    </g>
  `;
}

function renderPropertyPanel(state, part) {
  const stageLabel = stages.find((stage) => stage.id === state.activeStage)?.label ?? state.activeStage;
  const toolpath = state.toolpath ?? {};
  const simulation = state.simulation ?? {};
  const nc = state.nc ?? {};
  return `
    <div class="flc-panel-title">属性</div>
    <dl class="flc-props">
      <dt>阶段</dt><dd>${escapeText(stageLabel)}</dd>
      <dt>零件</dt><dd>${escapeText(part?.name ?? '未选择')}</dd>
      <dt>数量</dt><dd>${part ? `${part.placed}/${part.qty}` : '-'}</dd>
      <dt>工艺</dt><dd>${escapeText(part?.process ?? '-')}</dd>
      <dt>刀路</dt><dd>${toolpath.pathCount ?? 0} 段</dd>
      <dt>穿孔</dt><dd>${toolpath.pierceCount ?? 0}</dd>
      <dt>仿真帧</dt><dd>${simulation.frame ?? 0}</dd>
      <dt>NC</dt><dd>${nc.ready ? escapeText(nc.outputPath) : '未生成'}</dd>
    </dl>
    <div class="flc-log">
      ${(state.log ?? []).slice(0, 5).map((item) => `
        <div><span>${escapeText(item.time)}</span>${escapeText(item.message)}</div>
      `).join('')}
    </div>
  `;
}

function createEmptyProjectState(project = {}) {
  return {
    projectId: project?.projectId ?? '',
    projectName: project?.projectName ?? 'Laser Project',
    activeStage: 'drawing',
    selectedPartId: '',
    operation: { state: 'Idle', message: 'Ready', progress: 0 },
    sheets: [
      { id: 'sheet-1500', name: 'Sheet 1500 x 3000', material: 'Q235', thickness: 2.0, width: 3000, height: 1500, utilization: 0 },
    ],
    parts: [],
    checks: [{ level: 'ok', label: '等待导入图纸' }],
    toolpath: { visible: false, pathCount: 0, totalLength: 0, pierceCount: 0, estimatedSeconds: 0 },
    simulation: { visible: false, frame: 0, warnings: [] },
    nc: { ready: false, outputPath: '' },
    log: [],
  };
}

function installStyle() {
  if (styleInstalled || typeof document === 'undefined') {
    return;
  }

  const style = document.createElement('style');
  style.textContent = `
    .flc-product-dashboard,.flc-cam-workspace{color:#1f2a33;font-family:"Segoe UI",Arial,sans-serif}
    .flc-product-dashboard{border:1px solid #cfd9e2;background:#fff;border-radius:6px;padding:14px}
    .flc-dashboard-main{display:flex;justify-content:space-between;gap:18px;align-items:center;margin-bottom:12px}
    .flc-kicker{color:#476273;font-size:12px;font-weight:700;text-transform:uppercase}
    .flc-dashboard-main h2{margin:4px 0 0;font-size:20px}.flc-dashboard-main p{margin:6px 0 0;color:#657587}
    .flc-dashboard-stats{display:grid;grid-template-columns:repeat(3,minmax(96px,auto));gap:10px}
    .flc-dashboard-stats div{border-left:3px solid #2f6f7e;padding-left:10px}.flc-dashboard-stats span{display:block;font-weight:800}.flc-dashboard-stats label{color:#657587;font-size:12px}
    .flc-pipeline{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:8px}
    .flc-pipeline-step,.flc-stage-tabs button,.flc-command,.flc-tree-item{border:1px solid #c8d4de;background:#fff;border-radius:6px;color:#1f2a33}
    .flc-pipeline-step{min-height:42px;display:flex;align-items:center;gap:8px;padding:0 10px;font-weight:700}.flc-pipeline-step span{color:#657587;font-size:11px}
    .flc-pipeline-step.active,.flc-stage-tabs button.active,.flc-tree-item.active{border-color:#2f6f7e;background:#eaf6f7}
    .flc-cam-workspace{min-height:580px;display:grid;grid-template-rows:auto auto auto minmax(0,1fr) auto;gap:10px}
    .flc-toolbar,.flc-stage-tabs,.flc-bottom{display:flex;align-items:center;justify-content:space-between;gap:10px}.flc-toolbar-group,.flc-stage-tabs,.flc-checks{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
    .flc-command{min-height:32px;padding:0 10px;font-weight:700}.flc-command:hover{border-color:#2f6f7e}.flc-command:disabled{opacity:.65;cursor:default}
    .flc-run-state{min-width:250px;display:grid;grid-template-columns:auto 120px;gap:10px;align-items:center;color:#476273;font-size:12px}
    .flc-progress{height:8px;overflow:hidden;border-radius:999px;background:#dce4ea}.flc-progress i{display:block;height:100%;background:#2f6f7e}
    .flc-stage-tabs{justify-content:flex-start;border-bottom:1px solid #d9e1e8;padding-bottom:8px}.flc-stage-tabs button{min-width:64px;min-height:30px;padding:0 10px}
    .flc-error{border:1px solid #d8a1a1;background:#fff1f1;color:#a94747;border-radius:6px;padding:8px 10px}
    .flc-body{min-height:0;display:grid;grid-template-columns:210px minmax(460px,1fr)250px;gap:10px}
    .flc-tree,.flc-property,.flc-canvas-zone,.flc-bottom{border:1px solid #d1dbe4;border-radius:6px;background:#fff}.flc-tree,.flc-property{padding:10px;min-width:0}
    .flc-panel-title{color:#657587;font-size:12px;font-weight:800;letter-spacing:0;text-transform:uppercase}.flc-gap{margin-top:14px}
    .flc-tree-item{width:100%;min-height:34px;display:flex;align-items:center;justify-content:space-between;gap:8px;margin-top:7px;padding:0 9px;text-align:left}.flc-tree-item span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.flc-tree-item small{color:#657587}
    .flc-empty{margin-top:8px;color:#657587;font-size:13px}.flc-canvas-zone{min-width:0;min-height:0;padding:10px;background:#f7fafc}
    .flc-canvas-header{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:8px;color:#476273;font-size:12px}.flc-canvas-header div{display:flex;gap:12px;align-items:center}
    .flc-sheet{width:100%;min-height:410px;display:block;border:1px solid #c4d0da;background:#eef4f7}.flc-svg-part{fill:#dfe9ee;stroke:#547082;stroke-width:2}.flc-svg-active{fill:#b8e1e5;stroke:#1f7a8c;stroke-width:3}
    .flc-props{display:grid;grid-template-columns:64px minmax(0,1fr);gap:8px;margin:12px 0;font-size:13px}.flc-props dt{color:#657587}.flc-props dd{margin:0;overflow-wrap:anywhere}
    .flc-log{display:grid;gap:6px;font-size:12px;color:#476273}.flc-log div{border-top:1px solid #e3eaf0;padding-top:6px}.flc-log span{display:inline-block;min-width:70px;color:#657587}
    .flc-bottom{min-height:42px;padding:0 12px}.flc-checks span{font-size:12px;border:1px solid #d1dbe4;border-radius:999px;padding:4px 9px;background:#fff}.flc-checks .ok{color:#3f7650}.flc-checks .warn{color:#a66a25;border-color:#e2c28a;background:#fff8ec}
  `;
  document.head.appendChild(style);
  styleInstalled = true;
}

function escapeText(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');
}

export default createFlatLaserCamFrontend;
