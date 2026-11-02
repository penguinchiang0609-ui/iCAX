TopoDS_Shape TrimBoundaryGeometry(const TopoDS_Face& face) {
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        const auto source=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(source))continue;
        double first,last;const auto pc=BRep_Tool::CurveOnSurface(source,face,first,last);
        if(pc.IsNull())throw std::runtime_error("Missing trim pcurve");
        auto edge=BRepBuilderAPI_MakeEdge(pc,BRep_Tool::Surface(face),first,last).Edge();
        if(!BRepLib::BuildCurve3d(edge,1e-8,GeomAbs_C1))throw std::runtime_error("Cannot evaluate trim geometry");
        builder.Add(result,edge);
    }
    return result;
}

ObjectMap HealFaceStats(const TopoDS_Face& face) {
    VariantArray integrals,edges;
    for(double eps:{1e-5,1e-7,1e-9,1e-11}) {
        GProp_GProps p;const auto error=BRepGProp::SurfaceProperties(face,p,eps);
        integrals.emplace_back(ObjectMap{{"eps",eps},{"area",p.Mass()},{"estimatedError",error}});
    }
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    ObjectMap result{{"occAreas",integrals},{"uv",VariantArray{u0,u1,v0,v1}},
        {"surface",std::string(BRep_Tool::Surface(face)->DynamicType()->Name())},
        {"valid",BRepCheck_Analyzer(face,true).IsValid()},{"wires",Count(face,TopAbs_WIRE)}};
    try{result["greenArea"]=O::integration_detail::RefinedArea(face);}
    catch(const Standard_Failure& e){result["greenError"]=std::string(e.GetMessageString());}
    catch(const std::exception& e){result["greenError"]=std::string(e.what());}
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        const auto edge=TopoDS::Edge(it.Current());double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,face,a,b);
        double gap=0;
        if(!BRep_Tool::Degenerated(edge)&&!pc.IsNull()) {
            double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);
            if(!curve.IsNull())for(int i=0;i<=64;++i){const auto uv=pc->Value(a+(b-a)*i/64.);
                gap=std::max(gap,curve->Value(first+(last-first)*i/64.).Distance(BRep_Tool::Surface(face)->Value(uv.X(),uv.Y())));}
        }
        ObjectMap row{{"orientation",static_cast<int>(edge.Orientation())},{"range",VariantArray{a,b}},
            {"sameParameter",BRep_Tool::SameParameter(edge)},{"tolerance",BRep_Tool::Tolerance(edge)},
            {"parameterMatchedDeviation",gap}};
        if(!pc.IsNull()){const auto p=pc->Value(a),q=pc->Value(b);
            row["uvStart"]=VariantArray{p.X(),p.Y()};row["uvEnd"]=VariantArray{q.X(),q.Y()};}
        edges.emplace_back(row);
    }
    double wireGap=0,vertexError=0;VariantArray gaps;
    for(TopExp_Explorer wi(face,TopAbs_WIRE);wi.More();wi.Next()) {
        std::vector<std::pair<gp_Pnt,gp_Pnt>> ends;
        for(BRepTools_WireExplorer it(TopoDS::Wire(wi.Current()),face);it.More();it.Next()) {
            const auto edge=it.Current();double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,face,a,b);
            if(pc.IsNull())continue;const auto s=pc->Value(a),t=pc->Value(b);
            auto p=BRep_Tool::Surface(face)->Value(s.X(),s.Y()),q=BRep_Tool::Surface(face)->Value(t.X(),t.Y());
            if(edge.Orientation()==TopAbs_REVERSED)std::swap(p,q);
            const auto v=TopExp::FirstVertex(edge,true);if(!v.IsNull())vertexError=std::max(vertexError,p.Distance(BRep_Tool::Pnt(v)));
            ends.emplace_back(p,q);
        }
        for(std::size_t i=0;i<ends.size();++i){const auto gap=ends[i].second.Distance(ends[(i+1)%ends.size()].first);
            wireGap=std::max(wireGap,gap);if(gap>1e-6)gaps.emplace_back(ObjectMap{{"edgeInWire",static_cast<int>(i+1)},{"gapMm",gap}});}
    }
    result["maximumTrimWireGapMm"]=wireGap;result["maximumVertexToTrimErrorMm"]=vertexError;
    result["trimGaps"]=gaps;result["edges"]=edges;return result;
}

ObjectMap HealFacePair(const TopoDS_Face& before,const TopoDS_Face& after) {
    const auto a=TrimBoundaryGeometry(before),b=TrimBoundaryGeometry(after);
    ObjectMap result{{"trimSourceToResultMm",DirectedBoundaryDeviation(a,b)},
        {"trimResultToSourceMm",DirectedBoundaryDeviation(b,a)},
        {"originalEdgeSourceToResultMm",DirectedBoundaryDeviation(before,after)},
        {"originalEdgeResultToSourceMm",DirectedBoundaryDeviation(after,before)}};
    const auto s=Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(before));
    const auto t=Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(after));
    if(!s.IsNull()&&!t.IsNull()&&s->NbUPoles()==t->NbUPoles()&&s->NbVPoles()==t->NbVPoles()) {
        double gap=0,weights=0;
        for(int i=1;i<=s->NbUPoles();++i)for(int j=1;j<=s->NbVPoles();++j){
            gap=std::max(gap,s->Pole(i,j).Distance(t->Pole(i,j)));
            weights=std::max(weights,std::abs(s->Weight(i,j)-t->Weight(i,j)));}
        result["controlPoleGapMm"]=gap;result["controlWeightGap"]=weights;
    }
    return result;
}

int HealFaceAudit(const fs::path& source,const std::string& selection,const fs::path& directory) {
    fs::create_directories(directory);
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto healed=R::HealWork{}.Run(G::Persistence::Deserialize(bytes));
    if(!healed.Success)throw std::runtime_error("Heal input failed");
    const auto shape=Build(healed.Geometry);
    BRepTools::Write(shape,Utf8(directory/"source.brep").c_str());
    TopTools_IndexedMapOfShape originals;TopExp::MapShapes(shape,TopAbs_FACE,originals);
    std::vector<int> selected;std::istringstream indices(selection);std::string word;
    while(std::getline(indices,word,','))selected.push_back(std::stoi(word));
    VariantArray cases;
    for(bool rational:{false,true}) {
        const auto prepared=FlatArtifact(StepBoundaryCopy(shape,false,rational));
        TopTools_IndexedMapOfShape presentations;TopExp::MapShapes(prepared,TopAbs_FACE,presentations);
        const auto stage=rational?"rational":"ordinary";
        BRepTools::Write(prepared,Utf8(directory/(std::string(stage)+".brep")).c_str());
        for(int index:selected) {
            const auto original=TopoDS::Face(originals(index)),presentation=TopoDS::Face(presentations(index));
            const auto base=std::string(stage)+"-face-"+std::to_string(index);
            const auto file=directory/(base+".step");Step(presentation,file);
            STEPControl_Reader reader;reader.ReadFile(Utf8(file).c_str());reader.TransferRoots();
            TopExp_Explorer read(reader.OneShape(),TopAbs_FACE);if(!read.More())throw std::runtime_error("Missing round-trip face");
            const auto restored=TopoDS::Face(read.Current());
            BRepTools::Write(original,Utf8(directory/(base+"-original.brep")).c_str());
            BRepTools::Write(restored,Utf8(directory/(base+"-read.brep")).c_str());
            ObjectMap row{{"index",index},{"encoding",std::string(stage)},{"original",HealFaceStats(original)},
                {"presentation",HealFaceStats(presentation)},{"read",HealFaceStats(restored)},
                {"sourceToPresentation",HealFacePair(original,presentation)},
                {"presentationToRead",HealFacePair(presentation,restored)}};
            cases.emplace_back(row);Save(directory/"audit.json",{{"cases",cases},{"healRejected",healed.RejectedChanges},
                {"healDiagnostics",Strings(healed.Diagnostics)}});
        }
    }
    return 0;
}

int CompareFaceBReps(const fs::path& source,const fs::path& target,const fs::path& output) {
    TopoDS_Shape a,b;BRep_Builder builder;
    if(!BRepTools::Read(a,Utf8(source).c_str(),builder)||!BRepTools::Read(b,Utf8(target).c_str(),builder))
        throw std::runtime_error("Cannot read audit BRep");
    TopExp_Explorer before(a,TopAbs_FACE),after(b,TopAbs_FACE);
    if(!before.More()||!after.More())throw std::runtime_error("Cannot find audit face");
    const auto s=TopoDS::Face(before.Current()),t=TopoDS::Face(after.Current());
    Save(output,{{"source",HealFaceStats(s)},{"target",HealFaceStats(t)},{"geometry",HealFacePair(s,t)}});return 0;
}
