// Targeted acceptance for the two marked slots in the actual 013.IGS model.
// Geometry expectations are independent of the production cross-product rule.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const task=path.join(repo,'output/tests/machining-path-analysis-20261105');
const args=process.argv.slice(2),option=(name,fallback)=>args.includes(name)?args[args.indexOf(name)+1]:fallback;
const beforePath=path.resolve(option('--before',path.join(task,'inward-axis-case22/before.json')));
const afterPath=option('--after','');if(!afterPath)throw Error('--after is required');
const afterFile=path.resolve(afterPath),reportFile=path.resolve(option('--report',path.join(task,'inward-axis-case22/targeted-validation.json')));
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const before=read(beforePath),after=read(afterFile),hashes=read(path.join(task,'inward-axis-case22/before-hashes.json'));
const checks=[],proof=[];const check=(passed,name)=>checks.push({name,passed:!!passed});
const sub=(a,b)=>a.map((v,i)=>v-b[i]),add=(a,b,s=1)=>a.map((v,i)=>v+s*b[i]);
const dot=(a,b)=>a.reduce((s,v,i)=>s+v*b[i],0),norm=a=>Math.hypot(...a);
const sameVector=(a,b,tolerance=1e-7)=>Array.isArray(a)&&Array.isArray(b)&&a.length===3&&b.length===3&&norm(sub(a,b))<=tolerance;

// Exact closest-point distance to each existing display triangle. This is
// acceptance-only: it does not add analysis parameters or tool-axis records.
function triangleDistance(p,a,b,c){
  const ab=sub(b,a),ac=sub(c,a),ap=sub(p,a),d1=dot(ab,ap),d2=dot(ac,ap);
  if(d1<=0&&d2<=0)return norm(ap);
  const bp=sub(p,b),d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return norm(bp);
  const vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return norm(sub(p,add(a,ab,d1/(d1-d3))));
  const cp=sub(p,c),d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return norm(cp);
  const vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return norm(sub(p,add(a,ac,d2/(d2-d6))));
  const va=d3*d6-d5*d4;
  if(va<=0&&d4-d3>=0&&d5-d6>=0)return norm(sub(p,add(b,sub(c,b),(d4-d3)/(d4-d3+d5-d6))));
  const denominator=1/(va+vb+vc);return norm(sub(p,add(add(a,ab,vb*denominator),ac,vc*denominator)));
}
function faceDistance(face,p){
  let distance=Infinity;
  for(let i=0;i<face.indices.length;i+=3){
    const [a,b,c]=face.indices.slice(i,i+3).map(index=>face.positions.slice(index*3,index*3+3));
    const candidate=triangleDistance(p,a,b,c);if(Number.isFinite(candidate))distance=Math.min(distance,candidate);
  }
  return distance;
}
function boundaryProofPoint(edge,face){
  const [a,b]=edge.endpoints.map(endpoint=>endpoint.position),mid=a.map((v,i)=>(v+b[i])/2);
  if(edge.curveKind==='Line')return mid;
  const vertices=[];for(let i=0;i<face.positions.length;i+=3)vertices.push(face.positions.slice(i,i+3));
  // Choose an existing interior mesh vertex on this top curved boundary,
  // avoiding the corner where a second boundary would also constrain motion.
  const candidates=vertices.filter(p=>Math.abs(p[2]-a[2])<1e-6
    &&p[0]>=Math.min(a[0],b[0])-1e-6&&p[0]<=Math.max(a[0],b[0])+1e-6
    &&p[1]>=Math.min(a[1],b[1])-1e-6&&p[1]<=Math.max(a[1],b[1])+1e-6);
  candidates.sort((p,q)=>norm(sub(p,mid))-norm(sub(q,mid)));
  if(!candidates.length)throw Error(`No existing top-boundary mesh vertex for ${edge.pathKey}`);
  return candidates[0];
}

check(after.name==='013.IGS'&&after.success&&after.complete,'actual case22 completed successfully');
check(after.analysisGranularity==='logical-edge-parts','analysis remains at logical-edge granularity');
check(Object.entries(before.counts).every(([key,value])=>after.counts[key]===value)
  &&Object.keys(after.counts).every(key=>Object.hasOwn(before.counts,key)
    ||(key==='unresolvedBoundaries'&&after.counts[key]===0)),
  'logical-path, side, parallel and whole-model counts are unchanged; omitted legacy unresolved count means zero');
check(JSON.stringify(before.geometry)===JSON.stringify(after.geometry),'all original complete-model mesh geometry is unchanged');
check(after.edges.every(edge=>edge.endpoints.length===2),'every logical record still contains exactly two endpoints');
const beforeEdges=new Map(before.edges.map(edge=>[edge.pathKey,edge]));
const originalIdentity=edge=>({key:edge.pathKey,faceId:edge.faceId,sourceEdgeIds:edge.sourceEdgeIds,sourceUses:edge.sourceUses,
  endpoints:edge.endpoints.map(p=>({endpoint:p.endpoint,sourceEdgeId:p.sourceEdgeId,sourceSpanKey:p.sourceSpanKey,parameter:p.parameter,position:p.position}))});
check(before.edges.length===after.edges.length&&after.edges.every(edge=>beforeEdges.has(edge.pathKey)
  &&JSON.stringify(originalIdentity(beforeEdges.get(edge.pathKey)))===JSON.stringify(originalIdentity(edge))),
  'all original edge identities, complete source chains, endpoint positions and parameters are unchanged');
let axisCount=0,maxAxisNormalDot=0;
for(const edge of after.edges)if(edge.path)for(const endpoint of edge.endpoints)if(endpoint.axisValid){
  axisCount++;maxAxisNormalDot=Math.max(maxAxisNormalDot,Math.abs(dot(endpoint.axis,endpoint.normal)));
}
check(axisCount===1794&&maxAxisNormalDot<1e-8,'all 1794 valid endpoint axes remain perpendicular to their own machining-face normal');

const targets=[
  {key:'F224:W240:G7:P1',source:'1066',expected:[0,0,-1],mode:'StraightGenerator',kind:'RuledBSplineV'},
  {key:'F225:W241:G3:P1',source:'1070',expected:[0,0,-1],mode:'StraightGenerator',kind:'LinearExtrusion'},
  {key:'F203:W219:G4:P1',source:'977',expected:[0,1,0],mode:'PlaneTangentCrossNormal',kind:'Plane'},
  {key:'F204:W220:G4:P1',source:'981',expected:[0,1,0],mode:'PlaneTangentCrossNormal',kind:'Plane'},
];
for(const target of targets){
  const edge=after.edges.find(edge=>edge.pathKey===target.key),face=after.geometry.faces.find(face=>face.faceId===edge?.faceId);
  check(edge&&face&&edge.path&&edge.axisMode===target.mode&&edge.surfaceKind===target.kind
    &&edge.sourceEdgeIds.length===1&&edge.sourceEdgeIds[0]===target.source,`${target.key} retains its actual source face, edge and support family`);
  if(!edge||!face)continue;
  check(edge.endpoints.length===2&&edge.endpoints.every(p=>p.axisValid&&sameVector(p.axis,target.expected)
    &&Math.abs(dot(p.axis,p.normal))<1e-8),`${target.key} has exactly two independently expected inward endpoint axes`);
  if(target.mode==='StraightGenerator')check(edge.endpoints.every(p=>sameVector(p.generator,[0,0,-1])&&sameVector(p.axis,p.generator)),
    `${target.key} retains its certified -Z generator without reversing it outside the top trim`);
  const boundary=boundaryProofPoint(edge,face),step=.1;
  const insideDistance=faceDistance(face,add(boundary,target.expected,step));
  const outsideDistance=faceDistance(face,add(boundary,target.expected,-step));
  check(faceDistance(face,boundary)<1e-6&&insideDistance<.001&&outsideDistance>.09,
    `${target.key} points into its independently checked existing face trim, while the opposite direction leaves it`);
  proof.push({pathKey:edge.pathKey,sourceEdgeId:target.source,expectedAxis:target.expected,
    endpoints:edge.endpoints,boundaryProofPoint:boundary,stepMm:step,insideFaceDistanceMm:insideDistance,
    oppositeFaceDistanceMm:outsideDistance,verificationGeometry:'unchanged existing face mesh; tolerance 0.001 mm'});
}

const cache=read(path.join(repo,'TestData/Tube/PathAnalysis/cache-manifest.json')).models.find(model=>Number(model.id)===22);
check(hash(cache.originalCad.path)===cache.originalCad.sha256&&hashes.sourceCad===cache.originalCad.sha256,'the actual original 013.IGS CAD bytes are unchanged');
for(const [name,file] of Object.entries(cache.files))check(hash(file.path)===file.sha256,`exact Classify cache ${name} is byte-for-byte unchanged`);
const afterBrep=afterFile.replace(/\.json$/i,'.brep');
check(fs.existsSync(afterBrep)&&hash(afterBrep)===hashes.case22Brep,'the complete original-model BREP bytes are unchanged');

const report={status:checks.every(check=>check.passed)?'passed':'failed',checks,failedChecks:checks.filter(check=>!check.passed).length,
  model:'013.IGS',axisCount,maxAxisNormalDot,proof,
  inputs:{before:{path:beforePath,sha256:hash(beforePath)},after:{path:afterFile,sha256:hash(afterFile)}}};
fs.mkdirSync(path.dirname(reportFile),{recursive:true});fs.writeFileSync(reportFile,JSON.stringify(report,null,2));
process.stdout.write(JSON.stringify({status:report.status,checks:checks.length,failedChecks:report.failedChecks,report:reportFile})+'\n');
process.exitCode=report.failedChecks?1:0;
