// Included inside the private section-extraction namespace. Source faces and
// supports remain unchanged; only bounded projection regions are constructed.
struct SSourceProjectionEvidence
{
    std::uint64_t EdgeId=0,FaceId=0;
    std::vector<Point2> Points;
    TopoDS_Edge Projection;
};

SSourceProjectionEvidence SourceProjectionEvidence(std::uint64_t edgeId,std::uint64_t faceId,
    const Handle(Geom_Curve)& curve,double first,double last)
{
    SSourceProjectionEvidence result;result.EdgeId=edgeId;result.FaceId=faceId;
    for(int i=0;i<=64;++i) {
        const auto p=curve->Value(first+(last-first)*(i/64.0));result.Points.push_back({p.Y(),p.Z()});
    }
    const auto spline=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve,first,last));
    auto projected=Handle(Geom_BSplineCurve)::DownCast(spline->Copy());
    Bnd_Box controlBounds;
    for(int i=1;i<=projected->NbPoles();++i) {
        const auto p=projected->Pole(i);const gp_Pnt flat(p.Y(),p.Z(),0);
        projected->SetPole(i,flat);controlBounds.Add(flat);
    }
    double x,y,z,X,Y,Z;controlBounds.Get(x,y,z,X,Y,Z);
    if(std::hypot(X-x,Y-y)>Precision::Confusion()) {
        BRepBuilderAPI_MakeEdge make(projected);if(make.IsDone())result.Projection=make.Edge();
    }
    return result;
}

TopoDS_Face BoundedProjectionFace(TopoDS_Wire wire,double tolerance)
{
    const gp_Pln plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));
    BRepBuilderAPI_MakeFace face(plane,wire,false);if(!face.IsDone())return {};
    if(BRepTopAdaptor_FClass2d(face.Face(),Precision::Confusion()).PerformInfinitePoint()==TopAbs_IN) {
        wire.Reverse();face=BRepBuilderAPI_MakeFace(plane,wire,false);
    }
    return BRepCheck_Analyzer(face.Face()).IsValid()?face.Face():TopoDS_Face{};
}

bool ProjectionEvidenceFits(const TopoDS_Face& face,const std::vector<SSourceProjectionEvidence>& evidence,
    double tolerance,bool cavity,std::string& reason)
{
    SExactCandidateClassifier classifier(face,tolerance);Bnd_Box bounds;
    for(const auto& boundary:classifier.Boundary)bounds.Add(boundary.Bounds);
    // Axial generators and repeated source occurrences can project to the
    // identical point. Preserve every source record, but evaluate the exact
    // same geometric predicate once per coordinate pair at this precision.
    std::set<std::pair<double,double>> visited;
    for(const auto& source:evidence)for(const auto& point:source.Points) {
        if(!visited.emplace(point.X,point.Y).second)continue;
        const gp_Pnt p(point.X,point.Y,0);
        const bool outsideBounds=bounds.IsOut(p);
        if(cavity&&outsideBounds)continue;
        const auto relation=outsideBounds?EPointRelation::Outside:classifier.Classify(point);
        if(cavity) {
            if(relation!=EPointRelation::Inside)continue;
        } else {
            if(relation!=EPointRelation::Outside)continue;
            double distance=std::numeric_limits<double>::infinity();
            for(const auto& boundary:classifier.Boundary) {
                distance=std::min({distance,p.Distance(boundary.Curve->Value(boundary.First)),
                    p.Distance(boundary.Curve->Value(boundary.Last))});
                GeomAPI_ProjectPointOnCurve nearest(p,boundary.Curve,boundary.First,boundary.Last);
                if(nearest.NbPoints())distance=std::min(distance,nearest.LowerDistance());
            }
            if(distance<=tolerance)continue;
        }
        reason=std::string(cavity?"Interior":"Uncontained")+" source edge="+std::to_string(source.EdgeId)
            +" face="+std::to_string(source.FaceId)+" point=("+std::to_string(point.X)+","+std::to_string(point.Y)+")";
        return false;
    }
    return true;
}

std::vector<TopoDS_Face> PartitionProjectionCavity(const TopoDS_Face& cavity,
    const std::vector<SSourceProjectionEvidence>& evidence,const SSectionWireOptions& options)
{
    Bnd_Box bounds;BRepBndLib::AddOptimal(cavity,bounds,false,false);
    TopTools_ListOfShape arguments,tools;arguments.Append(cavity);
    for(const auto& source:evidence) {
        if(source.Projection.IsNull())continue;
        Bnd_Box other;BRepBndLib::AddOptimal(source.Projection,other,false,false);if(bounds.IsOut(other))continue;
        if(options.Progress)options.Progress("clipping cavity limiter source-edge="+std::to_string(source.EdgeId));
        TopTools_ListOfShape clipArguments,clipTools;clipArguments.Append(source.Projection);clipTools.Append(cavity);
        BRepAlgoAPI_Common clip;clip.SetArguments(clipArguments);clip.SetTools(clipTools);
        clip.SetNonDestructive(true);clip.Build();
        if(!clip.IsDone())throw std::runtime_error("Cannot clip an exact projected cavity limiter");
        for(TopExp_Explorer it(clip.Shape(),TopAbs_EDGE);it.More();it.Next())tools.Append(it.Current());
    }
    BRepAlgoAPI_Splitter split;split.SetArguments(arguments);split.SetTools(tools);split.SetNonDestructive(true);split.Build();
    if(!split.IsDone())throw std::runtime_error("Cannot partition the bounded cavity by its complete source boundaries");
    std::vector<std::pair<double,TopoDS_Face>> ordered;
    for(TopExp_Explorer it(split.Shape(),TopAbs_FACE);it.More();it.Next()) {
        const auto finite=BoundedProjectionFace(BRepTools::OuterWire(TopoDS::Face(it.Current())),options.dConnectionTolerance);
        if(!finite.IsNull())ordered.emplace_back(iCAX::OpenCascade::integration_detail::RefinedArea(finite),finite);
    }
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    std::vector<TopoDS_Face> result;for(const auto& [area,face]:ordered)result.push_back(face);return result;
}

SLoopCandidate ProjectionLoopCandidate(const TopoDS_Face& face,const SSectionWireOptions& options)
{
    SLoopCandidate loop;loop.ExactFace=face;
    gp_Trsf toYZ;toYZ.SetValues(0,0,1,0, 1,0,0,0, 0,1,0,0);
    const auto wire=BRepTools::OuterWire(face);
    for(BRepTools_WireExplorer it(wire,face);it.More();it.Next()) {
        const auto edge=it.Current();if(BRep_Tool::Degenerated(edge))continue;
        const auto moved=BRepBuilderAPI_Transform(edge,toYZ,true).Shape();
        const auto neutral=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(moved,"","",options.dConnectionTolerance);
        const SGeometryIndex index(neutral);
        if(neutral.Edges.size()!=1)throw std::runtime_error("Cannot retain an exact projection edge");
        const auto& record=neutral.Edges.front();const auto curve=index.Curves.find(record.Curve3Id);
        if(curve==index.Curves.end())throw std::runtime_error("Projection edge has no exact support");
        const auto flat=FlattenCurve(*curve->second);const auto native=iCAX::OpenCascade::BuildOpenCascadeCurve(*curve->second);
        if(!flat||native.IsNull())throw std::runtime_error("Cannot convert an exact projected support");
        const auto a=native->Value(record.Range.First),b=native->Value(record.Range.Last);
        SSectionWireEdge value;value.Curve=*flat;value.Range=record.Range;value.Start={a.Y(),a.Z()};value.End={b.Y(),b.Z()};
        value.Samples=SubSamples(value,0,1,options.nCurveSampleCount);
        if(edge.Orientation()==TopAbs_REVERSED) {
            std::swap(value.Start,value.End);std::reverse(value.Samples.begin(),value.Samples.end());value.bReversed=true;
        }
        AppendSamples(value,loop.Points);loop.Edges.push_back(std::move(value));
    }
    loop.dSignedArea=std::copysign(iCAX::OpenCascade::integration_detail::RefinedArea(face),SignedArea(loop.Points));
    return loop;
}

bool ProjectionVoidAlreadyRepresented(const TopoDS_Face& face,
    const std::vector<TopoDS_Face>& accepted,double tolerance)
{
    std::vector<SSourceProjectionEvidence> boundary;
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        double first,last;const auto curve=BRep_Tool::Curve(TopoDS::Edge(it.Current()),first,last);
        if(curve.IsNull())continue;
        SSourceProjectionEvidence edge;
        for(int i=0;i<=64;++i) {
            const auto p=curve->Value(first+(last-first)*(i/64.0));edge.Points.push_back({p.X(),p.Y()});
        }
        boundary.push_back(std::move(edge));
    }
    std::string reason;
    for(const auto& prior:accepted)
        if(ProjectionEvidenceFits(prior,boundary,tolerance,false,reason))return true;
    return false;
}

SSectionWiresResult RecoverMaterialSection(const BRepModel& geometry,const SSectionWireOptions& options,
    const std::vector<std::uint64_t>& sideFaces)
{
    SSectionWiresResult result;result.SideFaceIds=sideFaces;
    const auto built=iCAX::OpenCascade::ReconstructOpenCascadeShape(geometry);
    if(!built.bOK)throw std::runtime_error("Cannot reconstruct original material faces");
    std::vector<std::pair<std::uint64_t,TopoDS_Face>> sourceFaces;
    for(const auto& face:geometry.Faces)sourceFaces.emplace_back(face.Id,built.Faces.at(face.Id));
    const SGeometryIndex sourceIndex(geometry);
    std::unordered_map<std::uint64_t,std::uint64_t> edgeFaces;
    for(const auto& face:geometry.Faces)for(const auto wireId:face.WireIds)
        for(const auto& coedge:sourceIndex.Wires.at(wireId)->Coedges)
            edgeFaces.try_emplace(coedge.EdgeId,face.Id);
    std::vector<SSourceProjectionEvidence> evidence;
    for(const auto& edge:geometry.Edges) {
        if(edge.Degenerated)continue;double first,last;const auto curve=BRep_Tool::Curve(built.Edges.at(edge.Id),first,last);
        if(!curve.IsNull())evidence.push_back(SourceProjectionEvidence(edge.Id,
            edgeFaces.contains(edge.Id)?edgeFaces.at(edge.Id):0,curve,first,last));
    }
    for(auto& [id,face]:sourceFaces)if(section_detail::NeedsPreciseTrimTopology(face,options.dConnectionTolerance)) {
        std::string reason;face=section_detail::PreciseTrimAnalysisFace(face,options.dConnectionTolerance,reason);
        if(face.IsNull())throw std::runtime_error("Cannot certify original face="+std::to_string(id)+" at work precision: "+reason);
    }
    // A transverse annular source face supplies exact finite void boundaries.
    // Prefer that original topology when its outer boundary contains every
    // source edge; the cavities still undergo the complete evidence checks
    // below. This also avoids a needless union of overlapping end faces.
    TopoDS_Shape projected;
    for(const auto& [id,face]:sourceFaces) {
        BRepAdaptor_Surface support(face);
        if(support.GetType()!=GeomAbs_Plane)continue;
        if(std::abs(support.Plane().Axis().Direction().X())<std::cos(options.dFaceParallelToleranceRadians))continue;
        int wires=0;for(TopExp_Explorer it(face,TopAbs_WIRE);it.More();it.Next())++wires;
        if(wires<2)continue;
        const auto candidate=section_detail::ProjectSingleValuedFace(face,options.dConnectionTolerance);
        if(candidate.IsNull())continue;
        const auto enclosing=BoundedProjectionFace(BRepTools::OuterWire(candidate),options.dConnectionTolerance);
        std::string reason;
        if(!enclosing.IsNull()&&ProjectionEvidenceFits(enclosing,evidence,options.dConnectionTolerance,false,reason)) {
            projected=candidate;result.Diagnostics.push_back("Exact transverse source-face cavity seed: face="+std::to_string(id));break;
        }
    }
    if(projected.IsNull())projected=section_detail::UniteMaterialProjections(sourceFaces,options.dConnectionTolerance,options.Progress,result.Diagnostics);
    if(projected.IsNull())throw std::runtime_error("No bounded material projection is available");
    std::vector<std::pair<double,TopoDS_Face>> components;
    for(TopExp_Explorer it(projected,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());const auto outer=BoundedProjectionFace(BRepTools::OuterWire(face),options.dConnectionTolerance);
        if(!outer.IsNull())components.emplace_back(iCAX::OpenCascade::integration_detail::RefinedArea(outer),face);
    }
    std::stable_sort(components.begin(),components.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    TopoDS_Face material,outer;
    for(const auto& [area,face]:components) {
        const auto candidate=BoundedProjectionFace(BRepTools::OuterWire(face),options.dConnectionTolerance);std::string reason;
        if(ProjectionEvidenceFits(candidate,evidence,options.dConnectionTolerance,false,reason)){material=face;outer=candidate;break;}
        result.Diagnostics.push_back("Rejected projected outer: "+reason);
    }
    if(outer.IsNull())return result;
    result.Diagnostics.push_back("Projected outer contains all "+std::to_string(evidence.size())
        +" original source edges at the configured connection precision (65 support samples per edge)");
    std::vector<SLoopCandidate> loops;loops.push_back(ProjectionLoopCandidate(outer,options));
    const auto materialEvidence=ProjectMaterialFaces(geometry,options,result.Diagnostics,&built);
    for(TopExp_Explorer wi(material,TopAbs_WIRE);wi.More();wi.Next()) {
        if(wi.Current().IsSame(BRepTools::OuterWire(material)))continue;
        const auto cavity=BoundedProjectionFace(TopoDS::Wire(wi.Current()),options.dConnectionTolerance);
        if(cavity.IsNull())throw std::runtime_error("Projected cavity has invalid finite topology");
        std::string reason;std::vector<TopoDS_Face> candidates;
        if(ProjectionEvidenceFits(cavity,evidence,options.dConnectionTolerance,true,reason))candidates.push_back(cavity);
        else {
            result.Diagnostics.push_back("Refining cavity against complete source boundaries: "+reason);
            candidates=PartitionProjectionCavity(cavity,evidence,options);
        }
        // Keep the axial extractor's edge-disjoint cavity semantics. Accepting
        // a ring consumes its native edges, then dangling/bridge edges are
        // removed before considering another bounded cell. No coordinate
        // clustering, area threshold, or enlarged precision is involved.
        TopTools_IndexedMapOfShape nativeEdges,nativeVertices;
        for(const auto& face:candidates) {
            TopExp::MapShapes(face,TopAbs_EDGE,nativeEdges);
            TopExp::MapShapes(face,TopAbs_VERTEX,nativeVertices);
        }
        std::vector<SGraphNode> nodes(nativeVertices.Extent());
        std::vector<SGraphEdge> graph(nativeEdges.Extent());
        for(int i=1;i<=nativeEdges.Extent();++i) {
            auto edge=TopoDS::Edge(nativeEdges(i));edge.Orientation(TopAbs_FORWARD);
            TopoDS_Vertex first,last;TopExp::Vertices(edge,first,last);
            if(first.IsNull()||last.IsNull())throw std::runtime_error("Cavity graph has an unbounded native edge");
            auto& item=graph[i-1];item.StartNode=nativeVertices.FindIndex(first)-1;item.EndNode=nativeVertices.FindIndex(last)-1;
            nodes[item.StartNode].IncidentEdges.push_back(i-1);
            nodes[item.EndNode].IncidentEdges.push_back(i-1);
        }
        std::size_t accepted=0;
        std::vector<TopoDS_Face> acceptedVoids;
        for(const auto& face:candidates) {
            PruneDegreeOneEdges(nodes,graph);PruneBridgeEdges(nodes,graph);
            std::vector<std::size_t> indices;bool active=true;
            for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next()) {
                const auto index=static_cast<std::size_t>(nativeEdges.FindIndex(edge.Current())-1);
                indices.push_back(index);active=active&&graph[index].bActive;
            }
            if(!active)continue;
            // Native splitting works at native endpoint precision. A boundary
            // strip already represented by an accepted void at the caller's
            // unchanged connection precision is not an independent cavity.
            // This compares geometry, never cell area or source-face identity.
            if(ProjectionVoidAlreadyRepresented(face,acceptedVoids,options.dConnectionTolerance)) {
                result.Diagnostics.push_back("Partition cell is already represented by an accepted cavity at configured connection precision");
                continue;
            }
            if(!ProjectionEvidenceFits(face,evidence,options.dConnectionTolerance,true,reason)) {
                result.Diagnostics.push_back("Rejected projected cavity cell: "+reason);continue;
            }
            ShapeAnalysis_Wire topology(BRepTools::OuterWire(face),face,options.dConnectionTolerance);
            if(topology.CheckSelfIntersection()) {
                result.Diagnostics.push_back("Rejected self-intersecting native partition boundary: area="
                    +std::to_string(iCAX::OpenCascade::integration_detail::RefinedArea(face)));
                continue;
            }
            auto loop=ProjectionLoopCandidate(face,options);
            if(CandidateIsMaterialRegion(loop,materialEvidence,options.dConnectionTolerance,reason,options.Progress,options.nMaximumConcurrency)) {
                result.Diagnostics.push_back("Rejected projected material cell: "+reason);continue;
            }
            for(const auto index:indices)graph[index].bActive=false;
            result.Diagnostics.push_back("Accepted source-free native cavity: area="+std::to_string(loop.dSignedArea)
                +" edges="+std::to_string(loop.Edges.size()));
            loops.push_back(std::move(loop));acceptedVoids.push_back(face);++accepted;
        }
        if(!accepted)throw std::runtime_error("No source-free void cell remains in the projected cavity");
    }
    std::stable_sort(loops.begin()+1,loops.end(),[](const auto& a,const auto& b){return std::abs(a.dSignedArea)>std::abs(b.dSignedArea);});
    for(std::size_t index=0;index<loops.size();++index) {
        const auto& candidate=loops[index];
        ShapeAnalysis_Wire check(BRepTools::OuterWire(candidate.ExactFace),candidate.ExactFace,options.dConnectionTolerance);
        if(check.CheckSelfIntersection()) {
            std::string detail="Projected section wire self-intersects: loop="+std::to_string(index)
                +" area="+std::to_string(candidate.dSignedArea)+" edges="+std::to_string(candidate.Edges.size());
            const int count=static_cast<int>(candidate.Edges.size());
            for(int a=1;a<=count;++a) {
                if(check.CheckSelfIntersectingEdge(a))detail+=" self-edge="+std::to_string(a);
                for(int b=a+1;b<=count;++b)if(check.CheckIntersectingEdges(a,b)) {
                    detail+=" pair="+std::to_string(a)+","+std::to_string(b);
                }
            }
            throw std::runtime_error(detail);
        }
        SSectionWire wire;wire.bInner=index!=0;wire.bClosed=true;wire.dSignedArea=candidate.dSignedArea;wire.Edges=candidate.Edges;
        if((!wire.bInner&&wire.dSignedArea<0)||(wire.bInner&&wire.dSignedArea>0))ReverseWire(wire);
        for(std::size_t i=0;i<wire.Edges.size();++i) {
            const auto& edge=wire.Edges[i];const auto& next=wire.Edges[(i+1)%wire.Edges.size()];
            const auto a=EvaluateProjectedCurvePoint(edge,edge.bReversed?1.:0.);
            const auto b=EvaluateProjectedCurvePoint(edge,edge.bReversed?0.:1.);
            if(!a||!b||Distance(*a,edge.Start)>options.dConnectionTolerance||Distance(*b,edge.End)>options.dConnectionTolerance
                ||Distance(edge.End,next.Start)>options.dConnectionTolerance)
                throw std::runtime_error("Exact projected support endpoint or closure exceeds configured precision");
        }
        StartWireAtLowestEdge(wire,options.dConnectionTolerance);result.Wires.push_back(std::move(wire));
    }
    result.bSuccess=!result.Wires.empty();
    result.Diagnostics.push_back("Recovered bounded material projection with complete source-edge containment and source-free cavity cells");
    return result;
}
