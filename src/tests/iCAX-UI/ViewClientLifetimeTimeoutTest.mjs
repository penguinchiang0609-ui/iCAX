import assert from 'node:assert/strict';
import {ViewClient} from '../../iCAX-UI/SDK/View/viewClient.mjs';

for (const configured of [undefined, 120000]) {
  const calls=[];
  const client=new ViewClient({
    async invoke(method,payload,options) {
      calls.push({method,payload,options});
      return method==='View.Release'?{released:true}:{viewId:'test-view',resource:{url:'resource://test/view'}};
    },
    resources:{head:()=>new Promise(()=>{})},
  });
  const reader=await client.start({sources:[{sourceId:'test-source',where:'1=1'}]}, {timeoutMs:configured});
  assert.equal(await reader.stop(),true);
  assert.equal(await reader.stop(),false,'each reader releases its reference once');
  assert.deepEqual(calls.map(c=>[c.method,c.options.timeoutMs]),[
    ['View.GetOrCreate',configured??10000],['View.Release',configured??10000],
  ]);
  await client.dispose();
}
console.log('PASS ViewClient lifetime: configured request budget applies to creation and release');
