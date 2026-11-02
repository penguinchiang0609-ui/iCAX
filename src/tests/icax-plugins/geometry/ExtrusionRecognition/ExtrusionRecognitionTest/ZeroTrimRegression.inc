// Public Classify regression for certified one-dimensional trim domains.
// Real inputs are the fresh neutral checkpoints in mesh-diagnostic/*/cache.
int ZeroTrimRegression(const fs::path& directory,const fs::path& realCacheRoot)
{
    fs::create_directories(directory);VariantArray checks,cases;
    const auto save=[&](const char* status="running"){Save(directory/"checks.json",{{"status",std::string(status)},{"checks",checks},{"cases",cases}});};
    const auto check=[&](const bool passed,const std::string& name){checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});save();if(!passed)throw std::runtime_error(name);};
    const auto read=[](const fs::path& file){std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Missing neutral checkpoint: "+Utf8(file));
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});return G::Persistence::Deserialize(bytes);};
    const auto tag=[](const G::BRepFace& face,const std::string& value){return std::find(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),value)!=face.Metadata.Tags.end();};
    const auto geometryOnly=[](G::BRepModel model){model.Metadata={};const auto clear=[](auto& records){for(auto& record:records)record.Metadata={};};
        clear(model.Curves2);clear(model.Curves3);clear(model.Surfaces3);clear(model.Triangulations3);clear(model.Vertices);clear(model.Edges);clear(model.Wires);clear(model.Faces);clear(model.Shells);clear(model.Solids);clear(model.CompSolids);clear(model.Compounds);
        return G::Persistence::Serialize(model);};
    try
    {
        R::CExtrudeRecognizesService service;
        const auto nominal=O::ConvertOpenCascadeShapeToBRep(BRepPrimAPI_MakeBox(gp_Pnt(-2,-10,-10),4,20,20).Shape(),"","",0.001);
        const auto stock=service.ExtractSectionWires(nominal);check(stock.bSuccess,"independent exact rectangular stock section is available");
        const auto run=[&](const std::string& name,const G::BRepModel& model,const R::SSectionWiresResult& section){
            const auto before=G::Persistence::Serialize(model);R::SFeatureToolpathOptions options;options.bSingleExtrudedStock=true;
            const auto result=service.ClassifySectionFaces(model,section,options);
            cases.emplace_back(ObjectMap{{"name",name},{"success",result.bSuccess},{"splitFaces",int(result.SplitGeometry.Faces.size())},{"machiningFaces",int(result.MachiningGeometry.Faces.size())},{"nonMachiningFaces",int(result.NonMachiningGeometry.Faces.size())},{"diagnostics",Strings(result.Diagnostics)}});save();
            check(result.bSuccess,name+" public Classify succeeds");
            check(before==G::Persistence::Serialize(model),name+" input neutral CAD is byte-identical after classification");
            std::set<std::uint64_t> partition(result.MachiningFaceIds.begin(),result.MachiningFaceIds.end());
            const auto firstCount=partition.size();partition.insert(result.NonMachiningFaceIds.begin(),result.NonMachiningFaceIds.end());
            check(firstCount==result.MachiningFaceIds.size()&&partition.size()==result.SplitGeometry.Faces.size()
                &&result.MachiningFaceIds.size()+result.NonMachiningFaceIds.size()==partition.size(),name+" complete split faces form a disjoint exhaustive partition");
            return result;
        };
        std::vector<std::pair<std::string,TopoDS_Face>> positive;
        positive.emplace_back("positive-micro-rectangle-1e-16",BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,1e-8,0,1e-8).Face());
        TColgp_Array1OfPnt upper(1,3),lower(1,3);upper(1)={0,0,0};upper(2)={.5,.01,0};upper(3)={1,0,0};lower(1)={0,0,0};lower(2)={.5,-.01,0};lower(3)={1,0,0};
        auto a=BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(upper)).Edge(),b=BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(lower)).Edge();b.Reverse();BRepBuilderAPI_MakeWire lens;lens.Add(a);lens.Add(b);
        positive.emplace_back("positive-nonlinear-two-edge-lens-height-0.01",BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),lens.Wire(),true).Face());
        positive.emplace_back("positive-full-period-cylinder",BRepBuilderAPI_MakeFace(new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(1,0,0)),1),0,6.283185307179586,0,1,1e-7).Face());
        for(const auto& [name,face]:positive)
        {
            check(BRepCheck_Analyzer(face,true).IsValid(),name+" source face is valid");
            const auto source=O::ConvertOpenCascadeShapeToBRep(face,"","",0.001);const double inputArea=Area(Build(source));
            const auto result=run(name,source,stock);
            check(result.SplitGeometry.Faces.size()==1&&result.MachiningGeometry.Faces.size()==1&&result.NonMachiningGeometry.Faces.empty(),name+" retains the complete positive face as machining");
            const double outputArea=Area(Build(result.MachiningGeometry));
            auto areaEvidence=cases.back().To<ObjectMap>();areaEvidence["inputAreaMm2"]=inputArea;areaEvidence["machiningAreaMm2"]=outputArea;
            areaEvidence["areaRelativeError"]=inputArea>0?std::abs(inputArea-outputArea)/inputArea:0.0;
            if(name=="positive-nonlinear-two-edge-lens-height-0.01")areaEvidence["analyticExpectedAreaMm2"]=.02/3.0;
            cases.back()=iCAX::Data::Variant(areaEvidence);save();
            check(inputArea>0&&outputArea>0&&std::abs(inputArea-outputArea)<=inputArea*1e-8,name+" preserves its full positive area without an absolute area cutoff");
            check(std::none_of(result.SplitGeometry.Faces.begin(),result.SplitGeometry.Faces.end(),[&](const auto& f){return tag(f,"classify.zeroAreaTrim=true");}),name+" has no zero-trim certificate");
        }
        struct RealCase final{const char* Name;int Split,Machining,NonMachining;std::vector<std::uint64_t> ZeroFaces;const char* Proof;};
        const std::vector<RealCase> realCases{
            {"ab52-a",51,33,18,{9},"two-affine-pcurve-uses-retrace-the-same-native-vertex-pair-within-parameter-roundoff"},
            {"ab52-b",51,33,18,{9},"two-affine-pcurve-uses-retrace-the-same-native-vertex-pair-within-parameter-roundoff"},
            {"sleeve",14,10,4,{7,9},"exactly-collapsed-nonperiodic-U-trim-domain-with-constant-control-coordinate"}};
        for(const auto& item:realCases)
        {
            const auto cache=realCacheRoot/item.Name/"cache";const auto source=read(cache/"aligned.icaxbrep");const auto baseline=read(cache/"split.icaxbrep");
            const auto sourceBefore=G::Persistence::Serialize(source),baselineBefore=G::Persistence::Serialize(baseline);
            const auto section=service.ExtractSectionWires(source);check(section.bSuccess,std::string(item.Name)+" full exact section is recovered from the original aligned CAD");
            const auto result=run(item.Name,source,section);
            check(int(result.SplitGeometry.Faces.size())==item.Split&&int(result.MachiningGeometry.Faces.size())==item.Machining
                &&int(result.NonMachiningGeometry.Faces.size())==item.NonMachining,std::string(item.Name)+" expected complete split and machining/non-machining counts are retained");
            for(const auto id:item.ZeroFaces)
            {
                const auto find=[&](const G::BRepModel& model)->const G::BRepFace*{const auto it=std::find_if(model.Faces.begin(),model.Faces.end(),[&](const auto& f){return f.Id==id;});return it==model.Faces.end()?nullptr:&*it;};
                const auto splitFace=find(result.SplitGeometry),nonMachiningFace=find(result.NonMachiningGeometry);
                check(splitFace&&nonMachiningFace&&!find(result.MachiningGeometry),std::string(item.Name)+" output face "+std::to_string(id)+" remains in Split and is present only in NonMachining");
                check(tag(*splitFace,"classify.zeroAreaTrim=true")&&tag(*splitFace,std::string("classify.zeroAreaProof=")+item.Proof)
                    &&tag(*splitFace,"classify.zeroAreaSourceFaceId="+std::to_string(id)),std::string(item.Name)+" zero domain proof and exact output face identity are recorded");
                const auto expected=R::detail::SectionFaceSubset(baseline,{id}),actual=R::detail::SectionFaceSubset(result.SplitGeometry,{id});
                check(geometryOnly(expected)==geometryOnly(actual),std::string(item.Name)+" certified zero face retains exact source split support, pcurves, 3D curves and topology");
            }
            check(sourceBefore==G::Persistence::Serialize(source)&&baselineBefore==G::Persistence::Serialize(baseline),std::string(item.Name)+" source and baseline neutral geometries remain byte-identical");
            const auto data=G::Persistence::Serialize(result.NonMachiningGeometry);std::ofstream output(directory/(std::string(item.Name)+"-non-machining.icaxbrep"),std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),data.size());
        }
        save("passed");return 0;
    }
    catch(const std::exception& error){Save(directory/"checks.json",{{"status",std::string("failed")},{"error",std::string(error.what())},{"checks",checks},{"cases",cases}});return 1;}
}
