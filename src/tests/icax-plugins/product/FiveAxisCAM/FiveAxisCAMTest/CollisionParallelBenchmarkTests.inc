// Include inside namespace five_axis_tests, after SimulationCollisionDomainTests.inc.
// This opt-in acceptance test only reads the source project. All reports are
// written to ICAX_COLLISION_BENCHMARK_OUTPUT on C:.
namespace collision_parallel_benchmark
{
    using Clock=std::chrono::steady_clock;

    std::filesystem::path EnvironmentPath(const wchar_t* name)
    {
        const auto length=GetEnvironmentVariableW(name,nullptr,0);
        if(!length)return {};
        std::wstring value(length,L'\0');
        const auto written=GetEnvironmentVariableW(name,value.data(),length);
        if(!written||written>=length)throw std::runtime_error("Benchmark environment changed while reading it");
        value.resize(written);return std::filesystem::path(value);
    }
    Text PathText(const std::filesystem::path& path)
    {
        const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
    }
    double Milliseconds(Clock::time_point first,Clock::time_point last=Clock::now())
    {
        return std::chrono::duration<double,std::milli>(last-first).count();
    }
    void Write(const std::filesystem::path& output,const Map& result)
    {
        std::ofstream stream(output,std::ios::binary|std::ios::trunc);
        if(!stream)throw std::runtime_error("Cannot create collision benchmark report");
        stream<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(result)<<'\n';
        if(!stream)throw std::runtime_error("Cannot write collision benchmark report");
    }
    Map ActiveSetup(Scene& scene)
    {
        const auto state=Read(scene);
        for(const auto& value:state.at("setups").To<Array>())
            if(value.To<Map>().at("id")==state.at("activeSetupId"))return value.To<Map>();
        throw std::runtime_error("Benchmark project has no active setup");
    }
    bool BudgetLimited(const Map& segment)
    {
        const auto reason=segment.at("reason").To<Text>();
        return reason.find("TIME")!=Text::npos||reason.find("BUDGET")!=Text::npos||
            reason.find("Time")!=Text::npos||reason.find("Budget")!=Text::npos;
    }
    void Issue(Array& issues,const Text& code,const Text& detail,const Text& index={})
    {
        Map issue{{"code",code},{"detail",detail}};
        if(!index.empty())issue["segmentIndex"]=index;
        issues.emplace_back(std::move(issue));
    }
    std::vector<double> RequiredClearances(const Map& report,const Map& setup)
    {
        const auto threshold=setup.at("rapidSettings").To<Map>().at("collisionThresholdMm").To<double>();
        std::map<Text,double> rapidClearances;
        for(const auto& value:setup.at("generatedGeometry").To<Map>().at("transitions").To<Array>()){
            const auto transition=value.To<Map>();
            rapidClearances[transition.at("to").To<Text>()]=threshold+transition.at("policy").To<Map>().at("marginMm").To<double>();
        }
        std::vector<double> frames;
        for(const auto& value:report.at("frames").To<Array>()){
            const auto frame=value.To<Map>();
            frames.push_back(frame.at("role").To<Text>()=="Rapid"?rapidClearances.at(frame.at("intentId").To<Text>()):threshold);
        }
        std::vector<double> segments;
        for(std::size_t i=0;i+1<frames.size();++i)segments.push_back(std::max(frames[i],frames[i+1]));
        if(segments.empty()&&!frames.empty())segments.push_back(frames.front());
        return segments;
    }
    bool FinitePoint(const iCAX::Data::Variant& value)
    {
        const auto point=value.To<Array>();
        return point.size()==3&&std::all_of(point.begin(),point.end(),[](const iCAX::Data::Variant& coordinate){return std::isfinite(coordinate.To<double>());});
    }
    void ValidateReport(const Map& report,const Map& setup,unsigned workers,Array& issues)
    {
        if(report.at("validity").To<Text>()!="Current")Issue(issues,"ReportNotCurrent","The completed task did not publish a current report");
        if(report.at("checkMode").To<Text>()!="Full"||report.at("reusedSegmentCount").To<Text>()!="0")
            Issue(issues,"ReportReused","The benchmark must perform a complete check without reusing a stored report");
        if(report.at("checkOrder").To<Text>()!="KeypointsThenIntervals")Issue(issues,"WrongCheckOrder","Expected the keypoint phase before the interval phase");
        const auto expectedWorkers=std::min(workers,std::max(1u,std::thread::hardware_concurrency()));
        if(std::stoul(report.at("parallelWorkers").To<Text>())!=expectedWorkers)Issue(issues,"WrongWorkerCount","The report worker count does not match the requested benchmark configuration and available processors");
        if(!std::stoull(report.at("keypointCheckCount").To<Text>()))Issue(issues,"NoKeypointChecks","A real project benchmark must include static keypoint checks");
        const auto segments=report.at("segments").To<Array>();
        const auto required=RequiredClearances(report,setup);
        if(segments.size()!=required.size()){Issue(issues,"FrameSegmentCount","The segment count does not match the captured frames");return;}
        if(std::stoull(report.at("checkedSegmentCount").To<Text>())!=segments.size())Issue(issues,"IncompleteCheck","Not all captured intervals were checked");
        for(std::size_t i=0;i<segments.size();++i){
            const auto segment=segments[i].To<Map>();const auto index=std::to_string(i);
            if(segment.at("index").To<Text>()!=index)Issue(issues,"SegmentOrder","A returned segment is not in original trajectory order",index);
            const auto status=segment.at("status").To<Text>();
            const auto fraction=segment.at("fraction").To<double>();
            if(!std::isfinite(fraction)||fraction<0||fraction>1)Issue(issues,"InvalidFraction","A segment fraction is outside the motion interval",index);
            if(status=="Clear"){
                const auto certificate=segment.at("certifiedClearanceMm").To<double>();
                if(!std::isfinite(certificate)||certificate+1e-7<required[i])Issue(issues,"InvalidClearCertificate","The clearance certificate is smaller than the actual required segment clearance",index);
            }else if(status=="Collision"){
                if(segment.at("obstacleId").To<Text>().empty())Issue(issues,"MissingObstacle","A collision must identify its obstacle",index);
                if(!segment.contains("witness")){Issue(issues,"MissingCollisionWitness","A collision must carry an actual witness",index);continue;}
                const auto witness=segment.at("witness").To<Map>();
                const auto separation=witness.at("separationMm").To<double>(),clearance=witness.at("clearanceMm").To<double>(),distance=witness.at("surfaceDistanceMm").To<double>();
                if(!std::isfinite(separation)||separation<0||separation>required[i]+1e-7||!std::isfinite(clearance)||std::abs(clearance-required[i])>1e-7||
                    !std::isfinite(distance)||distance<0||!FinitePoint(witness.at("headPoint"))||!FinitePoint(witness.at("obstaclePoint"))||
                    std::stoull(witness.at("headBoxIndex").To<Text>())>=std::stoull(report.at("headBoxCount").To<Text>()))
                    Issue(issues,"InvalidCollisionWitness","The collision witness does not certify the required clearance with a valid body box and finite points",index);
            }else if(status!="Unchecked")Issue(issues,"UnknownSegmentStatus","Unexpected collision segment status",index);
        }
    }
    unsigned ParallelTasks()
    {
        const auto value=EnvironmentPath(L"ICAX_COLLISION_BENCHMARK_PARALLEL_TASKS").wstring();
        if(value.empty())return 8;
        std::size_t consumed=0;const auto workers=std::stoul(value,&consumed);
        if(consumed!=value.size()||workers<1||workers>32)throw std::invalid_argument("ICAX_COLLISION_BENCHMARK_PARALLEL_TASKS must be an integer from 1 to 32");
        return static_cast<unsigned>(workers);
    }
    Map LoadSerialBaseline(const std::filesystem::path& source,const std::filesystem::path& baseline)
    {
        if(!baseline.is_absolute()||!std::filesystem::is_regular_file(baseline))throw std::invalid_argument("Serial baseline must be an existing absolute JSON path");
        std::ifstream stream(baseline,std::ios::binary);
        if(!stream)throw std::runtime_error("Cannot read serial benchmark baseline");
        const Text contents{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
        const auto original=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(contents).To<Map>();
        const auto originalSource=original.at("sourceProject").To<Text>();
        const auto originalPath=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(originalSource.data()),originalSource.size()));
        if(!std::filesystem::equivalent(source,originalPath))throw std::invalid_argument("Serial baseline belongs to a different source project");
        if(!original.at("sourceProjectUnchanged").To<bool>())throw std::invalid_argument("Serial baseline did not preserve the source project");
        if(std::filesystem::last_write_time(source)>std::filesystem::last_write_time(baseline))throw std::invalid_argument("Source project is newer than the serial baseline JSON");
        auto serial=original.at("serial").To<Map>();
        if(serial.at("status").To<Text>()!="Passed"||serial.at("requestedWorkers").To<Text>()!="1"||serial.at("timedOut").To<bool>())
            throw std::invalid_argument("Serial baseline must contain a Passed, completed one worker round");
        if(serial.at("totalMs").To<double>()<=0)throw std::invalid_argument("Serial baseline has no positive measured duration");
        iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        const auto prepared=project.PrepareOpen(source);const auto info=prepared.Result().Info;
        Scene scene(info.ProjectID,info.MainSceneID,false);
        project.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
        Array issues;ValidateReport(serial.at("report").To<Map>(),ActiveSetup(scene),1,issues);
        for(const auto& value:serial.at("progress").To<Array>()){
            const auto progress=value.To<Map>().at("collisionProgress").To<Map>();
            if(progress.at("stage").To<Text>()=="Intervals"&&progress.at("completedKeypointCount")!=progress.at("totalKeypointCount"))
                Issue(issues,"BaselinePhaseBarrierViolation","The saved baseline started intervals before all keypoint checks completed");
        }
        if(!issues.empty())throw std::invalid_argument("Serial baseline evidence failed validation: "+iCAX::Data::VariantSerializer::Serialize(issues));
        serial["reusedBaseline"]=true;serial["baselinePath"]=PathText(baseline);
        std::cout<<"[collision benchmark] reusing measured serial baseline "<<PathText(baseline)<<" totalMs="<<serial.at("totalMs").To<double>()<<std::endl;
        return serial;
    }
    Map Run(const std::filesystem::path& source,unsigned workers)
    {
        Map result{{"requestedWorkers",std::to_string(workers)},{"timeoutMs",Text("900000")},{"pollIntervalMs",Text("25")},{"status",Text("Preparing")}};
        Array progress,issues;std::unique_ptr<Scene> scene;Map task;Text previousProgress;
        Clock::time_point lastProgressLog{};
        const auto restoreStart=Clock::now();
        Clock::time_point started{},requested{};
        bool startedTask=false,returnedTask=false,timedOut=false,observedIntervals=false;
        const auto observe=[&](const Map& value){
            if(!value.contains("collisionProgress"))return;
            const auto current=value.at("collisionProgress").To<Map>();
            const auto now=Clock::now();
            if(lastProgressLog==Clock::time_point{}||now-lastProgressLog>=std::chrono::seconds(10)){
                std::cout<<"[collision benchmark] workers="<<workers<<" elapsedMs="<<Milliseconds(started,now)
                    <<" stage="<<current.at("stage").To<Text>()
                    <<" keypoints="<<current.at("completedKeypointCount").To<Text>()<<'/'<<current.at("totalKeypointCount").To<Text>()
                    <<" segments="<<current.at("completedSegmentCount").To<Text>()<<'/'<<current.at("totalSegmentCount").To<Text>()<<std::endl;
                lastProgressLog=now;
            }
            if(current.at("stage").To<Text>()=="Intervals"){
                observedIntervals=true;
                if(current.at("completedKeypointCount")!=current.at("totalKeypointCount"))
                    Issue(issues,"PhaseBarrierViolation","Interval checks started before every keypoint check completed");
            }
            const auto serialized=iCAX::Data::VariantSerializer::Serialize(current);
            if(serialized!=previousProgress){
                progress.emplace_back(Map{{"elapsedMs",Milliseconds(started)},{"taskStatus",value.at("status")},{"collisionProgress",current}});
                previousProgress=serialized;
            }
        };
        try{
            iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
            const auto prepared=project.PrepareOpen(source);const auto info=prepared.Result().Info;
            scene=std::make_unique<Scene>(info.ProjectID,info.MainSceneID,false);
            project.Restore(prepared,scene->Database(),scene->Resources());InitializeScene(*scene);
            const auto state=Read(*scene);const auto setupID=*ID::from_string(state.at("activeSetupId").To<Text>());
            auto& transaction=scene->Database().BeginTransaction("Acceptance benchmark: clear stored collision report");
            transaction.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"SimulationCheckData",Map{}}});
            if(!scene->Database().CommitTransaction(transaction))throw std::runtime_error("Could not clear stored collision report");
            if(!scene->Database().GetEntity(setupID)->GetComponent<CFiveAxisSetupComponent>()->GetSimulationCheckData().empty())throw std::runtime_error("Stored collision report was not cleared");
            const auto setup=ActiveSetup(*scene);
            result["restoreAndPrepareMs"]=Milliseconds(restoreStart);
            auto request=GenerateRequest(*scene);request["maxParallelTasks"]=std::to_string(workers);
            started=Clock::now();startedTask=true;
            task=Invoke(scene.get(),"CamSimulation","CheckCollision",request);requested=Clock::now();returnedTask=true;
            result["startRequestMs"]=Milliseconds(started,requested);result["startTask"]=task;observe(task);
            const auto deadline=started+std::chrono::minutes(15);
            while(task.at("status").To<Text>()=="Running"){
                if(Clock::now()>=deadline){
                    timedOut=true;Issue(issues,"TaskTimeout","The collision task exceeded the 15 minute benchmark budget");
                    Invoke(scene.get(),"CamSimulation","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                task=Invoke(scene.get(),"CamWorkpiece","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});observe(task);
            }
            if(timedOut){
                // Join cancellation before destroying the scene and before
                // starting the independent next round.
                const auto cancellationDeadline=Clock::now()+std::chrono::seconds(30);
                while(task.at("status").To<Text>()=="Running"&&Clock::now()<cancellationDeadline){
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    task=Invoke(scene.get(),"CamWorkpiece","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});observe(task);
                }
                if(task.at("status").To<Text>()=="Running")Issue(issues,"CancellationTimeout","The timed out task did not finish cancellation within 30 seconds");
            }
            const auto finished=Clock::now();
            result["waitMs"]=Milliseconds(requested,finished);result["totalMs"]=Milliseconds(started,finished);result["terminalTask"]=task;
            if(task.at("status").To<Text>()!="Completed")Issue(issues,"TaskNotCompleted","The collision task ended with status "+task.at("status").To<Text>());
            else{
                const auto report=ActiveSetup(*scene).at("simulationCheck").To<Map>();result["report"]=report;
                for(const auto* key:{"checkOrder","parallelWorkers","keypointCheckCount"})result[key]=report.at(key);
                Array budgetLimited;for(const auto& value:report.at("segments").To<Array>())if(BudgetLimited(value.To<Map>()))budgetLimited.emplace_back(value.To<Map>().at("index"));
                result["budgetLimitedSegmentIndices"]=budgetLimited;
                ValidateReport(report,setup,workers,issues);
                if(!observedIntervals)Issue(issues,"MissingIntervalProgress","The benchmark never observed interval phase progress");
            }
        }catch(const std::exception& error){
            Issue(issues,"RunException",error.what());
            if(startedTask){result["totalMs"]=Milliseconds(started);if(returnedTask)result["waitMs"]=Milliseconds(requested);else result["startRequestMs"]=Milliseconds(started);}
            if(!task.empty())result["lastTask"]=task;
        }
        result["timedOut"]=timedOut;result["progress"]=progress;result["issues"]=issues;
        result["status"]=Text(issues.empty()?"Passed":"Failed");
        std::cout<<"[collision benchmark] workers="<<workers<<" totalMs="
            <<(result.contains("totalMs")?result.at("totalMs").To<double>():0.)
            <<" status="<<result.at("status").To<Text>()<<std::endl;
        return result;
    }
    Map Compare(const Map& serial,const Map& parallel)
    {
        Array issues,differences;
        if(serial.at("status").To<Text>()!="Passed"||parallel.at("status").To<Text>()!="Passed")Issue(issues,"RunFailed","Both full collision checks must complete and pass report validation");
        if(serial.contains("report")&&parallel.contains("report")){
            const auto first=serial.at("report").To<Map>(),second=parallel.at("report").To<Map>();
            if(first.at("frames")!=second.at("frames"))Issue(issues,"FramesChanged","Serial and parallel checks captured different complete poses");
            if(first.at("keypointCheckCount")!=second.at("keypointCheckCount"))Issue(issues,"KeypointCountChanged","The two runs did not check the same number of keypoints");
            if(first.contains("keypoints")&&second.contains("keypoints")){
                const auto keypointsA=first.at("keypoints").To<Array>(),keypointsB=second.at("keypoints").To<Array>();
                if(keypointsA.size()!=keypointsB.size())Issue(issues,"KeypointResultCountChanged","The two runs returned different numbers of static keypoint results");
                for(std::size_t i=0;i<std::min(keypointsA.size(),keypointsB.size());++i){
                    const auto left=keypointsA[i].To<Map>(),right=keypointsB[i].To<Map>();
                    if(left.at("frameIndex")!=right.at("frameIndex")||left.at("status")!=right.at("status")||left.at("obstacleId")!=right.at("obstacleId"))
                        Issue(issues,BudgetLimited(left)||BudgetLimited(right)?"TimeBudgetKeypointMismatch":"KeypointResultMismatch","A static keypoint result differs between serial and parallel checks",std::to_string(i));
                }
            }
            const auto a=first.at("segments").To<Array>(),b=second.at("segments").To<Array>();
            if(a.size()!=b.size())Issue(issues,"SegmentCountChanged","Serial and parallel checks returned different segment counts");
            for(std::size_t i=0;i<std::min(a.size(),b.size());++i){
                const auto left=a[i].To<Map>(),right=b[i].To<Map>();
                if(left.at("status")!=right.at("status")||left.at("obstacleId")!=right.at("obstacleId")){
                    const bool budget=BudgetLimited(left)||BudgetLimited(right);
                    differences.emplace_back(Map{{"segmentIndex",std::to_string(i)},{"serial",left},{"parallel",right},{"budgetLimited",budget}});
                    Issue(issues,budget?"TimeBudgetResultMismatch":"SegmentResultMismatch",budget?
                        "Segment status or obstacle differs after a time limited solve; no speedup claim is valid":
                        "Segment status or obstacle differs between serial and parallel checks",std::to_string(i));
                }
            }
        }else Issue(issues,"MissingReport","Cannot compare missing collision reports");
        Map result{{"status",Text(issues.empty()?"Passed":"Failed")},{"issues",issues},{"segmentDifferences",differences},
            {"comparisonPolicy",Text("Exact frames and ordered segment status/obstacle; independently valid clearance certificates and collision witnesses; witness fractions and certificate numeric values need not be identical")}};
        if(issues.empty()){
            const auto serialMs=serial.at("totalMs").To<double>(),parallelMs=parallel.at("totalMs").To<double>();
            if(parallelMs>0)result["totalTimeSpeedup"]=serialMs/parallelMs;
        }
        return result;
    }
}

TEST(FiveAxisCollisionParallel, DISABLED_RealProjectSerialAndParallelBenchmark)
{
    using namespace collision_parallel_benchmark;
    const auto source=EnvironmentPath(L"ICAX_COLLISION_BENCHMARK_PROJECT");
    const auto output=EnvironmentPath(L"ICAX_COLLISION_BENCHMARK_OUTPUT");
    if(source.empty()||output.empty()){
        std::cout<<"Set ICAX_COLLISION_BENCHMARK_PROJECT and ICAX_COLLISION_BENCHMARK_OUTPUT to opt into the real project benchmark"<<std::endl;
        return;
    }
    ASSERT_TRUE(source.is_absolute());ASSERT_TRUE(std::filesystem::is_regular_file(source));
    ASSERT_TRUE(output.is_absolute());ASSERT_TRUE(output.root_name()==L"C:"||output.root_name()==L"c:")<<"Benchmark output must be on C:";
    ASSERT_NE(std::filesystem::weakly_canonical(source),std::filesystem::weakly_canonical(output));
    std::filesystem::create_directories(output.parent_path());
    const auto originalSize=std::filesystem::file_size(source);
    const auto originalTime=std::filesystem::last_write_time(source);
    const auto parallelTasks=ParallelTasks();
    const auto baseline=EnvironmentPath(L"ICAX_COLLISION_BENCHMARK_SERIAL_REPORT");
    if(!baseline.empty())ASSERT_NE(std::filesystem::weakly_canonical(baseline),std::filesystem::weakly_canonical(output))<<"Output must preserve the existing serial baseline JSON";
    Map result{{"schema",Text("icax.collision-parallel-benchmark")},{"sourceProject",PathText(source)},{"output",PathText(output)},
        {"status",Text("Running")},{"sourceProjectReadOnly",true},{"serialMeasuredThisRun",baseline.empty()},
        {"roundOrder",baseline.empty()?Array{Text("1"),std::to_string(parallelTasks)}:Array{std::to_string(parallelTasks)}}};
    if(!baseline.empty())result["serialBaselinePath"]=PathText(baseline);
    Write(output,result);
    Map serial;
    try{serial=baseline.empty()?collision_parallel_benchmark::Run(source,1):LoadSerialBaseline(source,baseline);}
    catch(const std::exception& error){result["status"]=Text("Failed");result["error"]=Text(error.what());Write(output,result);FAIL()<<error.what();}
    result["serial"]=serial;Write(output,result);
    const auto parallel=collision_parallel_benchmark::Run(source,parallelTasks);result["parallel"]=parallel;
    auto comparison=Compare(serial,parallel);
    const bool unchanged=originalSize==std::filesystem::file_size(source)&&originalTime==std::filesystem::last_write_time(source);
    result["sourceProjectUnchanged"]=unchanged;
    if(!unchanged){auto issues=comparison.at("issues").To<Array>();Issue(issues,"SourceChanged","The source project size or write timestamp changed during the benchmark");comparison["issues"]=issues;comparison["status"]=Text("Failed");comparison.erase("totalTimeSpeedup");}
    result["comparison"]=comparison;result["status"]=comparison.at("status");Write(output,result);
    EXPECT_EQ("Passed",result.at("status").To<Text>())<<"See complete timing, progress, reports and failure evidence in "<<PathText(output);
}
