import * as THREE from './three/three.module.js';
import {LineSegments2} from './three-lines/LineSegments2.js';
import {LineSegmentsGeometry} from './three-lines/LineSegmentsGeometry.js';
import {LineMaterial} from './three-lines/LineMaterial.js';

const $ = id => document.getElementById(id);
const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'}[c]));
const idOf = value => String(value ?? '');
const number = value => Number.isFinite(value) ? (value !== 0 && Math.abs(value) < 1e-5 ? value.toExponential(4) : Number(value.toPrecision(9)).toLocaleString('zh-CN', {maximumFractionDigits:9})) : '未取得';
const distance = value => Number.isFinite(value) ? `${number(value)} mm` : '未取得';
const yesNo = value => value === true ? '是' : value === false ? '否' : '未取得';
const SKIN = {OnSkin:{name:'在皮上', color:'#00B8D9'}, OffSkin:{name:'不在皮上', color:'#F59E0B'}, PartialOnSkin:{name:'部分在皮上', color:'#A855F7'}};
const CONVEXITY = {Convex:{name:'凸', pattern:'实线', dash:[]}, Concave:{name:'凹', pattern:'虚线', dash:[9,5]}, Coplanar:{name:'共面', pattern:'点划线', dash:[11,4,2,4]}, Free:{name:'自由边', pattern:'点线', dash:[2,4]}};
const query = new URLSearchParams(location.search);
const canvas = $('scene');
const renderer = new THREE.WebGLRenderer({canvas, antialias:true, alpha:false});
renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
const scene = new THREE.Scene();
scene.background = new THREE.Color('#111d2b');
scene.add(new THREE.HemisphereLight(0xf1f6ff, 0x647990, 2.4));
const light = new THREE.DirectionalLight(0xffffff, 2.0);
light.position.set(2, -3, 5);
scene.add(light);
const camera = new THREE.PerspectiveCamera(37, 1, .001, 1e7);
camera.up.set(0, 0, 1);
const faceGroup = new THREE.Group();
const edgeGroup = new THREE.Group();
scene.add(faceGroup, edgeGroup);
const raycaster = new THREE.Raycaster();
raycaster.params.Line2 = {threshold:7};
const mouse = new THREE.Vector2();
const target = new THREE.Vector3();
let models = [], currentModel = null, currentData = null, faceMap = new Map(), edgeMap = new Map();
let faceMeshes = [], edgeLines = [], selectedFace = '', selectedEdge = '';
let modelRadius = 1, orbitRadius = 5, theta = .8, phi = 1.08, width = 1, height = 1;
let loadRevision = 0, drawPending = false, hoverPending = false, pointer = null;
let materialCache = new Map();
let representatives = new Map(), equivalenceGroups = new Map(), displayEdges = new Map(), equivalenceConflicts = [];
let sourceEdgeMap = new Map();
let phaseRuns = [];
const REASONS = {
  'all-finite-section-distance-samples-on-skin':'全部有限轮廓距离采样均在判皮容差内',
  'whole-source-edge-within-finite-skin-distance-tolerance':'完整原边在测距容差内，保留近似皮归属',
  'finite-native-support-span-on-skin':'该参数段沿有限皮支撑',
  'source-skin-attribution-propagated-through-coincident-analysis-span':'通过对应的分析边段保留来源皮面归属',
  'skin-contact-at-one-or-both-endpoints-only':'端点与皮接触；该子段没有连续皮支撑',
  'no-finite-section-distance-sample-on-skin':'端点与内部采样均不在皮上',
  'no-skin-contact':'未发现皮接触',
  'no-sampled-skin-contact':'端点与内部采样均未发现皮接触',
  'mixed-skin-contact-inside-source-edge':'一条原始边内部存在混合皮接触，尚未拆分参数段',
  'interior-on-skin-with-off-skin-endpoint':'内部在皮上但存在不在皮上的端点，不能归入端点式部分皮上',
  'shared-source-coedge-on-verified-stock-skin-support':'与已验证的来源皮面共享原始边',
  'one-machining-face':'仅一个加工面使用此边，因此为自由边',
  'oriented-dihedral-samples':'根据沿边采样的有向二面角判定',
  'same-planar-support-and-tangent-dihedral':'两侧面共用同一平面，沿边二面角相切',
  'tangent-supports-are-not-coplanar':'两侧曲面沿边相切，但不共面，凹凸枚举尚不能表达',
  'closed-solid-material-orientation-with-local-second-face-consistency':'以闭实体材料朝向为锚，统一第二邻面的局部分析朝向',
  'first-source-face-anchor-with-local-second-face-consistency':'以第一个来源面为锚，统一第二邻面的局部分析朝向',
  'first-adjacent-face-anchor-with-local-second-face-consistency':'以第一个相邻面为锚，统一第二邻面的局部分析朝向',
  'oriented-dihedral-samples-with-local-second-face-orientation-repair':'修正第二邻面的局部分析朝向后，按有向二面角采样判定',
  'zero-local-dihedral-with-local-second-face-orientation-repair':'修正第二邻面的局部分析朝向后，零二面角平接',
  'zero-local-dihedral':'两面局部零二面角平接',
  'parameter-seam':'同一面的参数接缝，不是物理加工边',
  'same-face-parameter-seam':'同一面的参数接缝，不是物理加工边',
  'same-face-seam-or-repeated-use':'同一面的参数接缝或重复边使用，不能当作两个邻面',
  'non-manifold-machining-adjacency':'相邻加工面超过两个，不能唯一判定二面角',
  'repeated-coedge-use':'存在重复的面内边使用，邻接关系尚不明确',
  'missing-oriented-face-use':'缺少带方向的面内边使用记录',
  'inconsistent-coedge-orientation':'两侧面内边方向不一致，不能可靠判凹凸',
  'singular-edge-tangent-or-surface-normal':'边切向或面法向在采样处退化',
  'opposed-coincident-normals':'两侧法向相反且重合，无法可靠区分材料侧',
  'convexity-changes-along-edge':'沿边凹凸性发生变化，不能整条赋同一枚举',
  'partial-geometric-adjacency-needs-edge-segmentation':'只在部分参数段发现邻面，需要先分段，不能整条判自由或凹凸',
  'additional-dihedral-sample-exceeds-adjacency-gap':'补充二面角采样超过共边匹配容差，邻接尚不可靠',
  'singular-planar-interior-direction':'共面边界的面内方向退化，不能判断两面位于边的哪一侧',
  'coplanar-faces-do-not-have-opposite-side-interiors':'共面面片未位于公共边的相对两侧，不能作为连续邻面',
  'same-planar-support-with-opposite-side-face-interiors':'两面共面，面内区域位于公共边的相对两侧',
  'partial-geometric-adjacency':'局部几何重合，仅部分边段有邻接',
  'coincident-sampled-correspondence':'完整有限边的双向采样对应',
  'coincident-sampled-analysis-spans':'有限分析边段的几何采样对应',
  'finite-covered-parameter-span-correspondence-within-1e-6-no-topology-mutation':'有限参数覆盖区间对应在 0.000001 mm 内；未修改原拓扑',
  'shared-neutral-edge':'共享同一中立模型边',
  'single-machining-owner':'仅一个加工面使用',
  'finite-curve-subinterval-overlap-needs-segmentation':'有限曲线子区间重合，需要分段表达邻接',
  '33-bidirectional-finite-curve-samples-within-1e-6-no-topology-mutation':'双向各33点对应均在 0.000001 mm 内；未修改原拓扑',
  'coincident-coplanar-boundaries-have-same-side-interiors-not-adjacency':'边界虽重合，但共面区域在同侧，已排除为邻接',
  'neutral-source-coedge-ownership':'根据中立源模型的面内边使用关系',
  'invalid-or-zero-curve-range':'原曲线参数域无效或长度为零',
  'degenerated-edge':'原始边退化，不能作普通曲线分析',
  'wire-connectivity-walk-incomplete-internal-external-or-branched-use':'线环包含内部、外部或分叉边使用，无法完成可靠排序',
  'matched-edge-classification-conflict':'同一几何共边的原始边记录判定不一致，显示为冲突待检查',
  'Convexity adjacency includes machining faces only; non-machining faces never count as a second machining neighbor':'凹凸邻接只统计加工面，非加工面不计为第二个邻面。',
  'Finite section distances and dihedral angles are sampled evidence, not a continuous error certificate':'轮廓距离与二面角为采样证据，未认证连续全域误差。',
  'Detached machining boundaries may be paired by 33 bidirectional finite-curve samples within 1e-6 mm; this is sampled correspondence, not a continuous coincidence certificate or sewn topology':'独立加工面边界可通过双向各33点、0.000001 mm 容差建立采样对应；未认证连续重合，也未缝合或修改原拓扑。',
  'Source skin attribution uses existing shared coedges and sampled finite analytic skin support; detached near-coincident sources remain geometric classifications':'来源皮归属采用现有共边与有限解析皮面支持；未建立共边的近似来源保留几何判定。',
  'Successful Classify and finite section contours are required':'需要成功的加工面分类和有效的有限截面轮廓。',
  'Machining edge analysis options are invalid':'加工面边分析参数无效。',
  'Classify has no machining faces':'分类结果没有加工面。'
};
function explain(reason) { return REASONS[reason] ?? String(reason??'').replace(/^skin-evaluation-failed:\s*/, '皮关系计算失败：').replace(/^dihedral-evaluation-failed:\s*/, '二面角计算失败：'); }

function skinValid(edge) { return edge.skinValid !== false && Boolean(SKIN[edge.skinRelation]); }
function convexityValid(edge) { return edge.convexityValid !== false && Boolean(CONVEXITY[edge.convexity]); }
function valid(edge) { return skinValid(edge) && convexityValid(edge); }
function edgeFaceIds(edge) { return (edge.faceIds ?? edge.machiningFaceIds ?? []).map(idOf); }
function edgeSourceFaceIds(edge) { return (edge.sourceFaceIds ?? edgeFaceIds(edge)).map(idOf); }
function faceLabel(id) {const face=faceMap.get(idOf(id));return `F${face?.faceIndex??id}`;}
function sourceTags(face) { return Array.isArray(face?.sourceTags)?face.sourceTags.filter(tag=>typeof tag==='string'):[]; }
function sourceTag(face,name) {
  const prefix=`classify.${name}=`;
  return sourceTags(face).find(tag=>tag.startsWith(prefix))?.slice(prefix.length);
}
function classificationDomain(face) {
  if(sourceTag(face,'zeroAreaTrim')==='true')return 'zero-area';
  return sourceTag(face,'interfaceDomain')??'';
}
const DOMAIN_NAMES={'internal':'内部接合片（不加工）','machining-remainder':'外露剩余片（加工）','zero-area':'零裁剪域（不加工）'};
const ZERO_TRIM_PROOFS={
  'exactly-collapsed-nonperiodic-U-trim-domain-with-constant-control-coordinate':'非周期 U 裁剪域严格坍缩，控制坐标恒定',
  'exactly-collapsed-nonperiodic-V-trim-domain-with-constant-control-coordinate':'非周期 V 裁剪域严格坍缩，控制坐标恒定',
  'two-affine-pcurve-uses-retrace-the-same-native-vertex-pair-within-parameter-roundoff':'两条仿射参数曲线在参数舍入界内沿同一原生顶点对往返'
};
function faceSourceHtml(face) {
  const tags=sourceTags(face),domain=classificationDomain(face),rows=[];
  if(face?.sourceId!==undefined)rows.push(['来源 sourceId',esc(face.sourceId||'未记录')]);
  if(domain)rows.push(['分类片类型',esc(DOMAIN_NAMES[domain]??domain)]);
  for(const [key,label] of [['sourceFaceIds','来源原面 ID'],['interfaceAnchorSourceFaceIds','接合锚定原面 ID'],['sourceEdgeIds','来源原边 ID'],['sourceCurve3Ids','来源三维曲线 ID']]){
    const value=sourceTag(face,key);if(value!==undefined)rows.push([label,esc(value||'未记录')]);
  }
  if(!rows.length&&!tags.length)return '';
  return `<section class="face-source"><h3>Classify 来源与分片</h3>${dl(rows)}<p class="muted">sourceId 与来源原面 ID 是来源标识；当前加工面的选中和定位使用模型面 ID。</p>${zeroTrimHtml(face)}${tags.length?`<details><summary>完整 sourceTags（${tags.length}）</summary>${valuesHtml(tags)}</details>`:''}</section>`;
}
function zeroTrimHtml(face) {
  if(classificationDomain(face)!=='zero-area')return '';
  const proof=sourceTag(face,'zeroAreaProof');
  const rows=[['裁剪域证明',esc(ZERO_TRIM_PROOFS[proof]??proof??'未记录')]];
  for(const [key,label] of [['zeroAreaSourceFaceId','Classify 输出面 ID'],['zeroAreaSourceSplitFaceIndex','分割前面索引'],['zeroAreaUVBounds','UV 参数范围'],['zeroAreaWireCount','线环数'],['zeroAreaCoedgeUseCount','面内边使用数'],['zeroAreaUVRoundoffBudget','UV 舍入预算'],['zeroAreaMaximumEndpointUVGap','最大端点 UV 间隙'],['zeroAreaUVAreaRepresentationBound','UV 面积表示界']]){
    const value=sourceTag(face,key);if(value!==undefined)rows.push([label,esc(value)]);
  }
  return `<div class="zero-trim-certificate"><b>零裁剪域证书</b>${dl(rows)}<p class="muted">面 ID 属于本次 Classify 输出；分割前面索引不表示导入原模型的祖先面编号。</p><p class="muted">数值按证书原文显示。UV 是曲面参数；UV 面积表示界不是物理面积阈值。</p>${proof?`<details><summary>证明标识</summary>${valuesHtml(proof)}</details>`:''}</div>`;
}
function classificationReviewHtml(expanded=false) {
  const review=currentData?.classificationReview;if(!review)return '';
  const records=review.nonMachiningDerivedFaces??[];
  const exposed=[...faceMap.entries()].filter(([,face])=>classificationDomain(face)==='machining-remainder');
  const internal=records.filter(face=>classificationDomain(face)==='internal');
  const zero=records.filter(face=>classificationDomain(face)==='zero-area');
  const other=records.filter(face=>!['internal','zero-area'].includes(classificationDomain(face)));
  const nonMachiningEntry=face=>`<article class="domain-entry"><b>${esc(DOMAIN_NAMES[classificationDomain(face)]??'分类派生片')}</b><div>分类模型面 ID ${esc(face.faceId)}</div>${faceSourceHtml(face)}</article>`;
  return `<details class="classification-review" ${expanded?'open':''}><summary>Classify 分类证据 · 外露 ${exposed.length} / 内部 ${internal.length} / 零裁剪域 ${zero.length}</summary>${dl([
    ['Classify 加工面',esc(review.machiningFaces??'未记录')],['Classify 不加工面',esc(review.nonMachiningFaces??'未记录')],
    ['外露剩余片',String(exposed.length)],['内部接合片',String(internal.length)],['零裁剪域证书',String(zero.length)]
  ])}<p class="muted">下列计数只统计带分片或零裁剪域标记的记录。内部片和零裁剪域不在当前加工面网格中。</p>${exposed.length?`<details class="exposed-domains"><summary>外露剩余片（可定位）· ${exposed.length}</summary>${exposed.map(([id,face])=>`<article class="domain-entry"><button data-face="${esc(id)}">${esc(faceLabel(id))} · 模型面 ID ${esc(id)}</button>${faceSourceHtml(face)}</article>`).join('')}</details>`:''}${internal.length?`<details class="internal-domains"><summary>内部接合片（不加工）· ${internal.length}</summary>${internal.map(nonMachiningEntry).join('')}</details>`:''}${zero.length?`<details class="zero-trim-domains"><summary>零裁剪域证书（不加工）· ${zero.length}</summary>${zero.map(nonMachiningEntry).join('')}</details>`:''}${other.length?`<details><summary>其他分类派生片 · ${other.length}</summary>${other.map(nonMachiningEntry).join('')}</details>`:''}${review.diagnostics?.length?`<details><summary>Classify 诊断（${review.diagnostics.length}）</summary>${diagnosticsHtml(review.diagnostics)}</details>`:''}<details><summary>完整 classificationReview</summary>${valuesHtml(review)}</details></details>`;
}
function edgeLabel(value) {
  const edge=typeof value==='object'?value:edgeMap.get(idOf(value));if(!edge)return idOf(value);
  const split=(sourceEdgeMap.get(idOf(edge.edgeId))?.segmentKeys?.length??0)>1;
  return `E${edge.edgeId}${split?' · S'+edge.segmentId:''}`;
}
function edgeReason(edge) {
  const fields=[edge.skinReason,edge.convexityReason,...(edge.diagnostics??[])].filter(Boolean);
  if(!fields.length&&edge.reason)fields.push(...String(edge.reason).split(/\s*;\s*/));
  return [...new Set(fields)].map(explain).join('；');
}
function stageMessages(data=currentData) {
  if(!data)return [];
  const messages=[];
  if(data.success===false)messages.push({kind:'analysis',text:'边分析未完成，具体原因见分析诊断。'});
  if(data.success!==false&&data.complete===false)messages.push({kind:'analysis',text:'分析结果未完成，请查看失败原因。'});
  if(data.artifacts?.stepRoundTrip?.passed===false)messages.push({kind:'artifact',text:'STEP 输出或读回验证未通过；与边分析结果分开记录。'});
  if(data.artifacts?.brepRoundTripPassed===false)messages.push({kind:'artifact',text:'整件 BREP 读回验证未通过。'});
  if(data.geometry?.issues?.length)messages.push({kind:'geometry',text:`显示网格有 ${data.geometry.issues.length} 项问题，见显示与导出检查。`});
  if(equivalenceConflicts.length)messages.push({kind:'analysis',text:`分析失败：${equivalenceConflicts.length} 组等价边段的属性不一致，未作为有效边段绘制。`});
  return messages;
}
function stageMessagesHtml() {return stageMessages().map(message=>`<div class="notice warning">${esc(message.text)}</div>`).join('');}
function diagnosticsHtml(diagnostics,warning=false) {
  if(!diagnostics?.length)return '';
  return `<ul class="diagnostics ${warning?'warning':''}">${diagnostics.map(item=>`<li>${esc(explain(typeof item==='string'?item:item.message??JSON.stringify(item)))}</li>`).join('')}</ul>`;
}
function artifactChecksHtml() {
  const artifact=currentData?.artifacts??{}, issues=currentData?.geometry?.issues??[];
  if(!Object.keys(artifact).length&&!issues.length)return '';
  const step=artifact.stepRoundTrip;
  return `<h3>显示与导出检查</h3>${dl([
    ['边分析',currentData.success===false?'<span class="bad">未完成</span>':'已执行'],
    ['关系判定',currentData.complete===false?'分析未完成':'每段唯一两枚举'],
    ['STEP 读回',step?.passed===true?'通过':step?.passed===false?'<span class="bad">未通过</span>':'未记录'],
    ['BREP 读回',artifact.brepRoundTripPassed===true?'通过':artifact.brepRoundTripPassed===false?'<span class="bad">未通过</span>':'未记录'],
    ['显示网格问题',String(issues.length)]
  ])}${issues.length?diagnosticsHtml(issues,true):''}${step?`<details ${step.passed===false?'open':''}><summary>STEP 读回记录</summary>${valuesHtml(step)}</details>`:''}`;
}
function faceEdges(faceId) {
  const face = faceMap.get(idOf(faceId));
  if (Array.isArray(face?.edgeKeys)) return face.edgeKeys.map(idOf).map(id=>edgeMap.get(id)).filter(Boolean);
  return [...edgeMap.values()].filter(edge=>edgeSourceFaceIds(edge).includes(idOf(faceId)));
}
function setStatus(text, error = false) {
  $('status').hidden = !text;
  $('status').textContent = text;
  $('status').classList.toggle('error', error);
}
function updateCamera() {
  phi = Math.max(.015, Math.min(Math.PI - .015, phi));
  camera.position.set(target.x + orbitRadius*Math.sin(phi)*Math.cos(theta), target.y + orbitRadius*Math.sin(phi)*Math.sin(theta), target.z + orbitRadius*Math.cos(phi));
  camera.near = Math.max(modelRadius * 1e-5, orbitRadius * 1e-5, 1e-9);
  camera.far = Math.max(modelRadius * 200, orbitRadius * 30, camera.near * 1000);
  camera.aspect = width/height;
  camera.updateProjectionMatrix();
  camera.lookAt(target);
  camera.updateMatrixWorld(true);
  requestDraw();
}
function resize() {
  const rect = canvas.getBoundingClientRect();
  width = Math.max(1, rect.width); height = Math.max(1, rect.height);
  renderer.setSize(width, height, false);
  for (const line of edgeLines) line.material.resolution.set(width, height);
  updateCamera();
}
function requestDraw() {
  if (drawPending) return;
  drawPending = true;
  requestAnimationFrame(()=>{
    drawPending = false;
    updateScreenDistances();
    renderer.render(scene, camera);
  });
}
function updateScreenDistances() {
  // Distances are measured after projection, in CSS pixels. Changing model size
  // or zoom therefore never enlarges dash lengths or line widths in model units.
  const p = new THREE.Vector3(), q = new THREE.Vector3();
  const lengths=new Map(), phases=new Map();
  for(const line of edgeLines){
    const starts=line.geometry.attributes.instanceStart, ends=line.geometry.attributes.instanceEnd;
    const values=[];let total=0;
    for(let i=0;i<starts.count;i++){
      p.fromBufferAttribute(starts,i).project(camera);q.fromBufferAttribute(ends,i).project(camera);
      const value=Math.hypot((p.x-q.x)*width*.5,(p.y-q.y)*height*.5);
      values.push(Number.isFinite(value)?value:0);total+=values.at(-1);
    }
    lengths.set(line.userData.edgeKey,{values,total});
  }
  // Keep the pattern phase across fragmented source edges in a logical group.
  // Resetting at every tiny TopoEdge can otherwise turn a dotted edge into a solid line.
  for(const run of phaseRuns){let cursor=0;for(const item of run){const length=lengths.get(item.edgeKey)?.total??0;if(!phases.has(item.edgeKey))phases.set(item.edgeKey,{offset:cursor+(item.forward?0:length),sign:item.forward?1:-1});cursor+=length;}}
  for (const line of edgeLines) {
    if (!line.material.dashed) continue;
    const starts = line.geometry.attributes.instanceStart;
    const distancesA = line.geometry.attributes.instanceDistanceStart;
    const distancesB = line.geometry.attributes.instanceDistanceEnd;
    const phase=phases.get(line.userData.edgeKey)??{offset:0,sign:1};
    const lengthsForLine=lengths.get(line.userData.edgeKey).values;
    let cumulative = 0;
    for (let i=0; i<starts.count; i++) {
      distancesA.setX(i,phase.offset+phase.sign*cumulative);
      cumulative += lengthsForLine[i];
      distancesB.setX(i,phase.offset+phase.sign*cumulative);
    }
    distancesA.needsUpdate = true; distancesB.needsUpdate = true;
  }
}
function lineMaterial(edge, selected = false) {
  const color = SKIN[edge.skinRelation].color;
  const kind = edge.convexity;
  const key = `${color}:${kind}:${selected}:${$('xray').checked}`;
  if (materialCache.has(key)) return materialCache.get(key);
  const pattern = CONVEXITY[kind].dash;
  const material = new LineMaterial({color, linewidth:selected ? 4.6 : 2.4,
    worldUnits:false, dashed:pattern.length>0, dashSize:9, gapSize:5,
    depthTest:!$('xray').checked, depthWrite:false, transparent:true, opacity:1});
  material.resolution.set(width,height);
  if (pattern.length) {
    const period = pattern.reduce((a,b)=>a+b,0);
    const intervals = [];
    let start = 0;
    pattern.forEach((length,i)=>{
      if (i%2===0) intervals.push(`(phase >= ${start.toFixed(1)} && phase <= ${(start+length).toFixed(1)})`);
      start += length;
    });
    material.onBeforeCompile = shader => {
      shader.fragmentShader = shader.fragmentShader.replace(
        /if\s*\(\s*mod\(\s*vLineDistance\s*\+\s*dashOffset,\s*dashSize\s*\+\s*gapSize\s*\)\s*>\s*dashSize\s*\)\s*discard;/,
        `float phase = mod(vLineDistance + dashOffset, ${period.toFixed(1)}); if (!(${intervals.join(' || ')})) discard;`);
    };
    material.customProgramCacheKey = ()=>`pixel-pattern-${kind}`;
  }
  materialCache.set(key,material);
  return material;
}
function clearScene() {
  for (const mesh of faceMeshes) {mesh.geometry.dispose(); mesh.material.dispose(); faceGroup.remove(mesh);}
  for (const line of edgeLines) {line.geometry.dispose(); edgeGroup.remove(line);}
  for (const material of materialCache.values()) material.dispose();
  materialCache.clear(); faceMeshes=[]; edgeLines=[];
}
function flatArray(value) { return Array.isArray(value) ? value.flat() : []; }
function compareIds(a,b) {
  if(/^\d+$/.test(a)&&/^\d+$/.test(b)){const x=BigInt(a),y=BigInt(b);return x<y?-1:x>y?1:0;}
  return a.localeCompare(b,undefined,{numeric:true});
}
function buildEquivalence() {
  const parent=new Map([...edgeMap.keys()].map(id=>[id,id]));
  const find=id=>{let root=id;while(parent.get(root)!==root)root=parent.get(root);while(parent.get(id)!==id){const next=parent.get(id);parent.set(id,root);id=next;}return root;};
  for(const [id,edge] of edgeMap)for(const other of edge.matchedSpanKeys??[]){const targetId=idOf(other);if(!parent.has(targetId))continue;const a=find(id),b=find(targetId);if(a!==b)parent.set(compareIds(a,b)<=0?b:a,compareIds(a,b)<=0?a:b);}
  representatives=new Map();equivalenceGroups=new Map();displayEdges=new Map();equivalenceConflicts=[];
  for(const id of edgeMap.keys()){const representative=find(id);representatives.set(id,representative);if(!equivalenceGroups.has(representative))equivalenceGroups.set(representative,[]);equivalenceGroups.get(representative).push(id);}
  for(const [representative,ids] of equivalenceGroups){
    ids.sort(compareIds);
    const members=ids.map(id=>edgeMap.get(id));
    const skinStates=new Set(members.map(edge=>`${skinValid(edge)}:${edge.skinRelation}`));
    const convexityStates=new Set(members.map(edge=>`${convexityValid(edge)}:${edge.convexity}`));
    const display={...edgeMap.get(representative)};
    if(skinStates.size>1||convexityStates.size>1){equivalenceConflicts.push({representative,edgeKeys:ids,skinConflict:skinStates.size>1,convexityConflict:convexityStates.size>1});continue;}
    displayEdges.set(representative,display);
  }
  phaseRuns=[];
  const dist2=(a,b)=>a&&b?a.reduce((sum,value,index)=>sum+(value-b[index])**2,0):Infinity;
  for(const face of faceMap.values())for(const wire of face.wires??[]){if(wire.ordered===false)continue;for(const group of wire.groups??wire.logicalGroups??[]){
    const run=[];
    for(const source of group.sourceUses??[]){
      const keys=[...(sourceEdgeMap.get(idOf(source.edgeId))?.segmentKeys??[])];if(source.reversed)keys.reverse();
      for(const key of keys){const originalKey=idOf(key),representative=representatives.get(originalKey);if(!representative||!displayEdges.has(representative))continue;const original=edgeMap.get(originalKey),display=displayEdges.get(representative);const originalPoints=original.samples??[],displayPoints=display.samples??[];const aligned=dist2(displayPoints[0],originalPoints[0])<=dist2(displayPoints[0],originalPoints.at(-1));run.push({edgeKey:representative,forward:aligned!==Boolean(source.reversed)});}
    }
    if(run.length)phaseRuns.push(run);
  }}
}
function equivalentIds(id) {return equivalenceGroups.get(representatives.get(idOf(id)))??[idOf(id)];}
function equivalenceHtml(id) {
  const ids=equivalentIds(id), conflict=equivalenceConflicts.find(group=>group.edgeKeys.includes(id));
  if(ids.length<2)return '';
  return `<h3>同一几何边段的原始记录</h3><div class="notice ${conflict?'warning':''}">${ids.map(value=>esc(edgeLabel(value))).join(' ↔ ')}<br>${conflict?'分析失败，此组未作为有效边段绘制。':`三维只绘制代表段 ${esc(edgeLabel(representatives.get(id)))}，原边编号和分段区间全部保留。`}</div>${conflict?edgeRows(ids.map(value=>edgeMap.get(value))):''}`;
}
function buildScene(data) {
  clearScene();
  faceMap = new Map((data.faces ?? []).map(face=>[idOf(face.faceId ?? face.id),face]));
  sourceEdgeMap=new Map((data.sourceEdges??[]).map(edge=>[idOf(edge.edgeId),edge]));
  const invalidOutput=(data.edges??[]).filter(edge=>!edge.edgeKey||!valid(edge)||!Number.isFinite(edge.firstParameter)||!Number.isFinite(edge.lastParameter));
  if(invalidOutput.length){data.success=false;data.complete=false;data.diagnostics??=[];data.diagnostics.unshift(`${invalidOutput.length} 条边段输出无效，缺少唯一业务枚举、段标识或参数区间；分析失败。`);data.invalidOutputSegments=invalidOutput;}
  edgeMap = new Map((data.edges ?? []).filter(edge=>!invalidOutput.includes(edge)).map(edge=>[idOf(edge.edgeKey),edge]));
  buildEquivalence();
  if(equivalenceConflicts.length){data.success=false;data.complete=false;data.diagnostics??=[];data.diagnostics.unshift('等价几何边段的属性不一致，分析失败；冲突段未作为有效几何绘制。');}
  const geometries = data.geometry?.faces ?? data.faceMeshes ?? [];
  for (const face of geometries) {
    const id = idOf(face.faceId ?? face.id), positions = flatArray(face.positions ?? face.vertices);
    if (positions.length < 9 || positions.length%3) continue;
    if (!faceMap.has(id)) faceMap.set(id,{faceId:id});
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));
    const indices = flatArray(face.indices ?? face.triangles);
    if (indices.length) geometry.setIndex(indices);
    geometry.computeVertexNormals();
    const material = new THREE.MeshStandardMaterial({color:'#70859c', side:THREE.DoubleSide,
      metalness:0,roughness:.87,transparent:true,opacity:Number($('opacity').value),
      polygonOffset:true,polygonOffsetFactor:1,polygonOffsetUnits:1,depthWrite:true});
    const mesh = new THREE.Mesh(geometry,material);
    mesh.userData.faceId=id; faceMeshes.push(mesh); faceGroup.add(mesh);
  }
  for (const [id,edge] of displayEdges) {
    const points = edge.samples ?? edge.previewSamples ?? [];
    const positions = [];
    if (!Array.isArray(points)) continue;
    const xyz = typeof points[0] === 'number' ? Array.from({length:Math.floor(points.length/3)},(_,i)=>points.slice(i*3,i*3+3)) : points;
    for (let i=1; i<xyz.length; i++) {
      if (xyz[i-1].length!==3 || xyz[i].length!==3 || ![...xyz[i-1],...xyz[i]].every(Number.isFinite)) continue;
      if (xyz[i].every((v,j)=>v===xyz[i-1][j])) continue;
      positions.push(...xyz[i-1],...xyz[i]);
    }
    if (!positions.length) continue;
    const geometry = new LineSegmentsGeometry(); geometry.setPositions(positions);
    const count = positions.length/6;
    geometry.setAttribute('instanceDistanceStart',new THREE.InstancedBufferAttribute(new Float32Array(count),1));
    geometry.setAttribute('instanceDistanceEnd',new THREE.InstancedBufferAttribute(new Float32Array(count),1));
    const line = new LineSegments2(geometry,lineMaterial(edge));
    line.userData.edgeKey=id;line.userData.originalSpanKeys=equivalenceGroups.get(id);line.renderOrder=3;line.frustumCulled=false;
    edgeLines.push(line);edgeGroup.add(line);
  }
  scene.updateMatrixWorld(true);
  fit();
  if (!faceMeshes.length && !edgeLines.length) setStatus(data.success===false?`边分析未完成，未输出可显示几何。\n${(data.diagnostics??[]).slice(0,3).map(explain).join('\n')||'具体原因见右侧诊断。'}`:'本结果没有可显示的三维采样或面网格。\n判定数据仍可在右侧查看。',data.success===false);
  else setStatus('');
}
function fit() {
  const box = new THREE.Box3();
  if (faceMeshes.length) box.setFromObject(faceGroup);
  else if (edgeLines.length) box.setFromObject(edgeGroup);
  if (box.isEmpty()) {target.set(0,0,0);modelRadius=1;}
  else {box.getCenter(target); modelRadius=Math.max(box.getSize(new THREE.Vector3()).length()/2,1e-9);}
  const angle = camera.fov*Math.PI/360;
  orbitRadius = modelRadius/Math.sin(angle)/Math.min(1,width/height)*1.12;
  updateCamera();
}
function fitSelection() {
  const mesh=faceMeshes.find(value=>value.userData.faceId===selectedFace);
  const line=edgeLines.find(value=>value.userData.edgeKey===representatives.get(selectedEdge));
  const object=mesh??line;if(!object)return;
  const box=new THREE.Box3().setFromObject(object);if(box.isEmpty())return;
  box.getCenter(target);
  const localRadius=Math.max(box.getSize(new THREE.Vector3()).length()/2,modelRadius*1e-5);
  orbitRadius=localRadius/Math.sin(camera.fov*Math.PI/360)/Math.min(1,width/height)*1.25;
  updateCamera();
}
function updateModelList() {
  const search = $('modelSearch').value.trim().toLocaleLowerCase();
  const shown = models.filter(model=>`${model.id} ${model.name}`.toLocaleLowerCase().includes(search));
  $('modelList').innerHTML = shown.map(model=>`<button data-model="${esc(model.id)}" class="${idOf(model.id)===idOf(currentModel?.id)?'active':''}">${esc(model.id)} · ${esc(model.name)}${model.faceCount!==undefined?`<small>${esc(model.faceCount)} 面 · ${esc(model.edgeCount??'—')} 分析边段${model.success===false||model.status==='failed'||model.complete===false?' · <span class="bad">分析失败</span>':''}</small>`:''}</button>`).join('') || '<div class="missing">没有匹配的模型。</div>';
}
function updateFaceList() {
  const search = $('faceSearch').value.trim().replace(/^f/i,'');
  const faces = [...faceMap.entries()].filter(([id,face])=>`${face.faceIndex??id}`.includes(search));
  $('faceList').innerHTML=faces.map(([id,face])=>{
    const edges=faceEdges(id);
    const domain=classificationDomain(face);
    return `<button data-face="${esc(id)}" class="${id===selectedFace?'active':''}">${esc(faceLabel(id))}<small>模型面 ID ${esc(id)} · ${edges.length} 分析边段${domain?`<br>${esc(DOMAIN_NAMES[domain]??domain)}`:''}</small></button>`;
  }).join('') || '<div class="missing">没有匹配的加工面。</div>';
}
function dashSvg(edge) {
  const dash = CONVEXITY[edge.convexity].dash;
  return `<svg class="edge-style" style="color:${SKIN[edge.skinRelation].color}"><path d="M0 7H43"${dash.length?` stroke-dasharray="${dash.join(' ')}"`:''}/></svg>`;
}
function edgeRows(edges) {
  return `<div class="edge-table">${edges.map(edge=>{
    const id=idOf(edge.edgeKey), skin=SKIN[edge.skinRelation].name, convexity=CONVEXITY[edge.convexity].name;
    return `<button class="edge-row ${id===selectedEdge?'active':''}" data-edge="${esc(id)}"><b>${esc(edgeLabel(edge))}</b><span class="edge-role">${dashSvg(edge)}${skin} · ${convexity}</span><small>${edgeSourceFaceIds(edge).map(id=>esc(faceLabel(id))).join('、')} · 参数 ${number(edge.firstParameter)} ～ ${number(edge.lastParameter)}</small></button>`;
  }).join('')}</div>`;
}
function dl(rows) { return `<dl>${rows.map(([label,value])=>`<dt>${label}</dt><dd>${value}</dd>`).join('')}</dl>`; }
function valuesHtml(value) { return `<pre class="evidence">${esc(typeof value==='string'?value:JSON.stringify(value,null,2))}</pre>`; }
function sourceUseIds(group) {
  return (group.sourceUses??group.edgeIds??group.sourceEdgeIds??[]).map(use=>typeof use==='object'?use.edgeId??use.sourceEdgeId??use.id:use).filter(value=>value!==undefined).map(idOf);
}
function wireGroupsHtml(face) {
  if(!face?.wires?.length)return '';
  return `<h3>线环与几何连续组</h3>${face.wires.map((wire,index)=>{
    const groups=wire.groups??wire.logicalGroups??[];
    return `<div class="notice ${wire.ordered===false?'warning':''}"><b>W${esc(wire.wireId??wire.id??index+1)}</b> · ${wire.ordered===false?'线环尚未可靠排序':'按原线环组织'}${wire.reason?`<br>${esc(explain(wire.reason))}`:''}</div>${groups.map((group,groupIndex)=>`<div class="evidence"><b>G${esc(group.groupId??group.id??groupIndex+1)}</b> · ${sourceUseIds(group).map(id=>'E'+esc(id)).join(' → ')||'源边见记录'}${group.reason?`<br>${esc(explain(group.reason))}`:''}</div>`).join('')}`;
  }).join('')}<details><summary>完整线环记录</summary>${valuesHtml(face.wires)}</details>`;
}
function edgeGroupsHtml(edgeId,faceIds) {
  const sourceId=idOf(edgeMap.get(edgeId)?.edgeId);
  const memberships=[];
  for(const faceId of faceIds){const face=faceMap.get(faceId);for(const [wireIndex,wire] of (face?.wires??[]).entries())for(const [groupIndex,group] of (wire.groups??wire.logicalGroups??[]).entries()){
    if(sourceUseIds(group).includes(sourceId))memberships.push(`<div class="evidence">${esc(faceLabel(faceId))}（模型面 ID ${esc(faceId)}） · W${esc(wire.wireId??wire.id??wireIndex+1)} · G${esc(group.groupId??group.id??groupIndex+1)}<br>源边：${sourceUseIds(group).map(id=>'E'+esc(id)).join(' → ')}${wire.ordered===false?`<br><span class="bad">线环未排序：${esc(explain(wire.reason??'未提供原因'))}</span>`:''}${group.reason?`<br>${esc(explain(group.reason))}`:''}</div>`);
  }}
  return memberships.length?`<h3>所属连续组</h3>${memberships.join('')}`:'';
}
function orientationHtml(data) {
  if(!data){const records=[...edgeMap.values()].filter(edge=>edge.orientationMethod);if(!records.length)return '';return `<h3>面分析朝向</h3>${dl([['有朝向记录的段',String(records.length)],['应用朝向修正的段',String(records.filter(edge=>edge.orientationRepaired).length)]])}<p class="muted">每段分别记录锚定面、翻转的分析面和方法。点选边段查看；原始CAD朝向独立保留。</p>`;}
  if(data.orientationRepaired===undefined)return '';
  const flipped=data.orientationFlippedFaceIds??[];
  return `<h3>面分析朝向</h3>${dl([
    ['应用朝向修正',yesNo(data.orientationRepaired)],
    ['锚定面',data.orientationAnchorFaceId!==undefined?`${esc(faceLabel(data.orientationAnchorFaceId))}（模型面 ID ${esc(data.orientationAnchorFaceId)}）`:'未记录'],
    ['翻转分析朝向',flipped.length?flipped.map(id=>`${esc(faceLabel(id))}（${esc(id)}）`).join('、'):'无'],
    ['统一方法',esc(explain(data.orientationMethod??'未记录'))]
  ])}<p class="muted">朝向统一用于本次边关系分析，原始几何独立保留。</p>`;
}
function excludedHtml(faceId) {
  const entries=(currentData?.excludedEdges??[]).filter(edge=>!faceId||(edge.faceIds??[]).map(idOf).includes(idOf(faceId)));
  if(!entries.length)return '';
  return `<h3>非物理边 · Excluded（${entries.length}）</h3><p class="muted">参数接缝与退化边不作为切割边段，不赋予凹凸业务枚举。</p>${entries.map(edge=>`<div class="evidence"><b>原边 E${esc(edge.edgeId)}</b> · ${(edge.faceIds??[]).map(id=>esc(faceLabel(id))).join('、')}<br>${esc(explain(edge.reason))}</div>`).join('')}<details><summary>排除记录原码</summary>${valuesHtml(entries)}</details>`;
}
function sourceSegmentsHtml(edge) {
  const source=sourceEdgeMap.get(idOf(edge.edgeId));if(!source)return '';
  const segments=(source.segmentKeys??[]).map(id=>edgeMap.get(idOf(id))).filter(Boolean);
  return `<h3>原边 E${esc(edge.edgeId)} 的分段</h3>${dl([['原曲线参数域',`${number(source.firstParameter)} ～ ${number(source.lastParameter)}`],['分析边段',String(segments.length)]])}${edgeRows(segments)}`;
}
function faceDetails(id) {
  const face=faceMap.get(id), edges=faceEdges(id);
  return `<h2>原加工面 ${esc(faceLabel(id))}</h2>${stageMessagesHtml()}${dl([
    ['Classify 面序号',esc(faceLabel(id))],['稳定模型面 ID',esc(id)],['原边记录',String(face.edgeIds?.length??0)],['分析边段',String(edges.length)],['边界环数',String(face.wires?.length??0)]
  ])}${faceSourceHtml(face)}${classificationReviewHtml()}<h3>此面的分析边段</h3>${edgeRows(edges)}${wireGroupsHtml(face)}${excludedHtml(id)}${orientationHtml()}${face.diagnostics?.length?`<h3>本面诊断</h3>${diagnosticsHtml(face.diagnostics)}`:''}${artifactChecksHtml()}`;
}
function skinCriterionHtml(edge,tolerance) {
  if(edge.skinRelation==='PartialOnSkin'&&edge.maxSkinDistanceMm<=tolerance)return `<div class="notice"><b>测距接近与连续皮支撑是两种判据</b><br>本段采样最大距皮 ${distance(edge.maxSkinDistanceMm)}，在 ${distance(tolerance)} 测距阈值内，只说明距离接近。该子段没有连续皮支撑，按端点接触记为“部分在皮上”。</div>`;
  return '';
}
function sampleDistanceHtml(edge) {
  if(!edge.classificationSamples?.length)return '';
  return `<details><summary>测距采样 · 阈值内仅表示距离接近</summary><p class="muted">下表布尔值来自 distance ≤ 测距阈值，不代表该点或子段的语义皮归属。</p><table class="sample-table"><thead><tr><th>参数</th><th>距皮 / mm</th><th>测距阈值内</th></tr></thead><tbody>${edge.classificationSamples.map(sample=>`<tr><td>${number(sample.parameter)}</td><td>${number(sample.skinDistanceMm)}</td><td>${yesNo(sample.onSkin)}</td></tr>`).join('')}</tbody></table></details>`;
}
function edgeDetails(key) {
  const edge=edgeMap.get(key), skin=SKIN[edge.skinRelation], convexity=CONVEXITY[edge.convexity];
  const reason=edgeReason(edge),faceIds=edgeFaceIds(edge);
  const skinTolerance=currentData.options?.skinDistanceToleranceMm??.001;
  const sourceApproximate=Boolean(edge.sourceSkinMembership)&&edge.maxSkinDistanceMm>skinTolerance;
  const rows=[
    ['原模型边 ID',`E${esc(edge.edgeId)}`],['段编号',`S${esc(edge.segmentId)}`],['段标识',esc(edge.edgeKey)],
    ['本段参数区间',`${number(edge.firstParameter)} ～ ${number(edge.lastParameter)}`],
    ['源加工面',edgeSourceFaceIds(edge).map(id=>`<button data-face="${esc(id)}" title="稳定模型面 ID ${esc(id)}">${esc(faceLabel(id))}</button>`).join(' ')||'未记录'],
    ['关联加工面',faceIds.map(id=>`<button data-face="${esc(id)}" title="稳定模型面 ID ${esc(id)}">${esc(faceLabel(id))}</button>`).join(' ')||'未记录'],
    ['皮关系',`<span class="badge"><i class="swatch" style="background:${skin.color}"></i>${skin.name}</span>`],
    ['凹凸性',`<span class="badge">${dashSvg(edge)}${convexity.name} · ${convexity.pattern}</span>`],
    ['起点测距阈值内',yesNo(edge.classificationSamples?.[0]?.onSkin)],['终点测距阈值内',yesNo(edge.classificationSamples?.at(-1)?.onSkin)],
    ['采样最大轮廓偏差',distance(edge.maxSkinDistanceMm)],['测距接触阈值',distance(skinTolerance)]
  ];
  rows.push(['皮归属依据',edge.sourceSkinMembership?'来源皮面归属':edge.skinReason==='whole-source-edge-within-finite-skin-distance-tolerance'?'完整原边的测距容差归属':edge.skinReason==='finite-native-support-span-on-skin'?'该子段的有限皮支撑':'端点接触及有限皮支撑关系']);
  if(edge.minSkinDistanceMm!==undefined)rows.push(['采样最小轮廓偏差',distance(edge.minSkinDistanceMm)]);
  if(edge.convexity!=='Free'&&Number.isFinite(edge.minSignedAngleRadians)&&Number.isFinite(edge.maxSignedAngleRadians))rows.push(['有向角范围',`${number(edge.minSignedAngleRadians*180/Math.PI)}° ～ ${number(edge.maxSignedAngleRadians*180/Math.PI)}°`]);
  if(edge.classificationSamples?.length)rows.push(['距离采样数',String(edge.classificationSamples.length)]);
  if(edge.matchedSpanKeys?.length)rows.push(['匹配的边段',edge.matchedSpanKeys.map(value=>`<button data-edge="${esc(value)}">${esc(edgeLabel(value))}</button>`).join(' ')]);
  if(edge.adjacencyMethod)rows.push(['邻接依据',esc(explain(edge.adjacencyMethod))]);
  if(edge.maxAdjacencyGapMm!==undefined)rows.push(['匹配最大缝隙',distance(edge.maxAdjacencyGapMm)]);
  if(edge.adjacencyReason)rows.push(['邻接说明',esc(explain(edge.adjacencyReason))]);
  if(edge.sourceSkinFaceIds?.length)rows.push(['来源皮面 ID',edge.sourceSkinFaceIds.map(id=>esc(id)).join('、')]);
  return `<h2>边段 ${esc(edgeLabel(edge))}</h2>${stageMessagesHtml()}${skinCriterionHtml(edge,skinTolerance)}${dl(rows)}${reason?`<h3>判定依据</h3><div class="notice">${esc(reason)}</div>`:''}${sourceApproximate?`<div class="notice">按来源皮面共边归属判为皮上。原始边界为近似表示，采样最大轮廓偏差 ${distance(edge.maxSkinDistanceMm)}，超过测距接触阈值 ${distance(skinTolerance)}。归属与几何精度分别记录，原曲线保留。</div>`:''}<p class="muted">轮廓偏差与二面角为采样记录，不代表连续全域误差证书。</p>${sourceSegmentsHtml(edge)}${equivalenceHtml(key)}${edgeGroupsHtml(key,edgeSourceFaceIds(edge))}${orientationHtml(edge)}${sampleDistanceHtml(edge)}<details><summary>完整边段记录</summary>${valuesHtml(edge)}</details>${artifactChecksHtml()}`;
}
function updateDetails() {
  if(selectedEdge&&edgeMap.has(selectedEdge))$('details').innerHTML=edgeDetails(selectedEdge);
  else if(selectedFace&&faceMap.has(selectedFace))$('details').innerHTML=faceDetails(selectedFace);
  else if(currentData){
    $('details').innerHTML=`<h2>整件加工面</h2>${stageMessagesHtml()}<p>点选一个加工面或边段，检查原边、分段区间及两个枚举。</p>${dl([['加工面',String(faceMap.size)],['原边记录',String(sourceEdgeMap.size)],['分析边段',String(edgeMap.size)],['显示边段',String(edgeLines.length)],['非物理边',String(currentData.excludedEdges?.length??0)]])}${classificationReviewHtml(true)}<div class="notice">邻接仅统计加工面。只有一个加工面使用的边段记为自由边。状态改变处使用原曲线参数分段，每段唯一着色和线型。</div>${currentData.diagnostics?.length?`<h3>分析诊断</h3>${diagnosticsHtml(currentData.diagnostics,currentData.success===false)}`:''}${currentData.invalidOutputSegments?.length?`<details open><summary>无效输出记录</summary>${valuesHtml(currentData.invalidOutputSegments)}</details>`:''}${orientationHtml()}${excludedHtml()}${artifactChecksHtml()}<h3>全部分析边段</h3>${edgeRows([...edgeMap.values()])}`;
  }
  $('selectionCaption').textContent=selectedEdge?`${edgeLabel(selectedEdge)} · ${selectedFace?faceLabel(selectedFace)+' · ':''}${SKIN[edgeMap.get(selectedEdge).skinRelation].name} / ${CONVEXITY[edgeMap.get(selectedEdge).convexity].name}`:selectedFace?`原加工面 ${faceLabel(selectedFace)} · 模型面 ID ${selectedFace} · ${faceEdges(selectedFace).length} 分析边段`:'完整模型 · 皮关系看颜色，凹凸性看线型；按原曲线参数区间检查分段。';
}
function applySelection() {
  const baseOpacity=Number($('opacity').value);
  for (const mesh of faceMeshes) {
    const selected=mesh.userData.faceId===selectedFace;
    mesh.material.color.set(selected?'#aad0f3':'#70859c');
    mesh.material.opacity=selected?Math.min(baseOpacity,.65):baseOpacity;
  }
  for (const line of edgeLines) {
    const edge=displayEdges.get(line.userData.edgeKey), selected=line.userData.edgeKey===representatives.get(selectedEdge);
    line.material=lineMaterial(edge,selected);
    line.renderOrder=selected?4:3;
  }
  $('fitSelection').disabled=!selectedFace&&!selectedEdge;
  updateFaceList();updateDetails();requestDraw();
}
function setSelection(faceId='',edgeId='',writeHash=true) {
  const previousSelection=`${selectedFace}:${selectedEdge}`;
  selectedFace=faceMap.has(idOf(faceId))?idOf(faceId):'';
  selectedEdge=edgeMap.has(idOf(edgeId))?idOf(edgeId):'';
  if(selectedEdge&&!selectedFace)selectedFace=edgeSourceFaceIds(edgeMap.get(selectedEdge))[0]??'';
  applySelection();
  if(previousSelection!==`${selectedFace}:${selectedEdge}`)$('details').scrollTop=0;
  if(writeHash)updateHash();
}
function updateHash() {
  const hash=new URLSearchParams();
  if(currentModel)hash.set('case',idOf(currentModel.id));
  if(selectedFace)hash.set('face',selectedFace);
  if(selectedEdge)hash.set('edge',selectedEdge);
  history.replaceState(null,'',`${location.pathname}${location.search}#${hash}`);
}
async function fetchJson(url) {
  const response=await fetch(url,{cache:'no-store'});
  if(!response.ok)throw Error(`${response.status}：${decodeURI(new URL(url,location.href).pathname)}`);
  return response.json();
}
async function loadModel(model,selection={}) {
  const revision=++loadRevision;
  window.__edgeAnalysisViewer={loaded:false,modelId:idOf(model.id)};
  currentModel=model;updateModelList();
  setStatus('正在读取加工面与边关系…');
  $('modelName').textContent=model.name??`模型 ${model.id}`;
  try{
    const data=model.inlineData??await fetchJson(model.dataUrl);
    if(revision!==loadRevision)return;
    if(!Array.isArray(data.edges)){if(data.success===false)data.edges=[];else throw Error('结果缺少 edges 数组。');}
    currentData=data;selectedFace='';selectedEdge='';
    $('details').scrollTop=0;
    buildScene(data);
    const modelWarnings=stageMessages(data);
    $('summary').textContent=`${models.length} 个模型 · 当前 ${faceMap.size} 面 · 原边记录 ${sourceEdgeMap.size} / 分析边段 ${edgeMap.size} / 显示段 ${edgeLines.length}${data.success===false||data.complete===false?' · 分析失败':''}${data.artifacts?.stepRoundTrip?.passed===false?' · STEP未通过':''}`;
    $('viewSubtitle').textContent=`完整加工面 · ${faceMap.size} 面 · ${sourceEdgeMap.size} 原边 / ${edgeMap.size} 分段 / ${edgeLines.length} 显示段${modelWarnings.length?' · 有检查信息':''}`;
    const url=new URL(model.dataUrl,location.href);
    const artifacts=data.artifacts??{};
    const defaultArtifact=ext=>{const value=new URL(url);value.pathname=value.pathname.replace(/\.json$/i,ext);value.search='';return value.href;};
    const artifactUrl=(value,fallback)=>{if(!value)return fallback;try{const resolved=new URL(value,url);return ['http:','https:'].includes(resolved.protocol)?resolved.href:fallback;}catch{return fallback;}};
    const brep=artifactUrl(artifacts.brep??data.brepUrl,defaultArtifact('.brep')), step=artifactUrl(artifacts.step??data.stepUrl,defaultArtifact('.step'));
    $('modelInfo').innerHTML=`<div>${faceMap.size} 原加工面<br>原边记录 ${sourceEdgeMap.size} · 分析边段 ${edgeMap.size}<br>显示段 ${edgeLines.length} · 非物理边 ${data.excludedEdges?.length??0}</div><div class="downloads"><a href="${esc(brep)}" download>整件 BREP</a><a href="${esc(step)}" download>整件 STEP</a><a href="${esc(model.dataUrl)}" download>判定数据</a></div>`;
    setSelection(selection.face??'',selection.edge??'');
    window.__edgeAnalysisViewer={loaded:true,modelId:idOf(model.id),faceCount:faceMap.size,sourceEdgeCount:sourceEdgeMap.size,segmentCount:edgeMap.size,renderedSegmentCount:edgeLines.length,excludedEdgeCount:data.excludedEdges?.length??0,invalidOutputSegmentCount:data.invalidOutputSegments?.length??0,
      analysisSuccess:data.success!==false,complete:data.complete!==false,displayConflicts:equivalenceConflicts,stageMessages:modelWarnings,
      selectFace:id=>setSelection(id),selectEdge:id=>setSelection('',id),getSelection:()=>({face:selectedFace,faceLabel:faceLabel(selectedFace),edge:selectedEdge,representative:representatives.get(selectedEdge)}),fitSelection,
      getCamera:()=>({radius:orbitRadius,modelRadius}),getEdgeStyles:()=>edgeLines.map(line=>{const edge=displayEdges.get(line.userData.edgeKey);return {edgeKey:line.userData.edgeKey,edgeId:edge.edgeId,segmentId:edge.segmentId,originalSpanKeys:line.userData.originalSpanKeys,width:line.material.linewidth,worldUnits:line.material.worldUnits,color:'#'+line.material.color.getHexString(),skinRelation:edge.skinRelation,convexity:edge.convexity,pattern:CONVEXITY[edge.convexity].dash};}),
      projectPoint:point=>{const p=new THREE.Vector3(...point).project(camera);return {x:(p.x+1)*width/2,y:(1-p.y)*height/2,z:p.z};}};
  }catch(error){
    if(revision!==loadRevision)return;
    clearScene();currentData=null;faceMap.clear();edgeMap.clear();
    setStatus(`无法读取此模型\n${error.message}`,true);
    $('summary').textContent='模型读取失败';$('faceList').innerHTML='';$('details').innerHTML=`<h2>读取失败</h2><p class="bad">${esc(error.message)}</p>`;
    console.error(error);
    window.__edgeAnalysisViewer={loaded:false,modelId:idOf(model.id),error:error.message};
  }
}
function hitAt(event) {
  const rect=canvas.getBoundingClientRect();
  mouse.set((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1);
  raycaster.setFromCamera(mouse,camera);
  const faceHit=raycaster.intersectObjects(faceMeshes,false)[0];
  const edgeHits=raycaster.intersectObjects(edgeLines,false);
  const edgeHit=$('xray').checked?edgeHits[0]:edgeHits.find(hit=>!faceHit||hit.distance<=faceHit.distance+modelRadius*.002);
  if(edgeHit)return {kind:'edge',id:edgeHit.object.userData.edgeKey};
  return faceHit?{kind:'face',id:faceHit.object.userData.faceId}:null;
}
canvas.addEventListener('pointerdown',event=>{
  if(event.button!==0&&event.button!==2)return;
  pointer={id:event.pointerId,button:event.button,x:event.clientX,y:event.clientY,startX:event.clientX,startY:event.clientY,dragged:false};
  canvas.setPointerCapture(event.pointerId);$('hover').hidden=true;
});
canvas.addEventListener('pointermove',event=>{
  if(pointer){
    const dx=event.clientX-pointer.x,dy=event.clientY-pointer.y;
    if(Math.hypot(event.clientX-pointer.startX,event.clientY-pointer.startY)>4)pointer.dragged=true;
    pointer.x=event.clientX;pointer.y=event.clientY;
    if(!pointer.dragged)return;
    if(pointer.button===0){theta-=dx*.006;phi-=dy*.006;}
    else{
      const scale=2*orbitRadius*Math.tan(camera.fov*Math.PI/360)/height;
      const right=new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld,0);
      const up=new THREE.Vector3().setFromMatrixColumn(camera.matrixWorld,1);
      target.addScaledVector(right,-dx*scale).addScaledVector(up,dy*scale);
    }
    updateCamera();return;
  }
  if(hoverPending||!currentData)return;
  hoverPending=true;
  requestAnimationFrame(()=>{
    hoverPending=false;
    const hit=hitAt(event);
    if(!hit){$('hover').hidden=true;canvas.style.cursor='grab';return;}
    canvas.style.cursor='pointer';
    const rect=canvas.getBoundingClientRect();
    $('hover').style.left=`${Math.min(event.clientX-rect.left+13,Math.max(0,width-230))}px`;
    $('hover').style.top=`${Math.max(0,event.clientY-rect.top-40)}px`;
    if(hit.kind==='edge'){
      const edge=edgeMap.get(hit.id);
      $('hover').textContent=`${edgeLabel(edge)} · ${edgeFaceIds(edge).map(faceLabel).join('、')}\n${SKIN[edge.skinRelation].name} · ${CONVEXITY[edge.convexity].name}`;
    }else $('hover').textContent=`原加工面 ${faceLabel(hit.id)} · 模型面 ID ${hit.id}`;
    $('hover').hidden=false;
  });
});
canvas.addEventListener('pointerup',event=>{
  if(!pointer||pointer.id!==event.pointerId)return;
  const click=!pointer.dragged&&pointer.button===0;pointer=null;
  if(canvas.hasPointerCapture(event.pointerId))canvas.releasePointerCapture(event.pointerId);
  if(click){const hit=hitAt(event);if(hit?.kind==='edge')setSelection('',hit.id);else if(hit)setSelection(hit.id);}
});
canvas.addEventListener('pointercancel',()=>{pointer=null;});
canvas.addEventListener('pointerleave',()=>{$('hover').hidden=true;});
canvas.addEventListener('contextmenu',event=>event.preventDefault());
canvas.addEventListener('wheel',event=>{event.preventDefault();orbitRadius=Math.max(modelRadius*1e-4,Math.min(modelRadius*1e5,orbitRadius*Math.exp(event.deltaY*.001)));updateCamera();},{passive:false});
$('modelSearch').addEventListener('input',updateModelList);
$('faceSearch').addEventListener('input',updateFaceList);
$('modelList').addEventListener('click',event=>{const row=event.target.closest('[data-model]');if(row){const model=models.find(value=>idOf(value.id)===row.dataset.model);if(model)loadModel(model);}});
$('faceList').addEventListener('click',event=>{const row=event.target.closest('[data-face]');if(row)setSelection(row.dataset.face);});
$('details').addEventListener('click',event=>{const edge=event.target.closest('[data-edge]'),face=event.target.closest('[data-face]');if(edge)setSelection(selectedFace,edge.dataset.edge);else if(face)setSelection(face.dataset.face);});
$('clearSelection').addEventListener('click',()=>setSelection());
$('fit').addEventListener('click',fit);
$('fitSelection').addEventListener('click',fitSelection);
$('iso').addEventListener('click',()=>{theta=.8;phi=1.08;updateCamera();});
for(const button of document.querySelectorAll('[data-axis]'))button.addEventListener('click',()=>{const axis=button.dataset.axis;theta=axis==='y'?Math.PI/2:0;phi=axis==='z'?.015:Math.PI/2;updateCamera();});
$('opacity').addEventListener('input',applySelection);
$('xray').addEventListener('change',applySelection);
new ResizeObserver(resize).observe($('viewport'));
window.addEventListener('hashchange',()=>{
  const hash=new URLSearchParams(location.hash.slice(1)),model=models.find(value=>idOf(value.id)===hash.get('case'));
  if(model&&idOf(model.id)!==idOf(currentModel?.id))loadModel(model,{face:hash.get('face'),edge:hash.get('edge')});
  else setSelection(hash.get('face')??'',hash.get('edge')??'',false);
});

async function start() {
  try{
    if(query.has('data')){
      const url=new URL(query.get('data'),location.href).href;
      const data=await fetchJson(url);
      models=[{id:'1',name:data.name??decodeURI(url.split('/').at(-1)).replace(/\.json$/i,''),dataUrl:url,inlineData:data}];
    }else{
      let catalog=null,catalogUrl=null;
      for(const candidate of query.has('catalog')?[query.get('catalog')]:['manifest.json','index.json']){
        try{catalog=await fetchJson(candidate);catalogUrl=new URL(candidate,location.href);break;}catch{}
      }
      if(!catalog)throw Error('没有可读取的模型目录。请先生成 EdgeAnalysis 结果，或使用 ?data=文件.json 打开单个结果。');
      const entries=Array.isArray(catalog)?catalog:catalog.models??catalog.cases??[];
      models=entries.map((entry,i)=>({...entry,id:entry.id??i+1,name:entry.name??entry.file??`模型 ${i+1}`,dataUrl:new URL(entry.dataUrl??entry.url??entry.file,catalogUrl).href}));
    }
    updateModelList();
    if(!models.length){$('summary').textContent='尚无 EdgeAnalysis 模型';setStatus('尚无边分析结果。\n生成 EdgeAnalysis 的整件 JSON 后，刷新此页即可查看。');return;}
    const hash=new URLSearchParams(location.hash.slice(1));
    const model=models.find(value=>idOf(value.id)===hash.get('case'))??models[0];
    await loadModel(model,{face:hash.get('face'),edge:hash.get('edge')});
  }catch(error){$('summary').textContent='尚未载入模型';setStatus(error.message,true);}
}
resize();start();
