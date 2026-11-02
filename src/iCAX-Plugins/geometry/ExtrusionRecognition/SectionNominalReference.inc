// A local end step must not redefine the reference section of an entire
// extrusion. Recover a section from actual longitudinal supports, retaining
// their source identities and checking that no stronger support was omitted.
bool HasLocalizedSectionEvidence(const BRepModel& geometry,const SSectionWiresResult& section,
    const SSectionWireOptions& options) {
    if(geometry.Vertices.empty())return false;const SGeometryIndex index(geometry);
    double first=std::numeric_limits<double>::max(),last=std::numeric_limits<double>::lowest();
    for(const auto& vertex:geometry.Vertices){first=std::min(first,vertex.Position.X);last=std::max(last,vertex.Position.X);}const double middle=(first+last)/2;
    struct Plane {std::uint64_t Id;const iCAX::GeometryData::PlaneSurface3* Surface;double First,Last;std::set<std::uint64_t> Edges;};
    std::vector<Plane> planes;
    for(const auto& face:geometry.Faces) {
        const auto* surface=FindSurface(index,face.Surface3Id);const auto* plane=surface?std::get_if<iCAX::GeometryData::PlaneSurface3>(surface):nullptr;
        if(!plane||!IsSideFace(face,index,options))continue;
        Plane item{face.Id,plane,std::numeric_limits<double>::max(),std::numeric_limits<double>::lowest(),{}};
        for(const auto wireId:face.WireIds)if(const auto* wire=FindWire(index,wireId))for(const auto& coedge:wire->Coedges) {
            item.Edges.insert(coedge.EdgeId);for(const auto id:{coedge.StartVertexId,coedge.EndVertexId})if(const auto* vertex=FindVertex(index,id)) {
                item.First=std::min(item.First,vertex->Position.X);item.Last=std::max(item.Last,vertex->Position.X);
            }
        }
        planes.push_back(std::move(item));
    }
    for(const auto& wire:section.Wires)for(const auto& edge:wire.Edges)for(const auto& source:planes)
        if(source.Id==edge.SourceFaceId||source.Edges.contains(edge.SourceEdgeId)) {
            const auto& n=source.Surface->Placement.ZDirection;const auto& origin=source.Surface->Placement.Location;
            if(std::abs(n.X)<=1e-10)for(const auto point:{edge.Start,edge.End})
                if(std::abs(n.Y*(point.X-origin.Y)+n.Z*(point.Y-origin.Z))>options.dConnectionTolerance)return true;
            if(source.First<=middle+options.dConnectionTolerance&&source.Last>=middle-options.dConnectionTolerance)continue;
            const gp_Dir normal(n.X,n.Y,n.Z);
            for(const auto& alternative:planes)if(alternative.First<=middle+options.dConnectionTolerance
                &&alternative.Last>=middle-options.dConnectionTolerance) {
                const auto& other=alternative.Surface->Placement.ZDirection;
                if(normal.IsParallel(gp_Dir(other.X,other.Y,other.Z),1e-10))return true;
            }
        }
    std::size_t suppressedWires=0;
    for(const auto& wire:section.Wires) {
        bool suppressed=false;
        for(std::size_t i=0;i<wire.Edges.size();++i) {
            const auto& projected=wire.Edges[i];const auto& next=wire.Edges[(i+1)%wire.Edges.size()];
            double budget=Precision::Confusion();
            const auto found=index.Edges.find(projected.SourceEdgeId);
            if(found!=index.Edges.end()) {
                budget=std::max(budget,found->second->Tolerance);
                for(const auto id:{found->second->StartVertexId,found->second->EndVertexId})
                    if(const auto* v=FindVertex(index,id))budget=std::max(budget,v->Tolerance);
                // Near-complete curve projection can close a real source
                // interval before graph traversal, leaving no endpoint gap.
                if(Distance(projected.Start,projected.End)<=Precision::Confusion()) {
                    const auto record=index.Curves.find(found->second->Curve3Id);
                    const auto curve=record==index.Curves.end()?Handle(Geom_Curve){}:iCAX::OpenCascade::BuildOpenCascadeCurve(*record->second);
                    if(!curve.IsNull()) {
                        const auto a=curve->Value(found->second->Range.First),b=curve->Value(found->second->Range.Last);
                        suppressed=suppressed||std::hypot(a.Y()-b.Y(),a.Z()-b.Z())>2*budget;
                    }
                }
            }
            if(const auto n=index.Edges.find(next.SourceEdgeId);n!=index.Edges.end())budget=std::max(budget,n->second->Tolerance);
            suppressed=suppressed||Distance(projected.End,next.Start)>2*budget;
            if(!suppressed&&projected.SourceFaceId&&Distance(projected.Start,projected.End)<=Precision::Confusion())
                for(const auto& face:geometry.Faces)if(face.Id==projected.SourceFaceId)
                    for(const auto id:face.WireIds)if(const auto* boundary=FindWire(index,id))for(const auto& coedge:boundary->Coedges) {
                        const auto original=index.Edges.find(coedge.EdgeId);if(original==index.Edges.end())continue;
                        const auto record=index.Curves.find(original->second->Curve3Id);if(record==index.Curves.end())continue;
                        const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(*record->second);if(curve.IsNull())continue;
                        const auto a=curve->Value(original->second->Range.First),b=curve->Value(original->second->Range.Last);
                        const double gap=std::hypot(a.Y()-b.Y(),a.Z()-b.Z());
                        if(std::abs(a.X()-b.X())<=options.dConnectionTolerance&&gap<=options.dConnectionTolerance
                            &&gap>2*std::max(Precision::Confusion(),original->second->Tolerance))suppressed=true;
                    }
        }
        if(suppressed)++suppressedWires;
    }
    // The lips must explain a junction suppressed in an existing boundary
    // loop. Close independent sheets inside a fully connected stock outline
    // are not evidence that its already complete reference lost a slit.
    if(suppressedWires==0)return false;
    // Two distinct, nearly coincident full-length lips can be collapsed by
    // the projection graph. Require their actual finite projected intervals,
    // beyond source uncertainty; a generic tolerant curve junction is not
    // evidence of missing stock material.
    struct Lip {std::uint64_t Face;Point2 A,B,Middle;double Budget;};std::vector<Lip> lips;
    for(const auto& source:planes)if(source.First<=first+options.dConnectionTolerance
        &&source.Last>=last-options.dConnectionTolerance)for(const auto id:source.Edges) {
        const auto found=index.Edges.find(id);if(found==index.Edges.end())continue;
        const auto record=index.Curves.find(found->second->Curve3Id);if(record==index.Curves.end())continue;
        const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(*record->second);if(curve.IsNull())continue;
        const auto a=curve->Value(found->second->Range.First),b=curve->Value(found->second->Range.Last);
        if(std::abs(a.X()-b.X())>options.dConnectionTolerance
            ||std::hypot(a.Y()-b.Y(),a.Z()-b.Z())<=2*options.dConnectionTolerance)continue;
        const auto p=curve->Value((found->second->Range.First+found->second->Range.Last)/2);
        double budget=std::max(Precision::Confusion(),found->second->Tolerance);
        for(const auto vertex:{found->second->StartVertexId,found->second->EndVertexId})
            if(const auto* v=FindVertex(index,vertex))budget=std::max(budget,v->Tolerance);
        lips.push_back({source.Id,{a.Y(),a.Z()},{b.Y(),b.Z()},{p.Y(),p.Z()},budget});
    }
    for(std::size_t i=0;i<lips.size();++i)for(std::size_t j=i+1;j<lips.size();++j) {
        const auto& a=lips[i];const auto& b=lips[j];if(a.Face==b.Face)continue;
        const double gap=std::min(std::max(Distance(a.A,b.A),Distance(a.B,b.B)),
            std::max(Distance(a.A,b.B),Distance(a.B,b.A)));
        if(gap>a.Budget+b.Budget&&gap<=options.dConnectionTolerance) {
            double distance=std::numeric_limits<double>::infinity();
            for(const auto& wire:section.Wires)for(const auto& edge:wire.Edges) {
                const auto native=iCAX::OpenCascade::BuildOpenCascadeCurve2(edge.Curve);if(native.IsNull())continue;
                Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(a.Middle.X,a.Middle.Y),native,edge.Range.First,edge.Range.Last);
                if(projection.NbPoints())distance=std::min(distance,projection.LowerDistance());
            }
            if(distance>options.dConnectionTolerance)return true;
        }
    }
    return false;
}

struct SNominalSectionEdge {
    TopoDS_Edge Edge;
    Handle(Geom_Curve) Curve;
    double First=0,Last=0,Budget=0;
    int Group=-1;
    std::uint64_t FaceId=0;
};

bool JoinNominalSectionSupports(std::vector<SNominalSectionEdge>& edges,double station,double tolerance) {
    const gp_Pln plane(gp_Ax3(gp_Pnt(station,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0)));
    struct End {std::size_t Edge;bool Last;gp_Pnt Point;};std::vector<End> ends;
    for(std::size_t i=0;i<edges.size();++i)for(bool last:{false,true})
        ends.push_back({i,last,edges[i].Curve->Value(last?edges[i].Last:edges[i].First)});
    std::vector<bool> connected(ends.size(),false);
    for(std::size_t i=0;i<ends.size();++i)for(std::size_t j=i+1;j<ends.size();++j)
        if(ends[i].Point.Distance(ends[j].Point)<=tolerance)connected[i]=connected[j]=true;
    struct Join {std::size_t A,B;double UA,UB;};std::vector<Join> joins;std::vector<int> choices(ends.size());
    for(std::size_t i=0;i<ends.size();++i)if(!connected[i])for(std::size_t j=i+1;j<ends.size();++j) {
        if(connected[j]||ends[i].Edge==ends[j].Edge)continue;
        const auto& a=edges[ends[i].Edge];const auto& b=edges[ends[j].Edge];if(a.Group==b.Group)continue;
        if(ends[i].Point.Distance(ends[j].Point)>a.Budget+b.Budget+2*Precision::Confusion())continue;
        const auto ca=GeomAPI::To2d(a.Curve,plane),cb=GeomAPI::To2d(b.Curve,plane);
        const Geom2dAdaptor_Curve aa(ca),bb(cb);
        if((aa.GetType()!=GeomAbs_Line&&aa.GetType()!=GeomAbs_Circle)
            ||(bb.GetType()!=GeomAbs_Line&&bb.GetType()!=GeomAbs_Circle))continue;
        Geom2dAPI_InterCurveCurve intersection(ca,cb,Precision::Confusion());
        int found=0;double ua=0,ub=0;
        for(int k=1;k<=intersection.NbPoints();++k) {
            const auto point=intersection.Point(k);const auto p=plane.Location().Translated(
                gp_Vec(plane.XAxis().Direction())*point.X()+gp_Vec(plane.YAxis().Direction())*point.Y());
            if(p.Distance(ends[i].Point)>a.Budget+Precision::Confusion()
                ||p.Distance(ends[j].Point)>b.Budget+Precision::Confusion())continue;
            Geom2dAPI_ProjectPointOnCurve pa(point,ca),pb(point,cb);if(!pa.NbPoints()||!pb.NbPoints())continue;
            ua=pa.LowerDistanceParameter();ub=pb.LowerDistanceParameter();
            const auto periodic=[](double u,const Handle(Geom_Curve)& c,double previous){
                return c->IsPeriodic()?u+std::round((previous-u)/c->Period())*c->Period():u;};
            ua=periodic(ua,a.Curve,ends[i].Last?a.Last:a.First);ub=periodic(ub,b.Curve,ends[j].Last?b.Last:b.First);
            ++found;
        }
        if(found==1){joins.push_back({i,j,ua,ub});++choices[i];++choices[j];}
    }
    for(const auto& join:joins)if(choices[join.A]==1&&choices[join.B]==1) {
        const auto& a=ends[join.A];const auto& b=ends[join.B];
        (a.Last?edges[a.Edge].Last:edges[a.Edge].First)=join.UA;
        (b.Last?edges[b.Edge].Last:edges[b.Edge].First)=join.UB;
        connected[join.A]=connected[join.B]=true;
    }
    if(std::any_of(connected.begin(),connected.end(),[](bool value){return !value;}))return false;
    for(auto& edge:edges) {
        if(edge.Last-edge.First<=Precision::PConfusion())return false;
        BRepBuilderAPI_MakeEdge make(edge.Curve,edge.First,edge.Last);if(!make.IsDone())return false;edge.Edge=make.Edge();
    }
    return true;
}

SSectionWiresResult ExtractCertifiedNominalSection(const BRepModel& geometry,const SSectionWireOptions& options,
    const SSectionWiresResult& axialSection) {
    SSectionWiresResult result;const SGeometryIndex index(geometry);
    const auto built=iCAX::OpenCascade::ReconstructOpenCascadeShape(geometry);if(!built.bOK)return result;
    // A local station may omit a flange whose remaining axial span is short.
    // Span ranking alone cannot certify the complete stock envelope. Keep
    // independent whole-model boundary evidence, just as the axial extractor
    // and material-projection recovery do for their outer candidates.
    std::map<double,std::vector<SSourceProjectionEvidence>> boundaryEvidence;
    TopTools_IndexedMapOfShape faceEdges;
    for(const auto& record:geometry.Faces) {
        const auto face=built.Faces.at(record.Id);
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());faceEdges.Add(edge);
            if(BRep_Tool::Degenerated(edge))continue;
            double first,last;const auto pc=BRep_Tool::CurveOnSurface(edge,face,first,last);
            if(pc.IsNull())return result;
            // The unchanged surface trim is the face boundary. An imported
            // independent 3D edge approximation may lie outside that trim;
            // it cannot veto the exact nominal section at work precision.
            BRep_Builder builder;TopoDS_Edge lift;builder.MakeEdge(lift);
            builder.UpdateEdge(lift,pc,face,Precision::Confusion());builder.Range(lift,face,first,last);
            if(!BRepLib::BuildCurve3d(lift,1e-8))return result;
            const auto curve=BRep_Tool::Curve(lift,first,last);if(curve.IsNull())return result;
            double budget=std::max(BRep_Tool::Tolerance(face),BRep_Tool::Tolerance(edge));
            for(TopExp_Explorer vi(edge,TopAbs_VERTEX);vi.More();vi.Next())budget=std::max(budget,BRep_Tool::Tolerance(TopoDS::Vertex(vi.Current())));
            boundaryEvidence[options.dConnectionTolerance+budget].push_back(SourceProjectionEvidence(0,record.Id,curve,first,last));
        }
    }
    for(const auto& record:geometry.Edges) {
        if(record.Degenerated||faceEdges.Contains(built.Edges.at(record.Id)))continue;double first,last;
        const auto curve=BRep_Tool::Curve(built.Edges.at(record.Id),first,last);
        if(!curve.IsNull())boundaryEvidence[options.dConnectionTolerance+BRep_Tool::Tolerance(built.Edges.at(record.Id))].push_back(SourceProjectionEvidence(record.Id,0,curve,first,last));
    }
    // The ID map deliberately stores canonical forward faces. Material side
    // belongs to the actual root/shell occurrence, not that canonical record.
    TopTools_IndexedMapOfShape occurrences;TopExp::MapShapes(built.Shape,TopAbs_FACE,occurrences);
    TopTools_IndexedMapOfShape solids;TopExp::MapShapes(built.Shape,TopAbs_SOLID,solids);
    std::optional<std::vector<SMaterialFaceProjection>> materialEvidence;
    std::vector<TopoDS_Face> certifiedCavities;
    if(solids.IsEmpty())for(const auto& prior:axialSection.Wires)if(prior.bInner) {
        BRepBuilderAPI_MakeWire wire;
        const gp_Pln plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));bool valid=true;
        for(const auto& boundary:prior.Edges) {
            const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(boundary.Curve);
            if(curve.IsNull()){valid=false;break;}
            BRepBuilderAPI_MakeEdge make(GeomAPI::To3d(curve,plane),boundary.Range.First,boundary.Range.Last);
            if(!make.IsDone()){valid=false;break;}auto edge=make.Edge();if(boundary.bReversed)edge.Reverse();wire.Add(edge);
        }
        if(valid&&wire.IsDone()) {
            const auto face=BoundedProjectionFace(wire.Wire(),options.dConnectionTolerance);
            if(!face.IsNull())certifiedCavities.push_back(face);
        }
    }
    struct Support {TopoDS_Face Face;double First,Last,Budget;std::vector<std::uint64_t> Faces;};
    std::vector<Support> supports;TopTools_IndexedMapOfShape axial;std::vector<int> faceGroups;
    std::vector<std::uint64_t> faceIds;std::vector<double> faceBudgets,events;BRep_Builder builder;TopoDS_Compound sides;builder.MakeCompound(sides);
    for(const auto& record:geometry.Faces) {
        if(!IsSideFace(record,index,options))continue;
        const int occurrence=occurrences.FindIndex(built.Faces.at(record.Id));if(!occurrence)continue;
        const auto face=TopoDS::Face(occurrences(occurrence));BRepAdaptor_Surface surface(face,true);
        Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);if(box.IsVoid())continue;
        double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);events.push_back(x);events.push_back(X);
        double budget=BRep_Tool::Tolerance(face);
        for(TopExp_Explorer it(face,TopAbs_VERTEX);it.More();it.Next()) {
            const auto vertex=TopoDS::Vertex(it.Current());events.push_back(BRep_Tool::Pnt(vertex).X());
            budget=std::max(budget,BRep_Tool::Tolerance(vertex));
        }
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next())budget=std::max(budget,BRep_Tool::Tolerance(TopoDS::Edge(it.Current())));
        int group=-1;
        for(std::size_t i=0;i<supports.size();++i) {
            BRepAdaptor_Surface other(supports[i].Face,true);if(surface.GetType()!=other.GetType())continue;bool same=false;
            if(surface.GetType()==GeomAbs_Plane) {
                const auto a=surface.Plane(),b=other.Plane();same=a.Axis().Direction().IsParallel(b.Axis().Direction(),1e-10)
                    &&a.Distance(b.Location())<=options.dConnectionTolerance;
            } else if(surface.GetType()==GeomAbs_Cylinder) {
                const auto a=surface.Cylinder(),b=other.Cylinder();same=a.Axis().Direction().IsParallel(b.Axis().Direction(),1e-10)
                    &&gp_Lin(a.Axis()).Distance(b.Location())<=options.dConnectionTolerance
                    &&std::abs(a.Radius()-b.Radius())<=options.dConnectionTolerance;
            }
            if(same){group=static_cast<int>(i);break;}
        }
        if(group<0){group=static_cast<int>(supports.size());supports.push_back({face,x,X,budget,{}});}
        auto& support=supports[group];support.First=std::min(support.First,x);support.Last=std::max(support.Last,X);
        support.Budget=std::max(support.Budget,budget);support.Faces.push_back(record.Id);
        axial.Add(face);faceGroups.push_back(group);faceIds.push_back(record.Id);faceBudgets.push_back(budget);builder.Add(sides,face);result.SideFaceIds.push_back(record.Id);
    }
    const double tolerance=options.dConnectionTolerance;
    std::sort(events.begin(),events.end());events.erase(std::unique(events.begin(),events.end(),[&](double a,double b){return std::abs(a-b)<=tolerance;}),events.end());
    struct Interval {double First,Last;};std::vector<Interval> intervals;
    for(std::size_t i=1;i<events.size();++i)if(events[i]-events[i-1]>2*tolerance)intervals.push_back({events[i-1],events[i]});
    std::sort(intervals.begin(),intervals.end(),[](const auto& a,const auto& b){return a.Last-a.First>b.Last-b.First;});
    double bestScore=-1,bestWidth=-1,bestStation=0,maxSupportSpan=0;std::vector<SSectionWire> bestWires;
    for(const auto& support:supports)maxSupportSpan=std::max(maxSupportSpan,support.Last-support.First);
    for(const auto& interval:intervals) {
        const double station=(interval.First+interval.Last)/2;
        if(options.Progress)options.Progress("nominal reference section X="+std::to_string(station));
        BRepAlgoAPI_Section cut(sides,gp_Pln(gp_Pnt(station,0,0),gp_Dir(1,0,0)),false);
        cut.SetNonDestructive(true);cut.Build();if(!cut.IsDone())continue;
        std::vector<SNominalSectionEdge> edges;std::set<int> used;bool provenance=true;
        for(TopExp_Explorer it(cut.Shape(),TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(edge))continue;TopoDS_Shape ancestor;
            if(!cut.HasAncestorFaceOn1(edge,ancestor)){provenance=false;break;}
            const int at=axial.FindIndex(ancestor);if(!at){provenance=false;break;}
            double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);if(curve.IsNull()){provenance=false;break;}
            const int group=faceGroups[at-1];used.insert(group);
            edges.push_back({edge,curve,first,last,faceBudgets[at-1],group,faceIds[at-1]});
        }
        if(!provenance||edges.empty())continue;
        double score=std::numeric_limits<double>::max();for(const auto group:used)score=std::min(score,supports[group].Last-supports[group].First);
        if(score<bestScore-tolerance)continue;
        if(!JoinNominalSectionSupports(edges,station,tolerance)){if(options.Progress)options.Progress("nominal rejected open support junctions");continue;}
        // Retain ownership across both connection attempts. Passing a raw
        // pointer creates a temporary handle whose destruction frees it.
        Handle(NCollection_HSequence<TopoDS_Shape>) sequence=new NCollection_HSequence<TopoDS_Shape>;
        for(const auto& edge:edges)sequence->Append(edge.Edge);
        // Prefer the exact existing junctions before allowing uncertain joins.
        // A wider connection budget must not weld the two lips of a real slit.
        auto wires=ShapeAnalysis_FreeBounds::ConnectEdgesToWires(sequence,Precision::Confusion(),false);
        bool exactlyClosed=wires->Length()>0;
        for(int i=1;i<=wires->Length()&&exactlyClosed;++i) {
            const auto wire=TopoDS::Wire(wires->Value(i));TopoDS_Vertex a,b;TopExp::Vertices(wire,a,b);
            exactlyClosed=!a.IsNull()&&!b.IsNull()&&BRep_Tool::Pnt(a).Distance(BRep_Tool::Pnt(b))<=Precision::Confusion();
        }
        if(!exactlyClosed)wires=ShapeAnalysis_FreeBounds::ConnectEdgesToWires(sequence,tolerance,false);
        gp_Trsf flatten;flatten.SetValues(0,1,0,0,0,0,1,0,1,0,0,-station);
        struct Loop {TopoDS_Face Face;SLoopCandidate Candidate;};std::vector<Loop> loops;
        bool valid=wires->Length()>0;
        for(int i=1;i<=wires->Length()&&valid;++i) {
            const auto wire=TopoDS::Wire(BRepBuilderAPI_Transform(wires->Value(i),flatten,true).Shape());
            const auto face=BoundedProjectionFace(wire,tolerance);if(face.IsNull()){valid=false;break;}
            auto candidate=ProjectionLoopCandidate(face,options);if(candidate.Edges.empty()){valid=false;break;}
            for(std::size_t j=0;j<candidate.Edges.size();++j)if(Distance(candidate.Edges[j].End,candidate.Edges[(j+1)%candidate.Edges.size()].Start)>tolerance){valid=false;break;}
            // The face has its own transformed topology; source provenance is
            // recovered by exact curve proximity on the station, never by IDs
            // assigned by an unrelated conversion of another model.
            for(auto& projected:candidate.Edges) {
                const auto point=EvaluateProjectedCurvePoint(projected,.5);if(!point){valid=false;break;}
                const gp_Pnt p(station,point->X,point->Y);double nearest=std::numeric_limits<double>::infinity();
                for(const auto& original:edges){GeomAPI_ProjectPointOnCurve projection(p,original.Curve,original.First,original.Last);
                    if(projection.NbPoints()&&projection.LowerDistance()<nearest){nearest=projection.LowerDistance();projected.SourceFaceId=original.FaceId;}}
                if(nearest>tolerance){valid=false;break;}
            }
            loops.push_back({face,std::move(candidate)});
        }
        if(!valid||loops.empty()){if(options.Progress)options.Progress("nominal rejected invalid source-supported wires");continue;}
        std::sort(loops.begin(),loops.end(),[](const auto& a,const auto& b){return std::abs(a.Candidate.dSignedArea)>std::abs(b.Candidate.dSignedArea);});
        std::string envelopeRejection;
        bool encloses=true;
        for(const auto& [budget,evidence]:boundaryEvidence)
            if(!ProjectionEvidenceFits(loops.front().Face,evidence,budget,false,envelopeRejection)) {
                envelopeRejection+="; work precision plus original boundary uncertainty="+std::to_string(budget)+" mm";encloses=false;break;
            }
        if(!encloses) {
            if(options.Progress)options.Progress("nominal rejected incomplete whole-model envelope: "+envelopeRejection);
            continue;
        }
        // A long local cut floor inside the material must not disqualify a
        // complete section at the short surviving end of a flange. Omitted
        // longer-lived supports matter when a proposed cavity would swallow
        // them, rather than merely because their axial span is greater.
        bool complete=true;
        for(std::size_t group=0;group<supports.size()&&complete;++group)
            if(!used.contains(static_cast<int>(group))&&supports[group].Last-supports[group].First>score+tolerance) {
                std::vector<SSourceProjectionEvidence> omitted;
                for(const auto faceId:supports[group].Faces)for(TopExp_Explorer it(built.Faces.at(faceId),TopAbs_EDGE);it.More();it.Next()) {
                    const auto edge=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(edge))continue;
                    double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);
                    if(!curve.IsNull())omitted.push_back(SourceProjectionEvidence(0,faceId,curve,first,last));
                }
                for(std::size_t cavity=1;cavity<loops.size()&&complete;++cavity) {
                    std::string reason;
                    complete=ProjectionEvidenceFits(loops[cavity].Face,omitted,tolerance,true,reason);
                    if(!complete&&options.Progress)options.Progress("nominal rejected omitted material in cavity: "+reason);
                }
            }
        if(!complete)continue;
        SExactCandidateClassifier enclosing(loops.front().Face,tolerance);
        for(std::size_t i=1;i<loops.size()&&valid;++i)for(const auto& point:loops[i].Candidate.Points)
            if(enclosing.Classify(point)==EPointRelation::Outside){valid=false;break;}
        if(!valid)continue;
        // Independent IGES sheets do not promise outward material normals.
        // Their cavities need finite source-material checks instead of an
        // orientation vote taken from arbitrary sheet occurrences.
        if(solids.IsEmpty()&&loops.size()>1) {
            for(std::size_t i=1;i<loops.size()&&valid;++i) {
                bool certified=false;
                for(const auto& prior:certifiedCavities)
                    if(ProjectionVoidAlreadyRepresented(loops[i].Face,{prior},tolerance)
                        &&ProjectionVoidAlreadyRepresented(prior,{loops[i].Face},tolerance)){certified=true;break;}
                // Both finite boundaries must match. Containment in only one
                // direction cannot certify a smaller, newly proposed void.
                if(certified)continue;
                if(!materialEvidence)materialEvidence=ProjectMaterialFaces(geometry,options,result.Diagnostics,&built);
                std::string reason;
                valid=!CandidateIsMaterialRegion(loops[i].Candidate,*materialEvidence,tolerance,reason,options.Progress,options.nMaximumConcurrency);
                if(!valid&&options.Progress)options.Progress("nominal rejected source material cavity: "+reason);
            }
        }
        if(!valid)continue;
        std::vector<SSectionWire> converted;
        for(std::size_t i=0;i<loops.size();++i) {
            SSectionWire wire;wire.bInner=i!=0;wire.bClosed=true;wire.dSignedArea=loops[i].Candidate.dSignedArea;wire.Edges=std::move(loops[i].Candidate.Edges);
            if((!wire.bInner&&wire.dSignedArea<0)||(wire.bInner&&wire.dSignedArea>0))ReverseWire(wire);
            // A closed internal material boundary must not be invented as a
            // cavity. Check the material side against the original oriented
            // source surfaces, independently of planar wire traversal.
            if(!solids.IsEmpty())for(const auto& edge:wire.Edges) {
                const auto point=EvaluateProjectedCurvePoint(edge,.5);const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(edge.Curve);
                const auto source=built.Faces.find(edge.SourceFaceId);if(!point||curve.IsNull()||source==built.Faces.end()){valid=false;break;}
                gp_Pnt2d p;gp_Vec2d tangent;curve->D1((edge.Range.First+edge.Range.Last)/2,p,tangent);if(edge.bReversed)tangent.Reverse();
                const auto surface=BRep_Tool::Surface(source->second);GeomAPI_ProjectPointOnSurf projection(gp_Pnt(station,point->X,point->Y),surface);
                if(!projection.NbPoints()||projection.LowerDistance()>tolerance){valid=false;break;}
                double u,v;projection.LowerDistanceParameters(u,v);gp_Pnt q;gp_Vec du,dv;surface->D1(u,v,q,du,dv);auto normal=du.Crossed(dv);
                const int occurrence=occurrences.FindIndex(source->second);
                if(!occurrence){valid=false;break;}
                if(occurrences(occurrence).Orientation()==TopAbs_REVERSED)normal.Reverse();
                const gp_Vec direction(0,tangent.X(),tangent.Y());
                if(direction.Dot(gp_Vec(1,0,0).Crossed(normal))<=0){
                    if(options.Progress)options.Progress("nominal rejected material side face="+std::to_string(edge.SourceFaceId)+" inner="+std::to_string(wire.bInner));
                    valid=false;break;
                }
            }
            if(!valid)break;
            StartWireAtLowestEdge(wire,tolerance);converted.push_back(std::move(wire));
        }
        if(!valid)continue;
        if(score>bestScore+tolerance||(std::abs(score-bestScore)<=tolerance&&interval.Last-interval.First>bestWidth)) {
            bestScore=score;bestWidth=interval.Last-interval.First;bestStation=station;bestWires=std::move(converted);
        }
        // Intervals are visited widest first and no minimum support span can
        // exceed the largest support span. This is the proven ranking bound.
        if(bestScore>=maxSupportSpan-tolerance)break;
    }
    if(!bestWires.empty()) {
        result.Wires=std::move(bestWires);result.bSuccess=true;
        result.Diagnostics.push_back("Nominal stock reference recovered from an exact axial station at X="+std::to_string(bestStation)
            +"; every boundary support spans at least "+std::to_string(bestScore)+" mm; complete source envelope retained and no longer-lived support lies inside a cavity");
        result.Diagnostics.push_back("Section junctions use unchanged analytic supports within their original import uncertainty; source faces, trims and classification tolerances are unchanged");
    }
    return result;
}


