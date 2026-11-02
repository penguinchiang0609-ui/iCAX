import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {attachMachineJog,stopMachineJog} from '../../apps/five-axis-cam/webpage/machineJog.mjs';
import {decodeMachineJogControl,encodeMachineJogResult} from './FiveAxisMachineJogPDOTestCodec.mjs';

const until=async predicate=>{const deadline=Date.now()+1500;while(!predicate()){if(Date.now()>deadline)throw Error('condition timeout');await new Promise(resolve=>setTimeout(resolve,2));}};
const matrix=x=>[1,0,0,x,0,1,0,0,0,0,1,0,0,0,0,1];
function dispatch(target,type,values={}){const event=new Event(type);for(const [key,value]of Object.entries(values))Object.defineProperty(event,key,{value});target.dispatchEvent(event);}
function fixture(){
 const frames=new Map(),calls=[],writes=[],pauses=[],previews=new Map();let frameId=0,q=0,version=0,mutation=1,held=false,sequence='0',saves=0,polls=0,history=0,releaseStart,status='Running';
 globalThis.requestAnimationFrame=cb=>{const id=++frameId;frames.set(id,cb);return id;};globalThis.cancelAnimationFrame=id=>frames.delete(id);
 const document=new EventTarget(),window=new EventTarget(),mount=new EventTarget(),button={disabled:false,dataset:{fiveCommand:'five.machine.jog-plus:joint'},getBoundingClientRect:()=>({left:0,right:10,top:0,bottom:10})};
 document.defaultView=window;document.hidden=false;document.activeElement=null;mount.ownerDocument=document;mount.closest=()=>button;mount.querySelector=()=>null;
 const m={entityId:'machine',enabled:true,joints:[{entityId:'joint',type:'prismatic',position:0,velocity:10}]},object=new THREE.Object3D();
 const view={scene:{domain:{expected:{epoch:'epoch',mutationSequence:'1'},setups:[]},machines:[m]},fiveMachine:{machineId:'machine',jogStep:.1},activeAreaId:'simulation',areas:{simulation:{viewReader:{pollingPaused:false,setPollingPaused:value=>pauses.push(['simulation',value]),setPollInterval(){},async poll(){polls++;}}},hidden:{viewReader:{pollingPaused:false,setPollingPaused:value=>pauses.push(['hidden',value])}}},viewport:{setTransformPreviews(entries){for(const [id,value]of entries)previews.set(id,value);object.matrix.fromArray(entries[0][1]);object.matrix.decompose(object.position,object.quaternion,object.scale);object.updateMatrixWorld(true);},clearTransformPreviews(ids){for(const id of ids)previews.delete(id);}}};
 const f={holdStart:false,beginError:'',view,calls,writes,pauses,frames,previews};
 const descriptor={token:'',epoch:'epoch',jointIds:['joint'],worldIds:['joint'],worldTransformIds:['joint'],controlPDO:{id:'input',version:1,payloadSize:1024},resultPDO:{id:'output',version:1,payloadSize:4096}};
 const result=token=>({...descriptor,token,status,positions:[q],worldMatrices:matrix(q),expected:{epoch:'epoch',mutationSequence:String(mutation)},changed:q!==0});
 const pdo={enabled:true,async withWriteDescriptor(d,write){const buffer=new ArrayBuffer(d.payloadSize),n=write(buffer),control=decodeMachineJogControl(buffer.slice(0,n));writes.push(control);held=control.held;sequence=control.sequence;return true;},async withReadDescriptor(d,read){if(held&&status==='Running'){q+=.01;mutation++;}const output={...result(view.fiveMachineJog.token),resultSequence:String(++version),processedSequence:sequence,mutationSequence:String(mutation),ticks:String(version)},bytes=encodeMachineJogResult(output);return read(bytes,{dataVersion:String(version)});}};
 const context={mount,project:{projectId:'project'},projectProxy:{state:{projectPath:'C:/test/jog.i5cam'},async autosave(){saves++;}},isCurrentProject:()=>true,actions:{refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){history++;}},sceneProxy:{pdo,async invoke(method,payload){calls.push({method,payload});if(method==='CamSimulation.StartMachineJog'){if(f.holdStart)await new Promise(resolve=>releaseStart=resolve);if(f.beginError)throw Error(f.beginError);return result(payload.token);}assert.equal(method,'CamSimulation.StopMachineJog','no KeepMachineJog mailbox call is permitted');status='Stopped';held=false;return result(payload.token);}}};
 const ops={renderProject(){},async refreshActiveAreaView(){assert.equal(view.fiveMachineJog.stream?.stopped??true,true);}},binding={view,context,ops};attachMachineJog(mount,binding);
 Object.assign(f,{context,ops,binding,press:()=>dispatch(mount,'pointerdown',{button:0,pointerId:1}),release:()=>dispatch(document,'pointerup',{pointerId:1}),releaseStart:()=>releaseStart?.(),setTerminal:value=>status=value,async tick(){const entry=frames.entries().next().value;assert.ok(entry);frames.delete(entry[0]);entry[1]();await view.fiveMachineJog.stream.pulsing;}});
 Object.defineProperties(f,{saves:{get:()=>saves},polls:{get:()=>polls},history:{get:()=>history},actualX:{get:()=>object.position.x},q:{get:()=>q}});return f;
}

test('held jog uses only Start/Stop commands, PDO paints exact accepted q/world, and one saved stop restores all readers',async()=>{
 const f=fixture();f.press();await until(()=>f.view.fiveMachineJog?.stream);await f.tick();await f.tick();assert.ok(f.q>0);assert.equal(f.actualX,f.q);assert.equal(f.view.scene.machines[0].joints[0].position,f.q);assert.equal(f.polls,0);assert.equal(f.saves,0);assert.deepEqual(f.calls.map(c=>c.method),['CamSimulation.StartMachineJog']);
 f.release();await f.view.fiveMachineJog.stopping;assert.deepEqual(f.calls.map(c=>c.method),['CamSimulation.StartMachineJog','CamSimulation.StopMachineJog']);assert.equal(f.writes.at(-1).held,false);assert.equal(f.actualX,f.q);assert.equal(f.saves,1);assert.equal(f.history,1);assert.equal(f.polls,1);assert.equal(f.frames.size,0);assert.equal(f.view.fiveMachineJog.ended,true);assert.equal(f.view.fiveMachineJog.running,false);assert.deepEqual(f.pauses,[['simulation',true],['hidden',true],['simulation',false],['hidden',false]]);f.binding.disposeMachineJog();
});
test('release while reliable Start is pending never publishes held=true and stops once the same Start returns',async()=>{
 const f=fixture();f.holdStart=true;f.press();await until(()=>f.calls.length===1);f.release();const stopping=f.view.fiveMachineJog.stopping;assert.equal(f.writes.length,0);f.releaseStart();await stopping;assert.equal(f.q,0);assert.equal(f.writes.length,0);assert.equal(f.calls.length,2);assert.equal(f.saves,0);assert.equal(f.view.fiveMachineJog.ended,true);f.binding.disposeMachineJog();
});
test('a terminal PDO automatically ends once and saves accepted movement without a status-query loop',async()=>{
 const f=fixture();f.press();await until(()=>f.view.fiveMachineJog?.stream);await f.tick();f.setTerminal('Expired');await f.tick();await f.view.fiveMachineJog.stopping;assert.equal(f.calls.length,2);assert.equal(f.view.fiveMachineJog.ended,true);assert.equal(f.saves,1);assert.equal(f.frames.size,0);f.binding.disposeMachineJog();
});
test('a committed Stop still checkpoints if final view or history synchronization fails',async()=>{
 for(const failing of ['view','history']){const f=fixture();if(failing==='view')f.ops.refreshActiveAreaView=async()=>{throw Error('view failed');};else f.context.actions.refreshNativeProjectHistory=async()=>{throw Error('history failed');};f.press();await until(()=>f.view.fiveMachineJog?.stream);await f.tick();assert.equal(f.previews.size,1);await stopMachineJog(f.view);assert.equal(f.saves,1);assert.equal(f.view.fiveMachineJog.ended,true);assert.equal(f.view.fiveMachineJog.running,false);assert.match(f.view.fiveMachine.error,/failed/);assert.equal(f.pauses.at(-1)[1],false);assert.equal(f.previews.size,0,'failed presentation cannot mask the next authoritative View or Undo');f.binding.disposeMachineJog();}
});
test('a failed Start response reliably retires its token and cannot leave paused readers or a live stream',async()=>{
 const f=fixture();f.beginError='Start timed out';f.press();await until(()=>f.calls.length===2);await f.view.fiveMachineJog.stopping;assert.equal(f.calls.at(-1).method,'CamSimulation.StopMachineJog');assert.equal(f.saves,0);assert.equal(f.frames.size,0);assert.equal(f.view.fiveMachineJog.running,false);assert.equal(f.pauses.at(-1)[1],false);f.binding.disposeMachineJog();
});
