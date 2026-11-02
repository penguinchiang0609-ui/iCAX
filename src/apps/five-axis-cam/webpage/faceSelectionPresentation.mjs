import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {button} from './workspaceControls.mjs';
import {sceneColors} from './scenePalette.mjs';

export function faceSelectionList({ids,selectedId,key,label,selectCommand,removeCommand,focusCommand,disabled='',empty}){
  return `<div class="five-support-face-list" data-five-key="${attr(key)}" role="group" aria-label="${attr(label)}">${ids.map((id,i)=>`<div class="five-support-face ${selectedId===id?'is-selected':''}" data-five-key="${attr(key)}-${attr(id)}">${button(selectCommand(id),`面 ${i+1}`,`${disabled} aria-pressed="${selectedId===id}" ${focusCommand?`data-five-face-focus="${attr(focusCommand(id))}" title="↑↓ 切换面 · Enter / F / 双击聚焦"`:''}`)}${focusCommand?faceSelectionAction(focusCommand(id),`定位面 ${i+1}`,'locate',`${disabled} class="five-face-locate"`):''}${button(removeCommand(id),'×',`${disabled} title="删除此面" aria-label="删除面 ${i+1}"`)}</div>`).join('')||`<p class="five-support-empty">${empty}</p>`}</div>`;
}

export function selectedFaceOverlay(source,faces,selectedId){
  const g=source.geometry,positions=g.attributes.position,points=[],colors=[],active=[];
  for(const face of faces){
    // Membership in the retained-face set keeps its established colour. The
    // currently inspected row is a separate presentation-only overlay.
    const color=new THREE.Color(0x20b897);
    for(const range of face.ranges)for(let n=+range.first*3;n<(+range.first+ +range.count)*3;n++){
      const i=g.index?g.index.getX(n):n,x=positions.getX(i),y=positions.getY(i),z=positions.getZ(i);points.push(x,y,z);colors.push(color.r,color.g,color.b);if(face.id===selectedId)active.push(x,y,z);
    }
  }
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(points,3));geometry.setAttribute('color',new THREE.Float32BufferAttribute(colors,3));
  const mesh=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({vertexColors:true,transparent:true,opacity:.5,side:THREE.DoubleSide,depthWrite:false,polygonOffset:true,polygonOffsetFactor:-2,polygonOffsetUnits:-2}));
  mesh.raycast=()=>{};mesh.renderOrder=65;mesh.userData.faceSelectionOverlay=true;
  if(active.length){
    const surface=new THREE.BufferGeometry();surface.setAttribute('position',new THREE.Float32BufferAttribute(active,3));
    const selected=new THREE.Mesh(surface,new THREE.MeshBasicMaterial({color:sceneColors.selected,transparent:true,opacity:.68,side:THREE.DoubleSide,depthTest:false,depthWrite:false}));
    selected.name='five-active-face';selected.userData.activeSelectionFace=selectedId;selected.raycast=()=>{};selected.renderOrder=100;mesh.add(selected);
    const outline=new THREE.LineSegments(new THREE.EdgesGeometry(surface,25),new THREE.LineBasicMaterial({color:sceneColors.selected,depthTest:false,depthWrite:false}));
    outline.userData.activeFaceOutline=true;outline.raycast=()=>{};outline.renderOrder=101;selected.add(outline);
  }
  return mesh;
}

const icons={locate:'M3 7V3h4 M13 3h4v4 M17 13v4h-4 M7 17H3v-4 M6 10h8 M10 6v8',manual:'m4 3 12 8-6 1-3 6-3-15Z M13 14l4 4',bulk:'M3 7h11v11H3z M7 3h11v11 M8 10v5 M5.5 12.5h5',clear:'M4 6h12 M8 3h4 M6 6l1 12h6l1-12 M9 9v6 M12 9v6'};
export const faceSelectionAction=(command,label,icon,extra='')=>`<button type="button" data-five-command="${attr(command)}" title="${attr(label)}" aria-label="${attr(label)}" ${extra}><svg viewBox="0 0 20 20" aria-hidden="true"><path d="${icons[icon]}"/></svg></button>`;
