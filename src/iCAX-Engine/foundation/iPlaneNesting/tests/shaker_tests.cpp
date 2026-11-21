#include "recovered_shaker.hpp"
#include "recovered_shaker_geometry.hpp"
#include "recovered_shaker_pair.hpp"
#include "recovered_convolution.hpp"
#include "recovered_convolution_pose.hpp"
#include "recovered_convolution_cache.hpp"
#include "recovered_shaker_relations.hpp"
#include "recovered_shaker_broadphase.hpp"
#include "recovered_shaker_step.hpp"
#include "recovered_shaker_finalize.hpp"
#include "recovered_shaker_iteration.hpp"
#include "recovered_convolution_minkowski.hpp"
#include "recovered_convolution_floating.hpp"
#include "shaker_runtime.hpp"
#include "recovered_rich_conversion.hpp"
#include <numbers>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace study::recovered;
namespace {
size_t checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b,const char *message){check(std::abs(a-b)<1e-8,message);}
ShakerPart part(double x,double y){ShakerPart p;p.position={x,y};p.bounds={0,100,0,100};p.angleBounds={-1,1};p.objective={0.25,1,0};return p;}
}
#include "shaker_pipeline_cases.inl"
#include "shaker_input_pipeline_cases.inl"
#include "minkowski_geometry_cases.inl"
int main(){try{
    // Two pieces translate simultaneously. Fixed separation is an explicit
    // contact plane; this test does not substitute for convolution/cell build.
    std::array<ShakerPart,2> parts{part(8,5),part(14,5)};
    ShakerProblem problem;problem.window=20;LinearModel model;
    const auto initial=shakerVariables(problem,parts,model);near(initial,15.5,"initial objective");
    shakerTranslationRow(problem,model,{1,0,-6},{},{},parts[0],parts[1],{6,0});
    auto solution=solveShakerHighs(model);check(solution.status==0,"LP solve");
    near(solution.objective,1.5,"coupled optimum");int refreshes=0;double objective=-1;
    check(shakerApplySolution(problem,parts,solution,objective,[&](auto&){++refreshes;})==0,"writeback");
    check(refreshes==2,"refresh per part");near(parts[1].position[0]-parts[0].position[0],6,"separation");
    near(parts[0].position[0],0,"first at origin");near(parts[1].position[0],6,"second follows first");
    near(parts[0].position[1],0,"y optimization");check(parts[0].rows.size()==1&&parts[1].rows.size()==1,"dual ownership");
    check(parts[0].dualMagnitude190>0&&parts[0].movable188==0,"active constraint dual");
    // Exact hand-derived rotation row with fixed xi and free xj. Mirrored
    // writeback reverses only the internal angular increment.
    parts={part(1,0),part(7,2)};parts[0].locked={1,1,0};parts[1].locked={0,1,0};
    parts[0].angle=parts[1].angle=0.2;parts[1].mirror={1,0};
    parts[0].angleBounds=parts[1].angleBounds={0.195,0.4};
    parts[0].objective={0,0,0};parts[1].objective={1,0,0};
    problem={};problem.window=4;problem.angularWindow=0.01;problem.rotation=true;model={};
    shakerVariables(problem,parts,model);
    shakerTranslationRow(problem,model,{1,0,-5},{{2,3},true},{},parts[0],parts[1],{6,2});
    near(model.rows[0].lower,6,"fixed axis rhs");check(model.rows[0].terms.size()==3,"rotation term count");
    near(model.rows[0].terms[0].coefficient,1,"j x");near(model.rows[0].terms[1].coefficient,3,"i angular");near(model.rows[0].terms[2].coefficient,-1,"j angular");
    solution=solveShakerHighs(model);check(solution.status==0,"rotation solve");
    near(solution.primal[parts[0].columns[2]],0.01,"rotation lower support optimum");
    near(solution.primal[parts[1].columns[2]],-0.01,"reflected increment optimum");
    shakerApplySolution(problem,parts,solution,objective,{});near(parts[0].angle,0.21,"positive angle writeback");near(parts[1].angle,0.21,"mirrored angle writeback");
    // Solver failure must leave caller placements/objective unchanged.
    auto saved=parts;const auto savedObjective=objective;model.addLowerRow({{parts[1].columns[0],1}},1000);
    solution=solveShakerHighs(model);check(solution.status!=0,"infeasible detected");
    check(shakerApplySolution(problem,parts,solution,objective,{})==2,"failure mapped");
    check(parts[0].position==saved[0].position&&parts[1].position==saved[1].position&&objective==savedObjective,"failure unchanged");
    solution.status=3;check(shakerApplySolution(problem,parts,solution,objective,{})==1,"stop mapped");
    // All axes fixed: no otherwise-empty slack-only constraint.
    parts[0].locked=parts[1].locked={1,1,1};problem.slack49=1;problem.slackCC=0;model={};
    check(!shakerTranslationRow(problem,model,{1,0,-5},{},{},parts[0],parts[1],{6,2}),"fixed row omitted");check(model.rows.empty(),"no slack-only row");
    auto cell=shakerRectangleCell({-1,-1,1,1});ShakerCellNode cut;cut.plane={1,0,-0.5};
    check(shakerClipCell(cell,cut)&&cell.size()==4,"cell clipping");check(!cell[0].artificial,"new plane becomes head");
    for(const auto &n:cell)check(n.vertex[0]>=0.5,"clipped side");
    cut.plane={1,0,-3};check(!shakerClipCell(cell,cut)&&cell.size()==4,"fully negative retains original cell");
    cut.plane={0,1,0};check(shakerClipCell(cell,cut),"successive clip");
    for(const auto &n:cell)check(n.vertex[0]>=0.5&&n.vertex[1]>=0,"successive retained side");
    problem={};problem.policy0=1;model={};shakerAdditionalVariables(problem,{},model);check(model.columns[0].upper==0&&model.columns[0].lower<0,"policy0 upper-bounded auxiliary");
    problem={};problem.slack49=1;model={};shakerAdditionalVariables(problem,{},model);check(model.columns[0].lower==0&&model.columns[0].upper>0,"policy49 lower-bounded auxiliary");
    const auto contact=shakerPointNear({0,0},{10,0},{4,0.01},0.02);check(contact.hit&&!contact.start&&!contact.end,"interior tolerance contact");
    check(!shakerPointNear({0,0},{10,0},{4,0.02},0.02).hit,"strict contact tolerance");
    // A real straight polygon path: tangent intervals -> original event order
    // -> source pairs/adjacency -> cell rows -> simultaneous LP -> winding.
    // The shape pair is explicitly supplied here; broadphase and scene policy
    // are not claimed as part of this connected arithmetic test.
    const std::array<ShakerPoint,4> squareA{{{0,0},{2,0},{2,2},{0,2}}},negativeB{{{0,0},{-2,0},{-2,-2},{0,-2}}};
    const auto entitiesA=convolutionPolygon(squareA),entitiesB=convolutionPolygon(negativeB);
    auto sourcePairs=convolutionSweep(entitiesA,entitiesB);convolutionLinkPairs(entitiesA,entitiesB,sourcePairs);auto boundary=convolutionRecords(entitiesA,entitiesB,sourcePairs,true);
    check(!boundary.empty(),"sourced square configuration boundary");
    check(shakerInside(boundary,{0,0},1e-6,false),"overlapping squares classified inside");
    check(!shakerInside(boundary,{3,0},1e-6,false),"separated squares classified outside");
    parts={part(5,3),part(8,3)};problem={};problem.window=20;model={};shakerVariables(problem,parts,model);
    ShakerPairOptions pairOptions;pairOptions.radius=20;pairOptions.geometryMode=0;pairOptions.sceneSpan=10;
    ShakerRelations relationScene;relationScene.types.index(100);relationScene.types.index(200);relationScene.types.set(0,1,true);
    std::array<ShakerRelationPart,2> relationParts;
    for(size_t n=0;n<2;++n){relationParts[n].id=int32_t(n);relationParts[n].identity=reinterpret_cast<uintptr_t>(&parts[n]);relationParts[n].values[int32_t(n)]={0,0,1};}
    relationScene.x.parents=relationScene.y.parents={0,1};
    for(size_t n=0;n<2;++n){relationScene.x.indices[relationParts[n].identity]=int32_t(n);relationScene.y.indices[relationParts[n].identity]=int32_t(n);}
    ShakerPairSource pairSource{[&](bool rotation){return relationScene.excluded(rotation,relationParts[0],relationParts[1],100,200);},[&](double expansion,int32_t mode){check(expansion==0&&mode==0,"ordinary sourced pair key");return boundary;}};
    double violation=0;const auto pairResult=shakerPairRows(problem,model,parts[0],parts[1],pairOptions,pairSource,violation);
    check(pairResult.status==0&&!model.rows.empty(),"sourced cell generated rows");solution=solveShakerHighs(model);check(solution.status==0,"sourced cell LP solve");
    check(shakerApplySolution(problem,parts,solution,objective,{})==0,"sourced candidate writeback");
    const ShakerPoint relative{parts[1].position[0]-parts[0].position[0],parts[1].position[1]-parts[0].position[1]};
    check(!shakerInside(boundary,relative,1e-6,false),"nonlinear sourced winding accepts candidate");
    check(std::abs(relative[0])>=2-1e-8||std::abs(relative[1])>=2-1e-8,"independent rectangle interiors do not overlap");
    check(objective<9.25,"connected optimization improves objective");
    // A common X/Y movement group must suppress this pair before requesting
    // its convolution. Free rotation overrides that suppression.
    relationScene.x.parents=relationScene.y.parents={0,0};model={};
    size_t convolutionCalls=0;pairSource.convolution=[&](double,int32_t){++convolutionCalls;return boundary;};
    shakerPairRows(problem,model,parts[0],parts[1],pairOptions,pairSource,violation);
    check(convolutionCalls==0&&model.rows.empty(),"same movement group suppresses pair");
    check(!relationScene.excluded(true,relationParts[0],relationParts[1],100,200),"free angular movement needs pair checking");
    testShakerPipeline();
    testShakerInputPipeline();
    testMinkowskiGeometry();
    {
        LinearModel narrow;
        const auto x=narrow.addColumn(-1,1,1);
        narrow.addRow({{x,1}},0,std::numeric_limits<double>::infinity());
        narrow.addRow({{x,1}},-std::numeric_limits<double>::infinity(),-5e-7);
        check(solveShakerHighs(narrow,2.,1e-5).status!=0,
              "Sub-tolerance contradictory hard rows must not publish a loosely feasible primal solution");
    }
    std::cout << "{\"pass\":true,\"checks\":" << checks
              << ",\"pipelineScenes\":16,\"pipelineRounds\":48,\"pipelineSolves\":" << shakerPipelineSolves
              << ",\"inputPipelineScenes\":" << inputPipelineScenes
              << ",\"inputPipelineSolves\":" << inputPipelineSolves
              << ",\"inputEnvelopeBefore\":" << inputPipelineEnvelopeBefore
              << ",\"inputEnvelopeAfter\":" << inputPipelineEnvelopeAfter
              << ",\"sdkPipelineScenes\":" << sdkPipelineScenes
              << ",\"sdkPipelineRotations\":" << sdkPipelineRotations
              << ",\"sdkPipelineCacheClears\":" << sdkPipelineCacheClears
              << ",\"sdkPipelineModes\":[" << sdkPipelineModes[0] << ',' << sdkPipelineModes[1]
              << ',' << sdkPipelineModes[2] << ',' << sdkPipelineModes[3] << ']'
              << ",\"backend\":\"HiGHS 1.7.2\"}\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
