import test from 'node:test';
import assert from 'node:assert/strict';
import {workpieceViewSnapshot,workpieceViewSnapshotApplied} from '../../apps/five-axis-cam/webpage/workpieceViewSnapshot.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
function fixture(){
 const history={fits:0,restored:[],camera:{target:[0,0,0],position:[160,160,160]}};
 const view={fiveAxisArea:'workpiece',activeAreaId:'workpiece',viewport:{getCameraState:()=>structuredClone(history.camera),setCameraState:camera=>{history.camera=structuredClone(camera);history.restored.push(camera);},fitViewToViewport:()=>{history.fits++;return true;}}};
 const show=(area,matrix=identity)=>{view.fiveAxisArea=area;view.activeAreaId=area;const result=workpieceViewSnapshot({},view,area,{rows:[{entityId:'part',data:{setupPlacementTransform:matrix}}]});workpieceViewSnapshotApplied({},view,area);return result;};
 return {view,history,show};
}

test('far-away operation placement does not reuse the source camera; returning restores the source view',()=>{
 const f=fixture();f.show('workpiece');assert.equal(f.history.fits,1);
 const source={target:[0,0,0],position:[100,200,300]};f.history.camera=structuredClone(source);
 const far=[...identity];far[3]=10000;const transformed=f.show('operation',far);
 assert.equal(f.history.fits,2);assert.deepEqual(transformed.rows[0].data.localToWorldMatrix,far);
 const operation={target:[10000,0,0],position:[10100,200,300]};f.history.camera=structuredClone(operation);
 f.show('workpiece');assert.deepEqual(f.history.camera,source);assert.equal(f.history.fits,2);
 f.show('operation',far);assert.deepEqual(f.history.camera,operation);assert.equal(f.history.fits,2);
});

test('operation and toolpath keep one installation frame camera and a late source snapshot cannot restore another page camera',()=>{
 const f=fixture();f.show('operation');const operation={target:[10000,0,0],position:[10400,200,300]};f.history.camera=structuredClone(operation);
 f.show('toolpath');assert.equal(f.history.fits,1);assert.deepEqual(f.history.camera,operation);
 f.show('workpiece');const source={target:[0,0,0],position:[20,30,40]};f.history.camera=structuredClone(source);
 workpieceViewSnapshot({},f.view,'operation',{rows:[]});f.view.activeAreaId='workpiece';
 workpieceViewSnapshotApplied({},f.view,'operation');assert.deepEqual(f.history.camera,source);
});
