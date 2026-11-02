import test from 'node:test';
import assert from 'node:assert/strict';
import {ProjectProxy} from '../../iCAX-UI/ProjectProxy/ProjectProxy.mjs';

test('automatic checkpoints and explicit saves use distinct framework methods on the main scene',async()=>{
 const calls=[],state={projectId:'project',projectPath:'C:/project.i5cam'},proxy=new ProjectProxy({},state);
 const scene={async invoke(method,payload,options){calls.push({method,payload,options});return {saved:true,persistence:method==='Project.AutoSave'?'Journal':'Snapshot',project:{...state,projectName:'saved'}};}};
 proxy.mainSceneProxy=scene;
 assert.equal((await proxy.autosave(undefined,{timeoutMs:1200})).persistence,'Journal');
 assert.equal(proxy.state.projectName,'saved');
 assert.equal((await proxy.save()).persistence,'Snapshot');
 assert.deepEqual(calls.map(c=>c.method),['Project.AutoSave','Project.Save']);
 assert.deepEqual(calls[0].payload,{projectPath:state.projectPath});assert.equal(calls[0].options.timeoutMs,1200);
});

test('a busy checkpoint is deferred without accepting state, and real failures remain visible',async()=>{
 const state={projectId:'project',projectPath:'C:/project.i5cam'},proxy=new ProjectProxy({},state);
 proxy.mainSceneProxy={async invoke(){return {saved:false,deferred:true,project:{projectId:'wrong'}};}};
 assert.equal((await proxy.autosave()).deferred,true);assert.equal(proxy.state,state);
 proxy.mainSceneProxy.invoke=async()=>({saved:false});await assert.rejects(proxy.autosave(),/自动保存未成功/);
 proxy.mainSceneProxy=null;await assert.rejects(proxy.autosave(),/主场景尚未就绪/);
});
