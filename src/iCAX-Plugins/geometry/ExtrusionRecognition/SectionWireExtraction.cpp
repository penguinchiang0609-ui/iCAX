#include "pch.h"
#include <OpenCascadeResourceImport/OpenCascadeTaskExecution.h>
#include <exception>
#include "ExtrudeRecognizesService.h"
#include "SectionExactClassifier.h"
#include "SectionProjectionMultiplicity.h"
#include "SectionMaterialProjection.h"
#include "SectionFaceSubset.h"

#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass_FaceExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <gp_GTrsf.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRepLib.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Bnd_Box.hxx>
#include <Bnd_Box2d.hxx>
#include <BndLib_Add2dCurve.hxx>
#include <BOPTools_AlgoTools2D.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2dConvert.hxx>
#include <GeomAPI.hxx>
#include <gp_Pln.hxx>
#include <GeomAPI_ExtremaCurveCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom_Line.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_Circle.hxx>
#include <GeomConvert_CurveToAnaCurve.hxx>
#include <GeomConvert.hxx>
#include <BSplCLib.hxx>
#include <GeomConvert_SurfToAnaSurf.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <ElCLib.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <NCollection_HSequence.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeAnalysis.hxx>
#include <ShapeExtend_WireData.hxx>
#include <ShapeAnalysis_Wire.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Vertex.hxx>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace iCAX::ExtrusionRecognition
{
namespace toolpath_detail
{
    SSectionFaceClassificationResult ClassifySectionFaces(
        const iCAX::GeometryData::BRepModel&, const SSectionWiresResult&,
        const SFeatureToolpathOptions&);
    SFeatureToolpathResult AnalyzeFeatureToolpaths(
        IN const iCAX::GeometryData::BRepModel& Geometry_,
        IN const SSectionWiresResult& Section_,
        IN const SFeatureToolpathOptions& Options_);
}

namespace
{
    using iCAX::GeometryData::BRepEdge;
    using iCAX::GeometryData::BRepFace;
    using iCAX::GeometryData::BRepModel;
    using iCAX::GeometryData::BRepVertex;
    using iCAX::GeometryData::BRepWire;
    using iCAX::GeometryData::Curve2;
    using iCAX::GeometryData::Curve3;
    using iCAX::GeometryData::Direction2;
    using iCAX::GeometryData::Direction3;
    using iCAX::GeometryData::ParameterRange;
    using iCAX::GeometryData::Point2;
    using iCAX::GeometryData::Point3;
    using iCAX::GeometryData::Surface3;
    using iCAX::GeometryData::Triangulation3;
    using iCAX::GeometryData::Vector3;

    constexpr double kPi = 3.1415926535897932384626433832795;
    constexpr double kEpsilon = 1.0e-12;

    struct SGeometryIndex final
    {
        std::unordered_map<std::uint64_t, const Curve3*> Curves;
        std::unordered_map<std::uint64_t, const Surface3*> Surfaces;
        std::unordered_map<std::uint64_t, const Triangulation3*> Triangulations;
        std::unordered_map<std::uint64_t, const BRepEdge*> Edges;
        std::unordered_map<std::uint64_t, const BRepWire*> Wires;
        std::unordered_map<std::uint64_t, const BRepVertex*> Vertices;

        explicit SGeometryIndex(IN const BRepModel& Geometry_)
        {
            Curves.reserve(Geometry_.Curves3.size());
            for (const auto& _Record : Geometry_.Curves3)
            {
                Curves.emplace(_Record.Id, &_Record.Geometry);
            }
            Surfaces.reserve(Geometry_.Surfaces3.size());
            for (const auto& _Record : Geometry_.Surfaces3)
            {
                Surfaces.emplace(_Record.Id, &_Record.Geometry);
            }
            Triangulations.reserve(Geometry_.Triangulations3.size());
            for (const auto& _Record : Geometry_.Triangulations3)
            {
                Triangulations.emplace(_Record.Id, &_Record.Geometry);
            }
            Edges.reserve(Geometry_.Edges.size());
            for (const auto& _Edge : Geometry_.Edges)
            {
                Edges.emplace(_Edge.Id, &_Edge);
            }
            Wires.reserve(Geometry_.Wires.size());
            for (const auto& _Wire : Geometry_.Wires)
            {
                Wires.emplace(_Wire.Id, &_Wire);
            }
            Vertices.reserve(Geometry_.Vertices.size());
            for (const auto& _Vertex : Geometry_.Vertices)
            {
                Vertices.emplace(_Vertex.Id, &_Vertex);
            }
        }
    };

    const Surface3* FindSurface(IN const SGeometryIndex& Index_, IN std::uint64_t ID_)
    {
        const auto _Iter = Index_.Surfaces.find(ID_);
        return _Iter == Index_.Surfaces.end() ? nullptr : _Iter->second;
    }

    const Triangulation3* FindTriangulation(IN const SGeometryIndex& Index_, IN std::uint64_t ID_)
    {
        const auto _Iter = Index_.Triangulations.find(ID_);
        return _Iter == Index_.Triangulations.end() ? nullptr : _Iter->second;
    }

    const BRepEdge* FindEdge(IN const SGeometryIndex& Index_, IN std::uint64_t ID_)
    {
        const auto _Iter = Index_.Edges.find(ID_);
        return _Iter == Index_.Edges.end() ? nullptr : _Iter->second;
    }

    const BRepWire* FindWire(IN const SGeometryIndex& Index_, IN std::uint64_t ID_)
    {
        const auto _Iter = Index_.Wires.find(ID_);
        return _Iter == Index_.Wires.end() ? nullptr : _Iter->second;
    }

    const BRepVertex* FindVertex(IN const SGeometryIndex& Index_, IN std::uint64_t ID_)
    {
        const auto _Iter = Index_.Vertices.find(ID_);
        return _Iter == Index_.Vertices.end() ? nullptr : _Iter->second;
    }

    Vector3 ToVector(IN const Direction3& Direction_)
    {
        return { Direction_.X, Direction_.Y, Direction_.Z };
    }

    Vector3 Difference(IN const Point3& Left_, IN const Point3& Right_)
    {
        return {
            Left_.X - Right_.X,
            Left_.Y - Right_.Y,
            Left_.Z - Right_.Z
        };
    }

    Point3 Add(IN const Point3& Point_, IN const Vector3& Vector_)
    {
        return {
            Point_.X + Vector_.X,
            Point_.Y + Vector_.Y,
            Point_.Z + Vector_.Z
        };
    }

    Vector3 AddVectors(IN const Vector3& Left_, IN const Vector3& Right_)
    {
        return {
            Left_.X + Right_.X,
            Left_.Y + Right_.Y,
            Left_.Z + Right_.Z
        };
    }

    Vector3 Scale(IN const Vector3& Vector_, IN double Scale_)
    {
        return {
            Vector_.X * Scale_,
            Vector_.Y * Scale_,
            Vector_.Z * Scale_
        };
    }

    double Dot(IN const Vector3& Left_, IN const Vector3& Right_)
    {
        return Left_.X * Right_.X
            + Left_.Y * Right_.Y
            + Left_.Z * Right_.Z;
    }

    double Length(IN const Vector3& Vector_)
    {
        return std::sqrt(Dot(Vector_, Vector_));
    }

    Vector3 Cross(IN const Vector3& Left_, IN const Vector3& Right_)
    {
        return {
            Left_.Y * Right_.Z - Left_.Z * Right_.Y,
            Left_.Z * Right_.X - Left_.X * Right_.Z,
            Left_.X * Right_.Y - Left_.Y * Right_.X
        };
    }

    bool TryNormalize(IN const Vector3& Vector_, OUT Direction3& Direction_)
    {
        const auto _Length = Length(Vector_);
        if (_Length <= kEpsilon)
        {
            return false;
        }
        Direction_ = {
            Vector_.X / _Length,
            Vector_.Y / _Length,
            Vector_.Z / _Length
        };
        return true;
    }

    Point2 Project(IN const Point3& Point_)
    {
        // The reset BRep is X-axial, so (Y, Z) are the YOZ coordinates.
        return { Point_.Y, Point_.Z };
    }

    double Distance(IN const Point2& Left_, IN const Point2& Right_)
    {
        return std::hypot(Left_.X - Right_.X, Left_.Y - Right_.Y);
    }

    double Dot2(IN const Point2& Left_, IN const Point2& Right_)
    {
        return Left_.X * Right_.X + Left_.Y * Right_.Y;
    }

    double Cross2(IN const Point2& Left_, IN const Point2& Right_)
    {
        return Left_.X * Right_.Y - Left_.Y * Right_.X;
    }

    bool TryDirection2(IN const Vector3& Vector_, OUT Direction2& Direction_)
    {
        const auto _Length = std::hypot(Vector_.Y, Vector_.Z);
        if (_Length <= kEpsilon)
        {
            return false;
        }
        Direction_ = { Vector_.Y / _Length, Vector_.Z / _Length };
        return true;
    }

    bool IsPerpendicularToX(IN const Direction3& Direction_, IN double ToleranceRadians_)
    {
        Direction3 _Unit;
        if (!TryNormalize(ToVector(Direction_), _Unit))
        {
            return false;
        }
        return std::abs(_Unit.X) <= std::sin(ToleranceRadians_);
    }

    bool IsParallelToX(IN const Direction3& Direction_, IN double ToleranceRadians_)
    {
        Direction3 _Unit;
        if (!TryNormalize(ToVector(Direction_), _Unit))
        {
            return false;
        }
        return std::abs(_Unit.X) >= std::cos(ToleranceRadians_);
    }

    bool TryBSplineGenerator(
        IN const iCAX::GeometryData::BSplineSurface3& Surface_,
        IN bool AlongV_,
        IN double ToleranceRadians_,
        OUT Direction3& Direction_)
    {
        if (Surface_.UCount < 2 || Surface_.VCount < 2
            || Surface_.Poles.size() != static_cast<std::size_t>(Surface_.UCount) * Surface_.VCount)
        {
            return false;
        }
        if ((AlongV_ && (Surface_.VDegree != 1 || Surface_.VCount < 2))
            || (!AlongV_ && (Surface_.UDegree != 1 || Surface_.UCount < 2)))
        {
            return false;
        }

        const auto _GeneratorCount = AlongV_ ? Surface_.UCount : Surface_.VCount;
        const auto _End = AlongV_ ? Surface_.VCount - 1u : Surface_.UCount - 1u;
        Vector3 _Sum{};
        std::size_t _ValidCount = 0;
        for (std::uint32_t _Index = 0; _Index < _GeneratorCount; ++_Index)
        {
            const auto _First = AlongV_
                ? static_cast<std::size_t>(_Index) * Surface_.VCount
                : _Index;
            const auto _Last = AlongV_
                ? static_cast<std::size_t>(_Index) * Surface_.VCount + _End
                : static_cast<std::size_t>(_End) * Surface_.VCount + _Index;
            Direction3 _Generator;
            if (!TryNormalize(
                Difference(Surface_.Poles[_Last], Surface_.Poles[_First]),
                _Generator))
            {
                continue;
            }
            const auto _Vector = ToVector(_Generator);
            if (_ValidCount == 0)
            {
                _Sum = _Vector;
            }
            else
            {
                const auto _Sign = Dot(_Sum, _Vector) < 0.0 ? -1.0 : 1.0;
                _Sum = AddVectors(_Sum, Scale(_Vector, _Sign));
            }
            ++_ValidCount;
        }
        if (_ValidCount == 0 || !TryNormalize(_Sum, Direction_))
        {
            return false;
        }
        const auto _MinimumDot = std::cos(ToleranceRadians_);
        for (std::uint32_t _Index = 0; _Index < _GeneratorCount; ++_Index)
        {
            const auto _First = AlongV_
                ? static_cast<std::size_t>(_Index) * Surface_.VCount
                : _Index;
            const auto _Last = AlongV_
                ? static_cast<std::size_t>(_Index) * Surface_.VCount + _End
                : static_cast<std::size_t>(_End) * Surface_.VCount + _Index;
            Direction3 _Generator;
            if (!TryNormalize(
                Difference(Surface_.Poles[_Last], Surface_.Poles[_First]),
                _Generator)
                || std::abs(Dot(ToVector(_Generator), ToVector(Direction_))) < _MinimumDot)
            {
                return false;
            }
        }
        return true;
    }

    bool IsSideFace(
        IN const BRepFace& Face_,
        IN const SGeometryIndex& Index_,
        IN const SSectionWireOptions& Options_)
    {
        const auto* _Surface = FindSurface(Index_, Face_.Surface3Id);
        if (!_Surface)
        {
            return false;
        }
        return std::visit([&](IN const auto& Surface_) {
            using T = std::decay_t<decltype(Surface_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::PlaneSurface3>)
            {
                return IsPerpendicularToX(
                    Surface_.Placement.ZDirection,
                    Options_.dFaceParallelToleranceRadians);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::CylindricalSurface3>)
            {
                return IsParallelToX(
                    Surface_.Placement.ZDirection,
                    Options_.dFaceParallelToleranceRadians);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::ExtrusionSurface3>)
            {
                return IsParallelToX(Surface_.Direction, Options_.dFaceParallelToleranceRadians);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::RevolutionSurface3>)
            {
                // IGES can store a cylindrical wall as a revolved axial line.
                // Tessellation normals at trimmed ends do not establish its
                // analytic generator direction and used to drop whole corners.
                if (!IsParallelToX(Surface_.Axis.Direction, Options_.dFaceParallelToleranceRadians)) return false;
                return std::visit([&](const auto& basis) {
                    using C = std::decay_t<decltype(basis)>;
                    if constexpr (std::is_same_v<C, iCAX::GeometryData::Line3>
                        || std::is_same_v<C, iCAX::GeometryData::Ray3>)
                        return IsParallelToX(basis.Axis.Direction, Options_.dFaceParallelToleranceRadians);
                    else if constexpr (std::is_same_v<C, iCAX::GeometryData::Segment3>) {
                        Direction3 direction;
                        return TryNormalize(Difference(basis.End, basis.Start), direction)
                            && IsParallelToX(direction, Options_.dFaceParallelToleranceRadians);
                    }
                    else if constexpr (std::is_same_v<C, iCAX::GeometryData::BSpline3>
                        || std::is_same_v<C, iCAX::GeometryData::NURBS3>
                        || std::is_same_v<C, iCAX::GeometryData::Bezier3>) {
                        if (basis.Poles.size() < 2) return false;
                        bool hasLength = false;
                        for (const auto& pole : basis.Poles) {
                            Direction3 direction;
                            if (!TryNormalize(Difference(pole, basis.Poles.front()), direction)) continue;
                            if (!IsParallelToX(direction, Options_.dFaceParallelToleranceRadians)) return false;
                            hasLength = true;
                        }
                        return hasLength;
                    }
                    else return false;
                }, Surface_.Basis);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::BSplineSurface3>)
            {
                // A planar spline can use oblique U/V directions (or higher
                // degrees). Neither parameter direction has to be the extrusion
                // direction. Classify the geometric support plane first.
                const auto _Native = iCAX::OpenCascade::BuildOpenCascadeSurface(*_Surface);
                if (!_Native.IsNull())
                {
                    const GeomLib_IsPlanarSurface _Planar(_Native, Options_.dConnectionTolerance);
                    if (_Planar.IsPlanar())
                    {
                        const auto _Normal = _Planar.Plan().Axis().Direction();
                        return std::abs(_Normal.X())
                            <= std::sin(Options_.dFaceParallelToleranceRadians);
                    }
                }
                Direction3 _Generator;
                return (TryBSplineGenerator(
                    Surface_,
                    true,
                    Options_.dFaceParallelToleranceRadians,
                    _Generator)
                    && IsParallelToX(
                        _Generator,
                        Options_.dFaceParallelToleranceRadians))
                    || (TryBSplineGenerator(
                        Surface_,
                        false,
                        Options_.dFaceParallelToleranceRadians,
                        _Generator)
                        && IsParallelToX(
                            _Generator,
                            Options_.dFaceParallelToleranceRadians));
            }
            else
            {
                const auto* _Mesh = FindTriangulation(Index_, Face_.Triangulation3Id);
                if (!_Mesh)
                {
                    return false;
                }
                bool _HasNormal = false;
                for (const auto& _Triangle : _Mesh->Triangles)
                {
                    if (_Triangle[0] >= _Mesh->Vertices.size()
                        || _Triangle[1] >= _Mesh->Vertices.size()
                        || _Triangle[2] >= _Mesh->Vertices.size())
                    {
                        continue;
                    }
                    const auto _Normal = Cross(
                        Difference(_Mesh->Vertices[_Triangle[1]], _Mesh->Vertices[_Triangle[0]]),
                        Difference(_Mesh->Vertices[_Triangle[2]], _Mesh->Vertices[_Triangle[0]]));
                    if (Length(_Normal) <= kEpsilon)
                    {
                        continue;
                    }
                    Direction3 _UnitNormal;
                    if (!TryNormalize(_Normal, _UnitNormal))
                    {
                        continue;
                    }
                    _HasNormal = true;
                    if (!IsPerpendicularToX(
                        _UnitNormal,
                        Options_.dFaceParallelToleranceRadians))
                    {
                        return false;
                    }
                }
                return _HasNormal;
            }
        }, *_Surface);
    }

    std::optional<Point3> EvaluateEndpoint(
        IN const Curve3& Curve_,
        IN const ParameterRange& Range_,
        IN bool Last_)
    {
        return std::visit([&](IN const auto& Value_) -> std::optional<Point3> {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Line3>
                || std::is_same_v<T, iCAX::GeometryData::Ray3>)
            {
                return Add(
                    Value_.Axis.Location,
                    Scale(ToVector(Value_.Axis.Direction), Last_ ? Range_.Last : Range_.First));
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment3>)
            {
                return Last_ ? Value_.End : Value_.Start;
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle3>)
            {
                const auto _Angle = Last_ ? Range_.Last : Range_.First;
                return Add(
                    Value_.Placement.Location,
                    AddVectors(
                        Scale(ToVector(Value_.Placement.XDirection), Value_.Radius * std::cos(_Angle)),
                        Scale(ToVector(Value_.Placement.YDirection), Value_.Radius * std::sin(_Angle))));
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc3>)
            {
                const auto _Angle = Last_ ? Value_.EndAngle : Value_.StartAngle;
                return Add(
                    Value_.Basis.Placement.Location,
                    AddVectors(
                        Scale(ToVector(Value_.Basis.Placement.XDirection), Value_.Basis.Radius * std::cos(_Angle)),
                        Scale(ToVector(Value_.Basis.Placement.YDirection), Value_.Basis.Radius * std::sin(_Angle))));
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse3>)
            {
                const auto _Angle = Last_ ? Range_.Last : Range_.First;
                return Add(
                    Value_.Placement.Location,
                    AddVectors(
                        Scale(ToVector(Value_.Placement.XDirection), Value_.MajorRadius * std::cos(_Angle)),
                        Scale(ToVector(Value_.Placement.YDirection), Value_.MinorRadius * std::sin(_Angle))));
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc3>)
            {
                const auto _Angle = Last_ ? Value_.EndAngle : Value_.StartAngle;
                return Add(
                    Value_.Basis.Placement.Location,
                    AddVectors(
                        Scale(ToVector(Value_.Basis.Placement.XDirection), Value_.Basis.MajorRadius * std::cos(_Angle)),
                        Scale(ToVector(Value_.Basis.Placement.YDirection), Value_.Basis.MinorRadius * std::sin(_Angle))));
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Polyline3>)
            {
                if (Value_.Points.empty()) return std::nullopt;
                return Last_ ? Value_.Points.back() : Value_.Points.front();
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Bezier3>
                || std::is_same_v<T, iCAX::GeometryData::BSpline3>
                || std::is_same_v<T, iCAX::GeometryData::NURBS3>)
            {
                if (Value_.Poles.empty()) return std::nullopt;
                return Last_ ? Value_.Poles.back() : Value_.Poles.front();
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Clothoid3>)
            {
                return Add(
                    Value_.Placement.Location,
                    Last_ ? Scale(ToVector(Value_.Placement.XDirection), Value_.Length) : Vector3{});
            }
            return std::nullopt;
        }, Curve_);
    }

    bool TryPlacement2(
        IN const iCAX::GeometryData::Placement3& Placement_,
        OUT iCAX::GeometryData::Placement2& Placement_2_)
    {
        Direction2 _X;
        Direction2 _Y;
        if (!TryDirection2(ToVector(Placement_.XDirection), _X)
            || !TryDirection2(ToVector(Placement_.YDirection), _Y))
        {
            return false;
        }
        if (std::abs(_X.X * _Y.X + _X.Y * _Y.Y) > 1.0e-6)
        {
            return false;
        }
        Placement_2_ = { Project(Placement_.Location), _X, _Y };
        return true;
    }

    std::optional<Curve2> FlattenCurve(IN const Curve3& Curve_)
    {
        std::optional<Curve2> _Result;
        std::visit([&](IN const auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Line3>)
            {
                Direction2 _Direction;
                if (TryDirection2(ToVector(Value_.Axis.Direction), _Direction))
                {
                    _Result = iCAX::GeometryData::Line2{ {
                        Project(Value_.Axis.Location), _Direction } };
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ray3>)
            {
                Direction2 _Direction;
                if (TryDirection2(ToVector(Value_.Axis.Direction), _Direction))
                {
                    _Result = iCAX::GeometryData::Ray2{ {
                        Project(Value_.Axis.Location), _Direction }, Value_.First };
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment3>)
            {
                const auto _Start = Project(Value_.Start);
                const auto _End = Project(Value_.End);
                if (Distance(_Start, _End) > kEpsilon)
                {
                    _Result = iCAX::GeometryData::Segment2{ _Start, _End };
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle3>)
            {
                iCAX::GeometryData::Placement2 _Placement;
                if (TryPlacement2(Value_.Placement, _Placement))
                {
                    _Result = iCAX::GeometryData::Circle2{ _Placement, Value_.Radius };
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc3>)
            {
                iCAX::GeometryData::Placement2 _Placement;
                if (TryPlacement2(Value_.Basis.Placement, _Placement))
                {
                    _Result = iCAX::GeometryData::Arc2{
                        { _Placement, Value_.Basis.Radius },
                        Value_.StartAngle,
                        Value_.EndAngle,
                        Value_.CounterClockwise };
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse3>)
            {
                iCAX::GeometryData::Placement2 _Placement;
                if (TryPlacement2(Value_.Placement, _Placement))
                {
                    _Result = iCAX::GeometryData::Ellipse2{
                        _Placement, Value_.MajorRadius, Value_.MinorRadius };
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc3>)
            {
                iCAX::GeometryData::Placement2 _Placement;
                if (TryPlacement2(Value_.Basis.Placement, _Placement))
                {
                    _Result = iCAX::GeometryData::EllipseArc2{
                        { _Placement, Value_.Basis.MajorRadius, Value_.Basis.MinorRadius },
                        Value_.StartAngle,
                        Value_.EndAngle,
                        Value_.CounterClockwise };
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Polyline3>)
            {
                iCAX::GeometryData::Polyline2 _Polyline;
                _Polyline.Closed = Value_.Closed;
                for (const auto& _Point : Value_.Points)
                {
                    _Polyline.Points.push_back(Project(_Point));
                }
                _Result = std::move(_Polyline);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Bezier3>)
            {
                iCAX::GeometryData::Bezier2 _Bezier;
                for (const auto& _Point : Value_.Poles)
                {
                    _Bezier.Poles.push_back(Project(_Point));
                }
                _Result = std::move(_Bezier);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::BSpline3>)
            {
                iCAX::GeometryData::BSpline2 _BSpline;
                _BSpline.Degree = Value_.Degree;
                _BSpline.Knots = Value_.Knots;
                _BSpline.Multiplicities = Value_.Multiplicities;
                _BSpline.Periodic = Value_.Periodic;
                for (const auto& _Point : Value_.Poles)
                {
                    _BSpline.Poles.push_back(Project(_Point));
                }
                _Result = std::move(_BSpline);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::NURBS3>)
            {
                iCAX::GeometryData::NURBS2 _NURBS;
                _NURBS.Degree = Value_.Degree;
                _NURBS.Weights = Value_.Weights;
                _NURBS.Knots = Value_.Knots;
                _NURBS.Multiplicities = Value_.Multiplicities;
                _NURBS.Periodic = Value_.Periodic;
                for (const auto& _Point : Value_.Poles)
                {
                    _NURBS.Poles.push_back(Project(_Point));
                }
                _Result = std::move(_NURBS);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Clothoid3>)
            {
                iCAX::GeometryData::Placement2 _Placement;
                if (TryPlacement2(Value_.Placement, _Placement))
                {
                    _Result = iCAX::GeometryData::Clothoid2{
                        _Placement,
                        Value_.StartCurvature,
                        Value_.EndCurvature,
                        Value_.Length };
                }
            }
        }, Curve_);
        return _Result;
    }

    std::vector<Point2> SampleCurve(
        IN const Curve2& Curve_,
        IN std::size_t SampleCount_)
    {
        const auto _Count = std::max<std::size_t>(8, SampleCount_);
        return std::visit([_Count](IN const auto& Value_) {
            using T = std::decay_t<decltype(Value_)>;
            std::vector<Point2> _Result;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Line2>)
            {
                _Result.push_back(Value_.Axis.Location);
                _Result.push_back({
                    Value_.Axis.Location.X + Value_.Axis.Direction.X,
                    Value_.Axis.Location.Y + Value_.Axis.Direction.Y });
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ray2>)
            {
                _Result.push_back({
                    Value_.Axis.Location.X + Value_.Axis.Direction.X * Value_.First,
                    Value_.Axis.Location.Y + Value_.Axis.Direction.Y * Value_.First });
                _Result.push_back({
                    Value_.Axis.Location.X + Value_.Axis.Direction.X * (Value_.First + 1.0),
                    Value_.Axis.Location.Y + Value_.Axis.Direction.Y * (Value_.First + 1.0) });
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment2>)
            {
                _Result = { Value_.Start, Value_.End };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>)
            {
                for (std::size_t _Index = 0; _Index <= _Count; ++_Index)
                {
                    const auto _Angle = 2.0 * kPi * static_cast<double>(_Index) / _Count;
                    _Result.push_back({
                        Value_.Placement.Location.X
                            + Value_.Radius * (Value_.Placement.XDirection.X * std::cos(_Angle)
                                + Value_.Placement.YDirection.X * std::sin(_Angle)),
                        Value_.Placement.Location.Y
                            + Value_.Radius * (Value_.Placement.XDirection.Y * std::cos(_Angle)
                                + Value_.Placement.YDirection.Y * std::sin(_Angle)) });
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
            {
                const auto _Delta = Value_.CounterClockwise
                    ? Value_.EndAngle - Value_.StartAngle
                    : Value_.StartAngle - Value_.EndAngle;
                for (std::size_t _Index = 0; _Index <= _Count; ++_Index)
                {
                    const auto _Angle = Value_.StartAngle
                        + _Delta * static_cast<double>(_Index) / _Count;
                    _Result.push_back({
                        Value_.Basis.Placement.Location.X
                            + Value_.Basis.Radius * (Value_.Basis.Placement.XDirection.X * std::cos(_Angle)
                                + Value_.Basis.Placement.YDirection.X * std::sin(_Angle)),
                        Value_.Basis.Placement.Location.Y
                            + Value_.Basis.Radius * (Value_.Basis.Placement.XDirection.Y * std::cos(_Angle)
                                + Value_.Basis.Placement.YDirection.Y * std::sin(_Angle)) });
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
            {
                for (std::size_t _Index = 0; _Index <= _Count; ++_Index)
                {
                    const auto _Angle = 2.0 * kPi * static_cast<double>(_Index) / _Count;
                    _Result.push_back({
                        Value_.Placement.Location.X
                            + Value_.MajorRadius * Value_.Placement.XDirection.X * std::cos(_Angle)
                            + Value_.MinorRadius * Value_.Placement.YDirection.X * std::sin(_Angle),
                        Value_.Placement.Location.Y
                            + Value_.MajorRadius * Value_.Placement.XDirection.Y * std::cos(_Angle)
                            + Value_.MinorRadius * Value_.Placement.YDirection.Y * std::sin(_Angle) });
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc2>)
            {
                const auto _Delta = Value_.CounterClockwise
                    ? Value_.EndAngle - Value_.StartAngle
                    : Value_.StartAngle - Value_.EndAngle;
                for (std::size_t _Index = 0; _Index <= _Count; ++_Index)
                {
                    const auto _Angle = Value_.StartAngle
                        + _Delta * static_cast<double>(_Index) / _Count;
                    _Result.push_back({
                        Value_.Basis.Placement.Location.X
                            + Value_.Basis.MajorRadius * Value_.Basis.Placement.XDirection.X * std::cos(_Angle)
                            + Value_.Basis.MinorRadius * Value_.Basis.Placement.YDirection.X * std::sin(_Angle),
                        Value_.Basis.Placement.Location.Y
                            + Value_.Basis.MajorRadius * Value_.Basis.Placement.XDirection.Y * std::cos(_Angle)
                            + Value_.Basis.MinorRadius * Value_.Basis.Placement.YDirection.Y * std::sin(_Angle) });
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Polyline2>)
            {
                _Result = Value_.Points;
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Bezier2>
                || std::is_same_v<T, iCAX::GeometryData::BSpline2>
                || std::is_same_v<T, iCAX::GeometryData::NURBS2>)
            {
                _Result = Value_.Poles;
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Clothoid2>)
            {
                _Result = {
                    Value_.Placement.Location,
                    {
                        Value_.Placement.Location.X + Value_.Placement.XDirection.X * Value_.Length,
                        Value_.Placement.Location.Y + Value_.Placement.XDirection.Y * Value_.Length
                    }
                };
            }
            return _Result;
        }, Curve_);
    }

    std::optional<SSectionWireEdge> ProjectAxialSurfaceBoundary(
        const TopoDS_Edge& Edge_, const TopoDS_Face& Face_, std::uint64_t SourceId_,
        const SSectionWireOptions& Options_,
        const iCAX::GeometryData::BSplineSurface3* Spline_ = nullptr,
        bool* IsPoint_ = nullptr)
    {
        // The caller has established an axial side surface. On cylinders
        // (including revolved lines) U is an angle; spline extrusions may
        // have a general transverse profile. The face pcurve is authoritative;
        // separately approximated 3D intersection splines must not create
        // duplicate almost-coincident tracks in the planar graph.
        double first, last;
        const auto pc = BRep_Tool::CurveOnSurface(Edge_, Face_, first, last);
        if (pc.IsNull()) return std::nullopt;
        Bnd_Box2d bounds;
        BndLib_Add2dCurve::AddOptimal(pc, first, last, 0.0, bounds);
        if (bounds.IsVoid()) return std::nullopt;
        // The box's safety gap is not part of the pcurve's parameter image.
        // Turning it into extra angular coverage extends fillets past their
        // tangent vertices and changes half-edge ordering at those vertices.
        bounds.SetGap(0.0);
        double u0, v0, u1, v1; bounds.Get(u0, v0, u1, v1);
        const auto surface = BRep_Tool::Surface(Face_);
        const bool linearExtrusion=GeomAdaptor_Surface(surface).GetType()==GeomAbs_SurfaceOfExtrusion;
        if (Spline_ || linearExtrusion)
        {
            // Recover the transverse isocurve of a proven linear extrusion.
            // Keep exact circular support when available, otherwise preserve
            // the general spline. All boundaries on this face consequently
            // use one support, including independently approximated ends.
            Direction3 generator;
            const auto axial = [&](bool alongV) {
                return TryBSplineGenerator(*Spline_, alongV,
                    Options_.dFaceParallelToleranceRadians, generator)
                    && std::abs(generator.X) >= std::cos(Options_.dFaceParallelToleranceRadians);
            };
            const bool alongV = linearExtrusion || axial(true);
            if (!alongV && !axial(false)) return std::nullopt;
            const auto iso = alongV ? surface->VIso((v0 + v1) * 0.5)
                : surface->UIso((u0 + u1) * 0.5);
            if(Options_.Progress)Options_.Progress("axial support source="+std::to_string(SourceId_)
                +" type="+iso->DynamicType()->Name()+" u=["+std::to_string(u0)+","+std::to_string(u1)
                +"] v=["+std::to_string(v0)+","+std::to_string(v1)+"]");
            double circleFirst = 0, circleLast = 0, gap = 0;
            const auto analytical = GeomConvert_CurveToAnaCurve::ComputeCircle(iso,
                Options_.dConnectionTolerance, iso->FirstParameter(), iso->LastParameter(),
                circleFirst, circleLast, gap);
            const auto exactCircle = Handle(Geom_Circle)::DownCast(analytical);
            if (exactCircle.IsNull() || gap > Options_.dConnectionTolerance
                || std::abs(exactCircle->Axis().Direction().X())
                    < std::cos(Options_.dFaceParallelToleranceRadians))
            {
                // General spline extrusions need the same surface-based
                // support as cylinders. Imported 3D trimming curves are
                // independently approximated and may differ from this exact
                // isocurve by the source edge tolerance. Preserve the spline
                // and its actual UV coverage instead of fitting that error.
                const auto spline = Handle(Geom_BSplineCurve)::DownCast(iso);
                const double t0 = alongV ? u0 : v0, t1 = alongV ? u1 : v1;
                if (spline.IsNull()) return std::nullopt;
                if (t1 <= t0) { if(IsPoint_)*IsPoint_=true;return std::nullopt; }
                const auto trimmed = Handle(Geom_BSplineCurve)::DownCast(spline->Copy());
                trimmed->Segment(t0, t1);
                iCAX::GeometryData::NURBS2 projected;
                projected.Degree = trimmed->Degree();
                projected.Periodic = trimmed->IsPeriodic();
                for (int i = 1; i <= trimmed->NbPoles(); ++i)
                {
                    const auto p = trimmed->Pole(i);
                    projected.Poles.push_back({p.Y(), p.Z()});
                    projected.Weights.push_back(trimmed->Weight(i));
                }
                for (int i = 1; i <= trimmed->NbKnots(); ++i)
                {
                    projected.Knots.push_back(trimmed->Knot(i));
                    projected.Multiplicities.push_back(trimmed->Multiplicity(i));
                }
                SSectionWireEdge result;
                result.SourceEdgeId = SourceId_;
                result.Curve = std::move(projected);
                result.Range = {trimmed->FirstParameter(), trimmed->LastParameter()};
                for (std::size_t i = 0; i <= Options_.nCurveSampleCount; ++i)
                {
                    const auto p = trimmed->Value(result.Range.First
                        + (result.Range.Last - result.Range.First) * i / Options_.nCurveSampleCount);
                    result.Samples.push_back({p.Y(), p.Z()});
                }
                result.Start = result.Samples.front(); result.End = result.Samples.back();
                if (std::all_of(std::get<iCAX::GeometryData::NURBS2>(result.Curve).Poles.begin(),
                        std::get<iCAX::GeometryData::NURBS2>(result.Curve).Poles.end(), [&](const auto& p) {
                        return Distance(p, result.Start) <= Options_.dConnectionTolerance;
                    })) { if(IsPoint_)*IsPoint_=true;return std::nullopt; }
                const double chord = Distance(result.Start, result.End);
                if (chord > Options_.dConnectionTolerance)
                {
                    const Point2 direction{(result.End.X - result.Start.X) / chord,
                        (result.End.Y - result.Start.Y) / chord};
                    const auto& value = std::get<iCAX::GeometryData::NURBS2>(result.Curve);
                    if (std::all_of(value.Poles.begin(), value.Poles.end(), [&](const auto& p) {
                            const Point2 delta{p.X - result.Start.X, p.Y - result.Start.Y};
                            const double along = Dot2(delta, direction);
                            return std::abs(delta.X * direction.Y - delta.Y * direction.X)
                                    <= Options_.dConnectionTolerance
                                && along >= -Options_.dConnectionTolerance
                                && along <= chord + Options_.dConnectionTolerance;
                        }))
                    {
                        result.Curve = iCAX::GeometryData::Line2{{result.Start, {direction.X, direction.Y}}};
                        result.Range = {0.0, chord};
                    }
                }
                return result;
            }
            const auto circle = exactCircle->Circ();
            const double t0 = alongV ? u0 : v0, t1 = alongV ? u1 : v1;
            if (t1 <= t0) { if(IsPoint_)*IsPoint_=true;return std::nullopt; }
            double angle = ElCLib::Parameter(circle, iso->Value(t0));
            const double start = angle;
            int sense = 0;
            for (std::size_t i = 1; i <= Options_.nCurveSampleCount; ++i)
            {
                const auto next = ElCLib::Parameter(circle, iso->Value(t0
                    + (t1 - t0) * i / Options_.nCurveSampleCount));
                const auto delta = std::remainder(next - angle, 2.0 * kPi);
                if (std::abs(delta) * circle.Radius() > Options_.dConnectionTolerance)
                {
                    const int current = delta > 0 ? 1 : -1;
                    if (sense && sense != current) return std::nullopt;
                    sense = current;
                }
                angle += delta;
            }
            if (std::abs(angle - start) * circle.Radius() <= Options_.dConnectionTolerance)
                { if(IsPoint_)*IsPoint_=true;return std::nullopt; }
            const auto x = circle.Position().XDirection(), y = circle.Position().YDirection();
            const iCAX::GeometryData::Circle2 projected{{{circle.Location().Y(), circle.Location().Z()},
                {x.Y(), x.Z()}, {y.Y(), y.Z()}}, circle.Radius()};
            SSectionWireEdge result;
            result.SourceEdgeId = SourceId_;
            const double lo = std::min(start, angle), hi = std::max(start, angle);
            if (hi - lo >= 2.0 * kPi - Options_.dConnectionTolerance / circle.Radius())
            {
                result.Curve = projected;
                result.Range = {0, 2.0 * kPi};
            }
            else
            {
                result.Curve = iCAX::GeometryData::Arc2{projected, lo, hi, true};
                result.Range = {lo, hi};
            }
            result.Samples = SampleCurve(result.Curve, Options_.nCurveSampleCount);
            result.Start = result.Samples.front();result.End = result.Samples.back();
            return result;
        }
        const double v = (v0 + v1) * 0.5;
        const auto p0 = surface->Value(0.0, v);
        const auto p90 = surface->Value(0.5 * kPi, v);
        const auto p180 = surface->Value(kPi, v);
        const Point2 center{0.5 * (p0.Y() + p180.Y()), 0.5 * (p0.Z() + p180.Z())};
        const Point2 a{p0.Y() - center.X, p0.Z() - center.Y};
        const Point2 b{p90.Y() - center.X, p90.Z() - center.Y};
        const double radius = std::hypot(a.X, a.Y), otherRadius = std::hypot(b.X, b.Y);
        if (radius <= Options_.dConnectionTolerance || otherRadius <= Options_.dConnectionTolerance
            || std::abs(p0.X() - p180.X()) > Options_.dConnectionTolerance
            || std::abs(p90.X() - p0.X()) > Options_.dConnectionTolerance
            || std::abs(radius - otherRadius) > Options_.dConnectionTolerance
            || std::abs(Dot2(a, b)) / radius > Options_.dConnectionTolerance)
            return std::nullopt;
        if ((u1 - u0) * radius <= Options_.dConnectionTolerance) { if(IsPoint_)*IsPoint_=true;return std::nullopt; }
        const iCAX::GeometryData::Circle2 circle{{center,
            {a.X / radius, a.Y / radius}, {b.X / otherRadius, b.Y / otherRadius}}, radius};
        SSectionWireEdge result;
        result.SourceEdgeId = SourceId_;
        if (u1 - u0 >= 2.0 * kPi - Options_.dConnectionTolerance / radius)
        {
            result.Curve = circle;
            result.Range = {0.0, 2.0 * kPi};
        }
        else
        {
            result.Curve = iCAX::GeometryData::Arc2{circle, u0, u1, true};
            result.Range = {u0, u1};
        }
        result.Samples = SampleCurve(result.Curve, Options_.nCurveSampleCount);
        result.Start = result.Samples.front(); result.End = result.Samples.back();
        return result;
    }

    std::optional<SSectionWireEdge> MakeProjectedEdge(
        IN const BRepEdge& Edge_,
        IN std::uint64_t StartVertexID_,
        IN std::uint64_t EndVertexID_,
        IN const Curve3& Curve_,
        IN const SGeometryIndex& Index_,
        IN const SSectionWireOptions& Options_)
    {
        const auto* _StartVertex = FindVertex(Index_, StartVertexID_);
        const auto* _EndVertex = FindVertex(Index_, EndVertexID_);
        const auto _Start3 = _StartVertex
            ? std::optional<Point3>(_StartVertex->Position)
            : EvaluateEndpoint(Curve_, Edge_.Range, false);
        const auto _End3 = _EndVertex
            ? std::optional<Point3>(_EndVertex->Position)
            : EvaluateEndpoint(Curve_, Edge_.Range, true);
        if (!_Start3 || !_End3)
        {
            return std::nullopt;
        }

        // A conic in a plane containing the extrusion axis projects to a
        // finite line interval, not a point. Its endpoints alone need not
        // bound that interval (a semicircle can return to the same point).
        // Evaluate the sinusoid's stationary parameters as well as the trim
        // endpoints to retain the complete, exact projected coverage.
        const bool _Conic = std::holds_alternative<iCAX::GeometryData::Circle3>(Curve_)
            || std::holds_alternative<iCAX::GeometryData::Arc3>(Curve_)
            || std::holds_alternative<iCAX::GeometryData::Ellipse3>(Curve_)
            || std::holds_alternative<iCAX::GeometryData::EllipseArc3>(Curve_);
        if (_Conic)
        {
            const auto _Exact = iCAX::OpenCascade::BuildOpenCascadeCurve(Curve_);
            if (!_Exact.IsNull())
            {
                const auto _P0 = _Exact->Value(0.0);
                const auto _P90 = _Exact->Value(0.5 * kPi);
                const auto _P180 = _Exact->Value(kPi);
                const Point2 _Center{0.5 * (_P0.Y() + _P180.Y()), 0.5 * (_P0.Z() + _P180.Z())};
                const Point2 _A{_P0.Y() - _Center.X, _P0.Z() - _Center.Y};
                const Point2 _B{_P90.Y() - _Center.X, _P90.Z() - _Center.Y};
                const double _ALength = std::hypot(_A.X, _A.Y), _BLength = std::hypot(_B.X, _B.Y);
                const auto _Basis = _ALength >= _BLength ? _A : _B;
                const double _Length = std::max(_ALength, _BLength);
                if (_Length > Options_.dConnectionTolerance)
                {
                    const Point2 _Direction{_Basis.X / _Length, _Basis.Y / _Length};
                    // Bound the transverse amplitude over the whole conic,
                    // rather than deciding collinearity from sampled chords.
                    if (std::hypot(Cross2(_Direction, _A), Cross2(_Direction, _B))
                        <= Options_.dConnectionTolerance)
                    {
                        const double _Cos = Dot2(_Direction, _A), _Sin = Dot2(_Direction, _B);
                        const double _First = std::min(Edge_.Range.First, Edge_.Range.Last);
                        const double _Last = std::max(Edge_.Range.First, Edge_.Range.Last);
                        const auto _Value = [&](double t) { return _Cos * std::cos(t) + _Sin * std::sin(t); };
                        double _Min = std::min(_Value(_First), _Value(_Last));
                        double _Max = std::max(_Value(_First), _Value(_Last));
                        const double _Stationary = std::atan2(_Sin, _Cos);
                        for (int i = 0; i < 2; ++i)
                        {
                            const double _Base = _Stationary + i * kPi;
                            const double _T = _Base + std::ceil((_First - _Base) / (2.0 * kPi)) * (2.0 * kPi);
                            if (_T <= _Last + kEpsilon)
                            {
                                _Min = std::min(_Min, _Value(_T));
                                _Max = std::max(_Max, _Value(_T));
                            }
                        }
                        if (_Max - _Min <= Options_.dConnectionTolerance) return std::nullopt;
                        SSectionWireEdge _Line;
                        _Line.SourceEdgeId = Edge_.Id;
                        _Line.Start = {_Center.X + _Min * _Direction.X, _Center.Y + _Min * _Direction.Y};
                        _Line.End = {_Center.X + _Max * _Direction.X, _Center.Y + _Max * _Direction.Y};
                        _Line.Curve = iCAX::GeometryData::Line2{{_Line.Start, {_Direction.X, _Direction.Y}}};
                        _Line.Range = {0.0, _Max - _Min};
                        _Line.Samples = {_Line.Start, _Line.End};
                        return _Line;
                    }
                }
            }
        }
        const auto _Curve2 = FlattenCurve(Curve_);
        if (!_Curve2)
        {
            return std::nullopt;
        }

        SSectionWireEdge _Result;
        _Result.SourceEdgeId = Edge_.Id;
        _Result.Curve = *_Curve2;
        _Result.Range = Edge_.Range;
        // Imported circle/ellipse edges can be trimmed through the BRep
        // range even when the underlying curve is a complete conic.
        if (std::abs(Edge_.Range.First) > kEpsilon
            || std::abs(Edge_.Range.Last - 2.0 * kPi) > kEpsilon)
        {
            if (const auto* _Circle = std::get_if<iCAX::GeometryData::Circle2>(&*_Curve2))
                _Result.Curve = iCAX::GeometryData::Arc2{
                    *_Circle, Edge_.Range.First, Edge_.Range.Last, true };
            else if (const auto* _Ellipse = std::get_if<iCAX::GeometryData::Ellipse2>(&*_Curve2))
                _Result.Curve = iCAX::GeometryData::EllipseArc2{
                    *_Ellipse, Edge_.Range.First, Edge_.Range.Last, true };
        }
        _Result.Start = Project(*_Start3);
        _Result.End = Project(*_End3);
        // STEP intersection curves may remain splines after projection even
        // when all of their control points lie on a straight generator. Use
        // that control-polygon bound (not a sampled chord fit) to give them
        // the same representation as coincident analytic line edges.
        const auto _ChordLength = Distance(_Result.Start, _Result.End);
        // Projection shortens an oblique line. FlattenCurve normalizes its 2D
        // direction, so retaining the 3D parameter range extends the projected
        // edge beyond its vertices and introduces false planar intersections.
        // Reparameterize the finite straight edge by its projected chord.
        if (std::holds_alternative<iCAX::GeometryData::Line2>(_Result.Curve)
            || std::holds_alternative<iCAX::GeometryData::Ray2>(_Result.Curve)
            || std::holds_alternative<iCAX::GeometryData::Segment2>(_Result.Curve))
        {
            if (_ChordLength <= Options_.dConnectionTolerance) return std::nullopt;
            _Result.Curve = iCAX::GeometryData::Line2{{_Result.Start,
                {(_Result.End.X - _Result.Start.X) / _ChordLength,
                 (_Result.End.Y - _Result.Start.Y) / _ChordLength}}};
            _Result.Range = {0.0, _ChordLength};
        }
        const auto _StraightCoverage = std::visit([&](const auto& Value_)
            -> std::optional<std::pair<Point2,Point2>> {
                using T = std::decay_t<decltype(Value_)>;
                if constexpr (std::is_same_v<T, iCAX::GeometryData::Bezier2>
                    || std::is_same_v<T, iCAX::GeometryData::BSpline2>
                    || std::is_same_v<T, iCAX::GeometryData::NURBS2>)
                {
                    if (Value_.Poles.size() < 2) return std::nullopt;
                    if constexpr (std::is_same_v<T, iCAX::GeometryData::NURBS2>)
                        if (std::any_of(Value_.Weights.begin(), Value_.Weights.end(),
                                [](double Weight_) { return !std::isfinite(Weight_) || Weight_ <= 0.0; })) return std::nullopt;
                    const auto origin=Value_.Poles.front();
                    Point2 direction{};double length=0;
                    for(const auto& pole:Value_.Poles){
                        const auto distance=Distance(origin,pole);
                        if(distance>length){length=distance;direction={pole.X-origin.X,pole.Y-origin.Y};}
                    }
                    if(length<=Options_.dConnectionTolerance)return std::nullopt;
                    direction={direction.X/length,direction.Y/length};
                    for (const auto& _Pole : Value_.Poles)
                    {
                        const Point2 delta{_Pole.X-origin.X,_Pole.Y-origin.Y};
                        if(std::abs(delta.X*direction.Y-delta.Y*direction.X)>Options_.dConnectionTolerance)
                            return std::nullopt;
                    }
                    // A curved 3D cut can fold back after projection, including
                    // identical projected endpoints. Its coverage is the exact
                    // trimmed scalar interval, not a U-turn in the section.
                    Bnd_Box2d bounds;
                    BndLib_Add2dCurve::AddOptimal(iCAX::OpenCascade::BuildOpenCascadeCurve2(_Result.Curve),
                        _Result.Range.First,_Result.Range.Last,0.0,bounds);
                    if(bounds.IsVoid())return std::nullopt;
                    // AddOptimal's safety gap is not part of the exact finite
                    // projected image. Keeping it extends a straight span past
                    // its adjoining arc/line vertices and opens the section.
                    bounds.SetGap(0.0);
                    double x0,y0,x1,y1;bounds.Get(x0,y0,x1,y1);
                    const bool useX=std::abs(direction.X)>=std::abs(direction.Y);
                    double a=((useX?x0:y0)-(useX?origin.X:origin.Y))/(useX?direction.X:direction.Y);
                    double b=((useX?x1:y1)-(useX?origin.X:origin.Y))/(useX?direction.X:direction.Y);
                    if(a>b)std::swap(a,b);
                    if(b-a<=Options_.dConnectionTolerance)return std::nullopt;
                    return std::pair<Point2,Point2>{{origin.X+a*direction.X,origin.Y+a*direction.Y},
                        {origin.X+b*direction.X,origin.Y+b*direction.Y}};
                }
                return std::nullopt;
            }, _Result.Curve);
        if(_StraightCoverage){
                _Result.Start=_StraightCoverage->first;_Result.End=_StraightCoverage->second;
                const auto length=Distance(_Result.Start,_Result.End);
                _Result.Curve = iCAX::GeometryData::Line2{{_Result.Start,
                    {(_Result.End.X - _Result.Start.X) / length,
                     (_Result.End.Y - _Result.Start.Y) / length}}};
                _Result.Range = {0.0,length};
        }
        // Samples must lie on the trimmed curve, never on a spline control
        // polygon or on the untrimmed supporting curve.
        if (const auto exact = iCAX::OpenCascade::BuildOpenCascadeCurve2(_Result.Curve); !exact.IsNull())
        {
            for (std::size_t i = 0; i <= Options_.nCurveSampleCount; ++i)
            {
                const auto p = exact->Value(_Result.Range.First
                    + (_Result.Range.Last - _Result.Range.First) * i / Options_.nCurveSampleCount);
                _Result.Samples.push_back({p.X(), p.Y()});
            }
        }
        else _Result.Samples = SampleCurve(_Result.Curve, Options_.nCurveSampleCount);
        if (_Result.Samples.empty())
        {
            _Result.Samples = { _Result.Start, _Result.End };
        }
        else if (Distance(_Result.Start, _Result.End) > Options_.dConnectionTolerance)
        {
            _Result.Samples.front() = _Result.Start;
            _Result.Samples.back() = _Result.End;
        }

        Point2 _Minimum = _Result.Samples.front();
        Point2 _Maximum = _Result.Samples.front();
        for (const auto& _Point : _Result.Samples)
        {
            _Minimum.X = std::min(_Minimum.X, _Point.X);
            _Minimum.Y = std::min(_Minimum.Y, _Point.Y);
            _Maximum.X = std::max(_Maximum.X, _Point.X);
            _Maximum.Y = std::max(_Maximum.Y, _Point.Y);
        }
        if (std::hypot(_Maximum.X - _Minimum.X, _Maximum.Y - _Minimum.Y)
            <= Options_.dConnectionTolerance)
        {
            return std::nullopt;
        }
        return _Result;
    }

    bool SamePoint(IN const Point2& Left_, IN const Point2& Right_, IN double Tolerance_)
    {
        return Distance(Left_, Right_) <= Tolerance_;
    }

    bool SameSamples(
        IN const std::vector<Point2>& Left_,
        IN const std::vector<Point2>& Right_,
        IN bool Reverse_,
        IN double Tolerance_)
    {
        if (Left_.size() != Right_.size())
        {
            return false;
        }
        for (std::size_t _Index = 0; _Index < Left_.size(); ++_Index)
        {
            const auto _RightIndex = Reverse_
                ? Right_.size() - 1 - _Index
                : _Index;
            if (!SamePoint(Left_[_Index], Right_[_RightIndex], Tolerance_ * 2.0))
            {
                return false;
            }
        }
        return true;
    }

    bool SameProjectedEdge(
        IN const SSectionWireEdge& Left_,
        IN const SSectionWireEdge& Right_,
        IN double Tolerance_)
    {
        const auto _SameEndpoints =
            (SamePoint(Left_.Start, Right_.Start, Tolerance_)
                && SamePoint(Left_.End, Right_.End, Tolerance_))
            || (SamePoint(Left_.Start, Right_.End, Tolerance_)
                && SamePoint(Left_.End, Right_.Start, Tolerance_));
        if (!_SameEndpoints)
        {
            return false;
        }
        const auto left = iCAX::OpenCascade::BuildOpenCascadeCurve2(Left_.Curve);
        const auto right = iCAX::OpenCascade::BuildOpenCascadeCurve2(Right_.Curve);
        if (left.IsNull() || right.IsNull()) return false;
        const auto covered = [&](const SSectionWireEdge& edge, const Handle(Geom2d_Curve)& curve,
            const SSectionWireEdge& other, const Handle(Geom2d_Curve)& target) {
            for (int i=0;i<=32;++i) {
                const auto p=curve->Value(edge.Range.First+(edge.Range.Last-edge.Range.First)*i/32.0);
                // An actual point on the target is sufficient coverage
                // evidence. Restricted spline extrema can omit a minimum on
                // nearly coincident short intervals; do not turn that solver
                // omission into a different projected edge.
                const auto u=i/32.0,span=other.Range.Last-other.Range.First;
                if(p.Distance(target->Value(other.Range.First+span*u))<=Tolerance_
                    ||p.Distance(target->Value(other.Range.Last-span*u))<=Tolerance_)continue;
                Geom2dAPI_ProjectPointOnCurve projection(p,target,
                    std::min(other.Range.First,other.Range.Last),std::max(other.Range.First,other.Range.Last));
                // Projection extrema can omit interval endpoints.
                const auto distance=std::min(p.Distance(target->Value(other.Range.First)),p.Distance(target->Value(other.Range.Last)));
                if(distance>Tolerance_ && (projection.NbPoints()==0||projection.LowerDistance()>Tolerance_))return false;
            }
            return true;
        };
        return covered(Left_,left,Right_,right)&&covered(Right_,right,Left_,left);
    }

    struct SConicInterval final
    {
        iCAX::GeometryData::Ellipse2 Basis;
        double First = 0.0;
        double Sweep = 2.0 * kPi;
        bool bCircle = false;
    };

    std::optional<SConicInterval> ConicInterval(IN const Curve2& Curve_)
    {
        std::optional<SConicInterval> _Result;
        std::visit([&](const auto& Value_) {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>)
                _Result = SConicInterval{ { Value_.Placement, Value_.Radius, Value_.Radius }, 0.0, 2.0 * kPi, true };
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
                _Result = SConicInterval{ Value_ };
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
                _Result = SConicInterval{
                    { Value_.Basis.Placement, Value_.Basis.Radius, Value_.Basis.Radius },
                    Value_.StartAngle,
                    Value_.CounterClockwise ? Value_.EndAngle - Value_.StartAngle
                        : Value_.StartAngle - Value_.EndAngle, true };
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc2>)
                _Result = SConicInterval{ Value_.Basis, Value_.StartAngle,
                    Value_.CounterClockwise ? Value_.EndAngle - Value_.StartAngle
                        : Value_.StartAngle - Value_.EndAngle };
        }, Curve_);
        return _Result;
    }

    bool IsStraightProjectedEdge(IN const SSectionWireEdge& Edge_)
    {
        return std::holds_alternative<iCAX::GeometryData::Line2>(Edge_.Curve)
            || std::holds_alternative<iCAX::GeometryData::Ray2>(Edge_.Curve)
            || std::holds_alternative<iCAX::GeometryData::Segment2>(Edge_.Curve);
    }

    // Merge geometric coverage before any endpoint-on-edge splitting. In
    // particular, a swallowed edge's endpoints must not split its host later.
    bool TryMergeProjectedEdges(
        IN OUT SSectionWireEdge& Left_,
        IN const SSectionWireEdge& Right_,
        IN const SSectionWireOptions& Options_)
    {
        const auto _Tolerance = Options_.dConnectionTolerance;
        if (IsStraightProjectedEdge(Left_) && IsStraightProjectedEdge(Right_))
        {
            const auto _Length = Distance(Left_.Start, Left_.End);
            if (_Length <= _Tolerance) return false;
            const Point2 _Direction{
                (Left_.End.X - Left_.Start.X) / _Length,
                (Left_.End.Y - Left_.Start.Y) / _Length };
            const Point2 _A{ Right_.Start.X - Left_.Start.X, Right_.Start.Y - Left_.Start.Y };
            const Point2 _B{ Right_.End.X - Left_.Start.X, Right_.End.Y - Left_.Start.Y };
            if (std::abs(Cross2(_Direction, _A)) > _Tolerance
                || std::abs(Cross2(_Direction, _B)) > _Tolerance) return false;
            const auto _First = std::min(Dot2(_Direction, _A), Dot2(_Direction, _B));
            const auto _Last = std::max(Dot2(_Direction, _A), Dot2(_Direction, _B));
            // Mere endpoint contact is not overlapping coverage.
            if (std::min(_Length, _Last) - std::max(0.0, _First) <= _Tolerance) return false;
            if (_First >= -_Tolerance && _Last <= _Length + _Tolerance) return true;
            if (_First <= _Tolerance && _Last >= _Length - _Tolerance)
            {
                Left_ = Right_;
                return true;
            }
            const auto _Begin = std::min(0.0, _First);
            const auto _End = std::max(_Length, _Last);
            const auto _Origin = Left_.Start;
            Left_.Start = { _Origin.X + _Begin * _Direction.X, _Origin.Y + _Begin * _Direction.Y };
            Left_.End = { _Origin.X + _End * _Direction.X, _Origin.Y + _End * _Direction.Y };
            Left_.Curve = iCAX::GeometryData::Line2{
                { Left_.Start, { _Direction.X, _Direction.Y } } };
            Left_.Range = { 0.0, _End - _Begin };
            Left_.Samples = { Left_.Start, Left_.End };
            return true;
        }

        const auto _Left = ConicInterval(Left_.Curve);
        const auto _Right = ConicInterval(Right_.Curve);
        if (_Left && _Right)
        {
            const auto& _A = _Left->Basis;
            const auto& _B = _Right->Basis;
            if (_Left->bCircle != _Right->bCircle
                || Distance(_A.Placement.Location, _B.Placement.Location) > _Tolerance
                || std::abs(_A.MajorRadius - _B.MajorRadius) > _Tolerance
                || std::abs(_A.MinorRadius - _B.MinorRadius) > _Tolerance
                || std::min(_A.MajorRadius, _A.MinorRadius) <= _Tolerance) return false;
            const auto _AxisDot = _A.Placement.XDirection.X * _B.Placement.XDirection.X
                + _A.Placement.XDirection.Y * _B.Placement.XDirection.Y;
            const auto _AxisCross = _A.Placement.XDirection.X * _B.Placement.XDirection.Y
                - _A.Placement.XDirection.Y * _B.Placement.XDirection.X;
            if (!_Left->bCircle && std::abs(_AxisCross) * _A.MajorRadius > _Tolerance) return false;
            const auto _Orientation = [](const auto& Placement_) {
                return Placement_.XDirection.X * Placement_.YDirection.Y
                    - Placement_.XDirection.Y * Placement_.YDirection.X;
            };
            const auto _Phase = _Left->bCircle
                ? std::atan2(_AxisCross * _Orientation(_A.Placement), _AxisDot)
                : (_AxisDot < 0.0 ? kPi : 0.0);
            const auto _Sign = _Orientation(_A.Placement) * _Orientation(_B.Placement) < 0.0 ? -1.0 : 1.0;
            const auto _Normalize = [](double Angle_) {
                const auto _Angle = std::fmod(Angle_, 2.0 * kPi);
                return _Angle < 0.0 ? _Angle + 2.0 * kPi : _Angle;
            };
            const auto _First = _Normalize(std::min(_Left->First, _Left->First + _Left->Sweep));
            const auto _Last = _First + std::min(std::abs(_Left->Sweep), 2.0 * kPi);
            const auto _OtherFirst = _Normalize(std::min(
                _Phase + _Sign * _Right->First,
                _Phase + _Sign * (_Right->First + _Right->Sweep)));
            const auto _OtherSpan = std::min(std::abs(_Right->Sweep), 2.0 * kPi);
            const auto _AngularTolerance = _Tolerance / _A.MajorRadius;
            for (int _Turn = -1; _Turn <= 1; ++_Turn)
            {
                const auto _Begin = _OtherFirst + _Turn * 2.0 * kPi;
                const auto _End = _Begin + _OtherSpan;
                if (std::min(_Last, _End) - std::max(_First, _Begin) <= _AngularTolerance) continue;
                if (_Last - _First >= 2.0 * kPi - _AngularTolerance
                    || (_Begin >= _First - _AngularTolerance && _End <= _Last + _AngularTolerance)) return true;
                if (_OtherSpan >= 2.0 * kPi - _AngularTolerance
                    || (_Begin <= _First + _AngularTolerance && _End >= _Last - _AngularTolerance))
                {
                    Left_ = Right_;
                    return true;
                }
                const auto _UnionFirst = std::min(_First, _Begin);
                const auto _UnionLast = std::max(_Last, _End);
                const bool _Full = _UnionLast - _UnionFirst >= 2.0 * kPi - _AngularTolerance;
                if (_Left->bCircle)
                {
                    const iCAX::GeometryData::Circle2 _Circle{ _A.Placement, _A.MajorRadius };
                    Left_.Curve = _Full ? Curve2(_Circle) : Curve2(iCAX::GeometryData::Arc2{
                        _Circle, _UnionFirst, _UnionLast, true });
                }
                else
                    Left_.Curve = _Full ? Curve2(_A) : Curve2(iCAX::GeometryData::EllipseArc2{
                        _A, _UnionFirst, _UnionLast, true });
                Left_.Range = _Full ? ParameterRange{ 0.0, 2.0 * kPi }
                    : ParameterRange{ _UnionFirst, _UnionLast };
                Left_.Samples = SampleCurve(Left_.Curve, Options_.nCurveSampleCount);
                Left_.Start = Left_.Samples.front();
                Left_.End = Left_.Samples.back();
                return true;
            }
            return false;
        }
        return SameProjectedEdge(Left_, Right_, _Tolerance);
    }

    std::vector<SSectionWireEdge> MergeProjectedEdges(
        IN std::vector<SSectionWireEdge> Edges_, IN const SSectionWireOptions& Options_)
    {
        // Source IDs make the retained representation independent of hash-map
        // iteration order. Restart after each merge to absorb overlap chains.
        std::sort(Edges_.begin(), Edges_.end(), [](const auto& Left_, const auto& Right_) {
            return Left_.SourceEdgeId < Right_.SourceEdgeId;
        });
        std::vector<SSectionWireEdge> _Merged;
        for (auto& _Edge : Edges_)
        {
            for (std::size_t _Index = 0; _Index < _Merged.size();)
            {
                if (TryMergeProjectedEdges(_Edge, _Merged[_Index], Options_))
                {
                    _Merged.erase(_Merged.begin() + _Index);
                    _Index = 0;
                }
                else ++_Index;
            }
            _Merged.push_back(std::move(_Edge));
        }
        return _Merged;
    }

    struct SProjectionResult final
    {
        double U = 0.0;
        Point2 Point;
        double dDistance = 0.0;
    };

    Point2 Lerp(
        IN const Point2& A_,
        IN const Point2& B_,
        IN double T_)
    {
        return {
            A_.X + (B_.X - A_.X) * T_,
            A_.Y + (B_.Y - A_.Y) * T_ };
    }

    double NormalizeParameter(
        IN const ParameterRange& Range_,
        IN double Parameter_)
    {
        const auto _Delta = Range_.Last - Range_.First;
        if (std::abs(_Delta) <= kEpsilon)
        {
            return 0.0;
        }
        return (Parameter_ - Range_.First) / _Delta;
    }

    std::optional<double> AngleParameterInRange(
        IN double Angle_,
        IN const ParameterRange& Range_)
    {
        const auto _Delta = Range_.Last - Range_.First;
        if (std::abs(_Delta) <= kEpsilon)
        {
            return std::nullopt;
        }
        double _Best = 0.0;
        double _BestDistance = (std::numeric_limits<double>::max)();
        for (int _Turn = -4; _Turn <= 4; ++_Turn)
        {
            const auto _Candidate = Angle_ + 2.0 * kPi * static_cast<double>(_Turn);
            const auto _Normalized = (_Candidate - Range_.First) / _Delta;
            const auto _DistanceToRange = _Normalized < 0.0
                ? -_Normalized
                : (_Normalized > 1.0 ? _Normalized - 1.0 : 0.0);
            if (_DistanceToRange < _BestDistance)
            {
                _BestDistance = _DistanceToRange;
                _Best = _Normalized;
            }
        }
        return _Best;
    }

    std::optional<SProjectionResult> ProjectPointToAnalyticCurve(
        IN const SSectionWireEdge& Edge_,
        IN const Point2& Point_,
        IN double Tolerance_)
    {
        std::optional<SProjectionResult> _Result;
        const auto _RangeDelta = Edge_.Range.Last - Edge_.Range.First;
        if (std::abs(_RangeDelta) <= kEpsilon)
        {
            return std::nullopt;
        }

        std::visit([&](IN const auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Line2>
                || std::is_same_v<T, iCAX::GeometryData::Ray2>)
            {
                const auto _DirectionLength = std::hypot(
                    Value_.Axis.Direction.X,
                    Value_.Axis.Direction.Y);
                if (_DirectionLength <= kEpsilon)
                {
                    return;
                }
                const Point2 _Direction{
                    Value_.Axis.Direction.X / _DirectionLength,
                    Value_.Axis.Direction.Y / _DirectionLength };
                const Point2 _Delta{
                    Point_.X - Value_.Axis.Location.X,
                    Point_.Y - Value_.Axis.Location.Y };
                const auto _Parameter = Dot2(_Delta, _Direction);
                const auto _U = NormalizeParameter(Edge_.Range, _Parameter);
                const Point2 _Projection{
                    Value_.Axis.Location.X + _Direction.X * _Parameter,
                    Value_.Axis.Location.Y + _Direction.Y * _Parameter };
                _Result = SProjectionResult{
                    _U,
                    _Projection,
                    Distance(Point_, _Projection) };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment2>)
            {
                const Point2 _Delta{
                    Value_.End.X - Value_.Start.X,
                    Value_.End.Y - Value_.Start.Y };
                const auto _LengthSquared = Dot2(_Delta, _Delta);
                if (_LengthSquared <= kEpsilon)
                {
                    return;
                }
                const Point2 _ToPoint{
                    Point_.X - Value_.Start.X,
                    Point_.Y - Value_.Start.Y };
                const auto _U = Dot2(_ToPoint, _Delta) / _LengthSquared;
                const auto _Clamped = std::clamp(_U, 0.0, 1.0);
                const auto _Projection = Lerp(Value_.Start, Value_.End, _Clamped);
                _Result = SProjectionResult{
                    _Clamped,
                    _Projection,
                    Distance(Point_, _Projection) };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>
                || std::is_same_v<T, iCAX::GeometryData::Arc2>
                || std::is_same_v<T, iCAX::GeometryData::Ellipse2>
                || std::is_same_v<T, iCAX::GeometryData::EllipseArc2>)
            {
                const auto& _Placement = [&]() -> const auto& {
                    if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>)
                    {
                        return Value_.Placement;
                    }
                    else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
                    {
                        return Value_.Basis.Placement;
                    }
                    else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
                    {
                        return Value_.Placement;
                    }
                    else
                    {
                        return Value_.Basis.Placement;
                    }
                }();
                const auto _MajorRadius = [&]() {
                    if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>
                        )
                    {
                        return Value_.Radius;
                    }
                    else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
                    {
                        return Value_.Basis.Radius;
                    }
                    else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
                    {
                        return Value_.MajorRadius;
                    }
                    else
                    {
                        return Value_.Basis.MajorRadius;
                    }
                }();
                const auto _MinorRadius = [&]() {
                    if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>
                        )
                    {
                        return Value_.Radius;
                    }
                    else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
                    {
                        return Value_.Basis.Radius;
                    }
                    else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
                    {
                        return Value_.MinorRadius;
                    }
                    else
                    {
                        return Value_.Basis.MinorRadius;
                    }
                }();
                if (_MajorRadius <= kEpsilon || _MinorRadius <= kEpsilon)
                {
                    return;
                }
                const Point2 _Delta{
                    Point_.X - _Placement.Location.X,
                    Point_.Y - _Placement.Location.Y };
                const auto _LocalX = _Delta.X * _Placement.XDirection.X
                    + _Delta.Y * _Placement.XDirection.Y;
                const auto _LocalY = _Delta.X * _Placement.YDirection.X
                    + _Delta.Y * _Placement.YDirection.Y;
                const auto _Angle = std::atan2(
                    _LocalY / _MinorRadius,
                    _LocalX / _MajorRadius);
                const auto _U = AngleParameterInRange(_Angle, Edge_.Range);
                if (!_U)
                {
                    return;
                }
                const auto _Parameter = Edge_.Range.First + _RangeDelta * *_U;
                const auto _Cos = std::cos(_Parameter);
                const auto _Sin = std::sin(_Parameter);
                const Point2 _Projection{
                    _Placement.Location.X
                        + _MajorRadius * (_Placement.XDirection.X * _Cos
                            + (_MinorRadius / _MajorRadius) * _Placement.YDirection.X * _Sin),
                    _Placement.Location.Y
                        + _MajorRadius * _Placement.XDirection.Y * _Cos
                        + _MinorRadius * _Placement.YDirection.Y * _Sin };
                // For ellipses, the normalized local-angle projection is an
                // exact point on the curve, but not the Euclidean closest
                // point. That is sufficient for detecting endpoint-on-curve.
                _Result = SProjectionResult{
                    *_U,
                    _Projection,
                    Distance(Point_, _Projection) };
            }
        }, Edge_.Curve);

        if (_Result && _Result->dDistance <= Tolerance_ * 4.0)
        {
            return _Result;
        }
        return std::nullopt;
    }

    Point2 PointOnSamples(
        IN const std::vector<Point2>& Samples_,
        IN double U_)
    {
        if (Samples_.empty())
        {
            return {};
        }
        if (Samples_.size() == 1)
        {
            return Samples_.front();
        }
        const auto _Clamped = std::clamp(U_, 0.0, 1.0);
        const auto _Position = _Clamped * static_cast<double>(Samples_.size() - 1);
        const auto _Index = std::min<std::size_t>(
            static_cast<std::size_t>(std::floor(_Position)),
            Samples_.size() - 2);
        return Lerp(Samples_[_Index], Samples_[_Index + 1], _Position - _Index);
    }

    void AppendUniqueInteriorParameter(
        IN double Parameter_,
        IN OUT std::vector<double>& Parameters_)
    {
        if (Parameter_ <= 1.0e-8 || Parameter_ >= 1.0 - 1.0e-8)
        {
            return;
        }
        if (std::none_of(
            Parameters_.begin(),
            Parameters_.end(),
            [&](IN const auto Existing_) {
                return std::abs(Existing_ - Parameter_) <= 1.0e-8;
            }))
        {
            Parameters_.push_back(Parameter_);
        }
    }

    void AppendArcStationaryParameters(
        IN double CoefficientCos_,
        IN double CoefficientSin_,
        IN double StartAngle_,
        IN double EndAngle_,
        IN bool CounterClockwise_,
        IN OUT std::vector<double>& Parameters_)
    {
        if (std::hypot(CoefficientCos_, CoefficientSin_) <= kEpsilon)
        {
            return;
        }
        const auto _Delta = CounterClockwise_
            ? EndAngle_ - StartAngle_
            : StartAngle_ - EndAngle_;
        if (std::abs(_Delta) <= kEpsilon)
        {
            return;
        }
        const auto _StationaryAngle = std::atan2(
            CoefficientSin_,
            CoefficientCos_);
        for (const auto _Angle : {
            _StationaryAngle,
            _StationaryAngle + kPi })
        {
            for (int _Turn = -4; _Turn <= 4; ++_Turn)
            {
                const auto _Candidate = _Angle
                    + 2.0 * kPi * static_cast<double>(_Turn);
                const auto _U = (_Candidate - StartAngle_) / _Delta;
                if (_U > 1.0e-8 && _U < 1.0 - 1.0e-8)
                {
                    AppendUniqueInteriorParameter(_U, Parameters_);
                }
            }
        }
    }

    std::vector<double> StationaryParameters(
        IN const SSectionWireEdge& Edge_)
    {
        std::vector<double> _Parameters;
        std::visit([&](IN const auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>)
            {
                AppendArcStationaryParameters(
                    Value_.Radius * Value_.Placement.XDirection.Y,
                    Value_.Radius * Value_.Placement.YDirection.Y,
                    0.0,
                    2.0 * kPi,
                    true,
                    _Parameters);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
            {
                AppendArcStationaryParameters(
                    Value_.Basis.Radius * Value_.Basis.Placement.XDirection.Y,
                    Value_.Basis.Radius * Value_.Basis.Placement.YDirection.Y,
                    Value_.StartAngle,
                    Value_.EndAngle,
                    Value_.CounterClockwise,
                    _Parameters);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
            {
                AppendArcStationaryParameters(
                    Value_.MajorRadius * Value_.Placement.XDirection.Y,
                    Value_.MinorRadius * Value_.Placement.YDirection.Y,
                    0.0,
                    2.0 * kPi,
                    true,
                    _Parameters);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc2>)
            {
                AppendArcStationaryParameters(
                    Value_.Basis.MajorRadius * Value_.Basis.Placement.XDirection.Y,
                    Value_.Basis.MinorRadius * Value_.Basis.Placement.YDirection.Y,
                    Value_.StartAngle,
                    Value_.EndAngle,
                    Value_.CounterClockwise,
                    _Parameters);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Polyline2>)
            {
                if (Value_.Points.size() > 2)
                {
                    const auto _Denominator = static_cast<double>(Value_.Points.size() - 1);
                    for (std::size_t _Index = 1;
                        _Index + 1 < Value_.Points.size();
                        ++_Index)
                    {
                        AppendUniqueInteriorParameter(
                            static_cast<double>(_Index) / _Denominator,
                            _Parameters);
                    }
                }
            }
        }, Edge_.Curve);
        return _Parameters;
    }

    std::optional<Point2> EvaluateProjectedCurvePoint(
        IN const SSectionWireEdge& Edge_,
        IN double Parameter_)
    {
        const auto curve = iCAX::OpenCascade::BuildOpenCascadeCurve2(Edge_.Curve);
        if (curve.IsNull()) return std::nullopt;
        const auto u = std::clamp(Parameter_, 0.0, 1.0);
        const auto p = curve->Value(Edge_.Range.First + (Edge_.Range.Last - Edge_.Range.First) * u);
        return Point2{p.X(), p.Y()};
    }

    Point2 NormalizePoint2(IN const Point2& Value_)
    {
        const auto _Length = std::hypot(Value_.X, Value_.Y);
        return _Length <= kEpsilon
            ? Point2{ 1.0, 0.0 }
            : Point2{ Value_.X / _Length, Value_.Y / _Length };
    }

    Point2 CurveTangentAt(
        IN const SSectionWireEdge& Edge_,
        IN double Parameter_)
    {
        const auto curve = iCAX::OpenCascade::BuildOpenCascadeCurve2(Edge_.Curve);
        if (curve.IsNull()) return NormalizePoint2({Edge_.End.X - Edge_.Start.X, Edge_.End.Y - Edge_.Start.Y});
        const auto delta = Edge_.Range.Last - Edge_.Range.First;
        gp_Pnt2d p; gp_Vec2d tangent;
        curve->D1(Edge_.Range.First + delta * std::clamp(Parameter_, 0.0, 1.0), p, tangent);
        return NormalizePoint2({tangent.X() * delta, tangent.Y() * delta});
    }

    struct SLowestCurvePoint final
    {
        double U = 0.0;
        Point2 Point;
        Point2 Tangent;
    };

    SLowestCurvePoint FindLowestCurvePoint(IN const SSectionWireEdge& Edge_)
    {
        // Curve parameters retain the source direction when a wire is reversed.
        if (Edge_.bReversed)
        {
            auto _Forward = Edge_;
            std::swap(_Forward.Start, _Forward.End);
            std::reverse(_Forward.Samples.begin(), _Forward.Samples.end());
            _Forward.bReversed = false;
            auto _Lowest = FindLowestCurvePoint(_Forward);
            _Lowest.U = 1.0 - _Lowest.U;
            _Lowest.Tangent = { -_Lowest.Tangent.X, -_Lowest.Tangent.Y };
            return _Lowest;
        }
        std::vector<double> _Candidates{ 0.0, 1.0 };
        const auto _Stationary = StationaryParameters(Edge_);
        _Candidates.insert(_Candidates.end(), _Stationary.begin(), _Stationary.end());
        if (_Stationary.empty() && Edge_.Samples.size() > 2)
        {
            const auto _Minimum = std::min_element(
                Edge_.Samples.begin(),
                Edge_.Samples.end(),
                [](IN const auto& Left_, IN const auto& Right_) {
                    return std::tie(Left_.Y, Left_.X) < std::tie(Right_.Y, Right_.X);
                });
            _Candidates.push_back(
                static_cast<double>(std::distance(Edge_.Samples.begin(), _Minimum))
                / static_cast<double>(Edge_.Samples.size() - 1));
        }

        SLowestCurvePoint _Result{
            0.0,
            Edge_.Start,
            CurveTangentAt(Edge_, 0.0) };
        for (const auto _U : _Candidates)
        {
            const auto _Point = _U <= 1.0e-8
                ? Edge_.Start
                : (_U >= 1.0 - 1.0e-8
                    ? Edge_.End
                    : EvaluateProjectedCurvePoint(Edge_, _U).value_or(
                        PointOnSamples(Edge_.Samples, _U)));
            if (_Point.Y < _Result.Point.Y - 1.0e-9
                || (std::abs(_Point.Y - _Result.Point.Y) <= 1.0e-9
                    && _Point.X < _Result.Point.X))
            {
                _Result = { _U, _Point, CurveTangentAt(Edge_, _U) };
            }
        }
        return _Result;
    }

    using SContourStartKey = std::array<double, 9>;

    SContourStartKey ContourStartKey(
        IN const SSectionWireEdge& Edge_,
        IN bool bReverse_,
        IN double Tolerance_)
    {
        const auto _Lowest = FindLowestCurvePoint(Edge_);
        const auto& _Start = bReverse_ ? Edge_.End : Edge_.Start;
        const auto& _End = bReverse_ ? Edge_.Start : Edge_.End;
        const Point2 _Tangent = bReverse_
            ? Point2{ -_Lowest.Tangent.X, -_Lowest.Tangent.Y }
            : _Lowest.Tangent;
        const auto _Interior = _Lowest.U > 1.0e-8 && _Lowest.U < 1.0 - 1.0e-8;
        // Grid keys give a strict ordering even for numerically equal extrema.
        // In (Y,Z), prefer lowest Z, then leftmost Y, then an outgoing edge
        // starting there. An interior minimum uses its tangent without splitting
        // the curve. Geometry breaks ties before the caller's storage index.
        const auto _Coordinate = [Tolerance_](double Value_) {
            return std::round(Value_ / Tolerance_);
        };
        return {
            _Coordinate(_Lowest.Point.Y), _Coordinate(_Lowest.Point.X),
            Distance(_Start, _Lowest.Point) <= Tolerance_ ? 0.0 : 1.0,
            _Interior && _Tangent.X >= -kEpsilon ? 1.0 : 0.0,
            std::round(std::abs(std::atan2(_Tangent.Y, _Tangent.X)) / 1.0e-12),
            _Coordinate(_Start.Y), _Coordinate(_Start.X),
            _Coordinate(_End.Y), _Coordinate(_End.X) };
    }

    SProjectionResult ProjectPointToSamples(
        IN const SSectionWireEdge& Edge_,
        IN const Point2& Point_)
    {
        SProjectionResult _Best;
        _Best.U = 0.0;
        _Best.Point = Edge_.Samples.empty() ? Edge_.Start : Edge_.Samples.front();
        _Best.dDistance = Distance(Point_, _Best.Point);
        if (Edge_.Samples.size() < 2)
        {
            return _Best;
        }
        for (std::size_t _Index = 0; _Index + 1 < Edge_.Samples.size(); ++_Index)
        {
            const auto& _A = Edge_.Samples[_Index];
            const auto& _B = Edge_.Samples[_Index + 1];
            const Point2 _Delta{ _B.X - _A.X, _B.Y - _A.Y };
            const auto _LengthSquared = Dot2(_Delta, _Delta);
            const Point2 _ToPoint{ Point_.X - _A.X, Point_.Y - _A.Y };
            const auto _LocalU = _LengthSquared <= kEpsilon
                ? 0.0
                : std::clamp(Dot2(_ToPoint, _Delta) / _LengthSquared, 0.0, 1.0);
            const auto _Projection = Lerp(_A, _B, _LocalU);
            const auto _Distance = Distance(Point_, _Projection);
            if (_Distance < _Best.dDistance)
            {
                _Best.U = (static_cast<double>(_Index) + _LocalU)
                    / static_cast<double>(Edge_.Samples.size() - 1);
                _Best.Point = _Projection;
                _Best.dDistance = _Distance;
            }
        }
        return _Best;
    }

    std::optional<SProjectionResult> ProjectPointToEdge(
        IN const SSectionWireEdge& Edge_,
        IN const Point2& Point_,
        IN double Tolerance_)
    {
        const auto curve = iCAX::OpenCascade::BuildOpenCascadeCurve2(Edge_.Curve);
        if (curve.IsNull()) return std::nullopt;
        const auto delta = Edge_.Range.Last - Edge_.Range.First;
        if (std::abs(delta) <= kEpsilon) return std::nullopt;
        Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(Point_.X, Point_.Y), curve,
            std::min(Edge_.Range.First, Edge_.Range.Last), std::max(Edge_.Range.First, Edge_.Range.Last));
        if (projection.NbPoints() == 0 || projection.LowerDistance() > Tolerance_) return std::nullopt;
        SProjectionResult result;
        result.U = (projection.LowerDistanceParameter() - Edge_.Range.First) / delta;
        const auto p = projection.NearestPoint(); result.Point = {p.X(), p.Y()};
        result.dDistance = projection.LowerDistance();
        return result;
    }

    struct SProjectedEdgeBounds final
    {
        double MinimumX = 0.0;
        double MaximumX = 0.0;
        double MinimumY = 0.0;
        double MaximumY = 0.0;
    };

    SProjectedEdgeBounds GetBounds(IN const SSectionWireEdge& Edge_)
    {
        const auto exact=iCAX::OpenCascade::BuildOpenCascadeCurve2(Edge_.Curve);
        if(!exact.IsNull()) {
            Bnd_Box2d box;
            const auto first=std::min(Edge_.Range.First,Edge_.Range.Last),last=std::max(Edge_.Range.First,Edge_.Range.Last);
            if(first==last)box.Add(exact->Value(first));
            else if(const auto spline=Handle(Geom2d_BSplineCurve)::DownCast(exact);!spline.IsNull()) {
                // Restrict the control hull to this actual split interval.
                // The parent's hull can retain a point-sized self-loop as a
                // large directed edge and corrupt the planar rotation order.
                const auto trimmed=Handle(Geom2d_BSplineCurve)::DownCast(spline->Copy());
                trimmed->Segment(first,last,0.0);
                for(int i=1;i<=trimmed->NbPoles();++i)box.Add(trimmed->Pole(i));
            } else if(const auto bezier=Handle(Geom2d_BezierCurve)::DownCast(exact);!bezier.IsNull()) {
                const auto trimmed=Handle(Geom2d_BezierCurve)::DownCast(bezier->Copy());trimmed->Segment(first,last);
                for(int i=1;i<=trimmed->NbPoles();++i)box.Add(trimmed->Pole(i));
            } else BndLib_Add2dCurve::AddOptimal(exact,first,last,0.0,box);
            if(box.IsVoid())throw std::runtime_error("Cannot bound projected source edge "+std::to_string(Edge_.SourceEdgeId));
            SProjectedEdgeBounds bounds;
            box.Get(bounds.MinimumX,bounds.MinimumY,bounds.MaximumX,bounds.MaximumY);
            return bounds;
        }
        const auto& _Samples = Edge_.Samples;
        const auto _First = _Samples.empty() ? Edge_.Start : _Samples.front();
        SProjectedEdgeBounds _Result{
            _First.X, _First.X, _First.Y, _First.Y };
        for (const auto& _Point : _Samples)
        {
            _Result.MinimumX = std::min(_Result.MinimumX, _Point.X);
            _Result.MaximumX = std::max(_Result.MaximumX, _Point.X);
            _Result.MinimumY = std::min(_Result.MinimumY, _Point.Y);
            _Result.MaximumY = std::max(_Result.MaximumY, _Point.Y);
        }
        _Result.MinimumX = std::min(_Result.MinimumX, Edge_.Start.X);
        _Result.MaximumX = std::max(_Result.MaximumX, Edge_.Start.X);
        _Result.MinimumX = std::min(_Result.MinimumX, Edge_.End.X);
        _Result.MaximumX = std::max(_Result.MaximumX, Edge_.End.X);
        _Result.MinimumY = std::min(_Result.MinimumY, Edge_.Start.Y);
        _Result.MaximumY = std::max(_Result.MaximumY, Edge_.Start.Y);
        _Result.MinimumY = std::min(_Result.MinimumY, Edge_.End.Y);
        _Result.MaximumY = std::max(_Result.MaximumY, Edge_.End.Y);
        // The AABB's lower Z bound must include an interior stationary point
        // of a curved edge; otherwise a coarse sample may select a different
        // edge as the bottom edge.
        const auto _Lowest = FindLowestCurvePoint(Edge_);
        _Result.MinimumY = std::min(_Result.MinimumY, _Lowest.Point.Y);
        return _Result;
    }

    struct SProjectedEdgeIndex final
    {
        double OriginX = 0.0;
        double CellSize = 1.0;
        std::unordered_map<std::int64_t, std::vector<std::size_t>> Buckets;
        std::vector<SProjectedEdgeBounds> Bounds;

        std::int64_t Cell(IN double X_) const
        {
            return static_cast<std::int64_t>(std::floor((X_ - OriginX) / CellSize));
        }

        void Add(IN std::size_t EdgeIndex_)
        {
            const auto& _Bounds = Bounds[EdgeIndex_];
            for (auto _Cell = Cell(_Bounds.MinimumX); _Cell <= Cell(_Bounds.MaximumX); ++_Cell)
            {
                Buckets[_Cell].push_back(EdgeIndex_);
            }
        }

        std::vector<std::size_t> Query(IN const Point2& Point_, IN double Tolerance_) const
        {
            std::vector<std::size_t> _Result;
            const auto _Bucket = Buckets.find(Cell(Point_.X));
            if (_Bucket == Buckets.end())
            {
                return _Result;
            }
            for (const auto _EdgeIndex : _Bucket->second)
            {
                const auto& _Bounds = Bounds[_EdgeIndex];
                if (Point_.X >= _Bounds.MinimumX - Tolerance_
                    && Point_.X <= _Bounds.MaximumX + Tolerance_
                    && Point_.Y >= _Bounds.MinimumY - Tolerance_
                    && Point_.Y <= _Bounds.MaximumY + Tolerance_)
                {
                    _Result.push_back(_EdgeIndex);
                }
            }
            return _Result;
        }
    };

    SProjectedEdgeIndex BuildProjectedEdgeIndex(
        IN const std::vector<SSectionWireEdge>& Edges_,
        IN double Tolerance_)
    {
        SProjectedEdgeIndex _Result;
        _Result.Bounds.reserve(Edges_.size());
        double _MinimumX = (std::numeric_limits<double>::max)();
        double _MaximumX = -(std::numeric_limits<double>::max)();
        for (const auto& _Edge : Edges_)
        {
            _Result.Bounds.push_back(GetBounds(_Edge));
            _MinimumX = std::min(_MinimumX, _Result.Bounds.back().MinimumX);
            _MaximumX = std::max(_MaximumX, _Result.Bounds.back().MaximumX);
        }
        _Result.OriginX = _MinimumX;
        const auto _Span = std::max(_MaximumX - _MinimumX, Tolerance_);
        _Result.CellSize = std::max(Tolerance_ * 8.0, _Span / 64.0);
        _Result.CellSize = std::max(_Result.CellSize, 1.0e-12);
        for (std::size_t _Index = 0; _Index < Edges_.size(); ++_Index)
        {
            _Result.Add(_Index);
        }
        return _Result;
    }

    std::vector<Point2> SubSamples(
        IN const SSectionWireEdge& Edge_,
        IN double StartU_,
        IN double EndU_,
        IN std::size_t SampleCount_)
    {
        const auto _Count = std::max<std::size_t>(2, SampleCount_);
        std::vector<Point2> _Result;
        _Result.reserve(_Count + 1);
        for (std::size_t _Index = 0; _Index <= _Count; ++_Index)
        {
            const auto _U = StartU_ + (EndU_ - StartU_)
                * static_cast<double>(_Index) / static_cast<double>(_Count);
            _Result.push_back(EvaluateProjectedCurvePoint(Edge_, _U).value());
        }
        _Result.front() = EvaluateProjectedCurvePoint(Edge_, StartU_).value();
        _Result.back() = EvaluateProjectedCurvePoint(Edge_, EndU_).value();
        return _Result;
    }

    std::vector<SSectionWireEdge> SplitProjectedEdge(
        IN const SSectionWireEdge& Edge_,
        IN std::vector<double> Cuts_,
        IN const SSectionWireOptions& Options_)
    {
        if (Cuts_.empty()) return { Edge_ };
        Cuts_.push_back(0.0);
        Cuts_.push_back(1.0);
        std::sort(Cuts_.begin(), Cuts_.end());
        Cuts_.erase(
            std::unique(Cuts_.begin(), Cuts_.end(), [](IN double Left_, IN double Right_) {
                return std::abs(Left_ - Right_) <= 1.0e-10;
            }),
            Cuts_.end());

        std::vector<SSectionWireEdge> _Result;
        _Result.reserve(Cuts_.size() - 1);
        const auto _RangeDelta = Edge_.Range.Last - Edge_.Range.First;
        for (std::size_t _Index = 0; _Index + 1 < Cuts_.size(); ++_Index)
        {
            const auto _StartU = Cuts_[_Index];
            const auto _EndU = Cuts_[_Index + 1];
            if (_EndU - _StartU <= 1.0e-10)
            {
                continue;
            }
            auto _Piece = Edge_;
            _Piece.Range.First = Edge_.Range.First + _RangeDelta * _StartU;
            _Piece.Range.Last = Edge_.Range.First + _RangeDelta * _EndU;
            _Piece.Start = EvaluateProjectedCurvePoint(Edge_, _StartU).value();
            _Piece.End = EvaluateProjectedCurvePoint(Edge_, _EndU).value();
            if (_StartU <= 1.0e-10)
            {
                _Piece.Start = Edge_.Start;
            }
            if (_EndU >= 1.0 - 1.0e-10)
            {
                _Piece.End = Edge_.End;
            }
            _Piece.Samples = SubSamples(
                Edge_,
                _StartU,
                _EndU,
                std::max<std::size_t>(8, Options_.nCurveSampleCount / 2));
            if (const auto _Conic = ConicInterval(Edge_.Curve))
            {
                const auto _First = _Conic->First + _Conic->Sweep * _StartU;
                const auto _Last = _Conic->First + _Conic->Sweep * _EndU;
                _Piece.Range = { _First, _Last };
                _Piece.Curve = _Conic->bCircle
                    ? Curve2(iCAX::GeometryData::Arc2{
                        { _Conic->Basis.Placement, _Conic->Basis.MajorRadius }, _First, _Last, true })
                    : Curve2(iCAX::GeometryData::EllipseArc2{ _Conic->Basis, _First, _Last, true });
            }
            else if (std::holds_alternative<iCAX::GeometryData::Segment2>(Edge_.Curve))
                _Piece.Curve = iCAX::GeometryData::Segment2{ _Piece.Start, _Piece.End };
            _Result.push_back(std::move(_Piece));
        }
        return _Result;
    }

    struct SGraphNode final
    {
        Point2 Position;
        std::vector<std::size_t> IncidentEdges;
        std::vector<std::size_t> IncidentHalfEdges;
    };

    struct SGraphEdge final
    {
        SSectionWireEdge Projected;
        std::size_t StartNode = 0;
        std::size_t EndNode = 0;
        bool bUsed = false;
        bool bActive = true;
    };

    double DirectedCurvature(const SGraphEdge& edge,bool forward)
    {
        const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(edge.Projected.Curve);
        if(curve.IsNull())return 0;
        const auto& range=edge.Projected.Range;
        const double parameter=forward?range.First:range.Last;
        Geom2dAdaptor_Curve trimmed(curve,std::min(range.First,range.Last),std::max(range.First,range.Last));
        gp_Pnt2d point;gp_Vec2d first,second;trimmed.D2(parameter,point,first,second);
        const double speed=first.Magnitude();if(speed<=kEpsilon)return 0;
        const double orientation=(range.Last>=range.First?1.0:-1.0)*(forward?1.0:-1.0);
        const double curvature=orientation*first.Crossed(second)/(speed*speed*speed);
        return std::isfinite(curvature)?curvature:0;
    }

    struct SHalfEdge final
    {
        std::size_t GraphEdge = 0;
        std::size_t FromNode = 0;
        std::size_t ToNode = 0;
        std::size_t Twin = 0;
        bool bForward = true;
        bool bVisited = false;
    };

    struct SGridKey final
    {
        std::int64_t Y = 0;
        std::int64_t Z = 0;

        bool operator==(IN const SGridKey& Other_) const noexcept
        {
            return Y == Other_.Y && Z == Other_.Z;
        }
    };

    struct SGridKeyHash final
    {
        std::size_t operator()(IN const SGridKey& Key_) const noexcept
        {
            const auto _Y = std::hash<std::int64_t>{}(Key_.Y);
            const auto _Z = std::hash<std::int64_t>{}(Key_.Z);
            return _Y ^ (_Z + 0x9e3779b97f4a7c15ULL + (_Y << 6) + (_Y >> 2));
        }
    };

    std::size_t FindOrAddNode(
        IN const Point2& Point_,
        IN double Tolerance_,
        IN OUT std::vector<SGraphNode>& Nodes_,
        IN OUT std::unordered_map<SGridKey, std::vector<std::size_t>, SGridKeyHash>& Grid_)
    {
        const auto _CellSize = std::max(Tolerance_, 1.0e-12);
        const SGridKey _Key{
            static_cast<std::int64_t>(std::llround(Point_.X / _CellSize)),
            static_cast<std::int64_t>(std::llround(Point_.Y / _CellSize)) };
        for (std::int64_t _Y = _Key.Y - 1; _Y <= _Key.Y + 1; ++_Y)
        {
            for (std::int64_t _Z = _Key.Z - 1; _Z <= _Key.Z + 1; ++_Z)
            {
                const auto _Bucket = Grid_.find({ _Y, _Z });
                if (_Bucket == Grid_.end())
                {
                    continue;
                }
                for (const auto _NodeIndex : _Bucket->second)
                {
                    if (SamePoint(Nodes_[_NodeIndex].Position, Point_, Tolerance_))
                    {
                        return _NodeIndex;
                    }
                }
            }
        }
        const auto _NodeIndex = Nodes_.size();
        Nodes_.push_back({ Point_, {} });
        Grid_[_Key].push_back(_NodeIndex);
        return _NodeIndex;
    }

    std::size_t OtherNode(IN const SGraphEdge& Edge_, IN std::size_t Node_)
    {
        return Edge_.StartNode == Node_ ? Edge_.EndNode : Edge_.StartNode;
    }

    Point2 OutgoingDirection(
        IN const SGraphEdge& Edge_,
        IN std::size_t Node_,
        IN const std::vector<SGraphNode>& Nodes_)
    {
        auto _Samples = Edge_.Projected.Samples;
        if (Node_ != Edge_.StartNode)
        {
            std::reverse(_Samples.begin(), _Samples.end());
        }
        for (std::size_t _Index = 1; _Index < _Samples.size(); ++_Index)
        {
            const auto _Delta = Point2{
                _Samples[_Index].X - _Samples[0].X,
                _Samples[_Index].Y - _Samples[0].Y };
            const auto _Length = std::hypot(_Delta.X, _Delta.Y);
            if (_Length > kEpsilon)
            {
                return { _Delta.X / _Length, _Delta.Y / _Length };
            }
        }
        const auto _Other = Nodes_[OtherNode(Edge_, Node_)].Position;
        const auto _Delta = Point2{
            _Other.X - Nodes_[Node_].Position.X,
            _Other.Y - Nodes_[Node_].Position.Y };
        const auto _Length = std::hypot(_Delta.X, _Delta.Y);
        return _Length <= kEpsilon
            ? Point2{ 1.0, 0.0 }
            : Point2{ _Delta.X / _Length, _Delta.Y / _Length };
    }

    Point2 HalfEdgeOutgoingDirection(
        IN const SHalfEdge& HalfEdge_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN const std::vector<SGraphNode>& Nodes_)
    {
        (void)Nodes_;
        const auto tangent=CurveTangentAt(GraphEdges_[HalfEdge_.GraphEdge].Projected,HalfEdge_.bForward?0.0:1.0);
        return HalfEdge_.bForward?tangent:Point2{-tangent.X,-tangent.Y};
    }

    std::size_t NextLeftFaceHalfEdge(
        IN std::size_t HalfEdgeIndex_,
        IN const std::vector<SHalfEdge>& HalfEdges_,
        IN const std::vector<SGraphNode>& Nodes_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN bool bLeftFace_)
    {
        const auto& _HalfEdge = HalfEdges_[HalfEdgeIndex_];
        const auto& _Outgoing = Nodes_[_HalfEdge.ToNode].IncidentHalfEdges;
        if (_Outgoing.empty())
        {
            return (std::numeric_limits<std::size_t>::max)();
        }
        const auto _TwinPosition = std::find(
            _Outgoing.begin(),
            _Outgoing.end(),
            _HalfEdge.Twin);
        if (_TwinPosition == _Outgoing.end())
        {
            return (std::numeric_limits<std::size_t>::max)();
        }

        // Outgoing half-edges are sorted counter-clockwise. The predecessor
        // of the incoming twin walks the face on the left; the successor
        // walks the face on the right. This is the planar-embedding
        // operation that replaces a greedy numeric turn comparison.
        const auto _Position = static_cast<std::size_t>(
            std::distance(_Outgoing.begin(), _TwinPosition));
        const auto _Previous = _Position == 0
            ? _Outgoing.size() - 1
            : _Position - 1;
        const auto _Next = (_Position + 1) % _Outgoing.size();
        (void)GraphEdges_;
        return _Outgoing[bLeftFace_ ? _Previous : _Next];
    }

    struct SLoopCandidate final
    {
        std::vector<SSectionWireEdge> Edges;
        std::vector<std::size_t> GraphEdgeIndices;
        std::vector<Point2> Points;
        double dSignedArea = 0.0;
        mutable TopoDS_Face ExactFace;
    };

    void AppendSamples(
        IN const SSectionWireEdge& Edge_,
        IN OUT std::vector<Point2>& Points_);

    double SignedArea(IN const std::vector<Point2>& Points_);

    std::vector<SLoopCandidate> TraceFace(
        std::size_t startHalfEdge, std::vector<SHalfEdge>& halfEdges,
        const SSectionWireOptions& options, const std::vector<SGraphNode>& nodes,
        const std::vector<SGraphEdge>& graphEdges, bool leftFace)
    {
        std::vector<SLoopCandidate> loops;
        std::vector<std::size_t> path;
        std::unordered_map<std::size_t, std::size_t> nodePosition;
        nodePosition.emplace(halfEdges[startHalfEdge].FromNode, 0);
        auto current = startHalfEdge;
        while (current < halfEdges.size() && !halfEdges[current].bVisited)
        {
            auto& half = halfEdges[current];
            half.bVisited = true;
            path.push_back(current);
            const auto existing = nodePosition.find(half.ToNode);
            if (existing != nodePosition.end())
            {
                // Any repeated node closes a loop. Retain only the closed
                // suffix; a lead-in or a previous excursion is not its boundary.
                const auto first = existing->second;
                SLoopCandidate loop;
                std::unordered_set<std::size_t> usedEdges;
                bool simple = true;
                for (std::size_t i = first; i < path.size(); ++i)
                {
                    const auto& step = halfEdges[path[i]];
                    simple = usedEdges.insert(step.GraphEdge).second && simple;
                    auto edge = graphEdges[step.GraphEdge].Projected;
                    if (!step.bForward)
                    {
                        std::swap(edge.Start, edge.End);
                        std::reverse(edge.Samples.begin(), edge.Samples.end());
                    }
                    edge.bReversed = !step.bForward;
                    loop.Edges.push_back(std::move(edge));
                    loop.GraphEdgeIndices.push_back(step.GraphEdge);
                    AppendSamples(loop.Edges.back(), loop.Points);
                }
                if (loop.Points.size() > 1 && SamePoint(loop.Points.front(), loop.Points.back(), options.dConnectionTolerance))
                    loop.Points.pop_back();
                loop.dSignedArea = SignedArea(loop.Points);
                if (simple && std::abs(loop.dSignedArea) > options.dConnectionTolerance * options.dConnectionTolerance)
                    loops.push_back(std::move(loop));
                for (std::size_t i = first + 1; i < path.size(); ++i)
                    nodePosition.erase(halfEdges[path[i]].FromNode);
                path.resize(first);
            }
            else nodePosition.emplace(half.ToNode, path.size());
            current = NextLeftFaceHalfEdge(current, halfEdges, nodes, graphEdges, leftFace);
        }
        return loops;
    }

    std::size_t Degree(
        IN std::size_t NodeIndex_,
        IN const std::vector<SGraphNode>& Nodes_,
        IN const std::vector<SGraphEdge>& GraphEdges_)
    {
        std::size_t _Degree = 0;
        for (const auto _GraphEdgeIndex : Nodes_[NodeIndex_].IncidentEdges)
        {
            const auto& _GraphEdge = GraphEdges_[_GraphEdgeIndex];
            if (!_GraphEdge.bActive)
            {
                continue;
            }
            _Degree += _GraphEdge.StartNode == _GraphEdge.EndNode ? 2 : 1;
        }
        return _Degree;
    }

    std::size_t FindActiveIncidentEdge(
        IN std::size_t NodeIndex_,
        IN const std::vector<SGraphNode>& Nodes_,
        IN const std::vector<SGraphEdge>& GraphEdges_)
    {
        for (const auto _GraphEdgeIndex : Nodes_[NodeIndex_].IncidentEdges)
        {
            if (GraphEdges_[_GraphEdgeIndex].bActive)
            {
                return _GraphEdgeIndex;
            }
        }
        return (std::numeric_limits<std::size_t>::max)();
    }

    std::size_t PruneDegreeOneEdges(
        IN OUT std::vector<SGraphNode>& Nodes_,
        IN OUT std::vector<SGraphEdge>& GraphEdges_)
    {
        std::deque<std::size_t> _Queue;
        std::vector<std::size_t> _Degrees(Nodes_.size(), 0);
        for (std::size_t _NodeIndex = 0; _NodeIndex < Nodes_.size(); ++_NodeIndex)
        {
            _Degrees[_NodeIndex] = Degree(_NodeIndex, Nodes_, GraphEdges_);
            if (_Degrees[_NodeIndex] == 1)
            {
                _Queue.push_back(_NodeIndex);
            }
        }

        std::size_t _RemovedEdges = 0;
        while (!_Queue.empty())
        {
            const auto _NodeIndex = _Queue.front();
            _Queue.pop_front();
            if (_Degrees[_NodeIndex] != 1)
            {
                continue;
            }
            const auto _GraphEdgeIndex = FindActiveIncidentEdge(
                _NodeIndex,
                Nodes_,
                GraphEdges_);
            if (_GraphEdgeIndex == (std::numeric_limits<std::size_t>::max)())
            {
                _Degrees[_NodeIndex] = 0;
                continue;
            }
            auto& _GraphEdge = GraphEdges_[_GraphEdgeIndex];
            _GraphEdge.bActive = false;
            ++_RemovedEdges;
            const auto _OtherNode = _GraphEdge.StartNode == _NodeIndex
                ? _GraphEdge.EndNode
                : _GraphEdge.StartNode;
            if (_GraphEdge.StartNode == _GraphEdge.EndNode)
            {
                _Degrees[_NodeIndex] = 0;
            }
            else
            {
                _Degrees[_NodeIndex] = 0;
                if (_Degrees[_OtherNode] > 0)
                {
                    --_Degrees[_OtherNode];
                }
                if (_Degrees[_OtherNode] == 1)
                {
                    _Queue.push_back(_OtherNode);
                }
            }
        }
        return _RemovedEdges;
    }

    // A bridge can join two otherwise closed boundaries without either end
    // having degree one. It belongs to no contour and would be walked twice
    // by the unbounded face, splicing an inner loop into the outer loop.
    std::size_t PruneBridgeEdges(const std::vector<SGraphNode>& nodes,
        std::vector<SGraphEdge>& edges)
    {
        const auto none=(std::numeric_limits<std::size_t>::max)();
        std::vector<std::size_t> order(nodes.size(),none),low(nodes.size()),parentEdge(nodes.size(),none),next(nodes.size());
        std::vector<std::size_t> stack,bridges;std::size_t clock=0;
        for(std::size_t root=0;root<nodes.size();++root){
            if(order[root]!=none)continue;
            order[root]=low[root]=clock++;stack.push_back(root);
            while(!stack.empty()){
                const auto node=stack.back();
                if(next[node]<nodes[node].IncidentEdges.size()){
                    const auto edgeId=nodes[node].IncidentEdges[next[node]++];
                    if(!edges[edgeId].bActive||edgeId==parentEdge[node])continue;
                    const auto other=OtherNode(edges[edgeId],node);
                    if(order[other]==none){parentEdge[other]=edgeId;order[other]=low[other]=clock++;stack.push_back(other);}
                    else low[node]=std::min(low[node],order[other]);
                }else{
                    stack.pop_back();
                    if(parentEdge[node]!=none){
                        const auto parent=OtherNode(edges[parentEdge[node]],node);
                        if(low[node]>order[parent])bridges.push_back(parentEdge[node]);
                        low[parent]=std::min(low[parent],low[node]);
                    }
                }
            }
        }
        for(const auto edge:bridges)edges[edge].bActive=false;
        return bridges.size();
    }

    void BuildHalfEdges(
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN OUT std::vector<SGraphNode>& Nodes_,
        OUT std::vector<SHalfEdge>& HalfEdges_)
    {
        HalfEdges_.clear();
        HalfEdges_.reserve(GraphEdges_.size() * 2);
        for (auto& _Node : Nodes_)
        {
            _Node.IncidentHalfEdges.clear();
        }
        for (std::size_t _GraphIndex = 0; _GraphIndex < GraphEdges_.size(); ++_GraphIndex)
        {
            const auto& _GraphEdge = GraphEdges_[_GraphIndex];
            if (!_GraphEdge.bActive)
            {
                continue;
            }
            const auto _Forward = HalfEdges_.size();
            const auto _Reverse = _Forward + 1;
            HalfEdges_.push_back({
                _GraphIndex,
                _GraphEdge.StartNode,
                _GraphEdge.EndNode,
                _Reverse,
                true,
                false });
            HalfEdges_.push_back({
                _GraphIndex,
                _GraphEdge.EndNode,
                _GraphEdge.StartNode,
                _Forward,
                false,
                false });
            Nodes_[_GraphEdge.StartNode].IncidentHalfEdges.push_back(_Forward);
            Nodes_[_GraphEdge.EndNode].IncidentHalfEdges.push_back(_Reverse);
        }

        for (auto& _Node : Nodes_)
        {
            std::sort(
                _Node.IncidentHalfEdges.begin(),
                _Node.IncidentHalfEdges.end(),
                [&](IN std::size_t Left_, IN std::size_t Right_) {
                    const auto _LeftDirection = HalfEdgeOutgoingDirection(
                        HalfEdges_[Left_],
                        GraphEdges_,
                        Nodes_);
                    const auto _RightDirection = HalfEdgeOutgoingDirection(
                        HalfEdges_[Right_],
                        GraphEdges_,
                        Nodes_);
                    auto _LeftAngle = std::atan2(_LeftDirection.Y, _LeftDirection.X);
                    auto _RightAngle = std::atan2(_RightDirection.Y, _RightDirection.X);
                    if (_LeftAngle < 0.0) _LeftAngle += 2.0 * kPi;
                    if (_RightAngle < 0.0) _RightAngle += 2.0 * kPi;
                    // Tangential branches have the same endpoint tangent.
                    // Their cyclic order is determined by signed curvature,
                    // not source edge IDs or the spacing of preview samples.
                    const auto angleKey=[](double angle){
                        constexpr double tolerance=1.0e-8;
                        if(angle< tolerance||2.0*kPi-angle<tolerance)return std::int64_t{0};
                        return static_cast<std::int64_t>(std::llround(angle/tolerance));
                    };
                    const auto leftKey=angleKey(_LeftAngle),rightKey=angleKey(_RightAngle);
                    if(leftKey!=rightKey)return leftKey<rightKey;
                    const auto& leftHalf=HalfEdges_[Left_];const auto& rightHalf=HalfEdges_[Right_];
                    const auto leftCurvature=DirectedCurvature(GraphEdges_[leftHalf.GraphEdge],leftHalf.bForward);
                    const auto rightCurvature=DirectedCurvature(GraphEdges_[rightHalf.GraphEdge],rightHalf.bForward);
                    if(leftCurvature!=rightCurvature)return leftCurvature<rightCurvature;
                    return Left_ < Right_;
                });
        }
    }

    std::vector<SLoopCandidate> CollectFaces(
        IN OUT std::vector<SHalfEdge>& HalfEdges_,
        IN const SSectionWireOptions& Options_,
        IN const std::vector<SGraphNode>& Nodes_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN bool bLeftFace_)
    {
        std::vector<SLoopCandidate> _Loops;
        std::vector<std::vector<std::size_t>> _LoopKeys;
        std::vector<std::pair<SContourStartKey, std::size_t>> _Starts;
        _Starts.reserve(HalfEdges_.size());
        for (std::size_t _Index = 0; _Index < HalfEdges_.size(); ++_Index)
        {
            const auto& _HalfEdge = HalfEdges_[_Index];
            _Starts.emplace_back(ContourStartKey(
                GraphEdges_[_HalfEdge.GraphEdge].Projected,
                !_HalfEdge.bForward,
                Options_.dConnectionTolerance), _Index);
        }
        std::sort(_Starts.begin(), _Starts.end());
        for (const auto& [_StartKey, _HalfEdgeIndex] : _Starts)
        {
            if (HalfEdges_[_HalfEdgeIndex].bVisited)
            {
                continue;
            }
            const auto _Faces = TraceFace(
                _HalfEdgeIndex,
                HalfEdges_,
                Options_,
                Nodes_,
                GraphEdges_,
                bLeftFace_);
            for (const auto& _FaceValue : _Faces)
            {
            const auto* _Face = &_FaceValue;
            auto _Key = _Face->GraphEdgeIndices;
            std::sort(_Key.begin(), _Key.end());
            const auto _Existing = std::find(
                _LoopKeys.begin(),
                _LoopKeys.end(),
                _Key);
            if (_Existing == _LoopKeys.end())
            {
                _LoopKeys.push_back(std::move(_Key));
                _Loops.push_back(std::move(*_Face));
                continue;
            }
            const auto _ExistingIndex = static_cast<std::size_t>(
                std::distance(_LoopKeys.begin(), _Existing));
            if (std::abs(_Face->dSignedArea)
                > std::abs(_Loops[_ExistingIndex].dSignedArea))
            {
                _Loops[_ExistingIndex] = std::move(*_Face);
            }
        }
        }
        return _Loops;
    }

    double DistanceToSegment(
        IN const Point2& Point_,
        IN const Point2& A_,
        IN const Point2& B_)
    {
        const Point2 _Delta{ B_.X - A_.X, B_.Y - A_.Y };
        const auto _LengthSquared = Dot2(_Delta, _Delta);
        const Point2 _ToPoint{ Point_.X - A_.X, Point_.Y - A_.Y };
        const auto _U = _LengthSquared <= kEpsilon
            ? 0.0
            : std::clamp(Dot2(_ToPoint, _Delta) / _LengthSquared, 0.0, 1.0);
        return Distance(Point_, Lerp(A_, B_, _U));
    }

    using section_detail::EPointRelation;
    using section_detail::SExactCandidateClassifier;

    EPointRelation ClassifyPoint(
        IN const Point2& Point_,
        IN const std::vector<Point2>& Polygon_,
        IN double Tolerance_)
    {
        if (Polygon_.size() < 3)
        {
            return EPointRelation::Outside;
        }
        for (std::size_t _Index = 0; _Index < Polygon_.size(); ++_Index)
        {
            if (DistanceToSegment(
                    Point_,
                    Polygon_[_Index],
                    Polygon_[(_Index + 1) % Polygon_.size()]) <= Tolerance_)
            {
                return EPointRelation::Boundary;
            }
        }

        bool _Inside = false;
        for (std::size_t _Index = 0, _Previous = Polygon_.size() - 1;
            _Index < Polygon_.size();
            _Previous = _Index++)
        {
            const auto& _A = Polygon_[_Previous];
            const auto& _B = Polygon_[_Index];
            const auto _Straddles = (_A.Y > Point_.Y) != (_B.Y > Point_.Y);
            if (!_Straddles)
            {
                continue;
            }
            const auto _IntersectionX = _A.X
                + (_B.X - _A.X) * (Point_.Y - _A.Y) / (_B.Y - _A.Y);
            if (Point_.X < _IntersectionX)
            {
                _Inside = !_Inside;
            }
        }
        return _Inside ? EPointRelation::Inside : EPointRelation::Outside;
    }

    std::unordered_set<std::size_t> MakeBoundaryEdgeSet(
        IN const SLoopCandidate& Loop_)
    {
        return {
            Loop_.GraphEdgeIndices.begin(),
            Loop_.GraphEdgeIndices.end() };
    }

    std::vector<Point2> EdgeSupportPoints(IN const SGraphEdge& Edge_)
    {
        if (!Edge_.Projected.Samples.empty())
        {
            return Edge_.Projected.Samples;
        }
        return { Edge_.Projected.Start, Edge_.Projected.End };
    }

    TopoDS_Face CandidateFace(const SLoopCandidate& candidate, double tolerance)
    {
        if(!candidate.ExactFace.IsNull())return candidate.ExactFace;
        BRepBuilderAPI_MakeWire wire;
        const gp_Pln plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));
        for(const auto& edge:candidate.Edges) {
            const auto c=iCAX::OpenCascade::BuildOpenCascadeCurve2(edge.Curve);
            if(c.IsNull())return {};
            BRepBuilderAPI_MakeEdge maker(GeomAPI::To3d(c,plane),edge.Range.First,edge.Range.Last);
            if(!maker.IsDone())return {};
            auto shape=maker.Edge();
            BRep_Builder builder;
            for(TopExp_Explorer v(shape,TopAbs_VERTEX);v.More();v.Next())
                builder.UpdateVertex(TopoDS::Vertex(v.Current()),tolerance);
            if(edge.bReversed)shape.Reverse();wire.Add(shape);
        }
        if(!wire.IsDone())return {};
        // A candidate denotes a finite region. Use the native infinite-point
        // classification to orient it independently of traversal direction.
        auto boundary=wire.Wire();BRepBuilderAPI_MakeFace face(plane,boundary,false);
        if(!face.IsDone())return {};
        if(BRepTopAdaptor_FClass2d(face.Face(),tolerance).PerformInfinitePoint()==TopAbs_IN) {
            boundary.Reverse();BRepBuilderAPI_MakeFace bounded(plane,boundary,false);
            if(!bounded.IsDone())return {};candidate.ExactFace=bounded.Face();
        } else candidate.ExactFace=face.Face();
        return candidate.ExactFace;
    }

    bool CandidateContainsAllActiveEdges(
        IN const SLoopCandidate& Candidate_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN double Tolerance_,
        OUT std::string* Rejection_ = nullptr,
        const std::function<void(const std::string&)>& Progress_ = {},
        std::shared_ptr<SExactCandidateClassifier>* Cache_ = nullptr)
    {
        if(Progress_)Progress_("outer candidate face edges="+std::to_string(Candidate_.Edges.size())
            +" area="+std::to_string(Candidate_.dSignedArea));
        const auto _BoundaryEdges = MakeBoundaryEdgeSet(Candidate_);
        auto cached=Cache_?*Cache_:std::shared_ptr<SExactCandidateClassifier>{};
        if(!cached) {
            const auto face=CandidateFace(Candidate_,Tolerance_);
            if(face.IsNull()) { if(Rejection_)*Rejection_="Cannot build the closed candidate face"; return false; }
            cached=std::make_shared<SExactCandidateClassifier>(face,Tolerance_);
            if(Cache_)*Cache_=cached;
        }
        auto& classifier=*cached;
        Bnd_Box enclosingBounds;
        for(const auto& boundary:classifier.Boundary)enclosingBounds.Add(boundary.Bounds);
        for (std::size_t _Index = 0; _Index < GraphEdges_.size(); ++_Index)
        {
            if (!GraphEdges_[_Index].bActive || _BoundaryEdges.contains(_Index))
            {
                continue;
            }
            std::vector<Point2> _Points;
            if(Progress_)Progress_("outer containment source-edge="+std::to_string(GraphEdges_[_Index].Projected.SourceEdgeId));
            const auto& projected=GraphEdges_[_Index].Projected;
            const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(projected.Curve);
            for(int sample=0;sample<=64;++sample) {
                const auto p=curve->Value(projected.Range.First+(projected.Range.Last-projected.Range.First)*(sample/64.0));
                _Points.push_back({p.X(),p.Y()});
            }
            for (std::size_t _PointIndex = 0; _PointIndex < _Points.size(); ++_PointIndex)
            {
                if(!enclosingBounds.IsVoid()&&enclosingBounds.IsOut(gp_Pnt(_Points[_PointIndex].X,_Points[_PointIndex].Y,0))) {
                    if(Rejection_)*Rejection_="Projected source edge "+std::to_string(GraphEdges_[_Index].Projected.SourceEdgeId)
                        +" is outside the finite candidate bounds";
                    return false;
                }
                // This is the existing finite-boundary OUT fallback, tested
                // first to avoid a redundant whole-wire ray on boundary points.
                if(classifier.OnFiniteBoundary(_Points[_PointIndex]))continue;
                const auto _Relation = classifier.Classify(_Points[_PointIndex]);
                if (_Relation == EPointRelation::Outside)
                {
                    double nearest=std::numeric_limits<double>::infinity();
                    const gp_Pnt p(_Points[_PointIndex].X,_Points[_PointIndex].Y,0);
                    for(const auto& boundary:classifier.Boundary) {
                        nearest=std::min({nearest,p.Distance(boundary.Curve->Value(boundary.First)),p.Distance(boundary.Curve->Value(boundary.Last))});
                        GeomAPI_ProjectPointOnCurve projection(p,boundary.Curve,boundary.First,boundary.Last);
                        if(projection.NbPoints())nearest=std::min(nearest,projection.LowerDistance());
                    }
                    // The ray may miss an ON classification at a junction.
                    // Distance to the finite exact boundary is the same
                    // geometric boundary test used for IN classifications.
                    if(nearest<=Tolerance_)continue;
                    if(Rejection_)*Rejection_="Projected source edge "
                        +std::to_string(GraphEdges_[_Index].Projected.SourceEdgeId)+" lies outside at ("
                        +std::to_string(_Points[_PointIndex].X)+","+std::to_string(_Points[_PointIndex].Y)+") boundary-distance="+std::to_string(nearest);
                    return false;
                }

            }
        }
        return true;
    }

    bool CandidateInteriorHasLine(
        IN const SLoopCandidate& Candidate_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN double Tolerance_,
        OUT std::string& Rejection_)
    {
        const auto _BoundaryEdges = MakeBoundaryEdgeSet(Candidate_);
        const auto face=CandidateFace(Candidate_,Tolerance_);
        if(face.IsNull()){Rejection_="Cannot build candidate face";return true;}
        SExactCandidateClassifier classifier(face,Tolerance_);
        for (std::size_t _Index = 0; _Index < GraphEdges_.size(); ++_Index)
        {
            if (_BoundaryEdges.contains(_Index))
            {
                continue;
            }
            std::vector<Point2> _Points;
            const auto& projected=GraphEdges_[_Index].Projected;
            const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(projected.Curve);
            for(int sample=0;sample<=64;++sample) {
                const auto p=curve->Value(projected.Range.First+(projected.Range.Last-projected.Range.First)*(sample/64.0));
                _Points.push_back({p.X(),p.Y()});
            }
            for (std::size_t _PointIndex = 0; _PointIndex < _Points.size(); ++_PointIndex)
            {
                if (classifier.Classify(_Points[_PointIndex])
                    == EPointRelation::Inside)
                {
                    Rejection_="Interior projected edge " + std::to_string(_Index)
                        + " source=" + std::to_string(GraphEdges_[_Index].Projected.SourceEdgeId)
                        + " at (" + std::to_string(_Points[_PointIndex].X) + ","
                        + std::to_string(_Points[_PointIndex].Y) + ") active="
                        + std::to_string(GraphEdges_[_Index].bActive);
                    double boundaryDistance=(std::numeric_limits<double>::max)();
                    const gp_Pnt point(_Points[_PointIndex].X,_Points[_PointIndex].Y,0);
                    for(const auto& boundary:classifier.Boundary) {
                        boundaryDistance=std::min({boundaryDistance,point.Distance(boundary.Curve->Value(boundary.First)),
                            point.Distance(boundary.Curve->Value(boundary.Last))});
                        GeomAPI_ProjectPointOnCurve projection(point,boundary.Curve,boundary.First,boundary.Last);
                        if(projection.NbPoints())boundaryDistance=std::min(boundaryDistance,projection.LowerDistance());
                    }
                    Rejection_+=" boundary-distance="+std::to_string(boundaryDistance);
                    return true;
                }

            }
        }
        return false;
    }

    struct SMaterialFaceProjection
    {
        std::uint64_t SourceFaceId;
        TopoDS_Face Face;
        double FirstX, LastX;
        bool Spatial = false;
        double PlanarAreaScale = 0;
        TopoDS_Face Footprint;
        Bnd_Box ProjectedBounds;
    };

    // Retain the filled, trimmed regions of source faces, including their
    // holes. Boundary curves alone cannot distinguish material from a void.
    // No sewing or material union is performed here. For curved source faces,
    // intersect their original trimmed region with a candidate's axial prism.
    std::vector<SMaterialFaceProjection> ProjectMaterialFaces(
        const BRepModel& geometry, const SSectionWireOptions& options,
        std::vector<std::string>& diagnostics,
        const iCAX::OpenCascade::SOpenCascadeBRepBuildResult* prepared=nullptr)
    {
        std::vector<SMaterialFaceProjection> result;
        iCAX::OpenCascade::SOpenCascadeBRepBuildResult local;
        if(!prepared){local=iCAX::OpenCascade::ReconstructOpenCascadeShape(geometry);prepared=&local;}
        const auto& built=*prepared;
        if(!built.bOK)throw std::runtime_error("Cannot reconstruct source faces for material classification");
        std::size_t nonPlanar=0;
        const SGeometryIndex index(geometry);
        struct SProjectionWork {std::uint64_t Id;TopoDS_Face Face;double PlanarAreaScale;};
        std::vector<SProjectionWork> privateFaces;
        for(const auto& [id,face]:built.Faces){
            if(options.Progress)options.Progress("material support face="+std::to_string(id));
            // Test read-only supports before making task-private geometry.
            // Axial walls supply no projected material area and never enter
            // the Boolean tests, so copying all their trims is wasted work.
            const auto surface=BRep_Tool::Surface(face);if(surface.IsNull())continue;
            const GeomLib_IsPlanarSurface planar(surface,options.dConnectionTolerance);
            double scale=0;
            if(planar.IsPlanar()){
                scale=std::abs(planar.Plan().Axis().Direction().X());
                if(scale<=std::sin(options.dFaceParallelToleranceRadians))continue;
            }else{
                const auto source=std::find_if(geometry.Faces.begin(),geometry.Faces.end(),[&](const auto& value){return value.Id==id;});
                if(source!=geometry.Faces.end()&&IsSideFace(*source,index,options))continue;
            }
            privateFaces.push_back({id,TopoDS::Face(BRepBuilderAPI_Copy(face,true,false).Shape()),scale});
        }
        std::vector<std::optional<SMaterialFaceProjection>> regions(privateFaces.size());
        const auto workers=iCAX::OpenCascade::detail::ForGeometryIndices(privateFaces.size(),options.nMaximumConcurrency,[&](std::size_t at){
            const auto& [id,face,scale]=privateFaces[at];
            // These boxes only reject disjoint material or enclose its prism;
            // no dimensions or coverage are measured from them. A conservative
            // native bound (without display triangulation) is sufficient. All
            // surviving intersections still use the original precise trims.
            Bnd_Box box;BRepBndLib::Add(face,box,false);
            double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
            SMaterialFaceProjection region{id,face,x,X,true,scale};
            if(scale>0){
                region.Footprint=section_detail::ProjectSingleValuedFace(face,options.dConnectionTolerance);
            }
            // Reuse the spatial box calculated above. Only a derived
            // planar footprint needs another bound calculation.
            if(region.Footprint.IsNull())region.ProjectedBounds=box;
            else BRepBndLib::Add(region.Footprint,region.ProjectedBounds,false);
            if(region.Spatial&&region.Footprint.IsNull()){
                double x,y,z,X,Y,Z;region.ProjectedBounds.Get(x,y,z,X,Y,Z);region.ProjectedBounds.SetVoid();
                region.ProjectedBounds.Add(gp_Pnt(y,z,0));region.ProjectedBounds.Add(gp_Pnt(Y,Z,0));
            }
            regions[at]=std::move(region);
        });
        for(auto& region:regions)if(region){if(region->PlanarAreaScale==0)++nonPlanar;result.push_back(std::move(*region));}
        diagnostics.push_back("Projected independent material faces with CAD Task workers="+std::to_string(workers));
        diagnostics.push_back("Copied "+std::to_string(privateFaces.size())+" material-support faces after filtering "+std::to_string(built.Faces.size()-privateFaces.size())+" non-projecting faces");
        std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.PlanarAreaScale>b.PlanarAreaScale;});
        diagnostics.push_back("Retained trimmed material regions from " + std::to_string(result.size()-nonPlanar)
            + " planar source faces and " + std::to_string(nonPlanar)
            + " curved source faces for axial-prism intersection");
        return result;
    }

    using section_detail::ProjectionMultiplicityBound;
    using section_detail::SingleValuedMaterialPatches;

    bool CandidateIsMaterialRegion(const SLoopCandidate& candidate,
        const std::vector<SMaterialFaceProjection>& material,double tolerance,std::string& reason,
        const std::function<void(const std::string&)>& progress,std::size_t maximumConcurrency=0)
    {
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 2
        // MSVC checked containers serialize their iterator bookkeeping. These
        // allocation-heavy Booleans lose throughput under that global lock:
        // keep this stage serial, while face projection, normalization and
        // skin classification continue to use the geometry Task pool.
        maximumConcurrency=1;
#endif
        const auto face=CandidateFace(candidate,tolerance);
        if(face.IsNull())throw std::runtime_error("Cannot build inner candidate for material classification");
        GProp_GProps candidateProperties;BRepGProp::SurfaceProperties(face,candidateProperties,1e-9);
        const auto candidateArea=std::abs(candidateProperties.Mass());
        GProp_GProps perimeter;BRepGProp::LinearProperties(face,perimeter);
        // Permit only a boundary strip at the configured geometric precision;
        // this is an area uncertainty, not a small-cavity size filter.
        const double areaUncertainty=tolerance*std::max(tolerance,perimeter.Mass());
        const bool belowAreaPrecision=candidateArea<=areaUncertainty;
        Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,false,false);
        TopoDS_Shape prism;
        if(std::any_of(material.begin(),material.end(),[](const auto& region){return region.Spatial;})){
            double first=(std::numeric_limits<double>::max)(),last=(std::numeric_limits<double>::lowest)();
            for(const auto& region:material){first=std::min(first,region.FirstX);last=std::max(last,region.LastX);}
            // P is in the projected material region iff the line parallel to
            // X through P meets the original face. Intersecting the filled
            // candidate prism with that face tests this over the whole region,
            // including trimming and holes, without replacing it by a polygon.
            gp_Trsf tr;tr.SetValues(0,0,1,first-tolerance, 1,0,0,0, 0,1,0,0);
            const auto section=BRepBuilderAPI_Transform(face,tr,true).Shape();
            prism=BRepPrimAPI_MakePrism(section,gp_Vec(last-first+2*tolerance,0,0)).Shape();
        }
        double possibleCoverage=0;
        bool hasMaterialFaceIntersection=false;
        std::string coverageEvidence;
        std::vector<const SMaterialFaceProjection*> coveringRegions;
        struct SIntersection {
            bool Overlaps=false,Evaluated=false,Valid=false;
            TopoDS_Shape Argument,Tool,Shape;
            double Area=0;
            std::exception_ptr Error;
        };
        std::vector<SIntersection> intersections(material.size());
        for(std::size_t i=0;i<material.size();++i){
            intersections[i].Overlaps=!bounds.IsOut(material[i].ProjectedBounds);
        }
        const auto workers=iCAX::OpenCascade::detail::GeometryConcurrency(maximumConcurrency);
        bool evaluatedAny=false;
        // First try one support: a single end face often proves a filled cell.
        // Only unresolved cells need bounded parallel batches. Preparing every
        // Boolean up front defeats early acceptance on repeated profiles.
        for(std::size_t i=0;i<material.size();++i) {
            if(!intersections[i].Overlaps)continue;
            if(!intersections[i].Evaluated){
                std::vector<std::size_t> batch;
                const auto batchSize=evaluatedAny?workers:std::size_t{1};
                for(std::size_t j=i;j<material.size()&&batch.size()<batchSize;++j){
                    auto& job=intersections[j];if(!job.Overlaps)continue;
                    const auto& region=material[j];
                    if(progress)progress("material common face="+std::to_string(region.SourceFaceId)+" candidate-area="+std::to_string(candidateArea));
                    job.Argument=BRepBuilderAPI_Copy(region.Spatial&&region.Footprint.IsNull()?prism:TopoDS_Shape(face),true,false).Shape();
                    job.Tool=BRepBuilderAPI_Copy(region.Footprint.IsNull()?region.Face:region.Footprint,true,false).Shape();
                    batch.push_back(j);
                }
                iCAX::OpenCascade::detail::ForGeometryIndices(batch.size(),maximumConcurrency,[&](std::size_t at){
                    auto& job=intersections[batch[at]];
                    try{
                        TopTools_ListOfShape arguments,tools;arguments.Append(job.Argument);tools.Append(job.Tool);
                        BRepAlgoAPI_Common common;common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.Build();
                        job.Valid=common.IsDone();if(job.Valid){job.Shape=common.Shape();job.Valid=BRepCheck_Analyzer(job.Shape).IsValid();}
                        if(job.Valid){GProp_GProps properties;BRepGProp::SurfaceProperties(job.Shape,properties,1e-9);job.Area=properties.Mass();}
                    }catch(...){job.Error=std::current_exception();}
                    job.Evaluated=true;
                });
                evaluatedAny=true;
            }
            // Consume in source order; later speculative failures cannot
            // override an earlier complete material-coverage proof.
            const auto& region=material[i];const auto& intersection=intersections[i];
            if(intersection.Error)std::rethrow_exception(intersection.Error);
            const bool footprint=!region.Footprint.IsNull();
            if(!intersection.Valid) {
                // An invalid Boolean has no positive coverage evidence. The
                // entire original face area remains a conservative upper
                // bound, so a small failed intersection cannot fill a large
                // cavity and a potentially covering face still needs proof.
                GProp_GProps sourceArea;BRepGProp::SurfaceProperties(region.Face,sourceArea,1e-9);
                possibleCoverage+=std::abs(sourceArea.Mass())*(region.PlanarAreaScale>0?region.PlanarAreaScale:1.);
                coveringRegions.push_back(&region);
                coverageEvidence+=" invalid-common source-face="+std::to_string(region.SourceFaceId);
                continue;
            }
            hasMaterialFaceIntersection=hasMaterialFaceIntersection||TopExp_Explorer(intersection.Shape,TopAbs_FACE).More();
            if(intersection.Area<0)throw std::runtime_error("Candidate/source-face intersection has negative material area: source-face="+std::to_string(region.SourceFaceId));
            const auto spatialArea=intersection.Area;
            double upper=spatialArea,lower=region.Spatial?0.0:spatialArea;
            if(region.PlanarAreaScale>0) {
                upper=lower=spatialArea*(footprint?1.:region.PlanarAreaScale);
            }
            else if(region.Spatial&&spatialArea>areaUncertainty){
                // Orthogonal footprint area is integral |normal.X| dA, not
                // the original 3D area. Invertible compression X*=e measures
                // sqrt(nx^2+e^2*(ny^2+nz^2)) dA, an upper bound whose error
                // is at most e*spatialArea. Preserve the actual face trims.
                const auto errorBudget=areaUncertainty/16.0;
                const auto compression=errorBudget/spatialArea;
                gp_GTrsf project;project.SetVectorialPart(gp_Mat(compression,0,0, 0,1,0, 0,0,1));
                BRepBuilderAPI_GTransform projected(intersection.Shape,project,true);
                if(!projected.IsDone())throw std::runtime_error("Cannot measure projected material coverage");
                GProp_GProps p;BRepGProp::SurfaceProperties(projected.Shape(),p,1e-9);
                upper=std::abs(p.Mass());lower=std::max(0.0,upper-errorBudget);
            }
            possibleCoverage+=upper;
            if(upper>0)coveringRegions.push_back(&region);
            if(upper>0)coverageEvidence+=" face="+std::to_string(region.SourceFaceId)
                +" coverage=["+std::to_string(lower)+","+std::to_string(upper)+"]";
            int multiplicity=1;
            if(region.Spatial&&upper>=candidateArea-areaUncertainty){
                const auto bound=ProjectionMultiplicityBound(region.Face,tolerance);
                if(!bound)continue;
                multiplicity=*bound;
            }
            // A footprint can cover the same point repeatedly (a sphere has
            // front and back sheets). Dividing by a proven maximum fiber
            // multiplicity gives a lower bound on covered area. Reject only
            // when that lower bound fills the cell, within its precision.
            if(!belowAreaPrecision&&lower/multiplicity>=candidateArea-areaUncertainty) {
                reason="source face " + std::to_string(region.SourceFaceId)
                    + " fills candidate cell: covered-area lower bound "
                    + std::to_string(lower/multiplicity) + " of " + std::to_string(candidateArea)
                    + " mm2; axial range ["
                    + std::to_string(region.FirstX) + "," + std::to_string(region.LastX) + "]";
                return true;
            }
        }
        if(!hasMaterialFaceIntersection&&possibleCoverage<candidateArea-areaUncertainty)return false;
        // Resolve overlapping footprints by subtracting their exact axial
        // sweeps from the candidate section. A sweep of a single-valued face
        // is a solid whose section is precisely its footprint. This retains
        // the original trims/pcurves and avoids a singular flattening transform.
        // Never fuse source solids or count overlapping areas twice.
        if(possibleCoverage>=candidateArea-areaUncertainty) {
            // Already planar footprints share the exact candidate plane.
            // Subtract there directly: an artificial very thin swept solid
            // can be swallowed by Boolean tolerances on otherwise exact caps.
            const bool planarFootprints=std::all_of(coveringRegions.begin(),coveringRegions.end(),
                [](const auto* region){return !region->Footprint.IsNull();});
            gp_Trsf toYZ;toYZ.SetValues(0,0,1,0, 1,0,0,0, 0,1,0,0);
            TopoDS_Shape uncovered=planarFootprints?TopoDS_Shape(face):BRepBuilderAPI_Transform(face,toYZ,true).Shape();
            for(const auto* region:coveringRegions) {
                const auto patches=planarFootprints?std::vector<TopoDS_Face>{region->Footprint}
                    :!region->Footprint.IsNull()?std::vector<TopoDS_Face>{TopoDS::Face(BRepBuilderAPI_Transform(region->Footprint,toYZ,true).Shape())}
                    :region->PlanarAreaScale>0?std::vector<TopoDS_Face>{region->Face}
                    :SingleValuedMaterialPatches(region->Face,tolerance);
                for(const auto& patch:patches) {
                if(progress)progress("material footprint subtraction face="+std::to_string(region->SourceFaceId));
                gp_Trsf shift;shift.SetTranslation(gp_Vec(region->Footprint.IsNull()?-region->LastX-tolerance:-tolerance,0,0));
                const auto start=BRepBuilderAPI_Transform(patch,shift,true).Shape();
                auto sweep=planarFootprints?TopoDS_Shape(patch):BRepPrimAPI_MakePrism(start,gp_Vec(region->LastX-region->FirstX+2*tolerance,0,0)).Shape();
                if(sweep.IsNull()||!BRepCheck_Analyzer(sweep).IsValid())continue;
                if(!planarFootprints){GProp_GProps volume;BRepGProp::VolumeProperties(sweep,volume);
                    if(volume.Mass()<0)sweep.Reverse();}
                TopTools_ListOfShape subtractArguments,subtractTools;
                subtractArguments.Append(uncovered);subtractTools.Append(sweep);
                BRepAlgoAPI_Cut subtract;subtract.SetArguments(subtractArguments);subtract.SetTools(subtractTools);
                subtract.SetNonDestructive(true);subtract.Build();
                if(!subtract.IsDone()||!BRepCheck_Analyzer(subtract.Shape()).IsValid()) {
                    // An unusable redundant support supplies no coverage
                    // evidence. Keep the previous exact remainder; another
                    // valid support may still prove the complete union.
                    if(progress)progress("material footprint subtraction supplied no proof face="
                        +std::to_string(region->SourceFaceId));
                    continue;
                }
                uncovered=subtract.Shape();
                GProp_GProps remaining;BRepGProp::SurfaceProperties(uncovered,remaining,1e-9);
                const auto remainingArea=std::abs(remaining.Mass());
                if(progress)progress("material footprint uncovered-area="+std::to_string(remainingArea));
                const bool empty= !TopExp_Explorer(uncovered,TopAbs_FACE).More();
                // Below boundary-area precision an area comparison cannot
                // classify anything. Require an exactly empty topological
                // remainder instead; never discard a small cell by its size.
                if(empty||(!belowAreaPrecision&&remainingArea<=areaUncertainty)) {
                    reason="exact union of source-face footprints fills candidate cell; uncovered-area="
                        +std::to_string(remainingArea)+" of "+std::to_string(candidateArea)+" mm2";
                    return true;
                }
                }
            }
            throw std::runtime_error("Material projection coverage is ambiguous for a candidate cell; exact footprint subdivision is required; candidate-area="
                +std::to_string(candidateArea)+" area-uncertainty="+std::to_string(areaUncertainty)
                +" summed-upper="+std::to_string(possibleCoverage)+coverageEvidence);
        }
        return false;
    }

    void AppendSamples(IN const SSectionWireEdge& Edge_, IN OUT std::vector<Point2>& Points_)
    {
        if (Edge_.Samples.empty())
        {
            if (Points_.empty())
            {
                Points_.push_back(Edge_.Start);
            }
            Points_.push_back(Edge_.End);
            return;
        }
        if (Points_.empty())
        {
            Points_ = Edge_.Samples;
            return;
        }
        Points_.insert(Points_.end(), Edge_.Samples.begin() + 1, Edge_.Samples.end());
    }

    double SignedArea(IN const std::vector<Point2>& Points_)
    {
        if (Points_.size() < 3)
        {
            return 0.0;
        }
        double _Area = 0.0;
        for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
        {
            const auto& _A = Points_[_Index];
            const auto& _B = Points_[(_Index + 1) % Points_.size()];
            _Area += _A.X * _B.Y - _B.X * _A.Y;
        }
        return 0.5 * _Area;
    }

    std::optional<SLoopCandidate> TraceLoop(
        IN std::size_t StartEdge_,
        IN std::size_t StartNode_,
        IN const SSectionWireOptions& Options_,
        IN const std::vector<SGraphNode>& Nodes_,
        IN const std::vector<SGraphEdge>& GraphEdges_)
    {
        SLoopCandidate _Result;
        std::unordered_set<std::size_t> _LocalEdges;
        std::unordered_set<std::size_t> _VisitedNodes{ StartNode_ };
        auto _CurrentEdge = StartEdge_;
        auto _CurrentNode = StartNode_;
        const auto _StartNode = StartNode_;
        for (std::size_t _Step = 0; _Step <= GraphEdges_.size(); ++_Step)
        {
            if (_CurrentEdge >= GraphEdges_.size()
                || !GraphEdges_[_CurrentEdge].bActive
                || GraphEdges_[_CurrentEdge].bUsed
                || !_LocalEdges.insert(_CurrentEdge).second)
            {
                return std::nullopt;
            }
            const auto& _GraphEdge = GraphEdges_[_CurrentEdge];
            const auto _NextNode = OtherNode(_GraphEdge, _CurrentNode);
            auto _Projected = _GraphEdge.Projected;
            const auto _Reversed = _CurrentNode != _GraphEdge.StartNode;
            _Projected.bReversed = _Reversed;
            if (_Reversed)
            {
                std::swap(_Projected.Start, _Projected.End);
                std::reverse(_Projected.Samples.begin(), _Projected.Samples.end());
            }
            _Result.Edges.push_back(std::move(_Projected));
            _Result.GraphEdgeIndices.push_back(_CurrentEdge);
            AppendSamples(_Result.Edges.back(), _Result.Points);

            if (_NextNode == _StartNode)
            {
                if (_Result.Points.size() > 1
                    && SamePoint(_Result.Points.front(), _Result.Points.back(), Options_.dConnectionTolerance))
                {
                    _Result.Points.pop_back();
                }
                _Result.dSignedArea = SignedArea(_Result.Points);
                return std::abs(_Result.dSignedArea) > Options_.dConnectionTolerance
                    * Options_.dConnectionTolerance
                    ? std::optional<SLoopCandidate>(std::move(_Result))
                    : std::nullopt;
            }
            if (!_VisitedNodes.insert(_NextNode).second)
            {
                return std::nullopt;
            }

            std::vector<std::size_t> _Candidates;
            for (const auto _Candidate : Nodes_[_NextNode].IncidentEdges)
            {
                if (!GraphEdges_[_Candidate].bActive
                    || GraphEdges_[_Candidate].bUsed || _LocalEdges.contains(_Candidate))
                {
                    continue;
                }
                _Candidates.push_back(_Candidate);
            }
            if (_Candidates.empty())
            {
                return std::nullopt;
            }

            const auto _Incoming = _Result.Edges.back().Samples.size() >= 2
                ? Point2{
                    _Result.Edges.back().Samples.back().X
                        - _Result.Edges.back().Samples[_Result.Edges.back().Samples.size() - 2].X,
                    _Result.Edges.back().Samples.back().Y
                        - _Result.Edges.back().Samples[_Result.Edges.back().Samples.size() - 2].Y }
                : Point2{ 1.0, 0.0 };
            const auto _TurnAngle = [&](IN const Point2& Outgoing_) {
                auto _Turn = std::atan2(
                    Cross2(_Incoming, Outgoing_),
                    Dot2(_Incoming, Outgoing_));
                if (_Turn < 0.0)
                {
                    _Turn += 2.0 * kPi;
                }
                return _Turn;
            };
            const auto _IsUTurn = [&](IN double Turn_) {
                return std::abs(Turn_ - kPi)
                    <= Options_.dUTurnAngleToleranceRadians;
            };
            if (_Candidates.size() == 1)
            {
                if (_IsUTurn(_TurnAngle(OutgoingDirection(
                        GraphEdges_[_Candidates.front()],
                        _NextNode,
                        Nodes_))))
                {
                    return std::nullopt;
                }
                _CurrentEdge = _Candidates.front();
                _CurrentNode = _NextNode;
                continue;
            }

            auto _Best = (std::numeric_limits<std::size_t>::max)();
            double _BestTurn = -(std::numeric_limits<double>::max)();
            double _BestLocalTurn = -(std::numeric_limits<double>::max)();
            for (const auto _Candidate : _Candidates)
            {
                const auto _Outgoing = OutgoingDirection(
                    GraphEdges_[_Candidate],
                    _NextNode,
                    Nodes_);
                const auto _Turn = _TurnAngle(_Outgoing);
                if (_IsUTurn(_Turn))
                {
                    continue;
                }
                // The outer boundary walk uses the largest left turn. The
                // signed angle is measured from the incoming motion tangent
                // to the candidate outgoing tangent, in the YOZ plane.

                // Two edges can leave a node with the same first-order
                // tangent.  A typical example is a straight edge and a
                // tangent circular arc.  In that case compare signed
                // curvature, not a fixed-distance look-ahead.  Curvature is
                // scale independent and does not disappear merely because
                // the edge is sampled at a small physical interval.
                const auto _LocalTurn = DirectedCurvature(
                    GraphEdges_[_Candidate],
                    _NextNode==GraphEdges_[_Candidate].StartNode);

                constexpr auto _TurnTieTolerance = 1.0e-4;
                const auto _PrimaryTurnIsBetter = _Turn
                    > _BestTurn + _TurnTieTolerance;
                const auto _PrimaryTurnIsTied = std::abs(_Turn - _BestTurn)
                    <= _TurnTieTolerance;
                const auto _LocalTurnSide = _LocalTurn > _TurnTieTolerance
                    ? 1
                    : (_LocalTurn < -_TurnTieTolerance ? -1 : 0);
                const auto _BestLocalTurnSide = _BestLocalTurn > _TurnTieTolerance
                    ? 1
                    : (_BestLocalTurn < -_TurnTieTolerance ? -1 : 0);
                // Curvature magnitude is not an outer/inner criterion.  Only
                // its sign tells whether the candidate bends left or right;
                // if both candidates bend to the same side, keep the
                // topology/ID tie-break rather than preferring a tighter arc.
                const auto _LocalTurnIsBetter = _LocalTurnSide
                    > _BestLocalTurnSide;
                const auto _LocalTurnIsTied = _LocalTurnSide
                    == _BestLocalTurnSide;
                if (_Best == (std::numeric_limits<std::size_t>::max)()
                    || _PrimaryTurnIsBetter
                    || (_PrimaryTurnIsTied && _LocalTurnIsBetter)
                    || (_PrimaryTurnIsTied && _LocalTurnIsTied && _Candidate < _Best))
                {
                    _BestTurn = _Turn;
                    _BestLocalTurn = _LocalTurn;
                    _Best = _Candidate;
                }
            }
            if (_Best == (std::numeric_limits<std::size_t>::max)())
            {
                return std::nullopt;
            }
            _CurrentEdge = _Best;
            _CurrentNode = _NextNode;
        }
        return std::nullopt;
    }

    struct SOuterSeed final
    {
        std::size_t EdgeIndex = 0;
        std::size_t StartNode = 0;
        SContourStartKey Key{};
    };

    std::vector<SOuterSeed> FindOuterSeeds(
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN double Tolerance_)
    {
        std::vector<SOuterSeed> _Seeds;
        for (std::size_t _EdgeIndex = 0; _EdgeIndex < GraphEdges_.size(); ++_EdgeIndex)
        {
            const auto& _GraphEdge = GraphEdges_[_EdgeIndex];
            if (!_GraphEdge.bActive
                || _GraphEdge.StartNode == _GraphEdge.EndNode)
            {
                continue;
            }
            for (const bool _Reverse : { false, true })
            {
                _Seeds.push_back({
                    _EdgeIndex,
                    _Reverse ? _GraphEdge.EndNode : _GraphEdge.StartNode,
                    ContourStartKey(_GraphEdge.Projected, _Reverse, Tolerance_) });
            }
        }

        std::sort(_Seeds.begin(), _Seeds.end(), [](IN const auto& Left_, IN const auto& Right_) {
            return std::tie(Left_.Key, Left_.EdgeIndex, Left_.StartNode)
                < std::tie(Right_.Key, Right_.EdgeIndex, Right_.StartNode);
        });
        if (!_Seeds.empty())
        {
            const auto _LowestY = _Seeds.front().Key[0];
            _Seeds.erase(std::find_if(_Seeds.begin(), _Seeds.end(), [&](const auto& Seed_) {
                return Seed_.Key[0] > _LowestY;
            }), _Seeds.end());
        }
        return _Seeds;
    }

    void ReverseWire(IN OUT SSectionWire& Wire_)
    {
        std::reverse(Wire_.Edges.begin(), Wire_.Edges.end());
        for (auto& _Edge : Wire_.Edges)
        {
            std::swap(_Edge.Start, _Edge.End);
            _Edge.bReversed = !_Edge.bReversed;
            std::reverse(_Edge.Samples.begin(), _Edge.Samples.end());
        }
        Wire_.dSignedArea = -Wire_.dSignedArea;
    }

    void StartWireAtLowestEdge(IN OUT SSectionWire& Wire_, IN double Tolerance_)
    {
        if (Wire_.Edges.empty()) return;
        auto _First = Wire_.Edges.begin();
        auto _BestKey = ContourStartKey(*_First, false, Tolerance_);
        for (auto _Edge = _First + 1; _Edge != Wire_.Edges.end(); ++_Edge)
        {
            const auto _Key = ContourStartKey(*_Edge, false, Tolerance_);
            if (_Key < _BestKey)
            {
                _First = _Edge;
                _BestKey = _Key;
            }
        }
        std::rotate(Wire_.Edges.begin(), _First, Wire_.Edges.end());
    }
}

static SSectionWiresResult ExtractAxialSectionWires(
    IN const iCAX::GeometryData::BRepModel& Geometry_,
    IN const SSectionWireOptions& Options_)
{
    SSectionWiresResult _Result;
    const auto progress=[&](const std::string& message){if(Options_.Progress)Options_.Progress(message);};
    progress("build source");
    if (Options_.dFaceParallelToleranceRadians <= 0.0
        || Options_.dFaceParallelToleranceRadians >= 0.5 * kPi
        || Options_.dConnectionTolerance <= 0.0
        || Options_.nCurveSampleCount < 8
        || Options_.dUTurnAngleToleranceRadians <= 0.0
        || Options_.dUTurnAngleToleranceRadians >= 0.5 * kPi)
    {
        _Result.Diagnostics.push_back("Section wire options are invalid");
        return _Result;
    }

    const SGeometryIndex _Index(Geometry_);
    double _PartFirstX = (std::numeric_limits<double>::max)();
    double _PartLastX = (std::numeric_limits<double>::lowest)();
    for (const auto& _Vertex : Geometry_.Vertices)
    {
        _PartFirstX = std::min(_PartFirstX, _Vertex.Position.X);
        _PartLastX = std::max(_PartLastX, _Vertex.Position.X);
    }
    const auto _PartAxialSpan = _PartLastX - _PartFirstX;
    std::size_t _SkippedLocalCutWires = 0;
    std::size_t _TransverseFaces = 0;
    const auto _SourceShape = iCAX::OpenCascade::ReconstructOpenCascadeShape(Geometry_);
    if (!_SourceShape.bOK)
    {
        _Result.Diagnostics.push_back("Cannot reconstruct source faces for section boundaries");
        return _Result;
    }
    std::unordered_map<std::uint64_t, std::tuple<const BRepEdge*, std::uint64_t, std::uint64_t>> _Sources;
    decltype(_Sources) _TransverseSources;
    std::unordered_map<std::uint64_t, std::uint64_t> _AxialSourceFaces;
    for (const auto& _Face : Geometry_.Faces)
    {
        const bool _SideFace = IsSideFace(_Face, _Index, Options_);
        bool _TransverseFace = false;
        if (!_SideFace)
        {
            const auto _Found = _SourceShape.Faces.find(_Face.Id);
            if (_Found != _SourceShape.Faces.end())
            {
                const auto _Surface = BRep_Tool::Surface(_Found->second);
                if (!_Surface.IsNull())
                {
                    const GeomLib_IsPlanarSurface _Planar(_Surface, Options_.dConnectionTolerance);
                    _TransverseFace = _Planar.IsPlanar()
                        && std::abs(_Planar.Plan().Axis().Direction().X())
                            >= std::cos(Options_.dFaceParallelToleranceRadians);
                }
            }
        }
        if (!_SideFace && !_TransverseFace)
        {
            continue;
        }
        if (_TransverseFace) ++_TransverseFaces;
        std::vector<double> _WireAxialSpans;
        _WireAxialSpans.reserve(_Face.WireIds.size());
        for (const auto _WireID : _Face.WireIds)
        {
            const auto* _Wire = FindWire(_Index, _WireID);
            double _FirstX = (std::numeric_limits<double>::max)();
            double _LastX = (std::numeric_limits<double>::lowest)();
            if (_Wire)
            {
                for (const auto& _Coedge : _Wire->Coedges)
                {
                    for (const auto _VertexID : { _Coedge.StartVertexId, _Coedge.EndVertexId })
                    {
                        const auto* _Vertex = FindVertex(_Index, _VertexID);
                        if (!_Vertex) continue;
                        _FirstX = std::min(_FirstX, _Vertex->Position.X);
                        _LastX = std::max(_LastX, _Vertex->Position.X);
                    }
                }
            }
            _WireAxialSpans.push_back(_LastX >= _FirstX ? _LastX - _FirstX : 0.0);
        }
        const auto _LargestWireSpan = _WireAxialSpans.empty() ? 0.0
            : *std::max_element(_WireAxialSpans.begin(), _WireAxialSpans.end());
        if (_SideFace) _Result.SideFaceIds.push_back(_Face.Id);
        // Transverse faces supply real boundary edges that need not belong to
        // a longitudinal side face (for example, an end lip next to a chamfer).
        // Without them the projection has dangling sides, which graph pruning
        // incorrectly shortens to an interior cross-edge. Keep their trimmed
        // wires; material-region classification still excludes filled loops.
        // A hole drilled through a side wall adds local inner wires to that
        // wall. Their intersection curves are machining boundaries, not the
        // profile at a clean axial station. Only filter them when a separate
        // wire on the same face demonstrably spans most of the tube length.
        const bool _HasSpanningBoundary = _SideFace && _PartAxialSpan > Options_.dConnectionTolerance
            && _LargestWireSpan >= 0.9 * _PartAxialSpan;
        for (std::size_t _WireIndex = 0; _WireIndex < _Face.WireIds.size(); ++_WireIndex)
        {
            if (_HasSpanningBoundary
                && _WireAxialSpans[_WireIndex] + Options_.dConnectionTolerance
                    < 0.5 * _LargestWireSpan)
            {
                ++_SkippedLocalCutWires;
                continue;
            }
            const auto _WireID = _Face.WireIds[_WireIndex];
            const auto* _Wire = FindWire(_Index, _WireID);
            if (!_Wire)
            {
                continue;
            }
            for (const auto& _Coedge : _Wire->Coedges)
            {
                const auto* _Edge = FindEdge(_Index, _Coedge.EdgeId);
                if (!_Edge || _Edge->Degenerated || _Edge->Curve3Id == 0)
                {
                    continue;
                }
                auto& _FaceSources = _SideFace ? _Sources : _TransverseSources;
                const auto* _Surface = FindSurface(_Index, _Face.Surface3Id);
                if (_SideFace && _Surface
                    && (std::holds_alternative<iCAX::GeometryData::CylindricalSurface3>(*_Surface)
                        || std::holds_alternative<iCAX::GeometryData::RevolutionSurface3>(*_Surface)
                        || std::holds_alternative<iCAX::GeometryData::ExtrusionSurface3>(*_Surface)
                        || std::holds_alternative<iCAX::GeometryData::BSplineSurface3>(*_Surface)))
                    _AxialSourceFaces.emplace(_Edge->Id, _Face.Id);
                auto [_Iter, _Inserted] = _FaceSources.emplace(
                    _Edge->Id,
                    std::make_tuple(
                        _Edge,
                        _Edge->StartVertexId != 0 ? _Edge->StartVertexId : _Coedge.StartVertexId,
                        _Edge->EndVertexId != 0 ? _Edge->EndVertexId : _Coedge.EndVertexId));
                if (!_Inserted)
                {
                    auto& [_StoredEdge, _StartID, _EndID] = _Iter->second;
                    if (_StartID == 0) _StartID = _Coedge.StartVertexId;
                    if (_EndID == 0) _EndID = _Coedge.EndVertexId;
                    (void)_StoredEdge;
                }
            }
        }
    }
    if (_SkippedLocalCutWires > 0)
    {
        _Result.Diagnostics.push_back(
            "Ignored " + std::to_string(_SkippedLocalCutWires)
            + " local side-face cut wires outside the spanning extrusion boundary");
    }
    _Result.Diagnostics.push_back("Retained boundary evidence from " + std::to_string(_TransverseFaces)
        + " transverse planar faces");
    if (_Result.SideFaceIds.empty())
    {
        _Result.Diagnostics.push_back("No face parallel to the X axis was found");
        return _Result;
    }

    std::vector<SSectionWireEdge> _ProjectedEdges;
    _ProjectedEdges.reserve(_Sources.size());
    for (const auto& [_EdgeID, _Source] : _Sources)
    {
        progress("project edge="+std::to_string(_EdgeID));
        const auto* _Curve = [&]() -> const Curve3* {
            const auto _Iter = _Index.Curves.find(std::get<0>(_Source)->Curve3Id);
            return _Iter == _Index.Curves.end() ? nullptr : _Iter->second;
        }();
        if (!_Curve)
        {
            continue;
        }
        auto _Projected = MakeProjectedEdge(
            *std::get<0>(_Source),
            std::get<1>(_Source),
            std::get<2>(_Source),
            *_Curve,
            _Index,
            Options_);
        // Preserve analytically straight source edges: an axial generator
        // projects to a point. A short curved spline may have been simplified
        // to a line within tolerance, so use its original source type here;
        // it must still receive the exact circular surface projection.
        if (_Projected
            && !std::holds_alternative<iCAX::GeometryData::Line3>(*_Curve)
            && !std::holds_alternative<iCAX::GeometryData::Ray3>(*_Curve)
            && !std::holds_alternative<iCAX::GeometryData::Segment3>(*_Curve))
        {
            if (const auto face = _AxialSourceFaces.find(_EdgeID); face != _AxialSourceFaces.end())
            {
                const auto* sourceFace = [&]() -> const BRepFace* {
                    const auto found = std::find_if(Geometry_.Faces.begin(), Geometry_.Faces.end(),
                        [&](const auto& f) { return f.Id == face->second; });
                    return found == Geometry_.Faces.end() ? nullptr : &*found;
                }();
                const auto* surface = sourceFace ? FindSurface(_Index, sourceFace->Surface3Id) : nullptr;
                const auto* spline = surface ? std::get_if<iCAX::GeometryData::BSplineSurface3>(surface) : nullptr;
                bool pointProjection=false;
                if (const auto boundary = ProjectAxialSurfaceBoundary(_SourceShape.Edges.at(_EdgeID),
                        _SourceShape.Faces.at(face->second), _EdgeID, Options_, spline,&pointProjection))
                    _Projected = boundary;
                else if(pointProjection) { progress("authoritative axial boundary projects to a point: source="+std::to_string(_EdgeID));continue; }
            }
        }
        if (!_Projected)
        {
            continue;
        }
        _ProjectedEdges.push_back(*_Projected);
    }
    if (_ProjectedEdges.empty())
    {
        _Result.Diagnostics.push_back("Side faces produced no non-degenerate projected edges");
        return _Result;
    }
    const auto traceProjection=[&](const char* stage,const auto& edges) {
        if(!Options_.Progress)return;
        for(const auto& edge:edges)progress(std::string("projection ")+stage+" source="+std::to_string(edge.SourceEdgeId)
            +" start=("+std::to_string(edge.Start.X)+","+std::to_string(edge.Start.Y)
            +") end=("+std::to_string(edge.End.X)+","+std::to_string(edge.End.Y)+")");
    };
    traceProjection("source",_ProjectedEdges);

    // 1. Merge coincident coverage, including contained and partially
    // overlapping edges. Only surviving endpoints participate in step 2.
    const auto _UnmergedCount = _ProjectedEdges.size();
    progress("merge projected edges count="+std::to_string(_UnmergedCount));
    _ProjectedEdges = MergeProjectedEdges(std::move(_ProjectedEdges), Options_);
    traceProjection("merged",_ProjectedEdges);
    _Result.Diagnostics.push_back("Merged " + std::to_string(_UnmergedCount - _ProjectedEdges.size())
        + " overlapping projected edges before endpoint-on-edge splitting");

    // 2. The original BRep edge endpoints are not sufficient graph vertices:
    // one edge endpoint may lie in the interior of another projected edge.
    // Planarize those T-junctions before constructing the connectivity graph.
    const auto _ProjectedEdgeIndex = BuildProjectedEdgeIndex(
        _ProjectedEdges,
        Options_.dConnectionTolerance);
    std::vector<std::vector<double>> _SplitParameters(_ProjectedEdges.size());
    std::size_t _InteriorSplitCount = 0;
    progress("split endpoint contacts");
    for (std::size_t _EdgeIndex = 0; _EdgeIndex < _ProjectedEdges.size(); ++_EdgeIndex)
    {
        const auto& _Edge = _ProjectedEdges[_EdgeIndex];
        const std::array<Point2, 2> _Endpoints{ _Edge.Start, _Edge.End };
        for (const auto& _Endpoint : _Endpoints)
        {
            for (const auto _HostIndex : _ProjectedEdgeIndex.Query(
                _Endpoint,
                Options_.dConnectionTolerance))
            {
                if (_HostIndex == _EdgeIndex)
                {
                    continue;
                }
                const auto& _Host = _ProjectedEdges[_HostIndex];
                if (SamePoint(_Endpoint, _Host.Start, Options_.dConnectionTolerance)
                    || SamePoint(_Endpoint, _Host.End, Options_.dConnectionTolerance))
                {
                    // This is already represented by the endpoint-node merge.
                    continue;
                }
                const auto _Projection = ProjectPointToEdge(
                    _Host,
                    _Endpoint,
                    Options_.dConnectionTolerance);
                if (!_Projection
                    || _Projection->U <= 1.0e-8
                    || _Projection->U >= 1.0 - 1.0e-8)
                {
                    continue;
                }
                _SplitParameters[_HostIndex].push_back(_Projection->U);
            }
        }
    }

    // Projection can introduce crossings in the interiors of both edges.
    // Endpoint-on-edge tests alone do not produce a planar embedding.
    std::vector<Handle(Geom2d_Curve)> intersectionCurves;
    for(const auto& edge:_ProjectedEdges){
        const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(edge.Curve);
        intersectionCurves.push_back(curve.IsNull()?curve:Handle(Geom2d_Curve)(new Geom2d_TrimmedCurve(curve,edge.Range.First,edge.Range.Last)));
    }
    for(std::size_t i=0;i<_ProjectedEdges.size();++i)for(std::size_t j=i+1;j<_ProjectedEdges.size();++j){
        if(intersectionCurves[i].IsNull()||intersectionCurves[j].IsNull())continue;
        const auto& a=_ProjectedEdgeIndex.Bounds[i];const auto& b=_ProjectedEdgeIndex.Bounds[j];
        const double tolerance=Options_.dConnectionTolerance;
        if(a.MaximumX+tolerance<b.MinimumX||b.MaximumX+tolerance<a.MinimumX
            ||a.MaximumY+tolerance<b.MinimumY||b.MaximumY+tolerance<a.MinimumY)continue;
        if(Options_.Progress)progress("intersect projected edges="+std::to_string(_ProjectedEdges[i].SourceEdgeId)
            +","+std::to_string(_ProjectedEdges[j].SourceEdgeId));
        Geom2dAPI_InterCurveCurve intersection(intersectionCurves[i],intersectionCurves[j],tolerance);
        for(int k=1;k<=intersection.NbPoints();++k){
            const auto p=intersection.Point(k);const Point2 point{p.X(),p.Y()};
            for(const auto index:{i,j})if(const auto projection=ProjectPointToEdge(_ProjectedEdges[index],point,tolerance))
                if(projection->U>1e-8&&projection->U<1.0-1e-8)_SplitParameters[index].push_back(projection->U);
        }
    }
    std::vector<SSectionWireEdge> _PlanarizedEdges;
    _PlanarizedEdges.reserve(_ProjectedEdges.size() + _InteriorSplitCount);
    for (std::size_t _EdgeIndex = 0; _EdgeIndex < _ProjectedEdges.size(); ++_EdgeIndex)
    {
        auto& _Cuts = _SplitParameters[_EdgeIndex];
        std::sort(_Cuts.begin(), _Cuts.end());
        _Cuts.erase(
            std::unique(_Cuts.begin(), _Cuts.end(), [](IN double Left_, IN double Right_) {
                return std::abs(Left_ - Right_) <= 1.0e-8;
            }),
            _Cuts.end());
        _InteriorSplitCount += _Cuts.size();
        const auto _Pieces = SplitProjectedEdge(
            _ProjectedEdges[_EdgeIndex],
            _Cuts,
            Options_);
        _PlanarizedEdges.insert(
            _PlanarizedEdges.end(),
            _Pieces.begin(),
            _Pieces.end());
    }
    if (_InteriorSplitCount > 0)
    {
        _Result.Diagnostics.push_back(
            "Split " + std::to_string(_InteriorSplitCount)
            + " projected edges at endpoint-on-edge interior contacts");
    }

    // Splitting may expose coincident arcs represented by different source
    // curve types (e.g. an analytic circle and an IGES/STEP spline). Collapse
    // only equal complete pieces here; never merge across a newly split node.
    std::vector<SSectionWireEdge> uniquePieces;
    for(auto& piece:_PlanarizedEdges) {
        auto found=std::find_if(uniquePieces.begin(),uniquePieces.end(),[&](const auto& other){
            return SameProjectedEdge(piece,other,Options_.dConnectionTolerance);
        });
        if(found==uniquePieces.end())uniquePieces.push_back(std::move(piece));
        else if(ConicInterval(piece.Curve)&&!ConicInterval(found->Curve))*found=std::move(piece);
    }
    _PlanarizedEdges=std::move(uniquePieces);
    traceProjection("split",_PlanarizedEdges);

    // 3. Merge points only after all remaining endpoint-on-edge contacts have
    // split their hosts. Do not merge edges again: these splits define nodes.
    std::vector<SGraphNode> _Nodes;
    std::vector<SGraphEdge> _GraphEdges;
    std::unordered_map<SGridKey, std::vector<std::size_t>, SGridKeyHash> _Grid;
    _Nodes.reserve(_PlanarizedEdges.size());
    _GraphEdges.reserve(_PlanarizedEdges.size());
    for (const auto& _Projected : _PlanarizedEdges)
    {
        const auto _StartNode=FindOrAddNode(_Projected.Start,Options_.dConnectionTolerance,_Nodes,_Grid);
        const auto _EndNode=FindOrAddNode(_Projected.End,Options_.dConnectionTolerance,_Nodes,_Grid);
        // Endpoint clustering contracts sub-tolerance pieces to a single
        // vertex. Such a piece must not remain a directed graph self-loop.
        // Test the entire exact-curve bounding box: a genuine closed circle
        // also shares an endpoint but has non-degenerate spatial extent.
        if (_StartNode == _EndNode) {
            const auto bounds = GetBounds(_Projected);
            if (std::hypot(bounds.MaximumX-bounds.MinimumX,
                           bounds.MaximumY-bounds.MinimumY) <= Options_.dConnectionTolerance)
                continue;
        }
        const auto _GraphIndex = _GraphEdges.size();
        _GraphEdges.push_back({ _Projected, _StartNode, _EndNode, false });
        _Nodes[_StartNode].IncidentEdges.push_back(_GraphIndex);
        if (_EndNode != _StartNode)
        {
            _Nodes[_EndNode].IncidentEdges.push_back(_GraphIndex);
        }
    }


    // Complete dangling side boundaries only with actual transverse BRep
    // edges. Importers may encode the same corner differently on an end face
    // and its side face; injecting every end-face curve would duplicate those
    // already connected boundaries. A completion must connect a free end to
    // an existing node (which may already be a branch), without crossing the
    // graph or duplicating an existing edge.
    struct SBoundaryCompletion { SSectionWireEdge Edge; std::size_t Start, End; };
    std::vector<SBoundaryCompletion> completions;
    std::vector<SSectionWireEdge> completionEdges;
    std::vector<std::uint64_t> transverseIds;
    for (const auto& [id, source] : _TransverseSources) transverseIds.push_back(id);
    std::sort(transverseIds.begin(), transverseIds.end());
    for (const auto id : transverseIds)
    {
        const auto& source = _TransverseSources.at(id);
        const auto found = _Index.Curves.find(std::get<0>(source)->Curve3Id);
        if (found == _Index.Curves.end()) continue;
        const auto* curve = found->second;
        const auto edge = MakeProjectedEdge(*std::get<0>(source), std::get<1>(source),
            std::get<2>(source), *curve, _Index, Options_);
        if (edge) completionEdges.push_back(*edge);
    }
    // A rounded end can have no topological edge at the extreme of the
    // projection. On a cylinder its silhouette satisfies n dot X = 0: cut
    // the original trimmed face by the plane through its axis with normal
    // X - (X dot axis) axis. Only genuine face-clipped generators can close
    // free endpoints; this never draws an arbitrary segment across a gap.
    if (std::count_if(_Nodes.begin(), _Nodes.end(), [&](const auto& node) {
        return Degree(static_cast<std::size_t>(&node - _Nodes.data()), _Nodes, _GraphEdges) == 1;
    }) >= 2)
    {
        for (const auto& face : Geometry_.Faces)
        {
            const auto* surface = FindSurface(_Index, face.Surface3Id);
            if (!surface || IsSideFace(face, _Index, Options_)) continue;
            std::optional<gp_Ax1> axis;
            std::visit([&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, iCAX::GeometryData::CylindricalSurface3>)
                    axis = gp_Ax1(gp_Pnt(s.Placement.Location.X,s.Placement.Location.Y,s.Placement.Location.Z),
                        gp_Dir(s.Placement.ZDirection.X,s.Placement.ZDirection.Y,s.Placement.ZDirection.Z));
                else if constexpr (std::is_same_v<T, iCAX::GeometryData::RevolutionSurface3>)
                {
                    const bool axial = std::visit([&](const auto& basis) {
                        using C = std::decay_t<decltype(basis)>;
                        if constexpr (std::is_same_v<C,iCAX::GeometryData::Line3>)
                            return std::abs(Dot(ToVector(basis.Axis.Direction),ToVector(s.Axis.Direction))) >= 1.0-1.0e-12;
                        else return false;
                    },s.Basis);
                    if(axial)axis=gp_Ax1(gp_Pnt(s.Axis.Location.X,s.Axis.Location.Y,s.Axis.Location.Z),
                        gp_Dir(s.Axis.Direction.X,s.Axis.Direction.Y,s.Axis.Direction.Z));
                }
            },*surface);
            if (!axis) continue;
            progress("silhouette source-face="+std::to_string(face.Id));
            const gp_Vec a(axis->Direction()), projection(1,0,0);
            const gp_Vec normal = projection - a * projection.Dot(a);
            if (normal.SquareMagnitude() < 1.0e-20) continue;
            BRepAlgoAPI_Section cut(_SourceShape.Faces.at(face.Id),gp_Pln(axis->Location(),gp_Dir(normal)),false);
            cut.SetNonDestructive(true);cut.Build();
            if (!cut.IsDone()) continue;
            const auto silhouette = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(cut.Shape(),"","",Options_.dConnectionTolerance);
            const SGeometryIndex silhouetteIndex(silhouette);
            for (const auto& source : silhouette.Edges)
            {
                const auto found = silhouetteIndex.Curves.find(source.Curve3Id);
                if (found == silhouetteIndex.Curves.end()) continue;
                auto edge = MakeProjectedEdge(source,source.StartVertexId,source.EndVertexId,*found->second,silhouetteIndex,Options_);
                if (!edge) continue;
                edge->SourceEdgeId=0;edge->SourceFaceId=face.Id;
                completionEdges.push_back(std::move(*edge));
            }
        }
    }
    if(std::any_of(_Nodes.begin(),_Nodes.end(),[&](const auto& node) {
        return Degree(static_cast<std::size_t>(&node-_Nodes.data()),_Nodes,_GraphEdges)==1;
    })) {
    // End-face evidence can meet a side boundary in its interior or contain
    // intermediate vertices. Planarize those actual contacts before finding
    // a completion; requiring one end-face edge to join two existing free
    // nodes misses valid channel flanges.
    completionEdges=MergeProjectedEdges(std::move(completionEdges),Options_);
    std::vector<SGraphEdge> sidePieces;
    for(const auto& existing:_GraphEdges) {
        std::vector<double> cuts;
        for(const auto& cap:completionEdges)for(const auto point:{cap.Start,cap.End})
            if(const auto projected=ProjectPointToEdge(existing.Projected,point,Options_.dConnectionTolerance))
                if(projected->U>1e-8&&projected->U<1-1e-8)cuts.push_back(projected->U);
        for(const auto& piece:SplitProjectedEdge(existing.Projected,cuts,Options_)) {
            const auto start=FindOrAddNode(piece.Start,Options_.dConnectionTolerance,_Nodes,_Grid);
            const auto end=FindOrAddNode(piece.End,Options_.dConnectionTolerance,_Nodes,_Grid);
            if(start!=end)sidePieces.push_back({piece,start,end,false});
            else if(existing.StartNode==existing.EndNode)sidePieces.push_back(existing);
        }
    }
    _GraphEdges=std::move(sidePieces);
    for(auto& node:_Nodes)node.IncidentEdges.clear();
    for(std::size_t i=0;i<_GraphEdges.size();++i) {
        const auto& edge=_GraphEdges[i];_Nodes[edge.StartNode].IncidentEdges.push_back(i);
        if(edge.EndNode!=edge.StartNode)_Nodes[edge.EndNode].IncidentEdges.push_back(i);
    }
    const auto sideNodeCount=_Nodes.size();
    std::vector<SSectionWireEdge> capPieces;
    std::vector<Point2> capContacts;
    for(const auto& node:_Nodes)capContacts.push_back(node.Position);
    for(const auto& cap:completionEdges){capContacts.push_back(cap.Start);capContacts.push_back(cap.End);}
    for(const auto& cap:completionEdges) {
        std::vector<double> cuts;
        for(const auto& point:capContacts)
            if(const auto projected=ProjectPointToEdge(cap,point,Options_.dConnectionTolerance))
                if(projected->U>1e-8&&projected->U<1-1e-8)cuts.push_back(projected->U);
        const auto pieces=SplitProjectedEdge(cap,cuts,Options_);
        capPieces.insert(capPieces.end(),pieces.begin(),pieces.end());
    }
    for (const auto& value : capPieces)
    {
        const auto* edge = &value;
        const auto start=FindOrAddNode(edge->Start,Options_.dConnectionTolerance,_Nodes,_Grid);
        const auto end=FindOrAddNode(edge->End,Options_.dConnectionTolerance,_Nodes,_Grid);
        if(start==end)continue;
        if(start<sideNodeCount&&end<sideNodeCount
            &&Degree(start,_Nodes,_GraphEdges)!=1&&Degree(end,_Nodes,_GraphEdges)!=1)continue;
        if (std::any_of(completions.begin(), completions.end(), [&](const auto& other) {
            return SameProjectedEdge(*edge, other.Edge, Options_.dConnectionTolerance);
        })) continue;
        bool clear = true;
        const Handle(Geom2d_Curve) candidate = new Geom2d_TrimmedCurve(
            iCAX::OpenCascade::BuildOpenCascadeCurve2(edge->Curve), edge->Range.First, edge->Range.Last);
        for (const auto& existing : _GraphEdges)
        {
            if (!existing.bActive) continue;
            if (SameProjectedEdge(*edge, existing.Projected, Options_.dConnectionTolerance)) { clear = false; break; }
            const auto& other = existing.Projected;
            const Handle(Geom2d_Curve) host = new Geom2d_TrimmedCurve(
                iCAX::OpenCascade::BuildOpenCascadeCurve2(other.Curve), other.Range.First, other.Range.Last);
            Geom2dAPI_InterCurveCurve intersection(candidate, host, Options_.dConnectionTolerance);
            if (intersection.NbSegments() > 0) { clear = false; break; }
            for (int k = 1; k <= intersection.NbPoints(); ++k)
            {
                const auto p = intersection.Point(k);
                if (!SamePoint({p.X(), p.Y()}, edge->Start, Options_.dConnectionTolerance)
                    && !SamePoint({p.X(), p.Y()}, edge->End, Options_.dConnectionTolerance)) clear = false;
            }
            if (!clear) break;
        }
        if (clear) completions.push_back({*edge, start, end});
    }
    std::sort(completions.begin(), completions.end(), [](const auto& a, const auto& b) {
        return std::tie(a.Edge.SourceEdgeId,a.Edge.SourceFaceId) < std::tie(b.Edge.SourceEdgeId,b.Edge.SourceFaceId);
    });
    std::vector<std::vector<std::size_t>> capAdjacency(_Nodes.size());
    for(std::size_t i=0;i<completions.size();++i) {
        capAdjacency[completions[i].Start].push_back(i);
        capAdjacency[completions[i].End].push_back(i);
    }
    std::unordered_set<std::size_t> selectedCompletions;
    for(std::size_t start=0;start<sideNodeCount;++start)
    {
        if(Degree(start,_Nodes,_GraphEdges)!=1)continue;
        std::vector<std::vector<std::size_t>> paths;
        std::vector<std::size_t> path;std::vector<bool> visited(_Nodes.size());
        std::function<void(std::size_t)> walk=[&](std::size_t node) {
            if(paths.size()>1)return;
            if(node!=start&&node<sideNodeCount&&Degree(node,_Nodes,_GraphEdges)>0){paths.push_back(path);return;}
            visited[node]=true;
            for(const auto index:capAdjacency[node]) {
                const auto& edge=completions[index];const auto next=edge.Start==node?edge.End:edge.Start;
                if(visited[next])continue;
                path.push_back(index);walk(next);path.pop_back();
                if(paths.size()>1)break;
            }
            visited[node]=false;
        };
        walk(start);
        if(paths.size()==1)selectedCompletions.insert(paths.front().begin(),paths.front().end());
        else if(paths.size()>1)_Result.Diagnostics.push_back("Ambiguous transverse boundary chain was not used");
    }
    for(std::size_t i=0;i<completions.size();++i)if(selectedCompletions.contains(i))
    {
        const auto& completion=completions[i];
        const auto index = _GraphEdges.size();
        _GraphEdges.push_back({completion.Edge, completion.Start, completion.End, false});
        _Nodes[completion.Start].IncidentEdges.push_back(index);
        _Nodes[completion.End].IncidentEdges.push_back(index);
        _Result.Diagnostics.push_back(completion.Edge.SourceFaceId
            ? "Completed dangling projection with exact silhouette of source face " + std::to_string(completion.Edge.SourceFaceId)
            : "Completed dangling projection with transverse source edge " + std::to_string(completion.Edge.SourceEdgeId));
    }
    }

    const auto _BoundaryEvidence = _GraphEdges;
    if(Options_.Progress)for(std::size_t node=0;node<_Nodes.size();++node)if(Degree(node,_Nodes,_GraphEdges)==1) {
        for(const auto edge:_Nodes[node].IncidentEdges)
            progress("projection dangling node="+std::to_string(node)+" source="+std::to_string(_GraphEdges[edge].Projected.SourceEdgeId)
                +" point=("+std::to_string(_Nodes[node].Position.X)+","+std::to_string(_Nodes[node].Position.Y)+")");
    }
    progress("trace outer graph edges="+std::to_string(_GraphEdges.size()));
    const auto _RemovedBeforeOuter = PruneDegreeOneEdges(_Nodes, _GraphEdges);
    if (_RemovedBeforeOuter > 0)
    {
        _Result.Diagnostics.push_back(
            "Pruned " + std::to_string(_RemovedBeforeOuter)
            + " degree-one edges before contour detection");
    }

    const auto _Bridges=PruneBridgeEdges(_Nodes,_GraphEdges);
    if(_Bridges)_Result.Diagnostics.push_back("Removed " + std::to_string(_Bridges) + " non-cyclic bridge edges before contour detection");
    std::vector<SHalfEdge> _HalfEdges;
    progress("sort graph half edges");
    BuildHalfEdges(_GraphEdges, _Nodes, _HalfEdges);
    progress("collect outer faces");
    auto _OuterCandidates = CollectFaces(
        _HalfEdges,
        Options_,
        _Nodes,
        _GraphEdges,
        true);

    std::optional<SLoopCandidate> _Outer;
    std::shared_ptr<SExactCandidateClassifier> outerClassifier;
    std::vector<std::string> _OuterRejections;
    struct SOuterCheck {bool Contains;std::string Reason;std::shared_ptr<SExactCandidateClassifier> Classifier;};
    std::map<std::vector<std::size_t>,SOuterCheck> outerChecks;
    const auto testOuter=[&](const SLoopCandidate& candidate,std::string& reason,
            std::shared_ptr<SExactCandidateClassifier>& classifier) {
        if(candidate.GraphEdgeIndices.empty())return CandidateContainsAllActiveEdges(candidate,_GraphEdges,
            Options_.dConnectionTolerance,&reason,Options_.Progress,&classifier);
        auto key=candidate.GraphEdgeIndices;std::sort(key.begin(),key.end());
        auto known=outerChecks.find(key);
        if(known==outerChecks.end()) {
            const bool contains=CandidateContainsAllActiveEdges(candidate,_GraphEdges,
                Options_.dConnectionTolerance,&reason,Options_.Progress,&classifier);
            known=outerChecks.emplace(std::move(key),SOuterCheck{contains,reason,classifier}).first;
        }
        reason=known->second.Reason;classifier=known->second.Classifier;return known->second.Contains;
    };

    std::stable_sort(_OuterCandidates.begin(),_OuterCandidates.end(),[](const auto& a,const auto& b){
        return std::abs(a.dSignedArea)>std::abs(b.dSignedArea);
    });

    for (auto& _Candidate : _OuterCandidates)
    {
        std::string rejection;
        std::shared_ptr<SExactCandidateClassifier> candidateClassifier;
        const auto _ContainsAll = testOuter(_Candidate,rejection,candidateClassifier);
        if(!_ContainsAll&&_OuterRejections.size()<8)
            _OuterRejections.push_back("Rejected enclosing contour area="+std::to_string(_Candidate.dSignedArea)+": "+rejection);
        if (_ContainsAll)
        {
            if (!_Outer || std::abs(_Candidate.dSignedArea)
                    > std::abs(_Outer->dSignedArea))
            {
                _Outer = std::move(_Candidate);
                outerClassifier=std::move(candidateClassifier);
                break; // No later candidate can exceed this enclosing area.
            }
        }
    }
    // A chord/connection inside a concave section splits the planar embedding
    // into bounded faces.  The actual outside boundary is then no longer one
    // of those faces, so face collection alone cannot recover it.  Trace the
    // boundary from the lowest edge as a second pass; the largest-left-turn
    // walk keeps the outside material on its left while still allowing local
    // concavities.
    if (!_Outer)
    {
        for (const auto& _Seed : FindOuterSeeds(
                 _GraphEdges, Options_.dConnectionTolerance))
        {
            const auto _Candidate = TraceLoop(
                _Seed.EdgeIndex,
                _Seed.StartNode,
                Options_,
                _Nodes,
                _GraphEdges);
            std::shared_ptr<SExactCandidateClassifier> candidateClassifier;
            std::string seedRejection;
            if (!_Candidate || !testOuter(*_Candidate,seedRejection,candidateClassifier))
            {
                if(_Candidate&&Options_.Progress)progress("outer seed rejected area="+std::to_string(_Candidate->dSignedArea)+": "+seedRejection);
                continue;
            }
            if (!_Outer || std::abs(_Candidate->dSignedArea)
                    > std::abs(_Outer->dSignedArea))
            {
                _Outer = std::move(*_Candidate);
                outerClassifier=std::move(candidateClassifier);
            }
        }
    }
    if (!_Outer)
    {
        _Result.Diagnostics.push_back(
            "No closed contour contains all remaining projected edges as the outer contour");
        _Result.Diagnostics.insert(_Result.Diagnostics.end(),_OuterRejections.begin(),_OuterRejections.end());
        for (const auto& edge : _BoundaryEvidence)
        {
            for (const auto node : {edge.StartNode, edge.EndNode})
            {
                if (std::count_if(_BoundaryEvidence.begin(), _BoundaryEvidence.end(), [&](const auto& other) {
                    return other.StartNode == node || other.EndNode == node;
                }) != 1) continue;
                const auto point = node == edge.StartNode ? edge.Projected.Start : edge.Projected.End;
                _Result.Diagnostics.push_back("Unconnected source boundary edge " + std::to_string(edge.Projected.SourceEdgeId)
                    + " at (" + std::to_string(point.X) + "," + std::to_string(point.Y) + ")");
            }
        }
        return _Result;
    }

    std::string prunedBoundaryRejection;
    if (!CandidateContainsAllActiveEdges(*_Outer, _BoundaryEvidence, Options_.dConnectionTolerance,&prunedBoundaryRejection,Options_.Progress,&outerClassifier))
    {
        _Result.Diagnostics.push_back("Outer contour does not enclose source boundary evidence removed by graph pruning: "
            +prunedBoundaryRejection);
        return _Result;
    }
    std::vector<SLoopCandidate> _Loops;
    _Loops.push_back(std::move(*_Outer));
    for (const auto _GraphEdgeIndex : _Loops.front().GraphEdgeIndices)
    {
        _GraphEdges[_GraphEdgeIndex].bActive = false;
    }

    const auto _RemovedAfterOuter = PruneDegreeOneEdges(_Nodes, _GraphEdges);
    if (_RemovedAfterOuter > 0)
    {
        _Result.Diagnostics.push_back(
            "Pruned " + std::to_string(_RemovedAfterOuter)
            + " degree-one edges after removing the outer contour");
    }

    try {
        const auto material=ProjectMaterialFaces(Geometry_,Options_,_Result.Diagnostics,&_SourceShape);
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 2
        _Result.Diagnostics.push_back("Material Boolean intersections use one worker to avoid MSVC checked-iterator global-lock contention; independent face stages retain CAD Task parallelism");
#endif
        std::unordered_set<std::string> rejected;
        struct SCellClassification { bool HasLine, IsMaterial; std::string Reason; };
        std::map<std::vector<std::size_t>,SCellClassification> cellClassifications;
        for (;;) {
            PruneDegreeOneEdges(_Nodes,_GraphEdges);
            PruneBridgeEdges(_Nodes,_GraphEdges);
            BuildHalfEdges(_GraphEdges,_Nodes,_HalfEdges);
            auto candidates=CollectFaces(_HalfEdges,Options_,_Nodes,_GraphEdges,false);
            if(!candidates.empty()&&material.empty()) {
                _Result.Diagnostics.push_back("Cannot classify closed regions as cavities: no usable source material-face region evidence");
                return _Result;
            }
            std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
                return std::abs(a.dSignedArea)>std::abs(b.dSignedArea);
            });
            std::optional<std::size_t> accepted;
            std::size_t candidateIndex=0;
            for(auto& candidate:candidates) {
                const auto index=candidateIndex++;
                std::string reason;
                auto key=candidate.GraphEdgeIndices;std::sort(key.begin(),key.end());
                auto known=cellClassifications.find(key);
                if(known==cellClassifications.end()) {
                    const bool hasLine=CandidateInteriorHasLine(candidate,_GraphEdges,
                        Options_.dConnectionTolerance,reason);
                    const bool isMaterial=!hasLine&&CandidateIsMaterialRegion(candidate,material,
                        Options_.dConnectionTolerance,reason,Options_.Progress,Options_.nMaximumConcurrency);
                    known=cellClassifications.emplace(std::move(key),SCellClassification{hasLine,isMaterial,reason}).first;
                }
                // Interior tests deliberately include inactive edges as well;
                // their geometry and source material do not change on rebuild.
                const auto [hasLine,isMaterial,cachedReason]=known->second;reason=cachedReason;
                if(hasLine||isMaterial) {
                    if(Options_.CandidateAudit)Options_.CandidateAudit(
                        isMaterial?"material":"interior-source-edge",candidate.Edges);
                    const auto message=std::string(isMaterial?"Rejected material region as cavity: ":"Rejected non-minimal inner loop: ")
                        + "area="+std::to_string(candidate.dSignedArea)+"; "+reason;
                    if(rejected.insert(message).second)_Result.Diagnostics.push_back(message);
                    continue;
                }
                // Acceptance, not merely closure, consumes the undirected
                // edges. Rebuild the half-edge graph before finding another
                // cavity; rejected material regions never consume edges.
                accepted=index;
                break; // Remaining results would be discarded on graph rebuild.
            }
            if(!accepted)break;
            auto& candidate=candidates[*accepted];
            for(const auto index:candidate.GraphEdgeIndices)_GraphEdges[index].bActive=false;
            _Loops.push_back(std::move(candidate));
        }
    } catch(const Standard_Failure& error) {
        _Result.Diagnostics.push_back(std::string("Material region classification failed: ")+error.GetMessageString());
        return _Result;
    } catch(const std::exception& error) {
        _Result.Diagnostics.push_back(std::string("Material region classification failed: ")+error.what());
        return _Result;
    }

    if (_Loops.empty())
    {
        _Result.Diagnostics.push_back("No closed contour was found in the planar graph");
        return _Result;
    }
    std::sort(
        _Loops.begin() + 1,
        _Loops.end(),
        [](IN const auto& Left_, IN const auto& Right_) {
            return std::abs(Left_.dSignedArea) > std::abs(Right_.dSignedArea);
        });

    _Result.Wires.reserve(_Loops.size());
    for (std::size_t _IndexLoop = 0; _IndexLoop < _Loops.size(); ++_IndexLoop)
    {
        SSectionWire _Wire;
        _Wire.bInner = _IndexLoop != 0;
        _Wire.bClosed = true;
        _Wire.dSignedArea = _Loops[_IndexLoop].dSignedArea;
        _Wire.Edges = std::move(_Loops[_IndexLoop].Edges);
        // Outer contours are CCW in (Y,Z); holes are CW. The graph traversal
        // itself is deliberately orientation-agnostic.
        if ((_IndexLoop == 0 && _Wire.dSignedArea < 0.0)
            || (_IndexLoop != 0 && _Wire.dSignedArea > 0.0))
        {
            ReverseWire(_Wire);
        }
        SLoopCandidate validation;
        validation.Edges = _Wire.Edges;
        validation.ExactFace=_Loops[_IndexLoop].ExactFace;
        const auto face = CandidateFace(validation, Options_.dConnectionTolerance);
        bool valid = !face.IsNull() && BRepCheck_Analyzer(face).IsValid();
        if (valid) {
            ShapeAnalysis_Wire analysis(BRepTools::OuterWire(face), face, Options_.dConnectionTolerance);
            valid = !analysis.CheckSelfIntersection();
        }
        for (std::size_t i=0;i<_Wire.Edges.size() && valid;++i) {
            const auto& edge=_Wire.Edges[i];
            const auto& next=_Wire.Edges[(i+1)%_Wire.Edges.size()];
            const auto start=EvaluateProjectedCurvePoint(edge,edge.bReversed?1.0:0.0);
            const auto end=EvaluateProjectedCurvePoint(edge,edge.bReversed?0.0:1.0);
            valid=start&&end&&Distance(*start,edge.Start)<=Options_.dConnectionTolerance
                &&Distance(*end,edge.End)<=Options_.dConnectionTolerance
                &&Distance(edge.End,next.Start)<=Options_.dConnectionTolerance;
        }
        if (!valid) {
            _Result.Wires.clear();
            _Result.Diagnostics.push_back("Rejected contour with invalid closure, self intersection, or inconsistent exact endpoints");
            return _Result;
        }
        StartWireAtLowestEdge(_Wire, Options_.dConnectionTolerance);
        _Result.Wires.push_back(std::move(_Wire));
    }
    _Result.bSuccess = true;
    _Result.Diagnostics.push_back(
        "Projected " + std::to_string(_ProjectedEdges.size())
        + " unique edges from " + std::to_string(_Result.SideFaceIds.size())
        + " X-parallel side faces into the YOZ plane");
    _Result.Diagnostics.push_back(
        "Selected enclosing outer loop and edge-disjoint minimal inner loops, excluding projected source material regions");
    _Result.Diagnostics.push_back(
        "Contours were recovered by planar half-edge face traversal");
    return _Result;
}

namespace {
#include "SectionMaterialRecovery.inc"
#include "SectionNominalReference.inc"
}

SSectionWiresResult CExtrudeRecognizesService::ExtractSectionWires(
    const iCAX::GeometryData::BRepModel& geometry,const SSectionWireOptions& options) const
{
    auto axial=ExtractAxialSectionWires(geometry,options);
    if(options.dConnectionTolerance>0&&options.dFaceParallelToleranceRadians>0
        &&options.dFaceParallelToleranceRadians<.5*kPi&&options.nCurveSampleCount>=8
        &&options.dUTurnAngleToleranceRadians>0&&options.dUTurnAngleToleranceRadians<.5*kPi
        &&axial.bSuccess&&HasLocalizedSectionEvidence(geometry,axial,options)) {
        try {
            auto nominal=ExtractCertifiedNominalSection(geometry,options,axial);
            if(nominal.bSuccess)return nominal;
        } catch(const Standard_Failure&) {
            // A station without a complete source-supported perimeter cannot
            // establish a nominal stock reference. The projected evidence
            // below still has to pass all of its own geometric checks.
        } catch(const std::exception& error) {
            if(options.Progress)options.Progress(std::string("Nominal section evidence unavailable: ")+error.what());
        }
    }
    if(axial.bSuccess||axial.SideFaceIds.empty()
        ||options.dFaceParallelToleranceRadians<=0||options.dFaceParallelToleranceRadians>=0.5*kPi
        ||options.dConnectionTolerance<=0||options.nCurveSampleCount<8
        ||options.dUTurnAngleToleranceRadians<=0||options.dUTurnAngleToleranceRadians>=0.5*kPi)return axial;
    try {
        auto recovered=RecoverMaterialSection(geometry,options,axial.SideFaceIds);
        if(recovered.bSuccess) {
            recovered.Diagnostics.insert(recovered.Diagnostics.begin(),axial.Diagnostics.begin(),axial.Diagnostics.end());
            return recovered;
        }
        axial.Diagnostics.insert(axial.Diagnostics.end(),recovered.Diagnostics.begin(),recovered.Diagnostics.end());
    } catch(const Standard_Failure& error) {
        axial.Diagnostics.push_back(std::string("Material projection recovery failed: ")+error.GetMessageString());
    } catch(const std::exception& error) {
        axial.Diagnostics.push_back(std::string("Material projection recovery failed: ")+error.what());
    }
    return axial;
}

namespace direction_detail
{
SSectionWiresResult ExtractCandidateSection(const iCAX::GeometryData::BRepModel& geometry,
    const SSectionWireOptions& options)
{
    // Direction ranking tests the extrusion assumption itself. A complete
    // arbitrary-view projection must not turn a rejected direction into a vote.
    return ExtractAxialSectionWires(geometry,options);
}

bool TryContainingExtrusionVolume(const iCAX::GeometryData::BRepModel& geometry,
    const SSectionWiresResult& section, const SSectionWireOptions& options,
    double& volume, double& uncertainty)
{
    try
    {
        if (!section.bSuccess || section.Wires.empty()) return false;
        const gp_Pln plane(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)));
        TopoDS_Wire outer;
        std::vector<TopoDS_Wire> holes;
        for (const auto& wire : section.Wires)
        {
            BRepBuilderAPI_MakeWire boundary;
            for (const auto& edge : wire.Edges)
            {
                const auto projected = iCAX::OpenCascade::BuildOpenCascadeCurve2(edge.Curve);
                if (projected.IsNull()) return false;
                BRepBuilderAPI_MakeEdge maker(GeomAPI::To3d(projected, plane), edge.Range.First, edge.Range.Last);
                if (!maker.IsDone()) return false;
                auto native = maker.Edge();
                if (edge.bReversed) native.Reverse();
                boundary.Add(native);
            }
            if (!boundary.IsDone()) return false;
            if (wire.bInner) holes.push_back(boundary.Wire());
            else if (!outer.IsNull()) return false;
            else outer = boundary.Wire();
        }
        if (outer.IsNull()) return false;
        BRepBuilderAPI_MakeFace face(plane, outer, true);
        for (const auto& hole : holes) face.Add(hole);
        if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid()) return false;
        const auto source = iCAX::OpenCascade::ReconstructOpenCascadeShape(geometry);
        if (!source.bOK || source.Shape.IsNull()) return false;
        Bnd_Box bounds; BRepBndLib::AddOptimal(source.Shape, bounds, false, false); bounds.SetGap(0);
        if (bounds.IsVoid()) return false;
        double x, y, z, X, Y, Z; bounds.Get(x, y, z, X, Y, Z);
        const double span = X - x, tolerance = options.dConnectionTolerance;
        if (span <= tolerance) return false;
        GProp_GProps area, perimeter, sourceArea, sourceLength;
        BRepGProp::SurfaceProperties(face.Face(), area, 1e-9);
        BRepGProp::LinearProperties(face.Face(), perimeter);
        BRepGProp::SurfaceProperties(source.Shape, sourceArea, 1e-9);
        BRepGProp::LinearProperties(source.Shape, sourceLength);
        volume = std::abs(area.Mass()) * span;
        uncertainty = tolerance * (std::abs(area.Mass()) + perimeter.Mass() * span);
        gp_Trsf pose; pose.SetTranslation(gp_Vec(x - tolerance, 0, 0));
        const auto base = BRepBuilderAPI_Transform(face.Face(), pose, true).Shape();
        const auto envelope = BRepPrimAPI_MakePrism(base, gp_Vec(span + 2 * tolerance, 0, 0)).Shape();
        TopTools_ListOfShape arguments, tools;
        arguments.Append(source.Shape); tools.Append(envelope);
        BRepAlgoAPI_Cut outside;
        outside.SetArguments(arguments); outside.SetTools(tools);
        outside.SetNonDestructive(true); outside.SetFuzzyValue(tolerance); outside.Build();
        if (!outside.IsDone() || outside.Shape().IsNull()) return false;
        GProp_GProps remainder;
        if (TopExp_Explorer(source.Shape, TopAbs_SOLID).More())
        {
            BRepGProp::VolumeProperties(outside.Shape(), remainder);
            return std::abs(remainder.Mass()) <= tolerance * std::abs(sourceArea.Mass());
        }
        BRepGProp::SurfaceProperties(outside.Shape(), remainder, 1e-9);
        return std::abs(remainder.Mass()) <= tolerance * sourceLength.Mass();
    }
    catch (const Standard_Failure&) { return false; }
    catch (const std::exception&) { return false; }
}
}

SSectionFaceClassificationResult CExtrudeRecognizesService::ClassifySectionFaces(
    const iCAX::GeometryData::BRepModel& geometry, const SSectionWiresResult& section,
    const SFeatureToolpathOptions& options) const
{
    return toolpath_detail::ClassifySectionFaces(geometry, section, options);
}

SFeatureToolpathResult CExtrudeRecognizesService::AnalyzeFeatureToolpaths(
    IN const iCAX::GeometryData::BRepModel& Geometry_,
    IN const SSectionWiresResult& Section_,
    IN const SFeatureToolpathOptions& Options_) const
{
    return toolpath_detail::AnalyzeFeatureToolpaths(Geometry_, Section_, Options_);
}
}

namespace iCAX::ExtrusionRecognition::toolpath_detail
{
namespace
{
    #include "SectionZeroAreaTrimFace.inc"

    std::string ZeroTrimNumber(double value)
    {
        std::ostringstream text;text<<std::setprecision(17)<<value;return text.str();
    }

    constexpr double kEpsilon = 1.0e-12;
    constexpr double kHalfPi = 1.57079632679489661923;

    using Point2 = iCAX::GeometryData::Point2;
    using Direction3 = iCAX::GeometryData::Direction3;

    iCAX::GeometryData::Point3 ToPoint3(IN const gp_Pnt& Point_)
    {
        return { Point_.X(), Point_.Y(), Point_.Z() };
    }

    bool TryDirection(IN const gp_Vec& Vector_, OUT Direction3& Direction_)
    {
        const auto _Length = Vector_.Magnitude();
        if (_Length <= kEpsilon)
        {
            return false;
        }
        Direction_ = {
            Vector_.X() / _Length,
            Vector_.Y() / _Length,
            Vector_.Z() / _Length };
        return true;
    }

    gp_Vec ToVec(IN const Direction3& Direction_)
    {
        return { Direction_.X, Direction_.Y, Direction_.Z };
    }

    gp_Vec ToVec(IN const gp_Dir& Direction_)
    {
        return { Direction_.X(), Direction_.Y(), Direction_.Z() };
    }

    bool IsFinite(IN const double Value_)
    {
        return std::isfinite(Value_)
            && !Precision::IsInfinite(Value_);
    }

    double Distance2(IN const Point2& A_, IN const Point2& B_)
    {
        return std::hypot(A_.X - B_.X, A_.Y - B_.Y);
    }

    double DistancePointSegment2(
        IN const Point2& Point_,
        IN const Point2& Start_,
        IN const Point2& End_)
    {
        const auto _DX = End_.X - Start_.X;
        const auto _DY = End_.Y - Start_.Y;
        const auto _Length2 = _DX * _DX + _DY * _DY;
        if (_Length2 <= kEpsilon)
        {
            return Distance2(Point_, Start_);
        }
        const auto _T = std::clamp(
            ((Point_.X - Start_.X) * _DX
                + (Point_.Y - Start_.Y) * _DY) / _Length2,
            0.0,
            1.0);
        return Distance2(
            Point_,
            { Start_.X + _T * _DX, Start_.Y + _T * _DY });
    }

    struct SSectionSupport final
    {
        struct STrimmedCurve final
        {
            Handle(Geom2d_Curve) Curve;
            double First = 0.0;
            double Last = 0.0;
        };
        std::vector<std::pair<Point2, Point2>> Segments;
        std::vector<STrimmedCurve> Curves;
        TopoDS_Face MaterialSection;
    };

    SSectionSupport BuildSectionSupport(IN const SSectionWiresResult& Section_)
    {
        SSectionSupport _Result;
        for (const auto& _Wire : Section_.Wires)
        {
            for (const auto& _Edge : _Wire.Edges)
            {
                if (IsStraightProjectedEdge(_Edge))
                {
                    _Result.Segments.emplace_back(_Edge.Start, _Edge.End);
                    continue;
                }
                const auto _Curve = iCAX::OpenCascade::BuildOpenCascadeCurve2(_Edge.Curve);
                if (!_Curve.IsNull())
                {
                    _Result.Curves.push_back({ _Curve,
                        std::min(_Edge.Range.First, _Edge.Range.Last),
                        std::max(_Edge.Range.First, _Edge.Range.Last) });
                }
            }
        }
        // Keep the exact fixed section for proving covered internal portions
        // of a contributing stock wall. No source faces are sewn or fused.
        try {
            const gp_Pln plane(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0)));
            TopoDS_Wire outer;std::vector<TopoDS_Wire> holes;
            for(const auto& wire:Section_.Wires){
                BRepBuilderAPI_MakeWire boundary;
                double joinTolerance=Precision::Confusion();
                for(std::size_t i=0;i<wire.Edges.size();++i){
                    const auto& a=wire.Edges[i];const auto& b=wire.Edges[(i+1)%wire.Edges.size()];
                    const auto ac=iCAX::OpenCascade::BuildOpenCascadeCurve2(a.Curve);
                    const auto bc=iCAX::OpenCascade::BuildOpenCascadeCurve2(b.Curve);
                    const auto end=ac->Value(a.bReversed?a.Range.First:a.Range.Last);
                    const auto start=bc->Value(b.bReversed?b.Range.Last:b.Range.First);
                    joinTolerance=std::max(joinTolerance,end.Distance(start)+Precision::Confusion());
                }
                for(const auto& edge:wire.Edges){
                    const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(edge.Curve);
                    BRepBuilderAPI_MakeEdge maker(GeomAPI::To3d(curve,plane),edge.Range.First,edge.Range.Last);
                    if(!maker.IsDone())return _Result;
                    auto native=maker.Edge();BRep_Builder builder;
                    for(TopExp_Explorer v(native,TopAbs_VERTEX);v.More();v.Next())
                        builder.UpdateVertex(TopoDS::Vertex(v.Current()),joinTolerance);
                    if(edge.bReversed)native.Reverse();boundary.Add(native);
                }
                if(!boundary.IsDone())return _Result;
                if(wire.bInner)holes.push_back(boundary.Wire());else outer=boundary.Wire();
            }
            if(!outer.IsNull()){
                BRepBuilderAPI_MakeFace face(plane,outer,true);for(const auto& hole:holes)face.Add(hole);
                if(face.IsDone()&&BRepCheck_Analyzer(face.Face()).IsValid())_Result.MaterialSection=face.Face();
            }
        } catch(const Standard_Failure&) {}
        return _Result;
    }

    bool IsOnSkin(
        IN const gp_Pnt& Point_,
        IN const SSectionSupport& Support_,
        IN const double Tolerance_)
    {
        const Point2 _Projected{ Point_.Y(), Point_.Z() };
        if (std::any_of(Support_.Segments.begin(), Support_.Segments.end(),
            [&](const auto& _Segment) {
                return DistancePointSegment2(_Projected, _Segment.first, _Segment.second)
                    <= Tolerance_;
            }))
        {
            return true;
        }
        // Display chords have an independent approximation error. Use the
        // exact trimmed contour so original longitudinal fillets do not
        // become machining faces merely because they lie between samples.
        const gp_Pnt2d _Point(Point_.Y(), Point_.Z());
        for (const auto& _Support : Support_.Curves)
        {
            if (_Point.Distance(_Support.Curve->Value(_Support.First)) <= Tolerance_
                || _Point.Distance(_Support.Curve->Value(_Support.Last)) <= Tolerance_)
            {
                return true;
            }
            Geom2dAPI_ProjectPointOnCurve _Projection(
                _Point, _Support.Curve, _Support.First, _Support.Last);
            if (_Projection.NbPoints() > 0 && _Projection.LowerDistance() <= Tolerance_)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<gp_Pnt> SampleEdge(
        IN const TopoDS_Edge& Edge_,
        IN const std::size_t Count_,
        IN const double Tolerance_,
        IN const TopoDS_Face& Face_ = TopoDS_Face())
    {
        std::vector<gp_Pnt> _Result;
        if (Edge_.IsNull() || BRep_Tool::Degenerated(Edge_))
        {
            return _Result;
        }

        BRepAdaptor_Curve _Curve;
        if (Face_.IsNull()) _Curve.Initialize(Edge_);
        // When classifying a face, use its trimmed surface boundary. The
        // separately stored 3D edge can have a much looser import tolerance;
        // that approximation is not a physical offset of the side surface.
        else _Curve.Initialize(Edge_, Face_);
        const auto _Count = static_cast<int>(std::max<std::size_t>(2, Count_));
        const auto _First = _Curve.FirstParameter();
        const auto _Last = _Curve.LastParameter();
        if (!IsFinite(_First) || !IsFinite(_Last)
            || std::abs(_Last - _First) <= kEpsilon)
        {
            const auto _FirstVertex = TopExp::FirstVertex(Edge_, true);
            const auto _LastVertex = TopExp::LastVertex(Edge_, true);
            if (!_FirstVertex.IsNull())
            {
                _Result.push_back(BRep_Tool::Pnt(_FirstVertex));
            }
            if (!_LastVertex.IsNull()
                && (_Result.empty()
                    || _Result.back().Distance(BRep_Tool::Pnt(_LastVertex))
                        > Tolerance_))
            {
                _Result.push_back(BRep_Tool::Pnt(_LastVertex));
            }
            return _Result;
        }

        GCPnts_UniformAbscissa _Abscissa(_Curve, _Count, Tolerance_);
        if (_Abscissa.IsDone() && _Abscissa.NbPoints() == _Count)
        {
            _Result.reserve(static_cast<std::size_t>(_Count));
            for (int _Index = 1; _Index <= _Count; ++_Index)
            {
                _Result.push_back(_Curve.Value(_Abscissa.Parameter(_Index)));
            }
            return _Result;
        }

        _Result.reserve(static_cast<std::size_t>(_Count));
        for (int _Index = 0; _Index < _Count; ++_Index)
        {
            const auto _U = static_cast<double>(_Index)
                / static_cast<double>(_Count - 1);
            _Result.push_back(_Curve.Value(
                _First + (_Last - _First) * _U));
        }
        return _Result;
    }

    bool IsGeometricallyStraight(
        IN const std::vector<gp_Pnt>& Samples_,
        IN const double Tolerance_)
    {
        if (Samples_.size() < 2)
        {
            return false;
        }
        const gp_Vec _Chord(Samples_.front(), Samples_.back());
        const auto _Length = _Chord.Magnitude();
        if (_Length <= Tolerance_)
        {
            return false;
        }
        return std::all_of(
            Samples_.begin() + 1,
            Samples_.end() - 1,
            [&](IN const auto& _Point) {
                return _Chord.Crossed(gp_Vec(Samples_.front(), _Point))
                    .Magnitude() / _Length <= Tolerance_;
            });
    }

    bool TryFaceNormal(
        IN const TopoDS_Face& Face_,
        OUT gp_Dir& Normal_)
    {
        try
        {
            BRepAdaptor_Surface _Surface(Face_, true);
            double _U0 = 0.0;
            double _U1 = 0.0;
            double _V0 = 0.0;
            double _V1 = 0.0;
            BRepTools::UVBounds(Face_, _U0, _U1, _V0, _V1);
            if (!IsFinite(_U0) || !IsFinite(_U1))
            {
                _U0 = _Surface.FirstUParameter();
                _U1 = _Surface.LastUParameter();
            }
            if (!IsFinite(_V0) || !IsFinite(_V1))
            {
                _V0 = _Surface.FirstVParameter();
                _V1 = _Surface.LastVParameter();
            }
            if (!IsFinite(_U0) || !IsFinite(_U1)
                || !IsFinite(_V0) || !IsFinite(_V1))
            {
                return false;
            }
            gp_Pnt _Point;
            gp_Vec _DU;
            gp_Vec _DV;
            _Surface.D1(
                0.5 * (_U0 + _U1),
                0.5 * (_V0 + _V1),
                _Point,
                _DU,
                _DV);
            auto _Normal = _DU.Crossed(_DV);
            if (_Normal.SquareMagnitude() <= kEpsilon * kEpsilon)
            {
                return false;
            }
            if (Face_.Orientation() == TopAbs_REVERSED)
            {
                _Normal.Reverse();
            }
            Normal_ = gp_Dir(_Normal);
            return true;
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
    }

    int FindSameIndex(
        IN const TopTools_IndexedMapOfShape& Map_,
        IN const TopoDS_Shape& Shape_)
    {
        const auto _Index = Map_.FindIndex(Shape_);
        if (_Index > 0)
        {
            return _Index;
        }
        for (int _Candidate = 1;
            _Candidate <= Map_.Extent();
            ++_Candidate)
        {
            if (Map_(_Candidate).IsSame(Shape_))
            {
                return _Candidate;
            }
        }
        return 0;
    }

    struct SEdgeContext final
    {
        TopoDS_Edge Edge;
        std::vector<gp_Pnt> Samples;
        bool bOnSkin = false;
        bool bCanIn = false;
        bool bStartOnSkin = false;
        bool bEndOnSkin = false;
        bool bStraight = false;
        bool bFree = true;
        bool bConvex = false;
        bool bConcave = false;
        std::vector<int> FaceIndices;
    };

    struct SFaceContext final
    {
        TopoDS_Face Face;
        std::vector<int> EdgeIndices;
        gp_Dir Normal = gp_Dir(0.0, 0.0, 1.0);
        bool bHasNormal = false;
        bool bSkinFace = false;
    };

    // A SSeg is a logical machining segment, not a new OCC shape.  It keeps
    // the ordered source edges so that several fragmented TopoDS_Edge objects
    // can be handled as one continuous boundary segment without losing their
    // individual edge ids or edge-level properties.
    struct SSeg final
    {
        std::vector<int> EdgeIndices;
        std::vector<TopoDS_Edge> Edges;
        std::vector<gp_Pnt> Samples;
        bool bOnSkin = false;
        bool bCanIn = false;
        bool bStartOnSkin = false;
        bool bEndOnSkin = false;
        bool bGeometricallyStraight = false;
        bool bFree = true;
        bool bConvex = false;
        bool bConcave = false;
        bool bClosed = false;
    };

    using SSegArray = std::vector<SSeg>;

    enum class ESegmentSemantic : std::uint8_t
    {
        Unclassified = 0,
        Skin,
        Entry
    };

    ESegmentSemantic SegmentSemantic(IN const SEdgeContext& Edge_)
    {
        if (Edge_.bOnSkin)
        {
            return ESegmentSemantic::Skin;
        }
        return Edge_.bCanIn
            ? ESegmentSemantic::Entry
            : ESegmentSemantic::Unclassified;
    }

    ESegmentSemantic SegmentSemantic(IN const SSeg& Segment_)
    {
        if (Segment_.bOnSkin)
        {
            return ESegmentSemantic::Skin;
        }
        return Segment_.bCanIn
            ? ESegmentSemantic::Entry
            : ESegmentSemantic::Unclassified;
    }

    // This is deliberately created only while one face is being analyzed.
    // There is no global SSeg cache: edge-level information remains global,
    // while wire grouping and continuity grouping are face-local.
    struct SFaceSegmentAnalysis final
    {
        std::vector<SSegArray> WireSegments;
    };

    std::optional<gp_Pnt> EdgeEndpoint(
        IN const TopoDS_Edge& Edge_,
        IN const bool Last_)
    {
        try
        {
            const auto _Vertex = Last_
                ? TopExp::LastVertex(Edge_, true)
                : TopExp::FirstVertex(Edge_, true);
            if (!_Vertex.IsNull())
            {
                return BRep_Tool::Pnt(_Vertex);
            }

            BRepAdaptor_Curve _Curve(Edge_);
            const auto _Parameter = Last_
                ? _Curve.LastParameter()
                : _Curve.FirstParameter();
            if (!IsFinite(_Parameter))
            {
                return std::nullopt;
            }
            return _Curve.Value(_Parameter);
        }
        catch (const Standard_Failure&)
        {
            return std::nullopt;
        }
    }

    std::vector<gp_Pnt> SampleOrientedEdge(
        IN const TopoDS_Edge& Edge_,
        IN const std::size_t Count_,
        IN const double Tolerance_)
    {
        auto _Samples = SampleEdge(Edge_, Count_, Tolerance_);
        if (_Samples.size() < 2)
        {
            return _Samples;
        }

        const auto _Start = EdgeEndpoint(Edge_, false);
        const auto _End = EdgeEndpoint(Edge_, true);
        if (_Start && _End)
        {
            // SampleEdge normally already follows the edge orientation.  The
            // explicit check makes this true even for an edge returned from a
            // rebuilt wire whose orientation differs from the indexed edge.
            const auto _ForwardError = _Samples.front().Distance(*_Start)
                + _Samples.back().Distance(*_End);
            const auto _ReverseError = _Samples.front().Distance(*_End)
                + _Samples.back().Distance(*_Start);
            if (_ReverseError + Tolerance_ < _ForwardError)
            {
                std::reverse(_Samples.begin(), _Samples.end());
            }
        }
        return _Samples;
    }

    bool TryOrientedEdgeTangent(
        IN const TopoDS_Edge& Edge_,
        IN const bool AtEnd_,
        OUT gp_Vec& Tangent_)
    {
        try
        {
            BRepAdaptor_Curve _Curve(Edge_);
            const auto _First = _Curve.FirstParameter();
            const auto _Last = _Curve.LastParameter();
            if (!IsFinite(_First) || !IsFinite(_Last)
                || std::abs(_Last - _First) <= kEpsilon)
            {
                return false;
            }

            const auto _Parameter = AtEnd_ ? _Last : _First;
            gp_Pnt _Point;
            gp_Vec _Derivative;
            _Curve.D1(_Parameter, _Point, _Derivative);
            if (_Derivative.SquareMagnitude() <= kEpsilon * kEpsilon)
            {
                return false;
            }

            const auto _Expected = EdgeEndpoint(Edge_, AtEnd_);
            const auto _Opposite = EdgeEndpoint(Edge_, !AtEnd_);
            if (_Expected && _Opposite
                && _Point.Distance(*_Expected) > _Point.Distance(*_Opposite))
            {
                _Derivative.Reverse();
            }
            Tangent_ = _Derivative;
            return true;
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
    }

    bool IsContinuousJoin(
        IN const TopoDS_Edge& Previous_,
        IN const TopoDS_Edge& Next_,
        IN const SFeatureToolpathOptions& Options_)
    {
        const auto _PreviousEnd = EdgeEndpoint(Previous_, true);
        const auto _NextStart = EdgeEndpoint(Next_, false);
        if (!_PreviousEnd || !_NextStart
            || _PreviousEnd->Distance(*_NextStart)
                > Options_.dLinearTolerance)
        {
            return false;
        }

        gp_Vec _PreviousTangent;
        gp_Vec _NextTangent;
        if (!TryOrientedEdgeTangent(
                Previous_,
                true,
                _PreviousTangent)
            || !TryOrientedEdgeTangent(
                Next_,
                false,
                _NextTangent))
        {
            return false;
        }

        _PreviousTangent.Normalize();
        _NextTangent.Normalize();
        return _PreviousTangent.Dot(_NextTangent)
            >= std::cos(Options_.dAngularToleranceRadians);
    }

    bool IsEndpointJoin(
        IN const TopoDS_Edge& Previous_,
        IN const TopoDS_Edge& Next_,
        IN const double Tolerance_)
    {
        const auto _PreviousEnd = EdgeEndpoint(Previous_, true);
        const auto _NextStart = EdgeEndpoint(Next_, false);
        return _PreviousEnd && _NextStart
            && _PreviousEnd->Distance(*_NextStart) <= Tolerance_;
    }

    void AppendSamples(
        IN const std::vector<gp_Pnt>& Samples_,
        IN const double Tolerance_,
        IN OUT std::vector<gp_Pnt>& Target_)
    {
        for (const auto& _Point : Samples_)
        {
            if (!Target_.empty()
                && Target_.back().Distance(_Point) <= Tolerance_)
            {
                continue;
            }
            Target_.push_back(_Point);
        }
    }

    void AppendEdgeToSegment(
        IN const int EdgeIndex_,
        IN const TopoDS_Edge& OrientedEdge_,
        IN const std::vector<SEdgeContext>& EdgeContexts_,
        IN const SSectionSupport& Support_,
        IN const SFeatureToolpathOptions& Options_,
        IN OUT SSeg& Segment_)
    {
        const auto& _Edge = EdgeContexts_[static_cast<std::size_t>(EdgeIndex_)];
        if (Segment_.EdgeIndices.empty())
        {
            Segment_.bOnSkin = _Edge.bOnSkin;
            Segment_.bCanIn = _Edge.bCanIn;
            Segment_.bFree = _Edge.bFree;
            Segment_.bConvex = _Edge.bConvex;
            Segment_.bConcave = _Edge.bConcave;
            Segment_.bGeometricallyStraight = _Edge.bStraight;
        }
        else
        {
            // All edges in a segment have already passed the semantic run
            // boundary.  Keep this invariant explicit here as a guard against
            // future callers accidentally appending a mixed edge.
            if (SegmentSemantic(Segment_) != SegmentSemantic(_Edge))
            {
                return;
            }
            Segment_.bFree = Segment_.bFree && _Edge.bFree;
            Segment_.bCanIn = Segment_.bCanIn && _Edge.bCanIn;
            Segment_.bConvex = Segment_.bConvex && _Edge.bConvex;
            Segment_.bConcave = Segment_.bConcave || _Edge.bConcave;
            Segment_.bGeometricallyStraight =
                Segment_.bGeometricallyStraight && _Edge.bStraight;
        }

        Segment_.EdgeIndices.push_back(EdgeIndex_);
        Segment_.Edges.push_back(OrientedEdge_);
        const auto _Samples = SampleOrientedEdge(
            OrientedEdge_,
            Options_.nCurveClassificationSampleCount,
            Options_.dLinearTolerance);
        AppendSamples(
            _Samples,
            Options_.dLinearTolerance,
            Segment_.Samples);

        if (!Segment_.Samples.empty())
        {
            Segment_.bStartOnSkin = IsOnSkin(
                Segment_.Samples.front(),
                Support_,
                Options_.dSkinDistanceTolerance);
            Segment_.bEndOnSkin = IsOnSkin(
                Segment_.Samples.back(),
                Support_,
                Options_.dSkinDistanceTolerance);
            Segment_.bClosed = Segment_.Samples.size() > 2
                && Segment_.Samples.front().Distance(Segment_.Samples.back())
                    <= Options_.dLinearTolerance;
        }
    }

    std::vector<TopoDS_Wire> ConnectFaceWires(
        IN const TopoDS_Face& Face_,
        IN const double Tolerance_)
    {
        occ::handle<NCollection_HSequence<TopoDS_Shape>> _Edges =
            new NCollection_HSequence<TopoDS_Shape>();
        TopTools_IndexedMapOfShape _UniqueEdges;
        for (TopExp_Explorer _Explorer(Face_, TopAbs_EDGE);
            _Explorer.More();
            _Explorer.Next())
        {
            if (_UniqueEdges.FindIndex(_Explorer.Current()) == 0)
            {
                _UniqueEdges.Add(_Explorer.Current());
                _Edges->Append(_Explorer.Current());
            }
        }
        if (_Edges->IsEmpty())
        {
            return {};
        }

        // shared=true is intentional.  Face topology already contains the
        // authoritative vertex connectivity; using proximity here could
        // incorrectly join two nearby holes.
        const auto _Connected = ShapeAnalysis_FreeBounds::ConnectEdgesToWires(
            _Edges,
            Tolerance_,
            true);
        if (_Connected.IsNull() || _Connected->IsEmpty())
        {
            return {};
        }

        std::vector<TopoDS_Wire> _Result;
        _Result.reserve(static_cast<std::size_t>(_Connected->Length()));
        for (int _Index = 1; _Index <= _Connected->Length(); ++_Index)
        {
            const auto& _Shape = _Connected->Value(_Index);
            if (_Shape.ShapeType() == TopAbs_WIRE)
            {
                _Result.push_back(TopoDS::Wire(_Shape));
            }
        }
        return _Result;
    }

    std::vector<std::pair<int, TopoDS_Edge>> OrderedWireEdges(
        IN const TopoDS_Wire& Wire_,
        IN const TopoDS_Face& Face_,
        IN const TopTools_IndexedMapOfShape& EdgeMap_)
    {
        std::vector<std::pair<int, TopoDS_Edge>> _Result;
        std::unordered_set<int> _Seen;
        for (BRepTools_WireExplorer _Explorer(Wire_, Face_);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Edge = TopoDS::Edge(_Explorer.Current());
            const auto _Index = FindSameIndex(EdgeMap_, _Edge);
            if (_Index > 0 && _Seen.insert(_Index).second)
            {
                _Result.emplace_back(_Index, _Edge);
            }
        }

        // A malformed wire can make WireExplorer stop early.  Do not lose
        // topology in that case: append the missing members in their face
        // order.  The normal, valid-wire path never enters this fallback.
        TopTools_IndexedMapOfShape _WireEdges;
        TopExp::MapShapes(Wire_, TopAbs_EDGE, _WireEdges);
        if (static_cast<int>(_Result.size()) < _WireEdges.Extent())
        {
            for (TopExp_Explorer _Explorer(Wire_, TopAbs_EDGE);
                _Explorer.More();
                _Explorer.Next())
            {
                const auto _Edge = TopoDS::Edge(_Explorer.Current());
                const auto _Index = FindSameIndex(EdgeMap_, _Edge);
                if (_Index > 0 && _Seen.insert(_Index).second)
                {
                    _Result.emplace_back(_Index, _Edge);
                }
            }
        }
        return _Result;
    }

    SFaceSegmentAnalysis AnalyzeFaceSegments(
        IN const TopoDS_Face& Face_,
        IN const TopTools_IndexedMapOfShape& EdgeMap_,
        IN const std::vector<SEdgeContext>& EdgeContexts_,
        IN const SSectionSupport& Support_,
        IN const SFeatureToolpathOptions& Options_)
    {
        SFaceSegmentAnalysis _Result;
        const auto _Wires = ConnectFaceWires(
            Face_,
            Options_.dLinearTolerance);
        _Result.WireSegments.reserve(_Wires.size());

        for (const auto& _Wire : _Wires)
        {
            const auto _OrderedEdges = OrderedWireEdges(
                _Wire,
                Face_,
                EdgeMap_);
            if (_OrderedEdges.empty())
            {
                continue;
            }

            SSegArray _Segments;
            for (const auto& _OrderedEdge : _OrderedEdges)
            {
                const auto _EdgeIndex = _OrderedEdge.first;
                const auto& _Edge = EdgeContexts_[
                    static_cast<std::size_t>(_EdgeIndex)];
                // The semantic marker is the same for every face: skin has
                // priority over a propagated entry marker, then canIn marks
                // an entry run on an inner face.  Unclassified runs are split
                // by geometric continuity only.
                const auto _SameSemanticRun = !_Segments.empty()
                    && SegmentSemantic(_Segments.back())
                        == SegmentSemantic(_Edge);
                const auto _SemanticIsMarked =
                    SegmentSemantic(_Edge) != ESegmentSemantic::Unclassified;
                const auto _Continuous = _SameSemanticRun
                    && (_SemanticIsMarked
                        || (!_Segments.back().Edges.empty()
                            && IsContinuousJoin(
                                _Segments.back().Edges.back(),
                                _OrderedEdge.second,
                                Options_)));
                if (_Segments.empty()
                    || !_SameSemanticRun
                    || !_Continuous)
                {
                    _Segments.emplace_back();
                }
                AppendEdgeToSegment(
                    _EdgeIndex,
                    _OrderedEdge.second,
                    EdgeContexts_,
                    Support_,
                    Options_,
                    _Segments.back());
            }

            // A valid face wire is expected to be closed.  For a closed wire,
            // merge the seam pair only when both sides have the same skin
            // state and the tangent is continuous.  This also handles a
            // fragmented periodic curve without joining across an on-skin
            // state transition.
            if (_Segments.size() > 1
                && !_Segments.front().EdgeIndices.empty()
                && !_Segments.back().EdgeIndices.empty()
                && SegmentSemantic(_Segments.front())
                    == SegmentSemantic(_Segments.back())
                && (SegmentSemantic(_Segments.front())
                    != ESegmentSemantic::Unclassified
                    ? IsEndpointJoin(
                        _Segments.back().Edges.back(),
                        _Segments.front().Edges.front(),
                        Options_.dLinearTolerance)
                    : IsContinuousJoin(
                        _Segments.back().Edges.back(),
                        _Segments.front().Edges.front(),
                        Options_)))
            {
                const auto _BackStartOnSkin = _Segments.back().bStartOnSkin;
                SSeg _Merged = std::move(_Segments.back());
                _Segments.pop_back();
                _Merged.EdgeIndices.insert(
                    _Merged.EdgeIndices.end(),
                    _Segments.front().EdgeIndices.begin(),
                    _Segments.front().EdgeIndices.end());
                _Merged.Edges.insert(
                    _Merged.Edges.end(),
                    _Segments.front().Edges.begin(),
                    _Segments.front().Edges.end());
                AppendSamples(
                    _Segments.front().Samples,
                    Options_.dLinearTolerance,
                    _Merged.Samples);
                // The merged order is [old back, old front].
                _Merged.bStartOnSkin = _BackStartOnSkin;
                _Merged.bEndOnSkin = _Segments.front().bEndOnSkin;
                _Merged.bFree = _Merged.bFree && _Segments.front().bFree;
                _Merged.bConvex = _Merged.bConvex && _Segments.front().bConvex;
                _Merged.bConcave = _Merged.bConcave || _Segments.front().bConcave;
                _Merged.bGeometricallyStraight =
                    _Merged.bGeometricallyStraight
                    && _Segments.front().bGeometricallyStraight;
                _Merged.bCanIn = _Merged.bCanIn
                    && _Segments.front().bCanIn;
                _Merged.bClosed = true;
                _Segments.erase(_Segments.begin());
                _Segments.insert(
                    _Segments.begin(),
                    std::move(_Merged));
            }

            if (!_Segments.empty())
            {
                _Result.WireSegments.push_back(std::move(_Segments));
            }
        }
        return _Result;
    }

    const SSeg* FindSegmentContainingEdge(
        IN const SFaceSegmentAnalysis& Analysis_,
        IN const int EdgeIndex_)
    {
        for (const auto& _Segments : Analysis_.WireSegments)
        {
            for (const auto& _Segment : _Segments)
            {
                if (std::find(
                        _Segment.EdgeIndices.begin(),
                        _Segment.EdgeIndices.end(),
                        EdgeIndex_) != _Segment.EdgeIndices.end())
                {
                    return &_Segment;
                }
            }
        }
        return nullptr;
    }

    std::vector<const SSeg*> CollectInnerSegments(
        IN const SFaceSegmentAnalysis& Analysis_,
        IN const SSeg* Excluded_ = nullptr)
    {
        std::vector<const SSeg*> _Result;
        for (const auto& _Segments : Analysis_.WireSegments)
        {
            for (const auto& _Segment : _Segments)
            {
                if (&_Segment == Excluded_
                    || _Segment.bOnSkin
                    || _Segment.bStartOnSkin
                    || _Segment.bEndOnSkin)
                {
                    continue;
                }
                _Result.push_back(&_Segment);
            }
        }
        return _Result;
    }

    gp_Vec AverageBoundaryCenter(
        IN const SFaceContext& Face_,
        IN const std::vector<SEdgeContext>& Edges_)
    {
        gp_Vec _Sum(0.0, 0.0, 0.0);
        std::size_t _Count = 0;
        for (const auto _EdgeIndex : Face_.EdgeIndices)
        {
            if (_EdgeIndex <= 0
                || static_cast<std::size_t>(_EdgeIndex) >= Edges_.size())
            {
                continue;
            }
            for (const auto& _Point : Edges_[static_cast<std::size_t>(_EdgeIndex)].Samples)
            {
                _Sum += gp_Vec(gp_Pnt(0.0, 0.0, 0.0), _Point);
                ++_Count;
            }
        }
        return _Count == 0
            ? _Sum
            : _Sum / static_cast<double>(_Count);
    }

    std::optional<gp_Vec> FaceRulingAt(
        IN const SFaceContext& Face_,
        IN const gp_Pnt& Point_,
        IN const gp_Vec& Feed_,
        IN const gp_Vec& Center_)
    {
        try
        {
            BRepAdaptor_Surface _Surface(Face_.Face, true);
            gp_Vec _Ruling;
            switch (_Surface.GetType())
            {
            case GeomAbs_Cylinder:
                _Ruling = gp_Vec(_Surface.Cylinder().Axis().Direction());
                break;
            case GeomAbs_SurfaceOfExtrusion:
                _Ruling = gp_Vec(_Surface.Direction());
                break;
            case GeomAbs_Cone:
                _Ruling = gp_Vec(Point_, _Surface.Cone().Apex());
                break;
            case GeomAbs_Plane:
                if (!Face_.bHasNormal)
                {
                    return std::nullopt;
                }
                _Ruling = ToVec(Face_.Normal).Crossed(Feed_);
                break;
            case GeomAbs_BSplineSurface:
            case GeomAbs_BezierSurface:
            {
                const auto _SurfaceHandle = BRep_Tool::Surface(Face_.Face);
                if (_SurfaceHandle.IsNull())
                {
                    return std::nullopt;
                }
                GeomAPI_ProjectPointOnSurf _Projector(Point_, _SurfaceHandle);
                if (_Projector.NbPoints() < 1)
                {
                    return std::nullopt;
                }
                double _U = 0.0;
                double _V = 0.0;
                _Projector.LowerDistanceParameters(_U, _V);
                gp_Pnt _Evaluated;
                gp_Vec _DU;
                gp_Vec _DV;
                _Surface.D1(_U, _V, _Evaluated, _DU, _DV);
                _Ruling = _DU.Crossed(Feed_).SquareMagnitude()
                    >= _DV.Crossed(Feed_).SquareMagnitude()
                    ? _DU
                    : _DV;
                break;
            }
            default:
                if (!Face_.bHasNormal)
                {
                    return std::nullopt;
                }
                _Ruling = ToVec(Face_.Normal).Crossed(Feed_);
                break;
            }

            if (_Ruling.SquareMagnitude() <= kEpsilon * kEpsilon)
            {
                return std::nullopt;
            }

            const gp_Pnt _CenterPoint(Center_.X(), Center_.Y(), Center_.Z());
            if (_Ruling.Dot(gp_Vec(Point_, _CenterPoint)) < 0.0)
            {
                _Ruling.Reverse();
            }
            return _Ruling;
        }
        catch (const Standard_Failure&)
        {
            return std::nullopt;
        }
    }

    std::optional<gp_Vec> EstimateFeed(
        IN const std::vector<gp_Pnt>& Samples_,
        IN const std::size_t Index_)
    {
        if (Samples_.size() < 2)
        {
            return std::nullopt;
        }
        if (Index_ == 0)
        {
            return gp_Vec(Samples_[0], Samples_[1]);
        }
        if (Index_ + 1 >= Samples_.size())
        {
            return gp_Vec(Samples_[Samples_.size() - 2], Samples_.back());
        }
        return gp_Vec(Samples_[Index_ - 1], Samples_[Index_ + 1]);
    }

    EFeatureEdgeRole ClassifyEdge(IN const SEdgeContext& Edge_)
    {
        if (Edge_.bOnSkin)
        {
            return EFeatureEdgeRole::Skin;
        }
        if (Edge_.bStartOnSkin || Edge_.bEndOnSkin)
        {
            return EFeatureEdgeRole::Side;
        }
        return Edge_.bFree
            ? EFeatureEdgeRole::Free
            : EFeatureEdgeRole::Inner;
    }

    EFeatureEdgeRole ClassifySegment(IN const SSeg& Segment_)
    {
        if (Segment_.bOnSkin)
        {
            return EFeatureEdgeRole::Skin;
        }
        if (Segment_.bStartOnSkin || Segment_.bEndOnSkin)
        {
            return EFeatureEdgeRole::Side;
        }
        return Segment_.bFree
            ? EFeatureEdgeRole::Free
            : EFeatureEdgeRole::Inner;
    }

    bool IsValidOptions(IN const SFeatureToolpathOptions& Options_)
    {
        return Options_.dLinearTolerance > 0.0
            && Options_.dAngularToleranceRadians > 0.0
            && Options_.dAngularToleranceRadians < kHalfPi
            && Options_.dSkinDistanceTolerance >= Options_.dLinearTolerance
            && Options_.nTrajectorySampleCount >= 2
            && Options_.nCurveClassificationSampleCount >= 3;
    }

    std::vector<Point2> CollectSectionVertices(
        IN const SSectionWiresResult& Section_,
        IN const double Tolerance_)
    {
        std::vector<Point2> _Vertices;
        for (const auto& _Wire : Section_.Wires)
        {
            for (const auto& _Edge : _Wire.Edges)
            {
                const std::array<Point2, 2> _Candidates = {
                    _Edge.Start,
                    _Edge.End };
                for (const auto& _Candidate : _Candidates)
                {
                    const auto _Existing = std::find_if(
                        _Vertices.begin(),
                        _Vertices.end(),
                        [&](IN const auto& _Vertex) {
                            return Distance2(_Vertex, _Candidate) <= Tolerance_;
                        });
                    if (_Existing == _Vertices.end())
                    {
                        _Vertices.push_back(_Candidate);
                    }
                }
            }
        }
        return _Vertices;
    }

    struct SLineParameterSpan final
    {
        double First = 0.0;
        double Last = 0.0;
    };

    double LineParameter(
        IN const gp_Lin& Line_,
        IN const gp_Pnt& Point_)
    {
        return gp_Vec(Line_.Location(), Point_).Dot(
            gp_Vec(Line_.Direction()));
    }

    gp_Pnt LinePoint(
        IN const gp_Lin& Line_,
        IN const double Parameter_)
    {
        return Line_.Location().Translated(
            gp_Vec(Line_.Direction()) * Parameter_);
    }

    void AddLineParameter(
        IN const gp_Lin& Line_,
        IN const gp_Pnt& Point_,
        IN const double LineMin_,
        IN const double LineMax_,
        IN const double Tolerance_,
        IN OUT std::vector<double>& Parameters_)
    {
        if (Line_.Distance(Point_) > Tolerance_)
        {
            return;
        }
        auto _Parameter = LineParameter(Line_, Point_);
        if (!IsFinite(_Parameter)
            || _Parameter < LineMin_ - Tolerance_
            || _Parameter > LineMax_ + Tolerance_)
        {
            return;
        }
        _Parameter = std::clamp(_Parameter, LineMin_, LineMax_);
        Parameters_.push_back(_Parameter);
    }

    bool IsPointOnFace(
        IN const TopoDS_Face& Face_,
        IN const gp_Pnt& Point_,
        IN const double Tolerance_)
    {
        try
        {
            // FaceClassifier's 3D overload classifies the nearest UV
            // projection even when the point is far from the surface.
            // A cut must lie on the surface as well as inside its trim.
            const auto surface = BRep_Tool::Surface(Face_);
            if (surface.IsNull()) return false;
            GeomAPI_ProjectPointOnSurf projection(Point_, surface);
            if (projection.NbPoints() == 0 || projection.LowerDistance() > Tolerance_)
                return false;
            BRepClass_FaceClassifier _Classifier(
                Face_,
                Point_,
                Tolerance_,
                true);
            const auto _State = _Classifier.State();
            return _State == TopAbs_IN || _State == TopAbs_ON;
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
    }

    bool IsLineSegmentOnFace(
        IN const TopoDS_Face& Face_,
        IN const gp_Lin& Line_,
        IN const double First_,
        IN const double Last_,
        IN const SFeatureToolpathOptions& Options_)
    {
        const auto _Tolerance = std::max(
            Options_.dLinearTolerance,
            Options_.dSkinDistanceTolerance);
        const auto _SampleCount = std::max<std::size_t>(
            5,
            Options_.nCurveClassificationSampleCount);
        for (std::size_t _Index = 1;
            _Index < _SampleCount;
            ++_Index)
        {
            const auto _Ratio = static_cast<double>(_Index)
                / static_cast<double>(_SampleCount);
            if (!IsPointOnFace(
                Face_,
                LinePoint(
                    Line_,
                    First_ + (Last_ - First_) * _Ratio),
                _Tolerance))
            {
                return false;
            }
        }
        return true;
    }

    bool IsSplineOnLine(const TopoDS_Edge& edge, const gp_Lin& line,
        double tolerance)
    {
        try
        {
            double first = 0.0, last = 0.0;
            auto curve = BRep_Tool::Curve(edge, first, last);
            while (const auto trimmed = Handle(Geom_TrimmedCurve)::DownCast(curve))
                curve = trimmed->BasisCurve();
            const auto spline = Handle(Geom_BSplineCurve)::DownCast(curve);
            if (spline.IsNull() || !IsFinite(first) || !IsFinite(last) || last <= first)
                return false;
            auto segment = Handle(Geom_BSplineCurve)::DownCast(spline->Copy());
            segment->Segment(first, last);
            // Positive rational B-spline weights give the convex-hull property.
            // The trimmed curve stays in the tolerance tube around this line;
            // continuity covers the projected interval between its endpoints.
            for (int i = 1; i <= segment->NbPoles(); ++i)
                if (segment->Weight(i) <= 0.0 || line.Distance(segment->Pole(i)) > tolerance)
                    return false;
            return true;
        }
        catch (const Standard_Failure&)
        {
            // Failure to prove coincidence must retain normal intersection handling.
            return false;
        }
    }

    std::optional<SLineParameterSpan> SurfaceBoundaryLineSpan(
        const TopoDS_Edge& edge, const TopoDS_Face& face, const gp_Lin& line,
        const SFeatureToolpathOptions& options)
    {
        try
        {
            double first, last;
            const auto pc = BRep_Tool::CurveOnSurface(edge, face, first, last);
            const auto surface = BRep_Tool::Surface(face);
            if (pc.IsNull() || surface.IsNull()) return std::nullopt;
            Bnd_Box2d bounds;
            BndLib_Add2dCurve::AddOptimal(pc, first, last, 0.0, bounds);
            if (bounds.IsVoid()) return std::nullopt;
            bounds.SetGap(0);
            double u0, v0, u1, v1; bounds.Get(u0, v0, u1, v1);
            // Require an isoparametric boundary, to roundoff in its native
            // parameter units. This is not a geometric near-line fit.
            const double roundoff = 64 * std::numeric_limits<double>::epsilon()
                * std::max({1.0, std::abs(u0), std::abs(u1), std::abs(v0), std::abs(v1)});
            const bool fixedU = u1 - u0 <= roundoff;
            const bool fixedV = v1 - v0 <= roundoff;
            if (fixedU == fixedV) return std::nullopt;
            const auto iso = fixedU ? surface->UIso((u0 + u1) * 0.5)
                : surface->VIso((v0 + v1) * 0.5);
            const double lo = fixedU ? v0 : u0, hi = fixedU ? v1 : u1;
            BRepBuilderAPI_MakeEdge maker(iso, lo, hi);
            if (!maker.IsDone()) return std::nullopt;
            const auto exact = maker.Edge();
            const BRepAdaptor_Curve curve(exact);
            const double tolerance = std::max(options.dLinearTolerance, options.dSkinDistanceTolerance);
            const bool onLine = curve.GetType() == GeomAbs_Line
                ? curve.Line().Direction().IsParallel(line.Direction(), options.dAngularToleranceRadians)
                    && line.Distance(iso->Value(lo)) <= tolerance
                    && line.Distance(iso->Value(hi)) <= tolerance
                : curve.GetType() == GeomAbs_BSplineCurve && IsSplineOnLine(exact, line, tolerance);
            if (!onLine) return std::nullopt;
            const double a = LineParameter(line, iso->Value(lo)), b = LineParameter(line, iso->Value(hi));
            return SLineParameterSpan{std::min(a, b), std::max(a, b)};
        }
        catch (const Standard_Failure&)
        {
            return std::nullopt;
        }
    }

    struct SFaceBoundaryCurve
    {
        TopoDS_Edge Edge;
        TopoDS_Edge AnalysisEdge;
        Handle(Geom_Curve) Curve;
        double First=0,Last=0;
        Bnd_Box Bounds;
    };

    std::vector<SFaceBoundaryCurve> FaceBoundaryCurves(const TopoDS_Face& face)
    {
        std::vector<SFaceBoundaryCurve> result;
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
            SFaceBoundaryCurve boundary;boundary.Edge=TopoDS::Edge(it.Current());
            boundary.AnalysisEdge=boundary.Edge;
            if(BRep_Tool::Degenerated(boundary.Edge))continue;
            double first,last;const auto pc=BRep_Tool::CurveOnSurface(boundary.Edge,face,first,last);
            if(!pc.IsNull()) {
                // Lift each finite trim once. Projecting every complete X line
                // onto a surface that cannot contain it can fail to terminate.
                BRep_Builder builder;TopoDS_Edge lifted;builder.MakeEdge(lifted);
                builder.UpdateEdge(lifted,pc,face,Precision::Confusion());builder.Range(lifted,face,first,last);
                try { if(BRepLib::BuildCurve3d(lifted,1e-8)) {
                    boundary.AnalysisEdge=lifted;
                    boundary.Curve=BRep_Tool::Curve(lifted,boundary.First,boundary.Last);
                    BRepBndLib::AddOptimal(lifted,boundary.Bounds,false,false);
                } } catch(const Standard_Failure&) {}
            }
            if(boundary.Curve.IsNull()) {
                boundary.Curve=BRep_Tool::Curve(boundary.Edge,boundary.First,boundary.Last);
                BRepBndLib::AddOptimal(boundary.Edge,boundary.Bounds,false,false);
            }
            result.push_back(std::move(boundary));
        }
        return result;
    }

    void CollectFaceLineBreaks(
        IN const TopoDS_Face& Face_,
        IN const std::vector<SFaceBoundaryCurve>& Boundaries_,
        IN const gp_Lin& Line_,
        IN const double LineMin_,
        IN const double LineMax_,
        IN const SFeatureToolpathOptions& Options_,
        OUT std::vector<double>& Parameters_,
        OUT std::vector<SLineParameterSpan>& ExistingEdgeSpans_)
    {
        const auto _Tolerance = std::max(
            Options_.dLinearTolerance,
            Options_.dSkinDistanceTolerance);
        Handle(Geom_Line) _LineCurve = new Geom_Line(Line_);
        Bnd_Box finiteBounds;BRepBndLib::AddOptimal(Face_,finiteBounds,false,false);
        double xmin,ymin,zmin,xmax,ymax,zmax;finiteBounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
        finiteBounds.Enlarge(_Tolerance);
        if(finiteBounds.IsOut(Line_))return;
        for (const auto& boundary:Boundaries_)
        {
            const auto _Edge = boundary.AnalysisEdge;
            try
            {
                if (const auto span = SurfaceBoundaryLineSpan(boundary.Edge, Face_, Line_, Options_))
                {
                    AddLineParameter(Line_, LinePoint(Line_, span->First), LineMin_, LineMax_, _Tolerance, Parameters_);
                    AddLineParameter(Line_, LinePoint(Line_, span->Last), LineMin_, LineMax_, _Tolerance, Parameters_);
                    ExistingEdgeSpans_.push_back({std::max(LineMin_, span->First), std::min(LineMax_, span->Last)});
                    continue;
                }
                BRepAdaptor_Curve _Adaptor(_Edge);
                const auto _EdgeType = _Adaptor.GetType();
                const bool _StraightSpline = _EdgeType == GeomAbs_BSplineCurve
                    && IsSplineOnLine(_Edge, Line_, _Tolerance);
                if (_EdgeType == GeomAbs_Line || _StraightSpline)
                {
                    if (_StraightSpline || (_Adaptor.Line().Direction().IsParallel(
                            Line_.Direction(),
                            Options_.dAngularToleranceRadians)
                        && Line_.Distance(_Adaptor.Value(_Adaptor.FirstParameter())) <= _Tolerance
                        && Line_.Distance(_Adaptor.Value(_Adaptor.LastParameter())) <= _Tolerance))
                    {
                        const auto _FirstVertex = TopExp::FirstVertex(_Edge, true);
                        const auto _LastVertex = TopExp::LastVertex(_Edge, true);
                        const auto _FirstPoint = _FirstVertex.IsNull()
                            ? _Adaptor.Value(_Adaptor.FirstParameter())
                            : BRep_Tool::Pnt(_FirstVertex);
                        const auto _LastPoint = _LastVertex.IsNull()
                            ? _Adaptor.Value(_Adaptor.LastParameter())
                            : BRep_Tool::Pnt(_LastVertex);
                        AddLineParameter(
                            Line_,
                            _FirstPoint,
                            LineMin_,
                            LineMax_,
                            _Tolerance,
                            Parameters_);
                        AddLineParameter(
                            Line_,
                            _LastPoint,
                            LineMin_,
                            LineMax_,
                            _Tolerance,
                            Parameters_);
                        const auto _First = LineParameter(Line_, _FirstPoint);
                        const auto _Last = LineParameter(Line_, _LastPoint);
                        if (std::abs(_Last - _First) > _Tolerance)
                        {
                            ExistingEdgeSpans_.push_back({
                                std::max(LineMin_, std::min(_First, _Last)),
                                std::min(LineMax_, std::max(_First, _Last)) });
                        }
                        continue;
                    }
                }

                const double _CurveFirst=boundary.First,_CurveLast=boundary.Last;
                const auto _Curve=boundary.Curve;
                if (_Curve.IsNull()
                    || !IsFinite(_CurveFirst)
                    || !IsFinite(_CurveLast)
                    || std::abs(_CurveLast - _CurveFirst) <= kEpsilon)
                {
                    continue;
                }

                AddLineParameter(
                    Line_,
                    _Curve->Value(_CurveFirst),
                    LineMin_,
                    LineMax_,
                    _Tolerance,
                    Parameters_);
                AddLineParameter(
                    Line_,
                    _Curve->Value(_CurveLast),
                    LineMin_,
                    LineMax_,
                    _Tolerance,
                    Parameters_);

                // Reject a provably disjoint 3D edge before the global spline
                // extrema solver. UV intersections and exact surface boundary
                // spans above are retained even when the independent 3D
                // approximation is farther from the line.
                Bnd_Box curveBounds=boundary.Bounds;
                if(!curveBounds.IsVoid()) {
                    curveBounds.Enlarge(_Tolerance);
                    if(curveBounds.IsOut(Line_))continue;
                }
                GeomAPI_ExtremaCurveCurve _Extrema(
                    _LineCurve,
                    _Curve,
                    LineMin_,
                    LineMax_,
                    _CurveFirst,
                    _CurveLast);
                for (int _Index = 1;
                    _Index <= _Extrema.NbExtrema();
                    ++_Index)
                {
                    gp_Pnt _LinePoint;
                    gp_Pnt _EdgePoint;
                    double _LineParameter = 0.0;
                    double _EdgeParameter = 0.0;
                    _Extrema.Points(_Index, _LinePoint, _EdgePoint);
                    _Extrema.Parameters(
                        _Index,
                        _LineParameter,
                        _EdgeParameter);
                    if (_LinePoint.Distance(_EdgePoint) <= _Tolerance)
                    {
                        AddLineParameter(
                            Line_,
                            _LinePoint,
                            LineMin_,
                            LineMax_,
                            _Tolerance,
                            Parameters_);
                    }
                }
            }
            catch (const Standard_Failure&)
            {
                continue;
            }
        }
    }

    bool IsCoveredByExistingEdge(
        IN const double First_,
        IN const double Last_,
        IN const std::vector<SLineParameterSpan>& ExistingEdgeSpans_,
        IN const double Tolerance_)
    {
        // A boundary can consist of several consecutive source edges. Its
        // union already covers the cut even when no single edge spans it.
        // Inserting that cut again can create a duplicate wire in the face.
        auto spans=ExistingEdgeSpans_;
        std::sort(spans.begin(),spans.end(),[](const auto& a,const auto& b){return a.First<b.First;});
        double covered=First_;
        for(const auto& span:spans) {
            if(span.Last<covered-Tolerance_)continue;
            if(span.First>covered+Tolerance_)return false;
            covered=std::max(covered,span.Last);
            if(covered>=Last_-Tolerance_)return true;
        }
        return false;
    }

    bool IsSkinFace(const TopoDS_Face&, const SSectionSupport&, const SFeatureToolpathOptions&,
        std::vector<std::string>* = nullptr);

    SSectionSupport PrivateSectionSupport(const SSectionSupport& source) {
        SSectionSupport result;result.Segments=source.Segments;
        for(const auto& curve:source.Curves)result.Curves.push_back({Handle(Geom2d_Curve)::DownCast(curve.Curve->Copy()),curve.First,curve.Last});
        if(!source.MaterialSection.IsNull())result.MaterialSection=TopoDS::Face(BRepBuilderAPI_Copy(source.MaterialSection,true,false).Shape());
        return result;
    }

    bool CoincidentBoundaryControlHulls(const Handle(Geom_BSplineCurve)& sourceA,
        const Handle(Geom_BSplineCurve)& sourceB,double tolerance,int depth=0) {
        auto a=Handle(Geom_BSplineCurve)::DownCast(sourceA->Copy()),b=Handle(Geom_BSplineCurve)::DownCast(sourceB->Copy());
        const auto normalize=[](const auto& curve){NCollection_Array1<double> knots(1,curve->NbKnots());curve->Knots(knots);
            BSplCLib::Reparametrize(0.,1.,knots);curve->SetKnots(knots);};
        normalize(a);normalize(b);const int degree=std::max(a->Degree(),b->Degree());a->IncreaseDegree(degree);b->IncreaseDegree(degree);
        std::map<double,int> knots;
        for(const auto& curve:{a,b})for(int i=1;i<=curve->NbKnots();++i)knots[curve->Knot(i)]=std::max(knots[curve->Knot(i)],curve->Multiplicity(i));
        for(const auto& [knot,multiplicity]:knots)if(knot>0&&knot<1){a->InsertKnot(knot,multiplicity,0,false);b->InsertKnot(knot,multiplicity,0,false);}
        bool common=a->NbPoles()==b->NbPoles()&&a->NbKnots()==b->NbKnots();
        for(int i=1;i<=a->NbKnots()&&common;++i)common=a->Knot(i)==b->Knot(i)&&a->Multiplicity(i)==b->Multiplicity(i);
        if(common) {
            double poleError=0,radius=0,minimum=std::numeric_limits<double>::max(),maximum=0;
            const auto origin=b->Pole(1);
            for(int i=1;i<=a->NbPoles();++i) {
                if(a->Weight(i)<=0||b->Weight(i)<=0){common=false;break;}
                poleError=std::max(poleError,a->Pole(i).Distance(b->Pole(i)));radius=std::max(radius,origin.Distance(b->Pole(i)));
                const double ratio=a->Weight(i)/b->Weight(i);minimum=std::min(minimum,ratio);maximum=std::max(maximum,ratio);
            }
            // Equal bases with positive weights admit a global convex-hull
            // bound, including different homogeneous weight normalizations.
            if(common&&poleError+radius*(maximum-minimum)/minimum<=tolerance)return true;
        }
        if(depth>=12)return false;
        const auto point=sourceA->Value((sourceA->FirstParameter()+sourceA->LastParameter())/2);
        GeomAPI_ProjectPointOnCurve projection(point,sourceB,sourceB->FirstParameter(),sourceB->LastParameter());
        if(!projection.NbPoints()||projection.LowerDistance()>tolerance)return false;
        const double midA=(sourceA->FirstParameter()+sourceA->LastParameter())/2,midB=projection.LowerDistanceParameter();
        if(midB<=sourceB->FirstParameter()+Precision::PConfusion()||midB>=sourceB->LastParameter()-Precision::PConfusion())return false;
        auto a0=Handle(Geom_BSplineCurve)::DownCast(sourceA->Copy()),a1=Handle(Geom_BSplineCurve)::DownCast(sourceA->Copy());
        auto b0=Handle(Geom_BSplineCurve)::DownCast(sourceB->Copy()),b1=Handle(Geom_BSplineCurve)::DownCast(sourceB->Copy());
        a0->Segment(sourceA->FirstParameter(),midA);a1->Segment(midA,sourceA->LastParameter());
        b0->Segment(sourceB->FirstParameter(),midB);b1->Segment(midB,sourceB->LastParameter());
        return CoincidentBoundaryControlHulls(a0,b0,tolerance,depth+1)&&CoincidentBoundaryControlHulls(a1,b1,tolerance,depth+1);
    }

    std::vector<std::uint8_t> ProvenStockWallFaces(const TopTools_IndexedMapOfShape& faces,
        const SFeatureToolpathOptions& options,std::vector<std::string>& diagnostics) {
        std::vector<std::uint8_t> stock(faces.Extent()+1);
        if(!options.bSingleExtrudedStock||faces.IsEmpty())return stock;
        struct Face {std::set<int> Outer,All;double NormalX=0,First=0,Last=0,Budget=0;};
        struct Plane {gp_Pln Support;gp_Dir Normal;std::vector<int> Faces;std::set<int> Outer;bool Oriented=true;};
        std::vector<Face> context(faces.Extent()+1);std::vector<Plane> planes;TopTools_IndexedMapOfShape edgeIds;
        for(int i=1;i<=faces.Extent();++i) {
            const auto face=TopoDS::Face(faces(i));auto& item=context[i];
            for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next())item.All.insert(edgeIds.Add(it.Current()));
            item.Budget=std::max(options.dLinearTolerance,BRep_Tool::Tolerance(face));
            for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next())item.Budget=std::max(item.Budget,BRep_Tool::Tolerance(TopoDS::Edge(it.Current())));
            for(TopExp_Explorer it(face,TopAbs_VERTEX);it.More();it.Next())item.Budget=std::max(item.Budget,BRep_Tool::Tolerance(TopoDS::Vertex(it.Current())));
            const auto outer=BRepTools::OuterWire(face);
            if(!outer.IsNull())for(TopExp_Explorer it(outer,TopAbs_EDGE);it.More();it.Next())item.Outer.insert(edgeIds.Add(it.Current()));
            Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);if(box.IsVoid())continue;
            double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);item.First=x;item.Last=X;
            BRepAdaptor_Surface surface(face,true);double u,U,v,V;BRepTools::UVBounds(face,u,U,v,V);
            for(int a=0;a<3;++a)for(int b=0;b<3;++b){gp_Pnt p;gp_Vec du,dv;
                surface.D1(u+(U-u)*(a+.5)/3,v+(V-v)*(b+.5)/3,p,du,dv);const auto n=du.Crossed(dv);
                if(n.Magnitude()>kEpsilon)item.NormalX=std::max(item.NormalX,std::abs(n.X())/n.Magnitude());}
            if(surface.GetType()!=GeomAbs_Plane)continue;
            const auto plane=surface.Plane();auto normal=plane.Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
            int at=-1;
            for(std::size_t p=0;p<planes.size();++p)if(plane.Axis().Direction().IsParallel(planes[p].Support.Axis().Direction(),1e-10)
                &&plane.Distance(planes[p].Support.Location())<=options.dLinearTolerance){at=static_cast<int>(p);break;}
            if(at<0){at=static_cast<int>(planes.size());planes.push_back({plane,normal,{},{},true});}
            auto& group=planes[at];group.Oriented=group.Oriented&&normal.Dot(group.Normal)>0;
            group.Faces.push_back(i);group.Outer.insert(item.Outer.begin(),item.Outer.end());
        }
        std::vector<std::vector<int>> neighbors(faces.Extent()+1);
        // Opposite occurrences on the same support can be local contacting
        // cutting faces. Their end connections do not prove that the whole
        // face is stock; retain section splitting and interface checks there.
        std::vector<std::uint8_t> conflictingSupports(faces.Extent()+1);
        for(const auto& plane:planes)if(!plane.Oriented)for(const auto f:plane.Faces)conflictingSupports[f]=1;
        std::map<int,std::vector<int>> uses;double firstX=std::numeric_limits<double>::max(),lastX=std::numeric_limits<double>::lowest();
        for(int i=1;i<=faces.Extent();++i){firstX=std::min(firstX,context[i].First);lastX=std::max(lastX,context[i].Last);
            for(const auto edge:context[i].All)uses[edge].push_back(i);}
        for(const auto& [edge,adjacent]:uses)for(const auto a:adjacent)for(const auto b:adjacent)if(a!=b)neighbors[a].push_back(b);
        // IGES commonly represents adjacent faces with distinct free edge
        // records. Retain those records; prove finite boundary coincidence
        // separately instead of sewing or changing source topology.
        struct Boundary {TopoDS_Edge Edge;Handle(Geom_Curve) Curve;double First=0,Last=0;Bnd_Box Bounds;bool Line=false;gp_Pnt A,B;};
        std::vector<Boundary> boundary(edgeIds.Extent()+1);std::vector<int> freeEdges;
        for(int id=1;id<=edgeIds.Extent();++id)if(uses[id].size()==1) {
            auto& edge=boundary[id];edge.Edge=TopoDS::Edge(edgeIds(id));if(BRep_Tool::Degenerated(edge.Edge))continue;
            edge.Curve=BRep_Tool::Curve(edge.Edge,edge.First,edge.Last);if(edge.Curve.IsNull())continue;
            edge.A=edge.Curve->Value(edge.First);edge.B=edge.Curve->Value(edge.Last);
            if(edge.A.Distance(edge.B)>options.dLinearTolerance) {
                const gp_Lin line(edge.A,gp_Dir(gp_Vec(edge.A,edge.B)));
                edge.Line=BRepAdaptor_Curve(edge.Edge).GetType()==GeomAbs_Line||IsSplineOnLine(edge.Edge,line,options.dLinearTolerance);
            }
            BRepBndLib::Add(edge.Edge,edge.Bounds,false);edge.Bounds.Enlarge(options.dLinearTolerance);freeEdges.push_back(id);
        }
        std::map<int,std::set<int>> coincident;std::size_t matched=0;
        for(std::size_t i=0;i<freeEdges.size();++i)for(std::size_t j=i+1;j<freeEdges.size();++j) {
            const int left=freeEdges[i],right=freeEdges[j];const auto& a=boundary[left];const auto& b=boundary[right];
            if(uses[left].front()==uses[right].front()||a.Bounds.IsOut(b.Bounds))continue;
            bool same=false;
            if(a.Line&&b.Line) {
                const gp_Vec along(a.A,a.B);const double length=along.Magnitude();const auto direction=along/length;
                const gp_Lin line(a.A,gp_Dir(direction));
                if(line.Distance(b.A)<=options.dLinearTolerance&&line.Distance(b.B)<=options.dLinearTolerance) {
                    const double first=gp_Vec(a.A,b.A).Dot(direction),last=gp_Vec(a.A,b.B).Dot(direction);
                    same=std::min(length,std::max(first,last))-std::max(0.,std::min(first,last))>options.dLinearTolerance;
                }
            } else if((a.A.Distance(b.A)<=options.dLinearTolerance&&a.B.Distance(b.B)<=options.dLinearTolerance)
                ||(a.A.Distance(b.B)<=options.dLinearTolerance&&a.B.Distance(b.A)<=options.dLinearTolerance)) {
                try {
                    const auto ca=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(a.Curve,a.First,a.Last));
                    auto cb=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(b.Curve,b.First,b.Last));
                    if(a.A.Distance(b.B)<a.A.Distance(b.A))cb->Reverse();
                    same=CoincidentBoundaryControlHulls(ca,cb,options.dLinearTolerance);
                }catch(const Standard_Failure&) {}
            }
            if(!same&&(!a.Line||!b.Line)) {
                // Adjacent IGES faces may divide the same curved boundary at
                // different parameters. Match finite overlapping intervals,
                // then prove the complete overlap with control-hull bounds.
                // End-point proximity alone never establishes adjacency.
                try {
                    struct Match {double A,B;gp_Pnt Point;};std::vector<Match> matches;
                    for(const double parameter:{a.First,a.Last}) {
                        const auto point=a.Curve->Value(parameter);
                        GeomAPI_ProjectPointOnCurve projection(point,b.Curve,b.First,b.Last);
                        if(projection.NbPoints()&&projection.LowerDistance()<=options.dLinearTolerance)
                            matches.push_back({parameter,projection.LowerDistanceParameter(),point});
                    }
                    for(const double parameter:{b.First,b.Last}) {
                        const auto point=b.Curve->Value(parameter);
                        GeomAPI_ProjectPointOnCurve projection(point,a.Curve,a.First,a.Last);
                        if(projection.NbPoints()&&projection.LowerDistance()<=options.dLinearTolerance)
                            matches.push_back({projection.LowerDistanceParameter(),parameter,point});
                    }
                    for(std::size_t p=0;p<matches.size()&&!same;++p)for(std::size_t q=p+1;q<matches.size()&&!same;++q) {
                        auto first=matches[p],last=matches[q];if(first.A>last.A)std::swap(first,last);
                        if(first.Point.Distance(last.Point)<=options.dLinearTolerance
                            ||last.A-first.A<=Precision::PConfusion()||std::abs(last.B-first.B)<=Precision::PConfusion())continue;
                        const auto ca=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(a.Curve,first.A,last.A));
                        auto cb=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(b.Curve,std::min(first.B,last.B),std::max(first.B,last.B)));
                        if(first.B>last.B)cb->Reverse();
                        same=CoincidentBoundaryControlHulls(ca,cb,options.dLinearTolerance);
                    }
                }catch(const Standard_Failure&) {}
            }
            if(!same)continue;
            coincident[left].insert(right);coincident[right].insert(left);++matched;
            for(const auto f:uses[left])for(const auto g:uses[right]){neighbors[f].push_back(g);neighbors[g].push_back(f);}
        }
        const auto shares=[&](const auto& a,const auto& b){for(const auto edge:a){if(b.contains(edge))return true;
            if(const auto found=coincident.find(edge);found!=coincident.end())for(const auto other:found->second)if(b.contains(other))return true;}return false;};
        if(matched)diagnostics.push_back("Proved "+std::to_string(matched)+" coincident free-edge intervals without changing source faces");
        struct Terminals {std::set<int> First,Last;double NormalX=0;};
        std::vector<Terminals> terminals;
        for(std::size_t i=0;i<planes.size();++i)for(std::size_t j=i+1;j<planes.size();++j) {
            const auto& a=planes[i];const auto& b=planes[j];
            // Face-only IGES sheets need not carry outward material normals.
            // The independent terminal bridges establish this wall relation.
            if(!a.Oriented||!b.Oriented||!a.Support.Axis().Direction().IsParallel(b.Support.Axis().Direction(),1e-10)
                ||a.Support.Distance(b.Support.Location())<=options.dLinearTolerance)continue;
            const double wallNormalX=std::max(std::abs(a.Normal.X()),std::abs(b.Normal.X()));
            const double transverse=wallNormalX+std::sin(options.dAngularToleranceRadians);
            std::vector<int> component(faces.Extent()+1,-1);std::vector<std::pair<bool,bool>> reaches;
            for(int seed=1;seed<=faces.Extent();++seed)if(component[seed]<0&&context[seed].NormalX>transverse) {
                const int id=static_cast<int>(reaches.size());reaches.push_back({false,false});std::vector<int> queue{seed};component[seed]=id;
                for(std::size_t at=0;at<queue.size();++at){const int f=queue[at];
                    reaches[id].first=reaches[id].first||context[f].First<=firstX+context[f].Budget;
                    reaches[id].second=reaches[id].second||context[f].Last>=lastX-context[f].Budget;
                    for(const auto neighbor:neighbors[f])if(component[neighbor]<0&&context[neighbor].NormalX>transverse){component[neighbor]=id;queue.push_back(neighbor);}
                }
            }
            std::vector<int> bridges;
            for(int k=1;k<=faces.Extent();++k) {
                if(context[k].NormalX<=transverse)continue;
                // Hole/countersink walls belong to inner wires. They cannot
                // certify a stock wall's connection to its two axial ends.
                if(shares(a.Outer,context[k].All)&&shares(b.Outer,context[k].All))bridges.push_back(k);
            }
            std::optional<std::pair<int,int>> ends;double separation=-1;
            for(std::size_t p=0;p<bridges.size();++p)for(std::size_t q=p+1;q<bridges.size();++q) {
                int first=bridges[p],last=bridges[q];if(context[first].First>context[last].First)std::swap(first,last);
                const int left=component[first],right=component[last];
                // Local pocket end walls cannot certify stock skins. Their
                // transverse components must independently reach opposite
                // terminals of the whole part, without crossing a wall.
                if(left<0||right<0||left==right||!reaches[left].first||reaches[left].second
                    ||!reaches[right].second||reaches[right].first)continue;
                const double gap=context[last].First-context[first].Last;
                if(gap>options.dLinearTolerance&&gap>separation){separation=gap;ends={{first,last}};}
            }
            if(!ends)continue;
            Terminals terminal;terminal.NormalX=std::min(context[ends->first].NormalX,context[ends->second].NormalX);
            for(int f=1;f<=faces.Extent();++f){if(component[f]==component[ends->first])terminal.First.insert(f);
                if(component[f]==component[ends->second])terminal.Last.insert(f);}
            terminals.push_back(std::move(terminal));for(const auto f:a.Faces)stock[f]=1;for(const auto f:b.Faces)stock[f]=1;
            diagnostics.push_back("Stock wall pair proved by opposite parallel supports and common outer-wire end faces="
                +std::to_string(ends->first)+","+std::to_string(ends->second)+"; wall faces="
                +std::to_string(a.Faces.front())+","+std::to_string(b.Faces.front()));
        }
        // Curved or slightly drafted continuations can be stock skins too.
        // Require both already certified end faces and an outer-wire join to
        // an established wall; merely being adjacent to a hole is insufficient.
        bool changed=true;
        while(changed){changed=false;for(int i=1;i<=faces.Extent();++i)if(!stock[i]&&!conflictingSupports[i]) {
            for(const auto& terminal:terminals) {
                if(terminal.First.contains(i)||terminal.Last.contains(i)
                    ||context[i].NormalX+std::sin(options.dAngularToleranceRadians)>=terminal.NormalX)continue;
                bool first=false,last=false;
                for(const auto face:terminal.First)first=first||shares(context[i].Outer,context[face].All);
                for(const auto face:terminal.Last)last=last||shares(context[i].Outer,context[face].All);
                if(!first||!last)continue;
                bool joinsWall=false;for(int j=1;j<=faces.Extent()&&!joinsWall;++j)
                    if(stock[j]&&shares(context[i].Outer,context[j].Outer))joinsWall=true;
                if(!joinsWall)continue;
                stock[i]=1;changed=true;diagnostics.push_back("Stock skin continuation joins both certified end faces: face="+std::to_string(i));break;
            }
        }}
        return stock;
    }

    bool WallProofHasVaryingSection(const TopoDS_Face& face,const SFeatureToolpathOptions& options) {
        const BRepAdaptor_Surface surface(face,true);
        if(surface.GetType()!=GeomAbs_Plane||std::abs(surface.Plane().Axis().Direction().X())>1e-10)return true;
        // A paired draft can carry nominally X-parallel top/bottom planes
        // whose longitudinal boundary itself changes transverse position.
        // Constant-section planar walls still require finite skin coverage.
        for(const auto& boundary:FaceBoundaryCurves(face)) {
            const auto a=boundary.Curve->Value(boundary.First),b=boundary.Curve->Value(boundary.Last);
            if(std::abs(a.X()-b.X())>options.dLinearTolerance
                &&std::hypot(a.Y()-b.Y(),a.Z()-b.Z())>options.dLinearTolerance)return true;
        }
        return false;
    }

    std::vector<std::uint8_t> ClassifyStockSkin(const TopTools_IndexedMapOfShape& faces,
        const SSectionSupport& support,const SFeatureToolpathOptions& options,std::vector<std::string>& diagnostics,
        std::vector<Bnd_Box>* exactBounds=nullptr) {
        const auto wallFlags=ProvenStockWallFaces(faces,options,diagnostics);
        std::vector<TopoDS_Face> privateFaces;
        for(int i=1;i<=faces.Extent();++i)privateFaces.push_back(TopoDS::Face(BRepBuilderAPI_Copy(faces(i),true,false).Shape()));
        std::vector<std::uint8_t> flags(faces.Extent()+1);
        std::vector<std::vector<std::string>> messages(faces.Extent());
        if(exactBounds)exactBounds->resize(faces.Extent()+1);
        const auto count=std::min(privateFaces.size(),iCAX::OpenCascade::detail::GeometryConcurrency(options.nMaximumConcurrency));
        std::vector<SSectionSupport> supports;for(std::size_t i=0;i<count;++i)supports.push_back(PrivateSectionSupport(support));
        std::atomic<std::size_t> nextFace{0};
        const auto workers=iCAX::OpenCascade::detail::ForGeometryIndices(count,count,[&](std::size_t slot){
            const auto& local=supports[slot];
            for(auto i=nextFace.fetch_add(1);i<privateFaces.size();i=nextFace.fetch_add(1)) {
                if(exactBounds)BRepBndLib::AddOptimal(privateFaces[i],(*exactBounds)[i+1],false,false);
                // Terminal bridges certify a wall support, not every finite
                // region on that plane. A coplanar cut ledge can extend into
                // the web's missing material and must retain section splits.
                const bool extends=wallFlags[i+1]&&WallProofHasVaryingSection(privateFaces[i],options);
                flags[i+1]=(wallFlags[i+1]&&extends)||IsSkinFace(privateFaces[i],local,options,&messages[i]);
            }
        });
        for(const auto& message:messages)diagnostics.insert(diagnostics.end(),message.begin(),message.end());
        diagnostics.push_back("Classified independent stock-skin faces with CAD Task workers="+std::to_string(workers));
        return flags;
    }

    TopoDS_Shape SplitTargetBySectionVertices(
        IN const TopoDS_Shape& Target_,
        IN const SSectionWiresResult& Section_,
        IN const SFeatureToolpathOptions& Options_,
        OUT std::vector<std::string>& Diagnostics_)
    {
        Bnd_Box _Bounds;
        BRepBndLib::Add(Target_, _Bounds);
        if (_Bounds.IsVoid())
        {
            Diagnostics_.push_back(
                "Cannot split target: target bounds are void; keep original target");
            return Target_;
        }

        double _XMin = 0.0;
        double _YMin = 0.0;
        double _ZMin = 0.0;
        double _XMax = 0.0;
        double _YMax = 0.0;
        double _ZMax = 0.0;
        _Bounds.Get(_XMin, _YMin, _ZMin, _XMax, _YMax, _ZMax);
        if (!IsFinite(_XMin) || !IsFinite(_XMax)
            || _XMax - _XMin <= Options_.dLinearTolerance)
        {
            Diagnostics_.push_back(
                "Cannot split target: target has no usable X extent; keep original target");
            return Target_;
        }

        const auto _Vertices = CollectSectionVertices(
            Section_,
            Options_.dSkinDistanceTolerance);
        if (_Vertices.empty())
        {
            Diagnostics_.push_back(
                "Section has no usable vertices for face splitting");
            return Target_;
        }

        struct SFaceSplitWork final
        {
            TopoDS_Face SourceFace;
            int SourceIndex=0;
            std::vector<TopoDS_Edge> Edges;
            bool bFailed = false;
        };

        std::vector<SFaceSplitWork> _FaceWorks;
        std::size_t _ValidSegmentCount = 0;
        std::size_t _ExistingEdgeCount = 0;
        std::size_t _StockFaceCount = 0, _TransversePlaneCount = 0;
        const auto _Support = BuildSectionSupport(Section_);

        TopTools_IndexedMapOfShape _TargetFaces;
        TopExp::MapShapes(Target_, TopAbs_FACE, _TargetFaces);
        const auto wallFlags=ProvenStockWallFaces(_TargetFaces,Options_,Diagnostics_);
        struct SPreparedFace {TopoDS_Face Face;std::vector<std::string> Diagnostics;bool Skin=false,Transverse=false;};
        std::vector<SPreparedFace> prepared(_TargetFaces.Extent());
        for(int i=1;i<=_TargetFaces.Extent();++i)
            prepared[i-1].Face=TopoDS::Face(BRepBuilderAPI_Copy(_TargetFaces(i),true,false).Shape());
        const auto slots=std::min(prepared.size(),iCAX::OpenCascade::detail::GeometryConcurrency(Options_.nMaximumConcurrency));
        std::vector<SSectionSupport> supports;for(std::size_t i=0;i<slots;++i)supports.push_back(PrivateSectionSupport(_Support));
        std::atomic<std::size_t> nextPreparedFace{0};
        const auto preparationWorkers=iCAX::OpenCascade::detail::ForGeometryIndices(slots,slots,[&](std::size_t slot){
            const auto& support=supports[slot];
            for(auto i=nextPreparedFace.fetch_add(1);i<prepared.size();i=nextPreparedFace.fetch_add(1)){
                auto& job=prepared[i];auto& face=job.Face;const auto index=std::to_string(i+1);
                const auto planar=section_detail::CanonicalPlanarTrimFace(face);
                if(!planar.IsNull()){face=planar;job.Diagnostics.push_back("Exact planar support with unchanged surface trims: face="+index);}
                if(section_detail::NeedsPreciseTrimTopology(face,Options_.dLinearTolerance)){
                    std::string reason;const auto exact=section_detail::PreciseTrimAnalysisFace(face,Options_.dLinearTolerance,reason);
                    if(!exact.IsNull()){face=exact;job.Diagnostics.push_back("Reconstructed original face trims for splitting: face="+index);}
                    else job.Diagnostics.push_back("Original trim reconstruction unavailable: face="+index+" "+reason);
                }
                const BRepAdaptor_Surface surface(face);
                job.Skin=wallFlags[i+1]&&WallProofHasVaryingSection(face,Options_);
                job.Transverse=!job.Skin&&surface.GetType()==GeomAbs_Plane
                    &&std::abs(surface.Plane().Axis().Direction().X())>std::sin(Options_.dAngularToleranceRadians);
                if(!job.Transverse&&!job.Skin)job.Skin=IsSkinFace(face,support,Options_);
            }
        });
        Diagnostics_.push_back("Prepared independent split faces with CAD Task workers="+std::to_string(preparationWorkers));
        std::vector<SFaceSplitWork> splitWorks(prepared.size());
        std::vector<std::size_t> validSegments(prepared.size()),existingEdges(prepared.size());
        // Each task owns its face, trims and generated tools. Serialize
        // callbacks and collect outputs in source order after joining.
        std::mutex progressMutex;
        const auto splitWorkers=iCAX::OpenCascade::detail::ForGeometryIndices(prepared.size(),Options_.nMaximumConcurrency,[&](std::size_t at) {
            const int _FaceIndex=static_cast<int>(at)+1;
            auto& job=prepared[_FaceIndex-1];auto _Face=job.Face;
            if(job.Transverse||job.Skin)return;
            auto& _Work=splitWorks[at];
            _Work.SourceFace = _Face;
            _Work.SourceIndex = _FaceIndex;

            Bnd_Box _FaceBounds;BRepBndLib::AddOptimal(_Face,_FaceBounds,false,false);
            if(_FaceBounds.IsVoid())return;
            double _FaceXMin,_FaceYMin,_FaceZMin,_FaceXMax,_FaceYMax,_FaceZMax;
            _FaceBounds.Get(_FaceXMin,_FaceYMin,_FaceZMin,_FaceXMax,_FaceYMax,_FaceZMax);
            const auto _BoundaryCurves=FaceBoundaryCurves(_Face);

            for (const auto& _Vertex : _Vertices)
            {
                if (_Vertex.X < _FaceYMin - Options_.dSkinDistanceTolerance
                    || _Vertex.X > _FaceYMax + Options_.dSkinDistanceTolerance
                    || _Vertex.Y < _FaceZMin - Options_.dSkinDistanceTolerance
                    || _Vertex.Y > _FaceZMax + Options_.dSkinDistanceTolerance)
                {
                    continue;
                }

                const gp_Lin _Line(
                    gp_Pnt(0.0, _Vertex.X, _Vertex.Y),
                    gp_Dir(1.0, 0.0, 0.0));
                std::vector<double> _Parameters;
                std::vector<SLineParameterSpan> _ExistingEdgeSpans;
                if(Options_.Progress) {
                    std::lock_guard lock(progressMutex);
                    Options_.Progress("split face="+std::to_string(_FaceIndex)+" line=("+std::to_string(_Vertex.X)+","+std::to_string(_Vertex.Y)+") collect trims");
                }
                CollectFaceLineBreaks(
                    _Face,
                    _BoundaryCurves,
                    _Line,
                    _XMin,
                    _XMax,
                    Options_,
                    _Parameters,
                    _ExistingEdgeSpans);
                std::sort(_Parameters.begin(), _Parameters.end());
                _Parameters.erase(
                    std::unique(
                        _Parameters.begin(),
                        _Parameters.end(),
                        [&](const auto _Left, const auto _Right) {
                            return std::abs(_Left - _Right)
                                <= Options_.dLinearTolerance;
                        }),
                    _Parameters.end());
                if (_Parameters.size() < 2)
                {
                    continue;
                }

                TopoDS_Edge _LineEdge;
                for (std::size_t _Index = 1;
                    _Index < _Parameters.size();
                    ++_Index)
                {
                    const auto _First = _Parameters[_Index - 1];
                    const auto _Last = _Parameters[_Index];
                    if (_Last - _First <= Options_.dLinearTolerance)
                    {
                        continue;
                    }
                    if (!IsLineSegmentOnFace(
                        _Face,
                        _Line,
                        _First,
                        _Last,
                        Options_))
                    {
                        continue;
                    }

                    ++validSegments[at];
                    if (IsCoveredByExistingEdge(
                        _First,
                        _Last,
                        _ExistingEdgeSpans,
                        Options_.dLinearTolerance))
                    {
                        ++existingEdges[at];
                        continue;
                    }

                    try
                    {
                        // Measure the reached tolerance only where the line
                        // actually lies on this trimmed face. In particular a
                        // spline's 3D and UV representations need not agree to
                        // the default tolerance of a freshly made line edge.
                        auto _Segment = BRepBuilderAPI_MakeEdge(_Line, _First, _Last).Edge();
                        BOPTools_AlgoTools2D::BuildPCurveForEdgeOnFace(_Segment, _Face);
                        BRepLib::SameParameter(_Segment, Options_.dLinearTolerance, true);
                        const double _ReachedTolerance = BRep_Tool::Tolerance(_Segment);
                        BRep_Builder _ToolBuilder;
                        if (!_LineEdge.IsNull()) {
                            _ToolBuilder.UpdateEdge(_LineEdge,
                                std::max(BRep_Tool::Tolerance(_LineEdge), _ReachedTolerance));
                            continue;
                        }
                        // Break parameters prove that this generator crosses
                        // the trimmed face. They can come from independent
                        // edge approximations, so they must not also clip the
                        // Boolean tool: a tiny endpoint gap can leave a mixed
                        // stock/cut face unsplit. Let the splitter intersect
                        // the complete generator with the original trims.
                        const auto _FirstPoint = LinePoint(_Line, _FaceXMin - Options_.dLinearTolerance);
                        const auto _LastPoint = LinePoint(_Line, _FaceXMax + Options_.dLinearTolerance);
                        BRepBuilderAPI_MakeEdge _Maker(
                            _Line,
                            _FirstPoint,
                            _LastPoint);
                        if (!_Maker.IsDone())
                        {
                            _Work.bFailed = true;
                            continue;
                        }

                        auto _Edge = _Maker.Edge();
                        _ToolBuilder.UpdateEdge(_Edge, _ReachedTolerance);
                        // The complete tool can leave a finite spline support.
                        // The Boolean splitter constructs pcurves on its actual
                        // intersection segments; projecting the untrimmed tool
                        // here would fail or extrapolate the source surface.
                        _Work.Edges.push_back(_Edge);
                        _LineEdge = _Edge;
                    }
                    catch (const Standard_Failure& _Error_)
                    {
                        _Work.bFailed = true;
                        job.Diagnostics.push_back(
                            std::string("Failed to add face-local split edge on face ")+std::to_string(_FaceIndex)
                            +" (surface "+std::to_string(BRepAdaptor_Surface(_Face).GetType())+"): "
                            + _Error_.GetMessageString());
                    }
                }
            }

        });
        Diagnostics_.push_back("Collected independent face-local split tools with CAD Task workers="+std::to_string(splitWorkers));
        for(std::size_t at=0;at<prepared.size();++at) {
            const auto& job=prepared[at];
            Diagnostics_.insert(Diagnostics_.end(),job.Diagnostics.begin(),job.Diagnostics.end());
            _TransversePlaneCount+=job.Transverse;_StockFaceCount+=job.Skin;
            _ValidSegmentCount+=validSegments[at];_ExistingEdgeCount+=existingEdges[at];
            auto& work=splitWorks[at];
            if(!work.Edges.empty()||work.bFailed)_FaceWorks.push_back(std::move(work));
        }

        if(_TransversePlaneCount)Diagnostics_.push_back("Retained "+std::to_string(_TransversePlaneCount)
            +" transverse/oblique planar cutting faces without axial split searches; original geometry unchanged");
        if (_ValidSegmentCount == 0)
        {
            Diagnostics_.push_back(
                "No section-vertex split is needed outside " + std::to_string(_StockFaceCount)
                + " faces already on the nominal extrusion skin");
            return Target_;
        }

        std::size_t _AddedEdgeCount = 0;
        // Intersect all surface-qualified cuts with the original topology in
        // one operation. The Boolean splitter creates shared boundary vertices
        // itself; BRepFeat requires them to be shared before registering cuts.
        TopTools_ListOfShape _Arguments, _Tools;
        _Arguments.Append(Target_);
        for (const auto& _Work : _FaceWorks)
        {
            if (_Work.bFailed || _Work.Edges.empty())
            {
                Diagnostics_.push_back(
                    "Face-local split preparation failed; keep the original face");
                continue;
            }

            try
            {
                for (const auto& _Edge : _Work.Edges)
                {
                    _Tools.Append(_Edge);
                }
                _AddedEdgeCount += _Work.Edges.size();
            }
            catch (const Standard_Failure& _Error_)
            {
                Diagnostics_.push_back(std::string("OCC face split raised an exception; keep the original face: ")
                    + _Error_.GetMessageString());
                return {};
            }
        }
        if (_AddedEdgeCount == 0)
        {
            Diagnostics_.push_back(_ExistingEdgeCount == _ValidSegmentCount
                ? "All section-vertex segments already exist as target edges"
                : "No face was split successfully; keep the original target faces");
            return Target_;
        }

        BRep_Builder resultBuilder;TopoDS_Compound independentFaces;resultBuilder.MakeCompound(independentFaces);
        try
        {
            for(int i=1;i<=_TargetFaces.Extent();++i) {
                const auto face=TopoDS::Face(_TargetFaces(i));
                const auto work=std::find_if(_FaceWorks.begin(),_FaceWorks.end(),[&](const auto& item){return item.SourceIndex==i;});
                if(work==_FaceWorks.end()||work->Edges.empty()) {resultBuilder.Add(independentFaces,face);continue;}
                if(work->bFailed)return {};
                if(Options_.Progress)Options_.Progress("boolean split original face="+std::to_string(i)+" tools="+std::to_string(work->Edges.size()));
                TopTools_ListOfShape arguments,tools;arguments.Append(work->SourceFace);for(const auto& edge:work->Edges)tools.Append(edge);
                BRepAlgoAPI_Splitter splitter;splitter.SetArguments(arguments);splitter.SetTools(tools);
                splitter.SetNonDestructive(true);splitter.SetFuzzyValue(0.0);splitter.Build();
                if(!splitter.IsDone()||splitter.Shape().IsNull()) {
                    Diagnostics_.push_back("OCC face-local split failed: face="+std::to_string(i));return {};
                }
                GProp_GProps beforeArea,afterArea;BRepGProp::SurfaceProperties(work->SourceFace,beforeArea,1e-9);
                BRepGProp::SurfaceProperties(splitter.Shape(),afterArea,1e-9);
                const double localError=std::abs(beforeArea.Mass()-afterArea.Mass())/std::max(1.,std::abs(beforeArea.Mass()));
                if(localError>1e-6)Diagnostics_.push_back("Face split area changed: source-face="+std::to_string(i)
                    +" before="+std::to_string(beforeArea.Mass())+" after="+std::to_string(afterArea.Mass()));
                if(localError>1e-6) {
                    double refinedAfter=0;for(TopExp_Explorer it(splitter.Shape(),TopAbs_FACE);it.More();it.Next())refinedAfter+=iCAX::OpenCascade::integration_detail::RefinedArea(TopoDS::Face(it.Current()));
                    Diagnostics_.push_back("Independent trim-domain split area: source-face="+std::to_string(i)
                        +" before="+std::to_string(iCAX::OpenCascade::integration_detail::RefinedArea(work->SourceFace))+" after="+std::to_string(refinedAfter));
                }
                for(TopExp_Explorer it(splitter.Shape(),TopAbs_FACE);it.More();it.Next())resultBuilder.Add(independentFaces,it.Current());
            }
        }
        catch (const Standard_Failure& error)
        {
            Diagnostics_.push_back(std::string("OCC batched face split raised an exception: ")
                + error.GetMessageString());
            return {};
        }

        Diagnostics_.push_back(
            "Collected " + std::to_string(_ValidSegmentCount)
            + " face-local split segments ("
            + std::to_string(_AddedEdgeCount)
            + " new, "
            + std::to_string(_ExistingEdgeCount)
            + " already present)");
        Diagnostics_.push_back(
            "Each original face was split only by its own surface-qualified X-directed edges; independent source faces and opposite occurrences retained");
        return independentFaces;
    }

    bool IsPerpendicularToX(
        IN const gp_Vec& Vector_,
        IN const double ToleranceRadians_)
    {
        if (Vector_.SquareMagnitude() <= kEpsilon * kEpsilon)
        {
            return false;
        }
        return std::abs(Vector_.X()) / Vector_.Magnitude()
            <= std::sin(ToleranceRadians_);
    }

    bool IsXSideFace(
        IN const TopoDS_Face& Face_,
        IN const SFeatureToolpathOptions& Options_)
    {
        try
        {
            BRepAdaptor_Surface _Surface(Face_, true);
            double _U0 = 0.0;
            double _U1 = 0.0;
            double _V0 = 0.0;
            double _V1 = 0.0;
            BRepTools::UVBounds(Face_, _U0, _U1, _V0, _V1);
            if (!IsFinite(_U0) || !IsFinite(_U1)
                || !IsFinite(_V0) || !IsFinite(_V1)
                || std::abs(_U1 - _U0) <= kEpsilon
                || std::abs(_V1 - _V0) <= kEpsilon)
            {
                return false;
            }

            // Use the same geometric test as the old implementation:
            // sample a 3x3 UV grid, compute N = dU x dV, and require every
            // valid local normal to be perpendicular to the X extrusion axis.
            // This deliberately does not classify a face by its surface type.
            bool _HasNormal = false;
            constexpr int _SampleGridSize = 3;
            for (int _UIndex = 0;
                _UIndex < _SampleGridSize;
                ++_UIndex)
            {
                const auto _U = _U0 + (_U1 - _U0)
                    * (static_cast<double>(_UIndex) + 0.5)
                    / static_cast<double>(_SampleGridSize);
                for (int _VIndex = 0;
                    _VIndex < _SampleGridSize;
                    ++_VIndex)
                {
                    const auto _V = _V0 + (_V1 - _V0)
                        * (static_cast<double>(_VIndex) + 0.5)
                        / static_cast<double>(_SampleGridSize);
                    gp_Pnt _Point;
                    gp_Vec _DU;
                    gp_Vec _DV;
                    _Surface.D1(_U, _V, _Point, _DU, _DV);
                    const auto _Normal = _DU.Crossed(_DV);
                    if (_Normal.SquareMagnitude() <= kEpsilon * kEpsilon)
                    {
                        continue;
                    }

                    _HasNormal = true;
                    if (!IsPerpendicularToX(
                        _Normal,
                        Options_.dAngularToleranceRadians))
                    {
                        return false;
                    }
                }
            }
            return _HasNormal;
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
    }

    bool IsSkinFace(
        IN const TopoDS_Face& Face_,
        IN const SSectionSupport& Support_,
        IN const SFeatureToolpathOptions& Options_,
        std::vector<std::string>* Diagnostics_)
    {
        // Do not classify by "end face" or by a machining direction.  The
        // only geometric prerequisite is the same X-directed side-face rule
        // used by section extraction.
        if (!IsXSideFace(Face_, Options_))
        {
            return false;
        }

        bool _HasBoundarySample = false, outsideTrim = false, nearTrim = true;
        double sourceTolerance=BRep_Tool::Tolerance(Face_);
        for(TopExp_Explorer e(Face_,TopAbs_EDGE);e.More();e.Next())
            sourceTolerance=std::max(sourceTolerance,BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));
        try
        {
            const bool planar=BRepAdaptor_Surface(Face_,true).GetType()==GeomAbs_Plane;
            for (TopExp_Explorer _Explorer(Face_, TopAbs_EDGE);
                _Explorer.More();
                _Explorer.Next())
            {
                const auto _Samples = SampleEdge(
                    TopoDS::Edge(_Explorer.Current()),
                    Options_.nCurveClassificationSampleCount,
                    Options_.dLinearTolerance,
                    Face_);
                for (const auto& _Point : _Samples)
                {
                    _HasBoundarySample = true;
                    if (!IsOnSkin(
                        _Point,
                        Support_,
                        Options_.dSkinDistanceTolerance))
                    {
                        outsideTrim=true;
                        if(!IsOnSkin(_Point,Support_,Options_.dSkinDistanceTolerance+2*sourceTolerance)) {
                            // The planar proof below unconditionally rejects
                            // !nearTrim. Once proved, remaining samples and
                            // area integrals cannot change that result.
                            if(planar)return false;
                            nearTrim=false;
                        }
                    }
                }
            }
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
        if(!outsideTrim)return _HasBoundarySample;
        // A tolerant imported planar trim can contain a tiny endpoint loop
        // outside an otherwise exact stock plane. Prove area coverage within
        // the source's own boundary uncertainty; do not raise skin distance
        // tolerance or discard a precise, positive-area machining overhang.
        try {
            BRepAdaptor_Surface surface(Face_,true);
            Bnd_Box box;BRepBndLib::AddOptimal(Face_,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
            GProp_GProps area,perimeter;BRepGProp::SurfaceProperties(Face_,area,1e-9);BRepGProp::LinearProperties(Face_,perimeter);
            const double uncertainty=2*sourceTolerance*perimeter.Mass()+kPi*sourceTolerance*sourceTolerance;
            const auto commonArea=[&](const TopoDS_Shape& other){
                BRepAlgoAPI_Common common;TopTools_ListOfShape args,tools;args.Append(Face_);tools.Append(other);
                common.SetArguments(args);common.SetTools(tools);common.SetNonDestructive(true);
                common.SetFuzzyValue(Options_.dLinearTolerance);common.Build();
                if(!common.IsDone()||common.Shape().IsNull())return 0.;
                GProp_GProps covered;BRepGProp::SurfaceProperties(common.Shape(),covered,1e-9);return covered.Mass();
            };
            if(surface.GetType()!=GeomAbs_Plane){
                // An imported fillet can overlap its adjacent web. Its
                // visible part contributes to the nominal contour, while
                // the hidden continuation lies inside the same material.
                // Require positive exact skin overlap AND complete area
                // containment. A separate shallow groove has no stock-skin
                // overlap and cannot pass this proof.
                if(Support_.MaterialSection.IsNull()){
                    if(Diagnostics_)Diagnostics_->push_back("Curved stock coverage unavailable: fixed material section could not be built");
                    return false;
                }
                bool contributes=false;
                const gp_Pln plane(gp_Ax3(gp_Pnt(x-1,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0)));
                for(const auto& curve:Support_.Curves){
                    BRepBuilderAPI_MakeEdge edge(GeomAPI::To3d(curve.Curve,plane),curve.First,curve.Last);
                    if(!edge.IsDone())continue;
                    const auto skin=BRepPrimAPI_MakePrism(edge.Edge(),gp_Vec(X-x+2,0,0)).Shape();
                    if(commonArea(skin)>uncertainty){contributes=true;break;}
                }
                if(!contributes){
                    if(Diagnostics_)Diagnostics_->push_back("Curved side face has no positive stock-skin overlap: area="+std::to_string(area.Mass()));
                    return false;
                }
                gp_Trsf pose;pose.SetTranslation(gp_Vec(x-1,0,0));
                const auto base=BRepBuilderAPI_Transform(Support_.MaterialSection,pose,true).Shape();
                const auto stock=BRepPrimAPI_MakePrism(base,gp_Vec(X-x+2,0,0)).Shape();
                const double covered=commonArea(stock);
                if(Diagnostics_)Diagnostics_->push_back("Curved stock continuation coverage: area="+std::to_string(area.Mass())
                    +" covered="+std::to_string(covered)+" boundary uncertainty="+std::to_string(uncertainty));
                return covered>0&&std::abs(area.Mass()-covered)<=uncertainty;
            }
            if(!nearTrim)return false;
            for(const auto& segment:Support_.Segments){
                const gp_Pnt a(x-1,segment.first.X,segment.first.Y),b(x-1,segment.second.X,segment.second.Y);
                const auto plane=surface.Plane();
                if(plane.Distance(a)>Options_.dLinearTolerance||plane.Distance(b)>Options_.dLinearTolerance)continue;
                const auto edge=BRepBuilderAPI_MakeEdge(a,b).Edge();
                const auto skin=BRepPrimAPI_MakePrism(edge,gp_Vec(X-x+2,0,0)).Shape();
                const double covered=commonArea(skin);
                if(covered>0&&std::abs(area.Mass()-covered)<=uncertainty)return true;
            }
        } catch(const Standard_Failure&) {}
        return false;
    }

    std::unordered_set<int> InternalStockInterfaceFaces(
        const TopTools_IndexedMapOfShape& faces,const SSectionSupport& support,
        const SFeatureToolpathOptions& options,std::vector<std::string>& diagnostics,const std::vector<std::uint8_t>* skinFlags=nullptr,
        const std::vector<Bnd_Box>* exactBounds=nullptr)
    {
        std::unordered_set<int> result;
        if(!options.bSingleExtrudedStock||support.MaterialSection.IsNull()||faces.IsEmpty())return result;
        Bnd_Box bounds;for(int i=1;i<=faces.Extent();++i) {
            if(exactBounds)bounds.Add((*exactBounds)[i]);else BRepBndLib::AddOptimal(faces(i),bounds,false,false);
        }
        if(bounds.IsVoid())return result;double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
        if(X-x<=options.dLinearTolerance)return result;
        TopoDS_Shape stock;
        for(int i=1;i<=faces.Extent();++i) {
          try {
            const auto face=TopoDS::Face(faces(i));const BRepAdaptor_Surface surface(face);
            if(surface.GetType()!=GeomAbs_Plane||std::abs(surface.Plane().Axis().Direction().X())>1e-12)continue;
            Bnd_Box own;if(exactBounds)own=(*exactBounds)[i];else BRepBndLib::AddOptimal(face,own,false,false);double a,b,c,A,B,C;own.Get(a,b,c,A,B,C);
            // A local cut is not a permanent extrusion interface. Require
            // both axial terminals of this input, using the existing linear
            // tolerance, rather than a length or aspect-ratio threshold.
            if(std::abs(a-x)>options.dLinearTolerance||std::abs(A-X)>options.dLinearTolerance)continue;
            // Local cutting faces cannot satisfy this proof. Reject them before
            // repeating the much more expensive exact stock-skin predicate.
            if(skinFlags?(*skinFlags)[i]!=0:IsSkinFace(face,support,options))continue;
            GProp_GProps sourceMass;BRepGProp::SurfaceProperties(face,sourceMass,1e-9);const double sourceArea=std::abs(sourceMass.Mass());
            if(sourceArea<=0)continue;
            auto orientedNormal=gp_Vec(surface.Plane().Axis().Direction());if(face.Orientation()==TopAbs_REVERSED)orientedNormal.Reverse();
            int opposite=0;
            for(int j=1;j<=faces.Extent()&&!opposite;++j) {
                if(i==j)continue;const auto other=TopoDS::Face(faces(j));const BRepAdaptor_Surface otherSurface(other);
                if(otherSurface.GetType()!=GeomAbs_Plane||surface.Plane().Distance(otherSurface.Plane().Location())>Precision::Confusion())continue;
                auto otherNormal=gp_Vec(otherSurface.Plane().Axis().Direction());if(other.Orientation()==TopAbs_REVERSED)otherNormal.Reverse();
                if(orientedNormal.Dot(otherNormal)>-1+1e-12)continue;
                Bnd_Box otherBounds;if(exactBounds)otherBounds=(*exactBounds)[j];else BRepBndLib::AddOptimal(other,otherBounds,false,false);double ox,oy,oz,oX,oY,oZ;otherBounds.Get(ox,oy,oz,oX,oY,oZ);
                if(ox>a+options.dLinearTolerance||oX<A-options.dLinearTolerance)continue;
                BRepAlgoAPI_Common common;TopTools_ListOfShape arguments,tools;arguments.Append(face);tools.Append(other);
                common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.Build();
                if(!common.IsDone()||!BRepCheck_Analyzer(common.Shape(),true).IsValid())continue;
                GProp_GProps overlap;BRepGProp::SurfaceProperties(common.Shape(),overlap,1e-9);
                if(std::abs(std::abs(overlap.Mass())-sourceArea)<=1e-6*std::max(1.,sourceArea))opposite=j;
            }
            // Coincidence alone is insufficient: retain isolated pocket floors.
            if(!opposite)continue;
            if(stock.IsNull()) {
                gp_Trsf pose;pose.SetTranslation(gp_Vec(x-options.dLinearTolerance,0,0));
                const auto base=BRepBuilderAPI_Transform(support.MaterialSection,pose,true).Shape();
                stock=BRepPrimAPI_MakePrism(base,gp_Vec(X-x+2*options.dLinearTolerance,0,0)).Shape();
                if(stock.IsNull()||!BRepCheck_Analyzer(stock,true).IsValid())return result;
            }
            GProp_GProps mass;BRepGProp::SurfaceProperties(face,mass,1e-9);const double area=std::abs(mass.Mass());
            if(area<=0)continue;bool internal=true;double covered[2]={0,0};
            const gp_Vec normal(surface.Plane().Axis().Direction());
            for(int side=0;side<2;++side) {
                gp_Trsf shift;shift.SetTranslation(normal*((side?1.:-1.)*options.dSkinDistanceTolerance));
                const auto offset=BRepBuilderAPI_Transform(face,shift,true).Shape();
                BRepAlgoAPI_Common common;TopTools_ListOfShape arguments,tools;arguments.Append(offset);tools.Append(stock);
                common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.Build();
                if(!common.IsDone()||!BRepCheck_Analyzer(common.Shape(),true).IsValid()){internal=false;break;}
                GProp_GProps overlap;BRepGProp::SurfaceProperties(common.Shape(),overlap,1e-9);covered[side]=std::abs(overlap.Mass());
                if(std::abs(covered[side]-area)>1e-6*std::max(1.,area)){internal=false;break;}
            }
            if(internal) {
                result.insert(i);diagnostics.push_back("Internal fixed-stock interface face="+std::to_string(i)
                    +" opposite face="+std::to_string(opposite)+" spans both axial terminals; area="+std::to_string(area)+" material coverage on both normal sides="
                    +std::to_string(covered[0])+","+std::to_string(covered[1]));
            }
          } catch(const Standard_Failure&) {
            diagnostics.push_back("Internal stock interface proof incomplete for face="+std::to_string(i)+"; original machining classification retained");
          }
        }
        return result;
    }

    struct SInternalInterfacePiece final
    {
        TopoDS_Face Face;
        int ParentIndex=0;
        bool Internal=false;
        bool Derived=false;
        std::set<int> AnchorIndices;
        std::set<std::uint64_t> SourceFaceIds,AnchorSourceFaceIds,SourceEdgeIds,SourceCurveIds;
    };

    double InterfaceArea(const TopoDS_Shape& shape)
    {
        GProp_GProps mass;BRepGProp::SurfaceProperties(shape,mass,1e-9);return std::abs(mass.Mass());
    }

    std::string InterfaceIds(const auto& ids)
    {
        std::string text;for(const auto id:ids){if(!text.empty())text+=",";text+=std::to_string(id);}return text;
    }

    // The anchor already passed the complete, two-sided fixed-stock proof.
    // Only its actual finite domain may be inherited by an opposite face.
    std::vector<SInternalInterfacePiece> RefineInternalInterfaceDomains(
        const TopTools_IndexedMapOfShape& faces,const std::unordered_set<int>& interfaces,
        const std::vector<bool>& machining,std::vector<std::string>& diagnostics,bool& changed)
    {
        std::vector<SInternalInterfacePiece> output;changed=false;if(interfaces.empty())return output;
        std::vector<int> anchors(interfaces.begin(),interfaces.end());std::sort(anchors.begin(),anchors.end());
        const auto collect=[](const TopoDS_Shape& shape,const TopoDS_Face& source) {
            std::vector<TopoDS_Face> result;TopTools_IndexedMapOfShape fragments;TopExp::MapShapes(shape,TopAbs_FACE,fragments);
            TopLoc_Location sourceLocation;const auto sourceSupport=BRep_Tool::Surface(source,sourceLocation);
            for(int k=1;k<=fragments.Extent();++k){auto face=TopoDS::Face(fragments(k));face.Orientation(source.Orientation());
                TopLoc_Location location;const auto support=BRep_Tool::Surface(face,location);
                if(support!=sourceSupport||location!=sourceLocation||!BRepCheck_Analyzer(face,true).IsValid())
                    throw std::runtime_error("Internal-interface partition did not retain the unchanged valid source support");
                // Each source occurrence remains independent even if Common
                // reuses the opposite anchor's coincident native face.
                const auto independent=BRepBuilderAPI_Copy(face,false,false).Shape();
                result.push_back(TopoDS::Face(independent));
            }return result;
        };
        for(int i=1;i<=faces.Extent();++i){
            const auto source=TopoDS::Face(faces(i));std::vector<SInternalInterfacePiece> retained{{source,i,false,false}};
            std::vector<SInternalInterfacePiece> internal;
            if(machining[i]&&!interfaces.empty()){
                const BRepAdaptor_Surface surface(source,true);
                if(surface.GetType()==GeomAbs_Plane){
                    auto normal=gp_Vec(surface.Plane().Axis().Direction());if(source.Orientation()==TopAbs_REVERSED)normal.Reverse();
                    for(const int anchorIndex:anchors){
                        const auto anchor=TopoDS::Face(faces(anchorIndex));const BRepAdaptor_Surface other(anchor,true);
                        if(other.GetType()!=GeomAbs_Plane||surface.Plane().Distance(other.Plane().Location())>Precision::Confusion())continue;
                        auto otherNormal=gp_Vec(other.Plane().Axis().Direction());if(anchor.Orientation()==TopAbs_REVERSED)otherNormal.Reverse();
                        if(normal.Dot(otherNormal)>-1+1e-12)continue;
                        std::vector<SInternalInterfacePiece> remainder;
                        for(const auto& piece:retained){
                            TopTools_ListOfShape arguments,tools;arguments.Append(piece.Face);tools.Append(anchor);
                            BRepAlgoAPI_Common common;common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.Build();
                            if(!common.IsDone()||!BRepCheck_Analyzer(common.Shape(),true).IsValid())throw std::runtime_error("Internal-interface finite-domain intersection failed");
                            const double area=InterfaceArea(piece.Face),overlap=InterfaceArea(common.Shape());const double budget=1e-6*std::max(1.,area);
                            if(overlap<=0){remainder.push_back(piece);continue;}
                            BRepAlgoAPI_Cut cut;cut.SetArguments(arguments);cut.SetTools(tools);cut.SetNonDestructive(true);cut.Build();
                            if(!cut.IsDone()||!BRepCheck_Analyzer(cut.Shape(),true).IsValid())throw std::runtime_error("Internal-interface finite-domain remainder failed");
                            const auto overlapFaces=collect(common.Shape(),source),outsideFaces=collect(cut.Shape(),source);
                            const double outside=InterfaceArea(cut.Shape());
                            // Area tolerances verify the result; they never erase
                            // a valid positive remainder or authorize a whole-face reclassification.
                            if(!piece.Derived&&outsideFaces.empty()){remainder.push_back(piece);continue;}
                            if(overlapFaces.empty()||std::abs(area-overlap-outside)>budget)
                                throw std::runtime_error("Internal-interface partition does not conserve the complete parent-face area");
                            BRepAlgoAPI_Common duplicate;TopTools_ListOfShape overlapArguments,overlapTools;overlapArguments.Append(common.Shape());overlapTools.Append(cut.Shape());
                            duplicate.SetArguments(overlapArguments);duplicate.SetTools(overlapTools);duplicate.SetNonDestructive(true);duplicate.Build();
                            if(!duplicate.IsDone()||InterfaceArea(duplicate.Shape())>budget)throw std::runtime_error("Internal-interface partition contains overlapping fragment interiors");
                            auto anchors=piece.AnchorIndices;anchors.insert(anchorIndex);
                            for(const auto& face:overlapFaces)internal.push_back({face,i,true,true,anchors});
                            for(const auto& face:outsideFaces)remainder.push_back({face,i,false,true,anchors});
                            changed=true;
                        }retained=std::move(remainder);
                    }
                }
            }
            if(!internal.empty()){
                const double sourceArea=InterfaceArea(source),budget=1e-6*std::max(1.,sourceArea);double area=0;
                BRep_Builder builder;TopoDS_Compound domains;builder.MakeCompound(domains);
                for(const auto& piece:internal){area+=InterfaceArea(piece.Face);builder.Add(domains,piece.Face);}
                for(const auto& piece:retained){area+=InterfaceArea(piece.Face);builder.Add(domains,piece.Face);}
                if(std::abs(area-sourceArea)>budget)throw std::runtime_error("Internal-interface accumulated pieces lost parent-face area");
                for(int direction=0;direction<2;++direction){TopTools_ListOfShape arguments,tools;
                    arguments.Append(direction?TopoDS_Shape(domains):TopoDS_Shape(source));tools.Append(direction?TopoDS_Shape(source):TopoDS_Shape(domains));
                    BRepAlgoAPI_Cut difference;difference.SetArguments(arguments);difference.SetTools(tools);difference.SetNonDestructive(true);difference.Build();
                    if(!difference.IsDone()||InterfaceArea(difference.Shape())>budget)throw std::runtime_error("Internal-interface fragment union differs from its complete original domain");
                }
                diagnostics.push_back("Materialized partial internal-stock interface split face="+std::to_string(i)
                +" parent area="+std::to_string(InterfaceArea(source))+" internal area="+std::to_string([&]{double a=0;for(const auto& p:internal)a+=InterfaceArea(p.Face);return a;}())
                +" machining remainder area="+std::to_string([&]{double a=0;for(const auto& p:retained)a+=InterfaceArea(p.Face);return a;}())
                +"; unchanged supports, exact finite-domain Boolean partition, bidirectional domain coverage and complete area conservation verified");
            }
            output.insert(output.end(),internal.begin(),internal.end());output.insert(output.end(),retained.begin(),retained.end());
        }return output;
    }

    void TraceInternalInterfacePieces(std::vector<SInternalInterfacePiece>& pieces,
        const TopTools_IndexedMapOfShape& parentFaces,const iCAX::GeometryData::BRepModel& source,
        std::vector<std::string>& diagnostics)
    {
        if(std::none_of(pieces.begin(),pieces.end(),[](const auto& p){return p.Derived;}))return;
        const auto built=iCAX::OpenCascade::ReconstructOpenCascadeShape(source);
        if(!built.bOK)throw std::runtime_error("Cannot reconstruct original interface lineage");
        std::map<int,std::set<std::uint64_t>> sourceFaces;
        const auto identify=[&](int parent)->const std::set<std::uint64_t>& {
            if(sourceFaces.contains(parent))return sourceFaces.at(parent);
            auto& result=sourceFaces[parent];const auto face=TopoDS::Face(parentFaces(parent));const BRepAdaptor_Surface surface(face,true);
            auto normal=gp_Vec(surface.Plane().Axis().Direction());if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();const double area=InterfaceArea(face);
            for(const auto& record:source.Faces){const auto original=built.Faces.at(record.Id);const BRepAdaptor_Surface support(original,true);
                if(support.GetType()!=GeomAbs_Plane||surface.Plane().Distance(support.Plane().Location())>Precision::Confusion())continue;
                auto other=gp_Vec(support.Plane().Axis().Direction());if(record.Orientation==iCAX::GeometryData::ETopologyOrientation::Reversed)other.Reverse();if(normal.Dot(other)<1-1e-12)continue;
                BRepAlgoAPI_Common common;TopTools_ListOfShape arguments,tools;arguments.Append(face);tools.Append(original);common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.Build();
                if(common.IsDone()&&BRepCheck_Analyzer(common.Shape(),true).IsValid()&&std::abs(InterfaceArea(common.Shape())-area)<=1e-6*std::max(1.,area))result.insert(record.Id);
            }
            if(result.empty())throw std::runtime_error("Derived internal-interface domain has no complete oriented original-face lineage");return result;
        };
        struct SourceEdge {std::uint64_t Id=0,Curve=0;TopoDS_Edge Edge;Bnd_Box Bounds;};std::vector<SourceEdge> sourceEdges;
        for(const auto& record:source.Edges)if(!record.Degenerated){SourceEdge edge;edge.Id=record.Id;edge.Curve=record.Curve3Id;edge.Edge=built.Edges.at(record.Id);BRepBndLib::AddOptimal(edge.Edge,edge.Bounds,false,false);sourceEdges.push_back(std::move(edge));}
        std::map<std::uint64_t,const iCAX::GeometryData::BRepWire*> originalWires;for(const auto& wire:source.Wires)originalWires[wire.Id]=&wire;
        std::map<std::uint64_t,std::set<std::uint64_t>> originalFaceEdges;
        for(const auto& face:source.Faces)for(const auto wire:face.WireIds)for(const auto& use:originalWires.at(wire)->Coedges)originalFaceEdges[face.Id].insert(use.EdgeId);
        for(auto& piece:pieces)if(piece.Derived){
            piece.SourceFaceIds=identify(piece.ParentIndex);for(const int anchor:piece.AnchorIndices){const auto& ids=identify(anchor);piece.AnchorSourceFaceIds.insert(ids.begin(),ids.end());}
            std::set<std::uint64_t> allowedEdges;for(const auto face:piece.SourceFaceIds)allowedEdges.insert(originalFaceEdges.at(face).begin(),originalFaceEdges.at(face).end());
            for(const auto face:piece.AnchorSourceFaceIds)allowedEdges.insert(originalFaceEdges.at(face).begin(),originalFaceEdges.at(face).end());
            for(TopExp_Explorer it(piece.Face,TopAbs_EDGE);it.More();it.Next()){
                const auto edge=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(edge))continue;Bnd_Box box;BRepBndLib::AddOptimal(edge,box,false,false);
                GProp_GProps length;BRepGProp::LinearProperties(edge,length);const double budget=1e-6*std::max(1.,std::abs(length.Mass()));TopTools_ListOfShape coverage;
                for(const auto& original:sourceEdges){if(!allowedEdges.contains(original.Id)||box.IsOut(original.Bounds))continue;TopTools_ListOfShape arguments,tools;arguments.Append(edge);tools.Append(original.Edge);
                    BRepAlgoAPI_Common common;common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.Build();if(!common.IsDone())continue;
                    GProp_GProps overlap;BRepGProp::LinearProperties(common.Shape(),overlap);if(std::abs(overlap.Mass())<=1e-10)continue;
                    coverage.Append(original.Edge);piece.SourceEdgeIds.insert(original.Id);if(original.Curve)piece.SourceCurveIds.insert(original.Curve);
                }
                if(coverage.IsEmpty())throw std::runtime_error("Derived interface boundary has no original-curve lineage");
                TopTools_ListOfShape arguments;arguments.Append(edge);BRepAlgoAPI_Cut uncovered;uncovered.SetArguments(arguments);uncovered.SetTools(coverage);uncovered.SetNonDestructive(true);uncovered.Build();
                if(!uncovered.IsDone())throw std::runtime_error("Cannot verify original-curve coverage of the complete interface boundary");
                GProp_GProps remainder;BRepGProp::LinearProperties(uncovered.Shape(),remainder);if(std::abs(remainder.Mass())>budget)throw std::runtime_error("Derived interface boundary is not completely covered by original curves");
            }
            diagnostics.push_back("Internal-interface piece source split face="+std::to_string(piece.ParentIndex)+" source face IDs="+InterfaceIds(piece.SourceFaceIds)
                +" anchor source face IDs="+InterfaceIds(piece.AnchorSourceFaceIds)+" original edge IDs="+InterfaceIds(piece.SourceEdgeIds)+" original curve3 IDs="+InterfaceIds(piece.SourceCurveIds)
                +" area="+std::to_string(InterfaceArea(piece.Face))+" classification="+(piece.Internal?"NonMachining":"Machining"));
        }
    }

    TopoDS_Shape CollectMachiningFaces(
        IN const TopoDS_Shape& SplitTarget_,
        IN const SSectionSupport& Support_,
        IN const SFeatureToolpathOptions& Options_,
        OUT std::vector<std::string>& Diagnostics_)
    {
        TopTools_IndexedMapOfShape _Faces;
        TopExp::MapShapes(SplitTarget_, TopAbs_FACE, _Faces);
        if (_Faces.IsEmpty())
        {
            Diagnostics_.push_back(
                "Split target has no faces to classify");
            return {};
        }

        BRep_Builder _Builder;
        TopoDS_Compound _MachiningFaces;
        _Builder.MakeCompound(_MachiningFaces);

        const auto skinFlags=ClassifyStockSkin(_Faces,Support_,Options_,Diagnostics_);
        const auto interfaces=InternalStockInterfaceFaces(_Faces,Support_,Options_,Diagnostics_,&skinFlags);
        std::size_t _SkinFaceCount = 0;
        std::size_t _MachiningFaceCount = 0;
        for (int _Index = 1; _Index <= _Faces.Extent(); ++_Index)
        {
            const auto _Face = TopoDS::Face(_Faces(_Index));
            // Geometric boundaries include every positive-area non-stock face,
            // including blind-pocket floors. Tool accessibility is a CAM rule.
            if (interfaces.contains(_Index) || skinFlags[_Index] || IsZeroAreaTrimFace(_Face))
            {
                ++_SkinFaceCount;
                continue;
            }
            _Builder.Add(_MachiningFaces, _Face);
            ++_MachiningFaceCount;
        }

        if (_MachiningFaceCount == 0)
        {
            Diagnostics_.push_back(
                "No machining face remains after skin-face filtering");
            return {};
        }

        Diagnostics_.push_back(
            "Collected " + std::to_string(_MachiningFaceCount)
            + " machining faces; skipped "
            + std::to_string(_SkinFaceCount)
            + " skin faces");
        return _MachiningFaces;
    }

    TopoDS_Shape PrepareShape(
        IN const TopoDS_Shape& Source_,
        IN const SFeatureToolpathOptions& Options_,
        OUT std::vector<std::string>& Diagnostics_)
    {
        if (Source_.IsNull()) return {};
        // Classification consumes the original trimmed faces. Sewing, healing
        // and same-domain unification can change their boundaries and can
        // crash on otherwise readable IGES faces. They are not prerequisites
        // for introducing section seams. Work on an independent copy so the
        // splitter cannot change the caller's original edge tolerances.
        auto _Shape = iCAX::OpenCascade::ConvertOpenCascadeSweptToElementary(
            BRepBuilderAPI_Copy(Source_, true, false).Shape());
        // A revolved line/circle may be exactly a cylinder, sphere or torus.
        // Canonicalizing that support preserves its trims and avoids generic
        // revolution projection failures. No faces are joined or sewn.
        if (!BRepCheck_Analyzer(_Shape, true, true).IsValid())
        {
            Diagnostics_.push_back(
                "Source OCC target shape is not fully valid before face splitting");
        }
        return _Shape;
    }

    TopoDS_Shape ValidateSplitShape(
        IN const TopoDS_Shape& Source_,
        IN const SFeatureToolpathOptions& Options_,
        OUT std::vector<std::string>& Diagnostics_, bool* validity=nullptr)
    {
        if(validity)*validity=false;
        if (Source_.IsNull())
        {
            return {};
        }
        auto _Shape = Source_;
        // Validate without changing original pcurves or tolerance flags.
        // SameParameter(force=true) can invalidate a source face even when
        // no split was required. Newly created edges are parameterized where
        // they are constructed, rather than rewriting every source edge here.
        const bool valid=BRepCheck_Analyzer(_Shape, true, true).IsValid();
        if(validity)*validity=valid;
        if (!valid)
        {
            Diagnostics_.push_back(
                "OCC split result is not fully valid");
        }
        return _Shape;
    }

    TopoDS_Shape PrepareSectionSplit(const iCAX::GeometryData::BRepModel& Geometry_,
        const SSectionWiresResult& Section_, const SFeatureToolpathOptions& Options_,
        std::vector<std::string>& diagnostics,bool* validity=nullptr)
    {
    if(validity)*validity=false;
    if(Options_.Progress)Options_.Progress("classify build neutral source");
    const auto _Built = iCAX::OpenCascade::ReconstructOpenCascadeShape(Geometry_);
    if (!_Built.bOK || _Built.Shape.IsNull())
    {
        diagnostics.insert(
            diagnostics.end(),
            _Built.Diagnostics.begin(),
            _Built.Diagnostics.end());
        diagnostics.push_back(
            "Neutral BRep could not be translated to an OCC shape");
        return {};
    }

        // Preserve the source boundaries before introducing contour seams.
    if(Options_.Progress)Options_.Progress("classify prepare source topology");
    const auto _PreparedTarget = PrepareShape(
        _Built.Shape,
        Options_,
        diagnostics);
    if (_PreparedTarget.IsNull())
    {
        diagnostics.push_back(
            "Neutral target BRep could not be prepared for face splitting");
        return {};
    }

    if(Options_.Progress)Options_.Progress("classify split source faces");
    const auto _SplitTarget = SplitTargetBySectionVertices(
        _PreparedTarget,
        Section_,
        Options_,
        diagnostics);
    if (_SplitTarget.IsNull())
    {
        return {};
    }

    if(Options_.Progress)Options_.Progress("classify validate split topology");
    const auto _SplitShape = ValidateSplitShape(
        _SplitTarget,
        Options_,
        diagnostics,validity);
    if (_SplitShape.IsNull())
    {
        diagnostics.push_back(
            "OCC face-split result is null");
        return {};
    }

        return _SplitShape;
    }

    struct STask final
    {
        int FaceIndex = 0;
        int EntryEdgeIndex = 0;
    };

    std::uint64_t MakePathKey(IN const int FaceIndex_, IN const int EdgeIndex_)
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(FaceIndex_)) << 32)
            | static_cast<std::uint32_t>(EdgeIndex_);
    }

    std::optional<SFeatureToolpath> BuildPath(
        IN const SFaceContext& Face_,
        IN const std::vector<SEdgeContext>& Edges_,
        IN const SFaceSegmentAnalysis& SegmentAnalysis_,
        IN const SSeg& DriveSegment_,
        IN const std::vector<const SSeg*>& InnerSegments_,
        IN const SFeatureToolpathOptions& Options_,
        IN const bool FromInternal_)
    {
        if (DriveSegment_.EdgeIndices.empty()
            || DriveSegment_.Samples.size() < 2)
        {
            return std::nullopt;
        }

        SFeatureToolpath _Path;
        _Path.EntryEdgeId = static_cast<std::uint64_t>(
            DriveSegment_.EdgeIndices.front());
        _Path.bFromInternalFace = FromInternal_;
        _Path.bClosed = DriveSegment_.bClosed;

        bool _HasCurvedSide = false;
        // Role decisions are made from SSegs.  BoundaryEdges remains a
        // per-TopoDS_Edge diagnostic/output list, but it no longer controls
        // whether a logical side is curved or whether an inner boundary
        // exists.
        for (const auto& _Segments : SegmentAnalysis_.WireSegments)
        {
            for (const auto& _Segment : _Segments)
            {
                if (ClassifySegment(_Segment) == EFeatureEdgeRole::Side
                    && !_Segment.bGeometricallyStraight)
                {
                    _HasCurvedSide = true;
                }
            }
        }

        for (const auto _EdgeIndex : Face_.EdgeIndices)
        {
            const auto& _Edge = Edges_[static_cast<std::size_t>(_EdgeIndex)];
            SFeatureEdgeInfo _Info;
            _Info.EdgeId = static_cast<std::uint64_t>(_EdgeIndex);
            _Info.Role = ClassifyEdge(_Edge);
            _Info.bOnSkin = _Edge.bOnSkin;
            _Info.bCanIn = _Edge.bCanIn;
            _Info.bStartOnSkin = _Edge.bStartOnSkin;
            _Info.bEndOnSkin = _Edge.bEndOnSkin;
            _Info.bGeometricallyStraight = _Edge.bStraight;
            _Info.bFree = _Edge.bFree;
            _Info.bConvexToAdjacent = _Edge.bConvex;
            _Info.bConcaveToAdjacent = _Edge.bConcave;
            _Info.Samples.clear();
            _Info.Samples.reserve(_Edge.Samples.size());
            for (const auto& _Point : _Edge.Samples)
            {
                _Info.Samples.push_back(ToPoint3(_Point));
            }
            if (_Info.Role == EFeatureEdgeRole::Side && !_Edge.bStraight)
            {
                _HasCurvedSide = true;
            }
            _Path.BoundaryEdges.push_back(std::move(_Info));
        }

        if (_HasCurvedSide)
        {
            _Path.CutMode = EFeatureCutMode::Mark;
        }
        else if (!InnerSegments_.empty())
        {
            const auto* _Exit = InnerSegments_.front();
            if (_Exit == nullptr || _Exit->EdgeIndices.empty())
            {
                return std::nullopt;
            }
            _Path.ExitEdgeId = static_cast<std::uint64_t>(
                _Exit->EdgeIndices.front());
            if (_Exit->bFree || _Exit->bConcave)
            {
                _Path.CutMode = EFeatureCutMode::Partial;
            }
            else if (_Exit->bConvex)
            {
                _Path.CutMode = EFeatureCutMode::Through;
            }
            else
            {
                _Path.CutMode = EFeatureCutMode::Partial;
            }
        }
        else
        {
            _Path.CutMode = FromInternal_
                ? EFeatureCutMode::Partial
                : EFeatureCutMode::Through;
        }

        if (_Path.CutMode == EFeatureCutMode::Mark && !Options_.bEmitMarkPaths)
        {
            return std::nullopt;
        }

        const auto _SampleCount = DriveSegment_.Samples.size();
        const auto _Center = AverageBoundaryCenter(Face_, Edges_);
        _Path.Poses.reserve(_SampleCount);
        for (std::size_t _Index = 0; _Index < _SampleCount; ++_Index)
        {
            const auto _Feed = EstimateFeed(DriveSegment_.Samples, _Index);
            if (!_Feed)
            {
                continue;
            }
            const auto _Ruling = FaceRulingAt(
                Face_,
                DriveSegment_.Samples[_Index],
                *_Feed,
                _Center);
            if (!_Ruling)
            {
                continue;
            }

            Direction3 _IJK;
            Direction3 _FeedDirection;
            if (!TryDirection(*_Ruling, _IJK)
                || !TryDirection(*_Feed, _FeedDirection))
            {
                continue;
            }
            Direction3 _Normal;
            if (!Face_.bHasNormal || !TryDirection(ToVec(Face_.Normal), _Normal))
            {
                _Normal = { 0.0, 0.0, 1.0 };
            }

            double _Depth = 0.0;
            for (const auto* _Inner : InnerSegments_)
            {
                if (_Inner == nullptr)
                {
                    continue;
                }
                for (const auto& _Point : _Inner->Samples)
                {
                    _Depth = std::max(
                        _Depth,
                        std::max(0.0, _Ruling->Dot(
                            gp_Vec(DriveSegment_.Samples[_Index], _Point))));
                }
            }
            _Path.Poses.push_back({
                ToPoint3(DriveSegment_.Samples[_Index]),
                _IJK,
                _FeedDirection,
                _Normal,
                _SampleCount <= 1
                    ? 0.0
                    : static_cast<double>(_Index)
                        / static_cast<double>(_SampleCount - 1),
                _Depth });
        }
        return _Path.Poses.empty()
            ? std::nullopt
            : std::optional<SFeatureToolpath>(std::move(_Path));
    }
}

SSectionFaceClassificationResult ClassifySectionFaces(
    const iCAX::GeometryData::BRepModel& geometry, const SSectionWiresResult& section,
    const SFeatureToolpathOptions& options)
{
    SSectionFaceClassificationResult result;
    if (!IsValidOptions(options) || !section.bSuccess || section.Wires.empty()) {
        result.Diagnostics.push_back("Valid options and section contours are required");
        return result;
    }
    bool splitValid=false;
    auto split = PrepareSectionSplit(geometry, section, options, result.Diagnostics,&splitValid);
    if (split.IsNull()) return result;
    const auto support = BuildSectionSupport(section);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(split, TopAbs_FACE, faces);
    std::map<int,SZeroAreaTrimEvidence> zeroTrimEvidence;
    std::map<int,std::array<double,4>> zeroTrimDomains;
    for(int i=1;i<=faces.Extent();++i){SZeroAreaTrimEvidence evidence;
        if(!IsZeroAreaTrimFace(TopoDS::Face(faces(i)),&evidence))continue;
        double u,U,v,V;BRepTools::UVBounds(TopoDS::Face(faces(i)),u,U,v,V);zeroTrimEvidence[i]=evidence;zeroTrimDomains[i]={u,U,v,V};
        result.Diagnostics.push_back("Non-machining zero-area trim source split face="+std::to_string(i)+" proof="+evidence.Proof
            +" UV=["+ZeroTrimNumber(u)+","+ZeroTrimNumber(U)+","+ZeroTrimNumber(v)+","+ZeroTrimNumber(V)+"] wires="+std::to_string(evidence.nWireCount)
            +" coedge uses="+std::to_string(evidence.nEdgeUseCount)+" UV roundoff budget="+ZeroTrimNumber(evidence.dUVRoundoffBudget)
            +" maximum UV endpoint gap="+ZeroTrimNumber(evidence.dMaximumEndpointUVGap)+" UV area representation bound="+ZeroTrimNumber(evidence.dUVAreaRepresentationBound)
            +"; original face, support and trims retained in SplitGeometry");
    }
    if(options.Progress)options.Progress("classify geometric stock skin");
    std::vector<Bnd_Box> exactBounds;
    const auto skinFlags=ClassifyStockSkin(faces,support,options,result.Diagnostics,&exactBounds);
    if(options.Progress)options.Progress("classify stock skin and exact bounds ready");
    const auto interfaces=InternalStockInterfaceFaces(faces,support,options,result.Diagnostics,&skinFlags,&exactBounds);
    std::vector<bool> parentMachining(faces.Extent()+1,false);
    // "Machining" denotes all real boundary geometry not recoverable from the
    // nominal extrusion skin; it does not encode laser/milling accessibility.
    for(int i=1;i<=faces.Extent();++i)parentMachining[i]=!interfaces.contains(i)&&!skinFlags[i]&&!zeroTrimEvidence.contains(i);
    std::vector<SInternalInterfacePiece> interfacePieces;
    std::vector<bool> refinedMachining;
    std::map<int,std::size_t> refinedPieces;
    std::map<int,int> refinedParents;
    bool interfacesRefined=false;
    try {
        const auto parents=faces;
        interfacePieces=RefineInternalInterfaceDomains(parents,interfaces,parentMachining,result.Diagnostics,interfacesRefined);
        if(interfacesRefined){
            TraceInternalInterfacePieces(interfacePieces,parents,geometry,result.Diagnostics);
            BRep_Builder refinedBuilder;TopoDS_Compound refined;refinedBuilder.MakeCompound(refined);
            for(const auto& piece:interfacePieces)refinedBuilder.Add(refined,piece.Face);
            TopTools_IndexedMapOfShape refinedFaces;TopExp::MapShapes(refined,TopAbs_FACE,refinedFaces);
            if(refinedFaces.Extent()!=int(interfacePieces.size())||!BRepCheck_Analyzer(refined,true).IsValid()
                ||std::abs(InterfaceArea(refined)-InterfaceArea(split))>1e-6*std::max(1.,InterfaceArea(split)))
                throw std::runtime_error("Internal-interface refinement lost an original occurrence, validity or complete model area");
            refinedMachining.resize(refinedFaces.Extent()+1,false);
            for(std::size_t k=0;k<interfacePieces.size();++k){const auto& piece=interfacePieces[k];const int index=refinedFaces.FindIndex(piece.Face);
                refinedParents[index]=piece.ParentIndex;refinedMachining[index]=!piece.Internal&&parentMachining[piece.ParentIndex];if(piece.Derived)refinedPieces[index]=k;
            }
            split=refined;faces=refinedFaces;
        }
    }catch(const Standard_Failure& error){result.Diagnostics.push_back(std::string("Internal-interface refinement failed: ")+error.GetMessageString());return result;}
    catch(const std::exception& error){result.Diagnostics.push_back(std::string("Internal-interface refinement failed: ")+error.what());return result;}
    BRep_Builder builder;
    TopoDS_Compound machining, skin;
    builder.MakeCompound(machining); builder.MakeCompound(skin);
    int machiningCount = 0, skinCount = 0;
    std::unordered_set<int> machiningIndices;
    for (int i = 1; i <= faces.Extent(); ++i) {
        if(options.Progress)options.Progress("classify assign face="+std::to_string(i));
        const auto face = TopoDS::Face(faces(i));
        if (!(interfacesRefined?refinedMachining[i]:parentMachining[i])) { builder.Add(skin, face); ++skinCount; }
        else { builder.Add(machining, face); machiningIndices.insert(i); ++machiningCount; }
    }
    // Convert each exact split surface/trim once. The two partitions select
    // the same neutral records and IDs instead of converting all faces again.
    std::map<std::uint64_t,std::size_t> derivedLineage;
    std::map<std::uint64_t,int> zeroTrimLineage;
    result.SplitGeometry=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepWithDisplay(
        split,"","",[&](const TopoDS_Face& face,std::uint64_t id) {
            const int index=faces.FindIndex(face);
            if(!index)throw std::runtime_error("Converted classification face is absent from the split topology");
            if(refinedPieces.contains(index))derivedLineage[id]=refinedPieces.at(index);
            const int parent=interfacesRefined?refinedParents.at(index):index;if(zeroTrimEvidence.contains(parent))zeroTrimLineage[id]=parent;
            (machiningIndices.contains(index)?result.MachiningFaceIds:result.NonMachiningFaceIds).push_back(id);
            return iCAX::GeometryData::Triangulation3{};
        });
    for(auto& face:result.SplitGeometry.Faces)if(derivedLineage.contains(face.Id)){
        const auto& piece=interfacePieces[derivedLineage.at(face.Id)];face.Metadata.SourceId=InterfaceIds(piece.SourceFaceIds);
        face.Metadata.Tags.push_back("classify.sourceFaceIds="+InterfaceIds(piece.SourceFaceIds));
        face.Metadata.Tags.push_back("classify.interfaceAnchorSourceFaceIds="+InterfaceIds(piece.AnchorSourceFaceIds));
        face.Metadata.Tags.push_back("classify.sourceEdgeIds="+InterfaceIds(piece.SourceEdgeIds));
        face.Metadata.Tags.push_back("classify.sourceCurve3Ids="+InterfaceIds(piece.SourceCurveIds));
        face.Metadata.Tags.push_back(std::string("classify.interfaceDomain=")+(piece.Internal?"internal":"machining-remainder"));
    }
    for(auto& face:result.SplitGeometry.Faces)if(zeroTrimLineage.contains(face.Id)){
        const int parent=zeroTrimLineage.at(face.Id);const auto& evidence=zeroTrimEvidence.at(parent);const auto& uv=zeroTrimDomains.at(parent);
        face.Metadata.Tags.push_back("classify.zeroAreaTrim=true");
        face.Metadata.Tags.push_back("classify.zeroAreaSourceFaceId="+std::to_string(face.Id));
        face.Metadata.Tags.push_back("classify.zeroAreaSourceSplitFaceIndex="+std::to_string(parent));
        face.Metadata.Tags.push_back("classify.zeroAreaProof="+evidence.Proof);
        face.Metadata.Tags.push_back("classify.zeroAreaUVBounds="+ZeroTrimNumber(uv[0])+","+ZeroTrimNumber(uv[1])+","+ZeroTrimNumber(uv[2])+","+ZeroTrimNumber(uv[3]));
        face.Metadata.Tags.push_back("classify.zeroAreaWireCount="+std::to_string(evidence.nWireCount));
        face.Metadata.Tags.push_back("classify.zeroAreaCoedgeUseCount="+std::to_string(evidence.nEdgeUseCount));
        face.Metadata.Tags.push_back("classify.zeroAreaUVRoundoffBudget="+ZeroTrimNumber(evidence.dUVRoundoffBudget));
        face.Metadata.Tags.push_back("classify.zeroAreaMaximumEndpointUVGap="+ZeroTrimNumber(evidence.dMaximumEndpointUVGap));
        face.Metadata.Tags.push_back("classify.zeroAreaUVAreaRepresentationBound="+ZeroTrimNumber(evidence.dUVAreaRepresentationBound));
    }
    if(machiningCount)result.MachiningGeometry=detail::SectionFaceSubset(result.SplitGeometry,result.MachiningFaceIds);
    if(skinCount)result.NonMachiningGeometry=detail::SectionFaceSubset(result.SplitGeometry,result.NonMachiningFaceIds);
    result.Diagnostics.push_back("Classified " + std::to_string(machiningCount)
        + " machining faces and " + std::to_string(skinCount) + " non-machining faces");
    // The optional interface refinement additionally verifies every new trim,
    // unchanged supporting surface, occurrence and complete area partition.
    result.bSuccess = !faces.IsEmpty() && splitValid;
    return result;
}

SFeatureToolpathResult AnalyzeFeatureToolpaths(
    IN const iCAX::GeometryData::BRepModel& Geometry_,
    IN const SSectionWiresResult& Section_,
    IN const SFeatureToolpathOptions& Options_)
{
    SFeatureToolpathResult _Result;
    if (!IsValidOptions(Options_))
    {
        _Result.Diagnostics.push_back("Feature toolpath options are invalid");
        return _Result;
    }
    if (!Section_.bSuccess || Section_.Wires.empty())
    {
        _Result.Diagnostics.push_back(
            "A valid YOZ section contour is required before feature analysis");
        return _Result;
    }

    const auto _SplitShape = PrepareSectionSplit(Geometry_, Section_, Options_, _Result.Diagnostics);
    if (_SplitShape.IsNull()) return _Result;

    const auto _Support = BuildSectionSupport(Section_);
    const auto _Shape = CollectMachiningFaces(
        _SplitShape,
        _Support,
        Options_,
        _Result.Diagnostics);
    if (_Shape.IsNull())
    {
        return _Result;
    }

    TopTools_IndexedMapOfShape _Faces;
    TopTools_IndexedMapOfShape _Edges;
    TopExp::MapShapes(_Shape, TopAbs_FACE, _Faces);
    TopExp::MapShapes(_Shape, TopAbs_EDGE, _Edges);
    if (_Faces.IsEmpty() || _Edges.IsEmpty())
    {
        _Result.Diagnostics.push_back(
            "Prepared OCC split target has no faces or edges");
        return _Result;
    }

    std::vector<SFaceContext> _FaceContexts(
        static_cast<std::size_t>(_Faces.Extent() + 1));
    for (int _Index = 1;
        _Index <= _Faces.Extent();
        ++_Index)
    {
        auto& _Face = _FaceContexts[static_cast<std::size_t>(_Index)];
        _Face.Face = TopoDS::Face(_Faces(_Index));
        _Face.bHasNormal = TryFaceNormal(_Face.Face, _Face.Normal);
        _Face.bSkinFace = IsSkinFace(
            _Face.Face,
            _Support,
            Options_);
        for (TopExp_Explorer _Explorer(_Face.Face, TopAbs_EDGE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _EdgeIndex = FindSameIndex(_Edges, _Explorer.Current());
            if (_EdgeIndex > 0
                && std::find(
                    _Face.EdgeIndices.begin(),
                    _Face.EdgeIndices.end(),
                    _EdgeIndex) == _Face.EdgeIndices.end())
            {
                _Face.EdgeIndices.push_back(_EdgeIndex);
            }
        }
    }

    TopTools_IndexedDataMapOfShapeListOfShape _EdgeFaces;
    TopExp::MapShapesAndAncestors(
        _Shape,
        TopAbs_EDGE,
        TopAbs_FACE,
        _EdgeFaces);

    std::vector<SEdgeContext> _EdgeContexts(
        static_cast<std::size_t>(_Edges.Extent() + 1));
    for (int _Index = 1;
        _Index <= _Edges.Extent();
        ++_Index)
    {
        auto& _Edge = _EdgeContexts[static_cast<std::size_t>(_Index)];
        _Edge.Edge = TopoDS::Edge(_Edges(_Index));
        _Edge.Samples = SampleEdge(
            _Edge.Edge,
            Options_.nCurveClassificationSampleCount,
            Options_.dLinearTolerance);
        if (_Edge.Samples.empty())
        {
            continue;
        }
        _Edge.bStraight = IsGeometricallyStraight(
            _Edge.Samples,
            Options_.dSideStraightDistanceTolerance);
        _Edge.bStartOnSkin = IsOnSkin(
            _Edge.Samples.front(),
            _Support,
            Options_.dSkinDistanceTolerance);
        _Edge.bEndOnSkin = IsOnSkin(
            _Edge.Samples.back(),
            _Support,
            Options_.dSkinDistanceTolerance);
        _Edge.bOnSkin = std::all_of(
            _Edge.Samples.begin(),
            _Edge.Samples.end(),
            [&](IN const auto& _Point) {
                return IsOnSkin(
                    _Point,
                    _Support,
                    Options_.dSkinDistanceTolerance);
            });
        // Skin edges are valid initial entry edges.  Inner-face entry flags
        // are added later when a through edge propagates into an adjacent
        // face.
        _Edge.bCanIn = _Edge.bOnSkin;

        if (_EdgeFaces.Contains(_Edges(_Index)))
        {
            const auto& _Ancestors = _EdgeFaces.FindFromKey(_Edges(_Index));
            for (TopTools_ListOfShape::Iterator _Iterator(_Ancestors);
                _Iterator.More();
                _Iterator.Next())
            {
                const auto _FaceIndex = FindSameIndex(_Faces, _Iterator.Value());
                if (_FaceIndex > 0)
                {
                    _Edge.FaceIndices.push_back(_FaceIndex);
                }
            }
        }
        std::sort(_Edge.FaceIndices.begin(), _Edge.FaceIndices.end());
        _Edge.FaceIndices.erase(
            std::unique(_Edge.FaceIndices.begin(), _Edge.FaceIndices.end()),
            _Edge.FaceIndices.end());
        _Edge.bFree = _Edge.FaceIndices.size() <= 1;
    }

    for (int _EdgeIndex = 1;
        _EdgeIndex <= _Edges.Extent();
        ++_EdgeIndex)
    {
        auto& _Edge = _EdgeContexts[static_cast<std::size_t>(_EdgeIndex)];
        if (_Edge.FaceIndices.size() < 2 || _Edge.Samples.size() < 2)
        {
            continue;
        }
        const auto _Tangent = gp_Vec(
            _Edge.Samples.front(),
            _Edge.Samples.back());
        if (_Tangent.SquareMagnitude() <= kEpsilon)
        {
            continue;
        }
        const auto _TangentLength = _Tangent.Magnitude();
        const auto _FirstFace = _Edge.FaceIndices[0];
        const auto _SecondFace = _Edge.FaceIndices[1];
        const auto& _A = _FaceContexts[static_cast<std::size_t>(_FirstFace)];
        const auto& _B = _FaceContexts[static_cast<std::size_t>(_SecondFace)];
        if (!_A.bHasNormal || !_B.bHasNormal)
        {
            continue;
        }
        const auto _Sign = ToVec(_A.Normal).Crossed(ToVec(_B.Normal)).Dot(_Tangent)
            / _TangentLength;
        const auto _SignTolerance = std::sin(
            Options_.dAngularToleranceRadians);
        _Edge.bConvex = _Sign > _SignTolerance;
        _Edge.bConcave = _Sign < -_SignTolerance;
    }

    for (int _Index = 1;
        _Index <= _Edges.Extent();
        ++_Index)
    {
        const auto& _Edge = _EdgeContexts[static_cast<std::size_t>(_Index)];
        SFeatureEdgeInfo _Info;
        _Info.EdgeId = static_cast<std::uint64_t>(_Index);
        _Info.Role = ClassifyEdge(_Edge);
        _Info.bOnSkin = _Edge.bOnSkin;
        _Info.bCanIn = _Edge.bCanIn;
        _Info.bStartOnSkin = _Edge.bStartOnSkin;
        _Info.bEndOnSkin = _Edge.bEndOnSkin;
        _Info.bGeometricallyStraight = _Edge.bStraight;
        _Info.bFree = _Edge.bFree;
        _Info.bConvexToAdjacent = _Edge.bConvex;
        _Info.bConcaveToAdjacent = _Edge.bConcave;
        _Info.Samples.clear();
        _Info.Samples.reserve(_Edge.Samples.size());
        for (const auto& _Point : _Edge.Samples)
        {
            _Info.Samples.push_back(ToPoint3(_Point));
        }
        _Result.EdgeInfos.push_back(std::move(_Info));
    }

    std::deque<STask> _Queue;
    std::unordered_set<std::uint64_t> _Queued;
    for (std::size_t _FaceIndex = 1;
        _FaceIndex < _FaceContexts.size();
        ++_FaceIndex)
    {
        const auto& _Face = _FaceContexts[_FaceIndex];
        if (_Face.bSkinFace)
        {
            continue;
        }
        const auto _Analysis = AnalyzeFaceSegments(
            _Face.Face,
            _Edges,
            _EdgeContexts,
            _Support,
            Options_);
        for (const auto& _Segments : _Analysis.WireSegments)
        {
            for (const auto& _Drive : _Segments)
            {
                if (!_Drive.bOnSkin)
                {
                    continue;
                }

                const auto _InnerSegments = CollectInnerSegments(
                    _Analysis,
                    &_Drive);

                auto _Path = BuildPath(
                    _Face,
                    _EdgeContexts,
                    _Analysis,
                    _Drive,
                    _InnerSegments,
                    Options_,
                    false);
                if (!_Path)
                {
                    continue;
                }
                _Path->FaceId = static_cast<std::uint64_t>(_FaceIndex);
                _Result.Toolpaths.push_back(std::move(*_Path));

                if (!Options_.bProcessInternalFaces
                    || _Result.Toolpaths.back().CutMode
                        != EFeatureCutMode::Through)
                {
                    continue;
                }

                for (const auto* _Exit : _InnerSegments)
                {
                    if (_Exit == nullptr
                        || _Exit->bFree
                        || !_Exit->bConvex
                        || _Exit->EdgeIndices.empty())
                    {
                        continue;
                    }

                    // Propagate the entry marker to every fragmented edge in
                    // the logical exit segment before analyzing the adjacent
                    // face.  That adjacent face will rebuild its own SSegs
                    // and see one canIn run instead of one marker per fragment.
                    for (const auto _ExitIndex : _Exit->EdgeIndices)
                    {
                        _EdgeContexts[static_cast<std::size_t>(_ExitIndex)].bCanIn = true;
                    }

                    const auto _EntryEdgeIndex = _Exit->EdgeIndices.front();
                    const auto& _EntryEdge = _EdgeContexts[
                        static_cast<std::size_t>(_EntryEdgeIndex)];
                    for (const auto _AdjacentFace : _EntryEdge.FaceIndices)
                    {
                        if (static_cast<std::size_t>(_AdjacentFace) == _FaceIndex)
                        {
                            continue;
                        }
                        const auto _Key = MakePathKey(
                            _AdjacentFace,
                            _EntryEdgeIndex);
                        if (_Queued.insert(_Key).second)
                        {
                            _Queue.push_back({
                                _AdjacentFace,
                                _EntryEdgeIndex });
                        }
                    }
                }
            }
        }
    }

    while (Options_.bProcessInternalFaces && !_Queue.empty())
    {
        const auto _Task = _Queue.front();
        _Queue.pop_front();
        if (_Task.FaceIndex <= 0
            || static_cast<std::size_t>(_Task.FaceIndex) >= _FaceContexts.size())
        {
            continue;
        }
        const auto& _Face = _FaceContexts[static_cast<std::size_t>(_Task.FaceIndex)];
        if (_Face.bSkinFace)
        {
            continue;
        }
        const auto _Analysis = AnalyzeFaceSegments(
            _Face.Face,
            _Edges,
            _EdgeContexts,
            _Support,
            Options_);
        const auto* _Drive = FindSegmentContainingEdge(
            _Analysis,
            _Task.EntryEdgeIndex);
        if (_Drive == nullptr || !_Drive->bCanIn)
        {
            // An inner face without a propagated entry marker has no reliable
            // place to start processing.  Do not guess from geometry alone.
            continue;
        }
        const auto _InnerSegments = CollectInnerSegments(
            _Analysis,
            _Drive);

        auto _Path = BuildPath(
            _Face,
            _EdgeContexts,
            _Analysis,
            *_Drive,
            _InnerSegments,
            Options_,
            true);
        if (!_Path)
        {
            continue;
        }
        _Path->FaceId = static_cast<std::uint64_t>(_Task.FaceIndex);
        _Result.Toolpaths.push_back(std::move(*_Path));
        if (_Result.Toolpaths.back().CutMode != EFeatureCutMode::Through)
        {
            continue;
        }

        for (const auto* _Exit : _InnerSegments)
        {
            if (_Exit == nullptr
                || _Exit->bFree
                || !_Exit->bConvex
                || _Exit->EdgeIndices.empty())
            {
                continue;
            }

            for (const auto _ExitIndex : _Exit->EdgeIndices)
            {
                _EdgeContexts[static_cast<std::size_t>(_ExitIndex)].bCanIn = true;
                const auto& _ExitEdge = _EdgeContexts[
                    static_cast<std::size_t>(_ExitIndex)];
                for (const auto _AdjacentFace : _ExitEdge.FaceIndices)
                {
                    if (_AdjacentFace == _Task.FaceIndex)
                    {
                        continue;
                    }
                    const auto _Key = MakePathKey(
                        _AdjacentFace,
                        _ExitIndex);
                    if (_Queued.insert(_Key).second)
                    {
                        _Queue.push_back({ _AdjacentFace, _ExitIndex });
                    }
                }
            }
        }
    }

    _Result.bSuccess = !_Result.Toolpaths.empty();
    if (!_Result.bSuccess)
    {
        _Result.Diagnostics.push_back(
            "No feature face produced a valid OCC-derived toolpath");
    }
    return _Result;
}
}
