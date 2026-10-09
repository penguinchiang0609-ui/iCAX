#include "pch.h"

#include "TubeSideSketchCurveService.h"
#include "TubeSideSketchPlanarCurves.h"

#include <etp/Analyzer.hxx>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <Geom2dConvert.hxx>
#include <Geom2dConvert_ApproxCurve.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <GeomAPI.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <NCollection_List.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Trsf2d.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace iCAX::TubeDesigner
{
    namespace
    {
        constexpr double kCurveTolerance = 1.0e-7;
        constexpr double kPi = 3.1415926535897932384626433832795;

        struct SProfileCurve final
        {
            TopoDS_Edge Edge;
            double First = 0.0;
            double Last = 0.0;
            bool Reversed = false;
            double UFirst = 0.0;
            double ULast = 0.0;

            gp_Pnt Start() const
            {
                return BRepAdaptor_Curve(Edge).Value(Reversed ? Last : First);
            }

            gp_Pnt End() const
            {
                return BRepAdaptor_Curve(Edge).Value(Reversed ? First : Last);
            }

            double Length() const
            {
                BRepAdaptor_Curve _Curve(Edge);
                return GCPnts_AbscissaPoint::Length(_Curve, First, Last, kCurveTolerance);
            }
        };

        bool SameNativeProfileCurve(const SProfileCurve& A_, const SProfileCurve& B_)
        {
            if (A_.Edge.IsSame(B_.Edge)) return true;
            BRepAdaptor_Curve _A(A_.Edge), _B(B_.Edge);
            if (_A.GetType() != _B.GetType()) return false;
            constexpr double _AngularTolerance = 1.0e-10;
            switch (_A.GetType())
            {
            case GeomAbs_Line:
            {
                const auto _Left = _A.Line(), _Right = _B.Line();
                return _Left.Direction().IsParallel(_Right.Direction(), _AngularTolerance)
                    && _Left.Distance(_Right.Location()) <= kCurveTolerance;
            }
            case GeomAbs_Circle:
            {
                const auto _Left = _A.Circle(), _Right = _B.Circle();
                return _Left.Location().Distance(_Right.Location()) <= kCurveTolerance
                    && std::abs(_Left.Radius() - _Right.Radius()) <= kCurveTolerance
                    && _Left.Axis().Direction().IsParallel(
                        _Right.Axis().Direction(), _AngularTolerance);
            }
            case GeomAbs_Ellipse:
            {
                const auto _Left = _A.Ellipse(), _Right = _B.Ellipse();
                return _Left.Location().Distance(_Right.Location()) <= kCurveTolerance
                    && std::abs(_Left.MajorRadius() - _Right.MajorRadius()) <= kCurveTolerance
                    && std::abs(_Left.MinorRadius() - _Right.MinorRadius()) <= kCurveTolerance
                    && _Left.Axis().Direction().IsParallel(
                        _Right.Axis().Direction(), _AngularTolerance)
                    && _Left.XAxis().Direction().IsParallel(
                        _Right.XAxis().Direction(), _AngularTolerance);
            }
            default:
                return false;
            }
        }

        double Coordinate(const gp_Pnt& Point_, const gp_Ax3& Frame_, bool X_)
        {
            return gp_Vec(Frame_.Location(), Point_).Dot(
                gp_Vec(X_ ? Frame_.XDirection() : Frame_.YDirection()));
        }

        double Axial(const gp_Pnt& Point_, const gp_Ax3& Frame_)
        {
            return gp_Vec(Frame_.Location(), Point_).Dot(gp_Vec(Frame_.Direction()));
        }

        TopoDS_Face Rectangle(double X0_, double X1_, double Y0_, double Y1_)
        {
            BRepBuilderAPI_MakePolygon _Wire;
            _Wire.Add(gp_Pnt(X0_, Y0_, 0.0));
            _Wire.Add(gp_Pnt(X1_, Y0_, 0.0));
            _Wire.Add(gp_Pnt(X1_, Y1_, 0.0));
            _Wire.Add(gp_Pnt(X0_, Y1_, 0.0));
            _Wire.Close();
            return BRepBuilderAPI_MakeFace(_Wire.Wire());
        }

        double Volume(const TopoDS_Shape& Shape_)
        {
            GProp_GProps _Properties;
            BRepGProp::VolumeProperties(Shape_, _Properties);
            return std::abs(_Properties.Mass());
        }

        double WireArea(const TopoDS_Wire& Wire_)
        {
            GProp_GProps _Properties;
            BRepGProp::SurfaceProperties(BRepBuilderAPI_MakeFace(Wire_).Face(), _Properties);
            return std::abs(_Properties.Mass());
        }

        std::vector<SProfileCurve> ExactOuterProfile(const etp::PartAnalysis& Tube_)
        {
            if (Tube_.SectionWires.empty()) throw std::runtime_error("管材没有闭合截面");
            const auto _Outer = std::max_element(Tube_.SectionWires.begin(), Tube_.SectionWires.end(),
                [](const auto& A_, const auto& B_) { return WireArea(A_) < WireArea(B_); });
            std::vector<SProfileCurve> _Curves;
            for (BRepTools_WireExplorer _Explorer(*_Outer); _Explorer.More(); _Explorer.Next())
            {
                const auto _Edge = TopoDS::Edge(_Explorer.Current());
                BRepAdaptor_Curve _Curve(_Edge);
                SProfileCurve _Part{_Edge, _Curve.FirstParameter(), _Curve.LastParameter(),
                    _Edge.Orientation() == TopAbs_REVERSED};
                if (_Part.Length() > kCurveTolerance) _Curves.push_back(std::move(_Part));
            }
            if (_Curves.empty()) throw std::runtime_error("管材外轮廓没有曲线");

            // These evaluations determine orientation only. The actual curves
            // and their exact lengths remain the modeling representation.
            double _Area = 0.0;
            for (const auto& _Part : _Curves)
            {
                BRepAdaptor_Curve _Curve(_Part.Edge);
                auto _Previous = _Part.Start();
                for (int _Index = 1; _Index <= 16; ++_Index)
                {
                    const auto _Ratio = static_cast<double>(_Index) / 16.0;
                    const auto _T = _Part.Reversed
                        ? _Part.Last - (_Part.Last - _Part.First) * _Ratio
                        : _Part.First + (_Part.Last - _Part.First) * _Ratio;
                    const auto _Current = _Curve.Value(_T);
                    _Area += Coordinate(_Previous, Tube_.SectionFrame, true)
                        * Coordinate(_Current, Tube_.SectionFrame, false)
                        - Coordinate(_Current, Tube_.SectionFrame, true)
                        * Coordinate(_Previous, Tube_.SectionFrame, false);
                    _Previous = _Current;
                }
            }
            if (_Area < 0.0)
            {
                std::reverse(_Curves.begin(), _Curves.end());
                for (auto& _Curve : _Curves) _Curve.Reversed = !_Curve.Reversed;
            }

            std::size_t _SeamIndex = 0;
            double _SeamParameter = _Curves.front().Reversed
                ? _Curves.front().Last : _Curves.front().First;
            gp_Pnt _SeamPoint = _Curves.front().Start();
            const auto _Consider = [&](std::size_t Index_, double Parameter_, const gp_Pnt& Point_) {
                const auto _X = Coordinate(Point_, Tube_.SectionFrame, true);
                const auto _BestX = Coordinate(_SeamPoint, Tube_.SectionFrame, true);
                const auto _Y = Coordinate(Point_, Tube_.SectionFrame, false);
                const auto _BestY = Coordinate(_SeamPoint, Tube_.SectionFrame, false);
                if (_X < _BestX - kCurveTolerance
                    || (std::abs(_X - _BestX) <= kCurveTolerance && _Y < _BestY))
                {
                    _SeamIndex = Index_;
                    _SeamParameter = Parameter_;
                    _SeamPoint = Point_;
                }
            };
            for (std::size_t _Index = 0; _Index < _Curves.size(); ++_Index)
            {
                const auto& _Part = _Curves[_Index];
                BRepAdaptor_Curve _Curve(_Part.Edge);
                _Consider(_Index, _Part.First, _Curve.Value(_Part.First));
                _Consider(_Index, _Part.Last, _Curve.Value(_Part.Last));
                if (_Curve.GetType() == GeomAbs_Circle || _Curve.GetType() == GeomAbs_Ellipse)
                {
                    const auto _X = _Curve.GetType() == GeomAbs_Circle
                        ? gp_Vec(_Curve.Circle().XAxis().Direction()) * _Curve.Circle().Radius()
                        : gp_Vec(_Curve.Ellipse().XAxis().Direction()) * _Curve.Ellipse().MajorRadius();
                    const auto _Y = _Curve.GetType() == GeomAbs_Circle
                        ? gp_Vec(_Curve.Circle().YAxis().Direction()) * _Curve.Circle().Radius()
                        : gp_Vec(_Curve.Ellipse().YAxis().Direction()) * _Curve.Ellipse().MinorRadius();
                    const auto _Minimum = std::atan2(_Y.Dot(gp_Vec(Tube_.SectionFrame.XDirection())),
                        _X.Dot(gp_Vec(Tube_.SectionFrame.XDirection()))) + kPi;
                    for (int _Lap = -4; _Lap <= 4; ++_Lap)
                    {
                        const auto _T = _Minimum + 2.0 * kPi * _Lap;
                        if (_T > _Part.First + kCurveTolerance && _T < _Part.Last - kCurveTolerance)
                            _Consider(_Index, _T, _Curve.Value(_T));
                    }
                }
            }
            const auto _SeamPart = _Curves[_SeamIndex];
            std::vector<SProfileCurve> _Ordered;
            if (_SeamParameter > _SeamPart.First + kCurveTolerance
                && _SeamParameter < _SeamPart.Last - kCurveTolerance)
            {
                auto _After = _SeamPart;
                auto _Before = _SeamPart;
                if (_SeamPart.Reversed) { _After.Last = _SeamParameter; _Before.First = _SeamParameter; }
                else { _After.First = _SeamParameter; _Before.Last = _SeamParameter; }
                _Ordered.push_back(_After);
                for (std::size_t _Offset = 1; _Offset < _Curves.size(); ++_Offset)
                    _Ordered.push_back(_Curves[(_SeamIndex + _Offset) % _Curves.size()]);
                _Ordered.push_back(_Before);
            }
            else
            {
                const auto _StartParameter = _SeamPart.Reversed ? _SeamPart.Last : _SeamPart.First;
                if (std::abs(_SeamParameter - _StartParameter) > kCurveTolerance)
                    _SeamIndex = (_SeamIndex + 1) % _Curves.size();
                for (std::size_t _Offset = 0; _Offset < _Curves.size(); ++_Offset)
                    _Ordered.push_back(_Curves[(_SeamIndex + _Offset) % _Curves.size()]);
            }
            double _U = 0.0;
            for (auto& _Part : _Ordered)
            {
                _Part.UFirst = _U;
                _U += _Part.Length();
                _Part.ULast = _U;
            }
            return _Ordered;
        }

        struct SSideSurface final
        {
            bool Plane = false;
            gp_Trsf FlatToPlane;
            occ::handle<Geom_Surface> Surface;
            double UScale = 1.0;
            double UOffset = 0.0;
            double Depth = 0.0;
            double OutsideClearance = 0.0;
            gp_Vec Outward;
            occ::handle<Geom_Curve> ProfileCurve;
            double ProfileLength = 0.0;
            bool Approximate = false;
            bool ReverseFace = false;
            bool TranslationPrism = false;
        };

        double WallDepth(const etp::PartAnalysis& Tube_, const SProfileCurve& Profile_,
            double OuterClearance_, double InnerClearance_,
            const std::optional<gp_Vec>& FixedDirection_ = {},
            const std::optional<double>& CurvatureRadius_ = {})
        {
            Bnd_Box _Bounds;
            BRepBndLib::AddOptimal(Tube_.Shape, _Bounds, false, false);
            double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
            _Bounds.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
            const auto _Reach = std::hypot(_X1 - _X0, std::hypot(_Y1 - _Y0, _Z1 - _Z0)) * 2.0 + 1.0;
            IntCurvesFace_ShapeIntersector _Intersector;
            _Intersector.Load(Tube_.Shape, kCurveTolerance);
            BRepAdaptor_Curve _Curve(Profile_.Edge);
            double _Depth = 0.0;
            double _SolidRayDepth = 0.0;
            double _SecondWall = _Reach;
            for (int _Index = 0; _Index < 65; ++_Index)
            {
                const auto _Ratio = (static_cast<double>(_Index) + 0.5) / 65.0;
                const auto _T = Profile_.First + (Profile_.Last - Profile_.First) * _Ratio;
                gp_Pnt _Point;
                gp_Vec _Tangent;
                _Curve.D1(_T, _Point, _Tangent);
                if (Profile_.Reversed) _Tangent.Reverse();
                auto _Normal = _Tangent.Crossed(gp_Vec(Tube_.SectionFrame.Direction())).Normalized();
                if (FixedDirection_) _Normal = *FixedDirection_;
                _Point.Translate(gp_Vec(Tube_.SectionFrame.Direction())
                    * ((Tube_.AxialFirst + Tube_.AxialLast) * 0.5 - Axial(_Point, Tube_.SectionFrame)));
                const auto _Origin = _Point.Translated(_Normal * OuterClearance_);
                _Intersector.Perform(gp_Lin(_Origin, gp_Dir(-_Normal)), 0.0, _Reach);
                std::vector<double> _Hits;
                for (int _Hit = 1; _Hit <= _Intersector.NbPnt(); ++_Hit)
                {
                    const auto _Distance = _Intersector.WParameter(_Hit);
                    if (_Distance > kCurveTolerance) _Hits.push_back(_Distance);
                }
                std::sort(_Hits.begin(), _Hits.end());
                _Hits.erase(std::unique(_Hits.begin(), _Hits.end(), [](double A_, double B_) {
                    return std::abs(A_ - B_) <= 1.0e-5;
                }), _Hits.end());
                if (_Hits.size() < 2) throw std::runtime_error("无法确定外壁到内壁的连续切割深度");
                _SolidRayDepth = std::max(_SolidRayDepth, _Hits[1] - OuterClearance_);
                // At a sharp corner a normal ray along the extreme end of a
                // flat face can remain in the adjacent wall all the way to
                // the opposite outside skin. It is not an inner-wall hit.
                // Use actual cavity-crossing rays to determine this wall's
                // depth; adjacent complete face cutters cover the corner.
                if (Tube_.SectionWires.size() > 1 && _Hits.size() < 4) continue;
                _Depth = std::max(_Depth, _Hits[1] - OuterClearance_);
                if (_Hits.size() >= 3) _SecondWall = std::min(_SecondWall, _Hits[2] - OuterClearance_);
            }
            if (_Depth <= kCurveTolerance) _Depth = _SolidRayDepth;
            if (_Depth + InnerClearance_ >= _SecondWall - kCurveTolerance
                && !(CurvatureRadius_ && _Depth >= *CurvatureRadius_))
                throw std::runtime_error("管型局部内壁需要独立的连续深度曲面，不能穿透对侧材料");
            return _Depth + InnerClearance_;
        }

        SSideSurface MakeSurface(const etp::PartAnalysis& Tube_, const SProfileCurve& Profile_,
            const STubeSideSketchOptions& Options_)
        {
            SSideSurface _Result;
            _Result.OutsideClearance = Options_.OuterClearance;
            BRepAdaptor_Curve _Curve(Profile_.Edge);
            auto _Start = Profile_.Start();
            _Start.Translate(gp_Vec(Tube_.SectionFrame.Direction())
                * (Tube_.AxialFirst - Axial(_Start, Tube_.SectionFrame)));
            if (_Curve.GetType() == GeomAbs_Line)
            {
                // A planar wall meets its neighbour at a sharp corner. Extra
                // inward clearance there would remove real neighbour-wall
                // material rather than empty cavity. End at the exact inner
                // wall and let the final boolean share that native surface.
                _Result.Depth = WallDepth(Tube_, Profile_, _Result.OutsideClearance, 0.0);
                _Result.OutsideClearance = 0.0;
                const auto _Tangent = gp_Vec(Profile_.Start(), Profile_.End()).Normalized();
                const auto _Axis = gp_Vec(Tube_.SectionFrame.Direction());
                _Result.Outward = _Tangent.Crossed(_Axis).Normalized();
                const auto _Origin = _Start.Translated(_Tangent * -Profile_.UFirst
                    + _Result.Outward * _Result.OutsideClearance);
                _Result.FlatToPlane.SetValues(
                    _Tangent.X(), _Axis.X(), _Result.Outward.X(), _Origin.X(),
                    _Tangent.Y(), _Axis.Y(), _Result.Outward.Y(), _Origin.Y(),
                    _Tangent.Z(), _Axis.Z(), _Result.Outward.Z(), _Origin.Z());
                _Result.Plane = true;
                return _Result;
            }
            if (_Curve.GetType() == GeomAbs_Circle)
            {
                auto _Center = _Curve.Circle().Location();
                _Center.Translate(gp_Vec(Tube_.SectionFrame.Direction())
                    * (Tube_.AxialFirst - Axial(_Center, Tube_.SectionFrame)));
                const auto _Radius = _Curve.Circle().Radius();
                gp_Pnt _Midpoint;
                gp_Vec _Tangent;
                _Curve.D1((Profile_.First + Profile_.Last) * 0.5, _Midpoint, _Tangent);
                if (Profile_.Reversed) _Tangent.Reverse();
                const auto _Normal = _Tangent.Crossed(gp_Vec(Tube_.SectionFrame.Direction())).Normalized();
                const auto _Radial = gp_Vec(_Curve.Circle().Location(), _Midpoint).Normalized();
                const auto _RadialSign = _Normal.Dot(_Radial) >= 0.0 ? 1.0 : -1.0;
                if (_RadialSign < 0.0)
                    _Result.OutsideClearance = std::min(_Result.OutsideClearance, _Radius * 0.25);
                _Result.Depth = WallDepth(Tube_, Profile_, _Result.OutsideClearance, Options_.InnerClearance,
                    {}, _RadialSign > 0.0 ? std::optional<double>(_Radius) : std::nullopt);
                const auto _CrossesCurvatureCentre = _RadialSign > 0.0
                    && _Result.Depth >= _Radius - kCurveTolerance;
                if (_CrossesCurvatureCentre)
                {
                    if (Profile_.Last - Profile_.First >= kPi - 1.0e-3)
                        throw std::runtime_error("实心圆截面穿过曲率中心的贯穿切割不属于管壁镂空");
                    // A small outside corner radius may be smaller than the
                    // wall thickness. Pointwise normal offsets then fold at
                    // the curvature centre. Sweep the entire exact corner
                    // face along its bisector instead: the outer boundary is
                    // unchanged and the cutter remains one regular solid.
                    _Result.Outward = _Normal;
                    _Result.TranslationPrism = true;
                    _Result.Depth = WallDepth(Tube_, Profile_, _Result.OutsideClearance,
                        Options_.InnerClearance, _Normal);
                }
                if (_Radius + _RadialSign * _Result.OutsideClearance <= kCurveTolerance)
                    throw std::runtime_error("凹圆弧外侧间隙超过曲率半径");
                _Result.Surface = new Geom_CylindricalSurface(
                    gp_Ax3(_Center, Tube_.SectionFrame.Direction(), gp_Dir(gp_Vec(_Center, _Start))),
                    _Radius + (_Result.TranslationPrism ? 0.0 : _RadialSign * _Result.OutsideClearance));
                _Result.UScale = _RadialSign / _Radius;
                _Result.UOffset = -Profile_.UFirst * _Result.UScale;
                _Result.ReverseFace = _RadialSign < 0.0;
                return _Result;
            }
            // On a non-circular extrusion, arc length and the native curve
            // parameter have a nonlinear relationship. Keep the exact native
            // extrusion surface and approximate only that smooth UV mapping.
            double _First, _Last;
            auto _Native = BRep_Tool::Curve(Profile_.Edge, _First, _Last);
            if (_Native.IsNull()) throw std::runtime_error("原生管型边缺少空间曲线");
            occ::handle<Geom_Curve> _Basis = new Geom_TrimmedCurve(
                _Native, Profile_.First, Profile_.Last);
            if (Profile_.Reversed) _Basis->Reverse();
            gp_Trsf _Move;
            _Move.SetTranslation(gp_Vec(Tube_.SectionFrame.Direction())
                * (Tube_.AxialFirst - Axial(_Basis->Value(_Basis->FirstParameter()), Tube_.SectionFrame)));
            _Basis->Transform(_Move);
            _Result.ProfileCurve = _Basis;
            _Result.ProfileLength = Profile_.ULast - Profile_.UFirst;
            _Result.Depth = WallDepth(Tube_, Profile_, _Result.OutsideClearance, Options_.InnerClearance);
            const occ::handle<Geom_Surface> _Extrusion = new Geom_SurfaceOfLinearExtrusion(
                _Basis, Tube_.SectionFrame.Direction());
            _Result.Surface = new Geom_OffsetSurface(_Extrusion, _Result.OutsideClearance);
            _Result.UOffset = -Profile_.UFirst;
            _Result.Approximate = true;
            return _Result;
        }

        gp_Pnt2d NonlinearUV(const gp_Pnt2d& Point_, const SSideSurface& Mapping_)
        {
            GeomAdaptor_Curve _Curve(Mapping_.ProfileCurve);
            const auto _First = _Curve.FirstParameter();
            const auto _Last = _Curve.LastParameter();
            const auto _Length = Mapping_.ProfileLength;
            const auto _U = std::clamp(Point_.X() + Mapping_.UOffset, 0.0, _Length);
            if (_U <= kCurveTolerance) return gp_Pnt2d(_First, Point_.Y());
            if (_Length - _U <= kCurveTolerance) return gp_Pnt2d(_Last, Point_.Y());
            GCPnts_AbscissaPoint _Inverse(kCurveTolerance, _Curve, _U, _First);
            if (!_Inverse.IsDone()) throw std::runtime_error("管型弧长参数反求失败");
            return gp_Pnt2d(_Inverse.Parameter(), Point_.Y());
        }

        occ::handle<Geom2d_BSplineCurve> SmoothMappedCurve(
            const occ::handle<Geom2d_Curve>& Source_, double First_, double Last_,
            const SSideSurface& Mapping_)
        {
            // The refinement nodes constrain a single smooth B-spline. They
            // never become polygon edges, faces, or separate boolean tools.
            constexpr double _MappingTolerance = 1.0e-5;
            for (int _Intervals = 16; _Intervals <= 4096; _Intervals *= 2)
            {
                occ::handle<NCollection_HArray1<gp_Pnt2d>> _Points =
                    new NCollection_HArray1<gp_Pnt2d>(1, _Intervals + 1);
                occ::handle<NCollection_HArray1<double>> _Parameters =
                    new NCollection_HArray1<double>(1, _Intervals + 1);
                for (int _Index = 0; _Index <= _Intervals; ++_Index)
                {
                    const auto _T = First_ + (Last_ - First_) * _Index / _Intervals;
                    _Points->SetValue(_Index + 1, NonlinearUV(Source_->Value(_T), Mapping_));
                    _Parameters->SetValue(_Index + 1, _T);
                }
                Geom2dAPI_Interpolate _Interpolation(_Points, _Parameters, false, 1.0e-10);
                const auto _Tangent = [&](double Parameter_) {
                    gp_Pnt2d _Point;
                    gp_Vec2d _SourceTangent;
                    Source_->D1(Parameter_, _Point, _SourceTangent);
                    const auto _UV = NonlinearUV(_Point, Mapping_);
                    gp_Pnt _ProfilePoint;
                    gp_Vec _ProfileTangent;
                    Mapping_.ProfileCurve->D1(_UV.X(), _ProfilePoint, _ProfileTangent);
                    return gp_Vec2d(_SourceTangent.X() / _ProfileTangent.Magnitude(), _SourceTangent.Y());
                };
                const auto _StartTangent = _Tangent(First_);
                const auto _EndTangent = _Tangent(Last_);
                if (_StartTangent.SquareMagnitude() > 1.0e-20 && _EndTangent.SquareMagnitude() > 1.0e-20)
                    _Interpolation.Load(_StartTangent, _EndTangent, false);
                _Interpolation.Perform();
                if (!_Interpolation.IsDone()) throw std::runtime_error("管壁平滑曲线插值失败");
                const auto _Curve = _Interpolation.Curve();
                bool _WithinTolerance = true;
                for (int _Index = 0; _Index < _Intervals && _WithinTolerance; ++_Index)
                    for (int _Quarter = 1; _Quarter <= 3; ++_Quarter)
                    {
                        const auto _T = First_ + (Last_ - First_)
                            * (_Index + _Quarter * 0.25) / _Intervals;
                        const auto _Expected = NonlinearUV(Source_->Value(_T), Mapping_);
                        const auto _Actual = _Curve->Value(_T);
                        if (Mapping_.Surface->Value(_Expected.X(), _Expected.Y()).Distance(
                            Mapping_.Surface->Value(_Actual.X(), _Actual.Y())) > _MappingTolerance)
                        {
                            _WithinTolerance = false;
                            break;
                        }
                    }
                if (_WithinTolerance) return _Curve;
            }
            throw std::runtime_error("管壁平滑曲线无法达到 0.00001 mm 映射残差要求");
        }

        TopoDS_Face MapCurvedFace(const TopoDS_Face& Flat_, const SSideSurface& Mapping_)
        {
            std::vector<TopoDS_Wire> _MappedWires;
            const auto _Outer = BRepTools::OuterWire(Flat_);
            std::vector<TopoDS_Wire> _SourceWires{_Outer};
            for (TopExp_Explorer _Wires(Flat_, TopAbs_WIRE); _Wires.More(); _Wires.Next())
                if (!_Wires.Current().IsSame(_Outer)) _SourceWires.push_back(TopoDS::Wire(_Wires.Current()));
            for (const auto& _SourceWire : _SourceWires)
            {
                BRepBuilderAPI_MakeWire _Wire;
                for (BRepTools_WireExplorer _Edges(_SourceWire, Flat_);
                    _Edges.More(); _Edges.Next())
                {
                    const auto _Source = TopoDS::Edge(_Edges.Current());
                    double _First, _Last;
                    auto _SourceCurve = BRep_Tool::CurveOnSurface(_Source, Flat_, _First, _Last);
                    if (!_SourceCurve.IsNull())
                    {
                        GeomAdaptor_Surface _PlaneAdaptor(BRep_Tool::Surface(Flat_));
                        if (_PlaneAdaptor.GetType() != GeomAbs_Plane)
                            throw std::runtime_error("二维完整区域不是平面曲线");
                        const auto _Plane = _PlaneAdaptor.Plane();
                        gp_Trsf2d _ToWorldXY;
                        _ToWorldXY.SetValues(_Plane.XAxis().Direction().X(), _Plane.YAxis().Direction().X(), _Plane.Location().X(),
                            _Plane.XAxis().Direction().Y(), _Plane.YAxis().Direction().Y(), _Plane.Location().Y());
                        _SourceCurve = occ::down_cast<Geom2d_Curve>(_SourceCurve->Copy());
                        _SourceCurve->Transform(_ToWorldXY);
                    }
                    if (_SourceCurve.IsNull()) throw std::runtime_error("二维完整区域缺少曲线边界");
                    occ::handle<Geom2d_BSplineCurve> _Curve;
                    if (!Mapping_.ProfileCurve.IsNull())
                        _Curve = SmoothMappedCurve(_SourceCurve, _First, _Last, Mapping_);
                    else
                    {
                        const occ::handle<Geom2d_Curve> _Trimmed = new Geom2d_TrimmedCurve(_SourceCurve, _First, _Last);
                        try { _Curve = Geom2dConvert::CurveToBSplineCurve(_Trimmed); }
                        catch (const Standard_Failure&)
                        {
                            // Native ellipse/Bezier parallel curves are exact
                            // offset curves, which are not rational conics.
                            // Retain a smooth spline with an explicit kernel
                            // approximation bound instead of polygonizing it.
                            Geom2dConvert_ApproxCurve _Approximation(_Trimmed,
                                kCurveTolerance, GeomAbs_C1, 2048, 8);
                            if (!_Approximation.IsDone() || _Approximation.Curve().IsNull()
                                || _Approximation.MaxError() > kCurveTolerance)
                                throw std::runtime_error("完整偏置曲线无法满足平滑映射公差");
                            _Curve = _Approximation.Curve();
                        }
                        for (int _Index = 1; _Index <= _Curve->NbPoles(); ++_Index)
                        {
                            const auto _Pole = _Curve->Pole(_Index);
                            _Curve->SetPole(_Index, gp_Pnt2d(
                                _Pole.X() * Mapping_.UScale + Mapping_.UOffset, _Pole.Y()));
                        }
                    }
                    auto _Edge = BRepBuilderAPI_MakeEdge(_Curve, Mapping_.Surface,
                        _Curve->FirstParameter(), _Curve->LastParameter()).Edge();
                    if (_Source.Orientation() == TopAbs_REVERSED) _Edge.Reverse();
                    if (!BRepLib::BuildCurve3d(_Edge, kCurveTolerance))
                        throw std::runtime_error("管壁曲线的空间表示生成失败");
                    _Wire.Add(_Edge);
                }
                if (!_Wire.IsDone()) throw std::runtime_error("管壁完整曲线轮廓无法闭合");
                _MappedWires.push_back(_Wire.Wire());
            }
            if (_MappedWires.empty()) throw std::runtime_error("管壁完整区域没有外轮廓");
            BRepBuilderAPI_MakeFace _Face(Mapping_.Surface, _MappedWires.front(), true);
            for (std::size_t _Index = 1; _Index < _MappedWires.size(); ++_Index)
            {
                if (Mapping_.ReverseFace) _MappedWires[_Index].Reverse();
                _Face.Add(_MappedWires[_Index]);
            }
            if (!_Face.IsDone()) throw std::runtime_error("管壁曲线区域无法构成曲面");
            auto _Result = _Face.Face();
            if (Mapping_.ReverseFace) _Result.Reverse();
            if (!BRepCheck_Analyzer(_Result).IsValid())
                throw std::runtime_error("管壁完整曲线区域无效");
            return _Result;
        }

        TopoDS_Shape MakeCutter(const TopoDS_Face& Flat_, const SSideSurface& Surface_,
            const STubeSideSketchOptions& Options_)
        {
            if (Surface_.Plane)
            {
                const auto _Face = BRepBuilderAPI_Transform(Flat_, Surface_.FlatToPlane, true).Shape();
                BRepPrimAPI_MakePrism _Prism(_Face,
                    Surface_.Outward * -(Surface_.Depth + Surface_.OutsideClearance), true, true);
                if (!_Prism.IsDone()) throw std::runtime_error("完整平面曲线切刀生成失败");
                return _Prism.Shape();
            }
            const auto _Face = MapCurvedFace(Flat_, Surface_);
            if (Surface_.TranslationPrism)
            {
                gp_Trsf _Translation;
                _Translation.SetTranslation(Surface_.Outward * Surface_.OutsideClearance);
                const auto _Start = BRepBuilderAPI_Transform(_Face, _Translation, true).Shape();
                BRepPrimAPI_MakePrism _Prism(_Start,
                    Surface_.Outward * -(Surface_.Depth + Surface_.OutsideClearance), true, true);
                if (!_Prism.IsDone() || _Prism.Shape().IsNull()
                    || !BRepCheck_Analyzer(_Prism.Shape()).IsValid())
                    throw std::runtime_error("小圆角完整曲面区域的连续贯穿切刀生成失败");
                return _Prism.Shape();
            }
            BRepOffsetAPI_MakeThickSolid _Solid;
            _Solid.MakeThickSolidBySimple(_Face, -(Surface_.Depth + Surface_.OutsideClearance));
            if (!_Solid.IsDone() || _Solid.Shape().IsNull()
                || !BRepCheck_Analyzer(_Solid.Shape()).IsValid())
                throw std::runtime_error("完整曲面边界的连续实体切刀生成失败");
            return _Solid.Shape();
        }
    }

    STubeBRepUnfoldingResult UnfoldTubeSideSketchCurveReference(const TopoDS_Shape& Shape_)
    {
        STubeBRepUnfoldingResult _Result;
        _Result.Method = "exact-extrusion-profile-curves";
        try
        {
            etp::AnalyzerOptions _Options;
            _Options.MaterializeFaceSplits = false;
            _Options.DistanceTolerance = 1.0e-4;
            const auto _Tube = etp::ExtrusionToolpathAnalyzer(_Options).Analyze(Shape_);
            if (_Tube.Shape.IsNull() || _Tube.SectionWires.empty())
                throw std::runtime_error("无法确定管材原生闭合截面");
            const auto _Profiles = ExactOuterProfile(_Tube);
            _Result.Length = _Tube.AxialLast - _Tube.AxialFirst;
            _Result.Perimeter = _Profiles.back().ULast;
            _Result.bOK = _Result.Length > kCurveTolerance && _Result.Perimeter > kCurveTolerance;
            _Result.Periodic = _Result.bOK;
            _Result.Approximate = false;
            const auto _Direction = [&](const gp_Dir& Direction_) {
                gp_Vec _Vector(Direction_);
                _Vector.Transform(_Tube.NormalizedToSource);
                _Vector.Normalize();
                return std::array<double, 3>{_Vector.X(), _Vector.Y(), _Vector.Z()};
            };
            _Result.Axis = _Direction(_Tube.SectionFrame.Direction());
            _Result.SectionX = _Direction(_Tube.SectionFrame.XDirection());
            _Result.SectionY = _Direction(_Tube.SectionFrame.YDirection());
            _Result.ProfileStationCount = 1;
            _Result.PointCount = 4;
            _Result.LateralRectangle = {_Result.bOK, 0.0, _Result.Length, 0.0, _Result.Perimeter, true};
            STubeUnfoldedSurface _Surface;
            _Surface.LoopID = "outer";
            _Surface.UPeriod = _Result.Perimeter;
            _Surface.Rectangle = _Result.LateralRectangle;
            for (int _End = 0; _End < 2; ++_End)
            {
                STubeUnfoldedWire _Wire;
                _Wire.ID = static_cast<std::size_t>(_End);
                _Wire.Role = "end-boundary";
                _Wire.Closed = true;
                _Wire.WrapsAroundSeam = true;
                const auto _S = _End == 0 ? 0.0 : _Result.Length;
                _Wire.Points = {{_S, 0.0}, {_S, _Result.Perimeter}};
                _Surface.Wires.push_back(std::move(_Wire));
            }
            for (std::size_t _Index = 1; _Index < _Profiles.size(); ++_Index)
            {
                const auto& _Previous = _Profiles[_Index - 1];
                const auto& _Current = _Profiles[_Index];
                // Seam splits and adjacent pieces of one analytic supporting
                // curve do not introduce a physical profile junction.
                if (SameNativeProfileCurve(_Previous, _Current)) continue;
                const auto _U = _Current.UFirst;
                if (_U <= kCurveTolerance || _U >= _Result.Perimeter - kCurveTolerance)
                    continue;
                STubeUnfoldedWire _Wire;
                _Wire.ID = _Surface.Wires.size();
                _Wire.Role = "profile-junction";
                _Wire.Points = {{0.0, _U}, {_Result.Length, _U}};
                _Result.PointCount += _Wire.Points.size();
                _Surface.Wires.push_back(std::move(_Wire));
            }
            _Result.Surfaces.push_back(std::move(_Surface));
        }
        catch (const Standard_Failure& Error_)
        {
            _Result.Diagnostic = Error_.what();
        }
        catch (const std::exception& Error_)
        {
            _Result.Diagnostic = Error_.what();
        }
        return _Result;
    }

    STubeSideSketchResult ApplyTubeSideSketchCurves(const TopoDS_Shape& Shape_,
        const iCAX::Data::ObjectMap& Sketch_, const STubeSideSketchOptions& Options_)
    {
        STubeSideSketchResult _Result;
        _Result.Method = "curve-preserving-profile-surface";
        _Result.Approximate = false;
        try
        {
            if (Shape_.IsNull()) throw std::runtime_error("二维绘制缺少管坯实体");
            const auto _PositiveFinite = [](double Value_) { return std::isfinite(Value_) && Value_ > 0.0; };
            if (!_PositiveFinite(Options_.DistanceTolerance)
                || !_PositiveFinite(Options_.AngularToleranceRadians)
                || Options_.AngularToleranceRadians >= kPi
                || !_PositiveFinite(Options_.TrajectoryWidth)
                || !_PositiveFinite(Options_.OuterClearance)
                || !_PositiveFinite(Options_.InnerClearance)
                || Options_.MaximumToolPatches == 0)
                throw std::invalid_argument("二维曲线包覆参数无效");
            etp::AnalyzerOptions _Options;
            _Options.MaterializeFaceSplits = false;
            _Options.DistanceTolerance = Options_.DistanceTolerance;
            _Options.AngularToleranceRadians = Options_.AngularToleranceRadians;
            const auto _Tube = etp::ExtrusionToolpathAnalyzer(_Options).Analyze(Shape_);
            if (_Tube.Shape.IsNull() || _Tube.SectionWires.empty())
                throw std::runtime_error("无法确定管材轴向和原生截面曲线");
            const auto _Profiles = ExactOuterProfile(_Tube);
            const auto _Length = _Tube.AxialLast - _Tube.AxialFirst;
            const auto _Period = _Profiles.back().ULast;
            auto _EffectiveSketch = Sketch_;
            if (!_EffectiveSketch.contains("trajectoryWidth"))
                _EffectiveSketch["trajectoryWidth"] = Options_.TrajectoryWidth;
            const auto _Regions = BuildTubeSideSketchPlanarRegions(_EffectiveSketch, _Length, _Period, kCurveTolerance);
            if (!_Regions.bOK) throw std::runtime_error(_Regions.Diagnostic);
            _Result.ClosedLoopCount = _Regions.ClosedLoopCount;
            _Result.OpenTrajectoryCount = _Regions.OpenTrajectoryCount;
            _Result.TrajectoryPointCount = _Regions.CurveCount;
            _Result.Approximate = _Regions.Approximate;
            NCollection_List<TopoDS_Shape> _Tools;
            for (const auto& _Profile : _Profiles)
            {
                const auto _Clip = Rectangle(_Profile.UFirst, _Profile.ULast, 0.0, _Length);
                std::optional<SSideSurface> _Surface;
                for (const auto& _Region : _Regions.Faces)
                {
                    BRepAlgoAPI_Common _Common;
                    NCollection_List<TopoDS_Shape> _Arguments, _ClipTools;
                    _Arguments.Append(_Region);
                    _ClipTools.Append(_Clip);
                    _Common.SetArguments(_Arguments);
                    _Common.SetTools(_ClipTools);
                    _Common.SetNonDestructive(true);
                    _Common.SetFuzzyValue(kCurveTolerance);
                    _Common.Build();
                    if (!_Common.IsDone()) throw std::runtime_error("完整曲线区域与管型曲面边界裁剪失败");
                    for (TopExp_Explorer _Faces(_Common.Shape(), TopAbs_FACE); _Faces.More(); _Faces.Next())
                    {
                        if (!_Surface) _Surface = MakeSurface(_Tube, _Profile, Options_);
                        _Result.Approximate = _Result.Approximate || _Surface->Approximate;
                        auto _Tool = MakeCutter(TopoDS::Face(_Faces.Current()), *_Surface, Options_);
                        if (!BRepCheck_Analyzer(_Tool).IsValid())
                            throw std::runtime_error("完整曲线切刀不是有效实体");
                        _Tools.Append(_Tool);
                        ++_Result.ToolPatchCount;
                        if (_Result.ToolPatchCount > Options_.MaximumToolPatches)
                            throw std::runtime_error("完整曲面切刀数量超出上限");
                    }
                }
            }
            if (_Tools.IsEmpty())
            {
                _Result.bOK = true;
                _Result.Shape = Shape_;
                _Result.Diagnostic = "轨迹位于管坯轴向范围外，裁剪后保留原管坯";
                return _Result;
            }
            NCollection_List<TopoDS_Shape> _Arguments;
            _Arguments.Append(_Tube.Shape);
            BRepAlgoAPI_Cut _Cut;
            _Cut.SetArguments(_Arguments);
            _Cut.SetTools(_Tools);
            _Cut.SetNonDestructive(true);
            _Cut.SetFuzzyValue(kCurveTolerance);
            _Cut.Build();
            _Result.BooleanCutCount = 1;
            if (!_Cut.IsDone() || _Cut.Shape().IsNull() || !BRepCheck_Analyzer(_Cut.Shape()).IsValid())
                throw std::runtime_error("完整曲线实体整体切除失败");
            auto _Shape = _Cut.Shape();
            const auto _Before = Volume(_Tube.Shape);
            const auto _After = Volume(_Shape);
            if (_After <= kCurveTolerance) throw std::runtime_error("二维绘制切除后没有剩余实体");
            _Result.Changed = _Before - _After > 1.0e-8;
            if (!_Result.Changed) throw std::runtime_error("轨迹没有穿过当前管壁材料");
            // The recovered source transform is a direct isometry. Applying
            // it as a location preserves native offset curves and their
            // matching pcurves instead of rewriting both representations.
            _Shape = BRepBuilderAPI_Transform(_Shape, _Tube.NormalizedToSource, false).Shape();
            if (!BRepCheck_Analyzer(_Shape).IsValid())
            {
                // Recompute parameter correspondence on owned topology only;
                // the frozen blank and shared boolean input stay immutable.
                _Shape = BRepBuilderAPI_Copy(_Shape, true, false).Shape();
                BRepLib::SameParameter(_Shape, kCurveTolerance, true);
                if (!BRepCheck_Analyzer(_Shape).IsValid())
                    throw std::runtime_error("完整曲线实体在恢复零件位置后未通过几何校验");
            }
            _Result.Shape = _Shape;
            _Result.bOK = true;
            _Result.Diagnostic = "已保留完整曲线边界并一次整体切除";
        }
        catch (const Standard_Failure& Error_)
        {
            _Result.Diagnostic = std::string("曲线曲面建模失败：") + Error_.what();
        }
        catch (const std::exception& Error_)
        {
            _Result.Diagnostic = Error_.what();
        }
        return _Result;
    }
}
