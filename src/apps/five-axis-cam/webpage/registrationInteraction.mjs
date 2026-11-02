import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { activeTool, currentDraft } from './workspaceModel.mjs';
import { selectedPlacement, registrationPickOrder } from './registrationActions.mjs';
import { registrationReferenceCandidate, resolveRegistrationCandidate } from './registrationSnapping.mjs';

const alive = (context, view) => !view.disposed && context.isCurrentProject?.() !== false;
const selectionKey = view => JSON.stringify([view.scene?.domain?.expected, selectedPlacement(view)?.id, view.scene?.model?.geometryRef]);
const enabled = view => view.fiveAxisRegistrationOpen && ['rough', 'fine'].includes(activeTool(view)) && selectedPlacement(view);
const removeGroup = group => { group?.removeFromParent(); group?.traverse(o => { o.geometry?.dispose(); o.material?.dispose(); }); };
const labels = { origin:'原点', axisX:'X 轴方向点', plane:'XY 平面内点', model1:'P1', model2:'P2', model3:'P3' };

function prompt(view, state) {
  const pick = view.fiveAxisRegistrationPick;
  let text = '基准已选齐 · 确认坐标轴后点击确定';
  if (pick) {
    const step = registrationPickOrder(pick.draft, pick.tool).indexOf(pick.prefix) + 1;
    const target = pick.mode === 'plane' ? 'XY 平面' : pick.mode === 'line' ? (pick.prefix === 'axisX' ? 'X 轴直线' : 'XY 平面内另一条直线') : labels[pick.prefix];
    text = `${step}/3 · 选择${target}`;
    if (view.pending) text += ' · 正在拾取…';
    else if (state.error || state.mapError) text += ` · ${state.error || state.mapError}`;
    else if (state.hover) text += ` · 已吸附${({ endpoint: '端点', midpoint: '中点', center: '圆心', line: '直线', plane: '平面', point: '面上点' })[state.hover.query.mode]}`;
    else if (state.latest) text += ' · 识别中…';
  }
  if (!selectedPlacement(view)) text = '请先导入工件';
  state.prompt.textContent = text;
  state.prompt.dataset.state = state.error ? 'invalid' : state.hover ? 'ready' : 'waiting';
}

function draw(view, state) {
  const source = view.viewport.sceneObjects.get(selectedPlacement(view)?.id);
  const draft=currentDraft(view),revision=draft.registrationEditRevision;
  if(state.drawSource===source && state.drawRevision===revision && state.drawHover===state.hover && state.drawResults===draft.referenceResults && state.drawFeedback===draft.referenceFaces && state.drawFaces===state.faces && state.drawFace===state.previewFace && state.drawError===state.error && state.group)return;
  state.drawSource=source;state.drawRevision=revision;state.drawHover=state.hover;state.drawResults=draft.referenceResults;state.drawFeedback=draft.referenceFaces;state.drawFaces=state.faces;state.drawFace=state.previewFace;state.drawError=state.error;
  removeGroup(state.group);
  if (!source) { state.group = null; return; }
  // Attach to the source mesh so View transforms and coordinate conventions agree.
  const group = state.group = new THREE.Group(); group.name = 'registration-reference-feedback';
  source.add(group);
  if(!source.geometry.boundingBox)source.geometry.computeBoundingBox();
  const extent = source.geometry.boundingBox.getSize(new THREE.Vector3()).length();
  const length = Math.max(extent * .12, 1), radius = Math.max(extent * .0018, .015);
  const point = (p, color) => {
    const geometry = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...p)]);
    const material = new THREE.PointsMaterial({color, size:11, sizeAttenuation:false, depthTest:false, depthWrite:false});
    material.onBeforeCompile = shader => { shader.fragmentShader = shader.fragmentShader.replace('#include <clipping_planes_fragment>', '#include <clipping_planes_fragment>\nif(length(gl_PointCoord-vec2(.5))>.5)discard;'); };
    const marker = new THREE.Points(geometry, material); marker.renderOrder=65; group.add(marker);
  };
  const arrow = (p, direction, color) => {
    const d = new THREE.Vector3(...direction); if (d.lengthSq()<1e-12) return;
    const item = new THREE.ArrowHelper(d.normalize(), new THREE.Vector3(...p), length, color, length*.28, length*.15);
    // ArrowHelper's geometry is shared; only dispose our own clones.
    item.traverse(o => { if(o.geometry)o.geometry=o.geometry.clone(); if(o.material){o.material.depthTest=false;o.material.depthWrite=false;}o.renderOrder=64; });
    group.add(item);
  };
  const face = (result, color, hover=false, status='accepted') => {
      const geometry=source.geometry, positions=geometry.getAttribute('position'), vertices=[];
      for(const range of result.triangleRanges || []) for(let n=Number(range.first)*3;n<(Number(range.first)+Number(range.count))*3;n++) {
        const i=geometry.index?geometry.index.getX(n):n;
        if(i>=positions.count)continue;
        vertices.push(positions.getX(i),positions.getY(i),positions.getZ(i));
      }
      if(vertices.length) {
        const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(vertices,3));
        const mesh=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({color,transparent:true,opacity:hover ? .35 : .45,side:THREE.DoubleSide,depthWrite:false,polygonOffset:true,polygonOffsetFactor:-2,polygonOffsetUnits:-2}));
        mesh.userData.registrationFace=result.faceId;mesh.userData.registrationHover=hover;mesh.userData.registrationStatus=status;mesh.renderOrder=hover?66:60;group.add(mesh);
      }
  };
  const feature = (result, color, prefix, hover=false) => {
    if (!result?.point) return;
    const mode=result.query.mode;
    if(mode==='plane' && !hover && !draft.referenceFaces?.[prefix])face(result,color);
    if(mode==='line' && result.lineEnds?.length===2) {
      const [a,b]=result.lineEnds.map(p=>new THREE.Vector3(...p)),delta=b.clone().sub(a);
      const line=new THREE.Mesh(new THREE.CylinderGeometry(radius,radius,delta.length(),8),new THREE.MeshBasicMaterial({color,depthTest:false,depthWrite:false}));
      line.position.copy(a).add(b).multiplyScalar(.5);line.quaternion.setFromUnitVectors(new THREE.Vector3(0,1,0),delta.normalize());
      line.renderOrder=62;line.userData.registrationEdge=result.edgeId;group.add(line);
    }
    if(['line','plane'].includes(mode)) {
      const draft=currentDraft(view), direction=hover?result.direction:[...'XYZ'].map(axis=>Number(draft[`${prefix}${axis}`]));
      arrow(result.point,direction,color);
    } else point(result.point,color);
  };
  for(const [prefix,result] of Object.entries(currentDraft(view).referenceResults || {})) feature(result,0x3268ed,prefix);
  const selectedFaces=new Map();
  for(const feedback of Object.values(draft.referenceFaces || {})) {
    const selected=faceAt(state,feedback.triangleIndex);
    if(selected)selectedFaces.set(selected.faceId,{selected,status:feedback.status});
  }
  for(const {selected,status} of selectedFaces.values())face(selected,status==='rejected'?0xd85a4e:status==='pending'?0x08b5c8:0x3268ed,false,status);
  // A face under the pointer is feedback in every picking mode, including
  // three-point registration. It does not turn that point into a plane input.
  if(state.previewFace)face(state.previewFace,state.error?0xd85a4e:0x08b5c8,true);
  if(state.hover) feature(state.hover,0x08b5c8,'',true);
  group.traverse(o=>{o.raycast=()=>{};});
}

export function disposeRegistrationInteraction(view) {
  const state=view.fiveRegistrationInteraction;if(!state)return;
  clearTimeout(state.timer);state.latest=null;state.hover=null;state.dead=true;
  removeGroup(state.group);state.prompt.remove();
  view.fiveRegistrationInteraction=null;
  state.viewport.setHoverEnabled(false);
}

function faceAt(state,triangle) {
  return state.faces?.find(face=>triangle>=face.first && triangle<face.first+face.count) || null;
}
function updatePreviewFace(state) {
  state.previewFace=faceAt(state,Number(state.latest?.request.query.triangleIndex));
}

async function loadReferenceFaces(context,view,state,key) {
  const placement=selectedPlacement(view);
  try {
    const result=await context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',{
      expected:view.scene.domain.expected,placementId:placement.id,setupId:placement.setupId,
    });
    if(!alive(context,view)||state.dead||state.key!==key||selectionKey(view)!==key)return;
    const ranges=new Map();
    for(const face of result.faces){if(!ranges.has(face.faceId))ranges.set(face.faceId,[]);ranges.get(face.faceId).push(face);}
    state.faces=result.faces.map(face=>({...face,first:Number(face.first),count:Number(face.count),triangleRanges:ranges.get(face.faceId)}));
    updatePreviewFace(state);draw(view,state);prompt(view,state);
  } catch(error) {
    if(!alive(context,view)||state.dead||state.key!==key||selectionKey(view)!==key)return;
    state.mapError='面反馈加载失败';prompt(view,state);
  }
}

export function syncRegistrationInteraction(context,view) {
  const viewport=view.viewport;
  if(!viewport || !enabled(view)) { disposeRegistrationInteraction(view);return; }
  let state=view.fiveRegistrationInteraction;
  if(state?.viewport!==viewport) {disposeRegistrationInteraction(view);state=null;}
  if(!state) {
    const element=document.createElement('div');element.className='five-reference-prompt';element.setAttribute('role','status');
    viewport.root.appendChild(element);
    state=view.fiveRegistrationInteraction={viewport,prompt:element};
  }
  const key=selectionKey(view),pick=view.fiveAxisRegistrationPick;
  viewport.setHoverEnabled(!!pick);
  const geometryChanged=state.key!==key;
  if(state.pick!==pick || state.key!==key) {
    clearTimeout(state.timer);state.pick=pick;state.key=key;state.latest=null;state.candidate=null;state.hover=null;state.previewFace=null;state.error='';
  }
  if(geometryChanged){state.faces=null;state.mapError='';void loadReferenceFaces(context,view,state,key);}
  prompt(view,state);draw(view,state);
}

function hoverError(error, mode) {
  const message=error?.message || '';
  if(/REQUIRES_PLANE/.test(message))return '此面不是平面';
  if(/AMBIGUOUS/.test(message))return '基准不唯一，请移开交点';
  if(/NOT_FOUND|TOO_FAR/.test(message))return ({line:'请靠近直线边',endpoint:'请靠近端点',center:'请靠近圆边',midpoint:'请靠近曲线边'})[mode] || '此处没有可选基准';
  return '无法拾取，请重新选择';
}

async function resolveLatest(context,view,state) {
  if(state.busy||!state.latest||state.dead)return;
  const candidate=state.latest;state.busy=true;
  const valid=()=>alive(context,view)&&!state.dead&&enabled(view)&&state.latest===candidate
    &&view.fiveAxisRegistrationPick===candidate.pick&&selectionKey(view)===candidate.key;
  try {
    const result=await resolveRegistrationCandidate(context,candidate);
    if(valid()){state.hover=result;state.error='';draw(view,state);prompt(view,state);}
  } catch(error) {if(valid()){state.hover=null;state.error=hoverError(error,candidate.pick.mode);draw(view,state);prompt(view,state);}}
  finally {
    state.busy=false;
    if(!state.dead&&state.latest&&state.latest!==candidate)state.timer=setTimeout(()=>resolveLatest(context,view,state),35);
  }
}

export function handleRegistrationHover(context,view,userData,hit,event,_hits,pointer) {
  const state=view.fiveRegistrationInteraction,pick=view.fiveAxisRegistrationPick;
  if(!state||!pick||!enabled(view))return;
  clearTimeout(state.timer);
  const candidate=!view.pending && pointer?.pointerActive !== false && event?.type==='pointermove' && !event.buttons
    ? registrationReferenceCandidate(view,hit,event) : null;
  if(!candidate) {
    state.latest=null;state.hover=null;state.previewFace=null;state.error='';draw(view,state);prompt(view,state);return;
  }
  if(state.latest===candidate)return;
  candidate.key=selectionKey(view);state.latest=candidate;
  state.hover=null;state.error='';updatePreviewFace(state);draw(view,state);prompt(view,state);
  // At most one native query in flight; the latest pointer supersedes old replies.
  state.timer=setTimeout(()=>resolveLatest(context,view,state),35);
}
