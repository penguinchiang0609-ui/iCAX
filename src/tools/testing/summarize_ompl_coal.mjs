import {readFileSync,writeFileSync,existsSync} from 'node:fs';
import {resolve,dirname,join} from 'node:path';
import {createHash} from 'node:crypto';
const source=resolve(process.argv[2]??'output/tests/ompl-coal-evaluation/egg-479-final-report.json');
const report=JSON.parse(readFileSync(source,'utf8')),directory=dirname(source);
const regression=join(directory,'regression.xml');
const xml=existsSync(regression)?readFileSync(regression,'utf8'):'';
const tag=xml.match(/<testsuites\b[^>]*>/)?.[0]??'';
const attribute=name=>Number(tag.match(new RegExp(`${name}="(\\d+)"`))?.[1]??0);
const checks={tests:attribute('tests'),failures:attribute('failures'),errors:attribute('errors')};
const artifacts=[source,'output/tests/coal-optimized-build/OmplPlanningServices.dll','output/tests/coal-optimized-build/CoalCollisionServices.dll','output/tests/coal-optimized-build/FiveAxisCAMTest.exe','output/tests/egg-480-generation/egg-480-cut-generated.i5cam'];
const hashes=artifacts.filter(existsSync).map(path=>({path:resolve(path),sha256:createHash('sha256').update(readFileSync(path)).digest('hex')}));
const summary={evaluatedAt:new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',dateStyle:'full',timeStyle:'long'}).format(new Date()),
  recommendation:report.fixtureStressPassed?'OMPL can serve as a bounded obstacle-search fallback. Current full CAM path pipeline is not accepted; fix preparation, collision representation and endpoint validation first.':'The prototype has not passed fixture obstacle search; do not enable it in the product.',
  productionEnabled:false,regression:checks,planningStagePassed:report.planningStagePassed,fullPathPassed:report.functionalPassed,fixtureStressPassed:report.fixtureStressPassed,hashes};
writeFileSync(join(directory,'assessment.json'),JSON.stringify(summary,null,2));
const escape=v=>String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const number=(v,d=2)=>Number(v).toLocaleString('zh-CN',{maximumFractionDigits:d});
const prepared=report.rebuildCadMs+report.meshMs+report.coalPreparationMs;
const stress=report.fixtureStress??[];
const stressRows=stress.map(s=>`<tr><td>${s.repeat+1}</td><td>${escape(s.method)}</td><td>${s.directBlocked?'已阻挡':'未阻挡'}</td><td>${number(s.elapsedMs)} ms</td><td>${number(s.states,0)}</td><td>${s.audited?'通过':'未通过'}</td></tr>`).join('');
const exactRows=(report.exactEndpointDiagnostics??[]).map(s=>`<tr><td>${number(s.index,0)}</td><td>${s.done?number(s.boundaryDistanceMm,6)+' mm':'未完成'}</td><td>${number(s.elapsedMs)} ms</td></tr>`).join('');
const html=`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>OMPL＋Coal 实测评估</title><style>
body{font:16px/1.75 system-ui,"Microsoft YaHei",sans-serif;color:#263641;background:#f2f5f7;margin:0}main{max-width:1040px;margin:40px auto;background:white;padding:40px 52px;border-radius:12px}h1{font-size:30px}h2{font-size:21px;margin-top:34px}p{margin:12px 0}.lead{font-size:19px}.warn{background:#fff0e5;border-left:5px solid #c75c1c;padding:14px 20px}.good{color:#11684d}.bad{color:#ad382e}.muted{color:#627784}table{border-collapse:collapse;width:100%;margin:16px 0}th,td{border-bottom:1px solid #dbe3e8;padding:10px 14px;text-align:left}th{background:#edf2f5}code{font-size:14px}small{color:#627784}.flow{padding:18px;background:#eaf3f6;text-align:center}a{color:#176e98}</style><main>
<h1>OMPL＋Coal 实测评估</h1><p class="muted">${escape(summary.evaluatedAt)} · OMPL ${escape(report.ompl)} / Coal ${escape(report.coal)} · 原生 C++ service 评估</p>
<p class="lead">${report.fixtureStressPassed?'OMPL 可作为复杂绕障的搜索组件。当前慢点主要在输入准备和碰撞表示，完整空移流程仍未验收通过。':'当前原型未通过绕障验证，不能启用为产品默认规划器。'}</p>
<p class="warn"><strong>完整空移验收：${report.functionalPassed?'通过':'未通过'}。</strong>中间连接阶段与端点退刀、进刀分别统计；中间连接成功不代表整段空移安全。产品默认入口尚未切换。</p>
<h2>真实模型结果</h2><p>蛋型零件，3996 个 CAD 面、480 项原始加工操作、479 段相邻连接。原始刀路和姿态${report.sourceCutsUnchanged?'保持不变':'发生变化'}。</p>
<table><tr><th>指标</th><th>结果</th><th>含义</th></tr>
<tr><td>外部安全区之间的连接</td><td class="good">${number(report.centralAuditedSolved,0)} / ${number(report.testedConnections,0)}</td><td>均经独立连续运动复核</td></tr>
<tr><td>自然连接触发 RRT 搜索</td><td>${number(report.rrtSearches,0)} 段</td><td>这些连接都能直连，不能归为 OMPL 搜索提速</td></tr>
<tr><td>端点、退刀和进刀全部通过</td><td class="${report.functionalPassed?'good':'bad'}">${number(report.auditedSolved,0)} / ${number(report.testedConnections,0)}</td><td>保守碰撞包络仍拒绝原始端点，保留失败状态</td></tr>
<tr><td>479 段连接阶段总耗时</td><td>${number(report.planningMs/1000,3)} s</td><td>包含端点预检查和中间路径复核；不包含模型准备</td></tr>
<tr><td>单段中位 / P95 / 最大</td><td>${number(report.p50Ms)} / ${number(report.p95Ms)} / ${number(report.maximumMs)} ms</td><td>以上连接阶段的实测耗时</td></tr></table>
<h2>明确挡路的夹具：实际触发 OMPL</h2><p>在同一真实 CAD 模型上加入一个测试夹具，挡住起终配置之间的直线，重复运行 3 次。此测试夹具没有写入用户项目。</p>
<table><tr><th>次数</th><th>规划器</th><th>原直线</th><th>搜索耗时</th><th>配置点数</th><th>独立复核</th></tr>${stressRows}</table>
<h2>等待时间花在哪里</h2><table><tr><th>首次准备阶段</th><th>耗时</th></tr>
<tr><td>打开项目文件</td><td>${number(report.loadProjectMs/1000)} s</td></tr><tr><td>首次读取场景与生成结果</td><td>${number(report.readStateMs/1000)} s</td></tr>
<tr><td>重建 CAD</td><td>${number(report.rebuildCadMs/1000)} s</td></tr><tr><td>生成碰撞网格</td><td>${number(report.meshMs/1000)} s</td></tr><tr><td>准备 Coal 碰撞树</td><td>${number(report.coalPreparationMs/1000)} s</td></tr>
<tr><td><strong>CAD、网格和碰撞树合计</strong></td><td><strong>${number(prepared/1000)} s</strong></td></tr></table>
<p>碰撞网格达到 ${number(report.meshTriangles,0)} 个三角形。即使搜索很快，继续沿用这套准备链，首次等待仍然很长。以上是一次冷启动样本，不是稳定性能或对旧版本的提速比。</p>
<h2>端点误判对照</h2><p>保守包络未能确认原始端点间隙；以下 3 个抽查端点用 CAD 精确求距得到约 1 mm。不能把包络重叠直接说成真实碰撞，也不能据此推定其余端点都已验证。</p>
<table><tr><th>连接索引</th><th>CAD 精确边界距离</th><th>对照耗时</th></tr>${exactRows}</table>
<p>输入模型最大拓扑容差约 ${number(report.maximumCadToleranceMm,4)} mm，碰撞网格最大报告偏差约 ${number(report.maximumReportedDeflectionMm,4)} mm。当前按面误差扩张仍过于保守，需修正表示方式并重新完成端点及退进刀验收。</p>
<h2>接入方式与结论</h2><div class="flow">确定的退刀／进刀规则 → 可直接连接则复核 → 受阻时调用 OMPL → Coal 提供运动验证</div>
<p>规划核心已封装为独立 <code>OmplPlanningServices.dll</code>，公开接口 <code>IConfigurationPathPlanningService</code> 只接收数值轴空间和验证回调。Scene／SDO 继续拥有加工规则、版本及持久化。</p>
<p>建议先修正碰撞模型的误差表示、缓存和首次准备成本，再把 OMPL 作为复杂绕障的受限兜底。当前评估使用 XYZ＋偏航／倾角的刀头配置，尚未接入实际机床轴限位、逆运动学和 NC 输出。</p>
<p>常规回归：${checks.tests} 项，失败 ${checks.failures}，错误 ${checks.errors}。真实模型完整空移验收保留失败；没有以中间阶段通过覆盖它。</p>
<p><a href="${escape(source.split(/[\\/]/).at(-1))}">原始测量与全部路径</a> · <a href="assessment.json">结论与输入／模块 SHA256</a></p></main></html>`;
writeFileSync(join(directory,'assessment.html'),html);console.log(JSON.stringify({summary:join(directory,'assessment.json'),html:join(directory,'assessment.html'),regression:checks,fullPathPassed:report.functionalPassed,fixtureStressPassed:report.fixtureStressPassed}));
