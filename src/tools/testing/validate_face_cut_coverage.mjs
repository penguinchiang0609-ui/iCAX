// Verify native source-skin coverage without reconstructing a second mapper.
// Exact native event unions preserve narrow positive intervals and local gaps.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const args=process.argv.slice(2);
const option=(key,fallback)=>args.includes(key)?args[args.indexOf(key)+1]:fallback;
const run=path.resolve(option('--run','output/tests/skin-path-evidence-20261109/coverage-batch-v3'));
const publish=path.resolve(option('--publish',path.join(run,'publish/FaceCutCoverage')));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const summary=read(path.join(run,'summary.json'));
const manifest=read(path.join(publish,'manifest.json'));
const errors=[],pending=[];
const totals={models:0,sources:0,arrivals:0,evidence:0,rawEvidence:0,boundarySetEvidence:0,sideCompletions:0,coverageSpans:0,through:0,partial:0,pending:0};
let checks=0,errorCount=0;
function check(ok,name){checks++;if(!ok){errorCount++;if(errors.length<200)errors.push(name);}}
const sameId=(a,b)=>String(a)===String(b);
const unique=values=>[...new Set(values)];
const sameSet=(a,b)=>a.length===b.length&&new Set(a).size===a.length&&a.every(value=>b.includes(value));
const sourceRange=arrival=>[Math.min(arrival.sourceFirstParameter,arrival.sourceLastParameter),Math.max(arrival.sourceFirstParameter,arrival.sourceLastParameter)];
const contains=(use,edgeId,first,last)=>sameId(use.edgeId,edgeId)&&use.firstParameter<=first&&use.lastParameter>=last;
function intervalCovered(first,last,ranges){
 if(!Number.isFinite(first)||!Number.isFinite(last)||!(last>first))return false;
 let end=first;
 for(const [a,b]of [...ranges].sort((a,b)=>a[0]-b[0])){
  if(!Number.isFinite(a)||!Number.isFinite(b)||!(b>a))continue;
  const lo=Math.max(first,a),hi=Math.min(last,b);
  if(!(hi>lo))continue;
  if(hi<=end)continue;
  if(lo>end)return false;
  end=hi;if(end>=last)return true;
 }
 return end>=last;
}

function completeClassificationAtom(span,evidence,source){
 if(!source||span.kind==='Pending'||span.mappingComplete!==true||!sameId(span.faceId,source.faceId)
  ||span.sourcePathKey!==source.pathKey||!sameId(span.sourceEdgeId,span.targetEdgeId)
  ||!(span.evidenceIndices??[]).length
  ||!(source.sourceUses??[]).some(use=>contains(use,span.sourceEdgeId,span.firstParameter,span.lastParameter)))return false;
 const active=span.evidenceIndices.map(index=>evidence[index]);
 if(active.some(item=>!item||!sameId(item.faceId,span.faceId)||item.sourcePathKey!==span.sourcePathKey
  ||!sameId(item.sourceEdgeId,span.sourceEdgeId)
  ||!(source.sourceUses??[]).some(use=>contains(use,item.sourceEdgeId,item.sourceFirstParameter,item.sourceLastParameter))
  ||item.sourceFirstParameter>span.firstParameter||item.sourceLastParameter<span.lastParameter))return false;
 const through=active.some(item=>item.complete===true&&item.through===true),complete=active.every(item=>item.complete===true);
 return span.kind==='Through'?through:span.kind==='Partial'&&!through&&complete;
}

// Independently check that this proved side-arrival component reaches an open
// logical trace end. The native audit additionally checks actual TopoVertex
// or exact same-edge/native-event identity; previews are not topology proof.
function sideComponentTouchesEnd(source,side,arrivals,relations,arrivalIndex,targetDomain=false){
 if(!source||!side||source.closed||!side.side||(targetDomain&&side.closed)
  ||!sameId(source.faceId,side.faceId)||!sameId(source.wireId,side.wireId))return false;
 if(targetDomain&&arrivals[arrivalIndex]?.method!=='plane-line-first-physical-boundary')return false;
 const uses=(targetDomain?side:source).sourceUses??[],ranges=[];
 const compare=(a,b)=>a.index-b.index||(a.boundary!==b.boundary?(a.boundary?-1:1):a.boundary?0:(a.reversed?-1:1)*(a.parameter-b.parameter));
 const selected=unique(relations.filter(relation=>relation.sidePathKey===side.pathKey).map(relation=>relation.arrivalIndex));
 for(const index of selected){
  const arrival=arrivals[index];if(arrival?.sourcePathKey!==source.pathKey
   ||targetDomain&&arrival.method!=='plane-line-first-physical-boundary')continue;
  const [first,last]=targetDomain?[arrival.targetFirstParameter,arrival.targetLastParameter]:sourceRange(arrival);
  for(const [useIndex,use]of uses.entries()){
   if(!contains(use,targetDomain?arrival.targetEdgeId:arrival.sourceEdgeId,first,last)||!(last>first))continue;
   if(targetDomain&&!relations.some(relation=>relation.arrivalIndex===index&&relation.sidePathKey===side.pathKey&&relation.sideSpanKey===use.spanKey))continue;
   const position=parameter=>parameter===(use.reversed?use.lastParameter:use.firstParameter)?{index:useIndex,boundary:true}:
    parameter===(use.reversed?use.firstParameter:use.lastParameter)?{index:useIndex+1,boundary:true}:
    {index:useIndex,parameter,boundary:false,reversed:use.reversed};
   ranges.push({first:position(use.reversed?last:first),last:position(use.reversed?first:last),index});
  }
 }
 ranges.sort((a,b)=>compare(a.first,b.first)||compare(a.last,b.last));
 for(let begin=0;begin<ranges.length;){
  let finish=begin+1,last=ranges[begin].last;
  while(finish<ranges.length&&compare(ranges[finish].first,last)<=0){if(compare(last,ranges[finish].last)<0)last=ranges[finish].last;finish++;}
  if(ranges.slice(begin,finish).some(range=>range.index===arrivalIndex))return (ranges[begin].first.boundary&&ranges[begin].first.index===0)||(last.boundary&&last.index===uses.length);
  begin=finish;
 }
 return false;
}

check(summary.completed===summary.selected&&summary.rows.length===summary.selected,'every selected model completed');
check(manifest.schema===3&&manifest.coverageDomain==='source-skin-native-intervals','publication declares source-skin evidence schema 3');
for(const producer of Object.values(summary.producer))check(sha(producer.path)===producer.sha256,'producer unchanged: '+producer.path);
for(const manifestInput of [summary.inputPathManifest,summary.checkpointManifest].filter(Boolean))
 check(sha(manifestInput.path)===manifestInput.sha256,'upstream manifest unchanged: '+manifestInput.path);

for(const row of summary.rows){
 const label=`model ${row.id}`,entry=manifest.models.find(model=>sameId(model.id,row.id));totals.models++;
 check(entry?.status===row.status,label+': manifest status');
 check(row.sourceUnchanged&&sha(row.source)===row.sourceSha256,label+': original CAD unchanged');
 check(sha(row.input.path)===row.input.sha256,label+': upstream path JSON unchanged');
 const input=read(row.input.path),doc=read(row.artifacts.data.path);
 check(sha(row.artifacts.data.path)===row.artifacts.data.sha256,label+': output JSON hash');
 check(doc.stage==='faceCutCoverage'&&doc.schema===3&&doc.coverageDomain==='source-skin-native-intervals',label+': source-skin Work output schema 3');
 check(doc.run?.producer&&JSON.stringify(doc.run.producer)===JSON.stringify(summary.producer),label+': this native producer recorded');
 if(row.status==='not-run'||row.status==='failed'){
  check(!doc.success&&!doc.complete&&!!doc.error,label+': current failure or upstream absence explicit');
  check(['sources','arrivals','evidence','coverage'].every(key=>Array.isArray(doc[key])&&doc[key].length===0),label+': no borrowed prior coverage');
  continue;
 }
 check(doc.success===true&&doc.run.producedThisRun===true,label+': native output came from this invocation');
 check(doc.complete===(row.status==='passed'),label+': completeness matches state');
 check(doc.artifacts?.brepRoundTripPassed===true&&doc.artifacts?.stepRoundTrip?.passed===true,label+': whole BREP and styled STEP round trips');
 check(doc.artifacts?.completeModelBrep===true&&doc.faces?.length===input.faces?.length,label+': complete original face set retained');
 for(const kind of ['brep','step'])check(row.artifacts[kind]&&sha(row.artifacts[kind].path)===row.artifacts[kind].sha256,label+': '+kind+' belongs to this run');
 for(const checkpoint of Object.values(row.upstreamCheckpoint?.files??{}))
  check(sha(checkpoint.path)===checkpoint.sha256,label+': original Classify checkpoint unchanged: '+checkpoint.path);
 const allPaths=new Map((input.edges??[]).map(edge=>[edge.pathKey,edge]));
 const inputPaths=new Map([...allPaths].filter(([,edge])=>edge.path));
 const sidePaths=new Map([...allPaths].filter(([,edge])=>edge.side));
 const faces=new Map((input.faces??[]).map(face=>[String(face.faceId),face]));
 const physicalEdges=new Set((input.edges??[]).flatMap(edge=>(edge.sourceUses??[]).map(use=>String(edge.faceId)+':'+String(use.edgeId))));
 for(const boundary of input.unresolvedBoundaries??[])for(const edgeId of boundary.sourceEdgeIds??[])physicalEdges.add(String(boundary.faceId)+':'+String(edgeId));
 const sources=new Map(),arrivals=doc.arrivals??[],evidence=doc.evidence??[],coverage=doc.coverage??[],relations=doc.sideCorrespondences??[];
 for(const [key,value]of Object.entries({sourcePaths:doc.sources.length,arrivals:arrivals.length,evidence:evidence.length,coverageSpans:coverage.length,
  throughSpans:coverage.filter(span=>span.kind==='Through').length,partialSpans:coverage.filter(span=>span.kind==='Partial').length,pendingSpans:coverage.filter(span=>span.kind==='Pending').length}))
  check(doc.counts?.[key]===value,label+': native '+key+' count agrees with original records');
 check(doc.sources.length===inputPaths.size,label+': every selected skin source represented');
 for(const [key,edge]of inputPaths){
  check(edge.onSkinPath===true&&(edge.sourceUses??[]).length>0,label+'/'+key+': real path explicitly on skin');
  check((edge.sourceUses??[]).every(use=>use.skinValid&&use.skinRelation==='OnSkin'),label+'/'+key+': no non-skin convex or side path selected');
 }
 for(const source of doc.sources){
  const key=source.sourcePathKey,upstream=inputPaths.get(key),prefix=label+'/'+key;
  check(!sources.has(key),prefix+': source is unique');sources.set(key,source);totals.sources++;
  check(!!upstream&&sameId(source.faceId,upstream.faceId),prefix+': original faceId retained');
  check(faces.get(String(source.faceId))?.machining===true,prefix+': owner is machining face');
  check(sameSet((source.sourceEdgeIds??[]).map(String),(upstream?.sourceEdgeIds??[]).map(String)),prefix+': original edge lineage retained');
  check(source.classificationProofComplete===source.complete&&typeof source.exitCorrespondenceExpanded==='boolean',prefix+': classification proof and expanded correspondence are recorded separately');
  const endpointFacts=endpoints=>(endpoints??[]).map(endpoint=>Object.fromEntries(['endpoint','sourceEdgeId','parameter','position','positionValid','axis','axisValid','status'].map(field=>[field,endpoint[field]])));
  check(source.endpoints?.length===2&&upstream?.endpoints?.length===2&&JSON.stringify(endpointFacts(source.endpoints))===JSON.stringify(endpointFacts(upstream.endpoints)),prefix+': two original endpoint records, including null or parallel axes, are unchanged');
  const uses=upstream?.sourceUses??[];
  const finalClassification=uses.length>0&&uses.every(use=>intervalCovered(use.firstParameter,use.lastParameter,
   coverage.filter(span=>sameId(span.sourceEdgeId,use.edgeId)&&completeClassificationAtom(span,evidence,upstream)).map(span=>[span.firstParameter,span.lastParameter])));
  const finalCorrespondence=uses.length>0&&uses.every(use=>intervalCovered(use.firstParameter,use.lastParameter,
   arrivals.filter(arrival=>sameId(arrival.faceId,source.faceId)&&arrival.sourcePathKey===key&&sameId(arrival.sourceEdgeId,use.edgeId)
    &&uses.some(original=>contains(original,arrival.sourceEdgeId,...sourceRange(arrival)))).map(sourceRange)));
  check(source.complete===finalClassification,prefix+': final classification completeness exactly matches the complete evidence-derived coverage union in every original native source use');
  check(source.exitCorrespondenceExpanded===finalCorrespondence,prefix+': final correspondence expansion exactly matches the raw-arrival union in every original native source use');
  if(!source.complete){check(!!source.reason,prefix+': unresolved mapping has a concrete reason');pending.push({modelId:row.id,name:row.name,...source});}
  if(source.complete&&upstream)for(const use of upstream.sourceUses??[]){
   const ranges=evidence.filter(item=>item.complete===true&&item.sourcePathKey===key&&sameId(item.sourceEdgeId,use.edgeId)).map(sourceRange);
   check(intervalCovered(use.firstParameter,use.lastParameter,ranges),prefix+': complete classification proof covers original source span '+use.spanKey);
   check(!coverage.some(span=>span.sourcePathKey===key&&span.kind==='Pending'),prefix+': complete source has no local Pending atom');
  }
  if(source.exitCorrespondenceExpanded&&upstream)for(const use of upstream.sourceUses??[]){
   const ranges=arrivals.filter(arrival=>arrival.sourcePathKey===key&&sameId(arrival.sourceEdgeId,use.edgeId)).map(sourceRange);
   check(intervalCovered(use.firstParameter,use.lastParameter,ranges),prefix+': expanded correspondence has raw arrivals over the whole original source span '+use.spanKey);
  }
 }
 for(const [index,arrival]of arrivals.entries()){
  totals.arrivals++;const upstream=inputPaths.get(arrival.sourcePathKey),prefix=label+'/arrival '+index,[first,last]=sourceRange(arrival);
  check(!!upstream&&sameId(upstream.faceId,arrival.faceId),prefix+': real skin source and same-face mapping');
  check(physicalEdges.has(String(arrival.faceId)+':'+String(arrival.targetEdgeId)),prefix+': target is an original boundary of this face');
  check([first,last,arrival.targetFirstParameter,arrival.targetLastParameter].every(Number.isFinite),prefix+': finite native parameters');
  check(first<last&&arrival.targetFirstParameter<arrival.targetLastParameter,prefix+': positive native source and target intervals');
  check((upstream?.sourceUses??[]).some(use=>contains(use,arrival.sourceEdgeId,first,last)),prefix+': full source interval belongs to original skin path');
  const originalTargetUses=[...allPaths.values()].filter(edge=>sameId(edge.faceId,arrival.faceId)).flatMap(edge=>edge.sourceUses??[])
   .filter(use=>use.spanKey===arrival.targetSpanKey&&contains(use,arrival.targetEdgeId,arrival.targetFirstParameter,arrival.targetLastParameter));
  check(originalTargetUses.length>0,prefix+': target interval and span identity come from an original same-face source use');
  check(originalTargetUses.every(use=>use.skinRelation===arrival.targetSkinRelation&&use.skinValid===arrival.targetSkinValid
   &&use.convexity===arrival.targetConvexity&&use.convexityValid===arrival.targetConvexityValid),prefix+': target attributes match original per-span facts, including mixed convexity');
 }
 check(Array.isArray(doc.sideCorrespondences),label+': native side correspondence index is present');
 check(doc.counts?.sideCorrespondences===relations.length,label+': side relation count agrees');
 const sideRelations=new Set();
 for(const [index,relation]of relations.entries()){
  const prefix=label+'/side relation '+index,arrival=arrivals[relation.arrivalIndex],side=sidePaths.get(relation.sidePathKey);
  check(Number.isInteger(relation.arrivalIndex)&&!!arrival,prefix+': existing first-boundary arrival');
  check(!!side&&sameId(side.faceId,arrival?.faceId),prefix+': existing same-face logical side');
  check((side?.sourceUses??[]).some(use=>use.spanKey===relation.sideSpanKey&&contains(use,arrival?.targetEdgeId,arrival?.targetFirstParameter,arrival?.targetLastParameter)),prefix+': complete native target interval belongs to recorded side');
  const identity=relation.arrivalIndex+':'+relation.sidePathKey+':'+relation.sideSpanKey;
  check(!sideRelations.has(identity),prefix+': side reference is unique');sideRelations.add(identity);
 }
 check(evidence.filter(item=>item.hasArrival===true).length===arrivals.length,label+': each raw first-boundary arrival has one raw evidence record');
 const evidenceArrivals=new Set();
 for(const [index,item]of evidence.entries()){
  totals.evidence++;const prefix=label+'/evidence '+index,arrival=arrivals[item.arrivalIndex],upstream=inputPaths.get(item.sourcePathKey),witness=allPaths.get(item.witnessLogicEdgeId);
  check(item.index===index,prefix+': stable evidence index');
  check(item.classificationProofComplete===item.complete&&typeof item.hasArrival==='boolean',prefix+': classification proof and raw-arrival presence are explicit');
  check(!!upstream&&sameId(item.faceId,upstream.faceId)&&(upstream.sourceUses??[]).some(use=>contains(use,item.sourceEdgeId,item.sourceFirstParameter,item.sourceLastParameter)),prefix+': evidence belongs to the original same-face native skin source');
  check(Number.isFinite(item.sourceFirstParameter)&&Number.isFinite(item.sourceLastParameter)&&item.sourceFirstParameter<item.sourceLastParameter,prefix+': finite positive native source evidence interval');
  check(!!item.reason,prefix+': evidence has a concrete reason');
  if(item.kind==='SkinExitBoundarySet'){
   totals.boundarySetEvidence++;
   check(item.hasArrival===false&&item.complete===true&&item.through===true&&item.exitCorrespondenceExpanded===false,prefix+': boundary-set proof is complete through classification with no expanded exit correspondence');
   check(sources.get(item.sourcePathKey)?.complete===true,prefix+': owning source declares completed classification independently of its final raw correspondence state');
   check(['arrivalIndex','witnessEdgeId','witnessLogicEdgeId','witnessSpanKey','witnessFirstParameter','witnessLastParameter','targetEdgeId'].every(key=>!Object.hasOwn(item,key)),prefix+': boundary-set proof has no invented arrival or single target');
   check((upstream?.sourceUses??[]).some(use=>sameId(use.edgeId,item.sourceEdgeId)&&use.firstParameter===item.sourceFirstParameter&&use.lastParameter===item.sourceLastParameter),prefix+': boundary-set evidence retains a whole original source-use interval');
   const planarFamily=upstream?.axisMode==='PlaneTangentCrossNormal'&&item.reason?.includes('bounded-valid-native-plane-domain');
   const ruledFamily=upstream?.axisMode==='StraightGenerator'&&item.reason?.includes('bounded-valid-native-ruled-domain')
    &&/native-degree-one-two-pole-[uv]-generator/.test(item.reason??'')
    &&item.reason?.includes('positive-weights-nondegenerate-ruling-and-full-trim-jacobian-certified')
    &&item.reason?.includes('finite-isolated-source-generator-parallel-parameters-retained')
    &&item.reason?.includes('source-regular-between-finitely-many-native-generator-critical-parameters')
    &&item.reason?.includes('original-coedge-inward-generator-family');
   check(planarFamily||ruledFamily,prefix+': boundary-set source has the matching strict native plane or ruled-family certificate');
   if(ruledFamily)check(sources.get(item.sourcePathKey)?.method==='bounded-ruled-all-skin-boundary-exit',prefix+': native ruled proof is not mislabeled as a planar exit method');
   check(['bounded','valid','inward','regular'].every(word=>item.reason?.includes(word))&&item.reason?.includes('exit-correspondence-not-expanded'),prefix+': bounded valid domain, inward regular-interior family and unexpanded correspondence are declared');
   const faceEdges=[...allPaths.values()].filter(edge=>sameId(edge.faceId,item.faceId));
   const unresolved=(input.unresolvedBoundaries??[]).filter(boundary=>sameId(boundary.faceId,item.faceId));
   check(faceEdges.length>0&&unresolved.length===0&&!(faces.get(String(item.faceId))?.unresolvedBoundaryKeys??[]).length,prefix+': every boundary is represented without an unresolved face wire');
   check(faceEdges.every(edge=>edge.path===true&&edge.onSkinPath===true&&(edge.sourceUses??[]).length>0),prefix+': no non-skin or unknown logical boundary is hidden by the boundary-set proof');
   const expected=new Map();
   const nativeDomains=new Map();
   const spanIdentity=value=>JSON.stringify([String(value.edgeId),value.spanKey,value.firstParameter,value.lastParameter]);
   for(const edge of faceEdges)for(const use of edge.sourceUses??[]){
    check(use.skinValid===true&&use.skinRelation==='OnSkin'&&use.sourceSkinMembership===true&&(use.sourceSkinFaceIds??[]).length>0,prefix+': every original boundary span has complete source-skin attribution');
    const identity=spanIdentity(use),prior=expected.get(identity);
    if(prior)check(prior.skinReason===use.skinReason&&sameSet((prior.sourceSkinFaceIds??[]).map(String),(use.sourceSkinFaceIds??[]).map(String)),prefix+': repeated original span facts agree');
    else expected.set(identity,use);
    const edgeId=String(use.edgeId);if(!nativeDomains.has(edgeId))nativeDomains.set(edgeId,[]);
    nativeDomains.get(edgeId).push([use.firstParameter,use.lastParameter]);
   }
   for(const [edgeId,ranges]of nativeDomains){
    const first=Math.min(...ranges.map(range=>range[0])),last=Math.max(...ranges.map(range=>range[1]));
    check(ranges.every(([lo,hi])=>Number.isFinite(lo)&&Number.isFinite(hi)&&lo<hi)&&intervalCovered(first,last,ranges),prefix+': source boundary E'+edgeId+' has no omitted positive native interval between original spans');
   }
   const candidates=item.boundaryWitnesses??[],seen=new Set();
   check(Array.isArray(item.boundaryWitnesses)&&candidates.length>0,prefix+': actual physical skin boundary witness set is retained');
   for(const candidate of candidates){
    const identity=spanIdentity(candidate),original=expected.get(identity);
    check(!seen.has(identity),prefix+': candidate native boundary span is unique');seen.add(identity);
    check(!!original&&physicalEdges.has(String(item.faceId)+':'+String(candidate.edgeId)),prefix+': candidate is an unchanged original same-face edge/span/native interval');
    check(Number.isFinite(candidate.firstParameter)&&Number.isFinite(candidate.lastParameter)&&candidate.firstParameter<candidate.lastParameter,prefix+': candidate retains a positive native boundary interval');
    check(original&&candidate.skinReason===original.skinReason&&sameSet((candidate.sourceSkinFaceIds??[]).map(String),(original.sourceSkinFaceIds??[]).map(String)),prefix+': candidate retains exact source-skin provenance and reason');
   }
   check(sameSet([...seen],[...expected.keys()]),prefix+': candidate set covers every original boundary span with no omitted or added interval');
   continue;
  }
  totals.rawEvidence++;
  check(item.hasArrival===true&&item.exitCorrespondenceExpanded===true,prefix+': ordinary boundary evidence keeps its real raw arrival');
  check(!item.boundaryWitnesses?.length,prefix+': ordinary arrival evidence does not invent a boundary-set proof');
  check(Number.isInteger(item.arrivalIndex)&&!!arrival&&!evidenceArrivals.has(item.arrivalIndex),prefix+': unique raw first-boundary arrival');evidenceArrivals.add(item.arrivalIndex);
  if(!arrival)continue;
  const [first,last]=sourceRange(arrival);
  check(item.sourcePathKey===arrival.sourcePathKey&&sameId(item.faceId,arrival.faceId)&&sameId(item.sourceEdgeId,arrival.sourceEdgeId),prefix+': same source and face as raw mapping');
  check(item.sourceFirstParameter===first&&item.sourceLastParameter===last,prefix+': original increasing native source range');
  check(sameId(item.witnessEdgeId,arrival.targetEdgeId)&&item.witnessSpanKey===arrival.targetSpanKey&&item.witnessFirstParameter===arrival.targetFirstParameter&&item.witnessLastParameter===arrival.targetLastParameter,prefix+': unchanged physical witness range');
  if(item.witnessLogicEdgeId)check(witness&&sameId(witness.faceId,item.faceId)&&(witness.sourceUses??[]).some(use=>contains(use,item.witnessEdgeId,item.witnessFirstParameter,item.witnessLastParameter)),prefix+': existing same-face logical witness');
  if(arrival.targetSkinValid&&arrival.targetSkinRelation==='OnSkin'){
   check(item.kind==='SkinExit'&&item.complete&&item.through,prefix+': skin exit proves through, including far span of same edge');
  }else if(arrival.targetConvexityValid&&arrival.targetConvexity==='Convex'){
   check(item.kind==='ConvexBoundary'&&item.complete&&item.through,prefix+': convex witness proves through even when also a side');
  }else if(item.kind==='SideCompletion'){
   totals.sideCompletions++;
   const side=sidePaths.get(item.witnessLogicEdgeId);
   check(item.complete&&item.through&&sideRelations.has(item.arrivalIndex+':'+item.witnessLogicEdgeId+':'+item.witnessSpanKey),prefix+': side completion uses an existing proved side arrival');
   const targetDomain=item.reason.includes('target-native-component');
   check(sideComponentTouchesEnd(upstream,side,arrivals,relations,item.arrivalIndex,targetDomain),prefix+': side-arrival component reaches an exact native '+(targetDomain?'target-side':'source-chain')+' end');
   check(item.reason.includes('shared-topological-vertex')||item.reason.includes('shared-native-edge-parameter-event'),prefix+': side-incidence proof basis is recorded');
  }else if(arrival.targetSkinValid&&arrival.targetConvexityValid&&arrival.targetConvexity==='Concave'){
   check(item.kind==='ConcaveTermination'&&item.complete&&!item.through,prefix+': concave termination proves retained material');
  }else if(arrival.targetSkinValid&&arrival.targetConvexityValid){
   check(item.kind==='OtherBoundary'&&!item.complete&&!item.through,prefix+': free or coplanar boundary alone cannot prove a partial cut');
  }else{
   check(item.kind==='OtherBoundary'&&!item.complete&&!item.through,prefix+': missing target attributes remain local Pending evidence');
  }
 }
 for(const [index,span]of coverage.entries()){
  totals.coverageSpans++;const prefix=label+'/coverage '+index,upstream=inputPaths.get(span.sourcePathKey),indices=span.evidenceIndices??[];
  check(faces.get(String(span.faceId))?.machining===true&&upstream&&sameId(upstream.faceId,span.faceId),prefix+': original skin path and machining face');
  check(sameId(span.sourceEdgeId,span.targetEdgeId),prefix+': coverage lies on source skin edge');
  check(Number.isFinite(span.firstParameter)&&Number.isFinite(span.lastParameter)&&span.firstParameter<span.lastParameter,prefix+': positive native source interval');
  check((upstream?.sourceUses??[]).some(use=>contains(use,span.sourceEdgeId,span.firstParameter,span.lastParameter)),prefix+': entire output atom is in an original skin source use');
  check(sameSet(span.sourcePathKeys??[],[span.sourcePathKey])&&sameSet(span.baselineSourcePathKeys??[],[span.sourcePathKey]),prefix+': singleton owning source, no contributor counting');
  check(sameSet(indices,unique(indices)),prefix+': unique evidence indices');
  for(const evidenceIndex of indices){
   const item=evidence[evidenceIndex];
   check(Number.isInteger(evidenceIndex)&&item&&sameId(item.faceId,span.faceId)&&item.sourcePathKey===span.sourcePathKey&&sameId(item.sourceEdgeId,span.sourceEdgeId)&&item.sourceFirstParameter<=span.firstParameter&&item.sourceLastParameter>=span.lastParameter,prefix+': evidence covers this complete source-domain atom');
  }
  const active=indices.map(i=>evidence[i]).filter(Boolean),through=active.some(item=>item.complete&&item.through),complete=active.length>0&&active.every(item=>item.complete);
  const expected=through?'Through':complete?'Partial':'Pending';
  check(span.kind===expected&&span.mappingComplete===(through||complete),prefix+': local evidence alone determines coverage');
  check(span.classificationProofComplete===span.mappingComplete&&span.exitCorrespondenceExpanded===active.some(item=>item.hasArrival===true),prefix+': classification completeness and raw correspondence expansion remain separate');
  check(sameSet(span.arrivalIndices??[],unique(active.filter(item=>item.hasArrival===true).map(item=>item.arrivalIndex))),prefix+': raw-arrival references exclude boundary-set proof placeholders');
  check(span.faceMappingComplete===!(doc.incompleteFaceIds??[]).some(id=>sameId(id,span.faceId)),prefix+': face-level inspection flag agrees independently of local kind');
  check(!!span.reason,prefix+': local coverage has a concrete reason');
  if(span.kind==='Through')totals.through++;else if(span.kind==='Partial')totals.partial++;else{totals.pending++;check(sources.get(span.sourcePathKey)?.complete===false,prefix+': Pending belongs to an incomplete source');}
 }

 // Walk every original-source/evidence/output event, independently from native
 // interval construction. This detects omitted pieces, duplicate atoms, and
 // accidental target-domain or cross-source aggregation.
 const groups=new Map();
 const group=(key,edgeId)=>{
  const id=key+'@'+String(edgeId);
  if(!groups.has(id))groups.set(id,{baselines:[],evidence:[],outputs:[]});
  return groups.get(id);
 };
 for(const source of inputPaths.values())for(const use of source.sourceUses??[])group(source.pathKey,use.edgeId).baselines.push([use.firstParameter,use.lastParameter]);
 for(const [index,item]of evidence.entries())group(item.sourcePathKey,item.sourceEdgeId).evidence.push({...item,index});
 for(const span of coverage)group(span.sourcePathKey,span.sourceEdgeId).outputs.push(span);
 for(const [key,values]of groups){
  const events=unique([...values.baselines.flat(),...values.evidence.flatMap(item=>[item.sourceFirstParameter,item.sourceLastParameter]),...values.outputs.flatMap(span=>[span.firstParameter,span.lastParameter])]).sort((a,b)=>a-b);
  for(let i=1;i<events.length;i++){
   const first=events[i-1],last=events[i];if(!(last>first))continue;
   const baseline=values.baselines.some(([lo,hi])=>lo<=first&&hi>=last);
   const expectedEvidence=values.evidence.filter(item=>item.sourceFirstParameter<=first&&item.sourceLastParameter>=last).map(item=>item.index);
   const actual=values.outputs.filter(span=>span.firstParameter<=first&&span.lastParameter>=last);
   check(actual.length===(baseline?1:0),label+'/'+key+': every source-domain native atom appears exactly once');
   if(baseline&&actual.length===1)check(sameSet(actual[0].evidenceIndices??[],expectedEvidence),label+'/'+key+': exact local evidence set, including empty Pending intervals');
  }
 }
 check(doc.complete===((doc.incompleteFaceIds??[]).length===0),label+': overall completeness agrees with incomplete face list');
}
const report={schema:3,coverageDomain:'source-skin-native-intervals',status:errorCount?'failed':'passed',checks,totals,errorCount,errors,pendingSources:pending,
 validationScope:'Native source/evidence identity, exact interval unions, original per-span attributes, complete source-attributed skin boundary sets, separate classification/correspondence state, complete-model artifact round trips and hashes; native audits independently verify bounded valid topology, regular-interior inward normal families and side incidence. Preview mesh points are not proof.'};
fs.writeFileSync(path.join(run,'validation.json'),JSON.stringify(report,null,2));
console.log(JSON.stringify({status:report.status,checks,totals,errorCount,errors:errors.slice(0,12),pendingSources:pending.length},null,2));
if(errorCount)process.exitCode=1;
