import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {Vector3} from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

// Shared scene transform presentation. Each owner keeps its own geometry,
// validation, history and persistence semantics.
export function renderTransformTools({owner,title,modes,mode,busy=false,switcher}){
 const switches=switcher?`<div class="five-motion-switch" data-transform-owner="${owner}" data-five-key="${owner}-switcher" role="toolbar" aria-label="${attr(switcher.label)}">${switcher.modes.map(([id,label,command,disabled=false,tooltip=label])=>`<button type="button" data-transform-switch="${attr(id)}" data-five-key="${owner}-switch-${attr(id)}" data-five-command="${attr(command)}" aria-label="${attr(label)}" aria-pressed="${switcher.mode===id}" title="${attr(tooltip)}" ${busy||disabled?'disabled':''}>${esc(label)}</button>`).join('')}</div>`:'';
 return `<div class="five-placement-tools" data-transform-owner="${owner}" data-five-key="${owner}-tools" role="toolbar" aria-label="${attr(title)}">${modes.map(([id,label,command])=>{const key={translate:'W',rotate:'E',scale:'R',bounds:'M',focus:'F'}[id];return `<button type="button" data-transform-mode="${attr(id)}" data-five-key="${owner}-mode-${id}" data-five-command="${attr(command)}" aria-label="${attr(label)}" aria-keyshortcuts="${key}" ${id==='focus'?'':`aria-pressed="${mode===id}"`} title="${attr(label)}（${key}）" ${busy?'disabled':''}>${esc(label)}<kbd>${key}</kbd></button>`;}).join('')}<span class="five-placement-snap" data-five-placement-snap role="status" title="按住 Alt 临时关闭吸附" hidden></span></div>${switches}`;
}
export function renderTransformValue({owner,mode,axis='',fields,attribute,busy=false,error='',title:caption}){
 const title=caption||(mode==='translate'?'位移':mode==='scale'?'缩放比例':'角度'),unit=mode==='translate'?'mm':mode==='scale'?'×':'°';
 return `<div class="five-placement-hud" data-transform-owner="${owner}" data-five-key="${owner}-value" data-axis="${attr(axis)}" role="group" aria-label="${attr(title)}">${caption?`<strong class="five-transform-caption">${esc(caption)}</strong>`:''}${fields.map(f=>`<label class="five-placement-field" data-five-key="${owner}-${f.name}" data-axis="${attr(f.label.toLowerCase())}"><span>${esc(f.label)}</span><input aria-label="${attr(f.aria||title)}" data-five-key="${owner}-input-${f.name}" ${attribute}="${attr(f.name)}" type="text" inputmode="decimal" autocomplete="off" value="${attr(f.value)}" ${busy||f.readonly?'readonly':''} aria-busy="${!!busy}"><small>${f.unit||unit}</small></label>`).join('')}${error?`<div class="five-placement-error" role="alert">${esc(error)}</div>`:''}</div>`;
}
export function transformShortcutMode(event){
 if(event.defaultPrevented||event.repeat||event.isComposing||event.ctrlKey||event.metaKey||event.altKey||event.shiftKey||event.target?.closest?.('input,textarea,select,[contenteditable]:not([contenteditable="false"]),[role="textbox"],[role="dialog"]'))return null;
 return {w:'translate',e:'rotate',r:'scale',m:'bounds',f:'focus'}[event.key?.toLowerCase()]||null;
}
export function transformShortcutButton(event,mount){
 const mode=transformShortcutMode(event);if(!mode||!mount.contains(event.target))return null;
 return [...mount.querySelectorAll(`.five-placement-tools:not([hidden]) button[data-transform-mode="${mode}"]`)].find(button=>!button.disabled&&!button.closest('[hidden]'))||null;
}
export function positionTransformHud(context,point,owner){
 const selector=owner?`[data-transform-owner="${owner}"]`:'';
 const tools=context.mount.querySelector('.five-placement-tools'+selector);
 const motion=context.mount.querySelector('.five-motion-switch'+selector);
 if(tools)tools.hidden=!point.visible;if(motion)motion.hidden=!point.visible;
 if(tools&&motion){
  const gap=5,width=tools.offsetWidth+gap+motion.offsetWidth,height=Math.max(tools.offsetHeight,motion.offsetHeight);
  const x=Math.max(16,Math.min(point.x-tools.offsetWidth/2,point.width-width-16)),bottom=Math.max(66+height,Math.min(point.y+112+tools.offsetHeight,point.height-60));
  tools.style.left=`${x}px`;tools.style.top=`${bottom-tools.offsetHeight}px`;
  motion.style.left=`${x+tools.offsetWidth+gap}px`;motion.style.top=`${bottom-motion.offsetHeight}px`;
 }else if(tools){tools.style.left=`${Math.max(16,Math.min(point.x-tools.offsetWidth/2,point.width-tools.offsetWidth-16))}px`;tools.style.top=`${Math.max(66,Math.min(point.y+112,point.height-tools.offsetHeight-60))}px`;}
 const hud=context.mount.querySelector('.five-placement-hud'+selector);if(!hud)return;
 hud.hidden=!point.visible;let x=point.x+110;if(x+hud.offsetWidth>point.width-16)x=point.x-110-hud.offsetWidth;
 hud.style.left=`${Math.max(16,Math.min(x,point.width-hud.offsetWidth-16))}px`;
 hud.style.top=`${Math.max(66,Math.min(point.y-hud.offsetHeight/2,point.height-hud.offsetHeight-60))}px`;
}
export function positionWorldTransformHud(context,viewport,position,owner){
 const canvas=viewport?.renderer?.domElement;if(!context.mount||!canvas||!viewport.camera||!position)return;
 const point=new Vector3(...position).project(viewport.camera),width=canvas.clientWidth||canvas.getBoundingClientRect().width,height=canvas.clientHeight||canvas.getBoundingClientRect().height;
 positionTransformHud(context,{x:(point.x+1)*width/2,y:(1-point.y)*height/2,width,height,visible:Math.abs(point.z)<=1&&Math.abs(point.x)<=1&&Math.abs(point.y)<=1},owner);
}
export function focusTransformValue(context,attribute){const input=context.mount.querySelector(`[${attribute}]`);input?.focus({preventScroll:true});input?.select();}
