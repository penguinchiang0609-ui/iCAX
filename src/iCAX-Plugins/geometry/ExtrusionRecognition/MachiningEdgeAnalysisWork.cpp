#include "pch.h"
#include "MachiningEdgeAnalysisWork.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomProjLib.hxx>
#include <GeomAPI.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Pln.hxx>
#include <sstream>
#include <iomanip>

namespace iCAX::ExtrusionRecognition
{
namespace
{
namespace G = iCAX::GeometryData;
namespace O = iCAX::OpenCascade;
constexpr double kSupportTolerance = 1.0e-7;
constexpr double kAdjacencyTolerance = 1.0e-6;

TopAbs_Orientation Orientation(const G::ETopologyOrientation orientation)
{
    switch (orientation)
    {
    case G::ETopologyOrientation::Reversed: return TopAbs_REVERSED;
    case G::ETopologyOrientation::Internal: return TopAbs_INTERNAL;
    case G::ETopologyOrientation::External: return TopAbs_EXTERNAL;
    default: return TopAbs_FORWARD;
    }
}

struct MaterialOrientation final
{
    std::set<std::uint64_t> AnchoredFaceIds;
    std::set<std::uint64_t> FlippedFaceIds;
};

MaterialOrientation ApplyMaterialOrientation(O::SOpenCascadeBRepBuildResult& built)
{
    MaterialOrientation result;
    TopTools_IndexedMapOfShape identities;
    std::map<int,std::uint64_t> faceIds;
    for(const auto& [id,face]:built.Faces)faceIds[identities.Add(face)]=id;
    std::map<std::uint64_t,TopAbs_Orientation> materialOrientations;
    std::set<std::uint64_t> conflicts;
    for(TopExp_Explorer solids(built.Shape,TopAbs_SOLID);solids.More();solids.Next())
    {
        auto solid=solids.Current();bool closed=true;std::size_t shellCount=0;
        for(TopExp_Explorer shells(solid,TopAbs_SHELL);shells.More();shells.Next())
        {
            ++shellCount;closed=closed&&BRep_Tool::IsClosed(shells.Current());
        }
        if(!closed||!shellCount)continue;
        BRepClass3d_SolidClassifier classifier(solid);classifier.PerformInfinitePoint(kSupportTolerance);
        if(classifier.State()==TopAbs_IN)solid.Reverse();
        else if(classifier.State()!=TopAbs_OUT)continue;
        for(TopExp_Explorer faces(solid,TopAbs_FACE);faces.More();faces.Next())
        {
            const int index=identities.FindIndex(faces.Current());
            if(!index)continue;
            const auto id=faceIds.at(index);
            const auto orientation=faces.Current().Orientation();
            if(orientation!=TopAbs_FORWARD&&orientation!=TopAbs_REVERSED)continue;
            if(materialOrientations.contains(id)&&materialOrientations.at(id)!=orientation)conflicts.insert(id);
            else materialOrientations[id]=orientation;
        }
    }
    for(const auto& [id,orientation]:materialOrientations)if(!conflicts.contains(id))
    {
        result.AnchoredFaceIds.insert(id);
        if(built.Faces.at(id).Orientation()!=orientation)result.FlippedFaceIds.insert(id);
        built.Faces.at(id).Orientation(orientation);
    }
    return result;
}

struct SectionCurve final
{
    Handle(Geom2d_Curve) Curve;
    double First = 0.0;
    double Last = 0.0;
};

double SkinDistance(const gp_Pnt& point, const std::vector<SectionCurve>& section)
{
    const gp_Pnt2d projected(point.Y(), point.Z());
    double distance = (std::numeric_limits<double>::max)();
    for (const auto& edge : section)
    {
        distance = std::min({ distance, projected.Distance(edge.Curve->Value(edge.First)),
            projected.Distance(edge.Curve->Value(edge.Last)) });
        Geom2dAPI_ProjectPointOnCurve project(projected, edge.Curve, edge.First, edge.Last);
        if (project.NbPoints()) distance = std::min(distance, project.LowerDistance());
    }
    return distance;
}

#include "MachiningEdgeSkinSpans.inc"

bool Plane(const TopoDS_Face& face, gp_Pln& plane)
{
    BRepAdaptor_Surface surface(face, true);
    if (surface.GetType() == GeomAbs_Plane)
    {
        plane = surface.Plane();
        return true;
    }
    GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face), kSupportTolerance);
    if (!planar.IsPlanar()) return false;
    plane = planar.Plan();
    return true;
}

// Shared source coedges are a membership proof only when the other support
// really is stock skin. Recess floors are non-machining too, so membership in
// the NonMachining partition alone is deliberately insufficient.
bool IsSourceSkinFace(const TopoDS_Face& face, const std::vector<SectionCurve>& section)
{
    BRepAdaptor_Surface surface(face, true);
    gp_Pln plane;
    if (Plane(face, plane))
    {
        if (std::abs(plane.Axis().Direction().X()) > 1.0e-10) return false;
    }
    else if (surface.GetType() == GeomAbs_Cylinder)
    {
        if (std::abs(surface.Cylinder().Axis().Direction().X()) < 1.0 - 1.0e-10) return false;
    }
    else return false;

    std::size_t samples = 0;
    for (TopExp_Explorer edges(face, TopAbs_EDGE); edges.More(); edges.Next())
    {
        const auto edge = TopoDS::Edge(edges.Current());
        if (BRep_Tool::Degenerated(edge)) continue;
        BRepAdaptor_Curve2d curve(edge, face);
        const double first = curve.FirstParameter(), last = curve.LastParameter();
        if (!std::isfinite(first) || !std::isfinite(last)) return false;
        for (std::size_t i = 0; i < 17; ++i)
        {
            const auto uv = curve.Value(first + (last - first) * double(i) / 16.0);
            gp_Pnt point;
            surface.D0(uv.X(), uv.Y(), point);
            if (SkinDistance(point, section) > kSupportTolerance) return false;
            ++samples;
        }
    }
    return samples != 0;
}

bool NormalAt(const TopoDS_Face& face, const TopoDS_Edge& edge,
    const double parameter, const gp_Pnt& point, gp_Vec& normal)
{
    BRepAdaptor_Surface surface(face, true);
    gp_Pnt2d uv;
    bool hasUv = false;
    if (BRep_Tool::SameParameter(edge))
    {
        try
        {
            BRepAdaptor_Curve2d curve(edge, face);
            if (parameter >= curve.FirstParameter() - 1e-12
                && parameter <= curve.LastParameter() + 1e-12)
            {
                uv = curve.Value(parameter);
                hasUv = true;
            }
        }
        catch (const Standard_Failure&) {}
    }
    if (!hasUv)
    {
        GeomAPI_ProjectPointOnSurf project(point, BRep_Tool::Surface(face));
        if (!project.NbPoints()) return false;
        double u = 0.0, v = 0.0;
        project.LowerDistanceParameters(u, v);
        uv.SetCoord(u, v);
    }
    gp_Pnt evaluated;
    gp_Vec du, dv;
    surface.D1(uv.X(), uv.Y(), evaluated, du, dv);
    normal = du.Crossed(dv);
    if (normal.SquareMagnitude() <= 1e-24) return false;
    normal.Normalize();
    if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
    return face.Orientation() == TopAbs_FORWARD || face.Orientation() == TopAbs_REVERSED;
}

TopoDS_Edge FaceUse(const TopoDS_Face& face, const TopoDS_Edge& edge)
{
    for (TopExp_Explorer uses(face, TopAbs_EDGE); uses.More(); uses.Next())
        if (uses.Current().IsSame(edge)) return TopoDS::Edge(uses.Current());
    return {};
}

bool InsetSurfacePoint(const TopoDS_Face& face,const TopoDS_Edge& edge,
    const double parameter,const gp_Pnt& edgePoint,const gp_Vec& inward,
    const double distance,gp_Pnt& result)
{
    BRepAdaptor_Surface surface(face,true);gp_Pnt2d uv;bool located=false;
    if(BRep_Tool::SameParameter(edge))
    {
        try
        {
            BRepAdaptor_Curve2d curve(edge,face);
            if(parameter>=curve.FirstParameter()-1e-12&&parameter<=curve.LastParameter()+1e-12)
            {uv=curve.Value(parameter);located=true;}
        }
        catch(const Standard_Failure&){}
    }
    if(!located)
    {
        GeomAPI_ProjectPointOnSurf project(edgePoint,BRep_Tool::Surface(face));if(!project.NbPoints())return false;
        double u=0,v=0;project.LowerDistanceParameters(u,v);uv.SetCoord(u,v);
    }
    gp_Pnt point;gp_Vec du,dv;surface.D1(uv.X(),uv.Y(),point,du,dv);
    const double aa=du.Dot(du),bb=du.Dot(dv),cc=dv.Dot(dv),determinant=aa*cc-bb*bb;
    if(determinant<=1e-24)return false;
    const double uStep=(cc*inward.Dot(du)-bb*inward.Dot(dv))/determinant;
    const double vStep=(aa*inward.Dot(dv)-bb*inward.Dot(du))/determinant;
    const gp_Pnt2d shifted(uv.X()+uStep*distance,uv.Y()+vStep*distance);
    BRepClass_FaceClassifier classify(face,shifted,1e-8);
    if(classify.State()!=TopAbs_IN)return false;
    surface.D0(shifted.X(),shifted.Y(),result);return true;
}

double InteriorExtent(const TopoDS_Face& face,const gp_Pnt& point,const gp_Vec& inward)
{
    Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
    double result=0;
    for(const double a:{x,X})for(const double b:{y,Y})for(const double c:{z,Z})
        result=std::max(result,gp_Vec(point,gp_Pnt(a,b,c)).Dot(inward));
    return result;
}

bool OpposedNormalMaterialLimit(const TopoDS_Face& firstFace,const TopoDS_Edge& firstUse,
    const double firstParameter,const gp_Pnt& firstPoint,
    const TopoDS_Face& secondFace,const TopoDS_Edge& secondUse,
    const double secondParameter,const gp_Pnt& secondPoint,
    const gp_Vec& anchorNormal,const gp_Vec& anchorTangent,const double gapBudget,
    double& signedAngle,std::string& evidence)
{
    auto inward=anchorNormal.Crossed(anchorTangent);if(inward.SquareMagnitude()<1e-24)return false;inward.Normalize();
    const double extent=std::min(InteriorExtent(firstFace,firstPoint,inward),InteriorExtent(secondFace,secondPoint,inward));
    if(!std::isfinite(extent)||extent<=0)return false;
    const double uncertainty=std::max(1e-10,gapBudget*8);
    int stableSign=0,validProbes=0;std::ostringstream details;details<<std::setprecision(10);
    for(const double fraction:{1e-4,1e-3,1e-2,1e-1,.25})
    {
        const double distance=extent*fraction;gp_Pnt a,b;
        if(!InsetSurfacePoint(firstFace,firstUse,firstParameter,firstPoint,inward,distance,a)
            ||!InsetSurfacePoint(secondFace,secondUse,secondParameter,secondPoint,inward,distance,b))continue;
        const double materialSign=anchorNormal.Dot(gp_Vec(a,b));
        if(std::abs(materialSign)<=uncertainty)continue;
        const int sign=materialSign<0?-1:1;
        if(stableSign&&sign!=stableSign)return false;
        stableSign=sign;++validProbes;details<<" offset="<<distance<<"mm sign="<<materialSign<<"mm;";
        if(validProbes>=2)
        {
            // Two trim-interior surface branches leave the common tangent in
            // the same direction. Negative separation lies on the anchor's
            // material side: a zero-angle convex knife edge. Positive is the
            // complementary reentrant material limit. Swapping faces reverses
            // both the material normal and the point difference.
            signedAngle=stableSign<0?3.14159265358979323846:-3.14159265358979323846;
            evidence=details.str();return true;
        }
    }
    return false;
}

struct FiniteEdge final
{
    std::uint64_t Id = 0;
    Handle(Geom_Curve) Curve;
    double First = 0.0, Last = 0.0;
    gp_Pnt Start, End;
    bool Closed = false;
    std::array<double,3> Minimum{}, Maximum{};
};

struct EdgeProjection final
{
    double Parameter = 0.0;
    double Distance = 0.0;
};

FiniteEdge ReadFinite(const TopoDS_Edge& edge,const std::uint64_t id)
{
    FiniteEdge result;result.Id=id;result.Curve=BRep_Tool::Curve(edge,result.First,result.Last);
    if(result.Curve.IsNull()||!std::isfinite(result.First)||!std::isfinite(result.Last)||result.First>=result.Last)
        throw std::runtime_error("Invalid finite source edge E"+std::to_string(id));
    result.Start=result.Curve->Value(result.First);result.End=result.Curve->Value(result.Last);
    result.Closed=result.Start.Distance(result.End)<=kAdjacencyTolerance;
    Bnd_Box box;BRepBndLib::AddOptimal(edge,box,false,false);
    box.Get(result.Minimum[0],result.Minimum[1],result.Minimum[2],result.Maximum[0],result.Maximum[1],result.Maximum[2]);
    return result;
}

FiniteEdge RestrictFinite(FiniteEdge edge,const double first,const double last)
{
    edge.First=first;edge.Last=last;edge.Start=edge.Curve->Value(first);edge.End=edge.Curve->Value(last);
    edge.Closed=edge.Start.Distance(edge.End)<=kAdjacencyTolerance;return edge;
}

EdgeProjection Project(const gp_Pnt& point, const FiniteEdge& target)
{
    EdgeProjection result{target.First,point.Distance(target.Start)};
    const double endDistance=point.Distance(target.End);
    if(endDistance<result.Distance)result={target.Last,endDistance};
    GeomAPI_ProjectPointOnCurve project(point,target.Curve,target.First,target.Last);
    if(project.NbPoints()&&project.LowerDistance()<result.Distance)
        result={project.LowerDistanceParameter(),project.LowerDistance()};
    return result;
}

bool BoxesIntersect(const std::array<double,3>& a,const std::array<double,3>& A,
    const std::array<double,3>& b,const std::array<double,3>& B)
{
    for(std::size_t axis=0;axis<3;++axis)
        if(a[axis]>B[axis]+kAdjacencyTolerance||b[axis]>A[axis]+kAdjacencyTolerance)return false;
    return true;
}

// Read-only broad phase: long edges do not explode a spatial hash into many
// cells, and unrelated whole-model edges never reach curve projection.
class EdgeBoxTree final
{
    struct Node final
    {
        std::array<double,3> Minimum{},Maximum{};
        std::size_t Begin=0,End=0;
        int Left=-1,Right=-1;
    };
    const std::vector<FiniteEdge>& Edges;
    std::vector<std::size_t> Order;
    std::vector<Node> Nodes;
    int Build(const std::size_t begin,const std::size_t end)
    {
        Node node;node.Begin=begin;node.End=end;
        node.Minimum=Edges[Order[begin]].Minimum;node.Maximum=Edges[Order[begin]].Maximum;
        for(std::size_t i=begin+1;i<end;++i)for(std::size_t axis=0;axis<3;++axis)
        {
            node.Minimum[axis]=std::min(node.Minimum[axis],Edges[Order[i]].Minimum[axis]);
            node.Maximum[axis]=std::max(node.Maximum[axis],Edges[Order[i]].Maximum[axis]);
        }
        const int index=static_cast<int>(Nodes.size());Nodes.push_back(node);
        if(end-begin<=8)return index;
        std::size_t axis=0;
        for(std::size_t i=1;i<3;++i)if(node.Maximum[i]-node.Minimum[i]>node.Maximum[axis]-node.Minimum[axis])axis=i;
        const auto middle=begin+(end-begin)/2;
        std::nth_element(Order.begin()+begin,Order.begin()+middle,Order.begin()+end,[&](const auto a,const auto b)
            {return Edges[a].Minimum[axis]+Edges[a].Maximum[axis]<Edges[b].Minimum[axis]+Edges[b].Maximum[axis];});
        const int left=Build(begin,middle),right=Build(middle,end);
        Nodes[index].Left=left;Nodes[index].Right=right;
        return index;
    }
    void Query(const int index,const std::size_t edgeIndex,std::vector<std::size_t>& found)const
    {
        const auto& node=Nodes[index];const auto& edge=Edges[edgeIndex];
        if(!BoxesIntersect(node.Minimum,node.Maximum,edge.Minimum,edge.Maximum))return;
        if(node.Left<0)
        {
            for(std::size_t i=node.Begin;i<node.End;++i)
                if(Order[i]>edgeIndex&&BoxesIntersect(Edges[Order[i]].Minimum,Edges[Order[i]].Maximum,edge.Minimum,edge.Maximum))
                    found.push_back(Order[i]);
        }
        else {Query(node.Left,edgeIndex,found);Query(node.Right,edgeIndex,found);}
    }
public:
    explicit EdgeBoxTree(const std::vector<FiniteEdge>& edges):Edges(edges)
    {
        for(std::size_t i=0;i<edges.size();++i)Order.push_back(i);
        if(!Order.empty())Build(0,Order.size());
    }
    std::vector<std::size_t> Candidates(const std::size_t index)const
    {
        std::vector<std::size_t> result;if(!Nodes.empty())Query(0,index,result);return result;
    }
};

bool FullyEquivalent(const FiniteEdge& a,const FiniteEdge& b,double& maxGap)
{
    if(a.Closed!=b.Closed)return false;
    if(!a.Closed&&!((a.Start.Distance(b.Start)<=kAdjacencyTolerance&&a.End.Distance(b.End)<=kAdjacencyTolerance)
        ||(a.Start.Distance(b.End)<=kAdjacencyTolerance&&a.End.Distance(b.Start)<=kAdjacencyTolerance)))return false;
    for(std::size_t axis=0;axis<3;++axis)
        if(std::abs(a.Minimum[axis]-b.Minimum[axis])>kAdjacencyTolerance
            ||std::abs(a.Maximum[axis]-b.Maximum[axis])>kAdjacencyTolerance)return false;
    maxGap=0.0;
    for(const auto& pair:{std::pair{&a,&b},std::pair{&b,&a}})
        for(std::size_t i=0;i<=32;++i)
        {
            const auto point=pair.first->Curve->Value(pair.first->First+(pair.first->Last-pair.first->First)*double(i)/32.0);
            const auto projection=Project(point,*pair.second);
            maxGap=std::max(maxGap,projection.Distance);
            if(projection.Distance>kAdjacencyTolerance)return false;
        }
    return true;
}

bool PartiallyOverlaps(const FiniteEdge& a,const FiniteEdge& b)
{
    std::vector<double> parameters;
    for(const auto& point:{a.Start,a.End})
        if(Project(point,b).Distance<=kAdjacencyTolerance)
            parameters.push_back(point.Distance(a.Start)<=kAdjacencyTolerance?a.First:a.Last);
    for(const auto& point:{b.Start,b.End})
    {
        const auto projection=Project(point,a);
        if(projection.Distance<=kAdjacencyTolerance)parameters.push_back(projection.Parameter);
    }
    if(parameters.size()<2)return false;
    std::sort(parameters.begin(),parameters.end());
    const auto validInterval=[&](const double first,const double last)
    {
        if(std::abs(first-last)<=1e-13)return false;
        const auto p=a.Curve->Value(first),q=a.Curve->Value(last);
        if(p.Distance(q)<=kAdjacencyTolerance*16)return false;
        for(int i=1;i<8;++i)
            if(Project(a.Curve->Value(first+(last-first)*double(i)/8.0),b).Distance>kAdjacencyTolerance)return false;
        return true;
    };
    for(std::size_t i=1;i<parameters.size();++i)
        if(validInterval(parameters[i-1],parameters[i]))return true;
    return false;
}

struct OwnerUse final
{
    std::uint64_t EdgeId=0;
    std::size_t Count=0;
};

bool SamePlane(const TopoDS_Face& a,const TopoDS_Face& b)
{
    gp_Pln first,second;
    return Plane(a,first)&&Plane(b,second)
        &&first.Distance(second.Location())<=kAdjacencyTolerance
        &&std::abs(first.Axis().Direction().Dot(second.Axis().Direction()))>=1.0-1e-12;
}

int PlanarInteriorSides(const TopoDS_Face& a,const TopoDS_Edge& aEdge,
    const TopoDS_Face& b,const TopoDS_Edge& bEdge,const FiniteEdge& first,const FiniteEdge& second)
{
    if(!SamePlane(a,b))return 0;
    const auto aUse=FaceUse(a,aEdge),bUse=FaceUse(b,bEdge);
    if(aUse.IsNull()||bUse.IsNull())return 0;
    const double parameter=(first.First+first.Last)/2.0;
    gp_Pnt point;gp_Vec ta,tb,na,nb;first.Curve->D1(parameter,point,ta);
    const auto projection=Project(point,second);
    gp_Pnt q;second.Curve->D1(projection.Parameter,q,tb);
    if(!NormalAt(a,aUse,parameter,point,na)||!NormalAt(b,bUse,projection.Parameter,q,nb))return 0;
    if(aUse.Orientation()==TopAbs_REVERSED)ta.Reverse();
    if(bUse.Orientation()==TopAbs_REVERSED)tb.Reverse();
    auto insideA=na.Crossed(ta),insideB=nb.Crossed(tb);
    if(insideA.SquareMagnitude()<1e-24||insideB.SquareMagnitude()<1e-24)return 0;
    insideA.Normalize();insideB.Normalize();
    const double relation=insideA.Dot(insideB);
    return relation>.99?1:relation<-.99?-1:0;
}

struct ResolvedAdjacency final
{
    std::map<std::uint64_t,OwnerUse> Owners;
    std::vector<std::uint64_t> Matched,Partial,Rejected;
    double MaxGap=0.0;
};

std::map<std::uint64_t,ResolvedAdjacency> ResolveAdjacency(
    const std::map<std::uint64_t,std::map<std::uint64_t,std::size_t>>& owners,
    const std::map<std::uint64_t,TopoDS_Edge>& nativeEdges,
    const std::map<std::uint64_t,TopoDS_Face>& faces)
{
    std::vector<FiniteEdge> edges;
    for(const auto& [id,uses]:owners)
    {
        const auto& edge=nativeEdges.at(id);
        if(BRep_Tool::Degenerated(edge))continue;
        edges.push_back(ReadFinite(edge,id));
    }
    std::vector<std::size_t> parent;std::vector<double> gaps(edges.size(),0.0);
    std::vector<std::vector<std::size_t>> componentMembers;
    for(std::size_t i=0;i<edges.size();++i){parent.push_back(i);componentMembers.push_back({i});}
    const auto root=[&](std::size_t i){while(parent[i]!=i)i=parent[i];return i;};
    std::map<std::uint64_t,std::set<std::uint64_t>> partial,rejected;
    struct Candidate final {std::size_t First=0,Second=0;double Gap=0.0;};
    std::vector<Candidate> completeCandidates;
    EdgeBoxTree tree(edges);
    for(std::size_t i=0;i<edges.size();++i)
        for(const auto j:tree.Candidates(i))
        {
            const auto& a=edges[i];const auto& b=edges[j];
            bool sharedFace=false;
            for(const auto& [face,count]:owners.at(a.Id))sharedFace=sharedFace||owners.at(b.Id).contains(face);
            if(sharedFace)continue;
            double gap=0.0;const bool full=FullyEquivalent(a,b,gap);
            if(!full&&!PartiallyOverlaps(a,b))continue;
            if(owners.at(a.Id).size()>=2||owners.at(b.Id).size()>=2)
            {
                rejected[a.Id].insert(b.Id);rejected[b.Id].insert(a.Id);continue;
            }
            bool onlySameSide=true;
            for(const auto& [fa,ca]:owners.at(a.Id))for(const auto& [fb,cb]:owners.at(b.Id))
                onlySameSide=onlySameSide&&PlanarInteriorSides(faces.at(fa),nativeEdges.at(a.Id),faces.at(fb),nativeEdges.at(b.Id),a,b)==1;
            if(onlySameSide)
            {
                rejected[a.Id].insert(b.Id);rejected[b.Id].insert(a.Id);continue;
            }
            if(full)
            {
                completeCandidates.push_back({i,j,gap});
            }
            else {partial[a.Id].insert(b.Id);partial[b.Id].insert(a.Id);}
        }
    // All pairwise exclusions are known before any transitive union. A third
    // coincident edge may not reconnect two already-rejected same-side edges.
    for(const auto& candidate:completeCandidates)
    {
        const auto aRoot=root(candidate.First),bRoot=root(candidate.Second);
        if(aRoot==bRoot)continue;
        bool permitted=true;
        for(const auto i:componentMembers[aRoot])
            for(const auto j:componentMembers[bRoot])
            {
                if(rejected[edges[i].Id].contains(edges[j].Id))permitted=false;
                for(const auto& [face,count]:owners.at(edges[i].Id))
                    if(owners.at(edges[j].Id).contains(face))permitted=false;
            }
        if(!permitted)continue;
        parent[bRoot]=aRoot;
        componentMembers[aRoot].insert(componentMembers[aRoot].end(),componentMembers[bRoot].begin(),componentMembers[bRoot].end());
        componentMembers[bRoot].clear();
        gaps[candidate.First]=std::max(gaps[candidate.First],candidate.Gap);
        gaps[candidate.Second]=std::max(gaps[candidate.Second],candidate.Gap);
    }
    std::map<std::size_t,std::vector<std::size_t>> classes;
    for(std::size_t i=0;i<edges.size();++i)classes[root(i)].push_back(i);
    std::map<std::uint64_t,ResolvedAdjacency> result;
    for(const auto& [classId,members]:classes)
    {
        ResolvedAdjacency resolved;std::set<std::uint64_t> partialIds,rejectedIds;
        for(const auto index:members)
        {
            const auto id=edges[index].Id;resolved.Matched.push_back(id);resolved.MaxGap=std::max(resolved.MaxGap,gaps[index]);
            partialIds.insert(partial[id].begin(),partial[id].end());rejectedIds.insert(rejected[id].begin(),rejected[id].end());
            for(const auto& [face,count]:owners.at(id))
            {
                auto& use=resolved.Owners[face];if(!use.EdgeId)use.EdgeId=id;use.Count+=count;
            }
        }
        for(const auto id:resolved.Matched)partialIds.erase(id);
        resolved.Partial={partialIds.begin(),partialIds.end()};resolved.Rejected={rejectedIds.begin(),rejectedIds.end()};
        for(const auto id:resolved.Matched)result[id]=resolved;
    }
    for(const auto& [id,uses]:owners)if(!result.contains(id))
    {
        auto& resolved=result[id];resolved.Matched={id};
        for(const auto& [face,count]:uses)resolved.Owners[face]={id,count};
    }
    return result;
}

double EventTolerance(const FiniteEdge& edge)
{
    return std::max(1.0e-12,(edge.Last-edge.First)*1.0e-10);
}

void NormalizeEvents(std::vector<double>& events,const FiniteEdge& edge)
{
    events.push_back(edge.First);events.push_back(edge.Last);
    for(auto& value:events)value=std::clamp(value,edge.First,edge.Last);
    std::sort(events.begin(),events.end());
    const double epsilon=EventTolerance(edge);
    const auto sameEvent=[&](const double first,const double last)
    {
        if(std::abs(first-last)<=epsilon)return true;
        const auto anchor=edge.Curve->Value(first);
        if(anchor.Distance(edge.Curve->Value(last))>kAdjacencyTolerance)return false;
        // The same world endpoint can occur at both ends of a closed loop.
        // Only collapse its parameter aliases when the intervening arc also
        // stays inside the correspondence budget.
        for(const double fraction:{.25,.5,.75})
            if(anchor.Distance(edge.Curve->Value(first+(last-first)*fraction))>kAdjacencyTolerance)return false;
        return true;
    };
    std::vector<double> normalized{edge.First};
    for(const auto value:events)
    {
        if(value<=edge.First+epsilon||value>=edge.Last-epsilon)continue;
        if(sameEvent(normalized.back(),value)||sameEvent(value,edge.Last))continue;
        normalized.push_back(value);
    }
    // Retain both native range endpoints even for a very short valid edge.
    normalized.push_back(edge.Last);events=std::move(normalized);
}

void PropagateEvents(std::map<std::uint64_t,std::vector<double>>& events,
    const std::map<std::uint64_t,FiniteEdge>& finite,
    const std::map<std::uint64_t,ResolvedAdjacency>& adjacency)
{
    for(int iteration=0;iteration<4;++iteration)
    {
        bool changed=false;
        for(const auto& [id,source]:finite)
        {
            auto& output=events.at(id);const auto before=output.size();
            std::set<std::uint64_t> targets(adjacency.at(id).Matched.begin(),adjacency.at(id).Matched.end());
            targets.insert(adjacency.at(id).Partial.begin(),adjacency.at(id).Partial.end());
            for(const auto targetId:targets)if(targetId!=id&&events.contains(targetId))
                for(const auto parameter:events.at(targetId))
                {
                    const auto projection=Project(finite.at(targetId).Curve->Value(parameter),source);
                    if(projection.Distance<=kAdjacencyTolerance)output.push_back(projection.Parameter);
                }
            NormalizeEvents(output,source);changed=changed||before!=output.size();
        }
        if(!changed)break;
    }
}

bool SpanContained(const FiniteEdge& span,const FiniteEdge& target,double& maxGap)
{
    for(int i=0;i<=8;++i)
    {
        const auto projection=Project(span.Curve->Value(span.First+(span.Last-span.First)*double(i)/8.0),target);
        if(projection.Distance>kAdjacencyTolerance)return false;
        maxGap=std::max(maxGap,projection.Distance);
    }
    return true;
}

ResolvedAdjacency ActiveSpanAdjacency(const FiniteEdge& span,
    const std::map<std::uint64_t,FiniteEdge>& finite,
    const std::map<std::uint64_t,ResolvedAdjacency>& resolved,
    const std::map<std::uint64_t,std::map<std::uint64_t,std::size_t>>& owners)
{
    const auto& source=resolved.at(span.Id);
    ResolvedAdjacency result;std::set<std::uint64_t> selected(source.Matched.begin(),source.Matched.end());
    std::set<std::uint64_t> candidates(source.Partial.begin(),source.Partial.end());
    for(const auto target:candidates)
    {
        if(!finite.contains(target))continue;
        double gap=0.0;if(!SpanContained(span,finite.at(target),gap))continue;
        bool permitted=true;
        for(const auto existing:selected)
        {
            const auto& rejected=resolved.at(existing).Rejected;
            if(std::find(rejected.begin(),rejected.end(),target)!=rejected.end())permitted=false;
            for(const auto& [face,count]:owners.at(existing))if(owners.at(target).contains(face))permitted=false;
        }
        if(!permitted)continue;
        for(const auto matched:resolved.at(target).Matched)selected.insert(matched);
        result.MaxGap=std::max(result.MaxGap,gap);
    }
    result.Matched={selected.begin(),selected.end()};result.Rejected=source.Rejected;
    for(const auto id:selected)
    {
        double gap=0.0;SpanContained(span,finite.at(id),gap);result.MaxGap=std::max(result.MaxGap,gap);
        for(const auto& [face,count]:owners.at(id))
        {
            auto& owner=result.Owners[face];if(!owner.EdgeId)owner.EdgeId=id;owner.Count+=count;
        }
    }
    return result;
}

std::set<std::uint64_t> ProtectNativeMaterialPairsOnConflict(
    std::map<std::uint64_t,ResolvedAdjacency>& resolved,
    const std::map<std::uint64_t,FiniteEdge>& finite,
    const std::map<std::uint64_t,std::map<std::uint64_t,std::size_t>>& machiningOwners,
    const std::map<std::uint64_t,std::set<std::uint64_t>>& allOwners)
{
    // Geometric recovery must not erase a complete native material pairing.
    // Detect the conflict on real covered intervals first, so every already
    // valid <=2-owner analysis follows exactly the previous successful path.
    bool possibleConflict=false;
    for(const auto& [id,adjacency]:resolved)
    {
        if(machiningOwners.at(id).size()!=1||allOwners.at(id).size()!=2)continue;
        std::set<std::uint64_t> possibleFaces;
        for(const auto source:adjacency.Matched)for(const auto& [face,count]:machiningOwners.at(source))possibleFaces.insert(face);
        for(const auto source:adjacency.Partial)for(const auto& [face,count]:machiningOwners.at(source))possibleFaces.insert(face);
        if(possibleFaces.size()>2){possibleConflict=true;break;}
    }
    if(!possibleConflict)return {};
    std::map<std::uint64_t,std::vector<double>> boundaries;
    for(const auto& [id,edge]:finite)boundaries[id]={edge.First,edge.Last};
    PropagateEvents(boundaries,finite,resolved);
    std::set<std::uint64_t> protectedEdges;
    for(const auto& [id,edge]:finite)
        for(std::size_t i=1;i<boundaries.at(id).size();++i)
        {
            const auto span=RestrictFinite(edge,boundaries.at(id)[i-1],boundaries.at(id)[i]);
            const auto active=ActiveSpanAdjacency(span,finite,resolved,machiningOwners);
            if(active.Owners.size()<=2)continue;
            for(const auto participant:active.Matched)
                if(machiningOwners.at(participant).size()==1&&allOwners.at(participant).size()==2)
                    protectedEdges.insert(participant);
        }
    if(protectedEdges.empty())return protectedEdges;
    for(const auto protectedId:protectedEdges)
    {
        const auto former=resolved.at(protectedId);
        std::set<std::uint64_t> crossLinks(former.Matched.begin(),former.Matched.end());
        crossLinks.insert(former.Partial.begin(),former.Partial.end());crossLinks.erase(protectedId);
        for(auto& [id,adjacency]:resolved)if(id!=protectedId)
        {
            const auto remove=[&](auto& values)
            {values.erase(std::remove(values.begin(),values.end(),protectedId),values.end());};
            const bool linked=std::find(adjacency.Matched.begin(),adjacency.Matched.end(),protectedId)!=adjacency.Matched.end()
                ||std::find(adjacency.Partial.begin(),adjacency.Partial.end(),protectedId)!=adjacency.Partial.end();
            remove(adjacency.Matched);remove(adjacency.Partial);
            if(linked)adjacency.Rejected.push_back(protectedId);
        }
        auto& own=resolved.at(protectedId);own.Matched={protectedId};own.Partial.clear();
        own.Rejected.insert(own.Rejected.end(),crossLinks.begin(),crossLinks.end());own.MaxGap=0;
    }
    for(auto& [id,adjacency]:resolved)
    {
        if(adjacency.Matched.empty())adjacency.Matched={id};
        adjacency.Owners.clear();
        for(const auto source:adjacency.Matched)for(const auto& [face,count]:machiningOwners.at(source))
        {
            auto& owner=adjacency.Owners[face];if(!owner.EdgeId)owner.EdgeId=source;owner.Count+=count;
        }
        std::sort(adjacency.Rejected.begin(),adjacency.Rejected.end());
        adjacency.Rejected.erase(std::unique(adjacency.Rejected.begin(),adjacency.Rejected.end()),adjacency.Rejected.end());
    }
    return protectedEdges;
}

double SignedSpanAngle(const double parameter,const FiniteEdge& source,
    const ResolvedAdjacency& adjacency,const std::uint64_t anchor,
    const std::map<std::uint64_t,FiniteEdge>& finite,
    const std::map<std::uint64_t,TopoDS_Face>& faces,
    const std::map<std::uint64_t,TopoDS_Edge>& edges)
{
    const auto other=adjacency.Owners.begin()->first==anchor?std::next(adjacency.Owners.begin())->first:adjacency.Owners.begin()->first;
    const auto aId=adjacency.Owners.at(anchor).EdgeId,bId=adjacency.Owners.at(other).EdgeId;
    const auto point=source.Curve->Value(parameter);const auto aProjection=Project(point,finite.at(aId)),bProjection=Project(point,finite.at(bId));
    const auto aUse=FaceUse(faces.at(anchor),edges.at(aId)),bUse=FaceUse(faces.at(other),edges.at(bId));
    gp_Pnt p,q;gp_Vec ta,tb,na,nb;
    finite.at(aId).Curve->D1(aProjection.Parameter,p,ta);finite.at(bId).Curve->D1(bProjection.Parameter,q,tb);
    if(!NormalAt(faces.at(anchor),aUse,aProjection.Parameter,p,na)||!NormalAt(faces.at(other),bUse,bProjection.Parameter,q,nb)
        ||ta.SquareMagnitude()<1e-24||tb.SquareMagnitude()<1e-24)throw std::runtime_error("Singular dihedral while finding a true sign event");
    ta.Normalize();tb.Normalize();if(aUse.Orientation()==TopAbs_REVERSED)ta.Reverse();if(bUse.Orientation()==TopAbs_REVERSED)tb.Reverse();
    if(ta.Dot(tb)>0)nb.Reverse();return std::atan2(ta.Dot(na.Crossed(nb)),std::clamp(na.Dot(nb),-1.0,1.0));
}

constexpr bool ZeroSeparatesOppositeSigns(const double before,const double zero,const double after)
{
    return zero==0.0&&((before>0.0&&after<0.0)||(before<0.0&&after>0.0));
}
static_assert(ZeroSeparatesOppositeSigns(1.0,0.0,-1.0));
static_assert(ZeroSeparatesOppositeSigns(-1.0,-0.0,1.0));
static_assert(!ZeroSeparatesOppositeSigns(1.0,0.0,1.0));
static_assert(!ZeroSeparatesOppositeSigns(1.0,0.1,-1.0));

void AnalyzeConvexity(SMachiningEdgeAnalysis& result,
    const std::map<std::uint64_t, TopoDS_Face>& faces,
    const std::map<std::uint64_t, TopoDS_Edge>& edges,
    const ResolvedAdjacency& adjacency,
    const SMachiningEdgeAnalysisOptions& options)
{
    if(!adjacency.Partial.empty())
    {
        result.ConvexityReason="partial-geometric-adjacency-needs-edge-segmentation";
        return;
    }
    if (result.MachiningFaceIds.size() == 1)
    {
        if (adjacency.Owners.at(result.MachiningFaceIds.front()).Count != 1)
        {
            result.ConvexityReason = "same-face-seam-or-repeated-use";
            return;
        }
        result.Convexity = EEdgeConvexity::Free;
        result.bConvexityValid = true;
        result.ConvexityReason = "one-machining-face";
        return;
    }
    if (result.MachiningFaceIds.size() != 2)
    {
        result.ConvexityReason = "non-manifold-machining-adjacency";
        return;
    }
    const auto firstId = result.OrientationAnchorFaceId;
    const auto secondId = result.MachiningFaceIds[0]==firstId?result.MachiningFaceIds[1]:result.MachiningFaceIds[0];
    if (adjacency.Owners.at(firstId).Count != 1 || adjacency.Owners.at(secondId).Count != 1)
    {
        result.ConvexityReason = "repeated-coedge-use";
        return;
    }
    const auto& firstFace = faces.at(firstId);
    const auto& secondFace = faces.at(secondId);
    const auto& firstEdge=edges.at(adjacency.Owners.at(firstId).EdgeId);
    const auto& secondEdge=edges.at(adjacency.Owners.at(secondId).EdgeId);
    const auto firstUse = FaceUse(firstFace, firstEdge), secondUse = FaceUse(secondFace, secondEdge);
    if (firstUse.IsNull() || secondUse.IsNull())
    {
        result.ConvexityReason = "missing-oriented-face-use";
        return;
    }
    if ((firstUse.Orientation() != TopAbs_FORWARD && firstUse.Orientation() != TopAbs_REVERSED)
        || (secondUse.Orientation() != TopAbs_FORWARD && secondUse.Orientation() != TopAbs_REVERSED))
    {
        result.ConvexityReason = "inconsistent-coedge-orientation";
        return;
    }
    BRepAdaptor_Curve curve(firstEdge);
    double first=result.FirstParameter,last=result.LastParameter;
    if(!firstEdge.IsSame(edges.at(result.EdgeId)))
    {
        const auto source=ReadFinite(edges.at(result.EdgeId),result.EdgeId);
        const auto target=ReadFinite(firstEdge,adjacency.Owners.at(firstId).EdgeId);
        const auto begin=Project(source.Curve->Value(first),target),end=Project(source.Curve->Value(last),target);
        first=std::min(begin.Parameter,end.Parameter);last=std::max(begin.Parameter,end.Parameter);
        if(last-first<=EventTolerance(target)&&result.LastParameter-result.FirstParameter>EventTolerance(source))
        {
            first=target.First;last=target.Last;
        }
    }
    FiniteEdge second;
    second.Curve=BRep_Tool::Curve(secondEdge,second.First,second.Last);
    second.Start=second.Curve->Value(second.First);second.End=second.Curve->Value(second.Last);
    bool positive = false, negative = false;
    std::string opposedEvidence;
    for (std::size_t i = 0; i < options.nAngleSampleCount; ++i)
    {
        const double parameter = first + (last - first)
            * (double(i) + 0.5) / double(options.nAngleSampleCount);
        gp_Pnt point;
        gp_Vec tangent,secondTangent, firstNormal, secondNormal;
        curve.D1(parameter, point, tangent);
        const auto projection=Project(point,second);
        result.MaxAdjacencyGap=std::max(result.MaxAdjacencyGap,projection.Distance);
        if(projection.Distance>kAdjacencyTolerance)
        {
            result.ConvexityReason="additional-dihedral-sample-exceeds-adjacency-gap";
            return;
        }
        gp_Pnt secondPoint;
        second.Curve->D1(projection.Parameter,secondPoint,secondTangent);
        if (tangent.SquareMagnitude() <= 1e-24
            ||secondTangent.SquareMagnitude()<=1e-24
            || !NormalAt(firstFace, firstUse, parameter, point, firstNormal)
            || !NormalAt(secondFace, secondUse, projection.Parameter, secondPoint, secondNormal))
        {
            result.ConvexityReason = "singular-edge-tangent-or-surface-normal";
            return;
        }
        tangent.Normalize();
        secondTangent.Normalize();
        if (firstUse.Orientation() == TopAbs_REVERSED) tangent.Reverse();
        if (secondUse.Orientation() == TopAbs_REVERSED) secondTangent.Reverse();
        if(tangent.Dot(secondTangent)>0.0)
        {
            // Imported independent faces can have unrelated orientation bits.
            // Reverse only this local second-face analysis frame. Reversing
            // its normal and its coedge together preserves the actual face
            // interior side and leaves every original CAD record untouched.
            secondNormal.Reverse();secondTangent.Reverse();
            result.bOrientationRepaired=true;
            if(std::find(result.OrientationFlippedFaceIds.begin(),result.OrientationFlippedFaceIds.end(),secondId)
                ==result.OrientationFlippedFaceIds.end())result.OrientationFlippedFaceIds.push_back(secondId);
        }
        const double sine = tangent.Dot(firstNormal.Crossed(secondNormal));
        const double cosine = std::clamp(firstNormal.Dot(secondNormal), -1.0, 1.0);
        double angle=0.0;
        if (cosine < -1.0 + 1e-10 && std::abs(sine) < 1e-10)
        {
            std::string evidence;
            if(!OpposedNormalMaterialLimit(firstFace,firstUse,parameter,point,
                secondFace,secondUse,projection.Parameter,secondPoint,firstNormal,tangent,
                result.MaxAdjacencyGap,angle,evidence))
            {
                result.ConvexityReason="opposed-coincident-normals-without-resolved-material-limit";return;
            }
            if(opposedEvidence.empty())opposedEvidence=evidence;
        }
        else angle=std::atan2(sine,cosine);
        if (result.AngleSamples.empty()) result.MinSignedAngleRadians = result.MaxSignedAngleRadians = angle;
        else
        {
            result.MinSignedAngleRadians = std::min(result.MinSignedAngleRadians, angle);
            result.MaxSignedAngleRadians = std::max(result.MaxSignedAngleRadians, angle);
        }
        result.AngleSamples.push_back({ parameter, angle });
        positive = positive || angle > options.dAngularToleranceRadians;
        negative = negative || angle < -options.dAngularToleranceRadians;
    }
    if (positive && negative)
    {
        result.ConvexityReason = "convexity-changes-along-edge";
        return;
    }
    if (positive || negative)
    {
        result.Convexity = positive ? EEdgeConvexity::Convex : EEdgeConvexity::Concave;
        result.bConvexityValid = true;
        result.ConvexityReason = !opposedEvidence.empty()?"opposed-normal-second-order-source-material-limit;"+opposedEvidence
            :result.bOrientationRepaired?"oriented-dihedral-samples-with-local-second-face-orientation-repair":"oriented-dihedral-samples";
        return;
    }
    // This analysis describes the dihedral about the edge. A zero dihedral
    // is Coplanar here even when the two supports belong to a curved surface;
    // it does not authorize the separate whole-face merge/Side removal rule.
    result.Convexity = EEdgeConvexity::Coplanar;
    result.bConvexityValid = true;
    result.ConvexityReason = result.bOrientationRepaired
        ? "zero-local-dihedral-with-local-second-face-orientation-repair" : "zero-local-dihedral";
}

void AnalyzeSkin(SMachiningEdgeAnalysis& result, const TopoDS_Edge& edge,
    const std::vector<SectionCurve>& section, const SMachiningEdgeAnalysisOptions& options,
    const bool wholeParentOnSkin)
{
    BRepAdaptor_Curve curve(edge);
    const double first = result.FirstParameter, last = result.LastParameter;
    if (!std::isfinite(first) || !std::isfinite(last) || first == last)
    {
        result.SkinReason = "invalid-or-zero-curve-range";
        return;
    }
    const bool supported=wholeParentOnSkin||SkinSpanOnSupport(edge,section,result.FirstParameter,result.LastParameter);
    for (std::size_t i = 0; i < options.nCurveSampleCount; ++i)
    {
        const double parameter = first + (last - first) * double(i)
            / double(options.nCurveSampleCount - 1);
        const auto point = curve.Value(parameter);
        const double distance = SkinDistance(point, section);
        const bool onSkin = distance <= options.dSkinDistanceTolerance;
        result.MinSkinDistance = result.Samples.empty() ? distance : std::min(result.MinSkinDistance, distance);
        result.Samples.push_back({ parameter, { point.X(), point.Y(), point.Z() }, distance, onSkin });
        result.MaxSkinDistance = std::max(result.MaxSkinDistance, distance);
    }
    result.bStartOnSkin = result.Samples.front().bOnSkin;
    result.bEndOnSkin = result.Samples.back().bOnSkin;
    if (supported)
    {
        result.SkinRelation = ESkinRelation::OnSkin;
        result.bSkinRelationValid = true;
        result.SkinReason = wholeParentOnSkin?"whole-source-edge-within-finite-skin-distance-tolerance"
            :"finite-native-support-span-on-skin";
    }
    else
    {
        result.SkinRelation = result.bStartOnSkin || result.bEndOnSkin
            ? ESkinRelation::PartialOnSkin : ESkinRelation::OffSkin;
        result.bSkinRelationValid = true;
        result.SkinReason = result.SkinRelation == ESkinRelation::PartialOnSkin
            ? "skin-contact-at-one-or-both-endpoints-only" : "no-sampled-skin-contact";
    }
}

gp_Pnt EndPoint(const TopoDS_Edge& edge, const bool reversed, const bool atEnd)
{
    if (BRep_Tool::Degenerated(edge))
    {
        for (TopExp_Explorer vertices(edge, TopAbs_VERTEX); vertices.More(); vertices.Next())
            return BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current()));
        throw std::runtime_error("Degenerated edge has no vertex");
    }
    BRepAdaptor_Curve curve(edge);
    return curve.Value(reversed != atEnd ? curve.LastParameter() : curve.FirstParameter());
}

gp_Vec EndTangent(const TopoDS_Edge& edge, const bool reversed, const bool atEnd)
{
    BRepAdaptor_Curve curve(edge);
    gp_Pnt point;
    gp_Vec tangent;
    curve.D1(reversed != atEnd ? curve.LastParameter() : curve.FirstParameter(), point, tangent);
    if (reversed) tangent.Reverse();
    return tangent;
}

std::string CurveKind(const TopoDS_Edge& edge)
{
    if (BRep_Tool::Degenerated(edge)) return "Degenerated";
    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() == GeomAbs_Line) return "Line";
    if (curve.GetType() == GeomAbs_Circle) return "Circle";
    // B-spline exports often encode exact lines. A collinear control polygon
    // is an exact geometric test, unlike fitting a line to sparse samples.
    if (curve.GetType() == GeomAbs_BSplineCurve)
    {
        const auto spline = curve.BSpline();
        const auto first = spline->Pole(1), last = spline->Pole(spline->NbPoles());
        const gp_Vec chord(first, last);
        if (chord.SquareMagnitude() > 1e-24)
        {
            bool linear = true;
            for (int i = 2; i < spline->NbPoles(); ++i)
                linear = linear && gp_Vec(first, spline->Pole(i)).Crossed(chord).Magnitude()
                    / chord.Magnitude() <= kSupportTolerance;
            if (linear) return "Line";
        }
    }
    return "Curve";
}

bool Continuous(const SMachiningBoundaryUse& first, const SMachiningBoundaryUse& second,
    const std::map<std::uint64_t, TopoDS_Edge>& edges, const SMachiningEdgeAnalysisOptions& options)
{
    const auto& a = edges.at(first.EdgeId);
    const auto& b = edges.at(second.EdgeId);
    if (EndPoint(a, first.bReversed, true).Distance(EndPoint(b, second.bReversed, false))
        > options.dConnectionTolerance) return false;
    const auto firstKind = CurveKind(a), secondKind = CurveKind(b);
    if (firstKind != secondKind) return false;
    if (firstKind == "Circle")
    {
        const auto ca = BRepAdaptor_Curve(a).Circle(), cb = BRepAdaptor_Curve(b).Circle();
        if (ca.Location().Distance(cb.Location()) > kSupportTolerance
            || std::abs(ca.Radius() - cb.Radius()) > kSupportTolerance
            || std::abs(ca.Axis().Direction().Dot(cb.Axis().Direction()))
                < std::cos(options.dAngularToleranceRadians)) return false;
    }
    auto ta = EndTangent(a, first.bReversed, true), tb = EndTangent(b, second.bReversed, false);
    if (ta.SquareMagnitude() <= 1e-24 || tb.SquareMagnitude() <= 1e-24) return false;
    ta.Normalize(); tb.Normalize();
    return ta.Dot(tb) >= std::cos(options.dAngularToleranceRadians);
}

SMachiningFaceEdgeAnalysis GroupFace(const G::BRepFace& face,
    const std::map<std::uint64_t, const G::BRepWire*>& wires,
    const std::map<std::uint64_t, TopoDS_Edge>& edges,
    const TopoDS_Face& builtFace,
    const SMachiningEdgeAnalysisOptions& options)
{
    SMachiningFaceEdgeAnalysis result;
    result.FaceId = face.Id;
    std::vector<TopoDS_Wire> nativeWires;
    for (TopoDS_Iterator children(builtFace, false, true); children.More(); children.Next())
        if (children.Value().ShapeType() == TopAbs_WIRE)
            nativeWires.push_back(TopoDS::Wire(children.Value()));
    if (nativeWires.size() != face.WireIds.size())
        throw std::runtime_error("Reconstructed face wire count differs from neutral records");
    for (std::size_t wi = 0; wi < face.WireIds.size(); ++wi)
    {
        const auto& wire = *wires.at(face.WireIds[wi]);
        SMachiningBoundaryWire output;
        output.WireId = wire.Id;
        const bool reversed = face.WireOrientations.size() == face.WireIds.size()
            && face.WireOrientations[wi] == G::ETopologyOrientation::Reversed;
        std::vector<SMachiningBoundaryUse> uses;
        std::map<std::uint64_t, std::size_t> remaining;
        for (const auto& use : wire.Coedges) ++remaining[use.EdgeId];
        for (BRepTools_WireExplorer ordered(nativeWires[wi], builtFace); ordered.More(); ordered.Next())
        {
            const auto nativeEdge = ordered.Current();
            bool found = false;
            for (const auto& use : wire.Coedges)
                if (remaining[use.EdgeId] && edges.at(use.EdgeId).IsSame(nativeEdge))
                {
                    uses.push_back({ use.EdgeId, nativeEdge.Orientation() == TopAbs_REVERSED });
                    --remaining[use.EdgeId];
                    found = true;
                    break;
                }
            if (!found) throw std::runtime_error("Ordered coedge is absent from the neutral wire");
        }
        for (const auto& use : wire.Coedges)
            if (remaining[use.EdgeId])
            {
                output.bOrdered = false;
                output.Reason = "wire-connectivity-walk-incomplete-internal-external-or-branched-use";
                uses.push_back({ use.EdgeId, (use.Orientation == G::ETopologyOrientation::Reversed) != reversed });
                --remaining[use.EdgeId];
            }
        for (const auto& use : uses)
        {
            bool join = false;
            if (output.bOrdered && !output.Groups.empty())
            {
                try { join = Continuous(output.Groups.back().SourceUses.back(), use, edges, options); }
                catch (const Standard_Failure&) {}
            }
            if (!join)
            {
                SMachiningBoundaryGroup group;
                group.CurveKind = CurveKind(edges.at(use.EdgeId));
                output.Groups.push_back(std::move(group));
            }
            output.Groups.back().SourceUses.push_back(use);
        }
        if (output.bOrdered && wire.Closed && output.Groups.size() > 1)
        {
            bool join = false;
            try { join = Continuous(output.Groups.back().SourceUses.back(), output.Groups.front().SourceUses.front(), edges, options); }
            catch (const Standard_Failure&) {}
            if (join)
            {
                auto merged = std::move(output.Groups.back());
                output.Groups.pop_back();
                merged.SourceUses.insert(merged.SourceUses.end(), output.Groups.front().SourceUses.begin(), output.Groups.front().SourceUses.end());
                output.Groups.front() = std::move(merged);
            }
        }
        for (std::size_t gi = 0; gi < output.Groups.size(); ++gi)
        {
            auto& group = output.Groups[gi];
            group.GroupId = gi + 1;
            const auto& first = group.SourceUses.front();
            const auto& last = group.SourceUses.back();
            group.bClosed = EndPoint(edges.at(first.EdgeId), first.bReversed, false)
                .Distance(EndPoint(edges.at(last.EdgeId), last.bReversed, true)) <= options.dConnectionTolerance;
        }
        result.Wires.push_back(std::move(output));
    }
    return result;
}
}

SMachiningEdgeAnalysisResult MachiningEdgeAnalysisWork::Run(
    const SSectionFaceClassificationResult& classification,
    const SSectionWiresResult& section,
    const SMachiningEdgeAnalysisOptions& options) const
{
    SMachiningEdgeAnalysisResult result;
    if (!classification.bSuccess || !section.bSuccess || section.Wires.empty())
    {
        result.Diagnostics.push_back("Successful Classify and finite section contours are required");
        return result;
    }
    if (!std::isfinite(options.dSkinDistanceTolerance) || options.dSkinDistanceTolerance <= 0
        || !std::isfinite(options.dConnectionTolerance) || options.dConnectionTolerance <= 0
        || !std::isfinite(options.dAngularToleranceRadians) || options.dAngularToleranceRadians <= 0
        || options.dAngularToleranceRadians >= 1.5707963267948966
        || options.nCurveSampleCount < 3 || options.nCurveSampleCount > 4097
        || options.nAngleSampleCount < 3 || options.nAngleSampleCount > 1025)
    {
        result.Diagnostics.push_back("Machining edge analysis options are invalid");
        return result;
    }
    try
    {
        std::vector<SectionCurve> sectionCurves;
        for (const auto& wire : section.Wires)
            for (const auto& edge : wire.Edges)
            {
                auto curve = O::BuildOpenCascadeCurve2(edge.Curve);
                if (curve.IsNull() || !std::isfinite(edge.Range.First) || !std::isfinite(edge.Range.Last)
                    || edge.Range.First == edge.Range.Last)
                    throw std::invalid_argument("Section contains an invalid finite curve");
                sectionCurves.push_back({ curve, std::min(edge.Range.First, edge.Range.Last), std::max(edge.Range.First, edge.Range.Last) });
            }
        if (sectionCurves.empty()) throw std::invalid_argument("Section has no finite boundary curves");
        const auto& model = classification.MachiningGeometry;
        if (classification.MachiningFaceIds.size() != model.Faces.size())
            throw std::invalid_argument("Classify machining face IDs and geometry disagree");
        if (model.Faces.empty())
        {
            result.bSuccess = result.bComplete = true;
            result.Diagnostics.push_back("Classify has no machining faces");
            return result;
        }
        const auto& allModel = classification.SplitGeometry.Faces.empty() ? model : classification.SplitGeometry;
        auto built = O::ReconstructOpenCascadeShape(allModel);
        if (!built.bOK) throw std::runtime_error("Cannot reconstruct classified topology");
        std::map<std::uint64_t, const G::BRepWire*> wires;
        std::map<std::uint64_t, std::map<std::uint64_t, std::size_t>> owners;
        std::map<std::uint64_t, std::set<std::uint64_t>> allOwners;
        std::set<std::uint64_t> machiningIds(classification.MachiningFaceIds.begin(), classification.MachiningFaceIds.end());
        for (const auto& wire : allModel.Wires) wires.emplace(wire.Id, &wire);
        for (const auto& face : allModel.Faces)
        {
            if (!built.Faces.contains(face.Id)) throw std::runtime_error("Missing reconstructed face ID");
            built.Faces.at(face.Id).Orientation(Orientation(face.Orientation));
            for (const auto wireId : face.WireIds)
                for (const auto& use : wires.at(wireId)->Coedges)
                {
                    allOwners[use.EdgeId].insert(face.Id);
                    if (machiningIds.contains(face.Id)) ++owners[use.EdgeId][face.Id];
                }
        }
        const auto materialOrientation=ApplyMaterialOrientation(built);
        for (const auto& face : model.Faces)
        {
            if (!machiningIds.contains(face.Id)) throw std::runtime_error("Machining geometry contains an unlisted face");
            result.Faces.push_back(GroupFace(face, wires, built.Edges, built.Faces.at(face.Id), options));
        }
        auto activeOwners=owners;
        for(const auto& [id,uses]:owners)
        {
            const auto& edge=built.Edges.at(id);
            const bool degenerated=BRep_Tool::Degenerated(edge);
            const bool seam=uses.size()==1&&uses.begin()->second>1
                &&BRep_Tool::IsClosed(edge,built.Faces.at(uses.begin()->first));
            if(!degenerated&&!seam)continue;
            SMachiningEdgeAnalysis excluded;excluded.EdgeId=id;excluded.SegmentId=0;
            excluded.Key="E"+std::to_string(id)+":excluded";
            excluded.SkinReason=excluded.ConvexityReason=degenerated?"degenerated-edge":"parametric-same-face-seam";
            for(const auto& [face,count]:uses)excluded.SourceMachiningFaceIds.push_back(face);
            excluded.MachiningFaceIds=excluded.SourceMachiningFaceIds;
            for(const auto& source:model.Edges)if(source.Id==id)
            {excluded.FirstParameter=source.Range.First;excluded.LastParameter=source.Range.Last;break;}
            result.ExcludedEdges.push_back(std::move(excluded));activeOwners.erase(id);
        }
        if(options.Progress)options.Progress("edge analysis read-only finite geometric adjacency");
        auto resolvedAdjacency=ResolveAdjacency(activeOwners,built.Edges,built.Faces);
        std::map<std::uint64_t,FiniteEdge> finite;
        std::map<std::uint64_t,std::vector<double>> events;
        std::map<std::uint64_t,bool> sourceSkin;
        std::map<std::uint64_t,bool> wholeParentOnSkin;
        std::map<std::uint64_t,std::vector<std::uint64_t>> skinSources;
        for(const auto& [id,uses]:activeOwners)
        {
            finite[id]=ReadFinite(built.Edges.at(id),id);
            for(const auto face:allOwners.at(id))if(!machiningIds.contains(face))
            {
                if(!sourceSkin.contains(face))
                {
                    try{sourceSkin[face]=IsSourceSkinFace(built.Faces.at(face),sectionCurves);}
                    catch(const Standard_Failure&){sourceSkin[face]=false;}
                }
                if(sourceSkin.at(face))skinSources[id].push_back(face);
            }
        }
        const auto protectedMaterialEdges=ProtectNativeMaterialPairsOnConflict(resolvedAdjacency,finite,activeOwners,allOwners);
        for(const auto id:protectedMaterialEdges)
        {
            std::ostringstream reason;reason<<"Preserved native-material-edge-pair E"<<id<<" faces=";
            for(const auto face:allOwners.at(id))reason<<face<<",";
            reason<<" after coincident geometric recovery produced more than two machining owners on a covered span; cross-contact links rejected";
            result.Diagnostics.push_back(reason.str());
        }
        for(const auto& [id,edge]:finite)
        {
            std::set<std::uint64_t> inherited(skinSources[id].begin(),skinSources[id].end());
            for(const auto matched:resolvedAdjacency.at(id).Matched)
                inherited.insert(skinSources[matched].begin(),skinSources[matched].end());
            skinSources[id]={inherited.begin(),inherited.end()};
            // Preserve the original whole-edge finite-distance semantics.
            // This is evaluated once on the unchanged parent edge; a child
            // cannot become Skin merely by lying near a skin endpoint.
            bool allOnSkin=true;
            for(std::size_t i=0;i<options.nCurveSampleCount;++i)
            {
                const double parameter=edge.First+(edge.Last-edge.First)*double(i)/double(options.nCurveSampleCount-1);
                const double distance=SkinDistance(edge.Curve->Value(parameter),sectionCurves);
                allOnSkin=allOnSkin&&distance<=options.dSkinDistanceTolerance;
            }
            wholeParentOnSkin[id]=allOnSkin||!skinSources[id].empty();
            events[id]=wholeParentOnSkin[id]?std::vector<double>{edge.First,edge.Last}
                :CollectSkinBreaks(built.Edges.at(id),sectionCurves,edge.First,edge.Last);
            NormalizeEvents(events[id],edge);
        }
        PropagateEvents(events,finite,resolvedAdjacency);
        bool finished=false;
        for(int refinement=0;refinement<8&&!finished;++refinement)
        {
            result.Edges.clear();bool addedAngleEvent=false;
            for(const auto& [id,edge]:finite)
            {
                if(options.Progress)options.Progress("edge analysis spans E"+std::to_string(id));
                const auto parameters=events.at(id);
                for(std::size_t segment=1;segment<parameters.size();++segment)
                {
                    if(parameters[segment]-parameters[segment-1]<=EventTolerance(edge))continue;
                    const auto span=RestrictFinite(edge,parameters[segment-1],parameters[segment]);
                    const auto adjacency=ActiveSpanAdjacency(span,finite,resolvedAdjacency,activeOwners);
                    SMachiningEdgeAnalysis output;output.EdgeId=id;output.SegmentId=segment;
                    output.Key="E"+std::to_string(id)+":S"+std::to_string(segment);
                    output.FirstParameter=span.First;output.LastParameter=span.Last;
                    for(const auto& [face,count]:activeOwners.at(id))output.SourceMachiningFaceIds.push_back(face);
                    for(const auto& [face,use]:adjacency.Owners)output.MachiningFaceIds.push_back(face);
                    output.MatchedEdgeIds=adjacency.Matched;output.PartialOverlapEdgeIds=resolvedAdjacency.at(id).Partial;
                    output.RejectedCoincidentEdgeIds=adjacency.Rejected;output.MaxAdjacencyGap=adjacency.MaxGap;
                    output.AdjacencyMethod=adjacency.Matched.size()>1?"coincident-sampled-analysis-spans"
                        :activeOwners.at(id).size()>1?"shared-neutral-edge":"single-machining-owner";
                    output.AdjacencyReason=adjacency.Matched.size()>1?"finite-covered-parameter-span-correspondence-within-1e-6-no-topology-mutation"
                        :!adjacency.Rejected.empty()?"coincident-boundary-excluded-by-owner-or-same-side-constraints":"neutral-source-coedge-ownership";
                    if(protectedMaterialEdges.contains(id))
                    {
                        output.AdjacencyReason="preserved-complete-native-material-edge-pair-with-machining-and-nonmachining-faces=";
                        for(const auto face:allOwners.at(id))output.AdjacencyReason+=std::to_string(face)+",";
                    }
                    output.OrientationAnchorFaceId=output.MachiningFaceIds.front();
                    for(const auto face:output.MachiningFaceIds)if(materialOrientation.AnchoredFaceIds.contains(face))
                    {output.OrientationAnchorFaceId=face;break;}
                    output.OrientationMethod=materialOrientation.AnchoredFaceIds.contains(output.OrientationAnchorFaceId)
                        ?"closed-solid-material-orientation-with-local-second-face-consistency":"first-adjacent-face-anchor-with-local-second-face-consistency";
                    for(const auto face:output.MachiningFaceIds)if(materialOrientation.FlippedFaceIds.contains(face))
                    {output.bOrientationRepaired=true;output.OrientationFlippedFaceIds.push_back(face);}
                    AnalyzeSkin(output,built.Edges.at(id),sectionCurves,options,wholeParentOnSkin.at(id));
                    output.SourceSkinFaceIds=skinSources[id];
                    if(!output.SourceSkinFaceIds.empty())
                    {
                        output.bSourceSkinMembership=true;output.bSkinRelationValid=true;output.SkinRelation=ESkinRelation::OnSkin;
                        output.bStartOnSkin=output.bEndOnSkin=true;
                        output.SkinReason="shared-source-skin-attribution-for-analysis-span";
                    }
                    AnalyzeConvexity(output,built.Faces,built.Edges,adjacency,options);
                    if(!output.bConvexityValid&&output.ConvexityReason=="convexity-changes-along-edge")
                    {
                        const auto anchorEdge=adjacency.Owners.at(output.OrientationAnchorFaceId).EdgeId;
                        const auto& anchor=finite.at(anchorEdge);
                        bool spanEventAdded=false;
                        for(std::size_t i=0;i<output.AngleSamples.size();++i)
                        {
                            if(output.AngleSamples[i].SignedAngleRadians!=0.0)continue;
                            std::size_t left=i,right=i;
                            while(left>0&&output.AngleSamples[left-1].SignedAngleRadians==0.0)--left;
                            while(right+1<output.AngleSamples.size()&&output.AngleSamples[right+1].SignedAngleRadians==0.0)++right;
                            if(left>0&&right+1<output.AngleSamples.size()
                                &&ZeroSeparatesOppositeSigns(output.AngleSamples[left-1].SignedAngleRadians,0.0,
                                    output.AngleSamples[right+1].SignedAngleRadians))
                            {
                                const double event=Project(anchor.Curve->Value(output.AngleSamples[i].Parameter),edge).Parameter;
                                if(event>span.First+EventTolerance(edge)&&event<span.Last-EventTolerance(edge))
                                {events[id].push_back(event);addedAngleEvent=spanEventAdded=true;}
                            }
                            i=right;
                        }
                        for(std::size_t i=1;i<output.AngleSamples.size();++i)
                        {
                            const auto& a=output.AngleSamples[i-1];const auto& b=output.AngleSamples[i];
                            if(a.SignedAngleRadians*b.SignedAngleRadians>=0)continue;
                            double first=Project(anchor.Curve->Value(a.Parameter),edge).Parameter;
                            double last=Project(anchor.Curve->Value(b.Parameter),edge).Parameter;
                            if(first>last)std::swap(first,last);
                            double sign=SignedSpanAngle(first,edge,adjacency,output.OrientationAnchorFaceId,finite,built.Faces,built.Edges);
                            for(int iteration=0;iteration<48&&last-first>EventTolerance(edge);++iteration)
                            {
                                const double middle=(first+last)/2;
                                const double value=SignedSpanAngle(middle,edge,adjacency,output.OrientationAnchorFaceId,finite,built.Faces,built.Edges);
                                if(sign*value<=0)last=middle;else{first=middle;sign=value;}
                            }
                            events[id].push_back((first+last)/2);addedAngleEvent=spanEventAdded=true;
                        }
                        if(!spanEventAdded)throw std::runtime_error("A changing dihedral produced no interior zero event for "+output.Key);
                        continue;
                    }
                    if(!output.bConvexityValid)throw std::runtime_error("Invalid machining boundary "+output.Key+": "+output.ConvexityReason);
                    if(!output.bSkinRelationValid)throw std::runtime_error("Invalid skin span "+output.Key+": "+output.SkinReason);
                    result.Edges.push_back(std::move(output));
                }
            }
            if(addedAngleEvent)
            {
                for(auto& [id,values]:events)NormalizeEvents(values,finite.at(id));
                PropagateEvents(events,finite,resolvedAdjacency);
            }
            else finished=true;
        }
        if(!finished)throw std::runtime_error("Dihedral sign events did not converge to uniform analysis spans");
        std::map<std::uint64_t,std::vector<std::size_t>> bySource;
        for(std::size_t i=0;i<result.Edges.size();++i)bySource[result.Edges[i].EdgeId].push_back(i);
        for(auto& output:result.Edges)
        {
            const auto source=RestrictFinite(finite.at(output.EdgeId),output.FirstParameter,output.LastParameter);
            for(const auto id:output.MatchedEdgeIds)for(const auto index:bySource[id])
            {
                const auto& other=result.Edges[index];
                if(std::find(other.MatchedEdgeIds.begin(),other.MatchedEdgeIds.end(),output.EdgeId)==other.MatchedEdgeIds.end())continue;
                const auto target=RestrictFinite(finite.at(id),other.FirstParameter,other.LastParameter);
                double gap=0.0;
                if(SpanContained(source,target,gap)&&SpanContained(target,source,gap))output.MatchedSpanKeys.push_back(other.Key);
            }
            std::sort(output.MatchedSpanKeys.begin(),output.MatchedSpanKeys.end());
            output.MatchedSpanKeys.erase(std::unique(output.MatchedSpanKeys.begin(),output.MatchedSpanKeys.end()),output.MatchedSpanKeys.end());
            if(output.MatchedSpanKeys.empty())output.MatchedSpanKeys.push_back(output.Key);
        }
        std::map<std::string,std::size_t> keyIndex;
        for(std::size_t i=0;i<result.Edges.size();++i)keyIndex[result.Edges[i].Key]=i;
        for(auto& output:result.Edges)
        {
            std::set<std::uint64_t> inherited(output.SourceSkinFaceIds.begin(),output.SourceSkinFaceIds.end());
            std::string wholeParentWitness;
            for(const auto& key:output.MatchedSpanKeys)
            {
                const auto& other=result.Edges.at(keyIndex.at(key));
                if(other.bSourceSkinMembership)inherited.insert(other.SourceSkinFaceIds.begin(),other.SourceSkinFaceIds.end());
                if(wholeParentWitness.empty()&&other.bSkinRelationValid&&other.SkinRelation==ESkinRelation::OnSkin
                    &&wholeParentOnSkin.at(other.EdgeId))wholeParentWitness=other.Key;
            }
            if(!inherited.empty())
            {
                output.SourceSkinFaceIds={inherited.begin(),inherited.end()};output.bSourceSkinMembership=true;
                output.SkinRelation=ESkinRelation::OnSkin;output.bStartOnSkin=output.bEndOnSkin=true;
                output.SkinReason="source-skin-attribution-propagated-through-coincident-analysis-span";
            }
            else if(output.SkinRelation!=ESkinRelation::OnSkin&&!wholeParentWitness.empty())
            {
                // A short complete source edge can satisfy the unchanged
                // whole-parent tolerance while its coincident peer belongs
                // to a longer mixed parent. Transfer only the matched span's
                // established relationship, never the peer's remainder.
                output.SkinRelation=ESkinRelation::OnSkin;output.bSkinRelationValid=true;
                output.bStartOnSkin=output.bEndOnSkin=true;
                output.SkinReason="whole-source-edge-skin-relation-inherited-from-"+wholeParentWitness+"-through-coincident-analysis-span";
            }
        }
        result.bSuccess = true;
        result.bComplete = std::all_of(result.Edges.begin(), result.Edges.end(), [](const auto& edge)
            { return edge.bSkinRelationValid && edge.bConvexityValid; });
        result.Diagnostics.push_back("Convexity adjacency includes machining faces only; non-machining faces never count as a second machining neighbor");
        result.Diagnostics.push_back("Finite section distances and dihedral angles are sampled evidence, not a continuous error certificate");
        result.Diagnostics.push_back("Convexity uses source closed-solid material orientation when available; otherwise the first source face anchors a local second-face orientation correction. Original CAD orientation records are unchanged");
        result.Diagnostics.push_back("Coplanar in this edge analysis means a zero local dihedral within angular tolerance, including smoothly joined curved supports; it does not authorize whole-face merging");
        result.Diagnostics.push_back("Detached machining boundaries may be paired by 33 bidirectional finite-curve samples within 1e-6 mm; this is sampled correspondence, not a continuous coincidence certificate or sewn topology");
        result.Diagnostics.push_back("Source skin attribution uses existing shared coedges and sampled finite analytic skin support; detached near-coincident sources remain geometric classifications");
    }
    catch (const Standard_Failure& e)
    {
        result.Diagnostics.push_back(std::string("OCC edge analysis failure: ") + e.GetMessageString());
    }
    catch (const std::exception& e) { result.Diagnostics.push_back(e.what()); }
    return result;
}
}
