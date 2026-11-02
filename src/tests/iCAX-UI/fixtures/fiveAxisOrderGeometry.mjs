import assert from 'node:assert/strict';
// Compare geometry, independently of preview-only operation identities and
// observed source fingerprints, which are rebound by the native transaction.
export function compareOrderPreviewGeometry(predicted,actual){
 const byId=new Map(actual.map(op=>[op.intentId,op]));let coordinates=0,maxPointError=0,maxDirectionError=0;
 const vectors=(a,b,direction=false)=>{assert.equal(a.length,b.length,'Sample count');for(let i=0;i<a.length;i++){assert.equal(a[i].length,b[i].length);for(let k=0;k<a[i].length;k++){const error=Math.abs(a[i][k]-b[i][k]);if(direction)maxDirectionError=Math.max(maxDirectionError,error);else maxPointError=Math.max(maxPointError,error);coordinates++;}}};
 for(const p of predicted){const a=byId.get(p.intentId);assert.ok(a,'Generated operation '+p.intentId);for(const key of ['start','end','setupStart','setupEnd'])vectors([p[key]],[a[key]]);
  assert.equal(p.parts.length,a.parts.length,'Part count '+p.intentId);for(let i=0;i<p.parts.length;i++){assert.equal(p.parts[i].role,a.parts[i].role);vectors(p.parts[i].points,a.parts[i].points);vectors(p.parts[i].directions,a.parts[i].directions,true);}}
 assert.ok(maxPointError<=1e-8,'Preview point error '+maxPointError);assert.ok(maxDirectionError<=1e-10,'Preview direction error '+maxDirectionError);
 return {operationCount:predicted.length,coordinates,maxPointError,maxDirectionError};
}
