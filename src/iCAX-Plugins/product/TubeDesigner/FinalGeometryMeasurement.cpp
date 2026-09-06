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
#include <gp_Quaternion.hxx>

#include <array>
#include <numbers>

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
        BRepBndLib::AddOBB(Shape_, _Bounds, true, true, true);
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
            _Frame.SectionX = *_SectionDirection;
            const auto _Other = gp_Vec(_Frame.Axis).Crossed(gp_Vec(_Frame.SectionX));
            if (_Other.SquareMagnitude() <= 1.0e-12) return std::nullopt;
            _Frame.SectionY = CanonicalDirection(gp_Dir(_Other));
        }
        else
        {
            std::array<SAxisExtent, 2> _SectionExtents{};
            std::size_t _SectionCursor = 0;
            for (const auto& _Extent : _ObbExtents)
            {
                if (std::abs(Dot(_Extent.Direction, _Frame.Axis)) > 1.0 - 1.0e-3) continue;
                if (_SectionCursor < _SectionExtents.size())
                    _SectionExtents[_SectionCursor++] = _Extent;
            }
            if (_SectionCursor < 2) return std::nullopt;
            _Frame.SectionX = CanonicalDirection(_SectionExtents[0].Direction);
            _Frame.SectionY = CanonicalDirection(_SectionExtents[1].Direction);
        }

        const auto _Points = UniqueVertices(Shape_);
        if (_Points.empty()) return std::nullopt;
        const auto& _Origin = _Points.front();
        const auto [_AxisMinimum, _AxisMaximum] = ProjectionBounds(
            _Points, _Origin, _Frame.Axis);
        const auto [_SectionXMinimum, _SectionXMaximum] = ProjectionBounds(
            _Points, _Origin, _Frame.SectionX);
        const auto [_SectionYMinimum, _SectionYMaximum] = ProjectionBounds(
            _Points, _Origin, _Frame.SectionY);
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

    ObjectMap MeasureSection(
        IN const SBodyFrame& Frame_, IN const std::vector<SCylinderEvidence>& Cylinders_)
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
        return _Section;
    }

    struct SEndPlaneCandidate final
    {
        double MinimumX = 0.0;
        double MaximumX = 0.0;
        double CenterX = 0.0;
        double SpanY = 0.0;
        double SpanZ = 0.0;
        double GradientY = 0.0;
        double GradientZ = 0.0;
    };

    std::optional<SEndPlaneCandidate> MakeEndPlaneCandidate(
        IN const TopoDS_Face& Face_)
    {
        BRepAdaptor_Surface _Surface(Face_, true);
        if (_Surface.GetType() != GeomAbs_Plane) return std::nullopt;
        const auto _Normal = _Surface.Plane().Axis().Direction();
        if (std::abs(_Normal.X()) <= 1.0e-5) return std::nullopt;

        Bnd_Box _Bounds;
        BRepBndLib::AddOptimal(Face_, _Bounds, false, false);
        _Bounds.SetGap(0.0);
        if (_Bounds.IsVoid() || _Bounds.IsWhole()) return std::nullopt;
        double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
        _Bounds.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
        if (!std::isfinite(_X0 + _Y0 + _Z0 + _X1 + _Y1 + _Z1)) return std::nullopt;
        return SEndPlaneCandidate{
            _X0, _X1, (_X0 + _X1) * 0.5, _Y1 - _Y0, _Z1 - _Z0,
            -_Normal.Y() / _Normal.X(), -_Normal.Z() / _Normal.X()
        };
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
    const auto _Points = UniqueVertices(_Normalized);
    if (_Points.empty()) return _Normalized;

    auto _MinimumX = (std::numeric_limits<double>::max)();
    auto _MinimumY = (std::numeric_limits<double>::max)();
    auto _MinimumZ = (std::numeric_limits<double>::max)();
    auto _MaximumY = (std::numeric_limits<double>::lowest)();
    auto _MaximumZ = (std::numeric_limits<double>::lowest)();
    auto _MaximumX = (std::numeric_limits<double>::lowest)();
    for (const auto& _Point : _Points)
    {
        _MinimumX = std::min(_MinimumX, _Point.X());
        _MaximumX = std::max(_MaximumX, _Point.X());
        _MinimumY = std::min(_MinimumY, _Point.Y());
        _MinimumZ = std::min(_MinimumZ, _Point.Z());
        _MaximumY = std::max(_MaximumY, _Point.Y());
        _MaximumZ = std::max(_MaximumZ, _Point.Z());
    }
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

    Bnd_Box _Bounds;
    BRepBndLib::AddOptimal(Shape_, _Bounds, false, false);
    _Bounds.SetGap(0.0);
    if (_Bounds.IsVoid() || _Bounds.IsWhole()) return _Result;
    double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
    _Bounds.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
    const auto _Envelope = _X1 - _X0;
    const auto _SectionY = _Y1 - _Y0;
    const auto _SectionZ = _Z1 - _Z0;
    if (!std::isfinite(_Envelope + _SectionY + _SectionZ)
        || _Envelope <= kLinearTolerance
        || _SectionY <= kLinearTolerance
        || _SectionZ <= kLinearTolerance)
        return _Result;

    std::optional<SEndPlaneCandidate> _Left;
    std::optional<SEndPlaneCandidate> _Right;
    for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
    {
        const auto _Candidate = MakeEndPlaneCandidate(TopoDS::Face(_Explorer.Current()));
        if (!_Candidate) continue;
        // End faces span the section in both directions. This rejects planar
        // walls and almost all hole/slot faces before looking at axial position.
        if (_Candidate->SpanY < _SectionY * 0.75
            || _Candidate->SpanZ < _SectionZ * 0.75)
            continue;
        if (_Candidate->MinimumX <= _X0 + kLinearTolerance * 4.0
            && (!_Left || _Candidate->CenterX < _Left->CenterX))
            _Left = _Candidate;
        if (_Candidate->MaximumX >= _X1 - kLinearTolerance * 4.0
            && (!_Right || _Candidate->CenterX > _Right->CenterX))
            _Right = _Candidate;
    }
    if (!_Left || !_Right || _Left->CenterX >= _Right->CenterX - kLinearTolerance)
        return _Result;

    const auto _LeftProjection = std::max(0.0, _Left->MaximumX - _Left->MinimumX);
    const auto _RightProjection = std::max(0.0, _Right->MaximumX - _Right->MinimumX);
    const auto _CentralLength = _Envelope - _LeftProjection - _RightProjection;
    if (_CentralLength <= kLinearTolerance * 2.0) return _Result;

    _Result.IsReliable = true;
    _Result.EnvelopeLength = _Envelope;
    _Result.MaterialEquivalentLength =
        _CentralLength + (_LeftProjection + _RightProjection) * 0.5;
    _Result.Left = {
        true,
        _LeftProjection <= kLinearTolerance ? 0.0 : _LeftProjection,
        _Left->GradientY,
        _Left->GradientZ
    };
    _Result.Right = {
        true,
        _RightProjection <= kLinearTolerance ? 0.0 : _RightProjection,
        _Right->GradientY,
        _Right->GradientZ
    };
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
        if (_Opening.Diameter > 0.0) _Feature["diameter"] = _Opening.Diameter;
        _Features.emplace_back(std::move(_Feature));
    }

    _Result["available"] = true;
    _Result["recognitionStatus"] = std::string("direct-topology");
    _Result["confidence"] = 1.0;
    _Result["relativeGeometryError"] = 0.0;
    _Result["length"] = _Length;
    _Result["linearReference"] = std::move(_Reference);
    _Result["section"] = MeasureSection(*_Frame, _Cylinders);
    _Result["features"] = std::move(_Features);
    _Result["openingCount"] = static_cast<unsigned long long>(_Openings.size());
    return _Result;
}
}
