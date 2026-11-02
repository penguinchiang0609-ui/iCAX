#include "pch.h"
#include "GeometryBoundarySweep.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeCancellation.h"
#include "Task/TaskStatus.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Geom2d_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <cmath>
#include <map>
#include <set>

namespace iCAX::Geometry::Detail
{
    std::vector<SBoundaryTermination> AnalyzeBoundaryTerminations(const iCAX::GeometryData::BRepModel& model,
        const std::vector<std::uint64_t>& selectedFaces,const std::vector<SAnalyticBoundary>& boundaries,const SGeometryAnalysisOptions& o)
    {
        if(o.DistanceToleranceMm<=0||!std::isfinite(o.DistanceToleranceMm)||o.AngleToleranceRad<=0||!std::isfinite(o.AngleToleranceRad)||o.MaximumEdgeUses==0)
            throw std::invalid_argument("GEOMETRY_ANALYSIS_INVALID_OPTIONS");
        const auto check=[&]{if(o.IsCancelled&&o.IsCancelled())throw iCAX::Tasks::TaskCanceledException();};check();
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);
        if(!built.bOK||!BRepCheck_Analyzer(built.Shape).IsValid())throw std::invalid_argument("GEOMETRY_SWEEP_INVALID_BREP");
        using IDs=std::set<std::uint64_t>;
        const IDs selected(selectedFaces.begin(),selectedFaces.end());
        if(selected.empty()||selected.size()!=selectedFaces.size())throw std::invalid_argument("GEOMETRY_PATCH_SELECTION_REQUIRED");
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> solids,faceMap,edgeMap;
        TopExp::MapShapes(built.Shape,TopAbs_SOLID,solids);
        std::vector<std::uint64_t> faceIDs(1),edgeIDs(1);
        for(const auto& [id,face]:built.Faces){faceMap.Add(face);faceIDs.push_back(id);}
        for(const auto& [id,edge]:built.Edges){edgeMap.Add(edge);edgeIDs.push_back(id);}
        struct SolidContext{
            bool Valid=true;
            std::map<std::uint64_t,TopoDS_Face> Faces;
            std::map<std::uint64_t,IDs> EdgeFaces,FaceEdges;
        };
        std::vector<SolidContext> contexts(solids.Extent()+1);
        std::map<std::uint64_t,IDs> faceOwners;
        for(int i=1;i<=solids.Extent();++i){check();auto& context=contexts[i];
            BRepClass3d_SolidClassifier classifier(solids(i));classifier.PerformInfinitePoint(o.DistanceToleranceMm);context.Valid=classifier.State()==TopAbs_OUT;
            for(TopExp_Explorer f(solids(i),TopAbs_FACE);f.More();f.Next()){
                const auto index=faceMap.FindIndex(f.Current());if(!index)throw std::runtime_error("GEOMETRY_ANALYSIS_SOURCE_MAPPING_MISSING");
                const auto id=faceIDs.at(index);faceOwners[id].insert(i);
                if(!context.Faces.emplace(id,TopoDS::Face(f.Current())).second)context.Valid=false;
            }
        }
        std::size_t visited=0;
        for(auto& context:contexts){
            for(const auto& [id,_]:context.Faces)if(faceOwners[id].size()!=1)context.Valid=false;
            if(!context.Valid)continue;
            for(const auto& [id,face]:context.Faces)for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){
                check();if(++visited>o.MaximumEdgeUses)throw std::runtime_error("GEOMETRY_ANALYSIS_BUDGET_EXHAUSTED");
                const auto index=edgeMap.FindIndex(e.Current());if(!index)throw std::runtime_error("GEOMETRY_ANALYSIS_SOURCE_MAPPING_MISSING");
                const auto eid=edgeIDs.at(index);context.EdgeFaces[eid].insert(id);context.FaceEdges[id].insert(eid);
            }
        }
        SolidContext* current=nullptr;
        // Sign uses the edge traversal of an outward-oriented material face.
        // Mirroring a face normal or swapping the two faces cannot flip the result.
        auto relation=[&](std::uint64_t eid)->std::string{
            auto& faces=current->Faces;auto& edgeFaces=current->EdgeFaces;
            const auto& owners=edgeFaces[eid];if(owners.size()!=2)return "Unknown";
            const auto& a=faces.at(*owners.begin());const auto& b=faces.at(*owners.rbegin());
            TopoDS_Edge use;for(TopExp_Explorer e(a,TopAbs_EDGE);e.More();e.Next())if(e.Current().IsSame(built.Edges.at(eid))){use=TopoDS::Edge(e.Current());break;}
            if(use.IsNull()||BRep_Tool::Degenerated(use))return "Unknown";
            double first,last,otherFirst,otherLast;const auto pa=BRep_Tool::CurveOnSurface(use,a,first,last),pb=BRep_Tool::CurveOnSurface(use,b,otherFirst,otherLast);
            if(pa.IsNull()||pb.IsNull()||std::abs(first-otherFirst)>o.DistanceToleranceMm||std::abs(last-otherLast)>o.DistanceToleranceMm)return "Unknown";
            const BRepAdaptor_Surface sa(a),sb(b);const BRepAdaptor_Curve curve(use);int sign=0;
            for(double fraction:{.05,.15,.3,.5,.7,.85,.95}){
                check();const auto t=first+(last-first)*fraction;const auto ua=pa->Value(t),ub=pb->Value(t);gp_Pnt p;gp_Vec au,av,bu,bv,tangent;
                sa.D1(ua.X(),ua.Y(),p,au,av);sb.D1(ub.X(),ub.Y(),p,bu,bv);curve.D1(t,p,tangent);
                auto na=au.Crossed(av),nb=bu.Crossed(bv);if(na.SquareMagnitude()<1e-24||nb.SquareMagnitude()<1e-24||tangent.SquareMagnitude()<1e-24)return "Unknown";
                na.Normalize();nb.Normalize();tangent.Normalize();if(a.Orientation()==TopAbs_REVERSED)na.Reverse();if(b.Orientation()==TopAbs_REVERSED)nb.Reverse();if(use.Orientation()==TopAbs_REVERSED)tangent.Reverse();
                const auto angle=na.Crossed(nb).Dot(tangent);if(std::abs(angle)<=std::sin(o.AngleToleranceRad))return "Smooth";
                const int sample=angle>0?1:-1;if(sign&&sign!=sample)return "Mixed";sign=sample;
            }
            return sign>0?"Convex":"Concave";
        };
        auto aggregate=[&](const IDs& edges){std::string result;for(auto id:edges){const auto r=relation(id);if(r=="Unknown"||r=="Smooth"||r=="Mixed")return r;if(!result.empty()&&result!=r)return std::string("Mixed");result=r;}return result.empty()?std::string("Unknown"):result;};
        const auto closedComponent=[&](const IDs& edges){
            NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> vertices;
            std::map<int,std::vector<int>> graph;
            for(auto edge:edges){check();if(!built.Edges.contains(edge))return false;
                TopoDS_Vertex a,b;TopExp::Vertices(built.Edges.at(edge),a,b);
                if(a.IsNull()||b.IsNull())return false;
                const int ia=vertices.Add(a),ib=vertices.Add(b);graph[ia].push_back(ib);graph[ib].push_back(ia);
            }
            if(graph.empty())return false;
            std::set<int> reached;std::vector<int> pending{graph.begin()->first};
            while(!pending.empty()){check();const auto vertex=pending.back();pending.pop_back();if(!reached.insert(vertex).second)continue;
                if(graph[vertex].size()!=2)return false;for(auto other:graph[vertex])if(!reached.contains(other))pending.push_back(other);}
            return reached.size()==graph.size();
        };
        std::vector<SBoundaryTermination> result;
        for(const auto& boundary:boundaries){check();SBoundaryTermination out;IDs entrance,support,side,opposite,terminals;
            for(const auto& span:boundary.Spans){entrance.insert(span.EdgeID);support.insert(span.FaceID);}out.EntranceEdges.assign(entrance.begin(),entrance.end());
            auto finish=[&](const char* issue){if(issue)out.Diagnostics.emplace_back(issue);out.Complete=true;result.push_back(std::move(out));};
            // Each boundary belongs to one material body. Unrelated bodies,
            // free surfaces and curves in the same STEP root do not change its
            // edge relations. Shared/ambiguous face ownership remains unresolved.
            IDs owners;bool unique=!support.empty();
            for(auto face:support){const auto& ids=faceOwners[face];if(ids.size()!=1)unique=false;owners.insert(ids.begin(),ids.end());}
            if(!unique||owners.size()!=1||!contexts[*owners.begin()].Valid){finish("SINGLE_ORIENTED_SOLID_REQUIRED");continue;}
            current=&contexts[*owners.begin()];auto& faces=current->Faces;auto& edgeFaces=current->EdgeFaces;auto& faceEdges=current->FaceEdges;
            if(!boundary.TopologyClosed||!boundary.Closed||entrance.empty()){finish("CLOSED_BOUNDARY_REQUIRED");continue;}
            // The entrance is a closed component of the selected face patch.
            // Internal shared edges cannot be used as side-wall entrances.
            bool valid=entrance.size()==boundary.Spans.size();
            for(const auto& span:boundary.Spans){
                if(!selected.contains(span.FaceID)||!edgeFaces[span.EdgeID].contains(span.FaceID)){valid=false;break;}
                const auto& owners=edgeFaces[span.EdgeID];unsigned inside=0;
                for(auto owner:owners)inside+=selected.contains(owner);
                if(owners.size()!=2||inside!=1){valid=false;break;}
                const BRepAdaptor_Curve curve(built.Edges.at(span.EdgeID));
                if(span.First!=curve.FirstParameter()||span.Last!=curve.LastParameter()){valid=false;break;}
                for(auto face:owners)if(!selected.contains(face))side.insert(face);
            }
            if(!valid){finish("SECTION_NONMANIFOLD_ADJACENCY");continue;}
            if(!closedComponent(entrance)){finish("ENTRANCE_WIRE_COVERAGE_UNRESOLVED");continue;}
            for(auto face:side)for(auto edge:faceEdges[face]){
                if(entrance.contains(edge)||BRep_Tool::IsClosed(built.Edges.at(edge),faces.at(face)))continue;
                const auto& owners=edgeFaces[edge];if(owners.size()!=2){valid=false;continue;}
                unsigned inside=0;for(auto owner:owners)inside+=side.contains(owner);
                if(inside==1){opposite.insert(edge);for(auto owner:owners)if(!side.contains(owner))terminals.insert(owner);}
            }
            out.SideFaces.assign(side.begin(),side.end());out.OppositeEdges.assign(opposite.begin(),opposite.end());out.TerminalFaces.assign(terminals.begin(),terminals.end());
            if(valid&&side.size()==1&&opposite.empty()){
                const auto cap=*side.begin();IDs capEdges;
                for(auto edge:faceEdges[cap])if(!BRep_Tool::Degenerated(built.Edges.at(edge)))capEdges.insert(edge);
                unsigned wires=0;for(TopExp_Explorer wire(faces.at(cap),TopAbs_WIRE);wire.More();wire.Next())++wires;
                if(capEdges==entrance&&wires==1&&BRepAdaptor_Surface(faces.at(cap)).GetType()==GeomAbs_Plane){
                    out.SingleFaceCap=true;out.EntranceRelation=aggregate(entrance);finish(nullptr);continue;
                }
            }
            // A selected back skin is a valid terminal. Only returning to the
            // current entrance's support faces invalidates the side-wall walk.
            if(!valid||opposite.empty()||std::any_of(terminals.begin(),terminals.end(),[&](auto id){return support.contains(id);})){finish("OPPOSITE_BOUNDARY_UNRESOLVED");continue;}
            // A second exit/branch cannot be promoted by a favorable dihedral
            // on only one port. Require one complete opposite wire.
            if(!closedComponent(opposite)){finish("OPPOSITE_BOUNDARY_HAS_BRANCHES");continue;}
            out.EntranceRelation=aggregate(entrance);out.OppositeRelation=aggregate(opposite);
            finish(nullptr);
        }
        return result;
    }
    namespace
    {
        using IDs=std::set<std::uint64_t>;
        gp_Pnt P(const std::array<double,3>& p){return {p[0],p[1],p[2]};}
        gp_Dir D(const std::array<double,3>& p){return {p[0],p[1],p[2]};}
        IDs Edges(const SAnalyticBoundary& b){IDs ids;for(const auto& s:b.Spans)ids.insert(s.EdgeID);return ids;}
        bool SameSection(const SAnalyticBoundary& a,const SAnalyticBoundary& b,const SGeometryAnalysisOptions& o)
        {
            if(a.GeometryKind!=b.GeometryKind||!D(a.Normal).IsParallel(D(b.Normal),o.AngleToleranceRad))return false;
            if(a.GeometryKind=="Circle")return std::abs(a.RadiusMm-b.RadiusMm)<=o.DistanceToleranceMm;
            if(a.GeometryKind=="Slot")return std::abs(a.WidthMm-b.WidthMm)<=o.DistanceToleranceMm
                &&std::abs(a.LengthMm-b.LengthMm)<=o.DistanceToleranceMm&&D(a.XDirection).IsParallel(D(b.XDirection),o.AngleToleranceRad);
            if(a.GeometryKind=="Rectangle")
            {
                const bool same=D(a.XDirection).IsParallel(D(b.XDirection),o.AngleToleranceRad);
                const bool swapped=D(a.XDirection).IsNormal(D(b.XDirection),o.AngleToleranceRad);
                return (same&&std::abs(a.WidthMm-b.WidthMm)<=o.DistanceToleranceMm&&std::abs(a.LengthMm-b.LengthMm)<=o.DistanceToleranceMm)
                    ||(swapped&&std::abs(a.WidthMm-b.LengthMm)<=o.DistanceToleranceMm&&std::abs(a.LengthMm-b.WidthMm)<=o.DistanceToleranceMm);
            }
            return false;
        }
    }
    SBoundarySweepAnalysis AnalyzeBoundarySweep(const IGeometryFeatureAnalysisService& service,
        const iCAX::GeometryData::BRepModel& model,std::uint64_t faceID,const std::vector<std::uint64_t>& requested,
        const SGeometryAnalysisOptions& o)
    {
        const IDs requestedEdges(requested.begin(),requested.end());
        if(requested.empty()||requestedEdges.size()!=requested.size())throw std::invalid_argument("GEOMETRY_SWEEP_BOUNDARY_REQUIRED");
        auto check=[&]{if(o.IsCancelled&&o.IsCancelled())throw iCAX::Tasks::TaskCanceledException();};check();
        iCAX::OpenCascade::COpenCascadeCancellationScope cancellation(o.IsCancelled);
        SBoundarySweepAnalysis out;
        const auto entrance=service.AnalyzeFace(model,faceID,o); // validates options and exact source mapping
        for(const auto& boundary:entrance.Boundaries)if(Edges(boundary)==requestedEdges)out.Entrance=boundary;
        if(out.Entrance.Spans.empty())throw std::invalid_argument("GEOMETRY_SWEEP_BOUNDARY_MISSING");
        auto unresolved=[&](const char* code){out.Diagnostics.emplace_back(code);out.Complete=true;check();return out;};
        if(!entrance.Planar||!out.Entrance.Valid||out.Entrance.GeometryKind=="GeneralContour")return unresolved("ANALYTIC_PLANAR_SECTION_REQUIRED");
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);check();
        if(!built.bOK||!BRepCheck_Analyzer(built.Shape).IsValid())throw std::invalid_argument("GEOMETRY_SWEEP_INVALID_BREP");
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> solids,allFaces,solidFaces;
        TopExp::MapShapes(built.Shape,TopAbs_SOLID,solids);TopExp::MapShapes(built.Shape,TopAbs_FACE,allFaces);
        if(solids.Extent()==0)return unresolved("SURFACE_ONLY");
        if(solids.Extent()!=1)return unresolved("SINGLE_SOLID_REQUIRED");
        TopExp::MapShapes(solids(1),TopAbs_FACE,solidFaces);
        if(allFaces.Extent()!=solidFaces.Extent())return unresolved("FREE_FACES_OUTSIDE_SOLID");
        BRepClass3d_SolidClassifier classifier(solids(1));classifier.PerformInfinitePoint(o.DistanceToleranceMm);
        if(classifier.State()!=TopAbs_OUT)return unresolved("SOLID_ORIENTATION_UNRESOLVED");
        out.SingleSolid=true;
        // The selected trimmed region is material on the outside of this loop.
        // Outer loops of boss tops must never become void/pass-through evidence.
        if(out.Entrance.RegionRelation!="InnerLoop")return unresolved("SECTION_IS_NOT_INNER_REGION");
        std::map<std::uint64_t,TopoDS_Face> faces;
        for(TopExp_Explorer f(solids(1),TopAbs_FACE);f.More();f.Next())
            for(const auto& [id,face]:built.Faces)if(face.IsSame(f.Current()))
            {
                if(faces.contains(id))throw std::invalid_argument("GEOMETRY_SWEEP_AMBIGUOUS_FACE_USE");
                faces.emplace(id,TopoDS::Face(f.Current()));
            }
        if(!faces.contains(faceID))throw std::invalid_argument("GEOMETRY_SWEEP_FACE_NOT_IN_SOLID");
        std::map<std::uint64_t,IDs> edgeFaces,faceEdges;
        std::size_t visited=0;
        for(const auto& [id,face]:faces)for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next())
        {
            check();if(++visited>o.MaximumEdgeUses)throw std::runtime_error("GEOMETRY_ANALYSIS_BUDGET_EXHAUSTED");
            for(const auto& [edgeID,edge]:built.Edges)if(edge.IsSame(e.Current()))
            {edgeFaces[edgeID].insert(id);faceEdges[id].insert(edgeID);break;}
        }
        const gp_Dir axis=D(out.Entrance.Normal);const gp_Pnt origin=P(out.Entrance.Center);
        auto coaxial=[&](const gp_Ax1& a){return a.Direction().IsParallel(axis,o.AngleToleranceRad)
            &&gp_Vec(origin,a.Location()).Crossed(gp_Vec(axis)).Magnitude()<=o.DistanceToleranceMm;};
        auto wallType=[&](std::uint64_t id)->std::string
        {
            const BRepAdaptor_Surface s(faces.at(id));
            if(s.GetType()==GeomAbs_Cylinder)
            {
                if(!s.Cylinder().Axis().Direction().IsParallel(axis,o.AngleToleranceRad))return {};
                if(out.Entrance.GeometryKind=="Circle"&&!coaxial(s.Cylinder().Axis()))return {};
                return "Cylinder";
            }
            if(s.GetType()==GeomAbs_Cone&&out.Entrance.GeometryKind=="Circle"&&coaxial(s.Cone().Axis()))return "Cone";
            if(s.GetType()==GeomAbs_Plane&&out.Entrance.GeometryKind!="Circle"
                &&s.Plane().Axis().Direction().IsNormal(axis,o.AngleToleranceRad))return "Plane";
            return {};
        };
        IDs sideIDs;
        for(const auto edge:requestedEdges)
        {
            if(edgeFaces[edge].size()!=2)return unresolved("SECTION_NONMANIFOLD_ADJACENCY");
            for(const auto f:edgeFaces[edge])if(f!=faceID)
            {if(wallType(f).empty())return unresolved("SIDE_SURFACE_UNSUPPORTED");sideIDs.insert(f);}
        }
        std::vector<std::uint64_t> queue(sideIDs.begin(),sideIDs.end());
        for(size_t q=0;q<queue.size();++q)
        {
            check();const auto f=queue[q];
            for(const auto edge:faceEdges[f])for(const auto other:edgeFaces[edge])
                if(other!=faceID&&!sideIDs.contains(other)&&!wallType(other).empty())
                {sideIDs.insert(other);queue.push_back(other);}
        }
        out.SideFaces.assign(sideIDs.begin(),sideIDs.end());
        const auto walls=service.AnalyzePatch(model,out.SideFaces,o);check();
        if(!walls.Complete||walls.Boundaries.size()!=2)return unresolved("SIDE_WALL_HAS_BRANCHES_OR_UNRESOLVED_PORTS");
        bool foundEntrance=false;
        for(const auto& boundary:walls.Boundaries)
        {
            if(Edges(boundary)==requestedEdges)foundEntrance=true;else out.Terminal=boundary;
        }
        if(!foundEntrance||!out.Terminal.Valid||!out.Terminal.Closed)return unresolved("SIDE_WALL_SECTION_COVERAGE_UNRESOLVED");
        const auto terminalEdges=Edges(out.Terminal);IDs terminalFaces;
        for(const auto edge:terminalEdges)
        {
            if(edgeFaces[edge].size()!=2)return unresolved("TERMINAL_NONMANIFOLD_ADJACENCY");
            for(const auto f:edgeFaces[edge])if(!sideIDs.contains(f))terminalFaces.insert(f);
        }
        out.TerminalFaces.assign(terminalFaces.begin(),terminalFaces.end());
        if(terminalFaces.size()!=1)return unresolved("TERMINAL_REGION_UNRESOLVED");
        const auto terminal=service.AnalyzeFace(model,*terminalFaces.begin(),o);
        bool terminalMatched=false;
        for(const auto& b:terminal.Boundaries)if(Edges(b)==terminalEdges)
        {out.Terminal=b;out.TerminalRelation=b.RegionRelation;terminalMatched=true;}
        if(!terminalMatched||!terminal.Planar||!out.Terminal.Valid)return unresolved("TERMINAL_REGION_UNRESOLVED");
        if(out.TerminalRelation=="OuterLoop"&&terminal.Boundaries.size()!=1)
        {out.TerminalRelation="OuterLoopWithOpenings";return unresolved("STEPPED_OR_CONNECTED_REGION");}
        const gp_Vec delta(origin,P(out.Terminal.Center));out.LengthMm=delta.Magnitude();
        if(out.LengthMm<=o.DistanceToleranceMm||delta.Crossed(gp_Vec(axis)).Magnitude()>o.DistanceToleranceMm
            ||!D(out.Terminal.Normal).IsParallel(axis,o.AngleToleranceRad))return unresolved("SECTION_AXIS_MISMATCH");
        const gp_Dir travel(delta);out.Direction={travel.X(),travel.Y(),travel.Z()};
        std::size_t cones=0;double coneAngle=0;
        for(const auto f:sideIDs)if(wallType(f)=="Cone"){++cones;coneAngle=std::abs(BRepAdaptor_Surface(faces.at(f)).Cone().SemiAngle());}
        TopoDS_Shape swept;
        if(cones)
        {
            // Mixed conical/cylindrical chains contain multiple measurement
            // sections. Keep them unresolved instead of choosing a diameter.
            if(cones!=sideIDs.size()||out.Entrance.GeometryKind!="Circle"||out.Terminal.GeometryKind!="Circle")
                return unresolved("STEPPED_OR_CHAMFERED_REGION");
            const auto firstCone=BRepAdaptor_Surface(faces.at(*sideIDs.begin())).Cone();
            for(const auto f:sideIDs)
            {
                const auto cone=BRepAdaptor_Surface(faces.at(f)).Cone();
                if(cone.Apex().Distance(firstCone.Apex())>o.DistanceToleranceMm
                    ||std::abs(std::abs(cone.SemiAngle())-std::abs(firstCone.SemiAngle()))>o.AngleToleranceRad)
                    return unresolved("MULTIPLE_CONICAL_SECTIONS");
            }
            const double radialDelta=std::abs(out.Entrance.RadiusMm-out.Terminal.RadiusMm);
            if(radialDelta<=o.DistanceToleranceMm||std::abs(std::atan2(radialDelta,out.LengthMm)-coneAngle)>o.AngleToleranceRad)
                return unresolved("CONICAL_SECTION_MISMATCH");
            out.WallGeometry="Conical";out.ConeHalfAngleRad=coneAngle;
            swept=BRepPrimAPI_MakeCone(gp_Ax2(origin,travel),out.Entrance.RadiusMm,out.Terminal.RadiusMm,out.LengthMm).Shape();
        }
        else
        {
            if(!SameSection(out.Entrance,out.Terminal,o))return unresolved("TRANSLATED_SECTION_MISMATCH");
            out.WallGeometry="LinearExtrusion";
            TopoDS_Wire source;
            for(TopExp_Explorer w(faces.at(faceID),TopAbs_WIRE);w.More();w.Next())
            {
                IDs ids;for(TopExp_Explorer e(w.Current(),TopAbs_EDGE);e.More();e.Next())
                    for(const auto& [id,edge]:built.Edges)if(edge.IsSame(e.Current())){ids.insert(id);break;}
                if(ids==requestedEdges){source=TopoDS::Wire(w.Current());break;}
            }
            if(source.IsNull())throw std::runtime_error("GEOMETRY_SWEEP_SOURCE_WIRE_MISSING");
            BRepBuilderAPI_MakeFace section(source,true);
            if(!section.IsDone())return unresolved("SECTION_FACE_CONSTRUCTION_FAILED");
            swept=BRepPrimAPI_MakePrism(section.Face(),delta,true).Shape();
        }
        check();if(swept.IsNull()||!BRepCheck_Analyzer(swept).IsValid())return unresolved("SWEPT_VOLUME_INVALID");
        out.SideWallCoverage=true;
        // Test the WHOLE analytical region, including off-centre obstructions.
        // No volume threshold: even a tiny solid fragment blocks empty-space
        // evidence. A boolean warning/failure is unresolved, never a pass.
        BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> arguments,tools;
        arguments.Append(swept);tools.Append(solids(1));common.SetArguments(arguments);common.SetTools(tools);
        common.SetNonDestructive(true);iCAX::OpenCascade::COpenCascadeCancellationScope::Build(common);check();
        if(!common.IsDone()||common.HasErrors()||common.HasWarnings())return unresolved("SWEPT_REGION_BOOLEAN_UNRESOLVED");
        const auto intersection=common.Shape();
        if(intersection.IsNull()||!BRepCheck_Analyzer(intersection).IsValid())return unresolved("SWEPT_REGION_RESULT_INVALID");
        out.VolumeTestComplete=true;out.SweptInteriorEmpty=!TopExp_Explorer(intersection,TopAbs_SOLID).More();
        if(!out.SweptInteriorEmpty)out.Diagnostics.emplace_back("SWEPT_REGION_CONTAINS_MATERIAL");
        out.Complete=true;check();return out;
    }
}
