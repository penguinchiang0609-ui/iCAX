import test from 'node:test';
import assert from 'node:assert/strict';
import {loadWorkData,renderWorkDetails,workLegend,WORK_LABELS} from '../workInspectionData.mjs';
import {coverageFixture as fixture,coverageEntry as entry,pathFixture} from './faceCutCoverage.fixture.mjs';
const load=()=>loadWorkData('faceCutCoverage',entry,async()=>structuredClone(fixture));
test('whole model and all reference edges survive skin-domain presentation without mutation',async()=>{
 const before=JSON.stringify(fixture);let requests=0;
 const data=await loadWorkData('faceCutCoverage',entry,async()=>{requests++;return fixture;});
 assert.equal(requests,1);assert.equal(JSON.stringify(fixture),before);
 assert.equal(data.faces.length,80);assert.equal(data.geometry.faces.length,80);
 assert.equal(data.edges.filter(e=>e.workReference).length,4);assert.equal(data.edges.filter(e=>e.workCoverage).length,7);
 for(const edge of data.edges){assert.equal(edge.path,false);assert.equal(edge.side,false);assert.equal(edge.endpoints.length,0);}
 assert.equal(data.edges.find(e=>e.pathKey==='F101:E11:C1').workColor,'#1fe05c');
 assert.equal(data.edges.find(e=>e.pathKey==='F101:E11:C4').workColor,'#f59e0b');
 assert.equal(data.edges.find(e=>e.pathKey==='F101:E11:C5').workColor,'#94a3b8');
 assert.equal(data.complete,false);assert.equal(data.success,true);
});
test('native evidence shows original face, skin parameters, witness and kind without pass counting',async()=>{
 const data=await load();
 const checks=[['F101:E11:C1',['到达非皮凸边','evidence[0]','E12','来源皮边逻辑标识','0 ～ 2']],['F101:E11:C2',['端点相邻侧边补全','SAVED-SIDE-14','SIDE14:SPAN1','arrival[1]']],['F101:E11:C3',['到达另一处皮边','E11','20 ～ 22','允许经过圆心']],['F101:E11:C4',['半切透','到达凹边','本区间判定完成']]];
 for(const [selectedEdge,expected] of checks){const html=renderWorkDetails(data,{selectedEdge});for(const text of expected)assert.ok(html.includes(text),text);assert.ok(!html.includes('不同来源路径数'));assert.ok(!html.includes('双路径'));}
 assert.equal(data.edges.find(e=>e.pathKey==='F101:E11:C3').analysisRecord.sourcePathKey,'F101:P1','same source far boundary is retained');
});
test('bounded skin exit proof names the full boundary set without inventing an arrival',async()=>{
 const native=structuredClone(fixture),index=native.evidence.length;
 native.evidence.push({index,faceId:'101',sourcePathKey:'F101:P3',sourceEdgeId:'13',sourceFirstParameter:0,sourceLastParameter:10,kind:'SkinExitBoundarySet',hasArrival:false,complete:true,classificationProofComplete:true,through:true,exitCorrespondenceExpanded:false,reason:'bounded-valid-native-plane-domain;all-physical-native-boundary-intervals-source-attributed-OnSkin;source-regular-on-open-native-knot-intervals;original-coedge-inward-normal-family;exit-correspondence-not-expanded',boundaryWitnesses:[{edgeId:'13',spanKey:'E13:S1',firstParameter:0,lastParameter:10,sourceSkinFaceIds:['111'],skinReason:'native source membership'},{edgeId:'21',spanKey:'E21:S3',firstParameter:3,lastParameter:9,sourceSkinFaceIds:['112'],skinReason:'whole native domain certificate'}]});
 Object.assign(native.sources[1],{complete:true,exitCorrespondenceExpanded:false,method:'bounded-planar-all-skin-boundary-exit',reason:'exit-correspondence-not-expanded'});
 Object.assign(native.coverage[5],{kind:'Through',mappingComplete:true,classificationProofComplete:true,exitCorrespondenceExpanded:false,evidenceIndices:[index],arrivalIndices:[]});
 const data=await loadWorkData('faceCutCoverage',entry,async()=>native),html=renderWorkDetails(data,{selectedEdge:'F101:E13:C6'});
 for(const text of ['已证明所有可能出口均在皮上','具体出口参数对应','尚未展开','E21:S3','3 ～ 9','原生平面域有效且有界','没有伪造对面点或到达记录'])assert.ok(html.includes(text),text);
 assert.ok(!html.includes('Eundefined'));assert.ok(!html.includes('arrival[undefined]'));assert.ok(!html.includes('arrival[0]'));
 assert.equal(data.edges.find(e=>e.pathKey==='F101:E13:C6').analysisRecord.arrivalIndices.length,0);
});
test('unresolved interval stays local and failed sources remain visible at face level',async()=>{
 const data=await load(),proved=renderWorkDetails(data,{selectedEdge:'F101:E11:C4'}),pending=renderWorkDetails(data,{selectedEdge:'F101:E11:C5'}),face=renderWorkDetails(data,{selectedFace:'101'});
 assert.ok(proved.includes('半切透'));assert.ok(!proved.includes('本面仍有未完成的来源映射'));
 assert.ok(pending.includes('判定待定'));assert.ok(pending.includes('平面曲边的刀轴随位置变化'));
 for(const text of ['F101:P3','unsupported&lt;surface&gt;','判定未完成','E14'])assert.ok(face.includes(text),text);
 assert.ok(!face.includes('data-edge="F102:E11:C7"'));
 const shared=renderWorkDetails(data,{selectedFace:'102',selectedEdge:data.edges.find(e=>e.workReference&&e.sourceEdgeIds.includes('11')).pathKey});
 assert.ok(shared.includes('F102:P1'));assert.ok(!shared.includes('unsupported&lt;surface&gt;'));
});
test('side display only joins saved face/source/native interval records',async()=>{
 const native=structuredClone(fixture);
 native.sideCorrespondences.push({arrivalIndex:4,sidePathKey:'WRONG-FACE',sideSpanKey:'WRONG'});
 const before=JSON.stringify(native),data=await loadWorkData('faceCutCoverage',entry,async()=>native),html=renderWorkDetails(data,{selectedEdge:'F101:E11:C2'});
 assert.ok(html.includes('SAVED-SIDE-14'));assert.ok(!html.includes('WRONG-FACE'));
 assert.ok(html.includes('不增加来源路径或覆盖次数'));assert.equal(JSON.stringify(native),before);
});

test('side native components and whole-source diagnostics retain their exact proof scope',async()=>{
 const native=structuredClone(fixture);
 const sideReason='first-boundary-side-component-reaches-a-shared-real-skin-trace-endpoint;shared-topological-vertex;target-native-component';
 const summaryReason='skin-native-interval-has-no-proven-first-boundary-arrival;whole-source-summary-not-cell-local;positive-source-projection-cell-below-native-event-resolution';
 native.evidence[1].reason=sideReason;
 native.coverage[4].reason=summaryReason;
 const before=JSON.stringify(native),data=await loadWorkData('faceCutCoverage',entry,async()=>native);
 const side=renderWorkDetails(data,{selectedEdge:'F101:E11:C2'}),pending=renderWorkDetails(data,{selectedEdge:'F101:E11:C5'});
 for(const text of ['来源原生区间或目标侧边原生区间','共享的真实端点','目标侧边原生区间的精确连通分量','不填补来源区间的正间隙'])assert.ok(side.includes(text),text);
 assert.ok(pending.includes('整条来源路径的诊断汇总，不代表本区间的已证确切原因'));
 assert.ok(side.includes(sideReason),'raw native side proof remains inspectable');
 assert.ok(pending.includes(summaryReason),'raw native summary remains inspectable');
 assert.equal(JSON.stringify(native),before);
});
test('native periodic values stay exact; missing or obsolete schemas never borrow geometry',async()=>{
 const native=structuredClone(fixture),first=10*Math.PI,last=first+2*Math.PI;
 native.coverage[0].firstParameter=first;native.coverage[0].lastParameter=last;
 const data=await loadWorkData('faceCutCoverage',entry,async()=>native);
 assert.ok(renderWorkDetails(data,{selectedEdge:'F101:E11:C1'}).includes(String(first)+' ～ '+String(last)));
 const missing=await loadWorkData('faceCutCoverage',{status:'not-run'},()=>{throw Error('must not fetch');});
 assert.equal(missing.faces.length,0);assert.equal(missing.edges.length,0);
 await assert.rejects(loadWorkData('faceCutCoverage',entry,async()=>({...fixture,schema:1})),/格式不完整/);
});

test('saved STEP point markers explain only their exact original positive coverage span',async()=>{
 const native=structuredClone(fixture),span=native.coverage[4];
 span.firstParameter=0;span.lastParameter=2*Number.MIN_VALUE;
 const marker={coverageKey:span.key,faceId:span.faceId,sourcePathKey:span.sourcePathKey,sourceEdgeId:span.sourceEdgeId,
  firstParameter:span.firstParameter,lastParameter:span.lastParameter,position:[10,20,30],builderError:6,
  nativeParameterResolution:2.2250738585072014e-308,reason:'positive-native-interval-below-kernel-edge-resolution',displayOnly:true};
 native.display={stepPointMarkers:[marker,{...marker,coverageKey:'WRONG-COVERAGE'},{...marker,faceId:'102'},{...marker,sourcePathKey:'WRONG-SOURCE'},{...marker,sourceEdgeId:'12'},{...marker,lastParameter:Number.MIN_VALUE}]};
 const before=JSON.stringify(native),data=await loadWorkData('faceCutCoverage',entry,async()=>native);
 const html=renderWorkDetails(data,{selectedEdge:span.key});
 for(const text of ['STEP 点标记（仅展示）','原生区间仍严格正长','点标记不新增刀路','0 ～ 1e-323','10，20，30','判定待定'])assert.ok(html.includes(text),text);
 assert.equal((html.match(/原始 STEP 展示标记/g)??[]).length,1,'only the exact face/source/edge/native-domain record is joined');
 assert.ok(!html.includes('WRONG-COVERAGE'));assert.ok(!html.includes('WRONG-SOURCE'));
 assert.equal(JSON.stringify(native),before);assert.equal(data.edges.filter(e=>e.workCoverage).length,native.coverage.length);
 assert.deepEqual(data.edges.find(e=>e.pathKey===span.key).previewSegments,span.previewSegments);
});
test('path stage keeps exactly two short pale-yellow endpoint axes and skin-only copy',async()=>{
 const input=pathFixture(),data=await loadWorkData('pathAnalysis',{...entry,dataUrl:'/path.json',geometryUrl:'/path.json'},async()=>input);
 assert.deepEqual(data.edges,input.edges);assert.equal(data.edges[0].endpoints.length,2);
 assert.ok(workLegend('pathAnalysis').includes('#f4df91'));assert.ok(workLegend('pathAnalysis').includes('实际刀路仅取皮边'));
 assert.ok(workLegend('faceCutCoverage').includes('灰色细线'));assert.ok(WORK_LABELS.faceCutCoverage);
});
