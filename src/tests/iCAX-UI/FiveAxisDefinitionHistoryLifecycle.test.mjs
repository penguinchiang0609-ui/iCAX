import test from 'node:test';
import assert from 'node:assert/strict';
import {ensureDefinitionHistory} from '../../apps/five-axis-cam/webpage/machineDefinitionHistory.mjs';

test('definition refresh waits for a scene and ignores a scene unloaded during its response',async()=>{
 const errors=[];let renders=0,requests=0,resolveList;
 const context={productProxy:{invoke:async method=>{
  requests++;
  if(method==='Product.GetSettingsHistory')return {undoSteps:[],redoSteps:[]};
  return new Promise(resolve=>{resolveList=resolve;});
 }},isCurrentProject:()=>true};
 const ops={renderProject:()=>renders++,appendProjectLog:(_context,_level,message)=>errors.push(message)};
 const view={scene:null};
 ensureDefinitionHistory(context,view,ops);
 assert.equal(requests,0,'startup without a scene must not request a refresh');
 view.scene={machineDefinitions:[]};
 ensureDefinitionHistory(context,view,ops);
 await new Promise(resolve=>setImmediate(resolve));
 assert.equal(typeof resolveList,'function');
 view.scene=null;
 resolveList({definitions:[{id:'definition'}]});
 await new Promise(resolve=>setImmediate(resolve));
 assert.deepEqual(errors,[]);
 assert.equal(renders,0);
 assert.equal(view.fiveDefinitionRefresh,false);
});
