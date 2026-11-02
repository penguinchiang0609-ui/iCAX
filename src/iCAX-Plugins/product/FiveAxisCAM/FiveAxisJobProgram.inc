#include "FiveAxisProgramCurves.inc"
// A controller-neutral tool-location program. M00 separates physical
// setups; no motion is invented between them. Machine postprocessing is separate.
Map ExportSetupProgram(Scene& scene,const Map& request) {
    CutFields(request,{"expected","destination","coordinateFrame","referencePoint","curveMode"});
    CheckExpected(*Runtime(scene),request);SetupIdle(scene);
    const auto frame=request.contains("coordinateFrame")?String(request,"coordinateFrame"):Text("Machine");
    const auto reference=request.contains("referencePoint")?String(request,"referencePoint"):Text("ToolTip");
    const bool cutPoints=frame=="Setup"&&reference=="CutPoint";
    if(!cutPoints&&!(frame=="Machine"&&reference=="ToolTip"))throw std::invalid_argument("CAM_JOB_OUTPUT_REFERENCE_INVALID");
    const auto curveMode=request.contains("curveMode")?String(request,"curveMode"):Text("PreferExact");
    if(curveMode!="PreferExact"&&curveMode!="Sampled")throw std::invalid_argument("CAM_JOB_OUTPUT_CURVE_MODE_INVALID");
    const auto state=ReadState(scene);const auto placements=state.at("placements").To<Array>(),workpieces=state.at("workpieces").To<Array>();
    const auto comment=[](Text text){for(auto& c:text)if(static_cast<unsigned char>(c)<32)c=' ';return text;};
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(12);
    out<<"$$ ICAX-CL "<<(cutPoints?2:1)<<"\n$$ TOOL LOCATIONS; NOT MACHINE NC; POSTPROCESSING REQUIRED\nUNITS/MM\n";
    if(cutPoints)out<<"$$ CUTTING POINTS AND UNIT TOOL AXES; XYZ/IJK IN EACH SETUP FRAME\nREFERENCE/CUTPOINT\n$$ IJK IS MODAL; THREE-VALUE FROM/GOTO RETAINS THE PREVIOUS IJK\n$$ CURVE/JSON DEFINES EXACT MOTION TO THE FOLLOWING GOTO; DO NOT REPLACE IT BY A LINE\n$$ SAMPLED SEGMENTS USE CAM LINEAR POSITION / MINIMUM-ROTATION AXIS INTERPOLATION\n$$ NO RAPID OR INTER-PATH CONNECTING MOTION\n";
    size_t count=0;
    for(const auto& value:state.at("setups").To<Array>()){
        const auto setup=value.To<Map>();const auto setupID=String(setup,"id");
        std::map<Text,Map> members;bool enabled=false;
        for(const auto& pv:placements){const auto p=pv.To<Map>();if(String(p,"setupId")!=setupID)continue;
            members.emplace(String(p,"id"),p);
            for(const auto& wv:workpieces){const auto w=wv.To<Map>();if(String(w,"id")==String(p,"workpieceId")&&w.at("enabled").To<bool>())enabled=true;}
        }
        if(!enabled||!setup.at("enabled").To<bool>())continue;
        const auto fail=[&](const char* reason){throw std::invalid_argument(Text("CAM_JOB_OUTPUT_NOT_READY: ")+String(setup,"name")+" - "+reason);};
        const auto geometry=setup.at("generatedGeometry").To<Map>();
        if(String(geometry,"status")!="Current"||!geometry.contains("operations"))fail("刀路缺失或已过期");
        const auto operations=geometry.at("operations").To<Array>();
        if(operations.empty())fail("没有可输出刀路");
        if(cutPoints){
            if(count)out<<"M00\n$$ RECLAMP AND ESTABLISH NEXT SETUP COORDINATES\n";
            ++count;out<<"$$ SETUP "<<count<<" "<<setupID<<" "<<comment(String(setup,"name"))<<"\nFRAME/SETUP\n";
            std::optional<AlignVector> modalAxis;
            const auto emitCutPoint=[&](const char* word,const AlignVector& p,const AlignVector& axis){
                out<<word<<p[0]<<","<<p[1]<<","<<p[2];const auto delta=modalAxis?AlignAdd(axis,*modalAxis,-1):AlignVector{1,1,1};
                if(!modalAxis||AlignDot(delta,delta)>1e-24){out<<","<<axis[0]<<","<<axis[1]<<","<<axis[2];modalAxis=axis;}out<<"\n";
            };
            for(size_t index=0;index<operations.size();++index){
                const auto& op=std::get<Map>(operations[index].m_Value);
                const auto member=members.find(String(op,"placementId"));if(member==members.end())fail("刀路装夹归属错误");
                const auto matrix=RigidMatrix(member->second.at("transform").To<Array>());
                const auto& parts=std::get<Array>(op.at("parts").m_Value);if(parts.empty())fail("刀路为空");
                out<<"$$ PATH "<<index+1<<" "<<String(op,"intentId")<<"\n";
                const auto path=ReadIntentPath(scene,Identifier(op,"intentId"));
                if(curveMode=="PreferExact"&&ProgramConstantAxis(scene,path,op)){
                    namespace Walk=iCAX::GeometryServices::OffsetTraversal;const auto exact=IntentExactCurve(path.Value);
                    const auto service=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();Walk::Evaluator evaluator(*service);
                    Array curves;for(size_t i=0;i<path.Value.Geometry.Segments.size();++i){const auto& segment=path.Value.Geometry.Segments[i];std::optional<iCAX::GeometryData::Direction3> direction;
                        if(const auto* offset=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&segment.Curve))if(const auto* normal=std::get_if<iCAX::GeometryData::SurfaceNormalField3>(&offset->ReferenceDirection);normal&&ProgramPlanarSurface(normal->Surface))direction=evaluator.At(exact.Segments[i],Walk::Start(exact.Segments[i])).ReferenceDirection;
                        auto record=ProgramCurveRecord(segment,matrix,direction?&*direction:nullptr);if(record.empty()){curves.clear();break;}curves.emplace_back(std::move(record));}
                    if(!curves.empty()){
                        const auto localAxis=AxisUnit(parts.front().To<Map>().at("directions").To<Array>().front());const auto axis=AlignUnit(AlignMap(matrix,localAxis,true));
                        const auto xyz=[&](const auto& sample){return AlignMap(matrix,{sample.OffsetPoint.X,sample.OffsetPoint.Y,sample.OffsetPoint.Z});};
                        emitCutPoint("FROM/",xyz(evaluator.At(exact.Segments.front(),Walk::Start(exact.Segments.front()))),axis);out<<"PROCESS/ON\n";
                        for(size_t i=0;i<curves.size();++i){out<<"CURVE/"<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(curves[i])<<"\n";emitCutPoint("GOTO/",xyz(evaluator.At(exact.Segments[i],Walk::End(exact.Segments[i]))),axis);}
                        out<<"PROCESS/OFF\n";continue;
                    }
                }
                bool first=true;AlignVector previous{};
                for(const auto& partValue:parts){
                    const auto& part=std::get<Map>(partValue.m_Value);
                    const auto& points=std::get<Array>(part.at("points").m_Value);
                    const auto& directions=std::get<Array>(part.at("directions").m_Value);
                    if(points.empty()||points.size()!=directions.size())fail("切割点或刀轴方向缺失");
                    for(size_t i=0;i<points.size();++i){
                        const auto p=AlignMap(matrix,RegistrationPoint(points[i]));
                        const auto axis=AlignUnit(AlignMap(matrix,RegistrationPoint(directions[i]),true));
                        for(const auto n:p)if(!std::isfinite(n))fail("切割点无效");
                        if(!first&&i==0){const auto delta=AlignAdd(p,previous,-1);if(AlignDot(delta,delta)>1e-8)fail("同一刀路内的切割段不连续");}
                        emitCutPoint(first?"FROM/":"GOTO/",p,axis);
                        if(first)out<<"PROCESS/ON\n";
                        first=false;previous=p;
                    }
                }
                out<<"PROCESS/OFF\n";
            }
            out<<"$$ END_SETUP\n";
            continue;
        }
        const double height=RegistrationNumber(setup.at("rapidSettings").To<Map>().at("followHeightMm"));
        if(!std::isfinite(height)||height<0)fail("跟随高度无效");
        const auto plan=setup.at("registrationPlan").To<Map>();
        if(String(plan,"status")!="Prepared"||String(plan,"machineStatus")!="Qualified"||!plan.contains("machineRegistration"))
            fail("请先保存当前装夹的三点及对应机床坐标，定位资料过期时须重新定位");
        const auto registration=plan.at("machineRegistration").To<Map>();
        if(String(registration,"sourceFrame")!="Setup"||String(registration,"targetFrame")!="Machine")fail("机床定位坐标系错误");
        const auto machine=RigidMatrix(registration.at("transform").To<Array>());
        if(count)out<<"PROCESS/OFF\nM00\n$$ RECLAMP AND ESTABLISH NEXT SETUP COORDINATES\n";
        ++count;out<<"$$ SETUP "<<count<<" "<<setupID<<" "<<comment(String(setup,"name"))<<"\nFRAME/MACHINE\n";
        bool first=true;AlignVector previous{};
        const auto emit=[&](const Map& part,const AlignMatrix& matrix,bool tcp){
            const auto points=part.at("points").To<Array>(),directions=part.at("directions").To<Array>();
            if(points.empty()||points.size()!=directions.size())fail("刀位或姿态缺失");
            for(size_t i=0;i<points.size();++i){
                auto direction=AlignUnit(AlignMap(matrix,RegistrationPoint(directions[i]),true));
                auto p=AlignMap(matrix,RegistrationPoint(points[i]));
                if(!tcp)p=AlignAdd(p,direction,height);
                p=AlignMap(machine,p);direction=AlignUnit(AlignMap(machine,direction,true));
                for(const auto n:p)if(!std::isfinite(n))fail("刀位无效");
                if(!first&&i==0){const auto delta=AlignAdd(p,previous,-1);if(AlignDot(delta,delta)>1e-8)fail("刀路或空移不连续");}
                out<<(first?"FROM/":"GOTO/")<<p[0]<<","<<p[1]<<","<<p[2]<<","<<direction[0]<<","<<direction[1]<<","<<direction[2]<<"\n";
                if(first&&!tcp)out<<"PROCESS/ON\n";
                first=false;previous=p;
            }
        };
        for(size_t index=0;index<operations.size();++index){
            const auto op=operations[index].To<Map>();const auto id=String(op,"intentId");
            const auto member=members.find(String(op,"placementId"));if(member==members.end())fail("刀路装夹归属错误");
            const auto matrix=RigidMatrix(member->second.at("transform").To<Array>());
            out<<"$$ PATH "<<index+1<<" "<<id<<"\n";
            if(!first)out<<"PROCESS/ON\n";
            const auto parts=op.at("parts").To<Array>();if(parts.empty())fail("刀路为空");
            for(const auto& part:parts)emit(part.To<Map>(),matrix,false);
            out<<"PROCESS/OFF\n";
            if(index+1<operations.size()){
                bool found=false;
                for(const auto& tv:geometry.at("transitions").To<Array>()){
                    const auto transition=tv.To<Map>();
                    if(String(transition,"from")!=id||String(transition,"to")!=String(operations[index+1].To<Map>(),"intentId"))continue;
                    if(String(transition,"geometryStatus")!="Generated"||String(transition,"frame")!="Setup"||String(transition,"referencePoint")!="ToolTip")fail("空移尚未生成");
                    out<<"RAPID\n";emit(transition,RigidMatrix(Identity()),true);found=true;break;
                }
                if(!found)fail("空移缺失");
            }
        }
        out<<"$$ END_SETUP\n";
    }
    if(!count)throw std::invalid_argument("CAM_JOB_OUTPUT_EMPTY");
    out<<"FINI\n";const auto bytes=out.str();
    Map result{{"format",Text("ICAX-CL")},{"coordinateFrame",frame},{"referencePoint",reference},{"machineReady",false},{"setupCount",static_cast<UInt>(count)},{"content",bytes}};
    if(cutPoints){result["formatVersion"]=UInt(2);result["axisMode"]=Text("Modal");result["curveMode"]=curveMode;result["collisionStatus"]=Text("RequiresPostprocessedReplay");}
    if(request.contains("destination")){
        const auto destination=String(request,"destination");
        const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(destination.data()),destination.size()));
        if(!path.is_absolute()||!std::filesystem::is_directory(path.parent_path()))throw std::invalid_argument("CAM_OUTPUT_FILE_PATH_REQUIRED");
        auto temporary=path;temporary+=L".tmp-"+std::filesystem::path(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())).wstring();
        try{
            {std::ofstream file(temporary,std::ios::binary);file.write(bytes.data(),bytes.size());file.flush();if(!file)throw std::runtime_error("CAM_OUTPUT_WRITE_FAILED");}
            {std::ifstream file(temporary,std::ios::binary);const Text actual((std::istreambuf_iterator<char>(file)),{});if(actual!=bytes)throw std::runtime_error("CAM_OUTPUT_READBACK_FAILED");}
            if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("CAM_OUTPUT_PUBLISH_FAILED");
        }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
        result["path"]=destination;
        result.erase("content");
    }
    return result;
}
