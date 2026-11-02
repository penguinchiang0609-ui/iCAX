'use strict';
// Offline acceptance oracle. Reads captured native results only; it never
// connects to CEF, injects input, changes a project, or controls the OS pointer.
// Bundle: {state, snapshot, plan, exported, reopenedPlan?}. `state` is the real
// CamScene.ReadState result; the other fields are ReadMachine, ReadMachinePlan
// and ExportMachineProgram results from the same accepted scene revision.
// Optional `mounting` is MountMachine's result, and `ncContent` is the actual
// emitted InverseTimeNC text (or exported.content when that format was used).
// Optional `jointJSONContent` is the actual JointJSON file text; unlike `motion`
// it also verifies the emitted bytes parse to the exact inspected document.
const fs=require('node:fs'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const identity=()=>[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const mul=(a,b)=>Array.from({length:16},(_,i)=>{const r=i>>2,c=i%4;let n=0;for(let k=0;k<4;k++)n+=a[r*4+k]*b[k*4+c];return n;});
const transform=(m,p,d=false)=>[0,1,2].map(r=>m[r*4]*p[0]+m[r*4+1]*p[1]+m[r*4+2]*p[2]+(d?0:m[r*4+3]));
const inverse=m=>{const n=identity();for(let r=0;r<3;r++)for(let c=0;c<3;c++)n[r*4+c]=m[c*4+r];for(let r=0;r<3;r++)n[r*4+3]=-(n[r*4]*m[3]+n[r*4+1]*m[7]+n[r*4+2]*m[11]);return n;};
const norm=v=>Math.hypot(...v),unit=v=>{const n=norm(v);assert.ok(n>1e-12&&Number.isFinite(n),'Finite nonzero direction');return v.map(x=>x/n);};
const difference=(a,b)=>Math.hypot(...a.map((x,i)=>x-b[i]));
const angular=(a,b)=>{a=unit(a);b=unit(b);return 2*Math.asin(Math.min(1,difference(a,b)/2));};
const interpolateDirection=(a,b,u)=>{a=unit(a);b=unit(b);const angle=angular(a,b);if(angle<1e-8)return unit(a.map((x,k)=>x+(b[k]-x)*u));if(Math.PI-angle<1e-8){const basis=[0,0,0];let k=0;for(let i=1;i<3;i++)if(Math.abs(a[i])<Math.abs(a[k]))k=i;basis[k]=1;const axis=unit([a[1]*basis[2]-a[2]*basis[1],a[2]*basis[0]-a[0]*basis[2],a[0]*basis[1]-a[1]*basis[0]]),cross=[axis[1]*a[2]-axis[2]*a[1],axis[2]*a[0]-axis[0]*a[2],axis[0]*a[1]-axis[1]*a[0]];return a.map((x,k)=>x*Math.cos(angle*u)+cross[k]*Math.sin(angle*u));}return unit(a.map((x,k)=>(x*Math.sin((1-u)*angle)+b[k]*Math.sin(u*angle))/Math.sin(angle)));};
const joint=(node,q)=>{const m=identity();if(node.type==='fixed')return m;const [x,y,z]=unit(node.axis);if(node.type==='prismatic'){m[3]=x*q;m[7]=y*q;m[11]=z*q;return m;}const c=Math.cos(q),s=Math.sin(q),t=1-c;return [t*x*x+c,t*x*y-s*z,t*x*z+s*y,0,t*x*y+s*z,t*y*y+c,t*y*z-s*x,0,t*x*z-s*y,t*y*z+s*x,t*z*z+c,0,0,0,0,1];};
function forward(snapshot,q){assert.equal(q.length,snapshot.nodes.length);const world=[];snapshot.nodes.forEach((n,i)=>{assert.ok(n.parent<i,'Topological native machine tree');const local=mul(n.origin,joint(n,q[i]));world.push(n.parent<0?local:mul(world[n.parent],local));});return world;}
const canonical=v=>Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v;
const hash=v=>crypto.createHash('sha256').update(JSON.stringify(canonical(v))).digest('hex');
// JSON numbers have one numeric zero value. Native text may spell it `-0`
// while its enclosing JS transport serializes it `0`; preserve every nonzero
// double exactly and report this sole representation normalization explicitly.
const normalizeJSONZeros=v=>typeof v==='number'&&v===0?0:Array.isArray(v)?v.map(normalizeJSONZeros):v&&typeof v==='object'?Object.fromEntries(Object.entries(v).map(([k,x])=>[k,normalizeJSONZeros(x)])):v;
const negativeZeroCount=v=>typeof v==='number'?Number(Object.is(v,-0)):Array.isArray(v)?v.reduce((n,x)=>n+negativeZeroCount(x),0):v&&typeof v==='object'?Object.values(v).reduce((n,x)=>n+negativeZeroCount(x),0):0;
const zeroSignDifferenceCount=(a,b)=>typeof a==='number'&&typeof b==='number'?Number(a===0&&b===0&&!Object.is(a,b)):a&&b&&typeof a==='object'&&typeof b==='object'?Object.keys(a).reduce((n,k)=>n+zeroSignDifferenceCount(a[k],b[k]),0):0;
function stationsFromState(state){
 const setup=state.setups.find(s=>s.id===state.activeSetupId);assert.ok(setup,'Active native setup');
 const geometry=setup.generatedGeometry;assert.equal(geometry.status,'Current','Actual CAM geometry must be current');
 const frames=[],height=Number(setup.rapidSettings.followHeightMm);assert.ok(Number.isFinite(height)&&height>=0);
 const append=(part,m,isRapid,intentId,order)=>{assert.equal(part.points.length,part.directions.length);part.points.forEach((p,i)=>{const axis=unit(transform(m,unit(part.directions[i]),true)),position=transform(m,p);if(!isRapid)for(let k=0;k<3;k++)position[k]+=height*axis[k];frames.push({position,beam:axis.map(x=>-x),role:isRapid?'Rapid':part.role,intentId,order});});};
 geometry.operations.forEach((op,i)=>{const placement=state.placements.find(p=>p.id===op.placementId);assert.ok(placement);for(const part of op.parts)append(part,placement.transform,false,op.intentId,op.order);const next=geometry.operations[i+1];if(!next)return;const t=geometry.transitions.find(t=>t.from===op.intentId&&t.to===next.intentId);assert.ok(t,'Every operation pair requires a real rapid');assert.ok(['Generated','Draft'].includes(t.geometryStatus));assert.equal(t.frame,'Setup');assert.equal(t.referencePoint,'ToolTip');assert.ok(t.points.length>1);append(t,identity(),true,next.intentId,`${op.order} → ${next.order}`);});
 return {setup,geometry,frames};
}
function audit(bundle,headEnvelopeAudit=bundle.headEnvelopeAudit){
 const {state,snapshot,plan}=bundle,exported=bundle.exported?.motion||bundle.exported;assert.ok(state&&snapshot&&plan&&exported,'Native state, snapshot, accepted plan and actual export required');
 const checks=[],check=(name,fn)=>{try{const detail=fn();checks.push({name,passed:true,...(detail===undefined?{}:{detail})});}catch(e){checks.push({name,passed:false,error:e.message});}};
 let source;check('Native plan is source-current, collision-clear and playable',()=>{assert.equal(plan.validity,'Current');assert.equal(plan.status,'Ready');assert.equal(plan.collisionStatus,'Clear');assert.equal(plan.playbackAllowed,true);assert.ok(plan.frames.length>2);assert.equal(plan.modelKey,snapshot.modelKey);assert.equal(plan.machineReady,false);assert.equal(plan.productionQualified,false);assert.equal(plan.interpolationCoverage,'SampledApproximation');assert.equal(plan.timingQualification,'VelocityOnly');assert.equal(plan.collisionScope,'MachineDeclaredBodiesAndOwnMesh');assert.equal(plan.collisionAlgorithm,'MachineBodyMeshV4');});
 if(headEnvelopeAudit)check('The native machine uses the exact full-triangle audited physical head envelope',()=>{
  assert.equal(headEnvelopeAudit.passed,true);assert.equal(headEnvelopeAudit.lengthUnit,'mm');assert.ok(headEnvelopeAudit.meshes.length>0);
  const actual=snapshot.collisionDefinitions.find(d=>d.id===snapshot.nodes[snapshot.tool].id)?.geometry;assert.ok(actual,'Native tool colliders');assert.equal(actual.length,headEnvelopeAudit.boxes.length);
  for(const box of headEnvelopeAudit.boxes){const geometry=actual.find(g=>(g.collisionName||g.name)===box.name);assert.ok(geometry,`Native head box ${box.name}`);assert.equal(geometry.geometry.type,'box');assert.equal(geometry.pose.length,6);for(let k=0;k<3;k++){assert.ok(Math.abs(geometry.pose[k]*1000-(box.min[k]+box.max[k])/2)<1e-8,`Native box origin ${box.name}`);assert.ok(Math.abs(geometry.geometry.size[k]*1000-(box.max[k]-box.min[k]))<1e-8,`Native box size ${box.name}`);assert.ok(Math.abs(geometry.pose[k+3])<1e-12,`Native box orientation ${box.name}`);}}
  for(const mesh of headEnvelopeAudit.meshes){assert.equal(mesh.uncoveredTriangleCount,0);assert.equal(mesh.uncoveredAreaMm2,0);}
  return {urdfSha256:headEnvelopeAudit.urdfSha256,meshes:headEnvelopeAudit.meshes.length,triangles:headEnvelopeAudit.meshes.reduce((n,m)=>n+m.triangles,0),nativeCollisionBoxes:actual.length};
 });
 if(headEnvelopeAudit)check('The accepted plan freezes all six real tool sources and their proven material volumes',()=>{
  assert.equal(snapshot.configuration.toolCollisionRepresentation,'OwnMesh');assert.equal(plan.collisionRepresentation,'ToolOwnMesh; OtherMachineBoxes; WorkpieceFixtureOwnMesh');
  const summary=snapshot.toolMesh;assert.ok(summary);assert.equal(Number(summary.sourceCount),6);assert.equal(Number(summary.triangleCount),4144);assert.equal(Number(summary.closedVolumeCount),18);assert.equal(summary.sources.length,6);
  assert.deepEqual(plan.toolMesh,summary);assert.deepEqual(exported.machine.toolMesh,summary);
  assert.deepEqual(summary.sources.map(s=>Number(s.triangleCount)).sort((a,b)=>a-b),headEnvelopeAudit.meshes.map(m=>m.triangles).sort((a,b)=>a-b));
  const refs=new Set();for(const s of summary.sources){assert.equal(typeof s.resource.url,'string');assert.ok(s.resource.url);assert.ok(/^\d+$/.test(String(s.resource.version))&&BigInt(s.resource.version)>0n);assert.ok(!refs.has(s.resource.url));refs.add(s.resource.url);assert.ok(s.localMatrix.length===16&&s.localMatrix.every(Number.isFinite));assert.ok(Number(s.closedVolumeCount)>0);}
  for(let k=0;k<3;k++){const lo=Math.min(...headEnvelopeAudit.meshes.map(m=>m.bounds.min[k])),hi=Math.max(...headEnvelopeAudit.meshes.map(m=>m.bounds.max[k]));assert.ok(Math.abs(summary.localBounds.min[k]-lo)<.0001);assert.ok(Math.abs(summary.localBounds.max[k]-hi)<.0001);}
  return {fixedSourceVersions:summary.sources.map(s=>s.resource),triangleCount:4144,provenClosedVolumes:18,localBounds:summary.localBounds};
 });
 check('Real source contains all 31 cuts and all 30 connecting rapids',()=>{source=stationsFromState(state);assert.equal(source.geometry.operations.length,31);assert.equal(source.geometry.transitions.length,30);assert.equal(new Set(source.geometry.operations.map(o=>o.intentId)).size,31);assert.equal(Number(plan.sourceStationCount),source.frames.length);return {sourceStations:source.frames.length,cutOperations:31,rapidConnections:30};});
 check('LC-3015 physical chain has three translation axes then C and A',()=>{const chain=[];let i=snapshot.tool;while(i>=0){if(snapshot.nodes[i].type!=='fixed')chain.unshift(i);i=snapshot.nodes[i].parent;}assert.equal(chain.length,5);const expected=[['prismatic',[1,0,0],-1500,1500],['prismatic',[0,1,0],-750,750],['prismatic',[0,0,1],-450,150],['revolute',[0,0,1],-Math.PI,Math.PI],['revolute',[1,0,0],-2.09439510239,2.09439510239]];chain.forEach((i,k)=>{const n=snapshot.nodes[i],e=expected[k];assert.equal(n.type,e[0]);assert.ok(difference(unit(n.axis),e[1])<1e-10);assert.ok(Math.abs(n.lower-e[2])<1e-7);assert.ok(Math.abs(n.upper-e[3])<1e-7);});assert.equal(snapshot.nodes.filter(n=>n.type!=='fixed').length,5);return {physicalAxisNodeIndices:chain,tcpMm:snapshot.configuration.tcp,beam:snapshot.configuration.beam};});
 if(bundle.mounting)check('Native mounting contains the whole enabled assembly and preserves the actual initial joints',()=>{
  const result=bundle.mounting,proof=result.mounting,setup=state.setups.find(s=>s.id===state.activeSetupId),included=[];
  for(const p of state.placements)if(p.setupId===setup.id&&state.workpieces.find(w=>w.id===p.workpieceId)?.enabled!==false)included.push(p.id);
  for(const f of state.fixtures)if(f.setupId===setup.id&&f.enabled!==false)included.push(f.id);
  assert.deepEqual([...proof.includedEntityIds].sort(),included.sort());assert.equal(proof.collisionStatus,'Clear');assert.equal(proof.calibrated,false);
  assert.deepEqual(result.configuration,snapshot.configuration);assert.deepEqual(proof.initialAxisPositions,snapshot.positions);
  const {min:lo,max:hi}=proof.setupBounds,{min:tableLo,max:tableHi}=proof.supportBounds,p=result.configuration.placement;
  for(let k=0;k<2;k++){assert.ok(hi[k]-lo[k]<=tableHi[k]-tableLo[k]+.0100001);assert.ok(Math.abs((lo[k]+hi[k])/2+p[4*k+3]-(tableLo[k]+tableHi[k])/2)<1e-7);}
  assert.ok(Math.abs(lo[2]+p[11]-proof.surfaceZMm)<1e-7);assert.deepEqual(result.configuration.tcp,[0,0,-220]);assert.deepEqual(result.configuration.beam,[0,0,-1]);return {includedEntities:included.length,surfaceZMm:proof.surfaceZMm,initialAxisPositions:proof.initialAxisPositions,mountReceiptCollisionRepresentation:result.configuration.toolCollisionRepresentation,acceptedPlanCollisionRepresentation:snapshot.configuration.toolCollisionRepresentation};
 });
 check('Every stored axis frame passes independent FK, TCP and tool-pose comparison',()=>{
  let maxTCP=0,maxToolMatrix=0,maxBeam=0,maxSourceTCP=0,maxEndpointAngle=0,maxVelocityRatio=0;const covered=new Set(),cutIds=new Set(),rapidIds=new Set();let processRuns=0,oldLaser=false,previousSource=-1,previousFraction=0;
  const frames=plan.frames,placement=snapshot.configuration.placement,tcp=snapshot.configuration.tcp,beam=snapshot.configuration.beam;
  for(let i=0;i<frames.length;i++){
   const f=frames[i],q=f.joints;assert.ok(q.every(Number.isFinite));q.forEach((x,k)=>{const n=snapshot.nodes[k];assert.ok(x>=n.lower-1e-8&&x<=n.upper+1e-8,`Axis limit frame ${i}, node ${k}`);if(n.type==='fixed')assert.ok(Math.abs(x)<1e-10);if(i&&n.type!=='fixed'){const ratio=Math.abs(x-frames[i-1].joints[k])/f.durationSeconds/n.velocity;maxVelocityRatio=Math.max(maxVelocityRatio,ratio);assert.ok(ratio<=1+1e-7,`Axis velocity frame ${i}, node ${k}: ${ratio}`);}});
   if(i){assert.ok(f.durationSeconds>0);assert.ok(Math.abs(f.timeSeconds-frames[i-1].timeSeconds-f.durationSeconds)<1e-7);}else assert.equal(f.durationSeconds,0);
   const world=forward(snapshot,q),relative=mul(inverse(world[snapshot.support]),world[snapshot.tool]),actualTCP=transform(relative,tcp),actualBeam=unit(transform(relative,beam,true));
   maxTCP=Math.max(maxTCP,difference(actualTCP,transform(placement,f.position)));maxToolMatrix=Math.max(maxToolMatrix,...relative.map((x,k)=>Math.abs(x-f.actualToolPose[k])));maxBeam=Math.max(maxBeam,difference(actualBeam,f.actualBeamDirection));
   assert.ok(maxTCP<1e-6,`Stored TCP differs from physical FK frame ${i}`);assert.ok(maxToolMatrix<1e-7,`Stored tool pose differs from physical FK frame ${i}`);assert.ok(maxBeam<1e-7,`Stored beam differs from physical FK frame ${i}`);
   assert.equal(typeof f.laser,'boolean');if(f.laser&&!oldLaser)processRuns++;oldLaser=f.laser;
   if(source&&f.sourceStation!=='Initial'){
    const s=Number(f.sourceStation),u=f.sourceFraction;assert.ok(Number.isInteger(s)&&s>=0&&s<source.frames.length);assert.ok(Number.isFinite(u)&&u>=0&&u<=1);
    assert.ok(s===previousSource||s===previousSource+1,`Source station order frame ${i}`);if(s===previousSource)assert.ok(u>previousFraction,`Source fraction order frame ${i}`);else if(previousSource>=0)assert.equal(previousFraction,1,`Previous source endpoint frame ${i}`);previousSource=s;previousFraction=u;
    if(u===1)covered.add(s);const station=source.frames[s];if(u===1){if(station.role==='Rapid')rapidIds.add(station.intentId);else cutIds.add(station.intentId);assert.equal(f.intentId,station.intentId);}
    if(s>0||u===1){const prev=source.frames[Math.max(0,s-1)],target=station.position.map((x,k)=>s?prev.position[k]+u*(x-prev.position[k]):x),error=difference(actualTCP,transform(placement,target));maxSourceTCP=Math.max(maxSourceTCP,error);assert.ok(error<=.0100001,`Authored source TCP mismatch frame ${i}: ${error} mm`);if(s>0&&prev.role!=='Rapid'&&station.role!=='Rapid'){const error=angular(actualBeam,transform(placement,interpolateDirection(prev.beam,station.beam,u),true));maxEndpointAngle=Math.max(maxEndpointAngle,error);assert.ok(error<=.00010001,`Authored cut interpolation direction mismatch frame ${i}: ${error} rad`);}}
    if(u===1&&station.role!=='Rapid'){const error=angular(actualBeam,transform(placement,station.beam,true));maxEndpointAngle=Math.max(maxEndpointAngle,error);assert.ok(error<=.00010001,`Authored cut direction mismatch frame ${i}: ${error} rad`);}
    const at=u<1?s-1:s,a=source.frames[at],b=source.frames[at+1],expected=s===0&&u<1?false:!!(a&&b&&a.role!=='Rapid'&&b.role!=='Rapid'&&a.intentId===b.intentId);assert.equal(f.laser,expected,`Laser boundary frame ${i}`);
   }else assert.equal(f.laser,false,'Initial pose is process off');
  }
  if(source){assert.equal(covered.size,source.frames.length,'Every CAM station has an accepted endpoint');assert.equal(cutIds.size,31);assert.equal(rapidIds.size,30);assert.equal(previousSource,source.frames.length-1);assert.equal(previousFraction,1);}assert.equal(Number(plan.sampleCount),frames.length);assert.ok(Math.abs(plan.durationSeconds-frames.at(-1).timeSeconds)<1e-7);assert.equal(processRuns,31,'Exactly 31 cutting engagements');assert.equal(frames.at(-1).laser,false);
  return {frameCount:frames.length,coveredSourceStations:covered.size,cutOperations:cutIds.size,rapidConnections:rapidIds.size,processRuns,maxTCPmm:maxTCP,maxToolMatrix,maxBeam,maxAuthoredTCPmm:maxSourceTCP,maxCutDirectionErrorRad:maxEndpointAngle,maxVelocityRatio};
 });
 check('Export consumes the exact stored axes and laser engagement for each move',()=>{assert.equal(exported.format,'ICAX-JOINT-MOTION-1');assert.equal(exported.interpolation,'JointLinear');assert.equal(exported.machineReady,false);assert.deepEqual(exported.initialJoints,plan.frames[0].joints);assert.equal(exported.instructions.length,plan.frames.length);exported.instructions.forEach((ins,i)=>{const f=plan.frames[i];assert.deepEqual(ins.joints,f.joints);assert.equal(ins.durationSeconds,f.durationSeconds);assert.equal(ins.process,i>0&&plan.frames[i-1].laser);assert.equal(ins.role,f.role);exported.postprocessor.axes.forEach(a=>{const k=snapshot.nodes.findIndex(n=>n.id===a.jointId);assert.ok(k>=0);let x=f.joints[k];if(snapshot.nodes[k].type!=='prismatic')x*=180/Math.PI;x=x*a.sign+a.offset;assert.ok(Math.abs(ins.axes[a.word]-x)<1e-8,`Export axis ${a.word} at frame ${i}`);});});return {instructions:exported.instructions.length,motionMoves:exported.instructions.length-1,planSha256:hash(plan)};});
 const jointJSON=bundle.jointJSONContent??(bundle.exported?.format==='JointJSON'?bundle.exported.content:undefined);
 if(jointJSON!==undefined)check('Actual JointJSON file equals the complete inspected motion document',()=>{assert.equal(typeof jointJSON,'string');const parsed=JSON.parse(jointJSON.replace(/^\uFEFF/,''));assert.deepEqual(normalizeJSONZeros(parsed),normalizeJSONZeros(exported));assert.deepEqual(parsed.instructions,exported.instructions);return {instructions:exported.instructions.length,numberComparison:'Exact IEEE double values; numeric zero sign ignored only',zeroSignOnlyDifferences:zeroSignDifferenceCount(parsed,exported),nonzeroNumberOrStructuralDifferences:0,instructionDifferences:0,negativeZeroValuesInActualFile:negativeZeroCount(parsed),negativeZeroValuesInHydratedMotion:negativeZeroCount(exported),contentSha256:crypto.createHash('sha256').update(jointJSON).digest('hex')};});
 const nc=bundle.ncContent??(bundle.exported?.format==='InverseTimeNC'?bundle.exported.content:undefined);
 if(nc!==undefined)check('Actual NC text executes the same axes, inverse-time feed and laser sequence',()=>{
  assert.equal(typeof nc,'string');const post=exported.postprocessor,lines=nc.replaceAll('\r','').split('\n').map(s=>s.trim()).filter(Boolean),on=post.processOn.trim(),off=post.processOff.trim();
  assert.ok(on&&!on.includes('\n')&&off&&!off.includes('\n'),'This text audit supports single-line process commands');assert.ok(lines.includes('G93')||lines.some(s=>/(?:^|\s)G93(?:\s|$)/.test(s)),'Inverse-time feed must be declared');
  let active=false,foundOff=false,moves=0,onCount=0;
  for(const line of lines){if(line.startsWith('(')||line.startsWith(';'))continue;if(line===off){active=false;foundOff=true;continue;}if(line===on){active=true;onCount++;continue;}
   if(!/^(?:N\d+\s+)?G0?1(?:\s|[XYZABCUVW])/.test(line))continue;
   assert.ok(foundOff,'A real process-off command must precede all motion');const ins=exported.instructions[++moves];assert.ok(ins,'NC contains an extra motion');assert.equal(active,ins.process,`NC laser state at move ${moves}`);
   const words=Object.fromEntries([...line.matchAll(/([XYZABCUVWF])\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)/g)].map(m=>[m[1],Number(m[2])]));
   for(const a of post.axes){assert.ok(Number.isFinite(words[a.word]),`Missing NC axis ${a.word}`);assert.ok(Math.abs(words[a.word]-ins.axes[a.word])<=.00000051,`NC axis ${a.word} at move ${moves}`);}
   assert.ok(Math.abs(words.F-60/ins.durationSeconds)<=.0000000011,`NC inverse-time feed at move ${moves}`);
  }
  assert.equal(moves,exported.instructions.length-1);assert.equal(onCount,31);assert.equal(active,false,'NC ends with laser off');return {moves,laserEngagements:onCount,contentSha256:crypto.createHash('sha256').update(nc).digest('hex')};
 });
 if(bundle.reopenedPlan)check('Save and native reopen preserve the exact accepted plan',()=>{assert.equal(hash(bundle.reopenedPlan),hash(plan));return {sha256:hash(plan)};});
 return {passed:checks.every(c=>c.passed),scope:'Offline independent physical FK and native result audit; continuous collision certification is the native report, no controller dynamics or physical-machine calibration asserted.',checks};
}
module.exports={audit,forward,stationsFromState};
if(require.main===module){const args=process.argv.slice(2),get=n=>{const i=args.indexOf(n);assert.ok(i>=0&&args[i+1],`Required ${n}`);return args[i+1];};const input=get('--bundle'),output=get('--output');let report;try{const envelope=args.includes('--head-envelope')?JSON.parse(fs.readFileSync(get('--head-envelope'),'utf8')):undefined;report=audit(JSON.parse(fs.readFileSync(input,'utf8')),envelope);}catch(e){report={passed:false,error:e.stack};}fs.writeFileSync(output,JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));process.exitCode=report.passed?0:1;}
