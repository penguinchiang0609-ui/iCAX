#include "pch.h"
#include "FinalGeometryMeasurement.h"

#include <Bnd_OBB.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopLoc_Location.hxx>
#include <Standard_Failure.hxx>
#include <gp_Quaternion.hxx>

#include <array>
#include <limits>
#include <numbers>
#include <set>

namespace iCAX::TubeDesigner
{
namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;

    constexpr double kLinearTolerance = 0.02;
    constexpr double kAngularTolerance = 1.0e-4;

    struct SAxisExtent final
    {
        gp_Dir Direction;
        double HalfSize = 0.0;
    };

    struct SBodyFrame final
    {
        gp_Pnt Center;
        gp_Dir Axis;
        gp_Dir SectionX;
        gp_Dir SectionY;
        gp_Dir DimensionOffset;
        std::array<SAxisExtent, 3> Extents;
        std::size_t LengthAxisIndex = 0;
        double HalfLength = 0.0;
        double SectionWidth = 0.0;
        double SectionHeight = 0.0;
    };

    struct SMeasuredOpening final
    {
        std::string Kind = "through-opening";
        double Station = 0.0;
        gp_Pnt Center;
        gp_Dir Axis;
        gp_Dir FaceTangent;
        std::string Shape = "rectangle";
        double Diameter = 0.0;
        double SpanAlong = 0.0;
        double SpanAcross = 0.0;
        double CenterToNegativeEdge = 0.0;
        double CenterToPositiveEdge = 0.0;
        double NegativeEdgeClearance = 0.0;
        double PositiveEdgeClearance = 0.0;
        double Confidence = 1.0;
        std::size_t EvidenceCount = 1;
        bool HasCylindricalEvidence = false;
    };

    struct SCylinderEvidence final
    {
        gp_Pnt Location;
        gp_Dir Direction;
        double Radius = 0.0;
        double AngularCoverage = 0.0;
        std::size_t FaceCount = 1;
    };

    VariantArray PointArray(IN const gp_Pnt& Point_)
    {
        return { Point_.X(), Point_.Y(), Point_.Z() };
    }

    VariantArray DirectionArray(IN const gp_Dir& Direction_)
    {
        return { Direction_.X(), Direction_.Y(), Direction_.Z() };
    }

    struct SSidePoint final
    {
        double X = 0.0;
        double Y = 0.0;
    };

    VariantArray SidePointArray(IN const SSidePoint& Point_)
    {
        return { Point_.X, Point_.Y };
    }

    double Dot(IN const gp_Dir& Left_, IN const gp_Dir& Right_)
    {
        return Left_.X() * Right_.X()
            + Left_.Y() * Right_.Y()
            + Left_.Z() * Right_.Z();
    }

    double Dot(IN const gp_Vec& Left_, IN const gp_Dir& Right_)
    {
        return Left_.X() * Right_.X()
            + Left_.Y() * Right_.Y()
            + Left_.Z() * Right_.Z();
    }

    gp_Dir CanonicalDirection(IN const gp_Dir& Direction_)
    {
        auto _Result = Direction_;
        const auto _ShouldReverse = std::abs(_Result.X()) > kAngularTolerance
            ? _Result.X() < 0.0
            : (std::abs(_Result.Y()) > kAngularTolerance
                ? _Result.Y() < 0.0
                : _Result.Z() < 0.0);
        if (_ShouldReverse) _Result.Reverse();
        return _Result;
    }

    gp_Pnt AddScaled(IN const gp_Pnt& Point_, IN const gp_Dir& Direction_, IN double Scale_)
    {
        return Point_.Translated(gp_Vec(Direction_) * Scale_);
    }

    double Project(IN const gp_Pnt& Point_, IN const gp_Pnt& Origin_, IN const gp_Dir& Direction_)
    {
        return Dot(gp_Vec(Origin_, Point_), Direction_);
    }

    double HalfExtent(IN const SBodyFrame& Frame_, IN const gp_Dir& Direction_)
    {
        double _Extent = 0.0;
        for (const auto& _Axis : Frame_.Extents)
            _Extent += std::abs(Dot(_Axis.Direction, Direction_)) * _Axis.HalfSize;
        return _Extent;
    }

    std::vector<gp_Pnt> UniqueVertices(IN const TopoDS_Shape& Shape_);
    std::pair<double, double> ProjectionBounds(
        IN const std::vector<gp_Pnt>& Points_, IN const gp_Pnt& Origin_,
        IN const gp_Dir& Direction_);

    struct SFrameBounds final
    {
        std::array<double, 3> Minimum;
        std::array<double, 3> Maximum;
    };

    std::optional<SFrameBounds> MeasureFrameBounds(
        IN const TopoDS_Shape& Shape_, IN const gp_Pnt& Origin_,
        IN const gp_Dir& Axis_, IN const gp_Dir& SectionX_,
        IN const gp_Dir& SectionY_)
    {
        // Vertices only describe seams on circular edges, not curve extrema.
        // Bound the exact BRep in an orthonormal measurement frame instead;
        // neither display tessellation nor its deflection may affect dimensions.
        const auto _OriginVector = gp_Vec(gp_Pnt(), Origin_);
        gp_Trsf _ToFrame;
        _ToFrame.SetValues(
            Axis_.X(), Axis_.Y(), Axis_.Z(), -Dot(_OriginVector, Axis_),
            SectionX_.X(), SectionX_.Y(), SectionX_.Z(), -Dot(_OriginVector, SectionX_),
            SectionY_.X(), SectionY_.Y(), SectionY_.Z(), -Dot(_OriginVector, SectionY_));
        const auto _LocalShape = BRepBuilderAPI_Transform(Shape_, _ToFrame, false).Shape();
        Bnd_Box _Box;
        BRepBndLib::AddOptimal(_LocalShape, _Box, false, false);
        _Box.SetGap(0.0);
        if (_Box.IsVoid() || _Box.IsWhole()) return std::nullopt;
        SFrameBounds _Bounds;
        _Box.Get(_Bounds.Minimum[0], _Bounds.Minimum[1], _Bounds.Minimum[2],
            _Bounds.Maximum[0], _Bounds.Maximum[1], _Bounds.Maximum[2]);
        for (std::size_t _Index = 0; _Index < 3; ++_Index)
            if (!std::isfinite(_Bounds.Minimum[_Index])
                || !std::isfinite(_Bounds.Maximum[_Index])) return std::nullopt;
        return _Bounds;
    }

    std::vector<gp_Pnt> SideProjectionPoints(
        IN const TopoDS_Shape& Shape_, IN const SBodyFrame& Frame_)
    {
        auto _Points = UniqueVertices(Shape_);
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_EDGE); _Explorer.More(); _Explorer.Next())
        {
            BRepAdaptor_Curve _Curve(TopoDS::Edge(_Explorer.Current()));
            if (_Curve.GetType() == GeomAbs_Line) continue;
            const auto _First = _Curve.FirstParameter();
            const auto _Last = _Curve.LastParameter();
            if (!std::isfinite(_First) || !std::isfinite(_Last)) continue;
            for (int _Index = 0; _Index <= 64; ++_Index)
                _Points.push_back(_Curve.Value(_First + (_Last - _First) * _Index / 64.0));
            if (_Curve.GetType() != GeomAbs_Circle
                && _Curve.GetType() != GeomAbs_Ellipse) continue;
            const auto _Axes = _Curve.GetType() == GeomAbs_Circle
                ? _Curve.Circle().Position() : _Curve.Ellipse().Position();
            const auto _RadiusX = _Curve.GetType() == GeomAbs_Circle
                ? _Curve.Circle().Radius() : _Curve.Ellipse().MajorRadius();
            const auto _RadiusY = _Curve.GetType() == GeomAbs_Circle
                ? _Curve.Circle().Radius() : _Curve.Ellipse().MinorRadius();
            for (const auto& _Direction : { Frame_.Axis, Frame_.DimensionOffset })
            {
                const auto _Phase = std::atan2(
                    _RadiusY * Dot(_Axes.YDirection(), _Direction),
                    _RadiusX * Dot(_Axes.XDirection(), _Direction));
                const auto _Start = static_cast<int>(std::ceil((_First - _Phase) / std::numbers::pi));
                const auto _End = static_cast<int>(std::floor((_Last - _Phase) / std::numbers::pi));
                for (auto _Index = _Start; _Index <= _End; ++_Index)
                    _Points.push_back(_Curve.Value(_Phase + _Index * std::numbers::pi));
            }
        }
        return _Points;
    }

    double SideCross(
        IN const SSidePoint& Origin_, IN const SSidePoint& Left_,
        IN const SSidePoint& Right_)
    {
        return (Left_.X - Origin_.X) * (Right_.Y - Origin_.Y)
            - (Left_.Y - Origin_.Y) * (Right_.X - Origin_.X);
    }

    std::vector<SSidePoint> ConvexSideOutline(IN std::vector<SSidePoint> Points_)
    {
        std::sort(Points_.begin(), Points_.end(), [](const auto& Left_, const auto& Right_) {
            // Sorting must be strictly lexicographic. An epsilon tie combined
            // with a raw X comparison can make both a < b and b < a true.
            // Geometric tolerance belongs in the duplicate/hull checks below.
            return Left_.X < Right_.X
                || (Left_.X == Right_.X
                    && Left_.Y < Right_.Y);
        });
        Points_.erase(std::unique(Points_.begin(), Points_.end(), [](const auto& Left_, const auto& Right_) {
            // Do not discard an exact curve extremum just because a sampled
            // neighbour lies within the feature-recognition tolerance.
            return std::hypot(Left_.X - Right_.X, Left_.Y - Right_.Y)
                <= 1.0e-9;
        }), Points_.end());
        if (Points_.size() < 3) return {};

        std::vector<SSidePoint> _Hull;
        _Hull.reserve(Points_.size() * 2);
        for (const auto& _Point : Points_)
        {
            while (_Hull.size() >= 2
                && SideCross(_Hull[_Hull.size() - 2], _Hull.back(), _Point)
                    <= kLinearTolerance * kLinearTolerance)
                _Hull.pop_back();
            _Hull.push_back(_Point);
        }
        const auto _LowerSize = _Hull.size();
        for (auto _Iterator = Points_.rbegin() + 1; _Iterator != Points_.rend(); ++_Iterator)
        {
            while (_Hull.size() > _LowerSize
                && SideCross(_Hull[_Hull.size() - 2], _Hull.back(), *_Iterator)
                    <= kLinearTolerance * kLinearTolerance)
                _Hull.pop_back();
            _Hull.push_back(*_Iterator);
        }
        if (!_Hull.empty()) _Hull.pop_back();
        return _Hull.size() >= 3 ? _Hull : std::vector<SSidePoint>{};
    }

    bool SameSideSegment(
        IN const std::pair<SSidePoint, SSidePoint>& Left_,
        IN const std::pair<SSidePoint, SSidePoint>& Right_)
    {
        const auto _Near = [](const auto& LeftPoint_, const auto& RightPoint_) {
            return std::hypot(
                LeftPoint_.X - RightPoint_.X,
                LeftPoint_.Y - RightPoint_.Y) <= kLinearTolerance;
        };
        return (_Near(Left_.first, Right_.first) && _Near(Left_.second, Right_.second))
            || (_Near(Left_.first, Right_.second) && _Near(Left_.second, Right_.first));
    }

    ObjectMap MeasureSideProjection(
        IN const TopoDS_Shape& Shape_, IN const SBodyFrame& Frame_)
    {
        const auto _Vertices = SideProjectionPoints(Shape_, Frame_);
        if (_Vertices.empty()) return {};
        const auto& _Origin = Frame_.Center;
        const auto _AxisMinimum = -Frame_.HalfLength;
        const auto _VerticalMinimum = -HalfExtent(Frame_, Frame_.DimensionOffset);
        const auto _Width = Frame_.HalfLength * 2.0;
        const auto _Height = -_VerticalMinimum * 2.0;
        if (_Width <= kLinearTolerance || _Height <= kLinearTolerance) return {};

        const auto _Project = [&](IN const gp_Pnt& Point_) {
            return SSidePoint{
                Project(Point_, _Origin, Frame_.Axis) - _AxisMinimum,
                Project(Point_, _Origin, Frame_.DimensionOffset) - _VerticalMinimum
            };
        };
        std::vector<SSidePoint> _ProjectedVertices;
        _ProjectedVertices.reserve(_Vertices.size());
        for (const auto& _Vertex : _Vertices) _ProjectedVertices.push_back(_Project(_Vertex));
        auto _Outline = ConvexSideOutline(std::move(_ProjectedVertices));
        if (_Outline.empty())
        {
            _Outline = { { 0.0, 0.0 }, { _Width, 0.0 },
                { _Width, _Height }, { 0.0, _Height } };
        }

        std::vector<std::pair<SSidePoint, SSidePoint>> _Segments;
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_EDGE); _Explorer.More(); _Explorer.Next())
        {
            const auto _Edge = TopoDS::Edge(_Explorer.Current());
            BRepAdaptor_Curve _Curve(_Edge);
            if (_Curve.GetType() != GeomAbs_Line) continue;
            TopoDS_Vertex _FirstVertex;
            TopoDS_Vertex _LastVertex;
            TopExp::Vertices(_Edge, _FirstVertex, _LastVertex, true);
            if (_FirstVertex.IsNull() || _LastVertex.IsNull()) continue;
            const auto _Segment = std::pair{
                _Project(BRep_Tool::Pnt(_FirstVertex)),
                _Project(BRep_Tool::Pnt(_LastVertex))
            };
            if (std::hypot(
                    _Segment.first.X - _Segment.second.X,
                    _Segment.first.Y - _Segment.second.Y) <= kLinearTolerance)
                continue;
            if (std::none_of(
                _Segments.begin(), _Segments.end(),
                [&](const auto& Existing_) { return SameSideSegment(Existing_, _Segment); }))
                _Segments.push_back(_Segment);
        }

        VariantArray _OutlineValues;
        for (const auto& _Point : _Outline)
            _OutlineValues.emplace_back(SidePointArray(_Point));
        VariantArray _SegmentValues;
        for (const auto& [_Start, _End] : _Segments)
            _SegmentValues.emplace_back(ObjectMap{
                { "start", SidePointArray(_Start) },
                { "end", SidePointArray(_End) }
            });

        const auto _ViewVector = gp_Vec(Frame_.Axis).Crossed(
            gp_Vec(Frame_.DimensionOffset));
        if (_ViewVector.SquareMagnitude() <= 1.0e-12) return {};
        return ObjectMap{
            { "width", _Width },
            { "height", _Height },
            { "outline", std::move(_OutlineValues) },
            { "segments", std::move(_SegmentValues) },
            { "horizontalAxis", DirectionArray(Frame_.Axis) },
            { "verticalAxis", DirectionArray(Frame_.DimensionOffset) },
            { "viewDirection", DirectionArray(gp_Dir(_ViewVector)) },
            { "origin", PointArray(_Origin
                .Translated(gp_Vec(Frame_.Axis) * _AxisMinimum)
                .Translated(gp_Vec(Frame_.DimensionOffset) * _VerticalMinimum)) }
        };
    }

    std::optional<gp_Dir> MeasureLinearBodyAxis(IN const TopoDS_Shape& Shape_)
    {
        double _LongestEdge = 0.0;
        std::optional<gp_Dir> _Axis;
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_EDGE); _Explorer.More(); _Explorer.Next())
        {
            const auto _Edge = TopoDS::Edge(_Explorer.Current());
            BRepAdaptor_Curve _Curve(_Edge);
            if (_Curve.GetType() != GeomAbs_Line) continue;
            TopoDS_Vertex _FirstVertex;
            TopoDS_Vertex _LastVertex;
            TopExp::Vertices(_Edge, _FirstVertex, _LastVertex, true);
            if (_FirstVertex.IsNull() || _LastVertex.IsNull()) continue;
            const auto _First = BRep_Tool::Pnt(_FirstVertex);
            const auto _Last = BRep_Tool::Pnt(_LastVertex);
            const auto _Length = _First.Distance(_Last);
            if (_Length <= _LongestEdge + kLinearTolerance) continue;
            const auto _Direction = gp_Vec(_First, _Last);
            if (_Direction.SquareMagnitude() <= kLinearTolerance * kLinearTolerance) continue;
            _LongestEdge = _Length;
            _Axis = CanonicalDirection(gp_Dir(_Direction));
        }
        return _Axis;
    }

    std::optional<gp_Dir> MeasureSectionDirection(
        IN const TopoDS_Shape& Shape_, IN const gp_Dir& BodyAxis_)
    {
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
        {
            BRepAdaptor_Surface _Surface(TopoDS::Face(_Explorer.Current()), true);
            if (_Surface.GetType() != GeomAbs_Plane) continue;
            const auto _Normal = CanonicalDirection(_Surface.Plane().Axis().Direction());
            if (std::abs(Dot(_Normal, BodyAxis_)) <= kAngularTolerance)
                return _Normal;
        }
        return std::nullopt;
    }

    std::pair<double, double> ProjectionBounds(
        IN const std::vector<gp_Pnt>& Points_, IN const gp_Pnt& Origin_,
        IN const gp_Dir& Direction_)
    {
        auto _Minimum = (std::numeric_limits<double>::max)();
        auto _Maximum = (std::numeric_limits<double>::lowest)();
        for (const auto& _Point : Points_)
        {
            const auto _Value = Project(_Point, Origin_, Direction_);
            _Minimum = std::min(_Minimum, _Value);
            _Maximum = std::max(_Maximum, _Value);
        }
        return { _Minimum, _Maximum };
    }

    std::optional<SBodyFrame> MeasureBodyFrame(IN const TopoDS_Shape& Shape_)
    {
        Bnd_OBB _Bounds;
        BRepBndLib::AddOBB(Shape_, _Bounds, false, true, false);
        if (_Bounds.IsVoid()) return std::nullopt;

        SBodyFrame _Frame;
        const std::array _ObbExtents{
            SAxisExtent{ _Bounds.XDirection(), _Bounds.XHSize() },
            SAxisExtent{ _Bounds.YDirection(), _Bounds.YHSize() },
            SAxisExtent{ _Bounds.ZDirection(), _Bounds.ZHSize() } };
        auto _LongestObbAxis = std::size_t{ 0 };
        for (std::size_t _Index = 1; _Index < _ObbExtents.size(); ++_Index)
        {
            if (_ObbExtents[_Index].HalfSize > _ObbExtents[_LongestObbAxis].HalfSize)
                _LongestObbAxis = _Index;
        }
        _Frame.Axis = MeasureLinearBodyAxis(Shape_).value_or(
            CanonicalDirection(_ObbExtents[_LongestObbAxis].Direction));

        const auto _SectionDirection = MeasureSectionDirection(Shape_, _Frame.Axis);
        if (_SectionDirection)
        {
            _Frame.SectionX = gp_Dir(gp_Vec(*_SectionDirection)
                - gp_Vec(_Frame.Axis) * Dot(*_SectionDirection, _Frame.Axis));
            const auto _Other = gp_Vec(_Frame.Axis).Crossed(gp_Vec(_Frame.SectionX));
            if (_Other.SquareMagnitude() <= 1.0e-12) return std::nullopt;
            _Frame.SectionY = CanonicalDirection(gp_Dir(_Other));
        }
        else
        {
            gp_Vec _SectionVector;
            for (const auto& _Extent : _ObbExtents)
            {
                const auto _Perpendicular = gp_Vec(_Extent.Direction)
                    - gp_Vec(_Frame.Axis) * Dot(_Extent.Direction, _Frame.Axis);
                if (_Perpendicular.SquareMagnitude() > _SectionVector.SquareMagnitude())
                    _SectionVector = _Perpendicular;
            }
            if (_SectionVector.SquareMagnitude() <= 1.0e-12) return std::nullopt;
            _Frame.SectionX = CanonicalDirection(gp_Dir(_SectionVector));
            _Frame.SectionY = CanonicalDirection(gp_Dir(
                gp_Vec(_Frame.Axis).Crossed(gp_Vec(_Frame.SectionX))));
        }

        const gp_Pnt _Origin;
        const auto _ExactBounds = MeasureFrameBounds(
            Shape_, _Origin, _Frame.Axis, _Frame.SectionX, _Frame.SectionY);
        if (!_ExactBounds) return std::nullopt;
        const auto [_AxisMinimum, _SectionXMinimum, _SectionYMinimum] = _ExactBounds->Minimum;
        const auto [_AxisMaximum, _SectionXMaximum, _SectionYMaximum] = _ExactBounds->Maximum;
        _Frame.HalfLength = (_AxisMaximum - _AxisMinimum) * 0.5;
        _Frame.SectionWidth = _SectionXMaximum - _SectionXMinimum;
        _Frame.SectionHeight = _SectionYMaximum - _SectionYMinimum;
        _Frame.Center = _Origin
            .Translated(gp_Vec(_Frame.Axis) * ((_AxisMinimum + _AxisMaximum) * 0.5))
            .Translated(gp_Vec(_Frame.SectionX) * ((_SectionXMinimum + _SectionXMaximum) * 0.5))
            .Translated(gp_Vec(_Frame.SectionY) * ((_SectionYMinimum + _SectionYMaximum) * 0.5));
        _Frame.Extents = {
            SAxisExtent{ _Frame.Axis, _Frame.HalfLength },
            SAxisExtent{ _Frame.SectionX, _Frame.SectionWidth * 0.5 },
            SAxisExtent{ _Frame.SectionY, _Frame.SectionHeight * 0.5 } };
        _Frame.LengthAxisIndex = 0;
        _Frame.DimensionOffset = std::abs(_Frame.SectionX.Z())
            >= std::abs(_Frame.SectionY.Z()) ? _Frame.SectionX : _Frame.SectionY;
        return _Frame;
    }

    std::optional<std::pair<double, gp_Pnt>> ProjectFeatureAxisToBody(
        IN const SBodyFrame& Frame_, IN const gp_Pnt& FeaturePoint_,
        IN const gp_Dir& FeatureAxis_)
    {
        const auto _Start = AddScaled(Frame_.Center, Frame_.Axis, -Frame_.HalfLength);
        const auto _Offset = gp_Vec(_Start, FeaturePoint_);
        const auto _Parallel = Dot(Frame_.Axis, FeatureAxis_);
        const auto _Denominator = 1.0 - _Parallel * _Parallel;
        if (_Denominator <= 1.0e-8) return std::nullopt;
        const auto _AlongBody = Dot(_Offset, Frame_.Axis);
        const auto _AlongFeature = Dot(_Offset, FeatureAxis_);
        const auto _Station = (_AlongBody - _Parallel * _AlongFeature) / _Denominator;
        if (_Station < -kLinearTolerance
            || _Station > Frame_.HalfLength * 2.0 + kLinearTolerance)
            return std::nullopt;
        const auto _FeatureParameter = _Parallel * _Station - _AlongFeature;
        return std::pair{
            std::clamp(_Station, 0.0, Frame_.HalfLength * 2.0),
            AddScaled(FeaturePoint_, FeatureAxis_, _FeatureParameter)
        };
    }

    gp_Dir MakeFaceTangent(IN const gp_Dir& FeatureAxis_, IN const gp_Dir& BodyAxis_)
    {
        const auto _Tangent = gp_Vec(FeatureAxis_).Crossed(gp_Vec(BodyAxis_));
        if (_Tangent.SquareMagnitude() <= 1.0e-12)
            throw std::invalid_argument("feature axis is parallel to body axis");
        return CanonicalDirection(gp_Dir(_Tangent));
    }

    void CompleteEdgeDistances(IN const SBodyFrame& Frame_, IN OUT SMeasuredOpening& Opening_)
    {
        const auto _HalfFaceSpan = HalfExtent(Frame_, Opening_.FaceTangent);
        const auto _CenterOffset = Project(Opening_.Center, Frame_.Center, Opening_.FaceTangent);
        const auto _OpeningHalfAcross = Opening_.SpanAcross * 0.5;
        Opening_.CenterToNegativeEdge = std::max(0.0, _HalfFaceSpan + _CenterOffset);
        Opening_.CenterToPositiveEdge = std::max(0.0, _HalfFaceSpan - _CenterOffset);
        Opening_.NegativeEdgeClearance = std::max(
            0.0, Opening_.CenterToNegativeEdge - _OpeningHalfAcross);
        Opening_.PositiveEdgeClearance = std::max(
            0.0, Opening_.CenterToPositiveEdge - _OpeningHalfAcross);
    }

    bool SameFeatureLine(
        IN const gp_Pnt& LeftPoint_, IN const gp_Dir& LeftDirection_, IN double LeftRadius_,
        IN const gp_Pnt& RightPoint_, IN const gp_Dir& RightDirection_, IN double RightRadius_)
    {
        if (std::abs(Dot(LeftDirection_, RightDirection_)) < 1.0 - kAngularTolerance
            || std::abs(LeftRadius_ - RightRadius_) > kLinearTolerance)
            return false;
        return gp_Vec(LeftPoint_, RightPoint_).Crossed(gp_Vec(LeftDirection_)).Magnitude()
            <= kLinearTolerance * 2.0;
    }

    std::vector<SCylinderEvidence> CollectCylinderEvidence(IN const TopoDS_Shape& Shape_)
    {
        std::vector<SCylinderEvidence> _Cylinders;
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
        {
            BRepAdaptor_Surface _Surface(TopoDS::Face(_Explorer.Current()), true);
            if (_Surface.GetType() != GeomAbs_Cylinder) continue;
            const auto _Cylinder = _Surface.Cylinder();
            if (!std::isfinite(_Cylinder.Radius()) || _Cylinder.Radius() <= kLinearTolerance)
                continue;
            const auto _Direction = CanonicalDirection(_Cylinder.Axis().Direction());
            const auto _AngularCoverage = std::clamp(
                std::abs(_Surface.LastUParameter() - _Surface.FirstUParameter()),
                0.0, 2.0 * std::numbers::pi);
            const auto _Existing = std::find_if(
                _Cylinders.begin(), _Cylinders.end(),
                [&](IN const auto& Candidate_) {
                    return SameFeatureLine(
                        Candidate_.Location, Candidate_.Direction, Candidate_.Radius,
                        _Cylinder.Location(), _Direction, _Cylinder.Radius());
                });
            if (_Existing == _Cylinders.end())
                _Cylinders.push_back({
                    _Cylinder.Location(), _Direction, _Cylinder.Radius(), _AngularCoverage, 1 });
            else
            {
                _Existing->AngularCoverage = std::min(
                    2.0 * std::numbers::pi,
                    _Existing->AngularCoverage + _AngularCoverage);
                ++_Existing->FaceCount;
            }
        }
        return _Cylinders;
    }

    bool SameOpening(IN const SMeasuredOpening& Left_, IN const SMeasuredOpening& Right_)
    {
        return std::abs(Left_.Station - Right_.Station) <= kLinearTolerance * 2.0
            && Left_.Center.Distance(Right_.Center) <= kLinearTolerance * 4.0;
    }

    void MergeOpening(IN OUT std::vector<SMeasuredOpening>& Openings_, IN SMeasuredOpening Opening_)
    {
        const auto _Existing = std::find_if(
            Openings_.begin(), Openings_.end(),
            [&](IN const auto& Candidate_) { return SameOpening(Candidate_, Opening_); });
        if (_Existing == Openings_.end())
        {
            Openings_.push_back(std::move(Opening_));
            return;
        }
        _Existing->EvidenceCount += Opening_.EvidenceCount;
        _Existing->SpanAlong = std::max(_Existing->SpanAlong, Opening_.SpanAlong);
        _Existing->SpanAcross = std::max(_Existing->SpanAcross, Opening_.SpanAcross);
        _Existing->Confidence = std::max(_Existing->Confidence, Opening_.Confidence);
        if (Opening_.HasCylindricalEvidence)
        {
            _Existing->HasCylindricalEvidence = true;
            _Existing->Shape = "circle";
            _Existing->Diameter = Opening_.Diameter;
            _Existing->Axis = Opening_.Axis;
            _Existing->FaceTangent = Opening_.FaceTangent;
        }
    }

    std::vector<gp_Pnt> UniqueVertices(IN const TopoDS_Shape& Shape_)
    {
        std::vector<gp_Pnt> _Points;
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_VERTEX); _Explorer.More(); _Explorer.Next())
        {
            const auto _Point = BRep_Tool::Pnt(TopoDS::Vertex(_Explorer.Current()));
            if (std::none_of(
                _Points.begin(), _Points.end(),
                [&](IN const auto& Existing_) {
                    return Existing_.Distance(_Point) <= kLinearTolerance;
                }))
                _Points.push_back(_Point);
        }
        return _Points;
    }

    std::pair<double, double> PointSpan(
        IN const std::vector<gp_Pnt>& Points_, IN const gp_Dir& Direction_)
    {
        auto _Minimum = (std::numeric_limits<double>::max)();
        auto _Maximum = (std::numeric_limits<double>::lowest)();
        const auto& _Origin = Points_.front();
        for (const auto& _Point : Points_)
        {
            const auto _Value = Project(_Point, _Origin, Direction_);
            _Minimum = std::min(_Minimum, _Value);
            _Maximum = std::max(_Maximum, _Value);
        }
        return { _Minimum, _Maximum };
    }

    gp_Pnt AveragePoint(IN const std::vector<gp_Pnt>& Points_)
    {
        double _X = 0.0, _Y = 0.0, _Z = 0.0;
        for (const auto& _Point : Points_)
        {
            _X += _Point.X();
            _Y += _Point.Y();
            _Z += _Point.Z();
        }
        const auto _Scale = 1.0 / static_cast<double>(Points_.size());
        return { _X * _Scale, _Y * _Scale, _Z * _Scale };
    }

    void CollectPlanarOpeningLoops(
        IN const TopoDS_Shape& Shape_, IN const SBodyFrame& Frame_,
        IN OUT std::vector<SMeasuredOpening>& Openings_)
    {
        std::vector<SMeasuredOpening> _LoopCandidates;
        for (TopExp_Explorer _FaceExplorer(Shape_, TopAbs_FACE);
            _FaceExplorer.More(); _FaceExplorer.Next())
        {
            const auto _Face = TopoDS::Face(_FaceExplorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Plane) continue;
            const auto _Normal = CanonicalDirection(_Surface.Plane().Axis().Direction());
            if (std::abs(Dot(_Normal, Frame_.Axis)) > 0.05) continue;
            const auto _OuterWire = BRepTools::OuterWire(_Face);
            for (TopExp_Explorer _WireExplorer(_Face, TopAbs_WIRE);
                _WireExplorer.More(); _WireExplorer.Next())
            {
                const auto _Wire = TopoDS::Wire(_WireExplorer.Current());
                if (_Wire.IsSame(_OuterWire)) continue;
                const auto _Points = UniqueVertices(_Wire);
                if (_Points.size() < 3) continue;
                const auto _FaceTangent = MakeFaceTangent(_Normal, Frame_.Axis);
                const auto [_AlongMinimum, _AlongMaximum] = PointSpan(_Points, Frame_.Axis);
                const auto [_AcrossMinimum, _AcrossMaximum] = PointSpan(_Points, _FaceTangent);
                const auto _SpanAlong = _AlongMaximum - _AlongMinimum;
                const auto _SpanAcross = _AcrossMaximum - _AcrossMinimum;
                if (_SpanAlong <= kLinearTolerance || _SpanAcross <= kLinearTolerance) continue;
                const auto _Projected = ProjectFeatureAxisToBody(
                    Frame_, AveragePoint(_Points), _Normal);
                if (!_Projected) continue;

                SMeasuredOpening _Opening;
                _Opening.Station = _Projected->first;
                _Opening.Center = _Projected->second;
                _Opening.Axis = _Normal;
                _Opening.FaceTangent = _FaceTangent;
                _Opening.SpanAlong = _SpanAlong;
                _Opening.SpanAcross = _SpanAcross;
                _Opening.Confidence = 0.9;
                MergeOpening(_LoopCandidates, std::move(_Opening));
            }
        }
        for (auto& _Opening : _LoopCandidates)
        {
            if (_Opening.EvidenceCount < 2) continue;
            CompleteEdgeDistances(Frame_, _Opening);
            MergeOpening(Openings_, std::move(_Opening));
        }
    }

    struct SCrossPlaneEvidence final
    {
        double Station = 0.0;
        std::vector<gp_Pnt> Points;
        std::size_t FaceCount = 0;
    };

    void CollectOpenBoundarySlots(
        IN const TopoDS_Shape& Shape_, IN const SBodyFrame& Frame_,
        IN OUT std::vector<SMeasuredOpening>& Openings_)
    {
        const auto _Length = Frame_.HalfLength * 2.0;
        const auto _Start = AddScaled(Frame_.Center, Frame_.Axis, -Frame_.HalfLength);
        std::vector<SCrossPlaneEvidence> _Planes;
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Plane) continue;
            const auto _Normal = CanonicalDirection(_Surface.Plane().Axis().Direction());
            if (std::abs(Dot(_Normal, Frame_.Axis)) < 1.0 - kAngularTolerance) continue;
            const auto _Station = Project(_Surface.Plane().Location(), _Start, Frame_.Axis);
            if (_Station <= kLinearTolerance * 4.0
                || _Station >= _Length - kLinearTolerance * 4.0)
                continue;
            auto _Points = UniqueVertices(_Face);
            if (_Points.size() < 3) continue;
            const auto _Existing = std::find_if(
                _Planes.begin(), _Planes.end(),
                [&](IN const auto& Candidate_) {
                    return std::abs(Candidate_.Station - _Station) <= kLinearTolerance * 2.0;
                });
            if (_Existing == _Planes.end())
                _Planes.push_back({ _Station, std::move(_Points), 1 });
            else
            {
                _Existing->Points.insert(
                    _Existing->Points.end(), _Points.begin(), _Points.end());
                ++_Existing->FaceCount;
            }
        }
        std::sort(
            _Planes.begin(), _Planes.end(),
            [](IN const auto& Left_, IN const auto& Right_) {
                return Left_.Station < Right_.Station;
            });
        const auto _MaximumSlotSpan = std::max(
            2.0, std::max(Frame_.SectionWidth, Frame_.SectionHeight) * 1.5);
        for (std::size_t _Index = 0; _Index + 1 < _Planes.size();)
        {
            const auto& _First = _Planes[_Index];
            const auto& _Second = _Planes[_Index + 1];
            const auto _SpanAlong = _Second.Station - _First.Station;
            if (_SpanAlong <= kLinearTolerance || _SpanAlong > _MaximumSlotSpan)
            {
                ++_Index;
                continue;
            }
            std::vector<gp_Pnt> _Points = _First.Points;
            _Points.insert(_Points.end(), _Second.Points.begin(), _Second.Points.end());
            const auto [_SpanXMinimum, _SpanXMaximum] = PointSpan(_Points, Frame_.SectionX);
            const auto [_SpanYMinimum, _SpanYMaximum] = PointSpan(_Points, Frame_.SectionY);
            const auto _SpanX = _SpanXMaximum - _SpanXMinimum;
            const auto _SpanY = _SpanYMaximum - _SpanYMinimum;

            SMeasuredOpening _Opening;
            _Opening.Kind = "side-opening";
            _Opening.Station = (_First.Station + _Second.Station) * 0.5;
            _Opening.Center = AveragePoint(_Points);
            _Opening.Axis = _SpanX >= _SpanY ? Frame_.SectionX : Frame_.SectionY;
            _Opening.FaceTangent = _SpanX >= _SpanY ? Frame_.SectionY : Frame_.SectionX;
            _Opening.Shape = "rectangle";
            _Opening.SpanAlong = _SpanAlong;
            _Opening.SpanAcross = std::min(_SpanX, _SpanY);
            _Opening.EvidenceCount = _First.FaceCount + _Second.FaceCount;
            _Opening.Confidence = 0.82;
            CompleteEdgeDistances(Frame_, _Opening);
            MergeOpening(Openings_, std::move(_Opening));
            _Index += 2;
        }
    }

    void CollectCylindricalOpenings(
        IN const std::vector<SCylinderEvidence>& Cylinders_, IN const SBodyFrame& Frame_,
        IN OUT std::vector<SMeasuredOpening>& Openings_)
    {
        for (const auto& _Cylinder : Cylinders_)
        {
            if (std::abs(Dot(_Cylinder.Direction, Frame_.Axis)) > 0.05) continue;
            if (_Cylinder.AngularCoverage < 2.0 * std::numbers::pi - 0.05) continue;
            const auto _Projected = ProjectFeatureAxisToBody(
                Frame_, _Cylinder.Location, _Cylinder.Direction);
            if (!_Projected) continue;
            SMeasuredOpening _Opening;
            _Opening.Station = _Projected->first;
            _Opening.Center = _Projected->second;
            _Opening.Axis = _Cylinder.Direction;
            _Opening.FaceTangent = MakeFaceTangent(_Cylinder.Direction, Frame_.Axis);
            _Opening.Shape = "circle";
            _Opening.Diameter = _Cylinder.Radius * 2.0;
            _Opening.SpanAlong = _Opening.Diameter;
            _Opening.SpanAcross = _Opening.Diameter;
            _Opening.EvidenceCount = _Cylinder.FaceCount;
            _Opening.HasCylindricalEvidence = true;
            CompleteEdgeDistances(Frame_, _Opening);
            MergeOpening(Openings_, std::move(_Opening));
        }
    }

    std::optional<double> MeasureRectangularWallThickness(
        IN const TopoDS_Shape& Shape_, IN const SBodyFrame& Frame_)
    {
        std::optional<double> _Best;
        for (TopExp_Explorer _FaceExplorer(Shape_, TopAbs_FACE); _FaceExplorer.More(); _FaceExplorer.Next())
        {
            const auto _Face = TopoDS::Face(_FaceExplorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Plane
                || std::abs(Dot(_Surface.Plane().Axis().Direction(), Frame_.Axis)) < 1.0 - 1.0e-3)
                continue;

            std::vector<std::pair<double, double>> _Loops;
            for (TopExp_Explorer _WireExplorer(_Face, TopAbs_WIRE); _WireExplorer.More(); _WireExplorer.Next())
            {
                const auto _Points = UniqueVertices(_WireExplorer.Current());
                if (_Points.empty()) continue;
                const auto [_X0, _X1] = ProjectionBounds(_Points, _Points.front(), Frame_.SectionX);
                const auto [_Y0, _Y1] = ProjectionBounds(_Points, _Points.front(), Frame_.SectionY);
                const auto _Width = _X1 - _X0;
                const auto _Height = _Y1 - _Y0;
                if (std::isfinite(_Width) && std::isfinite(_Height)
                    && _Width > kLinearTolerance && _Height > kLinearTolerance)
                    _Loops.emplace_back(_Width, _Height);
            }
            if (_Loops.size() < 2) continue;
            std::sort(_Loops.begin(), _Loops.end(), [](const auto& Left_, const auto& Right_) {
                return Left_.first * Left_.second > Right_.first * Right_.second;
            });
            const auto [_OuterWidth, _OuterHeight] = _Loops.front();
            for (std::size_t _Index = 1; _Index < _Loops.size(); ++_Index)
            {
                const auto [_InnerWidth, _InnerHeight] = _Loops[_Index];
                const auto _Wall = std::min(
                    (_OuterWidth - _InnerWidth) * 0.5,
                    (_OuterHeight - _InnerHeight) * 0.5);
                if (_Wall > kLinearTolerance && (!_Best || _Wall < *_Best)) _Best = _Wall;
            }
        }
        return _Best;
    }

    ObjectMap MeasureSection(
        IN const TopoDS_Shape& Shape_, IN const SBodyFrame& Frame_,
        IN const std::vector<SCylinderEvidence>& Cylinders_)
    {
        ObjectMap _Section{
            { "shape", std::string("rectangle") },
            { "width", Frame_.SectionWidth },
            { "height", Frame_.SectionHeight },
            { "wallThickness", 0.0 }
        };
        std::vector<double> _CoaxialRadii;
        for (const auto& _Cylinder : Cylinders_)
        {
            if (std::abs(Dot(_Cylinder.Direction, Frame_.Axis)) < 1.0 - kAngularTolerance)
                continue;
            const auto _Offset = gp_Vec(Frame_.Center, _Cylinder.Location)
                .Crossed(gp_Vec(Frame_.Axis)).Magnitude();
            if (_Offset > kLinearTolerance * 4.0) continue;
            if (std::none_of(
                _CoaxialRadii.begin(), _CoaxialRadii.end(),
                [&](IN double Radius_) {
                    return std::abs(Radius_ - _Cylinder.Radius) <= kLinearTolerance;
                }))
                _CoaxialRadii.push_back(_Cylinder.Radius);
        }
        if (!_CoaxialRadii.empty())
        {
            std::sort(_CoaxialRadii.begin(), _CoaxialRadii.end(), std::greater<>());
            _Section["shape"] = std::string("circle");
            _Section["diameter"] = _CoaxialRadii.front() * 2.0;
            _Section["width"] = _CoaxialRadii.front() * 2.0;
            _Section["height"] = _CoaxialRadii.front() * 2.0;
            if (_CoaxialRadii.size() > 1)
                _Section["wallThickness"] = std::max(0.0, _CoaxialRadii[0] - _CoaxialRadii[1]);
        }
        else if (const auto _WallThickness = MeasureRectangularWallThickness(Shape_, Frame_))
        {
            _Section["wallThickness"] = *_WallThickness;
            _Section["hollow"] = true;
        }
        return _Section;
    }

    struct SKnifePlane final
    {
        double GradientY = 0.0;
        double GradientZ = 0.0;
        double Offset = 0.0;
        double Radius = 0.0;
        double Score = 0.0;
    };

    std::optional<std::array<double, 6>> KnifeBounds(const TopoDS_Shape& Shape_, bool UseTolerance_ = true)
    {
        Bnd_Box _Bounds;
        // Analytic BRep bounds, including topology tolerances; never preview mesh
        // or just vertices (curved faces can protrude between sampled points).
        BRepBndLib::AddOptimal(Shape_, _Bounds, false, UseTolerance_);
        _Bounds.SetGap(0.0);
        if (_Bounds.IsVoid() || _Bounds.IsWhole()) return std::nullopt;
        std::array<double, 6> _Values;
        _Bounds.Get(_Values[0], _Values[1], _Values[2], _Values[3], _Values[4], _Values[5]);
        for (const auto _Value : _Values)
            if (!std::isfinite(_Value)) return std::nullopt;
        return _Values;
    }

    std::vector<gp_Pnt> KnifeSearchWitnesses(const TopoDS_Shape& Shape_)
    {
        std::vector<gp_Pnt> _Points;
        for (TopExp_Explorer _It(Shape_, TopAbs_EDGE); _It.More() && _Points.size() < 8192; _It.Next())
        {
            try
            {
                BRepAdaptor_Curve _Curve(TopoDS::Edge(_It.Current()));
                const auto _First = _Curve.FirstParameter(), _Last = _Curve.LastParameter();
                if (!std::isfinite(_First) || !std::isfinite(_Last)) continue;
                const int _Steps = _Curve.GetType() == GeomAbs_Line ? 1 : 16;
                for (int _Index = 0; _Index <= _Steps; ++_Index)
                    _Points.push_back(_Curve.Value(_First + (_Last - _First) * _Index / _Steps));
            }
            catch (const Standard_Failure&) { /* Optional search witnesses only. */ }
        }
        return _Points;
    }

    double KnifeScore(double Inset_, double Radius_)
    {
        // Reward less blank material, but charge for extending beyond the initial
        // axial AABB. This keeps a marginally useful steep tangent from producing
        // an unnecessarily long blank. The straight-cut baseline scores zero.
        return Inset_ - std::max(0.0, Radius_ - Inset_);
    }

    std::pair<SKnifePlane, SKnifePlane> SearchKnifePlanes(
        const TopoDS_Shape& CenteredShape_, const std::array<double, 6>& Bounds_)
    {
        const double _X0 = Bounds_[0], _X1 = Bounds_[3];
        const double _HalfY = (Bounds_[4] - Bounds_[1]) * 0.5;
        const double _HalfZ = (Bounds_[5] - Bounds_[2]) * 0.5;
        SKnifePlane _Left{ 0, 0, _X0 }, _Right{ 0, 0, _X1 };
        const auto _Witnesses = KnifeSearchWitnesses(CenteredShape_);
        std::set<std::pair<long long, long long>> _Visited;
        const auto _Try = [&](double A_, double B_) {
            // Bounded deterministic search, not a machine's angular capability.
            if (!std::isfinite(A_) || !std::isfinite(B_) || std::hypot(A_, B_) > 8.0) return;
            const auto _Key = std::pair{ std::llround(A_ * 100000.0), std::llround(B_ * 100000.0) };
            if (!_Visited.insert(_Key).second || (_Key.first == 0 && _Key.second == 0)) return;
            // Quantize BEFORE verifying support, so equal plane signatures really
            // mean the same tested direction, including on wide tube sections.
            A_ = _Key.first / 100000.0;
            B_ = _Key.second / 100000.0;
            const auto _Radius = std::abs(A_) * _HalfY + std::abs(B_) * _HalfZ;
            if (!_Witnesses.empty())
            {
                double _Min = std::numeric_limits<double>::infinity(), _Max = -_Min;
                for (const auto& _Point : _Witnesses)
                {
                    const auto _Q = _Point.X() - A_ * _Point.Y() - B_ * _Point.Z();
                    _Min = std::min(_Min, _Q); _Max = std::max(_Max, _Q);
                }
                // A subset of actual shape points gives an UPPER bound on the
                // achievable improvement. It can prune but never certify a blade.
                if (KnifeScore(_Min - _X0, _Radius) <= _Left.Score + kLinearTolerance
                    && KnifeScore(_X1 - _Max, _Radius) <= _Right.Score + kLinearTolerance) return;
            }
            try
            {
                const gp_Vec _Normal(1.0, -A_, -B_);
                gp_Trsf _Rotation;
                _Rotation.SetRotation(gp_Quaternion(_Normal, gp_Vec(1.0, 0.0, 0.0)));
                // Rotated X extrema solve the blade's translation directly:
                // min/max(x-a*y-b*z), equivalent to moving a knife until contact.
                // A Location shares topology; no copied result BRep is generated.
                const auto _Projected = KnifeBounds(CenteredShape_.Moved(TopLoc_Location(_Rotation)));
                if (!_Projected) return;
                constexpr double _Safety = 1.0e-5;
                const double _Min = (*_Projected)[0] * _Normal.Magnitude() - _Safety;
                const double _Max = (*_Projected)[3] * _Normal.Magnitude() + _Safety;
                const auto _LeftScore = KnifeScore(_Min - _X0, _Radius);
                const auto _RightScore = KnifeScore(_X1 - _Max, _Radius);
                if (_LeftScore > _Left.Score + kLinearTolerance)
                    _Left = { A_, B_, _Min, _Radius, _LeftScore };
                if (_RightScore > _Right.Score + kLinearTolerance)
                    _Right = { A_, B_, _Max, _Radius, _RightScore };
            }
            catch (const Standard_Failure&) { /* Keep the already verified baseline. */ }
        };

        // Existing planar faces are useful seeds, not an eligibility test. There
        // need not be any planar end face, let alone one spanning the whole tube.
        for (TopExp_Explorer _It(CenteredShape_, TopAbs_FACE); _It.More(); _It.Next())
        {
            try
            {
                BRepAdaptor_Surface _Surface(TopoDS::Face(_It.Current()), true);
                if (_Surface.GetType() != GeomAbs_Plane) continue;
                const auto _Normal = _Surface.Plane().Axis().Direction();
                if (std::abs(_Normal.X()) > 1.0e-5)
                    _Try(-_Normal.Y() / _Normal.X(), -_Normal.Z() / _Normal.X());
            }
            catch (const Standard_Failure&) {}
        }
        constexpr std::array _Slopes{ -4.0, -2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0, 4.0 };
        for (const auto _A : _Slopes)
            for (const auto _B : _Slopes) _Try(_A, _B);
        for (double _Step = 0.25; _Step >= 0.0009; _Step *= 0.25)
        {
            const std::array _Seeds{ _Left, _Right };
            for (const auto& _Seed : _Seeds)
                for (int _Y = -1; _Y <= 1; ++_Y)
                    for (int _Z = -1; _Z <= 1; ++_Z)
                        _Try(_Seed.GradientY + _Y * _Step, _Seed.GradientZ + _Z * _Step);
        }
        return { _Left, _Right };
    }
}

TopoDS_Shape NormalizeLinearPartForManufacturing(IN const TopoDS_Shape& Shape_)
{
    if (Shape_.IsNull()) return Shape_;
    const auto _Frame = MeasureBodyFrame(Shape_);
    if (!_Frame || _Frame->HalfLength <= kLinearTolerance) return Shape_;

    gp_Trsf _Rotation;
    _Rotation.SetRotation(gp_Quaternion(gp_Vec(_Frame->Axis), gp_Vec(1.0, 0.0, 0.0)));
    auto _Normalized = BRepBuilderAPI_Transform(Shape_, _Rotation, true).Shape();
    // A closed circular edge has only one seam vertex; vertex extents are not
    // section extents and would translate a round tube away from its centre.
    Bnd_Box _Box;
    BRepBndLib::AddOptimal(_Normalized, _Box, false, false);
    _Box.SetGap(0);
    if (_Box.IsVoid() || _Box.IsWhole()) return _Normalized;
    double _MinimumX, _MinimumY, _MinimumZ, _MaximumX, _MaximumY, _MaximumZ;
    _Box.Get(_MinimumX, _MinimumY, _MinimumZ, _MaximumX, _MaximumY, _MaximumZ);
    gp_Trsf _Translation;
    _Translation.SetTranslation(gp_Vec(
        -(_MinimumX + _MaximumX) * 0.5,
        -(_MinimumY + _MaximumY) * 0.5,
        -(_MinimumZ + _MaximumZ) * 0.5));
    return BRepBuilderAPI_Transform(_Normalized, _Translation, true).Shape();
}

SLinearNestingGeometry MeasureLinearNestingGeometry(IN const TopoDS_Shape& Shape_)
{
    SLinearNestingGeometry _Result;
    if (Shape_.IsNull()) return _Result;
    try
    {
        // Preserve the original straight AABB contract: adding topology tolerance
        // here could round an exact 400 mm straight part up to 400.01 mm.
        const auto _Bounds = KnifeBounds(Shape_, false);
        if (!_Bounds) return _Result;
        const auto& _B = *_Bounds;
        const double _Length = _B[3] - _B[0];
        if (_Length <= kLinearTolerance || _B[4] - _B[1] <= kLinearTolerance
            || _B[5] - _B[2] <= kLinearTolerance) return _Result;
        const gp_Vec _Center((_B[0] + _B[3]) * 0.5,
            (_B[1] + _B[4]) * 0.5, (_B[2] + _B[5]) * 0.5);
        gp_Trsf _Translation;
        _Translation.SetTranslation(-_Center);
        auto _CenteredBounds = _B;
        for (int _Axis = 0; _Axis < 3; ++_Axis)
        {
            _CenteredBounds[_Axis] -= _Center.Coord(_Axis + 1);
            _CenteredBounds[_Axis + 3] -= _Center.Coord(_Axis + 1);
        }
        // Establish a valid straight-cut fallback before probing any tilted knife.
        _Result.IsReliable = true;
        _Result.EnvelopeLength = _Result.MaterialEquivalentLength = _Length;
        _Result.AxialMinimum = _B[0]; _Result.AxialMaximum = _B[3];
        _Result.Left = { true, 0, 0, 0, _B[0] };
        _Result.Right = { true, 0, 0, 0, _B[3] };
        const auto [_Left, _Right] = SearchKnifePlanes(
            Shape_.Moved(TopLoc_Location(_Translation)), _CenteredBounds);
        const auto _Minimum = std::min(_CenteredBounds[0], _Left.Offset - _Left.Radius);
        const auto _Maximum = std::max(_CenteredBounds[3], _Right.Offset + _Right.Radius);
        const auto _Envelope = _Maximum - _Minimum;
        const auto _LeftProjection = _Left.Radius * 2.0;
        const auto _RightProjection = _Right.Radius * 2.0;
        // The two knife planes must leave a nonempty full-section central band.
        // Unsupported very short/steep combinations stay on the straight strategy.
        if (_Envelope - _LeftProjection - _RightProjection <= kLinearTolerance * 2.0)
            return _Result;
        const auto _End = [&](const SKnifePlane& Plane_) {
            return SPlanarNestingEnd{ true, Plane_.Radius * 2.0,
                Plane_.GradientY, Plane_.GradientZ,
                Plane_.Offset + _Center.X()
                    - Plane_.GradientY * _Center.Y() - Plane_.GradientZ * _Center.Z() };
        };
        _Result.EnvelopeLength = _Envelope;
        _Result.MaterialEquivalentLength = _Envelope - (_LeftProjection + _RightProjection) * 0.5;
        _Result.AxialMinimum = _Minimum + _Center.X();
        _Result.AxialMaximum = _Maximum + _Center.X();
        _Result.Left = _End(_Left); _Result.Right = _End(_Right);
    }
    catch (const Standard_Failure&) { /* A failed probe never enables overlap. */ }
    return _Result;
}

ObjectMap MeasureFinalPlateGeometry(
    IN const TopoDS_Shape& Shape_,
    IN const std::string& strResourceID_,
    IN std::uint64_t nResourceVersion_)
{
    auto _Result = MeasureFinalPartGeometry(Shape_, strResourceID_, nResourceVersion_);
    _Result["partKind"] = std::string("plate");
    const auto _Frame = Shape_.IsNull() ? std::optional<SBodyFrame>{} : MeasureBodyFrame(Shape_);
    if (!_Frame) return _Result;
    auto _Extents = _Frame->Extents;
    std::sort(_Extents.begin(), _Extents.end(), [](const auto& Left_, const auto& Right_) {
        return Left_.HalfSize > Right_.HalfSize;
    });
    ObjectMap _Plate{
        { "longSide", _Extents[0].HalfSize * 2.0 },
        { "shortSide", _Extents[1].HalfSize * 2.0 },
        { "thickness", _Extents[2].HalfSize * 2.0 },
        { "center", PointArray(_Frame->Center) },
        { "axes", VariantArray{ DirectionArray(_Extents[0].Direction),
            DirectionArray(_Extents[1].Direction), DirectionArray(_Extents[2].Direction) } }
    };
    _Result["plate"] = std::move(_Plate);
    _Result["section"] = ObjectMap{ { "kind", std::string("plate") } };
    return _Result;
}

ObjectMap MeasureFinalPartGeometry(
    IN const TopoDS_Shape& Shape_,
    IN const std::string& strResourceID_,
    IN std::uint64_t nResourceVersion_)
{
    ObjectMap _Result{
        { "source", std::string("final-brep") },
        { "resourceId", strResourceID_ },
        { "resourceVersion", static_cast<unsigned long long>(nResourceVersion_) },
        { "available", false }
    };
    if (Shape_.IsNull())
    {
        _Result["message"] = std::string("最终零件几何为空");
        return _Result;
    }

    const auto _Frame = MeasureBodyFrame(Shape_);
    if (!_Frame || _Frame->HalfLength <= kLinearTolerance)
    {
        _Result["message"] = std::string("无法从最终几何确定零件主方向");
        return _Result;
    }

    const auto _Length = _Frame->HalfLength * 2.0;
    const auto _Start = AddScaled(_Frame->Center, _Frame->Axis, -_Frame->HalfLength);
    const auto _End = AddScaled(_Frame->Center, _Frame->Axis, _Frame->HalfLength);
    const auto _SideVerticalMinimum = -HalfExtent(*_Frame, _Frame->DimensionOffset);
    const auto _SideViewVector = gp_Vec(_Frame->Axis).Crossed(
        gp_Vec(_Frame->DimensionOffset));
    const auto _SideViewDirection = gp_Dir(_SideViewVector);
    ObjectMap _Reference{
        { "kind", std::string("linear") },
        { "start", PointArray(_Start) },
        { "end", PointArray(_End) },
        { "sectionAxes", VariantArray{
            DirectionArray(_Frame->SectionX), DirectionArray(_Frame->SectionY) } },
        { "dimensionOffsetDirection", DirectionArray(_Frame->DimensionOffset) }
    };

    const auto _Cylinders = CollectCylinderEvidence(Shape_);
    std::vector<SMeasuredOpening> _Openings;
    CollectPlanarOpeningLoops(Shape_, *_Frame, _Openings);
    CollectCylindricalOpenings(_Cylinders, *_Frame, _Openings);
    CollectOpenBoundarySlots(Shape_, *_Frame, _Openings);
    for (auto& _Opening : _Openings) CompleteEdgeDistances(*_Frame, _Opening);
    std::sort(
        _Openings.begin(), _Openings.end(),
        [](IN const auto& Left_, IN const auto& Right_) {
            return Left_.Station < Right_.Station;
        });

    VariantArray _Features;
    for (const auto& _Opening : _Openings)
    {
        ObjectMap _Feature{
            { "kind", _Opening.Kind },
            { "center", PointArray(_Opening.Center) },
            { "axis", DirectionArray(_Opening.Axis) },
            { "station", _Opening.Station },
            { "shape", _Opening.Shape },
            { "openingSpanAlong", _Opening.SpanAlong },
            { "openingSpanAcross", _Opening.SpanAcross },
            { "faceTangent", DirectionArray(_Opening.FaceTangent) },
            { "centerToFaceEdgeNegative", _Opening.CenterToNegativeEdge },
            { "centerToFaceEdgePositive", _Opening.CenterToPositiveEdge },
            { "faceEdgeClearanceNegative", _Opening.NegativeEdgeClearance },
            { "faceEdgeClearancePositive", _Opening.PositiveEdgeClearance },
            { "confidence", _Opening.Confidence },
            { "evidenceCount", static_cast<unsigned long long>(_Opening.EvidenceCount) }
        };
        _Feature["sideCenter"] = VariantArray{
            _Opening.Station,
            Project(_Opening.Center, _Frame->Center, _Frame->DimensionOffset)
                - _SideVerticalMinimum
        };
        _Feature["sideSpanAcross"] = _Opening.SpanAcross
            * std::abs(Dot(_Opening.FaceTangent, _Frame->DimensionOffset));
        _Feature["sideVisible"] = std::abs(Dot(_Opening.Axis, _SideViewDirection)) >= 0.85;
        if (_Opening.Diameter > 0.0) _Feature["diameter"] = _Opening.Diameter;
        _Features.emplace_back(std::move(_Feature));
    }

    _Result["available"] = true;
    _Result["recognitionStatus"] = std::string("direct-topology");
    _Result["confidence"] = 1.0;
    _Result["relativeGeometryError"] = 0.0;
    _Result["length"] = _Length;
    _Result["linearReference"] = std::move(_Reference);
    _Result["sideProjection"] = MeasureSideProjection(Shape_, *_Frame);
    _Result["section"] = MeasureSection(Shape_, *_Frame, _Cylinders);
    _Result["features"] = std::move(_Features);
    _Result["openingCount"] = static_cast<unsigned long long>(_Openings.size());
    return _Result;
}
}
