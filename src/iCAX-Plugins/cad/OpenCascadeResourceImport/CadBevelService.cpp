#include "pch.h"
#include "CadBevelService.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "Services/ServicesHelper.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepGProp.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GProp_GProps.hxx>
#include <map>
#include <set>
#include <cmath>
#include <limits>
#include <BRepTools.hxx>
#include <sstream>
#include <iomanip>

namespace iCAX::CAD
{
    namespace
    {
        constexpr double kTolerance = 1.0e-6;
        constexpr double kPi = 3.14159265358979323846;
        using EdgeMap = std::map<std::uint64_t, TopoDS_Edge>;
        using FaceMap = std::map<std::uint64_t, TopoDS_Face>;
        std::array<double, 3> Point(const gp_Pnt& P) { return {P.X(), P.Y(), P.Z()}; }
        double Volume(const TopoDS_Shape& S)
        {
            GProp_GProps P; BRepGProp::VolumeProperties(S, P, 1.0e-9); return std::abs(P.Mass());
        }
        void NativeMaps(const TopoDS_Shape& S, EdgeMap& Edges, FaceMap& Faces)
        {
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> E, F;
            TopExp::MapShapes(S, TopAbs_EDGE, E); TopExp::MapShapes(S, TopAbs_FACE, F);
            for (int I=1; I<=E.Extent(); ++I) Edges.emplace(I, TopoDS::Edge(E(I)));
            for (int I=1; I<=F.Extent(); ++I) Faces.emplace(I, TopoDS::Face(F(I)));
        }
        template<class T> std::uint64_t ID(const std::map<std::uint64_t,T>& M, const TopoDS_Shape& S)
        {
            for (const auto& [K,V]:M) if (V.IsSame(S)) return K;
            return 0;
        }
        bool OnFace(const TopoDS_Edge& E, const TopoDS_Face& F)
        {
            for (TopExp_Explorer X(F,TopAbs_EDGE); X.More(); X.Next()) if (X.Current().IsSame(E)) return true;
            return false;
        }
        bool SharesVertex(const TopoDS_Edge& A, const TopoDS_Edge& B)
        {
            for (TopExp_Explorer X(A,TopAbs_VERTEX); X.More(); X.Next())
                for (TopExp_Explorer Y(B,TopAbs_VERTEX); Y.More(); Y.Next())
                    if (X.Current().IsSame(Y.Current())) return true;
            return false;
        }
        struct WallPair { TopoDS_Edge Other; TopoDS_Face Face; double Thickness=0; };
        // A land is meaningful only where the selected edge has a geometric
        // opposite boundary across one constant-thickness cut face. Matching
        // queries exact curves; display samples never participate in modeling.
        double PairThickness(const TopoDS_Edge& E, const TopoDS_Edge& Other)
        {
            if (E.IsSame(Other) || SharesVertex(E,Other) || BRep_Tool::Degenerated(Other)) return 0;
            double First,Last; const auto C=BRep_Tool::Curve(Other,First,Last);
            if (C.IsNull()) return 0;
            BRepAdaptor_Curve Source(E);
            double Minimum=std::numeric_limits<double>::max(),Maximum=0;
            for (double Fraction : {0.1,0.3,0.5,0.7,0.9})
            {
                gp_Pnt P; gp_Vec Tangent;
                Source.D1(Source.FirstParameter()+(Source.LastParameter()-Source.FirstParameter())*Fraction,P,Tangent);
                GeomAPI_ProjectPointOnCurve Projection(P,C,First,Last);
                if (!Projection.NbPoints()) return 0;
                gp_Pnt Q; gp_Vec OtherTangent; C->D1(Projection.LowerDistanceParameter(),Q,OtherTangent);
                if (Tangent.SquareMagnitude()<1e-20 || OtherTangent.SquareMagnitude()<1e-20) return 0;
                // Parallel offset curves on a curved wall can have different
                // circumferential derivative scales despite identical normal
                // depth. The constant-distance test below remains decisive.
                if (std::abs(Tangent.Normalized().Dot(OtherTangent.Normalized()))<0.95) return 0;
                const auto D=P.Distance(Q); Minimum=std::min(Minimum,D);Maximum=std::max(Maximum,D);
            }
            if (Minimum<=kTolerance || Maximum-Minimum>std::max(kTolerance*10,Maximum*1e-5)) return 0;
            return (Minimum+Maximum)*0.5;
        }
        WallPair FindWall(const TopoDS_Edge& E, const FaceMap& Faces, std::uint64_t RequestedFace)
        {
            WallPair Best;
            if(RequestedFace && !Faces.contains(RequestedFace)) throw std::runtime_error("坡口贯壁基准面已不存在");
            if(RequestedFace && !OnFace(E,Faces.at(RequestedFace))) throw std::runtime_error("所选贯壁基准面不属于该边");
            std::size_t CandidateFaceCount=0;
            for (const auto& [FaceID,F]:Faces)
            {
                if (RequestedFace && FaceID!=RequestedFace) continue;
                if (!OnFace(E,F)) continue;
                WallPair Candidate;
                for (TopExp_Explorer X(F,TopAbs_EDGE);X.More();X.Next())
                {
                    const auto Other=TopoDS::Edge(X.Current());
                    const auto Thickness=PairThickness(E,Other);
                    if (Thickness>0 && (!Candidate.Thickness || Thickness<Candidate.Thickness)) Candidate={Other,F,Thickness};
                }
                if(Candidate.Thickness) {++CandidateFaceCount;Best=Candidate;}
            }
            if(!RequestedFace && CandidateFaceCount>1)
                throw std::runtime_error("所选边存在多个可能的贯壁基准面，请选择切口壁面");
            if (!Best.Thickness) throw std::runtime_error("暂不支持：所选边在贯壁面上没有等厚的对侧边界");
            return Best;
        }
        SCadNativeBevelResult Apply(const TopoDS_Shape& Shape, const EdgeMap& Edges,
            const FaceMap& Faces, const std::vector<SCadBevelOperation>& Operations)
        {
            SCadNativeBevelResult Result;
            try
            {
                if (Shape.IsNull() || !BRepCheck_Analyzer(Shape).IsValid()) throw std::runtime_error("坡口输入必须是有效 CAD 实体");
                if (Operations.empty()) {Result.bOK=true;Result.Shape=Shape;return Result;}
                if (Operations.size()>4096) throw std::runtime_error("坡口边数量超过上限");
                // Work on copied topology: OCCT fillet preparation may adjust
                // shared edge tolerances even before Build finishes.
                BRepBuilderAPI_Copy Copy(Shape,true,false);
                BRepFilletAPI_MakeChamfer Builder(Copy.Shape());
                std::map<std::uint64_t,std::pair<std::uint64_t,std::pair<double,double>>> Added;
                std::map<int,std::pair<double,double>> ContourLaws;
                for (const auto& Op:Operations)
                {
                    if (Op.Side!="outer" && Op.Side!="inner" && Op.Side!="both") throw std::runtime_error("坡口侧面无效");
                    if (!std::isfinite(Op.AngleDegrees) || Op.AngleDegrees<=0 || Op.AngleDegrees>=90
                        || !std::isfinite(Op.RootFace) || Op.RootFace<0) throw std::runtime_error("坡口角度须在 0 到 90 度之间，钝边不得小于零");
                    const auto Found=Edges.find(Op.EdgeID);
                    if (Found==Edges.end()) throw std::runtime_error("坡口选择边已不存在");
                    const auto Wall=FindWall(Found->second,Faces,Op.ReferenceFaceID);
                    if (Op.RootFace>=Wall.Thickness-kTolerance) throw std::runtime_error("坡口钝边必须小于实际壁厚");
                    const auto FaceID=ID(Faces,Wall.Face);
                    const auto Add=[&](const TopoDS_Edge& Edge, double Depth)
                    {
                        const auto EdgeID=ID(Edges,Edge);
                        const auto Specification=std::make_pair(FaceID,std::make_pair(Depth,Op.AngleDegrees));
                        if(const auto Previous=Added.find(EdgeID);Previous!=Added.end())
                        {
                            if(Previous->second.first!=FaceID || std::abs(Previous->second.second.first-Depth)>kTolerance
                                || std::abs(Previous->second.second.second-Op.AngleDegrees)>1e-9)
                                throw std::runtime_error("同一 CAD 边的坡口设置冲突");
                            return;
                        }
                        Added.emplace(EdgeID,Specification);
                        const auto OwnedEdge=TopoDS::Edge(Copy.ModifiedShape(Edge));
                        const auto OwnedFace=TopoDS::Face(Copy.ModifiedShape(Wall.Face));
                        // OCCT's distance-angle law measures its angle from F.
                        Builder.AddDA(Depth,Op.AngleDegrees*kPi/180.0,OwnedEdge,OwnedFace);
                        const auto Contour=Builder.Contour(OwnedEdge);
                        if (!Contour) throw std::runtime_error("暂不支持：所选边无法形成坡口，请检查边的连续性");
                        const auto Law=std::make_pair(Depth,Op.AngleDegrees);
                        if(const auto Previous=ContourLaws.find(Contour);Previous!=ContourLaws.end())
                        {
                            if(std::abs(Previous->second.first-Depth)>kTolerance || std::abs(Previous->second.second-Op.AngleDegrees)>1e-9)
                                throw std::runtime_error("相切 CAD 边上的坡口设置冲突");
                        }
                        else ContourLaws.emplace(Contour,Law);
                    };
                    // A zero land uses a sub-tolerance residual so the two
                    // boundaries stay regular during kernel construction.
                    const auto Land=std::max(Op.RootFace,kTolerance*0.1);
                    const auto Depth=(Wall.Thickness-Land)/(Op.Side=="both"?2.0:1.0);
                    if (Op.Side!="inner") Add(Found->second,Depth);
                    if (Op.Side!="outer") Add(Wall.Other,Depth);
                }
                for(int Contour=1;Contour<=Builder.NbContours();++Contour)
                    for(int EdgeIndex=1;EdgeIndex<=Builder.NbEdges(Contour);++EdgeIndex)
                        if(std::none_of(Added.begin(),Added.end(),[&](const auto& Item){return Copy.ModifiedShape(Edges.at(Item.first)).IsSame(Builder.Edge(Contour,EdgeIndex));}))
                            throw std::runtime_error("暂不支持：相切边不能按所选单段单独生成坡口，请选择完整相切边链");
                Builder.Build();
                if (!Builder.IsDone() || Builder.Shape().IsNull() || !BRepCheck_Analyzer(Builder.Shape()).IsValid())
                    throw std::runtime_error("暂不支持：当前边与坡口尺寸无法形成有效实体，请调整角度或钝边");
                Result.RemovedVolume=Volume(Shape)-Volume(Builder.Shape());
                if (Result.RemovedVolume<=1e-9 || Volume(Builder.Shape())<=kTolerance)
                    throw std::runtime_error("坡口没有去除材料或已经去除整个实体");
                Result.Shape=Builder.Shape();Result.bOK=true;
                Result.Diagnostic="已生成真实三维坡口并保留指定钝边";
            }
            catch(const Standard_Failure& E) {Result.Diagnostic=std::string("坡口几何计算失败：")+E.what();}
            catch(const std::exception& E) {Result.Diagnostic=E.what();}
            return Result;
        }
        SCadTopologyResult Inspect(const TopoDS_Shape& Shape, const EdgeMap& Edges,const FaceMap& Faces)
        {
            SCadTopologyResult Result;
            try
            {
                if (Shape.IsNull() || !BRepCheck_Analyzer(Shape).IsValid()) throw std::runtime_error("CAD 拓扑必须是有效实体");
                for(const auto& [EdgeID,E]:Edges)
                {
                    if (BRep_Tool::Degenerated(E)) continue;
                    SCadSelectableEdge Record;Record.EdgeID=EdgeID;
                    for(const auto& [FaceID,F]:Faces) if(OnFace(E,F)) Record.AdjacentFaceIDs.push_back(FaceID);
                    BRepAdaptor_Curve Curve(E);
                    const auto Count=Curve.GetType()==GeomAbs_Line?1:48;
                    for(int I=0;I<=Count;++I) Record.Points.push_back(Point(Curve.Value(
                        Curve.FirstParameter()+(Curve.LastParameter()-Curve.FirstParameter())*I/Count)));
                    Result.Edges.push_back(std::move(Record));
                }
                for(const auto& [FaceID,F]:Faces)
                {
                    SCadSelectableFace Record;Record.FaceID=FaceID;
                    switch(BRepAdaptor_Surface(F).GetType())
                    {
                    case GeomAbs_Plane:Record.SurfaceKind="plane";break;
                    case GeomAbs_Cylinder:Record.SurfaceKind="cylinder";break;
                    case GeomAbs_Cone:Record.SurfaceKind="cone";break;
                    case GeomAbs_Sphere:Record.SurfaceKind="sphere";break;
                    case GeomAbs_Torus:Record.SurfaceKind="torus";break;
                    case GeomAbs_SurfaceOfExtrusion:Record.SurfaceKind="extrusion";break;
                    case GeomAbs_SurfaceOfRevolution:Record.SurfaceKind="revolution";break;
                    case GeomAbs_BSplineSurface:Record.SurfaceKind="bspline";break;
                    case GeomAbs_BezierSurface:Record.SurfaceKind="bezier";break;
                    default:Record.SurfaceKind="other";break;
                    }
                    for(TopExp_Explorer X(F,TopAbs_EDGE);X.More();X.Next())
                    {
                        const auto EdgeID=ID(Edges,X.Current());
                        if(EdgeID && std::find(Record.EdgeIDs.begin(),Record.EdgeIDs.end(),EdgeID)==Record.EdgeIDs.end()) Record.EdgeIDs.push_back(EdgeID);
                    }
                    // Mesh an owned face for selection display; querying
                    // topology never mutates the document's frozen BRep.
                    const auto Owned=TopoDS::Face(BRepBuilderAPI_Copy(F,true,false).Shape());
                    BRepMesh_IncrementalMesh Mesh(Owned,0.1,false,0.25,false);
                    TopLoc_Location Location;const auto Triangulation=BRep_Tool::Triangulation(Owned,Location);
                    if(!Triangulation.IsNull()) for(int I=1;I<=Triangulation->NbTriangles();++I)
                    {
                        int A,B,C;Triangulation->Triangle(I).Get(A,B,C);
                        if(F.Orientation()==TopAbs_REVERSED) std::swap(B,C);
                        for(int N:{A,B,C}) Record.TriangleVertices.push_back(Point(Triangulation->Node(N).Transformed(Location.Transformation())));
                    }
                    Result.Faces.push_back(std::move(Record));
                }
                Result.bOK=true;
            }
            catch(const Standard_Failure& E) {Result.Diagnostic=E.what();}
            catch(const std::exception& E) {Result.Diagnostic=E.what();}
            return Result;
        }
        class CCadBevelService final : public ICadBevelService
        {
            AUTO_REGIST_SERVICE(ICadBevelService,CCadBevelService)
        public:
            void OnLoad() override {}
            void OnUnload() override {}
            SCadTopologyResult InspectNative(const TopoDS_Shape& Shape) const override
            {
                try
                {
                    EdgeMap Edges;FaceMap Faces;NativeMaps(Shape,Edges,Faces);return iCAX::CAD::Inspect(Shape,Edges,Faces);
                }
                catch(const Standard_Failure& E) {return {false,{},{},std::string("CAD 拓扑读取失败：")+E.what()};}
                catch(const std::exception& E) {return {false,{},{},std::string("CAD 拓扑读取失败：")+E.what()};}
            }
            SCadNativeBevelResult ApplyNative(const TopoDS_Shape& Shape,const std::vector<SCadBevelOperation>& Operations) const override
            {
                SCadNativeBevelResult Result;
                try
                {
                    EdgeMap Edges;FaceMap Faces;NativeMaps(Shape,Edges,Faces);return iCAX::CAD::Apply(Shape,Edges,Faces,Operations);
                }
                catch(const Standard_Failure& E) {Result.Diagnostic=std::string("CAD 坡口拓扑读取失败：")+E.what();}
                catch(const std::exception& E) {Result.Diagnostic=std::string("CAD 坡口拓扑读取失败：")+E.what();}
                return Result;
            }
            SCadTopologyResult Inspect(const iCAX::GeometryData::BRepModel& Geometry) const override
            {
                const auto Built=iCAX::OpenCascade::BuildOpenCascadeShape(Geometry);
                if(!Built.bOK) return {false,{},{},"无法还原保存的 CAD 实体"};
                return iCAX::CAD::Inspect(Built.Shape,Built.Edges,Built.Faces);
            }
            SCadBevelResult Apply(const iCAX::GeometryData::BRepModel& Geometry,const std::vector<SCadBevelOperation>& Operations) const override
            {
                SCadBevelResult Result;
                try
                {
                    const auto Built=iCAX::OpenCascade::BuildOpenCascadeShape(Geometry);
                    if(!Built.bOK) {Result.Diagnostic="无法还原保存的 CAD 实体";return Result;}
                    const auto Native=iCAX::CAD::Apply(Built.Shape,Built.Edges,Built.Faces,Operations);
                    Result.Diagnostic=Native.Diagnostic;
                    if(!Native.bOK) return Result;
                    if(Operations.empty()) {Result.Geometry=Geometry;Result.bOK=true;return Result;}
                    auto Converted=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Native.Shape,Geometry.Metadata.Name,Geometry.Metadata.SourceId);
                    const auto Rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(Converted);
                    if(!Rebuilt.bOK || !BRepCheck_Analyzer(Rebuilt.Shape).IsValid())
                        throw std::runtime_error("坡口结果无法保存为有效 CAD 实体");
                    Result.Geometry=std::move(Converted);Result.RemovedVolume=Native.RemovedVolume;Result.bOK=true;
                }
                catch(const Standard_Failure& E) {Result.Diagnostic=std::string("坡口几何保存失败：")+E.what();}
                catch(const std::exception& E) {Result.Diagnostic=E.what();}
                return Result;
            }
        };
    }
    std::string CadExactTopologySignature(const TopoDS_Shape& Shape)
    {
        std::ostringstream bytes;BRepTools::Write(Shape,bytes,false,false,TopTools_FormatVersion_CURRENT);
        std::uint64_t hash=1469598103934665603ull;
        for(unsigned char value:bytes.str()){hash^=value;hash*=1099511628211ull;}
        std::ostringstream key;key<<std::hex<<hash;return key.str();
    }
    SCadNativeBevelResult ApplyCadNativeBevel(const TopoDS_Shape& Shape,
        const std::vector<SCadBevelOperation>& Operations)
    {
        EdgeMap edges;FaceMap faces;NativeMaps(Shape,edges,faces);return Apply(Shape,edges,faces,Operations);
    }
}
