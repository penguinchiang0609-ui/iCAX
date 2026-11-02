#include <set>
int ClassifyFaceInventory(const fs::path& source,const fs::path& directory,bool singleStock=false) {
    std::ifstream input(source,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto section=service.ExtractSectionWires(axis.AlignedGeometry);const auto before=G::Persistence::Serialize(axis.AlignedGeometry);
    R::SFeatureToolpathOptions options;options.bSingleExtrudedStock=singleStock;
    const auto classified=service.ClassifySectionFaces(axis.AlignedGeometry,section,options);
    const auto inventory=[](const G::BRepModel& geometry){
        const auto shape=Build(geometry);TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);VariantArray rows;
        for(int i=1;i<=faces.Extent();++i) {
            const auto face=TopoDS::Face(faces(i));Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
            BRepAdaptor_Surface surface(face);double u,U,v,V;BRepTools::UVBounds(face,u,U,v,V);gp_Pnt p;gp_Vec du,dv;surface.D1((u+U)/2,(v+V)/2,p,du,dv);auto n=du.Crossed(dv);if(n.Magnitude()>1e-15)n.Normalize();if(face.Orientation()==TopAbs_REVERSED)n.Reverse();
            rows.emplace_back(ObjectMap{{"index",i},{"area",Area(face)},{"surfaceType",static_cast<int>(surface.GetType())},{"edges",Count(face,TopAbs_EDGE)},
                {"bounds",VariantArray{x,y,z,X,Y,Z}},{"normal",VariantArray{n.X(),n.Y(),n.Z()}}});
        }
        return rows;
    };
    Save(directory/"inventory.json",{{"sourceFaces",inventory(axis.AlignedGeometry)},{"machiningFaces",inventory(classified.MachiningGeometry)},
        {"skinFaces",inventory(classified.NonMachiningGeometry)},{"success",classified.bSuccess},{"singleExtrudedStock",singleStock},
        {"sourceNeutralUnchanged",before==G::Persistence::Serialize(axis.AlignedGeometry)},{"diagnostics",Strings(classified.Diagnostics)}});return 0;
}
int SourceFaceAudit(const fs::path& source,int id,const fs::path& directory,const fs::path& seedFile={}) {
    fs::create_directories(directory);std::ifstream file(source,std::ios::binary);const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(data));const auto built=O::BuildOpenCascadeShape(axis.AlignedGeometry);
    const auto face=built.Faces.at(id);ObjectMap report{{"faceId",id},{"stats",HealFaceStats(face)}};VariantArray wires;
    for(TopExp_Explorer wi(face,TopAbs_WIRE);wi.More();wi.Next()){VariantArray edges;for(BRepTools_WireExplorer e(TopoDS::Wire(wi.Current()),face);e.More();e.Next()){double a,b;const auto pc=BRep_Tool::CurveOnSurface(e.Current(),face,a,b);edges.emplace_back(ObjectMap{{"first",a},{"last",b},{"pcurve",std::string(pc->DynamicType()->Name())}});}wires.emplace_back(ObjectMap{{"outer",wi.Current().IsSame(BRepTools::OuterWire(face))},{"edges",edges}});}
    report["wires"]=wires;
    if(!seedFile.empty()) {
        TopoDS_Shape shape;if(!BinTools::Read(shape,Utf8(seedFile).c_str()))throw std::runtime_error("Cannot read candidate checkpoint");
        const auto wire=TopoDS::Wire(TopExp_Explorer(shape,TopAbs_WIRE).Current());Bnd_Box bounds;BRepBndLib::AddOptimal(wire,bounds,false,false);
        double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);gp_Trsf flatten;flatten.SetValues(0,1,0,0,0,0,1,0,1,0,0,-(x+X)/2);
        const auto flat=TopoDS::Wire(BRepBuilderAPI_Transform(wire,flatten,true).Shape());VariantArray cases;
        Bnd_Box bodyBounds;BRepBndLib::AddOptimal(built.Shape,bodyBounds,false,false);double bx,by,bz,bX,bY,bZ;bodyBounds.Get(bx,by,bz,bX,bY,bZ);
        for(const double tolerance:{0.001,Precision::Confusion()}) {
            auto boundary=flat;BRepBuilderAPI_MakeFace cell(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),boundary,false);
            const auto infinite=BRepTopAdaptor_FClass2d(cell.Face(),tolerance).PerformInfinitePoint();
            if(infinite==TopAbs_IN){boundary.Reverse();cell=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),boundary,false);}
            gp_Trsf toYZ;toYZ.SetValues(0,0,1,bx-0.001,1,0,0,0,0,1,0,0);const auto base=BRepBuilderAPI_Transform(cell.Face(),toYZ,true).Shape();
            const auto prism=BRepPrimAPI_MakePrism(base,gp_Vec(bX-bx+0.002,0,0)).Shape();GProp_GProps mass;BRepGProp::VolumeProperties(prism,mass);
            BRepAlgoAPI_Common common(prism,face);common.Build();
            BRepAlgoAPI_Common orientedCommon(prism,face.Oriented(TopAbs_FORWARD));orientedCommon.Build();
            BRepAlgoAPI_Common faceCommon(base,face.Oriented(TopAbs_FORWARD));faceCommon.Build();
            cases.emplace_back(ObjectMap{{"tolerance",tolerance},{"sourceOrientation",static_cast<int>(face.Orientation())},{"originalInfinite",static_cast<int>(infinite)},{"cellArea",Area(cell.Face())},{"cellValid",BRepCheck_Analyzer(cell.Face()).IsValid()},{"volume",mass.Mass()},{"commonArea",Area(common.Shape())},{"commonValid",BRepCheck_Analyzer(common.Shape()).IsValid()},{"commonFaces",Count(common.Shape(),TopAbs_FACE)},{"forwardCommonArea",Area(orientedCommon.Shape())},{"forwardCommonValid",BRepCheck_Analyzer(orientedCommon.Shape()).IsValid()}});
            Step(FlatArtifact(common.Shape()),directory/(tolerance>1e-6?"work-common.step":"native-common.step"));
        }
        report["candidateChecks"]=cases;
    }
    Save(directory/"face.json",report);Step(FlatArtifact(face),directory/"source-face.step");return 0;
}
ObjectMap AuditExportMismatch(const TopoDS_Shape& source,const TopoDS_Shape& presentation,
    const TopoDS_Shape& restored,const fs::path& directory,bool rational) {
    TopTools_IndexedMapOfShape a,b,c;TopExp::MapShapes(source,TopAbs_FACE,a);
    TopExp::MapShapes(presentation,TopAbs_FACE,b);TopExp::MapShapes(restored,TopAbs_FACE,c);
    VariantArray cases;fs::create_directories(directory);
    for(int i=1;i<=a.Extent()&&i<=b.Extent()&&i<=c.Extent();++i) {
        const auto first=TopoDS::Face(a(i)),prepared=TopoDS::Face(b(i)),last=TopoDS::Face(c(i));
        if(Relative(Area(first),Area(last))<1e-6)continue;
        const auto prefix=std::string(rational?"rational":"ordinary")+"-face-"+std::to_string(i);
        std::string junctionRejection;const auto junction=ExplicitTrimJunctionFace(first,false,&junctionRejection);
        ObjectMap row{{"index",i},{"junctionAvailable",!junction.IsNull()},{"junctionRejection",junctionRejection},{"original",HealFaceStats(first)},{"presentation",HealFaceStats(prepared)},
            {"read",HealFaceStats(last)},{"sourceToPresentation",HealFacePair(first,prepared)},
            {"presentationToRead",HealFacePair(prepared,last)}};
        cases.emplace_back(row);
        Step(first,directory/(prefix+"-source.step"));Step(prepared,directory/(prefix+"-prepared.step"));
        Step(last,directory/(prefix+"-read.step"));
        Save(directory/(std::string(rational?"rational":"ordinary")+".json"),{{"cases",cases}});
    }
    return {{"cases",cases},{"sameNativeExecution",true}};
}

int MaterialCavityAudit(const fs::path& source,const fs::path& directory) {
    fs::create_directories(directory);std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto built=O::BuildOpenCascadeShape(axis.AlignedGeometry);
    std::vector<std::pair<std::uint64_t,TopoDS_Face>> faces;VariantArray sources;
    for(const auto& f:axis.AlignedGeometry.Faces) {
        const auto face=built.Faces.at(f.Id);faces.emplace_back(f.Id,face);
        ObjectMap row{{"faceId",std::to_string(f.Id)},{"wires",Count(face,TopAbs_WIRE)},
            {"type",static_cast<int>(BRepAdaptor_Surface(face).GetType())}};
        if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Plane) {
            const auto nx=BRepAdaptor_Surface(face).Plane().Axis().Direction().X();row["normalX"]=nx;
            if(std::abs(nx)>=std::cos(0.001)) {
                int n=0;for(TopExp_Explorer wi(face,TopAbs_WIRE);wi.More();wi.Next())
                    if(!wi.Current().IsSame(BRepTools::OuterWire(face))) {
                        const auto name="source-inner-face-"+std::to_string(f.Id)+"-"+std::to_string(++n);
                        BinTools::Write(wi.Current(),Utf8(directory/(name+".occ-cache")).c_str());
                        Step(wi.Current(),directory/(name+".step"));
                        const auto data=G::Persistence::Serialize(O::ConvertOpenCascadeShapeToBRep(wi.Current(),"","",0.001));
                        std::ofstream cache(directory/(name+".icaxbrep"),std::ios::binary);cache.write(reinterpret_cast<const char*>(data.data()),data.size());
                    }
            }
        }
        if(BRepAdaptor_Surface(face).GetType()==GeomAbs_SurfaceOfExtrusion) {
            const auto d=BRepAdaptor_Surface(face).Direction();row["direction"]=VariantArray{d.X(),d.Y(),d.Z()};
        }
        sources.emplace_back(row);
    }
    std::ofstream trace(directory/"trace.log");const auto start=Clock::now();
    const auto progress=[&](const std::string& s){trace<<std::chrono::duration<double>(Clock::now()-start).count()<<" "<<s<<std::endl;};
    std::vector<std::string> diagnostics;
    Save(directory/"inventory.json",{{"faces",sources}});
    const auto projection=R::section_detail::UniteMaterialProjections(faces,0.001,progress,diagnostics);
    VariantArray regions;
    if(!projection.IsNull()) {
        Step(FlatArtifact(projection),directory/"material-projection.step");
        for(TopExp_Explorer fi(projection,TopAbs_FACE);fi.More();fi.Next()) {
            const auto face=TopoDS::Face(fi.Current());VariantArray loops;
            for(TopExp_Explorer wi(face,TopAbs_WIRE);wi.More();wi.Next()) {
                auto wire=TopoDS::Wire(wi.Current());BRepBuilderAPI_MakeFace finite(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),wire,false);
                if(BRepTopAdaptor_FClass2d(finite.Face(),Precision::Confusion()).PerformInfinitePoint()==TopAbs_IN) {
                    wire.Reverse();finite=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),wire,false);
                }
                const bool outer=wi.Current().IsSame(BRepTools::OuterWire(face));
                ObjectMap row{{"outer",outer},{"area",O::integration_detail::RefinedArea(finite.Face())},
                    {"edges",Count(wire,TopAbs_EDGE)},{"valid",BRepCheck_Analyzer(finite.Face()).IsValid()}};
                if(!outer) {
                    R::section_detail::SExactCandidateClassifier check(finite.Face(),0.001);bool found=false;
                    for(const auto& e:axis.AlignedGeometry.Edges) {
                        if(found||e.Degenerated)continue;double a,b;const auto c=BRep_Tool::Curve(built.Edges.at(e.Id),a,b);if(c.IsNull())continue;
                        for(int k=0;k<=64;++k) {const auto p=c->Value(a+(b-a)*k/64.0);
                            if(check.Classify({p.Y(),p.Z()})==R::section_detail::EPointRelation::Inside) {
                                row["interiorSourceEdge"]=std::to_string(e.Id);row["interiorPoint"]=VariantArray{p.Y(),p.Z()};found=true;break;
                            }
                        }
                    }
                }
                loops.emplace_back(row);
            }
            regions.emplace_back(ObjectMap{{"loops",loops}});
        }
    }
    Save(directory/"material.json",{{"regions",regions},{"diagnostics",Strings(diagnostics)},
        {"seconds",std::chrono::duration<double>(Clock::now()-start).count()}});return 0;
}

int ClassifyExportAudit(const fs::path& source,const std::string& selection,const fs::path& directory) {
    fs::create_directories(directory);std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto geometry=G::Persistence::Deserialize(bytes);
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(geometry);
    const auto section=service.ExtractSectionWires(axis.AlignedGeometry);
    if(!section.bSuccess)throw std::runtime_error("Cannot audit classification without valid section");
    const auto classification=service.ClassifySectionFaces(axis.AlignedGeometry,section);
    if(!classification.bSuccess)throw std::runtime_error("Classification service failed during export audit");
    const auto body=Build(classification.MachiningGeometry);
    TopTools_IndexedMapOfShape originals;TopExp::MapShapes(body,TopAbs_FACE,originals);
    std::vector<int> selected;std::istringstream indices(selection);std::string word;
    while(std::getline(indices,word,','))selected.push_back(std::stoi(word));
    VariantArray cases;
    for(bool rational:{false,true}) {
        const auto prepared=FlatArtifact(StepBoundaryCopy(body,false,rational));
        TopTools_IndexedMapOfShape presentations;TopExp::MapShapes(prepared,TopAbs_FACE,presentations);
        for(int index:selected) {
            if(index<1||index>originals.Extent()||index>presentations.Extent())throw std::runtime_error("Audit face index is out of range");
            const auto original=TopoDS::Face(originals(index)),presentation=TopoDS::Face(presentations(index));
            const auto prefix=std::string(rational?"rational":"ordinary")+"-face-"+std::to_string(index);
            Step(original,directory/(prefix+"-source.step"));
            const auto file=directory/(prefix+"-export.step");Step(presentation,file);
            STEPControl_Reader reader;
            if(reader.ReadFile(Utf8(file).c_str())!=IFSelect_RetDone||!reader.TransferRoots())throw std::runtime_error("Cannot read audited STEP face");
            TopExp_Explorer it(reader.OneShape(),TopAbs_FACE);if(!it.More())throw std::runtime_error("Audited STEP has no face");
            const auto restored=TopoDS::Face(it.Current());
            cases.emplace_back(ObjectMap{{"index",index},{"encoding",std::string(rational?"rational":"ordinary")},
                {"original",HealFaceStats(original)},{"presentation",HealFaceStats(presentation)},
                {"read",HealFaceStats(restored)},{"sourceToPresentation",HealFacePair(original,presentation)},
                {"presentationToRead",HealFacePair(presentation,restored)}});
            Save(directory/"audit.json",{{"cases",cases},{"classificationDiagnostics",Strings(classification.Diagnostics)}});
        }
    }
    return 0;
}

int ClassifyTopologyAudit(const fs::path& source,const fs::path& directory) {
    fs::create_directories(directory);std::ifstream input(source,std::ios::binary);
    std::ofstream trace(directory/"trace.log");const auto began=Clock::now();
    const auto progress=[&](const std::string& message){trace<<std::chrono::duration<double>(Clock::now()-began).count()<<" "<<message<<std::endl;};
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    progress("recognize cached healed geometry");R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    progress("extract section");const auto section=service.ExtractSectionWires(axis.AlignedGeometry);
    progress("classify section faces");R::SFeatureToolpathOptions options;options.Progress=progress;
    const auto classification=service.ClassifySectionFaces(axis.AlignedGeometry,section,options);
    progress("classification returned; verify topology and area");
    ObjectMap report{{"source",ShapeValidity(Build(axis.AlignedGeometry))},{"serviceSuccess",classification.bSuccess},
        {"diagnostics",Strings(classification.Diagnostics)}};
    const auto areaAccounting=[](const TopoDS_Shape& shape) {
        TopTools_IndexedMapOfShape unique;TopExp::MapShapes(shape,TopAbs_FACE,unique);
        double summed=0,refined=0,occurrences=0;int count=0;VariantArray repeated,nonManifold;std::map<int,int> seen;
        TopTools_IndexedMapOfShape allEdges;TopExp::MapShapes(shape,TopAbs_EDGE,allEdges);std::map<int,std::set<int>> adjacent;
        for(int i=1;i<=unique.Extent();++i)for(TopExp_Explorer it(unique(i),TopAbs_EDGE);it.More();it.Next())adjacent[allEdges.FindIndex(it.Current())].insert(i);
        for(const auto& [edge,faces]:adjacent)if(faces.size()>2){VariantArray ids;for(const auto face:faces)ids.emplace_back(face);nonManifold.emplace_back(ObjectMap{{"edge",edge},{"faces",ids}});}
        for(int i=1;i<=unique.Extent();++i){summed+=Area(unique(i));refined+=O::integration_detail::RefinedArea(TopoDS::Face(unique(i)));}
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()){occurrences+=Area(it.Current());++count;const int id=unique.FindIndex(it.Current());if(++seen[id]>1)repeated.emplace_back(ObjectMap{{"face",id},{"area",Area(it.Current())},{"orientation",static_cast<int>(it.Current().Orientation())}});}
        return ObjectMap{{"rootArea",Area(shape)},{"uniqueFaceArea",summed},{"refinedUniqueFaceArea",refined},{"occurrenceArea",occurrences},{"faceOccurrences",count},{"uniqueFaces",unique.Extent()},{"repeated",repeated},{"nonManifoldEdges",nonManifold}};
    };
    report["sourceAreaAccounting"]=areaAccounting(Build(axis.AlignedGeometry));
    for(const auto& item:std::vector<std::pair<std::string,TopoDS_Shape>>{
        {"split",Build(classification.SplitGeometry)},{"machining",Build(classification.MachiningGeometry)},
        {"skin",Build(classification.NonMachiningGeometry)}}) {
        if(item.second.IsNull())continue;
        report[item.first]=ShapeValidity(item.second);
        report[item.first+"AreaAccounting"]=areaAccounting(item.second);
        TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(item.second,TopAbs_FACE,faces);TopExp::MapShapes(item.second,TopAbs_EDGE,edges);
        BRepCheck_Analyzer check(item.second,true);VariantArray badFaces;
        for(int i=1;i<=faces.Extent();++i)if(!check.IsValid(faces(i))) {
            VariantArray edgeIds;for(TopExp_Explorer it(faces(i),TopAbs_EDGE);it.More();it.Next())edgeIds.emplace_back(edges.FindIndex(it.Current()));
            const auto face=TopoDS::Face(faces(i));
            VariantArray intersections;
            for(TopExp_Explorer wi(face,TopAbs_WIRE);wi.More();wi.Next()) {
                ShapeAnalysis_Wire analysis(TopoDS::Wire(wi.Current()),face,0.001);std::vector<int> ordered;
                for(BRepTools_WireExplorer e(TopoDS::Wire(wi.Current()),face);e.More();e.Next())ordered.push_back(edges.FindIndex(e.Current()));
                for(int a=1;a<=static_cast<int>(ordered.size());++a)for(int b=a+1;b<=static_cast<int>(ordered.size());++b)
                    if(analysis.CheckIntersectingEdges(a,b))intersections.emplace_back(ObjectMap{{"edgeA",ordered[a-1]},{"edgeB",ordered[b-1]}});
            }
            badFaces.emplace_back(ObjectMap{{"faceIndex",i},{"edgeIndices",edgeIds},{"intersections",intersections},{"stats",HealFaceStats(face)}});
            Step(face,directory/(item.first+"-face-"+std::to_string(i)+".step"));
        }
        report[item.first+"InvalidFaces"]=badFaces;Save(directory/"topology.json",report);
    }
    return 0;
}
