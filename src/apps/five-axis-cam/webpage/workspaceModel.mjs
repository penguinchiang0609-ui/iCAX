// UI drafts only. Applied engineering state belongs to scene SDOs.
import { processDraftDefaults } from './processPolicyDraft.mjs';
import { buildCutTree } from './cutTreeModel.mjs';
export const AREAS = Object.freeze([
  { id: 'workpiece', title: '工件', hint: '工件 → 装夹 → 夹具单元' },
  { id: 'toolpath', title: 'CAM', hint: '刀路提取 → 工艺 → 生成 → 轨迹仿真与输出' },
  { id: 'simulation', title: '孪生', hint: '真实机型与装夹 · 数字孪生仿真' },
  { id: 'help', title: '帮助', hint: '操作说明、日志与诊断' },
]);
export const normalizeArea = id => AREAS.some(area => area.id === id) ? id : 'workpiece';
export const DEFAULT_TOOLS = Object.freeze({ workpiece: 'select', toolpath: 'select', simulation: 'machine-instances', help: 'manual' });
export const preparationKind = view => view.fivePreparationKind || 'workpiece';
export const geometryEditing = view => view.fiveAxisArea==='workpiece'&&preparationKind(view)==='workpiece';
export const geometryTools = Object.freeze(['scale','align']);
export const TOOLS = Object.freeze(Object.fromEntries([
  ['process', 'toolpath', '切割参数', '所选刀路', '设置当前刀路的切割工艺参数。', '保存后重新生成受影响的刀路。'],
  ['order', 'toolpath', '加工顺序', '当前位姿的刀路', '查看并调整加工顺序。', '只有明确执行排序操作才修改顺序。'],
  ['select', 'workpiece', '选择', '当前工件', '选择场景中的对象。', '退出平移、旋转与缩放。'],
  ['translate', 'workpiece', '平移', '当前工件', '按编程坐标系平移工件。', '确认后保存位置，并使后续加工结果过期。'],
  ['rotate', 'workpiece', '旋转', '当前工件', '绕指定中心和坐标轴旋转工件。', '确认后保存姿态，并使后续加工结果过期。'],
  ['scale', 'workpiece', '缩放', '当前工件', '拖动任意轴端方块或中心方块，等比缩放工件。', 'XYZ 使用同一比例缩放精确几何；确认后需重新提取刀路。'],
  ['align', 'workpiece', '自身坐标系重置', '当前工件', '选择基准面、Z 正方向、X 正方向和原点。', '确认后以对齐结果重设工件自身坐标系，位置和旋转归零；取消恢复原状态。'],
  ['rough', 'workpiece', '工件坐标系', '当前工件', '利用模型上的点、直线或面建立编程坐标系。', '确定原点、OX 方向和 OXY 平面；改变坐标后需重新生成与验证。'],
  ['fine', 'workpiece', '实测校准', '模型点与实测点', '把现场测得的坐标与模型特征点逐一配对，校准工件摆放。', '检查点组与残差后才能应用；已有有效实测校准时，不在现场重复应用同一变换。'],
  ['setup', 'workpiece', '装夹管理', '当前工件的装夹', '同一几何可建立多个装夹，各自保存姿态、工装和刀路。', '新增装夹从当前姿态开始，不复制刀路或工装。'],
  ['fixture', 'workpiece', '工装管理', '当前装夹', '导入工装、设计夹具、调整位置并检查静态间隙。', '隐藏工装仍参与检查；位置变更后重新检查。'],
  ['extract', 'toolpath', '添加刀路', '加工面与边界', '选面识别、选边成环或选择加工区域，预览后添加刀路。', '确认候选后产生切割定义，尚未产生机床运动。'],
  ['manual-extract', 'toolpath', '手动选面', '加工面与边界', '补充自动识别遗漏的加工面，也可使用已有拓扑选择。', '确认所选边界后建立切割定义。'],
  ['split', 'toolpath', '打断刀路', '刀路', '在指定位置分段，便于分段加工。', '保留来源关系；标准孔在确认打断时自动转为轮廓。'],
  ['path-translate', 'toolpath', '平移', '所选刀路', '按工件坐标系修正刀路位置。', '只修改刀路，不修改 CAD。'],
  ['path-rotate', 'toolpath', '旋转', '所选刀路', '绕指定中心与轴旋转刀路。', '刀路和刀轴一起旋转，不修改 CAD。'],
  ['generate', 'toolpath', '生成空移', '当前工件的工序连接', '自动准备切割路径，按规则快速生成空移，随后统一检测碰撞；也可选择单段自动生成安全空移。', '路径生成完成后仍需碰撞检测。'],
  ['keypoint', 'toolpath', '关键点编辑', '刀路上的局部位置', '在发生干涉或超行程的位置修正切割头姿态。', '人工点可增删，系统起终点保留；相邻点按选定规则过渡。'],
  ['rapid', 'toolpath', '空移路径编辑', '两道工序之间的空移段', '调整上一刀结束到下一刀开始之间的连接运动。', '只修改选中连接；切割头、工件和夹具间隙仍须通过验证。'],
  ['path-hole', 'toolpath', '孔加工方式', '刀路', '高级选项：切换标准孔或轮廓加工。', '保存刀路类型。'],
  ['adjust', 'toolpath', '刀路调整', '整条刀路', '调整整条路径的目标方向与允许调姿范围。', '目标角与允许误差分开；局部硬锁定的关键点仍需保留。'],
  ['manual-order', 'toolpath', '手动排序', '工序序列', '按加工需要固定顺序，或保留前缀后继续排序。', '前移/后移直接调整选中刀路顺序，可用统一的撤销和重做恢复。'],
  ['auto-order', 'toolpath', '自动排序', '工序序列', '在外轮廓后切等工艺约束下减少空移。', '重新排序全部刀路，并更新空移连接草稿。'],
  ['path-start', 'toolpath', '起点', '普通轮廓切割定义', '指定轮廓的切入位置，生成后查看对应的起刀姿态。', '修改切割定义起点后重新生成，保持原有加工方向。'],
  ['reverse', 'toolpath', '反向', '刀路关联的普通轮廓', '反转切割行进方向。', '同步更新起点、引线和过切关系，保留材料侧与成品尺寸。'],
  ['leads', 'toolpath', '引线', '所选切割定义的刀路方案', '从废料区域引入，避免直接在成品边界穿孔起刀。', '点击引线进入连续设置，依次点击切割刀路应用；下拉菜单修改引入、引出参数。'],
  ['side', 'toolpath', '阴阳切', '所选轮廓的材料侧', '确认成品与废料位于哪一侧，使引线进入废料侧。', '同时影响引线和补偿方向；反向不自动交换材料侧。'],
  ['overcut', 'toolpath', '过切', '闭合轮廓', '越过起点后沿原轮廓继续切一小段，帮助切透闭合处。', '不是沿末端切线延伸；开放轮廓不套用闭合过切。'],
  ['compensation', 'toolpath', '补偿', '所选切割定义的刀路方案', '按切缝宽度与留料侧补偿成品尺寸。', '明确补偿方向和拐角方式后重新生成。'],
  ['clear', 'toolpath', '清除', '所选对象的工艺覆盖', '按类别清除引线、补偿或过切，保留加工意图。', '保留起点、反向、阴阳切、孔类别、关键点和人工顺序；恢复继承是独立操作。'],
  ['simulation', 'toolpath', '轨迹仿真', '工件、切割头与加工路径', '绑定机床后按实际轴运动播放，工件与夹具随装夹部件运动。', '绑定机床使用经过行程与整机碰撞检查的运动计划；未绑定时预览工艺轨迹。'],
  ['collision', 'toolpath', '碰撞检测', '当前切割头、工件与夹具', '独立检查全部切割和空移运动。', '检测结果对应当前模型、路径和切割头。'],
  ['machine-simulation', 'simulation', '作业', '真实机型与当前装夹', '按实际轴运动驱动完整机床、切割头、工件和夹具。', '实机模型、运动链、行程和标定齐全后才能运行；未配置不生成仿真通过结论。'],
  ['machine-definitions', 'simulation', '机床定义', '机床资源库', '导入、启用、禁用和删除机床定义。', '定义用于创建实例，实例保存各自的参数。'],
  ['machine-instances', 'simulation', '机床实例', '机械结构与控制', '调整实例行程、点动轴和配置后置。', '后置随实例保存，作业直接继承。'],
  ['registration', 'workpiece', '三点定位', '供现场测量的模型特征点', '选择三个现场容易找到的点，随加工交付供执行端配对实测坐标。', '这是待测量的定位计划；选完模型点不代表已完成定位。现场用对应实测点求机床变换；这里不输入实测坐标。'],
  ['export', 'toolpath', '导出', '已验证的加工计划', '检查设备与定位资格，生成程序并重放实际输出文件。', '完整计划、有效报告与文件重放均通过才可交付。'],
  ['settings', 'toolpath', '空移策略', '机床、切割头与规划默认值', '配置设备和规划基础参数，供工程引用。', '局部修正留在对应刀路操作中；设备或配置改变后需重新验证。'],
  ['manual', 'help', '操作说明', '编程流程', '按对象理解各菜单的作用。', '参数目前仅为会话草稿，应用和计算能力待接入。'],
  ['diagnostics', 'help', '导出诊断包', '当前工程的诊断信息', '收集问题上下文以便排查。', '诊断导出能力待接入。'],
  ['logs', 'help', '日志', '运行记录', '查看导入、生成与验证过程中的操作记录。', '普通操作记录保留在公共日志区，问题修正从对应操作进入。'],
  ['about', 'help', '关于', '三维五轴 CAM', '基于 iCAX 框架的三维五轴加工工作台。', '当前为 UI 预览，尚未完成设备加工验收。'],
].map(([id, area, title, target, purpose, effect]) => [id, { area, title, target, purpose, effect }])));
export function workspaceState(view) {
  const ui=view.fiveAxisUI ??= { search: '', dock: 'sequence', selectedIntentId: '', drafts: {}, tools: { ...DEFAULT_TOOLS } };
  if(ui.tools.simulation==='machine-definitions')ui.tools.simulation='machine-instances';
  if(view.scene?.domain&&ui.selectionSnapshot!==view.scene.domain){
    ui.selectionSnapshot=view.scene.domain;
    const cuts=view.scene.intentToolpaths||[],available=new Set(cuts.filter(c=>!c.locked).map(c=>c.id));
    ui.selectedIntentIds=(ui.selectedIntentIds||[ui.selectedIntentId]).filter(id=>available.has(id));
    if(!available.has(ui.selectedIntentId))ui.selectedIntentId=ui.selectedIntentIds[0]||'';
    const tree=buildCutTree(view);
    ui.selectedGroupIds=(ui.selectedGroupIds||[]).filter(id=>{const n=tree.nodes.get(`g:${id}`);return n&&!n.containsLocked&&n.leafIds.every(id=>ui.selectedIntentIds.includes(id));});
    ui.selectedGroupId=ui.selectedGroupIds.length===1?ui.selectedGroupIds[0]:'';
  }
  return ui;
}
export function activeTool(view, area = view.fiveAxisArea) { return workspaceState(view).tools[normalizeArea(area)]; }
export function switchCamPage(view,page) {
  if((view.fiveCamPage||'cutting')===page)return;
  const wasRapid=view.fiveCamPage==='rapid';
  const ui=workspaceState(view),key=view.scene?.domain?.activeSetupId||'';
  const memory=view.fiveCamSelections??=new Map();
  if(page==='rapid'){
    memory.set(key,Object.fromEntries(['selectedIntentIds','selectedIntentId','selectedGroupIds','selectedGroupId','cutSelectionAnchor'].map(k=>[k,structuredClone(ui[k])])));
    Object.assign(ui,{selectedIntentIds:[],selectedIntentId:'',selectedGroupIds:[],selectedGroupId:'',cutSelectionAnchor:''});
  }else if(wasRapid){
    if(view.fiveRapid?.id)view.fiveRapidSelectedId=view.fiveRapid.id;
    if(view.fiveRapid)view.fiveRapid.id='';
    const saved=memory.get(key);
    if(saved)Object.assign(ui,structuredClone(saved));
    ui.selectionSnapshot=null;workspaceState(view);
  }
  view.fivePreferredCutSelection=[...(ui.selectedIntentIds||[])];
  view.fiveCamPage=page;
}
export function selectTool(view, id) {
  const item = TOOLS[id];
  if (!item) return false;
  const state = workspaceState(view); state.tools[item.area] = id;
  if(item.area==='workpiece')view.fivePreparationKind=id==='fixture'?'unit':['translate','rotate','fine','setup','registration'].includes(id)?'setup':'workpiece';
  if (['machine-simulation', 'export'].includes(id)) state.dock = 'issues';
  return true;
}
export function currentDraft(view, id = activeTool(view)) {
  const state = workspaceState(view);
  const scope = ['rough', 'fine'].includes(id) ? `workpiece:${view.scene?.model?.entityId || 'none'}:${id}`
    : id === 'fixture' ? `workpiece:${view.scene?.model?.entityId || 'none'}:${id}`
    : ['extract', 'manual-extract'].includes(id) ? `operation:extraction:${view.scene?.model?.entityId || 'none'}`
    : id === 'projection' ? `projection:${view.scene?.model?.entityId || 'none'}:${state.selectedIntentId || 'new'}`
    : ['registration', 'settings', 'export', 'manual-order', 'auto-order', 'rapid'].includes(id) ? `workpiece:${view.scene?.model?.entityId || 'none'}:operation:${id}`
    : `definition:${state.selectedIntentId || 'none'}`;
  return state.drafts[scope] ??= {
    ...processDraftDefaults,
    roughMethod: 'point', capture: 'point', extractMethod: 'manual',
    extractionRepresentation: 'Solid', extractionThickness: '', extractionSide: '1', extractionKind: 'all',
    ...Object.fromEntries(Array.from({ length: 10 }, (_, i) => i+1).flatMap(n => ['X', 'Y', 'Z'].flatMap(axis => [[`measured${n}${axis}`, ''], [`model${n}${axis}`, '']]))),
    ...Object.fromEntries(['origin', 'axisX', 'plane'].flatMap(prefix => ['X', 'Y', 'Z'].map(axis => [`${prefix}${axis}`, '']))),
    ...Object.fromEntries(Array.from({ length: 10 }, (_, i) => [`weight${i+1}`, '1'])),
    pointCount: '3', registrationTolerance: '0.1', weightMode: 'equal', weightSource: '', registrationPreview: null, referenceQueries: {}, snapDistance: '2',
    kerf: '', overcut: '0', compensationMode: 'auto', corner: 'round', side: 'auto',
    poseMode: 'normal', forward: '0', lateral: '0', forwardTolerance: '0', lateralTolerance: '0', totalTolerance: '0', normalPreference: '0.5',
    keypointConstraint: 'direction', keypointForward: '0', keypointLateral: '0',
    transition: 'length', connection: 'automatic', clearance: '', fixtureClearance: '',
    keypointLocation: '', purpose: 'production', note: '',
    retainedSide:'Exterior',
    followHeight: '', collisionThreshold: '', holeTolerance: '', chordError: '', pointError: '', vectorError: '',
    plane: 'xy', planeOffset: '0', sketchShape: 'circle', sketchDiameter: '10', sketchWidth: '20', sketchHeight: '10', pitchX: '40', pitchY: '40', countX: '1', countY: '1', projectionDirection: 'plane-normal', designAxis: 'projection',
  };
}

export function definitionStatus(view, item) {
  const draft = workspaceState(view).drafts[`definition:${item.entityId ?? item.id}`];
  // Local drafts are not committed geometry and cannot certify a generated plan.
  return draft?.edited ? '有参数草稿 · 刀路尚未生成' : '切割定义 · 刀路尚未生成';
}
export function sceneItems(view) {
  const scene = view.scene ?? {};
  const flatten = nodes => (Array.isArray(nodes) ? nodes : []).flatMap(node => [node, ...flatten(node.children)]);
  return { machines: scene.machines ?? [], intents: flatten(scene.intentToolpaths) };
}
export function outputChecks(view) {
  const { machines } = sceneItems(view);
  return [
    { title: '工件几何', detail: view.scene?.model?.isLoaded ? '已导入工件，仍需加工检查' : '尚未导入工件', tool: 'select' },
    { title: '设备配置', detail: machines.length ? '已载入机床，工具与标定待确认' : '选择机床与切割头', tool: 'settings' },
    { title: '定位资料', detail: view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId)?.registrationPlan?.status==='Prepared' ? '三点资料已准备，现场尚未测量' : '请在工件树的装夹中准备三点资料', tool: 'registration' },
    { title: '完整加工计划', detail: '尚无五轴完整计划；生成能力待接入', tool: 'generate' },
    { title: '整机仿真与验证', detail: '尚无实机配置及当前计划的有效报告', tool: 'machine-simulation' },
    { title: '实际输出重放', detail: '程序生成与重放能力待接入', tool: 'export' },
  ];
}
