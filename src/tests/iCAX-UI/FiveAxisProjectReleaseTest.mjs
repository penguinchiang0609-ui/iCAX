import test from 'node:test';
import assert from 'node:assert/strict';
import {releaseProject} from '../../apps/five-axis-cam/webpage/entry.mjs';
import {getProjectView,findProjectView,releaseProjectView} from '../../apps/_shared/workbench/state/projectViewStore.mjs';

test('closing a background FiveAxisCAM project with no mount releases its resources and preserves the active project',async()=>{
  const previousCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
  const activeId='release-active',closedId='release-background',active=getProjectView(activeId),closed=getProjectView(closedId);
  let activeDisposals=0,closedDisposals=0,readerStops=0;
  const canvas={},mesh={};active.viewport={canvas,mesh,dispose(){activeDisposals++;}};
  closed.viewport={dispose(){closedDisposals++;}};closed.areas.test={viewReader:{stop(){readerStops++;}}};
  closed.fiveAxisPointInsert=true;
  try{
    assert.equal(await releaseProject({project:{projectId:closedId},mount:null}),true);
    assert.equal(findProjectView(closedId),null);assert.equal(closed.disposed,true);
    assert.equal(closedDisposals,1);assert.equal(readerStops,1);assert.equal(closed.fiveAxisPointInsert,false);
    assert.equal(findProjectView(activeId),active);assert.equal(active.disposed,false);assert.equal(activeDisposals,0);
    assert.equal(active.viewport.canvas,canvas);assert.equal(active.viewport.mesh,mesh);
    assert.equal(await releaseProject({project:{projectId:closedId},mount:null}),false);
    assert.equal(findProjectView(closedId),null,'repeated close must not recreate a project view');
  }finally{await releaseProjectView(activeId);await releaseProjectView(closedId);globalThis.cancelAnimationFrame=previousCancel;}
});

test('a foreign mount is never inspected or detached while its background project waits for native cleanup',async()=>{
  const previousCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
  const id='release-foreign-mount',view=getProjectView(id);let finish,disposals=0;
  view.viewport={dispose(){disposals++;}};view.fiveMachineIK={inflight:new Promise(resolve=>finish=resolve)};
  const mount={get ownerDocument(){throw Error('active project mount must remain untouched');}};
  try{
    const closing=releaseProject({project:{projectId:id},mount});await Promise.resolve();
    assert.equal(disposals,0,'target viewport stays available until its native edit completes');
    finish();assert.equal(await closing,true);assert.equal(disposals,1);assert.equal(findProjectView(id),null);
  }finally{finish?.();await releaseProjectView(id);globalThis.cancelAnimationFrame=previousCancel;}
});
