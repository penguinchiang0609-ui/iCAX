#include "pch.h"
#include "SecurityWindow3Template.h"

namespace
{
    using namespace iCAX::TubeDesigner;
    using SPoint = SGeneratedPart::SPoint2D;
    using SEdge = SGeneratedPart::SManufacturingEdge;
    using SCutter = SGeneratedPart::SManufacturingCutter;
    constexpr double kPi = 3.14159265358979323846;

    struct SVGrooveContext final
    {
        double Width = 0.0;
        double Depth = 0.0;
        double Wall = 0.0;
        double CenterX = 0.0;
        double BaseY = 0.0;
        double RootY = 0.0;
        double HalfSlot = 0.0;
        double HalfToolDepth = 0.0;
        double TopY = 0.0;
        double Feature = 0.0;
    };

    void ValidateProfile(const STubeProfile& Profile_, const std::string& Name_)
    {
        if (Profile_.Type != "rect")
        {
            throw std::invalid_argument(Name_ + " profile type is not supported");
        }
        if (!std::isfinite(Profile_.Width)
            || !std::isfinite(Profile_.Depth)
            || !std::isfinite(Profile_.WallThickness)
            || Profile_.Width <= 0.0
            || Profile_.Depth <= 0.0
            || Profile_.WallThickness <= 0.0
            || Profile_.WallThickness * 2.0 >= std::min(Profile_.Width, Profile_.Depth))
        {
            throw std::invalid_argument(Name_ + " profile dimensions are invalid");
        }
    }

    std::string MakePartNumber(const std::string& ProductCode_, std::uint64_t Index_)
    {
        std::ostringstream _Stream;
        _Stream << ProductCode_ << '-' << std::setw(3) << std::setfill('0') << Index_;
        return _Stream.str();
    }

    SGeneratedPart MakePart(
        std::uint64_t Index_, const std::string& ProductCode_, const std::string& Role_,
        double X1_, double Y1_, double X2_, double Y2_, const STubeProfile& Profile_)
    {
        SGeneratedPart _Part;
        _Part.Index = Index_;
        _Part.PartNumber = MakePartNumber(ProductCode_, Index_);
        _Part.Role = Role_;
        _Part.Profile = Profile_;
        _Part.X1 = X1_;
        _Part.Y1 = Y1_;
        _Part.X2 = X2_;
        _Part.Y2 = Y2_;
        _Part.Length = std::hypot(X2_ - X1_, Y2_ - Y1_);
        return _Part;
    }

    double ProfileSize(const SGeneratedPart& Part_)
    {
        return std::max(Part_.Profile.Width, Part_.Profile.Depth);
    }

    std::vector<double> HorizontalPositions(
        std::uint64_t Count_, double Height_, double TopOffset_, double BottomOffset_)
    {
        if (Count_ == 0) return {};
        if (Count_ == 1) return { (Height_ - TopOffset_ + BottomOffset_) / 2.0 };
        const auto _TopY = Height_ - TopOffset_;
        const auto _BottomY = BottomOffset_;
        if (_TopY < _BottomY) throw std::invalid_argument("horizontal offsets leave no valid spacing");
        const auto _Step = (_TopY - _BottomY) / static_cast<double>(Count_ - 1);
        std::vector<double> _Result;
        _Result.reserve(static_cast<std::size_t>(Count_));
        for (std::uint64_t _Index = 0; _Index < Count_; ++_Index)
        {
            _Result.push_back(_TopY - static_cast<double>(_Index) * _Step);
        }
        return _Result;
    }

    double GrooveY(double Y_)
    {
        return -Y_;
    }

    SPoint GroovePoint(double X_, double Y_)
    {
        return { X_, GrooveY(Y_) };
    }

    SEdge Line(const SPoint& Start_, const SPoint& End_)
    {
        return { "line", Start_, {}, End_ };
    }

    SEdge Arc(const SPoint& Start_, const SPoint& Middle_, const SPoint& End_)
    {
        return { "arc-3pt", Start_, Middle_, End_ };
    }

    SCutter PolygonCutter(
        const std::string& Label_, std::vector<SPoint> Points_, double HalfDepth_)
    {
        SCutter _Result;
        _Result.Kind = "xy-polygon-prism";
        _Result.Label = Label_;
        _Result.StartZ = -HalfDepth_;
        _Result.EndZ = HalfDepth_;
        _Result.Points = std::move(Points_);
        return _Result;
    }

    SCutter ProfileCutter(
        const std::string& Label_, std::vector<SEdge> Edges_, double HalfDepth_)
    {
        SCutter _Result;
        _Result.Kind = "xy-profile-prism";
        _Result.Label = Label_;
        _Result.StartZ = -HalfDepth_;
        _Result.EndZ = HalfDepth_;
        _Result.Edges = std::move(Edges_);
        return _Result;
    }

    SCutter CylinderCutter(
        const std::string& Label_, const SPoint& Center_, double Radius_,
        double StartZ_, double EndZ_)
    {
        SCutter _Result;
        _Result.Kind = "z-cylinder";
        _Result.Label = Label_;
        _Result.StartZ = StartZ_;
        _Result.EndZ = EndZ_;
        _Result.Center = Center_;
        _Result.Radius = Radius_;
        return _Result;
    }

    SPoint DirectedArcMidpoint(
        const SPoint& Center_, double Radius_, const SPoint& Start_,
        const SPoint& End_, bool CounterClockwise_)
    {
        auto _StartAngle = std::atan2(Start_.Y - Center_.Y, Start_.X - Center_.X);
        auto _EndAngle = std::atan2(End_.Y - Center_.Y, End_.X - Center_.X);
        if (CounterClockwise_)
        {
            while (_EndAngle < _StartAngle) _EndAngle += kPi * 2.0;
        }
        else
        {
            while (_EndAngle > _StartAngle) _EndAngle -= kPi * 2.0;
        }
        const auto _MiddleAngle = (_StartAngle + _EndAngle) / 2.0;
        return {
            Center_.X + Radius_ * std::cos(_MiddleAngle),
            Center_.Y + Radius_ * std::sin(_MiddleAngle)
        };
    }

    SPoint PointOnSideArcAtY(
        const SPoint& Center_, double Radius_, double Y_, bool ChooseRight_,
        const SPoint& Fallback_)
    {
        const auto _DY = Y_ - Center_.Y;
        const auto _Inside = Radius_ * Radius_ - _DY * _DY;
        if (_Inside <= 1.0e-9) return { Fallback_.X, Y_ };
        const auto _DX = std::sqrt(_Inside);
        return { Center_.X + (ChooseRight_ ? _DX : -_DX), Y_ };
    }

    double ResolveBottomDistance(
        double FrameWidth_, double FrameDepth_, double Wall_, double Requested_)
    {
        const auto _Wall = std::clamp(
            Wall_, 0.1, std::min(FrameWidth_, FrameDepth_) / 2.0 - 0.1);
        const auto _Distance = Requested_ > 0.0 ? Requested_ : _Wall;
        return std::clamp(_Distance, 0.1, FrameWidth_ - _Wall);
    }

    SVGrooveContext MakeVGrooveContext(
        double CenterX_, const STubeProfile& Frame_, double BottomDistance_)
    {
        const auto _Wall = std::clamp(
            Frame_.WallThickness, 0.1, std::min(Frame_.Width, Frame_.Depth) / 2.0 - 0.1);
        const auto _RootY = Frame_.Width / 2.0
            - ResolveBottomDistance(Frame_.Width, Frame_.Depth, _Wall, BottomDistance_);
        const auto _BaseY = -Frame_.Width / 2.0 - _Wall;
        const auto _CutDepth = std::max(_RootY - _BaseY, 0.1);
        const auto _HalfSlot = std::max(_CutDepth * std::tan(kPi / 4.0), _Wall * 2.0);
        const auto _ToolDepth = Frame_.Depth + std::max(_Wall * 4.0, 10.0);
        return {
            Frame_.Width, Frame_.Depth, _Wall, CenterX_, _BaseY, _RootY,
            _HalfSlot, _ToolDepth / 2.0, 0.0, 0.0
        };
    }

    double ResolveRoundedRadius(
        const STubeProfile& Frame_, double BottomDistance_, double Requested_)
    {
        const auto _Wall = std::clamp(
            Frame_.WallThickness, 0.1, std::min(Frame_.Width, Frame_.Depth) / 2.0 - 0.1);
        const auto _Requested = Requested_ <= 0.0 ? _Wall * 0.5 : Requested_;
        const auto _BottomDistance = ResolveBottomDistance(
            Frame_.Width, Frame_.Depth, _Wall, BottomDistance_);
        const auto _CutDepth = std::max(Frame_.Width + _Wall - _BottomDistance, _Wall);
        return std::clamp(_Requested, 0.1, std::max(_Wall * 0.5, _CutDepth * 0.6));
    }

    double SideArcNonArcRun(double CutDepth_)
    {
        const auto _Run = CutDepth_ / std::tan(kPi / 2.0);
        return std::abs(_Run) <= 1.0e-6 ? 0.0 : _Run;
    }

    double ResolveSideArcRadius(const SVGrooveContext& Context_, double BottomY_)
    {
        const auto _PhysicalTopY = GrooveY(-Context_.Width / 2.0);
        const auto _PhysicalCutHeight = std::abs(_PhysicalTopY - BottomY_);
        return std::max(Context_.Wall, _PhysicalCutHeight);
    }

    struct SOvercutGeometry final
    {
        double TopY = 0.0;
        double BottomY = 0.0;
        double SlopeHalfWidthAtTop = 0.0;
        double PlatformHalfWidth = 0.0;
    };

    SOvercutGeometry SharpWallOvercutGeometry(const SVGrooveContext& Context_)
    {
        const auto _CutDepth = std::max(Context_.RootY - Context_.BaseY, 0.1);
        const auto _SlopeRunPerDepth = Context_.HalfSlot / _CutDepth;
        const auto _BottomDistance = std::clamp(
            Context_.Width / 2.0 - Context_.RootY, 0.1, Context_.Width);
        const auto _OvercutDepth = std::clamp(Context_.Wall, 0.1, _BottomDistance);
        const auto _OverlapLimit = std::max(0.05, std::min(_OvercutDepth * 0.25, _CutDepth * 0.01));
        const auto _Overlap = std::clamp(Context_.Wall * 0.05, 0.05, _OverlapLimit);
        return {
            Context_.RootY - _Overlap,
            Context_.RootY + _OvercutDepth,
            std::max(_Overlap * _SlopeRunPerDepth, 0.02),
            std::max((_OvercutDepth + _Overlap) * _SlopeRunPerDepth, Context_.Wall * 0.5)
        };
    }

    SCutter SharpCutter(const SVGrooveContext& Context_)
    {
        return PolygonCutter("sharp-v", {
            GroovePoint(Context_.CenterX - Context_.HalfSlot, Context_.BaseY),
            GroovePoint(Context_.CenterX, Context_.RootY),
            GroovePoint(Context_.CenterX + Context_.HalfSlot, Context_.BaseY)
        }, Context_.HalfToolDepth);
    }

    SCutter SharpWallOvercutVCutter(const SVGrooveContext& Context_)
    {
        const auto _Geometry = SharpWallOvercutGeometry(Context_);
        const auto _CX = Context_.CenterX;
        return PolygonCutter("sharp-v-wall-overcut", {
            GroovePoint(_CX - Context_.HalfSlot, Context_.BaseY),
            GroovePoint(_CX + Context_.HalfSlot, Context_.BaseY),
            GroovePoint(_CX + _Geometry.SlopeHalfWidthAtTop, _Geometry.TopY),
            GroovePoint(_CX + _Geometry.PlatformHalfWidth, _Geometry.TopY),
            GroovePoint(_CX + _Geometry.PlatformHalfWidth, _Geometry.BottomY),
            GroovePoint(_CX - _Geometry.PlatformHalfWidth, _Geometry.BottomY),
            GroovePoint(_CX - _Geometry.PlatformHalfWidth, _Geometry.TopY),
            GroovePoint(_CX - _Geometry.SlopeHalfWidthAtTop, _Geometry.TopY)
        }, Context_.HalfToolDepth);
    }

    SCutter RoundedCutter(const SVGrooveContext& Context_, double Radius_)
    {
        const auto _TopY = GrooveY(Context_.BaseY);
        const auto _BottomY = GrooveY(Context_.RootY);
        const auto _VerticalSign = _TopY >= _BottomY ? 1.0 : -1.0;
        const auto _CenterY = _BottomY + _VerticalSign * Radius_;
        const auto _CenterHalf = std::max(Radius_, Radius_ * 0.2);
        const auto _ArcDX = Radius_ * std::sin(kPi / 4.0);
        const auto _ArcDY = Radius_ * std::cos(kPi / 4.0) * _VerticalSign;
        const SPoint _LeftTop{ Context_.CenterX - Context_.HalfSlot, _TopY };
        const SPoint _RightTop{ Context_.CenterX + Context_.HalfSlot, _TopY };
        const SPoint _LeftCenter{ Context_.CenterX - _CenterHalf, _CenterY };
        const SPoint _RightCenter{ Context_.CenterX + _CenterHalf, _CenterY };
        const SPoint _LeftSlope{ _LeftCenter.X + _ArcDX, _CenterY - _ArcDY };
        const SPoint _RightSlope{ _RightCenter.X - _ArcDX, _CenterY - _ArcDY };
        const SPoint _LeftBottom{ _LeftCenter.X, _BottomY };
        const SPoint _RightBottom{ _RightCenter.X, _BottomY };
        const auto _LeftMiddle = DirectedArcMidpoint(
            _LeftCenter, Radius_, _LeftBottom, _LeftSlope, _VerticalSign > 0.0);
        const auto _RightMiddle = DirectedArcMidpoint(
            _RightCenter, Radius_, _RightSlope, _RightBottom, _VerticalSign > 0.0);
        return ProfileCutter("rounded-v", {
            Line(_LeftTop, _RightTop),
            Line(_RightTop, _RightSlope),
            Arc(_RightSlope, _RightMiddle, _RightBottom),
            Line(_RightBottom, _LeftBottom),
            Arc(_LeftBottom, _LeftMiddle, _LeftSlope),
            Line(_LeftSlope, _LeftTop)
        }, Context_.HalfToolDepth);
    }

    SCutter SideArcCutter(const SVGrooveContext& Context_, bool KeepLeft_)
    {
        const auto _TopY = GrooveY(Context_.BaseY);
        const auto _PhysicalTopY = GrooveY(-Context_.Width / 2.0);
        const auto _BottomY = GrooveY(Context_.RootY);
        const auto _VerticalSign = _TopY >= _BottomY ? 1.0 : -1.0;
        const auto _Radius = ResolveSideArcRadius(Context_, _BottomY);
        const auto _ArcDX = _Radius;
        const auto _ArcRise = _Radius * _VerticalSign;
        const auto _ArcLength = _Radius * kPi / 2.0;
        const auto _CenterY = _BottomY + _VerticalSign * _Radius;
        const auto _PhysicalRun = SideArcNonArcRun(std::abs(_PhysicalTopY - _BottomY));
        const auto _TopOverrun = SideArcNonArcRun(std::abs(_TopY - _PhysicalTopY));
        const auto _NonArcTopRun = _PhysicalRun + _TopOverrun;
        const auto _ArcSideTopRun = std::max(
            std::max(Context_.HalfSlot, std::abs(_NonArcTopRun)),
            std::abs(_ArcDX) + Context_.Wall);
        const auto _CX = Context_.CenterX;
        if (KeepLeft_)
        {
            const SPoint _LeftArcTop{ _CX - _ArcSideTopRun, _TopY };
            const SPoint _ArcEnd{ _CX, _BottomY + _ArcRise };
            const SPoint _LeftBottom{ _CX - _ArcDX, _BottomY };
            const SPoint _NonArcBottom{ _LeftBottom.X + _ArcLength, _BottomY };
            const SPoint _RightCutTop{ _NonArcBottom.X + _NonArcTopRun, _TopY };
            const SPoint _LeftCenter{ _LeftBottom.X, _CenterY };
            const auto _Middle = DirectedArcMidpoint(
                _LeftCenter, _Radius, _LeftBottom, _ArcEnd, _VerticalSign > 0.0);
            return ProfileCutter("left-arc", {
                Line(_LeftArcTop, _RightCutTop), Line(_RightCutTop, _NonArcBottom),
                Line(_NonArcBottom, _LeftBottom), Arc(_LeftBottom, _Middle, _ArcEnd),
                Line(_ArcEnd, _LeftArcTop)
            }, Context_.HalfToolDepth);
        }
        const SPoint _RightArcTop{ _CX + _ArcSideTopRun, _TopY };
        const SPoint _RightBottom{ _CX + _ArcDX, _BottomY };
        const SPoint _NonArcBottom{ _RightBottom.X - _ArcLength, _BottomY };
        const SPoint _LeftCutTop{ _NonArcBottom.X - _NonArcTopRun, _TopY };
        const SPoint _RightCenter{ _RightBottom.X, _CenterY };
        const SPoint _ArcEnd{ _CX, _BottomY + _ArcRise };
        const auto _Middle = DirectedArcMidpoint(
            _RightCenter, _Radius, _ArcEnd, _RightBottom, _VerticalSign > 0.0);
        return ProfileCutter("right-arc", {
            Line(_LeftCutTop, _RightArcTop), Line(_RightArcTop, _ArcEnd),
            Arc(_ArcEnd, _Middle, _RightBottom), Line(_RightBottom, _NonArcBottom),
            Line(_NonArcBottom, _LeftCutTop)
        }, Context_.HalfToolDepth);
    }

    SVGrooveContext MaleFemaleContext(SVGrooveContext Context_)
    {
        const auto _AutoFeature = Context_.Wall;
        Context_.Feature = std::clamp(_AutoFeature, 0.1, Context_.HalfSlot * 0.95);
        const auto _StraightDepth = std::clamp(
            Context_.Wall * 2.0, 0.1,
            std::max(Context_.RootY - Context_.BaseY, 0.1) * 0.95);
        Context_.TopY = Context_.BaseY + _StraightDepth;
        return Context_;
    }

    SCutter SharpMaleFemaleCutter(const SVGrooveContext& Context_)
    {
        const auto _CX = Context_.CenterX;
        const auto _LeftTopX = _CX - Context_.HalfSlot + Context_.Feature;
        const auto _RightSlopeTopX = _CX + Context_.HalfSlot - Context_.Feature;
        const auto _RightOuterX = _CX + Context_.HalfSlot + Context_.Feature;
        return PolygonCutter("sharp-v-male-female", {
            GroovePoint(_LeftTopX, Context_.BaseY),
            GroovePoint(_RightOuterX, Context_.BaseY),
            GroovePoint(_RightOuterX, Context_.TopY),
            GroovePoint(_RightSlopeTopX, Context_.TopY),
            GroovePoint(_CX, Context_.RootY),
            GroovePoint(_LeftTopX, Context_.TopY)
        }, Context_.HalfToolDepth);
    }

    SCutter SideArcMaleFemaleCutter(const SVGrooveContext& Context_, bool KeepLeft_)
    {
        const auto _TopY = GrooveY(Context_.BaseY);
        const auto _PhysicalTopY = GrooveY(-Context_.Width / 2.0);
        const auto _BottomY = GrooveY(Context_.RootY);
        const auto _VerticalSign = _TopY >= _BottomY ? 1.0 : -1.0;
        auto _FeatureY = _PhysicalTopY - _VerticalSign * Context_.Wall;
        _FeatureY = std::clamp(
            _FeatureY, std::min(_BottomY, _PhysicalTopY) + 0.1,
            std::max(_BottomY, _PhysicalTopY) - 0.1);
        const auto _Radius = ResolveSideArcRadius(Context_, _BottomY);
        const auto _ArcDX = _Radius;
        const auto _ArcRise = _Radius * _VerticalSign;
        const auto _ArcLength = _Radius * kPi / 2.0;
        const auto _CenterY = _BottomY + _VerticalSign * _Radius;
        const auto _FeatureRun = SideArcNonArcRun(std::abs(_FeatureY - _BottomY));
        const auto _CX = Context_.CenterX;
        if (KeepLeft_)
        {
            const SPoint _Center{ _CX - _ArcDX, _CenterY };
            const SPoint _ArcEnd{ _CX, _BottomY + _ArcRise };
            const auto _ArcFeature = PointOnSideArcAtY(_Center, _Radius, _FeatureY, true, _ArcEnd);
            const SPoint _ArcVerticalTop{ _ArcFeature.X, _TopY };
            const SPoint _LeftBottom{ _CX - _ArcDX, _BottomY };
            const SPoint _NonArcBottom{ _LeftBottom.X + _ArcLength, _BottomY };
            const SPoint _NonArcFeature{ _NonArcBottom.X + _FeatureRun, _FeatureY };
            const SPoint _NonArcTop{ _NonArcFeature.X, _TopY };
            const auto _Middle = DirectedArcMidpoint(
                _Center, _Radius, _LeftBottom, _ArcFeature, _VerticalSign > 0.0);
            return ProfileCutter("left-arc-male-female", {
                Line(_ArcVerticalTop, _NonArcTop), Line(_NonArcTop, _NonArcFeature),
                Line(_NonArcFeature, _NonArcBottom), Line(_NonArcBottom, _LeftBottom),
                Arc(_LeftBottom, _Middle, _ArcFeature), Line(_ArcFeature, _ArcVerticalTop)
            }, Context_.HalfToolDepth);
        }
        const SPoint _Center{ _CX + _ArcDX, _CenterY };
        const SPoint _ArcEnd{ _CX, _BottomY + _ArcRise };
        const auto _ArcFeature = PointOnSideArcAtY(_Center, _Radius, _FeatureY, false, _ArcEnd);
        const SPoint _ArcVerticalTop{ _ArcFeature.X, _TopY };
        const SPoint _RightBottom{ _CX + _ArcDX, _BottomY };
        const SPoint _NonArcBottom{ _RightBottom.X - _ArcLength, _BottomY };
        const SPoint _NonArcFeature{ _NonArcBottom.X - _FeatureRun, _FeatureY };
        const SPoint _NonArcTop{ _NonArcFeature.X, _TopY };
        const auto _Middle = DirectedArcMidpoint(
            _Center, _Radius, _ArcFeature, _RightBottom, _VerticalSign > 0.0);
        return ProfileCutter("right-arc-male-female", {
            Line(_NonArcTop, _ArcVerticalTop), Line(_ArcVerticalTop, _ArcFeature),
            Arc(_ArcFeature, _Middle, _RightBottom), Line(_RightBottom, _NonArcBottom),
            Line(_NonArcBottom, _NonArcFeature), Line(_NonArcFeature, _NonArcTop)
        }, Context_.HalfToolDepth);
    }

    SCutter BottomCutCutter(const SVGrooveContext& Context_, double GrooveRadius_)
    {
        const auto _Width = std::max(Context_.Wall * 4.0, GrooveRadius_ * 1.5);
        const auto _Height = std::max(Context_.Wall * 1.25, 1.0);
        return PolygonCutter("bottom-cut", {
            GroovePoint(Context_.CenterX - _Width / 2.0, Context_.RootY - _Height / 2.0),
            GroovePoint(Context_.CenterX + _Width / 2.0, Context_.RootY - _Height / 2.0),
            GroovePoint(Context_.CenterX + _Width / 2.0, Context_.RootY + _Height / 2.0),
            GroovePoint(Context_.CenterX - _Width / 2.0, Context_.RootY + _Height / 2.0)
        }, Context_.HalfToolDepth);
    }

    SCutter ReliefHoleCutter(
        const SVGrooveContext& Context_, double GrooveRadius_, double Diameter_, bool NoThrough_)
    {
        const auto _Radius = Diameter_ > 0.0
            ? Diameter_ / 2.0 : std::max(GrooveRadius_, Context_.Wall * 2.0);
        const auto _ThroughLength = Context_.Depth + std::max(Context_.Wall * 4.0, _Radius * 2.0);
        const auto _Length = NoThrough_
            ? std::max(Context_.Depth / 2.0, _Radius * 2.0) : _ThroughLength;
        const auto _StartZ = NoThrough_ ? -Context_.Depth / 2.0 - 0.5 : -_Length / 2.0;
        return CylinderCutter(
            "relief-hole", { Context_.CenterX, GrooveY(Context_.RootY) },
            _Radius, _StartZ, _StartZ + _Length);
    }

    SCutter WallOvercutCutter(const SVGrooveContext& Context_)
    {
        const auto _Geometry = SharpWallOvercutGeometry(Context_);
        const auto _CX = Context_.CenterX;
        return PolygonCutter("wall-overcut", {
            GroovePoint(_CX + _Geometry.SlopeHalfWidthAtTop, _Geometry.TopY),
            GroovePoint(_CX + _Geometry.PlatformHalfWidth, _Geometry.TopY),
            GroovePoint(_CX + _Geometry.PlatformHalfWidth, _Geometry.BottomY),
            GroovePoint(_CX - _Geometry.PlatformHalfWidth, _Geometry.BottomY),
            GroovePoint(_CX - _Geometry.PlatformHalfWidth, _Geometry.TopY),
            GroovePoint(_CX - _Geometry.SlopeHalfWidthAtTop, _Geometry.TopY),
            GroovePoint(_CX, Context_.RootY)
        }, Context_.HalfToolDepth);
    }

    std::vector<SCutter> MakeVGrooveCutters(
        double CenterX_, const SSecurityWindow3Parameters& Parameters_)
    {
        auto _Context = MakeVGrooveContext(
            CenterX_, Parameters_.Frame, Parameters_.VGrooveBottomDistance);
        std::vector<SCutter> _Result;
        if (Parameters_.VGrooveMaleFemale)
        {
            const auto _MaleFemale = MaleFemaleContext(_Context);
            if (Parameters_.VGrooveStyle == "left_arc")
                _Result.push_back(SideArcMaleFemaleCutter(_MaleFemale, true));
            else if (Parameters_.VGrooveStyle == "right_arc")
                _Result.push_back(SideArcMaleFemaleCutter(_MaleFemale, false));
            else
                _Result.push_back(SharpMaleFemaleCutter(_MaleFemale));
        }
        else if (Parameters_.VGrooveStyle == "rounded_v")
        {
            _Result.push_back(RoundedCutter(_Context, ResolveRoundedRadius(
                Parameters_.Frame, Parameters_.VGrooveBottomDistance, Parameters_.VGrooveRadius)));
        }
        else if (Parameters_.VGrooveStyle == "left_arc")
        {
            _Result.push_back(SideArcCutter(_Context, true));
        }
        else if (Parameters_.VGrooveStyle == "right_arc")
        {
            _Result.push_back(SideArcCutter(_Context, false));
        }
        else if (Parameters_.VGrooveWallOvercut)
        {
            _Result.push_back(SharpWallOvercutVCutter(_Context));
        }
        else
        {
            _Result.push_back(SharpCutter(_Context));
        }

        if (Parameters_.VGrooveStyle == "sharp_v" && Parameters_.VGrooveBottomCut)
            _Result.push_back(BottomCutCutter(_Context, Parameters_.VGrooveRadius));
        if (Parameters_.VGrooveStyle == "sharp_v" && Parameters_.VGrooveReliefHole)
            _Result.push_back(ReliefHoleCutter(
                _Context, Parameters_.VGrooveRadius, Parameters_.VGrooveReliefDiameter,
                Parameters_.VGrooveReliefNoThrough));
        if (Parameters_.VGrooveStyle == "sharp_v"
            && Parameters_.VGrooveMaleFemale && Parameters_.VGrooveWallOvercut)
            _Result.push_back(WallOvercutCutter(_Context));
        return _Result;
    }

    double SideArcPreviewRadius(const SSecurityWindow3Parameters& Parameters_)
    {
        const auto _RootY = Parameters_.Frame.Width / 2.0
            - Parameters_.VGrooveBottomDistance;
        const auto _BottomY = -_RootY;
        const auto _PhysicalTopY = Parameters_.Frame.Width / 2.0;
        const auto _Radius = std::max(
            Parameters_.Frame.WallThickness, std::abs(_PhysicalTopY - _BottomY));
        const auto _Maximum = std::min(Parameters_.Width, Parameters_.Height) / 2.0 - 0.1;
        return std::clamp(_Radius, std::min(std::max(
            Parameters_.Frame.WallThickness, 0.1), _Maximum), _Maximum);
    }

    void AddBranchCrossingJoints(
        SGeneratedProduct& Product_, const SSecurityWindow3Parameters& Parameters_,
        bool ContinuousOuterFrame_)
    {
        if (ContinuousOuterFrame_)
        {
            for (const auto& _Part : Product_.Parts)
            {
                if (_Part.Index == 1) continue;
                Product_.Joints.push_back({
                    1, _Part.Index, "through", Parameters_.AssemblyClearance,
                    (_Part.X1 + _Part.X2) / 2.0, (_Part.Y1 + _Part.Y2) / 2.0
                });
            }
        }

        for (const auto& _Vertical : Product_.Parts)
        {
            if (!_Vertical.IsVertical()) continue;
            const auto _VerticalX = (_Vertical.X1 + _Vertical.X2) / 2.0;
            const auto _VerticalMinY = std::min(_Vertical.Y1, _Vertical.Y2);
            const auto _VerticalMaxY = std::max(_Vertical.Y1, _Vertical.Y2);
            for (const auto& _Horizontal : Product_.Parts)
            {
                if (_Horizontal.IsVertical()) continue;
                if (_Vertical.Role.ends_with("-frame") && _Horizontal.Role.ends_with("-frame")) continue;
                if (ContinuousOuterFrame_ && _Horizontal.Index == 1) continue;
                const auto _HorizontalY = (_Horizontal.Y1 + _Horizontal.Y2) / 2.0;
                const auto _HorizontalMinX = std::min(_Horizontal.X1, _Horizontal.X2);
                const auto _HorizontalMaxX = std::max(_Horizontal.X1, _Horizontal.X2);
                if (_VerticalX < _HorizontalMinX || _VerticalX > _HorizontalMaxX
                    || _HorizontalY < _VerticalMinY || _HorizontalY > _VerticalMaxY) continue;
                const auto _HorizontalIsTarget = ProfileSize(_Horizontal) >= ProfileSize(_Vertical);
                Product_.Joints.push_back({
                    _HorizontalIsTarget ? _Horizontal.Index : _Vertical.Index,
                    _HorizontalIsTarget ? _Vertical.Index : _Horizontal.Index,
                    "through", Parameters_.AssemblyClearance, _VerticalX, _HorizontalY
                });
            }
        }
    }
}

iCAX::TubeDesigner::SGeneratedProduct iCAX::TubeDesigner::GenerateSecurityWindow3(
    const SSecurityWindow3Parameters& Parameters_)
{
    if (Parameters_.ProductCode.empty()) throw std::invalid_argument("product code is required");
    if (!std::isfinite(Parameters_.Height) || !std::isfinite(Parameters_.Width)
        || Parameters_.Height <= 0.0 || Parameters_.Width <= 0.0)
        throw std::invalid_argument("product width and height must be positive");
    if (Parameters_.HorizontalCount > 100 || Parameters_.MiddleVerticalCount > 10)
        throw std::invalid_argument("tube count is outside the supported range");
    if (!std::isfinite(Parameters_.FirstHorizontalTopOffset)
        || !std::isfinite(Parameters_.LastHorizontalBottomOffset)
        || Parameters_.FirstHorizontalTopOffset < 0.0
        || Parameters_.LastHorizontalBottomOffset < 0.0
        || Parameters_.FirstHorizontalTopOffset > Parameters_.Height
        || Parameters_.LastHorizontalBottomOffset > Parameters_.Height)
        throw std::invalid_argument("horizontal offsets must be inside the product height");
    if (!std::isfinite(Parameters_.HorizontalBranchReserve)
        || !std::isfinite(Parameters_.VerticalBranchReserve)
        || (Parameters_.HorizontalBranchReserve < 0.0 && Parameters_.HorizontalBranchReserve != -1.0)
        || (Parameters_.VerticalBranchReserve < 0.0 && Parameters_.VerticalBranchReserve != -1.0))
        throw std::invalid_argument("branch reserve must be -1 or non-negative");
    if (!std::isfinite(Parameters_.AssemblyClearance) || Parameters_.AssemblyClearance < 0.0)
        throw std::invalid_argument("assembly clearance cannot be negative");
    ValidateProfile(Parameters_.Frame, "frame");
    ValidateProfile(Parameters_.Vertical, "vertical");
    ValidateProfile(Parameters_.Horizontal, "horizontal");
    if (Parameters_.Horizontal.Width <= Parameters_.Vertical.Width
        || Parameters_.Frame.Width <= Parameters_.Horizontal.Width)
        throw std::invalid_argument("tube profile sizes must follow frame > horizontal > vertical");
    if (Parameters_.Width <= Parameters_.Frame.Width * 2.0
        || Parameters_.Height <= Parameters_.Frame.Width * 2.0)
        throw std::invalid_argument("product dimensions must be greater than twice the frame width");
    if (Parameters_.FrameJoinType != "miter_45"
        && Parameters_.FrameJoinType != "butt_90"
        && Parameters_.FrameJoinType != "v_groove_90")
        throw std::invalid_argument("unsupported frame join type");
    if (Parameters_.FrameButtWrapMode != "side_wraps_horizontal"
        && Parameters_.FrameButtWrapMode != "horizontal_wraps_side")
        throw std::invalid_argument("unsupported frame butt wrap mode");
    if (Parameters_.VGrooveStyle != "sharp_v"
        && Parameters_.VGrooveStyle != "rounded_v"
        && Parameters_.VGrooveStyle != "left_arc"
        && Parameters_.VGrooveStyle != "right_arc")
        throw std::invalid_argument("unsupported V-groove style");
    if (Parameters_.VGrooveStyle != "sharp_v"
        && (Parameters_.VGrooveBottomCut
            || Parameters_.VGrooveReliefHole || Parameters_.VGrooveWallOvercut))
        throw std::invalid_argument("V-groove add-ons require sharp V style");
    if (!std::isfinite(Parameters_.VGrooveBottomDistance)
        || Parameters_.VGrooveBottomDistance < 0.0
        || Parameters_.VGrooveBottomDistance >= Parameters_.Frame.Width - Parameters_.Frame.WallThickness)
        throw std::invalid_argument("V-groove bottom distance is invalid");
    if (!std::isfinite(Parameters_.VGrooveRadius) || Parameters_.VGrooveRadius < 0.0
        || !std::isfinite(Parameters_.VGrooveReliefDiameter) || Parameters_.VGrooveReliefDiameter < 0.0
        || !std::isfinite(Parameters_.VGrooveKFactor)
        || Parameters_.VGrooveKFactor < 0.0 || Parameters_.VGrooveKFactor > 1.0)
        throw std::invalid_argument("V-groove dimensions or K factor are invalid");

    const auto _HorizontalReserve = Parameters_.HorizontalBranchReserve < 0.0
        ? Parameters_.Frame.Width / 2.0 : Parameters_.HorizontalBranchReserve;
    const auto _VerticalReserve = Parameters_.VerticalBranchReserve < 0.0
        ? Parameters_.Frame.Width / 2.0 : Parameters_.VerticalBranchReserve;
    if (_HorizontalReserve > Parameters_.Frame.Width || _VerticalReserve > Parameters_.Frame.Width)
        throw std::invalid_argument("branch reserve cannot exceed frame width");

    SGeneratedProduct _Result;
    _Result.ProductCode = Parameters_.ProductCode;
    _Result.TemplateID = "security-window-3";
    _Result.TemplateVersion = "1.0.0";
    _Result.Width = Parameters_.Width;
    _Result.Height = Parameters_.Height;

    std::uint64_t _PartIndex = 1;
    const auto _ContinuousFrame = Parameters_.FrameJoinType == "v_groove_90";
    if (_ContinuousFrame)
    {
        const auto _Left = Parameters_.Frame.WallThickness;
        const auto _Right = Parameters_.Width - Parameters_.Frame.WallThickness;
        const auto _Bottom = Parameters_.Frame.WallThickness;
        const auto _Top = Parameters_.Height - Parameters_.Frame.WallThickness;
        const auto _Middle = (_Left + _Right) / 2.0;
        auto _Frame = MakePart(
            _PartIndex++, Parameters_.ProductCode, "outer-frame",
            _Middle, _Bottom, _Middle, _Bottom, Parameters_.Frame);
        _Frame.PathPoints = {
            { _Middle, _Bottom }, { _Right, _Bottom }, { _Right, _Top },
            { _Left, _Top }, { _Left, _Bottom }, { _Middle, _Bottom }
        };
        const auto _HorizontalRun = _Right - _Left;
        const auto _VerticalRun = _Top - _Bottom;
        const auto _BendAllowance = 2.0 * kPi * Parameters_.VGrooveKFactor
            * Parameters_.Frame.WallThickness * 90.0 / 360.0;
        SGeneratedPart::SManufacturingGeometry _Manufacturing;
        _Manufacturing.BaseLength = _HorizontalRun * 2.0 + _VerticalRun * 2.0
            + _BendAllowance * 4.0;
        _Manufacturing.BendAllowance = _BendAllowance;
        _Manufacturing.BendCount = 4;
        auto _Cursor = 0.0;
        for (const auto _Segment : std::vector<double>{
            _HorizontalRun / 2.0, _VerticalRun, _HorizontalRun, _VerticalRun })
        {
            const auto _Center = _Cursor + _Segment + _BendAllowance / 2.0;
            auto _Cutters = MakeVGrooveCutters(_Center, Parameters_);
            _Manufacturing.Cutters.insert(
                _Manufacturing.Cutters.end(), _Cutters.begin(), _Cutters.end());
            _Cursor += _Segment + _BendAllowance;
        }
        _Frame.Length = _Manufacturing.BaseLength;
        _Frame.ManufacturingGeometry = std::move(_Manufacturing);
        SGeneratedPart::SPreviewGeometry _Preview;
        _Preview.OuterMaxX = Parameters_.Width;
        _Preview.OuterMaxY = Parameters_.Height;
        _Preview.OpeningMinX = Parameters_.Frame.Width;
        _Preview.OpeningMinY = Parameters_.Frame.Width;
        _Preview.OpeningMaxX = Parameters_.Width - Parameters_.Frame.Width;
        _Preview.OpeningMaxY = Parameters_.Height - Parameters_.Frame.Width;
        _Preview.HalfDepth = Parameters_.Frame.Depth / 2.0;
        _Preview.WallThickness = Parameters_.Frame.WallThickness;
        if (Parameters_.VGrooveStyle == "left_arc" || Parameters_.VGrooveStyle == "right_arc")
            _Preview.RoundedOuterCornerRadius = SideArcPreviewRadius(Parameters_);
        _Frame.PreviewGeometry = _Preview;
        _Result.Parts.push_back(std::move(_Frame));
    }
    else
    {
        const auto _FrameHalf = Parameters_.Frame.Width / 2.0;
        auto _VerticalStart = 0.0;
        auto _VerticalEnd = Parameters_.Height;
        auto _HorizontalStart = 0.0;
        auto _HorizontalEnd = Parameters_.Width;
        if (Parameters_.FrameJoinType == "butt_90"
            && Parameters_.FrameButtWrapMode == "side_wraps_horizontal")
        {
            _HorizontalStart = Parameters_.Frame.Width;
            _HorizontalEnd = Parameters_.Width - Parameters_.Frame.Width;
        }
        else if (Parameters_.FrameJoinType == "butt_90")
        {
            _VerticalStart = Parameters_.Frame.Width;
            _VerticalEnd = Parameters_.Height - Parameters_.Frame.Width;
        }
        _Result.Parts.push_back(MakePart(_PartIndex++, Parameters_.ProductCode, "left-frame",
            _FrameHalf, _VerticalStart, _FrameHalf, _VerticalEnd, Parameters_.Frame));
        _Result.Parts.push_back(MakePart(_PartIndex++, Parameters_.ProductCode, "right-frame",
            Parameters_.Width - _FrameHalf, _VerticalStart,
            Parameters_.Width - _FrameHalf, _VerticalEnd, Parameters_.Frame));
        _Result.Parts.push_back(MakePart(_PartIndex++, Parameters_.ProductCode, "top-frame",
            _HorizontalStart, Parameters_.Height - _FrameHalf,
            _HorizontalEnd, Parameters_.Height - _FrameHalf, Parameters_.Frame));
        _Result.Parts.push_back(MakePart(_PartIndex++, Parameters_.ProductCode, "bottom-frame",
            _HorizontalStart, _FrameHalf, _HorizontalEnd, _FrameHalf, Parameters_.Frame));
        if (Parameters_.FrameJoinType == "miter_45")
        {
            for (auto& _Part : _Result.Parts)
            {
                _Part.StartCut = "miter-45";
                _Part.EndCut = "miter-45";
            }
        }
    }

    const auto _InnerLeft = Parameters_.Frame.Width;
    const auto _InnerRight = Parameters_.Width - Parameters_.Frame.Width;
    const auto _BranchStartX = Parameters_.Frame.Width - _HorizontalReserve;
    const auto _BranchEndX = Parameters_.Width - Parameters_.Frame.Width + _HorizontalReserve;
    const auto _BranchStartY = Parameters_.Frame.Width - _VerticalReserve;
    const auto _BranchEndY = Parameters_.Height - Parameters_.Frame.Width + _VerticalReserve;
    for (std::uint64_t _Index = 0; _Index < Parameters_.MiddleVerticalCount; ++_Index)
    {
        const auto _X = _InnerLeft + (_InnerRight - _InnerLeft)
            * static_cast<double>(_Index + 1)
            / static_cast<double>(Parameters_.MiddleVerticalCount + 1);
        _Result.Parts.push_back(MakePart(
            _PartIndex++, Parameters_.ProductCode, "middle-vertical",
            _X, _BranchStartY, _X, _BranchEndY, Parameters_.Vertical));
    }
    for (const auto _Y : HorizontalPositions(
        Parameters_.HorizontalCount, Parameters_.Height,
        Parameters_.FirstHorizontalTopOffset, Parameters_.LastHorizontalBottomOffset))
    {
        _Result.Parts.push_back(MakePart(
            _PartIndex++, Parameters_.ProductCode, "middle-horizontal",
            _BranchStartX, _Y, _BranchEndX, _Y, Parameters_.Horizontal));
    }
    AddBranchCrossingJoints(_Result, Parameters_, _ContinuousFrame);
    return _Result;
}
