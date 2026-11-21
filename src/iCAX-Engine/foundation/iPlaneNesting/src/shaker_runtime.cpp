#include "shaker_runtime.hpp"
#include "recovered_shaker_pair.hpp"
#include "recovered_convolution_cache.hpp"
#include <chrono>
#include <stdexcept>

namespace study::recovered {
ShakerRunResult runShakerScene(ShakerInputScene &scene,const ShakerIterationOptions &options,int32_t geometryMode){
    return runShakerScene(scene,options,geometryMode,{});
}
ShakerRunResult runShakerScene(ShakerInputScene &scene,const ShakerIterationOptions &options,int32_t geometryMode,
                             std::span<const ShakerPairConstraint> constraints){
    if (geometryMode < -1 || geometryMode > 2)
        throw std::invalid_argument("Shaker scene geometry mode");
    auto &pieces=scene.parts;auto &geometry=scene.geometry;
    if(pieces.size()!=scene.inputParts.size()||pieces.size()!=scene.relationParts.size()||pieces.size()!=geometry.parts.size())throw std::invalid_argument("Shaker scene ownership");
    const auto clock=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();};
    ShakerRunResult result;ConvolutionCacheAccounting accounting;ConvolutionStraightRuntime convolutionRuntime(accounting,[&](){return clock()/100;},clock);
    std::vector<std::shared_ptr<ConvolutionGeometry>> sources;std::vector<int32_t> types;std::vector<ShakerCheckItem> checkItems;
    for(size_t n=0;n<geometry.components.size();++n){const auto &c=geometry.components[n];const auto &part=geometry.parts.at(c.part);const auto shape=scene.inputParts.at(c.part).shape;
        if(!shape||c.points.size()<3)throw std::invalid_argument("Shaker needs normalized nonempty straight components");
        const auto slot=std::find(part.components.begin(),part.components.end(),n);if(slot==part.components.end())throw std::invalid_argument("Shaker component ownership");
        types.push_back(scene.shapes.at(*shape).components.at(size_t(slot-part.components.begin())).type);
        auto g=std::make_shared<ConvolutionGeometry>();g->key=int32_t(n);g->points=c.points;g->reference=part.reference;sources.push_back(std::move(g));checkItems.push_back({c.part,n+1});
    }
    const auto broadphase=[&](double radius){std::vector<ShakerBroadItem> items;for(const auto &c:geometry.components){const auto &b=c.bounds.value;items.push_back({{b[0],b[1]},{b[2],b[3]},c.identity});}return shakerBroadphase(items,radius);};
    const auto excluded=[&](bool rotation,ShakerPairIndices pair){const auto i=geometry.components.at(pair[0]).part,j=geometry.components.at(pair[1]).part;auto &a=scene.relationParts[i],&b=scene.relationParts[j];a.locked=pieces[i].locked;b.locked=pieces[j].locked;return scene.relations.excluded(rotation,a,b,types[pair[0]],types[pair[1]]);};
    const auto convolution=[&](const ShakerConvolutionKey &key,int32_t mode){ConvolutionKey request;request.expansion=key.expansion;
        for(size_t side=0;side<2;++side){const auto &p=key.poses[side];request.poses[side]={sources.at(p.geometry-1),p.angle,p.mirror,p.side};}return convolutionRuntime.get(request,result.convolutionSeconds,static_cast<ConvolutionBackend>(mode));};
    ShakerModelState state;auto &problem=state.problem;LinearModel model;ShakerSolution solution;double tolerance=0;int32_t iterationLimit=0;
    ShakerBuildRuntime build;build.clockNanoseconds=clock;build.broadphase=broadphase;build.sceneSpan=[&](){return shakerSceneSpan(pieces,geometry);};
    for(const auto &constraint:constraints)build.constraints.push_back([&,constraint](bool rotation){result.constraintRows+=shakerPairConstraintRows(constraint,rotation,pieces,model);});
    build.pairRows=[&](ShakerPairIndices pair,int32_t,auto admission,double,double &violation,double span){const auto i=geometry.components[pair[0]].part,j=geometry.components[pair[1]].part;
        ShakerPairOptions config;config.radius=state.broadRadius;config.toleranceLimit=state.toleranceLimit;config.sceneSpan=span;config.geometryMode=geometryMode;config.hasGeometryMode=geometryMode>=0;config.hasAdmissionTolerance=admission.has_value();config.admissionTolerance=admission.value_or(0);
        ShakerConvolutionKey key;key.poses[0]={pair[0]+1,pieces[i].angle,pieces[i].mirror,1};key.poses[1]={pair[1]+1,pieces[j].angle,{uint8_t(!pieces[j].mirror[0]),uint8_t(!pieces[j].mirror[1])},2};
        ShakerPairSource source{[&](bool rotation){return excluded(rotation,pair);},[&](double expansion,int32_t mode){key.expansion=expansion;return convolution(key,mode);}};++result.pairs;return shakerPairRows(problem,model,pieces[i],pieces[j],config,source,violation).status;};
    build.finalize=[&](){shakerFinalizeModel(problem,pieces,model,state.finalize,state.report,geometry);};
    ShakerStepRuntime step;step.prepareScene=[&](){shakerRefreshScene(pieces,geometry);};step.resetSolver=[&](){model={};solution={};};step.solverOption=[&](int32_t value){iterationLimit=value;};step.solverTolerance=[&](double value){tolerance=value;};step.clockNanoseconds=clock;
    step.variables=[&](){return shakerVariables(problem,pieces,model);};step.buildRows=[&](auto admission,double seconds,double &violation){return shakerBuildStep(problem,pieces,state.broadRadius,admission,seconds,violation,build);};step.columns=[&](){return int32_t(model.columns.size());};step.rows=[&](){return int32_t(model.rows.size());};
    step.optimize=[&](double seconds,double &objective){const auto start=clock();solution=solveShakerHighs(model,seconds<0?std::numeric_limits<double>::infinity():seconds,tolerance,iterationLimit);state.solveSeconds+=double(clock()-start)*1e-9;
        return shakerApplySolution(problem,pieces,solution,objective,[&](auto &p){shakerRefreshPart(pieces,geometry,size_t(&p-pieces.data()));});};
    step.solution={[&](){return solution.primal;},[&](){return solution.dual;}};
    ShakerCheckRuntime acceptance;acceptance.prepareScene=step.prepareScene;acceptance.broadphase=broadphase;acceptance.clockNanoseconds=clock;acceptance.excluded=excluded;acceptance.convolution=convolution;acceptance.hasGeometryMode=geometryMode>=0;acceptance.geometryMode=geometryMode;
    ShakerIterationRuntime outer;outer.initializeEnvironment=[](){};outer.clockNanoseconds=clock;outer.sceneSpan=build.sceneSpan;
    outer.solve=[&](const ShakerIterationRequest &request,std::optional<double> admission,double seconds){state=shakerCreateModel(request.policy,request.window,request.angularWindow,request.rotation,request.broadRadius,step.resetSolver);
        double before=0,after=0,violation=0;const auto report=shakerSolveStep(problem,state.report,admission,seconds,before,after,violation,step);++result.solves;result.rotationSolves+=request.rotation;return ShakerIterationResult{report.status,state.report.objective,{state.report.buildSeconds,state.solveSeconds,state.querySeconds}};};
    outer.finishStep=[&](){model={};solution={};};outer.checkPlacement=[&](double seconds,double limit){return shakerCheckPlacement(pieces,checkItems,seconds,limit,acceptance);};outer.refreshPart=[&](size_t n){shakerRefreshPart(pieces,geometry,n);};outer.clearConvolutionCache=[&](){convolutionRuntime.clear();++result.cacheClears;};outer.statistics=[](double,const auto &,int32_t){};
    result.status=shakerIterate(pieces,result.iteration,options,outer);return result;
}
ShakerRunResult runShakerFixedScene(ShakerInputScene &scene,const ShakerIterationOptions &options) {
    if (options.rotate)
        throw std::invalid_argument("Fixed-angle Shaker host cannot rotate");
    for (const auto &part : scene.parts)
        if (!part.locked[2])
            throw std::invalid_argument("Fixed-angle Shaker requires locked rotations");
    return runShakerScene(scene, options);
}
ShakerRunResult compactShakerFixedScene(ShakerInputScene &scene,int32_t first,int32_t second,double seconds,int32_t geometryMode){
    return compactShakerFixedScene(scene,first,second,seconds,geometryMode,{});
}
ShakerRunResult compactShakerFixedScene(ShakerInputScene &scene,int32_t first,int32_t second,double seconds,int32_t geometryMode,
                                      std::span<const ShakerPairConstraint> constraints){
    ShakerRunResult result;result.status=shakerConfigureCompaction(scene.parts,scene.boundaries,first,second);if(result.status)return result;
    for (const auto &part : scene.parts)
        if (!part.locked[2])
            throw std::invalid_argument("Fixed-angle Shaker requires locked rotations");
    ShakerCommonRuntime runtime;runtime.sceneScale=[&](){return shakerSceneScale(scene.parts,scene.geometry,scene.boundaries);};runtime.thresholdFactor=[](){return .0001;};runtime.rotationEnabled=[](){return false;};runtime.iterate=[&](const ShakerIterationOptions &options){result=runShakerScene(scene,options,geometryMode,constraints);return result.iteration.success;};
    const auto status=shakerCommonRun(seconds,{},runtime);result.status=status;return result;
}
ShakerRunResult shakeShakerScene(ShakerInputScene &scene,int32_t direction,bool rotate,double seconds,int32_t geometryMode) {
    return shakeShakerScene(scene,direction,rotate,seconds,geometryMode,{});
}
ShakerRunResult shakeShakerScene(ShakerInputScene &scene,int32_t direction,bool rotate,double seconds,int32_t geometryMode,
    std::span<const ShakerPairConstraint> constraints) {
    ShakerRunResult result;
    result.status = shakerConfigureDefault(scene.parts, scene.boundaries, direction);
    if (result.status)
        return result;
    ShakerCommonRuntime runtime;
    runtime.sceneScale = [&]() { return shakerSceneScale(scene.parts, scene.geometry, scene.boundaries); };
    runtime.thresholdFactor = []() { return .0001; };
    runtime.rotationEnabled = [=]() { return rotate; };
    runtime.iterate = [&](const ShakerIterationOptions &options) {
        result = runShakerScene(scene, options, geometryMode, constraints);
        return result.iteration.success;
    };
    const auto status = shakerCommonRun(seconds, {}, runtime);
    result.status = status;
    return result;
}
}
