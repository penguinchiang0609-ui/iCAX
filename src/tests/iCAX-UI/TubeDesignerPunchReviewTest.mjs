import assert from "node:assert/strict";
import { buildPunchReviewSummary, renderPunchReview } from "../../apps/tube-designer/webpage/punchReview.mjs";

const part={length:1000};
const original={type:"circle",diameter:10,station:100,arrayCount:5,arrayPitch:150,layoutDatum:"base"};
const state={baseLength:1000,catalogueStatus:"ready",tools:[],features:[original],ends:{start:{type:"keep"},end:{type:"keep"}}};
assert.equal(buildPunchReviewSummary(state,part).positions,5);
assert.equal(buildPunchReviewSummary(state,part).estimatedOpenings,5);
assert.equal(buildPunchReviewSummary(state,part).countsComplete,true);
const legacyState={...state,features:[{...original,layoutDatum:undefined}]};
assert.equal(buildPunchReviewSummary(legacyState,part).finishedDatumRecords,1);
assert.match(renderPunchReview(legacyState,part,n=>"fixture-"+n),/成品端面基准/);

// A frozen missing version is replayed, not re-solved after a tube-length change.
const frozenRef={id:"unavailable-tool",version:"1",digest:"old-exact-version"};
const frozen={type:"circle",toolLabel:"固化孔",toolRef:frozenRef,distributionMode:"fill",headMargin:100,tailMargin:100,arrayPitch:100,
  frozenTool:{schema:"icax.frozen-punch-tool",schemaVersion:1,ref:structuredClone(frozenRef),geometry:{mode:"solid"},instance:{arrayCount:9}},
  frozenCut:{url:"saved-cut",version:1}};
const frozenState={...state,features:[original,frozen],baseLength:2000,tools:[{id:frozenRef.id,version:"2",digest:"wrong-version"}]};
const frozenSummary=buildPunchReviewSummary(frozenState,part);
assert.equal(frozenSummary.positions,5,"frozen count must not be recomputed as 19 from a changed length");
assert.equal(frozenSummary.countsComplete,false);
assert.equal(frozenSummary.estimatedOpenings,null);
assert.equal(frozenSummary.unverifiedRecords.length,1);
assert.equal(frozenSummary.unverifiedRecords[0].frozen,true);
assert.match(frozenSummary.unverifiedRecords[0].message,/不按当前管长重算/);
const frozenHtml=renderPunchReview(frozenState,part,n=>"fixture-"+n);
assert.match(frozenHtml,/上述数量不是零件总数/);
assert.match(frozenHtml,/位置数待复核/);
assert.match(frozenHtml,/data-tube-designer-punch-index="1"/);
const matchedState={...frozenState,tools:[{...frozenRef,displayName:"已找到版本"}]};
assert.equal(buildPunchReviewSummary(matchedState,part).unverifiedRecords.length,0);
assert.equal(buildPunchReviewSummary(matchedState,part).positions,24);
const wrongDigestState={...matchedState,tools:[{...frozenRef,digest:"different-digest"}]};
assert.equal(buildPunchReviewSummary(wrongDigestState,part).unverifiedRecords.length,1);
assert.equal(buildPunchReviewSummary({...frozenState,catalogueStatus:"loading"},part).countsComplete,false);
assert.match(buildPunchReviewSummary({...frozenState,catalogueStatus:"loading"},part).unverifiedRecords[0].message,/目录尚未就绪/);
assert.equal(buildPunchReviewSummary({...frozenState,features:[{...frozen,enabled:false}]},part).disabled,1);
const missing={...frozen,frozenTool:undefined,frozenCut:undefined};
assert.equal(buildPunchReviewSummary({...frozenState,features:[missing]},part).unverifiedRecords[0].frozen,false);

// Tool positions are not necessarily complete holes: both a centre beyond an
// end and an in-stock centre whose profile meets the end need geometric review.
const reviewFeature=feature=>buildPunchReviewSummary({...state,features:[{...original,arrayCount:1,...feature}]},part);
assert.equal(reviewFeature({station:1002}).positions,1);
assert.equal(reviewFeature({station:1002}).estimatedOpenings,null);
assert.equal(reviewFeature({station:998}).estimatedOpenings,null);
assert.equal(reviewFeature({station:500,allowOpen:true}).estimatedOpenings,1,"an old switch must not make every interior circle uncountable");
assert.equal(reviewFeature({station:500,type:"custom"}).estimatedOpenings,null);
assert.equal(reviewFeature({station:20,type:"rectangle",spanAlong:10,spanAcross:60,rotation:90}).estimatedOpenings,null);
assert.equal(reviewFeature({station:20,type:"rectangle",spanAlong:10,spanAcross:60,rotation:0}).estimatedOpenings,1);
assert.equal(reviewFeature({station:20,type:"ellipse",spanAlong:10,spanAcross:60,rotation:90}).estimatedOpenings,null);
assert.equal(reviewFeature({station:20,type:"slot",spanAlong:60,spanAcross:10,rotation:0}).estimatedOpenings,null);
assert.equal(reviewFeature({station:500,toolTarget:"part"}).estimatedOpenings,null);
assert.equal(reviewFeature({station:500,through:true}).estimatedOpenings,2);
const invalid=reviewFeature({arrayCount:2,arrayPitch:0});
assert.equal(invalid.countsComplete,false);
assert.equal(invalid.estimatedOpenings,null);

const previewState={...state,revision:3,preview:{revision:3,length:998,includesDraft:false,toolCountExact:true,
  appliedPunchToolCount:4,outsideToolCount:1,endToolCount:2}};
let actual=buildPunchReviewSummary(previewState,part);
assert.equal(actual.positions,5);assert.equal(actual.appliedPunchTools,4);assert.equal(actual.outsideTools,1);assert.equal(actual.endTools,2);
assert.match(renderPunchReview(previewState,part,n=>"fixture-"+n),/实际相交孔刀/);
assert.match(renderPunchReview(previewState,part,n=>"fixture-"+n),/未接触主管，已略过/);
assert.equal(buildPunchReviewSummary({...previewState,revision:4},part).appliedPunchTools,null,"Stale preview numbers cannot describe newly edited geometry");
assert.equal(buildPunchReviewSummary({...previewState,preview:{...previewState.preview,toolCountExact:false}},part).appliedPunchTools,null);
assert.match(renderPunchReview({...previewState,preview:{...previewState.preview,includesDraft:true}},part,n=>"fixture-"+n),/本次三维预览（含新增行）/);

// Explicit groups count actual Cartesian instances, not neutralized old counts.
const arrays={...original,arrayCount:1,rowCount:1,depthMode:"both",opposite:true,
  arrayGroups:[{id:"x",type:"linear",axis:"X",count:3,spacing:50},
    {id:"y",type:"linear",axis:"Y",count:4,spacing:20}],arraySkips:[{x:1,y:2}]};
const groupState={...state,features:[arrays]};
let grouped=buildPunchReviewSummary(groupState,part);
assert.equal(grouped.positions,11);assert.equal(grouped.cutters,22);assert.equal(grouped.skipped,1);
assert.equal(grouped.estimatedOpenings,null,"Multiple world-axis placements cannot imply a physical opening count.");
assert.equal(grouped.groups[0].positions,11);assert.equal(grouped.groups[0].cutters,22);
assert.equal(buildPunchReviewSummary({...groupState,baseLength:1200},part).cutters,22);
assert.equal(buildPunchReviewSummary({...groupState,features:[{...arrays,enabled:false}]},part).disabled,1);
assert.equal(buildPunchReviewSummary({...groupState,features:[{...arrays,arrayGroups:[{id:"x",type:"linear",axis:"X",count:0}]}]},part).countsComplete,false);
const groupedFrozen={...frozen,arrayGroups:arrays.arrayGroups,arrayCandidateCount:12,arrayTransforms:[]};
const frozenGroups=buildPunchReviewSummary({...frozenState,features:[arrays,groupedFrozen]},part);
assert.equal(frozenGroups.cutters,22);assert.equal(frozenGroups.unverifiedRecords.length,1);
assert.equal(frozenGroups.countsComplete,false,"A missing frozen recipe must not be silently counted from new group rules.");
const onlyTools={...previewState,features:[arrays],preview:{...previewState.preview,toolsOnly:true}};
const onlyToolsSummary=buildPunchReviewSummary(onlyTools,part);
assert.equal(onlyToolsSummary.cutters,22);assert.equal(onlyToolsSummary.finishedLength,null);
assert.equal(onlyToolsSummary.appliedPunchTools,null);assert.equal(onlyToolsSummary.outsideTools,null);assert.equal(onlyToolsSummary.endTools,null);
const onlyToolsHtml=renderPunchReview(onlyTools,part,n=>"fixture-"+n);
assert.doesNotMatch(onlyToolsHtml,/预览包络长度|实际相交孔刀|未接触主管，已略过/);
assert.match(onlyToolsHtml,/成品在最终确认时计算/);

console.log("TubeDesignerPunchReviewTest passed: datum warnings, exact frozen versions, deferred counts and clipped-hole estimates.");
