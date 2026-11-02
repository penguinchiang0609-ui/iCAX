import test from 'node:test';
import assert from 'node:assert/strict';
import {ownedProjectKeydown,activateProjectDocumentBinding,releaseProjectDocumentBindings} from '../../apps/five-axis-cam/webpage/projectKeyboard.mjs';
import {cancelRapidPathMode,rapidPathCommand} from '../../apps/five-axis-cam/webpage/rapidPathEditing.mjs';

class DocumentTarget extends EventTarget{
  listeners=new Map();
  addEventListener(type,listener,options){super.addEventListener(type,listener,options);const key=type+':'+!!(options===true||options?.capture);if(!this.listeners.has(key))this.listeners.set(key,new Set());this.listeners.get(key).add(listener);}
  removeEventListener(type,listener,options){super.removeEventListener(type,listener,options);this.listeners.get(type+':'+!!(options===true||options?.capture))?.delete(listener);}
  count(type,capture=false){return this.listeners.get(type+':'+capture)?.size||0;}
}
const escape=()=>{const event=new Event('keydown',{cancelable:true});Object.defineProperty(event,'key',{value:'Escape'});return event;};
function binding(document,view={activeAreaId:'toolpath'}){
  const mount={isConnected:true,ownerDocument:document},state={view,area:'toolpath',context:{isCurrentProject:()=>true},visibility(){}};
  return {mount,state};
}

test('one document Escape exits an armed rapid mode once even when duplicate project callbacks are present',async()=>{
  for(const mode of ['inserting','deleting']){
    const document=new DocumentTarget(),view={activeAreaId:'toolpath',scene:{domain:{activeSetupId:'setup',setups:[{id:'setup',generatedGeometry:{transitions:[{from:'a',to:'b',policy:{strategy:'Quick'}}]}}]}}};
    const f=binding(document,view);
    const edit={inserting:false,deleting:false};edit[mode]=true;view.fiveRapid={id:'a/b',pathEdit:edit,token:1};
    let closes=0,renders=0;const pending=[],ops={renderProject(){renders++;}};
    const handle=event=>{event.preventDefault();if(cancelRapidPathMode(view))ops.renderProject();else{closes++;pending.push(rapidPathCommand(f.state.context,view,'path-cancel',ops));}};
    document.addEventListener('keydown',ownedProjectKeydown(f.mount,f.state,()=>true,handle));
    document.addEventListener('keydown',ownedProjectKeydown(f.mount,f.state,()=>true,handle));
    const first=escape();document.dispatchEvent(first);await Promise.all(pending);
    assert.equal(first.defaultPrevented,true);assert.equal(view.fiveRapid.pathEdit,edit);assert.equal(edit.inserting,false);assert.equal(edit.deleting,false);assert.equal(closes,0);assert.equal(renders,1);
    document.dispatchEvent(escape());await Promise.all(pending);assert.equal(closes,1);assert.equal(view.fiveRapid.pathEdit,null,'a separate second Escape can finish editing');
  }
});

test('retired, disconnected, disposed and inactive-area project handlers cannot consume document keys',()=>{
  for(const mutate of [f=>f.mount.isConnected=false,f=>f.state.view.disposed=true,f=>f.state.area='workpiece',f=>f.state.context.isCurrentProject=()=>false]){
    const document=new DocumentTarget(),f=binding(document);let calls=0;document.addEventListener('keydown',ownedProjectKeydown(f.mount,f.state,()=>true,()=>calls++));mutate(f);document.dispatchEvent(escape());assert.equal(calls,0);
  }
  const document=new DocumentTarget(),f=binding(document);let calls=0;document.addEventListener('keydown',ownedProjectKeydown(f.mount,f.state,()=>false,()=>calls++));document.dispatchEvent(escape());assert.equal(calls,0);
});

test('switching cached area mounts retains one document keyboard/visibility/jog registration and reactivates a returning mount',()=>{
  const document=new DocumentTarget(),view={activeAreaId:'toolpath'},items=Array.from({length:10},()=>binding(document,view));let jogStarts=0,jogStops=0;
  const activate=f=>{f.state.keydown=ownedProjectKeydown(f.mount,f.state,()=>true,()=>{});activateProjectDocumentBinding(f.mount,f.state,()=>{
    jogStarts++;const jog=()=>{};document.addEventListener('keydown',jog,{capture:true});f.state.disposeMachineJog=()=>{jogStops++;document.removeEventListener('keydown',jog,{capture:true});};
  });};
  try{
    for(const f of items){activate(f);assert.equal(document.count('keydown'),1);assert.equal(document.count('keydown',true),1);assert.equal(document.count('visibilitychange'),1);}
    assert.equal(jogStarts,10);assert.equal(jogStops,9);activate(items[0]);assert.equal(document.count('keydown'),1);assert.equal(document.count('keydown',true),1);assert.equal(jogStarts,11);
  }finally{releaseProjectDocumentBindings(view);}
  assert.equal(jogStops,11);assert.equal(document.count('keydown'),0);assert.equal(document.count('keydown',true),0);assert.equal(document.count('visibilitychange'),0);
});

test('closing a background project releases only its document registrations',()=>{
  const document=new DocumentTarget(),active=binding(document),background=binding(document);
  const activate=f=>{f.state.keydown=()=>{};activateProjectDocumentBinding(f.mount,f.state,()=>{});};
  try{activate(active);activate(background);assert.equal(document.count('keydown'),2);releaseProjectDocumentBindings(background.state.view);assert.equal(document.count('keydown'),1);assert.equal(document.listeners.get('keydown:false').has(active.state.keydown),true);}
  finally{releaseProjectDocumentBindings(active.state.view);releaseProjectDocumentBindings(background.state.view);}
});
