// This connects the recovered outer controller to real straight geometry and
// HiGHS. The full SDK and the geometric/configuration input producers remain
// separate from this explicit ordinary fixed-angle scene host.
// Intersection area is checked by independent triangle clipping, without
// using the convolution, winding or LP predicates that generated the result.
size_t shakerPipelineSolves=0;
void testShakerPipeline(){
    const auto cross=[](ShakerPoint a,ShakerPoint b,ShakerPoint c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
    const auto triangleIntersection=[&](std::array<ShakerPoint,3> a,std::array<ShakerPoint,3> b){
        std::vector<ShakerPoint> polygon(a.begin(),a.end());
        for(size_t edge=0;edge<3&&!polygon.empty();++edge){std::vector<ShakerPoint> next;
            for(size_t n=0;n<polygon.size();++n){const auto first=polygon[n],second=polygon[(n+1)%polygon.size()];const double da=cross(b[edge],b[(edge+1)%3],first),db=cross(b[edge],b[(edge+1)%3],second);
                if(da>=0)next.push_back(first);if((da>=0)!=(db>=0)){const double t=da/(da-db);next.push_back({first[0]+(second[0]-first[0])*t,first[1]+(second[1]-first[1])*t});}}
            polygon=std::move(next);
        }
        double area=0;for(size_t n=0;n<polygon.size();++n)area+=polygon[n][0]*polygon[(n+1)%polygon.size()][1]-polygon[(n+1)%polygon.size()][0]*polygon[n][1];return std::abs(area)*.5;
    };
    const std::array<std::vector<ShakerPoint>,3> shapes{{{{0,0},{3,0},{3,2},{0,2}},{{0,0},{3,0},{0,2}},{{0,0},{3,0},{3,1},{1,1},{1,3},{0,3}}}};
    for(size_t scene=0;scene<16;++scene){
        const size_t count=6+scene%10;std::vector<std::vector<ShakerPoint>> polygons(count);std::vector<ShakerPart> pieces(count);
        ShakerRelations relations;std::vector<ShakerRelationPart> relationParts(count);std::vector<ShakerPoint> sizes(count);
        for(size_t n=0;n<count;++n){polygons[n]=shapes[(n+scene)%shapes.size()];const double scale=scene%2?1:.5;
            double minX=0,minY=0,maxX=0,maxY=0;for(auto &p:polygons[n]){p[0]*=scale;p[1]*=scale;if((scene+n)%2)p={-p[1],p[0]};minX=std::min(minX,p[0]);minY=std::min(minY,p[1]);maxX=std::max(maxX,p[0]);maxY=std::max(maxY,p[1]);}
            for(auto &p:polygons[n]){p[0]-=minX;p[1]-=minY;}sizes[n]={maxX-minX,maxY-minY};
            pieces[n]=part(6+5*double(n%4),5+4*double(n/4));pieces[n].locked[2]=1;pieces[n].bounds={0,40-sizes[n][0],0,30-sizes[n][1]};pieces[n].objective={scene%3?.25:1,scene%3?1:.25,0};
            relationParts[n].id=int32_t(n);relationParts[n].identity=reinterpret_cast<uintptr_t>(&pieces[n]);relationParts[n].locked=pieces[n].locked;relationParts[n].values[int32_t(n)]={0,0,1};relations.types.index(int32_t(n));
            relations.x.parents.push_back(int32_t(n));relations.y.parents.push_back(int32_t(n));relations.x.indices[relationParts[n].identity]=int32_t(n);relations.y.indices[relationParts[n].identity]=int32_t(n);
        }
        for(size_t a=0;a<count;++a)for(size_t b=0;b<count;++b)relations.types.set(int32_t(a),int32_t(b),a!=b);
        ShakerSceneGeometry geometry;geometry.parts.resize(count);std::vector<ShakerCheckItem> checkItems;
        for(size_t n=0;n<count;++n){geometry.parts[n].typeIdentity=n+1;geometry.parts[n].components={n};ShakerGeometryComponent component;component.part=n;component.identity=n+1;component.points=polygons[n];geometry.components.push_back(component);checkItems.push_back({n,n+1});}
        const auto broadphase=[&](double radius){std::vector<ShakerBroadItem> items;for(const auto &c:geometry.components){const auto &b=c.bounds.value;items.push_back({{b[0],b[1]},{b[2],b[3]},c.identity});}return shakerBroadphase(items,radius);};
        ConvolutionCacheAccounting cacheAccounting;int64_t convolutionTicks=0;ConvolutionStraightRuntime convolutionRuntime(cacheAccounting,[&](){return ++convolutionTicks*10000;},[&](){return ++convolutionTicks*1000000;});
        double convolutionSeconds=0;std::vector<std::shared_ptr<ConvolutionGeometry>> sourceGeometry;
        for(size_t n=0;n<count;++n){auto g=std::make_shared<ConvolutionGeometry>();g->key=int32_t(n);g->reference=geometry.parts[n].reference;g->points=polygons[n];sourceGeometry.push_back(std::move(g));}
        const auto convolution=[&](const ShakerConvolutionKey &key,int32_t mode){
            check(key.expansion==0&&mode==0,"pipeline straight convolution request");ConvolutionKey request;request.expansion=key.expansion;
            for(size_t side=0;side<2;++side){const auto &p=key.poses[side];request.poses[side]={sourceGeometry.at(p.geometry-1),p.angle,p.mirror,p.side};}
            return convolutionRuntime.get(request,convolutionSeconds);
        };
        for(size_t round=0;round<3;++round){
            ShakerModelState modelState;auto &problem=modelState.problem;auto &state=modelState.report;auto &finalState=modelState.finalize;LinearModel model;ShakerSolution solution;
            int64_t ticks=0;const auto clock=[&](){return ++ticks*1000000;};double tolerance=0;int32_t iterationLimit=0;size_t pairsBuilt=0,solves=0;
            const auto excluded=[&](bool rotation,ShakerPairIndices pair){return relations.excluded(rotation,relationParts[pair[0]],relationParts[pair[1]],int32_t(pair[0]),int32_t(pair[1]));};
            ShakerBuildRuntime build;build.clockNanoseconds=clock;build.broadphase=broadphase;build.sceneSpan=[&](){return shakerSceneSpan(pieces,geometry);};
            build.pairRows=[&](ShakerPairIndices pair,int32_t,auto admission,double,double &violation,double span){const auto i=pair[0],j=pair[1];ShakerPairOptions options;options.radius=modelState.broadRadius;options.toleranceLimit=modelState.toleranceLimit;options.sceneSpan=span;options.geometryMode=0;options.hasAdmissionTolerance=admission.has_value();options.admissionTolerance=admission.value_or(0);
                ShakerConvolutionKey key;key.poses[0]={i+1,pieces[i].angle,pieces[i].mirror,1};key.poses[1]={j+1,pieces[j].angle,{uint8_t(!pieces[j].mirror[0]),uint8_t(!pieces[j].mirror[1])},2};
                ShakerPairSource source{[&](bool rotation){return excluded(rotation,pair);},[&](double expansion,int32_t mode){key.expansion=expansion;return convolution(key,mode);}};++pairsBuilt;return shakerPairRows(problem,model,pieces[i],pieces[j],options,source,violation).status;};
            build.finalize=[&](){shakerFinalizeModel(problem,pieces,model,finalState,state,geometry);};
            ShakerStepRuntime runtime;runtime.prepareScene=[&](){shakerRefreshScene(pieces,geometry);};runtime.resetSolver=[&](){model={};solution={};};runtime.solverOption=[&](int32_t option){iterationLimit=option;check(option==6000,"pipeline recovered iteration limit");};runtime.solverTolerance=[&](double value){tolerance=value;};runtime.clockNanoseconds=clock;
            runtime.variables=[&](){return shakerVariables(problem,pieces,model);};runtime.buildRows=[&](auto admission,double seconds,double &violation){return shakerBuildStep(problem,pieces,modelState.broadRadius,admission,seconds,violation,build);};runtime.columns=[&](){return int32_t(model.columns.size());};runtime.rows=[&](){return int32_t(model.rows.size());};
            runtime.optimize=[&](double seconds,double &objective){solution=solveShakerHighs(model,seconds<0?std::numeric_limits<double>::infinity():seconds,tolerance,iterationLimit);return shakerApplySolution(problem,pieces,solution,objective,[&](auto &p){shakerRefreshPart(pieces,geometry,size_t(&p-pieces.data()));});};runtime.solution={[&](){return solution.primal;},[&](){return solution.dual;}};
            ShakerCheckRuntime acceptance;acceptance.prepareScene=runtime.prepareScene;acceptance.broadphase=broadphase;acceptance.clockNanoseconds=clock;acceptance.excluded=excluded;acceptance.convolution=convolution;acceptance.hasGeometryMode=true;acceptance.geometryMode=0;
            ShakerIterationRuntime outer;outer.initializeEnvironment=[](){};outer.clockNanoseconds=clock;outer.sceneSpan=build.sceneSpan;
            double initialObjective=0;for(const auto &p:pieces)initialObjective+=p.objective[0]*p.position[0]+p.objective[1]*p.position[1];
            outer.solve=[&](const ShakerIterationRequest &request,std::optional<double> admission,double seconds){
                modelState=shakerCreateModel(request.policy,request.window,request.angularWindow,request.rotation,request.broadRadius,runtime.resetSolver);
                double before=0,after=0,violation=0;const auto report=shakerSolveStep(problem,state,admission,seconds,before,after,violation,runtime);++solves;++shakerPipelineSolves;
                check(report.status==0,"pipeline recovered solve control status");check(after<=before+1e-7,"pipeline step objective does not increase");
                return ShakerIterationResult{report.status,state.objective,{state.buildSeconds,modelState.solveSeconds,modelState.querySeconds}};
            };
            outer.finishStep=[](){};outer.checkPlacement=[&](double seconds,double limit){return shakerCheckPlacement(pieces,checkItems,seconds,limit,acceptance);};
            outer.refreshPart=[&](size_t n){shakerRefreshPart(pieces,geometry,n);};outer.clearConvolutionCache=[](){throw std::runtime_error("Fixed-angle pipeline entered rotation cache clear");};
            outer.statistics=[](double,const auto &,int32_t){};
            ShakerIterationState iteration;ShakerIterationOptions options;options.maximumIterations=3;options.stallLimit=2;options.seconds=30;options.translationWindow=12;options.retryStatus2=true;
            check(shakerIterate(pieces,iteration,options,outer)==0&&iteration.success,"pipeline nonlinear acceptance and saved-pose restore");
            check(solves>=2&&pairsBuilt>0,"pipeline iterated real broadphase/model/solve");check(iteration.objective<=initialObjective+1e-7,"pipeline saved optimum");
            std::vector<std::vector<ShakerPoint>> world=polygons;
            for(size_t n=0;n<count;++n){check(pieces[n].position[0]>=-1e-8&&pieces[n].position[1]>=-1e-8&&pieces[n].position[0]+sizes[n][0]<=40+1e-8&&pieces[n].position[1]+sizes[n][1]<=30+1e-8,"pipeline board bounds");for(auto &p:world[n]){p[0]+=pieces[n].position[0];p[1]+=pieces[n].position[1];}}
            for(size_t i=0;i<count;++i)for(size_t j=i+1;j<count;++j){double intersection=0;for(size_t a=1;a+1<world[i].size();++a)for(size_t b=1;b+1<world[j].size();++b)intersection+=triangleIntersection({world[i][0],world[i][a],world[i][a+1]},{world[j][0],world[j][b],world[j][b+1]});check(intersection<1e-7,"pipeline independent polygon interiors do not overlap");}
        }
    }
}
