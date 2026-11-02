// Keep the native choice control as the value/event owner. Its expanded view
// adds row actions that a platform <option> cannot contain.
const bindings=new WeakMap();
export function disposeHeadChoicePicker(context,view){const b=bindings.get(context.mount);if(b&&(!view||b.view===view)){b.controller.abort();bindings.delete(context.mount);}}
export function syncHeadChoicePicker(context,view,ops){
 if(!view.fiveSimulation?.headOpen&&view.fiveHeadEditor)view.fiveHeadEditor.choiceOpen=false;
 const mount=context.mount;if(!mount)return;let b=bindings.get(mount);
 if(b){Object.assign(b,{context,view,ops});return;}
 const controller=new AbortController(),options={signal:controller.signal};b={context,view,ops,controller};bindings.set(mount,b);
 const current=()=>!b.view.disposed&&b.context.isCurrentProject?.()!==false&&b.view.fiveSimulation?.headOpen;
 const select=()=>mount.querySelector('[data-five-simulation-field="head-choice"]');
 const render=()=>b.ops.renderProject(b.context,b.view);
 const close=focus=>{const s=b.view.fiveHeadEditor;if(!s?.choiceOpen)return;s.choiceOpen=false;render();if(focus)select()?.focus({preventScroll:true});};
 const focus=button=>{if(!button)return;button.focus({preventScroll:true});const menu=button.closest('[data-head-choice-menu]');if(!menu)return;const box=button.getBoundingClientRect(),bounds=menu.getBoundingClientRect();if(box.top<bounds.top)menu.scrollTop-=bounds.top-box.top;else if(box.bottom>bounds.bottom)menu.scrollTop+=box.bottom-bounds.bottom;};
 const open=last=>{const control=select();if(!current()||!control||control.disabled)return;b.view.fiveHeadEditor.choiceOpen=true;render();const buttons=[...mount.querySelectorAll('[data-head-choice-value]:not(:disabled)')];focus(last?buttons.at(-1):buttons.find(e=>e.getAttribute('aria-pressed')==='true')||buttons[0]);};
 mount.addEventListener('pointerdown',event=>{if(!current())return;if(event.target===select()&&event.button===0){event.preventDefault();open(false);}},options);
 mount.addEventListener('click',event=>{const option=event.target.closest?.('[data-head-choice-value]');if(!option||!current()||option.disabled)return;const control=select();if(!control||control.disabled)return;b.view.fiveHeadEditor.choiceOpen=false;control.value=option.dataset.headChoiceValue;control.dispatchEvent(new Event('change',{bubbles:true}));render();},options);
 mount.addEventListener('keydown',event=>{
  if(!current())return;
  if(event.target===select()&&['ArrowDown','ArrowUp','Enter',' '].includes(event.key)){event.preventDefault();open(event.key==='ArrowUp');return;}
  if(!b.view.fiveHeadEditor?.choiceOpen)return;
  if(event.key==='Escape'){event.preventDefault();event.stopPropagation();close(true);return;}
  const menu=event.target.closest?.('[data-head-choice-menu]');if(!menu)return;
  const buttons=[...menu.querySelectorAll('button:not(:disabled)')],at=buttons.indexOf(event.target);let next;
  if(event.key==='ArrowDown')next=(at+1)%buttons.length;else if(event.key==='ArrowUp')next=(at-1+buttons.length)%buttons.length;else if(event.key==='Home')next=0;else if(event.key==='End')next=buttons.length-1;
  if(next!==undefined){event.preventDefault();focus(buttons[next]);}
 },options);
 mount.ownerDocument.addEventListener('pointerdown',event=>{if(current()&&b.view.fiveHeadEditor?.choiceOpen&&!event.target.closest?.('.five-head-choice-picker'))close(false);},options);
 mount.addEventListener('focusout',event=>{if(current()&&b.view.fiveHeadEditor?.choiceOpen&&event.relatedTarget&&!event.relatedTarget.closest?.('.five-head-choice-picker'))close(false);},options);
}
