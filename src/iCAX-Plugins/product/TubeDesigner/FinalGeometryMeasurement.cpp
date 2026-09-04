#include "pch.h"
#include "FinalGeometryMeasurement.h"

#include "GeometryData/TubeNeutralGeometry.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"

#include <numbers>
#include <type_traits>

namespace iCAX::TubeDesigner
{
namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    using namespace iCAX::GeometryData;
    namespace Tube = iCAX::GeometryData::Tube;

    constexpr double kTolerance = 0.001;

    struct SSectionMetrics final
    {
        bool Valid = false;
        Point2 Center;
        double SpanX = 0.0;
        double SpanY = 0.0;
        double WallThickness = 0.0;
        std::string Shape = "curveLoop";
        double Diameter = 0.0;
        std::vector<Point2> OuterPoints;
    };

    struct SMeasuredOpening final
    {
        double Station = 0.0;
        Point3 Center;
        Direction3 Axis;
        Direction3 FaceTangent;
        std::string Shape;
        double Diameter = 0.0;
        double SpanAlong = 0.0;
        double SpanAcross = 0.0;
        double CenterToNegativeEdge = 0.0;
        double CenterToPositiveEdge = 0.0;
        double NegativeEdgeClearance = 0.0;
        double PositiveEdgeClearance = 0.0;
        double Confidence = 0.0;
    };

    VariantArray PointArray(IN const Point3& Point_)
    {
        return { Point_.X, Point_.Y, Point_.Z };
    }

    VariantArray DirectionArray(IN const Direction3& Direction_)
    {
        return { Direction_.X, Direction_.Y, Direction_.Z };
    }

    Vector3 ToVector(IN const Direction3& Direction_)
    {
        return { Direction_.X, Direction_.Y, Direction_.Z };
    }

    Vector3 Subtract(IN const Point3& Left_, IN const Point3& Right_)
    {
        return { Left_.X - Right_.X, Left_.Y - Right_.Y, Left_.Z - Right_.Z };
    }

    double Dot(IN const Vector3& Left_, IN const Vector3& Right_)
    {
        return Left_.X * Right_.X + Left_.Y * Right_.Y + Left_.Z * Right_.Z;
    }

    Vector3 Cross(IN const Vector3& Left_, IN const Vector3& Right_)
    {
        return {
            Left_.Y * Right_.Z - Left_.Z * Right_.Y,
            Left_.Z * Right_.X - Left_.X * Right_.Z,
            Left_.X * Right_.Y - Left_.Y * Right_.X
        };
    }

    double Length(IN const Vector3& Vector_)
    {
        return std::sqrt(Dot(Vector_, Vector_));
    }

    Direction3 Normalize(IN const Vector3& Vector_)
    {
        const auto _Length = Length(Vector_);
        if (_Length <= 1.0e-12) throw std::invalid_argument("cannot normalize zero vector");
        return { Vector_.X / _Length, Vector_.Y / _Length, Vector_.Z / _Length };
    }

    Point3 AddScaled(IN const Point3& Point_, IN const Direction3& Direction_, IN double dScale_)
    {
        return {
            Point_.X + Direction_.X * dScale_,
            Point_.Y + Direction_.Y * dScale_,
            Point_.Z + Direction_.Z * dScale_
        };
    }

    Point3 ToWorld(IN const Placement3& Frame_, IN const Point2& Point_, IN double dStation_ = 0.0)
    {
        return {
            Frame_.Location.X
                + Frame_.XDirection.X * Point_.X
                + Frame_.YDirection.X * Point_.Y
                + Frame_.ZDirection.X * dStation_,
            Frame_.Location.Y
                + Frame_.XDirection.Y * Point_.X
                + Frame_.YDirection.Y * Point_.Y
                + Frame_.ZDirection.Y * dStation_,
            Frame_.Location.Z
                + Frame_.XDirection.Z * Point_.X
                + Frame_.YDirection.Z * Point_.Y
                + Frame_.ZDirection.Z * dStation_
        };
    }

    Point2 CirclePoint(
        IN const Placement2& Placement_, IN double dRadiusX_, IN double dRadiusY_, IN double dAngle_)
    {
        const auto _Cos = std::cos(dAngle_);
        const auto _Sin = std::sin(dAngle_);
        return {
            Placement_.Location.X
                + Placement_.XDirection.X * dRadiusX_ * _Cos
                + Placement_.YDirection.X * dRadiusY_ * _Sin,
            Placement_.Location.Y
                + Placement_.XDirection.Y * dRadiusX_ * _Cos
                + Placement_.YDirection.Y * dRadiusY_ * _Sin
        };
    }

    template<class TAppender>
    void SampleAngleRange(
        IN double dStart_, IN double dEnd_, IN bool bCounterClockwise_,
        IN TAppender&& Appender_)
    {
        auto _Sweep = dEnd_ - dStart_;
        if (bCounterClockwise_)
        {
            while (_Sweep < 0.0) _Sweep += 2.0 * std::numbers::pi;
        }
        else
        {
            while (_Sweep > 0.0) _Sweep -= 2.0 * std::numbers::pi;
        }
        constexpr std::size_t _Samples = 96;
        for (std::size_t _Index = 0; _Index <= _Samples; ++_Index)
        {
            Appender_(dStart_ + _Sweep * static_cast<double>(_Index) / _Samples);
        }
    }

    void SampleCurve(IN const CurveSegment2& Segment_, IN OUT std::vector<Point2>& Points_)
    {
        std::visit([&Points_](IN const auto& Curve_) {
            using T = std::decay_t<decltype(Curve_)>;
            if constexpr (std::is_same_v<T, Segment2>)
            {
                Points_.push_back(Curve_.Start);
                Points_.push_back(Curve_.End);
            }
            else if constexpr (std::is_same_v<T, Circle2>)
            {
                for (std::size_t _Index = 0; _Index < 96; ++_Index)
                {
                    Points_.push_back(CirclePoint(
                        Curve_.Placement, Curve_.Radius, Curve_.Radius,
                        2.0 * std::numbers::pi * static_cast<double>(_Index) / 96.0));
                }
            }
            else if constexpr (std::is_same_v<T, Arc2>)
            {
                SampleAngleRange(
                    Curve_.StartAngle, Curve_.EndAngle, Curve_.CounterClockwise,
                    [&Points_, &Curve_](IN double dAngle_) {
                        Points_.push_back(CirclePoint(
                            Curve_.Basis.Placement, Curve_.Basis.Radius,
                            Curve_.Basis.Radius, dAngle_));
                    });
            }
            else if constexpr (std::is_same_v<T, Ellipse2>)
            {
                for (std::size_t _Index = 0; _Index < 96; ++_Index)
                {
                    Points_.push_back(CirclePoint(
                        Curve_.Placement, Curve_.MajorRadius, Curve_.MinorRadius,
                        2.0 * std::numbers::pi * static_cast<double>(_Index) / 96.0));
                }
            }
            else if constexpr (std::is_same_v<T, EllipseArc2>)
            {
                SampleAngleRange(
                    Curve_.StartAngle, Curve_.EndAngle, Curve_.CounterClockwise,
                    [&Points_, &Curve_](IN double dAngle_) {
                        Points_.push_back(CirclePoint(
                            Curve_.Basis.Placement, Curve_.Basis.MajorRadius,
                            Curve_.Basis.MinorRadius, dAngle_));
                    });
            }
            else if constexpr (std::is_same_v<T, Polyline2>)
            {
                Points_.insert(Points_.end(), Curve_.Points.begin(), Curve_.Points.end());
            }
            else if constexpr (
                std::is_same_v<T, Bezier2>
                || std::is_same_v<T, BSpline2>
                || std::is_same_v<T, NURBS2>)
            {
                Points_.insert(Points_.end(), Curve_.Poles.begin(), Curve_.Poles.end());
            }
            else if constexpr (std::is_same_v<T, Clothoid2>)
            {
                Points_.push_back(Curve_.Placement.Location);
            }
        }, Segment_.Curve);
    }

    std::vector<Point2> SampleOuterBoundary(IN const Tube::CPlanarRegion2& Section_)
    {
        std::vector<Point2> _Points;
        if (Section_.Boundaries.empty()) return _Points;
        for (const auto& _Curve : Section_.Boundaries.front().Curves)
        {
            SampleCurve(_Curve.Segment, _Points);
        }
        return _Points;
    }

    std::pair<double, double> Bounds(IN const std::vector<Point2>& Points_, IN bool bX_)
    {
        auto _Minimum = (std::numeric_limits<double>::max)();
        auto _Maximum = (std::numeric_limits<double>::lowest)();
        for (const auto& _Point : Points_)
        {
            const auto _Value = bX_ ? _Point.X : _Point.Y;
            _Minimum = std::min(_Minimum, _Value);
            _Maximum = std::max(_Maximum, _Value);
        }
        return { _Minimum, _Maximum };
    }

    SSectionMetrics MeasureSection(IN const Tube::SExtrudedRegionNode& Extrusion_)
    {
        SSectionMetrics _Result;
        _Result.OuterPoints = SampleOuterBoundary(Extrusion_.Section);
        if (_Result.OuterPoints.empty()) return _Result;
        const auto [_MinX, _MaxX] = Bounds(_Result.OuterPoints, true);
        const auto [_MinY, _MaxY] = Bounds(_Result.OuterPoints, false);
        _Result.Valid = true;
        _Result.Center = { (_MinX + _MaxX) * 0.5, (_MinY + _MaxY) * 0.5 };
        _Result.SpanX = _MaxX - _MinX;
        _Result.SpanY = _MaxY - _MinY;

        const auto& _OuterBoundary = Extrusion_.Section.Boundaries.front();
        if (_OuterBoundary.Curves.size() == 1)
        {
            const auto& _Curve = _OuterBoundary.Curves.front().Segment.Curve;
            if (const auto _Circle = std::get_if<Circle2>(&_Curve))
            {
                _Result.Shape = "circle";
                _Result.Diameter = _Circle->Radius * 2.0;
                _Result.Center = _Circle->Placement.Location;
            }
        }

        const auto _Outer = std::find_if(
            Extrusion_.SectionPrimitives.begin(), Extrusion_.SectionPrimitives.end(),
            [](IN const auto& Primitive_) {
                return Primitive_.Role == Tube::ESectionPrimitiveRole::OuterBoundary;
            });
        if (_Outer != Extrusion_.SectionPrimitives.end())
        {
            if (_Outer->Kind == Tube::ESectionPrimitiveKind::Circle && _Outer->Radius)
            {
                _Result.Shape = "circle";
                _Result.Diameter = _Outer->Radius->Value * 2.0;
                _Result.Center = _Outer->Center;
            }
            else if (_Outer->Kind == Tube::ESectionPrimitiveKind::Rectangle)
            {
                _Result.Shape = "rectangle";
                _Result.Center = _Outer->Center;
            }
        }

        if (Extrusion_.Section.Boundaries.size() > 1)
        {
            std::vector<Point2> _InnerPoints;
            for (const auto& _Curve : Extrusion_.Section.Boundaries[1].Curves)
            {
                SampleCurve(_Curve.Segment, _InnerPoints);
            }
            if (!_InnerPoints.empty())
            {
                const auto [_InnerMinX, _InnerMaxX] = Bounds(_InnerPoints, true);
                const auto [_InnerMinY, _InnerMaxY] = Bounds(_InnerPoints, false);
                _Result.WallThickness = std::max(0.0, std::min(
                    (_Result.SpanX - (_InnerMaxX - _InnerMinX)) * 0.5,
                    (_Result.SpanY - (_InnerMaxY - _InnerMinY)) * 0.5));
            }
        }
        return _Result;
    }

    std::pair<double, double> ProjectSection(
        IN const std::vector<Point2>& Points_, IN const Placement3& Frame_,
        IN const Direction3& Direction_)
    {
        const auto _Direction = ToVector(Direction_);
        auto _Minimum = (std::numeric_limits<double>::max)();
        auto _Maximum = (std::numeric_limits<double>::lowest)();
        for (const auto& _Point : Points_)
        {
            const Vector3 _Offset{
                Frame_.XDirection.X * _Point.X + Frame_.YDirection.X * _Point.Y,
                Frame_.XDirection.Y * _Point.X + Frame_.YDirection.Y * _Point.Y,
                Frame_.XDirection.Z * _Point.X + Frame_.YDirection.Z * _Point.Y
            };
            const auto _Projection = Dot(_Offset, _Direction);
            _Minimum = std::min(_Minimum, _Projection);
            _Maximum = std::max(_Maximum, _Projection);
        }
        return { _Minimum, _Maximum };
    }

    bool IsMeasuredOpeningNode(IN const Tube::SSolidNode& Node_)
    {
        if (!std::holds_alternative<Tube::SExtrudedRegionNode>(Node_.Data)
            || !Node_.Relations
            || !Node_.Relations->MaterialEffect
            || Node_.Relations->MaterialEffect->Role != Tube::EFeatureMaterialRole::Penetration)
        {
            return false;
        }
        if (Node_.RemovalSemantics
            && Node_.RemovalSemantics->Extent != Tube::ERemovalExtentKind::Through)
        {
            return false;
        }
        const auto _Semantic = Node_.Metadata.find("featureSemantic");
        return _Semantic == Node_.Metadata.end()
            || _Semantic->second == "perpendicular-penetration";
    }

    double NodeConfidence(IN const Tube::SSolidNode& Node_)
    {
        if (Node_.RemovalSemantics) return Node_.RemovalSemantics->Confidence;
        return 0.5;
    }

    std::optional<SMeasuredOpening> MeasureOpening(
        IN const Tube::SSolidNode& Node_,
        IN const Tube::SExtrudedRegionNode& Base_,
        IN const SSectionMetrics& BaseSection_,
        IN const Point3& BaseStart_,
        IN double dBaseLength_)
    {
        const auto& _Removal = std::get<Tube::SExtrudedRegionNode>(Node_.Data);
        const auto _Section = MeasureSection(_Removal);
        if (!_Section.Valid) return std::nullopt;
        const auto _BaseAxis = Base_.Frame.ZDirection;
        const auto _RemovalAxis = _Removal.Frame.ZDirection;
        const auto _BaseDirection = ToVector(_BaseAxis);
        const auto _RemovalDirection = ToVector(_RemovalAxis);
        const auto _RemovalLinePoint = ToWorld(_Removal.Frame, _Section.Center);
        const auto _Separation = Subtract(BaseStart_, _RemovalLinePoint);
        const auto _Parallel = Dot(_BaseDirection, _RemovalDirection);
        const auto _Denominator = 1.0 - _Parallel * _Parallel;
        if (_Denominator <= 1.0e-8) return std::nullopt;
        const auto _Station = (
            _Parallel * Dot(_RemovalDirection, _Separation)
            - Dot(_BaseDirection, _Separation)) / _Denominator;
        if (_Station < -kTolerance || _Station > dBaseLength_ + kTolerance) return std::nullopt;
        const auto _RemovalParameter = _Parallel * _Station
            + Dot(_RemovalDirection, _Separation);
        const auto _Center = AddScaled(
            _RemovalLinePoint, _RemovalAxis, _RemovalParameter);
        auto _FaceTangentVector = Cross(_RemovalDirection, _BaseDirection);
        if (Length(_FaceTangentVector) <= 1.0e-8) return std::nullopt;
        const auto _FaceTangent = Normalize(_FaceTangentVector);
        const auto [_OpeningMinAlong, _OpeningMaxAlong] = ProjectSection(
            _Section.OuterPoints, _Removal.Frame, _BaseAxis);
        const auto [_OpeningMinAcross, _OpeningMaxAcross] = ProjectSection(
            _Section.OuterPoints, _Removal.Frame, _FaceTangent);
        const auto [_BaseMinAcross, _BaseMaxAcross] = ProjectSection(
            BaseSection_.OuterPoints, Base_.Frame, _FaceTangent);
        const auto _BaseCenterWorld = ToWorld(Base_.Frame, BaseSection_.Center);
        const auto _CenterOffset = Subtract(_Center, _BaseCenterWorld);
        const auto _CenterProjection = Dot(_CenterOffset, ToVector(_FaceTangent));
        const auto _OpeningHalfAcross = (_OpeningMaxAcross - _OpeningMinAcross) * 0.5;

        SMeasuredOpening _Result;
        _Result.Station = std::clamp(_Station, 0.0, dBaseLength_);
        _Result.Center = _Center;
        _Result.Axis = _RemovalAxis;
        _Result.FaceTangent = _FaceTangent;
        _Result.Shape = _Section.Shape;
        _Result.Diameter = _Section.Diameter;
        _Result.SpanAlong = _OpeningMaxAlong - _OpeningMinAlong;
        _Result.SpanAcross = _OpeningMaxAcross - _OpeningMinAcross;
        _Result.CenterToNegativeEdge = std::max(0.0, _CenterProjection - _BaseMinAcross);
        _Result.CenterToPositiveEdge = std::max(0.0, _BaseMaxAcross - _CenterProjection);
        _Result.NegativeEdgeClearance = std::max(
            0.0, _Result.CenterToNegativeEdge - _OpeningHalfAcross);
        _Result.PositiveEdgeClearance = std::max(
            0.0, _Result.CenterToPositiveEdge - _OpeningHalfAcross);
        _Result.Confidence = NodeConfidence(Node_);
        return _Result;
    }

    bool SameOpening(IN const SMeasuredOpening& Left_, IN const SMeasuredOpening& Right_)
    {
        return std::abs(Left_.Station - Right_.Station) <= 0.01
            && Length(Subtract(Left_.Center, Right_.Center)) <= 0.02;
    }

    const Tube::SExtrudedRegionNode* FindBaseExtrusion(IN const Tube::CTubeNeutralGeometry& Geometry_)
    {
        const auto _Base = std::find_if(
            Geometry_.SolidNodes.begin(), Geometry_.SolidNodes.end(),
            [&Geometry_](IN const Tube::SSolidNode& Node_) {
                return Node_.ID == Geometry_.BaseNodeID
                    && std::holds_alternative<Tube::SExtrudedRegionNode>(Node_.Data);
            });
        return _Base == Geometry_.SolidNodes.end()
            ? nullptr
            : &std::get<Tube::SExtrudedRegionNode>(_Base->Data);
    }

    std::string RecognitionStatusText(IN Tube::ERecognitionStatus Status_)
    {
        switch (Status_)
        {
        case Tube::ERecognitionStatus::Exact: return "exact";
        case Tube::ERecognitionStatus::EquivalentButAmbiguous: return "equivalent-but-ambiguous";
        case Tube::ERecognitionStatus::Partial: return "partial";
        default: return "failed";
        }
    }
}

ObjectMap MeasureFinalPartGeometry(
    IN const TopoDS_Shape& Shape_,
    IN const std::string& strResourceID_,
    IN std::uint64_t nResourceVersion_)
{
    ObjectMap _Result;
    _Result["source"] = std::string("final-brep");
    _Result["resourceId"] = strResourceID_;
    _Result["resourceVersion"] = static_cast<unsigned long long>(nResourceVersion_);
    _Result["available"] = false;
    if (Shape_.IsNull())
    {
        _Result["message"] = std::string("最终零件几何为空");
        return _Result;
    }

    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.Tolerance = kTolerance;
    _Options.SourceBRepResourceID = strResourceID_;
    const auto _Conversion = iCAX::OpenCascade::ConvertBRepToTubeCSG(Shape_, _Options);
    const auto& _Geometry = _Conversion.Geometry;
    _Result["recognitionStatus"] = RecognitionStatusText(_Geometry.RecognitionStatus);
    _Result["confidence"] = _Geometry.Confidence;
    _Result["relativeGeometryError"] = _Geometry.RelativeVolumeError;
    const auto _Base = FindBaseExtrusion(_Geometry);
    if (!_Base)
    {
        VariantArray _Diagnostics;
        for (const auto& _Diagnostic : _Geometry.Diagnostics)
        {
            _Diagnostics.emplace_back(_Diagnostic.Message);
        }
        _Result["diagnostics"] = _Diagnostics;
        _Result["message"] = std::string("无法从最终几何识别线性主体");
        return _Result;
    }
    const auto _BaseSection = MeasureSection(*_Base);
    if (!_BaseSection.Valid)
    {
        _Result["message"] = std::string("无法从最终几何测得主体截面");
        return _Result;
    }

    const auto _Length = std::abs(_Base->Last - _Base->First);
    const auto _BaseStart = ToWorld(_Base->Frame, _BaseSection.Center, _Base->First);
    const auto _BaseEnd = ToWorld(_Base->Frame, _BaseSection.Center, _Base->Last);
    const auto _OffsetDirection = std::abs(_Base->Frame.ZDirection.Z) > 0.7
        ? _Base->Frame.XDirection
        : (std::abs(_Base->Frame.XDirection.Z) >= std::abs(_Base->Frame.YDirection.Z)
            ? _Base->Frame.XDirection
            : _Base->Frame.YDirection);

    ObjectMap _Reference;
    _Reference["kind"] = std::string("linear");
    _Reference["start"] = PointArray(_BaseStart);
    _Reference["end"] = PointArray(_BaseEnd);
    _Reference["sectionAxes"] = VariantArray{
        DirectionArray(_Base->Frame.XDirection),
        DirectionArray(_Base->Frame.YDirection)
    };
    _Reference["dimensionOffsetDirection"] = DirectionArray(_OffsetDirection);

    ObjectMap _Section;
    _Section["shape"] = _BaseSection.Shape;
    _Section["width"] = _BaseSection.SpanX;
    _Section["height"] = _BaseSection.SpanY;
    _Section["wallThickness"] = _BaseSection.WallThickness;
    if (_BaseSection.Diameter > 0.0) _Section["diameter"] = _BaseSection.Diameter;

    std::vector<SMeasuredOpening> _Openings;
    for (const auto& _Node : _Geometry.SolidNodes)
    {
        if (!IsMeasuredOpeningNode(_Node)) continue;
        const auto _Opening = MeasureOpening(
            _Node, *_Base, _BaseSection, _BaseStart, _Length);
        if (!_Opening) continue;
        const auto _Duplicate = std::find_if(
            _Openings.begin(), _Openings.end(),
            [&_Opening](IN const auto& Existing_) {
                return SameOpening(Existing_, *_Opening);
            });
        if (_Duplicate == _Openings.end())
        {
            _Openings.push_back(*_Opening);
        }
        else if (_Opening->Confidence > _Duplicate->Confidence)
        {
            *_Duplicate = *_Opening;
        }
    }
    std::sort(
        _Openings.begin(), _Openings.end(),
        [](IN const auto& Left_, IN const auto& Right_) {
            return Left_.Station < Right_.Station;
        });

    VariantArray _Features;
    for (const auto& _Opening : _Openings)
    {
        ObjectMap _Feature;
        _Feature["kind"] = std::string("through-opening");
        _Feature["center"] = PointArray(_Opening.Center);
        _Feature["axis"] = DirectionArray(_Opening.Axis);
        _Feature["station"] = _Opening.Station;
        _Feature["shape"] = _Opening.Shape;
        _Feature["openingSpanAlong"] = _Opening.SpanAlong;
        _Feature["openingSpanAcross"] = _Opening.SpanAcross;
        _Feature["faceTangent"] = DirectionArray(_Opening.FaceTangent);
        _Feature["centerToFaceEdgeNegative"] = _Opening.CenterToNegativeEdge;
        _Feature["centerToFaceEdgePositive"] = _Opening.CenterToPositiveEdge;
        _Feature["faceEdgeClearanceNegative"] = _Opening.NegativeEdgeClearance;
        _Feature["faceEdgeClearancePositive"] = _Opening.PositiveEdgeClearance;
        _Feature["confidence"] = _Opening.Confidence;
        if (_Opening.Diameter > 0.0) _Feature["diameter"] = _Opening.Diameter;
        _Features.emplace_back(_Feature);
    }

    _Result["available"] = true;
    _Result["length"] = _Length;
    _Result["linearReference"] = _Reference;
    _Result["section"] = _Section;
    _Result["features"] = _Features;
    _Result["openingCount"] = static_cast<unsigned long long>(_Openings.size());
    return _Result;
}
}
