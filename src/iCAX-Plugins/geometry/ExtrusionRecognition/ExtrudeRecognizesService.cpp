#include "pch.h"
#include "ExtrudeRecognizesService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <Standard_Failure.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepBuilderAPI_Transform.hxx>

namespace iCAX::ExtrusionRecognition
{
namespace direction_detail
{
    SSectionWiresResult ExtractCandidateSection(const iCAX::GeometryData::BRepModel&,
        const SSectionWireOptions&);
    bool TryContainingExtrusionVolume(const iCAX::GeometryData::BRepModel&,
        const SSectionWiresResult&, const SSectionWireOptions&, double&, double&);
}
namespace
{
    using iCAX::GeometryData::BRepModel;
    using iCAX::GeometryData::Direction3;
    using iCAX::GeometryData::Point3;
    using iCAX::GeometryData::Triangulation3;
    using iCAX::GeometryData::Transform3;
    using iCAX::GeometryData::Vector3;

    constexpr double kEpsilon = 1.0e-12;

    Vector3 ToVector(IN const Direction3& Direction_)
    {
        return { Direction_.X, Direction_.Y, Direction_.Z };
    }

    Vector3 ToVector(IN const Point3& Point_)
    {
        return { Point_.X, Point_.Y, Point_.Z };
    }

    Vector3 Difference(IN const Point3& A_, IN const Point3& B_)
    {
        return { A_.X - B_.X, A_.Y - B_.Y, A_.Z - B_.Z };
    }

    Vector3 Add(IN const Vector3& A_, IN const Vector3& B_)
    {
        return { A_.X + B_.X, A_.Y + B_.Y, A_.Z + B_.Z };
    }

    Vector3 Scale(IN const Vector3& Vector_, IN const double Scale_)
    {
        return { Vector_.X * Scale_, Vector_.Y * Scale_, Vector_.Z * Scale_ };
    }

    double Dot(IN const Vector3& A_, IN const Vector3& B_)
    {
        return A_.X * B_.X + A_.Y * B_.Y + A_.Z * B_.Z;
    }

    Vector3 Cross(IN const Vector3& A_, IN const Vector3& B_)
    {
        return {
            A_.Y * B_.Z - A_.Z * B_.Y,
            A_.Z * B_.X - A_.X * B_.Z,
            A_.X * B_.Y - A_.Y * B_.X
        };
    }

    double Length(IN const Vector3& Vector_)
    {
        return std::sqrt(Dot(Vector_, Vector_));
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

    Direction3 Canonicalize(IN const Direction3& Direction_)
    {
        Direction3 _Result = Direction_;
        const std::array<double, 3> _Components{
            std::abs(_Result.X),
            std::abs(_Result.Y),
            std::abs(_Result.Z)
        };
        const auto _Major = static_cast<std::size_t>(std::distance(
            _Components.begin(),
            std::max_element(_Components.begin(), _Components.end())));
        if ((&_Result.X)[_Major] < 0.0)
        {
            _Result.X = -_Result.X;
            _Result.Y = -_Result.Y;
            _Result.Z = -_Result.Z;
        }
        return _Result;
    }

    struct SDirectionCluster final
    {
        Vector3 Sum{};
        double dWeight = 0.0;
        std::size_t nEvidence = 0;
        std::vector<std::pair<Direction3, double>> Evidence;
    };

    Direction3 ClusterDirection(IN const SDirectionCluster& Cluster_)
    {
        Direction3 _Direction;
        if (!TryNormalize(Cluster_.Sum, _Direction))
        {
            return {};
        }
        return Canonicalize(_Direction);
    }

    void AddDirectionEvidence(
        IN OUT std::vector<SDirectionCluster>& Clusters_,
        IN const Direction3& Direction_,
        IN const double Weight_,
        IN const double AngularToleranceRadians_)
    {
        if (Weight_ <= 0.0)
        {
            return;
        }

        Direction3 _Direction;
        if (!TryNormalize(ToVector(Direction_), _Direction))
        {
            return;
        }
        _Direction = Canonicalize(_Direction);
        const auto _DirectionVector = ToVector(_Direction);
        const auto _MinimumDot = std::cos(AngularToleranceRadians_);

        for (auto& _Cluster : Clusters_)
        {
            auto _ClusterDirection = ClusterDirection(_Cluster);
            auto _Dot = Dot(_DirectionVector, ToVector(_ClusterDirection));
            if (std::abs(_Dot) < _MinimumDot)
            {
                continue;
            }
            if (_Dot < 0.0)
            {
                _Cluster.Sum = Add(_Cluster.Sum, Scale(_DirectionVector, -Weight_));
            }
            else
            {
                _Cluster.Sum = Add(_Cluster.Sum, Scale(_DirectionVector, Weight_));
            }
            _Cluster.dWeight += Weight_;
            ++_Cluster.nEvidence;
            _Cluster.Evidence.emplace_back(_Direction, Weight_);
            return;
        }

        Clusters_.push_back({
            Scale(_DirectionVector, Weight_),
            Weight_,
            1,
            { { _Direction, Weight_ } }
        });
    }

    void RefineClusterDirection(IN OUT SDirectionCluster& Cluster_)
    {
        // Angular clustering is deliberately tolerant of machining edges.
        // Its arithmetic mean, however, invents a direction between the true
        // generators and nearby sloped cut edges. Over a long part even that
        // small tilt moves a stock wall outside the distance tolerance.
        // The weighted angular medoid minimizes L1 unoriented angular error
        // among actual generators, retaining the dominant measured direction.
        double _BestCost = std::numeric_limits<double>::infinity();
        double _BestWeight = -1.0;
        Direction3 _Best = ClusterDirection(Cluster_);
        for (const auto& [_Candidate, _Weight] : Cluster_.Evidence)
        {
            double _Cost = 0.0;
            for (const auto& [_Other, _OtherWeight] : Cluster_.Evidence)
            {
                const auto _CrossLength = Length(Cross(ToVector(_Candidate), ToVector(_Other)));
                _Cost += _OtherWeight * std::atan2(_CrossLength,
                    std::abs(Dot(ToVector(_Candidate), ToVector(_Other))));
            }
            if (_Cost < _BestCost || (_Cost == _BestCost && _Weight > _BestWeight))
            {
                _BestCost = _Cost;
                _BestWeight = _Weight;
                _Best = _Candidate;
            }
        }
        Cluster_.Sum = Scale(ToVector(_Best), Cluster_.dWeight);
    }

    struct SGeometryIndex final
    {
        std::unordered_map<std::uint64_t, const iCAX::GeometryData::Curve3*> Curves;
        std::unordered_map<std::uint64_t, const iCAX::GeometryData::Surface3*> Surfaces;
        std::unordered_map<std::uint64_t, const Triangulation3*> Triangulations;
        std::unordered_map<std::uint64_t, const Point3*> Vertices;
        std::unordered_map<std::uint64_t, const iCAX::GeometryData::BRepWire*> Wires;

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

            Vertices.reserve(Geometry_.Vertices.size());
            for (const auto& _Vertex : Geometry_.Vertices)
            {
                Vertices.emplace(_Vertex.Id, &_Vertex.Position);
            }
            Wires.reserve(Geometry_.Wires.size());
            for (const auto& _Wire : Geometry_.Wires)
            {
                Wires.emplace(_Wire.Id, &_Wire);
            }
        }
    };

    const iCAX::GeometryData::Curve3* FindCurve(
        IN const SGeometryIndex& Index_,
        IN const std::uint64_t CurveID_)
    {
        const auto _Iter = Index_.Curves.find(CurveID_);
        return _Iter == Index_.Curves.end() ? nullptr : _Iter->second;
    }

    const iCAX::GeometryData::Surface3* FindSurface(
        IN const SGeometryIndex& Index_,
        IN const std::uint64_t SurfaceID_)
    {
        const auto _Iter = Index_.Surfaces.find(SurfaceID_);
        return _Iter == Index_.Surfaces.end() ? nullptr : _Iter->second;
    }

    const Triangulation3* FindTriangulation(
        IN const SGeometryIndex& Index_,
        IN const std::uint64_t TriangulationID_)
    {
        const auto _Iter = Index_.Triangulations.find(TriangulationID_);
        return _Iter == Index_.Triangulations.end() ? nullptr : _Iter->second;
    }

    const Point3* FindVertex(
        IN const SGeometryIndex& Index_,
        IN const std::uint64_t VertexID_)
    {
        const auto _Iter = Index_.Vertices.find(VertexID_);
        return _Iter == Index_.Vertices.end() ? nullptr : _Iter->second;
    }

    double SegmentLength(
        IN const SGeometryIndex& Index_,
        IN const iCAX::GeometryData::BRepEdge& Edge_,
        IN const iCAX::GeometryData::Curve3& Curve_)
    {
        if (const auto* _Start = FindVertex(Index_, Edge_.StartVertexId))
        {
            if (const auto* _End = FindVertex(Index_, Edge_.EndVertexId))
            {
                const auto _Length = Length(Difference(*_End, *_Start));
                if (_Length > kEpsilon)
                {
                    return _Length;
                }
            }
        }
        return std::visit([&Edge_](IN const auto& Value_) -> double {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Line3>)
            {
                return std::abs(Edge_.Range.Last - Edge_.Range.First);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment3>)
            {
                return Length(Difference(Value_.End, Value_.Start));
            }
            else
            {
                return 0.0;
            }
        }, Curve_);
    }

    std::optional<Direction3> StraightEdgeDirection(
        IN const iCAX::GeometryData::Curve3& Curve_)
    {
        return std::visit([](IN const auto& Value_) -> std::optional<Direction3> {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Line3>)
            {
                return Value_.Axis.Direction;
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment3>)
            {
                Direction3 _Direction;
                if (TryNormalize(Difference(Value_.End, Value_.Start), _Direction))
                {
                    return _Direction;
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Bezier3>
                || std::is_same_v<T, iCAX::GeometryData::BSpline3>
                || std::is_same_v<T, iCAX::GeometryData::NURBS3>)
            {
                // IGES can encode exact straight trim edges as splines. A
                // positive-weight curve stays in its control hull; collinear
                // poles therefore prove a line without sampling a curved cut.
                if (Value_.Poles.size() < 2) return std::nullopt;
                if constexpr (std::is_same_v<T, iCAX::GeometryData::NURBS3>)
                {
                    if (Value_.Weights.size() != Value_.Poles.size()
                        || std::any_of(Value_.Weights.begin(), Value_.Weights.end(),
                            [](double weight) { return !std::isfinite(weight) || weight <= 0.0; }))
                        return std::nullopt;
                }
                Vector3 span{};
                double length = 0.0, scale = 1.0;
                for (const auto& pole : Value_.Poles)
                {
                    if (!std::isfinite(pole.X) || !std::isfinite(pole.Y) || !std::isfinite(pole.Z))
                        return std::nullopt;
                    scale = std::max({scale, std::abs(pole.X), std::abs(pole.Y), std::abs(pole.Z)});
                    const auto delta = Difference(pole, Value_.Poles.front());
                    const auto distance = Length(delta);
                    if (distance > length) { span = delta; length = distance; }
                }
                Direction3 direction;
                if (!TryNormalize(span, direction)) return std::nullopt;
                const auto tolerance = 64.0 * std::numeric_limits<double>::epsilon()
                    * std::max(scale, length);
                for (const auto& pole : Value_.Poles)
                    if (Length(Cross(Difference(pole, Value_.Poles.front()), ToVector(direction))) > tolerance)
                        return std::nullopt;
                return direction;
            }
            return std::nullopt;
        }, Curve_);
    }

    struct SFaceMeshStats final
    {
        double dArea = 0.0;
        std::vector<std::pair<double, Direction3>> AreaNormals;
    };

    SFaceMeshStats MeasureFaceMesh(IN const Triangulation3& Mesh_)
    {
        SFaceMeshStats _Result;
        for (const auto& _Triangle : Mesh_.Triangles)
        {
            if (_Triangle[0] >= Mesh_.Vertices.size()
                || _Triangle[1] >= Mesh_.Vertices.size()
                || _Triangle[2] >= Mesh_.Vertices.size())
            {
                continue;
            }
            const auto _NormalVector = Cross(
                Difference(Mesh_.Vertices[_Triangle[1]], Mesh_.Vertices[_Triangle[0]]),
                Difference(Mesh_.Vertices[_Triangle[2]], Mesh_.Vertices[_Triangle[0]]));
            const auto _DoubleArea = Length(_NormalVector);
            if (_DoubleArea <= kEpsilon)
            {
                continue;
            }
            Direction3 _Normal;
            if (!TryNormalize(_NormalVector, _Normal))
            {
                continue;
            }
            const auto _Area = 0.5 * _DoubleArea;
            _Result.dArea += _Area;
            _Result.AreaNormals.emplace_back(_Area, _Normal);
        }
        return _Result;
    }

    std::vector<std::pair<double, Direction3>> CollectFaceNormalEvidence(
        IN const BRepModel& Geometry_,
        IN const SGeometryIndex& Index_)
    {
        std::vector<std::pair<double, Direction3>> _Evidence;
        for (const auto& _Face : Geometry_.Faces)
        {
            const auto* _Mesh = FindTriangulation(Index_, _Face.Triangulation3Id);
            if (!_Mesh)
            {
                continue;
            }
            const auto _Stats = MeasureFaceMesh(*_Mesh);
            _Evidence.insert(
                _Evidence.end(),
                _Stats.AreaNormals.begin(),
                _Stats.AreaNormals.end());
        }
        return _Evidence;
    }

    double FaceNormalVote(
        IN const std::vector<std::pair<double, Direction3>>& Evidence_,
        IN const Direction3& Candidate_)
    {
        const auto _Candidate = ToVector(Candidate_);
        double _Vote = 0.0;
        for (const auto& [_Area, _Normal] : Evidence_)
        {
            const auto _Perpendicularity = 1.0
                - std::abs(Dot(ToVector(_Normal), _Candidate));
            _Vote += _Area * _Perpendicularity * _Perpendicularity;
        }
        return _Vote;
    }

    bool TryAddBSplineGenerators(
        IN const iCAX::GeometryData::BSplineSurface3& Surface_,
        IN const bool AlongV_,
        IN const double MinimumLength_,
        IN const double AngularToleranceRadians_,
        OUT Direction3& Direction_,
        OUT double& Span_)
    {
        if (Surface_.UCount == 0 || Surface_.VCount == 0
            || Surface_.Poles.size() != static_cast<std::size_t>(Surface_.UCount) * Surface_.VCount)
        {
            return false;
        }

        // The neutral importer represents an OCC linear-extrusion/ruled face
        // with degree one in its generator parameter. Do not treat an arbitrary
        // spline patch's end-pole chord as a generator direction.
        if ((AlongV_ && (Surface_.VDegree != 1 || Surface_.VCount < 2))
            || (!AlongV_ && (Surface_.UDegree != 1 || Surface_.UCount < 2)))
        {
            return false;
        }

        const auto _GeneratorCount = AlongV_ ? Surface_.UCount : Surface_.VCount;
        const auto _Start = AlongV_ ? 0u : 0u;
        const auto _End = AlongV_
            ? Surface_.VCount - 1u
            : Surface_.UCount - 1u;
        if (_End == _Start || _GeneratorCount == 0)
        {
            return false;
        }

        Vector3 _Sum{};
        std::vector<Vector3> _Directions;
        std::vector<double> _Lengths;
        _Directions.reserve(_GeneratorCount);
        _Lengths.reserve(_GeneratorCount);
        for (std::uint32_t _Index = 0; _Index < _GeneratorCount; ++_Index)
        {
            const auto _FirstIndex = AlongV_
                ? static_cast<std::size_t>(_Index) * Surface_.VCount + _Start
                : static_cast<std::size_t>(_Start) * Surface_.VCount + _Index;
            const auto _LastIndex = AlongV_
                ? static_cast<std::size_t>(_Index) * Surface_.VCount + _End
                : static_cast<std::size_t>(_End) * Surface_.VCount + _Index;
            const auto _Delta = Difference(Surface_.Poles[_LastIndex], Surface_.Poles[_FirstIndex]);
            const auto _Length = Length(_Delta);
            if (_Length <= MinimumLength_)
            {
                continue;
            }
            Direction3 _Direction;
            if (!TryNormalize(_Delta, _Direction))
            {
                continue;
            }
            _Directions.push_back(ToVector(_Direction));
            _Lengths.push_back(_Length);
            if (_Sum.X == 0.0 && _Sum.Y == 0.0 && _Sum.Z == 0.0)
            {
                _Sum = ToVector(_Direction);
            }
            else
            {
                const auto _Sign = Dot(_Sum, ToVector(_Direction)) < 0.0 ? -1.0 : 1.0;
                _Sum = Add(_Sum, Scale(ToVector(_Direction), _Sign));
            }
        }

        if (_Directions.empty() || !TryNormalize(_Sum, Direction_))
        {
            return false;
        }
        const auto _MinimumDot = std::cos(AngularToleranceRadians_);
        for (const auto& _Direction : _Directions)
        {
            if (std::abs(Dot(_Direction, ToVector(Direction_))) < _MinimumDot)
            {
                return false;
            }
        }
        Span_ = *std::max_element(_Lengths.begin(), _Lengths.end());
        return Span_ > MinimumLength_;
    }

    double FaceAxialSpan(
        IN const Triangulation3& Mesh_,
        IN const Direction3& Direction_)
    {
        if (Mesh_.Vertices.empty())
        {
            return 0.0;
        }
        const auto _Axis = ToVector(Direction_);
        double _Minimum = (std::numeric_limits<double>::max)();
        double _Maximum = (std::numeric_limits<double>::lowest)();
        for (const auto& _Point : Mesh_.Vertices)
        {
            const auto _Projection = _Point.X * _Axis.X
                + _Point.Y * _Axis.Y
                + _Point.Z * _Axis.Z;
            _Minimum = std::min(_Minimum, _Projection);
            _Maximum = std::max(_Maximum, _Projection);
        }
        return std::max(0.0, _Maximum - _Minimum);
    }

    double TrimmedFaceAxialSpan(
        IN const iCAX::GeometryData::BRepFace& Face_,
        IN const SGeometryIndex& Index_,
        IN const Direction3& Direction_)
    {
        if (const auto* _Mesh = FindTriangulation(Index_, Face_.Triangulation3Id);
            _Mesh && !_Mesh->Vertices.empty())
        {
            return FaceAxialSpan(*_Mesh, Direction_);
        }

        // A supporting spline surface can extend far beyond its trimmed face
        // (e.g. the shallow embossed lettering on a production bracket). Its
        // control poles establish a generator direction, not its vote length.
        // Without a mesh, boundary vertices provide conservative evidence;
        // a face without either source must not vote using untrimmed poles.
        const auto _Axis = ToVector(Direction_);
        double _Minimum = (std::numeric_limits<double>::max)();
        double _Maximum = (std::numeric_limits<double>::lowest)();
        for (const auto _WireID : Face_.WireIds)
        {
            const auto _Wire = Index_.Wires.find(_WireID);
            if (_Wire == Index_.Wires.end()) continue;
            for (const auto& _Coedge : _Wire->second->Coedges)
            {
                for (const auto _VertexID : { _Coedge.StartVertexId, _Coedge.EndVertexId })
                {
                    const auto* _Point = FindVertex(Index_, _VertexID);
                    if (!_Point) continue;
                    const auto _Projection = Dot(ToVector(*_Point), _Axis);
                    _Minimum = std::min(_Minimum, _Projection);
                    _Maximum = std::max(_Maximum, _Projection);
                }
            }
        }
        return _Maximum >= _Minimum ? _Maximum - _Minimum : 0.0;
    }

    struct SAlignmentFrame final
    {
        Direction3 XAxis;
        Direction3 YAxis;
        Direction3 ZAxis;
        Point3 Center;
        Transform3 TRSF;
        gp_Trsf NativeTransform;
    };

    Point3 AddVector(IN const Point3& Point_, IN const Vector3& Vector_)
    {
        return {
            Point_.X + Vector_.X,
            Point_.Y + Vector_.Y,
            Point_.Z + Vector_.Z
        };
    }

    Vector3 TransformVector(IN const SAlignmentFrame& Frame_, IN const Vector3& Vector_)
    {
        const auto value=gp_Vec(Vector_.X,Vector_.Y,Vector_.Z).Transformed(Frame_.NativeTransform);
        return {value.X(),value.Y(),value.Z()};
    }

    Point3 TransformPoint(IN const SAlignmentFrame& Frame_, IN const Point3& Point_)
    {
        const auto value=gp_Pnt(Point_.X,Point_.Y,Point_.Z).Transformed(Frame_.NativeTransform);
        return {value.X(),value.Y(),value.Z()};
    }

    Direction3 TransformDirection(IN const SAlignmentFrame& Frame_, IN const Direction3& Direction_)
    {
        const auto value=gp_Dir(Direction_.X,Direction_.Y,Direction_.Z).Transformed(Frame_.NativeTransform);
        return {value.X(),value.Y(),value.Z()};
    }

    void ExtendBounds(
        IN const Point3& Point_,
        IN OUT Point3& Minimum_,
        IN OUT Point3& Maximum_,
        IN OUT bool& HasPoint_)
    {
        if (!HasPoint_)
        {
            Minimum_ = Point_;
            Maximum_ = Point_;
            HasPoint_ = true;
            return;
        }
        Minimum_.X = std::min(Minimum_.X, Point_.X);
        Minimum_.Y = std::min(Minimum_.Y, Point_.Y);
        Minimum_.Z = std::min(Minimum_.Z, Point_.Z);
        Maximum_.X = std::max(Maximum_.X, Point_.X);
        Maximum_.Y = std::max(Maximum_.Y, Point_.Y);
        Maximum_.Z = std::max(Maximum_.Z, Point_.Z);
    }

    void ExtendCurveBounds(
        IN const iCAX::GeometryData::Curve3& Curve_,
        IN OUT Point3& Minimum_,
        IN OUT Point3& Maximum_,
        IN OUT bool& HasPoint_)
    {
        std::visit([&](IN const auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Line3>
                || std::is_same_v<T, iCAX::GeometryData::Ray3>)
            {
                ExtendBounds(Value_.Axis.Location, Minimum_, Maximum_, HasPoint_);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment3>)
            {
                ExtendBounds(Value_.Start, Minimum_, Maximum_, HasPoint_);
                ExtendBounds(Value_.End, Minimum_, Maximum_, HasPoint_);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle3>)
            {
                ExtendBounds(Value_.Placement.Location, Minimum_, Maximum_, HasPoint_);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc3>)
            {
                ExtendBounds(Value_.Basis.Placement.Location, Minimum_, Maximum_, HasPoint_);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse3>)
            {
                ExtendBounds(Value_.Placement.Location, Minimum_, Maximum_, HasPoint_);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc3>)
            {
                ExtendBounds(Value_.Basis.Placement.Location, Minimum_, Maximum_, HasPoint_);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Polyline3>)
            {
                for (const auto& _Point : Value_.Points)
                {
                    ExtendBounds(_Point, Minimum_, Maximum_, HasPoint_);
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Bezier3>
                || std::is_same_v<T, iCAX::GeometryData::BSpline3>
                || std::is_same_v<T, iCAX::GeometryData::NURBS3>)
            {
                for (const auto& _Point : Value_.Poles)
                {
                    ExtendBounds(_Point, Minimum_, Maximum_, HasPoint_);
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Clothoid3>)
            {
                ExtendBounds(Value_.Placement.Location, Minimum_, Maximum_, HasPoint_);
            }
        }, Curve_);
    }

    void ExtendSurfaceBounds(
        IN const iCAX::GeometryData::Surface3& Surface_,
        IN OUT Point3& Minimum_,
        IN OUT Point3& Maximum_,
        IN OUT bool& HasPoint_)
    {
        std::visit([&](IN const auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::BSplineSurface3>)
            {
                for (const auto& _Point : Value_.Poles)
                {
                    ExtendBounds(_Point, Minimum_, Maximum_, HasPoint_);
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::RevolutionSurface3>
                || std::is_same_v<T, iCAX::GeometryData::ExtrusionSurface3>)
            {
                ExtendCurveBounds(Value_.Basis, Minimum_, Maximum_, HasPoint_);
            }
            else
            {
                ExtendBounds(Value_.Placement.Location, Minimum_, Maximum_, HasPoint_);
            }
        }, Surface_);
    }

    Point3 ModelCenter(
        IN const BRepModel& Geometry_,
        IN const SGeometryIndex& Index_)
    {
        Point3 _Minimum;
        Point3 _Maximum;
        bool _HasPoint = false;
        for (const auto& _Vertex : Geometry_.Vertices)
        {
            ExtendBounds(_Vertex.Position, _Minimum, _Maximum, _HasPoint);
        }
        for (const auto& _Record : Geometry_.Triangulations3)
        {
            for (const auto& _Point : _Record.Geometry.Vertices)
            {
                ExtendBounds(_Point, _Minimum, _Maximum, _HasPoint);
            }
        }
        for (const auto& _Record : Geometry_.Curves3)
        {
            ExtendCurveBounds(_Record.Geometry, _Minimum, _Maximum, _HasPoint);
        }
        for (const auto& _Record : Geometry_.Surfaces3)
        {
            ExtendSurfaceBounds(_Record.Geometry, _Minimum, _Maximum, _HasPoint);
        }

        // A line's placement is only a point on the line. If no vertices or mesh
        // are available, include the edge's parameter endpoints in the bounds.
        for (const auto& _Edge : Geometry_.Edges)
        {
            const auto* _Curve = FindCurve(Index_, _Edge.Curve3Id);
            if (!_Curve)
            {
                continue;
            }
            std::visit([&](IN const auto& Value_)
            {
                using T = std::decay_t<decltype(Value_)>;
                if constexpr (std::is_same_v<T, iCAX::GeometryData::Line3>
                    || std::is_same_v<T, iCAX::GeometryData::Ray3>)
                {
                    Direction3 _UnitDirection;
                    if (!TryNormalize(ToVector(Value_.Axis.Direction), _UnitDirection))
                    {
                        return;
                    }
                    const auto _Direction = ToVector(_UnitDirection);
                    ExtendBounds(
                        AddVector(Value_.Axis.Location, Scale(_Direction, _Edge.Range.First)),
                        _Minimum,
                        _Maximum,
                        _HasPoint);
                    ExtendBounds(
                        AddVector(Value_.Axis.Location, Scale(_Direction, _Edge.Range.Last)),
                        _Minimum,
                        _Maximum,
                        _HasPoint);
                }
            }, *_Curve);
        }

        if (!_HasPoint)
        {
            return {};
        }
        return {
            0.5 * (_Minimum.X + _Maximum.X),
            0.5 * (_Minimum.Y + _Maximum.Y),
            0.5 * (_Minimum.Z + _Maximum.Z)
        };
    }

    Transform3 Multiply(
        IN const Transform3& Left_,
        IN const Transform3& Right_)
    {
        Transform3 _Result;
        for (std::size_t _Row = 0; _Row < 4; ++_Row)
        {
            for (std::size_t _Column = 0; _Column < 4; ++_Column)
            {
                _Result.Matrix.Values[_Row][_Column] = 0.0;
                for (std::size_t _Index = 0; _Index < 4; ++_Index)
                {
                    _Result.Matrix.Values[_Row][_Column] +=
                        Left_.Matrix.Values[_Row][_Index]
                        * Right_.Matrix.Values[_Index][_Column];
                }
            }
        }
        return _Result;
    }

    SAlignmentFrame MakeAlignmentFrame(
        IN const Direction3& Axis_,
        IN const Point3& Center_)
    {
        SAlignmentFrame _Frame;
        _Frame.XAxis = Axis_;
        _Frame.Center = Center_;

        const Vector3 _Reference = std::abs(Axis_.X) < 0.9
            ? Vector3{ 1.0, 0.0, 0.0 }
            : Vector3{ 0.0, 1.0, 0.0 };
        const auto _YAxisVector = Cross(_Reference, ToVector(Axis_));
        if (!TryNormalize(_YAxisVector, _Frame.YAxis))
        {
            _Frame.YAxis = { 0.0, 1.0, 0.0 };
        }
        const auto _ZAxisVector = Cross(ToVector(Axis_), ToVector(_Frame.YAxis));
        if (!TryNormalize(_ZAxisVector, _Frame.ZAxis))
        {
            _Frame.ZAxis = { 0.0, 0.0, 1.0 };
        }

        _Frame.TRSF = {};
        _Frame.TRSF.Matrix.Values[0][0] = _Frame.XAxis.X;
        _Frame.TRSF.Matrix.Values[0][1] = _Frame.XAxis.Y;
        _Frame.TRSF.Matrix.Values[0][2] = _Frame.XAxis.Z;
        _Frame.TRSF.Matrix.Values[1][0] = _Frame.YAxis.X;
        _Frame.TRSF.Matrix.Values[1][1] = _Frame.YAxis.Y;
        _Frame.TRSF.Matrix.Values[1][2] = _Frame.YAxis.Z;
        _Frame.TRSF.Matrix.Values[2][0] = _Frame.ZAxis.X;
        _Frame.TRSF.Matrix.Values[2][1] = _Frame.ZAxis.Y;
        _Frame.TRSF.Matrix.Values[2][2] = _Frame.ZAxis.Z;
        _Frame.TRSF.Matrix.Values[0][3] = -Dot(ToVector(Center_), ToVector(_Frame.XAxis));
        _Frame.TRSF.Matrix.Values[1][3] = -Dot(ToVector(Center_), ToVector(_Frame.YAxis));
        _Frame.TRSF.Matrix.Values[2][3] = -Dot(ToVector(Center_), ToVector(_Frame.ZAxis));
        // Apply the same rigid transform to points, curve/surface placements
        // and the reported matrix. Independent centered-dot arithmetic can
        // move a vertex beyond its existing native tolerance by rounding even
        // though the corresponding kernel transform remains valid.
        auto& m=_Frame.TRSF.Matrix.Values;
        _Frame.NativeTransform.SetValues(m[0][0],m[0][1],m[0][2],m[0][3],
            m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3]);
        for(int row=0;row<3;++row)for(int column=0;column<4;++column)
            m[row][column]=_Frame.NativeTransform.Value(row+1,column+1);
        return _Frame;
    }

    void TransformPlacement(
        IN const SAlignmentFrame& Frame_,
        IN OUT iCAX::GeometryData::Placement3& Placement_)
    {
        Placement_.Location = TransformPoint(Frame_, Placement_.Location);
        Placement_.XDirection = TransformDirection(Frame_, Placement_.XDirection);
        Placement_.YDirection = TransformDirection(Frame_, Placement_.YDirection);
        Placement_.ZDirection = TransformDirection(Frame_, Placement_.ZDirection);
    }

    void TransformCurve(
        IN const SAlignmentFrame& Frame_,
        IN OUT iCAX::GeometryData::Curve3& Curve_)
    {
        std::visit([&](IN auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::Line3>
                || std::is_same_v<T, iCAX::GeometryData::Ray3>)
            {
                Value_.Axis.Location = TransformPoint(Frame_, Value_.Axis.Location);
                Value_.Axis.Direction = TransformDirection(Frame_, Value_.Axis.Direction);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Segment3>)
            {
                Value_.Start = TransformPoint(Frame_, Value_.Start);
                Value_.End = TransformPoint(Frame_, Value_.End);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Circle3>)
            {
                TransformPlacement(Frame_, Value_.Placement);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Arc3>)
            {
                TransformPlacement(Frame_, Value_.Basis.Placement);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Ellipse3>)
            {
                TransformPlacement(Frame_, Value_.Placement);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::EllipseArc3>)
            {
                TransformPlacement(Frame_, Value_.Basis.Placement);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Polyline3>)
            {
                for (auto& _Point : Value_.Points)
                {
                    _Point = TransformPoint(Frame_, _Point);
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Bezier3>
                || std::is_same_v<T, iCAX::GeometryData::BSpline3>
                || std::is_same_v<T, iCAX::GeometryData::NURBS3>)
            {
                for (auto& _Point : Value_.Poles)
                {
                    _Point = TransformPoint(Frame_, _Point);
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::Clothoid3>)
            {
                TransformPlacement(Frame_, Value_.Placement);
            }
        }, Curve_);
    }

    void TransformSurface(
        IN const SAlignmentFrame& Frame_,
        IN OUT iCAX::GeometryData::Surface3& Surface_)
    {
        std::visit([&](IN auto& Value_)
        {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::BSplineSurface3>)
            {
                for (auto& _Point : Value_.Poles)
                {
                    _Point = TransformPoint(Frame_, _Point);
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::RevolutionSurface3>)
            {
                TransformCurve(Frame_, Value_.Basis);
                Value_.Axis.Location = TransformPoint(Frame_, Value_.Axis.Location);
                Value_.Axis.Direction = TransformDirection(Frame_, Value_.Axis.Direction);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::ExtrusionSurface3>)
            {
                TransformCurve(Frame_, Value_.Basis);
                Value_.Direction = TransformDirection(Frame_, Value_.Direction);
            }
            else
            {
                TransformPlacement(Frame_, Value_.Placement);
            }
        }, Surface_);
    }

    void TransformShapeRef(
        IN const SAlignmentFrame& Frame_,
        IN OUT iCAX::GeometryData::BRepShapeRef& ShapeRef_)
    {
        ShapeRef_.Location = Multiply(Frame_.TRSF, ShapeRef_.Location);
    }

    BRepModel AlignGeometry(
        IN const BRepModel& Geometry_,
        IN const SAlignmentFrame& Frame_)
    {
        BRepModel _Result = Geometry_;
        // A rigid transform preserves geometric uncertainty, but evaluating
        // its coordinates in double precision adds arithmetic uncertainty.
        // Propagate a coordinate-scale rounding bound, not a machining
        // tolerance. Otherwise edges already at their native tolerance can
        // become invalid by ~1e-11 mm after an ordinary rotation.
        const double eps=std::numeric_limits<double>::epsilon();
        double coordinateScale=0;
        for(const auto& vertex:Geometry_.Vertices)
            coordinateScale=std::max(coordinateScale,
                std::abs(vertex.Position.X)+std::abs(vertex.Position.Y)+std::abs(vertex.Position.Z)
                +std::abs(Frame_.Center.X)+std::abs(Frame_.Center.Y)+std::abs(Frame_.Center.Z));
        const double roundoff=8*eps/(1-8*eps)*coordinateScale;
        for (auto& _Vertex : _Result.Vertices)
        {
            _Vertex.Position = TransformPoint(Frame_, _Vertex.Position);
            _Vertex.Tolerance+=roundoff;
        }
        for(auto& edge:_Result.Edges)edge.Tolerance+=roundoff;
        for (auto& _Record : _Result.Curves3)
        {
            TransformCurve(Frame_, _Record.Geometry);
        }
        for (auto& _Record : _Result.Surfaces3)
        {
            TransformSurface(Frame_, _Record.Geometry);
        }
        for (auto& _Record : _Result.Triangulations3)
        {
            for (auto& _Point : _Record.Geometry.Vertices)
            {
                _Point = TransformPoint(Frame_, _Point);
            }
            for (auto& _Normal : _Record.Geometry.Normals)
            {
                _Normal = TransformVector(Frame_, _Normal);
            }
        }
        for (auto& _Compound : _Result.Compounds)
        {
            for (auto& _Child : _Compound.Children)
            {
                TransformShapeRef(Frame_, _Child);
            }
        }
        for (auto& _RootShape : _Result.RootShapes)
        {
            TransformShapeRef(Frame_, _RootShape);
        }
        return _Result;
    }
}

void CExtrudeRecognizesService::OnLoad()
{
}

void CExtrudeRecognizesService::OnUnload()
{
}

SExtrusionDirectionResult CExtrudeRecognizesService::Recognize(
    IN const iCAX::GeometryData::BRepModel& Geometry_,
    IN const SExtrusionDirectionOptions& Options_) const
{
    SExtrusionDirectionResult _Result;
    if (Options_.dAngularToleranceRadians <= 0.0
        || Options_.dAngularToleranceRadians >= 1.5707963267948966
        || Options_.dMinimumEvidenceLength <= 0.0
        || Options_.dAmbiguousSecondToFirstRatio < 0.0
        || Options_.dAmbiguousSecondToFirstRatio > 1.0
        || Options_.dFaceNormalVoteWeight < 0.0)
    {
        _Result.Diagnostics.push_back("Extrude recognizes options are invalid");
        return _Result;
    }
    if (Geometry_.Edges.empty() && Geometry_.Faces.empty())
    {
        _Result.Diagnostics.push_back("BRep has no edges or faces");
        return _Result;
    }

    const SGeometryIndex _GeometryIndex(Geometry_);
    std::vector<SDirectionCluster> _Clusters;
    // Weight generator spans quadratically. Dense machining holes contribute
    // many short wall-depth edges and cylinders; summing their lengths alone
    // can overwhelm the continuous longitudinal generators of the tube.
    // Applying the same rule to both evidence sources keeps scale invariance.
    for (const auto& _Edge : Geometry_.Edges)
    {
        if (_Edge.Degenerated || _Edge.Curve3Id == 0)
        {
            continue;
        }
        const auto* _Curve = FindCurve(_GeometryIndex, _Edge.Curve3Id);
        if (!_Curve)
        {
            continue;
        }
        const auto _Direction = StraightEdgeDirection(*_Curve);
        if (!_Direction)
        {
            continue;
        }
        const auto _Length = SegmentLength(_GeometryIndex, _Edge, *_Curve);
        if (_Length > Options_.dMinimumEvidenceLength)
        {
            AddDirectionEvidence(
                _Clusters,
                *_Direction,
                _Length * _Length,
                Options_.dAngularToleranceRadians);
        }
    }

    for (const auto& _Face : Geometry_.Faces)
    {
        const auto* _Surface = FindSurface(_GeometryIndex, _Face.Surface3Id);
        if (!_Surface)
        {
            continue;
        }

        std::visit([&](IN const auto& _SurfaceValue)
        {
            using T = std::decay_t<decltype(_SurfaceValue)>;
            if constexpr (std::is_same_v<T, iCAX::GeometryData::CylindricalSurface3>)
            {
                const auto _Span = TrimmedFaceAxialSpan(
                    _Face, _GeometryIndex, _SurfaceValue.Placement.ZDirection);
                if (_Span > Options_.dMinimumEvidenceLength)
                {
                    AddDirectionEvidence(
                        _Clusters,
                        _SurfaceValue.Placement.ZDirection,
                        _Span * _Span,
                        Options_.dAngularToleranceRadians);
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::ExtrusionSurface3>)
            {
                const auto span = TrimmedFaceAxialSpan(_Face, _GeometryIndex, _SurfaceValue.Direction);
                if (span > Options_.dMinimumEvidenceLength)
                    AddDirectionEvidence(_Clusters, _SurfaceValue.Direction, span * span, Options_.dAngularToleranceRadians);
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::RevolutionSurface3>)
            {
                // A straight generator parallel to a revolution axis is a
                // cylinder. IGES may store cylinders in this exact form.
                const auto _Axis = ToVector(_SurfaceValue.Axis.Direction);
                const auto _Parallel = [&](const Vector3& Delta_) {
                    Direction3 _Direction;
                    return TryNormalize(Delta_, _Direction)
                        && std::abs(Dot(ToVector(_Direction), _Axis))
                            >= std::cos(Options_.dAngularToleranceRadians);
                };
                const bool _AxialLine = std::visit([&](const auto& Basis_) {
                    using C = std::decay_t<decltype(Basis_)>;
                    if constexpr (std::is_same_v<C, iCAX::GeometryData::Line3>
                        || std::is_same_v<C, iCAX::GeometryData::Ray3>)
                        return _Parallel(ToVector(Basis_.Axis.Direction));
                    else if constexpr (std::is_same_v<C, iCAX::GeometryData::Segment3>)
                        return _Parallel(Difference(Basis_.End, Basis_.Start));
                    else if constexpr (std::is_same_v<C, iCAX::GeometryData::BSpline3>
                        || std::is_same_v<C, iCAX::GeometryData::NURBS3>
                        || std::is_same_v<C, iCAX::GeometryData::Bezier3>)
                    {
                        if (Basis_.Poles.size() < 2) return false;
                        bool _HasLength = false;
                        for (const auto& _Pole : Basis_.Poles)
                        {
                            const auto _Delta = Difference(_Pole, Basis_.Poles.front());
                            if (Length(_Delta) <= Options_.dMinimumEvidenceLength) continue;
                            if (!_Parallel(_Delta)) return false;
                            _HasLength = true;
                        }
                        return _HasLength;
                    }
                    return false;
                }, _SurfaceValue.Basis);
                if (_AxialLine)
                {
                    const auto _Span = TrimmedFaceAxialSpan(_Face, _GeometryIndex, _SurfaceValue.Axis.Direction);
                    if (_Span > Options_.dMinimumEvidenceLength)
                        AddDirectionEvidence(_Clusters, _SurfaceValue.Axis.Direction,
                            _Span * _Span, Options_.dAngularToleranceRadians);
                }
            }
            else if constexpr (std::is_same_v<T, iCAX::GeometryData::BSplineSurface3>)
            {
                Direction3 _Direction;
                double _Span = 0.0;
                if (TryAddBSplineGenerators(
                    _SurfaceValue,
                    true,
                    Options_.dMinimumEvidenceLength,
                    Options_.dAngularToleranceRadians,
                    _Direction,
                    _Span))
                {
                    _Span = std::min(_Span,
                        TrimmedFaceAxialSpan(_Face, _GeometryIndex, _Direction));
                    if (_Span > Options_.dMinimumEvidenceLength)
                    {
                        AddDirectionEvidence(
                            _Clusters,
                            _Direction,
                            _Span * _Span,
                            Options_.dAngularToleranceRadians);
                    }
                }
                if (TryAddBSplineGenerators(
                    _SurfaceValue,
                    false,
                    Options_.dMinimumEvidenceLength,
                    Options_.dAngularToleranceRadians,
                    _Direction,
                    _Span))
                {
                    _Span = std::min(_Span,
                        TrimmedFaceAxialSpan(_Face, _GeometryIndex, _Direction));
                    if (_Span > Options_.dMinimumEvidenceLength)
                    {
                        AddDirectionEvidence(
                            _Clusters,
                            _Direction,
                            _Span * _Span,
                            Options_.dAngularToleranceRadians);
                    }
                }
            }
        }, *_Surface);
    }

    for (auto& _Cluster : _Clusters)
    {
        RefineClusterDirection(_Cluster);
    }
    if (_Clusters.empty())
    {
        _Result.Diagnostics.push_back("No straight-edge or parallel-generator direction was found");
        return _Result;
    }

    std::sort(
        _Clusters.begin(),
        _Clusters.end(),
        [](IN const auto& Left_, IN const auto& Right_) {
            return Left_.dWeight > Right_.dWeight;
        });
    const auto _PrimaryDirection = ClusterDirection(_Clusters.front());
    const auto _PrimaryWeight = _Clusters.front().dWeight;
    const auto _SecondaryWeight = _Clusters.size() > 1
        ? _Clusters[1].dWeight
        : 0.0;
    _Result.dPrimaryEvidenceWeight = _PrimaryWeight;
    _Result.dSecondaryEvidenceWeight = _SecondaryWeight;

    std::size_t _SelectedIndex = 0;
    std::optional<double> _EnvelopeSeparation;
    std::vector<double> _NormalVotes(_Clusters.size(), 0.0);
    bool _UsedCandidateSpan = false;
    // Equal votes cannot distinguish separate, collinear pieces: their edge
    // lengths do not include the distance between them. Compare actual trimmed
    // model spans along tied directions before using face-normal/envelope votes.
    // The weight allowance accounts only for floating-point summation roundoff.
    const auto _WeightRoundoff = 64 * std::numeric_limits<double>::epsilon()
        * std::max(1.0, _PrimaryWeight)
        * std::max<std::size_t>(1, Geometry_.Edges.size() + Geometry_.Faces.size());
    std::vector<std::size_t> _TiedCandidates;
    for (std::size_t i = 0; i < _Clusters.size(); ++i)
        if (std::abs(_Clusters[i].dWeight - _PrimaryWeight) <= _WeightRoundoff)
            _TiedCandidates.push_back(i);
    if (_TiedCandidates.size() > 1)
    {
        const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(Geometry_);
        if (_Built.bOK)
        {
            std::vector<std::pair<double, std::size_t>> _Spans;
            for (const auto i : _TiedCandidates)
            {
                const auto _Direction = ClusterDirection(_Clusters[i]);
                const auto _Frame = MakeAlignmentFrame(_Direction, ModelCenter(Geometry_, _GeometryIndex));
                const auto _Shape = BRepBuilderAPI_Transform(_Built.Shape, _Frame.NativeTransform, true).Shape();
                Bnd_Box _Bounds; BRepBndLib::AddOptimal(_Shape, _Bounds, false, false);
                double x, y, z, X, Y, Z; _Bounds.Get(x, y, z, X, Y, Z);
                _Spans.emplace_back(X - x, i);
                _Result.Diagnostics.push_back("Tied axis evidence: direction=("
                    + std::to_string(_Direction.X) + "," + std::to_string(_Direction.Y) + ","
                    + std::to_string(_Direction.Z) + ") weight=" + std::to_string(_Clusters[i].dWeight)
                    + " model-span=" + std::to_string(X - x) + " mm");
            }
            std::sort(_Spans.begin(), _Spans.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            if (_Spans[0].first - _Spans[1].first > Options_.dMinimumEvidenceLength)
            {
                _SelectedIndex = _Spans[0].second;
                _UsedCandidateSpan = true;
                _Result.Diagnostics.push_back("Selected the greatest model span among equal-evidence axis candidates: "
                    + std::to_string(_Spans[0].first) + " mm versus " + std::to_string(_Spans[1].first) + " mm");
            }
        }
    }
    const auto _IsAmbiguous = _Clusters.size() > 1
        && _SecondaryWeight >= _PrimaryWeight * Options_.dAmbiguousSecondToFirstRatio
        && std::abs(Dot(
            ToVector(_PrimaryDirection),
            ToVector(ClusterDirection(_Clusters[1]))))
            < std::cos(Options_.dAngularToleranceRadians * 2.0);
    if (_IsAmbiguous && !_UsedCandidateSpan)
    {
        _Result.bUsedFaceNormalDisambiguation = true;
        const auto _FaceNormalEvidence = CollectFaceNormalEvidence(
            Geometry_,
            _GeometryIndex);
        double _MaximumNormalVote = 0.0;
        for (std::size_t _Index = 0; _Index < _Clusters.size(); ++_Index)
        {
            _NormalVotes[_Index] = FaceNormalVote(
                _FaceNormalEvidence,
                ClusterDirection(_Clusters[_Index]));
            _MaximumNormalVote = std::max(_MaximumNormalVote, _NormalVotes[_Index]);
        }

        double _BestScore = -1.0;
        for (std::size_t _Index = 0; _Index < _Clusters.size(); ++_Index)
        {
            const auto _GeometryScore = _Clusters[_Index].dWeight / _PrimaryWeight;
            const auto _NormalScore = _MaximumNormalVote <= kEpsilon
                ? 0.0
                : _NormalVotes[_Index] / _MaximumNormalVote;
            const auto _Score = _GeometryScore
                + Options_.dFaceNormalVoteWeight * _NormalScore;
            if (_Score > _BestScore)
            {
                _BestScore = _Score;
                _SelectedIndex = _Index;
            }
        }
        // Edge lengths can be dominated by a wide cross-section on a short
        // extrusion. Among ambiguous directions, prefer the smallest complete
        // extruded material envelope, only after a Boolean containment check.
        // The comparison uses geometric tolerance bounds, not a tuned vote.
        struct SEnvelope { std::size_t Index; double Volume; double Uncertainty; };
        std::vector<SEnvelope> envelopes;
        bool allQualified = true;
        const SSectionWireOptions sectionOptions;
        for (std::size_t i = 0; i < _Clusters.size(); ++i)
        {
            if (_Clusters[i].dWeight < _PrimaryWeight * Options_.dAmbiguousSecondToFirstRatio) continue;
            try
            {
                const auto frame = MakeAlignmentFrame(ClusterDirection(_Clusters[i]), ModelCenter(Geometry_, _GeometryIndex));
                const auto aligned = AlignGeometry(Geometry_, frame);
                const auto section = direction_detail::ExtractCandidateSection(aligned, sectionOptions);
                double volume = 0, uncertainty = 0;
                if (!direction_detail::TryContainingExtrusionVolume(aligned, section, sectionOptions, volume, uncertainty))
                {
                    allQualified = false;
                    break;
                }
                envelopes.push_back({i, volume, uncertainty});
            }
            catch (const Standard_Failure& error)
            {
                _Result.Diagnostics.push_back(std::string("Could not qualify a competing extrusion envelope: ")
                    + error.GetMessageString());
                allQualified = false;
                break;
            }
        }
        if (allQualified && envelopes.size() > 1)
        {
            std::sort(envelopes.begin(), envelopes.end(), [](const auto& a, const auto& b) {
                return a.Volume < b.Volume;
            });
            const auto& first = envelopes[0]; const auto& second = envelopes[1];
            if (first.Volume + first.Uncertainty < second.Volume - second.Uncertainty)
            {
                _SelectedIndex = first.Index;
                _EnvelopeSeparation = (second.Volume - first.Volume) / second.Volume;
                _Result.bUsedFaceNormalDisambiguation = false;
                _Result.Diagnostics.push_back("Selected the smallest verified containing extrusion envelope: "
                    + std::to_string(first.Volume) + " mm3 versus " + std::to_string(second.Volume) + " mm3");
            }
        }
        if (_Result.bUsedFaceNormalDisambiguation)
            _Result.Diagnostics.push_back("Face normal area vote was used to disambiguate axis candidates");
    }

    _Result.Direction = ClusterDirection(_Clusters[_SelectedIndex]);
    _Result.CandidateDirections.push_back(_Result.Direction);
    for (const auto& _Cluster : _Clusters)
    {
        // Only directions strong enough to make the original vote ambiguous
        // may compete with it. A weak transverse face can otherwise turn an
        // unsupported long tube into a short solid blank plus large cutouts.
        if (_Cluster.dWeight < _PrimaryWeight * Options_.dAmbiguousSecondToFirstRatio)
            continue;
        const auto _Direction = ClusterDirection(_Cluster);
        if (std::none_of(_Result.CandidateDirections.begin(), _Result.CandidateDirections.end(),
            [&](const auto& Existing_) {
                return std::abs(Dot(ToVector(Existing_), ToVector(_Direction)))
                    >= std::cos(Options_.dAngularToleranceRadians);
            }))
        {
            _Result.CandidateDirections.push_back(_Direction);
        }
    }
    const auto _SelectedWeight = _Clusters[_SelectedIndex].dWeight;
    const auto _Separation = _EnvelopeSeparation ? *_EnvelopeSeparation : (_PrimaryWeight <= kEpsilon
        ? 0.0
        : (_SelectedWeight - _SecondaryWeight) / _PrimaryWeight);
    _Result.dConfidence = std::clamp(
        0.5 + 0.5 * _Separation,
        0.0,
        1.0);
    const auto _Center = ModelCenter(Geometry_, _GeometryIndex);
    const auto _Frame = MakeAlignmentFrame(_Result.Direction, _Center);
    _Result.TRSF = _Frame.TRSF;
    _Result.AlignedGeometry = AlignGeometry(Geometry_, _Frame);
    _Result.bSuccess = true;
    _Result.Diagnostics.push_back(
        "Axis recognized from " + std::to_string(_Clusters.size())
        + " unoriented direction clusters");
    _Result.Diagnostics.push_back(
        "BRep was aligned to +X and translated by its bounding-box center");
    return _Result;
}

SExtrusionDirectionResult CExtrudeRecognizesService::AlignCandidate(
    IN const BRepModel& Geometry_,
    IN const SExtrusionDirectionResult& Evidence_,
    IN std::size_t CandidateIndex_) const
{
    SExtrusionDirectionResult _Result;
    if (!Evidence_.bSuccess || CandidateIndex_ >= Evidence_.CandidateDirections.size())
    {
        _Result.Diagnostics.push_back("Requested extrusion axis has no direction evidence");
        return _Result;
    }
    _Result.Direction = Evidence_.CandidateDirections[CandidateIndex_];
    const SGeometryIndex _GeometryIndex(Geometry_);
    const auto _Frame = MakeAlignmentFrame(_Result.Direction, ModelCenter(Geometry_, _GeometryIndex));
    _Result.TRSF = _Frame.TRSF;
    _Result.AlignedGeometry = AlignGeometry(Geometry_, _Frame);
    _Result.bSuccess = true;
    _Result.Diagnostics.push_back("Aligned geometric extrusion-axis candidate "
        + std::to_string(CandidateIndex_) + " to +X without changing the direction vote");
    return _Result;
}
}
