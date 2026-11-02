// Pre-clearance from existing CAD boundaries, independent of machining paths.
// Only bounded inner wires on planar faces have an unambiguous trimmed-region
// meaning here. In particular, a periodic surface's noncontractible end wires
// are not holes just because BRepTools::OuterWire chooses one of them.
bool OpeningPlaneBound(const TopoDS_Face& face,double& deviation){
    BRepAdaptor_Surface surface(face,false);deviation=0;
    if(surface.GetType()==GeomAbs_Plane)return true;
    std::vector<gp_Pnt> poles;
    const auto collect=[&](const auto& spline){
        for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v){
            if(!std::isfinite(spline->Weight(u,v))||spline->Weight(u,v)<=0)return false;
            const auto pole=spline->Pole(u,v);if(!std::isfinite(pole.X())||!std::isfinite(pole.Y())||!std::isfinite(pole.Z()))return false;poles.push_back(pole);
        }return true;
    };
    if(surface.GetType()==GeomAbs_BSplineSurface){if(!collect(surface.BSpline()))return false;}
    else if(surface.GetType()==GeomAbs_BezierSurface){if(!collect(surface.Bezier()))return false;}
    else return false;
    if(poles.size()<3)return false;
    const auto origin=poles.front();gp_Vec axis;double longest=0;
    for(const auto& pole:poles){const gp_Vec delta(origin,pole);if(delta.SquareMagnitude()>longest){axis=delta;longest=delta.SquareMagnitude();}}
    if(longest<=1e-14)return false;
    gp_Vec normal;double widest=0;
    for(const auto& pole:poles){const auto cross=axis.Crossed(gp_Vec(origin,pole));if(cross.SquareMagnitude()>widest){normal=cross;widest=cross.SquareMagnitude();}}
    if(widest/longest<=1e-14)return false;
    normal.Normalize();
    // Positive rational basis functions form a convex combination. Checking
    // the complete control net bounds the entire surface, including between
    // knots; evaluating sample points could not provide that guarantee.
    for(const auto& pole:poles)deviation=std::max(deviation,std::abs(gp_Vec(origin,pole).Dot(normal)));
    return std::isfinite(deviation)&&deviation<=1e-7;
}
struct OpeningFace {
    TopoDS_Face Face;
    TopoDS_Wire Outer;
    std::vector<TopoDS_Wire> Inner;
    gp_Pnt Origin;
    gp_Vec Normal;
    double Deviation=0;
    size_t Solid=0;
    bool Planar=false,Selected=false;
    mutable std::optional<Bnd_Box> CachedBounds;
    const Bnd_Box& Bounds()const{
        if(!CachedBounds){Bnd_Box box;BRepBndLib::AddOptimal(Face,box,false,true);CachedBounds=box;}
        return *CachedBounds;
    }
};
// A mouth can lead into an inverted tray rather than through the workpiece.
// Prove that case from the complete shell, not a ray, projected area or size:
// the wall component must have exactly the mouth and ONE selected backing
// face's complete outer wire as its boundary. Reaching that face's inner wire
// is a through-hole wall, not a backing. Any additional exit stays protected.
class OpeningBackingProof {
    const std::vector<OpeningFace>& Faces;
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> Edges;
    std::vector<std::vector<size_t>> EdgeFaces,FaceEdges;
    std::vector<std::vector<int>> OuterEdges;
    static bool Contains(const std::vector<int>& edges,int id){return std::find(edges.begin(),edges.end(),id)!=edges.end();}
public:
    explicit OpeningBackingProof(const std::vector<OpeningFace>& faces):Faces(faces),FaceEdges(faces.size()),OuterEdges(faces.size()){
        for(size_t i=0;i<Faces.size();++i){
            NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> local;TopExp::MapShapes(Faces[i].Face,TopAbs_EDGE,local);
            for(int k=1;k<=local.Extent();++k){const int id=Edges.Add(local(k));FaceEdges[i].push_back(id);if(EdgeFaces.size()<static_cast<size_t>(id))EdgeFaces.resize(id);EdgeFaces[id-1].push_back(i);}
            if(!Faces[i].Outer.IsNull())for(TopExp_Explorer e(Faces[i].Outer,TopAbs_EDGE);e.More();e.Next())OuterEdges[i].push_back(Edges.FindIndex(e.Current()));
        }
    }
    std::vector<size_t> PotentialBackings(size_t mouth,bool allowLower=false)const{
        const auto& start=Faces[mouth];const auto inward=-start.Normal;std::vector<size_t> result;
        for(size_t i=0;i<Faces.size();++i)if(i!=mouth&&Faces[i].Solid==start.Solid&&Faces[i].Selected&&Faces[i].Planar&&!Faces[i].Outer.IsNull()){
            if(Faces[i].Normal.Dot(start.Normal)<1.-1e-10)continue;
            const auto& bounds=Faces[i].Bounds();
            if(bounds.IsVoid()||bounds.IsOpen())continue;double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);double first=1e100,last=-1e100;
            for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1}){const auto distance=gp_Vec(start.Origin,gp_Pnt(x,y,z)).Dot(inward);first=std::min(first,distance);last=std::max(last,distance);}
            // A formed sheet can have its closed backing below the mouth
            // (a downward bead) as well as above it (an inverted tray). The
            // complete wall-boundary proof still has to reach that backing.
            const auto tolerance=start.Deviation+Faces[i].Deviation+1e-7;
            if(first>tolerance||(allowLower&&last<-tolerance))result.push_back(i);
        }
        return result;
    }
    std::optional<size_t> Backing(size_t mouth,const TopoDS_Wire& wire,const std::function<void()>& check)const{
        std::vector<int> entrance;for(TopExp_Explorer e(wire,TopAbs_EDGE);e.More();e.Next()){const int id=Edges.FindIndex(e.Current());if(!id)return {};entrance.push_back(id);}
        if(entrance.empty())return {};
        std::vector<bool> caps(Faces.size(),false);
        for(auto cap:PotentialBackings(mouth,true))caps[cap]=true;
        std::vector<bool> visited(Faces.size(),false);std::vector<size_t> pending;std::vector<int> mouthBoundary,capBoundary;std::optional<size_t> cap;
        const auto visit=[&](size_t face,int edge)->bool{
            if(face==mouth){if(!Contains(entrance,edge))return false;mouthBoundary.push_back(edge);return true;}
            if(caps[face]){
                if(!Contains(OuterEdges[face],edge)||(cap&&*cap!=face))return false;
                cap=face;capBoundary.push_back(edge);return true;
            }
            if(!visited[face]){visited[face]=true;pending.push_back(face);}return true;
        };
        for(int edge:entrance){const auto& adjacent=EdgeFaces[edge-1];if(adjacent.size()!=2)return {};
            for(auto face:adjacent)if(face!=mouth&&!visit(face,edge))return {};
        }
        for(size_t cursor=0;cursor<pending.size();++cursor){check();const auto current=pending[cursor];
            for(int edge:FaceEdges[current]){const auto& adjacent=EdgeFaces[edge-1];
                if(adjacent.size()==1){if(!BRep_Tool::IsClosed(TopoDS::Edge(Edges(edge)),Faces[current].Face))return {};continue;}
                if(adjacent.size()!=2)return {};
                for(auto face:adjacent)if(face!=current&&!visit(face,edge))return {};
            }
        }
        if(!cap)return {};
        const auto same=[](std::vector<int> a,std::vector<int> b){const auto canonical=[](auto& v){std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());};canonical(a);canonical(b);return a==b;};
        if(!same(mouthBoundary,entrance)||!same(capBoundary,OuterEdges[*cap]))return {};
        return cap;
    }
};
// Side holes may connect a tray's inner and outer walls, so the stricter
// annular-shell proof above intentionally fails. This second proof covers the
// COMPLETE entry face: every point must either be in an independently retained
// backing-hole keep-out, or have real material above it in world Z. Sweeping an
// actual trimmed face downward is its exact vertical shadow; it neither fills
// that face's holes nor assumes that an inclined bore is aligned with Z.
bool OpeningShadowCovered(const std::vector<OpeningFace>& faces,size_t mouth,const TopoDS_Wire& wire,size_t backing,
    const std::vector<SectionPlateKeepOut>& protectedHoles,const std::function<void()>& check){
    try{
        BRepAdaptor_Surface surface(faces[mouth].Face,false);if(surface.GetType()!=GeomAbs_Plane)return false;
        BRepBuilderAPI_Copy wireCopy(wire,true,false);if(!wireCopy.IsDone())return false;
        BRepBuilderAPI_MakeFace entry(surface.Plane(),TopoDS::Wire(wireCopy.Shape()),true);if(!entry.IsDone()||!BRepCheck_Analyzer(entry.Face()).IsValid())return false;
        TopoDS_Shape remaining=entry.Face();Bnd_Box entryBounds;BRepBndLib::AddOptimal(remaining,entryBounds,false,true);
        if(entryBounds.IsVoid()||entryBounds.IsOpen())return false;double ex0,ey0,ez0,ex1,ey1,ez1;entryBounds.Get(ex0,ey0,ez0,ex1,ey1,ez1);
        const auto empty=[](const TopoDS_Shape& shape){return !TopExp_Explorer(shape,TopAbs_FACE).More();};
        const auto subtract=[&](const TopoDS_Shape& shape){BRepAlgoAPI_Cut cut;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(remaining);tools.Append(shape);cut.SetArguments(arguments);cut.SetTools(tools);cut.SetNonDestructive(true);cut.Build();if(!cut.IsDone()||!BRepCheck_Analyzer(cut.Shape()).IsValid())return false;remaining=cut.Shape();return true;};
        // These are the original independently validated cells, excluding this
        // mouth. Use only their declared margin, less than the later numerical
        // clearance expansion, so coverage cannot rely on an invented allowance.
        for(const auto& cell:protectedHoles){check();double x0=1e100,y0=x0,z0=x0,x1=-x0,y1=-x0,z1=-x0;
            for(auto p:cell.Vertices){x0=std::min(x0,p[0]);x1=std::max(x1,p[0]);y0=std::min(y0,p[1]);y1=std::max(y1,p[1]);z0=std::min(z0,p[2]);z1=std::max(z1,p[2]);}
            const double m=cell.Margin;
            if(x1+m<ex0||x0-m>ex1||y1+m<ey0||y0-m>ey1||z1+m<ez0||z0-m>ez1)continue;
            if(!subtract(BRepPrimAPI_MakeBox(gp_Pnt(x0-m,y0-m,z0-m),x1-x0+2*m,y1-y0+2*m,z1-z0+2*m).Shape()))return false;
        }
        // A containing hole cannot prove this mouth closed. Otherwise a
        // formed lip and its perforated backing prove each other closed and
        // protect every lip hole again, including the closed forming pockets.
        if(empty(remaining))return false;
        double top=ez1;for(const auto& value:faces)if(value.Solid==faces[mouth].Solid){const auto& bounds=value.Bounds();if(bounds.IsVoid()||bounds.IsOpen())return false;double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);top=std::max(top,z1);}
        const gp_Vec sweep(0,0,-(top-ez0+1.));
        // Try the backing and other planes before complex walls. All sources
        // remain actual trimmed material of the same valid closed solid.
        // Degenerate sweeps contribute no volume and establish no coverage.
        std::vector<size_t> sources{backing};for(int planar=1;planar>=0;--planar)for(size_t i=0;i<faces.size();++i)if(i!=backing&&faces[i].Solid==faces[mouth].Solid&&static_cast<int>(faces[i].Planar)==planar)sources.push_back(i);
        for(auto source:sources){check();const auto& value=faces[source];
            const auto& bounds=value.Bounds();double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
            if(z1<ez0||x1<ex0||x0>ex1||y1<ey0||y0>ey1)continue;
            BRepPrimAPI_MakePrism prism(value.Face,sweep,true);if(!prism.IsDone())continue;
            auto shadow=prism.Shape();if(shadow.ShapeType()!=TopAbs_SOLID)continue;
            if(!BRepCheck_Analyzer(shadow).IsValid())continue;
            BRepClass3d_SolidClassifier material(TopoDS::Solid(shadow));material.PerformInfinitePoint(1e-7);
            if(material.State()==TopAbs_IN)shadow.Reverse();else if(material.State()!=TopAbs_OUT)continue;
            // Failed optional shadows add no coverage. The previous remainder
            // is untouched; another valid material face may still prove it.
            if(!subtract(shadow))continue;if(empty(remaining))return true;
        }
    }catch(const Standard_Failure&){return false;}
    return false;
}
std::pair<size_t,size_t> AddOpeningBoundaryKeepOuts(SectionPlateInput& in,
    const iCAX::OpenCascade::SOpenCascadeBRepBuildResult& built,
    const TopLoc_Location& location,double bottom,double& maximumDeviation,size_t& nonExitBoundaries,const std::function<void()>& check){
    size_t boundaries=0,unclassified=0;
    if(!in.AvoidOpeningBoundaries)return {0,0};
    std::vector<TopoDS_Face> materialFaces;std::vector<size_t> materialSolids;size_t solidIndex=0;bool hasSolids=false;
    for(TopExp_Explorer e(built.Shape,TopAbs_SOLID);e.More();e.Next()){check();hasSolids=true;
        ++solidIndex;
        const auto solid=TopoDS::Solid(e.Current());if(!BRepCheck_Analyzer(solid).IsValid())continue;
        bool closed=true,hasShell=false;for(TopExp_Explorer shell(solid,TopAbs_SHELL);shell.More();shell.Next()){hasShell=true;closed&=BRep_Tool::IsClosed(shell.Current());}if(!closed||!hasShell)continue;
        BRepClass3d_SolidClassifier material(solid);material.PerformInfinitePoint(1e-7);
        if(material.State()!=TopAbs_OUT&&material.State()!=TopAbs_IN)continue;
        // Normalize even an inward-oriented, otherwise valid closed solid.
        for(TopExp_Explorer f(solid,TopAbs_FACE);f.More();f.Next()){auto face=TopoDS::Face(f.Current());if(material.State()==TopAbs_IN)face.Reverse();materialFaces.push_back(face);materialSolids.push_back(solidIndex);}
    }
    std::vector<OpeningFace> faces;
    const auto addFace=[&](const TopoDS_Face& source,size_t solid){
        OpeningFace value;value.Solid=solid;value.Face=TopoDS::Face(source.Moved(location));value.Outer=BRepTools::OuterWire(value.Face);
        for(TopExp_Explorer e(value.Face,TopAbs_WIRE);e.More();e.Next()){const auto wire=TopoDS::Wire(e.Current());if(!wire.IsSame(value.Outer))value.Inner.push_back(wire);}
        try{
            value.Planar=OpeningPlaneBound(value.Face,value.Deviation);
            if(value.Planar){BRepAdaptor_Surface surface(value.Face);gp_Vec du,dv;
                surface.D1((surface.FirstUParameter()+surface.LastUParameter())/2,(surface.FirstVParameter()+surface.LastVParameter())/2,value.Origin,du,dv);
                value.Normal=du.Crossed(dv);if(value.Face.Orientation()==TopAbs_REVERSED)value.Normal.Reverse();
                if(value.Normal.SquareMagnitude()<=1e-24)value.Planar=false;else value.Normal.Normalize();
            }
        }catch(const Standard_Failure&){value.Planar=false;}
        for(auto id:in.FaceIDs){const auto selected=built.Faces.find(id);if(selected!=built.Faces.end()&&selected->second.IsSame(source)){value.Selected=true;break;}}
        faces.push_back(std::move(value));
    };
    if(hasSolids){for(size_t i=0;i<materialFaces.size();++i)addFace(materialFaces[i],materialSolids[i]);}
    else for(const auto& [id,source]:built.Faces)addFace(source,0);
    struct Candidate {size_t FaceIndex;TopoDS_Wire Wire;SectionPlateKeepOut Corridor;};std::vector<Candidate> candidates;
    for(const auto& [id,source]:built.Faces){check();
        const auto world=TopoDS::Face(source.Moved(location));
        const auto found=std::find_if(faces.begin(),faces.end(),[&](const auto& value){return value.Face.IsSame(world);});
        if(found==faces.end()){size_t wires=0;for(TopExp_Explorer e(world,TopAbs_WIRE);e.More();e.Next())++wires;if(wires>1)++unclassified;continue;}
        const auto index=static_cast<size_t>(found-faces.begin());const auto& face=*found;
        if(face.Inner.empty())continue;
        if(!face.Planar){++unclassified;continue;}
        if(hasSolids){
            // This point only orients an already proven planar support; it is
            // not a sampled planarity or passage test. A top basin lip is not
            // a downward material exit. Never clear its entire interior box.
            if(face.Normal.Z()>=-1e-7){nonExitBoundaries+=face.Inner.size();continue;}
        }
        maximumDeviation=std::max(maximumDeviation,face.Deviation);
        if(face.Outer.IsNull())throw std::runtime_error("PLATE_OPENING_OUTER_WIRE_MISSING");
        for(const auto& wire:face.Inner){check();
            if(!BRepCheck_Analyzer(wire).IsValid())throw std::runtime_error("PLATE_OPENING_BOUNDARY_INVALID");
            // Exact trimmed curve extrema, including topology tolerances.
            // No preview polyline or workpiece tessellation enters this bound.
            Bnd_Box box;BRepBndLib::AddOptimal(wire,box,false,true);
            if(box.IsVoid()||box.IsOpen())throw std::runtime_error("PLATE_OPENING_BOUNDARY_UNBOUNDED");
            double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
            SectionPlateKeepOut corridor;corridor.Margin=in.OpeningBoundaryMargin;corridor.AffectsBase=!in.SolidBase;
            const double recessBottom=in.OpeningReliefDepth>0?std::max(bottom,z0-in.OpeningReliefDepth):bottom;
            for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{recessBottom,z1})corridor.Vertices.push_back({x,y,z});
            candidates.push_back({index,wire,std::move(corridor)});
        }
    }
    std::vector<bool> omitted(candidates.size(),false),protectedInner(candidates.size(),false);
    if(hasSolids){
        try{
        const OpeningBackingProof proof(faces);
        for(size_t i=0;i<candidates.size();++i){check();const auto& candidate=candidates[i];
            const auto strict=proof.Backing(candidate.FaceIndex,candidate.Wire,check);
            const auto backings=strict?std::vector<size_t>{*strict}:proof.PotentialBackings(candidate.FaceIndex);
            for(auto backing:backings){
            // A backing may itself be perforated. Every one of those real
            // holes must have an independently validated original keep-out;
            // never trade a broad cavity exclusion for an unprotected hole.
            std::vector<size_t> holes;bool complete=true;
            for(const auto& wire:faces[backing].Inner){const auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.FaceIndex==backing&&c.Wire.IsSame(wire);});
                if(found==candidates.end()){complete=false;break;}holes.push_back(static_cast<size_t>(found-candidates.begin()));}
            if(!complete)continue;
            // A perforated cap cannot certify a mouth lying wholly inside
            // one of its own declared hole corridors, even when the shell
            // traversal reaches its outer boundary via the rest of the part.
            // Reject that circular proof before protecting the cap's holes.
            const auto contains=[&](size_t hole){const auto& cell=candidates[hole].Corridor;
                std::array<double,3> first{1e100,1e100,1e100},last{-1e100,-1e100,-1e100};
                for(auto p:cell.Vertices)for(int k=0;k<3;++k){first[k]=std::min(first[k],p[k]-cell.Margin);last[k]=std::max(last[k],p[k]+cell.Margin);}
                for(auto p:candidate.Corridor.Vertices)for(int k=0;k<3;++k)if(p[k]<first[k]||p[k]>last[k])return false;return true;};
            if(std::any_of(holes.begin(),holes.end(),contains))continue;
            std::vector<SectionPlateKeepOut> protectedHoles;for(auto hole:holes)protectedHoles.push_back(candidates[hole].Corridor);
            if(!strict&&!OpeningShadowCovered(faces,candidate.FaceIndex,candidate.Wire,backing,protectedHoles,check))continue;
            omitted[i]=true;for(auto hole:holes)protectedInner[hole]=true;break;
            }
        }
        }catch(const Standard_Failure&){
            // An unavailable proof changes no opening protection. Cancellation
            // uses the caller's runtime error and is deliberately not caught.
            std::fill(omitted.begin(),omitted.end(),false);std::fill(protectedInner.begin(),protectedInner.end(),false);
        }
    }
    for(size_t i=0;i<candidates.size();++i){if(omitted[i]&&!protectedInner[i]){++nonExitBoundaries;continue;}in.KeepOuts.push_back(std::move(candidates[i].Corridor));++boundaries;}
    return {boundaries,unclassified};
}
