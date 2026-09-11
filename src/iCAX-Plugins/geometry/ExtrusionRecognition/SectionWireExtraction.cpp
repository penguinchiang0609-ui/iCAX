#include "pch.h"
#include "ExtrudeRecognizesService.h"

#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRepLib.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Bnd_Box.hxx>
#include <BOPTools_AlgoTools2D.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAPI_ExtremaCurveCurve.hxx>
#include <Geom_Line.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <NCollection_HSequence.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace iCAX::ExtrusionRecognition
{
namespace toolpath_detail
{
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
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::BSplineSurface3>)
            {
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
                const auto _Angle = Last_ ? 2.0 * kPi : 0.0;
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
                const auto _Angle = Last_ ? 2.0 * kPi : 0.0;
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

        const auto _Curve2 = FlattenCurve(Curve_);
        if (!_Curve2)
        {
            return std::nullopt;
        }

        SSectionWireEdge _Result;
        _Result.SourceEdgeId = Edge_.Id;
        _Result.Curve = *_Curve2;
        _Result.Range = Edge_.Range;
        _Result.Start = Project(*_Start3);
        _Result.End = Project(*_End3);
        _Result.Samples = SampleCurve(_Result.Curve, Options_.nCurveSampleCount);
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
        return SameSamples(Left_.Samples, Right_.Samples, false, Tolerance_)
            || SameSamples(Left_.Samples, Right_.Samples, true, Tolerance_);
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
        const auto _U = std::clamp(Parameter_, 0.0, 1.0);
        std::optional<Point2> _Result;
        std::visit([&](IN const auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment2>)
            {
                _Result = Lerp(Value_.Start, Value_.End, _U);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>)
            {
                const auto _Angle = 2.0 * kPi * _U;
                _Result = Point2{
                    Value_.Placement.Location.X
                        + Value_.Radius * (Value_.Placement.XDirection.X * std::cos(_Angle)
                            + Value_.Placement.YDirection.X * std::sin(_Angle)),
                    Value_.Placement.Location.Y
                        + Value_.Radius * (Value_.Placement.XDirection.Y * std::cos(_Angle)
                            + Value_.Placement.YDirection.Y * std::sin(_Angle)) };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
            {
                const auto _Delta = Value_.CounterClockwise
                    ? Value_.EndAngle - Value_.StartAngle
                    : Value_.StartAngle - Value_.EndAngle;
                const auto _Angle = Value_.StartAngle + _Delta * _U;
                _Result = Point2{
                    Value_.Basis.Placement.Location.X
                        + Value_.Basis.Radius * (Value_.Basis.Placement.XDirection.X * std::cos(_Angle)
                            + Value_.Basis.Placement.YDirection.X * std::sin(_Angle)),
                    Value_.Basis.Placement.Location.Y
                        + Value_.Basis.Radius * (Value_.Basis.Placement.XDirection.Y * std::cos(_Angle)
                            + Value_.Basis.Placement.YDirection.Y * std::sin(_Angle)) };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
            {
                const auto _Angle = 2.0 * kPi * _U;
                _Result = Point2{
                    Value_.Placement.Location.X
                        + Value_.MajorRadius * Value_.Placement.XDirection.X * std::cos(_Angle)
                        + Value_.MinorRadius * Value_.Placement.YDirection.X * std::sin(_Angle),
                    Value_.Placement.Location.Y
                        + Value_.MajorRadius * Value_.Placement.XDirection.Y * std::cos(_Angle)
                        + Value_.MinorRadius * Value_.Placement.YDirection.Y * std::sin(_Angle) };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc2>)
            {
                const auto _Delta = Value_.CounterClockwise
                    ? Value_.EndAngle - Value_.StartAngle
                    : Value_.StartAngle - Value_.EndAngle;
                const auto _Angle = Value_.StartAngle + _Delta * _U;
                _Result = Point2{
                    Value_.Basis.Placement.Location.X
                        + Value_.Basis.MajorRadius * Value_.Basis.Placement.XDirection.X * std::cos(_Angle)
                        + Value_.Basis.MinorRadius * Value_.Basis.Placement.YDirection.X * std::sin(_Angle),
                    Value_.Basis.Placement.Location.Y
                        + Value_.Basis.MajorRadius * Value_.Basis.Placement.XDirection.Y * std::cos(_Angle)
                        + Value_.Basis.MinorRadius * Value_.Basis.Placement.YDirection.Y * std::sin(_Angle) };
            }
        }, Edge_.Curve);
        return _Result ? _Result : std::optional<Point2>(PointOnSamples(Edge_.Samples, _U));
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
        const auto _U = std::clamp(Parameter_, 0.0, 1.0);
        std::optional<Point2> _Result;
        std::visit([&](IN const auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment2>)
            {
                _Result = Point2{
                    Value_.End.X - Value_.Start.X,
                    Value_.End.Y - Value_.Start.Y };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>)
            {
                const auto _Angle = 2.0 * kPi * _U;
                _Result = Point2{
                    2.0 * kPi * Value_.Radius * (
                        -Value_.Placement.XDirection.X * std::sin(_Angle)
                        + Value_.Placement.YDirection.X * std::cos(_Angle)),
                    2.0 * kPi * Value_.Radius * (
                        -Value_.Placement.XDirection.Y * std::sin(_Angle)
                        + Value_.Placement.YDirection.Y * std::cos(_Angle)) };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
            {
                const auto _Delta = Value_.CounterClockwise
                    ? Value_.EndAngle - Value_.StartAngle
                    : Value_.StartAngle - Value_.EndAngle;
                const auto _Angle = Value_.StartAngle + _Delta * _U;
                _Result = Point2{
                    _Delta * Value_.Basis.Radius * (
                        -Value_.Basis.Placement.XDirection.X * std::sin(_Angle)
                        + Value_.Basis.Placement.YDirection.X * std::cos(_Angle)),
                    _Delta * Value_.Basis.Radius * (
                        -Value_.Basis.Placement.XDirection.Y * std::sin(_Angle)
                        + Value_.Basis.Placement.YDirection.Y * std::cos(_Angle)) };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
            {
                const auto _Angle = 2.0 * kPi * _U;
                _Result = Point2{
                    2.0 * kPi * (
                        -Value_.MajorRadius * Value_.Placement.XDirection.X * std::sin(_Angle)
                        + Value_.MinorRadius * Value_.Placement.YDirection.X * std::cos(_Angle)),
                    2.0 * kPi * (
                        -Value_.MajorRadius * Value_.Placement.XDirection.Y * std::sin(_Angle)
                        + Value_.MinorRadius * Value_.Placement.YDirection.Y * std::cos(_Angle)) };
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc2>)
            {
                const auto _Delta = Value_.CounterClockwise
                    ? Value_.EndAngle - Value_.StartAngle
                    : Value_.StartAngle - Value_.EndAngle;
                const auto _Angle = Value_.StartAngle + _Delta * _U;
                _Result = Point2{
                    _Delta * (
                        -Value_.Basis.MajorRadius * Value_.Basis.Placement.XDirection.X * std::sin(_Angle)
                        + Value_.Basis.MinorRadius * Value_.Basis.Placement.YDirection.X * std::cos(_Angle)),
                    _Delta * (
                        -Value_.Basis.MajorRadius * Value_.Basis.Placement.XDirection.Y * std::sin(_Angle)
                        + Value_.Basis.MinorRadius * Value_.Basis.Placement.YDirection.Y * std::cos(_Angle)) };
            }
        }, Edge_.Curve);
        if (_Result)
        {
            return NormalizePoint2(*_Result);
        }

        const auto _Step = 1.0e-4;
        const auto _Left = std::max(0.0, _U - _Step);
        const auto _Right = std::min(1.0, _U + _Step);
        const auto _LeftPoint = EvaluateProjectedCurvePoint(Edge_, _Left).value_or(Edge_.Start);
        const auto _RightPoint = EvaluateProjectedCurvePoint(Edge_, _Right).value_or(Edge_.End);
        return NormalizePoint2({
            _RightPoint.X - _LeftPoint.X,
            _RightPoint.Y - _LeftPoint.Y });
    }

    struct SLowestCurvePoint final
    {
        double U = 0.0;
        Point2 Point;
        Point2 Tangent;
    };

    SLowestCurvePoint FindLowestCurvePoint(IN const SSectionWireEdge& Edge_)
    {
        std::vector<double> _Candidates{ 0.0, 1.0 };
        const auto _Stationary = StationaryParameters(Edge_);
        _Candidates.insert(_Candidates.end(), _Stationary.begin(), _Stationary.end());
        if (_Stationary.empty() && Edge_.Samples.size() > 2)
        {
            const auto _Minimum = std::min_element(
                Edge_.Samples.begin(),
                Edge_.Samples.end(),
                [](IN const auto& Left_, IN const auto& Right_) {
                    return Left_.Y < Right_.Y;
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
            if (_Point.Y < _Result.Point.Y - 1.0e-9)
            {
                _Result = { _U, _Point, CurveTangentAt(Edge_, _U) };
            }
        }
        return _Result;
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
        if (const auto _Analytic = ProjectPointToAnalyticCurve(Edge_, Point_, Tolerance_))
        {
            return _Analytic;
        }
        auto _Result = ProjectPointToSamples(Edge_, Point_);
        return _Result.dDistance <= Tolerance_ ? std::optional<SProjectionResult>(_Result) : std::nullopt;
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
            _Result.push_back(PointOnSamples(Edge_.Samples, _U));
        }
        _Result.front() = PointOnSamples(Edge_.Samples, StartU_);
        _Result.back() = PointOnSamples(Edge_.Samples, EndU_);
        return _Result;
    }

    std::vector<SSectionWireEdge> SplitProjectedEdge(
        IN const SSectionWireEdge& Edge_,
        IN std::vector<double> Cuts_,
        IN const SSectionWireOptions& Options_)
    {
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
            _Piece.Start = PointOnSamples(Edge_.Samples, _StartU);
            _Piece.End = PointOnSamples(Edge_.Samples, _EndU);
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

    double SignedCurvatureAtNode(
        IN const SGraphEdge& Edge_,
        IN std::size_t Node_)
    {
        std::optional<double> _Curvature;
        std::visit([&](IN const auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            const auto _BasisOrientation = [&](IN const auto& Placement_) {
                return Placement_.XDirection.X * Placement_.YDirection.Y
                    - Placement_.XDirection.Y * Placement_.YDirection.X;
            };
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle2>)
            {
                _Curvature = _BasisOrientation(Value_.Placement)
                    / std::max(Value_.Radius, kEpsilon);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc2>)
            {
                const auto _Delta = Value_.CounterClockwise
                    ? Value_.EndAngle - Value_.StartAngle
                    : Value_.StartAngle - Value_.EndAngle;
                _Curvature = _BasisOrientation(Value_.Basis.Placement)
                    * (_Delta < 0.0 ? -1.0 : 1.0)
                    / std::max(Value_.Basis.Radius, kEpsilon);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse2>)
            {
                _Curvature = _BasisOrientation(Value_.Placement)
                    / std::max(Value_.MajorRadius * Value_.MinorRadius, kEpsilon);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc2>)
            {
                const auto _Delta = Value_.CounterClockwise
                    ? Value_.EndAngle - Value_.StartAngle
                    : Value_.StartAngle - Value_.EndAngle;
                _Curvature = _BasisOrientation(Value_.Basis.Placement)
                    * (_Delta < 0.0 ? -1.0 : 1.0)
                    / std::max(
                        Value_.Basis.MajorRadius * Value_.Basis.MinorRadius,
                        kEpsilon);
            }
        }, Edge_.Projected.Curve);

        if (_Curvature)
        {
            return Node_ == Edge_.StartNode ? *_Curvature : -*_Curvature;
        }

        // Fallback for free-form curves: estimate the signed change of the
        // tangent using the first two directed sample segments.  This is only
        // used when an analytic curvature is unavailable; line edges return
        // zero exactly.
        auto _Samples = Edge_.Projected.Samples;
        if (Node_ != Edge_.StartNode)
        {
            std::reverse(_Samples.begin(), _Samples.end());
        }
        if (_Samples.size() < 3)
        {
            return 0.0;
        }
        const Point2 _First{
            _Samples[1].X - _Samples[0].X,
            _Samples[1].Y - _Samples[0].Y };
        const Point2 _Second{
            _Samples[2].X - _Samples[1].X,
            _Samples[2].Y - _Samples[1].Y };
        if (std::hypot(_First.X, _First.Y) <= kEpsilon
            || std::hypot(_Second.X, _Second.Y) <= kEpsilon)
        {
            return 0.0;
        }
        return std::atan2(Cross2(_First, _Second), Dot2(_First, _Second));
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
        auto _Samples = GraphEdges_[HalfEdge_.GraphEdge].Projected.Samples;
        if (!HalfEdge_.bForward)
        {
            std::reverse(_Samples.begin(), _Samples.end());
        }
        for (std::size_t _Index = 1; _Index < _Samples.size(); ++_Index)
        {
            const Point2 _Delta{
                _Samples[_Index].X - _Samples[0].X,
                _Samples[_Index].Y - _Samples[0].Y };
            const auto _Length = std::hypot(_Delta.X, _Delta.Y);
            if (_Length > kEpsilon)
            {
                return { _Delta.X / _Length, _Delta.Y / _Length };
            }
        }
        const auto& _From = Nodes_[HalfEdge_.FromNode].Position;
        const auto& _To = Nodes_[HalfEdge_.ToNode].Position;
        const Point2 _Delta{ _To.X - _From.X, _To.Y - _From.Y };
        const auto _Length = std::hypot(_Delta.X, _Delta.Y);
        return _Length <= kEpsilon
            ? Point2{ 1.0, 0.0 }
            : Point2{ _Delta.X / _Length, _Delta.Y / _Length };
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
    };

    void AppendSamples(
        IN const SSectionWireEdge& Edge_,
        IN OUT std::vector<Point2>& Points_);

    double SignedArea(IN const std::vector<Point2>& Points_);

    std::optional<SLoopCandidate> TraceFace(
        IN std::size_t StartHalfEdge_,
        IN OUT std::vector<SHalfEdge>& HalfEdges_,
        IN const SSectionWireOptions& Options_,
        IN const std::vector<SGraphNode>& Nodes_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN bool bLeftFace_)
    {
        SLoopCandidate _Result;
        auto _Current = StartHalfEdge_;
        for (std::size_t _Step = 0; _Step <= HalfEdges_.size(); ++_Step)
        {
            if (_Current >= HalfEdges_.size() || HalfEdges_[_Current].bVisited)
            {
                return std::nullopt;
            }
            auto& _HalfEdge = HalfEdges_[_Current];
            _HalfEdge.bVisited = true;
            const auto& _GraphEdge = GraphEdges_[_HalfEdge.GraphEdge];
            auto _Projected = _GraphEdge.Projected;
            if (!_HalfEdge.bForward)
            {
                std::swap(_Projected.Start, _Projected.End);
                std::reverse(_Projected.Samples.begin(), _Projected.Samples.end());
            }
            _Projected.bReversed = !_HalfEdge.bForward;
            _Result.Edges.push_back(std::move(_Projected));
            _Result.GraphEdgeIndices.push_back(_HalfEdge.GraphEdge);
            AppendSamples(_Result.Edges.back(), _Result.Points);

            _Current = NextLeftFaceHalfEdge(
                _Current,
                HalfEdges_,
                Nodes_,
                GraphEdges_,
                bLeftFace_);
            if (_Current == (std::numeric_limits<std::size_t>::max)())
            {
                return std::nullopt;
            }
            if (_Current == StartHalfEdge_)
            {
                if (_Result.Points.size() > 1
                    && SamePoint(
                        _Result.Points.front(),
                        _Result.Points.back(),
                        Options_.dConnectionTolerance))
                {
                    _Result.Points.pop_back();
                }
                _Result.dSignedArea = SignedArea(_Result.Points);
                return std::abs(_Result.dSignedArea)
                        > Options_.dConnectionTolerance * Options_.dConnectionTolerance
                    ? std::optional<SLoopCandidate>(std::move(_Result))
                    : std::nullopt;
            }
        }
        return std::nullopt;
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
                    if (std::abs(_LeftAngle - _RightAngle) > 1.0e-12)
                    {
                        return _LeftAngle < _RightAngle;
                    }
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
        for (std::size_t _HalfEdgeIndex = 0;
            _HalfEdgeIndex < HalfEdges_.size();
            ++_HalfEdgeIndex)
        {
            if (HalfEdges_[_HalfEdgeIndex].bVisited)
            {
                continue;
            }
            const auto _Face = TraceFace(
                _HalfEdgeIndex,
                HalfEdges_,
                Options_,
                Nodes_,
                GraphEdges_,
                bLeftFace_);
            if (!_Face)
            {
                continue;
            }
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

    enum class EPointRelation
    {
        Outside,
        Boundary,
        Inside
    };

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

    bool CandidateContainsAllActiveEdges(
        IN const SLoopCandidate& Candidate_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN double Tolerance_)
    {
        const auto _BoundaryEdges = MakeBoundaryEdgeSet(Candidate_);
        for (std::size_t _Index = 0; _Index < GraphEdges_.size(); ++_Index)
        {
            if (!GraphEdges_[_Index].bActive || _BoundaryEdges.contains(_Index))
            {
                continue;
            }
            const auto _Points = EdgeSupportPoints(GraphEdges_[_Index]);
            for (std::size_t _PointIndex = 0; _PointIndex < _Points.size(); ++_PointIndex)
            {
                const auto _Relation = ClassifyPoint(
                    _Points[_PointIndex],
                    Candidate_.Points,
                    Tolerance_);
                if (_Relation == EPointRelation::Outside)
                {
                    return false;
                }
                if (_PointIndex + 1 < _Points.size())
                {
                    const auto _Middle = Lerp(
                        _Points[_PointIndex],
                        _Points[_PointIndex + 1],
                        0.5);
                    if (ClassifyPoint(
                            _Middle,
                            Candidate_.Points,
                            Tolerance_)
                        == EPointRelation::Outside)
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool CandidateInteriorHasLine(
        IN const SLoopCandidate& Candidate_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN double Tolerance_)
    {
        const auto _BoundaryEdges = MakeBoundaryEdgeSet(Candidate_);
        for (std::size_t _Index = 0; _Index < GraphEdges_.size(); ++_Index)
        {
            if (!GraphEdges_[_Index].bActive || _BoundaryEdges.contains(_Index))
            {
                continue;
            }
            const auto _Points = EdgeSupportPoints(GraphEdges_[_Index]);
            for (std::size_t _PointIndex = 0; _PointIndex < _Points.size(); ++_PointIndex)
            {
                if (ClassifyPoint(
                        _Points[_PointIndex],
                        Candidate_.Points,
                        Tolerance_)
                    == EPointRelation::Inside)
                {
                    return true;
                }
                if (_PointIndex + 1 < _Points.size())
                {
                    const auto _Middle = Lerp(
                        _Points[_PointIndex],
                        _Points[_PointIndex + 1],
                        0.5);
                    if (ClassifyPoint(
                            _Middle,
                            Candidate_.Points,
                            Tolerance_)
                        == EPointRelation::Inside)
                    {
                        return true;
                    }
                }
            }
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
                if (GraphEdges_[_Candidate].bUsed || _LocalEdges.contains(_Candidate))
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
                const auto _LocalTurn = SignedCurvatureAtNode(
                    GraphEdges_[_Candidate],
                    _NextNode);

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
        double dAxisAngle = 0.0;
        int nDirectionPriority = 0;
    };

    std::vector<SOuterSeed> FindOuterSeeds(
        IN const std::vector<SGraphNode>& Nodes_,
        IN const std::vector<SGraphEdge>& GraphEdges_,
        IN double Tolerance_)
    {
        if (GraphEdges_.empty())
        {
            return {};
        }
        double _LowestY = (std::numeric_limits<double>::max)();
        std::vector<SProjectedEdgeBounds> _Bounds;
        _Bounds.reserve(GraphEdges_.size());
        for (const auto& _GraphEdge : GraphEdges_)
        {
            _Bounds.push_back(GetBounds(_GraphEdge.Projected));
            _LowestY = std::min(_LowestY, _Bounds.back().MinimumY);
        }

        std::vector<SOuterSeed> _Seeds;
        for (std::size_t _EdgeIndex = 0; _EdgeIndex < GraphEdges_.size(); ++_EdgeIndex)
        {
            const auto& _GraphEdge = GraphEdges_[_EdgeIndex];
            if (_Bounds[_EdgeIndex].MinimumY > _LowestY + Tolerance_
                || _GraphEdge.StartNode == _GraphEdge.EndNode)
            {
                continue;
            }
            const auto _StartIsLowest = std::abs(
                Nodes_[_GraphEdge.StartNode].Position.Y - _LowestY) <= Tolerance_;
            const auto _EndIsLowest = std::abs(
                Nodes_[_GraphEdge.EndNode].Position.Y - _LowestY) <= Tolerance_;

            std::vector<std::size_t> _StartNodes;
            if (_StartIsLowest && !_EndIsLowest)
            {
                _StartNodes.push_back(_GraphEdge.StartNode);
            }
            else if (_EndIsLowest && !_StartIsLowest)
            {
                _StartNodes.push_back(_GraphEdge.EndNode);
            }
            else
            {
                // A horizontal bottom edge has both endpoints at the lowest
                // level. Try both directions; the axis-angle ordering below
                // gives the stable preferred seed.
                _StartNodes.push_back(_GraphEdge.StartNode);
                _StartNodes.push_back(_GraphEdge.EndNode);
            }

            // The AABB selects the lowest edge.  If its actual lowest point
            // is inside a curved edge, use the first derivative at that point
            // only to prefer the clockwise endpoint-to-endpoint direction.
            // The edge is deliberately not split and the point is not
            // inserted into the graph.
            const auto _Lowest = FindLowestCurvePoint(_GraphEdge.Projected);
            const auto _HasInteriorLowest = _Lowest.U > 1.0e-8
                && _Lowest.U < 1.0 - 1.0e-8;
            const auto _StoredDirectionIsClockwise = _Lowest.Tangent.X < -kEpsilon;
            const auto _ClockwiseStartNode = _StoredDirectionIsClockwise
                ? _GraphEdge.StartNode
                : _GraphEdge.EndNode;
            for (const auto _StartNode : _StartNodes)
            {
                const auto _Outgoing = OutgoingDirection(
                    _GraphEdge,
                    _StartNode,
                    Nodes_);
                auto _Angle = std::atan2(_Outgoing.Y, _Outgoing.X);
                while (_Angle > kPi) _Angle -= 2.0 * kPi;
                while (_Angle < -kPi) _Angle += 2.0 * kPi;
                _Seeds.push_back({
                    _EdgeIndex,
                    _StartNode,
                    std::abs(_Angle),
                    _HasInteriorLowest && _StartNode == _ClockwiseStartNode ? 0 : 1 });
            }
        }

        std::sort(_Seeds.begin(), _Seeds.end(), [](IN const auto& Left_, IN const auto& Right_) {
            if (std::abs(Left_.dAxisAngle - Right_.dAxisAngle) > 1.0e-12)
            {
                return Left_.dAxisAngle < Right_.dAxisAngle;
            }
            if (Left_.nDirectionPriority != Right_.nDirectionPriority)
            {
                return Left_.nDirectionPriority < Right_.nDirectionPriority;
            }
            if (Left_.EdgeIndex != Right_.EdgeIndex)
            {
                return Left_.EdgeIndex < Right_.EdgeIndex;
            }
            return Left_.StartNode < Right_.StartNode;
        });
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
}

SSectionWiresResult CExtrudeRecognizesService::ExtractSectionWires(
    IN const iCAX::GeometryData::BRepModel& Geometry_,
    IN const SSectionWireOptions& Options_) const
{
    SSectionWiresResult _Result;
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
    std::unordered_map<std::uint64_t, std::tuple<const BRepEdge*, std::uint64_t, std::uint64_t>> _Sources;
    for (const auto& _Face : Geometry_.Faces)
    {
        if (!IsSideFace(_Face, _Index, Options_))
        {
            continue;
        }
        _Result.SideFaceIds.push_back(_Face.Id);
        for (const auto _WireID : _Face.WireIds)
        {
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
                auto [_Iter, _Inserted] = _Sources.emplace(
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
    if (_Result.SideFaceIds.empty())
    {
        _Result.Diagnostics.push_back("No face parallel to the X axis was found");
        return _Result;
    }

    std::vector<SSectionWireEdge> _ProjectedEdges;
    _ProjectedEdges.reserve(_Sources.size());
    for (const auto& [_EdgeID, _Source] : _Sources)
    {
        const auto* _Curve = [&]() -> const Curve3* {
            const auto _Iter = _Index.Curves.find(std::get<0>(_Source)->Curve3Id);
            return _Iter == _Index.Curves.end() ? nullptr : _Iter->second;
        }();
        if (!_Curve)
        {
            continue;
        }
        const auto _Projected = MakeProjectedEdge(
            *std::get<0>(_Source),
            std::get<1>(_Source),
            std::get<2>(_Source),
            *_Curve,
            _Index,
            Options_);
        if (!_Projected)
        {
            continue;
        }
        if (std::none_of(
            _ProjectedEdges.begin(),
            _ProjectedEdges.end(),
            [&](IN const auto& _Existing) {
                return SameProjectedEdge(
                    _Existing,
                    *_Projected,
                    Options_.dConnectionTolerance);
            }))
        {
            _ProjectedEdges.push_back(*_Projected);
        }
    }
    if (_ProjectedEdges.empty())
    {
        _Result.Diagnostics.push_back("Side faces produced no non-degenerate projected edges");
        return _Result;
    }

    // The original BRep edge endpoints are not sufficient graph vertices:
    // one edge endpoint may lie in the interior of another projected edge.
    // Planarize those T-junctions before constructing the connectivity graph.
    const auto _ProjectedEdgeIndex = BuildProjectedEdgeIndex(
        _ProjectedEdges,
        Options_.dConnectionTolerance);
    std::vector<std::vector<double>> _SplitParameters(_ProjectedEdges.size());
    std::size_t _InteriorSplitCount = 0;
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

    std::vector<SGraphNode> _Nodes;
    std::vector<SGraphEdge> _GraphEdges;
    std::unordered_map<SGridKey, std::vector<std::size_t>, SGridKeyHash> _Grid;
    _Nodes.reserve(_PlanarizedEdges.size());
    _GraphEdges.reserve(_PlanarizedEdges.size());
    for (const auto& _Projected : _PlanarizedEdges)
    {
        const auto _StartNode = FindOrAddNode(
            _Projected.Start,
            Options_.dConnectionTolerance,
            _Nodes,
            _Grid);
        const auto _EndNode = FindOrAddNode(
            _Projected.End,
            Options_.dConnectionTolerance,
            _Nodes,
            _Grid);
        const auto _GraphIndex = _GraphEdges.size();
        _GraphEdges.push_back({ _Projected, _StartNode, _EndNode, false });
        _Nodes[_StartNode].IncidentEdges.push_back(_GraphIndex);
        if (_EndNode != _StartNode)
        {
            _Nodes[_EndNode].IncidentEdges.push_back(_GraphIndex);
        }
    }

    const auto _RemovedBeforeOuter = PruneDegreeOneEdges(_Nodes, _GraphEdges);
    if (_RemovedBeforeOuter > 0)
    {
        _Result.Diagnostics.push_back(
            "Pruned " + std::to_string(_RemovedBeforeOuter)
            + " degree-one edges before contour detection");
    }

    std::vector<SHalfEdge> _HalfEdges;
    BuildHalfEdges(_GraphEdges, _Nodes, _HalfEdges);
    auto _OuterCandidates = CollectFaces(
        _HalfEdges,
        Options_,
        _Nodes,
        _GraphEdges,
        true);

    std::optional<SLoopCandidate> _Outer;
    std::optional<SLoopCandidate> _ExteriorFallback;
    for (auto& _Candidate : _OuterCandidates)
    {
        const auto _ContainsAll = CandidateContainsAllActiveEdges(
            _Candidate,
            _GraphEdges,
            Options_.dConnectionTolerance);
        if (_ContainsAll)
        {
            if (!_Outer || std::abs(_Candidate.dSignedArea)
                    > std::abs(_Outer->dSignedArea))
            {
                _Outer = std::move(_Candidate);
            }
        }
        // With a concave boundary, a supplied auxiliary connection may run
        // through the re-entrant notch.  It is not part of the section
        // boundary, but it also means the containment test cannot be used as
        // a hard rejection.  The unbounded face is the clockwise (negative)
        // face in this traversal, so retain it as a topology-only fallback.
        if (!_ContainsAll && _Candidate.dSignedArea < 0.0
            && (!_ExteriorFallback
                || std::abs(_Candidate.dSignedArea)
                    > std::abs(_ExteriorFallback->dSignedArea)))
        {
            _ExteriorFallback = std::move(_Candidate);
        }
    }
    if (!_Outer && _ExteriorFallback)
    {
        _Outer = std::move(*_ExteriorFallback);
        _Result.Diagnostics.push_back(
            "Outer contour selected from the unbounded planar face because auxiliary edges crossed the boundary interior test");
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
                 _Nodes, _GraphEdges, Options_.dConnectionTolerance))
        {
            const auto _Candidate = TraceLoop(
                _Seed.EdgeIndex,
                _Seed.StartNode,
                Options_,
                _Nodes,
                _GraphEdges);
            if (!_Candidate || !CandidateContainsAllActiveEdges(
                    *_Candidate,
                    _GraphEdges,
                    Options_.dConnectionTolerance))
            {
                continue;
            }
            if (!_Outer || std::abs(_Candidate->dSignedArea)
                    > std::abs(_Outer->dSignedArea))
            {
                _Outer = std::move(*_Candidate);
            }
        }
    }
    if (!_Outer)
    {
        _Result.Diagnostics.push_back(
            "No closed contour contains all remaining projected edges as the outer contour");
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

    BuildHalfEdges(_GraphEdges, _Nodes, _HalfEdges);
    auto _InnerCandidates = CollectFaces(
        _HalfEdges,
        Options_,
        _Nodes,
        _GraphEdges,
        false);
    for (auto& _Candidate : _InnerCandidates)
    {
        if (!CandidateInteriorHasLine(
                _Candidate,
                _GraphEdges,
                Options_.dConnectionTolerance))
        {
            _Loops.push_back(std::move(_Candidate));
        }
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
        _Result.Wires.push_back(std::move(_Wire));
    }
    _Result.bSuccess = true;
    _Result.Diagnostics.push_back(
        "Projected " + std::to_string(_ProjectedEdges.size())
        + " unique edges from " + std::to_string(_Result.SideFaceIds.size())
        + " X-parallel side faces into the YOZ plane");
    _Result.Diagnostics.push_back(
        "Selected the maximum-area closed contour as outer and the remaining closed contours as inner");
    _Result.Diagnostics.push_back(
        "Contours were recovered by planar half-edge face traversal");
    return _Result;
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
        std::vector<std::pair<Point2, Point2>> Segments;
    };

    SSectionSupport BuildSectionSupport(IN const SSectionWiresResult& Section_)
    {
        SSectionSupport _Result;
        for (const auto& _Wire : Section_.Wires)
        {
            for (const auto& _Edge : _Wire.Edges)
            {
                if (_Edge.Samples.size() >= 2)
                {
                    for (std::size_t _Index = 1;
                        _Index < _Edge.Samples.size();
                        ++_Index)
                    {
                        _Result.Segments.emplace_back(
                            _Edge.Samples[_Index - 1],
                            _Edge.Samples[_Index]);
                    }
                }
                else
                {
                    _Result.Segments.emplace_back(
                        _Edge.Start,
                        _Edge.End);
                }
            }
        }
        return _Result;
    }

    bool IsOnSkin(
        IN const gp_Pnt& Point_,
        IN const SSectionSupport& Support_,
        IN const double Tolerance_)
    {
        const Point2 _Projected{ Point_.Y(), Point_.Z() };
        return std::any_of(
            Support_.Segments.begin(),
            Support_.Segments.end(),
            [&](IN const auto& _Segment) {
                return DistancePointSegment2(
                    _Projected,
                    _Segment.first,
                    _Segment.second) <= Tolerance_;
            });
    }

    std::vector<gp_Pnt> SampleEdge(
        IN const TopoDS_Edge& Edge_,
        IN const std::size_t Count_,
        IN const double Tolerance_)
    {
        std::vector<gp_Pnt> _Result;
        if (Edge_.IsNull() || BRep_Tool::Degenerated(Edge_))
        {
            return _Result;
        }

        BRepAdaptor_Curve _Curve(Edge_);
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

    void CollectFaceLineBreaks(
        IN const TopoDS_Face& Face_,
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

        for (TopExp_Explorer _Explorer(Face_, TopAbs_EDGE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Edge = TopoDS::Edge(_Explorer.Current());
            try
            {
                BRepAdaptor_Curve _Adaptor(_Edge);
                const auto _EdgeType = _Adaptor.GetType();
                if (_EdgeType == GeomAbs_Line)
                {
                    const auto _EdgeLine = _Adaptor.Line();
                    if (_EdgeLine.Direction().IsParallel(
                            Line_.Direction(),
                            Options_.dAngularToleranceRadians)
                        && _EdgeLine.Distance(Line_) <= _Tolerance)
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

                double _CurveFirst = 0.0;
                double _CurveLast = 0.0;
                const auto _Curve = BRep_Tool::Curve(
                    _Edge,
                    _CurveFirst,
                    _CurveLast);
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
        return std::any_of(
            ExistingEdgeSpans_.begin(),
            ExistingEdgeSpans_.end(),
            [&](IN const auto& _Span) {
                return First_ >= _Span.First - Tolerance_
                    && Last_ <= _Span.Last + Tolerance_;
            });
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
            std::vector<TopoDS_Edge> Edges;
            bool bFailed = false;
        };

        std::vector<SFaceSplitWork> _FaceWorks;
        std::size_t _ValidSegmentCount = 0;
        std::size_t _ExistingEdgeCount = 0;

        TopTools_IndexedMapOfShape _TargetFaces;
        TopExp::MapShapes(Target_, TopAbs_FACE, _TargetFaces);
        for (int _FaceIndex = 1;
            _FaceIndex <= _TargetFaces.Extent();
            ++_FaceIndex)
        {
            const auto _Face = TopoDS::Face(_TargetFaces(_FaceIndex));
            SFaceSplitWork _Work;
            _Work.SourceFace = _Face;

            for (const auto& _Vertex : _Vertices)
            {
                Bnd_Box _FaceBounds;
                BRepBndLib::Add(_Face, _FaceBounds);
                if (_FaceBounds.IsVoid())
                {
                    continue;
                }

                double _FaceXMin = 0.0;
                double _FaceYMin = 0.0;
                double _FaceZMin = 0.0;
                double _FaceXMax = 0.0;
                double _FaceYMax = 0.0;
                double _FaceZMax = 0.0;
                _FaceBounds.Get(
                    _FaceXMin,
                    _FaceYMin,
                    _FaceZMin,
                    _FaceXMax,
                    _FaceYMax,
                    _FaceZMax);
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
                CollectFaceLineBreaks(
                    _Face,
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

                    ++_ValidSegmentCount;
                    if (IsCoveredByExistingEdge(
                        _First,
                        _Last,
                        _ExistingEdgeSpans,
                        Options_.dLinearTolerance))
                    {
                        ++_ExistingEdgeCount;
                        continue;
                    }

                    try
                    {
                        const auto _FirstPoint = LinePoint(_Line, _First);
                        const auto _LastPoint = LinePoint(_Line, _Last);
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
                        BRep_Builder _EdgeBuilder;
                        _EdgeBuilder.UpdateEdge(
                            _Edge,
                            std::max(
                                Options_.dLinearTolerance,
                                Options_.dSkinDistanceTolerance));
                        BOPTools_AlgoTools2D::BuildPCurveForEdgeOnFace(
                            _Edge,
                            _Face);
                        BRepLib::SameParameter(
                            _Edge,
                            Options_.dLinearTolerance,
                            true);
                        if (!BOPTools_AlgoTools2D::HasCurveOnSurface(
                            _Edge,
                            _Face))
                        {
                            _Work.bFailed = true;
                            continue;
                        }

                        _Work.Edges.push_back(_Edge);
                    }
                    catch (const Standard_Failure& _Error_)
                    {
                        _Work.bFailed = true;
                        Diagnostics_.push_back(
                            std::string("Failed to add face-local split edge: ")
                            + _Error_.GetMessageString());
                    }
                }
            }

            if (!_Work.Edges.empty() || _Work.bFailed)
            {
                _FaceWorks.push_back(std::move(_Work));
            }
        }

        if (_ValidSegmentCount == 0)
        {
            Diagnostics_.push_back(
                "No section-vertex line segment belongs to a target face");
            return Target_;
        }

        std::size_t _AddedEdgeCount = 0;
        TopoDS_Shape _Result = Target_;
        for (const auto& _Work : _FaceWorks)
        {
            if (_Work.bFailed || _Work.Edges.empty())
            {
                Diagnostics_.push_back(
                    "Face-local split preparation failed; keep the original face");
                continue;
            }

            TopoDS_Face _CurrentFace;
            for (TopExp_Explorer _FaceExplorer(_Result, TopAbs_FACE);
                _FaceExplorer.More();
                _FaceExplorer.Next())
            {
                if (_FaceExplorer.Current().IsSame(_Work.SourceFace))
                {
                    _CurrentFace = TopoDS::Face(_FaceExplorer.Current());
                    break;
                }
            }
            if (_CurrentFace.IsNull())
            {
                Diagnostics_.push_back(
                    "Cannot locate the original face after a previous split; keep the original face");
                continue;
            }

            try
            {
                BRepFeat_SplitShape _FaceSplitter(_Result);
                bool _AddFailed = false;
                for (const auto& _Edge : _Work.Edges)
                {
                    try
                    {
                        _FaceSplitter.Add(_Edge, _CurrentFace);
                    }
                    catch (const Standard_Failure& _Error_)
                    {
                        _AddFailed = true;
                        Diagnostics_.push_back(
                            std::string("Failed to register face-local split edge; keep the original face: ")
                            + _Error_.GetMessageString());
                        break;
                    }
                }
                if (_AddFailed)
                {
                    continue;
                }

                _FaceSplitter.Build();
                if (!_FaceSplitter.IsDone() || _FaceSplitter.Shape().IsNull())
                {
                    Diagnostics_.push_back(
                        "OCC face split failed; keep the original face");
                    continue;
                }

                _Result = _FaceSplitter.Shape();
                _AddedEdgeCount += _Work.Edges.size();
            }
            catch (const Standard_Failure& _Error_)
            {
                Diagnostics_.push_back(
                    std::string("OCC face split raised an exception; keep the original face: ")
                    + _Error_.GetMessageString());
            }
        }

        if (_AddedEdgeCount == 0)
        {
            Diagnostics_.push_back(
                _ExistingEdgeCount == _ValidSegmentCount
                    ? "All section-vertex segments already exist as target edges"
                    : "No face was split successfully; keep the original target faces");
            return Target_;
        }

        Diagnostics_.push_back(
            "Collected " + std::to_string(_ValidSegmentCount)
            + " face-local split segments ("
            + std::to_string(_AddedEdgeCount)
            + " new, "
            + std::to_string(_ExistingEdgeCount)
            + " already present)");
        Diagnostics_.push_back(
            "Target faces were split independently by exact X-directed face-local edges");
        return _Result;
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
        IN const SFeatureToolpathOptions& Options_)
    {
        // Do not classify by "end face" or by a machining direction.  The
        // only geometric prerequisite is the same X-directed side-face rule
        // used by section extraction.
        if (!IsXSideFace(Face_, Options_))
        {
            return false;
        }

        bool _HasBoundarySample = false;
        try
        {
            for (TopExp_Explorer _Explorer(Face_, TopAbs_EDGE);
                _Explorer.More();
                _Explorer.Next())
            {
                const auto _Samples = SampleEdge(
                    TopoDS::Edge(_Explorer.Current()),
                    Options_.nCurveClassificationSampleCount,
                    Options_.dLinearTolerance);
                for (const auto& _Point : _Samples)
                {
                    _HasBoundarySample = true;
                    if (!IsOnSkin(
                        _Point,
                        Support_,
                        Options_.dSkinDistanceTolerance))
                    {
                        return false;
                    }
                }
            }
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
        return _HasBoundarySample;
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

        std::size_t _SkinFaceCount = 0;
        std::size_t _MachiningFaceCount = 0;
        for (int _Index = 1; _Index <= _Faces.Extent(); ++_Index)
        {
            const auto _Face = TopoDS::Face(_Faces(_Index));
            if (IsSkinFace(_Face, Support_, Options_))
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
        ShapeFix_Shape _Fixer(Source_);
        _Fixer.SetPrecision(Options_.dLinearTolerance);
        _Fixer.Perform();
        auto _Shape = _Fixer.Shape();
        if (_Shape.IsNull())
        {
            return {};
        }

        BRepLib::SameParameter(
            _Shape,
            Options_.dLinearTolerance,
            true);

        BRepBuilderAPI_Sewing _Sewing(Options_.dLinearTolerance);
        _Sewing.Add(_Shape);
        _Sewing.Perform();
        if (!_Sewing.SewedShape().IsNull())
        {
            _Shape = _Sewing.SewedShape();
        }

        ShapeUpgrade_UnifySameDomain _Unifier(
            _Shape,
            true,
            true,
            false);
        _Unifier.SetLinearTolerance(Options_.dLinearTolerance);
        _Unifier.SetAngularTolerance(Options_.dAngularToleranceRadians);
        _Unifier.Build();
        if (!_Unifier.Shape().IsNull())
        {
            _Shape = _Unifier.Shape();
        }

        ShapeUpgrade_ShapeDivideContinuity _Divider(_Shape);
        _Divider.SetPrecision(Options_.dLinearTolerance);
        _Divider.SetMinTolerance(Options_.dLinearTolerance * 0.1);
        _Divider.SetMaxTolerance(Options_.dLinearTolerance * 10.0);
        _Divider.SetEdgeMode(2);
        _Divider.SetSurfaceSegmentMode(true);
        _Divider.SetBoundaryCriterion(GeomAbs_C2);
        _Divider.SetPCurveCriterion(GeomAbs_C2);
        _Divider.SetSurfaceCriterion(GeomAbs_C2);
        if (_Divider.Perform() && !_Divider.Result().IsNull())
        {
            _Shape = _Divider.Result();
        }
        else
        {
            Diagnostics_.push_back(
                "ShapeDivideContinuity did not produce a replacement shape");
        }

        BRepLib::SameParameter(
            _Shape,
            Options_.dLinearTolerance,
            true);
        if (!BRepCheck_Analyzer(_Shape, true).IsValid())
        {
            Diagnostics_.push_back(
                "Prepared OCC target shape is not fully valid");
        }
        return _Shape;
    }

    TopoDS_Shape ValidateSplitShape(
        IN const TopoDS_Shape& Source_,
        IN const SFeatureToolpathOptions& Options_,
        OUT std::vector<std::string>& Diagnostics_)
    {
        if (Source_.IsNull())
        {
            return {};
        }
        auto _Shape = Source_;
        // Do not run UnifySameDomain or ShapeDivideContinuity here.  They may
        // erase the very seams introduced by BRepFeat_SplitShape.
        BRepLib::SameParameter(
            _Shape,
            Options_.dLinearTolerance,
            true);
        if (!BRepCheck_Analyzer(_Shape, true).IsValid())
        {
            Diagnostics_.push_back(
                "OCC split result is not fully valid");
        }
        return _Shape;
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

    const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(Geometry_);
    if (!_Built.bOK || _Built.Shape.IsNull())
    {
        _Result.Diagnostics.insert(
            _Result.Diagnostics.end(),
            _Built.Diagnostics.begin(),
            _Built.Diagnostics.end());
        _Result.Diagnostics.push_back(
            "Neutral BRep could not be translated to an OCC shape");
        return _Result;
    }

    // Merge and heal before introducing contour seams.  Running domain
    // unification after splitting would erase the seams we need below.
    const auto _PreparedTarget = PrepareShape(
        _Built.Shape,
        Options_,
        _Result.Diagnostics);
    if (_PreparedTarget.IsNull())
    {
        _Result.Diagnostics.push_back(
            "Neutral target BRep could not be prepared for face splitting");
        return _Result;
    }

    const auto _SplitTarget = SplitTargetBySectionVertices(
        _PreparedTarget,
        Section_,
        Options_,
        _Result.Diagnostics);
    if (_SplitTarget.IsNull())
    {
        return _Result;
    }

    const auto _SplitShape = ValidateSplitShape(
        _SplitTarget,
        Options_,
        _Result.Diagnostics);
    if (_SplitShape.IsNull())
    {
        _Result.Diagnostics.push_back(
            "OCC face-split result is null");
        return _Result;
    }

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
