#include "pch.h"
#include "ExtrudeRecognizesService.h"

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
            if (_Candidates.size() == 1)
            {
                _CurrentEdge = _Candidates.front();
                _CurrentNode = _NextNode;
                continue;
            }

            const auto _Incoming = _Result.Edges.back().Samples.size() >= 2
                ? Point2{
                    _Result.Edges.back().Samples.back().X
                        - _Result.Edges.back().Samples[_Result.Edges.back().Samples.size() - 2].X,
                    _Result.Edges.back().Samples.back().Y
                        - _Result.Edges.back().Samples[_Result.Edges.back().Samples.size() - 2].Y }
                : Point2{ 1.0, 0.0 };
            auto _Best = _Candidates.front();
            double _BestTurn = -(std::numeric_limits<double>::max)();
            for (const auto _Candidate : _Candidates)
            {
                const auto _Outgoing = OutgoingDirection(
                    GraphEdges_[_Candidate],
                    _NextNode,
                    Nodes_);
                // The outer boundary walk uses the largest left turn. The
                // signed angle is measured from the incoming motion tangent
                // to the candidate outgoing tangent, in the YOZ plane.
                auto _Turn = std::atan2(
                    Cross2(_Incoming, _Outgoing),
                    Dot2(_Incoming, _Outgoing));
                if (_Turn < 0.0)
                {
                    _Turn += 2.0 * kPi;
                }
                if (_Turn > _BestTurn)
                {
                    _BestTurn = _Turn;
                    _Best = _Candidate;
                }
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
                    std::abs(_Angle) });
            }
        }

        std::sort(_Seeds.begin(), _Seeds.end(), [](IN const auto& Left_, IN const auto& Right_) {
            if (std::abs(Left_.dAxisAngle - Right_.dAxisAngle) > 1.0e-12)
            {
                return Left_.dAxisAngle < Right_.dAxisAngle;
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
        || Options_.nCurveSampleCount < 8)
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
    for (auto& _Candidate : _OuterCandidates)
    {
        const auto _ContainsAll = CandidateContainsAllActiveEdges(
            _Candidate,
            _GraphEdges,
            Options_.dConnectionTolerance);
        _Result.Diagnostics.push_back(
            "face candidate area=" + std::to_string(_Candidate.dSignedArea)
            + " edges=" + std::to_string(_Candidate.GraphEdgeIndices.size())
            + " contains=" + (_ContainsAll ? "true" : "false"));
        if (!_ContainsAll)
        {
            continue;
        }
        if (!_Outer || std::abs(_Candidate.dSignedArea)
                > std::abs(_Outer->dSignedArea))
        {
            _Outer = std::move(_Candidate);
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
}
