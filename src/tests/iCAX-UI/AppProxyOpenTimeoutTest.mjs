import test from 'node:test';
import assert from 'node:assert/strict';
import {AppProxy} from '../../iCAX-UI/AppProxy/AppProxy.mjs';
import {MockHostBridge} from '../../iCAX-UI/SDK/Bridge/mockHostBridge.mjs';
import {SDOTimeoutError} from '../../iCAX-UI/SDK/SDO/sdoClient.mjs';

test('a slow file open survives the ordinary request deadline and adopts its authoritative scene once',async()=>{
 const bridge=new MockHostBridge({delayMs:80});
 const app=await AppProxy.create(bridge,{sdo:{timeoutMs:20}});
 try{
  const opened=await app.openProjectFile('D:/projects/large.icax');
  assert.equal(opened.catalog.mainProject.state,'Running');assert.ok(opened.sceneProxy);assert.equal(opened.projectProxy.getMainScene(),opened.sceneProxy);
  assert.equal(bridge.postedFrames.length,1);
  assert.equal(app.sdoClient.timeoutMs,20);
  await assert.rejects(app.resolveProjectFile('D:/projects/other.icax'),SDOTimeoutError);
 }finally{app.dispose();}
});
test('an explicit file-open deadline is honored without retrying or adopting a late response',async()=>{
 const bridge=new MockHostBridge({delayMs:80}),app=await AppProxy.create(bridge,{sdo:{timeoutMs:1000}});
 try{
  await assert.rejects(app.openProjectFile('D:/projects/large.icax',{timeoutMs:20}),SDOTimeoutError);
  await new Promise(resolve=>setTimeout(resolve,100));assert.equal(app.products.size,0);
  assert.equal(bridge.postedFrames.length,1);
 }finally{app.dispose();}
});
