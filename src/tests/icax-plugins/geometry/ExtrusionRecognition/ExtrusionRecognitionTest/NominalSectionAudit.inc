// Read-only investigation of the longitudinal supports behind a stock section.
int NominalSectionAudit(const fs::path& source,const fs::path& directory) {
    fs::create_directories(directory);std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto model=G::Persistence::Deserialize(bytes);const auto built=O::ReconstructOpenCascadeShape(model);
    if(!built.bOK)throw std::runtime_error("Cannot reconstruct section audit source");
    const double tolerance=0.001,angle=0.001;
    struct Support {TopoDS_Face Face;double First,Last;std::vector<std::uint64_t> Faces;};
    std::vector<Support> groups;TopTools_IndexedMapOfShape axial;
    std::vector<int> faceGroups;std::vector<double> events;VariantArray supportRows;
    BRep_Builder builder;TopoDS_Compound sides;builder.MakeCompound(sides);
    for(const auto& f:model.Faces) {
        const auto face=built.Faces.at(f.Id);BRepAdaptor_Surface surface(face,true);
        double u,U,v,V;BRepTools::UVBounds(face,u,U,v,V);bool hasNormal=false,parallel=true;
        for(int i=0;i<3&&parallel;++i)for(int j=0;j<3;++j){gp_Pnt p;gp_Vec du,dv;
            surface.D1(u+(U-u)*(i+.5)/3,v+(V-v)*(j+.5)/3,p,du,dv);const auto n=du.Crossed(dv);
            if(n.Magnitude()<1e-12)continue;hasNormal=true;
            if(std::abs(n.X())/n.Magnitude()>std::sin(angle)){parallel=false;break;}}
        if(!parallel||!hasNormal)continue;
        Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
        events.push_back(x);events.push_back(X);
        for(TopExp_Explorer it(face,TopAbs_VERTEX);it.More();it.Next())events.push_back(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).X());
        int group=-1;
        for(std::size_t i=0;i<groups.size();++i) {
            BRepAdaptor_Surface other(groups[i].Face,true);if(other.GetType()!=surface.GetType())continue;
            bool same=false;
            if(surface.GetType()==GeomAbs_Plane) {
                const auto a=surface.Plane(),b=other.Plane();
                same=a.Axis().Direction().IsParallel(b.Axis().Direction(),1e-10)
                    &&a.Distance(b.Location())<=tolerance;
            } else if(surface.GetType()==GeomAbs_Cylinder) {
                const auto a=surface.Cylinder(),b=other.Cylinder();
                same=a.Axis().Direction().IsParallel(b.Axis().Direction(),1e-10)
                    &&gp_Lin(a.Axis()).Distance(b.Location())<=tolerance&&std::abs(a.Radius()-b.Radius())<=tolerance;
            }
            if(same){group=static_cast<int>(i);break;}
        }
        if(group<0){group=static_cast<int>(groups.size());groups.push_back({face,x,X,{}});}
        auto& support=groups[group];support.First=std::min(support.First,x);support.Last=std::max(support.Last,X);support.Faces.push_back(f.Id);
        axial.Add(face);faceGroups.push_back(group);builder.Add(sides,face);
    }
    for(std::size_t i=0;i<groups.size();++i){VariantArray ids;for(const auto id:groups[i].Faces)ids.emplace_back(std::to_string(id));
        supportRows.emplace_back(ObjectMap{{"group",static_cast<int>(i)},{"faces",ids},{"firstX",groups[i].First},{"lastX",groups[i].Last}});}
    std::sort(events.begin(),events.end());events.erase(std::unique(events.begin(),events.end(),[&](double a,double b){return std::abs(a-b)<=tolerance;}),events.end());
    struct Interval{double First,Last;};std::vector<Interval> intervals;
    for(std::size_t i=1;i<events.size();++i)if(events[i]-events[i-1]>2*tolerance)intervals.push_back({events[i-1],events[i]});
    std::sort(intervals.begin(),intervals.end(),[](const auto& a,const auto& b){return a.Last-a.First>b.Last-b.First;});
    VariantArray candidates;int index=0,best=-1;double bestScore=-1,bestWidth=-1;TopoDS_Shape bestShape;
    for(const auto& interval:intervals) {
        const double station=(interval.First+interval.Last)/2;
        BRepAlgoAPI_Section cut(sides,gp_Pln(gp_Pnt(station,0,0),gp_Dir(1,0,0)),false);
        cut.SetNonDestructive(true);cut.Build();if(!cut.IsDone())continue;
        auto edges=new NCollection_HSequence<TopoDS_Shape>;std::set<int> usedGroups;bool provenance=true;
        for(TopExp_Explorer it(cut.Shape(),TopAbs_EDGE);it.More();it.Next()) {
            if(BRep_Tool::Degenerated(TopoDS::Edge(it.Current())))continue;
            edges->Append(it.Current());TopoDS_Shape ancestor;
            if(!cut.HasAncestorFaceOn1(it.Current(),ancestor)){provenance=false;continue;}
            const int at=axial.FindIndex(ancestor);if(at>0)usedGroups.insert(faceGroups[at-1]);else provenance=false;
        }
        if(edges->IsEmpty())continue;
        const auto wires=ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges,tolerance,false);
        int closed=0,open=0;double gap=0,score=std::numeric_limits<double>::max();
        for(int i=1;i<=wires->Length();++i) {
            const auto wire=TopoDS::Wire(wires->Value(i));std::vector<std::pair<gp_Pnt,gp_Pnt>> ends;
            for(BRepTools_WireExplorer it(wire);it.More();it.Next()){double first,last;const auto c=BRep_Tool::Curve(it.Current(),first,last);if(c.IsNull())continue;
                auto a=c->Value(first),b=c->Value(last);if(it.Current().Orientation()==TopAbs_REVERSED)std::swap(a,b);ends.emplace_back(a,b);}
            double local=0;for(std::size_t j=0;j<ends.size();++j)local=std::max(local,ends[j].second.Distance(ends[(j+1)%ends.size()].first));
            gap=std::max(gap,local);if(!ends.empty()&&local<=tolerance)++closed;else ++open;
        }
        for(const auto at:usedGroups)score=std::min(score,groups[at].Last-groups[at].First);
        const bool valid=provenance&&closed>0&&!open&&!usedGroups.empty();VariantArray ids;
        for(const auto at:usedGroups)ids.emplace_back(at);
        const int id=index++;candidates.emplace_back(ObjectMap{{"index",id},{"stationX",station},{"intervalFirst",interval.First},{"intervalLast",interval.Last},
            {"valid",valid},{"closedWires",closed},{"openWires",open},{"maximumGapMm",gap},{"supportEnvelopeSpanMm",score},{"supportGroups",ids}});
        if(valid&&(score>bestScore+tolerance||(std::abs(score-bestScore)<=tolerance&&interval.Last-interval.First>bestWidth))) {
            best=id;bestScore=score;bestWidth=interval.Last-interval.First;bestShape=cut.Shape();
        }
    }
    Save(directory/"nominal-sections.json",{{"source",Utf8(source)},{"supports",supportRows},{"candidates",candidates},{"selected",best},{"selectedSupportSpanMm",bestScore}});
    if(!bestShape.IsNull()) {Step(bestShape,directory/"selected-section.step");RawProjectionSvg(O::ConvertOpenCascadeShapeToBRep(bestShape,"","",tolerance),directory/"selected-section.svg");}
    return 0;
}

int FaceMeshAudit(const fs::path& source,const fs::path& output) {
    const auto extension=source.extension().wstring();TopoDS_Shape shape;
    if(extension==L".step"||extension==L".stp") {STEPControl_Reader reader;if(reader.ReadFile(Utf8(source).c_str())!=IFSelect_RetDone)throw std::runtime_error("STEP mesh audit read failed");reader.TransferRoots();shape=reader.OneShape();}
    else {std::ifstream input(source,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());shape=Build(G::Persistence::Deserialize(bytes));}
    if(GetEnvironmentVariableW(L"ICAX_TUBE_TEST_REFINE_REVIEW_EDGES",nullptr,0)) {
        shape=RefineThinPlanarPresentationEdges(BRepBuilderAPI_Copy(shape,true,false).Shape());
        Step(shape,fs::path(output.wstring()+L".step"));
        STEPControl_Reader reader;reader.ReadFile(Utf8(fs::path(output.wstring()+L".step")).c_str());reader.TransferRoots();shape=reader.OneShape();
    }
    wchar_t requested[64]{};double deflection=.1;if(GetEnvironmentVariableW(L"ICAX_TUBE_MESH_DEFLECTION",requested,64))deflection=std::stod(requested);
    BRepTools::Clean(shape);BRepMesh_IncrementalMesh mesh(shape,deflection,false,0.5,false);VariantArray rows;int index=0;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());TopLoc_Location location;const auto triangulation=BRep_Tool::Triangulation(face,location);
        double area=0;int triangles=0;
        if(!triangulation.IsNull())for(int i=1;i<=triangulation->NbTriangles();++i){int a,b,c;triangulation->Triangle(i).Get(a,b,c);
            area+=gp_Vec(triangulation->Node(a),triangulation->Node(b)).Crossed(gp_Vec(triangulation->Node(a),triangulation->Node(c))).Magnitude()/2;++triangles;}
        double u,U,v,V;BRepTools::UVBounds(face,u,U,v,V);VariantArray edges;
        for(TopExp_Explorer ei(face,TopAbs_EDGE);ei.More();ei.Next()){
            const auto edge=TopoDS::Edge(ei.Current());double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,face,a,b);
            ObjectMap item{{"toleranceMm",BRep_Tool::Tolerance(edge)},{"range",VariantArray{a,b}},
                {"seam",BRep_Tool::IsClosed(edge,face)},{"degenerated",BRep_Tool::Degenerated(edge)}};
            if(!pc.IsNull()){const auto p=pc->Value(a),q=pc->Value(b);item["uvStart"]=VariantArray{p.X(),p.Y()};item["uvEnd"]=VariantArray{q.X(),q.Y()};}
            edges.emplace_back(item);
        }
        rows.emplace_back(ObjectMap{{"face",++index},{"exactAreaMm2",Area(face)},{"meshAreaMm2",area},{"triangles",triangles},
            {"surfaceType",static_cast<int>(BRepAdaptor_Surface(face).GetType())},{"uvBounds",VariantArray{u,U,v,V}},{"edges",edges}});
    }
    Save(output,{{"source",Utf8(source)},{"faces",rows},{"deflectionMm",deflection},{"scope",std::string("Independent native triangulation; exact surface areas retained")}});return 0;
}
