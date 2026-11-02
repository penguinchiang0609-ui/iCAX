namespace {
    Map ValidateMachinePost(Scene& scene,const ID& machineID,const Map& post){
        CutFields(post,{"name","format","extension","axes","header","footer","processOn","processOff","motion","cutFeedMmMin","travelFeedMmMin"});
        if(String(post,"name").empty())throw std::invalid_argument("请填写后置名称。");
        const auto format=String(post,"format");if(format!="JointJSON"&&format!="InverseTimeNC")throw std::invalid_argument("不支持的后置格式。");
        const auto extension=String(post,"extension");if(extension.empty()||extension.size()>12||extension.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")!=Text::npos)throw std::invalid_argument("文件扩展名只能包含英文字母和数字。");
        auto normalized=post;
        const auto blank=[](const Variant& v){if(v.Is<std::monostate>())return true;if(!v.Is<Text>())return false;const auto text=v.To<Text>();return std::all_of(text.begin(),text.end(),[](unsigned char c){return std::isspace(c);});};
        for(const auto key:{"cutFeedMmMin","travelFeedMmMin"}){if(!post.contains(key))continue;if(blank(post.at(key))){normalized.erase(key);continue;}const auto n=FiniteNumber(post.at(key),"后置进给必须为有限数值。");if(n<=0||n>1e6)throw std::invalid_argument("已填写的进给速度必须大于零且在有效范围内，留空表示不限。");}
        std::set<Text> joints,used,words;
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto p=Component<iCAX::CAM::CMachineElementComponent>(e);p&&p->GetMachineID()==machineID)
            if(const auto j=Component<iCAX::CAM::CMachineJointComponent>(e);j&&j->GetJointType()!="fixed")joints.insert(iCAX::Data::to_string(e->GetID()));
        Array axes;
        for(const auto& value:post.at("axes").To<Array>()){
            auto a=value.To<Map>();CutFields(a,{"jointId","word","sign","offset","maxAcceleration","maxJerk"});const auto id=String(a,"jointId"),word=String(a,"word");
            if(!joints.contains(id)||!used.insert(id).second)throw std::invalid_argument("后置轴必须与本机床运动关节逐一对应。");
            if(word.size()!=1||Text("XYZABCUVW").find(word)==Text::npos||!words.insert(word).second)throw std::invalid_argument("NC 轴字应为互不重复的 X/Y/Z/A/B/C/U/V/W。");
            const auto sign=FiniteNumber(a.at("sign"),"后置轴方向无效。");if(sign!=1&&sign!=-1)throw std::invalid_argument("后置轴方向只能是 +1 或 -1。");
            (void)FiniteNumber(a.at("offset"),"后置轴零位偏置无效。");
            for(const auto key:{"maxAcceleration","maxJerk"})if(a.contains(key)){if(blank(a.at(key)))a.erase(key);else if(FiniteNumber(a.at(key),"轴动态参数必须为有效数值。")<=0)throw std::invalid_argument("已填写的轴加速度与加加速度上限必须大于零，留空表示不限。");}
            axes.emplace_back(a);
        }
        if(used!=joints||joints.empty())throw std::invalid_argument("后置必须覆盖全部运动轴。");
        for(const auto key:{"header","footer","processOn","processOff","motion"}){const auto text=String(post,key);if(text.size()>16384||text.find('\0')!=Text::npos)throw std::invalid_argument("后置指令文本无效。");}
        if(format=="InverseTimeNC"){
            const auto motion=String(post,"motion");if(motion.find("{axes}")==Text::npos||motion.find("{feed}")==Text::npos||motion.find('\n')!=Text::npos||motion.find('\r')!=Text::npos)throw std::invalid_argument("运动行必须包含 {axes} 和 {feed}，采用各轴线性插补与逆时间进给。");
            auto tokens=motion;for(const auto token:{"{axes}","{feed}","{line}"}){size_t at=0;while((at=tokens.find(token))!=Text::npos)tokens.erase(at,std::strlen(token));}
            if(tokens.find_first_of("{}")!=Text::npos)throw std::invalid_argument("后置运动行存在未知占位符。");
            if(String(post,"header").empty()||String(post,"footer").empty()||String(post,"processOn").empty()||String(post,"processOff").empty())throw std::invalid_argument("NC 后置须配置程序头尾及加工启停指令。");
        }
        normalized["axes"]=axes;return normalized;
    }
    void WriteMachineProgram(const Text& destination,const Text& bytes){
        const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(destination.data()),destination.size()));
        if(!path.is_absolute()||!std::filesystem::is_directory(path.parent_path()))throw std::invalid_argument("CAM_OUTPUT_FILE_PATH_REQUIRED");
        auto temporary=path;temporary+=L".tmp-"+std::filesystem::path(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())).wstring();
        try{{std::ofstream f(temporary,std::ios::binary);f.write(bytes.data(),bytes.size());f.flush();if(!f)throw std::runtime_error("CAM_OUTPUT_WRITE_FAILED");}
            {std::ifstream f(temporary,std::ios::binary);const Text actual((std::istreambuf_iterator<char>(f)),{});if(actual!=bytes)throw std::runtime_error("CAM_OUTPUT_READBACK_FAILED");}
            if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("CAM_OUTPUT_PUBLISH_FAILED");
        }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
    }
}
Map ConfigureMachinePost(Scene& scene,const Map& request){
    CutFields(request,{"expected","machineId","postprocessor"});CheckExpected(*Runtime(scene),request);SetupIdle(scene);
    const auto id=Identifier(request,"machineId");Require<iCAX::CAM::CMachineInstanceComponent>(scene.Database(),id);
    const auto post=ValidateMachinePost(scene,id,request.at("postprocessor").To<Map>());
    Transaction(scene.Database(),"设置机床实例后置",[&](auto& tx){tx.ModifyComponent(id,iCAX::CAM::CMachineInstanceComponent::S_ClassName,{{"Postprocessor",post}});});
    return {{"postprocessor",post}};
}
Map ExportMachineProgram(Scene& scene,const Map& request){
    CutFields(request,{"expected","setupId","modelKey","destination"});CheckExpected(*Runtime(scene),request);SetupIdle(scene);
    const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),Identifier(request,"setupId"));
    const auto machine=CaptureMachine(scene,setup->GetMachineSetup());const auto snapshot=MachineSnapshotState(machine);
    if(String(request,"modelKey")!=String(snapshot,"modelKey"))throw std::invalid_argument("机床或后置已变化，请重新仿真后导出。");
    const auto post=ValidateMachinePost(scene,Identifier(machine.Configuration,"machineId"),machine.Postprocessor);
    if(String(post,"format")!="JointJSON")throw std::invalid_argument("CAM_OUTPUT_CAPABILITY_UNSUPPORTED: 当前通用 G1 后置不能保持连续五次轴轨迹，请使用连续轴运动 JSON，或配置经过验证的控制器适配器。");
    // Export the exact native, persisted plan that was inspected in simulation.
    // Its frozen source key must still match; never solve a different branch.
    auto plan=MachinePlanState(scene,Identifier(request,"setupId"),true);
    if(String(plan,"validity")!="Current")throw std::invalid_argument("机床运动计划已过期，请重新规划后导出。");
    if(String(plan,"status")!="Ready"||String(plan,"collisionStatus")!="Clear")throw std::invalid_argument("机床运动未通过碰撞与行程检查，不能导出："+String(plan,"reason"));
    const auto frames=plan.at("frames").To<Array>();const auto axes=post.at("axes").To<Array>();Array instructions;
    for(size_t i=0;i<frames.size();++i){const auto f=frames[i].To<Map>();const auto q=f.at("joints").To<Array>();Map values;
        for(const auto& value:axes){const auto a=value.To<Map>();const auto id=String(a,"jointId");const auto it=std::find_if(machine.Nodes.begin(),machine.Nodes.end(),[&](const auto& n){return n.ID==id;});
            const auto index=static_cast<size_t>(it-machine.Nodes.begin());double position=q[index].To<double>();if(it->Type!="prismatic")position*=180/std::numbers::pi;
            position=position*FiniteNumber(a.at("sign"),"axis sign")+FiniteNumber(a.at("offset"),"axis offset");values[String(a,"word")]=position;
        }
        const auto role=String(f,"role");
        // The first target is approached with the process off. At operation
        // boundaries the previous (source) point determines segment engagement.
        const bool engaged=i>0&&frames[i-1].To<Map>().at("laser").To<bool>();
        const double seconds=FiniteNumber(f.at("durationSeconds"),"segment duration");
        Map instruction{{"index",static_cast<UInt>(i)},{"axes",values},{"joints",f.at("joints")},{"durationSeconds",seconds},{"process",engaged},{"processAfter",f.at("laser")},{"role",role}};
        if(f.contains("motion")){
            instruction["motion"]=f.at("motion");Map mapped;const auto coefficients=f.at("motion").To<Map>().at("coefficients").To<Array>();
            for(const auto& value:axes){const auto a=value.To<Map>();const auto it=std::find_if(machine.Nodes.begin(),machine.Nodes.end(),[&](const auto& n){return n.ID==String(a,"jointId");});const auto index=static_cast<size_t>(it-machine.Nodes.begin());auto row=coefficients.at(index).To<Array>();const double scale=(it->Type=="prismatic"?1:180/std::numbers::pi)*FiniteNumber(a.at("sign"),"axis sign");for(auto& coefficient:row)coefficient=coefficient.To<double>()*scale;row[0]=row[0].To<double>()+FiniteNumber(a.at("offset"),"axis offset");mapped[String(a,"word")]=row;}instruction["axisCoefficients"]=mapped;
        }instructions.emplace_back(std::move(instruction));
        if(seconds<0)throw std::invalid_argument("无效的运动时间。");
    }
    Map document{{"format",Text("ICAX-JOINT-MOTION-2")},{"units",Text("mapped axes: mm,degree,second; native joints: mm,radian,second")},{"interpolation",Text("QuinticNormalized")},{"machine",snapshot},{"postprocessor",post},{"instructions",instructions},{"initialJoints",frames.front().To<Map>().at("joints")},{"machineReady",false},{"qualification",Text("ContinuousPolynomialConfiguredLimitsChecked; unspecified limits unbounded; sampled FK/feed; controller and process calibration required")},{"timingQualification",plan.at("timingQualification")},{"axisLimits",plan.contains("axisLimits")?plan.at("axisLimits"):Variant(Array{})}};
    const auto content=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(document);
    Map result{{"content",content},{"motion",document},{"format",post.at("format")},{"extension",post.at("extension")},{"instructionCount",static_cast<UInt>(frames.size()-1)},{"machineReady",false},{"modelKey",snapshot.at("modelKey")}};
    if(request.contains("destination")){const auto destination=String(request,"destination");WriteMachineProgram(destination,content);result["path"]=destination;result.erase("content");}
    return result;
}
Map ExportMachineProgramProjection(Scene& scene,const Map& request){
    const auto result=ExportMachineProgram(scene,request);Map metadata;
    for(const auto& [key,value]:result)if(key!="motion"&&key!="content")metadata.emplace(key,value);
    if(request.contains("destination"))return metadata;
    const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    const auto reference=setup->GetMachineMotionPlanData();
    const auto ref=PublishMachineWireResource(scene,setupID,"program",iCAX::Data::VariantSerializer::Serialize(EngineeringIdentity(setup->GetMachineMotionPlanData())),result,{});
    metadata["programResourceRef"]=WireRef(StoredRef(ref));return metadata;
}
