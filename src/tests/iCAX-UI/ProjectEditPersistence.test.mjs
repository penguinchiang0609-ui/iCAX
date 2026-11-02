import assert from 'node:assert/strict';
import{saveEditedProject,flushEditedProjectSave}from '../../apps/five-axis-cam/webpage/projectEditPersistence.mjs';
import{projectPersistenceTimeoutMs}from '../../apps/five-axis-cam/webpage/operationTiming.mjs';
let release,started,active=0,max=0,writes=[];
const waitStarted=()=>new Promise(r=>started=r), gates=[];
const projectProxy={state:{projectPath:'audit.i5cam'},autosave:async(path,options)=>{assert.equal(options.timeoutMs,projectPersistenceTimeoutMs);assert.ok(options.timeoutMs>15000);active++;max=Math.max(max,active);writes.push(path);started?.();await new Promise(r=>gates.push(r));active--;}};
const context={projectProxy},began=waitStarted(),one=saveEditedProject(context);await began;
const two=saveEditedProject(context),three=saveEditedProject(context);assert.equal(one,two);assert.equal(two,three);const second=waitStarted();gates.shift()();await second;assert.equal(writes.length,2);gates.shift()();await Promise.all([one,two,three]);await flushEditedProjectSave(context);assert.equal(max,1);
// A save issued from another completion reaction must not attach to a finished writer.
projectProxy.autosave=async path=>writes.push(path);await saveEditedProject(context);await Promise.resolve().then(()=>saveEditedProject(context));assert.equal(writes.length,4);
let fail=true;projectProxy.autosave=async()=>{if(fail){fail=false;throw Error('disk failure')}};await assert.rejects(saveEditedProject(context),/disk failure/);assert.equal(await saveEditedProject(context),true);assert.equal(await saveEditedProject({projectProxy:{state:{},autosave(){throw Error('no path')}}}),false);console.log('PASS serialized overlapping saves, late edits, retry and unnamed project');
// A native repository gesture can defer a checkpoint. It must remain pending,
// coalesce later edits, and never substitute a blocking explicit file save.
let checkpointCalls=0,explicitSaves=0;const deferred={projectProxy:{state:{projectPath:'journal.i5cam'},async autosave(){return ++checkpointCalls===1?{saved:false,deferred:true}:{saved:true,persistence:'Journal'};},async save(){explicitSaves++;}}};
const pending=saveEditedProject(deferred);await Promise.resolve();const later=saveEditedProject(deferred);assert.equal(pending,later);await pending;assert.equal(checkpointCalls,2);assert.equal(explicitSaves,0);
console.log('PASS native journal checkpoint deferral and coalescing');
