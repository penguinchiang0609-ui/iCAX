#include <BRepAlgoAPI_Splitter.hxx>
#include <ShapeAnalysis_Wire.hxx>
int CavityRefinementAudit(const fs::path& source,const fs::path& seedFile,const fs::path& directory,bool surfaceTrims=false) {
    fs::create_directories(directory);std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto built=O::BuildOpenCascadeShape(axis.AlignedGeometry);
    TopoDS_Shape seedShape;
    if(seedFile.extension()==".occ-cache") {
        if(!BinTools::Read(seedShape,Utf8(seedFile).c_str()))throw std::runtime_error("Cannot read native candidate checkpoint");
    } else if(seedFile.extension()==".icaxbrep") {
        std::ifstream file(seedFile,std::ios::binary);const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        seedShape=Build(G::Persistence::Deserialize(data));
    } else {STEPControl_Reader reader;if(reader.ReadFile(Utf8(seedFile).c_str())!=IFSelect_RetDone||!reader.TransferRoots())throw std::runtime_error("Cannot read candidate seed");seedShape=reader.OneShape();}
    TopTools_IndexedMapOfShape seedEdges;TopExp::MapShapes(seedShape,TopAbs_EDGE,seedEdges);
    TopExp_Explorer originalWire(seedShape,TopAbs_WIRE);
    TopoDS_Wire seedWire;
    if(originalWire.More())seedWire=TopoDS::Wire(originalWire.Current());
    else {
        TopTools_ListOfShape orderedEdges;for(int i=1;i<=seedEdges.Extent();++i)orderedEdges.Append(seedEdges(i));
        BRepBuilderAPI_MakeWire seed;seed.Add(orderedEdges);
        if(!seed.IsDone())throw std::runtime_error("Exact candidate edges cannot form a wire");seedWire=seed.Wire();
    }
    if(Count(seedWire,TopAbs_EDGE)!=seedEdges.Extent())throw std::runtime_error("Candidate assembly omitted disconnected edges");
    Save(directory/"seed.json",{{"inputEdges",seedEdges.Extent()},{"assembledEdges",Count(seedWire,TopAbs_EDGE)}});
    Bnd_Box box;BRepBndLib::AddOptimal(seedWire,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
    if(X-x>0.001)throw std::runtime_error("Candidate is not in a YZ plane");
    gp_Trsf flatten;flatten.SetValues(0,1,0,0,0,0,1,0,1,0,0,-(x+X)/2);
    auto wire=TopoDS::Wire(BRepBuilderAPI_Transform(seedWire,flatten,true).Shape());
    BRepBuilderAPI_MakeFace finite(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),wire,false);
    if(BRepTopAdaptor_FClass2d(finite.Face(),Precision::Confusion()).PerformInfinitePoint()==TopAbs_IN) {
        wire.Reverse();finite=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),wire,false);
    }
    const auto cavity=finite.Face();Bnd_Box cavityBounds;BRepBndLib::AddOptimal(cavity,cavityBounds,false,false);
    struct Evidence {std::uint64_t Id;std::vector<G::Point2> Points;};std::vector<Evidence> evidence;
    struct SourceCurve {std::uint64_t Id,Face;Handle(Geom_Curve) Curve;double First,Last;};std::vector<SourceCurve> curves;
    if(surfaceTrims) {
        std::map<std::uint64_t,const G::BRepWire*> wires;for(const auto& wire:axis.AlignedGeometry.Wires)wires[wire.Id]=&wire;
        for(const auto& faceRecord:axis.AlignedGeometry.Faces)for(const auto wireId:faceRecord.WireIds)
            for(const auto& coedge:wires.at(wireId)->Coedges) {
                const auto edge=built.Edges.at(coedge.EdgeId);if(BRep_Tool::Degenerated(edge))continue;
                const auto face=built.Faces.at(faceRecord.Id);double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,face,a,b);
                if(pc.IsNull())throw std::runtime_error("Source face has no authoritative trim");
                const auto lifted=BRepBuilderAPI_MakeEdge(pc,BRep_Tool::Surface(face),a,b).Edge();
                if(!BRepLib::BuildCurve3d(lifted,Precision::Confusion()))throw std::runtime_error("Source trim cannot be lifted");
                BRepLib_CheckCurveOnSurface check(lifted,face);check.Perform();
                if(!check.IsDone()||check.MaxDistance()>0.001)throw std::runtime_error("Lifted source trim exceeds unchanged work precision");
                const auto c=BRep_Tool::Curve(lifted,a,b);curves.push_back({coedge.EdgeId,faceRecord.Id,c,a,b});
            }
    } else for(const auto& edge:axis.AlignedGeometry.Edges) {
        if(edge.Degenerated)continue;double a,b;const auto c=BRep_Tool::Curve(built.Edges.at(edge.Id),a,b);
        if(!c.IsNull())curves.push_back({edge.Id,0,c,a,b});
    }
    TopTools_ListOfShape arguments,tools;arguments.Append(cavity);VariantArray limiters;
    std::ofstream trace(directory/"trace.log");const auto start=Clock::now();
    for(const auto& edge:curves) {
        const auto& c=edge.Curve;const auto a=edge.First,b=edge.Last;
        Evidence e{edge.Id,{}};for(int k=0;k<=64;++k){const auto p=c->Value(a+(b-a)*k/64.0);e.Points.push_back({p.Y(),p.Z()});}evidence.push_back(e);
        auto spline=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(c,a,b));Bnd_Box bounds;
        for(int i=1;i<=spline->NbPoles();++i){const auto p=spline->Pole(i);spline->SetPole(i,gp_Pnt(p.Y(),p.Z(),0));bounds.Add(spline->Pole(i));}
        if(bounds.IsOut(cavityBounds))continue;
        double p,q,r,P,Q,Rr;bounds.Get(p,q,r,P,Q,Rr);if(std::hypot(P-p,Q-q)<=Precision::Confusion())continue;
        BRepBuilderAPI_MakeEdge make(spline);if(!make.IsDone())continue;
        TopTools_ListOfShape clipArguments,clipTools;clipArguments.Append(make.Edge());clipTools.Append(cavity);
        BRepAlgoAPI_Common clip;clip.SetArguments(clipArguments);clip.SetTools(clipTools);clip.SetNonDestructive(true);clip.Build();
        if(!clip.IsDone())throw std::runtime_error("Candidate limiter clipping failed");int count=0;
        for(TopExp_Explorer it(clip.Shape(),TopAbs_EDGE);it.More();it.Next()){tools.Append(it.Current());++count;}
        if(count)limiters.emplace_back(ObjectMap{{"sourceEdge",std::to_string(edge.Id)},{"pieces",count}});
        trace<<std::chrono::duration<double>(Clock::now()-start).count()<<" edge "<<edge.Id<<std::endl;
    }
    BRepAlgoAPI_Splitter split;split.SetArguments(arguments);split.SetTools(tools);split.SetNonDestructive(true);split.Build();
    if(!split.IsDone())throw std::runtime_error("Candidate subdivision failed");
    std::vector<std::pair<double,TopoDS_Face>> cells;for(TopExp_Explorer it(split.Shape(),TopAbs_FACE);it.More();it.Next()){
        auto boundary=BRepTools::OuterWire(TopoDS::Face(it.Current()));
        BRepBuilderAPI_MakeFace bounded(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),boundary,false);
        if(BRepTopAdaptor_FClass2d(bounded.Face(),Precision::Confusion()).PerformInfinitePoint()==TopAbs_IN) {
            boundary.Reverse();bounded=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),boundary,false);
        }
        const auto face=bounded.Face();cells.emplace_back(O::integration_detail::RefinedArea(face),face);}
    std::sort(cells.begin(),cells.end(),[](const auto& a,const auto& b){return a.first>b.first;});VariantArray rows;int index=0;
    for(const auto& [area,face]:cells) {
        R::section_detail::SExactCandidateClassifier classifier(face,0.001);std::uint64_t inside=0;G::Point2 witness;
        Bnd_Box cellBounds;BRepBndLib::AddOptimal(face,cellBounds,false,false);cellBounds.Enlarge(0.001);
        for(const auto& e:evidence){if(inside)break;for(const auto& point:e.Points)
            if(!cellBounds.IsOut(gp_Pnt(point.X,point.Y,0))&&!classifier.OnFiniteBoundary(point)
                &&classifier.Classify(point)==R::section_detail::EPointRelation::Inside){inside=e.Id;witness=point;break;}}
        ShapeAnalysis_Wire analysis(BRepTools::OuterWire(face),face,0.001);
        double witnessDistance=1e100,winding=0;BRepTopAdaptor_FClass2d alternate(face,0.001);
        const auto alternateState=alternate.Perform(gp_Pnt2d(witness.X,witness.Y),false);
        if(inside)for(BRepTools_WireExplorer wi(BRepTools::OuterWire(face),face);wi.More();wi.Next()) {
            const auto edge=wi.Current();double a,b;const auto c=BRep_Tool::Curve(edge,a,b);if(c.IsNull())continue;
            GeomAPI_ProjectPointOnCurve nearestWitness(gp_Pnt(witness.X,witness.Y,0),c,a,b);
            if(nearestWitness.NbPoints())witnessDistance=std::min(witnessDistance,nearestWitness.LowerDistance());
            witnessDistance=std::min({witnessDistance,gp_Pnt(witness.X,witness.Y,0).Distance(c->Value(a)),gp_Pnt(witness.X,witness.Y,0).Distance(c->Value(b))});
            if(edge.Orientation()==TopAbs_REVERSED)std::swap(a,b);
            auto prior=c->Value(a);for(int j=1;j<=256;++j){const auto next=c->Value(a+(b-a)*j/256.0);
                const double ax=prior.X()-witness.X,ay=prior.Y()-witness.Y,bx=next.X()-witness.X,by=next.Y()-witness.Y;
                winding+=std::atan2(ax*by-ay*bx,ax*bx+ay*by);prior=next;}
        }
        const bool valid=BRepCheck_Analyzer(face).IsValid(),self=analysis.CheckSelfIntersection();
        rows.emplace_back(ObjectMap{{"index",++index},{"area",area},{"interiorSourceEdge",std::to_string(inside)},
            {"witness",VariantArray{witness.X,witness.Y}},{"alternateState",static_cast<int>(alternateState)},{"witnessDistance",witnessDistance},{"windingTurns",winding/(2*std::acos(-1.))},{"valid",valid},{"selfIntersection",self},{"edges",Count(face,TopAbs_EDGE)}});
        if(index<=4||(!inside&&valid&&!self))Step(face,directory/("cell-"+std::to_string(index)+".step"));
        Save(directory/"refinement.json",{{"cells",rows},{"limiters",limiters},{"seedValid",BRepCheck_Analyzer(cavity).IsValid()},
            {"seconds",std::chrono::duration<double>(Clock::now()-start).count()}});
    }
    return 0;
}
