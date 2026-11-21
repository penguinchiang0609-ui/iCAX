// Product-owned CAM contract. The planar service receives only conservative
// convex volumes in setup coordinates; workers never read the Scene/database.
namespace {
    namespace FixtureMotion=iCAX::GeometryServices::RigidMotion;
    using FixtureVector=FixtureMotion::Vector;
    Text FixtureCutClearanceMode(const Map& recipe){return recipe.contains("cutClearanceMode")?String(recipe,"cutClearanceMode"):Text("None");}
    void ValidateFixtureCutClearanceRecipe(const Map& recipe){
        const auto mode=FixtureCutClearanceMode(recipe);
        if(mode!="None"&&mode!="Toolpath")throw std::invalid_argument("CAM_FIXTURE_CUT_CLEARANCE_MODE_INVALID");
        if(mode=="None")return;
        for(const auto& [name,minimum,maximum]:{std::tuple{"cutClearanceMm",.05,100.},std::tuple{"beamRadiusMm",.01,25.}}){
            if(!recipe.contains(name))throw std::invalid_argument("CAM_FIXTURE_CUT_CLEARANCE_DIMENSION_INVALID");
            const auto value=FiniteNumber(recipe.at(name),"CAM_FIXTURE_CUT_CLEARANCE_DIMENSION_INVALID");
            if(value<minimum||value>maximum)throw std::invalid_argument("CAM_FIXTURE_CUT_CLEARANCE_DIMENSION_INVALID");
        }
    }
    Text FixtureCutClearanceKey(Scene& scene,const ID& setupID,const Map& recipe){
        if(FixtureCutClearanceMode(recipe)!="Toolpath")return {};
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);Map inputs;
        for(const auto& cut:IntentCuts(scene,setupID))inputs["cut/"+iCAX::Data::to_string(cut->GetID())]=Map{
            {"port",iCAX::CAM::Intent::ToData(cut->Path.Port)},{"fingerprint",cut->Path.Fingerprint},
            {"enabled",CutEnabled(scene,cut->GetID())},{"state",cut->GetResultState()}};
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            const auto id=iCAX::Data::to_string(entity->GetID());
            if(const auto p=Component<CFiveAxisPlacementComponent>(entity);p&&p->GetSetupID()==setupID){
                const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
                inputs[id]=Map{{"transform",p->GetTransform()},{"registration",EngineeringSignature(p->GetRegistrationData())},{"geometry",w->GetGeometryRef()},{"enabled",w->GetEnabled()}};
            }
        }
        // Deliberately excludes fixtures and the setup-wide revision: adopting
        // a fixture invalidates motion planning, but not its own cutting input.
        return iCAX::Data::VariantSerializer::Serialize(Map{{"algorithm",Text("fixture-box-cut-sweep-v2")},{"setupId",iCAX::Data::to_string(setupID)},{"inputs",inputs},
            {"profile",EngineeringSignature(setup->GetPlanningProfileData())},{"head",EffectiveHeadDefinition(scene,*setup)},{"meshDeflectionMm",CollisionMeshDeflectionMm},{"settings",RapidSettings(*setup)},
            {"clearance",recipe.at("cutClearanceMm")},{"beamRadius",recipe.at("beamRadiusMm")}});
    }
    struct FixtureCutSnapshot {
        GenerationSnapshot Generation;
        Map Settings,Head;
        Text Key;
        double Clearance=0,BeamRadius=0;
        std::vector<iCAX::GeometryServices::CollisionBox> HeadBoxes;
        std::vector<Ref> Dependencies;
    };
    std::vector<FixtureVector> FixtureBoxCorners(const iCAX::GeometryServices::CollisionBox& box){
        std::vector<FixtureVector> result;
        for(int bits=0;bits<8;++bits){auto p=box.Center;for(int j=0;j<3;++j)p=FixtureMotion::Add(p,FixtureMotion::Scale(box.Axes[j],box.HalfExtents[j]*(bits&(1<<j)?1:-1)));result.push_back(p);}return result;
    }
    FixtureVector FixtureTransformPoint(const std::array<double,16>& m,FixtureVector p,bool direction=false){
        FixtureVector result{};for(int i=0;i<3;++i){result[i]=direction?0:m[i*4+3];for(int j=0;j<3;++j)result[i]+=m[i*4+j]*p[j];}return result;
    }
    std::vector<FixtureVector> FixtureAxialEnvelope(double z,double height,double lower,double upper){
        // Circumscribed polygons enclose circular frusta, including end caps.
        std::vector<FixtureVector> result;constexpr int sides=12;const auto dilation=1/std::cos(std::numbers::pi/sides);
        for(int end=0;end<2;++end)for(int i=0;i<sides;++i){const auto angle=2*std::numbers::pi*i/sides,radius=(end?upper:lower)*dilation;result.push_back({radius*std::cos(angle),radius*std::sin(angle),z+end*height});}return result;
    }
    Map PrepareFixtureCutKeepOuts(const FixtureCutSnapshot& snapshot,iCAX::Geometry::SectionPlateInput& input,const GeneratedGeometry& generated,
        const iCAX::GeometryServices::IBRepCollisionBoundsService& bounds,const std::shared_ptr<std::atomic_bool>& cancelled){
        namespace M=FixtureMotion;const auto check=[&]{if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();};
        constexpr double chordAllowance=.02;
        const double follow=RegistrationNumber(snapshot.Settings.at("followHeightMm"));
        const double clearance=std::max(snapshot.Clearance,RegistrationNumber(snapshot.Settings.at("collisionThresholdMm")));
        std::vector<std::vector<FixtureVector>> head;
        // Use exactly the shared calibrated box source. Enclosing every roll
        // for fixture construction may remove extra material but cannot leave
        // a lobe of the actual collision boxes outside the generated keep-out.
        for(const auto& box:snapshot.HeadBoxes){double radial=0,z0=1e100,z1=-1e100;
            for(const auto& p:FixtureBoxCorners(box)){radial=std::max(radial,std::hypot(p[0],p[1]));z0=std::min(z0,p[2]);z1=std::max(z1,p[2]);}
            head.push_back(FixtureAxialEnvelope(z0,z1-z0,radial,radial));
        }
        if(head.empty())throw std::runtime_error("CAM_FIXTURE_HEAD_BOUNDS_REQUIRED");
        FixtureVector lo{1e100,1e100,1e100},hi{-1e100,-1e100,-1e100};
        for(const auto& box:bounds.Boxes(*input.Geometry,[&]{return cancelled->load();}))for(auto p:FixtureBoxCorners(box)){p=FixtureTransformPoint(input.Transform,p);for(int j=0;j<3;++j){lo[j]=std::min(lo[j],p[j]);hi[j]=std::max(hi[j],p[j]);}}
        if(lo[0]>hi[0])throw std::runtime_error("CAM_FIXTURE_SOURCE_BOUNDS_REQUIRED");
        lo[2]-=input.BaseDrop+input.BaseThickness+input.MinimumWeb;for(int j=0;j<2;++j){lo[j]-=input.Margin;hi[j]+=input.Margin;}
        if(input.BaseShape=="Circle"&&input.AreaWidth<=0){
            // The exact transformed BRep bounds used by the plate service can
            // have a different center from these transformed outer boxes.
            // Enclose every possible center and the larger automatic radius.
            const double radius=std::max(hi[0]-lo[0],hi[1]-lo[1])/2;
            for(int j=0;j<2;++j){lo[j]-=radius;hi[j]+=radius;}
        }
        if(input.AreaWidth>0){lo[0]=std::min(lo[0],input.AreaX-input.AreaWidth/2);hi[0]=std::max(hi[0],input.AreaX+input.AreaWidth/2);lo[1]=std::min(lo[1],input.AreaY-input.AreaDepth/2);hi[1]=std::max(hi[1],input.AreaY+input.AreaDepth/2);}
        std::vector<FixtureVector> corners;for(int bits=0;bits<8;++bits)corners.push_back({bits&1?hi[0]:lo[0],bits&2?hi[1]:lo[1],bits&4?hi[2]:lo[2]});
        std::size_t beamCells=0,headCells=0,segments=0,samples=0;double maximumSweepAllowance=0;
        const auto addSweep=[&](const std::vector<FixtureVector>& local,FixtureVector a,FixtureVector b,M::Quaternion qa,M::Quaternion qb,double margin,bool beam=false){
            double radius=0;for(auto p:local)radius=std::max(radius,M::Length(p));
            const auto angle=M::Angle(qa,qb),deviation=radius*(1-std::cos(angle/2));maximumSweepAllowance=std::max(maximumSweepAllowance,deviation);
            iCAX::Geometry::SectionPlateKeepOut cell;cell.Margin=margin+chordAllowance+deviation+1e-5;cell.AffectsBase=!beam||!input.SolidBase;
            for(auto p:local){cell.Vertices.push_back(M::Add(a,M::Rotate(qa,p)));cell.Vertices.push_back(M::Add(b,M::Rotate(qb,p)));}
            input.KeepOuts.push_back(std::move(cell));
            if(input.KeepOuts.size()>250000)throw std::invalid_argument("CAM_FIXTURE_CUT_CLEARANCE_LIMIT");
        };
        for(const auto& value:generated.Operations){check();const auto op=value.To<Map>();
            const auto cut=std::find_if(snapshot.Generation.Cuts.begin(),snapshot.Generation.Cuts.end(),[&](const auto& c){return c.IDText==String(op,"intentId");});
            if(cut==snapshot.Generation.Cuts.end())throw std::runtime_error("CAM_FIXTURE_CUT_SOURCE_MISMATCH");const auto transform=RigidMatrix(cut->Transform);
            for(const auto& partValue:op.at("parts").To<Array>()){const auto part=partValue.To<Map>();const auto points=part.at("points").To<Array>(),directions=part.at("directions").To<Array>();
                if(points.size()<2||points.size()!=directions.size())throw std::invalid_argument("CAM_FIXTURE_CUT_POSES_REQUIRED");samples+=points.size();
                for(std::size_t i=0;i+1<points.size();++i){check();++segments;
                    const auto a=FixtureTransformPoint(transform,RegistrationPoint(points[i])),b=FixtureTransformPoint(transform,RegistrationPoint(points[i+1]));
                    const auto da=M::Unit(FixtureTransformPoint(transform,RegistrationPoint(directions[i]),true)),db=M::Unit(FixtureTransformPoint(transform,RegistrationPoint(directions[i+1]),true));
                    RequireRigidHeadDirection(snapshot.Head,da);RequireRigidHeadDirection(snapshot.Head,db);
                    const auto qa=M::Between({0,0,1},da),qb=M::Multiply(M::Between(da,db),qa);
                    // Match the existing collision model: TCP translates
                    // linearly between endpoints; body rotates about TCP.
                    const auto ta=M::Add(a,M::Scale(da,follow)),tb=M::Add(b,M::Scale(db,follow));
                    const auto divisions=std::max(1,static_cast<int>(std::ceil(M::Angle(qa,qb)/.05)));
                    double length=0;for(auto p:corners)length=std::max({length,M::Length(M::Sub(p,a)),M::Length(M::Sub(p,b))});length+=clearance+snapshot.BeamRadius+1;
                    const std::vector<FixtureVector> beam{{0,0,0},{0,0,-length-follow}};
                    for(int piece=0;piece<divisions;++piece){const double first=static_cast<double>(piece)/divisions,last=static_cast<double>(piece+1)/divisions;
                        const auto pa=M::Add(ta,M::Scale(M::Sub(tb,ta),first)),pb=M::Add(ta,M::Scale(M::Sub(tb,ta),last));const auto ra=M::Interpolate(qa,qb,first),rb=M::Interpolate(qa,qb,last);
                        addSweep(beam,pa,pb,ra,rb,clearance+snapshot.BeamRadius,true);++beamCells;
                        for(const auto& body:head){addSweep(body,pa,pb,ra,rb,clearance);++headCells;}
                    }
                }
            }
        }
        if(!beamCells)throw std::invalid_argument("CAM_FIXTURE_CUT_POSES_REQUIRED");
        return {{"mode",Text("Toolpath")},{"status",Text("Clear")},{"basis",Text("ConservativeContinuousSweeps")},{"inputKey",snapshot.Key},{"scope",Text("CutAndLeadsOnly")},
            {"rapidStatus",Text("Unchecked")},{"beamScope",Text(input.SolidBase?"VerticalPlates":"AllPlates")},{"headScope",Text(snapshot.Head.contains("geometryRef")?"CalibratedBoxAxialOuterBounds":"BuiltinBoxAxialOuterBounds")},
            {"representation",Text("RollInvariantBoxCompoundSweeps")},{"headBoxCount",std::to_string(snapshot.HeadBoxes.size())},{"meshDeflectionMm",CollisionMeshDeflectionMm},
            {"operationCount",static_cast<UInt>(generated.Operations.size())},{"sampleCount",static_cast<UInt>(samples)},{"segmentCount",static_cast<UInt>(segments)},
            {"beamSweepCells",static_cast<UInt>(beamCells)},{"headSweepCells",static_cast<UInt>(headCells)},{"clearanceMm",clearance},{"beamRadiusMm",snapshot.BeamRadius},
            {"chordAllowanceMm",chordAllowance},{"maximumRotationAllowanceMm",maximumSweepAllowance},{"productionQualified",false}};
    }
    iCAX::Tasks::Task<std::shared_ptr<PlateGenerationResult>> StartFixturePlateWorker(Scene& scene,const ID& id,const Map& recipe,const iCAX::Geometry::SectionPlateInput& original,const std::shared_ptr<std::atomic_bool>& cancelled){
        const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();const auto display=scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();
        if(!service||!display)throw std::runtime_error("CAM_PLATE_SERVICE_UNAVAILABLE");
        std::shared_ptr<FixtureCutSnapshot> snapshot;
        const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();const auto bounds=scene.Services().Resolve<iCAX::GeometryServices::IBRepCollisionBoundsService>();
        if(FixtureCutClearanceMode(recipe)=="Toolpath"){
            if(!offset||!bounds)throw std::runtime_error("CAM_FIXTURE_CUT_SERVICES_REQUIRED");
            snapshot=std::make_shared<FixtureCutSnapshot>();snapshot->Generation=CaptureGeneration(scene,id);snapshot->Dependencies=snapshot->Generation.Dependencies;
            const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);snapshot->Head=EffectiveHeadDefinition(scene,*setup);snapshot->Settings=RapidSettings(*setup);
            snapshot->Key=FixtureCutClearanceKey(scene,id,recipe);snapshot->Clearance=FiniteNumber(recipe.at("cutClearanceMm"),"CAM_FIXTURE_CUT_CLEARANCE_DIMENSION_INVALID");snapshot->BeamRadius=FiniteNumber(recipe.at("beamRadiusMm"),"CAM_FIXTURE_CUT_CLEARANCE_DIMENSION_INVALID");
            if(snapshot->Head.contains("geometryRef")){
                if(!snapshot->Head.at("calibrated").To<bool>())throw std::invalid_argument("CAM_COLLISION_HEAD_CALIBRATION_REQUIRED");
                snapshot->Dependencies.push_back(ReadWireRef(WireRef(snapshot->Head.at("geometryRef").To<Map>())));
            }
            snapshot->HeadBoxes=HeadCollisionBoxes(scene,snapshot->Head);AppendAdditionalHeadEnvelope(snapshot->HeadBoxes,snapshot->Settings);
        }
        return iCAX::Tasks::Run([original,snapshot,service,display,offset,bounds,cancelled]{
            auto result=std::make_shared<PlateGenerationResult>();auto input=original;
            result->DisplayDeflection=std::min(.025,input.Gap/4);
            result->CutClearance={{"mode",Text("None")},{"status",Text("Unchecked")},{"scope",Text("None")},{"rapidStatus",Text("Unchecked")},{"productionQualified",false}};
            if(snapshot){const auto generated=CalculateGeneration(snapshot->Generation,offset,cancelled);result->CutClearance=PrepareFixtureCutKeepOuts(*snapshot,input,*generated,*bounds,cancelled);result->Dependencies=snapshot->Dependencies;}
            // Keep the plate coordinator on this worker: running it on the CAD
            // pool would force its nested plate jobs inline to avoid deadlock.
            result->Plates=service->Build(input,[cancelled]{return cancelled->load();});
            // All computational BReps are complete before display meshing.
            // The workpiece uses a bounded native face stage directly from the
            // coordinator. Plates then share the same budget, with each native
            // mesher inline. No outer job waits for nested CAD pool jobs.
            const auto displayStarted=std::chrono::steady_clock::now();
            char* traceValue=nullptr;size_t traceLength=0;_dupenv_s(&traceValue,&traceLength,"ICAX_FIXTURE_TRACE");const bool traceEnabled=traceValue!=nullptr;std::free(traceValue);std::mutex traceMutex;
            const auto trace=[&](const std::string& label,std::chrono::steady_clock::time_point phaseStarted)noexcept{
                if(!traceEnabled)return;try{const auto now=std::chrono::steady_clock::now();std::ostringstream line;
                    line<<"fixture display "<<std::chrono::duration_cast<std::chrono::milliseconds>(now-displayStarted).count()<<" ms "<<label<<" elapsedMs="<<std::chrono::duration_cast<std::chrono::milliseconds>(now-phaseStarted).count()<<'\n';
                    std::lock_guard lock(traceMutex);std::cerr<<line.str();
                }catch(...){/* Diagnostics never replace a geometry failure. */}
            };
            const size_t count=result->Plates.size()+1,maximumConcurrency=std::min<size_t>(4,input.MaximumConcurrency?input.MaximumConcurrency:4);
            result->PlateDisplayEdges.resize(result->Plates.size());
            std::vector<std::exception_ptr> failures(count);std::exception_ptr batchFailure;size_t workers=0;
            trace("batch begin jobs="+std::to_string(count)+" maximumConcurrency="+std::to_string(maximumConcurrency),displayStarted);
            const auto prepare=[&](size_t i){
                const auto jobStarted=std::chrono::steady_clock::now();const auto label=i?"plate "+std::to_string(i-1):std::string("workpiece");
                try{
                    if(cancelled->load())throw std::runtime_error("PLATE_CANCELLED");
                    iCAX::GeometryServices::BRepDisplayOptions options;options.IsCancelled=[cancelled]{return cancelled->load();};options.ParallelMeshing=i==0;options.MaximumConcurrency=i?1:maximumConcurrency;
                    options.Deflection=i?.05:result->DisplayDeflection;
                    const auto meshStarted=std::chrono::steady_clock::now();
                    if(i)result->Plates[i-1].Geometry=display->RefineMesh(result->Plates[i-1].Geometry,options);else result->DisplayGeometry=display->RefineMesh(*input.Geometry,options);
                    trace(label+" mesh complete",meshStarted);const auto edgesStarted=std::chrono::steady_clock::now();
                    if(i)result->PlateDisplayEdges[i-1]=display->BuildEdges(result->Plates[i-1].Geometry,options);else result->DisplayEdges=display->BuildEdges(*input.Geometry,options);
                    trace(label+" edges complete",edgesStarted);trace(label+" complete",jobStarted);
                }catch(...){failures[i]=std::current_exception();trace(label+" failed",jobStarted);}
            };
            prepare(0);
            if(failures[0]){trace("batch failed job=0",displayStarted);std::rethrow_exception(failures[0]);}
            try{workers=iCAX::OpenCascade::detail::ForGeometryIndices(result->Plates.size(),maximumConcurrency,[&](size_t i){prepare(i+1);});}catch(...){batchFailure=std::current_exception();}
            // All readers have drained, including cancellation or a scheduler
            // failure. Match the old workpiece-then-plate error precedence.
            for(size_t i=0;i<count;++i)if(failures[i]){trace("batch failed job="+std::to_string(i),displayStarted);std::rethrow_exception(failures[i]);}
            if(batchFailure){trace("batch failed",displayStarted);std::rethrow_exception(batchFailure);}
            trace("batch complete workers="+std::to_string(workers),displayStarted);return result;
        });
    }
}
