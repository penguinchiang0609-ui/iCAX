"""Build the reviewed five-page CAM function catalogue as a Chinese PDF."""
from pathlib import Path
import json
import re
from xml.sax.saxutils import escape

from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Paragraph, Table, TableStyle
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.colors import HexColor, white
from reportlab.lib.pagesizes import A4, landscape
from pypdf import PdfReader

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'output/pdf'
OUT.mkdir(parents=True, exist_ok=True)
PDF = OUT / '三维五轴CAM-三级功能规格表.pdf'
QA = ROOT / 'output/tests/five-axis-workbench'
pdfmetrics.registerFont(TTFont('YaHei', 'C:/Windows/Fonts/msyh.ttc', subfontIndex=0))
pdfmetrics.registerFont(TTFont('YaHeiBold', 'C:/Windows/Fonts/msyhbd.ttc', subfontIndex=0))
pdfmetrics.registerFontFamily('YaHei', normal='YaHei', bold='YaHeiBold')
W, H = landscape(A4)
M = 36
CW = W - 2*M
INK = HexColor('#18323E')
TEAL = HexColor('#087F8C')
MUTED = HexColor('#5A7280')
LINE = HexColor('#D5E2E6')
LIGHT = HexColor('#F2F7F8')

# First and second levels match the approved pages and ribbon groups.
# Rows are third-level user functions, not implementation modules.
PAGES = [
 ('01 工件', '模型与坐标定位', '以已有三维模型开始加工；添加模型和建立坐标不要求先做 CAD 编辑。', [
  ('1.1 模型', [
   ('workpiece.import','导入模型','把外部零件加入当前加工工程。','STEP/STP、IGES/IGS 文件；核对单位、对象及来源。','生成工件实例与可追踪几何。导入失败保留原工程；显示成功不等于具备完整加工条件。','通道接入'),
   ('models','模型管理','查看当前工件，衔接模型准备与 CAM 编程。','当前工件、几何版本、工件数量；左树选择对象。','显示工程内对象和活动工件；可进入添加模型、坐标、装夹和 CAM。','界面可用'),
   ('model-library','内置模型 / 参数化模型','快速添加标准工件、底板或辅助构件。','内置资源或板件/长方体/圆柱模板；长宽高、厚度/直径及用途。','创建实际几何后按工件、夹具或障碍物添加；尺寸草稿不能充当已生成模型。','设计待接入'),
  ]),
  ('1.2 坐标定位', [
   ('rough','粗定位','确定模型的编程朝向和原点。','点、直线或面；依次确定原点、OX 方向及 OXY 平面。','生成正交编程坐标；退化选择拒绝。坐标改变后相关运动与验证需重算。','设计待接入'),
   ('fine','精定位','用现场测量校准实际工件摆放。','模型点与实测 XYZ 一一配对；检查点组和残差。','得到并应用刚体定位；残差超限不应用，不以缩放拟合；执行端不重复叠加。','设计待接入'),
  ]),
 ]),
 ('01 工件', '装夹与夹具制造', '制造链：形面支撑 → 夹具装配 → 平面板件 → 下料与编号拼焊资料。', [
  ('1.3 装夹与夹具制造', [
   ('fixture','装夹环境','确定工件、夹具、工作台和障碍物的空间关系。','导入/添加辅助构件；实例位置、用途与碰撞角色。','形成当前装夹环境。对象隐藏不等于停用碰撞；装夹变化使旧仿真结论失效。','设计待接入'),
   ('fixture-generate','生成夹具','按工件形面生成可拼焊的板式支撑与底板。','支撑/禁布区；板片方向、间距、板厚、底板高度、间隙及插槽/焊接方式。','预览并确认夹具装配，保留来源和连接关系；避让头部、光路与落料区，分别核对支撑和夹紧。','设计待接入'),
   ('fixture-parts','夹具拆单','把确认的夹具装配拆为可制造的平面板件。','已确认装配版本；板件编号前缀、板材和板厚。','给出真实板件轮廓、孔槽、编号、数量及装配位置；非平面或无效几何需先处理。','设计待接入'),
   ('fixture-export','夹具导出','将板件交给平面开料，再按编号拼装焊接。','已核对板件与装配关系；DXF 格式、毫米 1:1。','交付按板厚/材质分组的轮廓、板件清单和编号拼焊图；与工件五轴程序分别输出。','设计待接入'),
  ]),
 ]),
 ('02 CAD', '可选的几何辅助', '行业主流程是导入加工。三维零件编辑可提供，也可缺省，不阻断正常 CAM。', [
  ('2.1 几何检查与修复', [
   ('inspection','几何检查','识别导入几何是否存在影响后续加工的问题。','当前工件及其几何版本；发起结构检查。','显示结构检查结果。普通刷新保留同版本报告，几何变化后要求重查；不代表刀路或整机已通过。','通道接入'),
   ('cad-repair','几何修复','修复缝隙、破损面和退化/重复拓扑。','检查问题；缝合、局部曲面或拓扑处理方式及允许误差。','先预览，再形成可撤销的新几何版本；保留原模型，重新核对切割定义的来源。','设计待接入'),
  ]),
  ('2.2 辅助编辑', [
   ('cad-edit','三维零件编辑','必要时改变零件实体，如 CSG 开孔和布尔编辑。','基体、工具体/局部特征；开孔直径与切除深度等。','形成可编辑特征与新实体版本。属于可选增强；不同于只建立加工定义的 CAM 草图投影。','可选待接入'),
   ('measure','测量','核对模型距离和坐标轴分量。','用公共捕捉选择两个模型点；明确单位与坐标系。','显示总距离及 ΔX、ΔY、ΔZ；只读，不改变工件、坐标或刀路。','设计待接入'),
  ]),
 ]),
 ('03 CAM', '切割线准备', '切割定义是可编辑输入；确认切割线后仍须设置工艺并生成实际运动。', [
  ('3.1 切割线准备', [
   ('extract','自动提取','从模型识别加工面和切割边界候选。','当前工件、适用识别条件；检查候选并排除不加工区域。','确认后建立有来源的切割定义；当前只有入口/草稿，不能以进入面板宣称已自动识别。','设计待接入'),
   ('manual-extract','手动选面','补充自动识别遗漏，或使用已有拓扑选择建线。','加工面、边界及当前拓扑引用；确认目标对象。','建立切割定义。已有拓扑建定义走共享通道；完整交互选面和候选确认仍待接入。','部分接入'),
   ('projection','草图投影','按孔径、孔距和安装方向在曲面上构造加工线。','工作平面与偏移；圆/矩形/腰型轮廓及孔组；目标面、投影方向、设计轴向。','保留草图尺寸、约束与投影来源，确认多层命中；不改实体，刀头姿态另设，尺寸修改可重投影。','设计待接入'),
   ('split','打断切割线','把普通轮廓分为可独立加工的线段。','原轮廓、分段位置；检查所得段的连续性和来源。','得到可编辑分段并保留来源；标准孔先转为可编辑轮廓，受影响刀路需重算。','设计待接入'),
  ]),
 ]),
 ('03 CAM', '工艺与方向', '起点与孔属性各维护一份；几何孔形、设计轴向和加工姿态分别表达。', [
  ('3.2 工艺与方向', [
   ('path-hole','孔与姿态','核对孔形，选择独立的加工姿态策略。','切割定义；圆孔/腰型孔等类别；连续调姿或固定方向。','圆孔不自动等于固定摆轴；固定方向须核对设计轴向、机床和工艺适用性。','设计待接入'),
   ('path-start','起点','指定普通轮廓的切入位置。','任意曲线位置或轮廓关键点。','更新同一切割定义，重新生成起刀姿态；不另存第二份刀路起点。','设计待接入'),
   ('leads','引线','在废料区域穿孔并进入成品边界。','直线/圆弧/板外引线；引入引出长度、角度和留料侧。','生成引入/引出段；板外引线适用于开放修边，关闭时保留草稿但不参与生成。','设计待接入'),
   ('side','阴阳切','明确成品保留侧及废料侧。','轮廓用途与承载面；保留内部、外部或有效自动判断。','引线进入废料侧，补偿按材料侧执行；不能按屏幕左右判断。','设计待接入'),
   ('overcut','过切','闭合轮廓越过起点继续切一段，帮助切透。','闭合轮廓、起点、方向、过切长度。','沿原轮廓延续；不等同于末端切线延长，不套用于开放线。','设计待接入'),
   ('compensation','补偿','按切缝宽度修正路径，使成品尺寸符合要求。','补偿开关、切缝宽度、材料侧与拐角方式。','形成补偿路径后重算；取消补偿不改变源几何定义。','设计待接入'),
   ('reverse','反向','改变普通轮廓的切割行进方向。','选中轮廓及方向预览。','同步起点、引线与过切，保持原材料保留侧和成品含义。','设计待接入'),
   ('clear','清除 / 恢复继承','移除指定工艺，或恢复配置的继承值。','引线、补偿、过切等类别；明确清除或恢复继承。','保留几何、起点、方向、材料侧、姿态与人工约束；两种动作不混用。','设计待接入'),
  ]),
 ]),
 ('03 CAM', '生成与调整', '一次“生成刀路”协调完整计划；局部姿态、整条路径及工序连接分别修改。', [
  ('3.3 生成与调整', [
   ('generate','生成刀路','为当前装夹一次生成完整切割与空移运动。','启用切割定义、工艺、姿态约束、机型工具和装夹配置。','包含路径、轴运动、顺序、全部连接及入口/结束运动；保留人工约束，失败不发布有效计划。','设计待接入'),
   ('keypoint','关键点编辑','修正路径局部的刀头方向或实际轴值。','有效路径位置；前倾/侧倾、方向约束或实际轴锁定；过渡规则。','保留人工关键点；系统起终点不可删除，相邻点按规则过渡并重新验证。','设计待接入'),
   ('adjust','刀路调整','调整整条路径的目标方向和允许调姿范围。','法向或倾斜方式；目标角、各方向容差、总容差与法向偏好。','目标与允许偏差分别保存；局部硬锁定仍保留，不能以整体修改覆盖人工约束。','设计待接入'),
   ('rapid','空移调整','修改上一刀结束到下一刀开始的连接。','选中连接；自动、直接、抬升或绕障策略；工件/夹具间隙。','重算对应连接及受影响边界；直接连接也必须满足可达性、间隙和验证要求。','设计待接入'),
   ('manual-order','手动排序','按加工需要锁定工序顺序。','从头排序或保留前缀后继续；按目标次序点选刀路。','形成独立序列草稿；后移为撤回一步，前移为重做一步，应用后重算连接。','设计待接入'),
   ('auto-order','自动排序','在工艺约束内减少空移。','内轮廓优先/外轮廓后切等规则；人工锁定顺序。','保持有效锁定和工艺依赖，重算受影响连接；不承诺全局最短路线。','设计待接入'),
  ]),
 ]),
 ('03 CAM', '检查、输出与设置', '轨迹播放帮助观察路径；生产交付仍需完整计划、有效整机验证和实际输出重放。', [
  ('3.4 检查与输出', [
   ('simulation','轨迹仿真','查看工件、切割头与加工路径的相对运动。','路径位置、工具方向及工艺事件；播放、步进、进度和速度。','检查起点、顺序、切割/空移及头部姿态；可返回关键点、整体或空移调整，不产生整机通过结论。','设计待接入'),
   ('registration','三点定位','为现场配对测量准备容易找到的模型点。','三个模型特征点、点序、执行责任方及现有定位状态。','输出待测量定位计划；只选模型点不算已定位，已有精定位时不重复应用变换。','设计待接入'),
   ('export','导出','把有效加工计划变成目标设备可交付程序。','生产/设备验收用途；当前计划、机型、定位、后处理和验证报告。','生成程序后重放实际文件；通过后交付程序、装夹定位、工艺摘要和记录；资格缺失保持禁用。','设计待接入'),
  ]),
  ('3.5 设置', [
   ('settings','设备与规划设置','管理机床、切割头、标定和加工默认参数。','SDF/URDF 设备定义；跟随高度、碰撞间隙、识别阈值、姿态与几何/方向精度。','设备导入已接通共享通道；配置选择、提交及完整规划待接入。实机仿真引用同一配置版本。','部分接入'),
  ]),
 ]),
 ('04 仿真', '真实机型数字孪生', '必须以实际运动轴驱动完整机床；单独切割头、通用示意模型和外观模型均不足以形成整机结论。', [
  ('4.1 数字孪生', [
   ('twin-setup','实机配置','确认数字模型与真实设备及装夹相符。','厂家/机型和版本；整机固定/运动件；轴序、零位、旋转中心、行程动态、工具标定和装夹。','形成经过核对的固定版本配置；资料缺失显示待配置，外观导入成功不授予仿真资格。','设计待接入'),
   ('machine-simulation','整机仿真','用当前完整轴轨迹重放实际机床运动。','有效实机配置、完整加工计划、工件、夹具和障碍物。','检查部件碰撞、行程、可达性、连续性和轴运动异常；缺少配置/轨迹不能运行或发布通过报告。','设计待接入'),
   ('twin-results','仿真结果','定位问题，修正后复验并保留证据。','与机型、工具、装夹和计划版本绑定的报告。','问题回到工序、关键点、空移或装夹；输入变化后报告失效。“暂无结果”始终表示未验证。','设计待接入'),
  ]),
 ]),
 ('05 帮助', '操作指引与问题排查', '帮助只提供使用说明和支持；设备、切割头及加工参数放在 CAM 设置。', [
  ('5.1 支持', [
   ('manual','操作说明','解释五个页面及完整加工主流程。','当前工作任务；三类定位和两类仿真的范围。','提供工作流说明及跳转指引；新建/打开/保存统一指向 iCAX 文件入口。','界面可用'),
   ('diagnostics','导出诊断包','收集可复现的问题上下文供排查。','工程范围、操作记录、错误及相关配置版本。','生成诊断资料；当前只展示上下文和入口，诊断包生成尚未接入。','设计待接入'),
   ('logs','日志','查看导入、生成和验证过程中的运行记录。','公共日志区；规划支持日期、级别和消息类别筛选。','普通结果写入日志；当前公共日志可用，专用筛选功能仍待接入。','部分接入'),
   ('about','关于','了解产品、界面版本和运行框架。','当前产品信息。','展示当前 UI 预览身份；不能将界面版本当作设备加工验收状态。','界面可用'),
  ]),
  ('5.2 界面', [
   ('view.reset-layout','重置布局','恢复工作台面板的公共默认布局。','用户主动点击；使用 iCAX 共享布局动作。','恢复界面布局；不应修改工件、切割定义、工艺或已保存工程。','共享入口'),
  ]),
 ]),
 ('附录 A', '公共工作台能力', '这些能力贯穿五页；iCAX 文件入口仍归应用外壳，不作为第六个产品页面。', [
  ('A.1 iCAX 文件与显示', [
   ('shared-files','工程新建 / 打开 / 保存','管理可恢复工程及其生命周期。','外壳统一文件入口、工程文件和目标路径。','产品不重复建文件页；当前 UI 草稿未持久化，不宣称点击保存已保存未接入的业务参数。','共享入口'),
   ('shared-viewport','三维视图 / 适合窗口','观察模型和当前加工对象。','SDK 视口、视角与适合窗口操作。','复用真实三维画布；示例网格仅验证界面，不证明精确几何加工或实机能力。','界面可用'),
  ]),
  ('A.2 统一任务与交互状态', [
   ('shared-tree','任务树 / 搜索 / 选择','持续定位正在编辑的加工对象。','工件、机床、切割定义与名称筛选。','跨工件、CAD、CAM、仿真保留选择和草稿；切割定义与生成结果区分状态。','界面可用'),
   ('shared-drafts','属性草稿与条件字段','编辑方案参数，同时保持输入连续。','按工件、切割定义或操作范围隔离的草稿。','关闭选项保留其草稿；不适用值不应参与生成。当前仅本次会话，未提交工程。','界面可用'),
   ('shared-refresh','刷新与交互保护','读取工程变化并保留正在进行的操作。','最新业务结果和刷新前即时交互状态。','保留画布、输入节点/监听器、焦点选区及左右/嵌套滚动；移除字段或切页不抢焦点。','界面可用'),
   ('shared-dock','底部流程 / 问题面板','连接当前页面的下一步与问题修正。','夹具制造、CAD 辅助、加工顺序、输出检查或实机仿真上下文。','按页面显示流程；无计划/报告时明确未生成、未验证，导航到相应修正入口。','界面可用'),
  ]),
 ]),
]

ROWS = [row for _,_,_,groups in PAGES for _,rows in groups for row in rows]
ids = [row[0] for row in ROWS]
assert len(set(ids)) == len(ids)
source = (ROOT/'src/apps/five-axis-cam/webpage/workspaceModel.mjs').read_text(encoding='utf-8')
tool_ids = set(re.findall(r"^  \['([^']+)', '(?:workpiece|cad|toolpath|simulation|help)',", source, re.M))
assert tool_ids <= set(ids), f'Missing product functions: {tool_ids-set(ids)}'
assert len(tool_ids) == 41

def para(text, size=9.6, color=INK, bold=False, leading=None):
    return Paragraph(escape(text).replace('\n','<br/>'), ParagraphStyle(
        'p', fontName='YaHeiBold' if bold else 'YaHei', fontSize=size,
        leading=leading or size*1.48, textColor=color, wordWrap='CJK', spaceAfter=0))

C = canvas.Canvas(str(PDF), pagesize=(W,H), pageCompression=1)
C.setTitle('三维五轴 CAM - 三级功能规格表')
C.setAuthor('iCAX')
C.setSubject('工件、CAD、CAM、仿真、帮助；页面、功能分组、具体功能')
TOTAL = len(PAGES)+1+3
page_no=0

def page_start(level, title, subtitle):
    global page_no
    page_no+=1
    C.setFillColor(TEAL); C.rect(0,H-8,W,8,fill=1,stroke=0)
    C.setFont('YaHeiBold',10); C.drawString(M,H-33,'iCAX  /  三维五轴 CAM')
    C.setFillColor(MUTED); C.setFont('YaHei',9); C.drawRightString(W-M,H-33,level)
    C.setFillColor(INK); C.setFont('YaHeiBold',22); C.drawString(M,H-69,title)
    p=para(subtitle,10,MUTED); _,ph=p.wrap(CW,45); p.drawOn(C,M,H-83-ph)
    C.setStrokeColor(LINE); C.line(M,37,W-M,37)
    C.setFont('YaHei',8); C.setFillColor(MUTED)
    C.drawString(M,22,'设计基线 1.0  |  2026-10-13  |  状态按当前界面核对')
    C.drawRightString(W-M,22,f'{page_no:02d} / {TOTAL:02d}')
    return H-96-ph

def table_group(title, rows, y, num, size=9.6):
    C.setFillColor(TEAL); C.setFont('YaHeiBold',11); C.drawString(M,y,title)
    y-=11
    data=[[para(t,9,white,True) for t in ['编号 / 三级功能','功能作用','输入与操作','结果与业务边界','当前状态']]]
    for row in rows:
        num+=1
        ident,name,purpose,inputs,result,state=row
        data.append([para(f'F{num:02d}\n{name}',size,bold=True),para(purpose,size),para(inputs,size),para(result,size),para(state,9,TEAL)])
    widths=[100,148,168,CW-496,80]
    t=Table(data,colWidths=widths,hAlign='LEFT')
    t.setStyle(TableStyle([
      ('BACKGROUND',(0,0),(-1,0),INK),('VALIGN',(0,0),(-1,-1),'TOP'),
      ('LEFTPADDING',(0,0),(-1,-1),8),('RIGHTPADDING',(0,0),(-1,-1),8),
      ('TOPPADDING',(0,0),(-1,-1),7),('BOTTOMPADDING',(0,0),(-1,-1),7),
      ('ROWBACKGROUNDS',(0,1),(-1,-1),[LIGHT,white]),
      ('LINEBELOW',(0,0),(-1,-1),0.35,LINE),
    ]))
    _,height=t.wrap(CW,H)
    if y-height<49:
        raise ValueError(f'Page {page_no} overflow: bottom={y-height:.1f}, {title}')
    t.drawOn(C,M,y-height)
    return y-height-23,num

def simple_table(headers, rows, widths, y, size=10):
    data=[[para(h,size,white,True) for h in headers]]+[[para(cell,size) for cell in row] for row in rows]
    t=Table(data,colWidths=widths)
    t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),INK),('ROWBACKGROUNDS',(0,1),(-1,-1),[LIGHT,white]),('VALIGN',(0,0),(-1,-1),'TOP'),('TOPPADDING',(0,0),(-1,-1),8),('BOTTOMPADDING',(0,0),(-1,-1),8),('LEFTPADDING',(0,0),(-1,-1),10),('RIGHTPADDING',(0,0),(-1,-1),10),('LINEBELOW',(0,0),(-1,-1),0.4,LINE)]))
    _,h=t.wrap(CW,H); t.drawOn(C,M,y-h); return y-h

y=page_start('规格总览','三级功能规格表','一级：页面  /  二级：功能分组  /  三级：具体功能。每项解释作用、输入、结果与当前实现状态。')
y=simple_table(['一级页面','主要任务与边界','页码'],[
 ('工件','导入与添加模型、坐标装夹、板式夹具生成、拆单及平面下料/拼焊交付。','02-03'),
 ('CAD','可选几何辅助：检查、修复、三维零件编辑和测量，不是进入 CAM 的前置。','04'),
 ('CAM','切割线准备、工艺与方向、生成调整、头部轨迹仿真、输出和设置。','05-08'),
 ('仿真','完整真实机型的数字孪生；缺少实机配置时待配置，不得以轨迹播放代替。','09'),
 ('帮助','操作说明、诊断、日志、产品信息和布局；公共能力见附录 A。','10-11'),
], [94,CW-174,80],y)
y-=25
p=para('主流程：工件导入与装夹 → CAM 编程 → 实机仿真 → CAM 输出；工程文件统一使用 iCAX 入口。',11,TEAL,True)
_,h=p.wrap(CW,60); p.drawOn(C,M,y-h); y-=h+15
legend='状态说明：界面可用 = 已实现页面交互；通道接入 = 已调用共享原生消息，但本轮未做真实原生验收；部分接入 = 仅部分子能力接通；设计待接入 = 页面/草稿已有，业务计算与提交尚不可用；共享入口 = 复用外壳能力；可选待接入 = 不影响导入加工的增强项。'
p=para(legend,10); _,h=p.wrap(CW,80);p.drawOn(C,M,y-h);y-=h+12
p=para(f'范围：43 项产品入口功能 + 6 项公共工作台能力，共 {len(ROWS)} 项。17 组浏览器检查和产品隔离检查通过；截图见附录 B。使用真实外壳/SDK 视口及受控原生代理，未验收原生后端或独立程序。当前为 UI 预览。',10,MUTED)
_,h=p.wrap(CW,70);p.drawOn(C,M,y-h)
assert y-h>49
C.showPage()

num=0
for level,title,subtitle,groups in PAGES:
    y=page_start(f'一级 / {level}',title,subtitle)
    for group,rows in groups:
        y,num=table_group(group,rows,y,num,size=9.2 if len(rows)>7 else 9.6)
    if level=='04 仿真':
        p=para('验证分工：CAM 轨迹仿真观察路径与切割头；本页验证完整机床、轴运动及装夹。两者应使用同源计划，不维护互不一致的第二套轨迹。',10,TEAL)
        _,h=p.wrap(CW,60);p.drawOn(C,M,y-h); assert y-h>49
    C.showPage()

for title,name,caption in [
 ('工件与夹具制造','10-workpiece.png','导入、添加、定位和夹具制造在工件页；统一任务树贯穿全部页面。'),
 ('CAM 编程','02-programming.png','自动提取、手动选面与草图投影直接可见；工艺、生成调整、轨迹仿真与输出在同页组织。'),
 ('独立实机仿真','17-machine-simulation.png','真实机型尚未配置时明确显示待配置；当前示例工件和画布不表示整机仿真已运行。'),
]:
    y=page_start('附录 B / 当前页面截图',title,caption)
    iw,ih=1600,1000
    maxh=y-50
    imagew=min(CW,maxh*iw/ih); imageh=imagew*ih/iw
    C.drawImage(str(QA/name),M+(CW-imagew)/2,y-imageh,width=imagew,height=imageh)
    C.showPage()

assert num==len(ROWS)==49
assert page_no==TOTAL==14
C.save()
reader=PdfReader(str(PDF))
assert len(reader.pages)==TOTAL
text='\n'.join(p.extract_text() or '' for p in reader.pages)
compact_text=re.sub(r'\s+', '', text)
for i, row in enumerate(ROWS,1):
    assert f'F{i:02d}' in text, row[1]
    assert re.sub(r'\s+', '', row[1]) in compact_text, row[1]
assert all(label in text for label in ['工件','CAD','CAM','仿真','帮助','平面','拼焊','待配置'])
manifest={'file':str(PDF),'pages':TOTAL,'functions':len(ROWS),'productFunctions':43,'sharedFunctions':6,'allProductToolIdsCovered':True,'textCheckPassed':True,'rows':[{'id':f'F{i:02d}','tool':r[0],'name':r[1],'status':r[-1]} for i,r in enumerate(ROWS,1)]}
(QA/'function-spec-check.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in manifest.items() if k!='rows'},ensure_ascii=False))
