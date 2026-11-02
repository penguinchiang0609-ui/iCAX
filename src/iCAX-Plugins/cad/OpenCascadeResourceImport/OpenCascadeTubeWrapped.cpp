#include "pch.h"
#include "OpenCascadeTubeWrapped.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
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
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
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
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <NCollection_List.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <NCollection_IndexedMap.hxx>
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

#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

namespace iCAX::CAD
{
    namespace
    {
        struct SGeometryClearance final
        {
            double OuterClearance = 0.2;
            double InnerClearance = 0.2;
        };

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

        double WireArea(const TopoDS_Wire& Wire_)
        {
            GProp_GProps _Properties;
            BRepGProp::SurfaceProperties(BRepBuilderAPI_MakeFace(Wire_).Face(), _Properties);
            return std::abs(_Properties.Mass());
        }

        std::vector<SProfileCurve> ExactOuterProfile(const STubeWrappedHostGeometry& Tube_)
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

        double WallDepth(const STubeWrappedHostGeometry& Tube_, const SProfileCurve& Profile_,
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

        SSideSurface MakeSurface(const STubeWrappedHostGeometry& Tube_, const SProfileCurve& Profile_,
            const SGeometryClearance& Options_)
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

        TopoDS_Shape MakeCutter(const TopoDS_Face& Flat_, const SSideSurface& Surface_)
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

        bool PointOnCurve(const gp_Pnt& Point_, const TopoDS_Edge& Edge_)
        {
            double _First,_Last;const auto _Curve=BRep_Tool::Curve(Edge_,_First,_Last);
            if(_Curve.IsNull()) return false;
            GeomAPI_ProjectPointOnCurve _Projection(Point_,_Curve,_First,_Last);
            return _Projection.NbPoints()>0 && _Projection.LowerDistance()<1.0e-4;
        }
        bool SelectedFlatBoundary(const TopoDS_Edge& Edge_,const std::vector<TopoDS_Edge>& BoundaryEdges_,double Period_)
        {
            if (BoundaryEdges_.empty()) return true;
            BRepAdaptor_Curve _Curve(Edge_);
            const auto _Mid=_Curve.Value((_Curve.FirstParameter()+_Curve.LastParameter())*0.5);
            for(const auto& _Source:BoundaryEdges_)
            {
                BRepAdaptor_Curve _Boundary(_Source);
                const auto _Anchor=_Boundary.Value((_Boundary.FirstParameter()+_Boundary.LastParameter())*0.5);
                const auto _Shift=std::round((_Anchor.X()-_Mid.X())/Period_)*Period_;
                bool _Matches=true;
                for(double _Fraction:{0.2,0.5,0.8})
                {
                    auto _Point=_Curve.Value(_Curve.FirstParameter()+(_Curve.LastParameter()-_Curve.FirstParameter())*_Fraction);
                    _Point.SetX(_Point.X()+_Shift);
                    if(!PointOnCurve(_Point,_Source)){_Matches=false;break;}
                }
                if(_Matches) return true;
            }
            return false;
        }
        SSideSurface ExactOuterSurface(SSideSurface Surface_)
        {
            if(!Surface_.Plane && !Surface_.TranslationPrism)
            {
                if(auto _Cylinder=occ::down_cast<Geom_CylindricalSurface>(Surface_.Surface);!_Cylinder.IsNull())
                    Surface_.Surface=new Geom_CylindricalSurface(_Cylinder->Position(),
                        _Cylinder->Radius()-(Surface_.ReverseFace?-1.0:1.0)*Surface_.OutsideClearance);
                else if(auto _Offset=occ::down_cast<Geom_OffsetSurface>(Surface_.Surface);!_Offset.IsNull())
                    Surface_.Surface=_Offset->BasisSurface();
            }
            Surface_.OutsideClearance=0;return Surface_;
        }
        gp_Pnt MappedPoint(const gp_Pnt& P_,const SSideSurface& Surface_)
        {
            if(Surface_.Plane) return P_.Transformed(Surface_.FlatToPlane);
            const auto _UV=Surface_.ProfileCurve.IsNull()?gp_Pnt2d(P_.X()*Surface_.UScale+Surface_.UOffset,P_.Y())
                :NonlinearUV(gp_Pnt2d(P_.X(),P_.Y()),Surface_);
            return Surface_.Surface->Value(_UV.X(),_UV.Y());
        }
        TopoDS_Shape PeriodicUVRegions(const TopoDS_Shape& Regions_, double Period_)
        {
            // The document retains complete source curves, including curves
            // crossing the seam or spanning several laps. Only evaluation
            // translates copies into the physical side-wall atlas.
            if (!std::isfinite(Period_) || Period_ <= kCurveTolerance)
                throw std::invalid_argument("Wrapped UV period is invalid");
            TopoDS_Compound _Result; BRep_Builder _Builder; _Builder.MakeCompound(_Result);
            std::size_t _Copies = 0;
            for (TopExp_Explorer _Faces(Regions_, TopAbs_FACE); _Faces.More(); _Faces.Next())
            {
                Bnd_Box _Bounds; BRepBndLib::AddOptimal(_Faces.Current(), _Bounds, false, false);
                if (_Bounds.IsVoid() || _Bounds.IsOpen())
                    throw std::invalid_argument("Wrapped UV region must be bounded");
                double _MinX, _MinY, _MinZ, _MaxX, _MaxY, _MaxZ;
                _Bounds.Get(_MinX, _MinY, _MinZ, _MaxX, _MaxY, _MaxZ);
                const auto _First = std::floor((_MinX + kCurveTolerance) / Period_);
                const auto _Last = std::floor((_MaxX - kCurveTolerance) / Period_);
                if (!std::isfinite(_First) || !std::isfinite(_Last) || _Last - _First > 20000)
                    throw std::invalid_argument("Wrapped UV region exceeds the periodic copy limit");
                for (double _Lap = _First; _Lap <= _Last; _Lap += 1.0)
                {
                    if (++_Copies > 20000 || _Lap + 1.0 == _Lap)
                        throw std::invalid_argument("Wrapped UV region exceeds the periodic copy limit");
                    if (_Lap == 0.0) _Builder.Add(_Result, _Faces.Current());
                    else {
                        gp_Trsf _Shift; _Shift.SetTranslation(gp_Vec(-_Lap * Period_, 0.0, 0.0));
                        _Builder.Add(_Result, BRepBuilderAPI_Transform(_Faces.Current(), _Shift, true).Shape());
                    }
                }
            }
            return _Result;
        }
    }

    STubeWrappedRegionResult EvaluateTubeWrappedRegion(const STubeWrappedHostGeometry& Host_,
        const TopoDS_Shape& UVFaces_, double OuterClearance_, double InnerClearance_)
    {
        if (Host_.Shape.IsNull() || Host_.SectionWires.empty() || UVFaces_.IsNull())
            throw std::invalid_argument("Wrapped region requires an extrusion and planar UV faces");
        if (!std::isfinite(OuterClearance_) || OuterClearance_ <= 0.0
            || !std::isfinite(InnerClearance_) || InnerClearance_ <= 0.0)
            throw std::invalid_argument("Wrapped region clearance must be positive and finite");
        if (!std::isfinite(Host_.AxialFirst) || !std::isfinite(Host_.AxialLast)
            || Host_.AxialLast - Host_.AxialFirst <= kCurveTolerance)
            throw std::invalid_argument("Wrapped host extrusion interval is invalid");
        const SGeometryClearance _Clearance{OuterClearance_, InnerClearance_};
        const auto& _Tube = Host_;
        const auto _Profiles = ExactOuterProfile(_Tube);
        const auto _Regions = PeriodicUVRegions(UVFaces_, _Profiles.back().ULast);
        const auto _Length = _Tube.AxialLast - _Tube.AxialFirst;
        STubeWrappedRegionResult _Result;
        TopoDS_Compound _Cutters;
        BRep_Builder _Builder;
        _Builder.MakeCompound(_Cutters);
        NCollection_List<TopoDS_Shape> _Patches;
        for (const auto& _Profile : _Profiles)
        {
            const auto _Clip = Rectangle(_Profile.UFirst, _Profile.ULast, 0.0, _Length);
            std::optional<SSideSurface> _Surface;
            for (TopExp_Explorer _Region(_Regions, TopAbs_FACE); _Region.More(); _Region.Next())
            {
                BRepAlgoAPI_Common _Common;
                NCollection_List<TopoDS_Shape> _Arguments, _ClipTools;
                _Arguments.Append(_Region.Current());
                _ClipTools.Append(_Clip);
                _Common.SetArguments(_Arguments);
                _Common.SetTools(_ClipTools);
                _Common.SetNonDestructive(true);
                _Common.SetFuzzyValue(kCurveTolerance);
                _Common.Build();
                if (!_Common.IsDone())
                    throw std::runtime_error("Wrapped UV region clipping failed");
                for (TopExp_Explorer _Faces(_Common.Shape(), TopAbs_FACE); _Faces.More(); _Faces.Next())
                {
                    if (!_Surface) _Surface = MakeSurface(_Tube, _Profile, _Clearance);
                    _Result.Approximate = _Result.Approximate || _Surface->Approximate;
                    const auto _Tool = MakeCutter(TopoDS::Face(_Faces.Current()), *_Surface);
                    if (_Tool.IsNull() || !BRepCheck_Analyzer(_Tool).IsValid())
                        throw std::runtime_error("Wrapped removal patch is not a valid solid");
                    _Builder.Add(_Cutters, _Tool);
                    _Patches.Append(_Tool);
                    if (++_Result.ToolPatchCount > 20000)
                        throw std::runtime_error("Wrapped region exceeds the cutter patch limit");
                }
            }
        }
        _Result.Shape = _Cutters;
        if (_Patches.Size() == 1) _Result.Shape = _Patches.First();
        else if (_Patches.Size() > 1)
        {
            // Adjacent wall cutters can intersect at a profile junction or
            // periodic seam. A compound of overlapping solids is not their
            // material-set union and is not a valid single Boolean operand.
            NCollection_List<TopoDS_Shape> _Arguments, _Tools;
            bool _First = true;
            for (const auto& _Patch : _Patches)
            {
                if (_First) { _Arguments.Append(_Patch); _First = false; }
                else _Tools.Append(_Patch);
            }
            BRepAlgoAPI_Fuse _Union;
            _Union.SetArguments(_Arguments); _Union.SetTools(_Tools);
            _Union.SetNonDestructive(true); _Union.SetFuzzyValue(kCurveTolerance);
            _Union.SetToFillHistory(false); _Union.Build();
            if (!_Union.IsDone() || _Union.Shape().IsNull()
                || !BRepCheck_Analyzer(_Union.Shape()).IsValid())
                throw std::runtime_error("Wrapped removal patch union failed");
            _Result.Shape = _Union.Shape();
        }
        return _Result;
    }

    std::vector<TopoDS_Edge> MapTubeWrappedBoundaries(const STubeWrappedHostGeometry& Host_,
        const TopoDS_Shape& UVFaces_, const std::vector<TopoDS_Edge>& BoundaryEdges_)
    {
        if (Host_.Shape.IsNull() || Host_.SectionWires.empty() || UVFaces_.IsNull()
            || !std::isfinite(Host_.AxialFirst) || !std::isfinite(Host_.AxialLast)
            || Host_.AxialLast - Host_.AxialFirst <= kCurveTolerance)
            throw std::invalid_argument("Wrapped boundary mapping requires exact extrusion geometry");
        const auto _Profiles = ExactOuterProfile(Host_);
        const auto _Period = _Profiles.back().ULast;
        const auto _PeriodicRegions = PeriodicUVRegions(UVFaces_, _Period);
        const auto _Length = Host_.AxialLast - Host_.AxialFirst;
        std::vector<TopoDS_Edge> _Result;
        for (const auto& _Profile : _Profiles)
        {
            const auto _Clip = Rectangle(_Profile.UFirst, _Profile.ULast, 0.0, _Length);
            std::optional<SSideSurface> _Surface;
            for (TopExp_Explorer _Regions(_PeriodicRegions, TopAbs_FACE); _Regions.More(); _Regions.Next())
            {
                BRepAlgoAPI_Common _Common(_Regions.Current(), _Clip);
                if (!_Common.IsDone()) throw std::runtime_error("Wrapped boundary clipping failed");
                for (TopExp_Explorer _Faces(_Common.Shape(), TopAbs_FACE); _Faces.More(); _Faces.Next())
                {
                    if (!_Surface) _Surface = ExactOuterSurface(MakeSurface(Host_, _Profile, SGeometryClearance{}));
                    const auto _Flat = TopoDS::Face(_Faces.Current());
                    const auto _Mapped = _Surface->Plane
                        ? TopoDS::Face(BRepBuilderAPI_Transform(_Flat, _Surface->FlatToPlane, true).Shape())
                        : MapCurvedFace(_Flat, *_Surface);
                    for (TopExp_Explorer _Edges(_Flat, TopAbs_EDGE); _Edges.More(); _Edges.Next())
                    {
                        const auto _FlatEdge = TopoDS::Edge(_Edges.Current());
                        if (!SelectedFlatBoundary(_FlatEdge, BoundaryEdges_, _Period)) continue;
                        BRepAdaptor_Curve _Curve(_FlatEdge);
                        for (TopExp_Explorer _MappedEdges(_Mapped, TopAbs_EDGE); _MappedEdges.More(); _MappedEdges.Next())
                        {
                            const auto _MappedEdge = TopoDS::Edge(_MappedEdges.Current());
                            bool _Matches = true;
                            for (double _Fraction : {0.2, 0.5, 0.8})
                                if (!PointOnCurve(MappedPoint(_Curve.Value(_Curve.FirstParameter()
                                    + (_Curve.LastParameter() - _Curve.FirstParameter()) * _Fraction), *_Surface), _MappedEdge))
                                { _Matches = false; break; }
                            if (_Matches) { _Result.push_back(_MappedEdge); break; }
                        }
                    }
                }
            }
        }
        return _Result;
    }

    std::vector<STubeWrappedTopologyBoundary> BindTubeWrappedBevel(
        const STubeWrappedHostGeometry& Host_, const TopoDS_Shape& UVFaces_,
        const std::vector<TopoDS_Edge>& BoundaryEdges_,
        const TopoDS_Shape& FinishedShape_, const TopoDS_Shape& Cutter_)
    {
        if (FinishedShape_.IsNull() || Cutter_.IsNull())
            throw std::invalid_argument("Wrapped bevel binding requires finished and cutter geometry");
        const auto _Boundaries = MapTubeWrappedBoundaries(Host_, UVFaces_, BoundaryEdges_);
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> _Edges, _Faces;
        TopExp::MapShapes(FinishedShape_, TopAbs_EDGE, _Edges);
        TopExp::MapShapes(FinishedShape_, TopAbs_FACE, _Faces);
        const auto _FaceBoundary = [](const TopoDS_Shape& Shape_) {
            TopoDS_Compound _Result; BRep_Builder _Builder; _Builder.MakeCompound(_Result);
            for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
                _Builder.Add(_Result, _Explorer.Current());
            return _Result;
        };
        const auto _HostSkin = _FaceBoundary(Host_.Shape), _ToolSkin = _FaceBoundary(Cutter_);
        const auto _Distance = [](const gp_Pnt& Point_, const TopoDS_Shape& Boundary_) {
            BRepExtrema_DistShapeShape _Distance(BRepBuilderAPI_MakeVertex(Point_).Vertex(), Boundary_);
            if (!_Distance.IsDone()) throw std::runtime_error("Exact cut-wall distance query failed");
            return _Distance.Value();
        };
        const auto _IsCutWall = [&](const TopoDS_Face& Face_) {
            double _U0, _U1, _V0, _V1; BRepTools::UVBounds(Face_, _U0, _U1, _V0, _V1);
            const auto _Surface = BRep_Tool::Surface(Face_);
            for (double _U : {0.5, 0.25, 0.75}) for (double _V : {0.5, 0.25, 0.75})
            {
                const gp_Pnt2d _UV(_U0 + (_U1 - _U0) * _U, _V0 + (_V1 - _V0) * _V);
                BRepClass_FaceClassifier _Classifier(Face_, _UV, kCurveTolerance);
                if (_Classifier.State() != TopAbs_IN) continue;
                const auto _Point = _Surface->Value(_UV.X(), _UV.Y());
                if (_Distance(_Point, _ToolSkin) <= 1.0e-6 && _Distance(_Point, _HostSkin) > 1.0e-6)
                    return true;
            }
            return false;
        };
        std::vector<STubeWrappedTopologyBoundary> _Result;
        for (int _Index = 1; _Index <= _Edges.Extent(); ++_Index)
        {
            const auto _Edge = TopoDS::Edge(_Edges(_Index));
            if (BRep_Tool::Degenerated(_Edge)) continue;
            BRepAdaptor_Curve _Curve(_Edge);
            bool _Selected = !_Boundaries.empty();
            for (double _Fraction : {0.0, 0.2, 0.5, 0.8, 1.0})
            {
                const auto _Point = _Curve.Value(_Curve.FirstParameter()
                    + (_Curve.LastParameter() - _Curve.FirstParameter()) * _Fraction);
                if (std::none_of(_Boundaries.begin(), _Boundaries.end(), [&](const auto& Boundary_) {
                    return PointOnCurve(_Point, Boundary_);
                })) { _Selected = false; break; }
            }
            if (!_Selected) continue;
            std::uint64_t _CutFaceID = 0;
            for (int _FaceIndex = 1; _FaceIndex <= _Faces.Extent(); ++_FaceIndex)
            {
                const auto _Face = TopoDS::Face(_Faces(_FaceIndex));
                bool _Adjacent = false;
                for (TopExp_Explorer _FaceEdges(_Face, TopAbs_EDGE); _FaceEdges.More(); _FaceEdges.Next())
                    if (_FaceEdges.Current().IsSame(_Edge)) { _Adjacent = true; break; }
                if (!_Adjacent || !_IsCutWall(_Face)) continue;
                if (_CutFaceID && _CutFaceID != static_cast<std::uint64_t>(_FaceIndex))
                    throw std::runtime_error("Wrapped bevel edge has ambiguous exposed cut walls");
                _CutFaceID = static_cast<std::uint64_t>(_FaceIndex);
            }
            if (_CutFaceID) _Result.push_back({static_cast<std::uint64_t>(_Index), _CutFaceID});
        }
        if (_Result.empty()) throw std::runtime_error("Wrapped bevel boundary has no actual exposed opening edge");
        return _Result;
    }
}
