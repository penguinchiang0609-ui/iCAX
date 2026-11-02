// Focused Classify regression: existing fixed-stock interface certificates
// propagate only through exact finite overlap, retaining all remaining trim.
int PartialInternalInterfaceRegression(const fs::path& directory,const fs::path& realAligned={})
{
    fs::create_directories(directory);VariantArray checks,cases;
    const auto save=[&]{Save(directory/"checks.json",{{"checks",checks},{"cases",cases}});};
    const auto check=[&](bool passed,const std::string& name){checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});save();if(!passed)throw std::runtime_error(name);};
    R::CExtrudeRecognizesService service;
    const auto nominal=[&](double low,double high){return service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(
        BRepPrimAPI_MakeBox(gp_Pnt(-50,-10,low),100,20,high-low).Shape(),"","",0.001));};
    const auto section=nominal(-2,2);check(section.bSuccess,"independent complete rectangular nominal stock section");
    const auto rectangle=[](double low,double high,double z,bool reversed){BRepBuilderAPI_MakePolygon wire;
        wire.Add(gp_Pnt(-50,low,z));wire.Add(gp_Pnt(50,low,z));wire.Add(gp_Pnt(50,high,z));wire.Add(gp_Pnt(-50,high,z));wire.Close();
        auto face=BRepBuilderAPI_MakeFace(wire.Wire()).Face();if(reversed)face.Reverse();return face;};
    const auto fixture=[&](double low,double high,double z,bool reversed){BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        builder.Add(compound,rectangle(-4,4,0,false));builder.Add(compound,rectangle(low,high,z,reversed));return O::ConvertOpenCascadeShapeToBRep(compound,"","",0.001);};
    const auto domain=[](const G::BRepFace& face,const char* expected){return std::find(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),std::string("classify.interfaceDomain=")+expected)!=face.Metadata.Tags.end();};
    const auto run=[&](const char* name,const G::BRepModel& model,const R::SSectionWiresResult& reference,bool single){
        const auto before=G::Persistence::Serialize(model);R::SFeatureToolpathOptions options;options.bSingleExtrudedStock=single;
        const auto result=service.ClassifySectionFaces(model,reference,options);
        cases.emplace_back(ObjectMap{{"name",std::string(name)},{"success",result.bSuccess},{"machiningFaces",int(result.MachiningGeometry.Faces.size())},{"nonMachiningFaces",int(result.NonMachiningGeometry.Faces.size())},{"sourceAreaMm2",Area(Build(model))},
            {"splitAreaMm2",result.bSuccess?Area(Build(result.SplitGeometry)):0.},{"machiningAreaMm2",result.bSuccess&&!result.MachiningGeometry.Faces.empty()?Area(Build(result.MachiningGeometry)):0.},{"nonMachiningAreaMm2",result.bSuccess&&!result.NonMachiningGeometry.Faces.empty()?Area(Build(result.NonMachiningGeometry)):0.},{"diagnostics",Strings(result.Diagnostics)}});save();
        check(result.bSuccess,std::string(name)+" classification succeeds");check(before==G::Persistence::Serialize(model),std::string(name)+" source neutral CAD remains byte-identical");
        check(BRepCheck_Analyzer(Build(result.SplitGeometry),true).IsValid(),std::string(name)+" derived whole model is valid");
        check(Relative(Area(Build(model)),Area(Build(result.SplitGeometry)))<1e-8,std::string(name)+" complete source area is conserved");return result;};
    const auto positive=run("partial-opposed-interface",fixture(-5,5,0,true),section,true);
    check(std::abs(Area(Build(positive.NonMachiningGeometry))-1600)<1e-6,"both independent interface occurrences are non-machining");
    check(std::abs(Area(Build(positive.MachiningGeometry))-200)<1e-6,"all exposed remainder area remains machining");
    int internal=0,remainder=0;for(const auto& face:positive.SplitGeometry.Faces)if(domain(face,"internal")||domain(face,"machining-remainder")){
        internal+=domain(face,"internal");remainder+=domain(face,"machining-remainder");
        check(!face.Metadata.SourceId.empty(),"derived fragment retains original source face ID");
        check(std::any_of(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),[](const std::string& tag){return tag.starts_with("classify.sourceCurve3Ids=")&&tag.size()>25;}),"derived fragment retains original curve IDs");
    }
    check(internal==1&&remainder==2,"internal common face and two independent exposed domains are materialized");
    const auto narrow=run("small-positive-remainders",fixture(-4.00001,4.00001,0,true),section,true);
    check(std::abs(Area(Build(narrow.MachiningGeometry))-.002)<1e-7,"small positive machining remainders are retained");
    for(const auto& control:{std::tuple{"same-orientation",fixture(-5,5,0,false),section,true},
        std::tuple{"parallel-distinct-support",fixture(-5,5,.01,true),section,true},
        std::tuple{"edge-contact-only",fixture(4,5,0,true),section,true},
        std::tuple{"single-stock-disabled",fixture(-5,5,0,true),section,false},
        std::tuple{"material-on-one-side",fixture(-5,5,0,true),nominal(0,4),true}}){
        const auto result=run(std::get<0>(control),std::get<1>(control),std::get<2>(control),std::get<3>(control));
        check(std::none_of(result.SplitGeometry.Faces.begin(),result.SplitGeometry.Faces.end(),[&](const auto& f){return domain(f,"internal")||domain(f,"machining-remainder");}),std::string(std::get<0>(control))+" does not invent an internal partition");
    }
    if(!realAligned.empty()){
        std::ifstream input(realAligned,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});const auto source=G::Persistence::Deserialize(bytes);
        check(source.Faces.size()==342&&source.Shells.size()==1&&source.Shells[0].Closed&&source.Solids.size()==1&&BRepCheck_Analyzer(Build(source),true).IsValid(),"247 original 342-face closed solid is valid");
        const auto actualSection=service.ExtractSectionWires(source);std::size_t edgeCount=0;bool closed=true;for(const auto& wire:actualSection.Wires){edgeCount+=wire.Edges.size();closed=closed&&wire.bClosed;}
        check(actualSection.bSuccess&&actualSection.Wires.size()==4&&edgeCount==130&&closed,"247 complete section retains four closed wires and 130 exact edges");
        const auto result=run("247-real-solid",source,actualSection,true);double source243Internal=0,source243Remainder=0;
        const auto built=O::ReconstructOpenCascadeShape(result.SplitGeometry);
        VariantArray lineage;
        for(const auto& face:result.SplitGeometry.Faces)if(domain(face,"internal")||domain(face,"machining-remainder")){
            const double area=Area(built.Faces.at(face.Id));lineage.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"sourceFaceIds",face.Metadata.SourceId},{"areaMm2",area},{"tags",Strings(face.Metadata.Tags)}});
            if(face.Metadata.SourceId=="243"){if(domain(face,"internal"))source243Internal+=area;else source243Remainder+=area;}
        }
        check(std::abs(source243Internal-17429.008899739987)<1e-4,"247 source243 overlap is an explicit non-machining domain");
        check(std::abs(source243Remainder-197.2738029362481)<1e-4,"247 source243 uncovered197.2738mm2 remains machining");
        Save(directory/"247-lineage.json",{{"source",Utf8(realAligned)},{"sourceFaceCount",int(source.Faces.size())},{"splitFaceCount",int(result.SplitGeometry.Faces.size())},
            {"sourceAreaMm2",Area(Build(source))},{"splitAreaMm2",Area(Build(result.SplitGeometry))},{"machiningAreaMm2",Area(Build(result.MachiningGeometry))},{"nonMachiningAreaMm2",Area(Build(result.NonMachiningGeometry))},
            {"sourceFace243InternalAreaMm2",source243Internal},{"sourceFace243MachiningRemainderAreaMm2",source243Remainder},{"fragments",lineage},{"diagnostics",Strings(result.Diagnostics)}});
        for(const auto& entry:std::vector<std::pair<const char*,const G::BRepModel*>>{{"split",&result.SplitGeometry},{"machining",&result.MachiningGeometry},{"non-machining",&result.NonMachiningGeometry}}){const auto serialized=G::Persistence::Serialize(*entry.second);std::ofstream output(directory/(std::string("247-")+entry.first+".icaxbrep"),std::ios::binary);output.write(reinterpret_cast<const char*>(serialized.data()),serialized.size());}
    }
    checks.emplace_back(ObjectMap{{"name",std::string("all partial-interface controls passed")},{"passed",true}});save();return 0;
}
